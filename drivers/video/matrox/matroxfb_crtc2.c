/*
 *
 * Hardware accelerated Matrox Millennium I, II, Mystique, G100, G200, G400 and G450.
 *
 * (c) 1998-2002 Petr Vandrovec <vandrove@vc.cvut.cz>
 *
 * Portions Copyright (c) 2001 Matrox Graphics Inc.
 *
 * Version: 1.65 2002/08/14
 *
 */

#include "matroxfb_maven.h"
#include "matroxfb_crtc2.h"
#include "matroxfb_misc.h"
#include "matroxfb_DAC1064.h"
#include <linux/matroxfb.h>
#include <asm/uaccess.h>

/* **************************************************** */

static int mem = 8192;

module_param(mem, int, 0);
MODULE_PARM_DESC(mem, "Memory size reserved for dualhead (default=8MB)");

/* **************************************************** */

static int matroxfb_dh_setcolreg(unsigned regno, unsigned red, unsigned green,
		unsigned blue, unsigned transp, struct fb_info* info) {
	u_int32_t col;
#define m2info (container_of(info, struct matroxfb_dh_fb_info, fbcon))

	if (regno >= 16)
		return 1;
	if (m2info->fbcon.var.grayscale) {
		/* gray = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}
	red = CNVT_TOHW(red, m2info->fbcon.var.red.length);
	green = CNVT_TOHW(green, m2info->fbcon.var.green.length);
	blue = CNVT_TOHW(blue, m2info->fbcon.var.blue.length);
	transp = CNVT_TOHW(transp, m2info->fbcon.var.transp.length);

	col = (red << m2info->fbcon.var.red.offset)     |
	      (green << m2info->fbcon.var.green.offset) |
	      (blue << m2info->fbcon.var.blue.offset)   |
	      (transp << m2info->fbcon.var.transp.offset);

	switch (m2info->fbcon.var.bits_per_pixel) {
		case 16:
			m2info->cmap[regno] = col | (col << 16);
			break;
		case 32:
			m2info->cmap[regno] = col;
			break;
	}
	return 0;
#undef m2info
}

static void matroxfb_dh_restore(struct matroxfb_dh_fb_info* m2info,
		struct my_timming* mt,
		int mode,
		unsigned int pos) {
	u_int32_t tmp;
	u_int32_t datactl;
	MINFO_FROM(m2info->primary_dev);

	switch (mode) {
		case 15:
			tmp = 0x00200000;
			break;
		case 16:
			tmp = 0x00400000;
			break;
/*		case 32: */
		default:
			tmp = 0x00800000;
			break;
	}
	tmp |= 0x00000001;	/* enable CRTC2 */
	datactl = 0;
	if (ACCESS_FBINFO(outputs[1]).src == MATROXFB_SRC_CRTC2) {
		if (ACCESS_FBINFO(devflags.g450dac)) {
			tmp |= 0x00000006; /* source from secondary pixel PLL */
			/* no vidrst when in monitor mode */
			if (ACCESS_FBINFO(outputs[1]).mode != MATROXFB_OUTPUT_MODE_MONITOR) {
				tmp |=  0xC0001000; /* Enable H/V vidrst */
			}
		} else {
			tmp |= 0x00000002; /* source from VDOCLK */
			tmp |= 0xC0000000; /* enable vvidrst & hvidrst */
			/* MGA TVO is our clock source */
		}
	} else if (ACCESS_FBINFO(outputs[0]).src == MATROXFB_SRC_CRTC2) {
		tmp |= 0x00000004; /* source from pixclock */
		/* PIXPLL is our clock source */
	}
	if (ACCESS_FBINFO(outputs[0]).src == MATROXFB_SRC_CRTC2) {
		tmp |= 0x00100000;	/* connect CRTC2 to DAC */
	}
	if (mt->interlaced) {
		tmp |= 0x02000000;	/* interlaced, second field is bigger, as G450 apparently ignores it */
		mt->VDisplay >>= 1;
		mt->VSyncStart >>= 1;
		mt->VSyncEnd >>= 1;
		mt->VTotal >>= 1;
	}
	if ((mt->HTotal & 7) == 2) {
		datactl |= 0x00000010;
		mt->HTotal &= ~7;
	}
	tmp |= 0x10000000;	/* 0x10000000 is VIDRST polarity */
	mga_outl(0x3C14, ((mt->HDisplay - 8) << 16) | (mt->HTotal - 8));
	mga_outl(0x3C18, ((mt->HSyncEnd - 8) << 16) | (mt->HSyncStart - 8));
	mga_outl(0x3C1C, ((mt->VDisplay - 1) << 16) | (mt->VTotal - 1));
	mga_outl(0x3C20, ((mt->VSyncEnd - 1) << 16) | (mt->VSyncStart - 1));
	mga_outl(0x3C24, ((mt->VSyncStart) << 16) | (mt->HSyncStart));	/* preload */
	{
		u_int32_t linelen = m2info->fbcon.var.xres_virtual * (m2info->fbcon.var.bits_per_pixel >> 3);
		if (tmp & 0x02000000) {
			/* field #0 is smaller, so... */
			mga_outl(0x3C2C, pos);			/* field #1 vmemory start */
			mga_outl(0x3C28, pos + linelen);	/* field #0 vmemory start */
			linelen <<= 1;
			m2info->interlaced = 1;
		} else {
			mga_outl(0x3C28, pos);		/* vmemory start */
			m2info->interlaced = 0;
		}
		mga_outl(0x3C40, linelen);
	}
	mga_outl(0x3C4C, datactl);	/* data control */
	if (tmp & 0x02000000) {
		int i;

		mga_outl(0x3C10, tmp & ~0x02000000);
		for (i = 0; i < 2; i++) {
			unsigned int nl;
			unsigned int lastl = 0;

			while ((nl = mga_inl(0x3C48) & 0xFFF) >= lastl) {
				lastl = nl;
			}
		}
	}
	mga_outl(0x3C10, tmp);
	ACCESS_FBINFO(hw).crtc2.ctl = tmp;

	tmp = mt->VDisplay << 16;	/* line compare */
	if (mt->sync & FB_SYNC_HOR_HIGH_ACT)
		tmp |= 0x00000100;
	if (mt->sync & FB_SYNC_VERT_HIGH_ACT)
		tmp |= 0x00000200;
	mga_outl(0x3C44, tmp);
}

