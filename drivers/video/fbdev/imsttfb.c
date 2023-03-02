/*
 *  drivers/video/imsttfb.c -- frame buffer device for IMS TwinTurbo
 *
 *  This file is derived from the powermac console "imstt" driver:
 *  Copyright (C) 1997 Sigurdur Asgeirsson
 *  With additional hacking by Jeffrey Kuskin (jsk@mojave.stanford.edu)
 *  Modified by Danilo Beuche 1998
 *  Some register values added by Damien Doligez, INRIA Rocquencourt
 *  Various cleanups by Paul Mundt (lethal@chaoticdreams.org)
 *
 *  This file was written by Ryan Nielsen (ran@krazynet.com)
 *  Most of the frame buffer device stuff was copied from atyfb.c
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/aperture.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/uaccess.h>

#if defined(CONFIG_PPC_PMAC)
#include <linux/nvram.h>
#include "macmodes.h"
#endif

#ifndef __powerpc__
#define eieio()		/* Enforce In-order Execution of I/O */
#endif

/* TwinTurbo (Cosmo) registers */
enum {
	S1SA	=  0, /* 0x00 */
	S2SA	=  1, /* 0x04 */
	SP	=  2, /* 0x08 */
	DSA	=  3, /* 0x0C */
	CNT	=  4, /* 0x10 */
	DP_OCTL	=  5, /* 0x14 */
	CLR	=  6, /* 0x18 */
	BI	=  8, /* 0x20 */
	MBC	=  9, /* 0x24 */
	BLTCTL	= 10, /* 0x28 */

	/* Scan Timing Generator Registers */
	HES	= 12, /* 0x30 */
	HEB	= 13, /* 0x34 */
	HSB	= 14, /* 0x38 */
	HT	= 15, /* 0x3C */
	VES	= 16, /* 0x40 */
	VEB	= 17, /* 0x44 */
	VSB	= 18, /* 0x48 */
	VT	= 19, /* 0x4C */
	HCIV	= 20, /* 0x50 */
	VCIV	= 21, /* 0x54 */
	TCDR	= 22, /* 0x58 */
	VIL	= 23, /* 0x5C */
	STGCTL	= 24, /* 0x60 */

	/* Screen Refresh Generator Registers */
	SSR	= 25, /* 0x64 */
	HRIR	= 26, /* 0x68 */
	SPR	= 27, /* 0x6C */
	CMR	= 28, /* 0x70 */
	SRGCTL	= 29, /* 0x74 */

	/* RAM Refresh Generator Registers */
	RRCIV	= 30, /* 0x78 */
	RRSC	= 31, /* 0x7C */
	RRCR	= 34, /* 0x88 */

	/* System Registers */
	GIOE	= 32, /* 0x80 */
	GIO	= 33, /* 0x84 */
	SCR	= 35, /* 0x8C */
	SSTATUS	= 36, /* 0x90 */
	PRC	= 37, /* 0x94 */

#if 0
	/* PCI Registers */
	DVID	= 0x00000000L,
	SC	= 0x00000004L,
	CCR	= 0x00000008L,
	OG	= 0x0000000CL,
	BARM	= 0x00000010L,
	BARER	= 0x00000030L,
#endif
};

/* IBM 624 RAMDAC Direct Registers */
enum {
	PADDRW	= 0x00,
	PDATA	= 0x04,
	PPMASK	= 0x08,
	PADDRR	= 0x0c,
	PIDXLO	= 0x10,
	PIDXHI	= 0x14,
	PIDXDATA= 0x18,
	PIDXCTL	= 0x1c
};

/* IBM 624 RAMDAC Indirect Registers */
enum {
	CLKCTL		= 0x02,	/* (0x01) Miscellaneous Clock Control */
	SYNCCTL		= 0x03,	/* (0x00) Sync Control */
	HSYNCPOS	= 0x04,	/* (0x00) Horizontal Sync Position */
	PWRMNGMT	= 0x05,	/* (0x00) Power Management */
	DACOP		= 0x06,	/* (0x02) DAC Operation */
	PALETCTL	= 0x07,	/* (0x00) Palette Control */
	SYSCLKCTL	= 0x08,	/* (0x01) System Clock Control */
	PIXFMT		= 0x0a,	/* () Pixel Format  [bpp >> 3 + 2] */
	BPP8		= 0x0b,	/* () 8 Bits/Pixel Control */
	BPP16		= 0x0c, /* () 16 Bits/Pixel Control  [bit 1=1 for 565] */
	BPP24		= 0x0d,	/* () 24 Bits/Pixel Control */
	BPP32		= 0x0e,	/* () 32 Bits/Pixel Control */
	PIXCTL1		= 0x10, /* (0x05) Pixel PLL Control 1 */
	PIXCTL2		= 0x11,	/* (0x00) Pixel PLL Control 2 */
	SYSCLKN		= 0x15,	/* () System Clock N (System PLL Reference Divider) */
	SYSCLKM		= 0x16,	/* () System Clock M (System PLL VCO Divider) */
	SYSCLKP		= 0x17,	/* () System Clock P */
	SYSCLKC		= 0x18,	/* () System Clock C */
	/*
	 * Dot clock rate is 20MHz * (m + 1) / ((n + 1) * (p ? 2 * p : 1)
	 * c is charge pump bias which depends on the VCO frequency
	 */
	PIXM0		= 0x20,	/* () Pixel M 0 */
	PIXN0		= 0x21,	/* () Pixel N 0 */
	PIXP0		= 0x22,	/* () Pixel P 0 */
	PIXC0		= 0x23,	/* () Pixel C 0 */
	CURSCTL		= 0x30,	/* (0x00) Cursor Control */
	CURSXLO		= 0x31,	/* () Cursor X position, low 8 bits */
	CURSXHI		= 0x32,	/* () Cursor X position, high 8 bits */
	CURSYLO		= 0x33,	/* () Cursor Y position, low 8 bits */
	CURSYHI		= 0x34,	/* () Cursor Y position, high 8 bits */
	CURSHOTX	= 0x35,	/* () Cursor Hot Spot X */
	CURSHOTY	= 0x36,	/* () Cursor Hot Spot Y */
	CURSACCTL	= 0x37,	/* () Advanced Cursor Control Enable */
	CURSACATTR	= 0x38,	/* () Advanced Cursor Attribute */
	CURS1R		= 0x40,	/* () Cursor 1 Red */
	CURS1G		= 0x41,	/* () Cursor 1 Green */
	CURS1B		= 0x42,	/* () Cursor 1 Blue */
	CURS2R		= 0x43,	/* () Cursor 2 Red */
	CURS2G		= 0x44,	/* () Cursor 2 Green */
	CURS2B		= 0x45,	/* () Cursor 2 Blue */
	CURS3R		= 0x46,	/* () Cursor 3 Red */
	CURS3G		= 0x47,	/* () Cursor 3 Green */
	CURS3B		= 0x48,	/* () Cursor 3 Blue */
	BORDR		= 0x60,	/* () Border Color Red */
	BORDG		= 0x61,	/* () Border Color Green */
	BORDB		= 0x62,	/* () Border Color Blue */
	MISCTL1		= 0x70,	/* (0x00) Miscellaneous Control 1 */
	MISCTL2		= 0x71,	/* (0x00) Miscellaneous Control 2 */
	MISCTL3		= 0x72,	/* (0x00) Miscellaneous Control 3 */
	KEYCTL		= 0x78	/* (0x00) Key Control/DB Operation */
};

