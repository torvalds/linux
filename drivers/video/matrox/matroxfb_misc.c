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
 *               "David C. Hansen" <haveblue@us.ibm.com>
 *                     Fixes
 *
 * (following author is not in any relation with this code, but his code
 *  is included in this driver)
 *
 * Based on framebuffer driver for VBE 2.0 compliant graphic boards
 *     (c) 1998 Gerd Knorr <kraxel@cs.tu-berlin.de>
 *
 * (following author is not in any relation with this code, but his ideas
 *  were used when writting this driver)
 *
 *		 FreeVBE/AF (Matrox), "Shawn Hargreaves" <shawn@talula.demon.co.uk>
 *
 */

/* make checkconfig does not check includes for this... */
#include <linux/config.h>

#include "matroxfb_misc.h"
#include <linux/interrupt.h>
#include <linux/matroxfb.h>

void matroxfb_DAC_out(CPMINFO int reg, int val) {
	DBG_REG(__FUNCTION__)
	mga_outb(M_RAMDAC_BASE+M_X_INDEX, reg);
	mga_outb(M_RAMDAC_BASE+M_X_DATAREG, val);
}

int matroxfb_DAC_in(CPMINFO int reg) {
	DBG_REG(__FUNCTION__)
	mga_outb(M_RAMDAC_BASE+M_X_INDEX, reg);
	return mga_inb(M_RAMDAC_BASE+M_X_DATAREG);
}

void matroxfb_var2my(struct fb_var_screeninfo* var, struct my_timming* mt) {
	unsigned int pixclock = var->pixclock;

	DBG(__FUNCTION__)

	if (!pixclock) pixclock = 10000;	/* 10ns = 100MHz */
	mt->pixclock = 1000000000 / pixclock;
	if (mt->pixclock < 1) mt->pixclock = 1;
	mt->mnp = -1;
	mt->dblscan = var->vmode & FB_VMODE_DOUBLE;
	mt->interlaced = var->vmode & FB_VMODE_INTERLACED;
	mt->HDisplay = var->xres;
	mt->HSyncStart = mt->HDisplay + var->right_margin;
	mt->HSyncEnd = mt->HSyncStart + var->hsync_len;
	mt->HTotal = mt->HSyncEnd + var->left_margin;
	mt->VDisplay = var->yres;
	mt->VSyncStart = mt->VDisplay + var->lower_margin;
	mt->VSyncEnd = mt->VSyncStart + var->vsync_len;
	mt->VTotal = mt->VSyncEnd + var->upper_margin;
	mt->sync = var->sync;
}

int matroxfb_PLL_calcclock(const struct matrox_pll_features* pll, unsigned int freq, unsigned int fmax,
		unsigned int* in, unsigned int* feed, unsigned int* post) {
	unsigned int bestdiff = ~0;
	unsigned int bestvco = 0;
	unsigned int fxtal = pll->ref_freq;
	unsigned int fwant;
	unsigned int p;

	DBG(__FUNCTION__)

	fwant = freq;

#ifdef DEBUG
	printk(KERN_ERR "post_shift_max: %d\n", pll->post_shift_max);
	printk(KERN_ERR "ref_freq: %d\n", pll->ref_freq);
	printk(KERN_ERR "freq: %d\n", freq);
	printk(KERN_ERR "vco_freq_min: %d\n", pll->vco_freq_min);
	printk(KERN_ERR "in_div_min: %d\n", pll->in_div_min);
	printk(KERN_ERR "in_div_max: %d\n", pll->in_div_max);
	printk(KERN_ERR "feed_div_min: %d\n", pll->feed_div_min);
	printk(KERN_ERR "feed_div_max: %d\n", pll->feed_div_max);
	printk(KERN_ERR "fmax: %d\n", fmax);
#endif
	for (p = 1; p <= pll->post_shift_max; p++) {
		if (fwant * 2 > fmax)
			break;
		fwant *= 2;
	}
	if (fwant < pll->vco_freq_min) fwant = pll->vco_freq_min;
	if (fwant > fmax) fwant = fmax;
	for (; p-- > 0; fwant >>= 1, bestdiff >>= 1) {
		unsigned int m;

		if (fwant < pll->vco_freq_min) break;
		for (m = pll->in_div_min; m <= pll->in_div_max; m++) {
			unsigned int diff, fvco;
			unsigned int n;

			n = (fwant * (m + 1) + (fxtal >> 1)) / fxtal - 1;
			if (n > pll->feed_div_max)
				break;
			if (n < pll->feed_div_min)
				n = pll->feed_div_min;
			fvco = (fxtal * (n + 1)) / (m + 1);
			if (fvco < fwant)
				diff = fwant - fvco;
			else
				diff = fvco - fwant;
			if (diff < bestdiff) {
				bestdiff = diff;
				*post = p;
				*in = m;
				*feed = n;
				bestvco = fvco;
			}
		}
	}
	dprintk(KERN_ERR "clk: %02X %02X %02X %d %d %d\n", *in, *feed, *post, fxtal, bestvco, fwant);
	return bestvco;
}

