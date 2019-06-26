Kernel driver w83l785ts
=======================

Supported chips:

  * Winbond W83L785TS-S

    Prefix: 'w83l785ts'

    Addresses scanned: I2C 0x2e

    Datasheet: Publicly available at the Winbond USA website

	       http://www.winbond-usa.com/products/winbond_products/pdfs/PCIC/W83L785TS-S.pdf

Authors:
	Jean Delvare <jdelvare@suse.de>

Description
-----------

The W83L785TS-S is a digital temperature sensor. It senses the
temperature of a single external diode. The high limit is
theoretically defined as 85 or 100 degrees C through a combination
of external resistors, so the user cannot change it. Values seen so
far suggest that the two possible limits are actually 95 and 110
degrees C. The datasheet is rather poor and obviously inaccurate
on several points including this one.

All temperature values are given in degrees Celsius. Resolution
is 1.0 degree. See the datasheet for details.

The w83l785ts driver will not update its values more frequently than
every other second; reading them more often will do no harm, but will
return 'old' values.

Known Issues
------------

On some systems (Asus), the BIOS is known to interfere with the driver
and cause read errors. Or maybe the W83L785TS-S chip is simply unreliable,
we don't really know. The driver will retry a given number of times
(5 by default) and then give up, returning the old value (or 0 if
there is no old value). It seems to work well enough so that you should
not notice anything. Thanks to James Bolt for helping test this feature.
