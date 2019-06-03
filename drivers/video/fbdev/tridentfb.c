// SPDX-License-Identifier: GPL-2.0-only
/*
 * Frame buffer driver for Trident TGUI, Blade and Image series
 *
 * Copyright 2001, 2002 - Jani Monoses   <jani@iv.ro>
 * Copyright 2009 Krzysztof Helt <krzysztof.h1@wp.pl>
 *
 * CREDITS:(in order of appearance)
 *	skeletonfb.c by Geert Uytterhoeven and other fb code in drivers/video
 *	Special thanks ;) to Mattia Crivellini <tia@mclink.it>
 *	much inspired by the XFree86 4.x Trident driver sources
 *	by Alan Hourihane the FreeVGA project
 *	Francesco Salvestrini <salvestrini@users.sf.net> XP support,
 *	code, suggestions
 * TODO:
 *	timing value tweaking so it looks good on every monitor in every mode
 */

#include <linux/module.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <linux/delay.h>
#include <video/vga.h>
#include <video/trident.h>

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

struct tridentfb_par {
	void __iomem *io_virt;	/* iospace virtual memory address */
	u32 pseudo_pal[16];
	int chip_id;
	int flatpanel;
	void (*init_accel) (struct tridentfb_par *, int, int);
	void (*wait_engine) (struct tridentfb_par *);
	void (*fill_rect)
		(struct tridentfb_par *par, u32, u32, u32, u32, u32, u32);
	void (*copy_rect)
		(struct tridentfb_par *par, u32, u32, u32, u32, u32, u32);
	void (*image_blit)
		(struct tridentfb_par *par, const char*,
		 u32, u32, u32, u32, u32, u32);
	unsigned char eng_oper;	/* engine operation... */
	bool ddc_registered;
	struct i2c_adapter ddc_adapter;
	struct i2c_algo_bit_data ddc_algo;
};

static struct fb_fix_screeninfo tridentfb_fix = {
	.id = "Trident",
	.type = FB_TYPE_PACKED_PIXELS,
	.ypanstep = 1,
	.visual = FB_VISUAL_PSEUDOCOLOR,
	.accel = FB_ACCEL_NONE,
};

/* defaults which are normally overriden by user values */

/* video mode */
static char *mode_option;
static int bpp = 8;

static int noaccel;

static int center;
static int stretch;

static int fp;
static int crt;

static int memsize;
static int memdiff;
static int nativex;

module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option, "Initial video mode e.g. '648x480-8@60'");
module_param_named(mode, mode_option, charp, 0);
MODULE_PARM_DESC(mode, "Initial video mode e.g. '648x480-8@60' (deprecated)");
module_param(bpp, int, 0);
module_param(center, int, 0);
module_param(stretch, int, 0);
module_param(noaccel, int, 0);
module_param(memsize, int, 0);
module_param(memdiff, int, 0);
module_param(nativex, int, 0);
module_param(fp, int, 0);
MODULE_PARM_DESC(fp, "Define if flatpanel is connected");
module_param(crt, int, 0);
MODULE_PARM_DESC(crt, "Define if CRT is connected");

static inline int is_oldclock(int id)
{
	return	(id == TGUI9440) ||
		(id == TGUI9660) ||
		(id == CYBER9320);
}

static inline int is_oldprotect(int id)
{
	return	is_oldclock(id) ||
		(id == PROVIDIA9685) ||
		(id == CYBER9382) ||
		(id == CYBER9385);
}

static inline int is_blade(int id)
{
	return	(id == BLADE3D) ||
		(id == CYBERBLADEE4) ||
		(id == CYBERBLADEi7) ||
		(id == CYBERBLADEi7D) ||
		(id == CYBERBLADEi1) ||
		(id == CYBERBLADEi1D) ||
		(id == CYBERBLADEAi1) ||
		(id == CYBERBLADEAi1D);
}

static inline int is_xp(int id)
{
	return	(id == CYBERBLADEXPAi1) ||
		(id == CYBERBLADEXPm8) ||
		(id == CYBERBLADEXPm16);
}

static inline int is3Dchip(int id)
{
	return	is_blade(id) || is_xp(id) ||
		(id == CYBER9397) || (id == CYBER9397DVD) ||
		(id == CYBER9520) || (id == CYBER9525DVD) ||
		(id == IMAGE975) || (id == IMAGE985);
}

static inline int iscyber(int id)
{
	switch (id) {
	case CYBER9388:
	case CYBER9382:
	case CYBER9385:
	case CYBER9397:
	case CYBER9397DVD:
	case CYBER9520:
	case CYBER9525DVD:
	case CYBERBLADEE4:
	case CYBERBLADEi7D:
	case CYBERBLADEi1:
	case CYBERBLADEi1D:
	case CYBERBLADEAi1:
	case CYBERBLADEAi1D:
	case CYBERBLADEXPAi1:
		return 1;

	case CYBER9320:
	case CYBERBLADEi7:	/* VIA MPV4 integrated version */
	default:
		/* case CYBERBLDAEXPm8:  Strange */
		/* case CYBERBLDAEXPm16: Strange */
		return 0;
	}
}

static inline void t_outb(struct tridentfb_par *p, u8 val, u16 reg)
{
	fb_writeb(val, p->io_virt + reg);
}

static inline u8 t_inb(struct tridentfb_par *p, u16 reg)
{
	return fb_readb(p->io_virt + reg);
}

static inline void writemmr(struct tridentfb_par *par, u16 r, u32 v)
{
	fb_writel(v, par->io_virt + r);
}

static inline u32 readmmr(struct tridentfb_par *par, u16 r)
{
	return fb_readl(par->io_virt + r);
}

#define DDC_SDA_TGUI		BIT(0)
#define DDC_SCL_TGUI		BIT(1)
#define DDC_SCL_DRIVE_TGUI	BIT(2)
#define DDC_SDA_DRIVE_TGUI	BIT(3)
#define DDC_MASK_TGUI		(DDC_SCL_DRIVE_TGUI | DDC_SDA_DRIVE_TGUI)

static void tridentfb_ddc_setscl_tgui(void *data, int val)
{
	struct tridentfb_par *par = data;
	u8 reg = vga_mm_rcrt(par->io_virt, I2C) & DDC_MASK_TGUI;

	if (val)
		reg &= ~DDC_SCL_DRIVE_TGUI; /* disable drive - don't drive hi */
	else
		reg |= DDC_SCL_DRIVE_TGUI; /* drive low */

	vga_mm_wcrt(par->io_virt, I2C, reg);
}

static void tridentfb_ddc_setsda_tgui(void *data, int val)
{
	struct tridentfb_par *par = data;
	u8 reg = vga_mm_rcrt(par->io_virt, I2C) & DDC_MASK_TGUI;

	if (val)
		reg &= ~DDC_SDA_DRIVE_TGUI; /* disable drive - don't drive hi */
	else
		reg |= DDC_SDA_DRIVE_TGUI; /* drive low */

	vga_mm_wcrt(par->io_virt, I2C, reg);
}

static int tridentfb_ddc_getsda_tgui(void *data)
{
	struct tridentfb_par *par = data;

	return !!(vga_mm_rcrt(par->io_virt, I2C) & DDC_SDA_TGUI);
}

#define DDC_SDA_IN	BIT(0)
#define DDC_SCL_OUT	BIT(1)
#define DDC_SDA_OUT	BIT(3)
#define DDC_SCL_IN	BIT(6)
#define DDC_MASK	(DDC_SCL_OUT | DDC_SDA_OUT)

