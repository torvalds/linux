===========================
Kernel driver spi-sc18is602
===========================

Supported chips:

  * NXP SI18IS602/602B/603

    Datasheet: http://www.nxp.com/documents/data_sheet/SC18IS602_602B_603.pdf

Author:
        Guenter Roeck <linux@roeck-us.net>


Description
-----------

This driver provides connects a NXP SC18IS602/603 I2C-bus to SPI bridge to the
kernel's SPI core subsystem.

The driver does not probe for supported chips, since the SI18IS602/603 does not
support Chip ID registers. You will have to instantiate the devices explicitly.
Please see Documentation/i2c/instantiating-devices.rst for details.


Usage Notes
-----------

This driver requires the I2C adapter driver to support raw I2C messages. I2C
adapter drivers which can only handle the SMBus protocol are not supported.

The maximum SPI message size supported by SC18IS602/603 is 200 bytes. Attempts
to initiate longer transfers will fail with -EINVAL. EEPROM read operations and
similar large accesses have to be split into multiple chunks of no more than
200 bytes per SPI message (128 bytes of data per message is recommended). This
means that programs such as "cp" or "od", which automatically use large block
sizes to access a device, can not be used directly to read data from EEPROM.
Programs such as dd, where the block size can be specified, should be used
instead.
