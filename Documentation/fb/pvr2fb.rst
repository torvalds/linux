===============================================
pvr2fb - PowerVR 2 graphics frame buffer driver
===============================================

This is a driver for PowerVR 2 based graphics frame buffers, such as the
one found in the Dreamcast.

Advantages:

 * It provides a nice large console (128 cols + 48 lines with 1024x768)
   without using tiny, unreadable fonts (NOT on the Dreamcast)
 * You can run XF86_FBDev on top of /dev/fb0
 * Most important: boot logo :-)

Disadvantages:

 * Driver is largely untested on non-Dreamcast systems.

Configuration
=============

You can pass kernel command line options to pvr2fb with
`video=pvr2fb:option1,option2:value2,option3` (multiple options should be
separated by comma, values are separated from options by `:`).

Accepted options:

==========  ==================================================================
font:X      default font to use. All fonts are supported, including the
	    SUN12x22 font which is very nice at high resolutions.


mode:X      default video mode with format [xres]x[yres]-<bpp>@<refresh rate>
	    The following video modes are supported:
	    640x640-16@60, 640x480-24@60, 640x480-32@60. The Dreamcast
	    defaults to 640x480-16@60. At the time of writing the
	    24bpp and 32bpp modes function poorly. Work to fix that is
	    ongoing

	    Note: the 640x240 mode is currently broken, and should not be
	    used for any reason. It is only mentioned here as a reference.

inverse     invert colors on screen (for LCD displays)

nomtrr      disables write combining on frame buffer. This slows down driver
	    but there is reported minor incompatibility between GUS DMA and
	    XFree under high loads if write combining is enabled (sound
	    dropouts). MTRR is enabled by default on systems that have it
	    configured and that support it.

cable:X     cable type. This can be any of the following: vga, rgb, and
	    composite. If none is specified, we guess.

output:X    output type. This can be any of the following: pal, ntsc, and
	    vga. If none is specified, we guess.
==========  ==================================================================

X11
===

XF86_FBDev has been shown to work on the Dreamcast in the past - though not yet
on any 2.6 series kernel.

Paul Mundt <lethal@linuxdc.org>

Updated by Adrian McMenamin <adrian@mcmen.demon.co.uk>