static void tridentfb_ddc_setscl(void *data, int val)
{
	struct tridentfb_par *par = data;
	unsigned char reg;

	reg = vga_mm_rcrt(par->io_virt, I2C) & DDC_MASK;
	if (val)
		reg |= DDC_SCL_OUT;
	else
		reg &= ~DDC_SCL_OUT;
	vga_mm_wcrt(par->io_virt, I2C, reg);
}

static void tridentfb_ddc_setsda(void *data, int val)
{
	struct tridentfb_par *par = data;
	unsigned char reg;

	reg = vga_mm_rcrt(par->io_virt, I2C) & DDC_MASK;
	if (!val)
		reg |= DDC_SDA_OUT;
	else
		reg &= ~DDC_SDA_OUT;
	vga_mm_wcrt(par->io_virt, I2C, reg);
}

static int tridentfb_ddc_getscl(void *data)
{
	struct tridentfb_par *par = data;

	return !!(vga_mm_rcrt(par->io_virt, I2C) & DDC_SCL_IN);
}

static int tridentfb_ddc_getsda(void *data)
{
	struct tridentfb_par *par = data;

	return !!(vga_mm_rcrt(par->io_virt, I2C) & DDC_SDA_IN);
}

static int tridentfb_setup_ddc_bus(struct fb_info *info)
{
	struct tridentfb_par *par = info->par;

	strlcpy(par->ddc_adapter.name, info->fix.id,
		sizeof(par->ddc_adapter.name));
	par->ddc_adapter.owner		= THIS_MODULE;
	par->ddc_adapter.class		= I2C_CLASS_DDC;
	par->ddc_adapter.algo_data	= &par->ddc_algo;
	par->ddc_adapter.dev.parent	= info->device;
	if (is_oldclock(par->chip_id)) { /* not sure if this check is OK */
		par->ddc_algo.setsda	= tridentfb_ddc_setsda_tgui;
		par->ddc_algo.setscl	= tridentfb_ddc_setscl_tgui;
		par->ddc_algo.getsda	= tridentfb_ddc_getsda_tgui;
		/* no getscl */
	} else {
		par->ddc_algo.setsda	= tridentfb_ddc_setsda;
		par->ddc_algo.setscl	= tridentfb_ddc_setscl;
		par->ddc_algo.getsda	= tridentfb_ddc_getsda;
		par->ddc_algo.getscl	= tridentfb_ddc_getscl;
	}
	par->ddc_algo.udelay		= 10;
	par->ddc_algo.timeout		= 20;
	par->ddc_algo.data		= par;

	i2c_set_adapdata(&par->ddc_adapter, par);

	return i2c_bit_add_bus(&par->ddc_adapter);
}

/*
 * Blade specific acceleration.
 */

#define point(x, y) ((y) << 16 | (x))

static void blade_init_accel(struct tridentfb_par *par, int pitch, int bpp)
{
	int v1 = (pitch >> 3) << 20;
	int tmp = bpp == 24 ? 2 : (bpp >> 4);
	int v2 = v1 | (tmp << 29);

	writemmr(par, 0x21C0, v2);
	writemmr(par, 0x21C4, v2);
	writemmr(par, 0x21B8, v2);
	writemmr(par, 0x21BC, v2);
	writemmr(par, 0x21D0, v1);
	writemmr(par, 0x21D4, v1);
	writemmr(par, 0x21C8, v1);
	writemmr(par, 0x21CC, v1);
	writemmr(par, 0x216C, 0);
}

static void blade_wait_engine(struct tridentfb_par *par)
{
	while (readmmr(par, STATUS) & 0xFA800000)
		cpu_relax();
}

static void blade_fill_rect(struct tridentfb_par *par,
			    u32 x, u32 y, u32 w, u32 h, u32 c, u32 rop)
{
	writemmr(par, COLOR, c);
	writemmr(par, ROP, rop ? ROP_X : ROP_S);
	writemmr(par, CMD, 0x20000000 | 1 << 19 | 1 << 4 | 2 << 2);

	writemmr(par, DST1, point(x, y));
	writemmr(par, DST2, point(x + w - 1, y + h - 1));
}

static void blade_image_blit(struct tridentfb_par *par, const char *data,
			     u32 x, u32 y, u32 w, u32 h, u32 c, u32 b)
{
	unsigned size = ((w + 31) >> 5) * h;

	writemmr(par, COLOR, c);
	writemmr(par, BGCOLOR, b);
	writemmr(par, CMD, 0xa0000000 | 3 << 19);

	writemmr(par, DST1, point(x, y));
	writemmr(par, DST2, point(x + w - 1, y + h - 1));

	iowrite32_rep(par->io_virt + 0x10000, data, size);
}

static void blade_copy_rect(struct tridentfb_par *par,
			    u32 x1, u32 y1, u32 x2, u32 y2, u32 w, u32 h)
{
	int direction = 2;
	u32 s1 = point(x1, y1);
	u32 s2 = point(x1 + w - 1, y1 + h - 1);
	u32 d1 = point(x2, y2);
	u32 d2 = point(x2 + w - 1, y2 + h - 1);

	if ((y1 > y2) || ((y1 == y2) && (x1 > x2)))
		direction = 0;

	writemmr(par, ROP, ROP_S);
	writemmr(par, CMD, 0xE0000000 | 1 << 19 | 1 << 4 | 1 << 2 | direction);

	writemmr(par, SRC1, direction ? s2 : s1);
	writemmr(par, SRC2, direction ? s1 : s2);
	writemmr(par, DST1, direction ? d2 : d1);
	writemmr(par, DST2, direction ? d1 : d2);
}

/*
 * BladeXP specific acceleration functions
 */

static void xp_init_accel(struct tridentfb_par *par, int pitch, int bpp)
{
	unsigned char x = bpp == 24 ? 3 : (bpp >> 4);
	int v1 = pitch << (bpp == 24 ? 20 : (18 + x));

	switch (pitch << (bpp >> 3)) {
	case 8192:
	case 512:
		x |= 0x00;
		break;
	case 1024:
		x |= 0x04;
		break;
	case 2048:
		x |= 0x08;
		break;
	case 4096:
		x |= 0x0C;
		break;
	}

	t_outb(par, x, 0x2125);

	par->eng_oper = x | 0x40;

	writemmr(par, 0x2154, v1);
	writemmr(par, 0x2150, v1);
	t_outb(par, 3, 0x2126);
}

static void xp_wait_engine(struct tridentfb_par *par)
{
	int count = 0;
	int timeout = 0;

	while (t_inb(par, STATUS) & 0x80) {
		count++;
		if (count == 10000000) {
			/* Timeout */
			count = 9990000;
			timeout++;
			if (timeout == 8) {
				/* Reset engine */
				t_outb(par, 0x00, STATUS);
				return;
			}
		}
		cpu_relax();
	}
}

static void xp_fill_rect(struct tridentfb_par *par,
			 u32 x, u32 y, u32 w, u32 h, u32 c, u32 rop)
{
	writemmr(par, 0x2127, ROP_P);
	writemmr(par, 0x2158, c);
	writemmr(par, DRAWFL, 0x4000);
	writemmr(par, OLDDIM, point(h, w));
	writemmr(par, OLDDST, point(y, x));
	t_outb(par, 0x01, OLDCMD);
	t_outb(par, par->eng_oper, 0x2125);
}