int matroxfb_vgaHWinit(WPMINFO struct my_timming* m) {
	unsigned int hd, hs, he, hbe, ht;
	unsigned int vd, vs, ve, vt, lc;
	unsigned int wd;
	unsigned int divider;
	int i;
	int fwidth;
	struct matrox_hw_state * const hw = &ACCESS_FBINFO(hw);

	fwidth = 8;

	DBG(__FUNCTION__)

	hw->SEQ[0] = 0x00;
	if (fwidth == 9)
		hw->SEQ[1] = 0x00;
	else
		hw->SEQ[1] = 0x01;	/* or 0x09 */
	hw->SEQ[2] = 0x0F;	/* bitplanes */
	hw->SEQ[3] = 0x00;
	hw->SEQ[4] = 0x0E;
	/* CRTC 0..7, 9, 16..19, 21, 22 are reprogrammed by Matrox Millennium code... Hope that by MGA1064 too */
	if (m->dblscan) {
		m->VTotal <<= 1;
		m->VDisplay <<= 1;
		m->VSyncStart <<= 1;
		m->VSyncEnd <<= 1;
	}
	if (m->interlaced) {
		m->VTotal >>= 1;
		m->VDisplay >>= 1;
		m->VSyncStart >>= 1;
		m->VSyncEnd >>= 1;
	}

	/* GCTL is ignored when not using 0xA0000 aperture */
	hw->GCTL[0] = 0x00;
	hw->GCTL[1] = 0x00;
	hw->GCTL[2] = 0x00;
	hw->GCTL[3] = 0x00;
	hw->GCTL[4] = 0x00;
	hw->GCTL[5] = 0x40;
	hw->GCTL[6] = 0x05;
	hw->GCTL[7] = 0x0F;
	hw->GCTL[8] = 0xFF;

	/* Whole ATTR is ignored in PowerGraphics mode */
	for (i = 0; i < 16; i++)
		hw->ATTR[i] = i;
	hw->ATTR[16] = 0x41;
	hw->ATTR[17] = 0xFF;
	hw->ATTR[18] = 0x0F;
	if (fwidth == 9)
		hw->ATTR[19] = 0x08;
	else
		hw->ATTR[19] = 0x00;
	hw->ATTR[20] = 0x00;

	hd = m->HDisplay >> 3;
	hs = m->HSyncStart >> 3;
	he = m->HSyncEnd >> 3;
	ht = m->HTotal >> 3;
	/* standard timmings are in 8pixels, but for interleaved we cannot */
	/* do it for 4bpp (because of (4bpp >> 1(interleaved))/4 == 0) */
	/* using 16 or more pixels per unit can save us */
	divider = ACCESS_FBINFO(curr.final_bppShift);
	while (divider & 3) {
		hd >>= 1;
		hs >>= 1;
		he >>= 1;
		ht >>= 1;
		divider <<= 1;
	}
	divider = divider / 4;
	/* divider can be from 1 to 8 */
	while (divider > 8) {
		hd <<= 1;
		hs <<= 1;
		he <<= 1;
		ht <<= 1;
		divider >>= 1;
	}
	hd = hd - 1;
	hs = hs - 1;
	he = he - 1;
	ht = ht - 1;
	vd = m->VDisplay - 1;
	vs = m->VSyncStart - 1;
	ve = m->VSyncEnd - 1;
	vt = m->VTotal - 2;
	lc = vd;
	/* G200 cannot work with (ht & 7) == 6 */
	if (((ht & 0x07) == 0x06) || ((ht & 0x0F) == 0x04))
		ht++;
	hbe = ht;
	wd = ACCESS_FBINFO(fbcon).var.xres_virtual * ACCESS_FBINFO(curr.final_bppShift) / 64;

	hw->CRTCEXT[0] = 0;
	hw->CRTCEXT[5] = 0;
	if (m->interlaced) {
		hw->CRTCEXT[0] = 0x80;
		hw->CRTCEXT[5] = (hs + he - ht) >> 1;
		if (!m->dblscan)
			wd <<= 1;
		vt &= ~1;
	}
	hw->CRTCEXT[0] |=  (wd & 0x300) >> 4;
	hw->CRTCEXT[1] = (((ht - 4) & 0x100) >> 8) |
			  ((hd      & 0x100) >> 7) | /* blanking */
			  ((hs      & 0x100) >> 6) | /* sync start */
			   (hbe     & 0x040);	 /* end hor. blanking */
	/* FIXME: Enable vidrst only on G400, and only if TV-out is used */
	if (ACCESS_FBINFO(outputs[1]).src == MATROXFB_SRC_CRTC1)
		hw->CRTCEXT[1] |= 0x88;		/* enable horizontal and vertical vidrst */
	hw->CRTCEXT[2] =  ((vt & 0xC00) >> 10) |
			  ((vd & 0x400) >>  8) |	/* disp end */
			  ((vd & 0xC00) >>  7) |	/* vblanking start */
			  ((vs & 0xC00) >>  5) |
			  ((lc & 0x400) >>  3);
	hw->CRTCEXT[3] = (divider - 1) | 0x80;
	hw->CRTCEXT[4] = 0;

	hw->CRTC[0] = ht-4;
	hw->CRTC[1] = hd;
	hw->CRTC[2] = hd;
	hw->CRTC[3] = (hbe & 0x1F) | 0x80;
	hw->CRTC[4] = hs;
	hw->CRTC[5] = ((hbe & 0x20) << 2) | (he & 0x1F);
	hw->CRTC[6] = vt & 0xFF;
	hw->CRTC[7] = ((vt & 0x100) >> 8) |
		      ((vd & 0x100) >> 7) |
		      ((vs & 0x100) >> 6) |
		      ((vd & 0x100) >> 5) |
		      ((lc & 0x100) >> 4) |
		      ((vt & 0x200) >> 4) |
		      ((vd & 0x200) >> 3) |
		      ((vs & 0x200) >> 2);
	hw->CRTC[8] = 0x00;
	hw->CRTC[9] = ((vd & 0x200) >> 4) |
		      ((lc & 0x200) >> 3);
	if (m->dblscan && !m->interlaced)
		hw->CRTC[9] |= 0x80;
	for (i = 10; i < 16; i++)
		hw->CRTC[i] = 0x00;
	hw->CRTC[16] = vs /* & 0xFF */;
	hw->CRTC[17] = (ve & 0x0F) | 0x20;
	hw->CRTC[18] = vd /* & 0xFF */;
	hw->CRTC[19] = wd /* & 0xFF */;
	hw->CRTC[20] = 0x00;
	hw->CRTC[21] = vd /* & 0xFF */;
	hw->CRTC[22] = (vt + 1) /* & 0xFF */;
	hw->CRTC[23] = 0xC3;
	hw->CRTC[24] = lc;
	return 0;
};