/* TI TVP 3030 RAMDAC Direct Registers */
enum {
	TVPADDRW = 0x00,	/* 0  Palette/Cursor RAM Write Address/Index */
	TVPPDATA = 0x04,	/* 1  Palette Data RAM Data */
	TVPPMASK = 0x08,	/* 2  Pixel Read-Mask */
	TVPPADRR = 0x0c,	/* 3  Palette/Cursor RAM Read Address */
	TVPCADRW = 0x10,	/* 4  Cursor/Overscan Color Write Address */
	TVPCDATA = 0x14,	/* 5  Cursor/Overscan Color Data */
				/* 6  reserved */
	TVPCADRR = 0x1c,	/* 7  Cursor/Overscan Color Read Address */
				/* 8  reserved */
	TVPDCCTL = 0x24,	/* 9  Direct Cursor Control */
	TVPIDATA = 0x28,	/* 10 Index Data */
	TVPCRDAT = 0x2c,	/* 11 Cursor RAM Data */
	TVPCXPOL = 0x30,	/* 12 Cursor-Position X LSB */
	TVPCXPOH = 0x34,	/* 13 Cursor-Position X MSB */
	TVPCYPOL = 0x38,	/* 14 Cursor-Position Y LSB */
	TVPCYPOH = 0x3c,	/* 15 Cursor-Position Y MSB */
};

/* TI TVP 3030 RAMDAC Indirect Registers */
enum {
	TVPIRREV = 0x01,	/* Silicon Revision [RO] */
	TVPIRICC = 0x06,	/* Indirect Cursor Control 	(0x00) */
	TVPIRBRC = 0x07,	/* Byte Router Control 	(0xe4) */
	TVPIRLAC = 0x0f,	/* Latch Control 		(0x06) */
	TVPIRTCC = 0x18,	/* True Color Control  	(0x80) */
	TVPIRMXC = 0x19,	/* Multiplex Control		(0x98) */
	TVPIRCLS = 0x1a,	/* Clock Selection		(0x07) */
	TVPIRPPG = 0x1c,	/* Palette Page		(0x00) */
	TVPIRGEC = 0x1d,	/* General Control 		(0x00) */
	TVPIRMIC = 0x1e,	/* Miscellaneous Control	(0x00) */
	TVPIRPLA = 0x2c,	/* PLL Address */
	TVPIRPPD = 0x2d,	/* Pixel Clock PLL Data */
	TVPIRMPD = 0x2e,	/* Memory Clock PLL Data */
	TVPIRLPD = 0x2f,	/* Loop Clock PLL Data */
	TVPIRCKL = 0x30,	/* Color-Key Overlay Low */
	TVPIRCKH = 0x31,	/* Color-Key Overlay High */
	TVPIRCRL = 0x32,	/* Color-Key Red Low */
	TVPIRCRH = 0x33,	/* Color-Key Red High */
	TVPIRCGL = 0x34,	/* Color-Key Green Low */
	TVPIRCGH = 0x35,	/* Color-Key Green High */
	TVPIRCBL = 0x36,	/* Color-Key Blue Low */
	TVPIRCBH = 0x37,	/* Color-Key Blue High */
	TVPIRCKC = 0x38,	/* Color-Key Control 		(0x00) */
	TVPIRMLC = 0x39,	/* MCLK/Loop Clock Control	(0x18) */
	TVPIRSEN = 0x3a,	/* Sense Test			(0x00) */
	TVPIRTMD = 0x3b,	/* Test Mode Data */
	TVPIRRML = 0x3c,	/* CRC Remainder LSB [RO] */
	TVPIRRMM = 0x3d,	/* CRC Remainder MSB [RO] */
	TVPIRRMS = 0x3e,	/* CRC  Bit Select [WO] */
	TVPIRDID = 0x3f,	/* Device ID [RO] 		(0x30) */
	TVPIRRES = 0xff		/* Software Reset [WO] */
};

struct initvalues {
	__u8 addr, value;
};

static struct initvalues ibm_initregs[] = {
	{ CLKCTL,	0x21 },
	{ SYNCCTL,	0x00 },
	{ HSYNCPOS,	0x00 },
	{ PWRMNGMT,	0x00 },
	{ DACOP,	0x02 },
	{ PALETCTL,	0x00 },
	{ SYSCLKCTL,	0x01 },

	/*
	 * Note that colors in X are correct only if all video data is
	 * passed through the palette in the DAC.  That is, "indirect
	 * color" must be configured.  This is the case for the IBM DAC
	 * used in the 2MB and 4MB cards, at least.
	 */
	{ BPP8,		0x00 },
	{ BPP16,	0x01 },
	{ BPP24,	0x00 },
	{ BPP32,	0x00 },

	{ PIXCTL1,	0x05 },
	{ PIXCTL2,	0x00 },
	{ SYSCLKN,	0x08 },
	{ SYSCLKM,	0x4f },
	{ SYSCLKP,	0x00 },
	{ SYSCLKC,	0x00 },
	{ CURSCTL,	0x00 },
	{ CURSACCTL,	0x01 },
	{ CURSACATTR,	0xa8 },
	{ CURS1R,	0xff },
	{ CURS1G,	0xff },
	{ CURS1B,	0xff },
	{ CURS2R,	0xff },
	{ CURS2G,	0xff },
	{ CURS2B,	0xff },
	{ CURS3R,	0xff },
	{ CURS3G,	0xff },
	{ CURS3B,	0xff },
	{ BORDR,	0xff },
	{ BORDG,	0xff },
	{ BORDB,	0xff },
	{ MISCTL1,	0x01 },
	{ MISCTL2,	0x45 },
	{ MISCTL3,	0x00 },
	{ KEYCTL,	0x00 }
};

static struct initvalues tvp_initregs[] = {
	{ TVPIRICC,	0x00 },
	{ TVPIRBRC,	0xe4 },
	{ TVPIRLAC,	0x06 },
	{ TVPIRTCC,	0x80 },
	{ TVPIRMXC,	0x4d },
	{ TVPIRCLS,	0x05 },
	{ TVPIRPPG,	0x00 },
	{ TVPIRGEC,	0x00 },
	{ TVPIRMIC,	0x08 },
	{ TVPIRCKL,	0xff },
	{ TVPIRCKH,	0xff },
	{ TVPIRCRL,	0xff },
	{ TVPIRCRH,	0xff },
	{ TVPIRCGL,	0xff },
	{ TVPIRCGH,	0xff },
	{ TVPIRCBL,	0xff },
	{ TVPIRCBH,	0xff },
	{ TVPIRCKC,	0x00 },
	{ TVPIRPLA,	0x00 },
	{ TVPIRPPD,	0xc0 },
	{ TVPIRPPD,	0xd5 },
	{ TVPIRPPD,	0xea },
	{ TVPIRPLA,	0x00 },
	{ TVPIRMPD,	0xb9 },
	{ TVPIRMPD,	0x3a },
	{ TVPIRMPD,	0xb1 },
	{ TVPIRPLA,	0x00 },
	{ TVPIRLPD,	0xc1 },
	{ TVPIRLPD,	0x3d },
	{ TVPIRLPD,	0xf3 },
};

struct imstt_regvals {
	__u32 pitch;
	__u16 hes, heb, hsb, ht, ves, veb, vsb, vt, vil;
	__u8 pclk_m, pclk_n, pclk_p;
	/* Values of the tvp which change depending on colormode x resolution */
	__u8 mlc[3];	/* Memory Loop Config 0x39 */
	__u8 lckl_p[3];	/* P value of LCKL PLL */
};

struct imstt_par {
	struct imstt_regvals init;
	__u32 __iomem *dc_regs;
	unsigned long cmap_regs_phys;
	__u8 *cmap_regs;
	__u32 ramdac;
	__u32 palette[16];
};

enum {
	IBM = 0,
	TVP = 1
};

#define INIT_BPP		8
#define INIT_XRES		640
#define INIT_YRES		480

static int inverse = 0;
static char fontname[40] __initdata = { 0 };
#if defined(CONFIG_PPC_PMAC)
static signed char init_vmode = -1, init_cmode = -1;
#endif

static struct imstt_regvals tvp_reg_init_2 = {
	512,
	0x0002, 0x0006, 0x0026, 0x0028, 0x0003, 0x0016, 0x0196, 0x0197, 0x0196,
	0xec, 0x2a, 0xf3,
	{ 0x3c, 0x3b, 0x39 }, { 0xf3, 0xf3, 0xf3 }
};