static void xp_copy_rect(struct tridentfb_par *par,
			 u32 x1, u32 y1, u32 x2, u32 y2, u32 w, u32 h)
{
	u32 x1_tmp, x2_tmp, y1_tmp, y2_tmp;
	int direction = 0x0004;

	if ((x1 < x2) && (y1 == y2)) {
		direction |= 0x0200;
		x1_tmp = x1 + w - 1;
		x2_tmp = x2 + w - 1;
	} else {
		x1_tmp = x1;
		x2_tmp = x2;
	}

	if (y1 < y2) {
		direction |= 0x0100;
		y1_tmp = y1 + h - 1;
		y2_tmp = y2 + h - 1;
	} else {
		y1_tmp = y1;
		y2_tmp = y2;
	}

	writemmr(par, DRAWFL, direction);
	t_outb(par, ROP_S, 0x2127);
	writemmr(par, OLDSRC, point(y1_tmp, x1_tmp));
	writemmr(par, OLDDST, point(y2_tmp, x2_tmp));
	writemmr(par, OLDDIM, point(h, w));
	t_outb(par, 0x01, OLDCMD);
}

/*
 * Image specific acceleration functions
 */
static void image_init_accel(struct tridentfb_par *par, int pitch, int bpp)
{
	int tmp = bpp == 24 ? 2: (bpp >> 4);

	writemmr(par, 0x2120, 0xF0000000);
	writemmr(par, 0x2120, 0x40000000 | tmp);
	writemmr(par, 0x2120, 0x80000000);
	writemmr(par, 0x2144, 0x00000000);
	writemmr(par, 0x2148, 0x00000000);
	writemmr(par, 0x2150, 0x00000000);
	writemmr(par, 0x2154, 0x00000000);
	writemmr(par, 0x2120, 0x60000000 | (pitch << 16) | pitch);
	writemmr(par, 0x216C, 0x00000000);
	writemmr(par, 0x2170, 0x00000000);
	writemmr(par, 0x217C, 0x00000000);
	writemmr(par, 0x2120, 0x10000000);
	writemmr(par, 0x2130, (2047 << 16) | 2047);
}

static void image_wait_engine(struct tridentfb_par *par)
{
	while (readmmr(par, 0x2164) & 0xF0000000)
		cpu_relax();
}

static void image_fill_rect(struct tridentfb_par *par,
			    u32 x, u32 y, u32 w, u32 h, u32 c, u32 rop)
{
	writemmr(par, 0x2120, 0x80000000);
	writemmr(par, 0x2120, 0x90000000 | ROP_S);

	writemmr(par, 0x2144, c);

	writemmr(par, DST1, point(x, y));
	writemmr(par, DST2, point(x + w - 1, y + h - 1));

	writemmr(par, 0x2124, 0x80000000 | 3 << 22 | 1 << 10 | 1 << 9);
}

static void image_copy_rect(struct tridentfb_par *par,
			    u32 x1, u32 y1, u32 x2, u32 y2, u32 w, u32 h)
{
	int direction = 0x4;
	u32 s1 = point(x1, y1);
	u32 s2 = point(x1 + w - 1, y1 + h - 1);
	u32 d1 = point(x2, y2);
	u32 d2 = point(x2 + w - 1, y2 + h - 1);

	if ((y1 > y2) || ((y1 == y2) && (x1 > x2)))
		direction = 0;

	writemmr(par, 0x2120, 0x80000000);
	writemmr(par, 0x2120, 0x90000000 | ROP_S);

	writemmr(par, SRC1, direction ? s2 : s1);
	writemmr(par, SRC2, direction ? s1 : s2);
	writemmr(par, DST1, direction ? d2 : d1);
	writemmr(par, DST2, direction ? d1 : d2);
	writemmr(par, 0x2124,
		 0x80000000 | 1 << 22 | 1 << 10 | 1 << 7 | direction);
}

/*
 * TGUI 9440/96XX acceleration
 */

static void tgui_init_accel(struct tridentfb_par *par, int pitch, int bpp)
{
	unsigned char x = bpp == 24 ? 3 : (bpp >> 4);

	/* disable clipping */
	writemmr(par, 0x2148, 0);
	writemmr(par, 0x214C, point(4095, 2047));

	switch ((pitch * bpp) / 8) {
	case 8192:
	case 512:
		x |= 0x00;
		break;
	case 1024:
		x |= 0x04;
		break;
	case 2048:
		x |= 0x08;
		break;
	case 4096:
		x |= 0x0C;
		break;
	}

	fb_writew(x, par->io_virt + 0x2122);
}

static void tgui_fill_rect(struct tridentfb_par *par,
			   u32 x, u32 y, u32 w, u32 h, u32 c, u32 rop)
{
	t_outb(par, ROP_P, 0x2127);
	writemmr(par, OLDCLR, c);
	writemmr(par, DRAWFL, 0x4020);
	writemmr(par, OLDDIM, point(w - 1, h - 1));
	writemmr(par, OLDDST, point(x, y));
	t_outb(par, 1, OLDCMD);
}

static void tgui_copy_rect(struct tridentfb_par *par,
			   u32 x1, u32 y1, u32 x2, u32 y2, u32 w, u32 h)
{
	int flags = 0;
	u16 x1_tmp, x2_tmp, y1_tmp, y2_tmp;

	if ((x1 < x2) && (y1 == y2)) {
		flags |= 0x0200;
		x1_tmp = x1 + w - 1;
		x2_tmp = x2 + w - 1;
	} else {
		x1_tmp = x1;
		x2_tmp = x2;
	}

	if (y1 < y2) {
		flags |= 0x0100;
		y1_tmp = y1 + h - 1;
		y2_tmp = y2 + h - 1;
	} else {
		y1_tmp = y1;
		y2_tmp = y2;
	}

	writemmr(par, DRAWFL, 0x4 | flags);
	t_outb(par, ROP_S, 0x2127);
	writemmr(par, OLDSRC, point(x1_tmp, y1_tmp));
	writemmr(par, OLDDST, point(x2_tmp, y2_tmp));
	writemmr(par, OLDDIM, point(w - 1, h - 1));
	t_outb(par, 1, OLDCMD);
}

/*
 * Accel functions called by the upper layers
 */
static void tridentfb_fillrect(struct fb_info *info,
			       const struct fb_fillrect *fr)
{
	struct tridentfb_par *par = info->par;
	int col;

	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_fillrect(info, fr);
		return;
	}
	if (info->var.bits_per_pixel == 8) {
		col = fr->color;
		col |= col << 8;
		col |= col << 16;
	} else
		col = ((u32 *)(info->pseudo_palette))[fr->color];

	par->wait_engine(par);
	par->fill_rect(par, fr->dx, fr->dy, fr->width,
		       fr->height, col, fr->rop);
}

static void tridentfb_imageblit(struct fb_info *info,
				const struct fb_image *img)
{
	struct tridentfb_par *par = info->par;
	int col, bgcol;

	if ((info->flags & FBINFO_HWACCEL_DISABLED) || img->depth != 1) {
		cfb_imageblit(info, img);
		return;
	}
	if (info->var.bits_per_pixel == 8) {
		col = img->fg_color;
		col |= col << 8;
		col |= col << 16;
		bgcol = img->bg_color;
		bgcol |= bgcol << 8;
		bgcol |= bgcol << 16;
	} else {
		col = ((u32 *)(info->pseudo_palette))[img->fg_color];
		bgcol = ((u32 *)(info->pseudo_palette))[img->bg_color];
	}

	par->wait_engine(par);
	if (par->image_blit)
		par->image_blit(par, img->data, img->dx, img->dy,
				img->width, img->height, col, bgcol);
	else
		cfb_imageblit(info, img);
}

static void tridentfb_copyarea(struct fb_info *info,
			       const struct fb_copyarea *ca)
{
	struct tridentfb_par *par = info->par;

	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_copyarea(info, ca);
		return;
	}
	par->wait_engine(par);
	par->copy_rect(par, ca->sx, ca->sy, ca->dx, ca->dy,
		       ca->width, ca->height);
}

