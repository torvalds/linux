/*
 * linux/drivers/video/amifb.c -- Amiga builtin chipset frame buffer device
 *
 *    Copyright (C) 1995-2003 Geert Uytterhoeven
 *
 *          with work by Roman Zippel
 *
 *
 * This file is based on the Atari frame buffer device (atafb.c):
 *
 *    Copyright (C) 1994 Martin Schaller
 *                       Roman Hodek
 *
 *          with work by Andreas Schwab
 *                       Guenther Kelleter
 *
 * and on the original Amiga console driver (amicon.c):
 *
 *    Copyright (C) 1993 Hamish Macdonald
 *                       Greg Harp
 *    Copyright (C) 1994 David Carter [carter@compsci.bristol.ac.uk]
 *
 *          with work by William Rucklidge (wjr@cs.cornell.edu)
 *                       Geert Uytterhoeven
 *                       Jes Sorensen (jds@kom.auc.dk)
 *
 *
 * History:
 *
 *   - 24 Jul 96: Copper generates now vblank interrupt and
 *                VESA Power Saving Protocol is fully implemented
 *   - 14 Jul 96: Rework and hopefully last ECS bugs fixed
 *   -  7 Mar 96: Hardware sprite support by Roman Zippel
 *   - 18 Feb 96: OCS and ECS support by Roman Zippel
 *                Hardware functions completely rewritten
 *   -  2 Dec 95: AGA version by Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>

#include <asm/irq.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/setup.h>

#include "c2p.h"


#define DEBUG

#if !defined(CONFIG_FB_AMIGA_OCS) && !defined(CONFIG_FB_AMIGA_ECS) && !defined(CONFIG_FB_AMIGA_AGA)
#define CONFIG_FB_AMIGA_OCS   /* define at least one fb driver, this will change later */
#endif

#if !defined(CONFIG_FB_AMIGA_OCS)
#  define IS_OCS (0)
#elif defined(CONFIG_FB_AMIGA_ECS) || defined(CONFIG_FB_AMIGA_AGA)
#  define IS_OCS (chipset == TAG_OCS)
#else
#  define CONFIG_FB_AMIGA_OCS_ONLY
#  define IS_OCS (1)
#endif

#if !defined(CONFIG_FB_AMIGA_ECS)
#  define IS_ECS (0)
#elif defined(CONFIG_FB_AMIGA_OCS) || defined(CONFIG_FB_AMIGA_AGA)
#  define IS_ECS (chipset == TAG_ECS)
#else
#  define CONFIG_FB_AMIGA_ECS_ONLY
#  define IS_ECS (1)
#endif

#if !defined(CONFIG_FB_AMIGA_AGA)
#  define IS_AGA (0)
#elif defined(CONFIG_FB_AMIGA_OCS) || defined(CONFIG_FB_AMIGA_ECS)
#  define IS_AGA (chipset == TAG_AGA)
#else
#  define CONFIG_FB_AMIGA_AGA_ONLY
#  define IS_AGA (1)
#endif

#ifdef DEBUG
#  define DPRINTK(fmt, args...)	printk(KERN_DEBUG "%s: " fmt, __func__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif

/*******************************************************************************


   Generic video timings
   ---------------------

   Timings used by the frame buffer interface:

   +----------+---------------------------------------------+----------+-------+
   |          |                ^                            |          |       |
   |          |                |upper_margin                |          |       |
   |          |                v                            |          |       |
   +----------###############################################----------+-------+
   |          #                ^                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |   left   #                |                            #  right   | hsync |
   |  margin  #                |       xres                 #  margin  |  len  |
   |<-------->#<---------------+--------------------------->#<-------->|<----->|
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |yres                        #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                v                            #          |       |
   +----------###############################################----------+-------+
   |          |                ^                            |          |       |
   |          |                |lower_margin                |          |       |
   |          |                v                            |          |       |
   +----------+---------------------------------------------+----------+-------+
   |          |                ^                            |          |       |
   |          |                |vsync_len                   |          |       |
   |          |                v                            |          |       |
   +----------+---------------------------------------------+----------+-------+


   Amiga video timings
   -------------------

   The Amiga native chipsets uses another timing scheme:

      - hsstrt:   Start of horizontal synchronization pulse
      - hsstop:   End of horizontal synchronization pulse
      - htotal:   Last value on the line (i.e. line length = htotal + 1)
      - vsstrt:   Start of vertical synchronization pulse
      - vsstop:   End of vertical synchronization pulse
      - vtotal:   Last line value (i.e. number of lines = vtotal + 1)
      - hcenter:  Start of vertical retrace for interlace

   You can specify the blanking timings independently. Currently I just set
   them equal to the respective synchronization values:

      - hbstrt:   Start of horizontal blank
      - hbstop:   End of horizontal blank
      - vbstrt:   Start of vertical blank
      - vbstop:   End of vertical blank

   Horizontal values are in color clock cycles (280 ns), vertical values are in
   scanlines.

   (0, 0) is somewhere in the upper-left corner :-)


   Amiga visible window definitions
   --------------------------------

   Currently I only have values for AGA, SHRES (28 MHz dotclock). Feel free to
   make corrections and/or additions.

   Within the above synchronization specifications, the visible window is
   defined by the following parameters (actual register resolutions may be
   different; all horizontal values are normalized with respect to the pixel
   clock):

      - diwstrt_h:   Horizontal start of the visible window
      - diwstop_h:   Horizontal stop + 1(*) of the visible window
      - diwstrt_v:   Vertical start of the visible window
      - diwstop_v:   Vertical stop of the visible window
      - ddfstrt:     Horizontal start of display DMA
      - ddfstop:     Horizontal stop of display DMA
      - hscroll:     Horizontal display output delay

   Sprite positioning:

      - sprstrt_h:   Horizontal start - 4 of sprite
      - sprstrt_v:   Vertical start of sprite

   (*) Even Commodore did it wrong in the AGA monitor drivers by not adding 1.

   Horizontal values are in dotclock cycles (35 ns), vertical values are in
   scanlines.

   (0, 0) is somewhere in the upper-left corner :-)


   Dependencies (AGA, SHRES (35 ns dotclock))
   -------------------------------------------

   Since there are much more parameters for the Amiga display than for the
   frame buffer interface, there must be some dependencies among the Amiga
   display parameters. Here's what I found out:

      - ddfstrt and ddfstop are best aligned to 64 pixels.
      - the chipset needs 64 + 4 horizontal pixels after the DMA start before
	the first pixel is output, so diwstrt_h = ddfstrt + 64 + 4 if you want
	to display the first pixel on the line too. Increase diwstrt_h for
	virtual screen panning.
      - the display DMA always fetches 64 pixels at a time (fmode = 3).
      - ddfstop is ddfstrt+#pixels - 64.
      - diwstop_h = diwstrt_h + xres + 1. Because of the additional 1 this can
	be 1 more than htotal.
      - hscroll simply adds a delay to the display output. Smooth horizontal
	panning needs an extra 64 pixels on the left to prefetch the pixels that
	`fall off' on the left.
      - if ddfstrt < 192, the sprite DMA cycles are all stolen by the bitplane
	DMA, so it's best to make the DMA start as late as possible.
      - you really don't want to make ddfstrt < 128, since this will steal DMA
	cycles from the other DMA channels (audio, floppy and Chip RAM refresh).
      - I make diwstop_h and diwstop_v as large as possible.

   General dependencies
   --------------------

      - all values are SHRES pixel (35ns)

		  table 1:fetchstart  table 2:prefetch    table 3:fetchsize
		  ------------------  ----------------    -----------------
   Pixclock     # SHRES|HIRES|LORES # SHRES|HIRES|LORES # SHRES|HIRES|LORES
   -------------#------+-----+------#------+-----+------#------+-----+------
   Bus width 1x #   16 |  32 |  64  #   16 |  32 |  64  #   64 |  64 |  64
   Bus width 2x #   32 |  64 | 128  #   32 |  64 |  64  #   64 |  64 | 128
   Bus width 4x #   64 | 128 | 256  #   64 |  64 |  64  #   64 | 128 | 256

      - chipset needs 4 pixels before the first pixel is output
      - ddfstrt must be aligned to fetchstart (table 1)
      - chipset needs also prefetch (table 2) to get first pixel data, so
	ddfstrt = ((diwstrt_h - 4) & -fetchstart) - prefetch
      - for horizontal panning decrease diwstrt_h
      - the length of a fetchline must be aligned to fetchsize (table 3)
      - if fetchstart is smaller than fetchsize, then ddfstrt can a little bit
	moved to optimize use of dma (useful for OCS/ECS overscan displays)
      - ddfstop is ddfstrt + ddfsize - fetchsize
      - If C= didn't change anything for AGA, then at following positions the
	dma bus is already used:
	ddfstrt <  48 -> memory refresh
		<  96 -> disk dma
		< 160 -> audio dma
		< 192 -> sprite 0 dma
		< 416 -> sprite dma (32 per sprite)
      - in accordance with the hardware reference manual a hardware stop is at
	192, but AGA (ECS?) can go below this.

   DMA priorities
   --------------

   Since there are limits on the earliest start value for display DMA and the
   display of sprites, I use the following policy on horizontal panning and
   the hardware cursor:

      - if you want to start display DMA too early, you lose the ability to
	do smooth horizontal panning (xpanstep 1 -> 64).
      - if you want to go even further, you lose the hardware cursor too.

   IMHO a hardware cursor is more important for X than horizontal scrolling,
   so that's my motivation.


   Implementation
   --------------

   ami_decode_var() converts the frame buffer values to the Amiga values. It's
   just a `straightforward' implementation of the above rules.


   Standard VGA timings
   --------------------

	       xres  yres    left  right  upper  lower    hsync    vsync
	       ----  ----    ----  -----  -----  -----    -----    -----
      80x25     720   400      27     45     35     12      108        2
      80x30     720   480      27     45     30      9      108        2

   These were taken from a XFree86 configuration file, recalculated for a 28 MHz
   dotclock (Amigas don't have a 25 MHz dotclock) and converted to frame buffer
   generic timings.

   As a comparison, graphics/monitor.h suggests the following:

	       xres  yres    left  right  upper  lower    hsync    vsync
	       ----  ----    ----  -----  -----  -----    -----    -----

      VGA       640   480      52    112     24     19    112 -      2 +
      VGA70     640   400      52    112     27     21    112 -      2 -


   Sync polarities
   ---------------

      VSYNC    HSYNC    Vertical size    Vertical total
      -----    -----    -------------    --------------
	+        +           Reserved          Reserved
	+        -                400               414
	-        +                350               362
	-        -                480               496

   Source: CL-GD542X Technical Reference Manual, Cirrus Logic, Oct 1992


   Broadcast video timings
   -----------------------

   According to the CCIR and RETMA specifications, we have the following values:

   CCIR -> PAL
   -----------

      - a scanline is 64 µs long, of which 52.48 µs are visible. This is about
	736 visible 70 ns pixels per line.
      - we have 625 scanlines, of which 575 are visible (interlaced); after
	rounding this becomes 576.

   RETMA -> NTSC
   -------------

      - a scanline is 63.5 µs long, of which 53.5 µs are visible.  This is about
	736 visible 70 ns pixels per line.
      - we have 525 scanlines, of which 485 are visible (interlaced); after
	rounding this becomes 484.

   Thus if you want a PAL compatible display, you have to do the following:

      - set the FB_SYNC_BROADCAST flag to indicate that standard broadcast
	timings are to be used.
      - make sure upper_margin + yres + lower_margin + vsync_len = 625 for an
	interlaced, 312 for a non-interlaced and 156 for a doublescanned
	display.
      - make sure left_margin + xres + right_margin + hsync_len = 1816 for a
	SHRES, 908 for a HIRES and 454 for a LORES display.
      - the left visible part begins at 360 (SHRES; HIRES:180, LORES:90),
	left_margin + 2 * hsync_len must be greater or equal.
      - the upper visible part begins at 48 (interlaced; non-interlaced:24,
	doublescanned:12), upper_margin + 2 * vsync_len must be greater or
	equal.
      - ami_encode_var() calculates margins with a hsync of 5320 ns and a vsync
	of 4 scanlines

   The settings for a NTSC compatible display are straightforward.

   Note that in a strict sense the PAL and NTSC standards only define the
   encoding of the color part (chrominance) of the video signal and don't say
   anything about horizontal/vertical synchronization nor refresh rates.


							    -- Geert --

*******************************************************************************/


	/*
	 * Custom Chipset Definitions
	 */

#define CUSTOM_OFS(fld) ((long)&((struct CUSTOM*)0)->fld)

	/*
	 * BPLCON0 -- Bitplane Control Register 0
	 */

#define BPC0_HIRES	(0x8000)
#define BPC0_BPU2	(0x4000) /* Bit plane used count */
#define BPC0_BPU1	(0x2000)
#define BPC0_BPU0	(0x1000)
#define BPC0_HAM	(0x0800) /* HAM mode */
#define BPC0_DPF	(0x0400) /* Double playfield */
#define BPC0_COLOR	(0x0200) /* Enable colorburst */
#define BPC0_GAUD	(0x0100) /* Genlock audio enable */
#define BPC0_UHRES	(0x0080) /* Ultrahi res enable */
#define BPC0_SHRES	(0x0040) /* Super hi res mode */
#define BPC0_BYPASS	(0x0020) /* Bypass LUT - AGA */
#define BPC0_BPU3	(0x0010) /* AGA */
#define BPC0_LPEN	(0x0008) /* Light pen enable */
#define BPC0_LACE	(0x0004) /* Interlace */
#define BPC0_ERSY	(0x0002) /* External resync */
#define BPC0_ECSENA	(0x0001) /* ECS enable */

	/*
	 * BPLCON2 -- Bitplane Control Register 2
	 */

#define BPC2_ZDBPSEL2	(0x4000) /* Bitplane to be used for ZD - AGA */
#define BPC2_ZDBPSEL1	(0x2000)
#define BPC2_ZDBPSEL0	(0x1000)
#define BPC2_ZDBPEN	(0x0800) /* Enable ZD with ZDBPSELx - AGA */
#define BPC2_ZDCTEN	(0x0400) /* Enable ZD with palette bit #31 - AGA */
#define BPC2_KILLEHB	(0x0200) /* Kill EHB mode - AGA */
#define BPC2_RDRAM	(0x0100) /* Color table accesses read, not write - AGA */
#define BPC2_SOGEN	(0x0080) /* SOG output pin high - AGA */
#define BPC2_PF2PRI	(0x0040) /* PF2 priority over PF1 */
#define BPC2_PF2P2	(0x0020) /* PF2 priority wrt sprites */
#define BPC2_PF2P1	(0x0010)
#define BPC2_PF2P0	(0x0008)
#define BPC2_PF1P2	(0x0004) /* ditto PF1 */
#define BPC2_PF1P1	(0x0002)
#define BPC2_PF1P0	(0x0001)

	/*
	 * BPLCON3 -- Bitplane Control Register 3 (AGA)
	 */

#define BPC3_BANK2	(0x8000) /* Bits to select color register bank */
#define BPC3_BANK1	(0x4000)
#define BPC3_BANK0	(0x2000)
#define BPC3_PF2OF2	(0x1000) /* Bits for color table offset when PF2 */
#define BPC3_PF2OF1	(0x0800)
#define BPC3_PF2OF0	(0x0400)
#define BPC3_LOCT	(0x0200) /* Color register writes go to low bits */
#define BPC3_SPRES1	(0x0080) /* Sprite resolution bits */
#define BPC3_SPRES0	(0x0040)
#define BPC3_BRDRBLNK	(0x0020) /* Border blanked? */
#define BPC3_BRDRTRAN	(0x0010) /* Border transparent? */
#define BPC3_ZDCLKEN	(0x0004) /* ZD pin is 14 MHz (HIRES) clock output */
#define BPC3_BRDRSPRT	(0x0002) /* Sprites in border? */
#define BPC3_EXTBLKEN	(0x0001) /* BLANK programmable */

	/*
	 * BPLCON4 -- Bitplane Control Register 4 (AGA)
	 */