static void matroxfb_dh_disable(struct matroxfb_dh_fb_info* m2info) {
	MINFO_FROM(m2info->primary_dev);

	mga_outl(0x3C10, 0x00000004);	/* disable CRTC2, CRTC1->DAC1, PLL as clock source */
	ACCESS_FBINFO(hw).crtc2.ctl = 0x00000004;
}

static void matroxfb_dh_cfbX_init(struct matroxfb_dh_fb_info* m2info) {
	/* no acceleration for secondary head... */
	m2info->cmap[16] = 0xFFFFFFFF;
}

static void matroxfb_dh_pan_var(struct matroxfb_dh_fb_info* m2info,
		struct fb_var_screeninfo* var) {
	unsigned int pos;
	unsigned int linelen;
	unsigned int pixelsize;
	MINFO_FROM(m2info->primary_dev);

	m2info->fbcon.var.xoffset = var->xoffset;
	m2info->fbcon.var.yoffset = var->yoffset;
	pixelsize = m2info->fbcon.var.bits_per_pixel >> 3;
	linelen = m2info->fbcon.var.xres_virtual * pixelsize;
	pos = m2info->fbcon.var.yoffset * linelen + m2info->fbcon.var.xoffset * pixelsize;
	pos += m2info->video.offbase;
	if (m2info->interlaced) {
		mga_outl(0x3C2C, pos);
		mga_outl(0x3C28, pos + linelen);
	} else {
		mga_outl(0x3C28, pos);
	}
}

