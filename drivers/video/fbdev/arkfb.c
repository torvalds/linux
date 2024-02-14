/*
 *  linux/drivers/video/arkfb.c -- Frame buffer device driver for ARK 2000PV
 *  with ICS 5342 dac (it is easy to add support for different dacs).
 *
 *  Copyright (c) 2007 Ondrej Zajicek <santiago@crfreenet.org>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 *  Code is based on s3fb
 */

#include <linux/aperture.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/svga.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/console.h> /* Why should fb driver call console functions? because console_lock() */
#include <video/vga.h>

struct arkfb_info {
	int mclk_freq;
	int wc_cookie;

	struct dac_info *dac;
	struct vgastate state;
	struct mutex open_lock;
	unsigned int ref_count;
	u32 pseudo_palette[16];
};


/* ------------------------------------------------------------------------- */


static const struct svga_fb_format arkfb_formats[] = {
	{ 0,  {0, 6, 0},  {0, 6, 0},  {0, 6, 0}, {0, 0, 0}, 0,
		FB_TYPE_TEXT, FB_AUX_TEXT_SVGA_STEP4,	FB_VISUAL_PSEUDOCOLOR, 8, 8},
	{ 4,  {0, 6, 0},  {0, 6, 0},  {0, 6, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_PSEUDOCOLOR, 8, 16},
	{ 4,  {0, 6, 0},  {0, 6, 0},  {0, 6, 0}, {0, 0, 0}, 1,
		FB_TYPE_INTERLEAVED_PLANES, 1,		FB_VISUAL_PSEUDOCOLOR, 8, 16},
	{ 8,  {0, 6, 0},  {0, 6, 0},  {0, 6, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_PSEUDOCOLOR, 8, 8},
	{16,  {10, 5, 0}, {5, 5, 0},  {0, 5, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_TRUECOLOR, 4, 4},
	{16,  {11, 5, 0}, {5, 6, 0},  {0, 5, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_TRUECOLOR, 4, 4},
	{24,  {16, 8, 0}, {8, 8, 0},  {0, 8, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_TRUECOLOR, 8, 8},
	{32,  {16, 8, 0}, {8, 8, 0},  {0, 8, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_TRUECOLOR, 2, 2},
	SVGA_FORMAT_END
};


/* CRT timing register sets */

static const struct vga_regset ark_h_total_regs[]        = {{0x00, 0, 7}, {0x41, 7, 7}, VGA_REGSET_END};
static const struct vga_regset ark_h_display_regs[]      = {{0x01, 0, 7}, {0x41, 6, 6}, VGA_REGSET_END};
static const struct vga_regset ark_h_blank_start_regs[]  = {{0x02, 0, 7}, {0x41, 5, 5}, VGA_REGSET_END};
static const struct vga_regset ark_h_blank_end_regs[]    = {{0x03, 0, 4}, {0x05, 7, 7	}, VGA_REGSET_END};
static const struct vga_regset ark_h_sync_start_regs[]   = {{0x04, 0, 7}, {0x41, 4, 4}, VGA_REGSET_END};
static const struct vga_regset ark_h_sync_end_regs[]     = {{0x05, 0, 4}, VGA_REGSET_END};

static const struct vga_regset ark_v_total_regs[]        = {{0x06, 0, 7}, {0x07, 0, 0}, {0x07, 5, 5}, {0x40, 7, 7}, VGA_REGSET_END};
static const struct vga_regset ark_v_display_regs[]      = {{0x12, 0, 7}, {0x07, 1, 1}, {0x07, 6, 6}, {0x40, 6, 6}, VGA_REGSET_END};
static const struct vga_regset ark_v_blank_start_regs[]  = {{0x15, 0, 7}, {0x07, 3, 3}, {0x09, 5, 5}, {0x40, 5, 5}, VGA_REGSET_END};
// const struct vga_regset ark_v_blank_end_regs[]    = {{0x16, 0, 6}, VGA_REGSET_END};
static const struct vga_regset ark_v_blank_end_regs[]    = {{0x16, 0, 7}, VGA_REGSET_END};
static const struct vga_regset ark_v_sync_start_regs[]   = {{0x10, 0, 7}, {0x07, 2, 2}, {0x07, 7, 7}, {0x40, 4, 4}, VGA_REGSET_END};
static const struct vga_regset ark_v_sync_end_regs[]     = {{0x11, 0, 3}, VGA_REGSET_END};

static const struct vga_regset ark_line_compare_regs[]   = {{0x18, 0, 7}, {0x07, 4, 4}, {0x09, 6, 6}, VGA_REGSET_END};
static const struct vga_regset ark_start_address_regs[]  = {{0x0d, 0, 7}, {0x0c, 0, 7}, {0x40, 0, 2}, VGA_REGSET_END};
static const struct vga_regset ark_offset_regs[]         = {{0x13, 0, 7}, {0x41, 3, 3}, VGA_REGSET_END};

static const struct svga_timing_regs ark_timing_regs     = {
	ark_h_total_regs, ark_h_display_regs, ark_h_blank_start_regs,
	ark_h_blank_end_regs, ark_h_sync_start_regs, ark_h_sync_end_regs,
	ark_v_total_regs, ark_v_display_regs, ark_v_blank_start_regs,
	ark_v_blank_end_regs, ark_v_sync_start_regs, ark_v_sync_end_regs,
};


/* ------------------------------------------------------------------------- */


/* Module parameters */

static char *mode_option = "640x480-8@60";

MODULE_AUTHOR("(c) 2007 Ondrej Zajicek <santiago@crfreenet.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("fbdev driver for ARK 2000PV");

module_param(mode_option, charp, 0444);
MODULE_PARM_DESC(mode_option, "Default video mode ('640x480-8@60', etc)");
module_param_named(mode, mode_option, charp, 0444);
MODULE_PARM_DESC(mode, "Default video mode ('640x480-8@60', etc) (deprecated)");

static int threshold = 4;

module_param(threshold, int, 0644);
MODULE_PARM_DESC(threshold, "FIFO threshold");


/* ------------------------------------------------------------------------- */


static void arkfb_settile(struct fb_info *info, struct fb_tilemap *map)
{
	const u8 *font = map->data;
	u8 __iomem *fb = (u8 __iomem *)info->screen_base;
	int i, c;

	if ((map->width != 8) || (map->height != 16) ||
	    (map->depth != 1) || (map->length != 256)) {
		fb_err(info, "unsupported font parameters: width %d, height %d, depth %d, length %d\n",
		       map->width, map->height, map->depth, map->length);
		return;
	}

	fb += 2;
	for (c = 0; c < map->length; c++) {
		for (i = 0; i < map->height; i++) {
			fb_writeb(font[i], &fb[i * 4]);
			fb_writeb(font[i], &fb[i * 4 + (128 * 8)]);
		}
		fb += 128;

		if ((c % 8) == 7)
			fb += 128*8;

		font += map->height;
	}
}

static void arkfb_tilecursor(struct fb_info *info, struct fb_tilecursor *cursor)
{
	struct arkfb_info *par = info->par;

	svga_tilecursor(par->state.vgabase, info, cursor);
}

static struct fb_tile_ops arkfb_tile_ops = {
	.fb_settile	= arkfb_settile,
	.fb_tilecopy	= svga_tilecopy,
	.fb_tilefill    = svga_tilefill,
	.fb_tileblit    = svga_tileblit,
	.fb_tilecursor  = arkfb_tilecursor,
	.fb_get_tilemax = svga_get_tilemax,
};


/* ------------------------------------------------------------------------- */


/* image data is MSB-first, fb structure is MSB-first too */
static inline u32 expand_color(u32 c)
{
	return ((c & 1) | ((c & 2) << 7) | ((c & 4) << 14) | ((c & 8) << 21)) * 0xFF;
}

/* arkfb_iplan_imageblit silently assumes that almost everything is 8-pixel aligned */
static void arkfb_iplan_imageblit(struct fb_info *info, const struct fb_image *image)
{
	u32 fg = expand_color(image->fg_color);
	u32 bg = expand_color(image->bg_color);
	const u8 *src1, *src;
	u8 __iomem *dst1;
	u32 __iomem *dst;
	u32 val;
	int x, y;

	src1 = image->data;
	dst1 = info->screen_base + (image->dy * info->fix.line_length)
		 + ((image->dx / 8) * 4);

	for (y = 0; y < image->height; y++) {
		src = src1;
		dst = (u32 __iomem *) dst1;
		for (x = 0; x < image->width; x += 8) {
			val = *(src++) * 0x01010101;
			val = (val & fg) | (~val & bg);
			fb_writel(val, dst++);
		}
		src1 += image->width / 8;
		dst1 += info->fix.line_length;
	}

}

/* arkfb_iplan_fillrect silently assumes that almost everything is 8-pixel aligned */
static void arkfb_iplan_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	u32 fg = expand_color(rect->color);
	u8 __iomem *dst1;
	u32 __iomem *dst;
	int x, y;

	dst1 = info->screen_base + (rect->dy * info->fix.line_length)
		 + ((rect->dx / 8) * 4);

	for (y = 0; y < rect->height; y++) {
		dst = (u32 __iomem *) dst1;
		for (x = 0; x < rect->width; x += 8) {
			fb_writel(fg, dst++);
		}
		dst1 += info->fix.line_length;
	}

}


/* image data is MSB-first, fb structure is high-nibble-in-low-byte-first */
static inline u32 expand_pixel(u32 c)
{
	return (((c &  1) << 24) | ((c &  2) << 27) | ((c &  4) << 14) | ((c &   8) << 17) |
		((c & 16) <<  4) | ((c & 32) <<  7) | ((c & 64) >>  6) | ((c & 128) >>  3)) * 0xF;
}

/* arkfb_cfb4_imageblit silently assumes that almost everything is 8-pixel aligned */
static void arkfb_cfb4_imageblit(struct fb_info *info, const struct fb_image *image)
{
	u32 fg = image->fg_color * 0x11111111;
	u32 bg = image->bg_color * 0x11111111;
	const u8 *src1, *src;
	u8 __iomem *dst1;
	u32 __iomem *dst;
	u32 val;
	int x, y;

	src1 = image->data;
	dst1 = info->screen_base + (image->dy * info->fix.line_length)
		 + ((image->dx / 8) * 4);

	for (y = 0; y < image->height; y++) {
		src = src1;
		dst = (u32 __iomem *) dst1;
		for (x = 0; x < image->width; x += 8) {
			val = expand_pixel(*(src++));
			val = (val & fg) | (~val & bg);
			fb_writel(val, dst++);
		}
		src1 += image->width / 8;
		dst1 += info->fix.line_length;
	}

}

static void arkfb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	if ((info->var.bits_per_pixel == 4) && (image->depth == 1)
	    && ((image->width % 8) == 0) && ((image->dx % 8) == 0)) {
		if (info->fix.type == FB_TYPE_INTERLEAVED_PLANES)
			arkfb_iplan_imageblit(info, image);
		else
			arkfb_cfb4_imageblit(info, image);
	} else
		cfb_imageblit(info, image);
}

static void arkfb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	if ((info->var.bits_per_pixel == 4)
	    && ((rect->width % 8) == 0) && ((rect->dx % 8) == 0)
	    && (info->fix.type == FB_TYPE_INTERLEAVED_PLANES))
		arkfb_iplan_fillrect(info, rect);
	 else
		cfb_fillrect(info, rect);
}


/* ------------------------------------------------------------------------- */


enum
{
	DAC_PSEUDO8_8,
	DAC_RGB1555_8,
	DAC_RGB0565_8,
	DAC_RGB0888_8,
	DAC_RGB8888_8,
	DAC_PSEUDO8_16,
	DAC_RGB1555_16,
	DAC_RGB0565_16,
	DAC_RGB0888_16,
	DAC_RGB8888_16,
	DAC_MAX
};

struct dac_ops {
	int (*dac_get_mode)(struct dac_info *info);
	int (*dac_set_mode)(struct dac_info *info, int mode);
	int (*dac_get_freq)(struct dac_info *info, int channel);
	int (*dac_set_freq)(struct dac_info *info, int channel, u32 freq);
	void (*dac_release)(struct dac_info *info);
};

typedef void (*dac_read_regs_t)(void *data, u8 *code, int count);
typedef void (*dac_write_regs_t)(void *data, u8 *code, int count);

struct dac_info
{
	struct dac_ops *dacops;
	dac_read_regs_t dac_read_regs;
	dac_write_regs_t dac_write_regs;
	void *data;
};

static inline void dac_read_regs(struct dac_info *info, u8 *code, int count)
{
	info->dac_read_regs(info->data, code, count);
}

static inline void dac_write_reg(struct dac_info *info, u8 reg, u8 val)
{
	u8 code[2] = {reg, val};
	info->dac_write_regs(info->data, code, 1);
}

static inline void dac_write_regs(struct dac_info *info, u8 *code, int count)
{
	info->dac_write_regs(info->data, code, count);
}

static inline int dac_set_mode(struct dac_info *info, int mode)
{
	return info->dacops->dac_set_mode(info, mode);
}

static inline int dac_set_freq(struct dac_info *info, int channel, u32 freq)
{
	return info->dacops->dac_set_freq(info, channel, freq);
}

static inline void dac_release(struct dac_info *info)
{
	info->dacops->dac_release(info);
}


/* ------------------------------------------------------------------------- */


/* ICS5342 DAC */

struct ics5342_info
{
	struct dac_info dac;
	u8 mode;
};

#define DAC_PAR(info) ((struct ics5342_info *) info)

/* LSB is set to distinguish unused slots */
static const u8 ics5342_mode_table[DAC_MAX] = {
	[DAC_PSEUDO8_8]  = 0x01, [DAC_RGB1555_8]  = 0x21, [DAC_RGB0565_8]  = 0x61,
	[DAC_RGB0888_8]  = 0x41, [DAC_PSEUDO8_16] = 0x11, [DAC_RGB1555_16] = 0x31,
	[DAC_RGB0565_16] = 0x51, [DAC_RGB0888_16] = 0x91, [DAC_RGB8888_16] = 0x71
};

static int ics5342_set_mode(struct dac_info *info, int mode)
{
	u8 code;

	if (mode >= DAC_MAX)
		return -EINVAL;

	code = ics5342_mode_table[mode];

	if (! code)
		return -EINVAL;

	dac_write_reg(info, 6, code & 0xF0);
	DAC_PAR(info)->mode = mode;

	return 0;
}

static const struct svga_pll ics5342_pll = {3, 129, 3, 33, 0, 3,
	60000, 250000, 14318};

/* pd4 - allow only posdivider 4 (r=2) */
static const struct svga_pll ics5342_pll_pd4 = {3, 129, 3, 33, 2, 2,
	60000, 335000, 14318};

/* 270 MHz should be upper bound for VCO clock according to specs,
   but that is too restrictive in pd4 case */

static int ics5342_set_freq(struct dac_info *info, int channel, u32 freq)
{
	u16 m, n, r;

	/* only postdivider 4 (r=2) is valid in mode DAC_PSEUDO8_16 */
	int rv = svga_compute_pll((DAC_PAR(info)->mode == DAC_PSEUDO8_16)
				  ? &ics5342_pll_pd4 : &ics5342_pll,
				  freq, &m, &n, &r, 0);

	if (rv < 0) {
		return -EINVAL;
	} else {
		u8 code[6] = {4, 3, 5, m-2, 5, (n-2) | (r << 5)};
		dac_write_regs(info, code, 3);
		return 0;
	}
}

static void ics5342_release(struct dac_info *info)
{
	ics5342_set_mode(info, DAC_PSEUDO8_8);
	kfree(info);
}

static struct dac_ops ics5342_ops = {
	.dac_set_mode	= ics5342_set_mode,
	.dac_set_freq	= ics5342_set_freq,
	.dac_release	= ics5342_release
};


static struct dac_info * ics5342_init(dac_read_regs_t drr, dac_write_regs_t dwr, void *data)
{
	struct dac_info *info = kzalloc(sizeof(struct ics5342_info), GFP_KERNEL);

	if (! info)
		return NULL;

	info->dacops = &ics5342_ops;
	info->dac_read_regs = drr;
	info->dac_write_regs = dwr;
	info->data = data;
	DAC_PAR(info)->mode = DAC_PSEUDO8_8; /* estimation */
	return info;
}


/* ------------------------------------------------------------------------- */


static unsigned short dac_regs[4] = {0x3c8, 0x3c9, 0x3c6, 0x3c7};

static void ark_dac_read_regs(void *data, u8 *code, int count)
{
	struct fb_info *info = data;
	struct arkfb_info *par;
	u8 regval;

	par = info->par;
	regval = vga_rseq(par->state.vgabase, 0x1C);
	while (count != 0)
	{
		vga_wseq(par->state.vgabase, 0x1C, regval | (code[0] & 4 ? 0x80 : 0));
		code[1] = vga_r(par->state.vgabase, dac_regs[code[0] & 3]);
		count--;
		code += 2;
	}

	vga_wseq(par->state.vgabase, 0x1C, regval);
}

static void ark_dac_write_regs(void *data, u8 *code, int count)
{
	struct fb_info *info = data;
	struct arkfb_info *par;
	u8 regval;

	par = info->par;
	regval = vga_rseq(par->state.vgabase, 0x1C);
	while (count != 0)
	{
		vga_wseq(par->state.vgabase, 0x1C, regval | (code[0] & 4 ? 0x80 : 0));
		vga_w(par->state.vgabase, dac_regs[code[0] & 3], code[1]);
		count--;
		code += 2;
	}

	vga_wseq(par->state.vgabase, 0x1C, regval);
}


static void ark_set_pixclock(struct fb_info *info, u32 pixclock)
{
	struct arkfb_info *par = info->par;
	u8 regval;

	int rv = dac_set_freq(par->dac, 0, 1000000000 / pixclock);
	if (rv < 0) {
		fb_err(info, "cannot set requested pixclock, keeping old value\n");
		return;
	}

	/* Set VGA misc register  */
	regval = vga_r(par->state.vgabase, VGA_MIS_R);
	vga_w(par->state.vgabase, VGA_MIS_W, regval | VGA_MIS_ENB_PLL_LOAD);
}


/* Open framebuffer */

static int arkfb_open(struct fb_info *info, int user)
{
	struct arkfb_info *par = info->par;

	mutex_lock(&(par->open_lock));
	if (par->ref_count == 0) {
		void __iomem *vgabase = par->state.vgabase;

		memset(&(par->state), 0, sizeof(struct vgastate));
		par->state.vgabase = vgabase;
		par->state.flags = VGA_SAVE_MODE | VGA_SAVE_FONTS | VGA_SAVE_CMAP;
		par->state.num_crtc = 0x60;
		par->state.num_seq = 0x30;
		save_vga(&(par->state));
	}

	par->ref_count++;
	mutex_unlock(&(par->open_lock));

	return 0;
}

/* Close framebuffer */

static int arkfb_release(struct fb_info *info, int user)
{
	struct arkfb_info *par = info->par;

	mutex_lock(&(par->open_lock));
	if (par->ref_count == 0) {
		mutex_unlock(&(par->open_lock));
		return -EINVAL;
	}

	if (par->ref_count == 1) {
		restore_vga(&(par->state));
		dac_set_mode(par->dac, DAC_PSEUDO8_8);
	}

	par->ref_count--;
	mutex_unlock(&(par->open_lock));

	return 0;
}

/* Validate passed in var */

static int arkfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	int rv, mem, step;

	if (!var->pixclock)
		return -EINVAL;

	/* Find appropriate format */
	rv = svga_match_format (arkfb_formats, var, NULL);
	if (rv < 0)
	{
		fb_err(info, "unsupported mode requested\n");
		return rv;
	}

	/* Do not allow to have real resoulution larger than virtual */
	if (var->xres > var->xres_virtual)
		var->xres_virtual = var->xres;

	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;

	/* Round up xres_virtual to have proper alignment of lines */
	step = arkfb_formats[rv].xresstep - 1;
	var->xres_virtual = (var->xres_virtual+step) & ~step;


	/* Check whether have enough memory */
	mem = ((var->bits_per_pixel * var->xres_virtual) >> 3) * var->yres_virtual;
	if (mem > info->screen_size)
	{
		fb_err(info, "not enough framebuffer memory (%d kB requested, %d kB available)\n",
		       mem >> 10, (unsigned int) (info->screen_size >> 10));
		return -EINVAL;
	}

	rv = svga_check_timings (&ark_timing_regs, var, info->node);
	if (rv < 0)
	{
		fb_err(info, "invalid timings requested\n");
		return rv;
	}

	/* Interlaced mode is broken */
	if (var->vmode & FB_VMODE_INTERLACED)
		return -EINVAL;

	return 0;
}

/* Set video mode from par */

static int arkfb_set_par(struct fb_info *info)
{
	struct arkfb_info *par = info->par;
	u32 value, mode, hmul, hdiv, offset_value, screen_size;
	u32 bpp = info->var.bits_per_pixel;
	u8 regval;

	if (bpp != 0) {
		info->fix.ypanstep = 1;
		info->fix.line_length = (info->var.xres_virtual * bpp) / 8;

		info->flags &= ~FBINFO_MISC_TILEBLITTING;
		info->tileops = NULL;

		/* in 4bpp supports 8p wide tiles only, any tiles otherwise */
		info->pixmap.blit_x = (bpp == 4) ? (1 << (8 - 1)) : (~(u32)0);
		info->pixmap.blit_y = ~(u32)0;

		offset_value = (info->var.xres_virtual * bpp) / 64;
		screen_size = info->var.yres_virtual * info->fix.line_length;
	} else {
		info->fix.ypanstep = 16;
		info->fix.line_length = 0;

		info->flags |= FBINFO_MISC_TILEBLITTING;
		info->tileops = &arkfb_tile_ops;

		/* supports 8x16 tiles only */
		info->pixmap.blit_x = 1 << (8 - 1);
		info->pixmap.blit_y = 1 << (16 - 1);

		offset_value = info->var.xres_virtual / 16;
		screen_size = (info->var.xres_virtual * info->var.yres_virtual) / 64;
	}

	info->var.xoffset = 0;
	info->var.yoffset = 0;
	info->var.activate = FB_ACTIVATE_NOW;

	/* Unlock registers */
	svga_wcrt_mask(par->state.vgabase, 0x11, 0x00, 0x80);

	/* Blank screen and turn off sync */
	svga_wseq_mask(par->state.vgabase, 0x01, 0x20, 0x20);
	svga_wcrt_mask(par->state.vgabase, 0x17, 0x00, 0x80);

	/* Set default values */
	svga_set_default_gfx_regs(par->state.vgabase);
	svga_set_default_atc_regs(par->state.vgabase);
	svga_set_default_seq_regs(par->state.vgabase);
	svga_set_default_crt_regs(par->state.vgabase);
	svga_wcrt_multi(par->state.vgabase, ark_line_compare_regs, 0xFFFFFFFF);
	svga_wcrt_multi(par->state.vgabase, ark_start_address_regs, 0);

	/* ARK specific initialization */
	svga_wseq_mask(par->state.vgabase, 0x10, 0x1F, 0x1F); /* enable linear framebuffer and full memory access */
	svga_wseq_mask(par->state.vgabase, 0x12, 0x03, 0x03); /* 4 MB linear framebuffer size */

	vga_wseq(par->state.vgabase, 0x13, info->fix.smem_start >> 16);
	vga_wseq(par->state.vgabase, 0x14, info->fix.smem_start >> 24);
	vga_wseq(par->state.vgabase, 0x15, 0);
	vga_wseq(par->state.vgabase, 0x16, 0);

	/* Set the FIFO threshold register */
	/* It is fascinating way to store 5-bit value in 8-bit register */
	regval = 0x10 | ((threshold & 0x0E) >> 1) | (threshold & 0x01) << 7 | (threshold & 0x10) << 1;
	vga_wseq(par->state.vgabase, 0x18, regval);

	/* Set the offset register */
	fb_dbg(info, "offset register       : %d\n", offset_value);
	svga_wcrt_multi(par->state.vgabase, ark_offset_regs, offset_value);

	/* fix for hi-res textmode */
	svga_wcrt_mask(par->state.vgabase, 0x40, 0x08, 0x08);

	if (info->var.vmode & FB_VMODE_DOUBLE)
		svga_wcrt_mask(par->state.vgabase, 0x09, 0x80, 0x80);
	else
		svga_wcrt_mask(par->state.vgabase, 0x09, 0x00, 0x80);

	if (info->var.vmode & FB_VMODE_INTERLACED)
		svga_wcrt_mask(par->state.vgabase, 0x44, 0x04, 0x04);
	else
		svga_wcrt_mask(par->state.vgabase, 0x44, 0x00, 0x04);

	hmul = 1;
	hdiv = 1;
	mode = svga_match_format(arkfb_formats, &(info->var), &(info->fix));

	/* Set mode-specific register values */
	switch (mode) {
	case 0:
		fb_dbg(info, "text mode\n");
		svga_set_textmode_vga_regs(par->state.vgabase);

		vga_wseq(par->state.vgabase, 0x11, 0x10); /* basic VGA mode */
		svga_wcrt_mask(par->state.vgabase, 0x46, 0x00, 0x04); /* 8bit pixel path */
		dac_set_mode(par->dac, DAC_PSEUDO8_8);

		break;
	case 1:
		fb_dbg(info, "4 bit pseudocolor\n");
		vga_wgfx(par->state.vgabase, VGA_GFX_MODE, 0x40);

		vga_wseq(par->state.vgabase, 0x11, 0x10); /* basic VGA mode */
		svga_wcrt_mask(par->state.vgabase, 0x46, 0x00, 0x04); /* 8bit pixel path */
		dac_set_mode(par->dac, DAC_PSEUDO8_8);
		break;
	case 2:
		fb_dbg(info, "4 bit pseudocolor, planar\n");

		vga_wseq(par->state.vgabase, 0x11, 0x10); /* basic VGA mode */
		svga_wcrt_mask(par->state.vgabase, 0x46, 0x00, 0x04); /* 8bit pixel path */
		dac_set_mode(par->dac, DAC_PSEUDO8_8);
		break;
	case 3:
		fb_dbg(info, "8 bit pseudocolor\n");

		vga_wseq(par->state.vgabase, 0x11, 0x16); /* 8bpp accel mode */

		if (info->var.pixclock > 20000) {
			fb_dbg(info, "not using multiplex\n");
			svga_wcrt_mask(par->state.vgabase, 0x46, 0x00, 0x04); /* 8bit pixel path */
			dac_set_mode(par->dac, DAC_PSEUDO8_8);
		} else {
			fb_dbg(info, "using multiplex\n");
			svga_wcrt_mask(par->state.vgabase, 0x46, 0x04, 0x04); /* 16bit pixel path */
			dac_set_mode(par->dac, DAC_PSEUDO8_16);
			hdiv = 2;
		}
		break;
	case 4:
		fb_dbg(info, "5/5/5 truecolor\n");

		vga_wseq(par->state.vgabase, 0x11, 0x1A); /* 16bpp accel mode */
		svga_wcrt_mask(par->state.vgabase, 0x46, 0x04, 0x04); /* 16bit pixel path */
		dac_set_mode(par->dac, DAC_RGB1555_16);
		break;
	case 5:
		fb_dbg(info, "5/6/5 truecolor\n");

		vga_wseq(par->state.vgabase, 0x11, 0x1A); /* 16bpp accel mode */
		svga_wcrt_mask(par->state.vgabase, 0x46, 0x04, 0x04); /* 16bit pixel path */
		dac_set_mode(par->dac, DAC_RGB0565_16);
		break;
	case 6:
		fb_dbg(info, "8/8/8 truecolor\n");

		vga_wseq(par->state.vgabase, 0x11, 0x16); /* 8bpp accel mode ??? */
		svga_wcrt_mask(par->state.vgabase, 0x46, 0x04, 0x04); /* 16bit pixel path */
		dac_set_mode(par->dac, DAC_RGB0888_16);
		hmul = 3;
		hdiv = 2;
		break;
	case 7:
		fb_dbg(info, "8/8/8/8 truecolor\n");

		vga_wseq(par->state.vgabase, 0x11, 0x1E); /* 32bpp accel mode */
		svga_wcrt_mask(par->state.vgabase, 0x46, 0x04, 0x04); /* 16bit pixel path */
		dac_set_mode(par->dac, DAC_RGB8888_16);
		hmul = 2;
		break;
	default:
		fb_err(info, "unsupported mode - bug\n");
		return -EINVAL;
	}

	value = (hdiv * info->var.pixclock) / hmul;
	if (!value) {
		fb_dbg(info, "invalid pixclock\n");
		value = 1;
	}
	ark_set_pixclock(info, value);
	svga_set_timings(par->state.vgabase, &ark_timing_regs, &(info->var), hmul, hdiv,
			 (info->var.vmode & FB_VMODE_DOUBLE)     ? 2 : 1,
			 (info->var.vmode & FB_VMODE_INTERLACED) ? 2 : 1,
			  hmul, info->node);

	/* Set interlaced mode start/end register */
	value = info->var.xres + info->var.left_margin + info->var.right_margin + info->var.hsync_len;
	value = ((value * hmul / hdiv) / 8) - 5;
	vga_wcrt(par->state.vgabase, 0x42, (value + 1) / 2);

	if (screen_size > info->screen_size)
		screen_size = info->screen_size;
	memset_io(info->screen_base, 0x00, screen_size);
	/* Device and screen back on */
	svga_wcrt_mask(par->state.vgabase, 0x17, 0x80, 0x80);
	svga_wseq_mask(par->state.vgabase, 0x01, 0x00, 0x20);

	return 0;
}

/* Set a colour register */

static int arkfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
				u_int transp, struct fb_info *fb)
{
	switch (fb->var.bits_per_pixel) {
	case 0:
	case 4:
		if (regno >= 16)
			return -EINVAL;

		if ((fb->var.bits_per_pixel == 4) &&
		    (fb->var.nonstd == 0)) {
			outb(0xF0, VGA_PEL_MSK);
			outb(regno*16, VGA_PEL_IW);
		} else {
			outb(0x0F, VGA_PEL_MSK);
			outb(regno, VGA_PEL_IW);
		}
		outb(red >> 10, VGA_PEL_D);
		outb(green >> 10, VGA_PEL_D);
		outb(blue >> 10, VGA_PEL_D);
		break;
	case 8:
		if (regno >= 256)
			return -EINVAL;

		outb(0xFF, VGA_PEL_MSK);
		outb(regno, VGA_PEL_IW);
		outb(red >> 10, VGA_PEL_D);
		outb(green >> 10, VGA_PEL_D);
		outb(blue >> 10, VGA_PEL_D);
		break;
	case 16:
		if (regno >= 16)
			return 0;

		if (fb->var.green.length == 5)
			((u32*)fb->pseudo_palette)[regno] = ((red & 0xF800) >> 1) |
				((green & 0xF800) >> 6) | ((blue & 0xF800) >> 11);
		else if (fb->var.green.length == 6)
			((u32*)fb->pseudo_palette)[regno] = (red & 0xF800) |
				((green & 0xFC00) >> 5) | ((blue & 0xF800) >> 11);
		else
			return -EINVAL;
		break;
	case 24:
	case 32:
		if (regno >= 16)
			return 0;

		((u32*)fb->pseudo_palette)[regno] = ((red & 0xFF00) << 8) |
			(green & 0xFF00) | ((blue & 0xFF00) >> 8);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* Set the display blanking state */

static int arkfb_blank(int blank_mode, struct fb_info *info)
{
	struct arkfb_info *par = info->par;

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		fb_dbg(info, "unblank\n");
		svga_wseq_mask(par->state.vgabase, 0x01, 0x00, 0x20);
		svga_wcrt_mask(par->state.vgabase, 0x17, 0x80, 0x80);
		break;
	case FB_BLANK_NORMAL:
		fb_dbg(info, "blank\n");
		svga_wseq_mask(par->state.vgabase, 0x01, 0x20, 0x20);
		svga_wcrt_mask(par->state.vgabase, 0x17, 0x80, 0x80);
		break;
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_VSYNC_SUSPEND:
		fb_dbg(info, "sync down\n");
		svga_wseq_mask(par->state.vgabase, 0x01, 0x20, 0x20);
		svga_wcrt_mask(par->state.vgabase, 0x17, 0x00, 0x80);
		break;
	}
	return 0;
}


/* Pan the display */

static int arkfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct arkfb_info *par = info->par;
	unsigned int offset;

	/* Calculate the offset */
	if (info->var.bits_per_pixel == 0) {
		offset = (var->yoffset / 16) * (info->var.xres_virtual / 2)
		       + (var->xoffset / 2);
		offset = offset >> 2;
	} else {
		offset = (var->yoffset * info->fix.line_length) +
			 (var->xoffset * info->var.bits_per_pixel / 8);
		offset = offset >> ((info->var.bits_per_pixel == 4) ? 2 : 3);
	}

	/* Set the offset */
	svga_wcrt_multi(par->state.vgabase, ark_start_address_regs, offset);

	return 0;
}


/* ------------------------------------------------------------------------- */


/* Frame buffer operations */

static const struct fb_ops arkfb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= arkfb_open,
	.fb_release	= arkfb_release,
	__FB_DEFAULT_IOMEM_OPS_RDWR,
	.fb_check_var	= arkfb_check_var,
	.fb_set_par	= arkfb_set_par,
	.fb_setcolreg	= arkfb_setcolreg,
	.fb_blank	= arkfb_blank,
	.fb_pan_display	= arkfb_pan_display,
	.fb_fillrect	= arkfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= arkfb_imageblit,
	__FB_DEFAULT_IOMEM_OPS_MMAP,
	.fb_get_caps    = svga_get_caps,
};


/* ------------------------------------------------------------------------- */


/* PCI probe */
static int ark_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct pci_bus_region bus_reg;
	struct resource vga_res;
	struct fb_info *info;
	struct arkfb_info *par;
	int rc;
	u8 regval;

	rc = aperture_remove_conflicting_pci_devices(dev, "arkfb");
	if (rc < 0)
		return rc;

	/* Ignore secondary VGA device because there is no VGA arbitration */
	if (! svga_primary_device(dev)) {
		dev_info(&(dev->dev), "ignoring secondary device\n");
		return -ENODEV;
	}

	/* Allocate and fill driver data structure */
	info = framebuffer_alloc(sizeof(struct arkfb_info), &(dev->dev));
	if (!info)
		return -ENOMEM;

	par = info->par;
	mutex_init(&par->open_lock);

	info->flags = FBINFO_PARTIAL_PAN_OK | FBINFO_HWACCEL_YPAN;
	info->fbops = &arkfb_ops;

	/* Prepare PCI device */
	rc = pci_enable_device(dev);
	if (rc < 0) {
		dev_err(info->device, "cannot enable PCI device\n");
		goto err_enable_device;
	}

	rc = pci_request_regions(dev, "arkfb");
	if (rc < 0) {
		dev_err(info->device, "cannot reserve framebuffer region\n");
		goto err_request_regions;
	}

	par->dac = ics5342_init(ark_dac_read_regs, ark_dac_write_regs, info);
	if (! par->dac) {
		rc = -ENOMEM;
		dev_err(info->device, "RAMDAC initialization failed\n");
		goto err_dac;
	}

	info->fix.smem_start = pci_resource_start(dev, 0);
	info->fix.smem_len = pci_resource_len(dev, 0);

	/* Map physical IO memory address into kernel space */
	info->screen_base = pci_iomap_wc(dev, 0, 0);
	if (! info->screen_base) {
		rc = -ENOMEM;
		dev_err(info->device, "iomap for framebuffer failed\n");
		goto err_iomap;
	}

	bus_reg.start = 0;
	bus_reg.end = 64 * 1024;

	vga_res.flags = IORESOURCE_IO;

	pcibios_bus_to_resource(dev->bus, &vga_res, &bus_reg);

	par->state.vgabase = (void __iomem *) (unsigned long) vga_res.start;

	/* FIXME get memsize */
	regval = vga_rseq(par->state.vgabase, 0x10);
	info->screen_size = (1 << (regval >> 6)) << 20;
	info->fix.smem_len = info->screen_size;

	strcpy(info->fix.id, "ARK 2000PV");
	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	info->fix.ypanstep = 0;
	info->fix.accel = FB_ACCEL_NONE;
	info->pseudo_palette = (void*) (par->pseudo_palette);

	/* Prepare startup mode */
	rc = fb_find_mode(&(info->var), info, mode_option, NULL, 0, NULL, 8);
	if (! ((rc == 1) || (rc == 2))) {
		rc = -EINVAL;
		dev_err(info->device, "mode %s not found\n", mode_option);
		goto err_find_mode;
	}

	rc = fb_alloc_cmap(&info->cmap, 256, 0);
	if (rc < 0) {
		dev_err(info->device, "cannot allocate colormap\n");
		goto err_alloc_cmap;
	}

	rc = register_framebuffer(info);
	if (rc < 0) {
		dev_err(info->device, "cannot register framebuffer\n");
		goto err_reg_fb;
	}

	fb_info(info, "%s on %s, %d MB RAM\n",
		info->fix.id, pci_name(dev), info->fix.smem_len >> 20);

	/* Record a reference to the driver data */
	pci_set_drvdata(dev, info);
	par->wc_cookie = arch_phys_wc_add(info->fix.smem_start,
					  info->fix.smem_len);
	return 0;

	/* Error handling */
err_reg_fb:
	fb_dealloc_cmap(&info->cmap);
err_alloc_cmap:
err_find_mode:
	pci_iounmap(dev, info->screen_base);
err_iomap:
	dac_release(par->dac);
err_dac:
	pci_release_regions(dev);
err_request_regions:
/*	pci_disable_device(dev); */
err_enable_device:
	framebuffer_release(info);
	return rc;
}

