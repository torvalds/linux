/*
 * linux/drivers/video/s3fb.c -- Frame buffer device driver for S3 Trio/Virge
 *
 * Copyright (c) 2006-2007 Ondrej Zajicek <santiago@crfreenet.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Code is based on David Boucher's viafb (http://davesdomain.org.uk/viafb/)
 * which is based on the code of neofb.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/svga.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/console.h> /* Why should fb driver call console functions? because console_lock() */
#include <video/vga.h>

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

struct s3fb_info {
	int chip, rev, mclk_freq;
	int wc_cookie;
	struct vgastate state;
	struct mutex open_lock;
	unsigned int ref_count;
	u32 pseudo_palette[16];
#ifdef CONFIG_FB_S3_DDC
	u8 __iomem *mmio;
	bool ddc_registered;
	struct i2c_adapter ddc_adapter;
	struct i2c_algo_bit_data ddc_algo;
#endif
};


/* ------------------------------------------------------------------------- */

static const struct svga_fb_format s3fb_formats[] = {
	{ 0,  {0, 6, 0},  {0, 6, 0},  {0, 6, 0}, {0, 0, 0}, 0,
		FB_TYPE_TEXT, FB_AUX_TEXT_SVGA_STEP4,	FB_VISUAL_PSEUDOCOLOR, 8, 16},
	{ 4,  {0, 4, 0},  {0, 4, 0},  {0, 4, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_PSEUDOCOLOR, 8, 16},
	{ 4,  {0, 4, 0},  {0, 4, 0},  {0, 4, 0}, {0, 0, 0}, 1,
		FB_TYPE_INTERLEAVED_PLANES, 1,		FB_VISUAL_PSEUDOCOLOR, 8, 16},
	{ 8,  {0, 8, 0},  {0, 8, 0},  {0, 8, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_PSEUDOCOLOR, 4, 8},
	{16,  {10, 5, 0}, {5, 5, 0},  {0, 5, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_TRUECOLOR, 2, 4},
	{16,  {11, 5, 0}, {5, 6, 0},  {0, 5, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_TRUECOLOR, 2, 4},
	{24,  {16, 8, 0}, {8, 8, 0},  {0, 8, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_TRUECOLOR, 1, 2},
	{32,  {16, 8, 0}, {8, 8, 0},  {0, 8, 0}, {0, 0, 0}, 0,
		FB_TYPE_PACKED_PIXELS, 0,		FB_VISUAL_TRUECOLOR, 1, 2},
	SVGA_FORMAT_END
};


static const struct svga_pll s3_pll = {3, 129, 3, 33, 0, 3,
	35000, 240000, 14318};
static const struct svga_pll s3_trio3d_pll = {3, 129, 3, 31, 0, 4,
	230000, 460000, 14318};

static const int s3_memsizes[] = {4096, 0, 3072, 8192, 2048, 6144, 1024, 512};

static const char * const s3_names[] = {"S3 Unknown", "S3 Trio32", "S3 Trio64", "S3 Trio64V+",
			"S3 Trio64UV+", "S3 Trio64V2/DX", "S3 Trio64V2/GX",
			"S3 Plato/PX", "S3 Aurora64V+", "S3 Virge",
			"S3 Virge/VX", "S3 Virge/DX", "S3 Virge/GX",
			"S3 Virge/GX2", "S3 Virge/GX2+", "",
			"S3 Trio3D/1X", "S3 Trio3D/2X", "S3 Trio3D/2X",
			"S3 Trio3D", "S3 Virge/MX"};

#define CHIP_UNKNOWN		0x00
#define CHIP_732_TRIO32		0x01
#define CHIP_764_TRIO64		0x02
#define CHIP_765_TRIO64VP	0x03
#define CHIP_767_TRIO64UVP	0x04
#define CHIP_775_TRIO64V2_DX	0x05
#define CHIP_785_TRIO64V2_GX	0x06
#define CHIP_551_PLATO_PX	0x07
#define CHIP_M65_AURORA64VP	0x08
#define CHIP_325_VIRGE		0x09
#define CHIP_988_VIRGE_VX	0x0A
#define CHIP_375_VIRGE_DX	0x0B
#define CHIP_385_VIRGE_GX	0x0C
#define CHIP_357_VIRGE_GX2	0x0D
#define CHIP_359_VIRGE_GX2P	0x0E
#define CHIP_360_TRIO3D_1X	0x10
#define CHIP_362_TRIO3D_2X	0x11
#define CHIP_368_TRIO3D_2X	0x12
#define CHIP_365_TRIO3D		0x13
#define CHIP_260_VIRGE_MX	0x14

#define CHIP_XXX_TRIO		0x80
#define CHIP_XXX_TRIO64V2_DXGX	0x81
#define CHIP_XXX_VIRGE_DXGX	0x82
#define CHIP_36X_TRIO3D_1X_2X	0x83

#define CHIP_UNDECIDED_FLAG	0x80
#define CHIP_MASK		0xFF

#define MMIO_OFFSET		0x1000000
#define MMIO_SIZE		0x10000

/* CRT timing register sets */

static const struct vga_regset s3_h_total_regs[]        = {{0x00, 0, 7}, {0x5D, 0, 0}, VGA_REGSET_END};
static const struct vga_regset s3_h_display_regs[]      = {{0x01, 0, 7}, {0x5D, 1, 1}, VGA_REGSET_END};
static const struct vga_regset s3_h_blank_start_regs[]  = {{0x02, 0, 7}, {0x5D, 2, 2}, VGA_REGSET_END};
static const struct vga_regset s3_h_blank_end_regs[]    = {{0x03, 0, 4}, {0x05, 7, 7}, VGA_REGSET_END};
static const struct vga_regset s3_h_sync_start_regs[]   = {{0x04, 0, 7}, {0x5D, 4, 4}, VGA_REGSET_END};
static const struct vga_regset s3_h_sync_end_regs[]     = {{0x05, 0, 4}, VGA_REGSET_END};

static const struct vga_regset s3_v_total_regs[]        = {{0x06, 0, 7}, {0x07, 0, 0}, {0x07, 5, 5}, {0x5E, 0, 0}, VGA_REGSET_END};
static const struct vga_regset s3_v_display_regs[]      = {{0x12, 0, 7}, {0x07, 1, 1}, {0x07, 6, 6}, {0x5E, 1, 1}, VGA_REGSET_END};
static const struct vga_regset s3_v_blank_start_regs[]  = {{0x15, 0, 7}, {0x07, 3, 3}, {0x09, 5, 5}, {0x5E, 2, 2}, VGA_REGSET_END};
static const struct vga_regset s3_v_blank_end_regs[]    = {{0x16, 0, 7}, VGA_REGSET_END};
static const struct vga_regset s3_v_sync_start_regs[]   = {{0x10, 0, 7}, {0x07, 2, 2}, {0x07, 7, 7}, {0x5E, 4, 4}, VGA_REGSET_END};
static const struct vga_regset s3_v_sync_end_regs[]     = {{0x11, 0, 3}, VGA_REGSET_END};

static const struct vga_regset s3_line_compare_regs[]   = {{0x18, 0, 7}, {0x07, 4, 4}, {0x09, 6, 6}, {0x5E, 6, 6}, VGA_REGSET_END};
static const struct vga_regset s3_start_address_regs[]  = {{0x0d, 0, 7}, {0x0c, 0, 7}, {0x69, 0, 4}, VGA_REGSET_END};
static const struct vga_regset s3_offset_regs[]         = {{0x13, 0, 7}, {0x51, 4, 5}, VGA_REGSET_END}; /* set 0x43 bit 2 to 0 */

static const struct vga_regset s3_dtpc_regs[]		= {{0x3B, 0, 7}, {0x5D, 6, 6}, VGA_REGSET_END};

static const struct svga_timing_regs s3_timing_regs     = {
	s3_h_total_regs, s3_h_display_regs, s3_h_blank_start_regs,
	s3_h_blank_end_regs, s3_h_sync_start_regs, s3_h_sync_end_regs,
	s3_v_total_regs, s3_v_display_regs, s3_v_blank_start_regs,
	s3_v_blank_end_regs, s3_v_sync_start_regs, s3_v_sync_end_regs,
};


/* ------------------------------------------------------------------------- */

/* Module parameters */


static char *mode_option;
static int mtrr = 1;
static int fasttext = 1;


MODULE_AUTHOR("(c) 2006-2007 Ondrej Zajicek <santiago@crfreenet.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("fbdev driver for S3 Trio/Virge");

module_param(mode_option, charp, 0444);
MODULE_PARM_DESC(mode_option, "Default video mode ('640x480-8@60', etc)");
module_param_named(mode, mode_option, charp, 0444);
MODULE_PARM_DESC(mode, "Default video mode ('640x480-8@60', etc) (deprecated)");
module_param(mtrr, int, 0444);
MODULE_PARM_DESC(mtrr, "Enable write-combining with MTRR (1=enable, 0=disable, default=1)");

module_param(fasttext, int, 0644);
MODULE_PARM_DESC(fasttext, "Enable S3 fast text mode (1=enable, 0=disable, default=1)");


/* ------------------------------------------------------------------------- */

#ifdef CONFIG_FB_S3_DDC

#define DDC_REG		0xaa		/* Trio 3D/1X/2X */
#define DDC_MMIO_REG	0xff20		/* all other chips */
#define DDC_SCL_OUT	(1 << 0)
#define DDC_SDA_OUT	(1 << 1)
#define DDC_SCL_IN	(1 << 2)
#define DDC_SDA_IN	(1 << 3)
#define DDC_DRIVE_EN	(1 << 4)

static bool s3fb_ddc_needs_mmio(int chip)
{
	return !(chip == CHIP_360_TRIO3D_1X  ||
		 chip == CHIP_362_TRIO3D_2X  ||
		 chip == CHIP_368_TRIO3D_2X);
}

static u8 s3fb_ddc_read(struct s3fb_info *par)
{
	if (s3fb_ddc_needs_mmio(par->chip))
		return readb(par->mmio + DDC_MMIO_REG);
	else
		return vga_rcrt(par->state.vgabase, DDC_REG);
}

static void s3fb_ddc_write(struct s3fb_info *par, u8 val)
{
	if (s3fb_ddc_needs_mmio(par->chip))
		writeb(val, par->mmio + DDC_MMIO_REG);
	else
		vga_wcrt(par->state.vgabase, DDC_REG, val);
}

static void s3fb_ddc_setscl(void *data, int val)
{
	struct s3fb_info *par = data;
	unsigned char reg;

	reg = s3fb_ddc_read(par) | DDC_DRIVE_EN;
	if (val)
		reg |= DDC_SCL_OUT;
	else
		reg &= ~DDC_SCL_OUT;
	s3fb_ddc_write(par, reg);
}

static void s3fb_ddc_setsda(void *data, int val)
{
	struct s3fb_info *par = data;
	unsigned char reg;

	reg = s3fb_ddc_read(par) | DDC_DRIVE_EN;
	if (val)
		reg |= DDC_SDA_OUT;
	else
		reg &= ~DDC_SDA_OUT;
	s3fb_ddc_write(par, reg);
}

static int s3fb_ddc_getscl(void *data)
{
	struct s3fb_info *par = data;

	return !!(s3fb_ddc_read(par) & DDC_SCL_IN);
}

static int s3fb_ddc_getsda(void *data)
{
	struct s3fb_info *par = data;

	return !!(s3fb_ddc_read(par) & DDC_SDA_IN);
}

static int s3fb_setup_ddc_bus(struct fb_info *info)
{
	struct s3fb_info *par = info->par;

	strlcpy(par->ddc_adapter.name, info->fix.id,
		sizeof(par->ddc_adapter.name));
	par->ddc_adapter.owner		= THIS_MODULE;
	par->ddc_adapter.class		= I2C_CLASS_DDC;
	par->ddc_adapter.algo_data	= &par->ddc_algo;
	par->ddc_adapter.dev.parent	= info->device;
	par->ddc_algo.setsda		= s3fb_ddc_setsda;
	par->ddc_algo.setscl		= s3fb_ddc_setscl;
	par->ddc_algo.getsda		= s3fb_ddc_getsda;
	par->ddc_algo.getscl		= s3fb_ddc_getscl;
	par->ddc_algo.udelay		= 10;
	par->ddc_algo.timeout		= 20;
	par->ddc_algo.data		= par;

	i2c_set_adapdata(&par->ddc_adapter, par);

	/*
	 * some Virge cards have external MUX to switch chip I2C bus between
	 * DDC and extension pins - switch it do DDC
	 */
/*	vga_wseq(par->state.vgabase, 0x08, 0x06); - not needed, already unlocked */
	if (par->chip == CHIP_357_VIRGE_GX2 ||
	    par->chip == CHIP_359_VIRGE_GX2P ||
	    par->chip == CHIP_260_VIRGE_MX)
		svga_wseq_mask(par->state.vgabase, 0x0d, 0x01, 0x03);
	else
		svga_wseq_mask(par->state.vgabase, 0x0d, 0x00, 0x03);
	/* some Virge need this or the DDC is ignored */
	svga_wcrt_mask(par->state.vgabase, 0x5c, 0x03, 0x03);

	return i2c_bit_add_bus(&par->ddc_adapter);
}
#endif /* CONFIG_FB_S3_DDC */


/* ------------------------------------------------------------------------- */

/* Set font in S3 fast text mode */

static void s3fb_settile_fast(struct fb_info *info, struct fb_tilemap *map)
{
	const u8 *font = map->data;
	u8 __iomem *fb = (u8 __iomem *) info->screen_base;
	int i, c;

	if ((map->width != 8) || (map->height != 16) ||
	    (map->depth != 1) || (map->length != 256)) {
		fb_err(info, "unsupported font parameters: width %d, height %d, depth %d, length %d\n",
		       map->width, map->height, map->depth, map->length);
		return;
	}

	fb += 2;
	for (i = 0; i < map->height; i++) {
		for (c = 0; c < map->length; c++) {
			fb_writeb(font[c * map->height + i], fb + c * 4);
		}
		fb += 1024;
	}
}

static void s3fb_tilecursor(struct fb_info *info, struct fb_tilecursor *cursor)
{
	struct s3fb_info *par = info->par;

	svga_tilecursor(par->state.vgabase, info, cursor);
}

static struct fb_tile_ops s3fb_tile_ops = {
	.fb_settile	= svga_settile,
	.fb_tilecopy	= svga_tilecopy,
	.fb_tilefill    = svga_tilefill,
	.fb_tileblit    = svga_tileblit,
	.fb_tilecursor  = s3fb_tilecursor,
	.fb_get_tilemax = svga_get_tilemax,
};

static struct fb_tile_ops s3fb_fast_tile_ops = {
	.fb_settile	= s3fb_settile_fast,
	.fb_tilecopy	= svga_tilecopy,
	.fb_tilefill    = svga_tilefill,
	.fb_tileblit    = svga_tileblit,
	.fb_tilecursor  = s3fb_tilecursor,
	.fb_get_tilemax = svga_get_tilemax,
};


/* ------------------------------------------------------------------------- */

/* image data is MSB-first, fb structure is MSB-first too */
static inline u32 expand_color(u32 c)
{
	return ((c & 1) | ((c & 2) << 7) | ((c & 4) << 14) | ((c & 8) << 21)) * 0xFF;
}

/* s3fb_iplan_imageblit silently assumes that almost everything is 8-pixel aligned */
static void s3fb_iplan_imageblit(struct fb_info *info, const struct fb_image *image)
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

/* s3fb_iplan_fillrect silently assumes that almost everything is 8-pixel aligned */
static void s3fb_iplan_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
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

/* s3fb_cfb4_imageblit silently assumes that almost everything is 8-pixel aligned */
static void s3fb_cfb4_imageblit(struct fb_info *info, const struct fb_image *image)
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

static void s3fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	if ((info->var.bits_per_pixel == 4) && (image->depth == 1)
	    && ((image->width % 8) == 0) && ((image->dx % 8) == 0)) {
		if (info->fix.type == FB_TYPE_INTERLEAVED_PLANES)
			s3fb_iplan_imageblit(info, image);
		else
			s3fb_cfb4_imageblit(info, image);
	} else
		cfb_imageblit(info, image);
}

static void s3fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	if ((info->var.bits_per_pixel == 4)
	    && ((rect->width % 8) == 0) && ((rect->dx % 8) == 0)
	    && (info->fix.type == FB_TYPE_INTERLEAVED_PLANES))
		s3fb_iplan_fillrect(info, rect);
	 else
		cfb_fillrect(info, rect);
}



/* ------------------------------------------------------------------------- */


static void s3_set_pixclock(struct fb_info *info, u32 pixclock)
{
	struct s3fb_info *par = info->par;
	u16 m, n, r;
	u8 regval;
	int rv;

	rv = svga_compute_pll((par->chip == CHIP_365_TRIO3D) ? &s3_trio3d_pll : &s3_pll,
			      1000000000 / pixclock, &m, &n, &r, info->node);
	if (rv < 0) {
		fb_err(info, "cannot set requested pixclock, keeping old value\n");
		return;
	}

	/* Set VGA misc register  */
	regval = vga_r(par->state.vgabase, VGA_MIS_R);
	vga_w(par->state.vgabase, VGA_MIS_W, regval | VGA_MIS_ENB_PLL_LOAD);

	/* Set S3 clock registers */
	if (par->chip == CHIP_357_VIRGE_GX2 ||
	    par->chip == CHIP_359_VIRGE_GX2P ||
	    par->chip == CHIP_360_TRIO3D_1X ||
	    par->chip == CHIP_362_TRIO3D_2X ||
	    par->chip == CHIP_368_TRIO3D_2X ||
	    par->chip == CHIP_260_VIRGE_MX) {
		vga_wseq(par->state.vgabase, 0x12, (n - 2) | ((r & 3) << 6));	/* n and two bits of r */
		vga_wseq(par->state.vgabase, 0x29, r >> 2); /* remaining highest bit of r */
	} else
		vga_wseq(par->state.vgabase, 0x12, (n - 2) | (r << 5));
	vga_wseq(par->state.vgabase, 0x13, m - 2);

	udelay(1000);

	/* Activate clock - write 0, 1, 0 to seq/15 bit 5 */
	regval = vga_rseq (par->state.vgabase, 0x15); /* | 0x80; */
	vga_wseq(par->state.vgabase, 0x15, regval & ~(1<<5));
	vga_wseq(par->state.vgabase, 0x15, regval |  (1<<5));
	vga_wseq(par->state.vgabase, 0x15, regval & ~(1<<5));
}


/* Open framebuffer */

static int s3fb_open(struct fb_info *info, int user)
{
	struct s3fb_info *par = info->par;

	mutex_lock(&(par->open_lock));
	if (par->ref_count == 0) {
		void __iomem *vgabase = par->state.vgabase;

		memset(&(par->state), 0, sizeof(struct vgastate));
		par->state.vgabase = vgabase;
		par->state.flags = VGA_SAVE_MODE | VGA_SAVE_FONTS | VGA_SAVE_CMAP;
		par->state.num_crtc = 0x70;
		par->state.num_seq = 0x20;
		save_vga(&(par->state));
	}

	par->ref_count++;
	mutex_unlock(&(par->open_lock));

	return 0;
}

/* Close framebuffer */

static int s3fb_release(struct fb_info *info, int user)
{
	struct s3fb_info *par = info->par;

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

/* Validate passed in var */

static int s3fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct s3fb_info *par = info->par;
	int rv, mem, step;
	u16 m, n, r;

	if (!var->pixclock)
		return -EINVAL;

	/* Find appropriate format */
	rv = svga_match_format (s3fb_formats, var, NULL);

	/* 32bpp mode is not supported on VIRGE VX,
	   24bpp is not supported on others */
	if ((par->chip == CHIP_988_VIRGE_VX) ? (rv == 7) : (rv == 6))
		rv = -EINVAL;

	if (rv < 0) {
		fb_err(info, "unsupported mode requested\n");
		return rv;
	}

	/* Do not allow to have real resoulution larger than virtual */
	if (var->xres > var->xres_virtual)
		var->xres_virtual = var->xres;

	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;

	/* Round up xres_virtual to have proper alignment of lines */
	step = s3fb_formats[rv].xresstep - 1;
	var->xres_virtual = (var->xres_virtual+step) & ~step;

	/* Check whether have enough memory */
	mem = ((var->bits_per_pixel * var->xres_virtual) >> 3) * var->yres_virtual;
	if (mem > info->screen_size) {
		fb_err(info, "not enough framebuffer memory (%d kB requested , %u kB available)\n",
		       mem >> 10, (unsigned int) (info->screen_size >> 10));
		return -EINVAL;
	}

	rv = svga_check_timings (&s3_timing_regs, var, info->node);
	if (rv < 0) {
		fb_err(info, "invalid timings requested\n");
		return rv;
	}

	rv = svga_compute_pll(&s3_pll, PICOS2KHZ(var->pixclock), &m, &n, &r,
				info->node);
	if (rv < 0) {
		fb_err(info, "invalid pixclock value requested\n");
		return rv;
	}

	return 0;
}

/* Set video mode from par */

static int s3fb_set_par(struct fb_info *info)
{
	struct s3fb_info *par = info->par;
	u32 value, mode, hmul, offset_value, screen_size, multiplex, dbytes;
	u32 bpp = info->var.bits_per_pixel;
	u32 htotal, hsstart;

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
		info->tileops = fasttext ? &s3fb_fast_tile_ops : &s3fb_tile_ops;

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
	vga_wcrt(par->state.vgabase, 0x38, 0x48);
	vga_wcrt(par->state.vgabase, 0x39, 0xA5);
	vga_wseq(par->state.vgabase, 0x08, 0x06);
	svga_wcrt_mask(par->state.vgabase, 0x11, 0x00, 0x80);

	/* Blank screen and turn off sync */
	svga_wseq_mask(par->state.vgabase, 0x01, 0x20, 0x20);
	svga_wcrt_mask(par->state.vgabase, 0x17, 0x00, 0x80);

	/* Set default values */
	svga_set_default_gfx_regs(par->state.vgabase);
	svga_set_default_atc_regs(par->state.vgabase);
	svga_set_default_seq_regs(par->state.vgabase);
	svga_set_default_crt_regs(par->state.vgabase);
	svga_wcrt_multi(par->state.vgabase, s3_line_compare_regs, 0xFFFFFFFF);
	svga_wcrt_multi(par->state.vgabase, s3_start_address_regs, 0);

	/* S3 specific initialization */
	svga_wcrt_mask(par->state.vgabase, 0x58, 0x10, 0x10); /* enable linear framebuffer */
	svga_wcrt_mask(par->state.vgabase, 0x31, 0x08, 0x08); /* enable sequencer access to framebuffer above 256 kB */

/*	svga_wcrt_mask(par->state.vgabase, 0x33, 0x08, 0x08); */ /* DDR ?	*/
/*	svga_wcrt_mask(par->state.vgabase, 0x43, 0x01, 0x01); */ /* DDR ?	*/
	svga_wcrt_mask(par->state.vgabase, 0x33, 0x00, 0x08); /* no DDR ?	*/
	svga_wcrt_mask(par->state.vgabase, 0x43, 0x00, 0x01); /* no DDR ?	*/

	svga_wcrt_mask(par->state.vgabase, 0x5D, 0x00, 0x28); /* Clear strange HSlen bits */

/*	svga_wcrt_mask(par->state.vgabase, 0x58, 0x03, 0x03); */

/*	svga_wcrt_mask(par->state.vgabase, 0x53, 0x12, 0x13); */ /* enable MMIO */
/*	svga_wcrt_mask(par->state.vgabase, 0x40, 0x08, 0x08); */ /* enable write buffer */


	/* Set the offset register */
	fb_dbg(info, "offset register       : %d\n", offset_value);
	svga_wcrt_multi(par->state.vgabase, s3_offset_regs, offset_value);

	if (par->chip != CHIP_357_VIRGE_GX2 &&
	    par->chip != CHIP_359_VIRGE_GX2P &&
	    par->chip != CHIP_360_TRIO3D_1X &&
	    par->chip != CHIP_362_TRIO3D_2X &&
	    par->chip != CHIP_368_TRIO3D_2X &&
	    par->chip != CHIP_260_VIRGE_MX) {
		vga_wcrt(par->state.vgabase, 0x54, 0x18); /* M parameter */
		vga_wcrt(par->state.vgabase, 0x60, 0xff); /* N parameter */
		vga_wcrt(par->state.vgabase, 0x61, 0xff); /* L parameter */
		vga_wcrt(par->state.vgabase, 0x62, 0xff); /* L parameter */
	}

	vga_wcrt(par->state.vgabase, 0x3A, 0x35);
	svga_wattr(par->state.vgabase, 0x33, 0x00);

	if (info->var.vmode & FB_VMODE_DOUBLE)
		svga_wcrt_mask(par->state.vgabase, 0x09, 0x80, 0x80);
	else
		svga_wcrt_mask(par->state.vgabase, 0x09, 0x00, 0x80);

	if (info->var.vmode & FB_VMODE_INTERLACED)
		svga_wcrt_mask(par->state.vgabase, 0x42, 0x20, 0x20);
	else
		svga_wcrt_mask(par->state.vgabase, 0x42, 0x00, 0x20);

	/* Disable hardware graphics cursor */
	svga_wcrt_mask(par->state.vgabase, 0x45, 0x00, 0x01);
	/* Disable Streams engine */
	svga_wcrt_mask(par->state.vgabase, 0x67, 0x00, 0x0C);

	mode = svga_match_format(s3fb_formats, &(info->var), &(info->fix));

	/* S3 virge DX hack */
	if (par->chip == CHIP_375_VIRGE_DX) {
		vga_wcrt(par->state.vgabase, 0x86, 0x80);
		vga_wcrt(par->state.vgabase, 0x90, 0x00);
	}

	/* S3 virge VX hack */
	if (par->chip == CHIP_988_VIRGE_VX) {
		vga_wcrt(par->state.vgabase, 0x50, 0x00);
		vga_wcrt(par->state.vgabase, 0x67, 0x50);
		msleep(10); /* screen remains blank sometimes without this */
		vga_wcrt(par->state.vgabase, 0x63, (mode <= 2) ? 0x90 : 0x09);
		vga_wcrt(par->state.vgabase, 0x66, 0x90);
	}

	if (par->chip == CHIP_357_VIRGE_GX2 ||
	    par->chip == CHIP_359_VIRGE_GX2P ||
	    par->chip == CHIP_360_TRIO3D_1X ||
	    par->chip == CHIP_362_TRIO3D_2X ||
	    par->chip == CHIP_368_TRIO3D_2X ||
	    par->chip == CHIP_365_TRIO3D    ||
	    par->chip == CHIP_375_VIRGE_DX  ||
	    par->chip == CHIP_385_VIRGE_GX  ||
	    par->chip == CHIP_260_VIRGE_MX) {
		dbytes = info->var.xres * ((bpp+7)/8);
		vga_wcrt(par->state.vgabase, 0x91, (dbytes + 7) / 8);
		vga_wcrt(par->state.vgabase, 0x90, (((dbytes + 7) / 8) >> 8) | 0x80);

		vga_wcrt(par->state.vgabase, 0x66, 0x81);
	}

	if (par->chip == CHIP_357_VIRGE_GX2  ||
	    par->chip == CHIP_359_VIRGE_GX2P ||
	    par->chip == CHIP_360_TRIO3D_1X ||
	    par->chip == CHIP_362_TRIO3D_2X ||
	    par->chip == CHIP_368_TRIO3D_2X ||
	    par->chip == CHIP_260_VIRGE_MX)
		vga_wcrt(par->state.vgabase, 0x34, 0x00);
	else	/* enable Data Transfer Position Control (DTPC) */
		vga_wcrt(par->state.vgabase, 0x34, 0x10);

	svga_wcrt_mask(par->state.vgabase, 0x31, 0x00, 0x40);
	multiplex = 0;
	hmul = 1;

	/* Set mode-specific register values */
	switch (mode) {
	case 0:
		fb_dbg(info, "text mode\n");
		svga_set_textmode_vga_regs(par->state.vgabase);

		/* Set additional registers like in 8-bit mode */
		svga_wcrt_mask(par->state.vgabase, 0x50, 0x00, 0x30);
		svga_wcrt_mask(par->state.vgabase, 0x67, 0x00, 0xF0);

		/* Disable enhanced mode */
		svga_wcrt_mask(par->state.vgabase, 0x3A, 0x00, 0x30);

		if (fasttext) {
			fb_dbg(info, "high speed text mode set\n");
			svga_wcrt_mask(par->state.vgabase, 0x31, 0x40, 0x40);
		}
		break;
	case 1:
		fb_dbg(info, "4 bit pseudocolor\n");
		vga_wgfx(par->state.vgabase, VGA_GFX_MODE, 0x40);

		/* Set additional registers like in 8-bit mode */
		svga_wcrt_mask(par->state.vgabase, 0x50, 0x00, 0x30);
		svga_wcrt_mask(par->state.vgabase, 0x67, 0x00, 0xF0);

		/* disable enhanced mode */
		svga_wcrt_mask(par->state.vgabase, 0x3A, 0x00, 0x30);
		break;
	case 2:
		fb_dbg(info, "4 bit pseudocolor, planar\n");

		/* Set additional registers like in 8-bit mode */
		svga_wcrt_mask(par->state.vgabase, 0x50, 0x00, 0x30);
		svga_wcrt_mask(par->state.vgabase, 0x67, 0x00, 0xF0);

		/* disable enhanced mode */
		svga_wcrt_mask(par->state.vgabase, 0x3A, 0x00, 0x30);
		break;
	case 3:
		fb_dbg(info, "8 bit pseudocolor\n");
		svga_wcrt_mask(par->state.vgabase, 0x50, 0x00, 0x30);
		if (info->var.pixclock > 20000 ||
		    par->chip == CHIP_357_VIRGE_GX2 ||
		    par->chip == CHIP_359_VIRGE_GX2P ||
		    par->chip == CHIP_360_TRIO3D_1X ||
		    par->chip == CHIP_362_TRIO3D_2X ||
		    par->chip == CHIP_368_TRIO3D_2X ||
		    par->chip == CHIP_260_VIRGE_MX)
			svga_wcrt_mask(par->state.vgabase, 0x67, 0x00, 0xF0);
		else {
			svga_wcrt_mask(par->state.vgabase, 0x67, 0x10, 0xF0);
			multiplex = 1;
		}
		break;
	case 4:
		fb_dbg(info, "5/5/5 truecolor\n");
		if (par->chip == CHIP_988_VIRGE_VX) {
			if (info->var.pixclock > 20000)
				svga_wcrt_mask(par->state.vgabase, 0x67, 0x20, 0xF0);
			else
				svga_wcrt_mask(par->state.vgabase, 0x67, 0x30, 0xF0);
		} else if (par->chip == CHIP_365_TRIO3D) {
			svga_wcrt_mask(par->state.vgabase, 0x50, 0x10, 0x30);
			if (info->var.pixclock > 8695) {
				svga_wcrt_mask(par->state.vgabase, 0x67, 0x30, 0xF0);
				hmul = 2;
			} else {
				svga_wcrt_mask(par->state.vgabase, 0x67, 0x20, 0xF0);
				multiplex = 1;
			}
		} else {
			svga_wcrt_mask(par->state.vgabase, 0x50, 0x10, 0x30);
			svga_wcrt_mask(par->state.vgabase, 0x67, 0x30, 0xF0);
			if (par->chip != CHIP_357_VIRGE_GX2 &&
			    par->chip != CHIP_359_VIRGE_GX2P &&
			    par->chip != CHIP_360_TRIO3D_1X &&
			    par->chip != CHIP_362_TRIO3D_2X &&
			    par->chip != CHIP_368_TRIO3D_2X &&
			    par->chip != CHIP_260_VIRGE_MX)
				hmul = 2;
		}
		break;
	case 5:
		fb_dbg(info, "5/6/5 truecolor\n");
		if (par->chip == CHIP_988_VIRGE_VX) {
			if (info->var.pixclock > 20000)
				svga_wcrt_mask(par->state.vgabase, 0x67, 0x40, 0xF0);
			else
				svga_wcrt_mask(par->state.vgabase, 0x67, 0x50, 0xF0);
		} else if (par->chip == CHIP_365_TRIO3D) {
			svga_wcrt_mask(par->state.vgabase, 0x50, 0x10, 0x30);
			if (info->var.pixclock > 8695) {
				svga_wcrt_mask(par->state.vgabase, 0x67, 0x50, 0xF0);
				hmul = 2;
			} else {
				svga_wcrt_mask(par->state.vgabase, 0x67, 0x40, 0xF0);
				multiplex = 1;
			}
		} else {
			svga_wcrt_mask(par->state.vgabase, 0x50, 0x10, 0x30);
			svga_wcrt_mask(par->state.vgabase, 0x67, 0x50, 0xF0);
			if (par->chip != CHIP_357_VIRGE_GX2 &&
			    par->chip != CHIP_359_VIRGE_GX2P &&
			    par->chip != CHIP_360_TRIO3D_1X &&
			    par->chip != CHIP_362_TRIO3D_2X &&
			    par->chip != CHIP_368_TRIO3D_2X &&
			    par->chip != CHIP_260_VIRGE_MX)
				hmul = 2;
		}
		break;
	case 6:
		/* VIRGE VX case */
		fb_dbg(info, "8/8/8 truecolor\n");
		svga_wcrt_mask(par->state.vgabase, 0x67, 0xD0, 0xF0);
		break;
	case 7:
		fb_dbg(info, "8/8/8/8 truecolor\n");
		svga_wcrt_mask(par->state.vgabase, 0x50, 0x30, 0x30);
		svga_wcrt_mask(par->state.vgabase, 0x67, 0xD0, 0xF0);
		break;
	default:
		fb_err(info, "unsupported mode - bug\n");
		return -EINVAL;
	}

	if (par->chip != CHIP_988_VIRGE_VX) {
		svga_wseq_mask(par->state.vgabase, 0x15, multiplex ? 0x10 : 0x00, 0x10);
		svga_wseq_mask(par->state.vgabase, 0x18, multiplex ? 0x80 : 0x00, 0x80);
	}

	s3_set_pixclock(info, info->var.pixclock);
	svga_set_timings(par->state.vgabase, &s3_timing_regs, &(info->var), hmul, 1,
			 (info->var.vmode & FB_VMODE_DOUBLE)     ? 2 : 1,
			 (info->var.vmode & FB_VMODE_INTERLACED) ? 2 : 1,
			 hmul, info->node);

	/* Set interlaced mode start/end register */
	htotal = info->var.xres + info->var.left_margin + info->var.right_margin + info->var.hsync_len;
	htotal = ((htotal * hmul) / 8) - 5;
	vga_wcrt(par->state.vgabase, 0x3C, (htotal + 1) / 2);

	/* Set Data Transfer Position */
	hsstart = ((info->var.xres + info->var.right_margin) * hmul) / 8;
	/* + 2 is needed for Virge/VX, does no harm on other cards */
	value = clamp((htotal + hsstart + 1) / 2 + 2, hsstart + 4, htotal + 1);
	svga_wcrt_multi(par->state.vgabase, s3_dtpc_regs, value);

	memset_io(info->screen_base, 0x00, screen_size);
	/* Device and screen back on */
	svga_wcrt_mask(par->state.vgabase, 0x17, 0x80, 0x80);
	svga_wseq_mask(par->state.vgabase, 0x01, 0x00, 0x20);

	return 0;
}

/* Set a colour register */

static int s3fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
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
		else return -EINVAL;
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

static int s3fb_blank(int blank_mode, struct fb_info *info)
{
	struct s3fb_info *par = info->par;

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		fb_dbg(info, "unblank\n");
		svga_wcrt_mask(par->state.vgabase, 0x56, 0x00, 0x06);
		svga_wseq_mask(par->state.vgabase, 0x01, 0x00, 0x20);
		break;
	case FB_BLANK_NORMAL:
		fb_dbg(info, "blank\n");
		svga_wcrt_mask(par->state.vgabase, 0x56, 0x00, 0x06);
		svga_wseq_mask(par->state.vgabase, 0x01, 0x20, 0x20);
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		fb_dbg(info, "hsync\n");
		svga_wcrt_mask(par->state.vgabase, 0x56, 0x02, 0x06);
		svga_wseq_mask(par->state.vgabase, 0x01, 0x20, 0x20);
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		fb_dbg(info, "vsync\n");
		svga_wcrt_mask(par->state.vgabase, 0x56, 0x04, 0x06);
		svga_wseq_mask(par->state.vgabase, 0x01, 0x20, 0x20);
		break;
	case FB_BLANK_POWERDOWN:
		fb_dbg(info, "sync down\n");
		svga_wcrt_mask(par->state.vgabase, 0x56, 0x06, 0x06);
		svga_wseq_mask(par->state.vgabase, 0x01, 0x20, 0x20);
		break;
	}

	return 0;
}


/* Pan the display */

static int s3fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct s3fb_info *par = info->par;
	unsigned int offset;

	/* Calculate the offset */
	if (info->var.bits_per_pixel == 0) {
		offset = (var->yoffset / 16) * (info->var.xres_virtual / 2)
		       + (var->xoffset / 2);
		offset = offset >> 2;
	} else {
		offset = (var->yoffset * info->fix.line_length) +
			 (var->xoffset * info->var.bits_per_pixel / 8);
		offset = offset >> 2;
	}

	/* Set the offset */
	svga_wcrt_multi(par->state.vgabase, s3_start_address_regs, offset);

	return 0;
}

/* ------------------------------------------------------------------------- */

/* Frame buffer operations */

static const struct fb_ops s3fb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= s3fb_open,
	.fb_release	= s3fb_release,
	.fb_check_var	= s3fb_check_var,
	.fb_set_par	= s3fb_set_par,
	.fb_setcolreg	= s3fb_setcolreg,
	.fb_blank	= s3fb_blank,
	.fb_pan_display	= s3fb_pan_display,
	.fb_fillrect	= s3fb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= s3fb_imageblit,
	.fb_get_caps    = svga_get_caps,
};

/* ------------------------------------------------------------------------- */

static int s3_identification(struct s3fb_info *par)
{
	int chip = par->chip;

	if (chip == CHIP_XXX_TRIO) {
		u8 cr30 = vga_rcrt(par->state.vgabase, 0x30);
		u8 cr2e = vga_rcrt(par->state.vgabase, 0x2e);
		u8 cr2f = vga_rcrt(par->state.vgabase, 0x2f);

		if ((cr30 == 0xE0) || (cr30 == 0xE1)) {
			if (cr2e == 0x10)
				return CHIP_732_TRIO32;
			if (cr2e == 0x11) {
				if (! (cr2f & 0x40))
					return CHIP_764_TRIO64;
				else
					return CHIP_765_TRIO64VP;
			}
		}
	}

	if (chip == CHIP_XXX_TRIO64V2_DXGX) {
		u8 cr6f = vga_rcrt(par->state.vgabase, 0x6f);

		if (! (cr6f & 0x01))
			return CHIP_775_TRIO64V2_DX;
		else
			return CHIP_785_TRIO64V2_GX;
	}

	if (chip == CHIP_XXX_VIRGE_DXGX) {
		u8 cr6f = vga_rcrt(par->state.vgabase, 0x6f);

		if (! (cr6f & 0x01))
			return CHIP_375_VIRGE_DX;
		else
			return CHIP_385_VIRGE_GX;
	}

	if (chip == CHIP_36X_TRIO3D_1X_2X) {
		switch (vga_rcrt(par->state.vgabase, 0x2f)) {
		case 0x00:
			return CHIP_360_TRIO3D_1X;
		case 0x01:
			return CHIP_362_TRIO3D_2X;
		case 0x02:
			return CHIP_368_TRIO3D_2X;
		}
	}

	return CHIP_UNKNOWN;
}


/* PCI probe */

static int s3_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct pci_bus_region bus_reg;
	struct resource vga_res;
	struct fb_info *info;
	struct s3fb_info *par;
	int rc;
	u8 regval, cr38, cr39;
	bool found = false;

	/* Ignore secondary VGA device because there is no VGA arbitration */
	if (! svga_primary_device(dev)) {
		dev_info(&(dev->dev), "ignoring secondary device\n");
		return -ENODEV;
	}

	/* Allocate and fill driver data structure */
	info = framebuffer_alloc(sizeof(struct s3fb_info), &(dev->dev));
	if (!info)
		return -ENOMEM;

	par = info->par;
	mutex_init(&par->open_lock);

	info->flags = FBINFO_PARTIAL_PAN_OK | FBINFO_HWACCEL_YPAN;
	info->fbops = &s3fb_ops;

	/* Prepare PCI device */
	rc = pci_enable_device(dev);
	if (rc < 0) {
		dev_err(info->device, "cannot enable PCI device\n");
		goto err_enable_device;
	}

	rc = pci_request_regions(dev, "s3fb");
	if (rc < 0) {
		dev_err(info->device, "cannot reserve framebuffer region\n");
		goto err_request_regions;
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

	/* Unlock regs */
	cr38 = vga_rcrt(par->state.vgabase, 0x38);
	cr39 = vga_rcrt(par->state.vgabase, 0x39);
	vga_wseq(par->state.vgabase, 0x08, 0x06);
	vga_wcrt(par->state.vgabase, 0x38, 0x48);
	vga_wcrt(par->state.vgabase, 0x39, 0xA5);

	/* Identify chip type */
	par->chip = id->driver_data & CHIP_MASK;
	par->rev = vga_rcrt(par->state.vgabase, 0x2f);
	if (par->chip & CHIP_UNDECIDED_FLAG)
		par->chip = s3_identification(par);

	/* Find how many physical memory there is on card */
	/* 0x36 register is accessible even if other registers are locked */
	regval = vga_rcrt(par->state.vgabase, 0x36);
	if (par->chip == CHIP_360_TRIO3D_1X ||
	    par->chip == CHIP_362_TRIO3D_2X ||
	    par->chip == CHIP_368_TRIO3D_2X ||
	    par->chip == CHIP_365_TRIO3D) {
		switch ((regval & 0xE0) >> 5) {
		case 0: /* 8MB -- only 4MB usable for display */
		case 1: /* 4MB with 32-bit bus */
		case 2:	/* 4MB */
			info->screen_size = 4 << 20;
			break;
		case 4: /* 2MB on 365 Trio3D */
		case 6: /* 2MB */
			info->screen_size = 2 << 20;
			break;
		}
	} else if (par->chip == CHIP_357_VIRGE_GX2 ||
		   par->chip == CHIP_359_VIRGE_GX2P ||
		   par->chip == CHIP_260_VIRGE_MX) {
		switch ((regval & 0xC0) >> 6) {
		case 1: /* 4MB */
			info->screen_size = 4 << 20;
			break;
		case 3: /* 2MB */
			info->screen_size = 2 << 20;
			break;
		}
	} else if (par->chip == CHIP_988_VIRGE_VX) {
		switch ((regval & 0x60) >> 5) {
		case 0: /* 2MB */
			info->screen_size = 2 << 20;
			break;
		case 1: /* 4MB */
			info->screen_size = 4 << 20;
			break;
		case 2: /* 6MB */
			info->screen_size = 6 << 20;
			break;
		case 3: /* 8MB */
			info->screen_size = 8 << 20;
			break;
		}
		/* off-screen memory */
		regval = vga_rcrt(par->state.vgabase, 0x37);
		switch ((regval & 0x60) >> 5) {
		case 1: /* 4MB */
			info->screen_size -= 4 << 20;
			break;
		case 2: /* 2MB */
			info->screen_size -= 2 << 20;
			break;
		}
	} else
		info->screen_size = s3_memsizes[regval >> 5] << 10;
	info->fix.smem_len = info->screen_size;

	/* Find MCLK frequency */
	regval = vga_rseq(par->state.vgabase, 0x10);
	par->mclk_freq = ((vga_rseq(par->state.vgabase, 0x11) + 2) * 14318) / ((regval & 0x1F)  + 2);
	par->mclk_freq = par->mclk_freq >> (regval >> 5);

	/* Restore locks */
	vga_wcrt(par->state.vgabase, 0x38, cr38);
	vga_wcrt(par->state.vgabase, 0x39, cr39);

	strcpy(info->fix.id, s3_names [par->chip]);
	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	info->fix.ypanstep = 0;
	info->fix.accel = FB_ACCEL_NONE;
	info->pseudo_palette = (void*) (par->pseudo_palette);
	info->var.bits_per_pixel = 8;

#ifdef CONFIG_FB_S3_DDC
	/* Enable MMIO if needed */
	if (s3fb_ddc_needs_mmio(par->chip)) {
		par->mmio = ioremap(info->fix.smem_start + MMIO_OFFSET, MMIO_SIZE);
		if (par->mmio)
			svga_wcrt_mask(par->state.vgabase, 0x53, 0x08, 0x08);	/* enable MMIO */
		else
			dev_err(info->device, "unable to map MMIO at 0x%lx, disabling DDC",
				info->fix.smem_start + MMIO_OFFSET);
	}
	if (!s3fb_ddc_needs_mmio(par->chip) || par->mmio)
		if (s3fb_setup_ddc_bus(info) == 0) {
			u8 *edid = fb_ddc_read(&par->ddc_adapter);
			par->ddc_registered = true;
			if (edid) {
				fb_edid_to_monspecs(edid, &info->monspecs);
				kfree(edid);
				if (!info->monspecs.modedb)
					dev_err(info->device, "error getting mode database\n");
				else {
					const struct fb_videomode *m;

					fb_videomode_to_modelist(info->monspecs.modedb,
								 info->monspecs.modedb_len,
								 &info->modelist);
					m = fb_find_best_display(&info->monspecs, &info->modelist);
					if (m) {
						fb_videomode_to_var(&info->var, m);
						/* fill all other info->var's fields */
						if (s3fb_check_var(&info->var, info) == 0)
							found = true;
					}
				}
			}
		}
#endif
	if (!mode_option && !found)
		mode_option = "640x480-8@60";

	/* Prepare startup mode */
	if (mode_option) {
		rc = fb_find_mode(&info->var, info, mode_option,
				   info->monspecs.modedb, info->monspecs.modedb_len,
				   NULL, info->var.bits_per_pixel);
		if (!rc || rc == 4) {
			rc = -EINVAL;
			dev_err(info->device, "mode %s not found\n", mode_option);
			fb_destroy_modedb(info->monspecs.modedb);
			info->monspecs.modedb = NULL;
			goto err_find_mode;
		}
	}

	fb_destroy_modedb(info->monspecs.modedb);
	info->monspecs.modedb = NULL;

	/* maximize virtual vertical size for fast scrolling */
	info->var.yres_virtual = info->fix.smem_len * 8 /
			(info->var.bits_per_pixel * info->var.xres_virtual);
	if (info->var.yres_virtual < info->var.yres) {
		dev_err(info->device, "virtual vertical size smaller than real\n");
		rc = -EINVAL;
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

	fb_info(info, "%s on %s, %d MB RAM, %d MHz MCLK\n",
		info->fix.id, pci_name(dev),
		info->fix.smem_len >> 20, (par->mclk_freq + 500) / 1000);

	if (par->chip == CHIP_UNKNOWN)
		fb_info(info, "unknown chip, CR2D=%x, CR2E=%x, CRT2F=%x, CRT30=%x\n",
			vga_rcrt(par->state.vgabase, 0x2d),
			vga_rcrt(par->state.vgabase, 0x2e),
			vga_rcrt(par->state.vgabase, 0x2f),
			vga_rcrt(par->state.vgabase, 0x30));

	/* Record a reference to the driver data */
	pci_set_drvdata(dev, info);

	if (mtrr)
		par->wc_cookie = arch_phys_wc_add(info->fix.smem_start,
						  info->fix.smem_len);

	return 0;

	/* Error handling */
err_reg_fb:
	fb_dealloc_cmap(&info->cmap);
err_alloc_cmap:
err_find_mode:
#ifdef CONFIG_FB_S3_DDC
	if (par->ddc_registered)
		i2c_del_adapter(&par->ddc_adapter);
	if (par->mmio)
		iounmap(par->mmio);
#endif
	pci_iounmap(dev, info->screen_base);
err_iomap:
	pci_release_regions(dev);
err_request_regions:
/*	pci_disable_device(dev); */
err_enable_device:
	framebuffer_release(info);
	return rc;
}


/* PCI remove */

static void s3_pci_remove(struct pci_dev *dev)
{
	struct fb_info *info = pci_get_drvdata(dev);
	struct s3fb_info __maybe_unused *par;

	if (info) {
		par = info->par;
		arch_phys_wc_del(par->wc_cookie);
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);

#ifdef CONFIG_FB_S3_DDC
		if (par->ddc_registered)
			i2c_del_adapter(&par->ddc_adapter);
		if (par->mmio)
			iounmap(par->mmio);
#endif

		pci_iounmap(dev, info->screen_base);
		pci_release_regions(dev);
/*		pci_disable_device(dev); */

		framebuffer_release(info);
	}
}

/* PCI suspend */

static int __maybe_unused s3_pci_suspend(struct device *dev)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct s3fb_info *par = info->par;

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

static int __maybe_unused s3_pci_resume(struct device *dev)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct s3fb_info *par = info->par;

	dev_info(info->device, "resume\n");

	console_lock();
	mutex_lock(&(par->open_lock));

	if (par->ref_count == 0) {
		mutex_unlock(&(par->open_lock));
		console_unlock();
		return 0;
	}

	s3fb_set_par(info);
	fb_set_suspend(info, 0);

	mutex_unlock(&(par->open_lock));
	console_unlock();

	return 0;
}

