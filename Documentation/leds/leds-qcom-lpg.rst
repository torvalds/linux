.. SPDX-License-Identifier: GPL-2.0

==============================
Kernel driver for Qualcomm LPG
==============================

Description
-----------

The Qualcomm LPG can be found in a variety of Qualcomm PMICs and consists of a
number of PWM channels, a programmable pattern lookup table and a RGB LED
current sink.

To facilitate the various use cases, the LPG channels can be exposed as
individual LEDs, grouped together as RGB LEDs or otherwise be accessed as PWM
channels. The output of each PWM channel is routed to other hardware
blocks, such as the RGB current sink, GPIO pins etc.

The each PWM channel can operate with a period between 27us and 384 seconds and
has a 9 bit resolution of the duty cycle.

In order to provide support for status notifications with the CPU subsystem in
deeper idle states the LPG provides pattern support. This consists of a shared
lookup table of brightness values and per channel properties to select the
range within the table to use, the rate and if the pattern should repeat.

The pattern for a channel can be programmed using the "pattern" trigger, using
the hw_pattern attribute.

/sys/class/leds/<led>/hw_pattern
--------------------------------

Specify a hardware pattern for a Qualcomm LPG LED.

The pattern is a series of brightness and hold-time pairs, with the hold-time
expressed in milliseconds. The hold time is a property of the pattern and must
therefor be identical for each element in the pattern (except for the pauses
described below). As the LPG hardware is not able to perform the linear
transitions expected by the leds-trigger-pattern format, each entry in the
pattern must be followed a zero-length entry of the same brightness.

Simple pattern::

    "255 500 255 0 0 500 0 0"

        ^
        |
    255 +----+    +----+
        |    |    |    |      ...
      0 |    +----+    +----
        +---------------------->
        0    5   10   15     time (100ms)

The LPG supports specifying a longer hold-time for the first and last element
in the pattern, the so called "low pause" and "high pause".

Low-pause pattern::

    "255 1000 255 0 0 500 0 0 255 500 255 0 0 500 0 0"

        ^
        |
    255 +--------+    +----+    +----+    +--------+
        |        |    |    |    |    |    |        |      ...
      0 |        +----+    +----+    +----+        +----
        +----------------------------->
        0    5   10   15  20   25   time (100ms)

Similarily, the last entry can be stretched by using a higher hold-time on the
last entry.

In order to save space in the shared lookup table the LPG supports "ping-pong"
mode, in which case each run through the pattern is performed by first running
the pattern forward, then backwards. This mode is automatically used by the
driver when the given pattern is a palindrome. In this case the "high pause"
denotes the wait time before the pattern is run in reverse and as such the
specified hold-time of the middle item in the pattern is allowed to have a
different hold-time.