#define BPC4_BPLAM7	(0x8000) /* bitplane color XOR field */
#define BPC4_BPLAM6	(0x4000)
#define BPC4_BPLAM5	(0x2000)
#define BPC4_BPLAM4	(0x1000)
#define BPC4_BPLAM3	(0x0800)
#define BPC4_BPLAM2	(0x0400)
#define BPC4_BPLAM1	(0x0200)
#define BPC4_BPLAM0	(0x0100)
#define BPC4_ESPRM7	(0x0080) /* 4 high bits for even sprite colors */
#define BPC4_ESPRM6	(0x0040)
#define BPC4_ESPRM5	(0x0020)
#define BPC4_ESPRM4	(0x0010)
#define BPC4_OSPRM7	(0x0008) /* 4 high bits for odd sprite colors */
#define BPC4_OSPRM6	(0x0004)
#define BPC4_OSPRM5	(0x0002)
#define BPC4_OSPRM4	(0x0001)

	/*
	 * BEAMCON0 -- Beam Control Register
	 */

#define BMC0_HARDDIS	(0x4000) /* Disable hardware limits */
#define BMC0_LPENDIS	(0x2000) /* Disable light pen latch */
#define BMC0_VARVBEN	(0x1000) /* Enable variable vertical blank */
#define BMC0_LOLDIS	(0x0800) /* Disable long/short line toggle */
#define BMC0_CSCBEN	(0x0400) /* Composite sync/blank */
#define BMC0_VARVSYEN	(0x0200) /* Enable variable vertical sync */
#define BMC0_VARHSYEN	(0x0100) /* Enable variable horizontal sync */
#define BMC0_VARBEAMEN	(0x0080) /* Enable variable beam counters */
#define BMC0_DUAL	(0x0040) /* Enable alternate horizontal beam counter */
#define BMC0_PAL	(0x0020) /* Set decodes for PAL */
#define BMC0_VARCSYEN	(0x0010) /* Enable variable composite sync */
#define BMC0_BLANKEN	(0x0008) /* Blank enable (no longer used on AGA) */
#define BMC0_CSYTRUE	(0x0004) /* CSY polarity */
#define BMC0_VSYTRUE	(0x0002) /* VSY polarity */
#define BMC0_HSYTRUE	(0x0001) /* HSY polarity */


	/*
	 * FMODE -- Fetch Mode Control Register (AGA)
	 */

#define FMODE_SSCAN2	(0x8000) /* Sprite scan-doubling */
#define FMODE_BSCAN2	(0x4000) /* Use PF2 modulus every other line */
#define FMODE_SPAGEM	(0x0008) /* Sprite page mode */
#define FMODE_SPR32	(0x0004) /* Sprite 32 bit fetch */
#define FMODE_BPAGEM	(0x0002) /* Bitplane page mode */
#define FMODE_BPL32	(0x0001) /* Bitplane 32 bit fetch */

	/*
	 * Tags used to indicate a specific Pixel Clock
	 *
	 * clk_shift is the shift value to get the timings in 35 ns units
	 */

enum { TAG_SHRES, TAG_HIRES, TAG_LORES };

	/*
	 * Tags used to indicate the specific chipset
	 */

enum { TAG_OCS, TAG_ECS, TAG_AGA };

	/*
	 * Tags used to indicate the memory bandwidth
	 */

enum { TAG_FMODE_1, TAG_FMODE_2, TAG_FMODE_4 };


	/*
	 * Clock Definitions, Maximum Display Depth
	 *
	 * These depend on the E-Clock or the Chipset, so they are filled in
	 * dynamically
	 */

static u_long pixclock[3];	/* SHRES/HIRES/LORES: index = clk_shift */
static u_short maxdepth[3];	/* SHRES/HIRES/LORES: index = clk_shift */
static u_short maxfmode, chipset;


	/*
	 * Broadcast Video Timings
	 *
	 * Horizontal values are in 35 ns (SHRES) units
	 * Vertical values are in interlaced scanlines
	 */

#define PAL_DIWSTRT_H	(360)	/* PAL Window Limits */
#define PAL_DIWSTRT_V	(48)
#define PAL_HTOTAL	(1816)
#define PAL_VTOTAL	(625)

#define NTSC_DIWSTRT_H	(360)	/* NTSC Window Limits */
#define NTSC_DIWSTRT_V	(40)
#define NTSC_HTOTAL	(1816)
#define NTSC_VTOTAL	(525)


	/*
	 * Various macros
	 */

#define up2(v)		(((v) + 1) & -2)
#define down2(v)	((v) & -2)
#define div2(v)		((v)>>1)
#define mod2(v)		((v) & 1)

#define up4(v)		(((v) + 3) & -4)
#define down4(v)	((v) & -4)
#define mul4(v)		((v) << 2)
#define div4(v)		((v)>>2)
#define mod4(v)		((v) & 3)

#define up8(v)		(((v) + 7) & -8)
#define down8(v)	((v) & -8)
#define div8(v)		((v)>>3)
#define mod8(v)		((v) & 7)

#define up16(v)		(((v) + 15) & -16)
#define down16(v)	((v) & -16)
#define div16(v)	((v)>>4)
#define mod16(v)	((v) & 15)

#define up32(v)		(((v) + 31) & -32)
#define down32(v)	((v) & -32)
#define div32(v)	((v)>>5)
#define mod32(v)	((v) & 31)

#define up64(v)		(((v) + 63) & -64)
#define down64(v)	((v) & -64)
#define div64(v)	((v)>>6)
#define mod64(v)	((v) & 63)

#define upx(x, v)	(((v) + (x) - 1) & -(x))
#define downx(x, v)	((v) & -(x))
#define modx(x, v)	((v) & ((x) - 1))

/* if x1 is not a constant, this macro won't make real sense :-) */
#ifdef __mc68000__
#define DIVUL(x1, x2) ({int res; asm("divul %1,%2,%3": "=d" (res): \
	"d" (x2), "d" ((long)((x1) / 0x100000000ULL)), "0" ((long)(x1))); res;})
#else
/* We know a bit about the numbers, so we can do it this way */
#define DIVUL(x1, x2) ((((long)((unsigned long long)x1 >> 8) / x2) << 8) + \
	((((long)((unsigned long long)x1 >> 8) % x2) << 8) / x2))
#endif

#define highw(x)	((u_long)(x)>>16 & 0xffff)
#define loww(x)		((u_long)(x) & 0xffff)

#define custom		amiga_custom

#define VBlankOn()	custom.intena = IF_SETCLR|IF_COPER
#define VBlankOff()	custom.intena = IF_COPER


	/*
	 * Chip RAM we reserve for the Frame Buffer
	 *
	 * This defines the Maximum Virtual Screen Size
	 * (Setable per kernel options?)
	 */

#define VIDEOMEMSIZE_AGA_2M	(1310720) /* AGA (2MB) : max 1280*1024*256  */
#define VIDEOMEMSIZE_AGA_1M	(786432)  /* AGA (1MB) : max 1024*768*256   */
#define VIDEOMEMSIZE_ECS_2M	(655360)  /* ECS (2MB) : max 1280*1024*16   */
#define VIDEOMEMSIZE_ECS_1M	(393216)  /* ECS (1MB) : max 1024*768*16    */
#define VIDEOMEMSIZE_OCS	(262144)  /* OCS       : max ca. 800*600*16 */

#define SPRITEMEMSIZE		(64 * 64 / 4) /* max 64*64*4 */
#define DUMMYSPRITEMEMSIZE	(8)
static u_long spritememory;

#define CHIPRAM_SAFETY_LIMIT	(16384)

static u_long videomemory;

	/*
	 * This is the earliest allowed start of fetching display data.
	 * Only if you really want no hardware cursor and audio,
	 * set this to 128, but let it better at 192
	 */

static u_long min_fstrt = 192;

#define assignchunk(name, type, ptr, size) \
{ \
	(name) = (type)(ptr); \
	ptr += size; \
}


	/*
	 * Copper Instructions
	 */

#define CMOVE(val, reg)		(CUSTOM_OFS(reg) << 16 | (val))
#define CMOVE2(val, reg)	((CUSTOM_OFS(reg) + 2) << 16 | (val))
#define CWAIT(x, y)		(((y) & 0x1fe) << 23 | ((x) & 0x7f0) << 13 | 0x0001fffe)
#define CEND			(0xfffffffe)


typedef union {
	u_long l;
	u_short w[2];
} copins;

static struct copdisplay {
	copins *init;
	copins *wait;
	copins *list[2][2];
	copins *rebuild[2];
} copdisplay;

static u_short currentcop = 0;

	/*
	 * Hardware Cursor API Definitions
	 * These used to be in linux/fb.h, but were preliminary and used by
	 * amifb only anyway
	 */

#define FBIOGET_FCURSORINFO     0x4607
#define FBIOGET_VCURSORINFO     0x4608
#define FBIOPUT_VCURSORINFO     0x4609
#define FBIOGET_CURSORSTATE     0x460A
#define FBIOPUT_CURSORSTATE     0x460B


struct fb_fix_cursorinfo {
	__u16 crsr_width;		/* width and height of the cursor in */
	__u16 crsr_height;		/* pixels (zero if no cursor)	*/
	__u16 crsr_xsize;		/* cursor size in display pixels */
	__u16 crsr_ysize;
	__u16 crsr_color1;		/* colormap entry for cursor color1 */
	__u16 crsr_color2;		/* colormap entry for cursor color2 */
};

struct fb_var_cursorinfo {
	__u16 width;
	__u16 height;
	__u16 xspot;
	__u16 yspot;
	__u8 data[1];			/* field with [height][width]        */
};

struct fb_cursorstate {
	__s16 xoffset;
	__s16 yoffset;
	__u16 mode;
};

#define FB_CURSOR_OFF		0
#define FB_CURSOR_ON		1
#define FB_CURSOR_FLASH		2


	/*
	 * Hardware Cursor
	 */

static int cursorrate = 20;	/* Number of frames/flash toggle */
static u_short cursorstate = -1;
static u_short cursormode = FB_CURSOR_OFF;

static u_short *lofsprite, *shfsprite, *dummysprite;

	/*
	 * Current Video Mode
	 */

struct amifb_par {

	/* General Values */

	int xres;		/* vmode */
	int yres;		/* vmode */
	int vxres;		/* vmode */
	int vyres;		/* vmode */
	int xoffset;		/* vmode */
	int yoffset;		/* vmode */
	u_short bpp;		/* vmode */
	u_short clk_shift;	/* vmode */
	u_short line_shift;	/* vmode */
	int vmode;		/* vmode */
	u_short diwstrt_h;	/* vmode */
	u_short diwstop_h;	/* vmode */
	u_short diwstrt_v;	/* vmode */
	u_short diwstop_v;	/* vmode */
	u_long next_line;	/* modulo for next line */
	u_long next_plane;	/* modulo for next plane */

	/* Cursor Values */

	struct {
		short crsr_x;	/* movecursor */
		short crsr_y;	/* movecursor */
		short spot_x;
		short spot_y;
		u_short height;
		u_short width;
		u_short fmode;
	} crsr;

	/* OCS Hardware Registers */

	u_long bplpt0;		/* vmode, pan (Note: physical address) */
	u_long bplpt0wrap;	/* vmode, pan (Note: physical address) */
	u_short ddfstrt;
	u_short ddfstop;
	u_short bpl1mod;
	u_short bpl2mod;
	u_short bplcon0;	/* vmode */
	u_short bplcon1;	/* vmode */
	u_short htotal;		/* vmode */
	u_short vtotal;		/* vmode */

	/* Additional ECS Hardware Registers */

	u_short bplcon3;	/* vmode */
	u_short beamcon0;	/* vmode */
	u_short hsstrt;		/* vmode */
	u_short hsstop;		/* vmode */
	u_short hbstrt;		/* vmode */
	u_short hbstop;		/* vmode */
	u_short vsstrt;		/* vmode */
	u_short vsstop;		/* vmode */
	u_short vbstrt;		/* vmode */
	u_short vbstop;		/* vmode */
	u_short hcenter;	/* vmode */

	/* Additional AGA Hardware Registers */

	u_short fmode;		/* vmode */
};


	/*
	 *  Saved color entry 0 so we can restore it when unblanking
	 */

static u_char red0, green0, blue0;


#if defined(CONFIG_FB_AMIGA_ECS)
static u_short ecs_palette[32];
#endif


	/*
	 * Latches for Display Changes during VBlank
	 */

static u_short do_vmode_full = 0;	/* Change the Video Mode */
static u_short do_vmode_pan = 0;	/* Update the Video Mode */
static short do_blank = 0;		/* (Un)Blank the Screen (±1) */
static u_short do_cursor = 0;		/* Move the Cursor */


	/*
	 * Various Flags
	 */

static u_short is_blanked = 0;		/* Screen is Blanked */
static u_short is_lace = 0;		/* Screen is laced */

	/*
	 * Predefined Video Modes
	 *
	 */

static struct fb_videomode ami_modedb[] __initdata = {

	/*
	 *  AmigaOS Video Modes
	 *
	 *  If you change these, make sure to update DEFMODE_* as well!
	 */

	{
		/* 640x200, 15 kHz, 60 Hz (NTSC) */
		"ntsc", 60, 640, 200, TAG_HIRES, 106, 86, 44, 16, 76, 2,
		FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x400, 15 kHz, 60 Hz interlaced (NTSC) */
		"ntsc-lace", 60, 640, 400, TAG_HIRES, 106, 86, 88, 33, 76, 4,
		FB_SYNC_BROADCAST, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x256, 15 kHz, 50 Hz (PAL) */
		"pal", 50, 640, 256, TAG_HIRES, 106, 86, 40, 14, 76, 2,
		FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x512, 15 kHz, 50 Hz interlaced (PAL) */
		"pal-lace", 50, 640, 512, TAG_HIRES, 106, 86, 80, 29, 76, 4,
		FB_SYNC_BROADCAST, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x480, 29 kHz, 57 Hz */
		"multiscan", 57, 640, 480, TAG_SHRES, 96, 112, 29, 8, 72, 8,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x960, 29 kHz, 57 Hz interlaced */
		"multiscan-lace", 57, 640, 960, TAG_SHRES, 96, 112, 58, 16, 72,
		16,
		0, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x200, 15 kHz, 72 Hz */
		"euro36", 72, 640, 200, TAG_HIRES, 92, 124, 6, 6, 52, 5,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x400, 15 kHz, 72 Hz interlaced */
		"euro36-lace", 72, 640, 400, TAG_HIRES, 92, 124, 12, 12, 52,
		10,
		0, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x400, 29 kHz, 68 Hz */
		"euro72", 68, 640, 400, TAG_SHRES, 164, 92, 9, 9, 80, 8,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x800, 29 kHz, 68 Hz interlaced */
		"euro72-lace", 68, 640, 800, TAG_SHRES, 164, 92, 18, 18, 80,
		16,
		0, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* 800x300, 23 kHz, 70 Hz */
		"super72", 70, 800, 300, TAG_SHRES, 212, 140, 10, 11, 80, 7,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* 800x600, 23 kHz, 70 Hz interlaced */
		"super72-lace", 70, 800, 600, TAG_SHRES, 212, 140, 20, 22, 80,
		14,
		0, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x200, 27 kHz, 57 Hz doublescan */
		"dblntsc", 57, 640, 200, TAG_SHRES, 196, 124, 18, 17, 80, 4,
		0, FB_VMODE_DOUBLE | FB_VMODE_YWRAP
	}, {
		/* 640x400, 27 kHz, 57 Hz */
		"dblntsc-ff", 57, 640, 400, TAG_SHRES, 196, 124, 36, 35, 80, 7,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x800, 27 kHz, 57 Hz interlaced */
		"dblntsc-lace", 57, 640, 800, TAG_SHRES, 196, 124, 72, 70, 80,
		14,
		0, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x256, 27 kHz, 47 Hz doublescan */
		"dblpal", 47, 640, 256, TAG_SHRES, 196, 124, 14, 13, 80, 4,
		0, FB_VMODE_DOUBLE | FB_VMODE_YWRAP
	}, {
		/* 640x512, 27 kHz, 47 Hz */
		"dblpal-ff", 47, 640, 512, TAG_SHRES, 196, 124, 28, 27, 80, 7,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x1024, 27 kHz, 47 Hz interlaced */
		"dblpal-lace", 47, 640, 1024, TAG_SHRES, 196, 124, 56, 54, 80,
		14,
		0, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	},

	/*
	 *  VGA Video Modes
	 */