static const struct dev_pm_ops s3_pci_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.suspend	= s3_pci_suspend,
	.resume		= s3_pci_resume,
	.freeze		= NULL,
	.thaw		= s3_pci_resume,
	.poweroff	= s3_pci_suspend,
	.restore	= s3_pci_resume,
#endif
};

/* List of boards that we are trying to support */

static const struct pci_device_id s3_devices[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x8810), .driver_data = CHIP_XXX_TRIO},
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x8811), .driver_data = CHIP_XXX_TRIO},
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x8812), .driver_data = CHIP_M65_AURORA64VP},
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x8814), .driver_data = CHIP_767_TRIO64UVP},
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x8901), .driver_data = CHIP_XXX_TRIO64V2_DXGX},
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x8902), .driver_data = CHIP_551_PLATO_PX},

	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x5631), .driver_data = CHIP_325_VIRGE},
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x883D), .driver_data = CHIP_988_VIRGE_VX},
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x8A01), .driver_data = CHIP_XXX_VIRGE_DXGX},
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x8A10), .driver_data = CHIP_357_VIRGE_GX2},
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x8A11), .driver_data = CHIP_359_VIRGE_GX2P},
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x8A12), .driver_data = CHIP_359_VIRGE_GX2P},
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x8A13), .driver_data = CHIP_36X_TRIO3D_1X_2X},
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x8904), .driver_data = CHIP_365_TRIO3D},
	{PCI_DEVICE(PCI_VENDOR_ID_S3, 0x8C01), .driver_data = CHIP_260_VIRGE_MX},

	{0, 0, 0, 0, 0, 0, 0}
};


