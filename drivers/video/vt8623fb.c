/*
 * linux/drivers/video/vt8623fb.c - fbdev driver for
 * integrated graphic core in VIA VT8623 [CLE266] chipset
 *
 * Copyright (c) 2006-2007 Ondrej Zajicek <santiago@crfreenet.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Code is based on s3fb, some parts are from David Boucher's viafb
 * (http://davesdomain.org.uk/viafb/)
 */

#include <linux/version.h>
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
#include <linux/console.h> /* Why should fb driver call console functions? because acquire_console_sem() */
#include <video/vga.h>

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

struct vt8623fb_info {
	char __iomem *mmio_base;
	int mtrr_reg;
	struct vgastate state;
	struct mutex open_lock;
	unsigned int ref_count;
	u32 pseudo_palette[16];
};



/* ------------------------------------------------------------------------- */

static const struct svga_fb_format vt8623fb_formats[] = {
	{ 0,  {0, 6, 0},  {0, 6, 0},  {0, 6, 0}, {0, 0, 0}, 0,
		FB_TYPE_TEXT, FB_AUX_TEXT_SVGA_STEP8,	FB_VISUAL_PSEUDOCOLOR, 16, 16},
	{ 4,  {0, 6, 0},  {0, 6, 0},  {0, 6, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_PSEUDOCOLOR, 16, 16},
	{ 4,  {0, 6, 0},  {0, 6, 0},  {0, 6, 0}, {0, 0, 0}, 1,
		FB_TYPE_INTERLEAVED_PLANES, 1,		FB_VISUAL_PSEUDOCOLOR, 16, 16},
	{ 8,  {0, 6, 0},  {0, 6, 0},  {0, 6, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_PSEUDOCOLOR, 8, 8},
/*	{16,  {10, 5, 0}, {5, 5, 0},  {0, 5, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_TRUECOLOR, 4, 4},	*/
	{16,  {11, 5, 0}, {5, 6, 0},  {0, 5, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_TRUECOLOR, 4, 4},
	{32,  {16, 8, 0}, {8, 8, 0},  {0, 8, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_TRUECOLOR, 2, 2},
	SVGA_FORMAT_END
};

static const struct svga_pll vt8623_pll = {2, 127, 2, 7, 0, 3,
	60000, 300000, 14318};

/* CRT timing register sets */

static struct vga_regset vt8623_h_total_regs[]       = {{0x00, 0, 7}, {0x36, 3, 3}, VGA_REGSET_END};
static struct vga_regset vt8623_h_display_regs[]     = {{0x01, 0, 7}, VGA_REGSET_END};
static struct vga_regset vt8623_h_blank_start_regs[] = {{0x02, 0, 7}, VGA_REGSET_END};
static struct vga_regset vt8623_h_blank_end_regs[]   = {{0x03, 0, 4}, {0x05, 7, 7}, {0x33, 5, 5}, VGA_REGSET_END};
static struct vga_regset vt8623_h_sync_start_regs[]  = {{0x04, 0, 7}, {0x33, 4, 4}, VGA_REGSET_END};
static struct vga_regset vt8623_h_sync_end_regs[]    = {{0x05, 0, 4}, VGA_REGSET_END};

static struct vga_regset vt8623_v_total_regs[]       = {{0x06, 0, 7}, {0x07, 0, 0}, {0x07, 5, 5}, {0x35, 0, 0}, VGA_REGSET_END};
static struct vga_regset vt8623_v_display_regs[]     = {{0x12, 0, 7}, {0x07, 1, 1}, {0x07, 6, 6}, {0x35, 2, 2}, VGA_REGSET_END};
static struct vga_regset vt8623_v_blank_start_regs[] = {{0x15, 0, 7}, {0x07, 3, 3}, {0x09, 5, 5}, {0x35, 3, 3}, VGA_REGSET_END};
static struct vga_regset vt8623_v_blank_end_regs[]   = {{0x16, 0, 7}, VGA_REGSET_END};
static struct vga_regset vt8623_v_sync_start_regs[]  = {{0x10, 0, 7}, {0x07, 2, 2}, {0x07, 7, 7}, {0x35, 1, 1}, VGA_REGSET_END};
static struct vga_regset vt8623_v_sync_end_regs[]    = {{0x11, 0, 3}, VGA_REGSET_END};

static struct vga_regset vt8623_offset_regs[]        = {{0x13, 0, 7}, {0x35, 5, 7}, VGA_REGSET_END};
static struct vga_regset vt8623_line_compare_regs[]  = {{0x18, 0, 7}, {0x07, 4, 4}, {0x09, 6, 6}, {0x33, 0, 2}, {0x35, 4, 4}, VGA_REGSET_END};
static struct vga_regset vt8623_fetch_count_regs[]   = {{0x1C, 0, 7}, {0x1D, 0, 1}, VGA_REGSET_END};
static struct vga_regset vt8623_start_address_regs[] = {{0x0d, 0, 7}, {0x0c, 0, 7}, {0x34, 0, 7}, {0x48, 0, 1}, VGA_REGSET_END};

static struct svga_timing_regs vt8623_timing_regs     = {
	vt8623_h_total_regs, vt8623_h_display_regs, vt8623_h_blank_start_regs,
	vt8623_h_blank_end_regs, vt8623_h_sync_start_regs, vt8623_h_sync_end_regs,
	vt8623_v_total_regs, vt8623_v_display_regs, vt8623_v_blank_start_regs,
	vt8623_v_blank_end_regs, vt8623_v_sync_start_regs, vt8623_v_sync_end_regs,
};


/* ------------------------------------------------------------------------- */


/* Module parameters */

static char *mode_option = "640x480-8@60";

#ifdef CONFIG_MTRR
static int mtrr = 1;
#endif

MODULE_AUTHOR("(c) 2006 Ondrej Zajicek <santiago@crfreenet.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("fbdev driver for integrated graphics core in VIA VT8623 [CLE266]");

module_param(mode_option, charp, 0644);
MODULE_PARM_DESC(mode_option, "Default video mode ('640x480-8@60', etc)");
module_param_named(mode, mode_option, charp, 0);
MODULE_PARM_DESC(mode, "Default video mode e.g. '648x480-8@60' (deprecated)");

#ifdef CONFIG_MTRR
module_param(mtrr, int, 0444);
MODULE_PARM_DESC(mtrr, "Enable write-combining with MTRR (1=enable, 0=disable, default=1)");
#endif


/* ------------------------------------------------------------------------- */


static struct fb_tile_ops vt8623fb_tile_ops = {
	.fb_settile	= svga_settile,
	.fb_tilecopy	= svga_tilecopy,
	.fb_tilefill    = svga_tilefill,
	.fb_tileblit    = svga_tileblit,
	.fb_tilecursor  = svga_tilecursor,
	.fb_get_tilemax = svga_get_tilemax,
};


/* ------------------------------------------------------------------------- */


/* image data is MSB-first, fb structure is MSB-first too */
static inline u32 expand_color(u32 c)
{
	return ((c & 1) | ((c & 2) << 7) | ((c & 4) << 14) | ((c & 8) << 21)) * 0xFF;
}

/* vt8623fb_iplan_imageblit silently assumes that almost everything is 8-pixel aligned */
static void vt8623fb_iplan_imageblit(struct fb_info *info, const struct fb_image *image)
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

/* vt8623fb_iplan_fillrect silently assumes that almost everything is 8-pixel aligned */
static void vt8623fb_iplan_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
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

/* vt8623fb_cfb4_imageblit silently assumes that almost everything is 8-pixel aligned */
static void vt8623fb_cfb4_imageblit(struct fb_info *info, const struct fb_image *image)
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

static void vt8623fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	if ((info->var.bits_per_pixel == 4) && (image->depth == 1)
	    && ((image->width % 8) == 0) && ((image->dx % 8) == 0)) {
		if (info->fix.type == FB_TYPE_INTERLEAVED_PLANES)
			vt8623fb_iplan_imageblit(info, image);
		else
			vt8623fb_cfb4_imageblit(info, image);
	} else
		cfb_imageblit(info, image);
}

static void vt8623fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	if ((info->var.bits_per_pixel == 4)
	    && ((rect->width % 8) == 0) && ((rect->dx % 8) == 0)
	    && (info->fix.type == FB_TYPE_INTERLEAVED_PLANES))
		vt8623fb_iplan_fillrect(info, rect);
	 else
		cfb_fillrect(info, rect);
}


/* ------------------------------------------------------------------------- */


static void vt8623_set_pixclock(struct fb_info *info, u32 pixclock)
{
	u16 m, n, r;
	u8 regval;
	int rv;

	rv = svga_compute_pll(&vt8623_pll, 1000000000 / pixclock, &m, &n, &r, info->node);
	if (rv < 0) {
		printk(KERN_ERR "fb%d: cannot set requested pixclock, keeping old value\n", info->node);
		return;
	}

	/* Set VGA misc register  */
	regval = vga_r(NULL, VGA_MIS_R);
	vga_w(NULL, VGA_MIS_W, regval | VGA_MIS_ENB_PLL_LOAD);

	/* Set clock registers */
	vga_wseq(NULL, 0x46, (n  | (r << 6)));
	vga_wseq(NULL, 0x47, m);

	udelay(1000);

	/* PLL reset */
	svga_wseq_mask(0x40, 0x02, 0x02);
	svga_wseq_mask(0x40, 0x00, 0x02);
}


static int vt8623fb_open(struct fb_info *info, int user)
{
	struct vt8623fb_info *par = info->par;

	mutex_lock(&(par->open_lock));
	if (par->ref_count == 0) {
		memset(&(par->state), 0, sizeof(struct vgastate));
		par->state.flags = VGA_SAVE_MODE | VGA_SAVE_FONTS | VGA_SAVE_CMAP;
		par->state.num_crtc = 0xA2;
		par->state.num_seq = 0x50;
		save_vga(&(par->state));
	}

	par->ref_count++;
	mutex_unlock(&(par->open_lock));

	return 0;
}

static int vt8623fb_release(struct fb_info *info, int user)
{
	struct vt8623fb_info *par = info->par;

	mutex_lock(&(par->open_lock));
	if (par->ref_count == 0) {
		mutex_unlock(&(par->open_lock));
		return -EINVAL;
	}

	if (par->ref_count == 1)
		restore_vga(&(par->state));

	par->ref_count--;
	mutex_unlock(&(par->open_lock));

	return 0;
}

static int vt8623fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	int rv, mem, step;

	/* Find appropriate format */
	rv = svga_match_format (vt8623fb_formats, var, NULL);
	if (rv < 0)
	{
		printk(KERN_ERR "fb%d: unsupported mode requested\n", info->node);
		return rv;
	}

	/* Do not allow to have real resoulution larger than virtual */
	if (var->xres > var->xres_virtual)
		var->xres_virtual = var->xres;

	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;

	/* Round up xres_virtual to have proper alignment of lines */
	step = vt8623fb_formats[rv].xresstep - 1;
	var->xres_virtual = (var->xres_virtual+step) & ~step;

	/* Check whether have enough memory */
	mem = ((var->bits_per_pixel * var->xres_virtual) >> 3) * var->yres_virtual;
	if (mem > info->screen_size)
	{
		printk(KERN_ERR "fb%d: not enough framebuffer memory (%d kB requested , %d kB available)\n", info->node, mem >> 10, (unsigned int) (info->screen_size >> 10));
		return -EINVAL;
	}

	/* Text mode is limited to 256 kB of memory */
	if ((var->bits_per_pixel == 0) && (mem > (256*1024)))
	{
		printk(KERN_ERR "fb%d: text framebuffer size too large (%d kB requested, 256 kB possible)\n", info->node, mem >> 10);
		return -EINVAL;
	}

	rv = svga_check_timings (&vt8623_timing_regs, var, info->node);
	if (rv < 0)
	{
		printk(KERN_ERR "fb%d: invalid timings requested\n", info->node);
		return rv;
	}

	/* Interlaced mode not supported */
	if (var->vmode & FB_VMODE_INTERLACED)
		return -EINVAL;

	return 0;
}


static int vt8623fb_set_par(struct fb_info *info)
{
	u32 mode, offset_value, fetch_value, screen_size;
	u32 bpp = info->var.bits_per_pixel;

	if (bpp != 0) {
		info->fix.ypanstep = 1;
		info->fix.line_length = (info->var.xres_virtual * bpp) / 8;

		info->flags &= ~FBINFO_MISC_TILEBLITTING;
		info->tileops = NULL;

		/* in 4bpp supports 8p wide tiles only, any tiles otherwise */
		info->pixmap.blit_x = (bpp == 4) ? (1 << (8 - 1)) : (~(u32)0);
		info->pixmap.blit_y = ~(u32)0;

		offset_value = (info->var.xres_virtual * bpp) / 64;
		fetch_value  = ((info->var.xres * bpp) / 128) + 4;

		if (bpp == 4)
			fetch_value  = (info->var.xres / 8) + 8; /* + 0 is OK */

		screen_size  = info->var.yres_virtual * info->fix.line_length;
	} else {
		info->fix.ypanstep = 16;
		info->fix.line_length = 0;

		info->flags |= FBINFO_MISC_TILEBLITTING;
		info->tileops = &vt8623fb_tile_ops;

		/* supports 8x16 tiles only */
		info->pixmap.blit_x = 1 << (8 - 1);
		info->pixmap.blit_y = 1 << (16 - 1);

		offset_value = info->var.xres_virtual / 16;
		fetch_value  = (info->var.xres / 8) + 8;
		screen_size  = (info->var.xres_virtual * info->var.yres_virtual) / 64;
	}

	info->var.xoffset = 0;
	info->var.yoffset = 0;
	info->var.activate = FB_ACTIVATE_NOW;

	/* Unlock registers */
	svga_wseq_mask(0x10, 0x01, 0x01);
	svga_wcrt_mask(0x11, 0x00, 0x80);
	svga_wcrt_mask(0x47, 0x00, 0x01);

	/* Device, screen and sync off */
	svga_wseq_mask(0x01, 0x20, 0x20);
	svga_wcrt_mask(0x36, 0x30, 0x30);
	svga_wcrt_mask(0x17, 0x00, 0x80);

	/* Set default values */
	svga_set_default_gfx_regs();
	svga_set_default_atc_regs();
	svga_set_default_seq_regs();
	svga_set_default_crt_regs();
	svga_wcrt_multi(vt8623_line_compare_regs, 0xFFFFFFFF);
	svga_wcrt_multi(vt8623_start_address_regs, 0);

	svga_wcrt_multi(vt8623_offset_regs, offset_value);
	svga_wseq_multi(vt8623_fetch_count_regs, fetch_value);

	/* Clear H/V Skew */
	svga_wcrt_mask(0x03, 0x00, 0x60);
	svga_wcrt_mask(0x05, 0x00, 0x60);

	if (info->var.vmode & FB_VMODE_DOUBLE)
		svga_wcrt_mask(0x09, 0x80, 0x80);
	else
		svga_wcrt_mask(0x09, 0x00, 0x80);

	svga_wseq_mask(0x1E, 0xF0, 0xF0); // DI/DVP bus
	svga_wseq_mask(0x2A, 0x0F, 0x0F); // DI/DVP bus
	svga_wseq_mask(0x16, 0x08, 0xBF); // FIFO read treshold
	vga_wseq(NULL, 0x17, 0x1F);       // FIFO depth
	vga_wseq(NULL, 0x18, 0x4E);
	svga_wseq_mask(0x1A, 0x08, 0x08); // enable MMIO ?

	vga_wcrt(NULL, 0x32, 0x00);
	vga_wcrt(NULL, 0x34, 0x00);
	vga_wcrt(NULL, 0x6A, 0x80);
	vga_wcrt(NULL, 0x6A, 0xC0);

	vga_wgfx(NULL, 0x20, 0x00);
	vga_wgfx(NULL, 0x21, 0x00);
	vga_wgfx(NULL, 0x22, 0x00);

	/* Set SR15 according to number of bits per pixel */
	mode = svga_match_format(vt8623fb_formats, &(info->var), &(info->fix));
	switch (mode) {
	case 0:
		pr_debug("fb%d: text mode\n", info->node);
		svga_set_textmode_vga_regs();
		svga_wseq_mask(0x15, 0x00, 0xFE);
		svga_wcrt_mask(0x11, 0x60, 0x70);
		break;
	case 1:
		pr_debug("fb%d: 4 bit pseudocolor\n", info->node);
		vga_wgfx(NULL, VGA_GFX_MODE, 0x40);
		svga_wseq_mask(0x15, 0x20, 0xFE);
		svga_wcrt_mask(0x11, 0x00, 0x70);
		break;
	case 2:
		pr_debug("fb%d: 4 bit pseudocolor, planar\n", info->node);
		svga_wseq_mask(0x15, 0x00, 0xFE);
		svga_wcrt_mask(0x11, 0x00, 0x70);
		break;
	case 3:
		pr_debug("fb%d: 8 bit pseudocolor\n", info->node);
		svga_wseq_mask(0x15, 0x22, 0xFE);
		break;
	case 4:
		pr_debug("fb%d: 5/6/5 truecolor\n", info->node);
		svga_wseq_mask(0x15, 0xB6, 0xFE);
		break;
	case 5:
		pr_debug("fb%d: 8/8/8 truecolor\n", info->node);
		svga_wseq_mask(0x15, 0xAE, 0xFE);
		break;
	default:
		printk(KERN_ERR "vt8623fb: unsupported mode - bug\n");
		return (-EINVAL);
	}

	vt8623_set_pixclock(info, info->var.pixclock);
	svga_set_timings(&vt8623_timing_regs, &(info->var), 1, 1,
			 (info->var.vmode & FB_VMODE_DOUBLE) ? 2 : 1, 1,
			 1, info->node);

	memset_io(info->screen_base, 0x00, screen_size);

	/* Device and screen back on */
	svga_wcrt_mask(0x17, 0x80, 0x80);
	svga_wcrt_mask(0x36, 0x00, 0x30);
	svga_wseq_mask(0x01, 0x00, 0x20);

	return 0;
}


static int vt8623fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
				u_int transp, struct fb_info *fb)
{
	switch (fb->var.bits_per_pixel) {
	case 0:
	case 4:
		if (regno >= 16)
			return -EINVAL;

		outb(0x0F, VGA_PEL_MSK);
		outb(regno, VGA_PEL_IW);
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

		/* ((transp & 0xFF00) << 16) */
		((u32*)fb->pseudo_palette)[regno] = ((red & 0xFF00) << 8) |
			(green & 0xFF00) | ((blue & 0xFF00) >> 8);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}


static int vt8623fb_blank(int blank_mode, struct fb_info *info)
{
	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		pr_debug("fb%d: unblank\n", info->node);
		svga_wcrt_mask(0x36, 0x00, 0x30);
		svga_wseq_mask(0x01, 0x00, 0x20);
		break;
	case FB_BLANK_NORMAL:
		pr_debug("fb%d: blank\n", info->node);
		svga_wcrt_mask(0x36, 0x00, 0x30);
		svga_wseq_mask(0x01, 0x20, 0x20);
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		pr_debug("fb%d: DPMS standby (hsync off)\n", info->node);
		svga_wcrt_mask(0x36, 0x10, 0x30);
		svga_wseq_mask(0x01, 0x20, 0x20);
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		pr_debug("fb%d: DPMS suspend (vsync off)\n", info->node);
		svga_wcrt_mask(0x36, 0x20, 0x30);
		svga_wseq_mask(0x01, 0x20, 0x20);
		break;
	case FB_BLANK_POWERDOWN:
		pr_debug("fb%d: DPMS off (no sync)\n", info->node);
		svga_wcrt_mask(0x36, 0x30, 0x30);
		svga_wseq_mask(0x01, 0x20, 0x20);
		break;
	}

	return 0;
}


static int vt8623fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	unsigned int offset;

	/* Calculate the offset */
	if (var->bits_per_pixel == 0) {
		offset = (var->yoffset / 16) * var->xres_virtual + var->xoffset;
		offset = offset >> 3;
	} else {
		offset = (var->yoffset * info->fix.line_length) +
			 (var->xoffset * var->bits_per_pixel / 8);
		offset = offset >> ((var->bits_per_pixel == 4) ? 2 : 1);
	}

	/* Set the offset */
	svga_wcrt_multi(vt8623_start_address_regs, offset);

	return 0;
}


