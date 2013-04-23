/*
 * linux/drivers/video/omap2/omapfb-main.c
 *
 * Copyright (C) 2008 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/omapfb.h>

#include <video/omapdss.h>
#include <video/omapvrfb.h>

#include "omapfb.h"

#define MODULE_NAME     "omapfb"

#define OMAPFB_PLANE_XRES_MIN		8
#define OMAPFB_PLANE_YRES_MIN		8

static char *def_mode;
static char *def_vram;
static bool def_vrfb;
static int def_rotate;
static bool def_mirror;
static bool auto_update;
static unsigned int auto_update_freq;
module_param(auto_update, bool, 0);
module_param(auto_update_freq, uint, 0644);

#ifdef DEBUG
bool omapfb_debug;
module_param_named(debug, omapfb_debug, bool, 0644);
static bool omapfb_test_pattern;
module_param_named(test, omapfb_test_pattern, bool, 0644);
#endif

static int omapfb_fb_init(struct omapfb2_device *fbdev, struct fb_info *fbi);
static int omapfb_get_recommended_bpp(struct omapfb2_device *fbdev,
		struct omap_dss_device *dssdev);

#ifdef DEBUG
static void draw_pixel(struct fb_info *fbi, int x, int y, unsigned color)
{
	struct fb_var_screeninfo *var = &fbi->var;
	struct fb_fix_screeninfo *fix = &fbi->fix;
	void __iomem *addr = fbi->screen_base;
	const unsigned bytespp = var->bits_per_pixel >> 3;
	const unsigned line_len = fix->line_length / bytespp;

	int r = (color >> 16) & 0xff;
	int g = (color >> 8) & 0xff;
	int b = (color >> 0) & 0xff;

	if (var->bits_per_pixel == 16) {
		u16 __iomem *p = (u16 __iomem *)addr;
		p += y * line_len + x;

		r = r * 32 / 256;
		g = g * 64 / 256;
		b = b * 32 / 256;

		__raw_writew((r << 11) | (g << 5) | (b << 0), p);
	} else if (var->bits_per_pixel == 24) {
		u8 __iomem *p = (u8 __iomem *)addr;
		p += (y * line_len + x) * 3;

		__raw_writeb(b, p + 0);
		__raw_writeb(g, p + 1);
		__raw_writeb(r, p + 2);
	} else if (var->bits_per_pixel == 32) {
		u32 __iomem *p = (u32 __iomem *)addr;
		p += y * line_len + x;
		__raw_writel(color, p);
	}
}

static void fill_fb(struct fb_info *fbi)
{
	struct fb_var_screeninfo *var = &fbi->var;
	const short w = var->xres_virtual;
	const short h = var->yres_virtual;
	void __iomem *addr = fbi->screen_base;
	int y, x;

	if (!addr)
		return;

	DBG("fill_fb %dx%d, line_len %d bytes\n", w, h, fbi->fix.line_length);

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			if (x < 20 && y < 20)
				draw_pixel(fbi, x, y, 0xffffff);
			else if (x < 20 && (y > 20 && y < h - 20))
				draw_pixel(fbi, x, y, 0xff);
			else if (y < 20 && (x > 20 && x < w - 20))
				draw_pixel(fbi, x, y, 0xff00);
			else if (x > w - 20 && (y > 20 && y < h - 20))
				draw_pixel(fbi, x, y, 0xff0000);
			else if (y > h - 20 && (x > 20 && x < w - 20))
				draw_pixel(fbi, x, y, 0xffff00);
			else if (x == 20 || x == w - 20 ||
					y == 20 || y == h - 20)
				draw_pixel(fbi, x, y, 0xffffff);
			else if (x == y || w - x == h - y)
				draw_pixel(fbi, x, y, 0xff00ff);
			else if (w - x == y || x == h - y)
				draw_pixel(fbi, x, y, 0x00ffff);
			else if (x > 20 && y > 20 && x < w - 20 && y < h - 20) {
				int t = x * 3 / w;
				unsigned r = 0, g = 0, b = 0;
				unsigned c;
				if (var->bits_per_pixel == 16) {
					if (t == 0)
						b = (y % 32) * 256 / 32;
					else if (t == 1)
						g = (y % 64) * 256 / 64;
					else if (t == 2)
						r = (y % 32) * 256 / 32;
				} else {
					if (t == 0)
						b = (y % 256);
					else if (t == 1)
						g = (y % 256);
					else if (t == 2)
						r = (y % 256);
				}
				c = (r << 16) | (g << 8) | (b << 0);
				draw_pixel(fbi, x, y, c);
			} else {
				draw_pixel(fbi, x, y, 0);
			}
		}
	}
}
#endif

static unsigned omapfb_get_vrfb_offset(const struct omapfb_info *ofbi, int rot)
{
	const struct vrfb *vrfb = &ofbi->region->vrfb;
	unsigned offset;

	switch (rot) {
	case FB_ROTATE_UR:
		offset = 0;
		break;
	case FB_ROTATE_CW:
		offset = vrfb->yoffset;
		break;
	case FB_ROTATE_UD:
		offset = vrfb->yoffset * OMAP_VRFB_LINE_LEN + vrfb->xoffset;
		break;
	case FB_ROTATE_CCW:
		offset = vrfb->xoffset * OMAP_VRFB_LINE_LEN;
		break;
	default:
		BUG();
		return 0;
	}

	offset *= vrfb->bytespp;

	return offset;
}

static u32 omapfb_get_region_rot_paddr(const struct omapfb_info *ofbi, int rot)
{
	if (ofbi->rotation_type == OMAP_DSS_ROT_VRFB) {
		return ofbi->region->vrfb.paddr[rot]
			+ omapfb_get_vrfb_offset(ofbi, rot);
	} else {
		return ofbi->region->paddr;
	}
}

static u32 omapfb_get_region_paddr(const struct omapfb_info *ofbi)
{
	if (ofbi->rotation_type == OMAP_DSS_ROT_VRFB)
		return ofbi->region->vrfb.paddr[0];
	else
		return ofbi->region->paddr;
}

static void __iomem *omapfb_get_region_vaddr(const struct omapfb_info *ofbi)
{
	if (ofbi->rotation_type == OMAP_DSS_ROT_VRFB)
		return ofbi->region->vrfb.vaddr[0];
	else
		return ofbi->region->vaddr;
}

static struct omapfb_colormode omapfb_colormodes[] = {
	{
		.dssmode = OMAP_DSS_COLOR_UYVY,
		.bits_per_pixel = 16,
		.nonstd = OMAPFB_COLOR_YUV422,
	}, {
		.dssmode = OMAP_DSS_COLOR_YUV2,
		.bits_per_pixel = 16,
		.nonstd = OMAPFB_COLOR_YUY422,
	}, {
		.dssmode = OMAP_DSS_COLOR_ARGB16,
		.bits_per_pixel = 16,
		.red	= { .length = 4, .offset = 8, .msb_right = 0 },
		.green	= { .length = 4, .offset = 4, .msb_right = 0 },
		.blue	= { .length = 4, .offset = 0, .msb_right = 0 },
		.transp	= { .length = 4, .offset = 12, .msb_right = 0 },
	}, {
		.dssmode = OMAP_DSS_COLOR_RGB16,
		.bits_per_pixel = 16,
		.red	= { .length = 5, .offset = 11, .msb_right = 0 },
		.green	= { .length = 6, .offset = 5, .msb_right = 0 },
		.blue	= { .length = 5, .offset = 0, .msb_right = 0 },
		.transp	= { .length = 0, .offset = 0, .msb_right = 0 },
	}, {
		.dssmode = OMAP_DSS_COLOR_RGB24P,
		.bits_per_pixel = 24,
		.red	= { .length = 8, .offset = 16, .msb_right = 0 },
		.green	= { .length = 8, .offset = 8, .msb_right = 0 },
		.blue	= { .length = 8, .offset = 0, .msb_right = 0 },
		.transp	= { .length = 0, .offset = 0, .msb_right = 0 },
	}, {
		.dssmode = OMAP_DSS_COLOR_RGB24U,
		.bits_per_pixel = 32,
		.red	= { .length = 8, .offset = 16, .msb_right = 0 },
		.green	= { .length = 8, .offset = 8, .msb_right = 0 },
		.blue	= { .length = 8, .offset = 0, .msb_right = 0 },
		.transp	= { .length = 0, .offset = 0, .msb_right = 0 },
	}, {
		.dssmode = OMAP_DSS_COLOR_ARGB32,
		.bits_per_pixel = 32,
		.red	= { .length = 8, .offset = 16, .msb_right = 0 },
		.green	= { .length = 8, .offset = 8, .msb_right = 0 },
		.blue	= { .length = 8, .offset = 0, .msb_right = 0 },
		.transp	= { .length = 8, .offset = 24, .msb_right = 0 },
	}, {
		.dssmode = OMAP_DSS_COLOR_RGBA32,
		.bits_per_pixel = 32,
		.red	= { .length = 8, .offset = 24, .msb_right = 0 },
		.green	= { .length = 8, .offset = 16, .msb_right = 0 },
		.blue	= { .length = 8, .offset = 8, .msb_right = 0 },
		.transp	= { .length = 8, .offset = 0, .msb_right = 0 },
	}, {
		.dssmode = OMAP_DSS_COLOR_RGBX32,
		.bits_per_pixel = 32,
		.red	= { .length = 8, .offset = 24, .msb_right = 0 },
		.green	= { .length = 8, .offset = 16, .msb_right = 0 },
		.blue	= { .length = 8, .offset = 8, .msb_right = 0 },
		.transp	= { .length = 0, .offset = 0, .msb_right = 0 },
	},
};

static bool cmp_var_to_colormode(struct fb_var_screeninfo *var,
		struct omapfb_colormode *color)
{
	bool cmp_component(struct fb_bitfield *f1, struct fb_bitfield *f2)
	{
		return f1->length == f2->length &&
			f1->offset == f2->offset &&
			f1->msb_right == f2->msb_right;
	}

	if (var->bits_per_pixel == 0 ||
			var->red.length == 0 ||
			var->blue.length == 0 ||
			var->green.length == 0)
		return 0;

	return var->bits_per_pixel == color->bits_per_pixel &&
		cmp_component(&var->red, &color->red) &&
		cmp_component(&var->green, &color->green) &&
		cmp_component(&var->blue, &color->blue) &&
		cmp_component(&var->transp, &color->transp);
}

static void assign_colormode_to_var(struct fb_var_screeninfo *var,
		struct omapfb_colormode *color)
{
	var->bits_per_pixel = color->bits_per_pixel;
	var->nonstd = color->nonstd;
	var->red = color->red;
	var->green = color->green;
	var->blue = color->blue;
	var->transp = color->transp;
}

static int fb_mode_to_dss_mode(struct fb_var_screeninfo *var,
		enum omap_color_mode *mode)
{
	enum omap_color_mode dssmode;
	int i;

	/* first match with nonstd field */
	if (var->nonstd) {
		for (i = 0; i < ARRAY_SIZE(omapfb_colormodes); ++i) {
			struct omapfb_colormode *m = &omapfb_colormodes[i];
			if (var->nonstd == m->nonstd) {
				assign_colormode_to_var(var, m);
				*mode = m->dssmode;
				return 0;
			}
		}

		return -EINVAL;
	}

	/* then try exact match of bpp and colors */
	for (i = 0; i < ARRAY_SIZE(omapfb_colormodes); ++i) {
		struct omapfb_colormode *m = &omapfb_colormodes[i];
		if (cmp_var_to_colormode(var, m)) {
			assign_colormode_to_var(var, m);
			*mode = m->dssmode;
			return 0;
		}
	}

	/* match with bpp if user has not filled color fields
	 * properly */
	switch (var->bits_per_pixel) {
	case 1:
		dssmode = OMAP_DSS_COLOR_CLUT1;
		break;
	case 2:
		dssmode = OMAP_DSS_COLOR_CLUT2;
		break;
	case 4:
		dssmode = OMAP_DSS_COLOR_CLUT4;
		break;
	case 8:
		dssmode = OMAP_DSS_COLOR_CLUT8;
		break;
	case 12:
		dssmode = OMAP_DSS_COLOR_RGB12U;
		break;
	case 16:
		dssmode = OMAP_DSS_COLOR_RGB16;
		break;
	case 24:
		dssmode = OMAP_DSS_COLOR_RGB24P;
		break;
	case 32:
		dssmode = OMAP_DSS_COLOR_RGB24U;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(omapfb_colormodes); ++i) {
		struct omapfb_colormode *m = &omapfb_colormodes[i];
		if (dssmode == m->dssmode) {
			assign_colormode_to_var(var, m);
			*mode = m->dssmode;
			return 0;
		}
	}

	return -EINVAL;
}

