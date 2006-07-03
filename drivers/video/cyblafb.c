/*
 * Frame buffer driver for Trident Cyberblade/i1 graphics core
 *
 * Copyright 2005 Knut Petersen <Knut_Petersen@t-online.de>
 *
 * CREDITS:
 *	tridentfb.c by Jani Monoses
 *	see files above for further credits
 *
 */

#define CYBLAFB_DEBUG 0
#define CYBLAFB_KD_GRAPHICS_QUIRK 1

#define CYBLAFB_PIXMAPSIZE 8192

#include <linux/module.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/types.h>
#include <video/cyblafb.h>

#define VERSION "0.62"

struct cyblafb_par {
	u32 pseudo_pal[16];
	struct fb_ops ops;
};

static struct fb_fix_screeninfo cyblafb_fix __devinitdata = {
	.id = "CyBla",
	.type = FB_TYPE_PACKED_PIXELS,
	.xpanstep = 1,
	.ypanstep = 1,
	.ywrapstep = 1,
	.visual = FB_VISUAL_PSEUDOCOLOR,
	.accel = FB_ACCEL_NONE,
};

static char *mode __devinitdata = NULL;
static int bpp __devinitdata = 8;
static int ref __devinitdata = 75;
static int fp __devinitdata;
static int crt __devinitdata;
static int memsize __devinitdata;

static int basestride;
static int vesafb;
static int nativex;
static int center;
static int stretch;
static int pciwb = 1;
static int pcirb = 1;
static int pciwr = 1;
static int pcirr = 1;
static int disabled;
static int verbosity;
static int displaytype;

static void __iomem *io_virt;	// iospace virtual memory address

module_param(mode, charp, 0);
module_param(bpp, int, 0);
module_param(ref, int, 0);
module_param(fp, int, 0);
module_param(crt, int, 0);
module_param(nativex, int, 0);
module_param(center, int, 0);
module_param(stretch, int, 0);
module_param(pciwb, int, 0);
module_param(pcirb, int, 0);
module_param(pciwr, int, 0);
module_param(pcirr, int, 0);
module_param(memsize, int, 0);
module_param(verbosity, int, 0);

//=========================================
//
// Well, we have to fix the upper layers.
// Until this has been done, we work around
// the bugs.
//
//=========================================

#if (CYBLAFB_KD_GRAPHICS_QUIRK && CYBLAFB_DEBUG)
	if (disabled) { \
		printk("********\n");\
		dump_stack();\
		return val;\
	}

#elif CYBLAFB_KD_GRAPHICS_QUIRK
#define KD_GRAPHICS_RETURN(val)\
	if (disabled) {\
		return val;\
	}
#else
#define KD_GRAPHICS_RETURN(val)
#endif

//=========================================
//
// Port access macros for memory mapped io
//
//=========================================

#define out8(r, v) writeb(v, io_virt + r)
#define out32(r, v) writel(v, io_virt + r)
#define in8(r) readb(io_virt + r)
#define in32(r) readl(io_virt + r)

//======================================
//
// Hardware access inline functions
//
//======================================

static inline u8 read3X4(u32 reg)
{
	out8(0x3D4, reg);
	return in8(0x3D5);
}

static inline u8 read3C4(u32 reg)
{
	out8(0x3C4, reg);
	return in8(0x3C5);
}

static inline u8 read3CE(u32 reg)
{
	out8(0x3CE, reg);
	return in8(0x3CF);
}

static inline void write3X4(u32 reg, u8 val)
{
	out8(0x3D4, reg);
	out8(0x3D5, val);
}

static inline void write3C4(u32 reg, u8 val)
{
	out8(0x3C4, reg);
	out8(0x3C5, val);
}

static inline void write3CE(u32 reg, u8 val)
{
	out8(0x3CE, reg);
	out8(0x3CF, val);
}

static inline void write3C0(u32 reg, u8 val)
{
	in8(0x3DA);		// read to reset index
	out8(0x3C0, reg);
	out8(0x3C0, val);
}

//=================================================
//
// Enable memory mapped io and unprotect registers
//
//=================================================

static void enable_mmio(void)
{
	u8 tmp;

	outb(0x0B, 0x3C4);
	inb(0x3C5);		// Set NEW mode
	outb(SR0E, 0x3C4);	// write enable a lot of extended ports
	outb(0x80, 0x3C5);

	outb(SR11, 0x3C4);	// write enable those extended ports that
	outb(0x87, 0x3C5);	// are not affected by SR0E_New

	outb(CR1E, 0x3d4);	// clear write protect bit for port 0x3c2
	tmp = inb(0x3d5) & 0xBF;
	outb(CR1E, 0x3d4);
	outb(tmp, 0x3d5);

	outb(CR39, 0x3D4);
	outb(inb(0x3D5) | 0x01, 0x3D5); // Enable mmio
}

//=================================================
//
// Set pixel clock VCLK1
// - multipliers set elswhere
// - freq in units of 0.01 MHz
//
// Hardware bug: SR18 >= 250 is broken for the
//		 cyberblade/i1
//
//=================================================

static void set_vclk(struct cyblafb_par *par, int freq)
{
	u32 m, n, k;
	int f, fi, d, di;
	u8 lo = 0, hi = 0;

	d = 2000;
	k = freq >= 10000 ? 0 : freq >= 5000 ? 1 : freq >= 2500 ? 2 : 3;
	for (m = 0; m < 64; m++)
		for (n = 0; n < 250; n++) {
			fi = (int)(((5864727 * (n + 8)) /
				    ((m + 2) * (1 << k))) >> 12);
			if ((di = abs(fi - freq)) < d) {
				d = di;
				f = fi;
				lo = (u8) n;
				hi = (u8) ((k << 6) | m);
			}
		}
	write3C4(SR19, hi);
	write3C4(SR18, lo);
	if (verbosity > 0)
		output("pixclock = %d.%02d MHz, k/m/n %x %x %x\n",
		       freq / 100, freq % 100, (hi & 0xc0) >> 6, hi & 0x3f, lo);
}

//================================================
//
// Cyberblade specific Graphics Engine (GE) setup
//
//================================================

static void cyblafb_setup_GE(int pitch, int bpp)
{
	KD_GRAPHICS_RETURN();

	switch (bpp) {
	case 8:
		basestride = ((pitch >> 3) << 20) | (0 << 29);
		break;
	case 15:
		basestride = ((pitch >> 3) << 20) | (5 << 29);
		break;
	case 16:
		basestride = ((pitch >> 3) << 20) | (1 << 29);
		break;
	case 24:
	case 32:
		basestride = ((pitch >> 3) << 20) | (2 << 29);
		break;
	}

	write3X4(CR36, 0x90);	// reset GE
	write3X4(CR36, 0x80);	// enable GE
	out32(GE24, 1 << 7);	// reset all GE pointers by toggling
	out32(GE24, 0); 	//   d7 of GE24
	write3X4(CR2D, 0x00);	// GE Timinigs, no delays
	out32(GE6C, 0); 	// Pattern and Style, p 129, ok
}

