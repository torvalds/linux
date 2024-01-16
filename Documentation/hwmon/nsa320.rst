Kernel driver nsa320_hwmon
==========================

Supported chips:

  * Holtek HT46R065 microcontroller with onboard firmware that configures

	it to act as a hardware monitor.

    Prefix: 'nsa320'

    Addresses scanned: none

    Datasheet: Not available, driver was reverse engineered based upon the

	Zyxel kernel source



Author:

  Adam Baker <linux@baker-net.org.uk>

Description
-----------

This chip is known to be used in the Zyxel NSA320 and NSA325 NAS Units and
also in some variants of the NSA310 but the driver has only been tested
on the NSA320. In all of these devices it is connected to the same 3 GPIO
lines which are used to provide chip select, clock and data lines. The
interface behaves similarly to SPI but at much lower speeds than are normally
used for SPI.

Following each chip select pulse the chip will generate a single 32 bit word
that contains 0x55 as a marker to indicate that data is being read correctly,
followed by an 8 bit fan speed in 100s of RPM and a 16 bit temperature in
tenths of a degree.


sysfs-Interface
---------------

============= =================
temp1_input   temperature input
fan1_input    fan speed
============= =================

Notes
-----

The access timings used in the driver are the same as used in the Zyxel
provided kernel. Testing has shown that if the delay between chip select and
the first clock pulse is reduced from 100 ms to just under 10ms then the chip
will not produce any output. If the duration of either phase of the clock
is reduced from 100 us to less than 15 us then data pulses are likely to be
read twice corrupting the output. The above analysis is based upon a sample
of one unit but suggests that the Zyxel provided delay values include a
reasonable tolerance.

The driver incorporates a limit that it will not check for updated values
faster than once a second. This is because the hardware takes a relatively long
time to read the data from the device and when it does it reads both temp and
fan speed. As the most likely case for two accesses in quick succession is
to read both of these values avoiding a second read delay is desirable.