static int matroxfb_dh_decode_var(struct matroxfb_dh_fb_info* m2info,
		struct fb_var_screeninfo* var,
		int *visual,
		int *video_cmap_len,
		int *mode) {
	unsigned int mask;
	unsigned int memlen;
	unsigned int vramlen;

	switch (var->bits_per_pixel) {
		case 16:	mask = 0x1F;
				break;
		case 32:	mask = 0x0F;
				break;
		default:	return -EINVAL;
	}
	vramlen = m2info->video.len_usable;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;
	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	var->xres_virtual = (var->xres_virtual + mask) & ~mask;
	if (var->yres_virtual > 32767)
		return -EINVAL;
	memlen = var->xres_virtual * var->yres_virtual * (var->bits_per_pixel >> 3);
	if (memlen > vramlen)
		return -EINVAL;
	if (var->xoffset + var->xres > var->xres_virtual)
		var->xoffset = var->xres_virtual - var->xres;
	if (var->yoffset + var->yres > var->yres_virtual)
		var->yoffset = var->yres_virtual - var->yres;

	var->xres &= ~7;
	var->left_margin &= ~7;
	var->right_margin &= ~7;
	var->hsync_len &= ~7;

	*mode = var->bits_per_pixel;
	if (var->bits_per_pixel == 16) {
		if (var->green.length == 5) {
			var->red.offset = 10;
			var->red.length = 5;
			var->green.offset = 5;
			var->green.length = 5;
			var->blue.offset = 0;
			var->blue.length = 5;
			var->transp.offset = 15;
			var->transp.length = 1;
			*mode = 15;
		} else {
			var->red.offset = 11;
			var->red.length = 5;
			var->green.offset = 5;
			var->green.length = 6;
			var->blue.offset = 0;
			var->blue.length = 5;
			var->transp.offset = 0;
			var->transp.length = 0;
		}
	} else {
			var->red.offset = 16;
			var->red.length = 8;
			var->green.offset = 8;
			var->green.length = 8;
			var->blue.offset = 0;
			var->blue.length = 8;
			var->transp.offset = 24;
			var->transp.length = 8;
	}
	*visual = FB_VISUAL_TRUECOLOR;
	*video_cmap_len = 16;
	return 0;
}

static int matroxfb_dh_open(struct fb_info* info, int user) {
#define m2info (container_of(info, struct matroxfb_dh_fb_info, fbcon))
	MINFO_FROM(m2info->primary_dev);

	if (MINFO) {
		int err;

		if (ACCESS_FBINFO(dead)) {
			return -ENXIO;
		}
		err = ACCESS_FBINFO(fbops).fb_open(&ACCESS_FBINFO(fbcon), user);
		if (err) {
			return err;
		}
	}
	return 0;
#undef m2info
}

static int matroxfb_dh_release(struct fb_info* info, int user) {
#define m2info (container_of(info, struct matroxfb_dh_fb_info, fbcon))
	int err = 0;
	MINFO_FROM(m2info->primary_dev);

	if (MINFO) {
		err = ACCESS_FBINFO(fbops).fb_release(&ACCESS_FBINFO(fbcon), user);
	}
	return err;
#undef m2info
}

static void matroxfb_dh_init_fix(struct matroxfb_dh_fb_info *m2info) {
	struct fb_fix_screeninfo *fix = &m2info->fbcon.fix;

	strcpy(fix->id, "MATROX DH");

	fix->smem_start = m2info->video.base;
	fix->smem_len = m2info->video.len_usable;
	fix->ypanstep = 1;
	fix->ywrapstep = 0;
	fix->xpanstep = 8;	/* TBD */
	fix->mmio_start = m2info->mmio.base;
	fix->mmio_len = m2info->mmio.len;
	fix->accel = 0;		/* no accel... */
}

static int matroxfb_dh_check_var(struct fb_var_screeninfo* var, struct fb_info* info) {
#define m2info (container_of(info, struct matroxfb_dh_fb_info, fbcon))
	int visual;
	int cmap_len;
	int mode;

	return matroxfb_dh_decode_var(m2info, var, &visual, &cmap_len, &mode);
#undef m2info
}

