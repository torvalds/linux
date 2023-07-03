.. SPDX-License-Identifier: GPL-2.0

===========================================================
Kernel driver for Intel Cherry Trail Whiskey Cove PMIC LEDs
===========================================================

/sys/class/leds/<led>/hw_pattern
--------------------------------

Specify a hardware pattern for the Whiskey Cove PMIC LEDs.

The only supported pattern is hardware breathing mode::

    "0 2000 1 2000"

	^
	|
    Max-|     ---
	|    /   \
	|   /     \
	|  /       \     /
	| /         \   /
    Min-|-           ---
	|
	0------2------4--> time (sec)

The rise and fall times must be the same value.
Supported values are 2000, 1000, 500 and 250 for
breathing frequencies of 1/4, 1/2, 1 and 2 Hz.

The set pattern only controls the timing. For max brightness the last
set brightness is used and the max brightness can be changed
while breathing by writing the brightness attribute.

This is just like how blinking works in the LED subsystem,
for both sw and hw blinking the brightness can also be changed
while blinking. Breathing on this hw really is just a variant
mode of blinking.
