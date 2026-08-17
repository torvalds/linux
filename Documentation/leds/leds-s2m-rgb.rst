.. SPDX-License-Identifier: GPL-2.0

======================================
Samsung S2M Series PMIC RGB LED Driver
======================================

Description
-----------

The RGB LED on the S2M series PMIC hardware features a three-channel LED that
is grouped together as a single device. Furthermore, it supports 8-bit
brightness control for each channel. This LED is typically used as a status
indicator in mobile devices. It also supports various parameters for hardware
patterns.

The hardware pattern can be programmed using the "pattern" trigger, using the
hw_pattern attribute.

/sys/class/leds/<led>/repeat
----------------------------

The hardware supports only indefinitely repeating patterns. The repeat
attribute must be set to -1 for hardware patterns to function.

/sys/class/leds/<led>/hw_pattern
--------------------------------

Specify a hardware pattern for the RGB LEDs.

The pattern is a series of brightness levels and durations in milliseconds.
There should be only one non-zero brightness level. Unlike the results
described in leds-trigger-pattern, the transitions between on and off states
are smoothed out by the hardware.

Simple pattern::

    "255 3000 0 1000"

    255 -+ ''''''-.                     .-'''''''-.
         |         '.                 .'           '.
         |           \               /               \
         |            '.           .'                 '.
         |              '-.......-'                     '-
      0 -+-------+-------+-------+-------+-------+-------+--> time (s)
         0       1       2       3       4       5       6

As described in leds-trigger-pattern, it is also possible to use zero-length
entries to disable the ramping mechanism.

On-Off pattern::

    "255 1000 255 0 0 1000 0 0"

    255 -+ ------+       +-------+       +-------+
         |       |       |       |       |       |
         |       |       |       |       |       |
         |       |       |       |       |       |
         |       +-------+       +-------+       +-------
      0 -+-------+-------+-------+-------+-------+-------+--> time (s)
         0       1       2       3       4       5       6