	{
		/* 640x480, 31 kHz, 60 Hz (VGA) */
		"vga", 60, 640, 480, TAG_SHRES, 64, 96, 30, 9, 112, 2,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* 640x400, 31 kHz, 70 Hz (VGA) */
		"vga70", 70, 640, 400, TAG_SHRES, 64, 96, 35, 12, 112, 2,
		FB_SYNC_VERT_HIGH_ACT | FB_SYNC_COMP_HIGH_ACT,
		FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	},

#if 0

	/*
	 *  A2024 video modes
	 *  These modes don't work yet because there's no A2024 driver.
	 */

	{
		/* 1024x800, 10 Hz */
		"a2024-10", 10, 1024, 800, TAG_HIRES, 0, 0, 0, 0, 0, 0,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* 1024x800, 15 Hz */
		"a2024-15", 15, 1024, 800, TAG_HIRES, 0, 0, 0, 0, 0, 0,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}
#endif
};

#define NUM_TOTAL_MODES  ARRAY_SIZE(ami_modedb)

static char *mode_option __initdata = NULL;
static int round_down_bpp = 1;	/* for mode probing */

	/*
	 * Some default modes
	 */


#define DEFMODE_PAL	    2	/* "pal" for PAL OCS/ECS */
#define DEFMODE_NTSC	    0	/* "ntsc" for NTSC OCS/ECS */
#define DEFMODE_AMBER_PAL   3	/* "pal-lace" for flicker fixed PAL (A3000) */
#define DEFMODE_AMBER_NTSC  1	/* "ntsc-lace" for flicker fixed NTSC (A3000) */
#define DEFMODE_AGA	    19	/* "vga70" for AGA */


static int amifb_ilbm = 0;	/* interleaved or normal bitplanes */

static u32 amifb_hfmin __initdata;	/* monitor hfreq lower limit (Hz) */
static u32 amifb_hfmax __initdata;	/* monitor hfreq upper limit (Hz) */
static u16 amifb_vfmin __initdata;	/* monitor vfreq lower limit (Hz) */
static u16 amifb_vfmax __initdata;	/* monitor vfreq upper limit (Hz) */


	/*
	 * Macros for the conversion from real world values to hardware register
	 * values
	 *
	 * This helps us to keep our attention on the real stuff...
	 *
	 * Hardware limits for AGA:
	 *
	 *	parameter  min    max  step
	 *	---------  ---   ----  ----
	 *	diwstrt_h    0   2047     1
	 *	diwstrt_v    0   2047     1
	 *	diwstop_h    0   4095     1
	 *	diwstop_v    0   4095     1
	 *
	 *	ddfstrt      0   2032    16
	 *	ddfstop      0   2032    16
	 *
	 *	htotal       8   2048     8
	 *	hsstrt       0   2040     8
	 *	hsstop       0   2040     8
	 *	vtotal       1   4096     1
	 *	vsstrt       0   4095     1
	 *	vsstop       0   4095     1
	 *	hcenter      0   2040     8
	 *
	 *	hbstrt       0   2047     1
	 *	hbstop       0   2047     1
	 *	vbstrt       0   4095     1
	 *	vbstop       0   4095     1
	 *
	 * Horizontal values are in 35 ns (SHRES) pixels
	 * Vertical values are in half scanlines
	 */

/* bplcon1 (smooth scrolling) */

#define hscroll2hw(hscroll) \
	(((hscroll) << 12 & 0x3000) | ((hscroll) << 8 & 0xc300) | \
	 ((hscroll) << 4 & 0x0c00) | ((hscroll) << 2 & 0x00f0) | \
	 ((hscroll)>>2 & 0x000f))

/* diwstrt/diwstop/diwhigh (visible display window) */

#define diwstrt2hw(diwstrt_h, diwstrt_v) \
	(((diwstrt_v) << 7 & 0xff00) | ((diwstrt_h)>>2 & 0x00ff))
#define diwstop2hw(diwstop_h, diwstop_v) \
	(((diwstop_v) << 7 & 0xff00) | ((diwstop_h)>>2 & 0x00ff))
#define diwhigh2hw(diwstrt_h, diwstrt_v, diwstop_h, diwstop_v) \
	(((diwstop_h) << 3 & 0x2000) | ((diwstop_h) << 11 & 0x1800) | \
	 ((diwstop_v)>>1 & 0x0700) | ((diwstrt_h)>>5 & 0x0020) | \
	 ((diwstrt_h) << 3 & 0x0018) | ((diwstrt_v)>>9 & 0x0007))

/* ddfstrt/ddfstop (display DMA) */

#define ddfstrt2hw(ddfstrt)	div8(ddfstrt)
#define ddfstop2hw(ddfstop)	div8(ddfstop)

/* hsstrt/hsstop/htotal/vsstrt/vsstop/vtotal/hcenter (sync timings) */

#define hsstrt2hw(hsstrt)	(div8(hsstrt))
#define hsstop2hw(hsstop)	(div8(hsstop))
#define htotal2hw(htotal)	(div8(htotal) - 1)
#define vsstrt2hw(vsstrt)	(div2(vsstrt))
#define vsstop2hw(vsstop)	(div2(vsstop))
#define vtotal2hw(vtotal)	(div2(vtotal) - 1)
#define hcenter2hw(htotal)	(div8(htotal))

/* hbstrt/hbstop/vbstrt/vbstop (blanking timings) */

#define hbstrt2hw(hbstrt)	(((hbstrt) << 8 & 0x0700) | ((hbstrt)>>3 & 0x00ff))
#define hbstop2hw(hbstop)	(((hbstop) << 8 & 0x0700) | ((hbstop)>>3 & 0x00ff))
#define vbstrt2hw(vbstrt)	(div2(vbstrt))
#define vbstop2hw(vbstop)	(div2(vbstop))

/* colour */

#define rgb2hw8_high(red, green, blue) \
	(((red & 0xf0) << 4) | (green & 0xf0) | ((blue & 0xf0)>>4))
#define rgb2hw8_low(red, green, blue) \
	(((red & 0x0f) << 8) | ((green & 0x0f) << 4) | (blue & 0x0f))
#define rgb2hw4(red, green, blue) \
	(((red & 0xf0) << 4) | (green & 0xf0) | ((blue & 0xf0)>>4))
#define rgb2hw2(red, green, blue) \
	(((red & 0xc0) << 4) | (green & 0xc0) | ((blue & 0xc0)>>4))

/* sprpos/sprctl (sprite positioning) */

#define spr2hw_pos(start_v, start_h) \
	(((start_v) << 7 & 0xff00) | ((start_h)>>3 & 0x00ff))
#define spr2hw_ctl(start_v, start_h, stop_v) \
	(((stop_v) << 7 & 0xff00) | ((start_v)>>4 & 0x0040) | \
	 ((stop_v)>>5 & 0x0020) | ((start_h) << 3 & 0x0018) | \
	 ((start_v)>>7 & 0x0004) | ((stop_v)>>8 & 0x0002) | \
	 ((start_h)>>2 & 0x0001))

/* get current vertical position of beam */
#define get_vbpos()	((u_short)((*(u_long volatile *)&custom.vposr >> 7) & 0xffe))

	/*
	 * Copper Initialisation List
	 */

#define COPINITSIZE (sizeof(copins) * 40)

enum {
	cip_bplcon0
};

	/*
	 * Long Frame/Short Frame Copper List
	 * Don't change the order, build_copper()/rebuild_copper() rely on this
	 */

#define COPLISTSIZE (sizeof(copins) * 64)

enum {
	cop_wait, cop_bplcon0,
	cop_spr0ptrh, cop_spr0ptrl,
	cop_diwstrt, cop_diwstop,
	cop_diwhigh,
};

	/*
	 * Pixel modes for Bitplanes and Sprites
	 */

static u_short bplpixmode[3] = {
	BPC0_SHRES,			/*  35 ns */
	BPC0_HIRES,			/*  70 ns */
	0				/* 140 ns */
};

static u_short sprpixmode[3] = {
	BPC3_SPRES1 | BPC3_SPRES0,	/*  35 ns */
	BPC3_SPRES1,			/*  70 ns */
	BPC3_SPRES0			/* 140 ns */
};

	/*
	 * Fetch modes for Bitplanes and Sprites
	 */

static u_short bplfetchmode[3] = {
	0,				/* 1x */
	FMODE_BPL32,			/* 2x */
	FMODE_BPAGEM | FMODE_BPL32	/* 4x */
};

static u_short sprfetchmode[3] = {
	0,				/* 1x */
	FMODE_SPR32,			/* 2x */
	FMODE_SPAGEM | FMODE_SPR32	/* 4x */
};


/* --------------------------- Hardware routines --------------------------- */

	/*
	 * Get the video params out of `var'. If a value doesn't fit, round
	 * it up, if it's too big, return -EINVAL.
	 */