static struct imstt_regvals tvp_reg_init_6 = {
	640,
	0x0004, 0x0009, 0x0031, 0x0036, 0x0003, 0x002a, 0x020a, 0x020d, 0x020a,
	0xef, 0x2e, 0xb2,
	{ 0x39, 0x39, 0x38 }, { 0xf3, 0xf3, 0xf3 }
};

static struct imstt_regvals tvp_reg_init_12 = {
	800,
	0x0005, 0x000e, 0x0040, 0x0042, 0x0003, 0x018, 0x270, 0x271, 0x270,
	0xf6, 0x2e, 0xf2,
	{ 0x3a, 0x39, 0x38 }, { 0xf3, 0xf3, 0xf3 }
};

static struct imstt_regvals tvp_reg_init_13 = {
	832,
	0x0004, 0x0011, 0x0045, 0x0048, 0x0003, 0x002a, 0x029a, 0x029b, 0x0000,
	0xfe, 0x3e, 0xf1,
	{ 0x39, 0x38, 0x38 }, { 0xf3, 0xf3, 0xf2 }
};

static struct imstt_regvals tvp_reg_init_17 = {
	1024,
	0x0006, 0x0210, 0x0250, 0x0053, 0x1003, 0x0021, 0x0321, 0x0324, 0x0000,
	0xfc, 0x3a, 0xf1,
	{ 0x39, 0x38, 0x38 }, { 0xf3, 0xf3, 0xf2 }
};

static struct imstt_regvals tvp_reg_init_18 = {
	1152,
  	0x0009, 0x0011, 0x059, 0x5b, 0x0003, 0x0031, 0x0397, 0x039a, 0x0000,
	0xfd, 0x3a, 0xf1,
	{ 0x39, 0x38, 0x38 }, { 0xf3, 0xf3, 0xf2 }
};

static struct imstt_regvals tvp_reg_init_19 = {
	1280,
	0x0009, 0x0016, 0x0066, 0x0069, 0x0003, 0x0027, 0x03e7, 0x03e8, 0x03e7,
	0xf7, 0x36, 0xf0,
	{ 0x38, 0x38, 0x38 }, { 0xf3, 0xf2, 0xf1 }
};

static struct imstt_regvals tvp_reg_init_20 = {
	1280,
	0x0009, 0x0018, 0x0068, 0x006a, 0x0003, 0x0029, 0x0429, 0x042a, 0x0000,
	0xf0, 0x2d, 0xf0,
	{ 0x38, 0x38, 0x38 }, { 0xf3, 0xf2, 0xf1 }
};

/*
 * PCI driver prototypes
 */
static int imsttfb_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void imsttfb_remove(struct pci_dev *pdev);

/*
 * Register access
 */
static inline u32 read_reg_le32(volatile u32 __iomem *base, int regindex)
{
#ifdef __powerpc__
	return in_le32(base + regindex);
#else
	return readl(base + regindex);
#endif
}

static inline void write_reg_le32(volatile u32 __iomem *base, int regindex, u32 val)
{
#ifdef __powerpc__
	out_le32(base + regindex, val);
#else
	writel(val, base + regindex);
#endif
}

static __u32
getclkMHz(struct imstt_par *par)
{
	__u32 clk_m, clk_n, clk_p;

	clk_m = par->init.pclk_m;
	clk_n = par->init.pclk_n;
	clk_p = par->init.pclk_p;

	return 20 * (clk_m + 1) / ((clk_n + 1) * (clk_p ? 2 * clk_p : 1));
}

static void
setclkMHz(struct imstt_par *par, __u32 MHz)
{
	__u32 clk_m, clk_n, x, stage, spilled;

	clk_m = clk_n = 0;
	stage = spilled = 0;
	for (;;) {
		switch (stage) {
			case 0:
				clk_m++;
				break;
			case 1:
				clk_n++;
				break;
		}
		x = 20 * (clk_m + 1) / (clk_n + 1);
		if (x == MHz)
			break;
		if (x > MHz) {
			spilled = 1;
			stage = 1;
		} else if (spilled && x < MHz) {
			stage = 0;
		}
	}

	par->init.pclk_m = clk_m;
	par->init.pclk_n = clk_n;
	par->init.pclk_p = 0;
}

static struct imstt_regvals *
compute_imstt_regvals_ibm(struct imstt_par *par, int xres, int yres)
{
	struct imstt_regvals *init = &par->init;
	__u32 MHz, hes, heb, veb, htp, vtp;

	switch (xres) {
		case 640:
			hes = 0x0008; heb = 0x0012; veb = 0x002a; htp = 10; vtp = 2;
			MHz = 30 /* .25 */ ;
			break;
		case 832:
			hes = 0x0005; heb = 0x0020; veb = 0x0028; htp = 8; vtp = 3;
			MHz = 57 /* .27_ */ ;
			break;
		case 1024:
			hes = 0x000a; heb = 0x001c; veb = 0x0020; htp = 8; vtp = 3;
			MHz = 80;
			break;
		case 1152:
			hes = 0x0012; heb = 0x0022; veb = 0x0031; htp = 4; vtp = 3;
			MHz = 101 /* .6_ */ ;
			break;
		case 1280:
			hes = 0x0012; heb = 0x002f; veb = 0x0029; htp = 4; vtp = 1;
			MHz = yres == 960 ? 126 : 135;
			break;
		case 1600:
			hes = 0x0018; heb = 0x0040; veb = 0x002a; htp = 4; vtp = 3;
			MHz = 200;
			break;
		default:
			return NULL;
	}

	setclkMHz(par, MHz);

	init->hes = hes;
	init->heb = heb;
	init->hsb = init->heb + (xres >> 3);
	init->ht = init->hsb + htp;
	init->ves = 0x0003;
	init->veb = veb;
	init->vsb = init->veb + yres;
	init->vt = init->vsb + vtp;
	init->vil = init->vsb;

	init->pitch = xres;
	return init;
}

static struct imstt_regvals *
compute_imstt_regvals_tvp(struct imstt_par *par, int xres, int yres)
{
	struct imstt_regvals *init;

	switch (xres) {
		case 512:
			init = &tvp_reg_init_2;
			break;
		case 640:
			init = &tvp_reg_init_6;
			break;
		case 800:
			init = &tvp_reg_init_12;
			break;
		case 832:
			init = &tvp_reg_init_13;
			break;
		case 1024:
			init = &tvp_reg_init_17;
			break;
		case 1152:
			init = &tvp_reg_init_18;
			break;
		case 1280:
			init = yres == 960 ? &tvp_reg_init_19 : &tvp_reg_init_20;
			break;
		default:
			return NULL;
	}
	par->init = *init;
	return init;
}

static struct imstt_regvals *
compute_imstt_regvals (struct imstt_par *par, u_int xres, u_int yres)
{
	if (par->ramdac == IBM)
		return compute_imstt_regvals_ibm(par, xres, yres);
	else
		return compute_imstt_regvals_tvp(par, xres, yres);
}

static void
set_imstt_regvals_ibm (struct imstt_par *par, u_int bpp)
{
	struct imstt_regvals *init = &par->init;
	__u8 pformat = (bpp >> 3) + 2;

	par->cmap_regs[PIDXHI] = 0;		eieio();
	par->cmap_regs[PIDXLO] = PIXM0;		eieio();
	par->cmap_regs[PIDXDATA] = init->pclk_m;eieio();
	par->cmap_regs[PIDXLO] = PIXN0;		eieio();
	par->cmap_regs[PIDXDATA] = init->pclk_n;eieio();
	par->cmap_regs[PIDXLO] = PIXP0;		eieio();
	par->cmap_regs[PIDXDATA] = init->pclk_p;eieio();
	par->cmap_regs[PIDXLO] = PIXC0;		eieio();
	par->cmap_regs[PIDXDATA] = 0x02;	eieio();

	par->cmap_regs[PIDXLO] = PIXFMT;	eieio();
	par->cmap_regs[PIDXDATA] = pformat;	eieio();
}

