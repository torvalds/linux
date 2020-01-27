// SPDX-License-Identifier: GPL-2.0-only
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
 *               "Samuel Hocevar" <sam@via.ecp.fr>
 *                     Fixes
 *
 *               "Anton Altaparmakov" <AntonA@bigfoot.com>
 *                     G400 MAX/non-MAX distinction
 *
 *               "Ken Aaker" <kdaaker@rchland.vnet.ibm.com>
 *                     memtype extension (needed for GXT130P RS/6000 adapter)
 *
 *               "Uns Lider" <unslider@miranda.org>
 *                     G100 PLNWT fixes
 *
 *               "Denis Zaitsev" <zzz@cd-club.ru>
 *                     Fixes
 *
 *               "Mike Pieper" <mike@pieper-family.de>
 *                     TVOut enhandcements, V4L2 control interface.
 *
 *               "Diego Biurrun" <diego@biurrun.de>
 *                     DFP testing
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

#include <linux/version.h>

#include "matroxfb_base.h"
#include "matroxfb_misc.h"
#include "matroxfb_accel.h"
#include "matroxfb_DAC1064.h"
#include "matroxfb_Ti3026.h"
#include "matroxfb_maven.h"
#include "matroxfb_crtc2.h"
#include "matroxfb_g450.h"
#include <linux/matroxfb.h>
#include <linux/interrupt.h>
#include <linux/nvram.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#ifdef CONFIG_PPC_PMAC
#include <asm/machdep.h>
static int default_vmode = VMODE_NVRAM;
static int default_cmode = CMODE_NVRAM;
#endif

static void matroxfb_unregister_device(struct matrox_fb_info* minfo);

/* --------------------------------------------------------------------- */

/*
 * card parameters
 */

/* --------------------------------------------------------------------- */

static struct fb_var_screeninfo vesafb_defined = {
	640,480,640,480,/* W,H, W, H (virtual) load xres,xres_virtual*/
	0,0,		/* virtual -> visible no offset */
	8,		/* depth -> load bits_per_pixel */
	0,		/* greyscale ? */
	{0,0,0},	/* R */
	{0,0,0},	/* G */
	{0,0,0},	/* B */
	{0,0,0},	/* transparency */
	0,		/* standard pixel format */
	FB_ACTIVATE_NOW,
	-1,-1,
	FB_ACCELF_TEXT,	/* accel flags */
	39721L,48L,16L,33L,10L,
	96L,2L,~0,	/* No sync info */
	FB_VMODE_NONINTERLACED,
};



/* --------------------------------------------------------------------- */
static void update_crtc2(struct matrox_fb_info *minfo, unsigned int pos)
{
	struct matroxfb_dh_fb_info *info = minfo->crtc2.info;

	/* Make sure that displays are compatible */
	if (info && (info->fbcon.var.bits_per_pixel == minfo->fbcon.var.bits_per_pixel)
		 && (info->fbcon.var.xres_virtual == minfo->fbcon.var.xres_virtual)
		 && (info->fbcon.var.green.length == minfo->fbcon.var.green.length)
		 ) {
		switch (minfo->fbcon.var.bits_per_pixel) {
			case 16:
			case 32:
				pos = pos * 8;
				if (info->interlaced) {
					mga_outl(0x3C2C, pos);
					mga_outl(0x3C28, pos + minfo->fbcon.var.xres_virtual * minfo->fbcon.var.bits_per_pixel / 8);
				} else {
					mga_outl(0x3C28, pos);
				}
				break;
		}
	}
}

static void matroxfb_crtc1_panpos(struct matrox_fb_info *minfo)
{
	if (minfo->crtc1.panpos >= 0) {
		unsigned long flags;
		int panpos;

		matroxfb_DAC_lock_irqsave(flags);
		panpos = minfo->crtc1.panpos;
		if (panpos >= 0) {
			unsigned int extvga_reg;

			minfo->crtc1.panpos = -1; /* No update pending anymore */
			extvga_reg = mga_inb(M_EXTVGA_INDEX);
			mga_setr(M_EXTVGA_INDEX, 0x00, panpos);
			if (extvga_reg != 0x00) {
				mga_outb(M_EXTVGA_INDEX, extvga_reg);
			}
		}
		matroxfb_DAC_unlock_irqrestore(flags);
	}
}

static irqreturn_t matrox_irq(int irq, void *dev_id)
{
	u_int32_t status;
	int handled = 0;
	struct matrox_fb_info *minfo = dev_id;

	status = mga_inl(M_STATUS);

	if (status & 0x20) {
		mga_outl(M_ICLEAR, 0x20);
		minfo->crtc1.vsync.cnt++;
		matroxfb_crtc1_panpos(minfo);
		wake_up_interruptible(&minfo->crtc1.vsync.wait);
		handled = 1;
	}
	if (status & 0x200) {
		mga_outl(M_ICLEAR, 0x200);
		minfo->crtc2.vsync.cnt++;
		wake_up_interruptible(&minfo->crtc2.vsync.wait);
		handled = 1;
	}
	return IRQ_RETVAL(handled);
}

int matroxfb_enable_irq(struct matrox_fb_info *minfo, int reenable)
{
	u_int32_t bm;

	if (minfo->devflags.accelerator == FB_ACCEL_MATROX_MGAG400)
		bm = 0x220;
	else
		bm = 0x020;

	if (!test_and_set_bit(0, &minfo->irq_flags)) {
		if (request_irq(minfo->pcidev->irq, matrox_irq,
				IRQF_SHARED, "matroxfb", minfo)) {
			clear_bit(0, &minfo->irq_flags);
			return -EINVAL;
		}
		/* Clear any pending field interrupts */
		mga_outl(M_ICLEAR, bm);
		mga_outl(M_IEN, mga_inl(M_IEN) | bm);
	} else if (reenable) {
		u_int32_t ien;

		ien = mga_inl(M_IEN);
		if ((ien & bm) != bm) {
			printk(KERN_DEBUG "matroxfb: someone disabled IRQ [%08X]\n", ien);
			mga_outl(M_IEN, ien | bm);
		}
	}
	return 0;
}

static void matroxfb_disable_irq(struct matrox_fb_info *minfo)
{
	if (test_and_clear_bit(0, &minfo->irq_flags)) {
		/* Flush pending pan-at-vbl request... */
		matroxfb_crtc1_panpos(minfo);
		if (minfo->devflags.accelerator == FB_ACCEL_MATROX_MGAG400)
			mga_outl(M_IEN, mga_inl(M_IEN) & ~0x220);
		else
			mga_outl(M_IEN, mga_inl(M_IEN) & ~0x20);
		free_irq(minfo->pcidev->irq, minfo);
	}
}

int matroxfb_wait_for_sync(struct matrox_fb_info *minfo, u_int32_t crtc)
{
	struct matrox_vsync *vs;
	unsigned int cnt;
	int ret;

	switch (crtc) {
		case 0:
			vs = &minfo->crtc1.vsync;
			break;
		case 1:
			if (minfo->devflags.accelerator != FB_ACCEL_MATROX_MGAG400) {
				return -ENODEV;
			}
			vs = &minfo->crtc2.vsync;
			break;
		default:
			return -ENODEV;
	}
	ret = matroxfb_enable_irq(minfo, 0);
	if (ret) {
		return ret;
	}

	cnt = vs->cnt;
	ret = wait_event_interruptible_timeout(vs->wait, cnt != vs->cnt, HZ/10);
	if (ret < 0) {
		return ret;
	}
	if (ret == 0) {
		matroxfb_enable_irq(minfo, 1);
		return -ETIMEDOUT;
	}
	return 0;
}

/* --------------------------------------------------------------------- */

static void matrox_pan_var(struct matrox_fb_info *minfo,
			   struct fb_var_screeninfo *var)
{
	unsigned int pos;
	unsigned short p0, p1, p2;
	unsigned int p3;
	int vbl;
	unsigned long flags;

	CRITFLAGS

	DBG(__func__)

	if (minfo->dead)
		return;

	minfo->fbcon.var.xoffset = var->xoffset;
	minfo->fbcon.var.yoffset = var->yoffset;
	pos = (minfo->fbcon.var.yoffset * minfo->fbcon.var.xres_virtual + minfo->fbcon.var.xoffset) * minfo->curr.final_bppShift / 32;
	pos += minfo->curr.ydstorg.chunks;
	p0 = minfo->hw.CRTC[0x0D] = pos & 0xFF;
	p1 = minfo->hw.CRTC[0x0C] = (pos & 0xFF00) >> 8;
	p2 = minfo->hw.CRTCEXT[0] = (minfo->hw.CRTCEXT[0] & 0xB0) | ((pos >> 16) & 0x0F) | ((pos >> 14) & 0x40);
	p3 = minfo->hw.CRTCEXT[8] = pos >> 21;

	/* FB_ACTIVATE_VBL and we can acquire interrupts? Honor FB_ACTIVATE_VBL then... */
	vbl = (var->activate & FB_ACTIVATE_VBL) && (matroxfb_enable_irq(minfo, 0) == 0);

	CRITBEGIN

	matroxfb_DAC_lock_irqsave(flags);
	mga_setr(M_CRTC_INDEX, 0x0D, p0);
	mga_setr(M_CRTC_INDEX, 0x0C, p1);
	if (minfo->devflags.support32MB)
		mga_setr(M_EXTVGA_INDEX, 0x08, p3);
	if (vbl) {
		minfo->crtc1.panpos = p2;
	} else {
		/* Abort any pending change */
		minfo->crtc1.panpos = -1;
		mga_setr(M_EXTVGA_INDEX, 0x00, p2);
	}
	matroxfb_DAC_unlock_irqrestore(flags);

	update_crtc2(minfo, pos);

	CRITEND
}

static void matroxfb_remove(struct matrox_fb_info *minfo, int dummy)
{
	/* Currently we are holding big kernel lock on all dead & usecount updates.
	 * Destroy everything after all users release it. Especially do not unregister
	 * framebuffer and iounmap memory, neither fbmem nor fbcon-cfb* does not check
	 * for device unplugged when in use.
	 * In future we should point mmio.vbase & video.vbase somewhere where we can
	 * write data without causing too much damage...
	 */

	minfo->dead = 1;
	if (minfo->usecount) {
		/* destroy it later */
		return;
	}
	matroxfb_unregister_device(minfo);
	unregister_framebuffer(&minfo->fbcon);
	matroxfb_g450_shutdown(minfo);
	arch_phys_wc_del(minfo->wc_cookie);
	iounmap(minfo->mmio.vbase.vaddr);
	iounmap(minfo->video.vbase.vaddr);
	release_mem_region(minfo->video.base, minfo->video.len_maximum);
	release_mem_region(minfo->mmio.base, 16384);
	kfree(minfo);
}

	/*
	 * Open/Release the frame buffer device
	 */

static int matroxfb_open(struct fb_info *info, int user)
{
	struct matrox_fb_info *minfo = info2minfo(info);

	DBG_LOOP(__func__)

	if (minfo->dead) {
		return -ENXIO;
	}
	minfo->usecount++;
	if (user) {
		minfo->userusecount++;
	}
	return(0);
}

static int matroxfb_release(struct fb_info *info, int user)
{
	struct matrox_fb_info *minfo = info2minfo(info);

	DBG_LOOP(__func__)

	if (user) {
		if (0 == --minfo->userusecount) {
			matroxfb_disable_irq(minfo);
		}
	}
	if (!(--minfo->usecount) && minfo->dead) {
		matroxfb_remove(minfo, 0);
	}
	return(0);
}

static int matroxfb_pan_display(struct fb_var_screeninfo *var,
		struct fb_info* info) {
	struct matrox_fb_info *minfo = info2minfo(info);

	DBG(__func__)

	matrox_pan_var(minfo, var);
	return 0;
}

static int matroxfb_get_final_bppShift(const struct matrox_fb_info *minfo,
				       int bpp)
{
	int bppshft2;

	DBG(__func__)

	bppshft2 = bpp;
	if (!bppshft2) {
		return 8;
	}
	if (isInterleave(minfo))
		bppshft2 >>= 1;
	if (minfo->devflags.video64bits)
		bppshft2 >>= 1;
	return bppshft2;
}

