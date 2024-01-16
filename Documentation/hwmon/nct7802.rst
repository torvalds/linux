Kernel driver nct7802
=====================

Supported chips:

  * Nuvoton NCT7802Y

    Prefix: 'nct7802'

    Addresses scanned: I2C 0x28..0x2f

    Datasheet: Available from Nuvoton web site

Authors:

	Guenter Roeck <linux@roeck-us.net>

Description
-----------

This driver implements support for the Nuvoton NCT7802Y hardware monitoring
chip. NCT7802Y supports 6 temperature sensors, 5 voltage sensors, and 3 fan
speed sensors.

Smart Fan™ speed control is available via pwmX_auto_point attributes.

Tested Boards and BIOS Versions
-------------------------------

The driver has been reported to work with the following boards and
BIOS versions.

======================= ===============================================
Board			BIOS version
======================= ===============================================
Kontron COMe-bSC2	CHR2E934.001.GGO
Kontron COMe-bIP2	CCR2E212
======================= ===============================================
