=============================================
ADS GraphicsClient Plus Single Board Computer
=============================================

For more details, contact Applied Data Systems or see
http://www.applieddata.net/products.html

The original Linux support for this product has been provided by
Nicolas Pitre <nico@fluxnic.net>. Continued development work by
Woojung Huh <whuh@applieddata.net>

It's currently possible to mount a root filesystem via NFS providing a
complete Linux environment.  Otherwise a ramdisk image may be used.  The
board supports MTD/JFFS, so you could also mount something on there.

Use 'make graphicsclient_config' before any 'make config'.  This will set up
defaults for GraphicsClient Plus support.

The kernel zImage is linked to be loaded and executed at 0xc0200000.
Also the following registers should have the specified values upon entry::

	r0 = 0
	r1 = 29	(this is the GraphicsClient architecture number)

Linux can  be used with the ADS BootLoader that ships with the
newer rev boards. See their documentation on how to load Linux.
Angel is not available for the GraphicsClient Plus AFAIK.

There is a  board known as just the GraphicsClient that ADS used to
produce but has end of lifed. This code will not work on the older
board with the ADS bootloader, but should still work with Angel,
as outlined below.  In any case, if you're planning on deploying
something en masse, you should probably get the newer board.

If using Angel on the older boards, here is a typical angel.opt option file
if the kernel is loaded through the Angel Debug Monitor::

	base 0xc0200000
	entry 0xc0200000
	r0 0x00000000
	r1 0x0000001d
	device /dev/ttyS1
	options "38400 8N1"
	baud 115200
	#otherfile ramdisk.gz
	#otherbase 0xc0800000
	exec minicom

Then the kernel (and ramdisk if otherfile/otherbase lines above are
uncommented) would be loaded with::

	angelboot -f angelboot.opt zImage

Here it is assumed that the board is connected to ttyS1 on your PC
and that minicom is preconfigured with /dev/ttyS1, 38400 baud, 8N1, no flow
control by default.

If any other bootloader is used, ensure it accomplish the same, especially
for r0/r1 register values before jumping into the kernel.


Supported peripherals
=====================

- SA1100 LCD frame buffer (8/16bpp...sort of)
- on-board SMC 92C96 ethernet NIC
- SA1100 serial port
- flash memory access (MTD/JFFS)
- pcmcia
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

- UCB1200 audio with new ucb_generic layer
- everything else!  :-)

Notes
=====

- The flash on board is divided into 3 partitions.  mtd0 is where
  the ADS boot ROM and zImage is stored.  It's been marked as
  read-only to keep you from blasting over the bootloader. :)  mtd1 is
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
