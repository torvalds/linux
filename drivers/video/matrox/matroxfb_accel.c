/*
 *
 * Hardware accelerated Matrox Millennium I, II, Mystique, G100, G200 and G400
 *
 * (c) 1998-2002 Petr Vandrovec <vandrove@vc.cvut.cz>
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
 *  were used when writting this driver)
 *
 *		 FreeVBE/AF (Matrox), "Shawn Hargreaves" <shawn@talula.demon.co.uk>
 *
 */

#include "matroxfb_accel.h"
#include "matroxfb_DAC1064.h"
#include "matroxfb_Ti3026.h"
#include "matroxfb_misc.h"

#define curr_ydstorg(x)	ACCESS_FBINFO2(x, curr.ydstorg.pixels)

#define mga_ydstlen(y,l) mga_outl(M_YDSTLEN | M_EXEC, ((y) << 16) | (l))

static inline void matrox_cfb4_pal(u_int32_t* pal) {
	unsigned int i;
	
	for (i = 0; i < 16; i++) {
		pal[i] = i * 0x11111111U;
	}
	pal[i] = 0xFFFFFFFF;
}

static inline void matrox_cfb8_pal(u_int32_t* pal) {
	unsigned int i;
	
	for (i = 0; i < 16; i++) {
		pal[i] = i * 0x01010101U;
	}
	pal[i] = 0x0F0F0F0F;
}

static void matroxfb_copyarea(struct fb_info* info, const struct fb_copyarea* area);
static void matroxfb_fillrect(struct fb_info* info, const struct fb_fillrect* rect);
static void matroxfb_imageblit(struct fb_info* info, const struct fb_image* image);
static void matroxfb_cfb4_fillrect(struct fb_info* info, const struct fb_fillrect* rect);
static void matroxfb_cfb4_copyarea(struct fb_info* info, const struct fb_copyarea* area);

void matrox_cfbX_init(WPMINFO2) {
	u_int32_t maccess;
	u_int32_t mpitch;
	u_int32_t mopmode;
	int accel;

	DBG(__FUNCTION__)

	mpitch = ACCESS_FBINFO(fbcon).var.xres_virtual;

	ACCESS_FBINFO(fbops).fb_copyarea = cfb_copyarea;
	ACCESS_FBINFO(fbops).fb_fillrect = cfb_fillrect;
	ACCESS_FBINFO(fbops).fb_imageblit = cfb_imageblit;
	ACCESS_FBINFO(fbops).fb_cursor = NULL;

	accel = (ACCESS_FBINFO(fbcon).var.accel_flags & FB_ACCELF_TEXT) == FB_ACCELF_TEXT;

	switch (ACCESS_FBINFO(fbcon).var.bits_per_pixel) {
		case 4:		maccess = 0x00000000;	/* accelerate as 8bpp video */
				mpitch = (mpitch >> 1) | 0x8000; /* disable linearization */
				mopmode = M_OPMODE_4BPP;
				matrox_cfb4_pal(ACCESS_FBINFO(cmap));
				if (accel && !(mpitch & 1)) {
					ACCESS_FBINFO(fbops).fb_copyarea = matroxfb_cfb4_copyarea;
					ACCESS_FBINFO(fbops).fb_fillrect = matroxfb_cfb4_fillrect;
				}
				break;
		case 8:		maccess = 0x00000000;
				mopmode = M_OPMODE_8BPP;
				matrox_cfb8_pal(ACCESS_FBINFO(cmap));
				if (accel) {
					ACCESS_FBINFO(fbops).fb_copyarea = matroxfb_copyarea;
					ACCESS_FBINFO(fbops).fb_fillrect = matroxfb_fillrect;
					ACCESS_FBINFO(fbops).fb_imageblit = matroxfb_imageblit;
				}
				break;
		case 16:	if (ACCESS_FBINFO(fbcon).var.green.length == 5) {
					maccess = 0xC0000001;
					ACCESS_FBINFO(cmap[16]) = 0x7FFF7FFF;
				} else {
					maccess = 0x40000001;
					ACCESS_FBINFO(cmap[16]) = 0xFFFFFFFF;
				}
				mopmode = M_OPMODE_16BPP;
				if (accel) {
					ACCESS_FBINFO(fbops).fb_copyarea = matroxfb_copyarea;
					ACCESS_FBINFO(fbops).fb_fillrect = matroxfb_fillrect;
					ACCESS_FBINFO(fbops).fb_imageblit = matroxfb_imageblit;
				}
				break;
		case 24:	maccess = 0x00000003;
				mopmode = M_OPMODE_24BPP;
				ACCESS_FBINFO(cmap[16]) = 0xFFFFFFFF;
				if (accel) {
					ACCESS_FBINFO(fbops).fb_copyarea = matroxfb_copyarea;
					ACCESS_FBINFO(fbops).fb_fillrect = matroxfb_fillrect;
					ACCESS_FBINFO(fbops).fb_imageblit = matroxfb_imageblit;
				}
				break;
		case 32:	maccess = 0x00000002;
				mopmode = M_OPMODE_32BPP;
				ACCESS_FBINFO(cmap[16]) = 0xFFFFFFFF;
				if (accel) {
					ACCESS_FBINFO(fbops).fb_copyarea = matroxfb_copyarea;
					ACCESS_FBINFO(fbops).fb_fillrect = matroxfb_fillrect;
					ACCESS_FBINFO(fbops).fb_imageblit = matroxfb_imageblit;
				}
				break;
		default:	maccess = 0x00000000;
				mopmode = 0x00000000;
				break;	/* turn off acceleration!!! */
	}
	mga_fifo(8);
	mga_outl(M_PITCH, mpitch);
	mga_outl(M_YDSTORG, curr_ydstorg(MINFO));
	if (ACCESS_FBINFO(capable.plnwt))
		mga_outl(M_PLNWT, -1);
	if (ACCESS_FBINFO(capable.srcorg)) {
		mga_outl(M_SRCORG, 0);
		mga_outl(M_DSTORG, 0);
	}
	mga_outl(M_OPMODE, mopmode);
	mga_outl(M_CXBNDRY, 0xFFFF0000);
	mga_outl(M_YTOP, 0);
	mga_outl(M_YBOT, 0x01FFFFFF);
	mga_outl(M_MACCESS, maccess);
	ACCESS_FBINFO(accel.m_dwg_rect) = M_DWG_TRAP | M_DWG_SOLID | M_DWG_ARZERO | M_DWG_SGNZERO | M_DWG_SHIFTZERO;
	if (isMilleniumII(MINFO)) ACCESS_FBINFO(accel.m_dwg_rect) |= M_DWG_TRANSC;
	ACCESS_FBINFO(accel.m_opmode) = mopmode;
}