static int tridentfb_sync(struct fb_info *info)
{
	struct tridentfb_par *par = info->par;

	if (!(info->flags & FBINFO_HWACCEL_DISABLED))
		par->wait_engine(par);
	return 0;
}

/*
 * Hardware access functions
 */

static inline unsigned char read3X4(struct tridentfb_par *par, int reg)
{
	return vga_mm_rcrt(par->io_virt, reg);
}

static inline void write3X4(struct tridentfb_par *par, int reg,
			    unsigned char val)
{
	vga_mm_wcrt(par->io_virt, reg, val);
}

static inline unsigned char read3CE(struct tridentfb_par *par,
				    unsigned char reg)
{
	return vga_mm_rgfx(par->io_virt, reg);
}

static inline void writeAttr(struct tridentfb_par *par, int reg,
			     unsigned char val)
{
	fb_readb(par->io_virt + VGA_IS1_RC);	/* flip-flop to index */
	vga_mm_wattr(par->io_virt, reg, val);
}

static inline void write3CE(struct tridentfb_par *par, int reg,
			    unsigned char val)
{
	vga_mm_wgfx(par->io_virt, reg, val);
}

static void enable_mmio(struct tridentfb_par *par)
{
	/* Goto New Mode */
	vga_io_rseq(0x0B);

	/* Unprotect registers */
	vga_io_wseq(NewMode1, 0x80);
	if (!is_oldprotect(par->chip_id))
		vga_io_wseq(Protection, 0x92);

	/* Enable MMIO */
	outb(PCIReg, 0x3D4);
	outb(inb(0x3D5) | 0x01, 0x3D5);
}

static void disable_mmio(struct tridentfb_par *par)
{
	/* Goto New Mode */
	vga_mm_rseq(par->io_virt, 0x0B);

	/* Unprotect registers */
	vga_mm_wseq(par->io_virt, NewMode1, 0x80);
	if (!is_oldprotect(par->chip_id))
		vga_mm_wseq(par->io_virt, Protection, 0x92);

	/* Disable MMIO */
	t_outb(par, PCIReg, 0x3D4);
	t_outb(par, t_inb(par, 0x3D5) & ~0x01, 0x3D5);
}

static inline void crtc_unlock(struct tridentfb_par *par)
{
	write3X4(par, VGA_CRTC_V_SYNC_END,
		 read3X4(par, VGA_CRTC_V_SYNC_END) & 0x7F);
}

/*  Return flat panel's maximum x resolution */
static int get_nativex(struct tridentfb_par *par)
{
	int x, y, tmp;

	if (nativex)
		return nativex;

	tmp = (read3CE(par, VertStretch) >> 4) & 3;

	switch (tmp) {
	case 0:
		x = 1280; y = 1024;
		break;
	case 2:
		x = 1024; y = 768;
		break;
	case 3:
		x = 800; y = 600;
		break;
	case 1:
	default:
		x = 640;  y = 480;
		break;
	}

	output("%dx%d flat panel found\n", x, y);
	return x;
}

/* Set pitch */
static inline void set_lwidth(struct tridentfb_par *par, int width)
{
	write3X4(par, VGA_CRTC_OFFSET, width & 0xFF);
	/* chips older than TGUI9660 have only 1 width bit in AddColReg */
	/* touching the other one breaks I2C/DDC */
	if (par->chip_id == TGUI9440 || par->chip_id == CYBER9320)
		write3X4(par, AddColReg,
		     (read3X4(par, AddColReg) & 0xEF) | ((width & 0x100) >> 4));
	else
		write3X4(par, AddColReg,
		     (read3X4(par, AddColReg) & 0xCF) | ((width & 0x300) >> 4));
}

/* For resolutions smaller than FP resolution stretch */
static void screen_stretch(struct tridentfb_par *par)
{
	if (par->chip_id != CYBERBLADEXPAi1)
		write3CE(par, BiosReg, 0);
	else
		write3CE(par, BiosReg, 8);
	write3CE(par, VertStretch, (read3CE(par, VertStretch) & 0x7C) | 1);
	write3CE(par, HorStretch, (read3CE(par, HorStretch) & 0x7C) | 1);
}

/* For resolutions smaller than FP resolution center */
static inline void screen_center(struct tridentfb_par *par)
{
	write3CE(par, VertStretch, (read3CE(par, VertStretch) & 0x7C) | 0x80);
	write3CE(par, HorStretch, (read3CE(par, HorStretch) & 0x7C) | 0x80);
}

/* Address of first shown pixel in display memory */
static void set_screen_start(struct tridentfb_par *par, int base)
{
	u8 tmp;
	write3X4(par, VGA_CRTC_START_LO, base & 0xFF);
	write3X4(par, VGA_CRTC_START_HI, (base & 0xFF00) >> 8);
	tmp = read3X4(par, CRTCModuleTest) & 0xDF;
	write3X4(par, CRTCModuleTest, tmp | ((base & 0x10000) >> 11));
	tmp = read3X4(par, CRTHiOrd) & 0xF8;
	write3X4(par, CRTHiOrd, tmp | ((base & 0xE0000) >> 17));
}

/* Set dotclock frequency */
static void set_vclk(struct tridentfb_par *par, unsigned long freq)
{
	int m, n, k;
	unsigned long fi, d, di;
	unsigned char best_m = 0, best_n = 0, best_k = 0;
	unsigned char hi, lo;
	unsigned char shift = !is_oldclock(par->chip_id) ? 2 : 1;

	d = 20000;
	for (k = shift; k >= 0; k--)
		for (m = 1; m < 32; m++) {
			n = ((m + 2) << shift) - 8;
			for (n = (n < 0 ? 0 : n); n < 122; n++) {
				fi = ((14318l * (n + 8)) / (m + 2)) >> k;
				di = abs(fi - freq);
				if (di < d || (di == d && k == best_k)) {
					d = di;
					best_n = n;
					best_m = m;
					best_k = k;
				}
				if (fi > freq)
					break;
			}
		}

	if (is_oldclock(par->chip_id)) {
		lo = best_n | (best_m << 7);
		hi = (best_m >> 1) | (best_k << 4);
	} else {
		lo = best_n;
		hi = best_m | (best_k << 6);
	}

	if (is3Dchip(par->chip_id)) {
		vga_mm_wseq(par->io_virt, ClockHigh, hi);
		vga_mm_wseq(par->io_virt, ClockLow, lo);
	} else {
		t_outb(par, lo, 0x43C8);
		t_outb(par, hi, 0x43C9);
	}
	debug("VCLK = %X %X\n", hi, lo);
}

/* Set number of lines for flat panels*/
static void set_number_of_lines(struct tridentfb_par *par, int lines)
{
	int tmp = read3CE(par, CyberEnhance) & 0x8F;
	if (lines > 1024)
		tmp |= 0x50;
	else if (lines > 768)
		tmp |= 0x30;
	else if (lines > 600)
		tmp |= 0x20;
	else if (lines > 480)
		tmp |= 0x10;
	write3CE(par, CyberEnhance, tmp);
}

/*
 * If we see that FP is active we assume we have one.
 * Otherwise we have a CRT display. User can override.
 */
static int is_flatpanel(struct tridentfb_par *par)
{
	if (fp)
		return 1;
	if (crt || !iscyber(par->chip_id))
		return 0;
	return (read3CE(par, FPConfig) & 0x10) ? 1 : 0;
}