/* ------------------------------------------------------------------------- */


/* Frame buffer operations */

static struct fb_ops vt8623fb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= vt8623fb_open,
	.fb_release	= vt8623fb_release,
	.fb_check_var	= vt8623fb_check_var,
	.fb_set_par	= vt8623fb_set_par,
	.fb_setcolreg	= vt8623fb_setcolreg,
	.fb_blank	= vt8623fb_blank,
	.fb_pan_display	= vt8623fb_pan_display,
	.fb_fillrect	= vt8623fb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= vt8623fb_imageblit,
	.fb_get_caps    = svga_get_caps,
};


/* PCI probe */

static int __devinit vt8623_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct fb_info *info;
	struct vt8623fb_info *par;
	unsigned int memsize1, memsize2;
	int rc;

	/* Ignore secondary VGA device because there is no VGA arbitration */
	if (! svga_primary_device(dev)) {
		dev_info(&(dev->dev), "ignoring secondary device\n");
		return -ENODEV;
	}

	/* Allocate and fill driver data structure */
	info = framebuffer_alloc(sizeof(struct vt8623fb_info), NULL);
	if (! info) {
		dev_err(&(dev->dev), "cannot allocate memory\n");
		return -ENOMEM;
	}

	par = info->par;
	mutex_init(&par->open_lock);

	info->flags = FBINFO_PARTIAL_PAN_OK | FBINFO_HWACCEL_YPAN;
	info->fbops = &vt8623fb_ops;

	/* Prepare PCI device */

	rc = pci_enable_device(dev);
	if (rc < 0) {
		dev_err(&(dev->dev), "cannot enable PCI device\n");
		goto err_enable_device;
	}

	rc = pci_request_regions(dev, "vt8623fb");
	if (rc < 0) {
		dev_err(&(dev->dev), "cannot reserve framebuffer region\n");
		goto err_request_regions;
	}

	info->fix.smem_start = pci_resource_start(dev, 0);
	info->fix.smem_len = pci_resource_len(dev, 0);
	info->fix.mmio_start = pci_resource_start(dev, 1);
	info->fix.mmio_len = pci_resource_len(dev, 1);

	/* Map physical IO memory address into kernel space */
	info->screen_base = pci_iomap(dev, 0, 0);
	if (! info->screen_base) {
		rc = -ENOMEM;
		dev_err(&(dev->dev), "iomap for framebuffer failed\n");
		goto err_iomap_1;
	}

	par->mmio_base = pci_iomap(dev, 1, 0);
	if (! par->mmio_base) {
		rc = -ENOMEM;
		dev_err(&(dev->dev), "iomap for MMIO failed\n");
		goto err_iomap_2;
	}

	/* Find how many physical memory there is on card */
	memsize1 = (vga_rseq(NULL, 0x34) + 1) >> 1;
	memsize2 = vga_rseq(NULL, 0x39) << 2;

	if ((16 <= memsize1) && (memsize1 <= 64) && (memsize1 == memsize2))
		info->screen_size = memsize1 << 20;
	else {
		dev_err(&(dev->dev), "memory size detection failed (%x %x), suppose 16 MB\n", memsize1, memsize2);
		info->screen_size = 16 << 20;
	}

	info->fix.smem_len = info->screen_size;
	strcpy(info->fix.id, "VIA VT8623");
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	info->fix.ypanstep = 0;
	info->fix.accel = FB_ACCEL_NONE;
	info->pseudo_palette = (void*)par->pseudo_palette;

	/* Prepare startup mode */

	rc = fb_find_mode(&(info->var), info, mode_option, NULL, 0, NULL, 8);
	if (! ((rc == 1) || (rc == 2))) {
		rc = -EINVAL;
		dev_err(&(dev->dev), "mode %s not found\n", mode_option);
		goto err_find_mode;
	}

	rc = fb_alloc_cmap(&info->cmap, 256, 0);
	if (rc < 0) {
		dev_err(&(dev->dev), "cannot allocate colormap\n");
		goto err_alloc_cmap;
	}

	rc = register_framebuffer(info);
	if (rc < 0) {
		dev_err(&(dev->dev), "cannot register framebugger\n");
		goto err_reg_fb;
	}

	printk(KERN_INFO "fb%d: %s on %s, %d MB RAM\n", info->node, info->fix.id,
		 pci_name(dev), info->fix.smem_len >> 20);

	/* Record a reference to the driver data */
	pci_set_drvdata(dev, info);

