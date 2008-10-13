/* lcd-panel.h

   written by Marc Singer
   18 Jul 2005

   Copyright (C) 2005 Marc Singer

   -----------
   DESCRIPTION
   -----------

   Only one panel may be defined at a time.

   The pixel clock is calculated to be no greater than the target.

   Each timing value is accompanied by a specification comment.

     UNITS/MIN/TYP/MAX

   Most of the units will be in clocks.

   USE_RGB555

     Define this macro to configure the AMBA LCD controller to use an
     RGB555 encoding for the pels instead of the normal RGB565.

   LPD9520, LPD79524, LPD7A400, LPD7A404-10, LPD7A404-11

     These boards are best approximated by 555 for all panels.  Some
     can use an extra low-order bit of blue in bit 16 of the color
     value, but we don't have a way to communicate this non-linear
     mapping to the kernel.

*/

#if !defined (__LCD_PANEL_H__)
#    define   __LCD_PANEL_H__

#if defined (MACH_LPD79520)\
 || defined (MACH_LPD79524)\
 || defined (MACH_LPD7A400)\
 || defined (MACH_LPD7A404)
# define USE_RGB555
#endif

struct clcd_panel_extra {
	unsigned int hrmode;
	unsigned int clsen;
	unsigned int spsen;
	unsigned int pcdel;
	unsigned int revdel;
	unsigned int lpdel;
	unsigned int spldel;
	unsigned int pc2del;
};

#define NS_TO_CLOCK(ns,c)	((((ns)*((c)/1000) + (1000000 - 1))/1000000))
#define CLOCK_TO_DIV(e,c)	(((c) + (e) - 1)/(e))

#if defined CONFIG_FB_ARMCLCD_SHARP_LQ035Q7DB02_HRTFT

	/* Logic Product Development LCD 3.5" QVGA HRTFT -10 */
	/* Sharp PN LQ035Q7DB02 w/HRTFT controller chip */

#define PIX_CLOCK_TARGET	(6800000)
#define PIX_CLOCK_DIVIDER	CLOCK_TO_DIV (PIX_CLOCK_TARGET, HCLK)
#define PIX_CLOCK		(HCLK/PIX_CLOCK_DIVIDER)

static struct clcd_panel lcd_panel = {
	.mode	= {
		.name		= "3.5in QVGA (LQ035Q7DB02)",
		.xres		= 240,
		.yres		= 320,
		.pixclock	= PIX_CLOCK,
		.left_margin	= 16,
		.right_margin	= 21,
		.upper_margin	= 8,			// line/8/8/8
		.lower_margin	= 5,
		.hsync_len	= 61,
		.vsync_len	= NS_TO_CLOCK (60, PIX_CLOCK),
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_IPC | (PIX_CLOCK_DIVIDER - 2),
	.cntl		= CNTL_LCDTFT | CNTL_WATERMARK,
	.bpp		= 16,
};

#define HAS_LCD_PANEL_EXTRA

static struct clcd_panel_extra lcd_panel_extra = {
	.hrmode = 1,
	.clsen = 1,
	.spsen = 1,
	.pcdel = 8,
	.revdel = 7,
	.lpdel = 13,
	.spldel = 77,
	.pc2del = 208,
};

#endif

#if defined CONFIG_FB_ARMCLCD_SHARP_LQ057Q3DC02