static void
set_imstt_regvals_tvp (struct imstt_par *par, u_int bpp)
{
	struct imstt_regvals *init = &par->init;
	__u8 tcc, mxc, lckl_n, mic;
	__u8 mlc, lckl_p;

	switch (bpp) {
		default:
		case 8:
			tcc = 0x80;
			mxc = 0x4d;
			lckl_n = 0xc1;
			mlc = init->mlc[0];
			lckl_p = init->lckl_p[0];
			break;
		case 16:
			tcc = 0x44;
			mxc = 0x55;
			lckl_n = 0xe1;
			mlc = init->mlc[1];
			lckl_p = init->lckl_p[1];
			break;
		case 24:
			tcc = 0x5e;
			mxc = 0x5d;
			lckl_n = 0xf1;
			mlc = init->mlc[2];
			lckl_p = init->lckl_p[2];
			break;
		case 32:
			tcc = 0x46;
			mxc = 0x5d;
			lckl_n = 0xf1;
			mlc = init->mlc[2];
			lckl_p = init->lckl_p[2];
			break;
	}
	mic = 0x08;

	par->cmap_regs[TVPADDRW] = TVPIRPLA;		eieio();
	par->cmap_regs[TVPIDATA] = 0x00;		eieio();
	par->cmap_regs[TVPADDRW] = TVPIRPPD;		eieio();
	par->cmap_regs[TVPIDATA] = init->pclk_m;	eieio();
	par->cmap_regs[TVPADDRW] = TVPIRPPD;		eieio();
	par->cmap_regs[TVPIDATA] = init->pclk_n;	eieio();
	par->cmap_regs[TVPADDRW] = TVPIRPPD;		eieio();
	par->cmap_regs[TVPIDATA] = init->pclk_p;	eieio();

	par->cmap_regs[TVPADDRW] = TVPIRTCC;		eieio();
	par->cmap_regs[TVPIDATA] = tcc;			eieio();
	par->cmap_regs[TVPADDRW] = TVPIRMXC;		eieio();
	par->cmap_regs[TVPIDATA] = mxc;			eieio();
	par->cmap_regs[TVPADDRW] = TVPIRMIC;		eieio();
	par->cmap_regs[TVPIDATA] = mic;			eieio();

	par->cmap_regs[TVPADDRW] = TVPIRPLA;		eieio();
	par->cmap_regs[TVPIDATA] = 0x00;		eieio();
	par->cmap_regs[TVPADDRW] = TVPIRLPD;		eieio();
	par->cmap_regs[TVPIDATA] = lckl_n;		eieio();

	par->cmap_regs[TVPADDRW] = TVPIRPLA;		eieio();
	par->cmap_regs[TVPIDATA] = 0x15;		eieio();
	par->cmap_regs[TVPADDRW] = TVPIRMLC;		eieio();
	par->cmap_regs[TVPIDATA] = mlc;			eieio();

	par->cmap_regs[TVPADDRW] = TVPIRPLA;		eieio();
	par->cmap_regs[TVPIDATA] = 0x2a;		eieio();
	par->cmap_regs[TVPADDRW] = TVPIRLPD;		eieio();
	par->cmap_regs[TVPIDATA] = lckl_p;		eieio();
}

static void
set_imstt_regvals (struct fb_info *info, u_int bpp)
{
	struct imstt_par *par = info->par;
	struct imstt_regvals *init = &par->init;
	__u32 ctl, pitch, byteswap, scr;

	if (par->ramdac == IBM)
		set_imstt_regvals_ibm(par, bpp);
	else
		set_imstt_regvals_tvp(par, bpp);

  /*
   * From what I (jsk) can gather poking around with MacsBug,
   * bits 8 and 9 in the SCR register control endianness
   * correction (byte swapping).  These bits must be set according
   * to the color depth as follows:
   *     Color depth    Bit 9   Bit 8
   *     ==========     =====   =====
   *        8bpp          0       0
   *       16bpp          0       1
   *       32bpp          1       1
   */
	switch (bpp) {
		default:
		case 8:
			ctl = 0x17b1;
			pitch = init->pitch >> 2;
			byteswap = 0x000;
			break;
		case 16:
			ctl = 0x17b3;
			pitch = init->pitch >> 1;
			byteswap = 0x100;
			break;
		case 24:
			ctl = 0x17b9;
			pitch = init->pitch - (init->pitch >> 2);
			byteswap = 0x200;
			break;
		case 32:
			ctl = 0x17b5;
			pitch = init->pitch;
			byteswap = 0x300;
			break;
	}
	if (par->ramdac == TVP)
		ctl -= 0x30;

	write_reg_le32(par->dc_regs, HES, init->hes);
	write_reg_le32(par->dc_regs, HEB, init->heb);
	write_reg_le32(par->dc_regs, HSB, init->hsb);
	write_reg_le32(par->dc_regs, HT, init->ht);
	write_reg_le32(par->dc_regs, VES, init->ves);
	write_reg_le32(par->dc_regs, VEB, init->veb);
	write_reg_le32(par->dc_regs, VSB, init->vsb);
	write_reg_le32(par->dc_regs, VT, init->vt);
	write_reg_le32(par->dc_regs, VIL, init->vil);
	write_reg_le32(par->dc_regs, HCIV, 1);
	write_reg_le32(par->dc_regs, VCIV, 1);
	write_reg_le32(par->dc_regs, TCDR, 4);
	write_reg_le32(par->dc_regs, RRCIV, 1);
	write_reg_le32(par->dc_regs, RRSC, 0x980);
	write_reg_le32(par->dc_regs, RRCR, 0x11);

	if (par->ramdac == IBM) {
		write_reg_le32(par->dc_regs, HRIR, 0x0100);
		write_reg_le32(par->dc_regs, CMR, 0x00ff);
		write_reg_le32(par->dc_regs, SRGCTL, 0x0073);
	} else {
		write_reg_le32(par->dc_regs, HRIR, 0x0200);
		write_reg_le32(par->dc_regs, CMR, 0x01ff);
		write_reg_le32(par->dc_regs, SRGCTL, 0x0003);
	}

	switch (info->fix.smem_len) {
		case 0x200000:
			scr = 0x059d | byteswap;
			break;
		/* case 0x400000:
		   case 0x800000: */
		default:
			pitch >>= 1;
			scr = 0x150dd | byteswap;
			break;
	}

	write_reg_le32(par->dc_regs, SCR, scr);
	write_reg_le32(par->dc_regs, SPR, pitch);
	write_reg_le32(par->dc_regs, STGCTL, ctl);
}

static inline void
set_offset (struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct imstt_par *par = info->par;
	__u32 off = var->yoffset * (info->fix.line_length >> 3)
		    + ((var->xoffset * (info->var.bits_per_pixel >> 3)) >> 3);
	write_reg_le32(par->dc_regs, SSR, off);
}

static inline void
set_555 (struct imstt_par *par)
{
	if (par->ramdac == IBM) {
		par->cmap_regs[PIDXHI] = 0;		eieio();
		par->cmap_regs[PIDXLO] = BPP16;		eieio();
		par->cmap_regs[PIDXDATA] = 0x01;	eieio();
	} else {
		par->cmap_regs[TVPADDRW] = TVPIRTCC;	eieio();
		par->cmap_regs[TVPIDATA] = 0x44;	eieio();
	}
}

static inline void
set_565 (struct imstt_par *par)
{
	if (par->ramdac == IBM) {
		par->cmap_regs[PIDXHI] = 0;		eieio();
		par->cmap_regs[PIDXLO] = BPP16;		eieio();
		par->cmap_regs[PIDXDATA] = 0x03;	eieio();
	} else {
		par->cmap_regs[TVPADDRW] = TVPIRTCC;	eieio();
		par->cmap_regs[TVPIDATA] = 0x45;	eieio();
	}
}

