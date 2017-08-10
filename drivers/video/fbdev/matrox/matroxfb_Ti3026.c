/*
 *
 * Hardware accelerated Matrox Millennium I, II, Mystique, G100, G200 and G400
 *
 * (c) 1998-2002 Petr Vandrovec <vandrove@vc.cvut.cz>
 *
 * Portions Copyright (c) 2001 Matrox Graphics Inc.
 *
 * Version: 1.65 2002/08/14
 *
 * MTRR stuff: 1998 Tom Rini <trini@kernel.crashing.org>
 *
 * Contributors: "menion?" <menion@mindless.com>
 *                     Betatesting, fixes, ideas
 *
 *               "Kurt Garloff" <garloff@suse.de>
 *                     Betatesting, fixes, ideas, videomodes, videomodes timmings
 *
 *               "Tom Rini" <trini@kernel.crashing.org>
 *                     MTRR stuff, PPC cleanups, betatesting, fixes, ideas
 *
 *               "Bibek Sahu" <scorpio@dodds.net>
 *                     Access device through readb|w|l and write b|w|l
 *                     Extensive debugging stuff
 *
 *               "Daniel Haun" <haund@usa.net>
 *                     Testing, hardware cursor fixes
 *
 *               "Scott Wood" <sawst46+@pitt.edu>
 *                     Fixes
 *
 *               "Gerd Knorr" <kraxel@goldbach.isdn.cs.tu-berlin.de>
 *                     Betatesting
 *
 *               "Kelly French" <targon@hazmat.com>
 *               "Fernando Herrera" <fherrera@eurielec.etsit.upm.es>
 *                     Betatesting, bug reporting
 *
 *               "Pablo Bianucci" <pbian@pccp.com.ar>
 *                     Fixes, ideas, betatesting
 *
 *               "Inaky Perez Gonzalez" <inaky@peloncho.fis.ucm.es>
 *                     Fixes, enhandcements, ideas, betatesting
 *
 *               "Ryuichi Oikawa" <roikawa@rr.iiij4u.or.jp>
 *                     PPC betatesting, PPC support, backward compatibility
 *
 *               "Paul Womar" <Paul@pwomar.demon.co.uk>
 *               "Owen Waller" <O.Waller@ee.qub.ac.uk>
 *                     PPC betatesting
 *
 *               "Thomas Pornin" <pornin@bolet.ens.fr>
 *                     Alpha betatesting
 *
 *               "Pieter van Leuven" <pvl@iae.nl>
 *               "Ulf Jaenicke-Roessler" <ujr@physik.phy.tu-dresden.de>
 *                     G100 testing
 *
 *               "H. Peter Arvin" <hpa@transmeta.com>
 *                     Ideas
 *
 *               "Cort Dougan" <cort@cs.nmt.edu>
 *                     CHRP fixes and PReP cleanup
 *
 *               "Mark Vojkovich" <mvojkovi@ucsd.edu>
 *                     G400 support
 *
 * (following author is not in any relation with this code, but his code
 *  is included in this driver)
 *
 * Based on framebuffer driver for VBE 2.0 compliant graphic boards
 *     (c) 1998 Gerd Knorr <kraxel@cs.tu-berlin.de>
 *
 * (following author is not in any relation with this code, but his ideas
 *  were used when writing this driver)
 *
 *		 FreeVBE/AF (Matrox), "Shawn Hargreaves" <shawn@talula.demon.co.uk>
 *
 */


#include "matroxfb_Ti3026.h"
#include "matroxfb_misc.h"
#include "matroxfb_accel.h"
#include <linux/matroxfb.h>

#ifdef CONFIG_FB_MATROX_MILLENIUM
#define outTi3026 matroxfb_DAC_out
#define inTi3026 matroxfb_DAC_in

#define TVP3026_INDEX		0x00
#define TVP3026_PALWRADD	0x00
#define TVP3026_PALDATA		0x01
#define TVP3026_PIXRDMSK	0x02
#define TVP3026_PALRDADD	0x03
#define TVP3026_CURCOLWRADD	0x04
#define     TVP3026_CLOVERSCAN		0x00
#define     TVP3026_CLCOLOR0		0x01
#define     TVP3026_CLCOLOR1		0x02
#define     TVP3026_CLCOLOR2		0x03
#define TVP3026_CURCOLDATA	0x05
#define TVP3026_CURCOLRDADD	0x07
#define TVP3026_CURCTRL		0x09
#define TVP3026_X_DATAREG	0x0A
#define TVP3026_CURRAMDATA	0x0B
#define TVP3026_CURPOSXL	0x0C
#define TVP3026_CURPOSXH	0x0D
#define TVP3026_CURPOSYL	0x0E
#define TVP3026_CURPOSYH	0x0F