static int matroxfb_dh_set_par(struct fb_info* info) {
#define m2info (container_of(info, struct matroxfb_dh_fb_info, fbcon))
	int visual;
	int cmap_len;
	int mode;
	int err;
	struct fb_var_screeninfo* var = &info->var;
	MINFO_FROM(m2info->primary_dev);

	if ((err = matroxfb_dh_decode_var(m2info, var, &visual, &cmap_len, &mode)) != 0)
		return err;
	/* cmap */
	{
		m2info->fbcon.screen_base = vaddr_va(m2info->video.vbase);
		m2info->fbcon.fix.visual = visual;
		m2info->fbcon.fix.type = FB_TYPE_PACKED_PIXELS;
		m2info->fbcon.fix.type_aux = 0;
		m2info->fbcon.fix.line_length = (var->xres_virtual * var->bits_per_pixel) >> 3;
	}
	{
		struct my_timming mt;
		unsigned int pos;
		int out;
		int cnt;

		matroxfb_var2my(&m2info->fbcon.var, &mt);
		mt.crtc = MATROXFB_SRC_CRTC2;
		/* CRTC2 delay */
		mt.delay = 34;

		pos = (m2info->fbcon.var.yoffset * m2info->fbcon.var.xres_virtual + m2info->fbcon.var.xoffset) * m2info->fbcon.var.bits_per_pixel >> 3;
		pos += m2info->video.offbase;
		cnt = 0;
		down_read(&ACCESS_FBINFO(altout).lock);
		for (out = 0; out < MATROXFB_MAX_OUTPUTS; out++) {
			if (ACCESS_FBINFO(outputs[out]).src == MATROXFB_SRC_CRTC2) {
				cnt++;
				if (ACCESS_FBINFO(outputs[out]).output->compute) {
					ACCESS_FBINFO(outputs[out]).output->compute(ACCESS_FBINFO(outputs[out]).data, &mt);
				}
			}
		}
		ACCESS_FBINFO(crtc2).pixclock = mt.pixclock;
		ACCESS_FBINFO(crtc2).mnp = mt.mnp;
		up_read(&ACCESS_FBINFO(altout).lock);
		if (cnt) {
			matroxfb_dh_restore(m2info, &mt, mode, pos);
		} else {
			matroxfb_dh_disable(m2info);
		}
		DAC1064_global_init(PMINFO2);
		DAC1064_global_restore(PMINFO2);
		down_read(&ACCESS_FBINFO(altout).lock);
		for (out = 0; out < MATROXFB_MAX_OUTPUTS; out++) {
			if (ACCESS_FBINFO(outputs[out]).src == MATROXFB_SRC_CRTC2 &&
			    ACCESS_FBINFO(outputs[out]).output->program) {
				ACCESS_FBINFO(outputs[out]).output->program(ACCESS_FBINFO(outputs[out]).data);
			}
		}
		for (out = 0; out < MATROXFB_MAX_OUTPUTS; out++) {
			if (ACCESS_FBINFO(outputs[out]).src == MATROXFB_SRC_CRTC2 &&
			    ACCESS_FBINFO(outputs[out]).output->start) {
				ACCESS_FBINFO(outputs[out]).output->start(ACCESS_FBINFO(outputs[out]).data);
			}
		}
		up_read(&ACCESS_FBINFO(altout).lock);
		matroxfb_dh_cfbX_init(m2info);
	}
	m2info->initialized = 1;
	return 0;
#undef m2info
}

static int matroxfb_dh_pan_display(struct fb_var_screeninfo* var, struct fb_info* info) {
#define m2info (container_of(info, struct matroxfb_dh_fb_info, fbcon))
	matroxfb_dh_pan_var(m2info, var);
	return 0;
#undef m2info
}

static int matroxfb_dh_get_vblank(const struct matroxfb_dh_fb_info* m2info, struct fb_vblank* vblank) {
	MINFO_FROM(m2info->primary_dev);

	matroxfb_enable_irq(PMINFO 0);
	memset(vblank, 0, sizeof(*vblank));
	vblank->flags = FB_VBLANK_HAVE_VCOUNT | FB_VBLANK_HAVE_VBLANK;
	/* mask out reserved bits + field number (odd/even) */
	vblank->vcount = mga_inl(0x3C48) & 0x000007FF;
	/* compatibility stuff */
	if (vblank->vcount >= m2info->fbcon.var.yres)
		vblank->flags |= FB_VBLANK_VBLANKING;
        if (test_bit(0, &ACCESS_FBINFO(irq_flags))) {
                vblank->flags |= FB_VBLANK_HAVE_COUNT;
                /* Only one writer, aligned int value...
                   it should work without lock and without atomic_t */
                vblank->count = ACCESS_FBINFO(crtc2).vsync.cnt;
        }
	return 0;
}