static int ami_decode_var(struct fb_var_screeninfo *var, struct amifb_par *par,
			  const struct fb_info *info)
{
	u_short clk_shift, line_shift;
	u_long maxfetchstop, fstrt, fsize, fconst, xres_n, yres_n;
	u_int htotal, vtotal;

	/*
	 * Find a matching Pixel Clock
	 */

	for (clk_shift = TAG_SHRES; clk_shift <= TAG_LORES; clk_shift++)
		if (var->pixclock <= pixclock[clk_shift])
			break;
	if (clk_shift > TAG_LORES) {
		DPRINTK("pixclock too high\n");
		return -EINVAL;
	}
	par->clk_shift = clk_shift;

	/*
	 * Check the Geometry Values
	 */

	if ((par->xres = var->xres) < 64)
		par->xres = 64;
	if ((par->yres = var->yres) < 64)
		par->yres = 64;
	if ((par->vxres = var->xres_virtual) < par->xres)
		par->vxres = par->xres;
	if ((par->vyres = var->yres_virtual) < par->yres)
		par->vyres = par->yres;

	par->bpp = var->bits_per_pixel;
	if (!var->nonstd) {
		if (par->bpp < 1)
			par->bpp = 1;
		if (par->bpp > maxdepth[clk_shift]) {
			if (round_down_bpp && maxdepth[clk_shift])
				par->bpp = maxdepth[clk_shift];
			else {
				DPRINTK("invalid bpp\n");
				return -EINVAL;
			}
		}
	} else if (var->nonstd == FB_NONSTD_HAM) {
		if (par->bpp < 6)
			par->bpp = 6;
		if (par->bpp != 6) {
			if (par->bpp < 8)
				par->bpp = 8;
			if (par->bpp != 8 || !IS_AGA) {
				DPRINTK("invalid bpp for ham mode\n");
				return -EINVAL;
			}
		}
	} else {
		DPRINTK("unknown nonstd mode\n");
		return -EINVAL;
	}

	/*
	 * FB_VMODE_SMOOTH_XPAN will be cleared, if one of the following
	 * checks failed and smooth scrolling is not possible
	 */

	par->vmode = var->vmode | FB_VMODE_SMOOTH_XPAN;
	switch (par->vmode & FB_VMODE_MASK) {
	case FB_VMODE_INTERLACED:
		line_shift = 0;
		break;
	case FB_VMODE_NONINTERLACED:
		line_shift = 1;
		break;
	case FB_VMODE_DOUBLE:
		if (!IS_AGA) {
			DPRINTK("double mode only possible with aga\n");
			return -EINVAL;
		}
		line_shift = 2;
		break;
	default:
		DPRINTK("unknown video mode\n");
		return -EINVAL;
		break;
	}
	par->line_shift = line_shift;

	/*
	 * Vertical and Horizontal Timings
	 */

	xres_n = par->xres << clk_shift;
	yres_n = par->yres << line_shift;
	par->htotal = down8((var->left_margin + par->xres + var->right_margin +
			     var->hsync_len) << clk_shift);
	par->vtotal =
		down2(((var->upper_margin + par->yres + var->lower_margin +
			var->vsync_len) << line_shift) + 1);

	if (IS_AGA)
		par->bplcon3 = sprpixmode[clk_shift];
	else
		par->bplcon3 = 0;
	if (var->sync & FB_SYNC_BROADCAST) {
		par->diwstop_h = par->htotal -
			((var->right_margin - var->hsync_len) << clk_shift);
		if (IS_AGA)
			par->diwstop_h += mod4(var->hsync_len);
		else
			par->diwstop_h = down4(par->diwstop_h);

		par->diwstrt_h = par->diwstop_h - xres_n;
		par->diwstop_v = par->vtotal -
			((var->lower_margin - var->vsync_len) << line_shift);
		par->diwstrt_v = par->diwstop_v - yres_n;
		if (par->diwstop_h >= par->htotal + 8) {
			DPRINTK("invalid diwstop_h\n");
			return -EINVAL;
		}
		if (par->diwstop_v > par->vtotal) {
			DPRINTK("invalid diwstop_v\n");
			return -EINVAL;
		}

		if (!IS_OCS) {
			/* Initialize sync with some reasonable values for pwrsave */
			par->hsstrt = 160;
			par->hsstop = 320;
			par->vsstrt = 30;
			par->vsstop = 34;
		} else {
			par->hsstrt = 0;
			par->hsstop = 0;
			par->vsstrt = 0;
			par->vsstop = 0;
		}
		if (par->vtotal > (PAL_VTOTAL + NTSC_VTOTAL) / 2) {
			/* PAL video mode */
			if (par->htotal != PAL_HTOTAL) {
				DPRINTK("htotal invalid for pal\n");
				return -EINVAL;
			}
			if (par->diwstrt_h < PAL_DIWSTRT_H) {
				DPRINTK("diwstrt_h too low for pal\n");
				return -EINVAL;
			}
			if (par->diwstrt_v < PAL_DIWSTRT_V) {
				DPRINTK("diwstrt_v too low for pal\n");
				return -EINVAL;
			}
			htotal = PAL_HTOTAL>>clk_shift;
			vtotal = PAL_VTOTAL>>1;
			if (!IS_OCS) {
				par->beamcon0 = BMC0_PAL;
				par->bplcon3 |= BPC3_BRDRBLNK;
			} else if (AMIGAHW_PRESENT(AGNUS_HR_PAL) ||
				   AMIGAHW_PRESENT(AGNUS_HR_NTSC)) {
				par->beamcon0 = BMC0_PAL;
				par->hsstop = 1;
			} else if (amiga_vblank != 50) {
				DPRINTK("pal not supported by this chipset\n");
				return -EINVAL;
			}
		} else {
			/* NTSC video mode
			 * In the AGA chipset seems to be hardware bug with BPC3_BRDRBLNK
			 * and NTSC activated, so than better let diwstop_h <= 1812
			 */
			if (par->htotal != NTSC_HTOTAL) {
				DPRINTK("htotal invalid for ntsc\n");
				return -EINVAL;
			}
			if (par->diwstrt_h < NTSC_DIWSTRT_H) {
				DPRINTK("diwstrt_h too low for ntsc\n");
				return -EINVAL;
			}
			if (par->diwstrt_v < NTSC_DIWSTRT_V) {
				DPRINTK("diwstrt_v too low for ntsc\n");
				return -EINVAL;
			}
			htotal = NTSC_HTOTAL>>clk_shift;
			vtotal = NTSC_VTOTAL>>1;
			if (!IS_OCS) {
				par->beamcon0 = 0;
				par->bplcon3 |= BPC3_BRDRBLNK;
			} else if (AMIGAHW_PRESENT(AGNUS_HR_PAL) ||
				   AMIGAHW_PRESENT(AGNUS_HR_NTSC)) {
				par->beamcon0 = 0;
				par->hsstop = 1;
			} else if (amiga_vblank != 60) {
				DPRINTK("ntsc not supported by this chipset\n");
				return -EINVAL;
			}
		}
		if (IS_OCS) {
			if (par->diwstrt_h >= 1024 || par->diwstop_h < 1024 ||
			    par->diwstrt_v >=  512 || par->diwstop_v <  256) {
				DPRINTK("invalid position for display on ocs\n");
				return -EINVAL;
			}
		}
	} else if (!IS_OCS) {
		/* Programmable video mode */
		par->hsstrt = var->right_margin << clk_shift;
		par->hsstop = (var->right_margin + var->hsync_len) << clk_shift;
		par->diwstop_h = par->htotal - mod8(par->hsstrt) + 8 - (1 << clk_shift);
		if (!IS_AGA)
			par->diwstop_h = down4(par->diwstop_h) - 16;
		par->diwstrt_h = par->diwstop_h - xres_n;
		par->hbstop = par->diwstrt_h + 4;
		par->hbstrt = par->diwstop_h + 4;
		if (par->hbstrt >= par->htotal + 8)
			par->hbstrt -= par->htotal;
		par->hcenter = par->hsstrt + (par->htotal >> 1);
		par->vsstrt = var->lower_margin << line_shift;
		par->vsstop = (var->lower_margin + var->vsync_len) << line_shift;
		par->diwstop_v = par->vtotal;
		if ((par->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED)
			par->diwstop_v -= 2;
		par->diwstrt_v = par->diwstop_v - yres_n;
		par->vbstop = par->diwstrt_v - 2;
		par->vbstrt = par->diwstop_v - 2;
		if (par->vtotal > 2048) {
			DPRINTK("vtotal too high\n");
			return -EINVAL;
		}
		if (par->htotal > 2048) {
			DPRINTK("htotal too high\n");
			return -EINVAL;
		}
		par->bplcon3 |= BPC3_EXTBLKEN;
		par->beamcon0 = BMC0_HARDDIS | BMC0_VARVBEN | BMC0_LOLDIS |
				BMC0_VARVSYEN | BMC0_VARHSYEN | BMC0_VARBEAMEN |
				BMC0_PAL | BMC0_VARCSYEN;
		if (var->sync & FB_SYNC_HOR_HIGH_ACT)
			par->beamcon0 |= BMC0_HSYTRUE;
		if (var->sync & FB_SYNC_VERT_HIGH_ACT)
			par->beamcon0 |= BMC0_VSYTRUE;
		if (var->sync & FB_SYNC_COMP_HIGH_ACT)
			par->beamcon0 |= BMC0_CSYTRUE;
		htotal = par->htotal>>clk_shift;
		vtotal = par->vtotal>>1;
	} else {
		DPRINTK("only broadcast modes possible for ocs\n");
		return -EINVAL;
	}

	/*
	 * Checking the DMA timing
	 */

	fconst = 16 << maxfmode << clk_shift;

	/*
	 * smallest window start value without turn off other dma cycles
	 * than sprite1-7, unless you change min_fstrt
	 */


	fsize = ((maxfmode + clk_shift <= 1) ? fconst : 64);
	fstrt = downx(fconst, par->diwstrt_h - 4) - fsize;
	if (fstrt < min_fstrt) {
		DPRINTK("fetch start too low\n");
		return -EINVAL;
	}

	/*
	 * smallest window start value where smooth scrolling is possible
	 */

	fstrt = downx(fconst, par->diwstrt_h - fconst + (1 << clk_shift) - 4) -
		fsize;
	if (fstrt < min_fstrt)
		par->vmode &= ~FB_VMODE_SMOOTH_XPAN;

	maxfetchstop = down16(par->htotal - 80);

	fstrt = downx(fconst, par->diwstrt_h - 4) - 64 - fconst;
	fsize = upx(fconst, xres_n +
		    modx(fconst, downx(1 << clk_shift, par->diwstrt_h - 4)));
	if (fstrt + fsize > maxfetchstop)
		par->vmode &= ~FB_VMODE_SMOOTH_XPAN;

	fsize = upx(fconst, xres_n);
	if (fstrt + fsize > maxfetchstop) {
		DPRINTK("fetch stop too high\n");
		return -EINVAL;
	}

	if (maxfmode + clk_shift <= 1) {
		fsize = up64(xres_n + fconst - 1);
		if (min_fstrt + fsize - 64 > maxfetchstop)
			par->vmode &= ~FB_VMODE_SMOOTH_XPAN;

		fsize = up64(xres_n);
		if (min_fstrt + fsize - 64 > maxfetchstop) {
			DPRINTK("fetch size too high\n");
			return -EINVAL;
		}

		fsize -= 64;
	} else
		fsize -= fconst;

	/*
	 * Check if there is enough time to update the bitplane pointers for ywrap
	 */

	if (par->htotal - fsize - 64 < par->bpp * 64)
		par->vmode &= ~FB_VMODE_YWRAP;

	/*
	 * Bitplane calculations and check the Memory Requirements
	 */

	if (amifb_ilbm) {
		par->next_plane = div8(upx(16 << maxfmode, par->vxres));
		par->next_line = par->bpp * par->next_plane;
		if (par->next_line * par->vyres > info->fix.smem_len) {
			DPRINTK("too few video mem\n");
			return -EINVAL;
		}
	} else {
		par->next_line = div8(upx(16 << maxfmode, par->vxres));
		par->next_plane = par->vyres * par->next_line;
		if (par->next_plane * par->bpp > info->fix.smem_len) {
			DPRINTK("too few video mem\n");
			return -EINVAL;
		}
	}

	/*
	 * Hardware Register Values
	 */

	par->bplcon0 = BPC0_COLOR | bplpixmode[clk_shift];
	if (!IS_OCS)
		par->bplcon0 |= BPC0_ECSENA;
	if (par->bpp == 8)
		par->bplcon0 |= BPC0_BPU3;
	else
		par->bplcon0 |= par->bpp << 12;
	if (var->nonstd == FB_NONSTD_HAM)
		par->bplcon0 |= BPC0_HAM;
	if (var->sync & FB_SYNC_EXT)
		par->bplcon0 |= BPC0_ERSY;

	if (IS_AGA)
		par->fmode = bplfetchmode[maxfmode];

	switch (par->vmode & FB_VMODE_MASK) {
	case FB_VMODE_INTERLACED:
		par->bplcon0 |= BPC0_LACE;
		break;
	case FB_VMODE_DOUBLE:
		if (IS_AGA)
			par->fmode |= FMODE_SSCAN2 | FMODE_BSCAN2;
		break;
	}

	if (!((par->vmode ^ var->vmode) & FB_VMODE_YWRAP)) {
		par->xoffset = var->xoffset;
		par->yoffset = var->yoffset;
		if (par->vmode & FB_VMODE_YWRAP) {
			if (par->yoffset >= par->vyres)
				par->xoffset = par->yoffset = 0;
		} else {
			if (par->xoffset > upx(16 << maxfmode, par->vxres - par->xres) ||
			    par->yoffset > par->vyres - par->yres)
				par->xoffset = par->yoffset = 0;
		}
	} else
		par->xoffset = par->yoffset = 0;

	par->crsr.crsr_x = par->crsr.crsr_y = 0;
	par->crsr.spot_x = par->crsr.spot_y = 0;
	par->crsr.height = par->crsr.width = 0;

	return 0;
}

	/*
	 * Fill the `var' structure based on the values in `par' and maybe
	 * other values read out of the hardware.
	 */

static void ami_encode_var(struct fb_var_screeninfo *var,
			   struct amifb_par *par)
{
	u_short clk_shift, line_shift;

	memset(var, 0, sizeof(struct fb_var_screeninfo));

	clk_shift = par->clk_shift;
	line_shift = par->line_shift;

	var->xres = par->xres;
	var->yres = par->yres;
	var->xres_virtual = par->vxres;
	var->yres_virtual = par->vyres;
	var->xoffset = par->xoffset;
	var->yoffset = par->yoffset;

	var->bits_per_pixel = par->bpp;
	var->grayscale = 0;

	var->red.offset = 0;
	var->red.msb_right = 0;
	var->red.length = par->bpp;
	if (par->bplcon0 & BPC0_HAM)
		var->red.length -= 2;
	var->blue = var->green = var->red;
	var->transp.offset = 0;
	var->transp.length = 0;
	var->transp.msb_right = 0;

	if (par->bplcon0 & BPC0_HAM)
		var->nonstd = FB_NONSTD_HAM;
	else
		var->nonstd = 0;
	var->activate = 0;

	var->height = -1;
	var->width = -1;

	var->pixclock = pixclock[clk_shift];

	if (IS_AGA && par->fmode & FMODE_BSCAN2)
		var->vmode = FB_VMODE_DOUBLE;
	else if (par->bplcon0 & BPC0_LACE)
		var->vmode = FB_VMODE_INTERLACED;
	else
		var->vmode = FB_VMODE_NONINTERLACED;

	if (!IS_OCS && par->beamcon0 & BMC0_VARBEAMEN) {
		var->hsync_len = (par->hsstop - par->hsstrt)>>clk_shift;
		var->right_margin = par->hsstrt>>clk_shift;
		var->left_margin = (par->htotal>>clk_shift) - var->xres - var->right_margin - var->hsync_len;
		var->vsync_len = (par->vsstop - par->vsstrt)>>line_shift;
		var->lower_margin = par->vsstrt>>line_shift;
		var->upper_margin = (par->vtotal>>line_shift) - var->yres - var->lower_margin - var->vsync_len;
		var->sync = 0;
		if (par->beamcon0 & BMC0_HSYTRUE)
			var->sync |= FB_SYNC_HOR_HIGH_ACT;
		if (par->beamcon0 & BMC0_VSYTRUE)
			var->sync |= FB_SYNC_VERT_HIGH_ACT;
		if (par->beamcon0 & BMC0_CSYTRUE)
			var->sync |= FB_SYNC_COMP_HIGH_ACT;
	} else {
		var->sync = FB_SYNC_BROADCAST;
		var->hsync_len = (152>>clk_shift) + mod4(par->diwstop_h);
		var->right_margin = ((par->htotal - down4(par->diwstop_h))>>clk_shift) + var->hsync_len;
		var->left_margin = (par->htotal>>clk_shift) - var->xres - var->right_margin - var->hsync_len;
		var->vsync_len = 4>>line_shift;
		var->lower_margin = ((par->vtotal - par->diwstop_v)>>line_shift) + var->vsync_len;
		var->upper_margin = (((par->vtotal - 2)>>line_shift) + 1) - var->yres -
				    var->lower_margin - var->vsync_len;
	}

	if (par->bplcon0 & BPC0_ERSY)
		var->sync |= FB_SYNC_EXT;
	if (par->vmode & FB_VMODE_YWRAP)
		var->vmode |= FB_VMODE_YWRAP;
}


	/*
	 * Update hardware
	 */

static void ami_update_par(struct fb_info *info)
{
	struct amifb_par *par = info->par;
	short clk_shift, vshift, fstrt, fsize, fstop, fconst,  shift, move, mod;

	clk_shift = par->clk_shift;

	if (!(par->vmode & FB_VMODE_SMOOTH_XPAN))
		par->xoffset = upx(16 << maxfmode, par->xoffset);

	fconst = 16 << maxfmode << clk_shift;
	vshift = modx(16 << maxfmode, par->xoffset);
	fstrt = par->diwstrt_h - (vshift << clk_shift) - 4;
	fsize = (par->xres + vshift) << clk_shift;
	shift = modx(fconst, fstrt);
	move = downx(2 << maxfmode, div8(par->xoffset));
	if (maxfmode + clk_shift > 1) {
		fstrt = downx(fconst, fstrt) - 64;
		fsize = upx(fconst, fsize);
		fstop = fstrt + fsize - fconst;
	} else {
		mod = fstrt = downx(fconst, fstrt) - fconst;
		fstop = fstrt + upx(fconst, fsize) - 64;
		fsize = up64(fsize);
		fstrt = fstop - fsize + 64;
		if (fstrt < min_fstrt) {
			fstop += min_fstrt - fstrt;
			fstrt = min_fstrt;
		}
		move = move - div8((mod - fstrt)>>clk_shift);
	}
	mod = par->next_line - div8(fsize>>clk_shift);
	par->ddfstrt = fstrt;
	par->ddfstop = fstop;
	par->bplcon1 = hscroll2hw(shift);
	par->bpl2mod = mod;
	if (par->bplcon0 & BPC0_LACE)
		par->bpl2mod += par->next_line;
	if (IS_AGA && (par->fmode & FMODE_BSCAN2))
		par->bpl1mod = -div8(fsize>>clk_shift);
	else
		par->bpl1mod = par->bpl2mod;

	if (par->yoffset) {
		par->bplpt0 = info->fix.smem_start +
			      par->next_line * par->yoffset + move;
		if (par->vmode & FB_VMODE_YWRAP) {
			if (par->yoffset > par->vyres - par->yres) {
				par->bplpt0wrap = info->fix.smem_start + move;
				if (par->bplcon0 & BPC0_LACE &&
				    mod2(par->diwstrt_v + par->vyres -
					 par->yoffset))
					par->bplpt0wrap += par->next_line;
			}
		}
	} else
		par->bplpt0 = info->fix.smem_start + move;

	if (par->bplcon0 & BPC0_LACE && mod2(par->diwstrt_v))
		par->bplpt0 += par->next_line;
}


	/*
	 * Pan or Wrap the Display
	 *
	 * This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
	 * in `var'.
	 */

static void ami_pan_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct amifb_par *par = info->par;

	par->xoffset = var->xoffset;
	par->yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		par->vmode |= FB_VMODE_YWRAP;
	else
		par->vmode &= ~FB_VMODE_YWRAP;

	do_vmode_pan = 0;
	ami_update_par(info);
	do_vmode_pan = 1;
}


static void ami_update_display(const struct amifb_par *par)
{
	custom.bplcon1 = par->bplcon1;
	custom.bpl1mod = par->bpl1mod;
	custom.bpl2mod = par->bpl2mod;
	custom.ddfstrt = ddfstrt2hw(par->ddfstrt);
	custom.ddfstop = ddfstop2hw(par->ddfstop);
}

	/*
	 * Change the video mode (called by VBlank interrupt)
	 */

static void ami_init_display(const struct amifb_par *par)
{
	int i;

	custom.bplcon0 = par->bplcon0 & ~BPC0_LACE;
	custom.bplcon2 = (IS_OCS ? 0 : BPC2_KILLEHB) | BPC2_PF2P2 | BPC2_PF1P2;
	if (!IS_OCS) {
		custom.bplcon3 = par->bplcon3;
		if (IS_AGA)
			custom.bplcon4 = BPC4_ESPRM4 | BPC4_OSPRM4;
		if (par->beamcon0 & BMC0_VARBEAMEN) {
			custom.htotal = htotal2hw(par->htotal);
			custom.hbstrt = hbstrt2hw(par->hbstrt);
			custom.hbstop = hbstop2hw(par->hbstop);
			custom.hsstrt = hsstrt2hw(par->hsstrt);
			custom.hsstop = hsstop2hw(par->hsstop);
			custom.hcenter = hcenter2hw(par->hcenter);
			custom.vtotal = vtotal2hw(par->vtotal);
			custom.vbstrt = vbstrt2hw(par->vbstrt);
			custom.vbstop = vbstop2hw(par->vbstop);
			custom.vsstrt = vsstrt2hw(par->vsstrt);
			custom.vsstop = vsstop2hw(par->vsstop);
		}
	}
	if (!IS_OCS || par->hsstop)
		custom.beamcon0 = par->beamcon0;
	if (IS_AGA)
		custom.fmode = par->fmode;

	/*
	 * The minimum period for audio depends on htotal
	 */

	amiga_audio_min_period = div16(par->htotal);

	is_lace = par->bplcon0 & BPC0_LACE ? 1 : 0;
#if 1
	if (is_lace) {
		i = custom.vposr >> 15;
	} else {
		custom.vposw = custom.vposr | 0x8000;
		i = 1;
	}
#else
	i = 1;
	custom.vposw = custom.vposr | 0x8000;
#endif
	custom.cop2lc = (u_short *)ZTWO_PADDR(copdisplay.list[currentcop][i]);
}

	/*
	 * (Un)Blank the screen (called by VBlank interrupt)
	 */