void matroxfb_vgaHWrestore(WPMINFO2) {
	int i;
	struct matrox_hw_state * const hw = &ACCESS_FBINFO(hw);
	CRITFLAGS

	DBG(__FUNCTION__)

	dprintk(KERN_INFO "MiscOutReg: %02X\n", hw->MiscOutReg);
	dprintk(KERN_INFO "SEQ regs:   ");
	for (i = 0; i < 5; i++)
		dprintk("%02X:", hw->SEQ[i]);
	dprintk("\n");
	dprintk(KERN_INFO "GDC regs:   ");
	for (i = 0; i < 9; i++)
		dprintk("%02X:", hw->GCTL[i]);
	dprintk("\n");
	dprintk(KERN_INFO "CRTC regs: ");
	for (i = 0; i < 25; i++)
		dprintk("%02X:", hw->CRTC[i]);
	dprintk("\n");
	dprintk(KERN_INFO "ATTR regs: ");
	for (i = 0; i < 21; i++)
		dprintk("%02X:", hw->ATTR[i]);
	dprintk("\n");

	CRITBEGIN

	mga_inb(M_ATTR_RESET);
	mga_outb(M_ATTR_INDEX, 0);
	mga_outb(M_MISC_REG, hw->MiscOutReg);
	for (i = 1; i < 5; i++)
		mga_setr(M_SEQ_INDEX, i, hw->SEQ[i]);
	mga_setr(M_CRTC_INDEX, 17, hw->CRTC[17] & 0x7F);
	for (i = 0; i < 25; i++)
		mga_setr(M_CRTC_INDEX, i, hw->CRTC[i]);
	for (i = 0; i < 9; i++)
		mga_setr(M_GRAPHICS_INDEX, i, hw->GCTL[i]);
	for (i = 0; i < 21; i++) {
		mga_inb(M_ATTR_RESET);
		mga_outb(M_ATTR_INDEX, i);
		mga_outb(M_ATTR_INDEX, hw->ATTR[i]);
	}
	mga_outb(M_PALETTE_MASK, 0xFF);
	mga_outb(M_DAC_REG, 0x00);
	for (i = 0; i < 768; i++)
		mga_outb(M_DAC_VAL, hw->DACpal[i]);
	mga_inb(M_ATTR_RESET);
	mga_outb(M_ATTR_INDEX, 0x20);

	CRITEND
}

