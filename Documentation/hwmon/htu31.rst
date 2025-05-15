.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver HTU31
====================

Supported chips:

  * Measurement Specialties HTU31

    Prefix: 'htu31'

    Addresses scanned: -

    Datasheet: Publicly available from https://www.te.com/en/product-CAT-HSC0007.html

Author:

  - Andrei Lalaev <andrey.lalaev@gmail.com>

Description
-----------

HTU31 is a humidity and temperature sensor.

Supported temperature range is from -40 to 125 degrees Celsius.

Communication with the device is performed via I2C protocol. Sensor's default address
is 0x40.

sysfs-Interface
---------------

=================== =================
temp1_input:        temperature input
humidity1_input:    humidity input
heater_enable:      heater control
=================== =================
