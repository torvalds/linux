.. SPDX-License-Identifier: GPL-2.0

=====================
ACPI video extensions
=====================

This driver implement the ACPI Extensions For Display Adapters for
integrated graphics devices on motherboard, as specified in ACPI 2.0
Specification, Appendix B, allowing to perform some basic control like
defining the video POST device, retrieving EDID information or to
setup a video output, etc.  Note that this is an ref. implementation
only.  It may or may not work for your integrated video device.

The ACPI video driver does 3 things regarding backlight control.

Export a sysfs interface for user space to control backlight level
==================================================================

If the ACPI table has a video device, and acpi_backlight=vendor kernel
command line is not present, the driver will register a backlight device
and set the required backlight operation structure for it for the sysfs
interface control. For every registered class device, there will be a
directory named acpi_videoX under /sys/class/backlight.

The backlight sysfs interface has a standard definition here:
Documentation/ABI/stable/sysfs-class-backlight.

And what ACPI video driver does is:

actual_brightness:
  on read, control method _BQC will be evaluated to
  get the brightness level the firmware thinks it is at;
bl_power:
  not implemented, will set the current brightness instead;
brightness:
  on write, control method _BCM will run to set the requested brightness level;
max_brightness:
  Derived from the _BCL package(see below);
type:
  firmware

Note that ACPI video backlight driver will always use index for
brightness, actual_brightness and max_brightness. So if we have
the following _BCL package::

	Method (_BCL, 0, NotSerialized)
	{
		Return (Package (0x0C)
		{
			0x64,
			0x32,
			0x0A,
			0x14,
			0x1E,
			0x28,
			0x32,
			0x3C,
			0x46,
			0x50,
			0x5A,
			0x64
		})
	}

The first two levels are for when laptop are on AC or on battery and are
not used by Linux currently. The remaining 10 levels are supported levels
that we can choose from. The applicable index values are from 0 (that
corresponds to the 0x0A brightness value) to 9 (that corresponds to the
0x64 brightness value) inclusive. Each of those index values is regarded
as a "brightness level" indicator. Thus from the user space perspective
the range of available brightness levels is from 0 to 9 (max_brightness)
inclusive.

Notify user space about hotkey event
====================================

There are generally two cases for hotkey event reporting:

i) For some laptops, when user presses the hotkey, a scancode will be
   generated and sent to user space through the input device created by
   the keyboard driver as a key type input event, with proper remap, the
   following key code will appear to user space::

	EV_KEY, KEY_BRIGHTNESSUP
	EV_KEY, KEY_BRIGHTNESSDOWN
	etc.

For this case, ACPI video driver does not need to do anything(actually,
it doesn't even know this happened).

ii) For some laptops, the press of the hotkey will not generate the
    scancode, instead, firmware will notify the video device ACPI node
    about the event. The event value is defined in the ACPI spec. ACPI
    video driver will generate an key type input event according to the
    notify value it received and send the event to user space through the
    input device it created:

	=====		==================
	event		keycode
	=====		==================
	0x86		KEY_BRIGHTNESSUP
	0x87		KEY_BRIGHTNESSDOWN
	etc.
	=====		==================

so this would lead to the same effect as case i) now.

Once user space tool receives this event, it can modify the backlight
level through the sysfs interface.

Change backlight level in the kernel
====================================

This works for machines covered by case ii) in Section 2. Once the driver
received a notification, it will set the backlight level accordingly. This does
not affect the sending of event to user space, they are always sent to user
space regardless of whether or not the video module controls the backlight level
directly. This behaviour can be controlled through the brightness_switch_enabled
module parameter as documented in admin-guide/kernel-parameters.rst. It is
recommended to disable this behaviour once a GUI environment starts up and
wants to have full control of the backlight level.
