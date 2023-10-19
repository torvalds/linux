=================
What is aty128fb?
=================

.. [This file is cloned from VesaFB/matroxfb]

This is a driver for a graphic framebuffer for ATI Rage128 based devices
on Intel and PPC boxes.

Advantages:

 * It provides a nice large console (128 cols + 48 lines with 1024x768)
   without using tiny, unreadable fonts.
 * You can run XF68_FBDev on top of /dev/fb0
 * Most important: boot logo :-)

Disadvantages:

 * graphic mode is slower than text mode... but you should not notice
   if you use same resolution as you used in textmode.
 * still experimental.


How to use it?
==============

Switching modes is done using the  video=aty128fb:<resolution>... modedb
boot parameter or using `fbset` program.

See Documentation/fb/modedb.rst for more information on modedb
resolutions.

You should compile in both vgacon (to boot if you remove your Rage128 from
box) and aty128fb (for graphics mode). You should not compile-in vesafb
unless you have primary display on non-Rage128 VBE2.0 device (see
Documentation/fb/vesafb.rst for details).


X11
===

XF68_FBDev should generally work fine, but it is non-accelerated. As of
this document, 8 and 32bpp works fine.  There have been palette issues
when switching from X to console and back to X.  You will have to restart
X to fix this.


Configuration
=============

You can pass kernel command line options to vesafb with
`video=aty128fb:option1,option2:value2,option3` (multiple options should
be separated by comma, values are separated from options by `:`).
Accepted options:

========= =======================================================
noaccel   do not use acceleration engine. It is default.
accel     use acceleration engine. Not finished.
vmode:x   chooses PowerMacintosh video mode <x>. Deprecated.
cmode:x   chooses PowerMacintosh colour mode <x>. Deprecated.
<XxX@X>   selects startup videomode. See modedb.txt for detailed
	  explanation. Default is 640x480x8bpp.
========= =======================================================


Limitations
===========

There are known and unknown bugs, features and misfeatures.
Currently there are following known bugs:

 - This driver is still experimental and is not finished.  Too many
   bugs/errata to list here.

Brad Douglas <brad@neruo.com>