static int matroxfb_test_and_set_rounding(const struct matrox_fb_info *minfo,
					  int xres, int bpp)
{
	int over;
	int rounding;

	DBG(__func__)

	switch (bpp) {
		case 0:		return xres;
		case 4:		rounding = 128;
				break;
		case 8:		rounding = 64;	/* doc says 64; 32 is OK for G400 */
				break;
		case 16:	rounding = 32;
				break;
		case 24:	rounding = 64;	/* doc says 64; 32 is OK for G400 */
				break;
		default:	rounding = 16;
				/* on G400, 16 really does not work */
				if (minfo->devflags.accelerator == FB_ACCEL_MATROX_MGAG400)
					rounding = 32;
				break;
	}
	if (isInterleave(minfo)) {
		rounding *= 2;
	}
	over = xres % rounding;
	if (over)
		xres += rounding-over;
	return xres;
}

static int matroxfb_pitch_adjust(const struct matrox_fb_info *minfo, int xres,
				 int bpp)
{
	const int* width;
	int xres_new;

	DBG(__func__)

	if (!bpp) return xres;

	width = minfo->capable.vxres;

	if (minfo->devflags.precise_width) {
		while (*width) {
			if ((*width >= xres) && (matroxfb_test_and_set_rounding(minfo, *width, bpp) == *width)) {
				break;
			}
			width++;
		}
		xres_new = *width;
	} else {
		xres_new = matroxfb_test_and_set_rounding(minfo, xres, bpp);
	}
	return xres_new;
}

static int matroxfb_get_cmap_len(struct fb_var_screeninfo *var) {

	DBG(__func__)

	switch (var->bits_per_pixel) {
		case 4:
			return 16;	/* pseudocolor... 16 entries HW palette */
		case 8:
			return 256;	/* pseudocolor... 256 entries HW palette */
		case 16:
			return 16;	/* directcolor... 16 entries SW palette */
					/* Mystique: truecolor, 16 entries SW palette, HW palette hardwired into 1:1 mapping */
		case 24:
			return 16;	/* directcolor... 16 entries SW palette */
					/* Mystique: truecolor, 16 entries SW palette, HW palette hardwired into 1:1 mapping */
		case 32:
			return 16;	/* directcolor... 16 entries SW palette */
					/* Mystique: truecolor, 16 entries SW palette, HW palette hardwired into 1:1 mapping */
	}
	return 16;	/* return something reasonable... or panic()? */
}

static int matroxfb_decode_var(const struct matrox_fb_info *minfo,
			       struct fb_var_screeninfo *var, int *visual,
			       int *video_cmap_len, unsigned int* ydstorg)
{
	struct RGBT {
		unsigned char bpp;
		struct {
			unsigned char offset,
				      length;
		} red,
		  green,
		  blue,
		  transp;
		signed char visual;
	};
	static const struct RGBT table[]= {
		{ 8,{ 0,8},{0,8},{0,8},{ 0,0},MX_VISUAL_PSEUDOCOLOR},
		{15,{10,5},{5,5},{0,5},{15,1},MX_VISUAL_DIRECTCOLOR},
		{16,{11,5},{5,6},{0,5},{ 0,0},MX_VISUAL_DIRECTCOLOR},
		{24,{16,8},{8,8},{0,8},{ 0,0},MX_VISUAL_DIRECTCOLOR},
		{32,{16,8},{8,8},{0,8},{24,8},MX_VISUAL_DIRECTCOLOR}
	};
	struct RGBT const *rgbt;
	unsigned int bpp = var->bits_per_pixel;
	unsigned int vramlen;
	unsigned int memlen;

	DBG(__func__)

	switch (bpp) {
		case 4:	 if (!minfo->capable.cfb4) return -EINVAL;
			 break;
		case 8:	 break;
		case 16: break;
		case 24: break;
		case 32: break;
		default: return -EINVAL;
	}
	*ydstorg = 0;
	vramlen = minfo->video.len_usable;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;
	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;

	var->xres_virtual = matroxfb_pitch_adjust(minfo, var->xres_virtual, bpp);
	memlen = var->xres_virtual * bpp * var->yres_virtual / 8;
	if (memlen > vramlen) {
		var->yres_virtual = vramlen * 8 / (var->xres_virtual * bpp);
		memlen = var->xres_virtual * bpp * var->yres_virtual / 8;
	}
	/* There is hardware bug that no line can cross 4MB boundary */
	/* give up for CFB24, it is impossible to easy workaround it */
	/* for other try to do something */
	if (!minfo->capable.cross4MB && (memlen > 0x400000)) {
		if (bpp == 24) {
			/* sorry */
		} else {
			unsigned int linelen;
			unsigned int m1 = linelen = var->xres_virtual * bpp / 8;
			unsigned int m2 = PAGE_SIZE;	/* or 128 if you do not need PAGE ALIGNED address */
			unsigned int max_yres;

			while (m1) {
				while (m2 >= m1) m2 -= m1;
				swap(m1, m2);
			}
			m2 = linelen * PAGE_SIZE / m2;
			*ydstorg = m2 = 0x400000 % m2;
			max_yres = (vramlen - m2) / linelen;
			if (var->yres_virtual > max_yres)
				var->yres_virtual = max_yres;
		}
	}
	/* YDSTLEN contains only signed 16bit value */
	if (var->yres_virtual > 32767)
		var->yres_virtual = 32767;
	/* we must round yres/xres down, we already rounded y/xres_virtual up
	   if it was possible. We should return -EINVAL, but I disagree */
	if (var->yres_virtual < var->yres)
		var->yres = var->yres_virtual;
	if (var->xres_virtual < var->xres)
		var->xres = var->xres_virtual;
	if (var->xoffset + var->xres > var->xres_virtual)
		var->xoffset = var->xres_virtual - var->xres;
	if (var->yoffset + var->yres > var->yres_virtual)
		var->yoffset = var->yres_virtual - var->yres;

	if (bpp == 16 && var->green.length == 5) {
		bpp--; /* an artificial value - 15 */
	}

	for (rgbt = table; rgbt->bpp < bpp; rgbt++);
#define	SETCLR(clr)\
	var->clr.offset = rgbt->clr.offset;\
	var->clr.length = rgbt->clr.length
	SETCLR(red);
	SETCLR(green);
	SETCLR(blue);
	SETCLR(transp);
#undef	SETCLR
	*visual = rgbt->visual;

	if (bpp > 8)
		dprintk("matroxfb: truecolor: "
			"size=%d:%d:%d:%d, shift=%d:%d:%d:%d\n",
			var->transp.length, var->red.length, var->green.length, var->blue.length,
			var->transp.offset, var->red.offset, var->green.offset, var->blue.offset);

	*video_cmap_len = matroxfb_get_cmap_len(var);
	dprintk(KERN_INFO "requested %d*%d/%dbpp (%d*%d)\n", var->xres, var->yres, var->bits_per_pixel,
				var->xres_virtual, var->yres_virtual);
	return 0;
}

static int matroxfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			      unsigned blue, unsigned transp,
			      struct fb_info *fb_info)
{
	struct matrox_fb_info* minfo = container_of(fb_info, struct matrox_fb_info, fbcon);

	DBG(__func__)

	/*
	 *  Set a single color register. The values supplied are
	 *  already rounded down to the hardware's capabilities
	 *  (according to the entries in the `var' structure). Return
	 *  != 0 for invalid regno.
	 */

	if (regno >= minfo->curr.cmap_len)
		return 1;

	if (minfo->fbcon.var.grayscale) {
		/* gray = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}

	red = CNVT_TOHW(red, minfo->fbcon.var.red.length);
	green = CNVT_TOHW(green, minfo->fbcon.var.green.length);
	blue = CNVT_TOHW(blue, minfo->fbcon.var.blue.length);
	transp = CNVT_TOHW(transp, minfo->fbcon.var.transp.length);

	switch (minfo->fbcon.var.bits_per_pixel) {
	case 4:
	case 8:
		mga_outb(M_DAC_REG, regno);
		mga_outb(M_DAC_VAL, red);
		mga_outb(M_DAC_VAL, green);
		mga_outb(M_DAC_VAL, blue);
		break;
	case 16:
		if (regno >= 16)
			break;
		{
			u_int16_t col =
				(red << minfo->fbcon.var.red.offset)     |
				(green << minfo->fbcon.var.green.offset) |
				(blue << minfo->fbcon.var.blue.offset)   |
				(transp << minfo->fbcon.var.transp.offset); /* for 1:5:5:5 */
			minfo->cmap[regno] = col | (col << 16);
		}
		break;
	case 24:
	case 32:
		if (regno >= 16)
			break;
		minfo->cmap[regno] =
			(red   << minfo->fbcon.var.red.offset)   |
			(green << minfo->fbcon.var.green.offset) |
			(blue  << minfo->fbcon.var.blue.offset)  |
			(transp << minfo->fbcon.var.transp.offset);	/* 8:8:8:8 */
		break;
	}
	return 0;
}

static void matroxfb_init_fix(struct matrox_fb_info *minfo)
{
	struct fb_fix_screeninfo *fix = &minfo->fbcon.fix;
	DBG(__func__)

	strcpy(fix->id,"MATROX");

	fix->xpanstep = 8;	/* 8 for 8bpp, 4 for 16bpp, 2 for 32bpp */
	fix->ypanstep = 1;
	fix->ywrapstep = 0;
	fix->mmio_start = minfo->mmio.base;
	fix->mmio_len = minfo->mmio.len;
	fix->accel = minfo->devflags.accelerator;
}

static void matroxfb_update_fix(struct matrox_fb_info *minfo)
{
	struct fb_fix_screeninfo *fix = &minfo->fbcon.fix;
	DBG(__func__)

	mutex_lock(&minfo->fbcon.mm_lock);
	fix->smem_start = minfo->video.base + minfo->curr.ydstorg.bytes;
	fix->smem_len = minfo->video.len_usable - minfo->curr.ydstorg.bytes;
	mutex_unlock(&minfo->fbcon.mm_lock);
}

static int matroxfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	int err;
	int visual;
	int cmap_len;
	unsigned int ydstorg;
	struct matrox_fb_info *minfo = info2minfo(info);

	if (minfo->dead) {
		return -ENXIO;
	}
	if ((err = matroxfb_decode_var(minfo, var, &visual, &cmap_len, &ydstorg)) != 0)
		return err;
	return 0;
}

static int matroxfb_set_par(struct fb_info *info)
{
	int err;
	int visual;
	int cmap_len;
	unsigned int ydstorg;
	struct fb_var_screeninfo *var;
	struct matrox_fb_info *minfo = info2minfo(info);

	DBG(__func__)

	if (minfo->dead) {
		return -ENXIO;
	}

	var = &info->var;
	if ((err = matroxfb_decode_var(minfo, var, &visual, &cmap_len, &ydstorg)) != 0)
		return err;
	minfo->fbcon.screen_base = vaddr_va(minfo->video.vbase) + ydstorg;
	matroxfb_update_fix(minfo);
	minfo->fbcon.fix.visual = visual;
	minfo->fbcon.fix.type = FB_TYPE_PACKED_PIXELS;
	minfo->fbcon.fix.type_aux = 0;
	minfo->fbcon.fix.line_length = (var->xres_virtual * var->bits_per_pixel) >> 3;
	{
		unsigned int pos;

		minfo->curr.cmap_len = cmap_len;
		ydstorg += minfo->devflags.ydstorg;
		minfo->curr.ydstorg.bytes = ydstorg;
		minfo->curr.ydstorg.chunks = ydstorg >> (isInterleave(minfo) ? 3 : 2);
		if (var->bits_per_pixel == 4)
			minfo->curr.ydstorg.pixels = ydstorg;
		else
			minfo->curr.ydstorg.pixels = (ydstorg * 8) / var->bits_per_pixel;
		minfo->curr.final_bppShift = matroxfb_get_final_bppShift(minfo, var->bits_per_pixel);
		{	struct my_timming mt;
			struct matrox_hw_state* hw;
			int out;

			matroxfb_var2my(var, &mt);
			mt.crtc = MATROXFB_SRC_CRTC1;
			/* CRTC1 delays */
			switch (var->bits_per_pixel) {
				case  0:	mt.delay = 31 + 0; break;
				case 16:	mt.delay = 21 + 8; break;
				case 24:	mt.delay = 17 + 8; break;
				case 32:	mt.delay = 16 + 8; break;
				default:	mt.delay = 31 + 8; break;
			}

			hw = &minfo->hw;

			down_read(&minfo->altout.lock);
			for (out = 0; out < MATROXFB_MAX_OUTPUTS; out++) {
				if (minfo->outputs[out].src == MATROXFB_SRC_CRTC1 &&
				    minfo->outputs[out].output->compute) {
					minfo->outputs[out].output->compute(minfo->outputs[out].data, &mt);
				}
			}
			up_read(&minfo->altout.lock);
			minfo->crtc1.pixclock = mt.pixclock;
			minfo->crtc1.mnp = mt.mnp;
			minfo->hw_switch->init(minfo, &mt);
			pos = (var->yoffset * var->xres_virtual + var->xoffset) * minfo->curr.final_bppShift / 32;
			pos += minfo->curr.ydstorg.chunks;

			hw->CRTC[0x0D] = pos & 0xFF;
			hw->CRTC[0x0C] = (pos & 0xFF00) >> 8;
			hw->CRTCEXT[0] = (hw->CRTCEXT[0] & 0xF0) | ((pos >> 16) & 0x0F) | ((pos >> 14) & 0x40);
			hw->CRTCEXT[8] = pos >> 21;
			minfo->hw_switch->restore(minfo);
			update_crtc2(minfo, pos);
			down_read(&minfo->altout.lock);
			for (out = 0; out < MATROXFB_MAX_OUTPUTS; out++) {
				if (minfo->outputs[out].src == MATROXFB_SRC_CRTC1 &&
				    minfo->outputs[out].output->program) {
					minfo->outputs[out].output->program(minfo->outputs[out].data);
				}
			}
			for (out = 0; out < MATROXFB_MAX_OUTPUTS; out++) {
				if (minfo->outputs[out].src == MATROXFB_SRC_CRTC1 &&
				    minfo->outputs[out].output->start) {
					minfo->outputs[out].output->start(minfo->outputs[out].data);
				}
			}
			up_read(&minfo->altout.lock);
			matrox_cfbX_init(minfo);
		}
	}
	minfo->initialized = 1;
	return 0;
}

