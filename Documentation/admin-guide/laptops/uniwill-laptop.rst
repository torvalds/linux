.. SPDX-License-Identifier: GPL-2.0+

Uniwill laptop extra features
=============================

On laptops manufactured by Uniwill (either directly or as ODM), the ``uniwill-laptop`` driver
handles various platform-specific features.

Module Loading
--------------

The ``uniwill-laptop`` driver relies on a DMI table to automatically load on supported devices.
When using the ``force`` module parameter, this DMI check will be omitted, allowing the driver
to be loaded on unsupported devices for testing purposes.

Hotkeys
-------

Usually the FN keys work without a special driver. However as soon as the ``uniwill-laptop`` driver
is loaded, the FN keys need to be handled manually. This is done automatically by the driver itself.

Keyboard settings
-----------------

The ``uniwill-laptop`` driver allows the user to enable/disable:

 - the FN lock and super key of the integrated keyboard
 - the touchpad toggle functionality of the integrated touchpad

See Documentation/ABI/testing/sysfs-driver-uniwill-laptop for details.

Hwmon interface
---------------

The ``uniwill-laptop`` driver supports reading of the CPU and GPU temperature and supports up to
two fans. Userspace applications can access sensor readings over the hwmon sysfs interface.

Platform profile
----------------

Support for changing the platform performance mode is currently not implemented.

Battery Charging Control
------------------------

.. warning:: Some devices do not properly implement the charging threshold interface. Forcing
             the driver to enable access to said interface on such devices might damage the
             battery [1]_. Because of this the driver will not enable said feature even when
             using the ``force`` module parameter. The charging profile interface will be
             available instead.

The ``uniwill-laptop`` driver supports controlling the battery charge limit. This either happens
over the standard ``charge_control_end_threshold`` or ``charge_types`` power supply sysfs attribute,
depending on the device. When using the ``charge_control_end_threshold`` sysfs attribute, all values
between 1 and 100 percent are supported. When using the ``charge_types`` sysfs attribute, the driver
supports switching between the ``Standard``, ``Trickle`` and ``Long Life`` profiles.

Keep in mind that when using the ``charge_types`` sysfs attribute, the EC firmware will hide the
true charging status of the battery from the operating system, potentially misleading users into
thinking that the charging profile does not work. Checking the ``current_now`` sysfs attribute
tells you the true charging status of the battery even when using the ``charge_types`` sysfs
attribute (0 means that the battery is currently not charging).

Additionally the driver signals the presence of battery charging issues through the standard
``health`` power supply sysfs attribute.

It also lets you set whether a USB-C power source should prioritise charging the battery or
delivering immediate power to the cpu. See Documentation/ABI/testing/sysfs-driver-uniwill-laptop for
details.

Lightbar
--------

The ``uniwill-laptop`` driver exposes the lightbar found on some models as a standard multicolor
LED class device. The default name of this LED class device is ``uniwill:multicolor:status``.

See Documentation/ABI/testing/sysfs-driver-uniwill-laptop for details on how to control the various
animation modes of the lightbar.

Configurable TGP
----------------

The ``uniwill-laptop`` driver allows to set the configurable TGP for devices with NVIDIA GPUs that
allow it.

See Documentation/ABI/testing/sysfs-driver-uniwill-laptop for details.

References
==========

.. [1] https://www.reddit.com/r/XMG_gg/comments/ld9yyf/battery_limit_hidden_function_discovered_on/
