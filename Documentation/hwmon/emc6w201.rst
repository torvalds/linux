Kernel driver emc6w201
======================

Supported chips:

  * SMSC EMC6W201

    Prefix: 'emc6w201'

    Addresses scanned: I2C 0x2c, 0x2d, 0x2e

    Datasheet: Not public

Author: Jean Delvare <jdelvare@suse.de>


Description
-----------

From the datasheet:

"The EMC6W201 is an environmental monitoring device with automatic fan
control capability and enhanced system acoustics for noise suppression.
This ACPI compliant device provides hardware monitoring for up to six
voltages (including its own VCC) and five external thermal sensors,
measures the speed of up to five fans, and controls the speed of
multiple DC fans using three Pulse Width Modulator (PWM) outputs. Note
that it is possible to control more than three fans by connecting two
fans to one PWM output. The EMC6W201 will be available in a 36-pin
QFN package."

The device is functionally close to the EMC6D100 series, but is
register-incompatible.

The driver currently only supports the monitoring of the voltages,
temperatures and fan speeds. Limits can be changed. Alarms are not
supported, and neither is fan speed control.


Known Systems With EMC6W201
---------------------------

The EMC6W201 is a rare device, only found on a few systems, made in
2005 and 2006. Known systems with this device:

* Dell Precision 670 workstation
* Gigabyte 2CEWH mainboard