#define TVP3026_XSILICONREV	0x01
#define TVP3026_XCURCTRL	0x06
#define     TVP3026_XCURCTRL_DIS	0x00	/* transparent, transparent, transparent, transparent */
#define     TVP3026_XCURCTRL_3COLOR	0x01	/* transparent, 0, 1, 2 */
#define     TVP3026_XCURCTRL_XGA	0x02	/* 0, 1, transparent, complement */
#define     TVP3026_XCURCTRL_XWIN	0x03	/* transparent, transparent, 0, 1 */
#define     TVP3026_XCURCTRL_BLANK2048	0x00
#define     TVP3026_XCURCTRL_BLANK4096	0x10
#define     TVP3026_XCURCTRL_INTERLACED	0x20
#define     TVP3026_XCURCTRL_ODD	0x00 /* ext.signal ODD/\EVEN */
#define     TVP3026_XCURCTRL_EVEN	0x40 /* ext.signal EVEN/\ODD */
#define     TVP3026_XCURCTRL_INDIRECT	0x00
#define     TVP3026_XCURCTRL_DIRECT	0x80
#define TVP3026_XLATCHCTRL	0x0F
#define     TVP3026_XLATCHCTRL_1_1	0x06
#define     TVP3026_XLATCHCTRL_2_1	0x07
#define     TVP3026_XLATCHCTRL_4_1	0x06
#define     TVP3026_XLATCHCTRL_8_1	0x06
#define     TVP3026_XLATCHCTRL_16_1	0x06
#define     TVP3026A_XLATCHCTRL_4_3	0x06	/* ??? do not understand... but it works... !!! */
#define     TVP3026A_XLATCHCTRL_8_3	0x07
#define     TVP3026B_XLATCHCTRL_4_3	0x08
#define     TVP3026B_XLATCHCTRL_8_3	0x06	/* ??? do not understand... but it works... !!! */
#define TVP3026_XTRUECOLORCTRL	0x18
#define     TVP3026_XTRUECOLORCTRL_VRAM_SHIFT_ACCEL	0x00
#define     TVP3026_XTRUECOLORCTRL_VRAM_SHIFT_TVP	0x20
#define     TVP3026_XTRUECOLORCTRL_PSEUDOCOLOR		0x80
#define     TVP3026_XTRUECOLORCTRL_TRUECOLOR		0x40 /* paletized */
#define     TVP3026_XTRUECOLORCTRL_DIRECTCOLOR		0x00
#define     TVP3026_XTRUECOLORCTRL_24_ALTERNATE		0x08 /* 5:4/5:2 instead of 4:3/8:3 */
#define     TVP3026_XTRUECOLORCTRL_RGB_888		0x16 /* 4:3/8:3 (or 5:4/5:2) */
#define	    TVP3026_XTRUECOLORCTRL_BGR_888		0x17
#define     TVP3026_XTRUECOLORCTRL_ORGB_8888		0x06
#define     TVP3026_XTRUECOLORCTRL_BGRO_8888		0x07
#define     TVP3026_XTRUECOLORCTRL_RGB_565		0x05
#define     TVP3026_XTRUECOLORCTRL_ORGB_1555		0x04
#define     TVP3026_XTRUECOLORCTRL_RGB_664		0x03
#define     TVP3026_XTRUECOLORCTRL_RGBO_4444		0x01
#define TVP3026_XMUXCTRL	0x19
#define     TVP3026_XMUXCTRL_MEMORY_8BIT			0x01 /* - */
#define     TVP3026_XMUXCTRL_MEMORY_16BIT			0x02 /* - */
#define     TVP3026_XMUXCTRL_MEMORY_32BIT			0x03 /* 2MB RAM, 512K * 4 */
#define     TVP3026_XMUXCTRL_MEMORY_64BIT			0x04 /* >2MB RAM, 512K * 8 & more */
#define     TVP3026_XMUXCTRL_PIXEL_4BIT				0x40 /* L0,H0,L1,H1... */
#define     TVP3026_XMUXCTRL_PIXEL_4BIT_SWAPPED			0x60 /* H0,L0,H1,L1... */
#define     TVP3026_XMUXCTRL_PIXEL_8BIT				0x48
#define     TVP3026_XMUXCTRL_PIXEL_16BIT			0x50
#define     TVP3026_XMUXCTRL_PIXEL_32BIT			0x58
#define     TVP3026_XMUXCTRL_VGA				0x98 /* VGA MEMORY, 8BIT PIXEL */
#define TVP3026_XCLKCTRL	0x1A
#define     TVP3026_XCLKCTRL_DIV1	0x00
#define     TVP3026_XCLKCTRL_DIV2	0x10
#define     TVP3026_XCLKCTRL_DIV4	0x20
#define     TVP3026_XCLKCTRL_DIV8	0x30
#define     TVP3026_XCLKCTRL_DIV16	0x40
#define     TVP3026_XCLKCTRL_DIV32	0x50
#define     TVP3026_XCLKCTRL_DIV64	0x60
#define     TVP3026_XCLKCTRL_CLKSTOPPED	0x70
#define     TVP3026_XCLKCTRL_SRC_CLK0	0x00
#define     TVP3026_XCLKCTRL_SRC_CLK1   0x01
#define     TVP3026_XCLKCTRL_SRC_CLK2	0x02	/* CLK2 is TTL source*/
#define     TVP3026_XCLKCTRL_SRC_NCLK2	0x03	/* not CLK2 is TTL source */
#define     TVP3026_XCLKCTRL_SRC_ECLK2	0x04	/* CLK2 and not CLK2 is ECL source */
#define     TVP3026_XCLKCTRL_SRC_PLL	0x05
#define     TVP3026_XCLKCTRL_SRC_DIS	0x06	/* disable & poweroff internal clock */
#define     TVP3026_XCLKCTRL_SRC_CLK0VGA 0x07
#define TVP3026_XPALETTEPAGE	0x1C
#define TVP3026_XGENCTRL	0x1D
#define     TVP3026_XGENCTRL_HSYNC_POS	0x00
#define     TVP3026_XGENCTRL_HSYNC_NEG	0x01
#define     TVP3026_XGENCTRL_VSYNC_POS	0x00
#define     TVP3026_XGENCTRL_VSYNC_NEG	0x02
#define     TVP3026_XGENCTRL_LITTLE_ENDIAN 0x00
#define     TVP3026_XGENCTRL_BIG_ENDIAN    0x08
#define     TVP3026_XGENCTRL_BLACK_0IRE		0x00
#define     TVP3026_XGENCTRL_BLACK_75IRE	0x10
#define     TVP3026_XGENCTRL_NO_SYNC_ON_GREEN	0x00
#define     TVP3026_XGENCTRL_SYNC_ON_GREEN	0x20
#define     TVP3026_XGENCTRL_OVERSCAN_DIS	0x00
#define     TVP3026_XGENCTRL_OVERSCAN_EN	0x40
#define TVP3026_XMISCCTRL	0x1E
#define     TVP3026_XMISCCTRL_DAC_PUP	0x00
#define     TVP3026_XMISCCTRL_DAC_PDOWN	0x01
#define     TVP3026_XMISCCTRL_DAC_EXT	0x00 /* or 8, bit 3 is ignored */
#define     TVP3026_XMISCCTRL_DAC_6BIT	0x04
#define     TVP3026_XMISCCTRL_DAC_8BIT	0x0C
#define     TVP3026_XMISCCTRL_PSEL_DIS	0x00
#define     TVP3026_XMISCCTRL_PSEL_EN	0x10
#define     TVP3026_XMISCCTRL_PSEL_LOW	0x00 /* PSEL high selects directcolor */
#define     TVP3026_XMISCCTRL_PSEL_HIGH 0x20 /* PSEL high selects truecolor or pseudocolor */
#define TVP3026_XGENIOCTRL	0x2A
#define TVP3026_XGENIODATA	0x2B
#define TVP3026_XPLLADDR	0x2C
#define     TVP3026_XPLLADDR_X(LOOP,MCLK,PIX) (((LOOP)<<4) | ((MCLK)<<2) | (PIX))
#define     TVP3026_XPLLDATA_N		0x00
#define     TVP3026_XPLLDATA_M		0x01
#define     TVP3026_XPLLDATA_P		0x02
#define     TVP3026_XPLLDATA_STAT	0x03
#define TVP3026_XPIXPLLDATA	0x2D
#define TVP3026_XMEMPLLDATA	0x2E
#define TVP3026_XLOOPPLLDATA	0x2F
#define TVP3026_XCOLKEYOVRMIN	0x30
#define TVP3026_XCOLKEYOVRMAX	0x31
#define TVP3026_XCOLKEYREDMIN	0x32
#define TVP3026_XCOLKEYREDMAX	0x33
#define TVP3026_XCOLKEYGREENMIN	0x34
#define TVP3026_XCOLKEYGREENMAX	0x35
#define TVP3026_XCOLKEYBLUEMIN	0x36
#define TVP3026_XCOLKEYBLUEMAX	0x37
#define TVP3026_XCOLKEYCTRL	0x38
#define     TVP3026_XCOLKEYCTRL_OVR_EN	0x01
#define     TVP3026_XCOLKEYCTRL_RED_EN	0x02
#define     TVP3026_XCOLKEYCTRL_GREEN_EN 0x04
#define     TVP3026_XCOLKEYCTRL_BLUE_EN	0x08
#define     TVP3026_XCOLKEYCTRL_NEGATE	0x10
#define     TVP3026_XCOLKEYCTRL_ZOOM1	0x00
#define     TVP3026_XCOLKEYCTRL_ZOOM2	0x20
#define     TVP3026_XCOLKEYCTRL_ZOOM4	0x40
#define     TVP3026_XCOLKEYCTRL_ZOOM8	0x60
#define     TVP3026_XCOLKEYCTRL_ZOOM16	0x80
#define     TVP3026_XCOLKEYCTRL_ZOOM32	0xA0
#define TVP3026_XMEMPLLCTRL	0x39
#define     TVP3026_XMEMPLLCTRL_DIV(X)	(((X)-1)>>1)	/* 2,4,6,8,10,12,14,16, division applied to LOOP PLL after divide by 2^P */
#define     TVP3026_XMEMPLLCTRL_STROBEMKC4	0x08
#define     TVP3026_XMEMPLLCTRL_MCLK_DOTCLOCK	0x00	/* MKC4 */
#define     TVP3026_XMEMPLLCTRL_MCLK_MCLKPLL	0x10	/* MKC4 */
#define     TVP3026_XMEMPLLCTRL_RCLK_PIXPLL	0x00
#define     TVP3026_XMEMPLLCTRL_RCLK_LOOPPLL	0x20
#define     TVP3026_XMEMPLLCTRL_RCLK_DOTDIVN	0x40	/* dot clock divided by loop pclk N prescaler */
#define TVP3026_XSENSETEST	0x3A
#define TVP3026_XTESTMODEDATA	0x3B
#define TVP3026_XCRCREML	0x3C
#define TVP3026_XCRCREMH	0x3D
#define TVP3026_XCRCBITSEL	0x3E
#define TVP3026_XID		0x3F

