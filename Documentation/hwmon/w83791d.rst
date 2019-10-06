Kernel driver w83791d
=====================

Supported chips:

  * Winbond W83791D

    Prefix: 'w83791d'

    Addresses scanned: I2C 0x2c - 0x2f

    Datasheet: http://www.winbond-usa.com/products/winbond_products/pdfs/PCIC/W83791D_W83791Gb.pdf

Author: Charles Spirakis <bezaur@gmail.com>

This driver was derived from the w83781d.c and w83792d.c source files.

Credits:

  w83781d.c:

    - Frodo Looijaard <frodol@dds.nl>,
    - Philip Edelbrock <phil@netroedge.com>,
    - Mark Studebaker <mdsxyz123@yahoo.com>

  w83792d.c:

    - Shane Huang (Winbond),
    - Rudolf Marek <r.marek@assembler.cz>

Additional contributors:

    - Sven Anders <anders@anduras.de>
    - Marc Hulsman <m.hulsman@tudelft.nl>

Module Parameters
-----------------

* init boolean
    (default 0)

    Use 'init=1' to have the driver do extra software initializations.
    The default behavior is to do the minimum initialization possible
    and depend on the BIOS to properly setup the chip. If you know you
    have a w83791d and you're having problems, try init=1 before trying
    reset=1.

* reset boolean
    (default 0)

    Use 'reset=1' to reset the chip (via index 0x40, bit 7). The default
    behavior is no chip reset to preserve BIOS settings.

* force_subclients=bus,caddr,saddr,saddr
    This is used to force the i2c addresses for subclients of
    a certain chip. Example usage is `force_subclients=0,0x2f,0x4a,0x4b`
    to force the subclients of chip 0x2f on bus 0 to i2c addresses
    0x4a and 0x4b.


Description
-----------

This driver implements support for the Winbond W83791D chip. The W83791G
chip appears to be the same as the W83791D but is lead free.

Detection of the chip can sometimes be foiled because it can be in an
internal state that allows no clean access (Bank with ID register is not
currently selected). If you know the address of the chip, use a 'force'
parameter; this will put it into a more well-behaved state first.

The driver implements three temperature sensors, ten voltage sensors,
five fan rotation speed sensors and manual PWM control of each fan.

Temperatures are measured in degrees Celsius and measurement resolution is 1
degC for temp1 and 0.5 degC for temp2 and temp3. An alarm is triggered when
the temperature gets higher than the Overtemperature Shutdown value; it stays
on until the temperature falls below the Hysteresis value.

Voltage sensors (also known as IN sensors) report their values in millivolts.
An alarm is triggered if the voltage has crossed a programmable minimum
or maximum limit.

Fan rotation speeds are reported in RPM (rotations per minute). An alarm is
triggered if the rotation speed has dropped below a programmable limit. Fan
readings can be divided by a programmable divider (1, 2, 4, 8, 16,
32, 64 or 128 for all fans) to give the readings more range or accuracy.

Each fan controlled is controlled by PWM. The PWM duty cycle can be read and
set for each fan separately. Valid values range from 0 (stop) to 255 (full).
PWM 1-3 support Thermal Cruise mode, in which the PWMs are automatically
regulated to keep respectively temp 1-3 at a certain target temperature.
See below for the description of the sysfs-interface.

The w83791d has a global bit used to enable beeping from the speaker when an
alarm is triggered as well as a bitmask to enable or disable the beep for
specific alarms. You need both the global beep enable bit and the
corresponding beep bit to be on for a triggered alarm to sound a beep.

The sysfs interface to the global enable is via the sysfs beep_enable file.
This file is used for both legacy and new code.

The sysfs interface to the beep bitmask has migrated from the original legacy
method of a single sysfs beep_mask file to a newer method using multiple
`*_beep` files as described in `Documentation/hwmon/sysfs-interface.rst`.

A similar change has occurred for the bitmap corresponding to the alarms. The
original legacy method used a single sysfs alarms file containing a bitmap
of triggered alarms. The newer method uses multiple sysfs `*_alarm` files
(again following the pattern described in sysfs-interface).

Since both methods read and write the underlying hardware, they can be used
interchangeably and changes in one will automatically be reflected by
the other. If you use the legacy bitmask method, your user-space code is
responsible for handling the fact that the alarms and beep_mask bitmaps
are not the same (see the table below).

NOTE: All new code should be written to use the newer sysfs-interface
specification as that avoids bitmap problems and is the preferred interface
going forward.

The driver reads the hardware chip values at most once every three seconds.
User mode code requesting values more often will receive cached values.

/sys files
----------
The sysfs-interface is documented in the 'sysfs-interface' file. Only
chip-specific options are documented here.

======================= =======================================================
pwm[1-3]_enable		this file controls mode of fan/temperature control for
			fan 1-3. Fan/PWM 4-5 only support manual mode.

			    * 1 Manual mode
			    * 2 Thermal Cruise mode
			    * 3 Fan Speed Cruise mode (no further support)

temp[1-3]_target	defines the target temperature for Thermal Cruise mode.
			Unit: millidegree Celsius
			RW

temp[1-3]_tolerance	temperature tolerance for Thermal Cruise mode.
			Specifies an interval around the target temperature
			in which the fan speed is not changed.
			Unit: millidegree Celsius
			RW
======================= =======================================================

Alarms bitmap vs. beep_mask bitmask
-----------------------------------

For legacy code using the alarms and beep_mask files:

=============  ========  ========= ==========================
Signal         Alarms    beep_mask Obs
=============  ========  ========= ==========================
in0 (VCORE)    0x000001  0x000001
in1 (VINR0)    0x000002  0x002000  <== mismatch
in2 (+3.3VIN)  0x000004  0x000004
in3 (5VDD)     0x000008  0x000008
in4 (+12VIN)   0x000100  0x000100
in5 (-12VIN)   0x000200  0x000200
in6 (-5VIN)    0x000400  0x000400
in7 (VSB)      0x080000  0x010000  <== mismatch
in8 (VBAT)     0x100000  0x020000  <== mismatch
in9 (VINR1)    0x004000  0x004000
temp1          0x000010  0x000010
temp2          0x000020  0x000020
temp3          0x002000  0x000002  <== mismatch
fan1           0x000040  0x000040
fan2           0x000080  0x000080
fan3           0x000800  0x000800
fan4           0x200000  0x200000
fan5           0x400000  0x400000
tart1          0x010000  0x040000  <== mismatch
tart2          0x020000  0x080000  <== mismatch
tart3          0x040000  0x100000  <== mismatch
case_open      0x001000  0x001000
global_enable  -         0x800000  (modified via beep_enable)
=============  ========  ========= ==========================
