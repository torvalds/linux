/*
 * BRIEF MODULE DESCRIPTION
 *	Hardware definitions for the Au1100 LCD controller
 *
 * Copyright 2002 MontaVista Software
 * Copyright 2002 Alchemy Semiconductor
 * Author:	Alchemy Semiconductor, MontaVista Software
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _AU1100LCD_H
#define _AU1100LCD_H

/********************************************************************/
#define uint32 unsigned long
typedef volatile struct
{
	uint32	lcd_control;
	uint32	lcd_intstatus;
	uint32	lcd_intenable;
	uint32	lcd_horztiming;
	uint32	lcd_verttiming;
	uint32	lcd_clkcontrol;
	uint32	lcd_dmaaddr0;
	uint32	lcd_dmaaddr1;
	uint32	lcd_words;
	uint32	lcd_pwmdiv;
	uint32	lcd_pwmhi;
	uint32	reserved[(0x0400-0x002C)/4];
	uint32	lcd_pallettebase[256];

} AU1100_LCD;

/********************************************************************/

#define AU1100_LCD_ADDR		0xB5000000

/*
 * Register bit definitions
 */

/* lcd_control */
#define LCD_CONTROL_SBPPF		(7<<18)
#define LCD_CONTROL_SBPPF_655	(0<<18)
#define LCD_CONTROL_SBPPF_565	(1<<18)
#define LCD_CONTROL_SBPPF_556	(2<<18)
#define LCD_CONTROL_SBPPF_1555	(3<<18)
#define LCD_CONTROL_SBPPF_5551	(4<<18)
#define LCD_CONTROL_WP			(1<<17)
#define LCD_CONTROL_WD			(1<<16)
#define LCD_CONTROL_C			(1<<15)
#define LCD_CONTROL_SM			(3<<13)
#define LCD_CONTROL_SM_0		(0<<13)
#define LCD_CONTROL_SM_90		(1<<13)
#define LCD_CONTROL_SM_180		(2<<13)
#define LCD_CONTROL_SM_270		(3<<13)
#define LCD_CONTROL_DB			(1<<12)
#define LCD_CONTROL_CCO			(1<<11)
#define LCD_CONTROL_DP			(1<<10)
#define LCD_CONTROL_PO			(3<<8)
#define LCD_CONTROL_PO_00		(0<<8)
#define LCD_CONTROL_PO_01		(1<<8)
#define LCD_CONTROL_PO_10		(2<<8)
#define LCD_CONTROL_PO_11		(3<<8)
#define LCD_CONTROL_MPI			(1<<7)
#define LCD_CONTROL_PT			(1<<6)
#define LCD_CONTROL_PC			(1<<5)
#define LCD_CONTROL_BPP			(7<<1)
#define LCD_CONTROL_BPP_1		(0<<1)
#define LCD_CONTROL_BPP_2		(1<<1)
#define LCD_CONTROL_BPP_4		(2<<1)
#define LCD_CONTROL_BPP_8		(3<<1)
#define LCD_CONTROL_BPP_12		(4<<1)
#define LCD_CONTROL_BPP_16		(5<<1)
#define LCD_CONTROL_GO			(1<<0)

/* lcd_intstatus, lcd_intenable */
#define LCD_INT_SD				(1<<7)
#define LCD_INT_OF				(1<<6)
#define LCD_INT_UF				(1<<5)
#define LCD_INT_SA				(1<<3)
#define LCD_INT_SS				(1<<2)
#define LCD_INT_S1				(1<<1)
#define LCD_INT_S0				(1<<0)

/* lcd_horztiming */
#define LCD_HORZTIMING_HN2		(255<<24)
#define LCD_HORZTIMING_HN2_N(N)	(((N)-1)<<24)
#define LCD_HORZTIMING_HN1		(255<<16)
#define LCD_HORZTIMING_HN1_N(N)	(((N)-1)<<16)
#define LCD_HORZTIMING_HPW		(63<<10)
#define LCD_HORZTIMING_HPW_N(N)	(((N)-1)<<10)
#define LCD_HORZTIMING_PPL		(1023<<0)
#define LCD_HORZTIMING_PPL_N(N)	(((N)-1)<<0)

