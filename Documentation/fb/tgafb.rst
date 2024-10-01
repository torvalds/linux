==============
What is tgafb?
==============

This is a driver for DECChip 21030 based graphics framebuffers, a.k.a. TGA
cards, which are usually found in older Digital Alpha systems. The
following models are supported:

- ZLxP-E1 (8bpp, 2 MB VRAM)
- ZLxP-E2 (32bpp, 8 MB VRAM)
- ZLxP-E3 (32bpp, 16 MB VRAM, Zbuffer)

This version is an almost complete rewrite of the code written by Geert
Uytterhoeven, which was based on the original TGA console code written by
Jay Estabrook.

Major new features since Linux 2.0.x:

 * Support for multiple resolutions
 * Support for fixed-frequency and other oddball monitors
   (by allowing the video mode to be set at boot time)

User-visible changes since Linux 2.2.x:

 * Sync-on-green is now handled properly
 * More useful information is printed on bootup
   (this helps if people run into problems)

This driver does not (yet) support the TGA2 family of framebuffers, so the
PowerStorm 3D30/4D20 (also known as PBXGB) cards are not supported. These
can however be used with the standard VGA Text Console driver.


Configuration
=============

You can pass kernel command line options to tgafb with
`video=tgafb:option1,option2:value2,option3` (multiple options should be
separated by comma, values are separated from options by `:`).

Accepted options:

==========  ============================================================
font:X      default font to use. All fonts are supported, including the
	    SUN12x22 font which is very nice at high resolutions.

mode:X      default video mode. The following video modes are supported:
	    640x480-60, 800x600-56, 640x480-72, 800x600-60, 800x600-72,
	    1024x768-60, 1152x864-60, 1024x768-70, 1024x768-76,
	    1152x864-70, 1280x1024-61, 1024x768-85, 1280x1024-70,
	    1152x864-84, 1280x1024-76, 1280x1024-85
==========  ============================================================


Known Issues
============

The XFree86 FBDev server has been reported not to work, since tgafb doesn't do
mmap(). Running the standard XF86_TGA server from XFree86 3.3.x works fine for
me, however this server does not do acceleration, which make certain operations
quite slow. Support for acceleration is being progressively integrated in
XFree86 4.x.

When running tgafb in resolutions higher than 640x480, on switching VCs from
tgafb to XF86_TGA 3.3.x, the entire screen is not re-drawn and must be manually
refreshed. This is an X server problem, not a tgafb problem, and is fixed in
XFree86 4.0.

Enjoy!

Martin Lucina <mato@kotelna.sk>