static void ami_do_blank(const struct amifb_par *par)
{
#if defined(CONFIG_FB_AMIGA_AGA)
	u_short bplcon3 = par->bplcon3;
#endif
	u_char red, green, blue;

	if (do_blank > 0) {
		custom.dmacon = DMAF_RASTER | DMAF_SPRITE;
		red = green = blue = 0;
		if (!IS_OCS && do_blank > 1) {
			switch (do_blank) {
			case FB_BLANK_VSYNC_SUSPEND:
				custom.hsstrt = hsstrt2hw(par->hsstrt);
				custom.hsstop = hsstop2hw(par->hsstop);
				custom.vsstrt = vsstrt2hw(par->vtotal + 4);
				custom.vsstop = vsstop2hw(par->vtotal + 4);
				break;
			case FB_BLANK_HSYNC_SUSPEND:
				custom.hsstrt = hsstrt2hw(par->htotal + 16);
				custom.hsstop = hsstop2hw(par->htotal + 16);
				custom.vsstrt = vsstrt2hw(par->vsstrt);
				custom.vsstop = vsstrt2hw(par->vsstop);
				break;
			case FB_BLANK_POWERDOWN:
				custom.hsstrt = hsstrt2hw(par->htotal + 16);
				custom.hsstop = hsstop2hw(par->htotal + 16);
				custom.vsstrt = vsstrt2hw(par->vtotal + 4);
				custom.vsstop = vsstop2hw(par->vtotal + 4);
				break;
			}
			if (!(par->beamcon0 & BMC0_VARBEAMEN)) {
				custom.htotal = htotal2hw(par->htotal);
				custom.vtotal = vtotal2hw(par->vtotal);
				custom.beamcon0 = BMC0_HARDDIS | BMC0_VARBEAMEN |
						  BMC0_VARVSYEN | BMC0_VARHSYEN | BMC0_VARCSYEN;
			}
		}
	} else {
		custom.dmacon = DMAF_SETCLR | DMAF_RASTER | DMAF_SPRITE;
		red = red0;
		green = green0;
		blue = blue0;
		if (!IS_OCS) {
			custom.hsstrt = hsstrt2hw(par->hsstrt);
			custom.hsstop = hsstop2hw(par->hsstop);
			custom.vsstrt = vsstrt2hw(par->vsstrt);
			custom.vsstop = vsstop2hw(par->vsstop);
			custom.beamcon0 = par->beamcon0;
		}
	}
#if defined(CONFIG_FB_AMIGA_AGA)
	if (IS_AGA) {
		custom.bplcon3 = bplcon3;
		custom.color[0] = rgb2hw8_high(red, green, blue);
		custom.bplcon3 = bplcon3 | BPC3_LOCT;
		custom.color[0] = rgb2hw8_low(red, green, blue);
		custom.bplcon3 = bplcon3;
	} else
#endif
#if defined(CONFIG_FB_AMIGA_ECS)
	if (par->bplcon0 & BPC0_SHRES) {
		u_short color, mask;
		int i;

		mask = 0x3333;
		color = rgb2hw2(red, green, blue);
		for (i = 12; i >= 0; i -= 4)
			custom.color[i] = ecs_palette[i] = (ecs_palette[i] & mask) | color;
		mask <<= 2; color >>= 2;
		for (i = 3; i >= 0; i--)
			custom.color[i] = ecs_palette[i] = (ecs_palette[i] & mask) | color;
	} else
#endif
		custom.color[0] = rgb2hw4(red, green, blue);
	is_blanked = do_blank > 0 ? do_blank : 0;
}

static int ami_get_fix_cursorinfo(struct fb_fix_cursorinfo *fix,
				  const struct amifb_par *par)
{
	fix->crsr_width = fix->crsr_xsize = par->crsr.width;
	fix->crsr_height = fix->crsr_ysize = par->crsr.height;
	fix->crsr_color1 = 17;
	fix->crsr_color2 = 18;
	return 0;
}

static int ami_get_var_cursorinfo(struct fb_var_cursorinfo *var,
				  u_char __user *data,
				  const struct amifb_par *par)
{
	register u_short *lspr, *sspr;
#ifdef __mc68000__
	register u_long datawords asm ("d2");
#else
	register u_long datawords;
#endif
	register short delta;
	register u_char color;
	short height, width, bits, words;
	int size, alloc;

	size = par->crsr.height * par->crsr.width;
	alloc = var->height * var->width;
	var->height = par->crsr.height;
	var->width = par->crsr.width;
	var->xspot = par->crsr.spot_x;
	var->yspot = par->crsr.spot_y;
	if (size > var->height * var->width)
		return -ENAMETOOLONG;
	if (!access_ok(VERIFY_WRITE, data, size))
		return -EFAULT;
	delta = 1 << par->crsr.fmode;
	lspr = lofsprite + (delta << 1);
	if (par->bplcon0 & BPC0_LACE)
		sspr = shfsprite + (delta << 1);
	else
		sspr = NULL;
	for (height = (short)var->height - 1; height >= 0; height--) {
		bits = 0; words = delta; datawords = 0;
		for (width = (short)var->width - 1; width >= 0; width--) {
			if (bits == 0) {
				bits = 16; --words;
#ifdef __mc68000__
				asm volatile ("movew %1@(%3:w:2),%0 ; swap %0 ; movew %1@+,%0"
					: "=d" (datawords), "=a" (lspr) : "1" (lspr), "d" (delta));
#else
				datawords = (*(lspr + delta) << 16) | (*lspr++);
#endif
			}
			--bits;
#ifdef __mc68000__
			asm volatile (
				"clrb %0 ; swap %1 ; lslw #1,%1 ; roxlb #1,%0 ; "
				"swap %1 ; lslw #1,%1 ; roxlb #1,%0"
				: "=d" (color), "=d" (datawords) : "1" (datawords));
#else
			color = (((datawords >> 30) & 2)
				 | ((datawords >> 15) & 1));
			datawords <<= 1;
#endif
			put_user(color, data++);
		}
		if (bits > 0) {
			--words; ++lspr;
		}
		while (--words >= 0)
			++lspr;
#ifdef __mc68000__
		asm volatile ("lea %0@(%4:w:2),%0 ; tstl %1 ; jeq 1f ; exg %0,%1\n1:"
			: "=a" (lspr), "=a" (sspr) : "0" (lspr), "1" (sspr), "d" (delta));
#else
		lspr += delta;
		if (sspr) {
			u_short *tmp = lspr;
			lspr = sspr;
			sspr = tmp;
		}
#endif
	}
	return 0;
}

static int ami_set_var_cursorinfo(struct fb_var_cursorinfo *var,
				  u_char __user *data, struct amifb_par *par)
{
	register u_short *lspr, *sspr;
#ifdef __mc68000__
	register u_long datawords asm ("d2");
#else
	register u_long datawords;
#endif
	register short delta;
	u_short fmode;
	short height, width, bits, words;

	if (!var->width)
		return -EINVAL;
	else if (var->width <= 16)
		fmode = TAG_FMODE_1;
	else if (var->width <= 32)
		fmode = TAG_FMODE_2;
	else if (var->width <= 64)
		fmode = TAG_FMODE_4;
	else
		return -EINVAL;
	if (fmode > maxfmode)
		return -EINVAL;
	if (!var->height)
		return -EINVAL;
	if (!access_ok(VERIFY_READ, data, var->width * var->height))
		return -EFAULT;
	delta = 1 << fmode;
	lofsprite = shfsprite = (u_short *)spritememory;
	lspr = lofsprite + (delta << 1);
	if (par->bplcon0 & BPC0_LACE) {
		if (((var->height + 4) << fmode << 2) > SPRITEMEMSIZE)
			return -EINVAL;
		memset(lspr, 0, (var->height + 4) << fmode << 2);
		shfsprite += ((var->height + 5)&-2) << fmode;
		sspr = shfsprite + (delta << 1);
	} else {
		if (((var->height + 2) << fmode << 2) > SPRITEMEMSIZE)
			return -EINVAL;
		memset(lspr, 0, (var->height + 2) << fmode << 2);
		sspr = NULL;
	}
	for (height = (short)var->height - 1; height >= 0; height--) {
		bits = 16; words = delta; datawords = 0;
		for (width = (short)var->width - 1; width >= 0; width--) {
			unsigned long tdata = 0;
			get_user(tdata, data);
			data++;
#ifdef __mc68000__
			asm volatile (
				"lsrb #1,%2 ; roxlw #1,%0 ; swap %0 ; "
				"lsrb #1,%2 ; roxlw #1,%0 ; swap %0"
				: "=d" (datawords)
				: "0" (datawords), "d" (tdata));
#else
			datawords = ((datawords << 1) & 0xfffefffe);
			datawords |= tdata & 1;
			datawords |= (tdata & 2) << (16 - 1);
#endif
			if (--bits == 0) {
				bits = 16; --words;
#ifdef __mc68000__
				asm volatile ("swap %2 ; movew %2,%0@(%3:w:2) ; swap %2 ; movew %2,%0@+"
					: "=a" (lspr) : "0" (lspr), "d" (datawords), "d" (delta));
#else
				*(lspr + delta) = (u_short) (datawords >> 16);
				*lspr++ = (u_short) (datawords & 0xffff);
#endif
			}
		}
		if (bits < 16) {
			--words;
#ifdef __mc68000__
			asm volatile (
				"swap %2 ; lslw %4,%2 ; movew %2,%0@(%3:w:2) ; "
				"swap %2 ; lslw %4,%2 ; movew %2,%0@+"
				: "=a" (lspr) : "0" (lspr), "d" (datawords), "d" (delta), "d" (bits));
#else
			*(lspr + delta) = (u_short) (datawords >> (16 + bits));
			*lspr++ = (u_short) ((datawords & 0x0000ffff) >> bits);
#endif
		}
		while (--words >= 0) {
#ifdef __mc68000__
			asm volatile ("moveql #0,%%d0 ; movew %%d0,%0@(%2:w:2) ; movew %%d0,%0@+"
				: "=a" (lspr) : "0" (lspr), "d" (delta) : "d0");
#else
			*(lspr + delta) = 0;
			*lspr++ = 0;
#endif
		}
#ifdef __mc68000__
		asm volatile ("lea %0@(%4:w:2),%0 ; tstl %1 ; jeq 1f ; exg %0,%1\n1:"
			: "=a" (lspr), "=a" (sspr) : "0" (lspr), "1" (sspr), "d" (delta));
#else
		lspr += delta;
		if (sspr) {
			u_short *tmp = lspr;
			lspr = sspr;
			sspr = tmp;
		}
#endif
	}
	par->crsr.height = var->height;
	par->crsr.width = var->width;
	par->crsr.spot_x = var->xspot;
	par->crsr.spot_y = var->yspot;
	par->crsr.fmode = fmode;
	if (IS_AGA) {
		par->fmode &= ~(FMODE_SPAGEM | FMODE_SPR32);
		par->fmode |= sprfetchmode[fmode];
		custom.fmode = par->fmode;
	}
	return 0;
}

static int ami_get_cursorstate(struct fb_cursorstate *state,
			       const struct amifb_par *par)
{
	state->xoffset = par->crsr.crsr_x;
	state->yoffset = par->crsr.crsr_y;
	state->mode = cursormode;
	return 0;
}

static int ami_set_cursorstate(struct fb_cursorstate *state,
			       struct amifb_par *par)
{
	par->crsr.crsr_x = state->xoffset;
	par->crsr.crsr_y = state->yoffset;
	if ((cursormode = state->mode) == FB_CURSOR_OFF)
		cursorstate = -1;
	do_cursor = 1;
	return 0;
}

static void ami_set_sprite(const struct amifb_par *par)
{
	copins *copl, *cops;
	u_short hs, vs, ve;
	u_long pl, ps;
	short mx, my;

	cops = copdisplay.list[currentcop][0];
	copl = copdisplay.list[currentcop][1];
	ps = pl = ZTWO_PADDR(dummysprite);
	mx = par->crsr.crsr_x - par->crsr.spot_x;
	my = par->crsr.crsr_y - par->crsr.spot_y;
	if (!(par->vmode & FB_VMODE_YWRAP)) {
		mx -= par->xoffset;
		my -= par->yoffset;
	}
	if (!is_blanked && cursorstate > 0 && par->crsr.height > 0 &&
	    mx > -(short)par->crsr.width && mx < par->xres &&
	    my > -(short)par->crsr.height && my < par->yres) {
		pl = ZTWO_PADDR(lofsprite);
		hs = par->diwstrt_h + (mx << par->clk_shift) - 4;
		vs = par->diwstrt_v + (my << par->line_shift);
		ve = vs + (par->crsr.height << par->line_shift);
		if (par->bplcon0 & BPC0_LACE) {
			ps = ZTWO_PADDR(shfsprite);
			lofsprite[0] = spr2hw_pos(vs, hs);
			shfsprite[0] = spr2hw_pos(vs + 1, hs);
			if (mod2(vs)) {
				lofsprite[1 << par->crsr.fmode] = spr2hw_ctl(vs, hs, ve);
				shfsprite[1 << par->crsr.fmode] = spr2hw_ctl(vs + 1, hs, ve + 1);
				swap(pl, ps);
			} else {
				lofsprite[1 << par->crsr.fmode] = spr2hw_ctl(vs, hs, ve + 1);
				shfsprite[1 << par->crsr.fmode] = spr2hw_ctl(vs + 1, hs, ve);
			}
		} else {
			lofsprite[0] = spr2hw_pos(vs, hs) | (IS_AGA && (par->fmode & FMODE_BSCAN2) ? 0x80 : 0);
			lofsprite[1 << par->crsr.fmode] = spr2hw_ctl(vs, hs, ve);
		}
	}
	copl[cop_spr0ptrh].w[1] = highw(pl);
	copl[cop_spr0ptrl].w[1] = loww(pl);
	if (par->bplcon0 & BPC0_LACE) {
		cops[cop_spr0ptrh].w[1] = highw(ps);
		cops[cop_spr0ptrl].w[1] = loww(ps);
	}
}


	/*
	 * Initialise the Copper Initialisation List
	 */

static void __init ami_init_copper(void)
{
	copins *cop = copdisplay.init;
	u_long p;
	int i;

	if (!IS_OCS) {
		(cop++)->l = CMOVE(BPC0_COLOR | BPC0_SHRES | BPC0_ECSENA, bplcon0);
		(cop++)->l = CMOVE(0x0181, diwstrt);
		(cop++)->l = CMOVE(0x0281, diwstop);
		(cop++)->l = CMOVE(0x0000, diwhigh);
	} else
		(cop++)->l = CMOVE(BPC0_COLOR, bplcon0);
	p = ZTWO_PADDR(dummysprite);
	for (i = 0; i < 8; i++) {
		(cop++)->l = CMOVE(0, spr[i].pos);
		(cop++)->l = CMOVE(highw(p), sprpt[i]);
		(cop++)->l = CMOVE2(loww(p), sprpt[i]);
	}

	(cop++)->l = CMOVE(IF_SETCLR | IF_COPER, intreq);
	copdisplay.wait = cop;
	(cop++)->l = CEND;
	(cop++)->l = CMOVE(0, copjmp2);
	cop->l = CEND;

	custom.cop1lc = (u_short *)ZTWO_PADDR(copdisplay.init);
	custom.copjmp1 = 0;
}

static void ami_reinit_copper(const struct amifb_par *par)
{
	copdisplay.init[cip_bplcon0].w[1] = ~(BPC0_BPU3 | BPC0_BPU2 | BPC0_BPU1 | BPC0_BPU0) & par->bplcon0;
	copdisplay.wait->l = CWAIT(32, par->diwstrt_v - 4);
}


	/*
	 * Rebuild the Copper List
	 *
	 * We only change the things that are not static
	 */

