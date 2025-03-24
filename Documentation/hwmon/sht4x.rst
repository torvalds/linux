.. SPDX-License-Identifier: GPL-2.0

Kernel driver sht4x
===================

Supported Chips:

  * Sensirion SHT4X

    Prefix: 'sht4x'

    Addresses scanned: None

    Datasheet:

      English: https://www.sensirion.com/fileadmin/user_upload/customers/sensirion/Dokumente/2_Humidity_Sensors/Datasheets/Sensirion_Humidity_Sensors_SHT4x_Datasheet.pdf

Author: Navin Sankar Velliangiri <navin@linumiz.com>


Description
-----------

This driver implements support for the Sensirion SHT4x chip, a humidity
and temperature sensor. Temperature is measured in degree celsius, relative
humidity is expressed as a percentage. In sysfs interface, all values are
scaled by 1000, i.e. the value for 31.5 degrees celsius is 31500.

Usage Notes
-----------

The device communicates with the I2C protocol. Sensors can have the I2C
address 0x44. See Documentation/i2c/instantiating-devices.rst for methods
to instantiate the device.

Sysfs entries
-------------

=============== ============================================
temp1_input     Measured temperature in millidegrees Celsius
humidity1_input Measured humidity in %H
update_interval The minimum interval for polling the sensor,
                in milliseconds. Writable. Must be at least
                2000.
heater_power	The requested heater power, in milliwatts.
		Available values: 20, 110, 200 (default: 200).
heater_time	The requested operating time of the heater,
		in milliseconds.
		Available values: 100, 1000 (default 1000).
heater_enable	Enable the heater with the selected power
		and for the selected time in order to remove
		condensed water from the sensor surface. The
		heater cannot be manually turned off once
		enabled (it will automatically turn off
		after completing its operation).

			- 0: turned off (read-only value)
			- 1: turn on
=============== ============================================