/* PCI remove */

static void ark_pci_remove(struct pci_dev *dev)
{
	struct fb_info *info = pci_get_drvdata(dev);

	if (info) {
		struct arkfb_info *par = info->par;
		arch_phys_wc_del(par->wc_cookie);
		dac_release(par->dac);
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);

		pci_iounmap(dev, info->screen_base);
		pci_release_regions(dev);
/*		pci_disable_device(dev); */

		framebuffer_release(info);
	}
}


/* PCI suspend */

static int __maybe_unused ark_pci_suspend(struct device *dev)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct arkfb_info *par = info->par;

	dev_info(info->device, "suspend\n");

	console_lock();
	mutex_lock(&(par->open_lock));

	if (par->ref_count == 0) {
		mutex_unlock(&(par->open_lock));
		console_unlock();
		return 0;
	}

	fb_set_suspend(info, 1);

	mutex_unlock(&(par->open_lock));
	console_unlock();

	return 0;
}


/* PCI resume */

static int __maybe_unused ark_pci_resume(struct device *dev)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct arkfb_info *par = info->par;

	dev_info(info->device, "resume\n");

	console_lock();
	mutex_lock(&(par->open_lock));

	if (par->ref_count == 0)
		goto fail;

	arkfb_set_par(info);
	fb_set_suspend(info, 0);

