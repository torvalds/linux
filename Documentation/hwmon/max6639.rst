Kernel driver max6639
=====================

Supported chips:

  * Maxim MAX6639

    Prefix: 'max6639'

    Addresses scanned: I2C 0x2c, 0x2e, 0x2f

    Datasheet: https://datasheets.maximintegrated.com/en/ds/MAX6639-MAX6639F.pdf

Authors:
    - He Changqing <hechangqing@semptian.com>
    - Roland Stigge <stigge@antcom.de>

Description
-----------

This driver implements support for the Maxim MAX6639. This chip is a 2-channel
temperature monitor with dual PWM fan speed controller. It can monitor its own
temperature and one external diode-connected transistor or two external
diode-connected transistors.

The following device attributes are implemented via sysfs:

====================== ==== ===================================================
Attribute              R/W  Contents
====================== ==== ===================================================
temp1_input            R    Temperature channel 1 input (0..150 C)
temp2_input            R    Temperature channel 2 input (0..150 C)
temp1_fault            R    Temperature channel 1 diode fault
temp2_fault            R    Temperature channel 2 diode fault
temp1_max              RW   Set THERM temperature for input 1
			    (in C, see datasheet)
temp2_max              RW   Set THERM temperature for input 2
temp1_crit             RW   Set ALERT temperature for input 1
temp2_crit             RW   Set ALERT temperature for input 2
temp1_emergency        RW   Set OT temperature for input 1
			    (in C, see datasheet)
temp2_emergency        RW   Set OT temperature for input 2
pwm1                   RW   Fan 1 target duty cycle (0..255)
pwm2                   RW   Fan 2 target duty cycle (0..255)
fan1_input             R    TACH1 fan tachometer input (in RPM)
fan2_input             R    TACH2 fan tachometer input (in RPM)
fan1_fault             R    Fan 1 fault
fan2_fault             R    Fan 2 fault
temp1_max_alarm        R    Alarm on THERM temperature on channel 1
temp2_max_alarm        R    Alarm on THERM temperature on channel 2
temp1_crit_alarm       R    Alarm on ALERT temperature on channel 1
temp2_crit_alarm       R    Alarm on ALERT temperature on channel 2
temp1_emergency_alarm  R    Alarm on OT temperature on channel 1
temp2_emergency_alarm  R    Alarm on OT temperature on channel 2
====================== ==== ===================================================
