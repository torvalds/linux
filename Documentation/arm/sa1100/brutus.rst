======
Brutus
======

Brutus is an evaluation platform for the SA1100 manufactured by Intel.
For more details, see:

http://developer.intel.com

To compile for Brutus, you must issue the following commands::

	make brutus_config
	make config
	[accept all the defaults]
	make zImage

The resulting kernel will end up in linux/arch/arm/boot/zImage.  This file
must be loaded at 0xc0008000 in Brutus's memory and execution started at
0xc0008000 as well with the value of registers r0 = 0 and r1 = 16 upon
entry.

But prior to execute the kernel, a ramdisk image must also be loaded in
memory.  Use memory address 0xd8000000 for this.  Note that the file
containing the (compressed) ramdisk image must not exceed 4 MB.

Typically, you'll need angelboot to load the kernel.
The following angelboot.opt file should be used::

	base 0xc0008000
	entry 0xc0008000
	r0 0x00000000
	r1 0x00000010
	device /dev/ttyS0
	options "9600 8N1"
	baud 115200
	otherfile ramdisk_img.gz
	otherbase 0xd8000000

Then load the kernel and ramdisk with::

	angelboot -f angelboot.opt zImage

The first Brutus serial port (assumed to be linked to /dev/ttyS0 on your
host PC) is used by angel to load the kernel and ramdisk image. The serial
console is provided through the second Brutus serial port. To access it,
you may use minicom configured with /dev/ttyS1, 9600 baud, 8N1, no flow
control.

Currently supported
===================

	- RS232 serial ports
	- audio output
	- LCD screen
	- keyboard

The actual Brutus support may not be complete without extra patches.
If such patches exist, they should be found from
ftp.netwinder.org/users/n/nico.

A full PCMCIA support is still missing, although it's possible to hack
some drivers in order to drive already inserted cards at boot time with
little modifications.

Any contribution is welcome.

Please send patches to nico@fluxnic.net

Have Fun !