static const unsigned char DACseq[] =
{ TVP3026_XLATCHCTRL, TVP3026_XTRUECOLORCTRL,
  TVP3026_XMUXCTRL, TVP3026_XCLKCTRL,
  TVP3026_XPALETTEPAGE,
  TVP3026_XGENCTRL,
  TVP3026_XMISCCTRL,
  TVP3026_XGENIOCTRL,
  TVP3026_XGENIODATA,
  TVP3026_XCOLKEYOVRMIN, TVP3026_XCOLKEYOVRMAX, TVP3026_XCOLKEYREDMIN, TVP3026_XCOLKEYREDMAX,
  TVP3026_XCOLKEYGREENMIN, TVP3026_XCOLKEYGREENMAX, TVP3026_XCOLKEYBLUEMIN, TVP3026_XCOLKEYBLUEMAX,
  TVP3026_XCOLKEYCTRL,
  TVP3026_XMEMPLLCTRL, TVP3026_XSENSETEST, TVP3026_XCURCTRL };

#define POS3026_XLATCHCTRL	0
#define POS3026_XTRUECOLORCTRL	1
#define POS3026_XMUXCTRL	2
#define POS3026_XCLKCTRL	3
#define POS3026_XGENCTRL	5
#define POS3026_XMISCCTRL	6
#define POS3026_XMEMPLLCTRL	18
#define POS3026_XCURCTRL	20