static int check_fb_res_bounds(struct fb_var_screeninfo *var)
{
	int xres_min = OMAPFB_PLANE_XRES_MIN;
	int xres_max = 2048;
	int yres_min = OMAPFB_PLANE_YRES_MIN;
	int yres_max = 2048;

	/* XXX: some applications seem to set virtual res to 0. */
	if (var->xres_virtual == 0)
		var->xres_virtual = var->xres;

	if (var->yres_virtual == 0)
		var->yres_virtual = var->yres;

	if (var->xres_virtual < xres_min || var->yres_virtual < yres_min)
		return -EINVAL;

	if (var->xres < xres_min)
		var->xres = xres_min;
	if (var->yres < yres_min)
		var->yres = yres_min;
	if (var->xres > xres_max)
		var->xres = xres_max;
	if (var->yres > yres_max)
		var->yres = yres_max;

	if (var->xres > var->xres_virtual)
		var->xres = var->xres_virtual;
	if (var->yres > var->yres_virtual)
		var->yres = var->yres_virtual;

	return 0;
}

static void shrink_height(unsigned long max_frame_size,
		struct fb_var_screeninfo *var)
{
	DBG("can't fit FB into memory, reducing y\n");
	var->yres_virtual = max_frame_size /
		(var->xres_virtual * var->bits_per_pixel >> 3);

	if (var->yres_virtual < OMAPFB_PLANE_YRES_MIN)
		var->yres_virtual = OMAPFB_PLANE_YRES_MIN;

	if (var->yres > var->yres_virtual)
		var->yres = var->yres_virtual;
}

static void shrink_width(unsigned long max_frame_size,
		struct fb_var_screeninfo *var)
{
	DBG("can't fit FB into memory, reducing x\n");
	var->xres_virtual = max_frame_size / var->yres_virtual /
		(var->bits_per_pixel >> 3);

	if (var->xres_virtual < OMAPFB_PLANE_XRES_MIN)
		var->xres_virtual = OMAPFB_PLANE_XRES_MIN;

	if (var->xres > var->xres_virtual)
		var->xres = var->xres_virtual;
}

static int check_vrfb_fb_size(unsigned long region_size,
		const struct fb_var_screeninfo *var)
{
	unsigned long min_phys_size = omap_vrfb_min_phys_size(var->xres_virtual,
		var->yres_virtual, var->bits_per_pixel >> 3);

	return min_phys_size > region_size ? -EINVAL : 0;
}

static int check_fb_size(const struct omapfb_info *ofbi,
		struct fb_var_screeninfo *var)
{
	unsigned long max_frame_size = ofbi->region->size;
	int bytespp = var->bits_per_pixel >> 3;
	unsigned long line_size = var->xres_virtual * bytespp;

	if (ofbi->rotation_type == OMAP_DSS_ROT_VRFB) {
		/* One needs to check for both VRFB and OMAPFB limitations. */
		if (check_vrfb_fb_size(max_frame_size, var))
			shrink_height(omap_vrfb_max_height(
				max_frame_size, var->xres_virtual, bytespp) *
				line_size, var);

		if (check_vrfb_fb_size(max_frame_size, var)) {
			DBG("cannot fit FB to memory\n");
			return -EINVAL;
		}

		return 0;
	}

	DBG("max frame size %lu, line size %lu\n", max_frame_size, line_size);

	if (line_size * var->yres_virtual > max_frame_size)
		shrink_height(max_frame_size, var);

	if (line_size * var->yres_virtual > max_frame_size) {
		shrink_width(max_frame_size, var);
		line_size = var->xres_virtual * bytespp;
	}

