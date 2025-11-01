==========================================================
sm712fb - Silicon Motion SM712 graphics framebuffer driver
==========================================================

This is a graphics framebuffer driver for Silicon Motion SM712 based processors.

How to use it?
==============

Switching modes is done using the video=sm712fb:... boot parameter.

If you want, for example, enable a resolution of 1280x1024x24bpp you should
pass to the kernel this command line: "video=sm712fb:0x31B".

You should not compile-in vesafb.

Currently supported video modes are:

Graphic modes
-------------

===  =======  =======  ========  =========
bpp  640x480  800x600  1024x768  1280x1024
===  =======  =======  ========  =========
  8  0x301    0x303    0x305     0x307
 16  0x311    0x314    0x317     0x31A
 24  0x312    0x315    0x318     0x31B
===  =======  =======  ========  =========

Missing Features
================
(alias TODO list)

	* 2D acceleration
	* dual-head support