/* Try detecting the video memory size */
static unsigned int get_memsize(struct tridentfb_par *par)
{
	unsigned char tmp, tmp2;
	unsigned int k;

	/* If memory size provided by user */
	if (memsize)
		k = memsize * Kb;
	else
		switch (par->chip_id) {
		case CYBER9525DVD:
			k = 2560 * Kb;
			break;
		default:
			tmp = read3X4(par, SPR) & 0x0F;
			switch (tmp) {

			case 0x01:
				k = 512 * Kb;
				break;
			case 0x02:
				k = 6 * Mb;	/* XP */
				break;
			case 0x03:
				k = 1 * Mb;
				break;
			case 0x04:
				k = 8 * Mb;
				break;
			case 0x06:
				k = 10 * Mb;	/* XP */
				break;
			case 0x07:
				k = 2 * Mb;
				break;
			case 0x08:
				k = 12 * Mb;	/* XP */
				break;
			case 0x0A:
				k = 14 * Mb;	/* XP */
				break;
			case 0x0C:
				k = 16 * Mb;	/* XP */
				break;
			case 0x0E:		/* XP */

				tmp2 = vga_mm_rseq(par->io_virt, 0xC1);
				switch (tmp2) {
				case 0x00:
					k = 20 * Mb;
					break;
				case 0x01:
					k = 24 * Mb;
					break;
				case 0x10:
					k = 28 * Mb;
					break;
				case 0x11:
					k = 32 * Mb;
					break;
				default:
					k = 1 * Mb;
					break;
				}
				break;

			case 0x0F:
				k = 4 * Mb;
				break;
			default:
				k = 1 * Mb;
				break;
			}
		}

	k -= memdiff * Kb;
	output("framebuffer size = %d Kb\n", k / Kb);
	return k;
}

/* See if we can handle the video mode described in var */
static int tridentfb_check_var(struct fb_var_screeninfo *var,
			       struct fb_info *info)
{
	struct tridentfb_par *par = info->par;
	int bpp = var->bits_per_pixel;
	int line_length;
	int ramdac = 230000; /* 230MHz for most 3D chips */
	debug("enter\n");

	/* check color depth */
	if (bpp == 24)
		bpp = var->bits_per_pixel = 32;
	if (bpp != 8 && bpp != 16 && bpp != 32)
		return -EINVAL;
	if (par->chip_id == TGUI9440 && bpp == 32)
		return -EINVAL;
	/* check whether resolution fits on panel and in memory */
	if (par->flatpanel && nativex && var->xres > nativex)
		return -EINVAL;
	/* various resolution checks */
	var->xres = (var->xres + 7) & ~0x7;
	if (var->xres > var->xres_virtual)
		var->xres_virtual = var->xres;
	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;
	if (var->xres_virtual > 4095 || var->yres > 2048)
		return -EINVAL;
	/* prevent from position overflow for acceleration */
	if (var->yres_virtual > 0xffff)
		return -EINVAL;
	line_length = var->xres_virtual * bpp / 8;

	if (!is3Dchip(par->chip_id) &&
	    !(info->flags & FBINFO_HWACCEL_DISABLED)) {
		/* acceleration requires line length to be power of 2 */
		if (line_length <= 512)
			var->xres_virtual = 512 * 8 / bpp;
		else if (line_length <= 1024)
			var->xres_virtual = 1024 * 8 / bpp;
		else if (line_length <= 2048)
			var->xres_virtual = 2048 * 8 / bpp;
		else if (line_length <= 4096)
			var->xres_virtual = 4096 * 8 / bpp;
		else if (line_length <= 8192)
			var->xres_virtual = 8192 * 8 / bpp;
		else
			return -EINVAL;

		line_length = var->xres_virtual * bpp / 8;
	}

	/* datasheet specifies how to set panning only up to 4 MB */
	if (line_length * (var->yres_virtual - var->yres) > (4 << 20))
		var->yres_virtual = ((4 << 20) / line_length) + var->yres;

	if (line_length * var->yres_virtual > info->fix.smem_len)
		return -EINVAL;

	switch (bpp) {
	case 8:
		var->red.offset = 0;
		var->red.length = 8;
		var->green = var->red;
		var->blue = var->red;
		break;
	case 16:
		var->red.offset = 11;
		var->green.offset = 5;
		var->blue.offset = 0;
		var->red.length = 5;
		var->green.length = 6;
		var->blue.length = 5;
		break;
	case 32:
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		break;
	default:
		return -EINVAL;
	}

	if (is_xp(par->chip_id))
		ramdac = 350000;

	switch (par->chip_id) {
	case TGUI9440:
		ramdac = (bpp >= 16) ? 45000 : 90000;
		break;
	case CYBER9320:
	case TGUI9660:
		ramdac = 135000;
		break;
	case PROVIDIA9685:
	case CYBER9388:
	case CYBER9382:
	case CYBER9385:
		ramdac = 170000;
		break;
	}

	/* The clock is doubled for 32 bpp */
	if (bpp == 32)
		ramdac /= 2;

	if (PICOS2KHZ(var->pixclock) > ramdac)
		return -EINVAL;

	debug("exit\n");

	return 0;

}

/* Pan the display */
static int tridentfb_pan_display(struct fb_var_screeninfo *var,
				 struct fb_info *info)
{
	struct tridentfb_par *par = info->par;
	unsigned int offset;

	debug("enter\n");
	offset = (var->xoffset + (var->yoffset * info->var.xres_virtual))
		* info->var.bits_per_pixel / 32;
	set_screen_start(par, offset);
	debug("exit\n");
	return 0;
}

static inline void shadowmode_on(struct tridentfb_par *par)
{
	write3CE(par, CyberControl, read3CE(par, CyberControl) | 0x81);
}

static inline void shadowmode_off(struct tridentfb_par *par)
{
	write3CE(par, CyberControl, read3CE(par, CyberControl) & 0x7E);
}