MODULE_DEVICE_TABLE(pci, s3_devices);

static struct pci_driver s3fb_pci_driver = {
	.name		= "s3fb",
	.id_table	= s3_devices,
	.probe		= s3_pci_probe,
	.remove		= s3_pci_remove,
	.driver.pm	= &s3_pci_pm_ops,
};

/* Parse user specified options */

#ifndef MODULE
static int  __init s3fb_setup(char *options)
{
	char *opt;

	if (!options || !*options)
		return 0;

	while ((opt = strsep(&options, ",")) != NULL) {

		if (!*opt)
			continue;
		else if (!strncmp(opt, "mtrr:", 5))
			mtrr = simple_strtoul(opt + 5, NULL, 0);
		else if (!strncmp(opt, "fasttext:", 9))
			fasttext = simple_strtoul(opt + 9, NULL, 0);
		else
			mode_option = opt;
	}

	return 0;
}
#endif

/* Cleanup */

static void __exit s3fb_cleanup(void)
{
	pr_debug("s3fb: cleaning up\n");
	pci_unregister_driver(&s3fb_pci_driver);
}

/* Driver Initialisation */

static int __init s3fb_init(void)
{

#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("s3fb", &option))
		return -ENODEV;
	s3fb_setup(option);
#endif

	pr_debug("s3fb: initializing\n");
	return pci_register_driver(&s3fb_pci_driver);
}

/* ------------------------------------------------------------------------- */

/* Modularization */

module_init(s3fb_init);
module_exit(s3fb_cleanup);