	if (line_size * var->yres_virtual > max_frame_size) {
		DBG("cannot fit FB to memory\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * Consider if VRFB assisted rotation is in use and if the virtual space for
 * the zero degree view needs to be mapped. The need for mapping also acts as
 * the trigger for setting up the hardware on the context in question. This
 * ensures that one does not attempt to access the virtual view before the
 * hardware is serving the address translations.
 */
static int setup_vrfb_rotation(struct fb_info *fbi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_mem_region *rg = ofbi->region;
	struct vrfb *vrfb = &rg->vrfb;
	struct fb_var_screeninfo *var = &fbi->var;
	struct fb_fix_screeninfo *fix = &fbi->fix;
	unsigned bytespp;
	bool yuv_mode;
	enum omap_color_mode mode;
	int r;
	bool reconf;

	if (!rg->size || ofbi->rotation_type != OMAP_DSS_ROT_VRFB)
		return 0;

	DBG("setup_vrfb_rotation\n");

	r = fb_mode_to_dss_mode(var, &mode);
	if (r)
		return r;

	bytespp = var->bits_per_pixel >> 3;

	yuv_mode = mode == OMAP_DSS_COLOR_YUV2 || mode == OMAP_DSS_COLOR_UYVY;

	/* We need to reconfigure VRFB if the resolution changes, if yuv mode
	 * is enabled/disabled, or if bytes per pixel changes */

	/* XXX we shouldn't allow this when framebuffer is mmapped */

	reconf = false;

	if (yuv_mode != vrfb->yuv_mode)
		reconf = true;
	else if (bytespp != vrfb->bytespp)
		reconf = true;
	else if (vrfb->xres != var->xres_virtual ||
			vrfb->yres != var->yres_virtual)
		reconf = true;

	if (vrfb->vaddr[0] && reconf) {
		fbi->screen_base = NULL;
		fix->smem_start = 0;
		fix->smem_len = 0;
		iounmap(vrfb->vaddr[0]);
		vrfb->vaddr[0] = NULL;
		DBG("setup_vrfb_rotation: reset fb\n");
	}

	if (vrfb->vaddr[0])
		return 0;

	omap_vrfb_setup(&rg->vrfb, rg->paddr,
			var->xres_virtual,
			var->yres_virtual,
			bytespp, yuv_mode);

	/* Now one can ioremap the 0 angle view */
	r = omap_vrfb_map_angle(vrfb, var->yres_virtual, 0);
	if (r)
		return r;

	/* used by open/write in fbmem.c */
	fbi->screen_base = ofbi->region->vrfb.vaddr[0];

	fix->smem_start = ofbi->region->vrfb.paddr[0];

	switch (var->nonstd) {
	case OMAPFB_COLOR_YUV422:
	case OMAPFB_COLOR_YUY422:
		fix->line_length =
			(OMAP_VRFB_LINE_LEN * var->bits_per_pixel) >> 2;
		break;
	default:
		fix->line_length =
			(OMAP_VRFB_LINE_LEN * var->bits_per_pixel) >> 3;
		break;
	}

	fix->smem_len = var->yres_virtual * fix->line_length;

	return 0;
}

int dss_mode_to_fb_mode(enum omap_color_mode dssmode,
			struct fb_var_screeninfo *var)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(omapfb_colormodes); ++i) {
		struct omapfb_colormode *mode = &omapfb_colormodes[i];
		if (dssmode == mode->dssmode) {
			assign_colormode_to_var(var, mode);
			return 0;
		}
	}
	return -ENOENT;
}

void set_fb_fix(struct fb_info *fbi)
{
	struct fb_fix_screeninfo *fix = &fbi->fix;
	struct fb_var_screeninfo *var = &fbi->var;
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_mem_region *rg = ofbi->region;

	DBG("set_fb_fix\n");

	/* used by open/write in fbmem.c */
	fbi->screen_base = (char __iomem *)omapfb_get_region_vaddr(ofbi);

	/* used by mmap in fbmem.c */
	if (ofbi->rotation_type == OMAP_DSS_ROT_VRFB) {
		switch (var->nonstd) {
		case OMAPFB_COLOR_YUV422:
		case OMAPFB_COLOR_YUY422:
			fix->line_length =
				(OMAP_VRFB_LINE_LEN * var->bits_per_pixel) >> 2;
			break;
		default:
			fix->line_length =
				(OMAP_VRFB_LINE_LEN * var->bits_per_pixel) >> 3;
			break;
		}

		fix->smem_len = var->yres_virtual * fix->line_length;
	} else {
		fix->line_length =
			(var->xres_virtual * var->bits_per_pixel) >> 3;
		fix->smem_len = rg->size;
	}

	fix->smem_start = omapfb_get_region_paddr(ofbi);

	fix->type = FB_TYPE_PACKED_PIXELS;

	if (var->nonstd)
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	else {
		switch (var->bits_per_pixel) {
		case 32:
		case 24:
		case 16:
		case 12:
			fix->visual = FB_VISUAL_TRUECOLOR;
			/* 12bpp is stored in 16 bits */
			break;
		case 1:
		case 2:
		case 4:
		case 8:
			fix->visual = FB_VISUAL_PSEUDOCOLOR;
			break;
		}
	}

	fix->accel = FB_ACCEL_NONE;

	fix->xpanstep = 1;
	fix->ypanstep = 1;
}

/* check new var and possibly modify it to be ok */
int check_fb_var(struct fb_info *fbi, struct fb_var_screeninfo *var)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omap_dss_device *display = fb2display(fbi);
	enum omap_color_mode mode = 0;
	int i;
	int r;

	DBG("check_fb_var %d\n", ofbi->id);

	WARN_ON(!atomic_read(&ofbi->region->lock_count));

	r = fb_mode_to_dss_mode(var, &mode);
	if (r) {
		DBG("cannot convert var to omap dss mode\n");
		return r;
	}

	for (i = 0; i < ofbi->num_overlays; ++i) {
		if ((ofbi->overlays[i]->supported_modes & mode) == 0) {
			DBG("invalid mode\n");
			return -EINVAL;
		}
	}

	if (var->rotate > 3)
		return -EINVAL;

	if (check_fb_res_bounds(var))
		return -EINVAL;

	/* When no memory is allocated ignore the size check */
	if (ofbi->region->size != 0 && check_fb_size(ofbi, var))
		return -EINVAL;

	if (var->xres + var->xoffset > var->xres_virtual)
		var->xoffset = var->xres_virtual - var->xres;
	if (var->yres + var->yoffset > var->yres_virtual)
		var->yoffset = var->yres_virtual - var->yres;

	DBG("xres = %d, yres = %d, vxres = %d, vyres = %d\n",
			var->xres, var->yres,
			var->xres_virtual, var->yres_virtual);

	if (display && display->driver->get_dimensions) {
		u32 w, h;
		display->driver->get_dimensions(display, &w, &h);
		var->width = DIV_ROUND_CLOSEST(w, 1000);
		var->height = DIV_ROUND_CLOSEST(h, 1000);
	} else {
		var->height = -1;
		var->width = -1;
	}

	var->grayscale          = 0;

	if (display && display->driver->get_timings) {
		struct omap_video_timings timings;
		display->driver->get_timings(display, &timings);

		/* pixclock in ps, the rest in pixclock */
		var->pixclock = timings.pixel_clock != 0 ?
			KHZ2PICOS(timings.pixel_clock) :
			0;
		var->left_margin = timings.hbp;
		var->right_margin = timings.hfp;
		var->upper_margin = timings.vbp;
		var->lower_margin = timings.vfp;
		var->hsync_len = timings.hsw;
		var->vsync_len = timings.vsw;
		var->sync |= timings.hsync_level == OMAPDSS_SIG_ACTIVE_HIGH ?
				FB_SYNC_HOR_HIGH_ACT : 0;
		var->sync |= timings.vsync_level == OMAPDSS_SIG_ACTIVE_HIGH ?
				FB_SYNC_VERT_HIGH_ACT : 0;
		var->vmode = timings.interlace ?
				FB_VMODE_INTERLACED : FB_VMODE_NONINTERLACED;
	} else {
		var->pixclock = 0;
		var->left_margin = 0;
		var->right_margin = 0;
		var->upper_margin = 0;
		var->lower_margin = 0;
		var->hsync_len = 0;
		var->vsync_len = 0;
		var->sync = 0;
		var->vmode = FB_VMODE_NONINTERLACED;
	}

	return 0;
}

/*
 * ---------------------------------------------------------------------------
 * fbdev framework callbacks
 * ---------------------------------------------------------------------------
 */
static int omapfb_open(struct fb_info *fbi, int user)
{
	return 0;
}

static int omapfb_release(struct fb_info *fbi, int user)
{
	return 0;
}

static unsigned calc_rotation_offset_dma(const struct fb_var_screeninfo *var,
		const struct fb_fix_screeninfo *fix, int rotation)
{
	unsigned offset;

	offset = var->yoffset * fix->line_length +
		var->xoffset * (var->bits_per_pixel >> 3);

	return offset;
}

static unsigned calc_rotation_offset_vrfb(const struct fb_var_screeninfo *var,
		const struct fb_fix_screeninfo *fix, int rotation)
{
	unsigned offset;

	if (rotation == FB_ROTATE_UD)
		offset = (var->yres_virtual - var->yres) *
			fix->line_length;
	else if (rotation == FB_ROTATE_CW)
		offset = (var->yres_virtual - var->yres) *
			(var->bits_per_pixel >> 3);
	else
		offset = 0;

	if (rotation == FB_ROTATE_UR)
		offset += var->yoffset * fix->line_length +
			var->xoffset * (var->bits_per_pixel >> 3);
	else if (rotation == FB_ROTATE_UD)
		offset -= var->yoffset * fix->line_length +
			var->xoffset * (var->bits_per_pixel >> 3);
	else if (rotation == FB_ROTATE_CW)
		offset -= var->xoffset * fix->line_length +
			var->yoffset * (var->bits_per_pixel >> 3);
	else if (rotation == FB_ROTATE_CCW)
		offset += var->xoffset * fix->line_length +
			var->yoffset * (var->bits_per_pixel >> 3);

	return offset;
}

static void omapfb_calc_addr(const struct omapfb_info *ofbi,
			     const struct fb_var_screeninfo *var,
			     const struct fb_fix_screeninfo *fix,
			     int rotation, u32 *paddr)
{
	u32 data_start_p;
	int offset;

	if (ofbi->rotation_type == OMAP_DSS_ROT_VRFB)
		data_start_p = omapfb_get_region_rot_paddr(ofbi, rotation);
	else
		data_start_p = omapfb_get_region_paddr(ofbi);

	if (ofbi->rotation_type == OMAP_DSS_ROT_VRFB)
		offset = calc_rotation_offset_vrfb(var, fix, rotation);
	else
		offset = calc_rotation_offset_dma(var, fix, rotation);

	data_start_p += offset;

	if (offset)
		DBG("offset %d, %d = %d\n",
		    var->xoffset, var->yoffset, offset);

	DBG("paddr %x\n", data_start_p);

	*paddr = data_start_p;
}