static int matroxfb_get_vblank(struct matrox_fb_info *minfo,
			       struct fb_vblank *vblank)
{
	unsigned int sts1;

	matroxfb_enable_irq(minfo, 0);
	memset(vblank, 0, sizeof(*vblank));
	vblank->flags = FB_VBLANK_HAVE_VCOUNT | FB_VBLANK_HAVE_VSYNC |
			FB_VBLANK_HAVE_VBLANK | FB_VBLANK_HAVE_HBLANK;
	sts1 = mga_inb(M_INSTS1);
	vblank->vcount = mga_inl(M_VCOUNT);
	/* BTW, on my PIII/450 with G400, reading M_INSTS1
	   byte makes this call about 12% slower (1.70 vs. 2.05 us
	   per ioctl()) */
	if (sts1 & 1)
		vblank->flags |= FB_VBLANK_HBLANKING;
	if (sts1 & 8)
		vblank->flags |= FB_VBLANK_VSYNCING;
	if (vblank->vcount >= minfo->fbcon.var.yres)
		vblank->flags |= FB_VBLANK_VBLANKING;
	if (test_bit(0, &minfo->irq_flags)) {
		vblank->flags |= FB_VBLANK_HAVE_COUNT;
		/* Only one writer, aligned int value...
		   it should work without lock and without atomic_t */
		vblank->count = minfo->crtc1.vsync.cnt;
	}
	return 0;
}

static struct matrox_altout panellink_output = {
	.name	 = "Panellink output",
};

static int matroxfb_ioctl(struct fb_info *info,
			  unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct matrox_fb_info *minfo = info2minfo(info);

	DBG(__func__)

	if (minfo->dead) {
		return -ENXIO;
	}

	switch (cmd) {
		case FBIOGET_VBLANK:
			{
				struct fb_vblank vblank;
				int err;

				err = matroxfb_get_vblank(minfo, &vblank);
				if (err)
					return err;
				if (copy_to_user(argp, &vblank, sizeof(vblank)))
					return -EFAULT;
				return 0;
			}
		case FBIO_WAITFORVSYNC:
			{
				u_int32_t crt;

				if (get_user(crt, (u_int32_t __user *)arg))
					return -EFAULT;

				return matroxfb_wait_for_sync(minfo, crt);
			}
		case MATROXFB_SET_OUTPUT_MODE:
			{
				struct matroxioc_output_mode mom;
				struct matrox_altout *oproc;
				int val;

				if (copy_from_user(&mom, argp, sizeof(mom)))
					return -EFAULT;
				if (mom.output >= MATROXFB_MAX_OUTPUTS)
					return -ENXIO;
				down_read(&minfo->altout.lock);
				oproc = minfo->outputs[mom.output].output;
				if (!oproc) {
					val = -ENXIO;
				} else if (!oproc->verifymode) {
					if (mom.mode == MATROXFB_OUTPUT_MODE_MONITOR) {
						val = 0;
					} else {
						val = -EINVAL;
					}
				} else {
					val = oproc->verifymode(minfo->outputs[mom.output].data, mom.mode);
				}
				if (!val) {
					if (minfo->outputs[mom.output].mode != mom.mode) {
						minfo->outputs[mom.output].mode = mom.mode;
						val = 1;
					}
				}
				up_read(&minfo->altout.lock);
				if (val != 1)
					return val;
				switch (minfo->outputs[mom.output].src) {
					case MATROXFB_SRC_CRTC1:
						matroxfb_set_par(info);
						break;
					case MATROXFB_SRC_CRTC2:
						{
							struct matroxfb_dh_fb_info* crtc2;

							down_read(&minfo->crtc2.lock);
							crtc2 = minfo->crtc2.info;
							if (crtc2)
								crtc2->fbcon.fbops->fb_set_par(&crtc2->fbcon);
							up_read(&minfo->crtc2.lock);
						}
						break;
				}
				return 0;
			}
		case MATROXFB_GET_OUTPUT_MODE:
			{
				struct matroxioc_output_mode mom;
				struct matrox_altout *oproc;
				int val;

				if (copy_from_user(&mom, argp, sizeof(mom)))
					return -EFAULT;
				if (mom.output >= MATROXFB_MAX_OUTPUTS)
					return -ENXIO;
				down_read(&minfo->altout.lock);
				oproc = minfo->outputs[mom.output].output;
				if (!oproc) {
					val = -ENXIO;
				} else {
					mom.mode = minfo->outputs[mom.output].mode;
					val = 0;
				}
				up_read(&minfo->altout.lock);
				if (val)
					return val;
				if (copy_to_user(argp, &mom, sizeof(mom)))
					return -EFAULT;
				return 0;
			}
		case MATROXFB_SET_OUTPUT_CONNECTION:
			{
				u_int32_t tmp;
				int i;
				int changes;

				if (copy_from_user(&tmp, argp, sizeof(tmp)))
					return -EFAULT;
				for (i = 0; i < 32; i++) {
					if (tmp & (1 << i)) {
						if (i >= MATROXFB_MAX_OUTPUTS)
							return -ENXIO;
						if (!minfo->outputs[i].output)
							return -ENXIO;
						switch (minfo->outputs[i].src) {
							case MATROXFB_SRC_NONE:
							case MATROXFB_SRC_CRTC1:
								break;
							default:
								return -EBUSY;
						}
					}
				}
				if (minfo->devflags.panellink) {
					if (tmp & MATROXFB_OUTPUT_CONN_DFP) {
						if (tmp & MATROXFB_OUTPUT_CONN_SECONDARY)
							return -EINVAL;
						for (i = 0; i < MATROXFB_MAX_OUTPUTS; i++) {
							if (minfo->outputs[i].src == MATROXFB_SRC_CRTC2) {
								return -EBUSY;
							}
						}
					}
				}
				changes = 0;
				for (i = 0; i < MATROXFB_MAX_OUTPUTS; i++) {
					if (tmp & (1 << i)) {
						if (minfo->outputs[i].src != MATROXFB_SRC_CRTC1) {
							changes = 1;
							minfo->outputs[i].src = MATROXFB_SRC_CRTC1;
						}
					} else if (minfo->outputs[i].src == MATROXFB_SRC_CRTC1) {
						changes = 1;
						minfo->outputs[i].src = MATROXFB_SRC_NONE;
					}
				}
				if (!changes)
					return 0;
				matroxfb_set_par(info);
				return 0;
			}
		case MATROXFB_GET_OUTPUT_CONNECTION:
			{
				u_int32_t conn = 0;
				int i;

				for (i = 0; i < MATROXFB_MAX_OUTPUTS; i++) {
					if (minfo->outputs[i].src == MATROXFB_SRC_CRTC1) {
						conn |= 1 << i;
					}
				}
				if (put_user(conn, (u_int32_t __user *)arg))
					return -EFAULT;
				return 0;
			}
		case MATROXFB_GET_AVAILABLE_OUTPUTS:
			{
				u_int32_t conn = 0;
				int i;

				for (i = 0; i < MATROXFB_MAX_OUTPUTS; i++) {
					if (minfo->outputs[i].output) {
						switch (minfo->outputs[i].src) {
							case MATROXFB_SRC_NONE:
							case MATROXFB_SRC_CRTC1:
								conn |= 1 << i;
								break;
						}
					}
				}
				if (minfo->devflags.panellink) {
					if (conn & MATROXFB_OUTPUT_CONN_DFP)
						conn &= ~MATROXFB_OUTPUT_CONN_SECONDARY;
					if (conn & MATROXFB_OUTPUT_CONN_SECONDARY)
						conn &= ~MATROXFB_OUTPUT_CONN_DFP;
				}
				if (put_user(conn, (u_int32_t __user *)arg))
					return -EFAULT;
				return 0;
			}
		case MATROXFB_GET_ALL_OUTPUTS:
			{
				u_int32_t conn = 0;
				int i;

				for (i = 0; i < MATROXFB_MAX_OUTPUTS; i++) {
					if (minfo->outputs[i].output) {
						conn |= 1 << i;
					}
				}
				if (put_user(conn, (u_int32_t __user *)arg))
					return -EFAULT;
				return 0;
			}
		case VIDIOC_QUERYCAP:
			{
				struct v4l2_capability r;

				memset(&r, 0, sizeof(r));
				strcpy(r.driver, "matroxfb");
				strcpy(r.card, "Matrox");
				sprintf(r.bus_info, "PCI:%s", pci_name(minfo->pcidev));
				r.version = KERNEL_VERSION(1,0,0);
				r.capabilities = V4L2_CAP_VIDEO_OUTPUT;
				if (copy_to_user(argp, &r, sizeof(r)))
					return -EFAULT;
				return 0;

			}
		case VIDIOC_QUERYCTRL:
			{
				struct v4l2_queryctrl qctrl;
				int err;

				if (copy_from_user(&qctrl, argp, sizeof(qctrl)))
					return -EFAULT;

				down_read(&minfo->altout.lock);
				if (!minfo->outputs[1].output) {
					err = -ENXIO;
				} else if (minfo->outputs[1].output->getqueryctrl) {
					err = minfo->outputs[1].output->getqueryctrl(minfo->outputs[1].data, &qctrl);
				} else {
					err = -EINVAL;
				}
				up_read(&minfo->altout.lock);
				if (err >= 0 &&
				    copy_to_user(argp, &qctrl, sizeof(qctrl)))
					return -EFAULT;
				return err;
			}
		case VIDIOC_G_CTRL:
			{
				struct v4l2_control ctrl;
				int err;

				if (copy_from_user(&ctrl, argp, sizeof(ctrl)))
					return -EFAULT;

				down_read(&minfo->altout.lock);
				if (!minfo->outputs[1].output) {
					err = -ENXIO;
				} else if (minfo->outputs[1].output->getctrl) {
					err = minfo->outputs[1].output->getctrl(minfo->outputs[1].data, &ctrl);
				} else {
					err = -EINVAL;
				}
				up_read(&minfo->altout.lock);
				if (err >= 0 &&
				    copy_to_user(argp, &ctrl, sizeof(ctrl)))
					return -EFAULT;
				return err;
			}
		case VIDIOC_S_CTRL:
			{
				struct v4l2_control ctrl;
				int err;

				if (copy_from_user(&ctrl, argp, sizeof(ctrl)))
					return -EFAULT;

				down_read(&minfo->altout.lock);
				if (!minfo->outputs[1].output) {
					err = -ENXIO;
				} else if (minfo->outputs[1].output->setctrl) {
					err = minfo->outputs[1].output->setctrl(minfo->outputs[1].data, &ctrl);
				} else {
					err = -EINVAL;
				}
				up_read(&minfo->altout.lock);
				return err;
			}
	}
	return -ENOTTY;
}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */

