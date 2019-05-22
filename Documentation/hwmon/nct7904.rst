Kernel driver nct7904
=====================

Supported chip:

  * Nuvoton NCT7904D

    Prefix: nct7904

    Addresses: I2C 0x2d, 0x2e

    Datasheet: Publicly available at Nuvoton website

	http://www.nuvoton.com/

Author: Vadim V. Vlasov <vvlasov@dev.rtsoft.ru>


Description
-----------

The NCT7904D is a hardware monitor supporting up to 20 voltage sensors,
internal temperature sensor, Intel PECI and AMD SB-TSI CPU temperature
interface, up to 12 fan tachometer inputs, up to 4 fan control channels
with SmartFan.


Sysfs entries
-------------

Currently, the driver supports only the following features:

======================= =======================================================
in[1-20]_input		Input voltage measurements (mV)

fan[1-12]_input		Fan tachometer measurements (rpm)

temp1_input		Local temperature (1/1000 degree,
			0.125 degree resolution)

temp[2-9]_input		CPU temperatures (1/1000 degree,
			0.125 degree resolution)

pwm[1-4]_enable		R/W, 1/2 for manual or SmartFan mode
			Setting SmartFan mode is supported only if it has been
			previously configured by BIOS (or configuration EEPROM)

pwm[1-4]		R/O in SmartFan mode, R/W in manual control mode
======================= =======================================================

The driver checks sensor control registers and does not export the sensors
that are not enabled. Anyway, a sensor that is enabled may actually be not
connected and thus provide zero readings.


Limitations
-----------

The following features are not supported in current version:

 - SmartFan control
 - Watchdog
 - GPIO
 - external temperature sensors
 - SMI
 - min/max values
 - many other...