/* setup overlay according to the fb */
int omapfb_setup_overlay(struct fb_info *fbi, struct omap_overlay *ovl,
		u16 posx, u16 posy, u16 outw, u16 outh)
{
	int r = 0;
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct fb_var_screeninfo *var = &fbi->var;
	struct fb_fix_screeninfo *fix = &fbi->fix;
	enum omap_color_mode mode = 0;
	u32 data_start_p = 0;
	struct omap_overlay_info info;
	int xres, yres;
	int screen_width;
	int mirror;
	int rotation = var->rotate;
	int i;

	WARN_ON(!atomic_read(&ofbi->region->lock_count));

	for (i = 0; i < ofbi->num_overlays; i++) {
		if (ovl != ofbi->overlays[i])
			continue;

		rotation = (rotation + ofbi->rotation[i]) % 4;
		break;
	}

	DBG("setup_overlay %d, posx %d, posy %d, outw %d, outh %d\n", ofbi->id,
			posx, posy, outw, outh);

	if (rotation == FB_ROTATE_CW || rotation == FB_ROTATE_CCW) {
		xres = var->yres;
		yres = var->xres;
	} else {
		xres = var->xres;
		yres = var->yres;
	}

	if (ofbi->region->size)
		omapfb_calc_addr(ofbi, var, fix, rotation, &data_start_p);

	r = fb_mode_to_dss_mode(var, &mode);
	if (r) {
		DBG("fb_mode_to_dss_mode failed");
		goto err;
	}

	switch (var->nonstd) {
	case OMAPFB_COLOR_YUV422:
	case OMAPFB_COLOR_YUY422:
		if (ofbi->rotation_type == OMAP_DSS_ROT_VRFB) {
			screen_width = fix->line_length
				/ (var->bits_per_pixel >> 2);
			break;
		}
	default:
		screen_width = fix->line_length / (var->bits_per_pixel >> 3);
		break;
	}

	ovl->get_overlay_info(ovl, &info);

	if (ofbi->rotation_type == OMAP_DSS_ROT_VRFB)
		mirror = 0;
	else
		mirror = ofbi->mirror;

	info.paddr = data_start_p;
	info.screen_width = screen_width;
	info.width = xres;
	info.height = yres;
	info.color_mode = mode;
	info.rotation_type = ofbi->rotation_type;
	info.rotation = rotation;
	info.mirror = mirror;

	info.pos_x = posx;
	info.pos_y = posy;
	info.out_width = outw;
	info.out_height = outh;

	r = ovl->set_overlay_info(ovl, &info);
	if (r) {
		DBG("ovl->setup_overlay_info failed\n");
		goto err;
	}

	return 0;

err:
	DBG("setup_overlay failed\n");
	return r;
}

/* apply var to the overlay */
int omapfb_apply_changes(struct fb_info *fbi, int init)
{
	int r = 0;
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct fb_var_screeninfo *var = &fbi->var;
	struct omap_overlay *ovl;
	u16 posx, posy;
	u16 outw, outh;
	int i;

#ifdef DEBUG
	if (omapfb_test_pattern)
		fill_fb(fbi);
#endif

	WARN_ON(!atomic_read(&ofbi->region->lock_count));

	for (i = 0; i < ofbi->num_overlays; i++) {
		ovl = ofbi->overlays[i];

		DBG("apply_changes, fb %d, ovl %d\n", ofbi->id, ovl->id);

		if (ofbi->region->size == 0) {
			/* the fb is not available. disable the overlay */
			omapfb_overlay_enable(ovl, 0);
			if (!init && ovl->manager)
				ovl->manager->apply(ovl->manager);
			continue;
		}

		if (init || (ovl->caps & OMAP_DSS_OVL_CAP_SCALE) == 0) {
			int rotation = (var->rotate + ofbi->rotation[i]) % 4;
			if (rotation == FB_ROTATE_CW ||
					rotation == FB_ROTATE_CCW) {
				outw = var->yres;
				outh = var->xres;
			} else {
				outw = var->xres;
				outh = var->yres;
			}
		} else {
			struct omap_overlay_info info;
			ovl->get_overlay_info(ovl, &info);
			outw = info.out_width;
			outh = info.out_height;
		}

		if (init) {
			posx = 0;
			posy = 0;
		} else {
			struct omap_overlay_info info;
			ovl->get_overlay_info(ovl, &info);
			posx = info.pos_x;
			posy = info.pos_y;
		}

		r = omapfb_setup_overlay(fbi, ovl, posx, posy, outw, outh);
		if (r)
			goto err;

		if (!init && ovl->manager)
			ovl->manager->apply(ovl->manager);
	}
	return 0;
err:
	DBG("apply_changes failed\n");
	return r;
}

/* checks var and eventually tweaks it to something supported,
 * DO NOT MODIFY PAR */
static int omapfb_check_var(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	int r;

	DBG("check_var(%d)\n", FB2OFB(fbi)->id);

	omapfb_get_mem_region(ofbi->region);

	r = check_fb_var(fbi, var);

	omapfb_put_mem_region(ofbi->region);

	return r;
}

/* set the video mode according to info->var */
static int omapfb_set_par(struct fb_info *fbi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	int r;

	DBG("set_par(%d)\n", FB2OFB(fbi)->id);

	omapfb_get_mem_region(ofbi->region);

	set_fb_fix(fbi);

	r = setup_vrfb_rotation(fbi);
	if (r)
		goto out;

	r = omapfb_apply_changes(fbi, 0);

 out:
	omapfb_put_mem_region(ofbi->region);

	return r;
}

static int omapfb_pan_display(struct fb_var_screeninfo *var,
		struct fb_info *fbi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct fb_var_screeninfo new_var;
	int r;

	DBG("pan_display(%d)\n", FB2OFB(fbi)->id);

	if (var->xoffset == fbi->var.xoffset &&
	    var->yoffset == fbi->var.yoffset)
		return 0;

	new_var = fbi->var;
	new_var.xoffset = var->xoffset;
	new_var.yoffset = var->yoffset;

	fbi->var = new_var;

	omapfb_get_mem_region(ofbi->region);

	r = omapfb_apply_changes(fbi, 0);

	omapfb_put_mem_region(ofbi->region);

	return r;
}

static void mmap_user_open(struct vm_area_struct *vma)
{
	struct omapfb2_mem_region *rg = vma->vm_private_data;

	omapfb_get_mem_region(rg);
	atomic_inc(&rg->map_count);
	omapfb_put_mem_region(rg);
}

static void mmap_user_close(struct vm_area_struct *vma)
{
	struct omapfb2_mem_region *rg = vma->vm_private_data;

	omapfb_get_mem_region(rg);
	atomic_dec(&rg->map_count);
	omapfb_put_mem_region(rg);
}

static struct vm_operations_struct mmap_user_ops = {
	.open = mmap_user_open,
	.close = mmap_user_close,
};

static int omapfb_mmap(struct fb_info *fbi, struct vm_area_struct *vma)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct fb_fix_screeninfo *fix = &fbi->fix;
	struct omapfb2_mem_region *rg;
	unsigned long start;
	u32 len;
	int r;

	rg = omapfb_get_mem_region(ofbi->region);

	start = omapfb_get_region_paddr(ofbi);
	len = fix->smem_len;

	DBG("user mmap region start %lx, len %d, off %lx\n", start, len,
			vma->vm_pgoff << PAGE_SHIFT);

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vma->vm_ops = &mmap_user_ops;
	vma->vm_private_data = rg;

	r = vm_iomap_memory(vma, start, len);
	if (r)
		goto error;

	/* vm_ops.open won't be called for mmap itself. */
	atomic_inc(&rg->map_count);

	omapfb_put_mem_region(rg);

	return 0;

error:
	omapfb_put_mem_region(ofbi->region);

	return r;
}

/* Store a single color palette entry into a pseudo palette or the hardware
 * palette if one is available. For now we support only 16bpp and thus store
 * the entry only to the pseudo palette.
 */
static int _setcolreg(struct fb_info *fbi, u_int regno, u_int red, u_int green,
		u_int blue, u_int transp, int update_hw_pal)
{
	/*struct omapfb_info *ofbi = FB2OFB(fbi);*/
	/*struct omapfb2_device *fbdev = ofbi->fbdev;*/
	struct fb_var_screeninfo *var = &fbi->var;
	int r = 0;

	enum omapfb_color_format mode = OMAPFB_COLOR_RGB24U; /* XXX */

	/*switch (plane->color_mode) {*/
	switch (mode) {
	case OMAPFB_COLOR_YUV422:
	case OMAPFB_COLOR_YUV420:
	case OMAPFB_COLOR_YUY422:
		r = -EINVAL;
		break;
	case OMAPFB_COLOR_CLUT_8BPP:
	case OMAPFB_COLOR_CLUT_4BPP:
	case OMAPFB_COLOR_CLUT_2BPP:
	case OMAPFB_COLOR_CLUT_1BPP:
		/*
		   if (fbdev->ctrl->setcolreg)
		   r = fbdev->ctrl->setcolreg(regno, red, green, blue,
		   transp, update_hw_pal);
		   */
		/* Fallthrough */
		r = -EINVAL;
		break;
	case OMAPFB_COLOR_RGB565:
	case OMAPFB_COLOR_RGB444:
	case OMAPFB_COLOR_RGB24P:
	case OMAPFB_COLOR_RGB24U:
		if (r != 0)
			break;

		if (regno < 16) {
			u32 pal;
			pal = ((red >> (16 - var->red.length)) <<
					var->red.offset) |
				((green >> (16 - var->green.length)) <<
				 var->green.offset) |
				(blue >> (16 - var->blue.length));
			((u32 *)(fbi->pseudo_palette))[regno] = pal;
		}
		break;
	default:
		BUG();
	}
	return r;
}

static int omapfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		u_int transp, struct fb_info *info)
{
	DBG("setcolreg\n");

	return _setcolreg(info, regno, red, green, blue, transp, 1);
}

static int omapfb_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	int count, index, r;
	u16 *red, *green, *blue, *transp;
	u16 trans = 0xffff;

	DBG("setcmap\n");

	red     = cmap->red;
	green   = cmap->green;
	blue    = cmap->blue;
	transp  = cmap->transp;
	index   = cmap->start;

	for (count = 0; count < cmap->len; count++) {
		if (transp)
			trans = *transp++;
		r = _setcolreg(info, index++, *red++, *green++, *blue++, trans,
				count == cmap->len - 1);
		if (r != 0)
			return r;
	}

	return 0;
}