static int matroxfb_blank(int blank, struct fb_info *info)
{
	int seq;
	int crtc;
	CRITFLAGS
	struct matrox_fb_info *minfo = info2minfo(info);

	DBG(__func__)

	if (minfo->dead)
		return 1;

	switch (blank) {
	case FB_BLANK_NORMAL:  seq = 0x20; crtc = 0x00; break; /* works ??? */
	case FB_BLANK_VSYNC_SUSPEND:  seq = 0x20; crtc = 0x10; break;
	case FB_BLANK_HSYNC_SUSPEND:  seq = 0x20; crtc = 0x20; break;
	case FB_BLANK_POWERDOWN:  seq = 0x20; crtc = 0x30; break;
	default: seq = 0x00; crtc = 0x00; break;
	}

	CRITBEGIN

	mga_outb(M_SEQ_INDEX, 1);
	mga_outb(M_SEQ_DATA, (mga_inb(M_SEQ_DATA) & ~0x20) | seq);
	mga_outb(M_EXTVGA_INDEX, 1);
	mga_outb(M_EXTVGA_DATA, (mga_inb(M_EXTVGA_DATA) & ~0x30) | crtc);

	CRITEND
	return 0;
}

static const struct fb_ops matroxfb_ops = {
	.owner =	THIS_MODULE,
	.fb_open =	matroxfb_open,
	.fb_release =	matroxfb_release,
	.fb_check_var =	matroxfb_check_var,
	.fb_set_par =	matroxfb_set_par,
	.fb_setcolreg =	matroxfb_setcolreg,
	.fb_pan_display =matroxfb_pan_display,
	.fb_blank =	matroxfb_blank,
	.fb_ioctl =	matroxfb_ioctl,
/*	.fb_fillrect =	<set by matrox_cfbX_init>, */
/*	.fb_copyarea =	<set by matrox_cfbX_init>, */
/*	.fb_imageblit =	<set by matrox_cfbX_init>, */
/*	.fb_cursor =	<set by matrox_cfbX_init>, */
};

#define RSDepth(X)	(((X) >> 8) & 0x0F)
#define RS8bpp		0x1
#define RS15bpp		0x2
#define RS16bpp		0x3
#define RS32bpp		0x4
#define RS4bpp		0x5
#define RS24bpp		0x6
#define RSText		0x7
#define RSText8		0x8
/* 9-F */
static struct { struct fb_bitfield red, green, blue, transp; int bits_per_pixel; } colors[] = {
	{ {  0, 8, 0}, { 0, 8, 0}, { 0, 8, 0}, {  0, 0, 0},  8 },
	{ { 10, 5, 0}, { 5, 5, 0}, { 0, 5, 0}, { 15, 1, 0}, 16 },
	{ { 11, 5, 0}, { 5, 6, 0}, { 0, 5, 0}, {  0, 0, 0}, 16 },
	{ { 16, 8, 0}, { 8, 8, 0}, { 0, 8, 0}, { 24, 8, 0}, 32 },
	{ {  0, 8, 0}, { 0, 8, 0}, { 0, 8, 0}, {  0, 0, 0},  4 },
	{ { 16, 8, 0}, { 8, 8, 0}, { 0, 8, 0}, {  0, 0, 0}, 24 },
	{ {  0, 6, 0}, { 0, 6, 0}, { 0, 6, 0}, {  0, 0, 0},  0 },	/* textmode with (default) VGA8x16 */
	{ {  0, 6, 0}, { 0, 6, 0}, { 0, 6, 0}, {  0, 0, 0},  0 },	/* textmode hardwired to VGA8x8 */
};

/* initialized by setup, see explanation at end of file (search for MODULE_PARM_DESC) */
static unsigned int mem;		/* "matroxfb:mem:xxxxxM" */
static int option_precise_width = 1;	/* cannot be changed, option_precise_width==0 must imply noaccel */
static int inv24;			/* "matroxfb:inv24" */
static int cross4MB = -1;		/* "matroxfb:cross4MB" */
static int disabled;			/* "matroxfb:disabled" */
static int noaccel;			/* "matroxfb:noaccel" */
static int nopan;			/* "matroxfb:nopan" */
static int no_pci_retry;		/* "matroxfb:nopciretry" */
static int novga;			/* "matroxfb:novga" */
static int nobios;			/* "matroxfb:nobios" */
static int noinit = 1;			/* "matroxfb:init" */
static int inverse;			/* "matroxfb:inverse" */
static int sgram;			/* "matroxfb:sgram" */
static int mtrr = 1;			/* "matroxfb:nomtrr" */
static int grayscale;			/* "matroxfb:grayscale" */
static int dev = -1;			/* "matroxfb:dev:xxxxx" */
static unsigned int vesa = ~0;		/* "matroxfb:vesa:xxxxx" */
static int depth = -1;			/* "matroxfb:depth:xxxxx" */
static unsigned int xres;		/* "matroxfb:xres:xxxxx" */
static unsigned int yres;		/* "matroxfb:yres:xxxxx" */
static unsigned int upper = ~0;		/* "matroxfb:upper:xxxxx" */
static unsigned int lower = ~0;		/* "matroxfb:lower:xxxxx" */
static unsigned int vslen;		/* "matroxfb:vslen:xxxxx" */
static unsigned int left = ~0;		/* "matroxfb:left:xxxxx" */
static unsigned int right = ~0;		/* "matroxfb:right:xxxxx" */
static unsigned int hslen;		/* "matroxfb:hslen:xxxxx" */
static unsigned int pixclock;		/* "matroxfb:pixclock:xxxxx" */
static int sync = -1;			/* "matroxfb:sync:xxxxx" */
static unsigned int fv;			/* "matroxfb:fv:xxxxx" */
static unsigned int fh;			/* "matroxfb:fh:xxxxxk" */
static unsigned int maxclk;		/* "matroxfb:maxclk:xxxxM" */
static int dfp;				/* "matroxfb:dfp */
static int dfp_type = -1;		/* "matroxfb:dfp:xxx */
static int memtype = -1;		/* "matroxfb:memtype:xxx" */
static char outputs[8];			/* "matroxfb:outputs:xxx" */

#ifndef MODULE
static char videomode[64];		/* "matroxfb:mode:xxxxx" or "matroxfb:xxxxx" */
#endif

static int matroxfb_getmemory(struct matrox_fb_info *minfo,
			      unsigned int maxSize, unsigned int *realSize)
{
	vaddr_t vm;
	unsigned int offs;
	unsigned int offs2;
	unsigned char orig;
	unsigned char bytes[32];
	unsigned char* tmp;

	DBG(__func__)

	vm = minfo->video.vbase;
	maxSize &= ~0x1FFFFF;	/* must be X*2MB (really it must be 2 or X*4MB) */
	/* at least 2MB */
	if (maxSize < 0x0200000) return 0;
	if (maxSize > 0x2000000) maxSize = 0x2000000;

	mga_outb(M_EXTVGA_INDEX, 0x03);
	orig = mga_inb(M_EXTVGA_DATA);
	mga_outb(M_EXTVGA_DATA, orig | 0x80);

	tmp = bytes;
	for (offs = 0x100000; offs < maxSize; offs += 0x200000)
		*tmp++ = mga_readb(vm, offs);
	for (offs = 0x100000; offs < maxSize; offs += 0x200000)
		mga_writeb(vm, offs, 0x02);
	mga_outb(M_CACHEFLUSH, 0x00);
	for (offs = 0x100000; offs < maxSize; offs += 0x200000) {
		if (mga_readb(vm, offs) != 0x02)
			break;
		mga_writeb(vm, offs, mga_readb(vm, offs) - 0x02);
		if (mga_readb(vm, offs))
			break;
	}
	tmp = bytes;
	for (offs2 = 0x100000; offs2 < maxSize; offs2 += 0x200000)
		mga_writeb(vm, offs2, *tmp++);

	mga_outb(M_EXTVGA_INDEX, 0x03);
	mga_outb(M_EXTVGA_DATA, orig);

	*realSize = offs - 0x100000;
#ifdef CONFIG_FB_MATROX_MILLENIUM
	minfo->interleave = !(!isMillenium(minfo) || ((offs - 0x100000) & 0x3FFFFF));
#endif
	return 1;
}

struct video_board {
	int maxvram;
	int maxdisplayable;
	int accelID;
	struct matrox_switch* lowlevel;
		 };
#ifdef CONFIG_FB_MATROX_MILLENIUM
static struct video_board vbMillennium = {
	.maxvram = 0x0800000,
	.maxdisplayable = 0x0800000,
	.accelID = FB_ACCEL_MATROX_MGA2064W,
	.lowlevel = &matrox_millennium
};

static struct video_board vbMillennium2 = {
	.maxvram = 0x1000000,
	.maxdisplayable = 0x0800000,
	.accelID = FB_ACCEL_MATROX_MGA2164W,
	.lowlevel = &matrox_millennium
};

static struct video_board vbMillennium2A = {
	.maxvram = 0x1000000,
	.maxdisplayable = 0x0800000,
	.accelID = FB_ACCEL_MATROX_MGA2164W_AGP,
	.lowlevel = &matrox_millennium
};
#endif	/* CONFIG_FB_MATROX_MILLENIUM */
#ifdef CONFIG_FB_MATROX_MYSTIQUE
static struct video_board vbMystique = {
	.maxvram = 0x0800000,
	.maxdisplayable = 0x0800000,
	.accelID = FB_ACCEL_MATROX_MGA1064SG,
	.lowlevel = &matrox_mystique
};
#endif	/* CONFIG_FB_MATROX_MYSTIQUE */
#ifdef CONFIG_FB_MATROX_G
static struct video_board vbG100 = {
	.maxvram = 0x0800000,
	.maxdisplayable = 0x0800000,
	.accelID = FB_ACCEL_MATROX_MGAG100,
	.lowlevel = &matrox_G100
};

static struct video_board vbG200 = {
	.maxvram = 0x1000000,
	.maxdisplayable = 0x1000000,
	.accelID = FB_ACCEL_MATROX_MGAG200,
	.lowlevel = &matrox_G100
};
/* from doc it looks like that accelerator can draw only to low 16MB :-( Direct accesses & displaying are OK for
   whole 32MB */
static struct video_board vbG400 = {
	.maxvram = 0x2000000,
	.maxdisplayable = 0x1000000,
	.accelID = FB_ACCEL_MATROX_MGAG400,
	.lowlevel = &matrox_G100
};
#endif

#define DEVF_VIDEO64BIT		0x0001
#define	DEVF_SWAPS		0x0002
#define DEVF_SRCORG		0x0004
#define DEVF_DUALHEAD		0x0008
#define DEVF_CROSS4MB		0x0010
#define DEVF_TEXT4B		0x0020
/* #define DEVF_recycled	0x0040	*/
/* #define DEVF_recycled	0x0080	*/
#define DEVF_SUPPORT32MB	0x0100
#define DEVF_ANY_VXRES		0x0200
#define DEVF_TEXT16B		0x0400
#define DEVF_CRTC2		0x0800
#define DEVF_MAVEN_CAPABLE	0x1000
#define DEVF_PANELLINK_CAPABLE	0x2000
#define DEVF_G450DAC		0x4000

#define DEVF_GCORE	(DEVF_VIDEO64BIT | DEVF_SWAPS | DEVF_CROSS4MB)
#define DEVF_G2CORE	(DEVF_GCORE | DEVF_ANY_VXRES | DEVF_MAVEN_CAPABLE | DEVF_PANELLINK_CAPABLE | DEVF_SRCORG | DEVF_DUALHEAD)
#define DEVF_G100	(DEVF_GCORE) /* no doc, no vxres... */
#define DEVF_G200	(DEVF_G2CORE)
#define DEVF_G400	(DEVF_G2CORE | DEVF_SUPPORT32MB | DEVF_TEXT16B | DEVF_CRTC2)
/* if you'll find how to drive DFP... */
#define DEVF_G450	(DEVF_GCORE | DEVF_ANY_VXRES | DEVF_SUPPORT32MB | DEVF_TEXT16B | DEVF_CRTC2 | DEVF_G450DAC | DEVF_SRCORG | DEVF_DUALHEAD)
#define DEVF_G550	(DEVF_G450)