/* Set the hardware to the requested video mode */
static int tridentfb_set_par(struct fb_info *info)
{
	struct tridentfb_par *par = info->par;
	u32 htotal, hdispend, hsyncstart, hsyncend, hblankstart, hblankend;
	u32 vtotal, vdispend, vsyncstart, vsyncend, vblankstart, vblankend;
	struct fb_var_screeninfo *var = &info->var;
	int bpp = var->bits_per_pixel;
	unsigned char tmp;
	unsigned long vclk;

	debug("enter\n");
	hdispend = var->xres / 8 - 1;
	hsyncstart = (var->xres + var->right_margin) / 8;
	hsyncend = (var->xres + var->right_margin + var->hsync_len) / 8;
	htotal = (var->xres + var->left_margin + var->right_margin +
		  var->hsync_len) / 8 - 5;
	hblankstart = hdispend + 1;
	hblankend = htotal + 3;

	vdispend = var->yres - 1;
	vsyncstart = var->yres + var->lower_margin;
	vsyncend = vsyncstart + var->vsync_len;
	vtotal = var->upper_margin + vsyncend - 2;
	vblankstart = vdispend + 1;
	vblankend = vtotal;

	if (info->var.vmode & FB_VMODE_INTERLACED) {
		vtotal /= 2;
		vdispend /= 2;
		vsyncstart /= 2;
		vsyncend /= 2;
		vblankstart /= 2;
		vblankend /= 2;
	}

	enable_mmio(par);
	crtc_unlock(par);
	write3CE(par, CyberControl, 8);
	tmp = 0xEB;
	if (var->sync & FB_SYNC_HOR_HIGH_ACT)
		tmp &= ~0x40;
	if (var->sync & FB_SYNC_VERT_HIGH_ACT)
		tmp &= ~0x80;

	if (par->flatpanel && var->xres < nativex) {
		/*
		 * on flat panels with native size larger
		 * than requested resolution decide whether
		 * we stretch or center
		 */
		t_outb(par, tmp | 0xC0, VGA_MIS_W);

		shadowmode_on(par);

		if (center)
			screen_center(par);
		else if (stretch)
			screen_stretch(par);

	} else {
		t_outb(par, tmp, VGA_MIS_W);
		write3CE(par, CyberControl, 8);
	}

	/* vertical timing values */
	write3X4(par, VGA_CRTC_V_TOTAL, vtotal & 0xFF);
	write3X4(par, VGA_CRTC_V_DISP_END, vdispend & 0xFF);
	write3X4(par, VGA_CRTC_V_SYNC_START, vsyncstart & 0xFF);
	write3X4(par, VGA_CRTC_V_SYNC_END, (vsyncend & 0x0F));
	write3X4(par, VGA_CRTC_V_BLANK_START, vblankstart & 0xFF);
	write3X4(par, VGA_CRTC_V_BLANK_END, vblankend & 0xFF);

	/* horizontal timing values */
	write3X4(par, VGA_CRTC_H_TOTAL, htotal & 0xFF);
	write3X4(par, VGA_CRTC_H_DISP, hdispend & 0xFF);
	write3X4(par, VGA_CRTC_H_SYNC_START, hsyncstart & 0xFF);
	write3X4(par, VGA_CRTC_H_SYNC_END,
		 (hsyncend & 0x1F) | ((hblankend & 0x20) << 2));
	write3X4(par, VGA_CRTC_H_BLANK_START, hblankstart & 0xFF);
	write3X4(par, VGA_CRTC_H_BLANK_END, hblankend & 0x1F);

	/* higher bits of vertical timing values */
	tmp = 0x10;
	if (vtotal & 0x100) tmp |= 0x01;
	if (vdispend & 0x100) tmp |= 0x02;
	if (vsyncstart & 0x100) tmp |= 0x04;
	if (vblankstart & 0x100) tmp |= 0x08;

	if (vtotal & 0x200) tmp |= 0x20;
	if (vdispend & 0x200) tmp |= 0x40;
	if (vsyncstart & 0x200) tmp |= 0x80;
	write3X4(par, VGA_CRTC_OVERFLOW, tmp);

	tmp = read3X4(par, CRTHiOrd) & 0x07;
	tmp |= 0x08;	/* line compare bit 10 */
	if (vtotal & 0x400) tmp |= 0x80;
	if (vblankstart & 0x400) tmp |= 0x40;
	if (vsyncstart & 0x400) tmp |= 0x20;
	if (vdispend & 0x400) tmp |= 0x10;
	write3X4(par, CRTHiOrd, tmp);

	tmp = (htotal >> 8) & 0x01;
	tmp |= (hdispend >> 7) & 0x02;
	tmp |= (hsyncstart >> 5) & 0x08;
	tmp |= (hblankstart >> 4) & 0x10;
	write3X4(par, HorizOverflow, tmp);

	tmp = 0x40;
	if (vblankstart & 0x200) tmp |= 0x20;
//FIXME	if (info->var.vmode & FB_VMODE_DOUBLE) tmp |= 0x80;  /* double scan for 200 line modes */
	write3X4(par, VGA_CRTC_MAX_SCAN, tmp);

	write3X4(par, VGA_CRTC_LINE_COMPARE, 0xFF);
	write3X4(par, VGA_CRTC_PRESET_ROW, 0);
	write3X4(par, VGA_CRTC_MODE, 0xC3);

	write3X4(par, LinearAddReg, 0x20);	/* enable linear addressing */

	tmp = (info->var.vmode & FB_VMODE_INTERLACED) ? 0x84 : 0x80;
	/* enable access extended memory */
	write3X4(par, CRTCModuleTest, tmp);
	tmp = read3CE(par, MiscIntContReg) & ~0x4;
	if (info->var.vmode & FB_VMODE_INTERLACED)
		tmp |= 0x4;
	write3CE(par, MiscIntContReg, tmp);

	/* enable GE for text acceleration */
	write3X4(par, GraphEngReg, 0x80);

	switch (bpp) {
	case 8:
		tmp = 0x00;
		break;
	case 16:
		tmp = 0x05;
		break;
	case 24:
		tmp = 0x29;
		break;
	case 32:
		tmp = 0x09;
		break;
	}

	write3X4(par, PixelBusReg, tmp);

	tmp = read3X4(par, DRAMControl);
	if (!is_oldprotect(par->chip_id))
		tmp |= 0x10;
	if (iscyber(par->chip_id))
		tmp |= 0x20;
	write3X4(par, DRAMControl, tmp);	/* both IO, linear enable */

	write3X4(par, InterfaceSel, read3X4(par, InterfaceSel) | 0x40);
	if (!is_xp(par->chip_id))
		write3X4(par, Performance, read3X4(par, Performance) | 0x10);
	/* MMIO & PCI read and write burst enable */
	if (par->chip_id != TGUI9440 && par->chip_id != IMAGE975)
		write3X4(par, PCIReg, read3X4(par, PCIReg) | 0x06);

	vga_mm_wseq(par->io_virt, 0, 3);
	vga_mm_wseq(par->io_virt, 1, 1); /* set char clock 8 dots wide */
	/* enable 4 maps because needed in chain4 mode */
	vga_mm_wseq(par->io_virt, 2, 0x0F);
	vga_mm_wseq(par->io_virt, 3, 0);
	vga_mm_wseq(par->io_virt, 4, 0x0E); /* memory mode enable bitmaps ?? */

	/* convert from picoseconds to kHz */
	vclk = PICOS2KHZ(info->var.pixclock);

	/* divide clock by 2 if 32bpp chain4 mode display and CPU path */
	tmp = read3CE(par, MiscExtFunc) & 0xF0;
	if (bpp == 32 || (par->chip_id == TGUI9440 && bpp == 16)) {
		tmp |= 8;
		vclk *= 2;
	}
	set_vclk(par, vclk);
	write3CE(par, MiscExtFunc, tmp | 0x12);
	write3CE(par, 0x5, 0x40);	/* no CGA compat, allow 256 col */
	write3CE(par, 0x6, 0x05);	/* graphics mode */
	write3CE(par, 0x7, 0x0F);	/* planes? */

	/* graphics mode and support 256 color modes */
	writeAttr(par, 0x10, 0x41);
	writeAttr(par, 0x12, 0x0F);	/* planes */
	writeAttr(par, 0x13, 0);	/* horizontal pel panning */

	/* colors */
	for (tmp = 0; tmp < 0x10; tmp++)
		writeAttr(par, tmp, tmp);
	fb_readb(par->io_virt + VGA_IS1_RC);	/* flip-flop to index */
	t_outb(par, 0x20, VGA_ATT_W);		/* enable attr */

	switch (bpp) {
	case 8:
		tmp = 0;
		break;
	case 16:
		tmp = 0x30;
		break;
	case 24:
	case 32:
		tmp = 0xD0;
		break;
	}

	t_inb(par, VGA_PEL_IW);
	t_inb(par, VGA_PEL_MSK);
	t_inb(par, VGA_PEL_MSK);
	t_inb(par, VGA_PEL_MSK);
	t_inb(par, VGA_PEL_MSK);
	t_outb(par, tmp, VGA_PEL_MSK);
	t_inb(par, VGA_PEL_IW);

	if (par->flatpanel)
		set_number_of_lines(par, info->var.yres);
	info->fix.line_length = info->var.xres_virtual * bpp / 8;
	set_lwidth(par, info->fix.line_length / 8);

	if (!(info->flags & FBINFO_HWACCEL_DISABLED))
		par->init_accel(par, info->var.xres_virtual, bpp);

	info->fix.visual = (bpp == 8) ? FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;
	info->cmap.len = (bpp == 8) ? 256 : 16;
	debug("exit\n");
	return 0;
}