EXPORT_SYMBOL(matrox_cfbX_init);

static void matrox_accel_bmove(WPMINFO int vxres, int sy, int sx, int dy, int dx, int height, int width) {
	int start, end;
	CRITFLAGS

	DBG(__FUNCTION__)

	CRITBEGIN

	if ((dy < sy) || ((dy == sy) && (dx <= sx))) {
		mga_fifo(2);
		mga_outl(M_DWGCTL, M_DWG_BITBLT | M_DWG_SHIFTZERO | M_DWG_SGNZERO |
			 M_DWG_BFCOL | M_DWG_REPLACE);
		mga_outl(M_AR5, vxres);
		width--;
		start = sy*vxres+sx+curr_ydstorg(MINFO);
		end = start+width;
	} else {
		mga_fifo(3);
		mga_outl(M_DWGCTL, M_DWG_BITBLT | M_DWG_SHIFTZERO | M_DWG_BFCOL | M_DWG_REPLACE);
		mga_outl(M_SGN, 5);
		mga_outl(M_AR5, -vxres);
		width--;
		end = (sy+height-1)*vxres+sx+curr_ydstorg(MINFO);
		start = end+width;
		dy += height-1;
	}
	mga_fifo(4);
	mga_outl(M_AR0, end);
	mga_outl(M_AR3, start);
	mga_outl(M_FXBNDRY, ((dx+width)<<16) | dx);
	mga_ydstlen(dy, height);
	WaitTillIdle();

	CRITEND
}

static void matrox_accel_bmove_lin(WPMINFO int vxres, int sy, int sx, int dy, int dx, int height, int width) {
	int start, end;
	CRITFLAGS

	DBG(__FUNCTION__)

	CRITBEGIN

	if ((dy < sy) || ((dy == sy) && (dx <= sx))) {
		mga_fifo(2);
		mga_outl(M_DWGCTL, M_DWG_BITBLT | M_DWG_SHIFTZERO | M_DWG_SGNZERO |
			M_DWG_BFCOL | M_DWG_REPLACE);
		mga_outl(M_AR5, vxres);
		width--;
		start = sy*vxres+sx+curr_ydstorg(MINFO);
		end = start+width;
	} else {
		mga_fifo(3);
		mga_outl(M_DWGCTL, M_DWG_BITBLT | M_DWG_SHIFTZERO | M_DWG_BFCOL | M_DWG_REPLACE);
		mga_outl(M_SGN, 5);
		mga_outl(M_AR5, -vxres);
		width--;
		end = (sy+height-1)*vxres+sx+curr_ydstorg(MINFO);
		start = end+width;
		dy += height-1;
	}
	mga_fifo(5);
	mga_outl(M_AR0, end);
	mga_outl(M_AR3, start);
	mga_outl(M_FXBNDRY, ((dx+width)<<16) | dx);
	mga_outl(M_YDST, dy*vxres >> 5);
	mga_outl(M_LEN | M_EXEC, height);
	WaitTillIdle();

	CRITEND
}

