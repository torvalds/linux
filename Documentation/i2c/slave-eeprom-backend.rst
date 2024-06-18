==============================
Linux I2C slave EEPROM backend
==============================

by Wolfram Sang <wsa@sang-engineering.com> in 2014-20

This backend simulates an EEPROM on the connected I2C bus. Its memory contents
can be accessed from userspace via this file located in sysfs::

	/sys/bus/i2c/devices/<device-directory>/slave-eeprom

The following types are available: 24c02, 24c32, 24c64, and 24c512. Read-only
variants are also supported. The name needed for instantiating has the form
'slave-<type>[ro]'. Examples follow:

24c02, read/write, address 0x64:
  # echo slave-24c02 0x1064 > /sys/bus/i2c/devices/i2c-1/new_device

24c512, read-only, address 0x42:
  # echo slave-24c512ro 0x1042 > /sys/bus/i2c/devices/i2c-1/new_device

You can also preload data during boot if a device-property named
'firmware-name' contains a valid filename (DT or ACPI only).

As of 2015, Linux doesn't support poll on binary sysfs files, so there is no
notification when another master changed the content.