static int
imsttfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	if ((var->bits_per_pixel != 8 && var->bits_per_pixel != 16
	    && var->bits_per_pixel != 24 && var->bits_per_pixel != 32)
	    || var->xres_virtual < var->xres || var->yres_virtual < var->yres
	    || var->nonstd
	    || (var->vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
		return -EINVAL;

	if ((var->xres * var->yres) * (var->bits_per_pixel >> 3) > info->fix.smem_len
	    || (var->xres_virtual * var->yres_virtual) * (var->bits_per_pixel >> 3) > info->fix.smem_len)
		return -EINVAL;

	switch (var->bits_per_pixel) {
		case 8:
			var->red.offset = 0;
			var->red.length = 8;
			var->green.offset = 0;
			var->green.length = 8;
			var->blue.offset = 0;
			var->blue.length = 8;
			var->transp.offset = 0;
			var->transp.length = 0;
			break;
		case 16:	/* RGB 555 or 565 */
			if (var->green.length != 6)
				var->red.offset = 10;
			var->red.length = 5;
			var->green.offset = 5;
			if (var->green.length != 6)
				var->green.length = 5;
			var->blue.offset = 0;
			var->blue.length = 5;
			var->transp.offset = 0;
			var->transp.length = 0;
			break;
		case 24:	/* RGB 888 */
			var->red.offset = 16;
			var->red.length = 8;
			var->green.offset = 8;
			var->green.length = 8;
			var->blue.offset = 0;
			var->blue.length = 8;
			var->transp.offset = 0;
			var->transp.length = 0;
			break;
		case 32:	/* RGBA 8888 */
			var->red.offset = 16;
			var->red.length = 8;
			var->green.offset = 8;
			var->green.length = 8;
			var->blue.offset = 0;
			var->blue.length = 8;
			var->transp.offset = 24;
			var->transp.length = 8;
			break;
	}

	if (var->yres == var->yres_virtual) {
		__u32 vram = (info->fix.smem_len - (PAGE_SIZE << 2));
		var->yres_virtual = ((vram << 3) / var->bits_per_pixel) / var->xres_virtual;
		if (var->yres_virtual < var->yres)
			var->yres_virtual = var->yres;
	}

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;
	var->height = -1;
	var->width = -1;
	var->vmode = FB_VMODE_NONINTERLACED;
	var->left_margin = var->right_margin = 16;
	var->upper_margin = var->lower_margin = 16;
	var->hsync_len = var->vsync_len = 8;
	return 0;
}

static int
imsttfb_set_par(struct fb_info *info)
{
	struct imstt_par *par = info->par;

	if (!compute_imstt_regvals(par, info->var.xres, info->var.yres))
		return -EINVAL;

	if (info->var.green.length == 6)
		set_565(par);
	else
		set_555(par);
	set_imstt_regvals(info, info->var.bits_per_pixel);
	info->var.pixclock = 1000000 / getclkMHz(par);
	return 0;
}

static int
imsttfb_setcolreg (u_int regno, u_int red, u_int green, u_int blue,
		   u_int transp, struct fb_info *info)
{
	struct imstt_par *par = info->par;
	u_int bpp = info->var.bits_per_pixel;

	if (regno > 255)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	/* PADDRW/PDATA are the same as TVPPADDRW/TVPPDATA */
	if (0 && bpp == 16)	/* screws up X */
		par->cmap_regs[PADDRW] = regno << 3;
	else
		par->cmap_regs[PADDRW] = regno;
	eieio();

	par->cmap_regs[PDATA] = red;	eieio();
	par->cmap_regs[PDATA] = green;	eieio();
	par->cmap_regs[PDATA] = blue;	eieio();

	if (regno < 16)
		switch (bpp) {
			case 16:
				par->palette[regno] =
					(regno << (info->var.green.length ==
					5 ? 10 : 11)) | (regno << 5) | regno;
				break;
			case 24:
				par->palette[regno] =
					(regno << 16) | (regno << 8) | regno;
				break;
			case 32: {
				int i = (regno << 8) | regno;
				par->palette[regno] = (i << 16) |i;
				break;
			}
		}
	return 0;
}

static int
imsttfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	if (var->xoffset + info->var.xres > info->var.xres_virtual
	    || var->yoffset + info->var.yres > info->var.yres_virtual)
		return -EINVAL;

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;
	set_offset(var, info);
	return 0;
}

static int
imsttfb_blank(int blank, struct fb_info *info)
{
	struct imstt_par *par = info->par;
	__u32 ctrl;

	ctrl = read_reg_le32(par->dc_regs, STGCTL);
	if (blank > 0) {
		switch (blank) {
		case FB_BLANK_NORMAL:
		case FB_BLANK_POWERDOWN:
			ctrl &= ~0x00000380;
			if (par->ramdac == IBM) {
				par->cmap_regs[PIDXHI] = 0;		eieio();
				par->cmap_regs[PIDXLO] = MISCTL2;	eieio();
				par->cmap_regs[PIDXDATA] = 0x55;	eieio();
				par->cmap_regs[PIDXLO] = MISCTL1;	eieio();
				par->cmap_regs[PIDXDATA] = 0x11;	eieio();
				par->cmap_regs[PIDXLO] = SYNCCTL;	eieio();
				par->cmap_regs[PIDXDATA] = 0x0f;	eieio();
				par->cmap_regs[PIDXLO] = PWRMNGMT;	eieio();
				par->cmap_regs[PIDXDATA] = 0x1f;	eieio();
				par->cmap_regs[PIDXLO] = CLKCTL;	eieio();
				par->cmap_regs[PIDXDATA] = 0xc0;
			}
			break;
		case FB_BLANK_VSYNC_SUSPEND:
			ctrl &= ~0x00000020;
			break;
		case FB_BLANK_HSYNC_SUSPEND:
			ctrl &= ~0x00000010;
			break;
		}
	} else {
		if (par->ramdac == IBM) {
			ctrl |= 0x000017b0;
			par->cmap_regs[PIDXHI] = 0;		eieio();
			par->cmap_regs[PIDXLO] = CLKCTL;	eieio();
			par->cmap_regs[PIDXDATA] = 0x01;	eieio();
			par->cmap_regs[PIDXLO] = PWRMNGMT;	eieio();
			par->cmap_regs[PIDXDATA] = 0x00;	eieio();
			par->cmap_regs[PIDXLO] = SYNCCTL;	eieio();
			par->cmap_regs[PIDXDATA] = 0x00;	eieio();
			par->cmap_regs[PIDXLO] = MISCTL1;	eieio();
			par->cmap_regs[PIDXDATA] = 0x01;	eieio();
			par->cmap_regs[PIDXLO] = MISCTL2;	eieio();
			par->cmap_regs[PIDXDATA] = 0x45;	eieio();
		} else
			ctrl |= 0x00001780;
	}
	write_reg_le32(par->dc_regs, STGCTL, ctrl);
	return 0;
}

static void
imsttfb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct imstt_par *par = info->par;
	__u32 Bpp, line_pitch, bgc, dx, dy, width, height;

	bgc = rect->color;
	bgc |= (bgc << 8);
	bgc |= (bgc << 16);

	Bpp = info->var.bits_per_pixel >> 3,
	line_pitch = info->fix.line_length;

	dy = rect->dy * line_pitch;
	dx = rect->dx * Bpp;
	height = rect->height;
	height--;
	width = rect->width * Bpp;
	width--;

	if (rect->rop == ROP_COPY) {
		while(read_reg_le32(par->dc_regs, SSTATUS) & 0x80);
		write_reg_le32(par->dc_regs, DSA, dy + dx);
		write_reg_le32(par->dc_regs, CNT, (height << 16) | width);
		write_reg_le32(par->dc_regs, DP_OCTL, line_pitch);
		write_reg_le32(par->dc_regs, BI, 0xffffffff);
		write_reg_le32(par->dc_regs, MBC, 0xffffffff);
		write_reg_le32(par->dc_regs, CLR, bgc);
		write_reg_le32(par->dc_regs, BLTCTL, 0x840); /* 0x200000 */
		while(read_reg_le32(par->dc_regs, SSTATUS) & 0x80);
		while(read_reg_le32(par->dc_regs, SSTATUS) & 0x40);
	} else {
		while(read_reg_le32(par->dc_regs, SSTATUS) & 0x80);
		write_reg_le32(par->dc_regs, DSA, dy + dx);
		write_reg_le32(par->dc_regs, S1SA, dy + dx);
		write_reg_le32(par->dc_regs, CNT, (height << 16) | width);
		write_reg_le32(par->dc_regs, DP_OCTL, line_pitch);
		write_reg_le32(par->dc_regs, SP, line_pitch);
		write_reg_le32(par->dc_regs, BLTCTL, 0x40005);
		while(read_reg_le32(par->dc_regs, SSTATUS) & 0x80);
		while(read_reg_le32(par->dc_regs, SSTATUS) & 0x40);
	}
}