static const unsigned char MGADACbpp32[] =
{ TVP3026_XLATCHCTRL_2_1, TVP3026_XTRUECOLORCTRL_DIRECTCOLOR | TVP3026_XTRUECOLORCTRL_ORGB_8888,
  0x00, TVP3026_XCLKCTRL_DIV1 | TVP3026_XCLKCTRL_SRC_PLL,
  0x00,
  TVP3026_XGENCTRL_HSYNC_POS | TVP3026_XGENCTRL_VSYNC_POS | TVP3026_XGENCTRL_LITTLE_ENDIAN | TVP3026_XGENCTRL_BLACK_0IRE | TVP3026_XGENCTRL_NO_SYNC_ON_GREEN | TVP3026_XGENCTRL_OVERSCAN_DIS,
  TVP3026_XMISCCTRL_DAC_PUP | TVP3026_XMISCCTRL_DAC_8BIT | TVP3026_XMISCCTRL_PSEL_DIS | TVP3026_XMISCCTRL_PSEL_HIGH,
  0x00,
  0x1E,
  0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF,
  TVP3026_XCOLKEYCTRL_ZOOM1,
  0x00, 0x00, TVP3026_XCURCTRL_DIS };

static int Ti3026_calcclock(const struct matrox_fb_info *minfo,
			    unsigned int freq, unsigned int fmax, int *in,
			    int *feed, int *post)
{
	unsigned int fvco;
	unsigned int lin, lfeed, lpost;

	DBG(__func__)

	fvco = PLL_calcclock(minfo, freq, fmax, &lin, &lfeed, &lpost);
	fvco >>= (*post = lpost);
	*in = 64 - lin;
	*feed = 64 - lfeed;
	return fvco;
}

static int Ti3026_setpclk(struct matrox_fb_info *minfo, int clk)
{
	unsigned int f_pll;
	unsigned int pixfeed, pixin, pixpost;
	struct matrox_hw_state *hw = &minfo->hw;

	DBG(__func__)

	f_pll = Ti3026_calcclock(minfo, clk, minfo->max_pixel_clock, &pixin, &pixfeed, &pixpost);

	hw->DACclk[0] = pixin | 0xC0;
	hw->DACclk[1] = pixfeed;
	hw->DACclk[2] = pixpost | 0xB0;

	{
		unsigned int loopfeed, loopin, looppost, loopdiv, z;
		unsigned int Bpp;

		Bpp = minfo->curr.final_bppShift;

		if (minfo->fbcon.var.bits_per_pixel == 24) {
			loopfeed = 3;		/* set lm to any possible value */
			loopin = 3 * 32 / Bpp;
		} else {
			loopfeed = 4;
			loopin = 4 * 32 / Bpp;
		}
		z = (110000 * loopin) / (f_pll * loopfeed);
		loopdiv = 0; /* div 2 */
		if (z < 2)
			looppost = 0;
		else if (z < 4)
			looppost = 1;
		else if (z < 8)
			looppost = 2;
		else {
			looppost = 3;
			loopdiv = z/16;
		}
		if (minfo->fbcon.var.bits_per_pixel == 24) {
			hw->DACclk[3] = ((65 - loopin) & 0x3F) | 0xC0;
			hw->DACclk[4] = (65 - loopfeed) | 0x80;
			if (minfo->accel.ramdac_rev > 0x20) {
				if (isInterleave(minfo))
					hw->DACreg[POS3026_XLATCHCTRL] = TVP3026B_XLATCHCTRL_8_3;
				else {
					hw->DACclk[4] &= ~0xC0;
					hw->DACreg[POS3026_XLATCHCTRL] = TVP3026B_XLATCHCTRL_4_3;
				}
			} else {
				if (isInterleave(minfo))
					;	/* default... */
				else {
					hw->DACclk[4] ^= 0xC0;	/* change from 0x80 to 0x40 */
					hw->DACreg[POS3026_XLATCHCTRL] = TVP3026A_XLATCHCTRL_4_3;
				}
			}
			hw->DACclk[5] = looppost | 0xF8;
			if (minfo->devflags.mga_24bpp_fix)
				hw->DACclk[5] ^= 0x40;
		} else {
			hw->DACclk[3] = ((65 - loopin) & 0x3F) | 0xC0;
			hw->DACclk[4] = 65 - loopfeed;
			hw->DACclk[5] = looppost | 0xF0;
		}
		hw->DACreg[POS3026_XMEMPLLCTRL] = loopdiv | TVP3026_XMEMPLLCTRL_MCLK_MCLKPLL | TVP3026_XMEMPLLCTRL_RCLK_LOOPPLL;
	}
	return 0;
}

