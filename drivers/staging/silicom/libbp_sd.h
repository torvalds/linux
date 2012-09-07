/******************************************************************************/
/*                                                                            */
/* bypass library, Copyright (c) 2004 Silicom, Ltd                            */
/* Corporation.                                                               */
/*                                                                            */
/* This program is free software; you can redistribute it and/or modify       */
/* it under the terms of the GNU General Public License as published by       */
/* the Free Software Foundation, located in the file LICENSE.                 */
/*                                                                            */
/* Ver 1.0.0                                                                  */
/*                                                                            */
/* libbypass.h                                                                */
/*                                                                            */
/******************************************************************************/

#define BP_CAP                   0x01	//BIT_0
#define BP_STATUS_CAP            0x02	//BIT_1
#define BP_STATUS_CHANGE_CAP     0x04	//BIT_2
#define SW_CTL_CAP               0x08	//BIT_3
#define BP_DIS_CAP               0x10	//BIT_4
#define BP_DIS_STATUS_CAP        0x20	//BIT_5
#define STD_NIC_CAP              0x40	//BIT_6
#define BP_PWOFF_ON_CAP          0x80	//BIT_7
#define BP_PWOFF_OFF_CAP         0x0100	//BIT_8
#define BP_PWOFF_CTL_CAP         0x0200	//BIT_9
#define BP_PWUP_ON_CAP           0x0400	//BIT_10
#define BP_PWUP_OFF_CAP          0x0800	//BIT_11
#define BP_PWUP_CTL_CAP          0x1000	//BIT_12
#define WD_CTL_CAP               0x2000	//BIT_13
#define WD_STATUS_CAP            0x4000	//BIT_14
#define WD_TIMEOUT_CAP           0x8000	//BIT_15
#define TX_CTL_CAP               0x10000	//BIT_16
#define TX_STATUS_CAP            0x20000	//BIT_17
#define TAP_CAP                  0x40000	//BIT_18
#define TAP_STATUS_CAP           0x80000	//BIT_19
#define TAP_STATUS_CHANGE_CAP    0x100000	//BIT_20
#define TAP_DIS_CAP              0x200000	//BIT_21
#define TAP_DIS_STATUS_CAP       0x400000	//BIT_22
#define TAP_PWUP_ON_CAP          0x800000	//BIT_23
#define TAP_PWUP_OFF_CAP         0x1000000	//BIT 24
#define TAP_PWUP_CTL_CAP         0x2000000	//BIT 25
#define NIC_CAP_NEG              0x4000000	//BIT 26

#define WD_MIN_TIME_GET(desc)   (desc & 0xf)
#define WD_STEP_COUNT_GET(desc) (desc>>5) & 0xf
#define WDT_STEP_TIME           0x10

struct bp_info {
	char prod_name[14];
	unsigned char fw_ver;
};

/**
 * is_bypass - check if device is a Bypass controlling device
 * @if_index: network device index
 *
 * Output:
 *  1 -  if device is bypass controlling device, 
 *  0 -  if device is bypass slave device
 * -1 -  device not support Bypass
 **/
int is_bypass_sd(int if_index);

/**
 * get_bypass_slave - get second port participate in the Bypass pair
 * @if_index: network device index
 *
 * Output:
 *  network device index of the slave device
 * -1 - on failure (device not support Bypass or it's a slave device) 
 **/
int get_bypass_slave_sd(int if_index);