static int matroxfb_dh_ioctl(struct inode* inode,
		struct file* file,
		unsigned int cmd,
		unsigned long arg,
		struct fb_info* info) {
#define m2info (container_of(info, struct matroxfb_dh_fb_info, fbcon))
	MINFO_FROM(m2info->primary_dev);

	DBG(__FUNCTION__)

	switch (cmd) {
		case FBIOGET_VBLANK:
			{
				struct fb_vblank vblank;
				int err;

				err = matroxfb_dh_get_vblank(m2info, &vblank);
				if (err)
					return err;
				if (copy_to_user((void __user *)arg, &vblank, sizeof(vblank)))
					return -EFAULT;
				return 0;
			}
		case FBIO_WAITFORVSYNC:
			{
				u_int32_t crt;

				if (get_user(crt, (u_int32_t __user *)arg))
					return -EFAULT;

				if (crt != 0)
					return -ENODEV;
				return matroxfb_wait_for_sync(PMINFO 1);
			}
		case MATROXFB_SET_OUTPUT_MODE:
		case MATROXFB_GET_OUTPUT_MODE:
		case MATROXFB_GET_ALL_OUTPUTS:
			{
				return ACCESS_FBINFO(fbcon.fbops)->fb_ioctl(inode, file, cmd, arg, &ACCESS_FBINFO(fbcon));
			}
		case MATROXFB_SET_OUTPUT_CONNECTION:
			{
				u_int32_t tmp;
				int out;
				int changes;

				if (get_user(tmp, (u_int32_t __user *)arg))
					return -EFAULT;
				for (out = 0; out < 32; out++) {
					if (tmp & (1 << out)) {
						if (out >= MATROXFB_MAX_OUTPUTS)
							return -ENXIO;
						if (!ACCESS_FBINFO(outputs[out]).output)
							return -ENXIO;
						switch (ACCESS_FBINFO(outputs[out]).src) {
							case MATROXFB_SRC_NONE:
							case MATROXFB_SRC_CRTC2:
								break;
							default:
								return -EBUSY;
						}
					}
				}
				if (ACCESS_FBINFO(devflags.panellink)) {
					if (tmp & MATROXFB_OUTPUT_CONN_DFP)
						return -EINVAL;
					if ((ACCESS_FBINFO(outputs[2]).src == MATROXFB_SRC_CRTC1) && tmp)
						return -EBUSY;
				}
				changes = 0;
				for (out = 0; out < MATROXFB_MAX_OUTPUTS; out++) {
					if (tmp & (1 << out)) {
						if (ACCESS_FBINFO(outputs[out]).src != MATROXFB_SRC_CRTC2) {
							changes = 1;
							ACCESS_FBINFO(outputs[out]).src = MATROXFB_SRC_CRTC2;
						}
					} else if (ACCESS_FBINFO(outputs[out]).src == MATROXFB_SRC_CRTC2) {
						changes = 1;
						ACCESS_FBINFO(outputs[out]).src = MATROXFB_SRC_NONE;
					}
				}
				if (!changes)
					return 0;
				matroxfb_dh_set_par(info);
				return 0;
			}
		case MATROXFB_GET_OUTPUT_CONNECTION:
			{
				u_int32_t conn = 0;
				int out;

				for (out = 0; out < MATROXFB_MAX_OUTPUTS; out++) {
					if (ACCESS_FBINFO(outputs[out]).src == MATROXFB_SRC_CRTC2) {
						conn |= 1 << out;
					}
				}
				if (put_user(conn, (u_int32_t __user *)arg))
					return -EFAULT;
				return 0;
			}
		case MATROXFB_GET_AVAILABLE_OUTPUTS:
			{
				u_int32_t tmp = 0;
				int out;

				for (out = 0; out < MATROXFB_MAX_OUTPUTS; out++) {
					if (ACCESS_FBINFO(outputs[out]).output) {
						switch (ACCESS_FBINFO(outputs[out]).src) {
							case MATROXFB_SRC_NONE:
							case MATROXFB_SRC_CRTC2:
								tmp |= 1 << out;
								break;
						}
					}
				}
				if (ACCESS_FBINFO(devflags.panellink)) {
					tmp &= ~MATROXFB_OUTPUT_CONN_DFP;
					if (ACCESS_FBINFO(outputs[2]).src == MATROXFB_SRC_CRTC1) {
						tmp = 0;
					}
				}
				if (put_user(tmp, (u_int32_t __user *)arg))
					return -EFAULT;
				return 0;
			}
	}
	return -ENOTTY;
#undef m2info
}