static int Ti3026_init(struct matrox_fb_info *minfo, struct my_timming *m)
{
	u_int8_t muxctrl = isInterleave(minfo) ? TVP3026_XMUXCTRL_MEMORY_64BIT : TVP3026_XMUXCTRL_MEMORY_32BIT;
	struct matrox_hw_state *hw = &minfo->hw;

	DBG(__func__)

	memcpy(hw->DACreg, MGADACbpp32, sizeof(MGADACbpp32));
	switch (minfo->fbcon.var.bits_per_pixel) {
		case 4:	hw->DACreg[POS3026_XLATCHCTRL] = TVP3026_XLATCHCTRL_16_1;	/* or _8_1, they are same */
			hw->DACreg[POS3026_XTRUECOLORCTRL] = TVP3026_XTRUECOLORCTRL_PSEUDOCOLOR;
			hw->DACreg[POS3026_XMUXCTRL] = muxctrl | TVP3026_XMUXCTRL_PIXEL_4BIT;
			hw->DACreg[POS3026_XCLKCTRL] = TVP3026_XCLKCTRL_SRC_PLL | TVP3026_XCLKCTRL_DIV8;
			hw->DACreg[POS3026_XMISCCTRL] = TVP3026_XMISCCTRL_DAC_PUP | TVP3026_XMISCCTRL_DAC_8BIT | TVP3026_XMISCCTRL_PSEL_DIS | TVP3026_XMISCCTRL_PSEL_LOW;
			break;
		case 8: hw->DACreg[POS3026_XLATCHCTRL] = TVP3026_XLATCHCTRL_8_1;	/* or _4_1, they are same */
			hw->DACreg[POS3026_XTRUECOLORCTRL] = TVP3026_XTRUECOLORCTRL_PSEUDOCOLOR;
			hw->DACreg[POS3026_XMUXCTRL] = muxctrl | TVP3026_XMUXCTRL_PIXEL_8BIT;
			hw->DACreg[POS3026_XCLKCTRL] = TVP3026_XCLKCTRL_SRC_PLL | TVP3026_XCLKCTRL_DIV4;
			hw->DACreg[POS3026_XMISCCTRL] = TVP3026_XMISCCTRL_DAC_PUP | TVP3026_XMISCCTRL_DAC_8BIT | TVP3026_XMISCCTRL_PSEL_DIS | TVP3026_XMISCCTRL_PSEL_LOW;
			break;
		case 16:
			/* XLATCHCTRL should be _4_1 / _2_1... Why is not? (_2_1 is used every time) */
			hw->DACreg[POS3026_XTRUECOLORCTRL] = (minfo->fbcon.var.green.length == 5) ? (TVP3026_XTRUECOLORCTRL_DIRECTCOLOR | TVP3026_XTRUECOLORCTRL_ORGB_1555) : (TVP3026_XTRUECOLORCTRL_DIRECTCOLOR | TVP3026_XTRUECOLORCTRL_RGB_565);
			hw->DACreg[POS3026_XMUXCTRL] = muxctrl | TVP3026_XMUXCTRL_PIXEL_16BIT;
			hw->DACreg[POS3026_XCLKCTRL] = TVP3026_XCLKCTRL_SRC_PLL | TVP3026_XCLKCTRL_DIV2;
			break;
		case 24:
			/* XLATCHCTRL is: for (A) use _4_3 (?_8_3 is same? TBD), for (B) it is set in setpclk */
			hw->DACreg[POS3026_XTRUECOLORCTRL] = TVP3026_XTRUECOLORCTRL_DIRECTCOLOR | TVP3026_XTRUECOLORCTRL_RGB_888;
			hw->DACreg[POS3026_XMUXCTRL] = muxctrl | TVP3026_XMUXCTRL_PIXEL_32BIT;
			hw->DACreg[POS3026_XCLKCTRL] = TVP3026_XCLKCTRL_SRC_PLL | TVP3026_XCLKCTRL_DIV4;
			break;
		case 32:
			/* XLATCHCTRL should be _2_1 / _1_1... Why is not? (_2_1 is used every time) */
			hw->DACreg[POS3026_XMUXCTRL] = muxctrl | TVP3026_XMUXCTRL_PIXEL_32BIT;
			break;
		default:
			return 1;	/* TODO: failed */
	}
	if (matroxfb_vgaHWinit(minfo, m)) return 1;

	/* set SYNC */
	hw->MiscOutReg = 0xCB;
	if (m->sync & FB_SYNC_HOR_HIGH_ACT)
		hw->DACreg[POS3026_XGENCTRL] |= TVP3026_XGENCTRL_HSYNC_NEG;
	if (m->sync & FB_SYNC_VERT_HIGH_ACT)
		hw->DACreg[POS3026_XGENCTRL] |= TVP3026_XGENCTRL_VSYNC_NEG;
	if (m->sync & FB_SYNC_ON_GREEN)
		hw->DACreg[POS3026_XGENCTRL] |= TVP3026_XGENCTRL_SYNC_ON_GREEN;

	/* set DELAY */
	if (minfo->video.len < 0x400000)
		hw->CRTCEXT[3] |= 0x08;
	else if (minfo->video.len > 0x400000)
		hw->CRTCEXT[3] |= 0x10;

	/* set HWCURSOR */
	if (m->interlaced) {
		hw->DACreg[POS3026_XCURCTRL] |= TVP3026_XCURCTRL_INTERLACED;
	}
	if (m->HTotal >= 1536)
		hw->DACreg[POS3026_XCURCTRL] |= TVP3026_XCURCTRL_BLANK4096;

	/* set interleaving */
	hw->MXoptionReg &= ~0x00001000;
	if (isInterleave(minfo)) hw->MXoptionReg |= 0x00001000;

	/* set DAC */
	Ti3026_setpclk(minfo, m->pixclock);
	return 0;
}

