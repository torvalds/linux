========================
Kernel driver w1_ds28e17
========================

Supported chips:

  * Maxim DS28E17 1-Wire-to-I2C Master Bridge

supported family codes:

        =================  ====
	W1_FAMILY_DS28E17  0x19
        =================  ====

Author: Jan Kandziora <jjj@gmx.de>


Description
-----------
The DS28E17 is a Onewire slave device which acts as an I2C bus master.

This driver creates a new I2C bus for any DS28E17 device detected. I2C buses
come and go as the DS28E17 devices come and go. I2C slave devices connected to
a DS28E17 can be accessed by the kernel or userspace tools as if they were
connected to a "native" I2C bus master.


An udev rule like the following::

  SUBSYSTEM=="i2c-dev", KERNEL=="i2c-[0-9]*", ATTRS{name}=="w1-19-*", \
          SYMLINK+="i2c-$attr{name}"

may be used to create stable /dev/i2c- entries based on the unique id of the
DS28E17 chip.


Driver parameters are:

speed:
	This sets up the default I2C speed a DS28E17 get configured for as soon
	it is connected. The power-on default	of the DS28E17 is 400kBaud, but
	chips may come and go on the Onewire bus without being de-powered and
	as soon the "w1_ds28e17" driver notices a freshly connected, or
	reconnected DS28E17 device on the Onewire bus, it will re-apply this
	setting.

	Valid values are 100, 400, 900 [kBaud]. Any other value means to leave
	alone the current DS28E17 setting on detect. The default value is 100.

stretch:
	This sets up the default stretch value used for freshly connected
	DS28E17 devices. It is a multiplier used on the calculation of the busy
	wait time for an I2C transfer. This is to account for I2C slave devices
	which make heavy use of the I2C clock stretching feature and thus, the
	needed timeout cannot be pre-calculated correctly. As the w1_ds28e17
	driver checks the DS28E17's busy flag in a loop after the precalculated
	wait time, it should be hardly needed to tweak this setting.

	Leave it at 1 unless you get ETIMEDOUT errors and a "w1_slave_driver
	19-00000002dbd8: busy timeout" in the kernel log.

	Valid values are 1 to 9. The default is 1.


The driver creates sysfs files /sys/bus/w1/devices/19-<id>/speed and
/sys/bus/w1/devices/19-<id>/stretch for each device, preloaded with the default
settings from the driver parameters. They may be changed anytime. In addition a
directory /sys/bus/w1/devices/19-<id>/i2c-<nnn> for the I2C bus master sysfs
structure is created.


See https://github.com/ianka/w1_ds28e17 for even more information.