static void matroxfb_cfb4_copyarea(struct fb_info* info, const struct fb_copyarea* area) {
	MINFO_FROM_INFO(info);

	if ((area->sx | area->dx | area->width) & 1)
		cfb_copyarea(info, area);
	else
		matrox_accel_bmove_lin(PMINFO ACCESS_FBINFO(fbcon.var.xres_virtual) >> 1, area->sy, area->sx >> 1, area->dy, area->dx >> 1, area->height, area->width >> 1);
}

static void matroxfb_copyarea(struct fb_info* info, const struct fb_copyarea* area) {
	MINFO_FROM_INFO(info);

	matrox_accel_bmove(PMINFO ACCESS_FBINFO(fbcon.var.xres_virtual), area->sy, area->sx, area->dy, area->dx, area->height, area->width);
}

static void matroxfb_accel_clear(WPMINFO u_int32_t color, int sy, int sx, int height,
		int width) {
	CRITFLAGS

	DBG(__FUNCTION__)

	CRITBEGIN

	mga_fifo(5);
	mga_outl(M_DWGCTL, ACCESS_FBINFO(accel.m_dwg_rect) | M_DWG_REPLACE);
	mga_outl(M_FCOL, color);
	mga_outl(M_FXBNDRY, ((sx + width) << 16) | sx);
	mga_ydstlen(sy, height);
	WaitTillIdle();

	CRITEND
}

static void matroxfb_fillrect(struct fb_info* info, const struct fb_fillrect* rect) {
	MINFO_FROM_INFO(info);

	switch (rect->rop) {
		case ROP_COPY:
			matroxfb_accel_clear(PMINFO ((u_int32_t*)info->pseudo_palette)[rect->color], rect->dy, rect->dx, rect->height, rect->width);
			break;
	}
}

static void matroxfb_cfb4_clear(WPMINFO u_int32_t bgx, int sy, int sx, int height, int width) {
	int whattodo;
	CRITFLAGS

	DBG(__FUNCTION__)

	CRITBEGIN

	whattodo = 0;
	if (sx & 1) {
		sx ++;
		if (!width) return;
		width --;
		whattodo = 1;
	}
	if (width & 1) {
		whattodo |= 2;
	}
	width >>= 1;
	sx >>= 1;
	if (width) {
		mga_fifo(5);
		mga_outl(M_DWGCTL, ACCESS_FBINFO(accel.m_dwg_rect) | M_DWG_REPLACE2);
		mga_outl(M_FCOL, bgx);
		mga_outl(M_FXBNDRY, ((sx + width) << 16) | sx);
		mga_outl(M_YDST, sy * ACCESS_FBINFO(fbcon).var.xres_virtual >> 6);
		mga_outl(M_LEN | M_EXEC, height);
		WaitTillIdle();
	}
	if (whattodo) {
		u_int32_t step = ACCESS_FBINFO(fbcon).var.xres_virtual >> 1;
		vaddr_t vbase = ACCESS_FBINFO(video.vbase);
		if (whattodo & 1) {
			unsigned int uaddr = sy * step + sx - 1;
			u_int32_t loop;
			u_int8_t bgx2 = bgx & 0xF0;
			for (loop = height; loop > 0; loop --) {
				mga_writeb(vbase, uaddr, (mga_readb(vbase, uaddr) & 0x0F) | bgx2);
				uaddr += step;
			}
		}
		if (whattodo & 2) {
			unsigned int uaddr = sy * step + sx + width;
			u_int32_t loop;
			u_int8_t bgx2 = bgx & 0x0F;
			for (loop = height; loop > 0; loop --) {
				mga_writeb(vbase, uaddr, (mga_readb(vbase, uaddr) & 0xF0) | bgx2);
				uaddr += step;
			}
		}
	}

	CRITEND
}

