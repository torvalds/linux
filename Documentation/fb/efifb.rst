==============
What is efifb?
==============

This is a generic EFI platform driver for systems with UEFI firmware. The
system must be booted via the EFI stub for this to be usable. efifb supports
both firmware with Graphics Output Protocol (GOP) displays as well as older
systems with only Universal Graphics Adapter (UGA) displays.

Supported Hardware
==================

- iMac 17"/20"
- Macbook
- Macbook Pro 15"/17"
- MacMini
- ARM/ARM64/X86 systems with UEFI firmware

How to use it?
==============

For UGA displays, efifb does not have any kind of autodetection of your
machine.

You have to add the following kernel parameters in your elilo.conf::

	Macbook :
		video=efifb:macbook
	MacMini :
		video=efifb:mini
	Macbook Pro 15", iMac 17" :
		video=efifb:i17
	Macbook Pro 17", iMac 20" :
		video=efifb:i20

For GOP displays, efifb can autodetect the display's resolution and framebuffer
address, so these should work out of the box without any special parameters.

Accepted options:

======= ===========================================================
nowc	Don't map the framebuffer write combined. This can be used
	to workaround side-effects and slowdowns on other CPU cores
	when large amounts of console data are written.
======= ===========================================================

Options for GOP displays:

mode=n
        The EFI stub will set the mode of the display to mode number n if
        possible.

<xres>x<yres>[-(rgb|bgr|<bpp>)]
        The EFI stub will search for a display mode that matches the specified
        horizontal and vertical resolution, and optionally bit depth, and set
        the mode of the display to it if one is found. The bit depth can either
        "rgb" or "bgr" to match specifically those pixel formats, or a number
        for a mode with matching bits per pixel.

auto
        The EFI stub will choose the mode with the highest resolution (product
        of horizontal and vertical resolution). If there are multiple modes
        with the highest resolution, it will choose one with the highest color
        depth.

list
        The EFI stub will list out all the display modes that are available. A
        specific mode can then be chosen using one of the above options for the
        next boot.

Edgar Hucek <gimli@dark-green.com>