static void
imsttfb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct imstt_par *par = info->par;
	__u32 Bpp, line_pitch, fb_offset_old, fb_offset_new, sp, dp_octl;
 	__u32 cnt, bltctl, sx, sy, dx, dy, height, width;

	Bpp = info->var.bits_per_pixel >> 3,

	sx = area->sx * Bpp;
	sy = area->sy;
	dx = area->dx * Bpp;
	dy = area->dy;
	height = area->height;
	height--;
	width = area->width * Bpp;
	width--;

	line_pitch = info->fix.line_length;
	bltctl = 0x05;
	sp = line_pitch << 16;
	cnt = height << 16;

	if (sy < dy) {
		sy += height;
		dy += height;
		sp |= -(line_pitch) & 0xffff;
		dp_octl = -(line_pitch) & 0xffff;
	} else {
		sp |= line_pitch;
		dp_octl = line_pitch;
	}
	if (sx < dx) {
		sx += width;
		dx += width;
		bltctl |= 0x80;
		cnt |= -(width) & 0xffff;
	} else {
		cnt |= width;
	}
	fb_offset_old = sy * line_pitch + sx;
	fb_offset_new = dy * line_pitch + dx;

	while(read_reg_le32(par->dc_regs, SSTATUS) & 0x80);
	write_reg_le32(par->dc_regs, S1SA, fb_offset_old);
	write_reg_le32(par->dc_regs, SP, sp);
	write_reg_le32(par->dc_regs, DSA, fb_offset_new);
	write_reg_le32(par->dc_regs, CNT, cnt);
	write_reg_le32(par->dc_regs, DP_OCTL, dp_octl);
	write_reg_le32(par->dc_regs, BLTCTL, bltctl);
	while(read_reg_le32(par->dc_regs, SSTATUS) & 0x80);
	while(read_reg_le32(par->dc_regs, SSTATUS) & 0x40);
}

#if 0
static int
imsttfb_load_cursor_image(struct imstt_par *par, int width, int height, __u8 fgc)
{
	u_int x, y;

	if (width > 32 || height > 32)
		return -EINVAL;

	if (par->ramdac == IBM) {
		par->cmap_regs[PIDXHI] = 1;	eieio();
		for (x = 0; x < 0x100; x++) {
			par->cmap_regs[PIDXLO] = x;		eieio();
			par->cmap_regs[PIDXDATA] = 0x00;	eieio();
		}
		par->cmap_regs[PIDXHI] = 1;	eieio();
		for (y = 0; y < height; y++)
			for (x = 0; x < width >> 2; x++) {
				par->cmap_regs[PIDXLO] = x + y * 8;	eieio();
				par->cmap_regs[PIDXDATA] = 0xff;	eieio();
			}
		par->cmap_regs[PIDXHI] = 0;		eieio();
		par->cmap_regs[PIDXLO] = CURS1R;	eieio();
		par->cmap_regs[PIDXDATA] = fgc;		eieio();
		par->cmap_regs[PIDXLO] = CURS1G;	eieio();
		par->cmap_regs[PIDXDATA] = fgc;		eieio();
		par->cmap_regs[PIDXLO] = CURS1B;	eieio();
		par->cmap_regs[PIDXDATA] = fgc;		eieio();
		par->cmap_regs[PIDXLO] = CURS2R;	eieio();
		par->cmap_regs[PIDXDATA] = fgc;		eieio();
		par->cmap_regs[PIDXLO] = CURS2G;	eieio();
		par->cmap_regs[PIDXDATA] = fgc;		eieio();
		par->cmap_regs[PIDXLO] = CURS2B;	eieio();
		par->cmap_regs[PIDXDATA] = fgc;		eieio();
		par->cmap_regs[PIDXLO] = CURS3R;	eieio();
		par->cmap_regs[PIDXDATA] = fgc;		eieio();
		par->cmap_regs[PIDXLO] = CURS3G;	eieio();
		par->cmap_regs[PIDXDATA] = fgc;		eieio();
		par->cmap_regs[PIDXLO] = CURS3B;	eieio();
		par->cmap_regs[PIDXDATA] = fgc;		eieio();
	} else {
		par->cmap_regs[TVPADDRW] = TVPIRICC;	eieio();
		par->cmap_regs[TVPIDATA] &= 0x03;	eieio();
		par->cmap_regs[TVPADDRW] = 0;		eieio();
		for (x = 0; x < 0x200; x++) {
			par->cmap_regs[TVPCRDAT] = 0x00;	eieio();
		}
		for (x = 0; x < 0x200; x++) {
			par->cmap_regs[TVPCRDAT] = 0xff;	eieio();
		}
		par->cmap_regs[TVPADDRW] = TVPIRICC;	eieio();
		par->cmap_regs[TVPIDATA] &= 0x03;	eieio();
		for (y = 0; y < height; y++)
			for (x = 0; x < width >> 3; x++) {
				par->cmap_regs[TVPADDRW] = x + y * 8;	eieio();
				par->cmap_regs[TVPCRDAT] = 0xff;		eieio();
			}
		par->cmap_regs[TVPADDRW] = TVPIRICC;	eieio();
		par->cmap_regs[TVPIDATA] |= 0x08;	eieio();
		for (y = 0; y < height; y++)
			for (x = 0; x < width >> 3; x++) {
				par->cmap_regs[TVPADDRW] = x + y * 8;	eieio();
				par->cmap_regs[TVPCRDAT] = 0xff;		eieio();
			}
		par->cmap_regs[TVPCADRW] = 0x00;	eieio();
		for (x = 0; x < 12; x++) {
			par->cmap_regs[TVPCDATA] = fgc;
			eieio();
		}
	}
	return 1;
}

static void
imstt_set_cursor(struct imstt_par *par, struct fb_image *d, int on)
{
	if (par->ramdac == IBM) {
		par->cmap_regs[PIDXHI] = 0;	eieio();
		if (!on) {
			par->cmap_regs[PIDXLO] = CURSCTL;	eieio();
			par->cmap_regs[PIDXDATA] = 0x00;	eieio();
		} else {
			par->cmap_regs[PIDXLO] = CURSXHI;	eieio();
			par->cmap_regs[PIDXDATA] = d->dx >> 8;	eieio();
			par->cmap_regs[PIDXLO] = CURSXLO;	eieio();
			par->cmap_regs[PIDXDATA] = d->dx & 0xff;eieio();
			par->cmap_regs[PIDXLO] = CURSYHI;	eieio();
			par->cmap_regs[PIDXDATA] = d->dy >> 8;	eieio();
			par->cmap_regs[PIDXLO] = CURSYLO;	eieio();
			par->cmap_regs[PIDXDATA] = d->dy & 0xff;eieio();
			par->cmap_regs[PIDXLO] = CURSCTL;	eieio();
			par->cmap_regs[PIDXDATA] = 0x02;	eieio();
		}
	} else {
		if (!on) {
			par->cmap_regs[TVPADDRW] = TVPIRICC;	eieio();
			par->cmap_regs[TVPIDATA] = 0x00;	eieio();
		} else {
			__u16 x = d->dx + 0x40, y = d->dy + 0x40;

			par->cmap_regs[TVPCXPOH] = x >> 8;	eieio();
			par->cmap_regs[TVPCXPOL] = x & 0xff;	eieio();
			par->cmap_regs[TVPCYPOH] = y >> 8;	eieio();
			par->cmap_regs[TVPCYPOL] = y & 0xff;	eieio();
			par->cmap_regs[TVPADDRW] = TVPIRICC;	eieio();
			par->cmap_regs[TVPIDATA] = 0x02;	eieio();
		}
	}
}

