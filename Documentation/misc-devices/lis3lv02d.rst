=======================
Kernel driver lis3lv02d
=======================

Supported chips:

  * STMicroelectronics LIS3LV02DL, LIS3LV02DQ (12 bits precision)
  * STMicroelectronics LIS302DL, LIS3L02DQ, LIS331DL (8 bits) and
    LIS331DLH (16 bits)

Authors:
        - Yan Burman <burman.yan@gmail.com>
	- Eric Piel <eric.piel@tremplin-utc.net>


Description
-----------

This driver provides support for the accelerometer found in various HP laptops
sporting the feature officially called "HP Mobile Data Protection System 3D" or
"HP 3D DriveGuard". It detects automatically laptops with this sensor. Known
models (full list can be found in drivers/platform/x86/hp_accel.c) will have
their axis automatically oriented on standard way (eg: you can directly play
neverball). The accelerometer data is readable via
/sys/devices/faux/lis3lv02d. Reported values are scaled
to mg values (1/1000th of earth gravity).

Sysfs attributes under /sys/devices/faux/lis3lv02d/:

position
      - 3D position that the accelerometer reports. Format: "(x,y,z)"
rate
      - read reports the sampling rate of the accelerometer device in HZ.
	write changes sampling rate of the accelerometer device.
	Only values which are supported by HW are accepted.
selftest
      - performs selftest for the chip as specified by chip manufacturer.

This driver also provides an absolute input class device, allowing
the laptop to act as a pinball machine-esque joystick. Joystick device can be
calibrated. Joystick device can be in two different modes.
By default output values are scaled between -32768 .. 32767. In joystick raw
mode, joystick and sysfs position entry have the same scale. There can be
small difference due to input system fuzziness feature.
Events are also available as input event device.

Selftest is meant only for hardware diagnostic purposes. It is not meant to be
used during normal operations. Position data is not corrupted during selftest
but interrupt behaviour is not guaranteed to work reliably. In test mode, the
sensing element is internally moved little bit. Selftest measures difference
between normal mode and test mode. Chip specifications tell the acceptance
limit for each type of the chip. Limits are provided via platform data
to allow adjustment of the limits without a change to the actual driver.
Seltest returns either "OK x y z" or "FAIL x y z" where x, y and z are
measured difference between modes. Axes are not remapped in selftest mode.
Measurement values are provided to help HW diagnostic applications to make
final decision.

On HP laptops, if the led infrastructure is activated, support for a led
indicating disk protection will be provided as /sys/class/leds/hp::hddprotect.

Another feature of the driver is misc device called "freefall" that
acts similar to /dev/rtc and reacts on free-fall interrupts received
from the device. It supports blocking operations, poll/select and
fasync operation modes. You must read 1 bytes from the device.  The
result is number of free-fall interrupts since the last successful
read (or 255 if number of interrupts would not fit). See the freefall.c
file for an example on using the device.


Axes orientation
----------------

For better compatibility between the various laptops. The values reported by
the accelerometer are converted into a "standard" organisation of the axes
(aka "can play neverball out of the box"):

 * When the laptop is horizontal the position reported is about 0 for X and Y
   and a positive value for Z
 * If the left side is elevated, X increases (becomes positive)
 * If the front side (where the touchpad is) is elevated, Y decreases
   (becomes negative)
 * If the laptop is put upside-down, Z becomes negative

If your laptop model is not recognized (cf "dmesg"), you can send an
email to the maintainer to add it to the database.  When reporting a new
laptop, please include the output of "dmidecode" plus the value of
/sys/devices/faux/lis3lv02d/position in these four cases.

Q&A
---

Q: How do I safely simulate freefall? I have an HP "portable
workstation" which has about 3.5kg and a plastic case, so letting it
fall to the ground is out of question...

A: The sensor is pretty sensitive, so your hands can do it. Lift it
into free space, follow the fall with your hands for like 10
centimeters. That should be enough to trigger the detection.