static void get_pins(unsigned char __iomem* pins, struct matrox_bios* bd) {
	unsigned int b0 = readb(pins);
	
	if (b0 == 0x2E && readb(pins+1) == 0x41) {
		unsigned int pins_len = readb(pins+2);
		unsigned int i;
		unsigned char cksum;
		unsigned char* dst = bd->pins;

		if (pins_len < 3 || pins_len > 128) {
			return;
		}
		*dst++ = 0x2E;
		*dst++ = 0x41;
		*dst++ = pins_len;
		cksum = 0x2E + 0x41 + pins_len;
		for (i = 3; i < pins_len; i++) {
			cksum += *dst++ = readb(pins+i);
		}
		if (cksum) {
			return;
		}
		bd->pins_len = pins_len;
	} else if (b0 == 0x40 && readb(pins+1) == 0x00) {
		unsigned int i;
		unsigned char* dst = bd->pins;

		*dst++ = 0x40;
		*dst++ = 0;
		for (i = 2; i < 0x40; i++) {
			*dst++ = readb(pins+i);
		}
		bd->pins_len = 0x40;
	}
}

static void get_bios_version(unsigned char __iomem * vbios, struct matrox_bios* bd) {
	unsigned int pcir_offset;
	
	pcir_offset = readb(vbios + 24) | (readb(vbios + 25) << 8);
	if (pcir_offset >= 26 && pcir_offset < 0xFFE0 &&
	    readb(vbios + pcir_offset    ) == 'P' &&
	    readb(vbios + pcir_offset + 1) == 'C' &&
	    readb(vbios + pcir_offset + 2) == 'I' &&
	    readb(vbios + pcir_offset + 3) == 'R') {
		unsigned char h;

		h = readb(vbios + pcir_offset + 0x12);
		bd->version.vMaj = (h >> 4) & 0xF;
		bd->version.vMin = h & 0xF;
		bd->version.vRev = readb(vbios + pcir_offset + 0x13);
	} else {
		unsigned char h;

		h = readb(vbios + 5);
		bd->version.vMaj = (h >> 4) & 0xF;
		bd->version.vMin = h & 0xF;
		bd->version.vRev = 0;
	}
}

static void get_bios_output(unsigned char __iomem* vbios, struct matrox_bios* bd) {
	unsigned char b;
	
	b = readb(vbios + 0x7FF1);
	if (b == 0xFF) {
		b = 0;
	}
	bd->output.state = b;
}