/* lcd_verttiming */
#define LCD_VERTTIMING_VN2		(255<<24)
#define LCD_VERTTIMING_VN2_N(N)	(((N)-1)<<24)
#define LCD_VERTTIMING_VN1		(255<<16)
#define LCD_VERTTIMING_VN1_N(N)	(((N)-1)<<16)
#define LCD_VERTTIMING_VPW		(63<<10)
#define LCD_VERTTIMING_VPW_N(N)	(((N)-1)<<10)
#define LCD_VERTTIMING_LPP		(1023<<0)
#define LCD_VERTTIMING_LPP_N(N)	(((N)-1)<<0)

/* lcd_clkcontrol */
#define LCD_CLKCONTROL_IB		(1<<18)
#define LCD_CLKCONTROL_IC		(1<<17)
#define LCD_CLKCONTROL_IH		(1<<16)
#define LCD_CLKCONTROL_IV		(1<<15)
#define LCD_CLKCONTROL_BF		(31<<10)
#define LCD_CLKCONTROL_BF_N(N)	(((N)-1)<<10)
#define LCD_CLKCONTROL_PCD		(1023<<0)
#define LCD_CLKCONTROL_PCD_N(N)	((N)<<0)

/* lcd_pwmdiv */
#define LCD_PWMDIV_EN			(1<<12)
#define LCD_PWMDIV_PWMDIV		(2047<<0)
#define LCD_PWMDIV_PWMDIV_N(N)	(((N)-1)<<0)

/* lcd_pwmhi */
#define LCD_PWMHI_PWMHI1		(2047<<12)
#define LCD_PWMHI_PWMHI1_N(N)	((N)<<12)
#define LCD_PWMHI_PWMHI0		(2047<<0)
#define LCD_PWMHI_PWMHI0_N(N)	((N)<<0)

/* lcd_pallettebase - MONOCHROME */
#define LCD_PALLETTE_MONO_MI		(15<<0)
#define LCD_PALLETTE_MONO_MI_N(N)	((N)<<0)

/* lcd_pallettebase - COLOR */
#define LCD_PALLETTE_COLOR_BI		(15<<8)
#define LCD_PALLETTE_COLOR_BI_N(N)	((N)<<8)
#define LCD_PALLETTE_COLOR_GI		(15<<4)
#define LCD_PALLETTE_COLOR_GI_N(N)	((N)<<4)
#define LCD_PALLETTE_COLOR_RI		(15<<0)
#define LCD_PALLETTE_COLOR_RI_N(N)	((N)<<0)

/* lcd_palletebase - COLOR TFT PALLETIZED */
#define LCD_PALLETTE_TFT_DC			(65535<<0)
#define LCD_PALLETTE_TFT_DC_N(N)	((N)<<0)

/********************************************************************/

struct known_lcd_panels
{
	uint32 xres;
	uint32 yres;
	uint32 bpp;
	unsigned char  panel_name[256];
	uint32 mode_control;
	uint32 mode_horztiming;
	uint32 mode_verttiming;
	uint32 mode_clkcontrol;
	uint32 mode_pwmdiv;
	uint32 mode_pwmhi;
	uint32 mode_toyclksrc;
	uint32 mode_backlight;

};

#if defined(__BIG_ENDIAN)
#define LCD_DEFAULT_PIX_FORMAT LCD_CONTROL_PO_11
#else
#define LCD_DEFAULT_PIX_FORMAT LCD_CONTROL_PO_00
#endif

/*
 * The fb driver assumes that AUX PLL is at 48MHz.  That can
 * cover up to 800x600 resolution; if you need higher resolution,
 * you should modify the driver as needed, not just this structure.
 */