static void ami_rebuild_copper(const struct amifb_par *par)
{
	copins *copl, *cops;
	u_short line, h_end1, h_end2;
	short i;
	u_long p;

	if (IS_AGA && maxfmode + par->clk_shift == 0)
		h_end1 = par->diwstrt_h - 64;
	else
		h_end1 = par->htotal - 32;
	h_end2 = par->ddfstop + 64;

	ami_set_sprite(par);

	copl = copdisplay.rebuild[1];
	p = par->bplpt0;
	if (par->vmode & FB_VMODE_YWRAP) {
		if ((par->vyres - par->yoffset) != 1 || !mod2(par->diwstrt_v)) {
			if (par->yoffset > par->vyres - par->yres) {
				for (i = 0; i < (short)par->bpp; i++, p += par->next_plane) {
					(copl++)->l = CMOVE(highw(p), bplpt[i]);
					(copl++)->l = CMOVE2(loww(p), bplpt[i]);
				}
				line = par->diwstrt_v + ((par->vyres - par->yoffset) << par->line_shift) - 1;
				while (line >= 512) {
					(copl++)->l = CWAIT(h_end1, 510);
					line -= 512;
				}
				if (line >= 510 && IS_AGA && maxfmode + par->clk_shift == 0)
					(copl++)->l = CWAIT(h_end1, line);
				else
					(copl++)->l = CWAIT(h_end2, line);
				p = par->bplpt0wrap;
			}
		} else
			p = par->bplpt0wrap;
	}
	for (i = 0; i < (short)par->bpp; i++, p += par->next_plane) {
		(copl++)->l = CMOVE(highw(p), bplpt[i]);
		(copl++)->l = CMOVE2(loww(p), bplpt[i]);
	}
	copl->l = CEND;

	if (par->bplcon0 & BPC0_LACE) {
		cops = copdisplay.rebuild[0];
		p = par->bplpt0;
		if (mod2(par->diwstrt_v))
			p -= par->next_line;
		else
			p += par->next_line;
		if (par->vmode & FB_VMODE_YWRAP) {
			if ((par->vyres - par->yoffset) != 1 || mod2(par->diwstrt_v)) {
				if (par->yoffset > par->vyres - par->yres + 1) {
					for (i = 0; i < (short)par->bpp; i++, p += par->next_plane) {
						(cops++)->l = CMOVE(highw(p), bplpt[i]);
						(cops++)->l = CMOVE2(loww(p), bplpt[i]);
					}
					line = par->diwstrt_v + ((par->vyres - par->yoffset) << par->line_shift) - 2;
					while (line >= 512) {
						(cops++)->l = CWAIT(h_end1, 510);
						line -= 512;
					}
					if (line > 510 && IS_AGA && maxfmode + par->clk_shift == 0)
						(cops++)->l = CWAIT(h_end1, line);
					else
						(cops++)->l = CWAIT(h_end2, line);
					p = par->bplpt0wrap;
					if (mod2(par->diwstrt_v + par->vyres -
					    par->yoffset))
						p -= par->next_line;
					else
						p += par->next_line;
				}
			} else
				p = par->bplpt0wrap - par->next_line;
		}
		for (i = 0; i < (short)par->bpp; i++, p += par->next_plane) {
			(cops++)->l = CMOVE(highw(p), bplpt[i]);
			(cops++)->l = CMOVE2(loww(p), bplpt[i]);
		}
		cops->l = CEND;
	}
}


	/*
	 * Build the Copper List
	 */

static void ami_build_copper(struct fb_info *info)
{
	struct amifb_par *par = info->par;
	copins *copl, *cops;
	u_long p;

	currentcop = 1 - currentcop;

	copl = copdisplay.list[currentcop][1];

	(copl++)->l = CWAIT(0, 10);
	(copl++)->l = CMOVE(par->bplcon0, bplcon0);
	(copl++)->l = CMOVE(0, sprpt[0]);
	(copl++)->l = CMOVE2(0, sprpt[0]);

	if (par->bplcon0 & BPC0_LACE) {
		cops = copdisplay.list[currentcop][0];

		(cops++)->l = CWAIT(0, 10);
		(cops++)->l = CMOVE(par->bplcon0, bplcon0);
		(cops++)->l = CMOVE(0, sprpt[0]);
		(cops++)->l = CMOVE2(0, sprpt[0]);

		(copl++)->l = CMOVE(diwstrt2hw(par->diwstrt_h, par->diwstrt_v + 1), diwstrt);
		(copl++)->l = CMOVE(diwstop2hw(par->diwstop_h, par->diwstop_v + 1), diwstop);
		(cops++)->l = CMOVE(diwstrt2hw(par->diwstrt_h, par->diwstrt_v), diwstrt);
		(cops++)->l = CMOVE(diwstop2hw(par->diwstop_h, par->diwstop_v), diwstop);
		if (!IS_OCS) {
			(copl++)->l = CMOVE(diwhigh2hw(par->diwstrt_h, par->diwstrt_v + 1,
					    par->diwstop_h, par->diwstop_v + 1), diwhigh);
			(cops++)->l = CMOVE(diwhigh2hw(par->diwstrt_h, par->diwstrt_v,
					    par->diwstop_h, par->diwstop_v), diwhigh);
#if 0
			if (par->beamcon0 & BMC0_VARBEAMEN) {
				(copl++)->l = CMOVE(vtotal2hw(par->vtotal), vtotal);
				(copl++)->l = CMOVE(vbstrt2hw(par->vbstrt + 1), vbstrt);
				(copl++)->l = CMOVE(vbstop2hw(par->vbstop + 1), vbstop);
				(cops++)->l = CMOVE(vtotal2hw(par->vtotal), vtotal);
				(cops++)->l = CMOVE(vbstrt2hw(par->vbstrt), vbstrt);
				(cops++)->l = CMOVE(vbstop2hw(par->vbstop), vbstop);
			}
#endif
		}
		p = ZTWO_PADDR(copdisplay.list[currentcop][0]);
		(copl++)->l = CMOVE(highw(p), cop2lc);
		(copl++)->l = CMOVE2(loww(p), cop2lc);
		p = ZTWO_PADDR(copdisplay.list[currentcop][1]);
		(cops++)->l = CMOVE(highw(p), cop2lc);
		(cops++)->l = CMOVE2(loww(p), cop2lc);
		copdisplay.rebuild[0] = cops;
	} else {
		(copl++)->l = CMOVE(diwstrt2hw(par->diwstrt_h, par->diwstrt_v), diwstrt);
		(copl++)->l = CMOVE(diwstop2hw(par->diwstop_h, par->diwstop_v), diwstop);
		if (!IS_OCS) {
			(copl++)->l = CMOVE(diwhigh2hw(par->diwstrt_h, par->diwstrt_v,
					    par->diwstop_h, par->diwstop_v), diwhigh);
#if 0
			if (par->beamcon0 & BMC0_VARBEAMEN) {
				(copl++)->l = CMOVE(vtotal2hw(par->vtotal), vtotal);
				(copl++)->l = CMOVE(vbstrt2hw(par->vbstrt), vbstrt);
				(copl++)->l = CMOVE(vbstop2hw(par->vbstop), vbstop);
			}
#endif
		}
	}
	copdisplay.rebuild[1] = copl;

	ami_update_par(info);
	ami_rebuild_copper(info->par);
}

#ifndef MODULE
static void __init amifb_setup_mcap(char *spec)
{
	char *p;
	int vmin, vmax, hmin, hmax;

	/* Format for monitor capabilities is: <Vmin>;<Vmax>;<Hmin>;<Hmax>
	 * <V*> vertical freq. in Hz
	 * <H*> horizontal freq. in kHz
	 */

	if (!(p = strsep(&spec, ";")) || !*p)
		return;
	vmin = simple_strtoul(p, NULL, 10);
	if (vmin <= 0)
		return;
	if (!(p = strsep(&spec, ";")) || !*p)
		return;
	vmax = simple_strtoul(p, NULL, 10);
	if (vmax <= 0 || vmax <= vmin)
		return;
	if (!(p = strsep(&spec, ";")) || !*p)
		return;
	hmin = 1000 * simple_strtoul(p, NULL, 10);
	if (hmin <= 0)
		return;
	if (!(p = strsep(&spec, "")) || !*p)
		return;
	hmax = 1000 * simple_strtoul(p, NULL, 10);
	if (hmax <= 0 || hmax <= hmin)
		return;

	amifb_hfmin = hmin;
	amifb_hfmax = hmax;
	amifb_vfmin = vmin;
	amifb_vfmax = vmax;
}

static int __init amifb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		if (!strcmp(this_opt, "inverse")) {
			fb_invert_cmaps();
		} else if (!strcmp(this_opt, "ilbm"))
			amifb_ilbm = 1;
		else if (!strncmp(this_opt, "monitorcap:", 11))
			amifb_setup_mcap(this_opt + 11);
		else if (!strncmp(this_opt, "fstart:", 7))
			min_fstrt = simple_strtoul(this_opt + 7, NULL, 0);
		else
			mode_option = this_opt;
	}

	if (min_fstrt < 48)
		min_fstrt = 48;

	return 0;
}
#endif

static int amifb_check_var(struct fb_var_screeninfo *var,
			   struct fb_info *info)
{
	int err;
	struct amifb_par par;

	/* Validate wanted screen parameters */
	err = ami_decode_var(var, &par, info);
	if (err)
		return err;

	/* Encode (possibly rounded) screen parameters */
	ami_encode_var(var, &par);
	return 0;
}


static int amifb_set_par(struct fb_info *info)
{
	struct amifb_par *par = info->par;
	int error;

	do_vmode_pan = 0;
	do_vmode_full = 0;

	/* Decode wanted screen parameters */
	error = ami_decode_var(&info->var, par, info);
	if (error)
		return error;

	/* Set new videomode */
	ami_build_copper(info);

	/* Set VBlank trigger */
	do_vmode_full = 1;

	/* Update fix for new screen parameters */
	if (par->bpp == 1) {
		info->fix.type = FB_TYPE_PACKED_PIXELS;
		info->fix.type_aux = 0;
	} else if (amifb_ilbm) {
		info->fix.type = FB_TYPE_INTERLEAVED_PLANES;
		info->fix.type_aux = par->next_line;
	} else {
		info->fix.type = FB_TYPE_PLANES;
		info->fix.type_aux = 0;
	}
	info->fix.line_length = div8(upx(16 << maxfmode, par->vxres));

	if (par->vmode & FB_VMODE_YWRAP) {
		info->fix.ywrapstep = 1;
		info->fix.xpanstep = 0;
		info->fix.ypanstep = 0;
		info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YWRAP |
			FBINFO_READS_FAST; /* override SCROLL_REDRAW */
	} else {
		info->fix.ywrapstep = 0;
		if (par->vmode & FB_VMODE_SMOOTH_XPAN)
			info->fix.xpanstep = 1;
		else
			info->fix.xpanstep = 16 << maxfmode;
		info->fix.ypanstep = 1;
		info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;
	}
	return 0;
}


	/*
	 * Set a single color register. The values supplied are already
	 * rounded down to the hardware's capabilities (according to the
	 * entries in the var structure). Return != 0 for invalid regno.
	 */

static int amifb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info)
{
	const struct amifb_par *par = info->par;

	if (IS_AGA) {
		if (regno > 255)
			return 1;
	} else if (par->bplcon0 & BPC0_SHRES) {
		if (regno > 3)
			return 1;
	} else {
		if (regno > 31)
			return 1;
	}
	red >>= 8;
	green >>= 8;
	blue >>= 8;
	if (!regno) {
		red0 = red;
		green0 = green;
		blue0 = blue;
	}

	/*
	 * Update the corresponding Hardware Color Register, unless it's Color
	 * Register 0 and the screen is blanked.
	 *
	 * VBlank is switched off to protect bplcon3 or ecs_palette[] from
	 * being changed by ami_do_blank() during the VBlank.
	 */

	if (regno || !is_blanked) {
#if defined(CONFIG_FB_AMIGA_AGA)
		if (IS_AGA) {
			u_short bplcon3 = par->bplcon3;
			VBlankOff();
			custom.bplcon3 = bplcon3 | (regno << 8 & 0xe000);
			custom.color[regno & 31] = rgb2hw8_high(red, green,
								blue);
			custom.bplcon3 = bplcon3 | (regno << 8 & 0xe000) |
					 BPC3_LOCT;
			custom.color[regno & 31] = rgb2hw8_low(red, green,
							       blue);
			custom.bplcon3 = bplcon3;
			VBlankOn();
		} else
#endif
#if defined(CONFIG_FB_AMIGA_ECS)
		if (par->bplcon0 & BPC0_SHRES) {
			u_short color, mask;
			int i;

			mask = 0x3333;
			color = rgb2hw2(red, green, blue);
			VBlankOff();
			for (i = regno + 12; i >= (int)regno; i -= 4)
				custom.color[i] = ecs_palette[i] = (ecs_palette[i] & mask) | color;
			mask <<= 2; color >>= 2;
			regno = down16(regno) + mul4(mod4(regno));
			for (i = regno + 3; i >= (int)regno; i--)
				custom.color[i] = ecs_palette[i] = (ecs_palette[i] & mask) | color;
			VBlankOn();
		} else
#endif
			custom.color[regno] = rgb2hw4(red, green, blue);
	}
	return 0;
}


	/*
	 * Blank the display.
	 */

static int amifb_blank(int blank, struct fb_info *info)
{
	do_blank = blank ? blank : -1;

	return 0;
}


	/*
	 * Pan or Wrap the Display
	 *
	 * This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
	 */

static int amifb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset < 0 ||
			var->yoffset >= info->var.yres_virtual || var->xoffset)
				return -EINVAL;
	} else {
		/*
		 * TODO: There will be problems when xpan!=1, so some columns
		 * on the right side will never be seen
		 */
		if (var->xoffset + info->var.xres >
		    upx(16 << maxfmode, info->var.xres_virtual) ||
		    var->yoffset + info->var.yres > info->var.yres_virtual)
			return -EINVAL;
	}
	ami_pan_var(var, info);
	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;
	return 0;
}


#if BITS_PER_LONG == 32
#define BYTES_PER_LONG	4
#define SHIFT_PER_LONG	5
#elif BITS_PER_LONG == 64
#define BYTES_PER_LONG	8
#define SHIFT_PER_LONG	6
#else
#define Please update me
#endif


	/*
	 *  Compose two values, using a bitmask as decision value
	 *  This is equivalent to (a & mask) | (b & ~mask)
	 */

static inline unsigned long comp(unsigned long a, unsigned long b,
				 unsigned long mask)
{
	return ((a ^ b) & mask) ^ b;
}


static inline unsigned long xor(unsigned long a, unsigned long b,
				unsigned long mask)
{
	return (a & mask) ^ b;
}


	/*
	 *  Unaligned forward bit copy using 32-bit or 64-bit memory accesses
	 */

static void bitcpy(unsigned long *dst, int dst_idx, const unsigned long *src,
		   int src_idx, u32 n)
{
	unsigned long first, last;
	int shift = dst_idx - src_idx, left, right;
	unsigned long d0, d1;
	int m;

	if (!n)
		return;

	shift = dst_idx - src_idx;
	first = ~0UL >> dst_idx;
	last = ~(~0UL >> ((dst_idx + n) % BITS_PER_LONG));

	if (!shift) {
		// Same alignment for source and dest

		if (dst_idx + n <= BITS_PER_LONG) {
			// Single word
			if (last)
				first &= last;
			*dst = comp(*src, *dst, first);
		} else {
			// Multiple destination words
			// Leading bits
			if (first) {
				*dst = comp(*src, *dst, first);
				dst++;
				src++;
				n -= BITS_PER_LONG - dst_idx;
			}

			// Main chunk
			n /= BITS_PER_LONG;
			while (n >= 8) {
				*dst++ = *src++;
				*dst++ = *src++;
				*dst++ = *src++;
				*dst++ = *src++;
				*dst++ = *src++;
				*dst++ = *src++;
				*dst++ = *src++;
				*dst++ = *src++;
				n -= 8;
			}
			while (n--)
				*dst++ = *src++;

			// Trailing bits
			if (last)
				*dst = comp(*src, *dst, last);
		}
	} else {
		// Different alignment for source and dest

		right = shift & (BITS_PER_LONG - 1);
		left = -shift & (BITS_PER_LONG - 1);

		if (dst_idx + n <= BITS_PER_LONG) {
			// Single destination word
			if (last)
				first &= last;
			if (shift > 0) {
				// Single source word
				*dst = comp(*src >> right, *dst, first);
			} else if (src_idx + n <= BITS_PER_LONG) {
				// Single source word
				*dst = comp(*src << left, *dst, first);
			} else {
				// 2 source words
				d0 = *src++;
				d1 = *src;
				*dst = comp(d0 << left | d1 >> right, *dst,
					    first);
			}
		} else {
			// Multiple destination words
			d0 = *src++;
			// Leading bits
			if (shift > 0) {
				// Single source word
				*dst = comp(d0 >> right, *dst, first);
				dst++;
				n -= BITS_PER_LONG - dst_idx;
			} else {
				// 2 source words
				d1 = *src++;
				*dst = comp(d0 << left | d1 >> right, *dst,
					    first);
				d0 = d1;
				dst++;
				n -= BITS_PER_LONG - dst_idx;
			}

			// Main chunk
			m = n % BITS_PER_LONG;
			n /= BITS_PER_LONG;
			while (n >= 4) {
				d1 = *src++;
				*dst++ = d0 << left | d1 >> right;
				d0 = d1;
				d1 = *src++;
				*dst++ = d0 << left | d1 >> right;
				d0 = d1;
				d1 = *src++;
				*dst++ = d0 << left | d1 >> right;
				d0 = d1;
				d1 = *src++;
				*dst++ = d0 << left | d1 >> right;
				d0 = d1;
				n -= 4;
			}
			while (n--) {
				d1 = *src++;
				*dst++ = d0 << left | d1 >> right;
				d0 = d1;
			}

			// Trailing bits
			if (last) {
				if (m <= right) {
					// Single source word
					*dst = comp(d0 << left, *dst, last);
				} else {
					// 2 source words
					d1 = *src;
					*dst = comp(d0 << left | d1 >> right,
						    *dst, last);
				}
			}
		}
	}
}


	/*
	 *  Unaligned reverse bit copy using 32-bit or 64-bit memory accesses
	 */