static void ti3026_setMCLK(struct matrox_fb_info *minfo, int fout)
{
	unsigned int f_pll;
	unsigned int pclk_m, pclk_n, pclk_p;
	unsigned int mclk_m, mclk_n, mclk_p;
	unsigned int rfhcnt, mclk_ctl;
	int tmout;

	DBG(__func__)

	f_pll = Ti3026_calcclock(minfo, fout, minfo->max_pixel_clock, &mclk_n, &mclk_m, &mclk_p);

	/* save pclk */
	outTi3026(minfo, TVP3026_XPLLADDR, 0xFC);
	pclk_n = inTi3026(minfo, TVP3026_XPIXPLLDATA);
	outTi3026(minfo, TVP3026_XPLLADDR, 0xFD);
	pclk_m = inTi3026(minfo, TVP3026_XPIXPLLDATA);
	outTi3026(minfo, TVP3026_XPLLADDR, 0xFE);
	pclk_p = inTi3026(minfo, TVP3026_XPIXPLLDATA);

	/* stop pclk */
	outTi3026(minfo, TVP3026_XPLLADDR, 0xFE);
	outTi3026(minfo, TVP3026_XPIXPLLDATA, 0x00);

	/* set pclk to new mclk */
	outTi3026(minfo, TVP3026_XPLLADDR, 0xFC);
	outTi3026(minfo, TVP3026_XPIXPLLDATA, mclk_n | 0xC0);
	outTi3026(minfo, TVP3026_XPIXPLLDATA, mclk_m);
	outTi3026(minfo, TVP3026_XPIXPLLDATA, mclk_p | 0xB0);

	/* wait for PLL to lock */
	for (tmout = 500000; tmout; tmout--) {
		if (inTi3026(minfo, TVP3026_XPIXPLLDATA) & 0x40)
			break;
		udelay(10);
	}
	if (!tmout)
		printk(KERN_ERR "matroxfb: Temporary pixel PLL not locked after 5 secs\n");

	/* output pclk on mclk pin */
	mclk_ctl = inTi3026(minfo, TVP3026_XMEMPLLCTRL);
	outTi3026(minfo, TVP3026_XMEMPLLCTRL, mclk_ctl & 0xE7);
	outTi3026(minfo, TVP3026_XMEMPLLCTRL, (mclk_ctl & 0xE7) | TVP3026_XMEMPLLCTRL_STROBEMKC4);

	/* stop MCLK */
	outTi3026(minfo, TVP3026_XPLLADDR, 0xFB);
	outTi3026(minfo, TVP3026_XMEMPLLDATA, 0x00);

	/* set mclk to new freq */
	outTi3026(minfo, TVP3026_XPLLADDR, 0xF3);
	outTi3026(minfo, TVP3026_XMEMPLLDATA, mclk_n | 0xC0);
	outTi3026(minfo, TVP3026_XMEMPLLDATA, mclk_m);
	outTi3026(minfo, TVP3026_XMEMPLLDATA, mclk_p | 0xB0);

	/* wait for PLL to lock */
	for (tmout = 500000; tmout; tmout--) {
		if (inTi3026(minfo, TVP3026_XMEMPLLDATA) & 0x40)
			break;
		udelay(10);
	}
	if (!tmout)
		printk(KERN_ERR "matroxfb: Memory PLL not locked after 5 secs\n");

	f_pll = f_pll * 333 / (10000 << mclk_p);
	if (isMilleniumII(minfo)) {
		rfhcnt = (f_pll - 128) / 256;
		if (rfhcnt > 15)
			rfhcnt = 15;
	} else {
		rfhcnt = (f_pll - 64) / 128;
		if (rfhcnt > 15)
			rfhcnt = 0;
	}
	minfo->hw.MXoptionReg = (minfo->hw.MXoptionReg & ~0x000F0000) | (rfhcnt << 16);
	pci_write_config_dword(minfo->pcidev, PCI_OPTION_REG, minfo->hw.MXoptionReg);

	/* output MCLK to MCLK pin */
	outTi3026(minfo, TVP3026_XMEMPLLCTRL, (mclk_ctl & 0xE7) | TVP3026_XMEMPLLCTRL_MCLK_MCLKPLL);
	outTi3026(minfo, TVP3026_XMEMPLLCTRL, (mclk_ctl       ) | TVP3026_XMEMPLLCTRL_MCLK_MCLKPLL | TVP3026_XMEMPLLCTRL_STROBEMKC4);

	/* stop PCLK */
	outTi3026(minfo, TVP3026_XPLLADDR, 0xFE);
	outTi3026(minfo, TVP3026_XPIXPLLDATA, 0x00);

	/* restore pclk */
	outTi3026(minfo, TVP3026_XPLLADDR, 0xFC);
	outTi3026(minfo, TVP3026_XPIXPLLDATA, pclk_n);
	outTi3026(minfo, TVP3026_XPIXPLLDATA, pclk_m);
	outTi3026(minfo, TVP3026_XPIXPLLDATA, pclk_p);

	/* wait for PLL to lock */
	for (tmout = 500000; tmout; tmout--) {
		if (inTi3026(minfo, TVP3026_XPIXPLLDATA) & 0x40)
			break;
		udelay(10);
	}
	if (!tmout)
		printk(KERN_ERR "matroxfb: Pixel PLL not locked after 5 secs\n");
}