	/* Logic Product Development LCD 5.7" QVGA -10 */
	/* Sharp PN LQ057Q3DC02 */
	/* QVGA mode, V/Q=LOW */

/* From Sharp on 2006.1.3.  I believe some of the values are incorrect
 * based on the datasheet.

    Timing0	TIMING1		TIMING2		CONTROL
    0x140A0C4C	0x080504EF	0x013F380D	0x00000829
    HBP= 20	VBP=  8		BCD=  0
    HFP= 10	VFP=  5		CPL=319
    HSW= 12	VSW=  1		IOE=  0
    PPL= 19	LPP=239		IPC=  1
				IHS=  1
				IVS=  1
				ACB=  0
				CSEL= 0
				PCD= 13

 */

/* The full horizontal cycle (Th) is clock/360/400/450. */
/* The full vertical   cycle (Tv) is line/251/262/280. */

#define PIX_CLOCK_TARGET	(6300000) /* -/6.3/7 MHz */
#define PIX_CLOCK_DIVIDER	CLOCK_TO_DIV (PIX_CLOCK_TARGET, HCLK)
#define PIX_CLOCK		(HCLK/PIX_CLOCK_DIVIDER)

static struct clcd_panel lcd_panel = {
	.mode	= {
		.name		= "5.7in QVGA (LQ057Q3DC02)",
		.xres		= 320,
		.yres		= 240,
		.pixclock	= PIX_CLOCK,
		.left_margin	= 11,
		.right_margin	= 400-11-320-2,
		.upper_margin	= 7,			// line/7/7/7
		.lower_margin	= 262-7-240-2,
		.hsync_len	= 2,			// clk/2/96/200
		.vsync_len	= 2,			// line/2/-/34
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_IHS | TIM2_IVS
			| (PIX_CLOCK_DIVIDER - 2),
	.cntl		= CNTL_LCDTFT | CNTL_WATERMARK,
	.bpp		= 16,
};

#endif

#if defined CONFIG_FB_ARMCLCD_SHARP_LQ64D343

	/* Logic Product Development LCD 6.4" VGA -10 */
	/* Sharp PN LQ64D343 */

/* The full horizontal cycle (Th) is clock/750/800/900. */
/* The full vertical   cycle (Tv) is line/515/525/560. */

#define PIX_CLOCK_TARGET	(28330000)
#define PIX_CLOCK_DIVIDER	CLOCK_TO_DIV (PIX_CLOCK_TARGET, HCLK)
#define PIX_CLOCK		(HCLK/PIX_CLOCK_DIVIDER)

static struct clcd_panel lcd_panel = {
	.mode	= {
		.name		= "6.4in QVGA (LQ64D343)",
		.xres		= 640,
		.yres		= 480,
		.pixclock	= PIX_CLOCK,
		.left_margin	= 32,
		.right_margin	= 800-32-640-96,
		.upper_margin	= 32,			// line/34/34/34
		.lower_margin	= 540-32-480-2,
		.hsync_len	= 96,			// clk/2/96/200
		.vsync_len	= 2,			// line/2/-/34
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_IHS | TIM2_IVS
			| (PIX_CLOCK_DIVIDER - 2),
	.cntl		= CNTL_LCDTFT | CNTL_WATERMARK,
	.bpp		= 16,
};

#endif

#if defined CONFIG_FB_ARMCLCD_SHARP_LQ10D368

	/* Logic Product Development LCD 10.4" VGA -10 */
	/* Sharp PN LQ10D368 */

#define PIX_CLOCK_TARGET	(28330000)
#define PIX_CLOCK_DIVIDER	CLOCK_TO_DIV (PIX_CLOCK_TARGET, HCLK)
#define PIX_CLOCK		(HCLK/PIX_CLOCK_DIVIDER)

static struct clcd_panel lcd_panel = {
	.mode	= {
		.name		= "10.4in VGA (LQ10D368)",
		.xres		= 640,
		.yres		= 480,
		.pixclock	= PIX_CLOCK,
		.left_margin	= 21,
		.right_margin	= 15,
		.upper_margin	= 34,
		.lower_margin	= 5,
		.hsync_len	= 96,
		.vsync_len	= 16,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_IHS | TIM2_IVS
			| (PIX_CLOCK_DIVIDER - 2),
	.cntl		= CNTL_LCDTFT | CNTL_WATERMARK,
	.bpp		= 16,
};

#endif

#if defined CONFIG_FB_ARMCLCD_SHARP_LQ121S1DG41