static int omapfb_blank(int blank, struct fb_info *fbi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omap_dss_device *display = fb2display(fbi);
	struct omapfb_display_data *d;
	int r = 0;

	if (!display)
		return -EINVAL;

	omapfb_lock(fbdev);

	d = get_display_data(fbdev, display);

	switch (blank) {
	case FB_BLANK_UNBLANK:
		if (display->state == OMAP_DSS_DISPLAY_ACTIVE)
			goto exit;

		r = display->driver->enable(display);

		if ((display->caps & OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE) &&
				d->update_mode == OMAPFB_AUTO_UPDATE &&
				!d->auto_update_work_enabled)
			omapfb_start_auto_update(fbdev, display);

		break;

	case FB_BLANK_NORMAL:
		/* FB_BLANK_NORMAL could be implemented.
		 * Needs DSS additions. */
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
		if (display->state != OMAP_DSS_DISPLAY_ACTIVE)
			goto exit;

		if (d->auto_update_work_enabled)
			omapfb_stop_auto_update(fbdev, display);

		display->driver->disable(display);

		break;

	default:
		r = -EINVAL;
	}

exit:
	omapfb_unlock(fbdev);

	return r;
}

#if 0
/* XXX fb_read and fb_write are needed for VRFB */
ssize_t omapfb_write(struct fb_info *info, const char __user *buf,
		size_t count, loff_t *ppos)
{
	DBG("omapfb_write %d, %lu\n", count, (unsigned long)*ppos);
	/* XXX needed for VRFB */
	return count;
}
#endif

static struct fb_ops omapfb_ops = {
	.owner          = THIS_MODULE,
	.fb_open        = omapfb_open,
	.fb_release     = omapfb_release,
	.fb_fillrect    = cfb_fillrect,
	.fb_copyarea    = cfb_copyarea,
	.fb_imageblit   = cfb_imageblit,
	.fb_blank       = omapfb_blank,
	.fb_ioctl       = omapfb_ioctl,
	.fb_check_var   = omapfb_check_var,
	.fb_set_par     = omapfb_set_par,
	.fb_pan_display = omapfb_pan_display,
	.fb_mmap	= omapfb_mmap,
	.fb_setcolreg	= omapfb_setcolreg,
	.fb_setcmap	= omapfb_setcmap,
	/*.fb_write	= omapfb_write,*/
};

static void omapfb_free_fbmem(struct fb_info *fbi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omapfb2_mem_region *rg;

	rg = ofbi->region;

	if (rg->token == NULL)
		return;

	WARN_ON(atomic_read(&rg->map_count));

	if (ofbi->rotation_type == OMAP_DSS_ROT_VRFB) {
		/* unmap the 0 angle rotation */
		if (rg->vrfb.vaddr[0]) {
			iounmap(rg->vrfb.vaddr[0]);
			rg->vrfb.vaddr[0] = NULL;
		}

		omap_vrfb_release_ctx(&rg->vrfb);
	}

	dma_free_attrs(fbdev->dev, rg->size, rg->token, rg->dma_handle,
			&rg->attrs);

	rg->token = NULL;
	rg->vaddr = NULL;
	rg->paddr = 0;
	rg->alloc = 0;
	rg->size = 0;
}

static void clear_fb_info(struct fb_info *fbi)
{
	memset(&fbi->var, 0, sizeof(fbi->var));
	memset(&fbi->fix, 0, sizeof(fbi->fix));
	strlcpy(fbi->fix.id, MODULE_NAME, sizeof(fbi->fix.id));
}

static int omapfb_free_all_fbmem(struct omapfb2_device *fbdev)
{
	int i;

	DBG("free all fbmem\n");

	for (i = 0; i < fbdev->num_fbs; i++) {
		struct fb_info *fbi = fbdev->fbs[i];
		omapfb_free_fbmem(fbi);
		clear_fb_info(fbi);
	}

	return 0;
}

static int omapfb_alloc_fbmem(struct fb_info *fbi, unsigned long size,
		unsigned long paddr)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omapfb2_mem_region *rg;
	void *token;
	DEFINE_DMA_ATTRS(attrs);
	dma_addr_t dma_handle;
	int r;

	rg = ofbi->region;

	rg->paddr = 0;
	rg->vaddr = NULL;
	memset(&rg->vrfb, 0, sizeof rg->vrfb);
	rg->size = 0;
	rg->type = 0;
	rg->alloc = false;
	rg->map = false;

	size = PAGE_ALIGN(size);

	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);

	if (ofbi->rotation_type == OMAP_DSS_ROT_VRFB)
		dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, &attrs);

	DBG("allocating %lu bytes for fb %d\n", size, ofbi->id);

	token = dma_alloc_attrs(fbdev->dev, size, &dma_handle,
			GFP_KERNEL, &attrs);

	if (token == NULL) {
		dev_err(fbdev->dev, "failed to allocate framebuffer\n");
		return -ENOMEM;
	}

	DBG("allocated VRAM paddr %lx, vaddr %p\n",
			(unsigned long)dma_handle, token);

	if (ofbi->rotation_type == OMAP_DSS_ROT_VRFB) {
		r = omap_vrfb_request_ctx(&rg->vrfb);
		if (r) {
			dma_free_attrs(fbdev->dev, size, token, dma_handle,
					&attrs);
			dev_err(fbdev->dev, "vrfb create ctx failed\n");
			return r;
		}
	}

	rg->attrs = attrs;
	rg->token = token;
	rg->dma_handle = dma_handle;

	rg->paddr = (unsigned long)dma_handle;
	rg->vaddr = (void __iomem *)token;
	rg->size = size;
	rg->alloc = 1;

	return 0;
}

/* allocate fbmem using display resolution as reference */
static int omapfb_alloc_fbmem_display(struct fb_info *fbi, unsigned long size,
		unsigned long paddr)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omap_dss_device *display;
	int bytespp;

	display =  fb2display(fbi);

	if (!display)
		return 0;

	switch (omapfb_get_recommended_bpp(fbdev, display)) {
	case 16:
		bytespp = 2;
		break;
	case 24:
		bytespp = 4;
		break;
	default:
		bytespp = 4;
		break;
	}

	if (!size) {
		u16 w, h;

		display->driver->get_resolution(display, &w, &h);

		if (ofbi->rotation_type == OMAP_DSS_ROT_VRFB) {
			size = max(omap_vrfb_min_phys_size(w, h, bytespp),
					omap_vrfb_min_phys_size(h, w, bytespp));

			DBG("adjusting fb mem size for VRFB, %u -> %lu\n",
					w * h * bytespp, size);
		} else {
			size = w * h * bytespp;
		}
	}

	if (!size)
		return 0;

	return omapfb_alloc_fbmem(fbi, size, paddr);
}

static int omapfb_parse_vram_param(const char *param, int max_entries,
		unsigned long *sizes, unsigned long *paddrs)
{
	int fbnum;
	unsigned long size;
	unsigned long paddr = 0;
	char *p, *start;

	start = (char *)param;

	while (1) {
		p = start;

		fbnum = simple_strtoul(p, &p, 10);

		if (p == start)
			return -EINVAL;

		if (*p != ':')
			return -EINVAL;

		if (fbnum >= max_entries)
			return -EINVAL;

		size = memparse(p + 1, &p);

		if (!size)
			return -EINVAL;

		paddr = 0;

		if (*p == '@') {
			paddr = simple_strtoul(p + 1, &p, 16);

			if (!paddr)
				return -EINVAL;

		}

		WARN_ONCE(paddr,
			"reserving memory at predefined address not supported\n");

		paddrs[fbnum] = paddr;
		sizes[fbnum] = size;

		if (*p == 0)
			break;

		if (*p != ',')
			return -EINVAL;

		++p;

		start = p;
	}

	return 0;
}

static int omapfb_allocate_all_fbs(struct omapfb2_device *fbdev)
{
	int i, r;
	unsigned long vram_sizes[10];
	unsigned long vram_paddrs[10];

	memset(&vram_sizes, 0, sizeof(vram_sizes));
	memset(&vram_paddrs, 0, sizeof(vram_paddrs));

	if (def_vram &&	omapfb_parse_vram_param(def_vram, 10,
				vram_sizes, vram_paddrs)) {
		dev_err(fbdev->dev, "failed to parse vram parameter\n");

		memset(&vram_sizes, 0, sizeof(vram_sizes));
		memset(&vram_paddrs, 0, sizeof(vram_paddrs));
	}

	for (i = 0; i < fbdev->num_fbs; i++) {
		/* allocate memory automatically only for fb0, or if
		 * excplicitly defined with vram or plat data option */
		if (i == 0 || vram_sizes[i] != 0) {
			r = omapfb_alloc_fbmem_display(fbdev->fbs[i],
					vram_sizes[i], vram_paddrs[i]);

			if (r)
				return r;
		}
	}

	for (i = 0; i < fbdev->num_fbs; i++) {
		struct omapfb_info *ofbi = FB2OFB(fbdev->fbs[i]);
		struct omapfb2_mem_region *rg;
		rg = ofbi->region;

		DBG("region%d phys %08x virt %p size=%lu\n",
				i,
				rg->paddr,
				rg->vaddr,
				rg->size);
	}

	return 0;
}