static void get_bios_tvout(unsigned char __iomem* vbios, struct matrox_bios* bd) {
	unsigned int i;
	
	/* Check for 'IBM .*(V....TVO' string - it means TVO BIOS */
	bd->output.tvout = 0;
	if (readb(vbios + 0x1D) != 'I' ||
	    readb(vbios + 0x1E) != 'B' ||
	    readb(vbios + 0x1F) != 'M' ||
	    readb(vbios + 0x20) != ' ') {
	    	return;
	}
	for (i = 0x2D; i < 0x2D + 128; i++) {
		unsigned char b = readb(vbios + i);
		
		if (b == '(' && readb(vbios + i + 1) == 'V') {
			if (readb(vbios + i + 6) == 'T' &&
			    readb(vbios + i + 7) == 'V' &&
			    readb(vbios + i + 8) == 'O') {
				bd->output.tvout = 1;
			}
			return;
		}
		if (b == 0)
			break;
	}
}

static void parse_bios(unsigned char __iomem* vbios, struct matrox_bios* bd) {
	unsigned int pins_offset;
	
	if (readb(vbios) != 0x55 || readb(vbios + 1) != 0xAA) {
		return;
	}
	bd->bios_valid = 1;
	get_bios_version(vbios, bd);
	get_bios_output(vbios, bd);
	get_bios_tvout(vbios, bd);
	pins_offset = readb(vbios + 0x7FFC) | (readb(vbios + 0x7FFD) << 8);
	if (pins_offset <= 0xFF80) {
		get_pins(vbios + pins_offset, bd);
	}
}

#define get_u16(x) (le16_to_cpu(get_unaligned((__u16*)(x))))
#define get_u32(x) (le32_to_cpu(get_unaligned((__u32*)(x))))
static int parse_pins1(WPMINFO const struct matrox_bios* bd) {
	unsigned int maxdac;

	switch (bd->pins[22]) {
		case 0:		maxdac = 175000; break;
		case 1:		maxdac = 220000; break;
		default:	maxdac = 240000; break;
	}
	if (get_u16(bd->pins + 24)) {
		maxdac = get_u16(bd->pins + 24) * 10;
	}
	MINFO->limits.pixel.vcomax = maxdac;
	MINFO->values.pll.system = get_u16(bd->pins + 28) ? get_u16(bd->pins + 28) * 10 : 50000;
	/* ignore 4MB, 8MB, module clocks */
	MINFO->features.pll.ref_freq = 14318;
	MINFO->values.reg.mctlwtst	= 0x00030101;
	return 0;
}

static void default_pins1(WPMINFO2) {
	/* Millennium */
	MINFO->limits.pixel.vcomax	= 220000;
	MINFO->values.pll.system	=  50000;
	MINFO->features.pll.ref_freq	=  14318;
	MINFO->values.reg.mctlwtst	= 0x00030101;
}

static int parse_pins2(WPMINFO const struct matrox_bios* bd) {
	MINFO->limits.pixel.vcomax	=
	MINFO->limits.system.vcomax	= (bd->pins[41] == 0xFF) ? 230000 : ((bd->pins[41] + 100) * 1000);
	MINFO->values.reg.mctlwtst	= ((bd->pins[51] & 0x01) ? 0x00000001 : 0) |
					  ((bd->pins[51] & 0x02) ? 0x00000100 : 0) |
					  ((bd->pins[51] & 0x04) ? 0x00010000 : 0) |
					  ((bd->pins[51] & 0x08) ? 0x00020000 : 0);
	MINFO->values.pll.system	= (bd->pins[43] == 0xFF) ? 50000 : ((bd->pins[43] + 100) * 1000);
	MINFO->features.pll.ref_freq	= 14318;
	return 0;
}

static void default_pins2(WPMINFO2) {
	/* Millennium II, Mystique */
	MINFO->limits.pixel.vcomax	=
	MINFO->limits.system.vcomax	= 230000;
	MINFO->values.reg.mctlwtst	= 0x00030101;
	MINFO->values.pll.system	=  50000;
	MINFO->features.pll.ref_freq	=  14318;
}