static int matroxfb_dh_blank(int blank, struct fb_info* info) {
#define m2info (container_of(info, struct matroxfb_dh_fb_info, fbcon))
	switch (blank) {
		case 1:
		case 2:
		case 3:
		case 4:
		default:;
	}
	/* do something... */
	return 0;
#undef m2info
}

static struct fb_ops matroxfb_dh_ops = {
	.owner =	THIS_MODULE,
	.fb_open =	matroxfb_dh_open,
	.fb_release =	matroxfb_dh_release,
	.fb_check_var =	matroxfb_dh_check_var,
	.fb_set_par =	matroxfb_dh_set_par,
	.fb_setcolreg =	matroxfb_dh_setcolreg,
	.fb_pan_display =matroxfb_dh_pan_display,
	.fb_blank =	matroxfb_dh_blank,
	.fb_ioctl =	matroxfb_dh_ioctl,
	.fb_fillrect =	cfb_fillrect,
	.fb_copyarea =	cfb_copyarea,
	.fb_imageblit =	cfb_imageblit,
};

static struct fb_var_screeninfo matroxfb_dh_defined = {
		640,480,640,480,/* W,H, virtual W,H */
		0,0,		/* offset */
		32,		/* depth */
		0,		/* gray */
		{0,0,0},	/* R */
		{0,0,0},	/* G */
		{0,0,0},	/* B */
		{0,0,0},	/* alpha */
		0,		/* nonstd */
		FB_ACTIVATE_NOW,
		-1,-1,		/* display size */
		0,		/* accel flags */
		39721L,48L,16L,33L,10L,
		96L,2,0,	/* no sync info */
		FB_VMODE_NONINTERLACED,
		0, {0,0,0,0,0}
};

static int matroxfb_dh_regit(CPMINFO struct matroxfb_dh_fb_info* m2info) {
#define minfo (m2info->primary_dev)
	void* oldcrtc2;

	m2info->fbcon.fbops = &matroxfb_dh_ops;
	m2info->fbcon.flags = FBINFO_FLAG_DEFAULT;
	m2info->fbcon.flags |= FBINFO_HWACCEL_XPAN |
			       FBINFO_HWACCEL_YPAN;
	m2info->fbcon.pseudo_palette = m2info->cmap;
	fb_alloc_cmap(&m2info->fbcon.cmap, 256, 1);

	if (mem < 64)
		mem *= 1024;
	if (mem < 64*1024)
		mem *= 1024;
	mem &= ~0x00000FFF;	/* PAGE_MASK? */
	if (ACCESS_FBINFO(video.len_usable) + mem <= ACCESS_FBINFO(video.len))
		m2info->video.offbase = ACCESS_FBINFO(video.len) - mem;
	else if (ACCESS_FBINFO(video.len) < mem) {
		return -ENOMEM;
	} else { /* check yres on first head... */
		m2info->video.borrowed = mem;
		ACCESS_FBINFO(video.len_usable) -= mem;
		m2info->video.offbase = ACCESS_FBINFO(video.len_usable);
	}
	m2info->video.base = ACCESS_FBINFO(video.base) + m2info->video.offbase;
	m2info->video.len = m2info->video.len_usable = m2info->video.len_maximum = mem;
	m2info->video.vbase.vaddr = vaddr_va(ACCESS_FBINFO(video.vbase)) + m2info->video.offbase;
	m2info->mmio.base = ACCESS_FBINFO(mmio.base);
	m2info->mmio.vbase = ACCESS_FBINFO(mmio.vbase);
	m2info->mmio.len = ACCESS_FBINFO(mmio.len);

	matroxfb_dh_init_fix(m2info);
	if (register_framebuffer(&m2info->fbcon)) {
		return -ENXIO;
	}
	if (!m2info->initialized)
		fb_set_var(&m2info->fbcon, &matroxfb_dh_defined);
	down_write(&ACCESS_FBINFO(crtc2.lock));
	oldcrtc2 = ACCESS_FBINFO(crtc2.info);
	ACCESS_FBINFO(crtc2.info) = m2info;
	up_write(&ACCESS_FBINFO(crtc2.lock));
	if (oldcrtc2) {
		printk(KERN_ERR "matroxfb_crtc2: Internal consistency check failed: crtc2 already present: %p\n",
			oldcrtc2);
	}
	return 0;
#undef minfo
}