struct known_lcd_panels panels[] =
{
	{ /* 0: Pb1100 LCDA: Sharp 320x240 TFT panel */
		320, /* xres */
		240, /* yres */
		16,  /* bpp  */

		"Sharp_320x240_16",
		/* mode_control */
		( LCD_CONTROL_SBPPF_565
		/*LCD_CONTROL_WP*/
		/*LCD_CONTROL_WD*/
		| LCD_CONTROL_C
		| LCD_CONTROL_SM_0
		/*LCD_CONTROL_DB*/
		/*LCD_CONTROL_CCO*/
		/*LCD_CONTROL_DP*/
		| LCD_DEFAULT_PIX_FORMAT
		/*LCD_CONTROL_MPI*/
		| LCD_CONTROL_PT
		| LCD_CONTROL_PC
		| LCD_CONTROL_BPP_16 ),

		/* mode_horztiming */
		( LCD_HORZTIMING_HN2_N(8)
		| LCD_HORZTIMING_HN1_N(60)
		| LCD_HORZTIMING_HPW_N(12)
		| LCD_HORZTIMING_PPL_N(320) ),

		/* mode_verttiming */
		( LCD_VERTTIMING_VN2_N(5)
		| LCD_VERTTIMING_VN1_N(17)
		| LCD_VERTTIMING_VPW_N(1)
		| LCD_VERTTIMING_LPP_N(240) ),

		/* mode_clkcontrol */
		( 0
		/*LCD_CLKCONTROL_IB*/
		/*LCD_CLKCONTROL_IC*/
		/*LCD_CLKCONTROL_IH*/
		/*LCD_CLKCONTROL_IV*/
		| LCD_CLKCONTROL_PCD_N(1) ),

		/* mode_pwmdiv */
		0,

		/* mode_pwmhi */
		0,

		/* mode_toyclksrc */
		((1<<7) | (1<<6) | (1<<5)),

		/* mode_backlight */
		6
	},

	{ /* 1: Pb1100 LCDC 640x480 TFT panel */
		640, /* xres */
		480, /* yres */
		16,  /* bpp  */

		"Generic_640x480_16",

		/* mode_control */
		0x004806a | LCD_DEFAULT_PIX_FORMAT,

		/* mode_horztiming */
		0x3434d67f,

		/* mode_verttiming */
		0x0e0e39df,

		/* mode_clkcontrol */
		( 0
		/*LCD_CLKCONTROL_IB*/
		/*LCD_CLKCONTROL_IC*/
		/*LCD_CLKCONTROL_IH*/
		/*LCD_CLKCONTROL_IV*/
		| LCD_CLKCONTROL_PCD_N(1) ),

		/* mode_pwmdiv */
		0,

		/* mode_pwmhi */
		0,

		/* mode_toyclksrc */
		((1<<7) | (1<<6) | (0<<5)),

		/* mode_backlight */
		7
	},

	{ /* 2: Pb1100 LCDB 640x480 PrimeView TFT panel */
		640, /* xres */
		480, /* yres */
		16,  /* bpp  */

		"PrimeView_640x480_16",

		/* mode_control */
		0x0004886a | LCD_DEFAULT_PIX_FORMAT,

		/* mode_horztiming */
		0x0e4bfe7f,

		/* mode_verttiming */
		0x210805df,

		/* mode_clkcontrol */
		0x00038001,

		/* mode_pwmdiv */
		0,

		/* mode_pwmhi */
		0,

		/* mode_toyclksrc */
		((1<<7) | (1<<6) | (0<<5)),

		/* mode_backlight */
		7
	},

	{ /* 3: Pb1100 800x600x16bpp NEON CRT */
		800, /* xres */
		600, /* yres */
		16,  /* bpp */

		"NEON_800x600_16",

		/* mode_control */
		0x0004886A | LCD_DEFAULT_PIX_FORMAT,

		/* mode_horztiming */
		0x005AFF1F,

		/* mode_verttiming */
		0x16000E57,

		/* mode_clkcontrol */
		0x00020000,

		/* mode_pwmdiv */
		0,

		/* mode_pwmhi */
		0,

		/* mode_toyclksrc */
		((1<<7) | (1<<6) | (0<<5)),

		/* mode_backlight */
		7
	},

	{ /* 4: Pb1100 640x480x16bpp NEON CRT */
		640, /* xres */
		480, /* yres */
		16,  /* bpp */

		"NEON_640x480_16",

		/* mode_control */
		0x0004886A | LCD_DEFAULT_PIX_FORMAT,

		/* mode_horztiming */
		0x0052E27F,

		/* mode_verttiming */
		0x18000DDF,

		/* mode_clkcontrol */
		0x00020000,

		/* mode_pwmdiv */
		0,

		/* mode_pwmhi */
		0,

		/* mode_toyclksrc */
		((1<<7) | (1<<6) | (0<<5)),

		/* mode_backlight */
		7
	},
};
#endif /* _AU1100LCD_H */