static int parse_pins3(WPMINFO const struct matrox_bios* bd) {
	MINFO->limits.pixel.vcomax	=
	MINFO->limits.system.vcomax	= (bd->pins[36] == 0xFF) ? 230000			: ((bd->pins[36] + 100) * 1000);
	MINFO->values.reg.mctlwtst	= get_u32(bd->pins + 48) == 0xFFFFFFFF ? 0x01250A21	: get_u32(bd->pins + 48);
	/* memory config */
	MINFO->values.reg.memrdbk	= ((bd->pins[57] << 21) & 0x1E000000) |
					  ((bd->pins[57] << 22) & 0x00C00000) |
					  ((bd->pins[56] <<  1) & 0x000001E0) |
					  ( bd->pins[56]        & 0x0000000F);
	MINFO->values.reg.opt		= (bd->pins[54] & 7) << 10;
	MINFO->values.reg.opt2		= bd->pins[58] << 12;
	MINFO->features.pll.ref_freq	= (bd->pins[52] & 0x20) ? 14318 : 27000;
	return 0;
}

static void default_pins3(WPMINFO2) {
	/* G100, G200 */
	MINFO->limits.pixel.vcomax	=
	MINFO->limits.system.vcomax	= 230000;
	MINFO->values.reg.mctlwtst	= 0x01250A21;
	MINFO->values.reg.memrdbk	= 0x00000000;
	MINFO->values.reg.opt		= 0x00000C00;
	MINFO->values.reg.opt2		= 0x00000000;
	MINFO->features.pll.ref_freq	=  27000;
}

static int parse_pins4(WPMINFO const struct matrox_bios* bd) {
	MINFO->limits.pixel.vcomax	= (bd->pins[ 39] == 0xFF) ? 230000			: bd->pins[ 39] * 4000;
	MINFO->limits.system.vcomax	= (bd->pins[ 38] == 0xFF) ? MINFO->limits.pixel.vcomax	: bd->pins[ 38] * 4000;
	MINFO->values.reg.mctlwtst	= get_u32(bd->pins + 71);
	MINFO->values.reg.memrdbk	= ((bd->pins[87] << 21) & 0x1E000000) |
					  ((bd->pins[87] << 22) & 0x00C00000) |
					  ((bd->pins[86] <<  1) & 0x000001E0) |
					  ( bd->pins[86]        & 0x0000000F);
	MINFO->values.reg.opt		= ((bd->pins[53] << 15) & 0x00400000) |
					  ((bd->pins[53] << 22) & 0x10000000) |
					  ((bd->pins[53] <<  7) & 0x00001C00);
	MINFO->values.reg.opt3		= get_u32(bd->pins + 67);
	MINFO->values.pll.system	= (bd->pins[ 65] == 0xFF) ? 200000 			: bd->pins[ 65] * 4000;
	MINFO->features.pll.ref_freq	= (bd->pins[ 92] & 0x01) ? 14318 : 27000;
	return 0;
}

static void default_pins4(WPMINFO2) {
	/* G400 */
	MINFO->limits.pixel.vcomax	=
	MINFO->limits.system.vcomax	= 252000;
	MINFO->values.reg.mctlwtst	= 0x04A450A1;
	MINFO->values.reg.memrdbk	= 0x000000E7;
	MINFO->values.reg.opt		= 0x10000400;
	MINFO->values.reg.opt3		= 0x0190A419;
	MINFO->values.pll.system	= 200000;
	MINFO->features.pll.ref_freq	= 27000;
}