/* ************************** */

static int matroxfb_dh_registerfb(struct matroxfb_dh_fb_info* m2info) {
#define minfo (m2info->primary_dev)
	if (matroxfb_dh_regit(PMINFO m2info)) {
		printk(KERN_ERR "matroxfb_crtc2: secondary head failed to register\n");
		return -1;
	}
	printk(KERN_INFO "matroxfb_crtc2: secondary head of fb%u was registered as fb%u\n",
		ACCESS_FBINFO(fbcon.node), m2info->fbcon.node);
	m2info->fbcon_registered = 1;
	return 0;
#undef minfo
}

static void matroxfb_dh_deregisterfb(struct matroxfb_dh_fb_info* m2info) {
#define minfo (m2info->primary_dev)
	if (m2info->fbcon_registered) {
		int id;
		struct matroxfb_dh_fb_info* crtc2;

		down_write(&ACCESS_FBINFO(crtc2.lock));
		crtc2 = ACCESS_FBINFO(crtc2.info);
		if (crtc2 == m2info)
			ACCESS_FBINFO(crtc2.info) = NULL;
		up_write(&ACCESS_FBINFO(crtc2.lock));
		if (crtc2 != m2info) {
			printk(KERN_ERR "matroxfb_crtc2: Internal consistency check failed: crtc2 mismatch at unload: %p != %p\n",
				crtc2, m2info);
			printk(KERN_ERR "matroxfb_crtc2: Expect kernel crash after module unload.\n");
			return;
		}
		id = m2info->fbcon.node;
		unregister_framebuffer(&m2info->fbcon);
		/* return memory back to primary head */
		ACCESS_FBINFO(video.len_usable) += m2info->video.borrowed;
		printk(KERN_INFO "matroxfb_crtc2: fb%u unregistered\n", id);
		m2info->fbcon_registered = 0;
	}
#undef minfo
}

static void* matroxfb_crtc2_probe(struct matrox_fb_info* minfo) {
	struct matroxfb_dh_fb_info* m2info;

	/* hardware is CRTC2 incapable... */
	if (!ACCESS_FBINFO(devflags.crtc2))
		return NULL;
	m2info = (struct matroxfb_dh_fb_info*)kmalloc(sizeof(*m2info), GFP_KERNEL);
	if (!m2info) {
		printk(KERN_ERR "matroxfb_crtc2: Not enough memory for CRTC2 control structs\n");
		return NULL;
	}
	memset(m2info, 0, sizeof(*m2info));
	m2info->primary_dev = MINFO;
	if (matroxfb_dh_registerfb(m2info)) {
		kfree(m2info);
		printk(KERN_ERR "matroxfb_crtc2: CRTC2 framebuffer failed to register\n");
		return NULL;
	}
	return m2info;
}

static void matroxfb_crtc2_remove(struct matrox_fb_info* minfo, void* crtc2) {
	matroxfb_dh_deregisterfb(crtc2);
	kfree(crtc2);
}

static struct matroxfb_driver crtc2 = {
		.name =		"Matrox G400 CRTC2",
		.probe =	matroxfb_crtc2_probe,
		.remove =	matroxfb_crtc2_remove };

static int matroxfb_crtc2_init(void) {
	if (fb_get_options("matrox_crtc2fb", NULL))
		return -ENODEV;

	matroxfb_register_driver(&crtc2);
	return 0;
}

static void matroxfb_crtc2_exit(void) {
	matroxfb_unregister_driver(&crtc2);
}

MODULE_AUTHOR("(c) 1999-2002 Petr Vandrovec <vandrove@vc.cvut.cz>");
MODULE_DESCRIPTION("Matrox G400 CRTC2 driver");
MODULE_LICENSE("GPL");
module_init(matroxfb_crtc2_init);
module_exit(matroxfb_crtc2_exit);
/* we do not have __setup() yet */