static void ti3026_ramdac_init(struct matrox_fb_info *minfo)
{
	DBG(__func__)

	minfo->features.pll.vco_freq_min = 110000;
	minfo->features.pll.ref_freq	 = 114545;
	minfo->features.pll.feed_div_min = 2;
	minfo->features.pll.feed_div_max = 24;
	minfo->features.pll.in_div_min	 = 2;
	minfo->features.pll.in_div_max	 = 63;
	minfo->features.pll.post_shift_max = 3;
	if (minfo->devflags.noinit)
		return;
	ti3026_setMCLK(minfo, 60000);
}

static void Ti3026_restore(struct matrox_fb_info *minfo)
{
	int i;
	unsigned char progdac[6];
	struct matrox_hw_state *hw = &minfo->hw;
	CRITFLAGS

	DBG(__func__)

#ifdef DEBUG
	dprintk(KERN_INFO "EXTVGA regs: ");
	for (i = 0; i < 6; i++)
		dprintk("%02X:", hw->CRTCEXT[i]);
	dprintk("\n");
#endif

	CRITBEGIN

	pci_write_config_dword(minfo->pcidev, PCI_OPTION_REG, hw->MXoptionReg);

	CRITEND

	matroxfb_vgaHWrestore(minfo);

	CRITBEGIN

	minfo->crtc1.panpos = -1;
	for (i = 0; i < 6; i++)
		mga_setr(M_EXTVGA_INDEX, i, hw->CRTCEXT[i]);

	for (i = 0; i < 21; i++) {
		outTi3026(minfo, DACseq[i], hw->DACreg[i]);
	}

	outTi3026(minfo, TVP3026_XPLLADDR, 0x00);
	progdac[0] = inTi3026(minfo, TVP3026_XPIXPLLDATA);
	progdac[3] = inTi3026(minfo, TVP3026_XLOOPPLLDATA);
	outTi3026(minfo, TVP3026_XPLLADDR, 0x15);
	progdac[1] = inTi3026(minfo, TVP3026_XPIXPLLDATA);
	progdac[4] = inTi3026(minfo, TVP3026_XLOOPPLLDATA);
	outTi3026(minfo, TVP3026_XPLLADDR, 0x2A);
	progdac[2] = inTi3026(minfo, TVP3026_XPIXPLLDATA);
	progdac[5] = inTi3026(minfo, TVP3026_XLOOPPLLDATA);

	CRITEND
	if (memcmp(hw->DACclk, progdac, 6)) {
		/* agrhh... setting up PLL is very slow on Millennium... */
		/* Mystique PLL is locked in few ms, but Millennium PLL lock takes about 0.15 s... */
		/* Maybe even we should call schedule() ? */

		CRITBEGIN
		outTi3026(minfo, TVP3026_XCLKCTRL, hw->DACreg[POS3026_XCLKCTRL]);
		outTi3026(minfo, TVP3026_XPLLADDR, 0x2A);
		outTi3026(minfo, TVP3026_XLOOPPLLDATA, 0);
		outTi3026(minfo, TVP3026_XPIXPLLDATA, 0);

		outTi3026(minfo, TVP3026_XPLLADDR, 0x00);
		for (i = 0; i < 3; i++)
			outTi3026(minfo, TVP3026_XPIXPLLDATA, hw->DACclk[i]);
		/* wait for PLL only if PLL clock requested (always for PowerMode, never for VGA) */
		if (hw->MiscOutReg & 0x08) {
			int tmout;
			outTi3026(minfo, TVP3026_XPLLADDR, 0x3F);
			for (tmout = 500000; tmout; --tmout) {
				if (inTi3026(minfo, TVP3026_XPIXPLLDATA) & 0x40)
					break;
				udelay(10);
			}

			CRITEND

			if (!tmout)
				printk(KERN_ERR "matroxfb: Pixel PLL not locked after 5 secs\n");
			else
				dprintk(KERN_INFO "PixelPLL: %d\n", 500000-tmout);
			CRITBEGIN
		}
		outTi3026(minfo, TVP3026_XMEMPLLCTRL, hw->DACreg[POS3026_XMEMPLLCTRL]);
		outTi3026(minfo, TVP3026_XPLLADDR, 0x00);
		for (i = 3; i < 6; i++)
			outTi3026(minfo, TVP3026_XLOOPPLLDATA, hw->DACclk[i]);
		CRITEND
		if ((hw->MiscOutReg & 0x08) && ((hw->DACclk[5] & 0x80) == 0x80)) {
			int tmout;

			CRITBEGIN
			outTi3026(minfo, TVP3026_XPLLADDR, 0x3F);
			for (tmout = 500000; tmout; --tmout) {
				if (inTi3026(minfo, TVP3026_XLOOPPLLDATA) & 0x40)
					break;
				udelay(10);
			}
			CRITEND
			if (!tmout)
				printk(KERN_ERR "matroxfb: Loop PLL not locked after 5 secs\n");
			else
				dprintk(KERN_INFO "LoopPLL: %d\n", 500000-tmout);
		}
	}

#ifdef DEBUG
	dprintk(KERN_DEBUG "3026DACregs ");
	for (i = 0; i < 21; i++) {
		dprintk("R%02X=%02X ", DACseq[i], hw->DACreg[i]);
		if ((i & 0x7) == 0x7) dprintk(KERN_DEBUG "continuing... ");
	}
	dprintk(KERN_DEBUG "DACclk ");
	for (i = 0; i < 6; i++)
		dprintk("C%02X=%02X ", i, hw->DACclk[i]);
	dprintk("\n");
#endif
}