static void bitcpy_rev(unsigned long *dst, int dst_idx,
		       const unsigned long *src, int src_idx, u32 n)
{
	unsigned long first, last;
	int shift = dst_idx - src_idx, left, right;
	unsigned long d0, d1;
	int m;

	if (!n)
		return;

	dst += (n - 1) / BITS_PER_LONG;
	src += (n - 1) / BITS_PER_LONG;
	if ((n - 1) % BITS_PER_LONG) {
		dst_idx += (n - 1) % BITS_PER_LONG;
		dst += dst_idx >> SHIFT_PER_LONG;
		dst_idx &= BITS_PER_LONG - 1;
		src_idx += (n - 1) % BITS_PER_LONG;
		src += src_idx >> SHIFT_PER_LONG;
		src_idx &= BITS_PER_LONG - 1;
	}

	shift = dst_idx - src_idx;
	first = ~0UL << (BITS_PER_LONG - 1 - dst_idx);
	last = ~(~0UL << (BITS_PER_LONG - 1 - ((dst_idx - n) % BITS_PER_LONG)));

	if (!shift) {
		// Same alignment for source and dest

		if ((unsigned long)dst_idx + 1 >= n) {
			// Single word
			if (last)
				first &= last;
			*dst = comp(*src, *dst, first);
		} else {
			// Multiple destination words
			// Leading bits
			if (first) {
				*dst = comp(*src, *dst, first);
				dst--;
				src--;
				n -= dst_idx + 1;
			}

			// Main chunk
			n /= BITS_PER_LONG;
			while (n >= 8) {
				*dst-- = *src--;
				*dst-- = *src--;
				*dst-- = *src--;
				*dst-- = *src--;
				*dst-- = *src--;
				*dst-- = *src--;
				*dst-- = *src--;
				*dst-- = *src--;
				n -= 8;
			}
			while (n--)
				*dst-- = *src--;

			// Trailing bits
			if (last)
				*dst = comp(*src, *dst, last);
		}
	} else {
		// Different alignment for source and dest

		right = shift & (BITS_PER_LONG - 1);
		left = -shift & (BITS_PER_LONG - 1);

		if ((unsigned long)dst_idx + 1 >= n) {
			// Single destination word
			if (last)
				first &= last;
			if (shift < 0) {
				// Single source word
				*dst = comp(*src << left, *dst, first);
			} else if (1 + (unsigned long)src_idx >= n) {
				// Single source word
				*dst = comp(*src >> right, *dst, first);
			} else {
				// 2 source words
				d0 = *src--;
				d1 = *src;
				*dst = comp(d0 >> right | d1 << left, *dst,
					    first);
			}
		} else {
			// Multiple destination words
			d0 = *src--;
			// Leading bits
			if (shift < 0) {
				// Single source word
				*dst = comp(d0 << left, *dst, first);
				dst--;
				n -= dst_idx + 1;
			} else {
				// 2 source words
				d1 = *src--;
				*dst = comp(d0 >> right | d1 << left, *dst,
					    first);
				d0 = d1;
				dst--;
				n -= dst_idx + 1;
			}

			// Main chunk
			m = n % BITS_PER_LONG;
			n /= BITS_PER_LONG;
			while (n >= 4) {
				d1 = *src--;
				*dst-- = d0 >> right | d1 << left;
				d0 = d1;
				d1 = *src--;
				*dst-- = d0 >> right | d1 << left;
				d0 = d1;
				d1 = *src--;
				*dst-- = d0 >> right | d1 << left;
				d0 = d1;
				d1 = *src--;
				*dst-- = d0 >> right | d1 << left;
				d0 = d1;
				n -= 4;
			}
			while (n--) {
				d1 = *src--;
				*dst-- = d0 >> right | d1 << left;
				d0 = d1;
			}

			// Trailing bits
			if (last) {
				if (m <= left) {
					// Single source word
					*dst = comp(d0 >> right, *dst, last);
				} else {
					// 2 source words
					d1 = *src;
					*dst = comp(d0 >> right | d1 << left,
						    *dst, last);
				}
			}
		}
	}
}


	/*
	 *  Unaligned forward inverting bit copy using 32-bit or 64-bit memory
	 *  accesses
	 */

static void bitcpy_not(unsigned long *dst, int dst_idx,
		       const unsigned long *src, int src_idx, u32 n)
{
	unsigned long first, last;
	int shift = dst_idx - src_idx, left, right;
	unsigned long d0, d1;
	int m;

	if (!n)
		return;

	shift = dst_idx - src_idx;
	first = ~0UL >> dst_idx;
	last = ~(~0UL >> ((dst_idx + n) % BITS_PER_LONG));

	if (!shift) {
		// Same alignment for source and dest

		if (dst_idx + n <= BITS_PER_LONG) {
			// Single word
			if (last)
				first &= last;
			*dst = comp(~*src, *dst, first);
		} else {
			// Multiple destination words
			// Leading bits
			if (first) {
				*dst = comp(~*src, *dst, first);
				dst++;
				src++;
				n -= BITS_PER_LONG - dst_idx;
			}

			// Main chunk
			n /= BITS_PER_LONG;
			while (n >= 8) {
				*dst++ = ~*src++;
				*dst++ = ~*src++;
				*dst++ = ~*src++;
				*dst++ = ~*src++;
				*dst++ = ~*src++;
				*dst++ = ~*src++;
				*dst++ = ~*src++;
				*dst++ = ~*src++;
				n -= 8;
			}
			while (n--)
				*dst++ = ~*src++;

			// Trailing bits
			if (last)
				*dst = comp(~*src, *dst, last);
		}
	} else {
		// Different alignment for source and dest

		right = shift & (BITS_PER_LONG - 1);
		left = -shift & (BITS_PER_LONG - 1);

		if (dst_idx + n <= BITS_PER_LONG) {
			// Single destination word
			if (last)
				first &= last;
			if (shift > 0) {
				// Single source word
				*dst = comp(~*src >> right, *dst, first);
			} else if (src_idx + n <= BITS_PER_LONG) {
				// Single source word
				*dst = comp(~*src << left, *dst, first);
			} else {
				// 2 source words
				d0 = ~*src++;
				d1 = ~*src;
				*dst = comp(d0 << left | d1 >> right, *dst,
					    first);
			}
		} else {
			// Multiple destination words
			d0 = ~*src++;
			// Leading bits
			if (shift > 0) {
				// Single source word
				*dst = comp(d0 >> right, *dst, first);
				dst++;
				n -= BITS_PER_LONG - dst_idx;
			} else {
				// 2 source words
				d1 = ~*src++;
				*dst = comp(d0 << left | d1 >> right, *dst,
					    first);
				d0 = d1;
				dst++;
				n -= BITS_PER_LONG - dst_idx;
			}

			// Main chunk
			m = n % BITS_PER_LONG;
			n /= BITS_PER_LONG;
			while (n >= 4) {
				d1 = ~*src++;
				*dst++ = d0 << left | d1 >> right;
				d0 = d1;
				d1 = ~*src++;
				*dst++ = d0 << left | d1 >> right;
				d0 = d1;
				d1 = ~*src++;
				*dst++ = d0 << left | d1 >> right;
				d0 = d1;
				d1 = ~*src++;
				*dst++ = d0 << left | d1 >> right;
				d0 = d1;
				n -= 4;
			}
			while (n--) {
				d1 = ~*src++;
				*dst++ = d0 << left | d1 >> right;
				d0 = d1;
			}

			// Trailing bits
			if (last) {
				if (m <= right) {
					// Single source word
					*dst = comp(d0 << left, *dst, last);
				} else {
					// 2 source words
					d1 = ~*src;
					*dst = comp(d0 << left | d1 >> right,
						    *dst, last);
				}
			}
		}
	}
}


	/*
	 *  Unaligned 32-bit pattern fill using 32/64-bit memory accesses
	 */

static void bitfill32(unsigned long *dst, int dst_idx, u32 pat, u32 n)
{
	unsigned long val = pat;
	unsigned long first, last;

	if (!n)
		return;

#if BITS_PER_LONG == 64
	val |= val << 32;
#endif

	first = ~0UL >> dst_idx;
	last = ~(~0UL >> ((dst_idx + n) % BITS_PER_LONG));

	if (dst_idx + n <= BITS_PER_LONG) {
		// Single word
		if (last)
			first &= last;
		*dst = comp(val, *dst, first);
	} else {
		// Multiple destination words
		// Leading bits
		if (first) {
			*dst = comp(val, *dst, first);
			dst++;
			n -= BITS_PER_LONG - dst_idx;
		}

		// Main chunk
		n /= BITS_PER_LONG;
		while (n >= 8) {
			*dst++ = val;
			*dst++ = val;
			*dst++ = val;
			*dst++ = val;
			*dst++ = val;
			*dst++ = val;
			*dst++ = val;
			*dst++ = val;
			n -= 8;
		}
		while (n--)
			*dst++ = val;

		// Trailing bits
		if (last)
			*dst = comp(val, *dst, last);
	}
}


	/*
	 *  Unaligned 32-bit pattern xor using 32/64-bit memory accesses
	 */

static void bitxor32(unsigned long *dst, int dst_idx, u32 pat, u32 n)
{
	unsigned long val = pat;
	unsigned long first, last;

	if (!n)
		return;

#if BITS_PER_LONG == 64
	val |= val << 32;
#endif

	first = ~0UL >> dst_idx;
	last = ~(~0UL >> ((dst_idx + n) % BITS_PER_LONG));

	if (dst_idx + n <= BITS_PER_LONG) {
		// Single word
		if (last)
			first &= last;
		*dst = xor(val, *dst, first);
	} else {
		// Multiple destination words
		// Leading bits
		if (first) {
			*dst = xor(val, *dst, first);
			dst++;
			n -= BITS_PER_LONG - dst_idx;
		}

		// Main chunk
		n /= BITS_PER_LONG;
		while (n >= 4) {
			*dst++ ^= val;
			*dst++ ^= val;
			*dst++ ^= val;
			*dst++ ^= val;
			n -= 4;
		}
		while (n--)
			*dst++ ^= val;

		// Trailing bits
		if (last)
			*dst = xor(val, *dst, last);
	}
}

static inline void fill_one_line(int bpp, unsigned long next_plane,
				 unsigned long *dst, int dst_idx, u32 n,
				 u32 color)
{
	while (1) {
		dst += dst_idx >> SHIFT_PER_LONG;
		dst_idx &= (BITS_PER_LONG - 1);
		bitfill32(dst, dst_idx, color & 1 ? ~0 : 0, n);
		if (!--bpp)
			break;
		color >>= 1;
		dst_idx += next_plane * 8;
	}
}

static inline void xor_one_line(int bpp, unsigned long next_plane,
				unsigned long *dst, int dst_idx, u32 n,
				u32 color)
{
	while (color) {
		dst += dst_idx >> SHIFT_PER_LONG;
		dst_idx &= (BITS_PER_LONG - 1);
		bitxor32(dst, dst_idx, color & 1 ? ~0 : 0, n);
		if (!--bpp)
			break;
		color >>= 1;
		dst_idx += next_plane * 8;
	}
}


static void amifb_fillrect(struct fb_info *info,
			   const struct fb_fillrect *rect)
{
	struct amifb_par *par = info->par;
	int dst_idx, x2, y2;
	unsigned long *dst;
	u32 width, height;

	if (!rect->width || !rect->height)
		return;

	/*
	 * We could use hardware clipping but on many cards you get around
	 * hardware clipping by writing to framebuffer directly.
	 * */
	x2 = rect->dx + rect->width;
	y2 = rect->dy + rect->height;
	x2 = x2 < info->var.xres_virtual ? x2 : info->var.xres_virtual;
	y2 = y2 < info->var.yres_virtual ? y2 : info->var.yres_virtual;
	width = x2 - rect->dx;
	height = y2 - rect->dy;

	dst = (unsigned long *)
		((unsigned long)info->screen_base & ~(BYTES_PER_LONG - 1));
	dst_idx = ((unsigned long)info->screen_base & (BYTES_PER_LONG - 1)) * 8;
	dst_idx += rect->dy * par->next_line * 8 + rect->dx;
	while (height--) {
		switch (rect->rop) {
		case ROP_COPY:
			fill_one_line(info->var.bits_per_pixel,
				      par->next_plane, dst, dst_idx, width,
				      rect->color);
			break;

		case ROP_XOR:
			xor_one_line(info->var.bits_per_pixel, par->next_plane,
				     dst, dst_idx, width, rect->color);
			break;
		}
		dst_idx += par->next_line * 8;
	}
}

static inline void copy_one_line(int bpp, unsigned long next_plane,
				 unsigned long *dst, int dst_idx,
				 unsigned long *src, int src_idx, u32 n)
{
	while (1) {
		dst += dst_idx >> SHIFT_PER_LONG;
		dst_idx &= (BITS_PER_LONG - 1);
		src += src_idx >> SHIFT_PER_LONG;
		src_idx &= (BITS_PER_LONG - 1);
		bitcpy(dst, dst_idx, src, src_idx, n);
		if (!--bpp)
			break;
		dst_idx += next_plane * 8;
		src_idx += next_plane * 8;
	}
}

static inline void copy_one_line_rev(int bpp, unsigned long next_plane,
				     unsigned long *dst, int dst_idx,
				     unsigned long *src, int src_idx, u32 n)
{
	while (1) {
		dst += dst_idx >> SHIFT_PER_LONG;
		dst_idx &= (BITS_PER_LONG - 1);
		src += src_idx >> SHIFT_PER_LONG;
		src_idx &= (BITS_PER_LONG - 1);
		bitcpy_rev(dst, dst_idx, src, src_idx, n);
		if (!--bpp)
			break;
		dst_idx += next_plane * 8;
		src_idx += next_plane * 8;
	}
}


static void amifb_copyarea(struct fb_info *info,
			   const struct fb_copyarea *area)
{
	struct amifb_par *par = info->par;
	int x2, y2;
	u32 dx, dy, sx, sy, width, height;
	unsigned long *dst, *src;
	int dst_idx, src_idx;
	int rev_copy = 0;

	/* clip the destination */
	x2 = area->dx + area->width;
	y2 = area->dy + area->height;
	dx = area->dx > 0 ? area->dx : 0;
	dy = area->dy > 0 ? area->dy : 0;
	x2 = x2 < info->var.xres_virtual ? x2 : info->var.xres_virtual;
	y2 = y2 < info->var.yres_virtual ? y2 : info->var.yres_virtual;
	width = x2 - dx;
	height = y2 - dy;

	if (area->sx + dx < area->dx || area->sy + dy < area->dy)
		return;

	/* update sx,sy */
	sx = area->sx + (dx - area->dx);
	sy = area->sy + (dy - area->dy);

	/* the source must be completely inside the virtual screen */
	if (sx + width > info->var.xres_virtual ||
			sy + height > info->var.yres_virtual)
		return;

	if (dy > sy || (dy == sy && dx > sx)) {
		dy += height;
		sy += height;
		rev_copy = 1;
	}
	dst = (unsigned long *)
		((unsigned long)info->screen_base & ~(BYTES_PER_LONG - 1));
	src = dst;
	dst_idx = ((unsigned long)info->screen_base & (BYTES_PER_LONG - 1)) * 8;
	src_idx = dst_idx;
	dst_idx += dy * par->next_line * 8 + dx;
	src_idx += sy * par->next_line * 8 + sx;
	if (rev_copy) {
		while (height--) {
			dst_idx -= par->next_line * 8;
			src_idx -= par->next_line * 8;
			copy_one_line_rev(info->var.bits_per_pixel,
					  par->next_plane, dst, dst_idx, src,
					  src_idx, width);
		}
	} else {
		while (height--) {
			copy_one_line(info->var.bits_per_pixel,
				      par->next_plane, dst, dst_idx, src,
				      src_idx, width);
			dst_idx += par->next_line * 8;
			src_idx += par->next_line * 8;
		}
	}
}