	/* Logic Product Development LCD 12.1" SVGA -10 */
	/* Sharp PN LQ121S1DG41, was LQ121S1DG31 */

/* Note that with a 99993900 Hz HCLK, it is not possible to hit the
 * target clock frequency range of 35MHz to 42MHz. */

/* If the target pixel clock is substantially lower than the panel
 * spec, this is done to prevent the LCD display from glitching when
 * the CPU is under load.  A pixel clock higher than 25MHz
 * (empirically determined) will compete with the CPU for bus cycles
 * for the Ethernet chip.  However, even a pixel clock of 10MHz
 * competes with Compact Flash interface during some operations
 * (fdisk, e2fsck).  And, at that speed the display may have a visible
 * flicker. */

/* The full horizontal cycle (Th) is clock/832/1056/1395. */

#define PIX_CLOCK_TARGET	(20000000)
#define PIX_CLOCK_DIVIDER	CLOCK_TO_DIV (PIX_CLOCK_TARGET, HCLK)
#define PIX_CLOCK		(HCLK/PIX_CLOCK_DIVIDER)

static struct clcd_panel lcd_panel = {
	.mode	= {
		.name		= "12.1in SVGA (LQ121S1DG41)",
		.xres		= 800,
		.yres		= 600,
		.pixclock	= PIX_CLOCK,
		.left_margin	= 89,		// ns/5/-/(1/PIX_CLOCK)-10
		.right_margin	= 1056-800-89-128,
		.upper_margin	= 23,		// line/23/23/23
		.lower_margin	= 44,
		.hsync_len	= 128,		// clk/2/128/200
		.vsync_len	= 4,		// line/2/4/6
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_IHS | TIM2_IVS
			| (PIX_CLOCK_DIVIDER - 2),
	.cntl		= CNTL_LCDTFT | CNTL_WATERMARK,
	.bpp		= 16,
};

#endif

#if defined CONFIG_FB_ARMCLCD_HITACHI

	/* Hitachi*/
	/* Submitted by Michele Da Rold <michele.darold@ecsproject.com> */

#define PIX_CLOCK_TARGET	(49000000)
#define PIX_CLOCK_DIVIDER	CLOCK_TO_DIV (PIX_CLOCK_TARGET, HCLK)
#define PIX_CLOCK		(HCLK/PIX_CLOCK_DIVIDER)

static struct clcd_panel lcd_panel = {
	.mode	= {
		.name		= "Hitachi 800x480",
		.xres		= 800,
		.yres		= 480,
		.pixclock	= PIX_CLOCK,
		.left_margin	= 88,
		.right_margin	= 40,
		.upper_margin	= 32,
		.lower_margin	= 11,
		.hsync_len	= 128,
		.vsync_len	= 2,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_IPC | TIM2_IHS | TIM2_IVS
			| (PIX_CLOCK_DIVIDER - 2),
	.cntl		= CNTL_LCDTFT | CNTL_WATERMARK,
	.bpp		= 16,
};

#endif


#if defined CONFIG_FB_ARMCLCD_AUO_A070VW01_WIDE

	/* AU Optotronics  A070VW01 7.0 Wide Screen color Display*/
	/* Submitted by Michele Da Rold <michele.darold@ecsproject.com> */

#define PIX_CLOCK_TARGET	(10000000)
#define PIX_CLOCK_DIVIDER	CLOCK_TO_DIV (PIX_CLOCK_TARGET, HCLK)
#define PIX_CLOCK		(HCLK/PIX_CLOCK_DIVIDER)

static struct clcd_panel lcd_panel = {
	.mode	= {
		.name		= "7.0in Wide (A070VW01)",
		.xres		= 480,
		.yres		= 234,
		.pixclock	= PIX_CLOCK,
		.left_margin	= 30,
		.right_margin	= 25,
		.upper_margin	= 14,
		.lower_margin	= 12,
		.hsync_len	= 100,
		.vsync_len	= 1,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_IPC | TIM2_IHS | TIM2_IVS
			| (PIX_CLOCK_DIVIDER - 2),
	.cntl		= CNTL_LCDTFT | CNTL_WATERMARK,
	.bpp		= 16,
};

#endif

#undef NS_TO_CLOCK
#undef CLOCK_TO_DIV

#endif  /* __LCD_PANEL_H__ */
