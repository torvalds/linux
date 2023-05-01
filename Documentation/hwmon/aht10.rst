.. SPDX-License-Identifier: GPL-2.0

Kernel driver aht10
=====================

Supported chips:

  * Aosong AHT10

    Prefix: 'aht10'

    Addresses scanned: None

    Datasheet:

      Chinese: http://www.aosong.com/userfiles/files/media/AHT10%E4%BA%A7%E5%93%81%E6%89%8B%E5%86%8C%20A3%2020201210.pdf
      English: https://server4.eca.ir/eshop/AHT10/Aosong_AHT10_en_draft_0c.pdf

Author: Johannes Cornelis Draaijer <jcdra1@gmail.com>


Description
-----------

The AHT10 is a Temperature and Humidity sensor

The address of this i2c device may only be 0x38

Usage Notes
-----------

This driver does not probe for AHT10 devices, as there is no reliable
way to determine if an i2c chip is or isn't an AHT10. The device has
to be instantiated explicitly with the address 0x38. See
Documentation/i2c/instantiating-devices.rst for details.

Sysfs entries
-------------

=============== ============================================
temp1_input     Measured temperature in millidegrees Celsius
humidity1_input Measured humidity in %H
update_interval The minimum interval for polling the sensor,
                in milliseconds. Writable. Must be at
                least 2000.
=============== ============================================
