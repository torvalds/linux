.. SPDX-License-Identifier: GPL-2.0

DeviceTree Booting
------------------

  There is one single 32bit entry point to the kernel at code32_start,
  the decompressor (the real mode entry point goes to the same  32bit
  entry point once it switched into protected mode). That entry point
  supports one calling convention which is documented in
  Documentation/arch/x86/boot.rst
  The physical pointer to the device-tree block is passed via setup_data
  which requires at least boot protocol 2.09.
  The type filed is defined as

  #define SETUP_DTB                      2

  This device-tree is used as an extension to the "boot page". As such it
  does not parse / consider data which is already covered by the boot
  page. This includes memory size, reserved ranges, command line arguments
  or initrd address. It simply holds information which can not be retrieved
  otherwise like interrupt routing or a list of devices behind an I2C bus.