/* Set one color register */
static int tridentfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			       unsigned blue, unsigned transp,
			       struct fb_info *info)
{
	int bpp = info->var.bits_per_pixel;
	struct tridentfb_par *par = info->par;

	if (regno >= info->cmap.len)
		return 1;

	if (bpp == 8) {
		t_outb(par, 0xFF, VGA_PEL_MSK);
		t_outb(par, regno, VGA_PEL_IW);

		t_outb(par, red >> 10, VGA_PEL_D);
		t_outb(par, green >> 10, VGA_PEL_D);
		t_outb(par, blue >> 10, VGA_PEL_D);

	} else if (regno < 16) {
		if (bpp == 16) {	/* RGB 565 */
			u32 col;

			col = (red & 0xF800) | ((green & 0xFC00) >> 5) |
				((blue & 0xF800) >> 11);
			col |= col << 16;
			((u32 *)(info->pseudo_palette))[regno] = col;
		} else if (bpp == 32)		/* ARGB 8888 */
			((u32 *)info->pseudo_palette)[regno] =
				((transp & 0xFF00) << 16)	|
				((red & 0xFF00) << 8)		|
				((green & 0xFF00))		|
				((blue & 0xFF00) >> 8);
	}

	return 0;
}

/* Try blanking the screen. For flat panels it does nothing */
static int tridentfb_blank(int blank_mode, struct fb_info *info)
{
	unsigned char PMCont, DPMSCont;
	struct tridentfb_par *par = info->par;

	debug("enter\n");
	if (par->flatpanel)
		return 0;
	t_outb(par, 0x04, 0x83C8); /* Read DPMS Control */
	PMCont = t_inb(par, 0x83C6) & 0xFC;
	DPMSCont = read3CE(par, PowerStatus) & 0xFC;
	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		/* Screen: On, HSync: On, VSync: On */
	case FB_BLANK_NORMAL:
		/* Screen: Off, HSync: On, VSync: On */
		PMCont |= 0x03;
		DPMSCont |= 0x00;
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		/* Screen: Off, HSync: Off, VSync: On */
		PMCont |= 0x02;
		DPMSCont |= 0x01;
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		/* Screen: Off, HSync: On, VSync: Off */
		PMCont |= 0x02;
		DPMSCont |= 0x02;
		break;
	case FB_BLANK_POWERDOWN:
		/* Screen: Off, HSync: Off, VSync: Off */
		PMCont |= 0x00;
		DPMSCont |= 0x03;
		break;
	}

	write3CE(par, PowerStatus, DPMSCont);
	t_outb(par, 4, 0x83C8);
	t_outb(par, PMCont, 0x83C6);

	debug("exit\n");

	/* let fbcon do a softblank for us */
	return (blank_mode == FB_BLANK_NORMAL) ? 1 : 0;
}

static struct fb_ops tridentfb_ops = {
	.owner = THIS_MODULE,
	.fb_setcolreg = tridentfb_setcolreg,
	.fb_pan_display = tridentfb_pan_display,
	.fb_blank = tridentfb_blank,
	.fb_check_var = tridentfb_check_var,
	.fb_set_par = tridentfb_set_par,
	.fb_fillrect = tridentfb_fillrect,
	.fb_copyarea = tridentfb_copyarea,
	.fb_imageblit = tridentfb_imageblit,
	.fb_sync = tridentfb_sync,
};

static int trident_pci_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	int err;
	unsigned char revision;
	struct fb_info *info;
	struct tridentfb_par *default_par;
	int chip3D;
	int chip_id;
	bool found = false;

	err = pci_enable_device(dev);
	if (err)
		return err;

	info = framebuffer_alloc(sizeof(struct tridentfb_par), &dev->dev);
	if (!info)
		return -ENOMEM;
	default_par = info->par;

	chip_id = id->device;

	/* If PCI id is 0x9660 then further detect chip type */

	if (chip_id == TGUI9660) {
		revision = vga_io_rseq(RevisionID);

		switch (revision) {
		case 0x21:
			chip_id = PROVIDIA9685;
			break;
		case 0x22:
		case 0x23:
			chip_id = CYBER9397;
			break;
		case 0x2A:
			chip_id = CYBER9397DVD;
			break;
		case 0x30:
		case 0x33:
		case 0x34:
		case 0x35:
		case 0x38:
		case 0x3A:
		case 0xB3:
			chip_id = CYBER9385;
			break;
		case 0x40 ... 0x43:
			chip_id = CYBER9382;
			break;
		case 0x4A:
			chip_id = CYBER9388;
			break;
		default:
			break;
		}
	}

	chip3D = is3Dchip(chip_id);

	if (is_xp(chip_id)) {
		default_par->init_accel = xp_init_accel;
		default_par->wait_engine = xp_wait_engine;
		default_par->fill_rect = xp_fill_rect;
		default_par->copy_rect = xp_copy_rect;
		tridentfb_fix.accel = FB_ACCEL_TRIDENT_BLADEXP;
	} else if (is_blade(chip_id)) {
		default_par->init_accel = blade_init_accel;
		default_par->wait_engine = blade_wait_engine;
		default_par->fill_rect = blade_fill_rect;
		default_par->copy_rect = blade_copy_rect;
		default_par->image_blit = blade_image_blit;
		tridentfb_fix.accel = FB_ACCEL_TRIDENT_BLADE3D;
	} else if (chip3D) {			/* 3DImage family left */
		default_par->init_accel = image_init_accel;
		default_par->wait_engine = image_wait_engine;
		default_par->fill_rect = image_fill_rect;
		default_par->copy_rect = image_copy_rect;
		tridentfb_fix.accel = FB_ACCEL_TRIDENT_3DIMAGE;
	} else { 				/* TGUI 9440/96XX family */
		default_par->init_accel = tgui_init_accel;
		default_par->wait_engine = xp_wait_engine;
		default_par->fill_rect = tgui_fill_rect;
		default_par->copy_rect = tgui_copy_rect;
		tridentfb_fix.accel = FB_ACCEL_TRIDENT_TGUI;
	}

	default_par->chip_id = chip_id;

	/* setup MMIO region */
	tridentfb_fix.mmio_start = pci_resource_start(dev, 1);
	tridentfb_fix.mmio_len = pci_resource_len(dev, 1);

	if (!request_mem_region(tridentfb_fix.mmio_start,
				tridentfb_fix.mmio_len, "tridentfb")) {
		debug("request_region failed!\n");
		framebuffer_release(info);
		return -1;
	}

	default_par->io_virt = ioremap_nocache(tridentfb_fix.mmio_start,
					       tridentfb_fix.mmio_len);

	if (!default_par->io_virt) {
		debug("ioremap failed\n");
		err = -1;
		goto out_unmap1;
	}

	enable_mmio(default_par);

	/* setup framebuffer memory */
	tridentfb_fix.smem_start = pci_resource_start(dev, 0);
	tridentfb_fix.smem_len = get_memsize(default_par);

	if (!request_mem_region(tridentfb_fix.smem_start,
				tridentfb_fix.smem_len, "tridentfb")) {
		debug("request_mem_region failed!\n");
		disable_mmio(info->par);
		err = -1;
		goto out_unmap1;
	}

	info->screen_base = ioremap_nocache(tridentfb_fix.smem_start,
					    tridentfb_fix.smem_len);

	if (!info->screen_base) {
		debug("ioremap failed\n");
		err = -1;
		goto out_unmap2;
	}

	default_par->flatpanel = is_flatpanel(default_par);

	if (default_par->flatpanel)
		nativex = get_nativex(default_par);

	info->fix = tridentfb_fix;
	info->fbops = &tridentfb_ops;
	info->pseudo_palette = default_par->pseudo_pal;

	info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;
	if (!noaccel && default_par->init_accel) {
		info->flags &= ~FBINFO_HWACCEL_DISABLED;
		info->flags |= FBINFO_HWACCEL_COPYAREA;
		info->flags |= FBINFO_HWACCEL_FILLRECT;
	} else
		info->flags |= FBINFO_HWACCEL_DISABLED;

	if (is_blade(chip_id) && chip_id != BLADE3D)
		info->flags |= FBINFO_READS_FAST;

	info->pixmap.addr = kmalloc(4096, GFP_KERNEL);
	if (!info->pixmap.addr) {
		err = -ENOMEM;
		goto out_unmap2;
	}

	info->pixmap.size = 4096;
	info->pixmap.buf_align = 4;
	info->pixmap.scan_align = 1;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	info->var.bits_per_pixel = 8;

	if (default_par->image_blit) {
		info->flags |= FBINFO_HWACCEL_IMAGEBLIT;
		info->pixmap.scan_align = 4;
	}

	if (noaccel) {
		printk(KERN_DEBUG "disabling acceleration\n");
		info->flags |= FBINFO_HWACCEL_DISABLED;
		info->pixmap.scan_align = 1;
	}

	if (tridentfb_setup_ddc_bus(info) == 0) {
		u8 *edid = fb_ddc_read(&default_par->ddc_adapter);

		default_par->ddc_registered = true;
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
				m = fb_find_best_display(&info->monspecs,
							 &info->modelist);
				if (m) {
					fb_videomode_to_var(&info->var, m);
					/* fill all other info->var's fields */
					if (tridentfb_check_var(&info->var,
								info) == 0)
						found = true;
				}
			}
		}
	}

	if (!mode_option && !found)
		mode_option = "640x480-8@60";

	/* Prepare startup mode */
	if (mode_option) {
		err = fb_find_mode(&info->var, info, mode_option,
				   info->monspecs.modedb,
				   info->monspecs.modedb_len,
				   NULL, info->var.bits_per_pixel);
		if (!err || err == 4) {
			err = -EINVAL;
			dev_err(info->device, "mode %s not found\n",
								mode_option);
			fb_destroy_modedb(info->monspecs.modedb);
			info->monspecs.modedb = NULL;
			goto out_unmap2;
		}
	}

	fb_destroy_modedb(info->monspecs.modedb);
	info->monspecs.modedb = NULL;

	err = fb_alloc_cmap(&info->cmap, 256, 0);
	if (err < 0)
		goto out_unmap2;

	info->var.activate |= FB_ACTIVATE_NOW;
	info->device = &dev->dev;
	if (register_framebuffer(info) < 0) {
		printk(KERN_ERR "tridentfb: could not register framebuffer\n");
		fb_dealloc_cmap(&info->cmap);
		err = -EINVAL;
		goto out_unmap2;
	}
	output("fb%d: %s frame buffer device %dx%d-%dbpp\n",
	   info->node, info->fix.id, info->var.xres,
	   info->var.yres, info->var.bits_per_pixel);

	pci_set_drvdata(dev, info);
	return 0;

