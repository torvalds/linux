=================
Built-in firmware
=================

Firmware can be built-in to the kernel, this means building the firmware
into vmlinux directly, to enable avoiding having to look for firmware from
the filesystem. Instead, firmware can be looked for inside the kernel
directly. You can enable built-in firmware using the kernel configuration
options:

  * CONFIG_EXTRA_FIRMWARE
  * CONFIG_EXTRA_FIRMWARE_DIR

This should not be confused with CONFIG_FIRMWARE_IN_KERNEL, this is for drivers
which enables firmware to be built as part of the kernel build process. This
option, CONFIG_FIRMWARE_IN_KERNEL, will build all firmware for all drivers
enabled which ship its firmware inside the Linux kernel source tree.

There are a few reasons why you might want to consider building your firmware
into the kernel with CONFIG_EXTRA_FIRMWARE though:

* Speed
* Firmware is needed for accessing the boot device, and the user doesn't
  want to stuff the firmware into the boot initramfs.

Even if you have these needs there are a few reasons why you may not be
able to make use of built-in firmware:

* Legalese - firmware is non-GPL compatible
* Some firmware may be optional
* Firmware upgrades are possible, therefore a new firmware would implicate
  a complete kernel rebuild.
* Some firmware files may be really large in size. The remote-proc subsystem
  is an example subsystem which deals with these sorts of firmware
* The firmware may need to be scraped out from some device specific location
  dynamically, an example is calibration data for for some WiFi chipsets. This
  calibration data can be unique per sold device.