//=====================================================================
//
// Cyberblade specific syncing
//
//   A timeout might be caused by disabled mmio.
//   Cause:
//     - bit CR39 & 1 == 0 upon return, X trident driver bug
//     - kdm bug (KD_GRAPHICS not set on first switch)
//     - kernel design flaw (it believes in the correctness
//	 of kdm/X
//   First we try to sync ignoring that problem, as most of the
//   time that will succeed immediately and the enable_mmio()
//   would only degrade performance.
//
//=====================================================================

static int cyblafb_sync(struct fb_info *info)
{
	u32 status, i = 100000;

	KD_GRAPHICS_RETURN(0);

	while (((status = in32(GE20)) & 0xFe800000) && i != 0)
		i--;

	if (i == 0) {
		enable_mmio();
		i = 1000000;
		while (((status = in32(GE20)) & 0xFA800000) && i != 0)
			i--;
		if (i == 0) {
			output("GE Timeout, status: %x\n", status);
			if (status & 0x80000000)
				output("Bresenham Engine : Busy\n");
			if (status & 0x40000000)
				output("Setup Engine     : Busy\n");
			if (status & 0x20000000)
				output("SP / DPE         : Busy\n");
			if (status & 0x10000000)
				output("Memory Interface : Busy\n");
			if (status & 0x08000000)
				output("Com Lst Proc     : Busy\n");
			if (status & 0x04000000)
				output("Block Write      : Busy\n");
			if (status & 0x02000000)
				output("Command Buffer   : Full\n");
			if (status & 0x01000000)
				output("RESERVED         : Busy\n");
			if (status & 0x00800000)
				output("PCI Write Buffer : Busy\n");
			cyblafb_setup_GE(info->var.xres,
					 info->var.bits_per_pixel);
		}
	}

	return 0;
}

//==============================
//
// Cyberblade specific fillrect
//
//==============================

static void cyblafb_fillrect(struct fb_info *info, const struct fb_fillrect *fr)
{
	u32 bpp = info->var.bits_per_pixel, col, desty, height;

	KD_GRAPHICS_RETURN();

	switch (bpp) {
	default:
	case 8:
		col = fr->color;
		col |= col << 8;
		col |= col << 16;
		break;
	case 16:
		col = ((u32 *) (info->pseudo_palette))[fr->color];
		col |= col << 16;
		break;
	case 32:
		col = ((u32 *) (info->pseudo_palette))[fr->color];
		break;
	}

	desty = fr->dy;
	height = fr->height;
	while (height) {
		out32(GEB8, basestride | ((desty * info->var.xres_virtual *
					   bpp) >> 6));
		out32(GE60, col);
		out32(GE48, fr->rop ? 0x66 : ROP_S);
		out32(GE44, 0x20000000 | 1 << 19 | 1 << 4 | 2 << 2);
		out32(GE08, point(fr->dx, 0));
		out32(GE0C, point(fr->dx + fr->width - 1,
				  height > 4096 ? 4095 : height - 1));
		if (likely(height <= 4096))
			return;
		desty += 4096;
		height -= 4096;
	}
}

//================================================
//
// Cyberblade specific copyarea
//
// This function silently assumes that it never
// will be called with width or height exceeding
// 4096.
//
//================================================

static void cyblafb_copyarea(struct fb_info *info, const struct fb_copyarea *ca)
{
	u32 s1, s2, d1, d2, direction;

	KD_GRAPHICS_RETURN();

	s1 = point(ca->sx, 0);
	s2 = point(ca->sx + ca->width - 1, ca->height - 1);
	d1 = point(ca->dx, 0);
	d2 = point(ca->dx + ca->width - 1, ca->height - 1);

	if ((ca->sy > ca->dy) || ((ca->sy == ca->dy) && (ca->sx > ca->dx)))
		direction = 0;
	else
		direction = 2;

	out32(GEB8, basestride | ((ca->dy * info->var.xres_virtual *
				   info->var.bits_per_pixel) >> 6));
	out32(GEC8, basestride | ((ca->sy * info->var.xres_virtual *
				   info->var.bits_per_pixel) >> 6));
	out32(GE44, 0xa0000000 | 1 << 19 | 1 << 2 | direction);
	out32(GE00, direction ? s2 : s1);
	out32(GE04, direction ? s1 : s2);
	out32(GE08, direction ? d2 : d1);
	out32(GE0C, direction ? d1 : d2);
}

//=======================================================================
//
// Cyberblade specific imageblit
//
// Accelerated for the most usual case, blitting 1 - bit deep
// character images. Everything else is passed to the generic imageblit
// unless it is so insane that it is better to printk an alert.
//
// Hardware bug: _Never_ blit across pixel column 2048, that will lock
// the system. We split those blit requests into three blitting
// operations.
//
//=======================================================================

static void cyblafb_imageblit(struct fb_info *info,
			      const struct fb_image *image)
{
	u32 fgcol, bgcol;
	u32 *pd = (u32 *) image->data;
	u32 bpp = info->var.bits_per_pixel;

	KD_GRAPHICS_RETURN();

	// Used only for drawing the penguine (image->depth > 1)
	if (image->depth != 1) {
		cfb_imageblit(info, image);
		return;
	}
	// That should never happen, but it would be fatal
	if (image->width == 0 || image->height == 0) {
		output("imageblit: width/height 0 detected\n");
		return;
	}

	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		fgcol = ((u32 *) (info->pseudo_palette))[image->fg_color];
		bgcol = ((u32 *) (info->pseudo_palette))[image->bg_color];
	} else {
		fgcol = image->fg_color;
		bgcol = image->bg_color;
	}

	switch (bpp) {
	case 8:
		fgcol |= fgcol << 8;
		bgcol |= bgcol << 8;
	case 16:
		fgcol |= fgcol << 16;
		bgcol |= bgcol << 16;
	default:
		break;
	}

	out32(GEB8, basestride | ((image->dy * info->var.xres_virtual *
				   bpp) >> 6));
	out32(GE60, fgcol);
	out32(GE64, bgcol);

	if (!(image->dx < 2048 && (image->dx + image->width - 1) >= 2048)) {
		u32 dds = ((image->width + 31) >> 5) * image->height;
		out32(GE44, 0xa0000000 | 1 << 20 | 1 << 19);
		out32(GE08, point(image->dx, 0));
		out32(GE0C, point(image->dx + image->width - 1,
				  image->height - 1));
		while (dds--)
			out32(GE9C, *pd++);
	} else {
		int i, j;
		u32 ddstotal = (image->width + 31) >> 5;
		u32 ddsleft = (2048 - image->dx + 31) >> 5;
		u32 skipleft = ddstotal - ddsleft;

		out32(GE44, 0xa0000000 | 1 << 20 | 1 << 19);
		out32(GE08, point(image->dx, 0));
		out32(GE0C, point(2048 - 1, image->height - 1));
		for (i = 0; i < image->height; i++) {
			for (j = 0; j < ddsleft; j++)
				out32(GE9C, *pd++);
			pd += skipleft;
		}

		if (image->dx % 32) {
			out32(GE44, 0xa0000000 | 1 << 20 | 1 << 19);
			out32(GE08, point(2048, 0));
			if (image->width > ddsleft << 5)
				out32(GE0C, point(image->dx + (ddsleft << 5) -
						  1, image->height - 1));
			else
				out32(GE0C, point(image->dx + image->width - 1,
						  image->height - 1));
			pd = ((u32 *) image->data) + ddstotal - skipleft - 1;
			for (i = 0; i < image->height; i++) {
				out32(GE9C, swab32(swab32(*pd) << ((32 -
					    (image->dx & 31)) & 31)));
				pd += ddstotal;
			}
		}

		if (skipleft) {
			out32(GE44, 0xa0000000 | 1 << 20 | 1 << 19);
			out32(GE08, point(image->dx + (ddsleft << 5), 0));
			out32(GE0C, point(image->dx + image->width - 1,
					  image->height - 1));
			pd = (u32 *) image->data;
			for (i = 0; i < image->height; i++) {
				pd += ddsleft;
				for (j = 0; j < skipleft; j++)
					out32(GE9C, *pd++);
			}
		}
	}
}

