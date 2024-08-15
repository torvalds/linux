===============================================================
vt8623fb - fbdev driver for graphics core in VIA VT8623 chipset
===============================================================


Supported Hardware
==================

VIA VT8623 [CLE266] chipset and	its graphics core
(known as CastleRock or Unichrome)

I tested vt8623fb on VIA EPIA ML-6000


Supported Features
==================

	*  4 bpp pseudocolor modes (with 18bit palette, two variants)
	*  8 bpp pseudocolor mode (with 18bit palette)
	* 16 bpp truecolor mode (RGB 565)
	* 32 bpp truecolor mode (RGB 888)
	* text mode (activated by bpp = 0)
	* doublescan mode variant (not available in text mode)
	* panning in both directions
	* suspend/resume support
	* DPMS support

Text mode is supported even in higher resolutions, but there is limitation to
lower pixclocks (maximum about 100 MHz). This limitation is not enforced by
driver. Text mode supports 8bit wide fonts only (hardware limitation) and
16bit tall fonts (driver limitation).

There are two 4 bpp modes. First mode (selected if nonstd == 0) is mode with
packed pixels, high nibble first. Second mode (selected if nonstd == 1) is mode
with interleaved planes (1 byte interleave), MSB first. Both modes support
8bit wide fonts only (driver limitation).

Suspend/resume works on systems that initialize video card during resume and
if device is active (for example used by fbcon).


Missing Features
================
(alias TODO list)

	* secondary (not initialized by BIOS) device support
	* MMIO support
	* interlaced mode variant
	* support for fontwidths != 8 in 4 bpp modes
	* support for fontheight != 16 in text mode
	* hardware cursor
	* video overlay support
	* vsync synchronization
	* acceleration support (8514-like 2D, busmaster transfers)


Known bugs
==========

	* cursor disable in text mode doesn't work


--
Ondrej Zajicek <santiago@crfreenet.org>