/**
 * get_bypass_caps - get second port participate in the Bypass pair
 * @if_index: network device index
 *
 * Output:
 * flags word on success;flag word is a 32-bit mask word with each bit defines different 
 * capability as described bellow.
 * Value of 1 for supporting this feature. 0 for not supporting this feature.
 * -1 - on failure (if the device is not capable of the operation or not a Bypass device)
 * Bit	feature	                description
 * 
 * 0	BP_CAP	                The interface is Bypass capable in general
 * 
 * 1	BP_STATUS_CAP	        The interface can report of the current Bypass mode
 * 
 * 2	BP_STATUS_CHANGE_CAP	The interface can report on a change to bypass mode from 
 *                              the last time the mode was defined
 * 
 * 3	SW_CTL_CAP	            The interface is Software controlled capable for bypass/non bypass modes.
 * 
 * 4	BP_DIS_CAP	            The interface is capable of disabling the Bypass mode at all times. 
 *                              This mode will retain its mode even during power loss and also after 
 *                              power recovery. This will overcome on any bypass operation due to 
 *                              watchdog timeout or set bypass command.
 * 
 * 5	BP_DIS_STATUS_CAP	    The interface can report of the current DIS_BP_CAP
 * 
 * 6	STD_NIC_CAP	            The interface is capable to be configured to operate as standard, non Bypass, 
 *                              NIC interface (have direct connection to interfaces at all power modes)
 * 
 * 7	BP_PWOFF_NO_CAP	        The interface can be in Bypass mode at power off state
 * 
 * 8	BP_PWOFF_OFF_CAP	    The interface can disconnect the Bypass mode at power off state without 
 *                              effecting all the other states of operation
 * 
 * 9	BP_PWOFF_CTL_CAP	    The behavior of the Bypass mode at Power-off state can be controlled by 
 *                              software without effecting any other state
 * 
 *10    BP_PWUP_ON_CAP	        The interface can be in Bypass mode when power is turned on 
 *                              (until the system take control of the bypass functionality)
 * 
 *11	BP_PWUP_OFF_CAP	        The interface can disconnect from Bypass mode when power is turned on 
 *                              (until the system take control of the bypass functionality)
 * 
 *12	BP_PWUP_CTL_CAP	        The behavior of the Bypass mode at Power-up can be controlled by software
 * 
 *13	WD_CTL_CAP	            The interface has watchdog capabilities to turn to Bypass mode when not reset 
 *                              for defined period of time.
 * 
 *14	WD_STATUS_CAP	        The interface can report on the watchdog status (Active/inactive)
 * 
 *15	WD_TIMEOUT_CAP	        The interface can report the time left till watchdog triggers to Bypass mode.
 * 
 *16-31 RESERVED	
 *
 * **/
int get_bypass_caps_sd(int if_index);

/**
 * get_wd_set_caps - Obtain watchdog timer setting capabilities
 * @if_index: network device index
 *
 * Output:
 * 
 * Set of numbers defining the various parameters of the watchdog capable 
 * to be set to as described bellow.
 * -1 - on failure (device not support Bypass or it's a slave device)
 * 
 * Bit	feature	        description
 * 
 * 0-3	WD_MIN_TIME	    The interface WD minimal time period  in 100mS units
 * 
 * 4	WD_STEP_TIME	The steps of the WD timer in 
 *                      0 - for linear steps (WD_MIN_TIME * X)
 *                      1 - for multiply by 2 from previous step (WD_MIN_TIME * 2^X)
 * 
 * 5-8	WD_STEP_COUNT	Number of steps the WD timer supports in 2^X 
 *                      (X bit available for defining the value)
 * 
 * 
 * 
 **/
int get_wd_set_caps_sd(int if_index);

/**
 * set_bypass - set Bypass state
 * @if_index: network device index of the controlling device
 * @bypass_mode:  bypass mode (1=on, 0=off) 
 * Output:
 *  0 - on success
 * -1 - on failure (device not support Bypass or it's a slave device) 
 **/
int set_bypass_sd(int if_index, int bypass_mode);

/**
 * get_bypass - Get Bypass mode state
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - (off/on) on success
 * -1 - on failure (device not support Bypass or it's a slave device) 
 **/
int get_bypass_sd(int if_index);

/**
 * get_bypass_change - Get change of Bypass mode state from last status check
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - (off/on) on success
 * -1 - on failure (device not support Bypass or it's a slave device) 
 **/
int get_bypass_change_sd(int if_index);

/**
 * set_dis_bypass - Set Disable Bypass mode
 * @if_index: network device index of the controlling device
 * @dis_bypass: disable bypass(1=dis, 0=en)
 * Output:
 *  0 - on success
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device) 
 **/
int set_dis_bypass_sd(int if_index, int dis_bypass);

/**
 * get_dis_bypass - Get Disable Bypass mode state
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - on success (normal Bypass mode/ Disable bypass)
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device) 
 **/
int get_dis_bypass_sd(int if_index);

/**
 * set_bypass_pwoff - Set Bypass mode at power-off state
 * @if_index: network device index of the controlling device
 * @bypass_mode: bypass mode setting at power off state (1=BP en, 0=BP Dis)
 * Output:
 *  0 - on success 
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device) 
 **/
int set_bypass_pwoff_sd(int if_index, int bypass_mode);

/**
 * get_bypass_pwoff - Get Bypass mode state at power-off state
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - on success (Disable bypass at power off state / normal Bypass mode)
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device) 
 **/
int get_bypass_pwoff_sd(int if_index);

/**
 * set_bypass_pwup - Set Bypass mode at power-up state
 * @if_index: network device index of the controlling device
 * @bypass_mode: bypass mode setting at power up state (1=BP en, 0=BP Dis)
 * Output:
 *  0 - on success 
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device) 
 **/
int set_bypass_pwup_sd(int if_index, int bypass_mode);

/**
 * get_bypass_pwup - Get Bypass mode state at power-up state
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - on success (Disable bypass at power up state / normal Bypass mode)
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device) 
 **/