//==========================================================
//
// Check if video mode is acceptable. We change var->??? if
// video mode is slightly off or return error otherwise.
// info->??? must not be changed!
//
//==========================================================

static int cyblafb_check_var(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	int bpp = var->bits_per_pixel;

	//
	// we try to support 8, 16, 24 and 32 bpp modes,
	// default to 8
	//
	// there is a 24 bpp mode, but for now we change requests to 32 bpp
	// (This is what tridentfb does ... will be changed in the future)
	//
	//
	if (bpp % 8 != 0 || bpp < 8 || bpp > 32)
		bpp = 8;
	if (bpp == 24)
		bpp = var->bits_per_pixel = 32;

	//
	// interlaced modes are broken, fail if one is requested
	//
	if (var->vmode & FB_VMODE_INTERLACED)
		return -EINVAL;

	//
	// fail if requested resolution is higher than physical
	// flatpanel resolution
	//
	if ((displaytype == DISPLAY_FP) && nativex && var->xres > nativex)
		return -EINVAL;

	//
	// we do not allow vclk to exceed 230 MHz. If the requested
	// vclk is too high, we default to 200 MHz
	//
	if ((bpp == 32 ? 200000000 : 100000000) / var->pixclock > 23000)
		var->pixclock = (bpp == 32 ? 200000000 : 100000000) / 20000;

	//
	// enforce (h|v)sync_len limits
	//
	var->hsync_len &= ~7;
	if(var->hsync_len > 248)
		var->hsync_len = 248;

	var->vsync_len &= 15;

	//
	// Enforce horizontal and vertical hardware limits.
	// 1600x1200 is mentioned as a maximum, but higher resolutions could
	// work with slow refresh, small margins and short sync.
	//
	var->xres &= ~7;

	if (((var->xres + var->left_margin + var->right_margin +
			var->hsync_len) > (bpp == 32 ? 2040 : 4088)) ||
			((var->yres + var->upper_margin + var->lower_margin +
			var->vsync_len) > 2047))
		return -EINVAL;

	if ((var->xres > 1600) || (var->yres > 1200))
		output("Mode %dx%d exceeds documented limits.\n",
					   var->xres, var->yres);
	//
	// try to be smart about (x|y)res_virtual problems.
	//
	if (var->xres > var->xres_virtual)
		var->xres_virtual = var->xres;
	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;

	if (bpp == 8 || bpp == 16) {
		if (var->xres_virtual > 4088)
			var->xres_virtual = 4088;
	} else {
		if (var->xres_virtual > 2040)
			var->xres_virtual = 2040;
	}
	var->xres_virtual &= ~7;
	while (var->xres_virtual * var->yres_virtual * bpp / 8 >
	       info->fix.smem_len) {
		if (var->yres_virtual > var->yres)
			var->yres_virtual--;
		else if (var->xres_virtual > var->xres)
			var->xres_virtual -= 8;
		else
			return -EINVAL;
	}

	switch (bpp) {
	case 8:
		var->red.offset = 0;
		var->green.offset = 0;
		var->blue.offset = 0;
		var->red.length = 6;
		var->green.length = 6;
		var->blue.length = 6;
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

	return 0;
}

//=====================================================================
//
// Pan the display
//
// The datasheets defines crt start address to be 20 bits wide and
// to be programmed to CR0C, CR0D, CR1E and CR27. Actually there is
// CR2B[5] as an undocumented extension bit. Epia BIOS 2.07 does use
// it, so it is also safe to be used here. BTW: datasheet CR0E on page
// 90 really is CR1E, the real CRE is documented on page 72.
//
// BUT:
//
// As of internal version 0.60 we do not use vga panning any longer.
// Vga panning did not allow us the use of all available video memory
// and thus prevented ywrap scrolling. We do use the "right view"
// register now.
//
//
//=====================================================================

static int cyblafb_pan_display(struct fb_var_screeninfo *var,
			       struct fb_info *info)
{
	KD_GRAPHICS_RETURN(0);

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;
	out32(GE10, 0x80000000 | ((var->xoffset + (var->yoffset *
		    var->xres_virtual)) * var->bits_per_pixel / 32));
	return 0;
}

//============================================
//
// This will really help in case of a bug ...
// dump most gaphics core registers.
//
//============================================

static void regdump(struct cyblafb_par *par)
{
	int i;

	if (verbosity < 2)
		return;

	printk("\n");
	for (i = 0; i <= 0xff; i++) {
		outb(i, 0x3d4);
		printk("CR%02x=%02x ", i, inb(0x3d5));
		if (i % 16 == 15)
			printk("\n");
	}

	outb(0x30, 0x3ce);
	outb(inb(0x3cf) | 0x40, 0x3cf);
	for (i = 0; i <= 0x1f; i++) {
		if (i == 0 || (i > 2 && i < 8) || i == 0x10 || i == 0x11
		    || i == 0x16) {
			outb(i, 0x3d4);
			printk("CR%02x=%02x ", i, inb(0x3d5));
		} else
			printk("------- ");
		if (i % 16 == 15)
			printk("\n");
	}
	outb(0x30, 0x3ce);
	outb(inb(0x3cf) & 0xbf, 0x3cf);

	printk("\n");
	for (i = 0; i <= 0x7f; i++) {
		outb(i, 0x3ce);
		printk("GR%02x=%02x ", i, inb(0x3cf));
		if (i % 16 == 15)
			printk("\n");
	}

	printk("\n");
	for (i = 0; i <= 0xff; i++) {
		outb(i, 0x3c4);
		printk("SR%02x=%02x ", i, inb(0x3c5));
		if (i % 16 == 15)
			printk("\n");
	}

	printk("\n");
	for (i = 0; i <= 0x1F; i++) {
		inb(0x3da);	// next access is index!
		outb(i, 0x3c0);
		printk("AR%02x=%02x ", i, inb(0x3c1));
		if (i % 16 == 15)
			printk("\n");
	}
	printk("\n");

	inb(0x3DA);		// reset internal flag to 3c0 index
	outb(0x20, 0x3C0);	// enable attr

	return;
}

//=======================================================================
//
// Save State
//
// This function is called while a switch to KD_TEXT is in progress,
// before any of the other functions are called.
//
//=======================================================================

static void cyblafb_save_state(struct fb_info *info)
{
	struct cyblafb_par *par = info->par;
	if (verbosity > 0)
		output("Switching to KD_TEXT\n");
	disabled = 0;
	regdump(par);
	enable_mmio();
	return;
}

//=======================================================================
//
// Restore State
//
// This function is called while a switch to KD_GRAPHICS is in progress,
// We have to turn on vga style panning registers again because the
// trident driver of X does not know about GE10.
//
//=======================================================================

static void cyblafb_restore_state(struct fb_info *info)
{
	if (verbosity > 0)
		output("Switching to KD_GRAPHICS\n");
	out32(GE10, 0);
	disabled = 1;
	return;
}

//======================================
//
// Set hardware to requested video mode
//
//======================================

static int cyblafb_set_par(struct fb_info *info)
{
	struct cyblafb_par *par = info->par;
	u32 htotal, hdispend, hsyncstart, hsyncend, hblankstart,
	    hblankend, preendfetch, vtotal, vdispend, vsyncstart,
	    vsyncend, vblankstart, vblankend;
	struct fb_var_screeninfo *var = &info->var;
	int bpp = var->bits_per_pixel;
	int i;

	KD_GRAPHICS_RETURN(0);

	if (verbosity > 0)
		output("Switching to new mode: "
		       "fbset -g %d %d %d %d %d -t %d %d %d %d %d %d %d\n",
		       var->xres, var->yres, var->xres_virtual,
		       var->yres_virtual, var->bits_per_pixel, var->pixclock,
		       var->left_margin, var->right_margin, var->upper_margin,
		       var->lower_margin, var->hsync_len, var->vsync_len);

	htotal = (var->xres + var->left_margin + var->right_margin +
		  var->hsync_len) / 8 - 5;
	hdispend = var->xres / 8 - 1;
	hsyncstart = (var->xres + var->right_margin) / 8;
	hsyncend = var->hsync_len / 8;
	hblankstart = hdispend + 1;
	hblankend = htotal + 3; // should be htotal + 5, bios does it this way
	preendfetch = ((var->xres >> 3) + 1) * ((bpp + 1) >> 3);

	vtotal = var->yres + var->upper_margin + var->lower_margin +
							var->vsync_len - 2;
	vdispend = var->yres - 1;
	vsyncstart = var->yres + var->lower_margin;
	vblankstart = var->yres;
	vblankend = vtotal; // should be vtotal + 2, but bios does it this way
	vsyncend = var->vsync_len;

	enable_mmio();		// necessary! ... check X ...

	write3X4(CR11, read3X4(CR11) & 0x7F);	// unlock cr00 .. cr07

	write3CE(GR30, 8);

	if ((displaytype == DISPLAY_FP) && var->xres < nativex) {

		// stretch or center ?

		out8(0x3C2, 0xEB);

		write3CE(GR30, read3CE(GR30) | 0x81);	// shadow mode on

		if (center) {
			write3CE(GR52, (read3CE(GR52) & 0x7C) | 0x80);
			write3CE(GR53, (read3CE(GR53) & 0x7C) | 0x80);
		} else if (stretch) {
			write3CE(GR5D, 0);
			write3CE(GR52, (read3CE(GR52) & 0x7C) | 1);
			write3CE(GR53, (read3CE(GR53) & 0x7C) | 1);
		}

	} else {
		out8(0x3C2, 0x2B);
		write3CE(GR30, 8);
	}

	//
	// Setup CRxx regs
	//

	write3X4(CR00, htotal & 0xFF);
	write3X4(CR01, hdispend & 0xFF);
	write3X4(CR02, hblankstart & 0xFF);
	write3X4(CR03, hblankend & 0x1F);
	write3X4(CR04, hsyncstart & 0xFF);
	write3X4(CR05, (hsyncend & 0x1F) | ((hblankend & 0x20) << 2));
	write3X4(CR06, vtotal & 0xFF);
	write3X4(CR07, (vtotal & 0x100) >> 8 |
		       (vdispend & 0x100) >> 7 |
		       (vsyncstart & 0x100) >> 6 |
		       (vblankstart & 0x100) >> 5 |
		       0x10 |
		       (vtotal & 0x200) >> 4 |
		       (vdispend & 0x200) >> 3 | (vsyncstart & 0x200) >> 2);
	write3X4(CR08, 0);
	write3X4(CR09, (vblankstart & 0x200) >> 4 | 0x40 |	// FIX !!!
		       ((info->var.vmode & FB_VMODE_DOUBLE) ? 0x80 : 0));
	write3X4(CR0A, 0);	// Init to some reasonable default
	write3X4(CR0B, 0);	// Init to some reasonable default
	write3X4(CR0C, 0);	// Offset 0
	write3X4(CR0D, 0);	// Offset 0
	write3X4(CR0E, 0);	// Init to some reasonable default
	write3X4(CR0F, 0);	// Init to some reasonable default
	write3X4(CR10, vsyncstart & 0xFF);
	write3X4(CR11, (vsyncend & 0x0F));
	write3X4(CR12, vdispend & 0xFF);
	write3X4(CR13, ((info->var.xres_virtual * bpp) / (4 * 16)) & 0xFF);
	write3X4(CR14, 0x40);	// double word mode
	write3X4(CR15, vblankstart & 0xFF);
	write3X4(CR16, vblankend & 0xFF);
	write3X4(CR17, 0xE3);
	write3X4(CR18, 0xFF);
	//	 CR19: needed for interlaced modes ... ignore it for now
	write3X4(CR1A, 0x07);	// Arbitration Control Counter 1
	write3X4(CR1B, 0x07);	// Arbitration Control Counter 2
	write3X4(CR1C, 0x07);	// Arbitration Control Counter 3
	write3X4(CR1D, 0x00);	// Don't know, doesn't hurt ; -)
	write3X4(CR1E, (info->var.vmode & FB_VMODE_INTERLACED) ? 0x84 : 0x80);
	//	 CR1F: do not set, contains BIOS info about memsize
	write3X4(CR20, 0x20);	// enabe wr buf, disable 16bit planar mode
	write3X4(CR21, 0x20);	// enable linear memory access
	//	 CR22: RO cpu latch readback
	//	 CR23: ???
	//	 CR24: RO AR flag state
	//	 CR25: RAMDAC rw timing, pclk buffer tristate control ????
	//	 CR26: ???
	write3X4(CR27, (vdispend & 0x400) >> 6 |
		       (vsyncstart & 0x400) >> 5 |
		       (vblankstart & 0x400) >> 4 |
		       (vtotal & 0x400) >> 3 |
		       0x8);
	//	 CR28: ???
	write3X4(CR29, (read3X4(CR29) & 0xCF) | ((((info->var.xres_virtual *
			bpp) / (4 * 16)) & 0x300) >> 4));
	write3X4(CR2A, read3X4(CR2A) | 0x40);
	write3X4(CR2B, (htotal & 0x100) >> 8 |
		       (hdispend & 0x100) >> 7 |
		       // (0x00 & 0x100) >> 6 |   hinterlace para bit 8 ???
		       (hsyncstart & 0x100) >> 5 |
		       (hblankstart & 0x100) >> 4);
	//	 CR2C: ???
	//	 CR2D: initialized in cyblafb_setup_GE()
	write3X4(CR2F, 0x92);	// conservative, better signal quality
	//	 CR30: reserved
	//	 CR31: reserved
	//	 CR32: reserved
	//	 CR33: reserved
	//	 CR34: disabled in CR36
	//	 CR35: disabled in CR36
	//	 CR36: initialized in cyblafb_setup_GE
	//	 CR37: i2c, ignore for now
	write3X4(CR38, (bpp == 8) ? 0x00 :	//
		       (bpp == 16) ? 0x05 :	// highcolor
		       (bpp == 24) ? 0x29 :	// packed 24bit truecolor
		       (bpp == 32) ? 0x09 : 0); // truecolor, 16 bit pixelbus
	write3X4(CR39, 0x01 |	// MMIO enable
		       (pcirb ? 0x02 : 0) |	// pci read burst enable
		       (pciwb ? 0x04 : 0));	// pci write burst enable
	write3X4(CR55, 0x1F | // pci clocks * 2 for STOP# during 1st data phase
		       (pcirr ? 0x40 : 0) |	// pci read retry enable
		       (pciwr ? 0x80 : 0));	// pci write retry enable
	write3X4(CR56, preendfetch >> 8 < 2 ? (preendfetch >> 8 & 0x01) | 2
					    : 0);
	write3X4(CR57, preendfetch >> 8 < 2 ? preendfetch & 0xff : 0);
	write3X4(CR58, 0x82);	// Bios does this .... don't know more
	//
	// Setup SRxx regs
	//
	write3C4(SR00, 3);
	write3C4(SR01, 1);	//set char clock 8 dots wide
	write3C4(SR02, 0x0F);	//enable 4 maps needed in chain4 mode
	write3C4(SR03, 0);	//no character map select
	write3C4(SR04, 0x0E);	//memory mode: ext mem, even, chain4

	out8(0x3C4, 0x0b);
	in8(0x3C5);		// Set NEW mode
	write3C4(SR0D, 0x00);	// test ... check

	set_vclk(par, (bpp == 32 ? 200000000 : 100000000)
					/ info->var.pixclock);	//SR18, SR19

	//
	// Setup GRxx regs
	//
	write3CE(GR00, 0x00);	// test ... check
	write3CE(GR01, 0x00);	// test ... check
	write3CE(GR02, 0x00);	// test ... check
	write3CE(GR03, 0x00);	// test ... check
	write3CE(GR04, 0x00);	// test ... check
	write3CE(GR05, 0x40);	// no CGA compat, allow 256 col
	write3CE(GR06, 0x05);	// graphics mode
	write3CE(GR07, 0x0F);	// planes?
	write3CE(GR08, 0xFF);	// test ... check
	write3CE(GR0F, (bpp == 32) ? 0x1A : 0x12); // vclk / 2 if 32bpp, chain4
	write3CE(GR20, 0xC0);	// test ... check
	write3CE(GR2F, 0xA0);	// PCLK = VCLK, no skew,

	//
	// Setup ARxx regs
	//
	for (i = 0; i < 0x10; i++)	// set AR00 .. AR0f
		write3C0(i, i);
	write3C0(AR10, 0x41);	// graphics mode and support 256 color modes
	write3C0(AR12, 0x0F);	// planes
	write3C0(AR13, 0);	// horizontal pel panning
	in8(0x3DA);		// reset internal flag to 3c0 index
	out8(0x3C0, 0x20);	// enable attr

	//
	// Setup hidden RAMDAC command register
	//
	in8(0x3C8);		// these reads are
	in8(0x3C6);		// necessary to
	in8(0x3C6);		// unmask the RAMDAC
	in8(0x3C6);		// command reg, otherwise
	in8(0x3C6);		// we would write the pixelmask reg!
	out8(0x3C6, (bpp == 8) ? 0x00 : // 256 colors
	     (bpp == 15) ? 0x10 :	//
	     (bpp == 16) ? 0x30 :	// hicolor
	     (bpp == 24) ? 0xD0 :	// truecolor
	     (bpp == 32) ? 0xD0 : 0);	// truecolor
	in8(0x3C8);

	//
	// GR31 is not mentioned in the datasheet
	//
	if (displaytype == DISPLAY_FP)
		write3CE(GR31, (read3CE(GR31) & 0x8F) |
			 ((info->var.yres > 1024) ? 0x50 :
			  (info->var.yres > 768) ? 0x30 :
			  (info->var.yres > 600) ? 0x20 :
			  (info->var.yres > 480) ? 0x10 : 0));

	info->fix.visual = (bpp == 8) ? FB_VISUAL_PSEUDOCOLOR
				      : FB_VISUAL_TRUECOLOR;
	info->fix.line_length = info->var.xres_virtual * (bpp >> 3);
	info->cmap.len = (bpp == 8) ? 256 : 16;

	//
	// init acceleration engine
	//
	cyblafb_setup_GE(info->var.xres_virtual, info->var.bits_per_pixel);

	//
	// Set/clear flags to allow proper scroll mode selection.
	//
	if (var->xres == var->xres_virtual)
		info->flags &= ~FBINFO_HWACCEL_XPAN;
	else
		info->flags |= FBINFO_HWACCEL_XPAN;

	if (var->yres == var->yres_virtual)
		info->flags &= ~FBINFO_HWACCEL_YPAN;
	else
		info->flags |= FBINFO_HWACCEL_YPAN;

	if (info->fix.smem_len !=
	    var->xres_virtual * var->yres_virtual * bpp / 8)
		info->flags &= ~FBINFO_HWACCEL_YWRAP;
	else
		info->flags |= FBINFO_HWACCEL_YWRAP;

	regdump(par);

	return 0;
}

//========================
//
// Set one color register
//
//========================

static int cyblafb_setcolreg(unsigned regno, unsigned red, unsigned green,
			     unsigned blue, unsigned transp,
			     struct fb_info *info)
{
	int bpp = info->var.bits_per_pixel;

	KD_GRAPHICS_RETURN(0);

	if (regno >= info->cmap.len)
		return 1;

	if (bpp == 8) {
		out8(0x3C6, 0xFF);
		out8(0x3C8, regno);
		out8(0x3C9, red >> 10);
		out8(0x3C9, green >> 10);
		out8(0x3C9, blue >> 10);

	} else if (bpp == 16)	// RGB 565
		((u32 *) info->pseudo_palette)[regno] =
		    (red & 0xF800) |
		    ((green & 0xFC00) >> 5) | ((blue & 0xF800) >> 11);
	else if (bpp == 32)	// ARGB 8888
		((u32 *) info->pseudo_palette)[regno] =
		    ((transp & 0xFF00) << 16) |
		    ((red & 0xFF00) << 8) |
		    ((green & 0xFF00)) | ((blue & 0xFF00) >> 8);

	return 0;
}

//==========================================================
//
// Try blanking the screen. For flat panels it does nothing
//
//==========================================================

static int cyblafb_blank(int blank_mode, struct fb_info *info)
{
	unsigned char PMCont, DPMSCont;

	KD_GRAPHICS_RETURN(0);

	if (displaytype == DISPLAY_FP)
		return 0;

	out8(0x83C8, 0x04);	// DPMS Control
	PMCont = in8(0x83C6) & 0xFC;

	DPMSCont = read3CE(GR23) & 0xFC;

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:	// Screen: On, HSync: On, VSync: On
	case FB_BLANK_NORMAL:	// Screen: Off, HSync: On, VSync: On
		PMCont |= 0x03;
		DPMSCont |= 0x00;
		break;
	case FB_BLANK_HSYNC_SUSPEND:	// Screen: Off, HSync: Off, VSync: On
		PMCont |= 0x02;
		DPMSCont |= 0x01;
		break;
	case FB_BLANK_VSYNC_SUSPEND:	// Screen: Off, HSync: On, VSync: Off
		PMCont |= 0x02;
		DPMSCont |= 0x02;
		break;
	case FB_BLANK_POWERDOWN:	// Screen: Off, HSync: Off, VSync: Off
		PMCont |= 0x00;
		DPMSCont |= 0x03;
		break;
	}

	write3CE(GR23, DPMSCont);
	out8(0x83C8, 4);
	out8(0x83C6, PMCont);
	//
	// let fbcon do a softblank for us
	//
	return (blank_mode == FB_BLANK_NORMAL) ? 1 : 0;
}