fail:
	mutex_unlock(&(par->open_lock));
	console_unlock();
	return 0;
}

static const struct dev_pm_ops ark_pci_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.suspend	= ark_pci_suspend,
	.resume		= ark_pci_resume,
	.freeze		= NULL,
	.thaw		= ark_pci_resume,
	.poweroff	= ark_pci_suspend,
	.restore	= ark_pci_resume,
#endif
};

/* List of boards that we are trying to support */

static const struct pci_device_id ark_devices[] = {
	{PCI_DEVICE(0xEDD8, 0xA099)},
	{0, 0, 0, 0, 0, 0, 0}
};


MODULE_DEVICE_TABLE(pci, ark_devices);

static struct pci_driver arkfb_pci_driver = {
	.name		= "arkfb",
	.id_table	= ark_devices,
	.probe		= ark_pci_probe,
	.remove		= ark_pci_remove,
	.driver.pm	= &ark_pci_pm_ops,
};

/* Cleanup */

static void __exit arkfb_cleanup(void)
{
	pr_debug("arkfb: cleaning up\n");
	pci_unregister_driver(&arkfb_pci_driver);
}

/* Driver Initialisation */

static int __init arkfb_init(void)
{

#ifndef MODULE
	char *option = NULL;
#endif

	if (fb_modesetting_disabled("arkfb"))
		return -ENODEV;

#ifndef MODULE
	if (fb_get_options("arkfb", &option))
		return -ENODEV;

	if (option && *option)
		mode_option = option;
#endif

	pr_debug("arkfb: initializing\n");
	return pci_register_driver(&arkfb_pci_driver);
}

module_init(arkfb_init);
module_exit(arkfb_cleanup);