int get_bypass_pwup_sd(int if_index);

/**
 * set_bypass_wd - Set watchdog state
 * @if_index: network device index of the controlling device
 * @ms_timeout: requested timeout (in ms units), 0 for disabling the watchdog timer
 * @ms_timeout_set(output): requested timeout (in ms units), 
 *                          that the adapter supports and will be used by the watchdog
 * Output:
 * 0  - on success 
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device) 
 **/
int set_bypass_wd_sd(int if_index, int ms_timeout, int *ms_timeout_set);

/**
 * get_bypass_wd - Get watchdog state
 * @if_index: network device index of the controlling device
 * @ms_timeout (output): WDT timeout (in ms units), 
 *                       -1 for unknown wdt status
 *                        0 if WDT is disabled
 * Output:
 * 0  - on success
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device) 
 **/
int get_bypass_wd_sd(int if_index, int *ms_timeout_set);

/**
 * get_wd_expire_time - Get watchdog expire
 * @if_index: network device index of the controlling device
 * @ms_time_left (output): time left till watchdog time expire, 
 *                       -1 if WDT has expired
 *                       0  if WDT is disabled
 * Output:
 * 0  - on success
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device or unknown wdt status) 
 **/
int get_wd_expire_time_sd(int if_index, int *ms_time_left);

/**
 * reset_bypass_wd_timer - Reset watchdog timer
 * @if_index: network device index of the controlling device
 * 
 * Output:
 * 1  - on success
 * 0 - watchdog is not configured
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device or unknown wdt status) 
 **/
int reset_bypass_wd_timer_sd(int if_index);

/**
 * set_std_nic - Standard NIC mode of operation
 * @if_index: network device index of the controlling device
 * @nic_mode: 0/1 (Default Bypass mode / Standard NIC mode)
 * 
 * Output:
 * 0  - on success
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device) 
 **/
int set_std_nic_sd(int if_index, int nic_mode);

/**
 * get_std_nic - Get Standard NIC mode setting
 * @if_index: network device index of the controlling device
 * 
 * Output:
 * 0/1 (Default Bypass mode / Standard NIC mode) on success
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device) 
 **/
int get_std_nic_sd(int if_index);

/**
 * set_tx - set transmitter enable/disable 
 * @if_index: network device index of the controlling device
 * @tx_state: 0/1 (Transmit Disable / Transmit Enable)
 * 
 * Output:
 * 0  - on success
 * -1 - on failure (device is not capable of the operation ) 
 **/
int set_tx_sd(int if_index, int tx_state);

/**
 * get_tx - get transmitter state (disable / enable)
 * @if_index: network device index of the controlling device
 * 
 * Output:
 * 0/1 (ransmit Disable / Transmit Enable) on success
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass) 
 **/
int get_tx_sd(int if_index);

/**
 * set_tpl - set TPL enable/disable 
 * @if_index: network device index of the controlling device
 * @tx_state: 0/1 (TPL Disable / TPL Enable)
 * 
 * Output:
 * 0  - on success
 * -1 - on failure (device is not capable of the operation ) 
 **/
int set_tpl_sd(int if_index, int tpl_state);

/**
 * get_tpl - get TPL state (disable / enable)
 * @if_index: network device index of the controlling device
 * 
 * Output:
 * 0/1 (TPL Disable / TPL Enable) on success
 * -1 - on failure (device is not capable of the operation) 
 **/
int get_tpl_sd(int if_index);

int get_bp_hw_reset_sd(int if_index);

int set_bp_hw_reset_sd(int if_index, int status);

/**
 * set_tap - set TAP state
 * @if_index: network device index of the controlling device
 * @tap_mode: 1 tap mode , 0 normal nic mode 
 * Output:
 *  0 - on success
 * -1 - on failure (device not support TAP or it's a slave device) 
 **/
int set_tap_sd(int if_index, int tap_mode);

/**
 * get_tap - Get TAP mode state
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - (off/on) on success
 * -1 - on failure (device not support TAP or it's a slave device) 
 **/
int get_tap_sd(int if_index);

/**
 * get_tap_change - Get change of TAP mode state from last status check
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - (off/on) on success
 * -1 - on failure (device not support TAP or it's a slave device) 
 **/
int get_tap_change_sd(int if_index);

/**
 * set_dis_tap - Set Disable TAP mode
 * @if_index: network device index of the controlling device
 * @dis_tap: disable tap(1=dis, 0=en)
 * Output:
 *  0 - on success
 * -1 - on failure (device is not capable of the operation ordevice not support TAP 
 *                  or it's a slave device) 
 **/
int set_dis_tap_sd(int if_index, int dis_tap);