static struct board {
	unsigned short vendor, device, rev, svid, sid;
	unsigned int flags;
	unsigned int maxclk;
	enum mga_chip chip;
	struct video_board* base;
	const char* name;
		} dev_list[] = {
#ifdef CONFIG_FB_MATROX_MILLENIUM
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MIL,	0xFF,
		0,			0,
		DEVF_TEXT4B,
		230000,
		MGA_2064,
		&vbMillennium,
		"Millennium (PCI)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MIL_2,	0xFF,
		0,			0,
		DEVF_SWAPS,
		220000,
		MGA_2164,
		&vbMillennium2,
		"Millennium II (PCI)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MIL_2_AGP,	0xFF,
		0,			0,
		DEVF_SWAPS,
		250000,
		MGA_2164,
		&vbMillennium2A,
		"Millennium II (AGP)"},
#endif
#ifdef CONFIG_FB_MATROX_MYSTIQUE
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MYS,	0x02,
		0,			0,
		DEVF_VIDEO64BIT | DEVF_CROSS4MB,
		180000,
		MGA_1064,
		&vbMystique,
		"Mystique (PCI)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MYS,	0xFF,
		0,			0,
		DEVF_VIDEO64BIT | DEVF_SWAPS | DEVF_CROSS4MB,
		220000,
		MGA_1164,
		&vbMystique,
		"Mystique 220 (PCI)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MYS_AGP,	0x02,
		0,			0,
		DEVF_VIDEO64BIT | DEVF_CROSS4MB,
		180000,
		MGA_1064,
		&vbMystique,
		"Mystique (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MYS_AGP,	0xFF,
		0,			0,
		DEVF_VIDEO64BIT | DEVF_SWAPS | DEVF_CROSS4MB,
		220000,
		MGA_1164,
		&vbMystique,
		"Mystique 220 (AGP)"},
#endif
#ifdef CONFIG_FB_MATROX_G
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G100_MM,	0xFF,
		0,			0,
		DEVF_G100,
		230000,
		MGA_G100,
		&vbG100,
		"MGA-G100 (PCI)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G100_AGP,	0xFF,
		0,			0,
		DEVF_G100,
		230000,
		MGA_G100,
		&vbG100,
		"MGA-G100 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_PCI,	0xFF,
		0,			0,
		DEVF_G200,
		250000,
		MGA_G200,
		&vbG200,
		"MGA-G200 (PCI)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_AGP,	0xFF,
		PCI_SS_VENDOR_ID_MATROX,	PCI_SS_ID_MATROX_GENERIC,
		DEVF_G200,
		220000,
		MGA_G200,
		&vbG200,
		"MGA-G200 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_AGP,	0xFF,
		PCI_SS_VENDOR_ID_MATROX,	PCI_SS_ID_MATROX_MYSTIQUE_G200_AGP,
		DEVF_G200,
		230000,
		MGA_G200,
		&vbG200,
		"Mystique G200 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_AGP,	0xFF,
		PCI_SS_VENDOR_ID_MATROX,	PCI_SS_ID_MATROX_MILLENIUM_G200_AGP,
		DEVF_G200,
		250000,
		MGA_G200,
		&vbG200,
		"Millennium G200 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_AGP,	0xFF,
		PCI_SS_VENDOR_ID_MATROX,	PCI_SS_ID_MATROX_MARVEL_G200_AGP,
		DEVF_G200,
		230000,
		MGA_G200,
		&vbG200,
		"Marvel G200 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_AGP,	0xFF,
		PCI_SS_VENDOR_ID_SIEMENS_NIXDORF,	PCI_SS_ID_SIEMENS_MGA_G200_AGP,
		DEVF_G200,
		230000,
		MGA_G200,
		&vbG200,
		"MGA-G200 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_AGP,	0xFF,
		0,			0,
		DEVF_G200,
		230000,
		MGA_G200,
		&vbG200,
		"G200 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G400,	0x80,
		PCI_SS_VENDOR_ID_MATROX,	PCI_SS_ID_MATROX_MILLENNIUM_G400_MAX_AGP,
		DEVF_G400,
		360000,
		MGA_G400,
		&vbG400,
		"Millennium G400 MAX (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G400,	0x80,
		0,			0,
		DEVF_G400,
		300000,
		MGA_G400,
		&vbG400,
		"G400 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G400,	0xFF,
		0,			0,
		DEVF_G450,
		360000,
		MGA_G450,
		&vbG400,
		"G450"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G550,	0xFF,
		0,			0,
		DEVF_G550,
		360000,
		MGA_G550,
		&vbG400,
		"G550"},
#endif
	{0,			0,				0xFF,
		0,			0,
		0,
		0,
		0,
		NULL,
		NULL}};

#ifndef MODULE
static const struct fb_videomode defaultmode = {
	/* 640x480 @ 60Hz, 31.5 kHz */
	NULL, 60, 640, 480, 39721, 40, 24, 32, 11, 96, 2,
	0, FB_VMODE_NONINTERLACED
};

static int hotplug = 0;
#endif /* !MODULE */

static void setDefaultOutputs(struct matrox_fb_info *minfo)
{
	unsigned int i;
	const char* ptr;

	minfo->outputs[0].default_src = MATROXFB_SRC_CRTC1;
	if (minfo->devflags.g450dac) {
		minfo->outputs[1].default_src = MATROXFB_SRC_CRTC1;
		minfo->outputs[2].default_src = MATROXFB_SRC_CRTC1;
	} else if (dfp) {
		minfo->outputs[2].default_src = MATROXFB_SRC_CRTC1;
	}
	ptr = outputs;
	for (i = 0; i < MATROXFB_MAX_OUTPUTS; i++) {
		char c = *ptr++;

		if (c == 0) {
			break;
		}
		if (c == '0') {
			minfo->outputs[i].default_src = MATROXFB_SRC_NONE;
		} else if (c == '1') {
			minfo->outputs[i].default_src = MATROXFB_SRC_CRTC1;
		} else if (c == '2' && minfo->devflags.crtc2) {
			minfo->outputs[i].default_src = MATROXFB_SRC_CRTC2;
		} else {
			printk(KERN_ERR "matroxfb: Unknown outputs setting\n");
			break;
		}
	}
	/* Nullify this option for subsequent adapters */
	outputs[0] = 0;
}

static int initMatrox2(struct matrox_fb_info *minfo, struct board *b)
{
	unsigned long ctrlptr_phys = 0;
	unsigned long video_base_phys = 0;
	unsigned int memsize;
	int err;

	static const struct pci_device_id intel_82437[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82437) },
		{ },
	};

	DBG(__func__)

	/* set default values... */
	vesafb_defined.accel_flags = FB_ACCELF_TEXT;

	minfo->hw_switch = b->base->lowlevel;
	minfo->devflags.accelerator = b->base->accelID;
	minfo->max_pixel_clock = b->maxclk;

	printk(KERN_INFO "matroxfb: Matrox %s detected\n", b->name);
	minfo->capable.plnwt = 1;
	minfo->chip = b->chip;
	minfo->capable.srcorg = b->flags & DEVF_SRCORG;
	minfo->devflags.video64bits = b->flags & DEVF_VIDEO64BIT;
	if (b->flags & DEVF_TEXT4B) {
		minfo->devflags.vgastep = 4;
		minfo->devflags.textmode = 4;
		minfo->devflags.text_type_aux = FB_AUX_TEXT_MGA_STEP16;
	} else if (b->flags & DEVF_TEXT16B) {
		minfo->devflags.vgastep = 16;
		minfo->devflags.textmode = 1;
		minfo->devflags.text_type_aux = FB_AUX_TEXT_MGA_STEP16;
	} else {
		minfo->devflags.vgastep = 8;
		minfo->devflags.textmode = 1;
		minfo->devflags.text_type_aux = FB_AUX_TEXT_MGA_STEP8;
	}
	minfo->devflags.support32MB = (b->flags & DEVF_SUPPORT32MB) != 0;
	minfo->devflags.precise_width = !(b->flags & DEVF_ANY_VXRES);
	minfo->devflags.crtc2 = (b->flags & DEVF_CRTC2) != 0;
	minfo->devflags.maven_capable = (b->flags & DEVF_MAVEN_CAPABLE) != 0;
	minfo->devflags.dualhead = (b->flags & DEVF_DUALHEAD) != 0;
	minfo->devflags.dfp_type = dfp_type;
	minfo->devflags.g450dac = (b->flags & DEVF_G450DAC) != 0;
	minfo->devflags.textstep = minfo->devflags.vgastep * minfo->devflags.textmode;
	minfo->devflags.textvram = 65536 / minfo->devflags.textmode;
	setDefaultOutputs(minfo);
	if (b->flags & DEVF_PANELLINK_CAPABLE) {
		minfo->outputs[2].data = minfo;
		minfo->outputs[2].output = &panellink_output;
		minfo->outputs[2].src = minfo->outputs[2].default_src;
		minfo->outputs[2].mode = MATROXFB_OUTPUT_MODE_MONITOR;
		minfo->devflags.panellink = 1;
	}

	if (minfo->capable.cross4MB < 0)
		minfo->capable.cross4MB = b->flags & DEVF_CROSS4MB;
	if (b->flags & DEVF_SWAPS) {
		ctrlptr_phys = pci_resource_start(minfo->pcidev, 1);
		video_base_phys = pci_resource_start(minfo->pcidev, 0);
		minfo->devflags.fbResource = PCI_BASE_ADDRESS_0;
	} else {
		ctrlptr_phys = pci_resource_start(minfo->pcidev, 0);
		video_base_phys = pci_resource_start(minfo->pcidev, 1);
		minfo->devflags.fbResource = PCI_BASE_ADDRESS_1;
	}
	err = -EINVAL;
	if (!ctrlptr_phys) {
		printk(KERN_ERR "matroxfb: control registers are not available, matroxfb disabled\n");
		goto fail;
	}
	if (!video_base_phys) {
		printk(KERN_ERR "matroxfb: video RAM is not available in PCI address space, matroxfb disabled\n");
		goto fail;
	}
	memsize = b->base->maxvram;
	if (!request_mem_region(ctrlptr_phys, 16384, "matroxfb MMIO")) {
		goto fail;
	}
	if (!request_mem_region(video_base_phys, memsize, "matroxfb FB")) {
		goto failCtrlMR;
	}
	minfo->video.len_maximum = memsize;
	/* convert mem (autodetect k, M) */
	if (mem < 1024) mem *= 1024;
	if (mem < 0x00100000) mem *= 1024;

	if (mem && (mem < memsize))
		memsize = mem;
	err = -ENOMEM;

	minfo->mmio.vbase.vaddr = ioremap(ctrlptr_phys, 16384);
	if (!minfo->mmio.vbase.vaddr) {
		printk(KERN_ERR "matroxfb: cannot ioremap(%lX, 16384), matroxfb disabled\n", ctrlptr_phys);
		goto failVideoMR;
	}
	minfo->mmio.base = ctrlptr_phys;
	minfo->mmio.len = 16384;
	minfo->video.base = video_base_phys;
	minfo->video.vbase.vaddr = ioremap_wc(video_base_phys, memsize);
	if (!minfo->video.vbase.vaddr) {
		printk(KERN_ERR "matroxfb: cannot ioremap(%lX, %d), matroxfb disabled\n",
			video_base_phys, memsize);
		goto failCtrlIO;
	}
	{
		u_int32_t cmd;
		u_int32_t mga_option;

		pci_read_config_dword(minfo->pcidev, PCI_OPTION_REG, &mga_option);
		pci_read_config_dword(minfo->pcidev, PCI_COMMAND, &cmd);
		mga_option &= 0x7FFFFFFF; /* clear BIG_ENDIAN */
		mga_option |= MX_OPTION_BSWAP;
		/* disable palette snooping */
		cmd &= ~PCI_COMMAND_VGA_PALETTE;
		if (pci_dev_present(intel_82437)) {
			if (!(mga_option & 0x20000000) && !minfo->devflags.nopciretry) {
				printk(KERN_WARNING "matroxfb: Disabling PCI retries due to i82437 present\n");
			}
			mga_option |= 0x20000000;
			minfo->devflags.nopciretry = 1;
		}
		pci_write_config_dword(minfo->pcidev, PCI_COMMAND, cmd);
		pci_write_config_dword(minfo->pcidev, PCI_OPTION_REG, mga_option);
		minfo->hw.MXoptionReg = mga_option;

		/* select non-DMA memory for PCI_MGA_DATA, otherwise dump of PCI cfg space can lock PCI bus */
		/* maybe preinit() candidate, but it is same... for all devices... at this time... */
		pci_write_config_dword(minfo->pcidev, PCI_MGA_INDEX, 0x00003C00);
	}

	err = -ENXIO;
	matroxfb_read_pins(minfo);
	if (minfo->hw_switch->preinit(minfo)) {
		goto failVideoIO;
	}

	err = -ENOMEM;
	if (!matroxfb_getmemory(minfo, memsize, &minfo->video.len) || !minfo->video.len) {
		printk(KERN_ERR "matroxfb: cannot determine memory size\n");
		goto failVideoIO;
	}
	minfo->devflags.ydstorg = 0;

	minfo->video.base = video_base_phys;
	minfo->video.len_usable = minfo->video.len;
	if (minfo->video.len_usable > b->base->maxdisplayable)
		minfo->video.len_usable = b->base->maxdisplayable;
	if (mtrr)
		minfo->wc_cookie = arch_phys_wc_add(video_base_phys,
						    minfo->video.len);

	if (!minfo->devflags.novga)
		request_region(0x3C0, 32, "matrox");
	matroxfb_g450_connect(minfo);
	minfo->hw_switch->reset(minfo);

	minfo->fbcon.monspecs.hfmin = 0;
	minfo->fbcon.monspecs.hfmax = fh;
	minfo->fbcon.monspecs.vfmin = 0;
	minfo->fbcon.monspecs.vfmax = fv;
	minfo->fbcon.monspecs.dpms = 0;	/* TBD */

	/* static settings */
	vesafb_defined.red = colors[depth-1].red;
	vesafb_defined.green = colors[depth-1].green;
	vesafb_defined.blue = colors[depth-1].blue;
	vesafb_defined.bits_per_pixel = colors[depth-1].bits_per_pixel;
	vesafb_defined.grayscale = grayscale;
	vesafb_defined.vmode = 0;
	if (noaccel)
		vesafb_defined.accel_flags &= ~FB_ACCELF_TEXT;

	minfo->fbops = matroxfb_ops;
	minfo->fbcon.fbops = &minfo->fbops;
	minfo->fbcon.pseudo_palette = minfo->cmap;
	minfo->fbcon.flags = FBINFO_PARTIAL_PAN_OK | 	 /* Prefer panning for scroll under MC viewer/edit */
				      FBINFO_HWACCEL_COPYAREA |  /* We have hw-assisted bmove */
				      FBINFO_HWACCEL_FILLRECT |  /* And fillrect */
				      FBINFO_HWACCEL_IMAGEBLIT | /* And imageblit */
				      FBINFO_HWACCEL_XPAN |      /* And we support both horizontal */
				      FBINFO_HWACCEL_YPAN |      /* And vertical panning */
				      FBINFO_READS_FAST;
	minfo->video.len_usable &= PAGE_MASK;
	fb_alloc_cmap(&minfo->fbcon.cmap, 256, 1);

