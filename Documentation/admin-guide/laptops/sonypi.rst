==================================================
Sony Programmable I/O Control Device Driver Readme
==================================================

	- Copyright (C) 2001-2004 Stelian Pop <stelian@popies.net>
	- Copyright (C) 2001-2002 Alcôve <www.alcove.com>
	- Copyright (C) 2001 Michael Ashley <m.ashley@unsw.edu.au>
	- Copyright (C) 2001 Junichi Morita <jun1m@mars.dti.ne.jp>
	- Copyright (C) 2000 Takaya Kinjo <t-kinjo@tc4.so-net.ne.jp>
	- Copyright (C) 2000 Andrew Tridgell <tridge@samba.org>

This driver enables access to the Sony Programmable I/O Control Device which
can be found in many Sony Vaio laptops. Some newer Sony laptops (seems to be
limited to new FX series laptops, at least the FX501 and the FX702) lack a
sonypi device and are not supported at all by this driver.

It will give access (through a user space utility) to some events those laptops
generate, like:

	- jogdial events (the small wheel on the side of Vaios)
	- capture button events (only on Vaio Picturebook series)
	- Fn keys
	- bluetooth button (only on C1VR model)
	- programmable keys, back, help, zoom, thumbphrase buttons, etc.
	  (when available)

Those events (see linux/sonypi.h) can be polled using the character device node
/dev/sonypi (major 10, minor auto allocated or specified as an option).
A simple daemon which translates the jogdial movements into mouse wheel events
can be downloaded at: <http://popies.net/sonypi/>

Another option to intercept the events is to get them directly through the
input layer.

This driver supports also some ioctl commands for setting the LCD screen
brightness and querying the batteries charge information (some more
commands may be added in the future).

This driver can also be used to set the camera controls on Picturebook series
(brightness, contrast etc), and is used by the video4linux driver for the
Motion Eye camera.

Please note that this driver was created by reverse engineering the Windows
driver and the ACPI BIOS, because Sony doesn't agree to release any programming
specs for its laptops. If someone convinces them to do so, drop me a note.

Driver options:
---------------

Several options can be passed to the sonypi driver using the standard
module argument syntax (<param>=<value> when passing the option to the
module or sonypi.<param>=<value> on the kernel boot line when sonypi is
statically linked into the kernel). Those options are:

	=============== =======================================================
	minor:		minor number of the misc device /dev/sonypi,
			default is -1 (automatic allocation, see /proc/misc
			or kernel logs)

	camera:		if you have a PictureBook series Vaio (with the
			integrated MotionEye camera), set this parameter to 1
			in order to let the driver access to the camera

	fnkeyinit:	on some Vaios (C1VE, C1VR etc), the Fn key events don't
			get enabled unless you set this parameter to 1.
			Do not use this option unless it's actually necessary,
			some Vaio models don't deal well with this option.
			This option is available only if the kernel is
			compiled without ACPI support (since it conflicts
			with it and it shouldn't be required anyway if
			ACPI is already enabled).

	verbose:	set to 1 to print unknown events received from the
			sonypi device.
			set to 2 to print all events received from the
			sonypi device.

	compat:		uses some compatibility code for enabling the sonypi
			events. If the driver worked for you in the past
			(prior to version 1.5) and does not work anymore,
			add this option and report to the author.

	mask:		event mask telling the driver what events will be
			reported to the user. This parameter is required for
			some Vaio models where the hardware reuses values
			used in other Vaio models (like the FX series who does
			not have a jogdial but reuses the jogdial events for
			programmable keys events). The default event mask is
			set to 0xffffffff, meaning that all possible events
			will be tried. You can use the following bits to
			construct your own event mask (from
			drivers/char/sonypi.h)::

				SONYPI_JOGGER_MASK		0x0001
				SONYPI_CAPTURE_MASK		0x0002
				SONYPI_FNKEY_MASK		0x0004
				SONYPI_BLUETOOTH_MASK		0x0008
				SONYPI_PKEY_MASK		0x0010
				SONYPI_BACK_MASK		0x0020
				SONYPI_HELP_MASK		0x0040
				SONYPI_LID_MASK			0x0080
				SONYPI_ZOOM_MASK		0x0100
				SONYPI_THUMBPHRASE_MASK		0x0200
				SONYPI_MEYE_MASK		0x0400
				SONYPI_MEMORYSTICK_MASK		0x0800
				SONYPI_BATTERY_MASK		0x1000
				SONYPI_WIRELESS_MASK		0x2000

	useinput:	if set (which is the default) two input devices are
			created, one which interprets the jogdial events as
			mouse events, the other one which acts like a
			keyboard reporting the pressing of the special keys.
	=============== =======================================================

Module use:
-----------

In order to automatically load the sonypi module on use, you can put those
lines a configuration file in /etc/modprobe.d/::

	alias char-major-10-250 sonypi
	options sonypi minor=250

This supposes the use of minor 250 for the sonypi device::

	# mknod /dev/sonypi c 10 250

Bugs:
-----

	- several users reported that this driver disables the BIOS-managed
	  Fn-keys which put the laptop in sleeping state, or switch the
	  external monitor on/off. There is no workaround yet, since this
	  driver disables all APM management for those keys, by enabling the
	  ACPI management (and the ACPI core stuff is not complete yet). If
	  you have one of those laptops with working Fn keys and want to
	  continue to use them, don't use this driver.

	- some users reported that the laptop speed is lower (dhrystone
	  tested) when using the driver with the fnkeyinit parameter. I cannot
	  reproduce it on my laptop and not all users have this problem.
	  This happens because the fnkeyinit parameter enables the ACPI
	  mode (but without additional ACPI control, like processor
	  speed handling etc). Use ACPI instead of APM if it works on your
	  laptop.

	- sonypi lacks the ability to distinguish between certain key
	  events on some models.

	- some models with the nvidia card (geforce go 6200 tc) uses a
	  different way to adjust the backlighting of the screen. There
	  is a userspace utility to adjust the brightness on those models,
	  which can be downloaded from
	  https://www.acc.umu.se/~erikw/program/smartdimmer-0.1.tar.bz2

	- since all development was done by reverse engineering, there is
	  *absolutely no guarantee* that this driver will not crash your
	  laptop. Permanently.
