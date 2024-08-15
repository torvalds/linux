Frame Buffer Library
====================

The frame buffer drivers depend heavily on four data structures. These
structures are declared in include/linux/fb.h. They are fb_info,
fb_var_screeninfo, fb_fix_screeninfo and fb_monospecs. The last
three can be made available to and from userland.

fb_info defines the current state of a particular video card. Inside
fb_info, there exists a fb_ops structure which is a collection of
needed functions to make fbdev and fbcon work. fb_info is only visible
to the kernel.

fb_var_screeninfo is used to describe the features of a video card
that are user defined. With fb_var_screeninfo, things such as depth
and the resolution may be defined.

The next structure is fb_fix_screeninfo. This defines the properties
of a card that are created when a mode is set and can't be changed
otherwise. A good example of this is the start of the frame buffer
memory. This "locks" the address of the frame buffer memory, so that it
cannot be changed or moved.

The last structure is fb_monospecs. In the old API, there was little
importance for fb_monospecs. This allowed for forbidden things such as
setting a mode of 800x600 on a fix frequency monitor. With the new API,
fb_monospecs prevents such things, and if used correctly, can prevent a
monitor from being cooked. fb_monospecs will not be useful until
kernels 2.5.x.

Frame Buffer Memory
-------------------

.. kernel-doc:: drivers/video/fbdev/core/fbmem.c
   :export:

Frame Buffer Colormap
---------------------

.. kernel-doc:: drivers/video/fbdev/core/fbcmap.c
   :export:

Frame Buffer Video Mode Database
--------------------------------

.. kernel-doc:: drivers/video/fbdev/core/modedb.c
   :internal:

.. kernel-doc:: drivers/video/fbdev/core/modedb.c
   :export:

Frame Buffer Macintosh Video Mode Database
------------------------------------------

.. kernel-doc:: drivers/video/fbdev/macmodes.c
   :export:

Frame Buffer Fonts
------------------

Refer to the file lib/fonts/fonts.c for more information.