static struct fb_ops cyblafb_ops __devinitdata = {
	.owner = THIS_MODULE,
	.fb_setcolreg = cyblafb_setcolreg,
	.fb_pan_display = cyblafb_pan_display,
	.fb_blank = cyblafb_blank,
	.fb_check_var = cyblafb_check_var,
	.fb_set_par = cyblafb_set_par,
	.fb_fillrect = cyblafb_fillrect,
	.fb_copyarea = cyblafb_copyarea,
	.fb_imageblit = cyblafb_imageblit,
	.fb_sync = cyblafb_sync,
	.fb_restore_state = cyblafb_restore_state,
	.fb_save_state = cyblafb_save_state,
};

//==========================================================================
//
// getstartupmode() decides about the inital video mode
//
// There is no reason to use modedb, a lot of video modes there would
// need altered timings to display correctly. So I decided that it is much
// better to provide a limited optimized set of modes plus the option of
// using the mode in effect at startup time (might be selected using the
// vga=??? paramter). After that the user might use fbset to select any
// mode he likes, check_var will not try to alter geometry parameters as
// it would be necessary otherwise.
//
//==========================================================================

static int __devinit getstartupmode(struct fb_info *info)
{
	u32 htotal, hdispend, hsyncstart, hsyncend, hblankstart, hblankend,
	    vtotal, vdispend, vsyncstart, vsyncend, vblankstart, vblankend,
	    cr00, cr01, cr02, cr03, cr04, cr05, cr2b,
	    cr06, cr07, cr09, cr10, cr11, cr12, cr15, cr16, cr27,
	    cr38, sr0d, sr18, sr19, gr0f, fi, pxclkdiv, vclkdiv, tmp, i;

	struct modus {
		int xres; int vxres; int yres; int vyres;
		int bpp; int pxclk;
		int left_margin; int right_margin;
		int upper_margin; int lower_margin;
		int hsync_len; int vsync_len;
	} modedb[5] = {
		{
		0, 2048, 0, 4096, 0, 0, 0, 0, 0, 0, 0, 0}, {
		640, 2048, 480, 4096, 0, 0, -40, 24, 17, 0, 216, 3}, {
		800, 2048, 600, 4096, 0, 0, 96, 24, 14, 0, 136, 11}, {
		1024, 2048, 768, 4096, 0, 0, 144, 24, 29, 0, 120, 3}, {
		1280, 2048, 1024, 4096, 0, 0, 232, 16, 39, 0, 160, 3}
	};

	outb(0x00, 0x3d4); cr00 = inb(0x3d5);
	outb(0x01, 0x3d4); cr01 = inb(0x3d5);
	outb(0x02, 0x3d4); cr02 = inb(0x3d5);
	outb(0x03, 0x3d4); cr03 = inb(0x3d5);
	outb(0x04, 0x3d4); cr04 = inb(0x3d5);
	outb(0x05, 0x3d4); cr05 = inb(0x3d5);
	outb(0x06, 0x3d4); cr06 = inb(0x3d5);
	outb(0x07, 0x3d4); cr07 = inb(0x3d5);
	outb(0x09, 0x3d4); cr09 = inb(0x3d5);
	outb(0x10, 0x3d4); cr10 = inb(0x3d5);
	outb(0x11, 0x3d4); cr11 = inb(0x3d5);
	outb(0x12, 0x3d4); cr12 = inb(0x3d5);
	outb(0x15, 0x3d4); cr15 = inb(0x3d5);
	outb(0x16, 0x3d4); cr16 = inb(0x3d5);
	outb(0x27, 0x3d4); cr27 = inb(0x3d5);
	outb(0x2b, 0x3d4); cr2b = inb(0x3d5);
	outb(0x38, 0x3d4); cr38 = inb(0x3d5);

	outb(0x0b, 0x3c4);
	inb(0x3c5);

	outb(0x0d, 0x3c4); sr0d = inb(0x3c5);
	outb(0x18, 0x3c4); sr18 = inb(0x3c5);
	outb(0x19, 0x3c4); sr19 = inb(0x3c5);
	outb(0x0f, 0x3ce); gr0f = inb(0x3cf);

	htotal = cr00 | (cr2b & 0x01) << 8;
	hdispend = cr01 | (cr2b & 0x02) << 7;
	hblankstart = cr02 | (cr2b & 0x10) << 4;
	hblankend = (cr03 & 0x1f) | (cr05 & 0x80) >> 2;
	hsyncstart = cr04 | (cr2b & 0x08) << 5;
	hsyncend = cr05 & 0x1f;

	modedb[0].xres = hblankstart * 8;
	modedb[0].hsync_len = hsyncend * 8;
	modedb[0].right_margin = hsyncstart * 8 - modedb[0].xres;
	modedb[0].left_margin = (htotal + 5) * 8 - modedb[0].xres -
	    modedb[0].right_margin - modedb[0].hsync_len;

	vtotal = cr06 | (cr07 & 0x01) << 8 | (cr07 & 0x20) << 4
	    | (cr27 & 0x80) << 3;
	vdispend = cr12 | (cr07 & 0x02) << 7 | (cr07 & 0x40) << 3
	    | (cr27 & 0x10) << 6;
	vsyncstart = cr10 | (cr07 & 0x04) << 6 | (cr07 & 0x80) << 2
	    | (cr27 & 0x20) << 5;
	vsyncend = cr11 & 0x0f;
	vblankstart = cr15 | (cr07 & 0x08) << 5 | (cr09 & 0x20) << 4
	    | (cr27 & 0x40) << 4;
	vblankend = cr16;

	modedb[0].yres = vdispend + 1;
	modedb[0].vsync_len = vsyncend;
	modedb[0].lower_margin = vsyncstart - modedb[0].yres;
	modedb[0].upper_margin = vtotal - modedb[0].yres -
	    modedb[0].lower_margin - modedb[0].vsync_len + 2;

	tmp = cr38 & 0x3c;
	modedb[0].bpp = tmp == 0 ? 8 : tmp == 4 ? 16 : tmp == 28 ? 24 :
	    tmp == 8 ? 32 : 8;

	fi = ((5864727 * (sr18 + 8)) /
	      (((sr19 & 0x3f) + 2) * (1 << ((sr19 & 0xc0) >> 6)))) >> 12;
	pxclkdiv = ((gr0f & 0x08) >> 3 | (gr0f & 0x40) >> 5) + 1;
	tmp = sr0d & 0x06;
	vclkdiv = tmp == 0 ? 2 : tmp == 2 ? 4 : tmp == 4 ? 8 : 3; // * 2 !
	modedb[0].pxclk = ((100000000 * pxclkdiv * vclkdiv) >> 1) / fi;

	if (verbosity > 0)
		output("detected startup mode: "
		       "fbset -g %d %d %d ??? %d -t %d %d %d %d %d %d %d\n",
		       modedb[0].xres, modedb[0].yres, modedb[0].xres,
		       modedb[0].bpp, modedb[0].pxclk, modedb[0].left_margin,
		       modedb[0].right_margin, modedb[0].upper_margin,
		       modedb[0].lower_margin, modedb[0].hsync_len,
		       modedb[0].vsync_len);

	//
	// We use this goto target in case of a failed check_var. No, I really
	// do not want to do it in another way!
	//

      tryagain:

	i = (mode == NULL) ? 0 :
	    !strncmp(mode, "640x480", 7) ? 1 :
	    !strncmp(mode, "800x600", 7) ? 2 :
	    !strncmp(mode, "1024x768", 8) ? 3 :
	    !strncmp(mode, "1280x1024", 9) ? 4 : 0;

	ref = (ref < 50) ? 50 : (ref > 85) ? 85 : ref;

	if (i == 0) {
		info->var.pixclock = modedb[i].pxclk;
		info->var.bits_per_pixel = modedb[i].bpp;
	} else {
		info->var.pixclock = (100000000 /
				      ((modedb[i].left_margin +
					modedb[i].xres +
					modedb[i].right_margin +
					modedb[i].hsync_len) *
				       (modedb[i].upper_margin +
					modedb[i].yres +
					modedb[i].lower_margin +
					modedb[i].vsync_len) * ref / 10000));
		info->var.bits_per_pixel = bpp;
	}

	info->var.left_margin = modedb[i].left_margin;
	info->var.right_margin = modedb[i].right_margin;
	info->var.xres = modedb[i].xres;
	if (!(modedb[i].yres == 1280 && modedb[i].bpp == 32))
		info->var.xres_virtual = modedb[i].vxres;
	else
		info->var.xres_virtual = modedb[i].xres;
	info->var.xoffset = 0;
	info->var.hsync_len = modedb[i].hsync_len;
	info->var.upper_margin = modedb[i].upper_margin;
	info->var.yres = modedb[i].yres;
	info->var.yres_virtual = modedb[i].vyres;
	info->var.yoffset = 0;
	info->var.lower_margin = modedb[i].lower_margin;
	info->var.vsync_len = modedb[i].vsync_len;
	info->var.sync = 0;
	info->var.vmode = FB_VMODE_NONINTERLACED;

	if (cyblafb_check_var(&info->var, info)) {
		// 640x480 - 8@75 should really never fail. One case would
		// be fp == 1 and nativex < 640 ... give up then
		if (i == 1 && bpp == 8 && ref == 75) {
			output("Can't find a valid mode :-(\n");
			return -EINVAL;
		}
		// Our detected mode is unlikely to fail. If it does,
		// try 640x480 - 8@75 ...
		if (i == 0) {
			mode = "640x480";
			bpp = 8;
			ref = 75;
			output("Detected mode failed check_var! "
			       "Trying 640x480 - 8@75\n");
			goto tryagain;
		}
		// A specified video mode failed for some reason.
		// Try the startup mode first
		output("Specified mode '%s' failed check! "
		       "Falling back to startup mode.\n", mode);
		mode = NULL;
		goto tryagain;
	}

	return 0;
}