static int
imsttfb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct imstt_par *par = info->par;
        u32 flags = cursor->set, fg, bg, xx, yy;

	if (cursor->dest == NULL && cursor->rop == ROP_XOR)
		return 1;

	imstt_set_cursor(info, cursor, 0);

	if (flags & FB_CUR_SETPOS) {
		xx = cursor->image.dx - info->var.xoffset;
		yy = cursor->image.dy - info->var.yoffset;
	}

	if (flags & FB_CUR_SETSIZE) {
        }

        if (flags & (FB_CUR_SETSHAPE | FB_CUR_SETCMAP)) {
                int fg_idx = cursor->image.fg_color;
                int width = (cursor->image.width+7)/8;
                u8 *dat = (u8 *) cursor->image.data;
                u8 *dst = (u8 *) cursor->dest;
                u8 *msk = (u8 *) cursor->mask;

                switch (cursor->rop) {
                case ROP_XOR:
                        for (i = 0; i < cursor->image.height; i++) {
                                for (j = 0; j < width; j++) {
                                        d_idx = i * MAX_CURS/8  + j;
                                        data[d_idx] =  byte_rev[dat[s_idx] ^
                                                                dst[s_idx]];
                                        mask[d_idx] = byte_rev[msk[s_idx]];
                                        s_idx++;
                                }
                        }
                        break;
                case ROP_COPY:
                default:
                        for (i = 0; i < cursor->image.height; i++) {
                                for (j = 0; j < width; j++) {
                                        d_idx = i * MAX_CURS/8 + j;
                                        data[d_idx] = byte_rev[dat[s_idx]];
                                        mask[d_idx] = byte_rev[msk[s_idx]];
                                        s_idx++;
                                }
			}
			break;
		}

		fg = ((info->cmap.red[fg_idx] & 0xf8) << 7) |
                     ((info->cmap.green[fg_idx] & 0xf8) << 2) |
                     ((info->cmap.blue[fg_idx] & 0xf8) >> 3) | 1 << 15;

		imsttfb_load_cursor_image(par, xx, yy, fgc);
	}
	if (cursor->enable)
		imstt_set_cursor(info, cursor, 1);
	return 0;
}
#endif

#define FBIMSTT_SETREG		0x545401
#define FBIMSTT_GETREG		0x545402
#define FBIMSTT_SETCMAPREG	0x545403
#define FBIMSTT_GETCMAPREG	0x545404
#define FBIMSTT_SETIDXREG	0x545405
#define FBIMSTT_GETIDXREG	0x545406

static int
imsttfb_ioctl(struct fb_info *info, u_int cmd, u_long arg)
{
	struct imstt_par *par = info->par;
	void __user *argp = (void __user *)arg;
	__u32 reg[2];
	__u8 idx[2];

	switch (cmd) {
		case FBIMSTT_SETREG:
			if (copy_from_user(reg, argp, 8) || reg[0] > (0x1000 - sizeof(reg[0])) / sizeof(reg[0]))
				return -EFAULT;
			write_reg_le32(par->dc_regs, reg[0], reg[1]);
			return 0;
		case FBIMSTT_GETREG:
			if (copy_from_user(reg, argp, 4) || reg[0] > (0x1000 - sizeof(reg[0])) / sizeof(reg[0]))
				return -EFAULT;
			reg[1] = read_reg_le32(par->dc_regs, reg[0]);
			if (copy_to_user((void __user *)(arg + 4), &reg[1], 4))
				return -EFAULT;
			return 0;
		case FBIMSTT_SETCMAPREG:
			if (copy_from_user(reg, argp, 8) || reg[0] > (0x1000 - sizeof(reg[0])) / sizeof(reg[0]))
				return -EFAULT;
			write_reg_le32(((u_int __iomem *)par->cmap_regs), reg[0], reg[1]);
			return 0;
		case FBIMSTT_GETCMAPREG:
			if (copy_from_user(reg, argp, 4) || reg[0] > (0x1000 - sizeof(reg[0])) / sizeof(reg[0]))
				return -EFAULT;
			reg[1] = read_reg_le32(((u_int __iomem *)par->cmap_regs), reg[0]);
			if (copy_to_user((void __user *)(arg + 4), &reg[1], 4))
				return -EFAULT;
			return 0;
		case FBIMSTT_SETIDXREG:
			if (copy_from_user(idx, argp, 2))
				return -EFAULT;
			par->cmap_regs[PIDXHI] = 0;		eieio();
			par->cmap_regs[PIDXLO] = idx[0];	eieio();
			par->cmap_regs[PIDXDATA] = idx[1];	eieio();
			return 0;
		case FBIMSTT_GETIDXREG:
			if (copy_from_user(idx, argp, 1))
				return -EFAULT;
			par->cmap_regs[PIDXHI] = 0;		eieio();
			par->cmap_regs[PIDXLO] = idx[0];	eieio();
			idx[1] = par->cmap_regs[PIDXDATA];
			if (copy_to_user((void __user *)(arg + 1), &idx[1], 1))
				return -EFAULT;
			return 0;
		default:
			return -ENOIOCTLCMD;
	}
}

static const struct pci_device_id imsttfb_pci_tbl[] = {
	{ PCI_VENDOR_ID_IMS, PCI_DEVICE_ID_IMS_TT128,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, IBM },
	{ PCI_VENDOR_ID_IMS, PCI_DEVICE_ID_IMS_TT3D,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, TVP },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, imsttfb_pci_tbl);

static struct pci_driver imsttfb_pci_driver = {
	.name =		"imsttfb",
	.id_table =	imsttfb_pci_tbl,
	.probe =	imsttfb_probe,
	.remove =	imsttfb_remove,
};

static const struct fb_ops imsttfb_ops = {
	.owner 		= THIS_MODULE,
	.fb_check_var	= imsttfb_check_var,
	.fb_set_par 	= imsttfb_set_par,
	.fb_setcolreg 	= imsttfb_setcolreg,
	.fb_pan_display = imsttfb_pan_display,
	.fb_blank 	= imsttfb_blank,
	.fb_fillrect	= imsttfb_fillrect,
	.fb_copyarea	= imsttfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_ioctl 	= imsttfb_ioctl,
};