#ifndef MODULE
	/* mode database is marked __init!!! */
	if (!hotplug) {
		fb_find_mode(&vesafb_defined, &minfo->fbcon, videomode[0] ? videomode : NULL,
			NULL, 0, &defaultmode, vesafb_defined.bits_per_pixel);
	}
#endif /* !MODULE */

	/* mode modifiers */
	if (hslen)
		vesafb_defined.hsync_len = hslen;
	if (vslen)
		vesafb_defined.vsync_len = vslen;
	if (left != ~0)
		vesafb_defined.left_margin = left;
	if (right != ~0)
		vesafb_defined.right_margin = right;
	if (upper != ~0)
		vesafb_defined.upper_margin = upper;
	if (lower != ~0)
		vesafb_defined.lower_margin = lower;
	if (xres)
		vesafb_defined.xres = xres;
	if (yres)
		vesafb_defined.yres = yres;
	if (sync != -1)
		vesafb_defined.sync = sync;
	else if (vesafb_defined.sync == ~0) {
		vesafb_defined.sync = 0;
		if (yres < 400)
			vesafb_defined.sync |= FB_SYNC_HOR_HIGH_ACT;
		else if (yres < 480)
			vesafb_defined.sync |= FB_SYNC_VERT_HIGH_ACT;
	}

	/* fv, fh, maxclk limits was specified */
	{
		unsigned int tmp;

		if (fv) {
			tmp = fv * (vesafb_defined.upper_margin + vesafb_defined.yres
				  + vesafb_defined.lower_margin + vesafb_defined.vsync_len);
			if ((tmp < fh) || (fh == 0)) fh = tmp;
		}
		if (fh) {
			tmp = fh * (vesafb_defined.left_margin + vesafb_defined.xres
				  + vesafb_defined.right_margin + vesafb_defined.hsync_len);
			if ((tmp < maxclk) || (maxclk == 0)) maxclk = tmp;
		}
		tmp = (maxclk + 499) / 500;
		if (tmp) {
			tmp = (2000000000 + tmp) / tmp;
			if (tmp > pixclock) pixclock = tmp;
		}
	}
	if (pixclock) {
		if (pixclock < 2000)		/* > 500MHz */
			pixclock = 4000;	/* 250MHz */
		if (pixclock > 1000000)
			pixclock = 1000000;	/* 1MHz */
		vesafb_defined.pixclock = pixclock;
	}

	/* FIXME: Where to move this?! */
#if defined(CONFIG_PPC_PMAC)
#ifndef MODULE
	if (machine_is(powermac)) {
		struct fb_var_screeninfo var;

		if (default_vmode <= 0 || default_vmode > VMODE_MAX)
			default_vmode = VMODE_640_480_60;
#if defined(CONFIG_PPC32)
		if (IS_REACHABLE(CONFIG_NVRAM) && default_cmode == CMODE_NVRAM)
			default_cmode = nvram_read_byte(NV_CMODE);
#endif
		if (default_cmode < CMODE_8 || default_cmode > CMODE_32)
			default_cmode = CMODE_8;
		if (!mac_vmode_to_var(default_vmode, default_cmode, &var)) {
			var.accel_flags = vesafb_defined.accel_flags;
			var.xoffset = var.yoffset = 0;
			/* Note: mac_vmode_to_var() does not set all parameters */
			vesafb_defined = var;
		}
	}
#endif /* !MODULE */
#endif /* CONFIG_PPC_PMAC */
	vesafb_defined.xres_virtual = vesafb_defined.xres;
	if (nopan) {
		vesafb_defined.yres_virtual = vesafb_defined.yres;
	} else {
		vesafb_defined.yres_virtual = 65536; /* large enough to be INF, but small enough
							to yres_virtual * xres_virtual < 2^32 */
	}
	matroxfb_init_fix(minfo);
	minfo->fbcon.screen_base = vaddr_va(minfo->video.vbase);
	/* Normalize values (namely yres_virtual) */
	matroxfb_check_var(&vesafb_defined, &minfo->fbcon);
	/* And put it into "current" var. Do NOT program hardware yet, or we'll not take over
	 * vgacon correctly. fbcon_startup will call fb_set_par for us, WITHOUT check_var,
	 * and unfortunately it will do it BEFORE vgacon contents is saved, so it won't work
	 * anyway. But we at least tried... */
	minfo->fbcon.var = vesafb_defined;
	err = -EINVAL;

	printk(KERN_INFO "matroxfb: %dx%dx%dbpp (virtual: %dx%d)\n",
		vesafb_defined.xres, vesafb_defined.yres, vesafb_defined.bits_per_pixel,
		vesafb_defined.xres_virtual, vesafb_defined.yres_virtual);
	printk(KERN_INFO "matroxfb: framebuffer at 0x%lX, mapped to 0x%p, size %d\n",
		minfo->video.base, vaddr_va(minfo->video.vbase), minfo->video.len);

/* We do not have to set currcon to 0... register_framebuffer do it for us on first console
 * and we do not want currcon == 0 for subsequent framebuffers */

	minfo->fbcon.device = &minfo->pcidev->dev;
	if (register_framebuffer(&minfo->fbcon) < 0) {
		goto failVideoIO;
	}
	fb_info(&minfo->fbcon, "%s frame buffer device\n", minfo->fbcon.fix.id);

	/* there is no console on this fb... but we have to initialize hardware
	 * until someone tells me what is proper thing to do */
	if (!minfo->initialized) {
		fb_info(&minfo->fbcon, "initializing hardware\n");
		/* We have to use FB_ACTIVATE_FORCE, as we had to put vesafb_defined to the fbcon.var
		 * already before, so register_framebuffer works correctly. */
		vesafb_defined.activate |= FB_ACTIVATE_FORCE;
		fb_set_var(&minfo->fbcon, &vesafb_defined);
	}

	return 0;
failVideoIO:;
	matroxfb_g450_shutdown(minfo);
	iounmap(minfo->video.vbase.vaddr);
failCtrlIO:;
	iounmap(minfo->mmio.vbase.vaddr);
failVideoMR:;
	release_mem_region(video_base_phys, minfo->video.len_maximum);
failCtrlMR:;
	release_mem_region(ctrlptr_phys, 16384);
fail:;
	return err;
}

static LIST_HEAD(matroxfb_list);
static LIST_HEAD(matroxfb_driver_list);

#define matroxfb_l(x) list_entry(x, struct matrox_fb_info, next_fb)
#define matroxfb_driver_l(x) list_entry(x, struct matroxfb_driver, node)
int matroxfb_register_driver(struct matroxfb_driver* drv) {
	struct matrox_fb_info* minfo;

	list_add(&drv->node, &matroxfb_driver_list);
	for (minfo = matroxfb_l(matroxfb_list.next);
	     minfo != matroxfb_l(&matroxfb_list);
	     minfo = matroxfb_l(minfo->next_fb.next)) {
		void* p;

		if (minfo->drivers_count == MATROXFB_MAX_FB_DRIVERS)
			continue;
		p = drv->probe(minfo);
		if (p) {
			minfo->drivers_data[minfo->drivers_count] = p;
			minfo->drivers[minfo->drivers_count++] = drv;
		}
	}
	return 0;
}

void matroxfb_unregister_driver(struct matroxfb_driver* drv) {
	struct matrox_fb_info* minfo;

	list_del(&drv->node);
	for (minfo = matroxfb_l(matroxfb_list.next);
	     minfo != matroxfb_l(&matroxfb_list);
	     minfo = matroxfb_l(minfo->next_fb.next)) {
		int i;

		for (i = 0; i < minfo->drivers_count; ) {
			if (minfo->drivers[i] == drv) {
				if (drv && drv->remove)
					drv->remove(minfo, minfo->drivers_data[i]);
				minfo->drivers[i] = minfo->drivers[--minfo->drivers_count];
				minfo->drivers_data[i] = minfo->drivers_data[minfo->drivers_count];
			} else
				i++;
		}
	}
}

static void matroxfb_register_device(struct matrox_fb_info* minfo) {
	struct matroxfb_driver* drv;
	int i = 0;
	list_add(&minfo->next_fb, &matroxfb_list);
	for (drv = matroxfb_driver_l(matroxfb_driver_list.next);
	     drv != matroxfb_driver_l(&matroxfb_driver_list);
	     drv = matroxfb_driver_l(drv->node.next)) {
		if (drv->probe) {
			void *p = drv->probe(minfo);
			if (p) {
				minfo->drivers_data[i] = p;
				minfo->drivers[i++] = drv;
				if (i == MATROXFB_MAX_FB_DRIVERS)
					break;
			}
		}
	}
	minfo->drivers_count = i;
}

static void matroxfb_unregister_device(struct matrox_fb_info* minfo) {
	int i;

	list_del(&minfo->next_fb);
	for (i = 0; i < minfo->drivers_count; i++) {
		struct matroxfb_driver* drv = minfo->drivers[i];

		if (drv && drv->remove)
			drv->remove(minfo, minfo->drivers_data[i]);
	}
}