static int parse_pins5(WPMINFO const struct matrox_bios* bd) {
	unsigned int mult;
	
	mult = bd->pins[4]?8000:6000;
	
	MINFO->limits.pixel.vcomax	= (bd->pins[ 38] == 0xFF) ? 600000			: bd->pins[ 38] * mult;
	MINFO->limits.system.vcomax	= (bd->pins[ 36] == 0xFF) ? MINFO->limits.pixel.vcomax	: bd->pins[ 36] * mult;
	MINFO->limits.video.vcomax	= (bd->pins[ 37] == 0xFF) ? MINFO->limits.system.vcomax	: bd->pins[ 37] * mult;
	MINFO->limits.pixel.vcomin	= (bd->pins[123] == 0xFF) ? 256000			: bd->pins[123] * mult;
	MINFO->limits.system.vcomin	= (bd->pins[121] == 0xFF) ? MINFO->limits.pixel.vcomin	: bd->pins[121] * mult;
	MINFO->limits.video.vcomin	= (bd->pins[122] == 0xFF) ? MINFO->limits.system.vcomin	: bd->pins[122] * mult;
	MINFO->values.pll.system	=
	MINFO->values.pll.video		= (bd->pins[ 92] == 0xFF) ? 284000			: bd->pins[ 92] * 4000;
	MINFO->values.reg.opt		= get_u32(bd->pins+ 48);
	MINFO->values.reg.opt2		= get_u32(bd->pins+ 52);
	MINFO->values.reg.opt3		= get_u32(bd->pins+ 94);
	MINFO->values.reg.mctlwtst	= get_u32(bd->pins+ 98);
	MINFO->values.reg.memmisc	= get_u32(bd->pins+102);
	MINFO->values.reg.memrdbk	= get_u32(bd->pins+106);
	MINFO->features.pll.ref_freq	= (bd->pins[110] & 0x01) ? 14318 : 27000;
	MINFO->values.memory.ddr	= (bd->pins[114] & 0x60) == 0x20;
	MINFO->values.memory.dll	= (bd->pins[115] & 0x02) != 0;
	MINFO->values.memory.emrswen	= (bd->pins[115] & 0x01) != 0;
	MINFO->values.reg.maccess	= MINFO->values.memory.emrswen ? 0x00004000 : 0x00000000;
	if (bd->pins[115] & 4) {
		MINFO->values.reg.mctlwtst_core = MINFO->values.reg.mctlwtst;
	} else {
		u_int32_t wtst_xlat[] = { 0, 1, 5, 6, 7, 5, 2, 3 };
		MINFO->values.reg.mctlwtst_core = (MINFO->values.reg.mctlwtst & ~7) |
		                                  wtst_xlat[MINFO->values.reg.mctlwtst & 7];
	}
	return 0;
}

static void default_pins5(WPMINFO2) {
	/* Mine 16MB G450 with SDRAM DDR */
	MINFO->limits.pixel.vcomax	=
	MINFO->limits.system.vcomax	=
	MINFO->limits.video.vcomax	= 600000;
	MINFO->limits.pixel.vcomin	=
	MINFO->limits.system.vcomin	=
	MINFO->limits.video.vcomin	= 256000;
	MINFO->values.pll.system	=
	MINFO->values.pll.video		= 284000;
	MINFO->values.reg.opt		= 0x404A1160;
	MINFO->values.reg.opt2		= 0x0000AC00;
	MINFO->values.reg.opt3		= 0x0090A409;
	MINFO->values.reg.mctlwtst_core	=
	MINFO->values.reg.mctlwtst	= 0x0C81462B;
	MINFO->values.reg.memmisc	= 0x80000004;
	MINFO->values.reg.memrdbk	= 0x01001103;
	MINFO->features.pll.ref_freq	= 27000;
	MINFO->values.memory.ddr	= 1;
	MINFO->values.memory.dll	= 1;
	MINFO->values.memory.emrswen	= 1;
	MINFO->values.reg.maccess	= 0x00004000;
}

static int matroxfb_set_limits(WPMINFO const struct matrox_bios* bd) {
	unsigned int pins_version;
	static const unsigned int pinslen[] = { 64, 64, 64, 128, 128 };

	switch (ACCESS_FBINFO(chip)) {
		case MGA_2064:	default_pins1(PMINFO2); break;
		case MGA_2164:
		case MGA_1064:
		case MGA_1164:	default_pins2(PMINFO2); break;
		case MGA_G100:
		case MGA_G200:	default_pins3(PMINFO2); break;
		case MGA_G400:	default_pins4(PMINFO2); break;
		case MGA_G450:
		case MGA_G550:	default_pins5(PMINFO2); break;
	}
	if (!bd->bios_valid) {
		printk(KERN_INFO "matroxfb: Your Matrox device does not have BIOS\n");
		return -1;
	}
	if (bd->pins_len < 64) {
		printk(KERN_INFO "matroxfb: BIOS on your Matrox device does not contain powerup info\n");
		return -1;
	}
	if (bd->pins[0] == 0x2E && bd->pins[1] == 0x41) {
		pins_version = bd->pins[5];
		if (pins_version < 2 || pins_version > 5) {
			printk(KERN_INFO "matroxfb: Unknown version (%u) of powerup info\n", pins_version);
			return -1;
		}
	} else {
		pins_version = 1;
	}
	if (bd->pins_len != pinslen[pins_version - 1]) {
		printk(KERN_INFO "matroxfb: Invalid powerup info\n");
		return -1;
	}
	switch (pins_version) {
		case 1:
			return parse_pins1(PMINFO bd);
		case 2:
			return parse_pins2(PMINFO bd);
		case 3:
			return parse_pins3(PMINFO bd);
		case 4:
			return parse_pins4(PMINFO bd);
		case 5:
			return parse_pins5(PMINFO bd);
		default:
			printk(KERN_DEBUG "matroxfb: Powerup info version %u is not yet supported\n", pins_version);
			return -1;
	}
}