static inline void expand_one_line(int bpp, unsigned long next_plane,
				   unsigned long *dst, int dst_idx, u32 n,
				   const u8 *data, u32 bgcolor, u32 fgcolor)
{
	const unsigned long *src;
	int src_idx;

	while (1) {
		dst += dst_idx >> SHIFT_PER_LONG;
		dst_idx &= (BITS_PER_LONG - 1);
		if ((bgcolor ^ fgcolor) & 1) {
			src = (unsigned long *)
				((unsigned long)data & ~(BYTES_PER_LONG - 1));
			src_idx = ((unsigned long)data & (BYTES_PER_LONG - 1)) * 8;
			if (fgcolor & 1)
				bitcpy(dst, dst_idx, src, src_idx, n);
			else
				bitcpy_not(dst, dst_idx, src, src_idx, n);
			/* set or clear */
		} else
			bitfill32(dst, dst_idx, fgcolor & 1 ? ~0 : 0, n);
		if (!--bpp)
			break;
		bgcolor >>= 1;
		fgcolor >>= 1;
		dst_idx += next_plane * 8;
	}
}


static void amifb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct amifb_par *par = info->par;
	int x2, y2;
	unsigned long *dst;
	int dst_idx;
	const char *src;
	u32 dx, dy, width, height, pitch;

	/*
	 * We could use hardware clipping but on many cards you get around
	 * hardware clipping by writing to framebuffer directly like we are
	 * doing here.
	 */
	x2 = image->dx + image->width;
	y2 = image->dy + image->height;
	dx = image->dx;
	dy = image->dy;
	x2 = x2 < info->var.xres_virtual ? x2 : info->var.xres_virtual;
	y2 = y2 < info->var.yres_virtual ? y2 : info->var.yres_virtual;
	width  = x2 - dx;
	height = y2 - dy;

	if (image->depth == 1) {
		dst = (unsigned long *)
			((unsigned long)info->screen_base & ~(BYTES_PER_LONG - 1));
		dst_idx = ((unsigned long)info->screen_base & (BYTES_PER_LONG - 1)) * 8;
		dst_idx += dy * par->next_line * 8 + dx;
		src = image->data;
		pitch = (image->width + 7) / 8;
		while (height--) {
			expand_one_line(info->var.bits_per_pixel,
					par->next_plane, dst, dst_idx, width,
					src, image->bg_color,
					image->fg_color);
			dst_idx += par->next_line * 8;
			src += pitch;
		}
	} else {
		c2p_planar(info->screen_base, image->data, dx, dy, width,
			   height, par->next_line, par->next_plane,
			   image->width, info->var.bits_per_pixel);
	}
}


	/*
	 * Amiga Frame Buffer Specific ioctls
	 */

static int amifb_ioctl(struct fb_info *info,
		       unsigned int cmd, unsigned long arg)
{
	union {
		struct fb_fix_cursorinfo fix;
		struct fb_var_cursorinfo var;
		struct fb_cursorstate state;
	} crsr;
	void __user *argp = (void __user *)arg;
	int i;

	switch (cmd) {
	case FBIOGET_FCURSORINFO:
		i = ami_get_fix_cursorinfo(&crsr.fix, info->par);
		if (i)
			return i;
		return copy_to_user(argp, &crsr.fix,
				    sizeof(crsr.fix)) ? -EFAULT : 0;

	case FBIOGET_VCURSORINFO:
		i = ami_get_var_cursorinfo(&crsr.var,
			((struct fb_var_cursorinfo __user *)arg)->data,
			info->par);
		if (i)
			return i;
		return copy_to_user(argp, &crsr.var,
				    sizeof(crsr.var)) ? -EFAULT : 0;

	case FBIOPUT_VCURSORINFO:
		if (copy_from_user(&crsr.var, argp, sizeof(crsr.var)))
			return -EFAULT;
		return ami_set_var_cursorinfo(&crsr.var,
			((struct fb_var_cursorinfo __user *)arg)->data,
			info->par);

	case FBIOGET_CURSORSTATE:
		i = ami_get_cursorstate(&crsr.state, info->par);
		if (i)
			return i;
		return copy_to_user(argp, &crsr.state,
				    sizeof(crsr.state)) ? -EFAULT : 0;

	case FBIOPUT_CURSORSTATE:
		if (copy_from_user(&crsr.state, argp, sizeof(crsr.state)))
			return -EFAULT;
		return ami_set_cursorstate(&crsr.state, info->par);
	}
	return -EINVAL;
}


	/*
	 * Flash the cursor (called by VBlank interrupt)
	 */

static int flash_cursor(void)
{
	static int cursorcount = 1;

	if (cursormode == FB_CURSOR_FLASH) {
		if (!--cursorcount) {
			cursorstate = -cursorstate;
			cursorcount = cursorrate;
			if (!is_blanked)
				return 1;
		}
	}
	return 0;
}

	/*
	 * VBlank Display Interrupt
	 */

static irqreturn_t amifb_interrupt(int irq, void *dev_id)
{
	struct amifb_par *par = dev_id;

	if (do_vmode_pan || do_vmode_full)
		ami_update_display(par);

	if (do_vmode_full)
		ami_init_display(par);

	if (do_vmode_pan) {
		flash_cursor();
		ami_rebuild_copper(par);
		do_cursor = do_vmode_pan = 0;
	} else if (do_cursor) {
		flash_cursor();
		ami_set_sprite(par);
		do_cursor = 0;
	} else {
		if (flash_cursor())
			ami_set_sprite(par);
	}

	if (do_blank) {
		ami_do_blank(par);
		do_blank = 0;
	}

	if (do_vmode_full) {
		ami_reinit_copper(par);
		do_vmode_full = 0;
	}
	return IRQ_HANDLED;
}


static struct fb_ops amifb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= amifb_check_var,
	.fb_set_par	= amifb_set_par,
	.fb_setcolreg	= amifb_setcolreg,
	.fb_blank	= amifb_blank,
	.fb_pan_display	= amifb_pan_display,
	.fb_fillrect	= amifb_fillrect,
	.fb_copyarea	= amifb_copyarea,
	.fb_imageblit	= amifb_imageblit,
	.fb_ioctl	= amifb_ioctl,
};


	/*
	 * Allocate, Clear and Align a Block of Chip Memory
	 */

static void *aligned_chipptr;

static inline u_long __init chipalloc(u_long size)
{
	aligned_chipptr = amiga_chip_alloc(size, "amifb [RAM]");
	if (!aligned_chipptr) {
		pr_err("amifb: No Chip RAM for frame buffer");
		return 0;
	}
	memset(aligned_chipptr, 0, size);
	return (u_long)aligned_chipptr;
}

static inline void chipfree(void)
{
	if (aligned_chipptr)
		amiga_chip_free(aligned_chipptr);
}


	/*
	 * Initialisation
	 */

static int __init amifb_probe(struct platform_device *pdev)
{
	struct fb_info *info;
	int tag, i, err = 0;
	u_long chipptr;
	u_int defmode;

#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("amifb", &option)) {
		amifb_video_off();
		return -ENODEV;
	}
	amifb_setup(option);
#endif
	custom.dmacon = DMAF_ALL | DMAF_MASTER;

	info = framebuffer_alloc(sizeof(struct amifb_par), &pdev->dev);
	if (!info) {
		dev_err(&pdev->dev, "framebuffer_alloc failed\n");
		return -ENOMEM;
	}

	strcpy(info->fix.id, "Amiga ");
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	info->fix.accel = FB_ACCEL_AMIGABLITT;

	switch (amiga_chipset) {
#ifdef CONFIG_FB_AMIGA_OCS
	case CS_OCS:
		strcat(info->fix.id, "OCS");
default_chipset:
		chipset = TAG_OCS;
		maxdepth[TAG_SHRES] = 0;	/* OCS means no SHRES */
		maxdepth[TAG_HIRES] = 4;
		maxdepth[TAG_LORES] = 6;
		maxfmode = TAG_FMODE_1;
		defmode = amiga_vblank == 50 ? DEFMODE_PAL : DEFMODE_NTSC;
		info->fix.smem_len = VIDEOMEMSIZE_OCS;
		break;
#endif /* CONFIG_FB_AMIGA_OCS */

#ifdef CONFIG_FB_AMIGA_ECS
	case CS_ECS:
		strcat(info->fix.id, "ECS");
		chipset = TAG_ECS;
		maxdepth[TAG_SHRES] = 2;
		maxdepth[TAG_HIRES] = 4;
		maxdepth[TAG_LORES] = 6;
		maxfmode = TAG_FMODE_1;
		if (AMIGAHW_PRESENT(AMBER_FF))
			defmode = amiga_vblank == 50 ? DEFMODE_AMBER_PAL
						     : DEFMODE_AMBER_NTSC;
		else
			defmode = amiga_vblank == 50 ? DEFMODE_PAL
						     : DEFMODE_NTSC;
		if (amiga_chip_avail() - CHIPRAM_SAFETY_LIMIT >
		    VIDEOMEMSIZE_ECS_2M)
			info->fix.smem_len = VIDEOMEMSIZE_ECS_2M;
		else
			info->fix.smem_len = VIDEOMEMSIZE_ECS_1M;
		break;
#endif /* CONFIG_FB_AMIGA_ECS */

#ifdef CONFIG_FB_AMIGA_AGA
	case CS_AGA:
		strcat(info->fix.id, "AGA");
		chipset = TAG_AGA;
		maxdepth[TAG_SHRES] = 8;
		maxdepth[TAG_HIRES] = 8;
		maxdepth[TAG_LORES] = 8;
		maxfmode = TAG_FMODE_4;
		defmode = DEFMODE_AGA;
		if (amiga_chip_avail() - CHIPRAM_SAFETY_LIMIT >
		    VIDEOMEMSIZE_AGA_2M)
			info->fix.smem_len = VIDEOMEMSIZE_AGA_2M;
		else
			info->fix.smem_len = VIDEOMEMSIZE_AGA_1M;
		break;
#endif /* CONFIG_FB_AMIGA_AGA */

	default:
#ifdef CONFIG_FB_AMIGA_OCS
		printk("Unknown graphics chipset, defaulting to OCS\n");
		strcat(info->fix.id, "Unknown");
		goto default_chipset;
#else /* CONFIG_FB_AMIGA_OCS */
		err = -ENODEV;
		goto release;
#endif /* CONFIG_FB_AMIGA_OCS */
		break;
	}

	/*
	 * Calculate the Pixel Clock Values for this Machine
	 */

	{
	u_long tmp = DIVUL(200000000000ULL, amiga_eclock);

	pixclock[TAG_SHRES] = (tmp + 4) / 8;	/* SHRES:  35 ns / 28 MHz */
	pixclock[TAG_HIRES] = (tmp + 2) / 4;	/* HIRES:  70 ns / 14 MHz */
	pixclock[TAG_LORES] = (tmp + 1) / 2;	/* LORES: 140 ns /  7 MHz */
	}

	/*
	 * Replace the Tag Values with the Real Pixel Clock Values
	 */

	for (i = 0; i < NUM_TOTAL_MODES; i++) {
		struct fb_videomode *mode = &ami_modedb[i];
		tag = mode->pixclock;
		if (tag == TAG_SHRES || tag == TAG_HIRES || tag == TAG_LORES) {
			mode->pixclock = pixclock[tag];
		}
	}

	if (amifb_hfmin) {
		info->monspecs.hfmin = amifb_hfmin;
		info->monspecs.hfmax = amifb_hfmax;
		info->monspecs.vfmin = amifb_vfmin;
		info->monspecs.vfmax = amifb_vfmax;
	} else {
		/*
		 *  These are for a typical Amiga monitor (e.g. A1960)
		 */
		info->monspecs.hfmin = 15000;
		info->monspecs.hfmax = 38000;
		info->monspecs.vfmin = 49;
		info->monspecs.vfmax = 90;
	}

	info->fbops = &amifb_ops;
	info->flags = FBINFO_DEFAULT;
	info->device = &pdev->dev;

	if (!fb_find_mode(&info->var, info, mode_option, ami_modedb,
			  NUM_TOTAL_MODES, &ami_modedb[defmode], 4)) {
		err = -EINVAL;
		goto release;
	}

	fb_videomode_to_modelist(ami_modedb, NUM_TOTAL_MODES,
				 &info->modelist);

	round_down_bpp = 0;
	chipptr = chipalloc(info->fix.smem_len + SPRITEMEMSIZE +
			    DUMMYSPRITEMEMSIZE + COPINITSIZE +
			    4 * COPLISTSIZE);
	if (!chipptr) {
		err = -ENOMEM;
		goto release;
	}

	assignchunk(videomemory, u_long, chipptr, info->fix.smem_len);
	assignchunk(spritememory, u_long, chipptr, SPRITEMEMSIZE);
	assignchunk(dummysprite, u_short *, chipptr, DUMMYSPRITEMEMSIZE);
	assignchunk(copdisplay.init, copins *, chipptr, COPINITSIZE);
	assignchunk(copdisplay.list[0][0], copins *, chipptr, COPLISTSIZE);
	assignchunk(copdisplay.list[0][1], copins *, chipptr, COPLISTSIZE);
	assignchunk(copdisplay.list[1][0], copins *, chipptr, COPLISTSIZE);
	assignchunk(copdisplay.list[1][1], copins *, chipptr, COPLISTSIZE);

	/*
	 * access the videomem with writethrough cache
	 */
	info->fix.smem_start = (u_long)ZTWO_PADDR(videomemory);
	videomemory = (u_long)ioremap_wt(info->fix.smem_start,
					 info->fix.smem_len);
	if (!videomemory) {
		dev_warn(&pdev->dev,
			 "Unable to map videomem cached writethrough\n");
		info->screen_base = ZTWO_VADDR(info->fix.smem_start);
	} else
		info->screen_base = (char *)videomemory;

	memset(dummysprite, 0, DUMMYSPRITEMEMSIZE);

	/*
	 * Make sure the Copper has something to do
	 */
	ami_init_copper();

	/*
	 * Enable Display DMA
	 */
	custom.dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER |
			DMAF_BLITTER | DMAF_SPRITE;

	err = request_irq(IRQ_AMIGA_COPPER, amifb_interrupt, 0,
			  "fb vertb handler", info->par);
	if (err)
		goto disable_dma;

	err = fb_alloc_cmap(&info->cmap, 1 << info->var.bits_per_pixel, 0);
	if (err)
		goto free_irq;

	dev_set_drvdata(&pdev->dev, info);

	err = register_framebuffer(info);
	if (err)
		goto unset_drvdata;

	fb_info(info, "%s frame buffer device, using %dK of video memory\n",
		info->fix.id, info->fix.smem_len>>10);

	return 0;

unset_drvdata:
	fb_dealloc_cmap(&info->cmap);
free_irq:
	free_irq(IRQ_AMIGA_COPPER, info->par);
disable_dma:
	custom.dmacon = DMAF_ALL | DMAF_MASTER;
	if (videomemory)
		iounmap((void *)videomemory);
	chipfree();
release:
	framebuffer_release(info);
	return err;
}


static int __exit amifb_remove(struct platform_device *pdev)
{
	struct fb_info *info = dev_get_drvdata(&pdev->dev);

	unregister_framebuffer(info);
	fb_dealloc_cmap(&info->cmap);
	free_irq(IRQ_AMIGA_COPPER, info->par);
	custom.dmacon = DMAF_ALL | DMAF_MASTER;
	if (videomemory)
		iounmap((void *)videomemory);
	chipfree();
	framebuffer_release(info);
	amifb_video_off();
	return 0;
}

static struct platform_driver amifb_driver = {
	.remove = __exit_p(amifb_remove),
	.driver   = {
		.name	= "amiga-video",
	},
};

module_platform_driver_probe(amifb_driver, amifb_probe);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:amiga-video");