static int matroxfb_probe(struct pci_dev* pdev, const struct pci_device_id* dummy) {
	struct board* b;
	u_int16_t svid;
	u_int16_t sid;
	struct matrox_fb_info* minfo;
	int err;
	u_int32_t cmd;
	DBG(__func__)

	svid = pdev->subsystem_vendor;
	sid = pdev->subsystem_device;
	for (b = dev_list; b->vendor; b++) {
		if ((b->vendor != pdev->vendor) || (b->device != pdev->device) || (b->rev < pdev->revision)) continue;
		if (b->svid)
			if ((b->svid != svid) || (b->sid != sid)) continue;
		break;
	}
	/* not match... */
	if (!b->vendor)
		return -ENODEV;
	if (dev > 0) {
		/* not requested one... */
		dev--;
		return -ENODEV;
	}
	pci_read_config_dword(pdev, PCI_COMMAND, &cmd);
	if (pci_enable_device(pdev)) {
		return -1;
	}

	minfo = kzalloc(sizeof(*minfo), GFP_KERNEL);
	if (!minfo)
		return -ENOMEM;

	minfo->pcidev = pdev;
	minfo->dead = 0;
	minfo->usecount = 0;
	minfo->userusecount = 0;

	pci_set_drvdata(pdev, minfo);
	/* DEVFLAGS */
	minfo->devflags.memtype = memtype;
	if (memtype != -1)
		noinit = 0;
	if (cmd & PCI_COMMAND_MEMORY) {
		minfo->devflags.novga = novga;
		minfo->devflags.nobios = nobios;
		minfo->devflags.noinit = noinit;
		/* subsequent heads always needs initialization and must not enable BIOS */
		novga = 1;
		nobios = 1;
		noinit = 0;
	} else {
		minfo->devflags.novga = 1;
		minfo->devflags.nobios = 1;
		minfo->devflags.noinit = 0;
	}

	minfo->devflags.nopciretry = no_pci_retry;
	minfo->devflags.mga_24bpp_fix = inv24;
	minfo->devflags.precise_width = option_precise_width;
	minfo->devflags.sgram = sgram;
	minfo->capable.cross4MB = cross4MB;

	spin_lock_init(&minfo->lock.DAC);
	spin_lock_init(&minfo->lock.accel);
	init_rwsem(&minfo->crtc2.lock);
	init_rwsem(&minfo->altout.lock);
	mutex_init(&minfo->fbcon.mm_lock);
	minfo->irq_flags = 0;
	init_waitqueue_head(&minfo->crtc1.vsync.wait);
	init_waitqueue_head(&minfo->crtc2.vsync.wait);
	minfo->crtc1.panpos = -1;

	err = initMatrox2(minfo, b);
	if (!err) {
		matroxfb_register_device(minfo);
		return 0;
	}
	kfree(minfo);
	return -1;
}

static void pci_remove_matrox(struct pci_dev* pdev) {
	struct matrox_fb_info* minfo;

	minfo = pci_get_drvdata(pdev);
	matroxfb_remove(minfo, 1);
}

static const struct pci_device_id matroxfb_devices[] = {
#ifdef CONFIG_FB_MATROX_MILLENIUM
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MIL,
		PCI_ANY_ID,	PCI_ANY_ID,	0, 0, 0},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MIL_2,
		PCI_ANY_ID,	PCI_ANY_ID,	0, 0, 0},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MIL_2_AGP,
		PCI_ANY_ID,	PCI_ANY_ID,	0, 0, 0},
#endif
#ifdef CONFIG_FB_MATROX_MYSTIQUE
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MYS,
		PCI_ANY_ID,	PCI_ANY_ID,	0, 0, 0},
#endif
#ifdef CONFIG_FB_MATROX_G
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G100_MM,
		PCI_ANY_ID,	PCI_ANY_ID,	0, 0, 0},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G100_AGP,
		PCI_ANY_ID,	PCI_ANY_ID,	0, 0, 0},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_PCI,
		PCI_ANY_ID,	PCI_ANY_ID,	0, 0, 0},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_AGP,
		PCI_ANY_ID,	PCI_ANY_ID,	0, 0, 0},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G400,
		PCI_ANY_ID,	PCI_ANY_ID,	0, 0, 0},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G550,
		PCI_ANY_ID,	PCI_ANY_ID,	0, 0, 0},
#endif
	{0,			0,
		0,		0,		0, 0, 0}
};

MODULE_DEVICE_TABLE(pci, matroxfb_devices);


static struct pci_driver matroxfb_driver = {
	.name =		"matroxfb",
	.id_table =	matroxfb_devices,
	.probe =	matroxfb_probe,
	.remove =	pci_remove_matrox,
};

/* **************************** init-time only **************************** */

#define RSResolution(X)	((X) & 0x0F)
#define RS640x400	1
#define RS640x480	2
#define RS800x600	3
#define RS1024x768	4
#define RS1280x1024	5
#define RS1600x1200	6
#define RS768x576	7
#define RS960x720	8
#define RS1152x864	9
#define RS1408x1056	10
#define RS640x350	11
#define RS1056x344	12	/* 132 x 43 text */
#define RS1056x400	13	/* 132 x 50 text */
#define RS1056x480	14	/* 132 x 60 text */
#define RSNoxNo		15
/* 10-FF */
static struct { int xres, yres, left, right, upper, lower, hslen, vslen, vfreq; } timmings[] __initdata = {
	{  640,  400,  48, 16, 39,  8,  96, 2, 70 },
	{  640,  480,  48, 16, 33, 10,  96, 2, 60 },
	{  800,  600, 144, 24, 28,  8, 112, 6, 60 },
	{ 1024,  768, 160, 32, 30,  4, 128, 4, 60 },
	{ 1280, 1024, 224, 32, 32,  4, 136, 4, 60 },
	{ 1600, 1200, 272, 48, 32,  5, 152, 5, 60 },
	{  768,  576, 144, 16, 28,  6, 112, 4, 60 },
	{  960,  720, 144, 24, 28,  8, 112, 4, 60 },
	{ 1152,  864, 192, 32, 30,  4, 128, 4, 60 },
	{ 1408, 1056, 256, 40, 32,  5, 144, 5, 60 },
	{  640,  350,  48, 16, 39,  8,  96, 2, 70 },
	{ 1056,  344,  96, 24, 59, 44, 160, 2, 70 },
	{ 1056,  400,  96, 24, 39,  8, 160, 2, 70 },
	{ 1056,  480,  96, 24, 36, 12, 160, 3, 60 },
	{    0,    0,  ~0, ~0, ~0, ~0,   0, 0,  0 }
};

#define RSCreate(X,Y)	((X) | ((Y) << 8))
static struct { unsigned int vesa; unsigned int info; } *RSptr, vesamap[] __initdata = {
/* default must be first */
	{    ~0, RSCreate(RSNoxNo,     RS8bpp ) },
	{ 0x101, RSCreate(RS640x480,   RS8bpp ) },
	{ 0x100, RSCreate(RS640x400,   RS8bpp ) },
	{ 0x180, RSCreate(RS768x576,   RS8bpp ) },
	{ 0x103, RSCreate(RS800x600,   RS8bpp ) },
	{ 0x188, RSCreate(RS960x720,   RS8bpp ) },
	{ 0x105, RSCreate(RS1024x768,  RS8bpp ) },
	{ 0x190, RSCreate(RS1152x864,  RS8bpp ) },
	{ 0x107, RSCreate(RS1280x1024, RS8bpp ) },
	{ 0x198, RSCreate(RS1408x1056, RS8bpp ) },
	{ 0x11C, RSCreate(RS1600x1200, RS8bpp ) },
	{ 0x110, RSCreate(RS640x480,   RS15bpp) },
	{ 0x181, RSCreate(RS768x576,   RS15bpp) },
	{ 0x113, RSCreate(RS800x600,   RS15bpp) },
	{ 0x189, RSCreate(RS960x720,   RS15bpp) },
	{ 0x116, RSCreate(RS1024x768,  RS15bpp) },
	{ 0x191, RSCreate(RS1152x864,  RS15bpp) },
	{ 0x119, RSCreate(RS1280x1024, RS15bpp) },
	{ 0x199, RSCreate(RS1408x1056, RS15bpp) },
	{ 0x11D, RSCreate(RS1600x1200, RS15bpp) },
	{ 0x111, RSCreate(RS640x480,   RS16bpp) },
	{ 0x182, RSCreate(RS768x576,   RS16bpp) },
	{ 0x114, RSCreate(RS800x600,   RS16bpp) },
	{ 0x18A, RSCreate(RS960x720,   RS16bpp) },
	{ 0x117, RSCreate(RS1024x768,  RS16bpp) },
	{ 0x192, RSCreate(RS1152x864,  RS16bpp) },
	{ 0x11A, RSCreate(RS1280x1024, RS16bpp) },
	{ 0x19A, RSCreate(RS1408x1056, RS16bpp) },
	{ 0x11E, RSCreate(RS1600x1200, RS16bpp) },
	{ 0x1B2, RSCreate(RS640x480,   RS24bpp) },
	{ 0x184, RSCreate(RS768x576,   RS24bpp) },
	{ 0x1B5, RSCreate(RS800x600,   RS24bpp) },
	{ 0x18C, RSCreate(RS960x720,   RS24bpp) },
	{ 0x1B8, RSCreate(RS1024x768,  RS24bpp) },
	{ 0x194, RSCreate(RS1152x864,  RS24bpp) },
	{ 0x1BB, RSCreate(RS1280x1024, RS24bpp) },
	{ 0x19C, RSCreate(RS1408x1056, RS24bpp) },
	{ 0x1BF, RSCreate(RS1600x1200, RS24bpp) },
	{ 0x112, RSCreate(RS640x480,   RS32bpp) },
	{ 0x183, RSCreate(RS768x576,   RS32bpp) },
	{ 0x115, RSCreate(RS800x600,   RS32bpp) },
	{ 0x18B, RSCreate(RS960x720,   RS32bpp) },
	{ 0x118, RSCreate(RS1024x768,  RS32bpp) },
	{ 0x193, RSCreate(RS1152x864,  RS32bpp) },
	{ 0x11B, RSCreate(RS1280x1024, RS32bpp) },
	{ 0x19B, RSCreate(RS1408x1056, RS32bpp) },
	{ 0x11F, RSCreate(RS1600x1200, RS32bpp) },
	{ 0x010, RSCreate(RS640x350,   RS4bpp ) },
	{ 0x012, RSCreate(RS640x480,   RS4bpp ) },
	{ 0x102, RSCreate(RS800x600,   RS4bpp ) },
	{ 0x104, RSCreate(RS1024x768,  RS4bpp ) },
	{ 0x106, RSCreate(RS1280x1024, RS4bpp ) },
	{     0, 0				}};

static void __init matroxfb_init_params(void) {
	/* fh from kHz to Hz */
	if (fh < 1000)
		fh *= 1000;	/* 1kHz minimum */
	/* maxclk */
	if (maxclk < 1000) maxclk *= 1000;	/* kHz -> Hz, MHz -> kHz */
	if (maxclk < 1000000) maxclk *= 1000;	/* kHz -> Hz, 1MHz minimum */
	/* fix VESA number */
	if (vesa != ~0)
		vesa &= 0x1DFF;		/* mask out clearscreen, acceleration and so on */

	/* static settings */
	for (RSptr = vesamap; RSptr->vesa; RSptr++) {
		if (RSptr->vesa == vesa) break;
	}
	if (!RSptr->vesa) {
		printk(KERN_ERR "Invalid vesa mode 0x%04X\n", vesa);
		RSptr = vesamap;
	}
	{
		int res = RSResolution(RSptr->info)-1;
		if (left == ~0)
			left = timmings[res].left;
		if (!xres)
			xres = timmings[res].xres;
		if (right == ~0)
			right = timmings[res].right;
		if (!hslen)
			hslen = timmings[res].hslen;
		if (upper == ~0)
			upper = timmings[res].upper;
		if (!yres)
			yres = timmings[res].yres;
		if (lower == ~0)
			lower = timmings[res].lower;
		if (!vslen)
			vslen = timmings[res].vslen;
		if (!(fv||fh||maxclk||pixclock))
			fv = timmings[res].vfreq;
		if (depth == -1)
			depth = RSDepth(RSptr->info);
	}
}

static int __init matrox_init(void) {
	int err;

	matroxfb_init_params();
	err = pci_register_driver(&matroxfb_driver);
	dev = -1;	/* accept all new devices... */
	return err;
}

/* **************************** exit-time only **************************** */

static void __exit matrox_done(void) {
	pci_unregister_driver(&matroxfb_driver);
}

#ifndef MODULE

/* ************************* init in-kernel code ************************** */