static void omapfb_clear_fb(struct fb_info *fbi)
{
	const struct fb_fillrect rect = {
		.dx = 0,
		.dy = 0,
		.width = fbi->var.xres_virtual,
		.height = fbi->var.yres_virtual,
		.color = 0,
		.rop = ROP_COPY,
	};

	cfb_fillrect(fbi, &rect);
}

int omapfb_realloc_fbmem(struct fb_info *fbi, unsigned long size, int type)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omapfb2_mem_region *rg = ofbi->region;
	unsigned long old_size = rg->size;
	unsigned long old_paddr = rg->paddr;
	int old_type = rg->type;
	int r;

	if (type != OMAPFB_MEMTYPE_SDRAM)
		return -EINVAL;

	size = PAGE_ALIGN(size);

	if (old_size == size && old_type == type)
		return 0;

	omapfb_free_fbmem(fbi);

	if (size == 0) {
		clear_fb_info(fbi);
		return 0;
	}

	r = omapfb_alloc_fbmem(fbi, size, 0);

	if (r) {
		if (old_size)
			omapfb_alloc_fbmem(fbi, old_size, old_paddr);

		if (rg->size == 0)
			clear_fb_info(fbi);

		return r;
	}

	if (old_size == size)
		return 0;

	if (old_size == 0) {
		DBG("initializing fb %d\n", ofbi->id);
		r = omapfb_fb_init(fbdev, fbi);
		if (r) {
			DBG("omapfb_fb_init failed\n");
			goto err;
		}
		r = omapfb_apply_changes(fbi, 1);
		if (r) {
			DBG("omapfb_apply_changes failed\n");
			goto err;
		}
	} else {
		struct fb_var_screeninfo new_var;
		memcpy(&new_var, &fbi->var, sizeof(new_var));
		r = check_fb_var(fbi, &new_var);
		if (r)
			goto err;
		memcpy(&fbi->var, &new_var, sizeof(fbi->var));
		set_fb_fix(fbi);
		r = setup_vrfb_rotation(fbi);
		if (r)
			goto err;
	}

	omapfb_clear_fb(fbi);

	return 0;
err:
	omapfb_free_fbmem(fbi);
	clear_fb_info(fbi);
	return r;
}

static void omapfb_auto_update_work(struct work_struct *work)
{
	struct omap_dss_device *dssdev;
	struct omap_dss_driver *dssdrv;
	struct omapfb_display_data *d;
	u16 w, h;
	unsigned int freq;
	struct omapfb2_device *fbdev;

	d = container_of(work, struct omapfb_display_data,
			auto_update_work.work);

	dssdev = d->dssdev;
	dssdrv = dssdev->driver;
	fbdev = d->fbdev;

	if (!dssdrv || !dssdrv->update)
		return;

	if (dssdrv->sync)
		dssdrv->sync(dssdev);

	dssdrv->get_resolution(dssdev, &w, &h);
	dssdrv->update(dssdev, 0, 0, w, h);

	freq = auto_update_freq;
	if (freq == 0)
		freq = 20;
	queue_delayed_work(fbdev->auto_update_wq,
			&d->auto_update_work, HZ / freq);
}

void omapfb_start_auto_update(struct omapfb2_device *fbdev,
		struct omap_dss_device *display)
{
	struct omapfb_display_data *d;

	if (fbdev->auto_update_wq == NULL) {
		struct workqueue_struct *wq;

		wq = create_singlethread_workqueue("omapfb_auto_update");

		if (wq == NULL) {
			dev_err(fbdev->dev, "Failed to create workqueue for "
					"auto-update\n");
			return;
		}

		fbdev->auto_update_wq = wq;
	}

	d = get_display_data(fbdev, display);

	INIT_DELAYED_WORK(&d->auto_update_work, omapfb_auto_update_work);

	d->auto_update_work_enabled = true;

	omapfb_auto_update_work(&d->auto_update_work.work);
}

void omapfb_stop_auto_update(struct omapfb2_device *fbdev,
		struct omap_dss_device *display)
{
	struct omapfb_display_data *d;

	d = get_display_data(fbdev, display);

	cancel_delayed_work_sync(&d->auto_update_work);

	d->auto_update_work_enabled = false;
}

/* initialize fb_info, var, fix to something sane based on the display */
static int omapfb_fb_init(struct omapfb2_device *fbdev, struct fb_info *fbi)
{
	struct fb_var_screeninfo *var = &fbi->var;
	struct omap_dss_device *display = fb2display(fbi);
	struct omapfb_info *ofbi = FB2OFB(fbi);
	int r = 0;

	fbi->fbops = &omapfb_ops;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->pseudo_palette = fbdev->pseudo_palette;

	if (ofbi->region->size == 0) {
		clear_fb_info(fbi);
		return 0;
	}

	var->nonstd = 0;
	var->bits_per_pixel = 0;

	var->rotate = def_rotate;

	if (display) {
		u16 w, h;
		int rotation = (var->rotate + ofbi->rotation[0]) % 4;

		display->driver->get_resolution(display, &w, &h);

		if (rotation == FB_ROTATE_CW ||
				rotation == FB_ROTATE_CCW) {
			var->xres = h;
			var->yres = w;
		} else {
			var->xres = w;
			var->yres = h;
		}

		var->xres_virtual = var->xres;
		var->yres_virtual = var->yres;

		if (!var->bits_per_pixel) {
			switch (omapfb_get_recommended_bpp(fbdev, display)) {
			case 16:
				var->bits_per_pixel = 16;
				break;
			case 24:
				var->bits_per_pixel = 32;
				break;
			default:
				dev_err(fbdev->dev, "illegal display "
						"bpp\n");
				return -EINVAL;
			}
		}
	} else {
		/* if there's no display, let's just guess some basic values */
		var->xres = 320;
		var->yres = 240;
		var->xres_virtual = var->xres;
		var->yres_virtual = var->yres;
		if (!var->bits_per_pixel)
			var->bits_per_pixel = 16;
	}

	r = check_fb_var(fbi, var);
	if (r)
		goto err;

	set_fb_fix(fbi);
	r = setup_vrfb_rotation(fbi);
	if (r)
		goto err;

	r = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (r)
		dev_err(fbdev->dev, "unable to allocate color map memory\n");

err:
	return r;
}

static void fbinfo_cleanup(struct omapfb2_device *fbdev, struct fb_info *fbi)
{
	fb_dealloc_cmap(&fbi->cmap);
}


static void omapfb_free_resources(struct omapfb2_device *fbdev)
{
	int i;

	DBG("free_resources\n");

	if (fbdev == NULL)
		return;

	for (i = 0; i < fbdev->num_fbs; i++)
		unregister_framebuffer(fbdev->fbs[i]);

	/* free the reserved fbmem */
	omapfb_free_all_fbmem(fbdev);

	for (i = 0; i < fbdev->num_fbs; i++) {
		fbinfo_cleanup(fbdev, fbdev->fbs[i]);
		framebuffer_release(fbdev->fbs[i]);
	}

	for (i = 0; i < fbdev->num_displays; i++) {
		struct omap_dss_device *dssdev = fbdev->displays[i].dssdev;

		if (fbdev->displays[i].auto_update_work_enabled)
			omapfb_stop_auto_update(fbdev, dssdev);

		if (dssdev->state != OMAP_DSS_DISPLAY_DISABLED)
			dssdev->driver->disable(dssdev);

		omap_dss_put_device(dssdev);
	}

	if (fbdev->auto_update_wq != NULL) {
		flush_workqueue(fbdev->auto_update_wq);
		destroy_workqueue(fbdev->auto_update_wq);
		fbdev->auto_update_wq = NULL;
	}

	dev_set_drvdata(fbdev->dev, NULL);
}

