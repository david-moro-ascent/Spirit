#include "Copter.h"


/*
 * Init and run calls for althold, flight mode
 */

// althold_init - initialise althold controller
bool ModeAltHold::init(bool ignore_checks)
{
    // initialise position and desired velocity
    if (!pos_control->is_active_z()) {
        pos_control->set_alt_target_to_current_alt();
        pos_control->set_desired_velocity_z(inertial_nav.get_velocity_z());
    }

    last_roll = 0.0;
    last_pitch = 0.0;
	last_target_climb_rate = 0.0;
	last_target_yaw_rate = 0.0;

    return true;
}

// althold_run - runs the althold controller
// should be called at 100hz or more
void ModeAltHold::run()
{
    float takeoff_climb_rate = 0.0f;

    // initialize vertical speeds and acceleration
    pos_control->set_max_speed_z(-get_pilot_speed_dn(), g.pilot_speed_up);
    pos_control->set_max_accel_z(g.pilot_accel_z);

    // apply SIMPLE mode transform to pilot inputs
    update_simple_mode();

    // get pilot desired lean angles
    //float target_roll, target_pitch;
    //get_pilot_desired_lean_angles(target_roll, target_pitch, copter.aparm.angle_max, attitude_control->get_althold_lean_angle_max());


    /////////////////////////////////////////

    float target_roll, target_pitch, target_climb_rate, target_yaw_rate;
    int16_t  gimbal_pan, gimbal_tilt, gimbal_zoom, gimbal_focus;

    if(copter.ap.gimbal_control_active){

    	target_roll = last_roll;
    	target_pitch = last_pitch;
    	target_climb_rate = last_target_climb_rate;
    	target_yaw_rate = 0;

    	gimbal_tilt = RC_Channels::rc_channel(CH_2)->get_radio_in();
    	gimbal_pan = RC_Channels::rc_channel(CH_1)->get_radio_in();
    	gimbal_zoom = RC_Channels::rc_channel(CH_3)->get_radio_in();
    	gimbal_focus = RC_Channels::rc_channel(CH_4)->get_radio_in();

    	SRV_Channels::set_output_pwm(SRV_Channel::k_gimbal_tilt, gimbal_tilt);
    	SRV_Channels::set_output_pwm(SRV_Channel::k_gimbal_pan, gimbal_pan);
    	SRV_Channels::set_output_pwm(SRV_Channel::k_gimbal_zoom, gimbal_zoom);
    	SRV_Channels::set_output_pwm(SRV_Channel::k_gimbal_focus, gimbal_focus);

    }else{

		get_pilot_desired_lean_angles(target_roll, target_pitch, copter.aparm.angle_max, attitude_control->get_althold_lean_angle_max());

	 // get pilot's desired yaw rate
		target_yaw_rate = get_pilot_desired_yaw_rate(channel_yaw->get_control_in());

	 // get pilot desired climb rate
	  	target_climb_rate = get_pilot_desired_climb_rate(channel_throttle->get_control_in());
	  	target_climb_rate = constrain_float(target_climb_rate, -get_pilot_speed_dn(), g.pilot_speed_up);

	  	last_roll = target_roll;
	  	last_pitch = target_pitch;
	  	last_target_climb_rate = target_climb_rate;

	  	SRV_Channels::set_output_pwm(SRV_Channel::k_gimbal_tilt, 1500);
	  	SRV_Channels::set_output_pwm(SRV_Channel::k_gimbal_pan, 1500);
	  	SRV_Channels::set_output_pwm(SRV_Channel::k_gimbal_zoom, 1500);
	  	SRV_Channels::set_output_pwm(SRV_Channel::k_gimbal_focus, 1500);
    }


    /////////////////////////////////////////



    // get pilot's desired yaw rate
  //  float target_yaw_rate = get_pilot_desired_yaw_rate(channel_yaw->get_control_in());

    // get pilot desired climb rate
  //  float target_climb_rate = get_pilot_desired_climb_rate(channel_throttle->get_control_in());
   // target_climb_rate = constrain_float(target_climb_rate, -get_pilot_speed_dn(), g.pilot_speed_up);

    // Alt Hold State Machine Determination
    AltHoldModeState althold_state = get_alt_hold_state(target_climb_rate);

    // Alt Hold State Machine
    switch (althold_state) {

    case AltHold_MotorStopped:
        attitude_control->reset_rate_controller_I_terms();
        attitude_control->set_yaw_target_to_current_heading();
        pos_control->relax_alt_hold_controllers(0.0f);   // forces throttle output to go to zero
        break;

    case AltHold_Landed_Ground_Idle:
        attitude_control->set_yaw_target_to_current_heading();
        // FALLTHROUGH

    case AltHold_Landed_Pre_Takeoff:
        attitude_control->reset_rate_controller_I_terms();
        pos_control->relax_alt_hold_controllers(0.0f);   // forces throttle output to go to zero
        break;

    case AltHold_Takeoff:
        // initiate take-off
        if (!takeoff.running()) {
            takeoff.start(constrain_float(g.pilot_takeoff_alt,0.0f,1000.0f));
        }

        // get take-off adjusted pilot and takeoff climb rates
        takeoff.get_climb_rates(target_climb_rate, takeoff_climb_rate);

        // get avoidance adjusted climb rate
        target_climb_rate = get_avoidance_adjusted_climbrate(target_climb_rate);

        // set position controller targets
        pos_control->set_alt_target_from_climb_rate_ff(target_climb_rate, G_Dt, false);
        pos_control->add_takeoff_climb_rate(takeoff_climb_rate, G_Dt);
        break;

    case AltHold_Flying:
        motors->set_desired_spool_state(AP_Motors::DesiredSpoolState::THROTTLE_UNLIMITED);

#if AC_AVOID_ENABLED == ENABLED
        // apply avoidance
        copter.avoid.adjust_roll_pitch(target_roll, target_pitch, copter.aparm.angle_max);
#endif

        // adjust climb rate using rangefinder
        target_climb_rate = copter.surface_tracking.adjust_climb_rate(target_climb_rate);

        // get avoidance adjusted climb rate
        target_climb_rate = get_avoidance_adjusted_climbrate(target_climb_rate);

        pos_control->set_alt_target_from_climb_rate_ff(target_climb_rate, G_Dt, false);
        break;
    }

    // call attitude controller
    attitude_control->input_euler_angle_roll_pitch_euler_rate_yaw(target_roll, target_pitch, target_yaw_rate);

    // call z-axis position controller
    pos_control->update_z_controller();

}
