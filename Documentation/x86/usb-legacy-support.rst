
.. SPDX-License-Identifier: GPL-2.0

==================
USB Legacy support
==================

:Author: Vojtech Pavlik <vojtech@suse.cz>, January 2004


Also known as "USB Keyboard" or "USB Mouse support" in the BIOS Setup is a
feature that allows one to use the USB mouse and keyboard as if they were
their classic PS/2 counterparts.  This means one can use an USB keyboard to
type in LILO for example.

It has several drawbacks, though:

1) On some machines, the emulated PS/2 mouse takes over even when no USB
   mouse is present and a real PS/2 mouse is present.  In that case the extra
   features (wheel, extra buttons, touchpad mode) of the real PS/2 mouse may
   not be available.

2) If CONFIG_HIGHMEM64G is enabled, the PS/2 mouse emulation can cause
   system crashes, because the SMM BIOS is not expecting to be in PAE mode.
   The Intel E7505 is a typical machine where this happens.

3) If AMD64 64-bit mode is enabled, again system crashes often happen,
   because the SMM BIOS isn't expecting the CPU to be in 64-bit mode.  The
   BIOS manufacturers only test with Windows, and Windows doesn't do 64-bit
   yet.

Solutions:

Problem 1)
  can be solved by loading the USB drivers prior to loading the
  PS/2 mouse driver. Since the PS/2 mouse driver is in 2.6 compiled into
  the kernel unconditionally, this means the USB drivers need to be
  compiled-in, too.

Problem 2)
  can currently only be solved by either disabling HIGHMEM64G
  in the kernel config or USB Legacy support in the BIOS. A BIOS update
  could help, but so far no such update exists.

Problem 3)
  is usually fixed by a BIOS update. Check the board
  manufacturers web site. If an update is not available, disable USB
  Legacy support in the BIOS. If this alone doesn't help, try also adding
  idle=poll on the kernel command line. The BIOS may be entering the SMM
  on the HLT instruction as well.