//========================================================
//
// Detect activated memory size. Undefined values require
// memsize parameter.
//
//========================================================

static unsigned int __devinit get_memsize(void)
{
	unsigned char tmp;
	unsigned int k;

	if (memsize)
		k = memsize * Kb;
	else {
		tmp = read3X4(CR1F) & 0x0F;
		switch (tmp) {
		case 0x03:
			k = 1 * 1024 * 1024;
			break;
		case 0x07:
			k = 2 * 1024 * 1024;
			break;
		case 0x0F:
			k = 4 * 1024 * 1024;
			break;
		case 0x04:
			k = 8 * 1024 * 1024;
			break;
		default:
			k = 1 * 1024 * 1024;
			output("Unknown memory size code %x in CR1F."
			       " We default to 1 Mb for now, please"
			       " do provide a memsize parameter!\n", tmp);
		}
	}

	if (verbosity > 0)
		output("framebuffer size = %d Kb\n", k / Kb);
	return k;
}

//=========================================================
//
// Detect if a flat panel monitor connected to the special
// interface is active. Override is possible by fp and crt
// parameters.
//
//=========================================================

static unsigned int __devinit get_displaytype(void)
{
	if (fp)
		return DISPLAY_FP;
	if (crt)
		return DISPLAY_CRT;
	return (read3CE(GR33) & 0x10) ? DISPLAY_FP : DISPLAY_CRT;
}

