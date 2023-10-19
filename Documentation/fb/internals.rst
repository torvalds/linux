=============================
Frame Buffer device internals
=============================

This is a first start for some documentation about frame buffer device
internals.

Authors:

- Geert Uytterhoeven <geert@linux-m68k.org>, 21 July 1998
- James Simmons <jsimmons@user.sf.net>, Nov 26 2002

--------------------------------------------------------------------------------

Structures used by the frame buffer device API
==============================================

The following structures play a role in the game of frame buffer devices. They
are defined in <linux/fb.h>.

1. Outside the kernel (user space)

  - struct fb_fix_screeninfo

    Device independent unchangeable information about a frame buffer device and
    a specific video mode. This can be obtained using the FBIOGET_FSCREENINFO
    ioctl.

  - struct fb_var_screeninfo

    Device independent changeable information about a frame buffer device and a
    specific video mode. This can be obtained using the FBIOGET_VSCREENINFO
    ioctl, and updated with the FBIOPUT_VSCREENINFO ioctl. If you want to pan
    the screen only, you can use the FBIOPAN_DISPLAY ioctl.

  - struct fb_cmap

    Device independent colormap information. You can get and set the colormap
    using the FBIOGETCMAP and FBIOPUTCMAP ioctls.


2. Inside the kernel

  - struct fb_info

    Generic information, API and low level information about a specific frame
    buffer device instance (slot number, board address, ...).

  - struct `par`

    Device dependent information that uniquely defines the video mode for this
    particular piece of hardware.


Visuals used by the frame buffer device API
===========================================


Monochrome (FB_VISUAL_MONO01 and FB_VISUAL_MONO10)
--------------------------------------------------
Each pixel is either black or white.


Pseudo color (FB_VISUAL_PSEUDOCOLOR and FB_VISUAL_STATIC_PSEUDOCOLOR)
---------------------------------------------------------------------
The whole pixel value is fed through a programmable lookup table that has one
color (including red, green, and blue intensities) for each possible pixel
value, and that color is displayed.


True color (FB_VISUAL_TRUECOLOR)
--------------------------------
The pixel value is broken up into red, green, and blue fields.


Direct color (FB_VISUAL_DIRECTCOLOR)
------------------------------------
The pixel value is broken up into red, green, and blue fields, each of which
are looked up in separate red, green, and blue lookup tables.


Grayscale displays
------------------
Grayscale and static grayscale are special variants of pseudo color and static
pseudo color, where the red, green and blue components are always equal to
each other.
