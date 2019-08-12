========================================
ADS GraphicsMaster Single Board Computer
========================================

For more details, contact Applied Data Systems or see
http://www.applieddata.net/products.html

The original Linux support for this product has been provided by
Nicolas Pitre <nico@fluxnic.net>. Continued development work by
Woojung Huh <whuh@applieddata.net>

Use 'make graphicsmaster_config' before any 'make config'.
This will set up defaults for GraphicsMaster support.

The kernel zImage is linked to be loaded and executed at 0xc0400000.

Linux can  be used with the ADS BootLoader that ships with the
newer rev boards. See their documentation on how to load Linux.

Supported peripherals
=====================

- SA1100 LCD frame buffer (8/16bpp...sort of)
- SA1111 USB Master
- on-board SMC 92C96 ethernet NIC
- SA1100 serial port
- flash memory access (MTD/JFFS)
- pcmcia, compact flash
- touchscreen(ucb1200)
- ps/2 keyboard
- console on LCD screen
- serial ports (ttyS[0-2])
  - ttyS0 is default for serial console
- Smart I/O (ADC, keypad, digital inputs, etc)
  See http://www.eurotech-inc.com/linux-sbc.asp for IOCTL documentation
  and example user space code. ps/2 keybd is multiplexed through this driver

To do
=====

- everything else!  :-)

Notes
=====

- The flash on board is divided into 3 partitions.  mtd0 is where
  the zImage is stored.  It's been marked as read-only to keep you
  from blasting over the bootloader. :)  mtd1 is
  for the ramdisk.gz image.  mtd2 is user flash space and can be
  utilized for either JFFS or if you're feeling crazy, running ext2
  on top of it. If you're not using the ADS bootloader, you're
  welcome to blast over the mtd1 partition also.

- 16bpp mode requires a different cable than what ships with the board.
  Contact ADS or look through the manual to wire your own. Currently,
  if you compile with 16bit mode support and switch into a lower bpp
  mode, the timing is off so the image is corrupted.  This will be
  fixed soon.

Any contribution can be sent to nico@fluxnic.net and will be greatly welcome!