static int __init matroxfb_setup(char *options) {
	char *this_opt;

	DBG(__func__)

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt) continue;

		dprintk("matroxfb_setup: option %s\n", this_opt);

		if (!strncmp(this_opt, "dev:", 4))
			dev = simple_strtoul(this_opt+4, NULL, 0);
		else if (!strncmp(this_opt, "depth:", 6)) {
			switch (simple_strtoul(this_opt+6, NULL, 0)) {
				case 0: depth = RSText; break;
				case 4: depth = RS4bpp; break;
				case 8: depth = RS8bpp; break;
				case 15:depth = RS15bpp; break;
				case 16:depth = RS16bpp; break;
				case 24:depth = RS24bpp; break;
				case 32:depth = RS32bpp; break;
				default:
					printk(KERN_ERR "matroxfb: unsupported color depth\n");
			}
		} else if (!strncmp(this_opt, "xres:", 5))
			xres = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strncmp(this_opt, "yres:", 5))
			yres = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strncmp(this_opt, "vslen:", 6))
			vslen = simple_strtoul(this_opt+6, NULL, 0);
		else if (!strncmp(this_opt, "hslen:", 6))
			hslen = simple_strtoul(this_opt+6, NULL, 0);
		else if (!strncmp(this_opt, "left:", 5))
			left = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strncmp(this_opt, "right:", 6))
			right = simple_strtoul(this_opt+6, NULL, 0);
		else if (!strncmp(this_opt, "upper:", 6))
			upper = simple_strtoul(this_opt+6, NULL, 0);
		else if (!strncmp(this_opt, "lower:", 6))
			lower = simple_strtoul(this_opt+6, NULL, 0);
		else if (!strncmp(this_opt, "pixclock:", 9))
			pixclock = simple_strtoul(this_opt+9, NULL, 0);
		else if (!strncmp(this_opt, "sync:", 5))
			sync = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strncmp(this_opt, "vesa:", 5))
			vesa = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strncmp(this_opt, "maxclk:", 7))
			maxclk = simple_strtoul(this_opt+7, NULL, 0);
		else if (!strncmp(this_opt, "fh:", 3))
			fh = simple_strtoul(this_opt+3, NULL, 0);
		else if (!strncmp(this_opt, "fv:", 3))
			fv = simple_strtoul(this_opt+3, NULL, 0);
		else if (!strncmp(this_opt, "mem:", 4))
			mem = simple_strtoul(this_opt+4, NULL, 0);
		else if (!strncmp(this_opt, "mode:", 5))
			strlcpy(videomode, this_opt+5, sizeof(videomode));
		else if (!strncmp(this_opt, "outputs:", 8))
			strlcpy(outputs, this_opt+8, sizeof(outputs));
		else if (!strncmp(this_opt, "dfp:", 4)) {
			dfp_type = simple_strtoul(this_opt+4, NULL, 0);
			dfp = 1;
		}
#ifdef CONFIG_PPC_PMAC
		else if (!strncmp(this_opt, "vmode:", 6)) {
			unsigned int vmode = simple_strtoul(this_opt+6, NULL, 0);
			if (vmode > 0 && vmode <= VMODE_MAX)
				default_vmode = vmode;
		} else if (!strncmp(this_opt, "cmode:", 6)) {
			unsigned int cmode = simple_strtoul(this_opt+6, NULL, 0);
			switch (cmode) {
				case 0:
				case 8:
					default_cmode = CMODE_8;
					break;
				case 15:
				case 16:
					default_cmode = CMODE_16;
					break;
				case 24:
				case 32:
					default_cmode = CMODE_32;
					break;
			}
		}
#endif
		else if (!strcmp(this_opt, "disabled"))	/* nodisabled does not exist */
			disabled = 1;
		else if (!strcmp(this_opt, "enabled"))	/* noenabled does not exist */
			disabled = 0;
		else if (!strcmp(this_opt, "sgram"))	/* nosgram == sdram */
			sgram = 1;
		else if (!strcmp(this_opt, "sdram"))
			sgram = 0;
		else if (!strncmp(this_opt, "memtype:", 8))
			memtype = simple_strtoul(this_opt+8, NULL, 0);
		else {
			int value = 1;

			if (!strncmp(this_opt, "no", 2)) {
				value = 0;
				this_opt += 2;
			}
			if (! strcmp(this_opt, "inverse"))
				inverse = value;
			else if (!strcmp(this_opt, "accel"))
				noaccel = !value;
			else if (!strcmp(this_opt, "pan"))
				nopan = !value;
			else if (!strcmp(this_opt, "pciretry"))
				no_pci_retry = !value;
			else if (!strcmp(this_opt, "vga"))
				novga = !value;
			else if (!strcmp(this_opt, "bios"))
				nobios = !value;
			else if (!strcmp(this_opt, "init"))
				noinit = !value;
			else if (!strcmp(this_opt, "mtrr"))
				mtrr = value;
			else if (!strcmp(this_opt, "inv24"))
				inv24 = value;
			else if (!strcmp(this_opt, "cross4MB"))
				cross4MB = value;
			else if (!strcmp(this_opt, "grayscale"))
				grayscale = value;
			else if (!strcmp(this_opt, "dfp"))
				dfp = value;
			else {
				strlcpy(videomode, this_opt, sizeof(videomode));
			}
		}
	}
	return 0;
}

static int __initdata initialized = 0;

static int __init matroxfb_init(void)
{
	char *option = NULL;
	int err = 0;

	DBG(__func__)

	if (fb_get_options("matroxfb", &option))
		return -ENODEV;
	matroxfb_setup(option);

	if (disabled)
		return -ENXIO;
	if (!initialized) {
		initialized = 1;
		err = matrox_init();
	}
	hotplug = 1;
	/* never return failure, user can hotplug matrox later... */
	return err;
}

module_init(matroxfb_init);

#else

/* *************************** init module code **************************** */

MODULE_AUTHOR("(c) 1998-2002 Petr Vandrovec <vandrove@vc.cvut.cz>");
MODULE_DESCRIPTION("Accelerated FBDev driver for Matrox Millennium/Mystique/G100/G200/G400/G450/G550");
MODULE_LICENSE("GPL");

module_param(mem, int, 0);
MODULE_PARM_DESC(mem, "Size of available memory in MB, KB or B (2,4,8,12,16MB, default=autodetect)");
module_param(disabled, int, 0);
MODULE_PARM_DESC(disabled, "Disabled (0 or 1=disabled) (default=0)");
module_param(noaccel, int, 0);
MODULE_PARM_DESC(noaccel, "Do not use accelerating engine (0 or 1=disabled) (default=0)");
module_param(nopan, int, 0);
MODULE_PARM_DESC(nopan, "Disable pan on startup (0 or 1=disabled) (default=0)");
module_param(no_pci_retry, int, 0);
MODULE_PARM_DESC(no_pci_retry, "PCI retries enabled (0 or 1=disabled) (default=0)");
module_param(novga, int, 0);
MODULE_PARM_DESC(novga, "VGA I/O (0x3C0-0x3DF) disabled (0 or 1=disabled) (default=0)");
module_param(nobios, int, 0);
MODULE_PARM_DESC(nobios, "Disables ROM BIOS (0 or 1=disabled) (default=do not change BIOS state)");
module_param(noinit, int, 0);
MODULE_PARM_DESC(noinit, "Disables W/SG/SD-RAM and bus interface initialization (0 or 1=do not initialize) (default=0)");
module_param(memtype, int, 0);
MODULE_PARM_DESC(memtype, "Memory type for G200/G400 (see Documentation/fb/matroxfb.rst for explanation) (default=3 for G200, 0 for G400)");
module_param(mtrr, int, 0);
MODULE_PARM_DESC(mtrr, "This speeds up video memory accesses (0=disabled or 1) (default=1)");
module_param(sgram, int, 0);
MODULE_PARM_DESC(sgram, "Indicates that G100/G200/G400 has SGRAM memory (0=SDRAM, 1=SGRAM) (default=0)");
module_param(inv24, int, 0);
MODULE_PARM_DESC(inv24, "Inverts clock polarity for 24bpp and loop frequency > 100MHz (default=do not invert polarity)");
module_param(inverse, int, 0);
MODULE_PARM_DESC(inverse, "Inverse (0 or 1) (default=0)");
module_param(dev, int, 0);
MODULE_PARM_DESC(dev, "Multihead support, attach to device ID (0..N) (default=all working)");
module_param(vesa, int, 0);
MODULE_PARM_DESC(vesa, "Startup videomode (0x000-0x1FF) (default=0x101)");
module_param(xres, int, 0);
MODULE_PARM_DESC(xres, "Horizontal resolution (px), overrides xres from vesa (default=vesa)");
module_param(yres, int, 0);
MODULE_PARM_DESC(yres, "Vertical resolution (scans), overrides yres from vesa (default=vesa)");
module_param(upper, int, 0);
MODULE_PARM_DESC(upper, "Upper blank space (scans), overrides upper from vesa (default=vesa)");
module_param(lower, int, 0);
MODULE_PARM_DESC(lower, "Lower blank space (scans), overrides lower from vesa (default=vesa)");
module_param(vslen, int, 0);
MODULE_PARM_DESC(vslen, "Vertical sync length (scans), overrides lower from vesa (default=vesa)");
module_param(left, int, 0);
MODULE_PARM_DESC(left, "Left blank space (px), overrides left from vesa (default=vesa)");
module_param(right, int, 0);
MODULE_PARM_DESC(right, "Right blank space (px), overrides right from vesa (default=vesa)");
module_param(hslen, int, 0);
MODULE_PARM_DESC(hslen, "Horizontal sync length (px), overrides hslen from vesa (default=vesa)");
module_param(pixclock, int, 0);
MODULE_PARM_DESC(pixclock, "Pixelclock (ns), overrides pixclock from vesa (default=vesa)");
module_param(sync, int, 0);
MODULE_PARM_DESC(sync, "Sync polarity, overrides sync from vesa (default=vesa)");
module_param(depth, int, 0);
MODULE_PARM_DESC(depth, "Color depth (0=text,8,15,16,24,32) (default=vesa)");
module_param(maxclk, int, 0);
MODULE_PARM_DESC(maxclk, "Startup maximal clock, 0-999MHz, 1000-999999kHz, 1000000-INF Hz");
module_param(fh, int, 0);
MODULE_PARM_DESC(fh, "Startup horizontal frequency, 0-999kHz, 1000-INF Hz");
module_param(fv, int, 0);
MODULE_PARM_DESC(fv, "Startup vertical frequency, 0-INF Hz\n"
"You should specify \"fv:max_monitor_vsync,fh:max_monitor_hsync,maxclk:max_monitor_dotclock\"");
module_param(grayscale, int, 0);
MODULE_PARM_DESC(grayscale, "Sets display into grayscale. Works perfectly with paletized videomode (4, 8bpp), some limitations apply to 16, 24 and 32bpp videomodes (default=nograyscale)");
module_param(cross4MB, int, 0);
MODULE_PARM_DESC(cross4MB, "Specifies that 4MB boundary can be in middle of line. (default=autodetected)");
module_param(dfp, int, 0);
MODULE_PARM_DESC(dfp, "Specifies whether to use digital flat panel interface of G200/G400 (0 or 1) (default=0)");
module_param(dfp_type, int, 0);
MODULE_PARM_DESC(dfp_type, "Specifies DFP interface type (0 to 255) (default=read from hardware)");
module_param_string(outputs, outputs, sizeof(outputs), 0);
MODULE_PARM_DESC(outputs, "Specifies which CRTC is mapped to which output (string of up to three letters, consisting of 0 (disabled), 1 (CRTC1), 2 (CRTC2)) (default=111 for Gx50, 101 for G200/G400 with DFP, and 100 for all other devices)");
#ifdef CONFIG_PPC_PMAC
module_param_named(vmode, default_vmode, int, 0);
MODULE_PARM_DESC(vmode, "Specify the vmode mode number that should be used (640x480 default)");
module_param_named(cmode, default_cmode, int, 0);
MODULE_PARM_DESC(cmode, "Specify the video depth that should be used (8bit default)");
#endif

int __init init_module(void){

	DBG(__func__)

	if (disabled)
		return -ENXIO;

	if (depth == 0)
		depth = RSText;
	else if (depth == 4)
		depth = RS4bpp;
	else if (depth == 8)
		depth = RS8bpp;
	else if (depth == 15)
		depth = RS15bpp;
	else if (depth == 16)
		depth = RS16bpp;
	else if (depth == 24)
		depth = RS24bpp;
	else if (depth == 32)
		depth = RS32bpp;
	else if (depth != -1) {
		printk(KERN_ERR "matroxfb: depth %d is not supported, using default\n", depth);
		depth = -1;
	}
	matrox_init();
	/* never return failure; user can hotplug matrox later... */
	return 0;
}
#endif	/* MODULE */

module_exit(matrox_done);
EXPORT_SYMBOL(matroxfb_register_driver);
EXPORT_SYMBOL(matroxfb_unregister_driver);
EXPORT_SYMBOL(matroxfb_wait_for_sync);
EXPORT_SYMBOL(matroxfb_enable_irq);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

