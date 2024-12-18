.. SPDX-License-Identifier: GPL-2.0

============================================
Kernel driver for STMicroelectronics LED1202
============================================

/sys/class/leds/<led>/hw_pattern
--------------------------------

Specify a hardware pattern for the ST1202 LED. The LED controller
implements 12 low-side current generators with independent dimming
control. Internal volatile memory allows the user to store up to 8
different patterns. Each pattern is a particular output configuration
in terms of PWM duty-cycle and duration (ms).

To be compatible with the hardware pattern format, maximum 8 tuples of
brightness (PWM) and duration must be written to hw_pattern.

- Min pattern duration: 22 ms
- Max pattern duration: 5660 ms

The format of the hardware pattern values should be:
"brightness duration brightness duration ..."

/sys/class/leds/<led>/repeat
----------------------------

Specify a pattern repeat number, which is common for all channels.
Default is 1; negative numbers and 0 are invalid.

This file will always return the originally written repeat number.

When the 255 value is written to it, all patterns will repeat
indefinitely.