#ifdef CONFIG_MTRR
	if (mtrr) {
		par->mtrr_reg = -1;
		par->mtrr_reg = mtrr_add(info->fix.smem_start, info->fix.smem_len, MTRR_TYPE_WRCOMB, 1);
	}
#endif

	return 0;

	/* Error handling */
err_reg_fb:
	fb_dealloc_cmap(&info->cmap);
err_alloc_cmap:
err_find_mode:
	pci_iounmap(dev, par->mmio_base);
err_iomap_2:
	pci_iounmap(dev, info->screen_base);
err_iomap_1:
	pci_release_regions(dev);
err_request_regions:
/*	pci_disable_device(dev); */
err_enable_device:
	framebuffer_release(info);
	return rc;
}

/* PCI remove */

static void __devexit vt8623_pci_remove(struct pci_dev *dev)
{
	struct fb_info *info = pci_get_drvdata(dev);

	if (info) {
		struct vt8623fb_info *par = info->par;

#ifdef CONFIG_MTRR
		if (par->mtrr_reg >= 0) {
			mtrr_del(par->mtrr_reg, 0, 0);
			par->mtrr_reg = -1;
		}
#endif

		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);

		pci_iounmap(dev, info->screen_base);
		pci_iounmap(dev, par->mmio_base);
		pci_release_regions(dev);
/*		pci_disable_device(dev); */

		pci_set_drvdata(dev, NULL);
		framebuffer_release(info);
	}
}


