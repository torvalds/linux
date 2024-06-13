Kernel driver w1_ds2438
=======================

Supported chips:

  * Maxim DS2438 Smart Battery Monitor

supported family codes:
        ================        ====
        W1_FAMILY_DS2438        0x26
        ================        ====

Author: Mariusz Bialonczyk <manio@skyboo.net>

Description
-----------

The DS2438 chip provides several functions that are desirable to carry in
a battery pack. It also has a 40 bytes of nonvolatile EEPROM.
Because the ability of temperature, current and voltage measurement, the chip
is also often used in weather stations and applications such as: rain gauge,
wind speed/direction measuring, humidity sensing, etc.

Current support is provided through the following sysfs files (all files
except "iad" and "offset" are readonly):

"iad"
-----
This file controls the 'Current A/D Control Bit' (IAD) in the
Status/Configuration Register.
Writing a zero value will clear the IAD bit and disables the current
measurements.
Writing value "1" is setting the IAD bit (enables the measurements).
The IAD bit is enabled by default in the DS2438.

When writing to sysfs file bits 2-7 are ignored, so it's safe to write ASCII.
An I/O error is returned when there is a problem setting the new value.

"page0"
-------
This file provides full 8 bytes of the chip Page 0 (00h).
This page contains the most frequently accessed information of the DS2438.
Internally when this file is read, the additional CRC byte is also obtained
from the slave device. If it is correct, the 8 bytes page data are passed
to userspace, otherwise an I/O error is returned.

"page1"
-------
This file provides full 8 bytes of the chip Page 1 (01h).
This page contains the ICA, elapsed time meter and current offset data of the DS2438.
Internally when this file is read, the additional CRC byte is also obtained
from the slave device. If it is correct, the 8 bytes page data are passed
to userspace, otherwise an I/O error is returned.

"offset"
--------
This file controls the 2-byte Offset Register of the chip.
Writing a 2-byte value will change the Offset Register, which changes the
current measurement done by the chip. Changing this register to the two's complement
of the current register while forcing zero current through the load will calibrate
the chip, canceling offset errors in the current ADC.


"temperature"
-------------
Opening and reading this file initiates the CONVERT_T (temperature conversion)
command of the chip, afterwards the temperature is read from the device
registers and provided as an ASCII decimal value.

Important: The returned value has to be divided by 256 to get a real
temperature in degrees Celsius.

"vad", "vdd"
------------
Opening and reading this file initiates the CONVERT_V (voltage conversion)
command of the chip.

Depending on a sysfs filename a different input for the A/D will be selected:

vad:
    general purpose A/D input (VAD)
vdd:
    battery input (VDD)

After the voltage conversion the value is returned as decimal ASCII.
Note: To get a volts the value has to be divided by 100.
