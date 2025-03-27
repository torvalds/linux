.. SPDX-License-Identifier: GPL-2.0-or-later

==========================
Samsung Galaxy Book Driver
==========================

Joshua Grisham <josh@joshuagrisham.com>

This is a Linux x86 platform driver for Samsung Galaxy Book series notebook
devices which utilizes Samsung's ``SCAI`` ACPI device in order to control
extra features and receive various notifications.

Supported devices
=================

Any device with one of the supported ACPI device IDs should be supported. This
covers most of the "Samsung Galaxy Book" series notebooks that are currently
available as of this writing, and could include other Samsung notebook devices
as well.

Status
======

The following features are currently supported:

- :ref:`Keyboard backlight <keyboard-backlight>` control
- :ref:`Performance mode <performance-mode>` control implemented using the
  platform profile interface
- :ref:`Battery charge control end threshold
  <battery-charge-control-end-threshold>` (stop charging battery at given
  percentage value) implemented as a battery hook
- :ref:`Firmware Attributes <firmware-attributes>` to allow control of various
  device settings
- :ref:`Handling of Fn hotkeys <keyboard-hotkey-actions>` for various actions
- :ref:`Handling of ACPI notifications and hotkeys
  <acpi-notifications-and-hotkey-actions>`

Because different models of these devices can vary in their features, there is
logic built within the driver which attempts to test each implemented feature
for a valid response before enabling its support (registering additional devices
or extensions, adding sysfs attributes, etc). Therefore, it can be important to
note that not all features may be supported for your particular device.

The following features might be possible to implement but will require
additional investigation and are therefore not supported at this time:

- "Dolby Atmos" mode for the speakers
- "Outdoor Mode" for increasing screen brightness on models with ``SAM0427``
- "Silent Mode" on models with ``SAM0427``

.. _keyboard-backlight:

Keyboard backlight
==================

A new LED class named ``samsung-galaxybook::kbd_backlight`` is created which
will then expose the device using the standard sysfs-based LED interface at
``/sys/class/leds/samsung-galaxybook::kbd_backlight``. Brightness can be
controlled by writing the desired value to the ``brightness`` sysfs attribute or
with any other desired userspace utility.

.. note::
  Most of these devices have an ambient light sensor which also turns
  off the keyboard backlight under well-lit conditions. This behavior does not
  seem possible to control at this time, but can be good to be aware of.

.. _performance-mode:

Performance mode
================

This driver implements the
Documentation/userspace-api/sysfs-platform_profile.rst interface for working
with the "performance mode" function of the Samsung ACPI device.

Mapping of each Samsung "performance mode" to its respective platform profile is
performed dynamically by the driver, as not all models support all of the same
performance modes. Your device might have one or more of the following mappings:

- "Silent" maps to ``low-power``
- "Quiet" maps to ``quiet``
- "Optimized" maps to ``balanced``
- "High performance" maps to ``performance``

The result of the mapping can be printed in the kernel log when the module is
loaded. Supported profiles can also be retrieved from
``/sys/firmware/acpi/platform_profile_choices``, while
``/sys/firmware/acpi/platform_profile`` can be used to read or write the
currently selected profile.

The ``balanced`` platform profile will be set during module load if no profile
has been previously set.

.. _battery-charge-control-end-threshold:

Battery charge control end threshold
====================================

This platform driver will add the ability to set the battery's charge control
end threshold, but does not have the ability to set a start threshold.

This feature is typically called "Battery Saver" by the various Samsung
applications in Windows, but in Linux we have implemented the standardized
"charge control threshold" sysfs interface on the battery device to allow for
controlling this functionality from the userspace.

The sysfs attribute
``/sys/class/power_supply/BAT1/charge_control_end_threshold`` can be used to
read or set the desired charge end threshold.

If you wish to maintain interoperability with the Samsung Settings application
in Windows, then you should set the value to 100 to represent "off", or enable
the feature using only one of the following values: 50, 60, 70, 80, or 90.
Otherwise, the driver will accept any value between 1 and 100 as the percentage
that you wish the battery to stop charging at.

.. note::
  Some devices have been observed as automatically "turning off" the charge
  control end threshold if an input value of less than 30 is given.

.. _firmware-attributes:

Firmware Attributes
===================

The following enumeration-typed firmware attributes are set up by this driver
and should be accessible under
``/sys/class/firmware-attributes/samsung-galaxybook/attributes/`` if your device
supports them:

- ``power_on_lid_open`` (device should power on when the lid is opened)
- ``usb_charging``  (USB ports can deliver power to connected devices even when
  the device is powered off or in a low sleep state)
- ``block_recording`` (blocks access to camera and microphone)

All of these attributes are simple boolean-like enumeration values which use 0
to represent "off" and 1 to represent "on". Use the ``current_value`` attribute
to get or change the setting on the device.

Note that when ``block_recording`` is updated, the input device "Samsung Galaxy
Book Lens Cover" will receive a ``SW_CAMERA_LENS_COVER`` switch event which
reflects the current state.

.. _keyboard-hotkey-actions:

Keyboard hotkey actions (i8042 filter)
======================================

The i8042 filter will swallow the keyboard events for the Fn+F9 hotkey (Multi-
level keyboard backlight toggle) and Fn+F10 hotkey (Block recording toggle)
and instead execute their actions within the driver itself.

Fn+F9 will cycle through the brightness levels of the keyboard backlight. A
notification will be sent using ``led_classdev_notify_brightness_hw_changed``
so that the userspace can be aware of the change. This mimics the behavior of
other existing devices where the brightness level is cycled internally by the
embedded controller and then reported via a notification.

Fn+F10 will toggle the value of the "block recording" setting, which blocks
or allows usage of the built-in camera and microphone (and generates the same
Lens Cover switch event mentioned above).

.. _acpi-notifications-and-hotkey-actions:

ACPI notifications and hotkey actions
=====================================

ACPI notifications will generate ACPI netlink events under the device class
``samsung-galaxybook`` and bus ID matching the Samsung ACPI device ID found on
your device. The events can be received using userspace tools such as
``acpi_listen`` and ``acpid``.

The Fn+F11 Performance mode hotkey will be handled by the driver; each keypress
will cycle to the next available platform profile.
