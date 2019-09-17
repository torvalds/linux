========================
Kernel driver w1_ds28e04
========================

Supported chips:

  * Maxim DS28E04-100 4096-Bit Addressable 1-Wire EEPROM with PIO

supported family codes:

        =================	====
	W1_FAMILY_DS28E04	0x1C
        =================	====

Author: Markus Franke, <franke.m@sebakmt.com> <franm@hrz.tu-chemnitz.de>

Description
-----------

Support is provided through the sysfs files "eeprom" and "pio". CRC checking
during memory accesses can optionally be enabled/disabled via the device
attribute "crccheck". The strong pull-up can optionally be enabled/disabled
via the module parameter "w1_strong_pullup".

Memory Access

	A read operation on the "eeprom" file reads the given amount of bytes
	from the EEPROM of the DS28E04.

	A write operation on the "eeprom" file writes the given byte sequence
	to the EEPROM of the DS28E04. If CRC checking mode is enabled only
	fully aligned blocks of 32 bytes with valid CRC16 values (in bytes 30
	and 31) are allowed to be written.

PIO Access

	The 2 PIOs of the DS28E04-100 are accessible via the "pio" sysfs file.

	The current status of the PIO's is returned as an 8 bit value. Bit 0/1
	represent the state of PIO_0/PIO_1. Bits 2..7 do not care. The PIO's are
	driven low-active, i.e. the driver delivers/expects low-active values.