static int omapfb_create_framebuffers(struct omapfb2_device *fbdev)
{
	int r, i;

	fbdev->num_fbs = 0;

	DBG("create %d framebuffers\n",	CONFIG_FB_OMAP2_NUM_FBS);

	/* allocate fb_infos */
	for (i = 0; i < CONFIG_FB_OMAP2_NUM_FBS; i++) {
		struct fb_info *fbi;
		struct omapfb_info *ofbi;

		fbi = framebuffer_alloc(sizeof(struct omapfb_info),
				fbdev->dev);

		if (fbi == NULL) {
			dev_err(fbdev->dev,
				"unable to allocate memory for plane info\n");
			return -ENOMEM;
		}

		clear_fb_info(fbi);

		fbdev->fbs[i] = fbi;

		ofbi = FB2OFB(fbi);
		ofbi->fbdev = fbdev;
		ofbi->id = i;

		ofbi->region = &fbdev->regions[i];
		ofbi->region->id = i;
		init_rwsem(&ofbi->region->lock);

		/* assign these early, so that fb alloc can use them */
		ofbi->rotation_type = def_vrfb ? OMAP_DSS_ROT_VRFB :
			OMAP_DSS_ROT_DMA;
		ofbi->mirror = def_mirror;

		fbdev->num_fbs++;
	}

	DBG("fb_infos allocated\n");

	/* assign overlays for the fbs */
	for (i = 0; i < min(fbdev->num_fbs, fbdev->num_overlays); i++) {
		struct omapfb_info *ofbi = FB2OFB(fbdev->fbs[i]);

		ofbi->overlays[0] = fbdev->overlays[i];
		ofbi->num_overlays = 1;
	}

	/* allocate fb memories */
	r = omapfb_allocate_all_fbs(fbdev);
	if (r) {
		dev_err(fbdev->dev, "failed to allocate fbmem\n");
		return r;
	}

	DBG("fbmems allocated\n");

	/* setup fb_infos */
	for (i = 0; i < fbdev->num_fbs; i++) {
		struct fb_info *fbi = fbdev->fbs[i];
		struct omapfb_info *ofbi = FB2OFB(fbi);

		omapfb_get_mem_region(ofbi->region);
		r = omapfb_fb_init(fbdev, fbi);
		omapfb_put_mem_region(ofbi->region);

		if (r) {
			dev_err(fbdev->dev, "failed to setup fb_info\n");
			return r;
		}
	}

	for (i = 0; i < fbdev->num_fbs; i++) {
		struct fb_info *fbi = fbdev->fbs[i];
		struct omapfb_info *ofbi = FB2OFB(fbi);

		if (ofbi->region->size == 0)
			continue;

		omapfb_clear_fb(fbi);
	}

	DBG("fb_infos initialized\n");

	for (i = 0; i < fbdev->num_fbs; i++) {
		r = register_framebuffer(fbdev->fbs[i]);
		if (r != 0) {
			dev_err(fbdev->dev,
				"registering framebuffer %d failed\n", i);
			return r;
		}
	}

	DBG("framebuffers registered\n");

	for (i = 0; i < fbdev->num_fbs; i++) {
		struct fb_info *fbi = fbdev->fbs[i];
		struct omapfb_info *ofbi = FB2OFB(fbi);

		omapfb_get_mem_region(ofbi->region);
		r = omapfb_apply_changes(fbi, 1);
		omapfb_put_mem_region(ofbi->region);

		if (r) {
			dev_err(fbdev->dev, "failed to change mode\n");
			return r;
		}
	}

	/* Enable fb0 */
	if (fbdev->num_fbs > 0) {
		struct omapfb_info *ofbi = FB2OFB(fbdev->fbs[0]);

		if (ofbi->num_overlays > 0) {
			struct omap_overlay *ovl = ofbi->overlays[0];

			ovl->manager->apply(ovl->manager);

			r = omapfb_overlay_enable(ovl, 1);

			if (r) {
				dev_err(fbdev->dev,
						"failed to enable overlay\n");
				return r;
			}
		}
	}

	DBG("create_framebuffers done\n");

	return 0;
}

static int omapfb_mode_to_timings(const char *mode_str,
		struct omap_dss_device *display,
		struct omap_video_timings *timings, u8 *bpp)
{
	struct fb_info *fbi;
	struct fb_var_screeninfo *var;
	struct fb_ops *fbops;
	int r;

#ifdef CONFIG_OMAP2_DSS_VENC
	if (strcmp(mode_str, "pal") == 0) {
		*timings = omap_dss_pal_timings;
		*bpp = 24;
		return 0;
	} else if (strcmp(mode_str, "ntsc") == 0) {
		*timings = omap_dss_ntsc_timings;
		*bpp = 24;
		return 0;
	}
#endif

	/* this is quite a hack, but I wanted to use the modedb and for
	 * that we need fb_info and var, so we create dummy ones */

	*bpp = 0;
	fbi = NULL;
	var = NULL;
	fbops = NULL;

	fbi = kzalloc(sizeof(*fbi), GFP_KERNEL);
	if (fbi == NULL) {
		r = -ENOMEM;
		goto err;
	}

	var = kzalloc(sizeof(*var), GFP_KERNEL);
	if (var == NULL) {
		r = -ENOMEM;
		goto err;
	}

	fbops = kzalloc(sizeof(*fbops), GFP_KERNEL);
	if (fbops == NULL) {
		r = -ENOMEM;
		goto err;
	}

	fbi->fbops = fbops;

	r = fb_find_mode(var, fbi, mode_str, NULL, 0, NULL, 24);
	if (r == 0) {
		r = -EINVAL;
		goto err;
	}

	if (display->driver->get_timings) {
		display->driver->get_timings(display, timings);
	} else {
		timings->data_pclk_edge = OMAPDSS_DRIVE_SIG_RISING_EDGE;
		timings->de_level = OMAPDSS_SIG_ACTIVE_HIGH;
		timings->sync_pclk_edge = OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES;
	}

	timings->pixel_clock = PICOS2KHZ(var->pixclock);
	timings->hbp = var->left_margin;
	timings->hfp = var->right_margin;
	timings->vbp = var->upper_margin;
	timings->vfp = var->lower_margin;
	timings->hsw = var->hsync_len;
	timings->vsw = var->vsync_len;
	timings->x_res = var->xres;
	timings->y_res = var->yres;
	timings->hsync_level = var->sync & FB_SYNC_HOR_HIGH_ACT ?
				OMAPDSS_SIG_ACTIVE_HIGH :
				OMAPDSS_SIG_ACTIVE_LOW;
	timings->vsync_level = var->sync & FB_SYNC_VERT_HIGH_ACT ?
				OMAPDSS_SIG_ACTIVE_HIGH :
				OMAPDSS_SIG_ACTIVE_LOW;
	timings->interlace = var->vmode & FB_VMODE_INTERLACED;

	switch (var->bits_per_pixel) {
	case 16:
		*bpp = 16;
		break;
	case 24:
	case 32:
	default:
		*bpp = 24;
		break;
	}

	r = 0;

err:
	kfree(fbi);
	kfree(var);
	kfree(fbops);

	return r;
}

static int omapfb_set_def_mode(struct omapfb2_device *fbdev,
		struct omap_dss_device *display, char *mode_str)
{
	int r;
	u8 bpp;
	struct omap_video_timings timings, temp_timings;
	struct omapfb_display_data *d;

	r = omapfb_mode_to_timings(mode_str, display, &timings, &bpp);
	if (r)
		return r;

	d = get_display_data(fbdev, display);
	d->bpp_override = bpp;

	if (display->driver->check_timings) {
		r = display->driver->check_timings(display, &timings);
		if (r)
			return r;
	} else {
		/* If check_timings is not present compare xres and yres */
		if (display->driver->get_timings) {
			display->driver->get_timings(display, &temp_timings);

			if (temp_timings.x_res != timings.x_res ||
				temp_timings.y_res != timings.y_res)
				return -EINVAL;
		}
	}

	if (display->driver->set_timings)
			display->driver->set_timings(display, &timings);

	return 0;
}

static int omapfb_get_recommended_bpp(struct omapfb2_device *fbdev,
		struct omap_dss_device *dssdev)
{
	struct omapfb_display_data *d;

	BUG_ON(dssdev->driver->get_recommended_bpp == NULL);

	d = get_display_data(fbdev, dssdev);

	if (d->bpp_override != 0)
		return d->bpp_override;

	return dssdev->driver->get_recommended_bpp(dssdev);
}

static int omapfb_parse_def_modes(struct omapfb2_device *fbdev)
{
	char *str, *options, *this_opt;
	int r = 0;

	str = kstrdup(def_mode, GFP_KERNEL);
	if (!str)
		return -ENOMEM;
	options = str;

	while (!r && (this_opt = strsep(&options, ",")) != NULL) {
		char *p, *display_str, *mode_str;
		struct omap_dss_device *display;
		int i;

		p = strchr(this_opt, ':');
		if (!p) {
			r = -EINVAL;
			break;
		}

		*p = 0;
		display_str = this_opt;
		mode_str = p + 1;

		display = NULL;
		for (i = 0; i < fbdev->num_displays; ++i) {
			if (strcmp(fbdev->displays[i].dssdev->name,
						display_str) == 0) {
				display = fbdev->displays[i].dssdev;
				break;
			}
		}

		if (!display) {
			r = -EINVAL;
			break;
		}

		r = omapfb_set_def_mode(fbdev, display, mode_str);
		if (r)
			break;
	}

	kfree(str);

	return r;
}

static void fb_videomode_to_omap_timings(struct fb_videomode *m,
		struct omap_dss_device *display,
		struct omap_video_timings *t)
{
	if (display->driver->get_timings) {
		display->driver->get_timings(display, t);
	} else {
		t->data_pclk_edge = OMAPDSS_DRIVE_SIG_RISING_EDGE;
		t->de_level = OMAPDSS_SIG_ACTIVE_HIGH;
		t->sync_pclk_edge = OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES;
	}

	t->x_res = m->xres;
	t->y_res = m->yres;
	t->pixel_clock = PICOS2KHZ(m->pixclock);
	t->hsw = m->hsync_len;
	t->hfp = m->right_margin;
	t->hbp = m->left_margin;
	t->vsw = m->vsync_len;
	t->vfp = m->lower_margin;
	t->vbp = m->upper_margin;
	t->hsync_level = m->sync & FB_SYNC_HOR_HIGH_ACT ?
				OMAPDSS_SIG_ACTIVE_HIGH :
				OMAPDSS_SIG_ACTIVE_LOW;
	t->vsync_level = m->sync & FB_SYNC_VERT_HIGH_ACT ?
				OMAPDSS_SIG_ACTIVE_HIGH :
				OMAPDSS_SIG_ACTIVE_LOW;
	t->interlace = m->vmode & FB_VMODE_INTERLACED;
}

static int omapfb_find_best_mode(struct omap_dss_device *display,
		struct omap_video_timings *timings)
{
	struct fb_monspecs *specs;
	u8 *edid;
	int r, i, best_idx, len;

	if (!display->driver->read_edid)
		return -ENODEV;

	len = 0x80 * 2;
	edid = kmalloc(len, GFP_KERNEL);
	if (edid == NULL)
		return -ENOMEM;