/**
 * get_dis_tap - Get Disable TAP mode state
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - on success (normal TAP mode/ Disable TAP)
 * -1 - on failure (device is not capable of the operation ordevice not support TAP 
 *                  or it's a slave device) 
 **/
int get_dis_tap_sd(int if_index);

/**
 * set_tap_pwup - Set TAP mode at power-up state
 * @if_index: network device index of the controlling device
 * @bypass_mode: tap mode setting at power up state (1=TAP en, 0=TAP Dis)
 * Output:
 *  0 - on success 
 * -1 - on failure (device is not capable of the operation ordevice not support TAP 
 *                  or it's a slave device) 
 **/
int set_tap_pwup_sd(int if_index, int tap_mode);

/**
 * get_tap_pwup - Get TAP mode state at power-up state
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - on success (Disable TAP at power up state / normal TAP mode)
 * -1 - on failure (device is not capable of the operation ordevice not support TAP 
 *                  or it's a slave device) 
 **/
int get_tap_pwup_sd(int if_index);

/**
 * set_wd_exp_mode - Set adapter state when WDT expired.
 * @if_index: network device index of the controlling device
 * @bypass_mode:  adapter mode (1=tap mode, 0=bypass mode) 
 * Output:
 *  0 - on success
 * -1 - on failure (device not support Bypass or it's a slave device) 
 **/
int set_wd_exp_mode_sd(int if_index, int bypass_mode);

/**
 * get_wd_exp_mode - Get adapter state when WDT expired.
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - (bypass/tap) on success
 * -1 - on failure (device not support Bypass or it's a slave device) 
 **/
int get_wd_exp_mode_sd(int if_index);

/**
 * set_wd_autoreset - reset WDT periodically.
 * @if_index: network device index of the controlling device
 * @bypass_mode:  adapter mode (1=tap mode, 0=bypass mode) 
 * Output:
 * 1  - on success
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device or unknown wdt status) 
 **/
int set_wd_autoreset_sd(int if_index, int time);

/**
 * set_wd_autoreset - reset WDT periodically.
 * @if_index: network device index of the controlling device
 * @bypass_mode:  adapter mode (1=tap mode, 0=bypass mode) 
 * Output:
 * 1  - on success
 * -1 - on failure (device is not capable of the operation ordevice not support Bypass 
 *                  or it's a slave device or unknown wdt status) 
 **/
int get_wd_autoreset_sd(int if_index);
/**
 * set_disc - set DISC state
 * @if_index: network device index of the controlling device
 * @tap_mode: 1 DISC mode , 0 normal nic mode 
 * Output:
 *  0 - on success
 * -1 - on failure (device not support disconnect or it's a slave device) 
 **/
int set_bp_disc_sd(int if_index, int disc_mode);

/**
 * get_disc - Get disc mode state
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - (off/on) on success
 * -1 - on failure (device not support disconnect or it's a slave device) 
 **/
int get_bp_disc_sd(int if_index);

/**
 * get_disc_change - Get change of DISC mode state from last status check
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - (off/on) on success
 * -1 - on failure (device not support disconnect or it's a slave device) 
 **/
int get_bp_disc_change_sd(int if_index);

/**
 * set_dis_disc - Set Disable DISC mode
 * @if_index: network device index of the controlling device
 * @dis_disc: disable disconnect(1=dis, 0=en)
 * Output:
 *  0 - on success
 * -1 - on failure (device is not capable of the operation ordevice not support DISC 
 *                  or it's a slave device) 
 **/
int set_bp_dis_disc_sd(int if_index, int dis_disc);

/**
 * get_dis_disc - Get Disable DISC mode state
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - on success (normal DISC mode/ Disable DISC)
 * -1 - on failure (device is not capable of the operation ordevice not support TAP 
 *                  or it's a slave device) 
 **/
int get_bp_dis_disc_sd(int if_index);

/**
 * set_disc_pwup - Set DISC mode at power-up state
 * @if_index: network device index of the controlling device
 * @disc_mode: DISC mode setting at power up state (1= en, 0= Dis)
 * Output:
 *  0 - on success 
 * -1 - on failure (device is not capable of the operation ordevice not support DISC 
 *                  or it's a slave device) 
 **/
int set_bp_disc_pwup_sd(int if_index, int disc_mode);

/**
 * get_disc_pwup - Get DISC mode state at power-up state
 * @if_index: network device index of the controlling device
 * Output:
 *  0/1 - on success (Disable DISC at power up state / normal DISC mode)
 * -1 - on failure (device is not capable of the operation ordevice not support DISC 
 *                  or it's a slave device) 
 **/
int get_bp_disc_pwup_sd(int if_index);

int get_bypass_info_sd(int if_index, struct bp_info *bp_info);
int bp_if_scan_sd(void);
/*int get_dev_num_sd(void);*/
