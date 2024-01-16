Kernel driver sht21
===================

Supported chips:

  * Sensirion SHT21

    Prefix: 'sht21'

    Addresses scanned: none

    Datasheet: Publicly available at the Sensirion website

    https://www.sensirion.com/file/datasheet_sht21



  * Sensirion SHT25

    Prefix: 'sht25'

    Addresses scanned: none

    Datasheet: Publicly available at the Sensirion website

    https://www.sensirion.com/file/datasheet_sht25



Author:

  Urs Fleisch <urs.fleisch@sensirion.com>

Description
-----------

The SHT21 and SHT25 are humidity and temperature sensors in a DFN package of
only 3 x 3 mm footprint and 1.1 mm height. The difference between the two
devices is the higher level of precision of the SHT25 (1.8% relative humidity,
0.2 degree Celsius) compared with the SHT21 (2.0% relative humidity,
0.3 degree Celsius).

The devices communicate with the I2C protocol. All sensors are set to the same
I2C address 0x40, so an entry with I2C_BOARD_INFO("sht21", 0x40) can be used
in the board setup code.

sysfs-Interface
---------------

temp1_input
	- temperature input

humidity1_input
	- humidity input
eic
	- Electronic Identification Code

Notes
-----

The driver uses the default resolution settings of 12 bit for humidity and 14
bit for temperature, which results in typical measurement times of 22 ms for
humidity and 66 ms for temperature. To keep self heating below 0.1 degree
Celsius, the device should not be active for more than 10% of the time,
e.g. maximum two measurements per second at the given resolution.

Different resolutions, the on-chip heater, and using the CRC checksum
are not supported yet.
