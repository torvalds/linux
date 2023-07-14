Kernel driver acbel-fsg032
==========================

Supported chips:

  * ACBEL FSG032-00xG power supply.

Author: Lakshmi Yadlapati <lakshmiy@us.ibm.com>

Description
-----------

This driver supports ACBEL FSG032-00xG Power Supply. This driver
is a client to the core PMBus driver.

Usage Notes
-----------

This driver does not auto-detect devices. You will have to instantiate the
devices explicitly. Please see Documentation/i2c/instantiating-devices.rst for
details.

Sysfs entries
-------------

The following attributes are supported:

======================= ======================================================
curr1_crit              Critical maximum current.
curr1_crit_alarm        Input current critical alarm.
curr1_input             Measured output current.
curr1_label             "iin"
curr1_max               Maximum input current.
curr1_max_alarm         Maximum input current high alarm.
curr1_rated_max         Maximum rated input current.
curr2_crit              Critical maximum current.
curr2_crit_alarm        Output current critical alarm.
curr2_input             Measured output current.
curr2_label             "iout1"
curr2_max               Maximum output current.
curr2_max_alarm         Output current high alarm.
curr2_rated_max         Maximum rated output current.


fan1_alarm              Fan 1 warning.
fan1_fault	        Fan 1 fault.
fan1_input	        Fan 1 speed in RPM.
fan1_target             Set fan speed reference.

in1_alarm               Input voltage under-voltage alarm.
in1_input               Measured input voltage.
in1_label               "vin"
in1_rated_max           Maximum rated input voltage.
in1_rated_min           Minimum rated input voltage.
in2_crit                Critical maximum output voltage.
in2_crit_alarm          Output voltage critical high alarm.
in2_input               Measured output voltage.
in2_label               "vout1"
in2_lcrit               Critical minimum output voltage.
in2_lcrit_alarm         Output voltage critical low alarm.
in2_rated_max           Maximum rated output voltage.
in2_rated_min           Minimum rated output voltage.

power1_alarm            Input fault or alarm.
power1_input            Measured input power.
power1_label            "pin"
power1_max              Input power limit.
power1_rated_max        Maximum rated input power.
power2_crit             Critical output power limit.
power2_crit_alarm       Output power crit alarm limit exceeded.
power2_input            Measured output power.
power2_label            "pout"
power2_max              Output power limit.
power2_max_alarm        Output power high alarm.
power2_rated_max        Maximum rated output power.

temp[1-3]_input         Measured temperature.
temp[1-2]_max           Maximum temperature.
temp[1-3]_rated_max     Temperature high alarm.
======================= ======================================================