out_unmap2:
	if (default_par->ddc_registered)
		i2c_del_adapter(&default_par->ddc_adapter);
	kfree(info->pixmap.addr);
	if (info->screen_base)
		iounmap(info->screen_base);
	release_mem_region(tridentfb_fix.smem_start, tridentfb_fix.smem_len);
	disable_mmio(info->par);
out_unmap1:
	if (default_par->io_virt)
		iounmap(default_par->io_virt);
	release_mem_region(tridentfb_fix.mmio_start, tridentfb_fix.mmio_len);
	framebuffer_release(info);
	return err;
}

static void trident_pci_remove(struct pci_dev *dev)
{
	struct fb_info *info = pci_get_drvdata(dev);
	struct tridentfb_par *par = info->par;

	unregister_framebuffer(info);
	if (par->ddc_registered)
		i2c_del_adapter(&par->ddc_adapter);
	iounmap(par->io_virt);
	iounmap(info->screen_base);
	release_mem_region(tridentfb_fix.smem_start, tridentfb_fix.smem_len);
	release_mem_region(tridentfb_fix.mmio_start, tridentfb_fix.mmio_len);
	kfree(info->pixmap.addr);
	fb_dealloc_cmap(&info->cmap);
	framebuffer_release(info);
}

/* List of boards that we are trying to support */
static const struct pci_device_id trident_devices[] = {
	{PCI_VENDOR_ID_TRIDENT,	BLADE3D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBERBLADEi7, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBERBLADEi7D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBERBLADEi1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBERBLADEi1D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBERBLADEAi1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBERBLADEAi1D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBERBLADEE4, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	TGUI9440, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	TGUI9660, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	IMAGE975, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	IMAGE985, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBER9320, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBER9388, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBER9520, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBER9525DVD, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBER9397, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBER9397DVD, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBERBLADEXPAi1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBERBLADEXPm8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_TRIDENT,	CYBERBLADEXPm16, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci, trident_devices);

static struct pci_driver tridentfb_pci_driver = {
	.name = "tridentfb",
	.id_table = trident_devices,
	.probe = trident_pci_probe,
	.remove = trident_pci_remove,
};

/*
 * Parse user specified options (`video=trident:')
 * example:
 *	video=trident:800x600,bpp=16,noaccel
 */
#ifndef MODULE
static int __init tridentfb_setup(char *options)
{
	char *opt;
	if (!options || !*options)
		return 0;
	while ((opt = strsep(&options, ",")) != NULL) {
		if (!*opt)
			continue;
		if (!strncmp(opt, "noaccel", 7))
			noaccel = 1;
		else if (!strncmp(opt, "fp", 2))
			fp = 1;
		else if (!strncmp(opt, "crt", 3))
			fp = 0;
		else if (!strncmp(opt, "bpp=", 4))
			bpp = simple_strtoul(opt + 4, NULL, 0);
		else if (!strncmp(opt, "center", 6))
			center = 1;
		else if (!strncmp(opt, "stretch", 7))
			stretch = 1;
		else if (!strncmp(opt, "memsize=", 8))
			memsize = simple_strtoul(opt + 8, NULL, 0);
		else if (!strncmp(opt, "memdiff=", 8))
			memdiff = simple_strtoul(opt + 8, NULL, 0);
		else if (!strncmp(opt, "nativex=", 8))
			nativex = simple_strtoul(opt + 8, NULL, 0);
		else
			mode_option = opt;
	}
	return 0;
}
#endif

static int __init tridentfb_init(void)
{
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("tridentfb", &option))
		return -ENODEV;
	tridentfb_setup(option);
#endif
	return pci_register_driver(&tridentfb_pci_driver);
}

static void __exit tridentfb_exit(void)
{
	pci_unregister_driver(&tridentfb_pci_driver);
}

module_init(tridentfb_init);
module_exit(tridentfb_exit);

MODULE_AUTHOR("Jani Monoses <jani@iv.ro>");
MODULE_DESCRIPTION("Framebuffer driver for Trident cards");
MODULE_LICENSE("GPL");
MODULE_ALIAS("cyblafb");