static void matroxfb_cfb4_fillrect(struct fb_info* info, const struct fb_fillrect* rect) {
	MINFO_FROM_INFO(info);

	switch (rect->rop) {
		case ROP_COPY:
			matroxfb_cfb4_clear(PMINFO ((u_int32_t*)info->pseudo_palette)[rect->color], rect->dy, rect->dx, rect->height, rect->width);
			break;
	}
}

static void matroxfb_1bpp_imageblit(WPMINFO u_int32_t fgx, u_int32_t bgx,
		const u_int8_t* chardata, int width, int height, int yy, int xx) {
	u_int32_t step;
	u_int32_t ydstlen;
	u_int32_t xlen;
	u_int32_t ar0;
	u_int32_t charcell;
	u_int32_t fxbndry;
	vaddr_t mmio;
	int easy;
	CRITFLAGS

	DBG_HEAVY(__FUNCTION__);

	step = (width + 7) >> 3;
	charcell = height * step;
	xlen = (charcell + 3) & ~3;
	ydstlen = (yy << 16) | height;
	if (width == step << 3) {
		ar0 = height * width - 1;
		easy = 1;
	} else {
		ar0 = width - 1;
		easy = 0;
	}

	CRITBEGIN

	mga_fifo(3);
	if (easy)
		mga_outl(M_DWGCTL, M_DWG_ILOAD | M_DWG_SGNZERO | M_DWG_SHIFTZERO | M_DWG_BMONOWF | M_DWG_LINEAR | M_DWG_REPLACE);
	else
		mga_outl(M_DWGCTL, M_DWG_ILOAD | M_DWG_SGNZERO | M_DWG_SHIFTZERO | M_DWG_BMONOWF | M_DWG_REPLACE);
	mga_outl(M_FCOL, fgx);
	mga_outl(M_BCOL, bgx);
	fxbndry = ((xx + width - 1) << 16) | xx;
	mmio = ACCESS_FBINFO(mmio.vbase);

	mga_fifo(6);
	mga_writel(mmio, M_FXBNDRY, fxbndry);
	mga_writel(mmio, M_AR0, ar0);
	mga_writel(mmio, M_AR3, 0);
	if (easy) {
		mga_writel(mmio, M_YDSTLEN | M_EXEC, ydstlen);
		mga_memcpy_toio(mmio, chardata, xlen);
	} else {
		mga_writel(mmio, M_AR5, 0);
		mga_writel(mmio, M_YDSTLEN | M_EXEC, ydstlen);
		if ((step & 3) == 0) {
			/* Great. Source has 32bit aligned lines, so we can feed them
			   directly to the accelerator. */
			mga_memcpy_toio(mmio, chardata, charcell);
		} else if (step == 1) {
			/* Special case for 1..8bit widths */
			while (height--) {
#if defined(__BIG_ENDIAN)
				fb_writel((*chardata) << 24, mmio.vaddr);
#else
				fb_writel(*chardata, mmio.vaddr);
#endif
				chardata++;
			}
		} else if (step == 2) {
			/* Special case for 9..15bit widths */
			while (height--) {
#if defined(__BIG_ENDIAN)
				fb_writel((*(u_int16_t*)chardata) << 16, mmio.vaddr);
#else
				fb_writel(*(u_int16_t*)chardata, mmio.vaddr);
#endif
				chardata += 2;
			}
		} else {
			/* Tell... well, why bother... */
			while (height--) {
				size_t i;
				
				for (i = 0; i < step; i += 4) {
					/* Hope that there are at least three readable bytes beyond the end of bitmap */
					fb_writel(get_unaligned((u_int32_t*)(chardata + i)),mmio.vaddr);
				}
				chardata += step;
			}
		}
	}
	WaitTillIdle();
	CRITEND
}


static void matroxfb_imageblit(struct fb_info* info, const struct fb_image* image) {
	MINFO_FROM_INFO(info);

	DBG_HEAVY(__FUNCTION__);

	if (image->depth == 1) {
		u_int32_t fgx, bgx;

		fgx = ((u_int32_t*)info->pseudo_palette)[image->fg_color];
		bgx = ((u_int32_t*)info->pseudo_palette)[image->bg_color];
		matroxfb_1bpp_imageblit(PMINFO fgx, bgx, image->data, image->width, image->height, image->dy, image->dx);
	} else {
		/* Danger! image->depth is useless: logo painting code always
		   passes framebuffer color depth here, although logo data are
		   always 8bpp and info->pseudo_palette is changed to contain
		   logo palette to be used (but only for true/direct-color... sic...).
		   So do it completely in software... */
		cfb_imageblit(info, image);
	}
}

MODULE_LICENSE("GPL");