	r = display->driver->read_edid(display, edid, len);
	if (r < 0)
		goto err1;

	specs = kzalloc(sizeof(*specs), GFP_KERNEL);
	if (specs == NULL) {
		r = -ENOMEM;
		goto err1;
	}

	fb_edid_to_monspecs(edid, specs);

	best_idx = -1;

	for (i = 0; i < specs->modedb_len; ++i) {
		struct fb_videomode *m;
		struct omap_video_timings t;

		m = &specs->modedb[i];

		if (m->pixclock == 0)
			continue;

		/* skip repeated pixel modes */
		if (m->xres == 2880 || m->xres == 1440)
			continue;

		if (m->vmode & FB_VMODE_INTERLACED ||
				m->vmode & FB_VMODE_DOUBLE)
			continue;

		fb_videomode_to_omap_timings(m, display, &t);

		r = display->driver->check_timings(display, &t);
		if (r == 0) {
			best_idx = i;
			break;
		}
	}

	if (best_idx == -1) {
		r = -ENOENT;
		goto err2;
	}

	fb_videomode_to_omap_timings(&specs->modedb[best_idx], display,
		timings);

	r = 0;

err2:
	fb_destroy_modedb(specs->modedb);
	kfree(specs);
err1:
	kfree(edid);

	return r;
}

static int omapfb_init_display(struct omapfb2_device *fbdev,
		struct omap_dss_device *dssdev)
{
	struct omap_dss_driver *dssdrv = dssdev->driver;
	struct omapfb_display_data *d;
	int r;

	r = dssdrv->enable(dssdev);
	if (r) {
		dev_warn(fbdev->dev, "Failed to enable display '%s'\n",
				dssdev->name);
		return r;
	}

	d = get_display_data(fbdev, dssdev);

	d->fbdev = fbdev;

	if (dssdev->caps & OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE) {
		u16 w, h;

		if (auto_update) {
			omapfb_start_auto_update(fbdev, dssdev);
			d->update_mode = OMAPFB_AUTO_UPDATE;
		} else {
			d->update_mode = OMAPFB_MANUAL_UPDATE;
		}

		if (dssdrv->enable_te) {
			r = dssdrv->enable_te(dssdev, 1);
			if (r) {
				dev_err(fbdev->dev, "Failed to set TE\n");
				return r;
			}
		}

		dssdrv->get_resolution(dssdev, &w, &h);
		r = dssdrv->update(dssdev, 0, 0, w, h);
		if (r) {
			dev_err(fbdev->dev,
					"Failed to update display\n");
			return r;
		}
	} else {
		d->update_mode = OMAPFB_AUTO_UPDATE;
	}

	return 0;
}

static int omapfb_init_connections(struct omapfb2_device *fbdev,
		struct omap_dss_device *def_dssdev)
{
	int i, r;
	struct omap_overlay_manager *mgr;

	for (i = 0; i < fbdev->num_displays; ++i) {
		struct omap_dss_device *dssdev = fbdev->displays[i].dssdev;
		struct omap_dss_output *out;

		out = omapdss_find_output_from_display(dssdev);
		if (!out)
			continue;

		mgr = omap_dss_get_overlay_manager(out->dispc_channel);
		if (!mgr)
			continue;

		if (mgr->output)
			mgr->unset_output(mgr);

		mgr->set_output(mgr, out);
	}

	mgr = omapdss_find_mgr_from_display(def_dssdev);

	if (!mgr) {
		dev_err(fbdev->dev, "no ovl manager for the default display\n");
		return -EINVAL;
	}

	for (i = 0; i < fbdev->num_overlays; i++) {
		struct omap_overlay *ovl = fbdev->overlays[i];

		if (ovl->manager)
			ovl->unset_manager(ovl);

		r = ovl->set_manager(ovl, mgr);
		if (r)
			dev_warn(fbdev->dev,
					"failed to connect overlay %s to manager %s\n",
					ovl->name, mgr->name);
	}

	return 0;
}

static int omapfb_probe(struct platform_device *pdev)
{
	struct omapfb2_device *fbdev = NULL;
	int r = 0;
	int i;
	struct omap_dss_device *def_display;
	struct omap_dss_device *dssdev;

	DBG("omapfb_probe\n");

	if (omapdss_is_initialized() == false)
		return -EPROBE_DEFER;

	if (pdev->num_resources != 0) {
		dev_err(&pdev->dev, "probed for an unknown device\n");
		r = -ENODEV;
		goto err0;
	}

	fbdev = devm_kzalloc(&pdev->dev, sizeof(struct omapfb2_device),
			GFP_KERNEL);
	if (fbdev == NULL) {
		r = -ENOMEM;
		goto err0;
	}

	if (def_vrfb && !omap_vrfb_supported()) {
		def_vrfb = 0;
		dev_warn(&pdev->dev, "VRFB is not supported on this hardware, "
				"ignoring the module parameter vrfb=y\n");
	}

	r = omapdss_compat_init();
	if (r)
		goto err0;

	mutex_init(&fbdev->mtx);

	fbdev->dev = &pdev->dev;
	platform_set_drvdata(pdev, fbdev);

	fbdev->num_displays = 0;
	dssdev = NULL;
	for_each_dss_dev(dssdev) {
		struct omapfb_display_data *d;

		omap_dss_get_device(dssdev);

		if (!dssdev->driver) {
			dev_warn(&pdev->dev, "no driver for display: %s\n",
				dssdev->name);
			omap_dss_put_device(dssdev);
			continue;
		}

		d = &fbdev->displays[fbdev->num_displays++];
		d->dssdev = dssdev;
		if (dssdev->caps & OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE)
			d->update_mode = OMAPFB_MANUAL_UPDATE;
		else
			d->update_mode = OMAPFB_AUTO_UPDATE;
	}

	if (fbdev->num_displays == 0) {
		dev_err(&pdev->dev, "no displays\n");
		r = -EPROBE_DEFER;
		goto cleanup;
	}

	fbdev->num_overlays = omap_dss_get_num_overlays();
	for (i = 0; i < fbdev->num_overlays; i++)
		fbdev->overlays[i] = omap_dss_get_overlay(i);

	fbdev->num_managers = omap_dss_get_num_overlay_managers();
	for (i = 0; i < fbdev->num_managers; i++)
		fbdev->managers[i] = omap_dss_get_overlay_manager(i);

	def_display = NULL;

	for (i = 0; i < fbdev->num_displays; ++i) {
		struct omap_dss_device *dssdev;
		const char *def_name;

		def_name = omapdss_get_default_display_name();

		dssdev = fbdev->displays[i].dssdev;

		if (def_name == NULL ||
			(dssdev->name && strcmp(def_name, dssdev->name) == 0)) {
			def_display = dssdev;
			break;
		}
	}

	if (def_display == NULL) {
		dev_err(fbdev->dev, "failed to find default display\n");
		r = -EINVAL;
		goto cleanup;
	}

	r = omapfb_init_connections(fbdev, def_display);
	if (r) {
		dev_err(fbdev->dev, "failed to init overlay connections\n");
		goto cleanup;
	}

	if (def_mode && strlen(def_mode) > 0) {
		if (omapfb_parse_def_modes(fbdev))
			dev_warn(&pdev->dev, "cannot parse default modes\n");
	} else if (def_display && def_display->driver->set_timings &&
			def_display->driver->check_timings) {
		struct omap_video_timings t;

		r = omapfb_find_best_mode(def_display, &t);

		if (r == 0)
			def_display->driver->set_timings(def_display, &t);
	}

	r = omapfb_create_framebuffers(fbdev);
	if (r)
		goto cleanup;

	for (i = 0; i < fbdev->num_managers; i++) {
		struct omap_overlay_manager *mgr;
		mgr = fbdev->managers[i];
		r = mgr->apply(mgr);
		if (r)
			dev_warn(fbdev->dev, "failed to apply dispc config\n");
	}

	DBG("mgr->apply'ed\n");

	if (def_display) {
		r = omapfb_init_display(fbdev, def_display);
		if (r) {
			dev_err(fbdev->dev,
					"failed to initialize default "
					"display\n");
			goto cleanup;
		}
	}

	DBG("create sysfs for fbs\n");
	r = omapfb_create_sysfs(fbdev);
	if (r) {
		dev_err(fbdev->dev, "failed to create sysfs entries\n");
		goto cleanup;
	}

	return 0;

cleanup:
	omapfb_free_resources(fbdev);
	omapdss_compat_uninit();
err0:
	dev_err(&pdev->dev, "failed to setup omapfb\n");
	return r;
}

static int __exit omapfb_remove(struct platform_device *pdev)
{
	struct omapfb2_device *fbdev = platform_get_drvdata(pdev);

	/* FIXME: wait till completion of pending events */

	omapfb_remove_sysfs(fbdev);

	omapfb_free_resources(fbdev);

	omapdss_compat_uninit();

	return 0;
}

static struct platform_driver omapfb_driver = {
	.probe		= omapfb_probe,
	.remove         = __exit_p(omapfb_remove),
	.driver         = {
		.name   = "omapfb",
		.owner  = THIS_MODULE,
	},
};

module_param_named(mode, def_mode, charp, 0);
module_param_named(vram, def_vram, charp, 0);
module_param_named(rotate, def_rotate, int, 0);
module_param_named(vrfb, def_vrfb, bool, 0);
module_param_named(mirror, def_mirror, bool, 0);

module_platform_driver(omapfb_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("OMAP2/3 Framebuffer");
MODULE_LICENSE("GPL v2");