void matroxfb_read_pins(WPMINFO2) {
	u32 opt;
	u32 biosbase;
	u32 fbbase;
	struct pci_dev* pdev = ACCESS_FBINFO(pcidev);
	
	memset(&ACCESS_FBINFO(bios), 0, sizeof(ACCESS_FBINFO(bios)));
	pci_read_config_dword(pdev, PCI_OPTION_REG, &opt);
	pci_write_config_dword(pdev, PCI_OPTION_REG, opt | PCI_OPTION_ENABLE_ROM);
	pci_read_config_dword(pdev, PCI_ROM_ADDRESS, &biosbase);
	pci_read_config_dword(pdev, ACCESS_FBINFO(devflags.fbResource), &fbbase);
	pci_write_config_dword(pdev, PCI_ROM_ADDRESS, (fbbase & PCI_ROM_ADDRESS_MASK) | PCI_ROM_ADDRESS_ENABLE);
	parse_bios(vaddr_va(ACCESS_FBINFO(video).vbase), &ACCESS_FBINFO(bios));
	pci_write_config_dword(pdev, PCI_ROM_ADDRESS, biosbase);
	pci_write_config_dword(pdev, PCI_OPTION_REG, opt);
#ifdef CONFIG_X86
	if (!ACCESS_FBINFO(bios).bios_valid) {
		unsigned char __iomem* b;

		b = ioremap(0x000C0000, 65536);
		if (!b) {
			printk(KERN_INFO "matroxfb: Unable to map legacy BIOS\n");
		} else {
			unsigned int ven = readb(b+0x64+0) | (readb(b+0x64+1) << 8);
			unsigned int dev = readb(b+0x64+2) | (readb(b+0x64+3) << 8);
			
			if (ven != pdev->vendor || dev != pdev->device) {
				printk(KERN_INFO "matroxfb: Legacy BIOS is for %04X:%04X, while this device is %04X:%04X\n",
					ven, dev, pdev->vendor, pdev->device);
			} else {
				parse_bios(b, &ACCESS_FBINFO(bios));
			}
			iounmap(b);
		}
	}
#endif
	matroxfb_set_limits(PMINFO &ACCESS_FBINFO(bios));
}

EXPORT_SYMBOL(matroxfb_DAC_in);
EXPORT_SYMBOL(matroxfb_DAC_out);
EXPORT_SYMBOL(matroxfb_var2my);
EXPORT_SYMBOL(matroxfb_PLL_calcclock);
#ifndef CONFIG_FB_MATROX_MULTIHEAD
struct matrox_fb_info matroxfb_global_mxinfo;
EXPORT_SYMBOL(matroxfb_global_mxinfo);
#endif
EXPORT_SYMBOL(matroxfb_vgaHWinit);		/* DAC1064, Ti3026 */
EXPORT_SYMBOL(matroxfb_vgaHWrestore);		/* DAC1064, Ti3026 */
EXPORT_SYMBOL(matroxfb_read_pins);

MODULE_AUTHOR("(c) 1999-2002 Petr Vandrovec <vandrove@vc.cvut.cz>");
MODULE_DESCRIPTION("Miscellaneous support for Matrox video cards");
MODULE_LICENSE("GPL");