#ifdef CONFIG_PM
/* PCI suspend */

static int vt8623_pci_suspend(struct pci_dev* dev, pm_message_t state)
{
	struct fb_info *info = pci_get_drvdata(dev);
	struct vt8623fb_info *par = info->par;

	dev_info(&(dev->dev), "suspend\n");

	acquire_console_sem();
	mutex_lock(&(par->open_lock));

	if ((state.event == PM_EVENT_FREEZE) || (par->ref_count == 0)) {
		mutex_unlock(&(par->open_lock));
		release_console_sem();
		return 0;
	}

	fb_set_suspend(info, 1);

	pci_save_state(dev);
	pci_disable_device(dev);
	pci_set_power_state(dev, pci_choose_state(dev, state));

	mutex_unlock(&(par->open_lock));
	release_console_sem();

	return 0;
}


/* PCI resume */

static int vt8623_pci_resume(struct pci_dev* dev)
{
	struct fb_info *info = pci_get_drvdata(dev);
	struct vt8623fb_info *par = info->par;

	dev_info(&(dev->dev), "resume\n");

	acquire_console_sem();
	mutex_lock(&(par->open_lock));

	if (par->ref_count == 0) {
		mutex_unlock(&(par->open_lock));
		release_console_sem();
		return 0;
	}

	pci_set_power_state(dev, PCI_D0);
	pci_restore_state(dev);

	if (pci_enable_device(dev))
		goto fail;

	pci_set_master(dev);

	vt8623fb_set_par(info);
	fb_set_suspend(info, 0);

	mutex_unlock(&(par->open_lock));
fail:
	release_console_sem();

	return 0;
}
#else
#define vt8623_pci_suspend NULL
#define vt8623_pci_resume NULL
#endif /* CONFIG_PM */

