.. SPDX-License-Identifier: GPL-2.0

====================================
Multicolor LED handling under Linux
====================================

Description
===========
The multicolor class groups monochrome LEDs and allows controlling two
aspects of the final combined color: hue and lightness. The former is
controlled via the multi_intensity array file and the latter is controlled
via brightness file.

Multicolor Class Control
========================
The multicolor class presents files that groups the colors as indexes in an
array.  These files are children under the LED parent node created by the
led_class framework.  The led_class framework is documented in led-class.rst
within this documentation directory.

Each colored LED will be indexed under the ``multi_*`` files. The order of the
colors will be arbitrary. The ``multi_index`` file can be read to determine the
color name to indexed value.

The ``multi_index`` file is an array that contains the string list of the colors as
they are defined in each ``multi_*`` array file.

The ``multi_intensity`` is an array that can be read or written to for the
individual color intensities.  All elements within this array must be written in
order for the color LED intensities to be updated.

Directory Layout Example
========================
.. code-block:: console

    root:/sys/class/leds/multicolor:status# ls -lR
    -rw-r--r--    1 root     root          4096 Oct 19 16:16 brightness
    -r--r--r--    1 root     root          4096 Oct 19 16:16 max_brightness
    -r--r--r--    1 root     root          4096 Oct 19 16:16 multi_index
    -rw-r--r--    1 root     root          4096 Oct 19 16:16 multi_intensity

..

Multicolor Class Brightness Control
===================================
The brightness level for each LED is calculated based on the color LED
intensity setting divided by the global max_brightness setting multiplied by
the requested brightness.

``led_brightness = brightness * multi_intensity/max_brightness``

Example:
A user first writes the multi_intensity file with the brightness levels
for each LED that are necessary to achieve a certain color output from a
multicolor LED group.

.. code-block:: console

    # cat /sys/class/leds/multicolor:status/multi_index
    green blue red

    # echo 43 226 138 > /sys/class/leds/multicolor:status/multi_intensity

    red -
    	intensity = 138
    	max_brightness = 255
    green -
    	intensity = 43
    	max_brightness = 255
    blue -
    	intensity = 226
    	max_brightness = 255

..

The user can control the brightness of that multicolor LED group by writing the
global 'brightness' control.  Assuming a max_brightness of 255 the user
may want to dim the LED color group to half.  The user would write a value of
128 to the global brightness file then the values written to each LED will be
adjusted base on this value.

.. code-block:: console

    # cat /sys/class/leds/multicolor:status/max_brightness
    255
    # echo 128 > /sys/class/leds/multicolor:status/brightness

..

.. code-block:: none

    adjusted_red_value = 128 * 138/255 = 69
    adjusted_green_value = 128 * 43/255 = 21
    adjusted_blue_value = 128 * 226/255 = 113

..

Reading the global brightness file will return the current brightness value of
the color LED group.

.. code-block:: console

    # cat /sys/class/leds/multicolor:status/brightness
    128

..