//=====================================
//
// Get native resolution of flat panel
//
//=====================================

static int __devinit get_nativex(void)
{
	int x, y, tmp;

	if (nativex)
		return nativex;

	tmp = (read3CE(GR52) >> 4) & 3;

	switch (tmp) {
	case 0: x = 1280; y = 1024;
		break;
	case 2: x = 1024; y = 768;
		break;
	case 3: x = 800;  y = 600;
		break;
	case 4: x = 1400; y = 1050;
		break;
	case 1:
	default:
		x = 640; y = 480;
		break;
	}

	if (verbosity > 0)
		output("%dx%d flat panel found\n", x, y);
	return x;
}

static int __devinit cybla_pci_probe(struct pci_dev *dev,
				     const struct pci_device_id *id)
{
	struct fb_info *info;
	struct cyblafb_par *par;

	info = framebuffer_alloc(sizeof(struct cyblafb_par), &dev->dev);
	if (!info)
		goto errout_alloc_info;

	info->pixmap.addr = kzalloc(CYBLAFB_PIXMAPSIZE, GFP_KERNEL);
	if (!info->pixmap.addr) {
		output("allocation of pixmap buffer failed!\n");
		goto errout_alloc_pixmap;
	}
	info->pixmap.size = CYBLAFB_PIXMAPSIZE - 4;
	info->pixmap.buf_align = 4;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	info->pixmap.scan_align = 4;

	par = info->par;
	par->ops = cyblafb_ops;

	info->fix = cyblafb_fix;
	info->fbops = &par->ops;
	info->fix = cyblafb_fix;

	if (pci_enable_device(dev)) {
		output("could not enable device!\n");
		goto errout_enable;
	}
	// might already be requested by vga console or vesafb,
	// so we do care about success
	if (!request_region(0x3c0, 0x20, "cyblafb")) {
		output("region 0x3c0/0x20 already reserved\n");
		vesafb |= 1;

	}
	//
	// Graphics Engine Registers
	//
	if (!request_region(GEBase, 0x100, "cyblafb")) {
		output("region %#x/0x100 already reserved\n", GEBase);
		vesafb |= 2;
	}

	regdump(par);

	enable_mmio();

	// setup MMIO region
	info->fix.mmio_start = pci_resource_start(dev, 1);
	info->fix.mmio_len = 0x20000;

	if (!request_mem_region(info->fix.mmio_start,
				info->fix.mmio_len, "cyblafb")) {
		output("request_mem_region failed for mmio region!\n");
		goto errout_mmio_reqmem;
	}

	io_virt = ioremap_nocache(info->fix.mmio_start, info->fix.mmio_len);

	if (!io_virt) {
		output("ioremap failed for mmio region\n");
		goto errout_mmio_remap;
	}
	// setup framebuffer memory ... might already be requested
	// by vesafb. Not to fail in case of an unsuccessful request
	// is useful if both are loaded.
	info->fix.smem_start = pci_resource_start(dev, 0);
	info->fix.smem_len = get_memsize();

	if (!request_mem_region(info->fix.smem_start,
				info->fix.smem_len, "cyblafb")) {
		output("region %#lx/%#x already reserved\n",
		       info->fix.smem_start, info->fix.smem_len);
		vesafb |= 4;
	}

	info->screen_base = ioremap_nocache(info->fix.smem_start,
					    info->fix.smem_len);

	if (!info->screen_base) {
		output("ioremap failed for smem region\n");
		goto errout_smem_remap;
	}

	displaytype = get_displaytype();

	if (displaytype == DISPLAY_FP)
		nativex = get_nativex();

	info->flags = FBINFO_DEFAULT
		    | FBINFO_HWACCEL_COPYAREA
		    | FBINFO_HWACCEL_FILLRECT
		    | FBINFO_HWACCEL_IMAGEBLIT
		    | FBINFO_READS_FAST
//		    | FBINFO_PARTIAL_PAN_OK
		    | FBINFO_MISC_ALWAYS_SETPAR;

	info->pseudo_palette = par->pseudo_pal;

	if (getstartupmode(info))
		goto errout_findmode;

	fb_alloc_cmap(&info->cmap, 256, 0);

	if (register_framebuffer(info)) {
		output("Could not register CyBla framebuffer\n");
		goto errout_register;
	}

	pci_set_drvdata(dev, info);

	//
	// normal exit and error paths
	//

	return 0;

      errout_register:
      errout_findmode:
	iounmap(info->screen_base);
      errout_smem_remap:
	if (!(vesafb & 4))
		release_mem_region(info->fix.smem_start, info->fix.smem_len);
	iounmap(io_virt);
      errout_mmio_remap:
	release_mem_region(info->fix.mmio_start, info->fix.mmio_len);
      errout_mmio_reqmem:
	if (!(vesafb & 1))
		release_region(0x3c0, 32);
      errout_enable:
	kfree(info->pixmap.addr);
      errout_alloc_pixmap:
	framebuffer_release(info);
      errout_alloc_info:
	output("CyblaFB version %s aborting init.\n", VERSION);
	return -ENODEV;
}