static void Ti3026_reset(struct matrox_fb_info *minfo)
{
	DBG(__func__)

	ti3026_ramdac_init(minfo);
}

static struct matrox_altout ti3026_output = {
	.name	 = "Primary output",
};

static int Ti3026_preinit(struct matrox_fb_info *minfo)
{
	static const int vxres_mill2[] = { 512,        640, 768,  800,  832,  960,
					  1024, 1152, 1280,      1600, 1664, 1920,
					  2048, 0};
	static const int vxres_mill1[] = {             640, 768,  800,        960,
					  1024, 1152, 1280,      1600,       1920,
					  2048, 0};
	struct matrox_hw_state *hw = &minfo->hw;

	DBG(__func__)

	minfo->millenium = 1;
	minfo->milleniumII = (minfo->pcidev->device != PCI_DEVICE_ID_MATROX_MIL);
	minfo->capable.cfb4 = 1;
	minfo->capable.text = 1; /* isMilleniumII(minfo); */
	minfo->capable.vxres = isMilleniumII(minfo) ? vxres_mill2 : vxres_mill1;

	minfo->outputs[0].data = minfo;
	minfo->outputs[0].output = &ti3026_output;
	minfo->outputs[0].src = minfo->outputs[0].default_src;
	minfo->outputs[0].mode = MATROXFB_OUTPUT_MODE_MONITOR;

	if (minfo->devflags.noinit)
		return 0;
	/* preserve VGA I/O, BIOS and PPC */
	hw->MXoptionReg &= 0xC0000100;
	hw->MXoptionReg |= 0x002C0000;
	if (minfo->devflags.novga)
		hw->MXoptionReg &= ~0x00000100;
	if (minfo->devflags.nobios)
		hw->MXoptionReg &= ~0x40000000;
	if (minfo->devflags.nopciretry)
		hw->MXoptionReg |=  0x20000000;
	pci_write_config_dword(minfo->pcidev, PCI_OPTION_REG, hw->MXoptionReg);

	minfo->accel.ramdac_rev = inTi3026(minfo, TVP3026_XSILICONREV);

	outTi3026(minfo, TVP3026_XCLKCTRL, TVP3026_XCLKCTRL_SRC_CLK0VGA | TVP3026_XCLKCTRL_CLKSTOPPED);
	outTi3026(minfo, TVP3026_XTRUECOLORCTRL, TVP3026_XTRUECOLORCTRL_PSEUDOCOLOR);
	outTi3026(minfo, TVP3026_XMUXCTRL, TVP3026_XMUXCTRL_VGA);

	outTi3026(minfo, TVP3026_XPLLADDR, 0x2A);
	outTi3026(minfo, TVP3026_XLOOPPLLDATA, 0x00);
	outTi3026(minfo, TVP3026_XPIXPLLDATA, 0x00);

	mga_outb(M_MISC_REG, 0x67);

	outTi3026(minfo, TVP3026_XMEMPLLCTRL, TVP3026_XMEMPLLCTRL_STROBEMKC4 | TVP3026_XMEMPLLCTRL_MCLK_MCLKPLL);

	mga_outl(M_RESET, 1);
	udelay(250);
	mga_outl(M_RESET, 0);
	udelay(250);
	mga_outl(M_MACCESS, 0x00008000);
	udelay(10);
	return 0;
}

struct matrox_switch matrox_millennium = {
	.preinit	= Ti3026_preinit,
	.reset		= Ti3026_reset,
	.init		= Ti3026_init,
	.restore	= Ti3026_restore
};
EXPORT_SYMBOL(matrox_millennium);
#endif
MODULE_LICENSE("GPL");