static void init_imstt(struct fb_info *info)
{
	struct imstt_par *par = info->par;
	__u32 i, tmp, *ip, *end;

	tmp = read_reg_le32(par->dc_regs, PRC);
	if (par->ramdac == IBM)
		info->fix.smem_len = (tmp & 0x0004) ? 0x400000 : 0x200000;
	else
		info->fix.smem_len = 0x800000;

	ip = (__u32 *)info->screen_base;
	end = (__u32 *)(info->screen_base + info->fix.smem_len);
	while (ip < end)
		*ip++ = 0;

	/* initialize the card */
	tmp = read_reg_le32(par->dc_regs, STGCTL);
	write_reg_le32(par->dc_regs, STGCTL, tmp & ~0x1);
	write_reg_le32(par->dc_regs, SSR, 0);

	/* set default values for DAC registers */
	if (par->ramdac == IBM) {
		par->cmap_regs[PPMASK] = 0xff;
		eieio();
		par->cmap_regs[PIDXHI] = 0;
		eieio();
		for (i = 0; i < ARRAY_SIZE(ibm_initregs); i++) {
			par->cmap_regs[PIDXLO] = ibm_initregs[i].addr;
			eieio();
			par->cmap_regs[PIDXDATA] = ibm_initregs[i].value;
			eieio();
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(tvp_initregs); i++) {
			par->cmap_regs[TVPADDRW] = tvp_initregs[i].addr;
			eieio();
			par->cmap_regs[TVPIDATA] = tvp_initregs[i].value;
			eieio();
		}
	}

#if defined(CONFIG_PPC_PMAC) && defined(CONFIG_PPC32)
	if (IS_REACHABLE(CONFIG_NVRAM) && machine_is(powermac)) {
		int vmode = init_vmode, cmode = init_cmode;

		if (vmode == -1) {
			vmode = nvram_read_byte(NV_VMODE);
			if (vmode <= 0 || vmode > VMODE_MAX)
				vmode = VMODE_640_480_67;
		}
		if (cmode == -1) {
			cmode = nvram_read_byte(NV_CMODE);
			if (cmode < CMODE_8 || cmode > CMODE_32)
				cmode = CMODE_8;
		}
		if (mac_vmode_to_var(vmode, cmode, &info->var)) {
			info->var.xres = info->var.xres_virtual = INIT_XRES;
			info->var.yres = info->var.yres_virtual = INIT_YRES;
			info->var.bits_per_pixel = INIT_BPP;
		}
	} else
#endif
	{
		info->var.xres = info->var.xres_virtual = INIT_XRES;
		info->var.yres = info->var.yres_virtual = INIT_YRES;
		info->var.bits_per_pixel = INIT_BPP;
	}

	if ((info->var.xres * info->var.yres) * (info->var.bits_per_pixel >> 3) > info->fix.smem_len
	    || !(compute_imstt_regvals(par, info->var.xres, info->var.yres))) {
		printk("imsttfb: %ux%ux%u not supported\n", info->var.xres, info->var.yres, info->var.bits_per_pixel);
		framebuffer_release(info);
		return;
	}

	sprintf(info->fix.id, "IMS TT (%s)", par->ramdac == IBM ? "IBM" : "TVP");
	info->fix.mmio_len = 0x1000;
	info->fix.accel = FB_ACCEL_IMS_TWINTURBO;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = info->var.bits_per_pixel == 8 ? FB_VISUAL_PSEUDOCOLOR
							: FB_VISUAL_DIRECTCOLOR;
	info->fix.line_length = info->var.xres * (info->var.bits_per_pixel >> 3);
	info->fix.xpanstep = 8;
	info->fix.ypanstep = 1;
	info->fix.ywrapstep = 0;

	info->var.accel_flags = FB_ACCELF_TEXT;

//	if (par->ramdac == IBM)
//		imstt_cursor_init(info);
	if (info->var.green.length == 6)
		set_565(par);
	else
		set_555(par);
	set_imstt_regvals(info, info->var.bits_per_pixel);

	info->var.pixclock = 1000000 / getclkMHz(par);

	info->fbops = &imsttfb_ops;
	info->flags = FBINFO_DEFAULT |
                      FBINFO_HWACCEL_COPYAREA |
	              FBINFO_HWACCEL_FILLRECT |
	              FBINFO_HWACCEL_YPAN;

	fb_alloc_cmap(&info->cmap, 0, 0);

	if (register_framebuffer(info) < 0) {
		framebuffer_release(info);
		return;
	}

	tmp = (read_reg_le32(par->dc_regs, SSTATUS) & 0x0f00) >> 8;
	fb_info(info, "%s frame buffer; %uMB vram; chip version %u\n",
		info->fix.id, info->fix.smem_len >> 20, tmp);
}

static int imsttfb_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	unsigned long addr, size;
	struct imstt_par *par;
	struct fb_info *info;
	struct device_node *dp;
	int ret;

	ret = aperture_remove_conflicting_pci_devices(pdev, "imsttfb");
	if (ret)
		return ret;
	ret = -ENOMEM;

	dp = pci_device_to_OF_node(pdev);
	if(dp)
		printk(KERN_INFO "%s: OF name %pOFn\n",__func__, dp);
	else if (IS_ENABLED(CONFIG_OF))
		printk(KERN_ERR "imsttfb: no OF node for pci device\n");

	info = framebuffer_alloc(sizeof(struct imstt_par), &pdev->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;

	addr = pci_resource_start (pdev, 0);
	size = pci_resource_len (pdev, 0);

	if (!request_mem_region(addr, size, "imsttfb")) {
		printk(KERN_ERR "imsttfb: Can't reserve memory region\n");
		framebuffer_release(info);
		return -ENODEV;
	}

	switch (pdev->device) {
		case PCI_DEVICE_ID_IMS_TT128: /* IMS,tt128mbA */
			par->ramdac = IBM;
			if (of_node_name_eq(dp, "IMS,tt128mb8") ||
			    of_node_name_eq(dp, "IMS,tt128mb8A"))
				par->ramdac = TVP;
			break;
		case PCI_DEVICE_ID_IMS_TT3D:  /* IMS,tt3d */
			par->ramdac = TVP;
			break;
		default:
			printk(KERN_INFO "imsttfb: Device 0x%x unknown, "
					 "contact maintainer.\n", pdev->device);
			ret = -ENODEV;
			goto error;
	}

	info->fix.smem_start = addr;
	info->screen_base = (__u8 *)ioremap(addr, par->ramdac == IBM ?
					    0x400000 : 0x800000);
	if (!info->screen_base)
		goto error;
	info->fix.mmio_start = addr + 0x800000;
	par->dc_regs = ioremap(addr + 0x800000, 0x1000);
	if (!par->dc_regs)
		goto error;
	par->cmap_regs_phys = addr + 0x840000;
	par->cmap_regs = (__u8 *)ioremap(addr + 0x840000, 0x1000);
	if (!par->cmap_regs)
		goto error;
	info->pseudo_palette = par->palette;
	init_imstt(info);

	pci_set_drvdata(pdev, info);
	return 0;

error:
	if (par->dc_regs)
		iounmap(par->dc_regs);
	if (info->screen_base)
		iounmap(info->screen_base);
	release_mem_region(addr, size);
	framebuffer_release(info);
	return ret;
}

static void imsttfb_remove(struct pci_dev *pdev)
{
	struct fb_info *info = pci_get_drvdata(pdev);
	struct imstt_par *par = info->par;
	int size = pci_resource_len(pdev, 0);

	unregister_framebuffer(info);
	iounmap(par->cmap_regs);
	iounmap(par->dc_regs);
	iounmap(info->screen_base);
	release_mem_region(info->fix.smem_start, size);
	framebuffer_release(info);
}

#ifndef MODULE
static int __init
imsttfb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!strncmp(this_opt, "font:", 5)) {
			char *p;
			int i;

			p = this_opt + 5;
			for (i = 0; i < sizeof(fontname) - 1; i++)
				if (!*p || *p == ' ' || *p == ',')
					break;
			memcpy(fontname, this_opt + 5, i);
			fontname[i] = 0;
		} else if (!strncmp(this_opt, "inverse", 7)) {
			inverse = 1;
			fb_invert_cmaps();
		}
#if defined(CONFIG_PPC_PMAC)
		else if (!strncmp(this_opt, "vmode:", 6)) {
			int vmode = simple_strtoul(this_opt+6, NULL, 0);
			if (vmode > 0 && vmode <= VMODE_MAX)
				init_vmode = vmode;
		} else if (!strncmp(this_opt, "cmode:", 6)) {
			int cmode = simple_strtoul(this_opt+6, NULL, 0);
			switch (cmode) {
				case CMODE_8:
				case 8:
					init_cmode = CMODE_8;
					break;
				case CMODE_16:
				case 15:
				case 16:
					init_cmode = CMODE_16;
					break;
				case CMODE_32:
				case 24:
				case 32:
					init_cmode = CMODE_32;
					break;
			}
		}
#endif
	}
	return 0;
}

#endif /* MODULE */

static int __init imsttfb_init(void)
{
#ifndef MODULE
	char *option = NULL;
#endif

	if (fb_modesetting_disabled("imsttfb"))
		return -ENODEV;

#ifndef MODULE
	if (fb_get_options("imsttfb", &option))
		return -ENODEV;

	imsttfb_setup(option);
#endif
	return pci_register_driver(&imsttfb_pci_driver);
}

static void __exit imsttfb_exit(void)
{
	pci_unregister_driver(&imsttfb_pci_driver);
}

MODULE_LICENSE("GPL");

module_init(imsttfb_init);
module_exit(imsttfb_exit);

