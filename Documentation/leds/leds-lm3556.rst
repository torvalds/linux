========================
Kernel driver for lm3556
========================

* Texas Instrument:
  1.5 A Synchronous Boost LED Flash Driver w/ High-Side Current Source
* Datasheet: http://www.national.com/ds/LM/LM3556.pdf

Authors:
      - Daniel Jeong

	Contact:Daniel Jeong(daniel.jeong-at-ti.com, gshark.jeong-at-gmail.com)

Description
-----------
There are 3 functions in LM3556, Flash, Torch and Indicator.

Flash Mode
^^^^^^^^^^

In Flash Mode, the LED current source(LED) provides 16 target current levels
from 93.75 mA to 1500 mA.The Flash currents are adjusted via the CURRENT
CONTROL REGISTER(0x09).Flash mode is activated by the ENABLE REGISTER(0x0A),
or by pulling the STROBE pin HIGH.

LM3556 Flash can be controlled through sys/class/leds/flash/brightness file

* if STROBE pin is enabled, below example control brightness only, and
  ON / OFF will be controlled by STROBE pin.

Flash Example:

OFF::

	#echo 0 > sys/class/leds/flash/brightness

93.75 mA::

	#echo 1 > sys/class/leds/flash/brightness

...

1500  mA::

	#echo 16 > sys/class/leds/flash/brightness

Torch Mode
^^^^^^^^^^

In Torch Mode, the current source(LED) is programmed via the CURRENT CONTROL
REGISTER(0x09).Torch Mode is activated by the ENABLE REGISTER(0x0A) or by the
hardware TORCH input.

LM3556 torch can be controlled through sys/class/leds/torch/brightness file.
* if TORCH pin is enabled, below example control brightness only,
and ON / OFF will be controlled by TORCH pin.

Torch Example:

OFF::

	#echo 0 > sys/class/leds/torch/brightness

46.88 mA::

	#echo 1 > sys/class/leds/torch/brightness

...

375 mA::

	#echo 8 > sys/class/leds/torch/brightness

Indicator Mode
^^^^^^^^^^^^^^

Indicator pattern can be set through sys/class/leds/indicator/pattern file,
and 4 patterns are pre-defined in indicator_pattern array.

According to N-lank, Pulse time and N Period values, different pattern wiill
be generated.If you want new patterns for your own device, change
indicator_pattern array with your own values and INDIC_PATTERN_SIZE.

Please refer datasheet for more detail about N-Blank, Pulse time and N Period.

Indicator pattern example:

pattern 0::

	#echo 0 > sys/class/leds/indicator/pattern

...

pattern 3::

	#echo 3 > sys/class/leds/indicator/pattern

Indicator brightness can be controlled through
sys/class/leds/indicator/brightness file.

Example:

OFF::

	#echo 0 > sys/class/leds/indicator/brightness

5.86 mA::

	#echo 1 > sys/class/leds/indicator/brightness

...

46.875mA::

	#echo 8 > sys/class/leds/indicator/brightness

Notes
-----
Driver expects it is registered using the i2c_board_info mechanism.
To register the chip at address 0x63 on specific adapter, set the platform data
according to include/linux/platform_data/leds-lm3556.h, set the i2c board info

Example::

	static struct i2c_board_info board_i2c_ch4[] __initdata = {
		{
			 I2C_BOARD_INFO(LM3556_NAME, 0x63),
			 .platform_data = &lm3556_pdata,
		 },
	};

and register it in the platform init function

Example::

	board_register_i2c_bus(4, 400,
				board_i2c_ch4, ARRAY_SIZE(board_i2c_ch4));
