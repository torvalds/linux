Kernel driver via686a
=====================

Supported chips:

  * Via VT82C686A, VT82C686B  Southbridge Integrated Hardware Monitor

    Prefix: 'via686a'

    Addresses scanned: ISA in PCI-space encoded address

    Datasheet: On request through web form (http://www.via.com.tw/en/resources/download-center/)

Authors:
	- Kyösti Mälkki <kmalkki@cc.hut.fi>,
	- Mark D. Studebaker <mdsxyz123@yahoo.com>
	- Bob Dougherty <bobd@stanford.edu>
	- (Some conversion-factor data were contributed by
	- Jonathan Teh Soon Yew <j.teh@iname.com>
	- and Alex van Kaam <darkside@chello.nl>.)

Module Parameters
-----------------

======================= =======================================================
force_addr=0xaddr       Set the I/O base address. Useful for boards that
			don't set the address in the BIOS. Look for a BIOS
			upgrade before resorting to this. Does not do a
			PCI force; the via686a must still be present in lspci.
			Don't use this unless the driver complains that the
			base address is not set.
			Example: 'modprobe via686a force_addr=0x6000'
======================= =======================================================

Description
-----------

The driver does not distinguish between the chips and reports
all as a 686A.

The Via 686a southbridge has integrated hardware monitor functionality.
It also has an I2C bus, but this driver only supports the hardware monitor.
For the I2C bus driver, see <file:Documentation/i2c/busses/i2c-viapro>

The Via 686a implements three temperature sensors, two fan rotation speed
sensors, five voltage sensors and alarms.

Temperatures are measured in degrees Celsius. An alarm is triggered once
when the Overtemperature Shutdown limit is crossed; it is triggered again
as soon as it drops below the hysteresis value.

Fan rotation speeds are reported in RPM (rotations per minute). An alarm is
triggered if the rotation speed has dropped below a programmable limit. Fan
readings can be divided by a programmable divider (1, 2, 4 or 8) to give
the readings more range or accuracy. Not all RPM values can accurately be
represented, so some rounding is done. With a divider of 2, the lowest
representable value is around 2600 RPM.

Voltage sensors (also known as IN sensors) report their values in volts.
An alarm is triggered if the voltage has crossed a programmable minimum
or maximum limit. Voltages are internally scalled, so each voltage channel
has a different resolution and range.

If an alarm triggers, it will remain triggered until the hardware register
is read at least once. This means that the cause for the alarm may
already have disappeared! Note that in the current implementation, all
hardware registers are read whenever any data is read (unless it is less
than 1.5 seconds since the last update). This means that you can easily
miss once-only alarms.

The driver only updates its values each 1.5 seconds; reading it more often
will do no harm, but will return 'old' values.

Known Issues
------------

This driver handles sensors integrated in some VIA south bridges. It is
possible that a motherboard maker used a VT82C686A/B chip as part of a
product design but was not interested in its hardware monitoring features,
in which case the sensor inputs will not be wired. This is the case of
the Asus K7V, A7V and A7V133 motherboards, to name only a few of them.
So, if you need the force_addr parameter, and end up with values which
don't seem to make any sense, don't look any further: your chip is simply
not wired for hardware monitoring.
