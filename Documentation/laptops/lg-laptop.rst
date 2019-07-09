.. SPDX-License-Identifier: GPL-2.0+

:orphan:

LG Gram laptop extra features
=============================

By Matan Ziv-Av <matan@svgalib.org>


Hotkeys
-------

The following FN keys are ignored by the kernel without this driver:

- FN-F1 (LG control panel)   - Generates F15
- FN-F5 (Touchpad toggle)    - Generates F13
- FN-F6 (Airplane mode)      - Generates RFKILL
- FN-F8 (Keyboard backlight) - Generates F16.
  This key also changes keyboard backlight mode.
- FN-F9 (Reader mode)        - Generates F14

The rest of the FN keys work without a need for a special driver.


Reader mode
-----------

Writing 0/1 to /sys/devices/platform/lg-laptop/reader_mode disables/enables
reader mode. In this mode the screen colors change (blue color reduced),
and the reader mode indicator LED (on F9 key) turns on.


FN Lock
-------

Writing 0/1 to /sys/devices/platform/lg-laptop/fn_lock disables/enables
FN lock.


Battery care limit
------------------

Writing 80/100 to /sys/devices/platform/lg-laptop/battery_care_limit
sets the maximum capacity to charge the battery. Limiting the charge
reduces battery capacity loss over time.

This value is reset to 100 when the kernel boots.


Fan mode
--------

Writing 1/0 to /sys/devices/platform/lg-laptop/fan_mode disables/enables
the fan silent mode.


USB charge
----------

Writing 0/1 to /sys/devices/platform/lg-laptop/usb_charge disables/enables
charging another device from the USB port while the device is turned off.

This value is reset to 0 when the kernel boots.


LEDs
~~~~

The are two LED devices supported by the driver:

Keyboard backlight
------------------

A led device named kbd_led controls the keyboard backlight. There are three
lighting level: off (0), low (127) and high (255).

The keyboard backlight is also controlled by the key combination FN-F8
which cycles through those levels.


Touchpad indicator LED
----------------------

On the F5 key. Controlled by led device names tpad_led.
