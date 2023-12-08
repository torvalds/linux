Kernel driver pcf8591
=====================

Supported chips:

  * Philips/NXP PCF8591

    Prefix: 'pcf8591'

    Addresses scanned: none

    Datasheet: Publicly available at the NXP website

	       http://www.nxp.com/pip/PCF8591_6.html

Authors:
      - Aurelien Jarno <aurelien@aurel32.net>
      - valuable contributions by Jan M. Sendler <sendler@sendler.de>,
      - Jean Delvare <jdelvare@suse.de>


Description
-----------

The PCF8591 is an 8-bit A/D and D/A converter (4 analog inputs and one
analog output) for the I2C bus produced by Philips Semiconductors (now NXP).
It is designed to provide a byte I2C interface to up to 4 separate devices.

The PCF8591 has 4 analog inputs programmable as single-ended or
differential inputs:

- mode 0 : four single ended inputs
	Pins AIN0 to AIN3 are single ended inputs for channels 0 to 3

- mode 1 : three differential inputs
	Pins AIN3 is the common negative differential input
	Pins AIN0 to AIN2 are positive differential inputs for channels 0 to 2

- mode 2 : single ended and differential mixed
	Pins AIN0 and AIN1 are single ended inputs for channels 0 and 1
	Pins AIN2 is the positive differential input for channel 3
	Pins AIN3 is the negative differential input for channel 3

- mode 3 : two differential inputs
	Pins AIN0 is the positive differential input for channel 0
	Pins AIN1 is the negative differential input for channel 0
	Pins AIN2 is the positive differential input for channel 1
	Pins AIN3 is the negative differential input for channel 1

See the datasheet for details.

Module parameters
-----------------

* input_mode int

    Analog input mode:

	 - 0 = four single ended inputs
	 - 1 = three differential inputs
	 - 2 = single ended and differential mixed
	 - 3 = two differential inputs


Accessing PCF8591 via /sys interface
-------------------------------------

The PCF8591 is plainly impossible to detect! Thus the driver won't even
try. You have to explicitly instantiate the device at the relevant
address (in the interval [0x48..0x4f]) either through platform data, or
using the sysfs interface. See Documentation/i2c/instantiating-devices.rst
for details.

Directories are being created for each instantiated PCF8591:

/sys/bus/i2c/devices/<0>-<1>/
   where <0> is the bus the chip is connected to (e. g. i2c-0)
   and <1> the chip address ([48..4f])

Inside these directories, there are such files:

   in0_input, in1_input, in2_input, in3_input, out0_enable, out0_output, name

Name contains chip name.

The in0_input, in1_input, in2_input and in3_input files are RO. Reading gives
the value of the corresponding channel. Depending on the current analog inputs
configuration, files in2_input and in3_input may not exist. Values range
from 0 to 255 for single ended inputs and -128 to +127 for differential inputs
(8-bit ADC).

The out0_enable file is RW. Reading gives "1" for analog output enabled and
"0" for analog output disabled. Writing accepts "0" and "1" accordingly.

The out0_output file is RW. Writing a number between 0 and 255 (8-bit DAC), send
the value to the digital-to-analog converter. Note that a voltage will
only appears on AOUT pin if aout0_enable equals 1. Reading returns the last
value written.