/* List of boards that we are trying to support */

static struct pci_device_id vt8623_devices[] __devinitdata = {
	{PCI_DEVICE(PCI_VENDOR_ID_VIA, 0x3122)},
	{0, 0, 0, 0, 0, 0, 0}
};

MODULE_DEVICE_TABLE(pci, vt8623_devices);

static struct pci_driver vt8623fb_pci_driver = {
	.name		= "vt8623fb",
	.id_table	= vt8623_devices,
	.probe		= vt8623_pci_probe,
	.remove		= __devexit_p(vt8623_pci_remove),
	.suspend	= vt8623_pci_suspend,
	.resume		= vt8623_pci_resume,
};

/* Cleanup */

static void __exit vt8623fb_cleanup(void)
{
	pr_debug("vt8623fb: cleaning up\n");
	pci_unregister_driver(&vt8623fb_pci_driver);
}

/* Driver Initialisation */

static int __init vt8623fb_init(void)
{

#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("vt8623fb", &option))
		return -ENODEV;

	if (option && *option)
		mode_option = option;
#endif

	pr_debug("vt8623fb: initializing\n");
	return pci_register_driver(&vt8623fb_pci_driver);
}

/* ------------------------------------------------------------------------- */

/* Modularization */

module_init(vt8623fb_init);
module_exit(vt8623fb_cleanup);