static void __devexit cybla_pci_remove(struct pci_dev *dev)
{
	struct fb_info *info = pci_get_drvdata(dev);

	unregister_framebuffer(info);
	iounmap(io_virt);
	iounmap(info->screen_base);
	if (!(vesafb & 4))
		release_mem_region(info->fix.smem_start, info->fix.smem_len);
	release_mem_region(info->fix.mmio_start, info->fix.mmio_len);
	fb_dealloc_cmap(&info->cmap);
	if (!(vesafb & 2))
		release_region(GEBase, 0x100);
	if (!(vesafb & 1))
		release_region(0x3c0, 32);
	kfree(info->pixmap.addr);
	framebuffer_release(info);
	output("CyblaFB version %s normal exit.\n", VERSION);
}

//
// List of boards that we are trying to support
//
static struct pci_device_id cybla_devices[] = {
	{PCI_VENDOR_ID_TRIDENT, CYBERBLADEi1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci, cybla_devices);

static struct pci_driver cyblafb_pci_driver = {
	.name = "cyblafb",
	.id_table = cybla_devices,
	.probe = cybla_pci_probe,
	.remove = __devexit_p(cybla_pci_remove)
};

//=============================================================
//
// kernel command line example:
//
//	video=cyblafb:1280x1024, bpp=16, ref=50 ...
//
// modprobe command line example:
//
//	modprobe cyblafb mode=1280x1024 bpp=16 ref=50 ...
//
//=============================================================

static int __devinit cyblafb_init(void)
{
#ifndef MODULE
	char *options = NULL;
	char *opt;

	if (fb_get_options("cyblafb", &options))
		return -ENODEV;

	if (options && *options)
		while ((opt = strsep(&options, ",")) != NULL) {
			if (!*opt)
				continue;
			else if (!strncmp(opt, "bpp=", 4))
				bpp = simple_strtoul(opt + 4, NULL, 0);
			else if (!strncmp(opt, "ref=", 4))
				ref = simple_strtoul(opt + 4, NULL, 0);
			else if (!strncmp(opt, "fp", 2))
				displaytype = DISPLAY_FP;
			else if (!strncmp(opt, "crt", 3))
				displaytype = DISPLAY_CRT;
			else if (!strncmp(opt, "nativex=", 8))
				nativex = simple_strtoul(opt + 8, NULL, 0);
			else if (!strncmp(opt, "center", 6))
				center = 1;
			else if (!strncmp(opt, "stretch", 7))
				stretch = 1;
			else if (!strncmp(opt, "pciwb=", 6))
				pciwb = simple_strtoul(opt + 6, NULL, 0);
			else if (!strncmp(opt, "pcirb=", 6))
				pcirb = simple_strtoul(opt + 6, NULL, 0);
			else if (!strncmp(opt, "pciwr=", 6))
				pciwr = simple_strtoul(opt + 6, NULL, 0);
			else if (!strncmp(opt, "pcirr=", 6))
				pcirr = simple_strtoul(opt + 6, NULL, 0);
			else if (!strncmp(opt, "memsize=", 8))
				memsize = simple_strtoul(opt + 8, NULL, 0);
			else if (!strncmp(opt, "verbosity=", 10))
				verbosity = simple_strtoul(opt + 10, NULL, 0);
			else
				mode = opt;
		}
#endif
	output("CyblaFB version %s initializing\n", VERSION);
	return pci_register_driver(&cyblafb_pci_driver);
}

static void __exit cyblafb_exit(void)
{
	pci_unregister_driver(&cyblafb_pci_driver);
}

module_init(cyblafb_init);
module_exit(cyblafb_exit);

MODULE_AUTHOR("Knut Petersen <knut_petersen@t-online.de>");
MODULE_DESCRIPTION("Framebuffer driver for Cyberblade/i1 graphics core");
MODULE_LICENSE("GPL");
