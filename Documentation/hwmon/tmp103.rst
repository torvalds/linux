Kernel driver tmp103
====================

Supported chips:

  * Texas Instruments TMP103

    Prefix: 'tmp103'

    Addresses scanned: none

    Product info and datasheet: https://www.ti.com/product/tmp103

Author:

	Heiko Schocher <hs@denx.de>

Description
-----------

The TMP103 is a digital output temperature sensor in a four-ball
wafer chip-scale package (WCSP). The TMP103 is capable of reading
temperatures to a resolution of 1°C. The TMP103 is specified for
operation over a temperature range of -40°C to +125°C.

Resolution: 8 Bits
Accuracy: ±1°C Typ (-10°C to +100°C)

The driver provides the common sysfs-interface for temperatures (see
Documentation/hwmon/sysfs-interface.rst under Temperatures).

Please refer how to instantiate this driver:
Documentation/i2c/instantiating-devices.rst
