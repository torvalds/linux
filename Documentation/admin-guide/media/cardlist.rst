.. SPDX-License-Identifier: GPL-2.0

==========
Cards List
==========

The media subsystem provide support for lots of PCI and USB drivers, plus
platform-specific drivers. It also contains several ancillary I²C drivers.

The platform-specific drivers are usually present on embedded systems,
or are supported by the main board. Usually, setting them is done via
OpenFirmware or ACPI.

The PCI and USB drivers, however, are independent of the system's board,
and may be added/removed by the user.

You may also take a look at
https://linuxtv.org/wiki/index.php/Hardware_Device_Information
for more details about supported cards.

.. toctree::
	:maxdepth: 2

	usb-cardlist
	pci-cardlist
	platform-cardlist
	radio-cardlist
	i2c-cardlist
	misc-cardlist
