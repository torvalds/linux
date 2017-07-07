/*
 * linux/drivers/video/vga16.c -- VGA 16-color framebuffer driver
 * 
 * Copyright 1999 Ben Pfaff <pfaffben@debian.org> and Petr Vandrovec <VANDROVE@vc.cvut.cz>
 * Based on VGA info at http://www.goodnet.com/~tinara/FreeVGA/home.htm
 * Based on VESA framebuffer (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.  
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>

#include <asm/io.h>
#include <video/vga.h>

#define VGA_FB_PHYS 0xA0000
#define VGA_FB_PHYS_LEN 65536

#define MODE_SKIP4	1
#define MODE_8BPP	2
#define MODE_CFB	4
#define MODE_TEXT	8

/* --------------------------------------------------------------------- */

/*
 * card parameters
 */

struct vga16fb_par {
	/* structure holding original VGA register settings when the
           screen is blanked */
	struct {
		unsigned char	SeqCtrlIndex;	  /* Sequencer Index reg.   */
		unsigned char	CrtCtrlIndex;	  /* CRT-Contr. Index reg.  */
		unsigned char	CrtMiscIO;	  /* Miscellaneous register */
		unsigned char	HorizontalTotal;  /* CRT-Controller:00h */
		unsigned char	HorizDisplayEnd;  /* CRT-Controller:01h */
		unsigned char	StartHorizRetrace;/* CRT-Controller:04h */
		unsigned char	EndHorizRetrace;  /* CRT-Controller:05h */
		unsigned char	Overflow;	  /* CRT-Controller:07h */
		unsigned char	StartVertRetrace; /* CRT-Controller:10h */
		unsigned char	EndVertRetrace;	  /* CRT-Controller:11h */
		unsigned char	ModeControl;	  /* CRT-Controller:17h */
		unsigned char	ClockingMode;	  /* Seq-Controller:01h */
	} vga_state;
	struct vgastate state;
	unsigned int ref_count;
	int palette_blanked, vesa_blanked, mode, isVGA;
	u8 misc, pel_msk, vss, clkdiv;
	u8 crtc[VGA_CRT_C];
};

/* --------------------------------------------------------------------- */

static struct fb_var_screeninfo vga16fb_defined = {
	.xres		= 640,
	.yres		= 480,
	.xres_virtual	= 640,
	.yres_virtual	= 480,
	.bits_per_pixel	= 4,	
	.activate	= FB_ACTIVATE_TEST,
	.height		= -1,
	.width		= -1,
	.pixclock	= 39721,
	.left_margin	= 48,
	.right_margin	= 16,
	.upper_margin	= 33,
	.lower_margin	= 10,
	.hsync_len 	= 96,
	.vsync_len	= 2,
	.vmode		= FB_VMODE_NONINTERLACED,
};

/* name should not depend on EGA/VGA */
static const struct fb_fix_screeninfo vga16fb_fix = {
	.id		= "VGA16 VGA",
	.smem_start	= VGA_FB_PHYS,
	.smem_len	= VGA_FB_PHYS_LEN,
	.type		= FB_TYPE_VGA_PLANES,
	.type_aux	= FB_AUX_VGA_PLANES_VGA4,
	.visual		= FB_VISUAL_PSEUDOCOLOR,
	.xpanstep	= 8,
	.ypanstep	= 1,
	.line_length	= 640 / 8,
	.accel		= FB_ACCEL_NONE
};

/* The VGA's weird architecture often requires that we read a byte and
   write a byte to the same location.  It doesn't matter *what* byte
   we write, however.  This is because all the action goes on behind
   the scenes in the VGA's 32-bit latch register, and reading and writing
   video memory just invokes latch behavior.

   To avoid race conditions (is this necessary?), reading and writing
   the memory byte should be done with a single instruction.  One
   suitable instruction is the x86 bitwise OR.  The following
   read-modify-write routine should optimize to one such bitwise
   OR. */
static inline void rmw(volatile char __iomem *p)
{
	readb(p);
	writeb(1, p);
}

/* Set the Graphics Mode Register, and return its previous value.
   Bits 0-1 are write mode, bit 3 is read mode. */
static inline int setmode(int mode)
{
	int oldmode;
	
	oldmode = vga_io_rgfx(VGA_GFX_MODE);
	vga_io_w(VGA_GFX_D, mode);
	return oldmode;
}

/* Select the Bit Mask Register and return its value. */
static inline int selectmask(void)
{
	return vga_io_rgfx(VGA_GFX_BIT_MASK);
}

/* Set the value of the Bit Mask Register.  It must already have been
   selected with selectmask(). */
static inline void setmask(int mask)
{
	vga_io_w(VGA_GFX_D, mask);
}

/* Set the Data Rotate Register and return its old value. 
   Bits 0-2 are rotate count, bits 3-4 are logical operation
   (0=NOP, 1=AND, 2=OR, 3=XOR). */
static inline int setop(int op)
{
	int oldop;
	
	oldop = vga_io_rgfx(VGA_GFX_DATA_ROTATE);
	vga_io_w(VGA_GFX_D, op);
	return oldop;
}

/* Set the Enable Set/Reset Register and return its old value.  
   The code here always uses value 0xf for this register. */
static inline int setsr(int sr)
{
	int oldsr;

	oldsr = vga_io_rgfx(VGA_GFX_SR_ENABLE);
	vga_io_w(VGA_GFX_D, sr);
	return oldsr;
}

/* Set the Set/Reset Register and return its old value. */
static inline int setcolor(int color)
{
	int oldcolor;

	oldcolor = vga_io_rgfx(VGA_GFX_SR_VALUE);
	vga_io_w(VGA_GFX_D, color);
	return oldcolor;
}

/* Return the value in the Graphics Address Register. */
static inline int getindex(void)
{
	return vga_io_r(VGA_GFX_I);
}

/* Set the value in the Graphics Address Register. */
static inline void setindex(int index)
{
	vga_io_w(VGA_GFX_I, index);
}

static void vga16fb_pan_var(struct fb_info *info, 
			    struct fb_var_screeninfo *var)
{
	struct vga16fb_par *par = info->par;
	u32 xoffset, pos;

	xoffset = var->xoffset;
	if (info->var.bits_per_pixel == 8) {
		pos = (info->var.xres_virtual * var->yoffset + xoffset) >> 2;
	} else if (par->mode & MODE_TEXT) {
		int fh = 16; // FIXME !!! font height. Fugde for now.
		pos = (info->var.xres_virtual * (var->yoffset / fh) + xoffset) >> 3;
	} else {
		if (info->var.nonstd)
			xoffset--;
		pos = (info->var.xres_virtual * var->yoffset + xoffset) >> 3;
	}
	vga_io_wcrt(VGA_CRTC_START_HI, pos >> 8);
	vga_io_wcrt(VGA_CRTC_START_LO, pos & 0xFF);
	/* if we support CFB4, then we must! support xoffset with pixel
	 * granularity if someone supports xoffset in bit resolution */
	vga_io_r(VGA_IS1_RC);		/* reset flip-flop */
	vga_io_w(VGA_ATT_IW, VGA_ATC_PEL);
	if (info->var.bits_per_pixel == 8)
		vga_io_w(VGA_ATT_IW, (xoffset & 3) << 1);
	else
		vga_io_w(VGA_ATT_IW, xoffset & 7);
	vga_io_r(VGA_IS1_RC);
	vga_io_w(VGA_ATT_IW, 0x20);
}

static void vga16fb_update_fix(struct fb_info *info)
{
	if (info->var.bits_per_pixel == 4) {
		if (info->var.nonstd) {
			info->fix.type = FB_TYPE_PACKED_PIXELS;
			info->fix.line_length = info->var.xres_virtual / 2;
		} else {
			info->fix.type = FB_TYPE_VGA_PLANES;
			info->fix.type_aux = FB_AUX_VGA_PLANES_VGA4;
			info->fix.line_length = info->var.xres_virtual / 8;
		}
	} else if (info->var.bits_per_pixel == 0) {
		info->fix.type = FB_TYPE_TEXT;
		info->fix.type_aux = FB_AUX_TEXT_CGA;
		info->fix.line_length = info->var.xres_virtual / 4;
	} else {	/* 8bpp */
		if (info->var.nonstd) {
			info->fix.type = FB_TYPE_VGA_PLANES;
			info->fix.type_aux = FB_AUX_VGA_PLANES_CFB8;
			info->fix.line_length = info->var.xres_virtual / 4;
		} else {
			info->fix.type = FB_TYPE_PACKED_PIXELS;
			info->fix.line_length = info->var.xres_virtual;
		}
	}
}

static void vga16fb_clock_chip(struct vga16fb_par *par,
			       unsigned int pixclock,
			       const struct fb_info *info,
			       int mul, int div)
{
	static const struct {
		u32 pixclock;
		u8  misc;
		u8  seq_clock_mode;
	} *ptr, *best, vgaclocks[] = {
		{ 79442 /* 12.587 */, 0x00, 0x08},
		{ 70616 /* 14.161 */, 0x04, 0x08},
		{ 39721 /* 25.175 */, 0x00, 0x00},
		{ 35308 /* 28.322 */, 0x04, 0x00},
		{     0 /* bad */,    0x00, 0x00}};
	int err;

	pixclock = (pixclock * mul) / div;
	best = vgaclocks;
	err = pixclock - best->pixclock;
	if (err < 0) err = -err;
	for (ptr = vgaclocks + 1; ptr->pixclock; ptr++) {
		int tmp;

		tmp = pixclock - ptr->pixclock;
		if (tmp < 0) tmp = -tmp;
		if (tmp < err) {
			err = tmp;
			best = ptr;
		}
	}
	par->misc |= best->misc;
	par->clkdiv = best->seq_clock_mode;
	pixclock = (best->pixclock * div) / mul;		
}
			       
#define FAIL(X) return -EINVAL

static int vga16fb_open(struct fb_info *info, int user)
{
	struct vga16fb_par *par = info->par;

	if (!par->ref_count) {
		memset(&par->state, 0, sizeof(struct vgastate));
		par->state.flags = VGA_SAVE_FONTS | VGA_SAVE_MODE |
			VGA_SAVE_CMAP;
		save_vga(&par->state);
	}
	par->ref_count++;

	return 0;
}

static int vga16fb_release(struct fb_info *info, int user)
{
	struct vga16fb_par *par = info->par;

	if (!par->ref_count)
		return -EINVAL;

	if (par->ref_count == 1)
		restore_vga(&par->state);
	par->ref_count--;

	return 0;
}

static int vga16fb_check_var(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct vga16fb_par *par = info->par;
	u32 xres, right, hslen, left, xtotal;
	u32 yres, lower, vslen, upper, ytotal;
	u32 vxres, xoffset, vyres, yoffset;
	u32 pos;
	u8 r7, rMode;
	int shift;
	int mode;
	u32 maxmem;

	par->pel_msk = 0xFF;

	if (var->bits_per_pixel == 4) {
		if (var->nonstd) {
			if (!par->isVGA)
				return -EINVAL;
			shift = 3;
			mode = MODE_SKIP4 | MODE_CFB;
			maxmem = 16384;
			par->pel_msk = 0x0F;
		} else {
			shift = 3;
			mode = 0;
			maxmem = 65536;
		}
	} else if (var->bits_per_pixel == 8) {
		if (!par->isVGA)
			return -EINVAL;	/* no support on EGA */
		shift = 2;
		if (var->nonstd) {
			mode = MODE_8BPP | MODE_CFB;
			maxmem = 65536;
		} else {
			mode = MODE_SKIP4 | MODE_8BPP | MODE_CFB;
			maxmem = 16384;
		}
	} else
		return -EINVAL;

	xres = (var->xres + 7) & ~7;
	vxres = (var->xres_virtual + 0xF) & ~0xF;
	xoffset = (var->xoffset + 7) & ~7;
	left = (var->left_margin + 7) & ~7;
	right = (var->right_margin + 7) & ~7;
	hslen = (var->hsync_len + 7) & ~7;

	if (vxres < xres)
		vxres = xres;
	if (xres + xoffset > vxres)
		xoffset = vxres - xres;

	var->xres = xres;
	var->right_margin = right;
	var->hsync_len = hslen;
	var->left_margin = left;
	var->xres_virtual = vxres;
	var->xoffset = xoffset;

	xres >>= shift;
	right >>= shift;
	hslen >>= shift;
	left >>= shift;
	vxres >>= shift;
	xtotal = xres + right + hslen + left;
	if (xtotal >= 256)
		FAIL("xtotal too big");
	if (hslen > 32)
		FAIL("hslen too big");
	if (right + hslen + left > 64)
		FAIL("hblank too big");
	par->crtc[VGA_CRTC_H_TOTAL] = xtotal - 5;
	par->crtc[VGA_CRTC_H_BLANK_START] = xres - 1;
	par->crtc[VGA_CRTC_H_DISP] = xres - 1;
	pos = xres + right;
	par->crtc[VGA_CRTC_H_SYNC_START] = pos;
	pos += hslen;
	par->crtc[VGA_CRTC_H_SYNC_END] = pos & 0x1F;
	pos += left - 2; /* blank_end + 2 <= total + 5 */
	par->crtc[VGA_CRTC_H_BLANK_END] = (pos & 0x1F) | 0x80;
	if (pos & 0x20)
		par->crtc[VGA_CRTC_H_SYNC_END] |= 0x80;

	yres = var->yres;
	lower = var->lower_margin;
	vslen = var->vsync_len;
	upper = var->upper_margin;
	vyres = var->yres_virtual;
	yoffset = var->yoffset;

	if (yres > vyres)
		vyres = yres;
	if (vxres * vyres > maxmem) {
		vyres = maxmem / vxres;
		if (vyres < yres)
			return -ENOMEM;
	}
	if (yoffset + yres > vyres)
		yoffset = vyres - yres;
	var->yres = yres;
	var->lower_margin = lower;
	var->vsync_len = vslen;
	var->upper_margin = upper;
	var->yres_virtual = vyres;
	var->yoffset = yoffset;

	if (var->vmode & FB_VMODE_DOUBLE) {
		yres <<= 1;
		lower <<= 1;
		vslen <<= 1;
		upper <<= 1;
	}
	ytotal = yres + lower + vslen + upper;
	if (ytotal > 1024) {
		ytotal >>= 1;
		yres >>= 1;
		lower >>= 1;
		vslen >>= 1;
		upper >>= 1;
		rMode = 0x04;
	} else
		rMode = 0x00;
	if (ytotal > 1024)
		FAIL("ytotal too big");
	if (vslen > 16)
		FAIL("vslen too big");
	par->crtc[VGA_CRTC_V_TOTAL] = ytotal - 2;
	r7 = 0x10;	/* disable linecompare */
	if (ytotal & 0x100) r7 |= 0x01;
	if (ytotal & 0x200) r7 |= 0x20;
	par->crtc[VGA_CRTC_PRESET_ROW] = 0;
	par->crtc[VGA_CRTC_MAX_SCAN] = 0x40;	/* 1 scanline, no linecmp */
	if (var->vmode & FB_VMODE_DOUBLE)
		par->crtc[VGA_CRTC_MAX_SCAN] |= 0x80;
	par->crtc[VGA_CRTC_CURSOR_START] = 0x20;
	par->crtc[VGA_CRTC_CURSOR_END]   = 0x00;
	if ((mode & (MODE_CFB | MODE_8BPP)) == MODE_CFB)
		xoffset--;
	pos = yoffset * vxres + (xoffset >> shift);
	par->crtc[VGA_CRTC_START_HI]     = pos >> 8;
	par->crtc[VGA_CRTC_START_LO]     = pos & 0xFF;
	par->crtc[VGA_CRTC_CURSOR_HI]    = 0x00;
	par->crtc[VGA_CRTC_CURSOR_LO]    = 0x00;
	pos = yres - 1;
	par->crtc[VGA_CRTC_V_DISP_END] = pos & 0xFF;
	par->crtc[VGA_CRTC_V_BLANK_START] = pos & 0xFF;
	if (pos & 0x100)
		r7 |= 0x0A;	/* 0x02 -> DISP_END, 0x08 -> BLANK_START */
	if (pos & 0x200) {
		r7 |= 0x40;	/* 0x40 -> DISP_END */
		par->crtc[VGA_CRTC_MAX_SCAN] |= 0x20; /* BLANK_START */
	}
	pos += lower;
	par->crtc[VGA_CRTC_V_SYNC_START] = pos & 0xFF;
	if (pos & 0x100)
		r7 |= 0x04;
	if (pos & 0x200)
		r7 |= 0x80;
	pos += vslen;
	par->crtc[VGA_CRTC_V_SYNC_END] = (pos & 0x0F) & ~0x10; /* disabled IRQ */
	pos += upper - 1; /* blank_end + 1 <= ytotal + 2 */
	par->crtc[VGA_CRTC_V_BLANK_END] = pos & 0xFF; /* 0x7F for original VGA,
                     but some SVGA chips requires all 8 bits to set */
	if (vxres >= 512)
		FAIL("vxres too long");
	par->crtc[VGA_CRTC_OFFSET] = vxres >> 1;
	if (mode & MODE_SKIP4)
		par->crtc[VGA_CRTC_UNDERLINE] = 0x5F;	/* 256, cfb8 */
	else
		par->crtc[VGA_CRTC_UNDERLINE] = 0x1F;	/* 16, vgap */
	par->crtc[VGA_CRTC_MODE] = rMode | ((mode & MODE_TEXT) ? 0xA3 : 0xE3);
	par->crtc[VGA_CRTC_LINE_COMPARE] = 0xFF;
	par->crtc[VGA_CRTC_OVERFLOW] = r7;

	par->vss = 0x00;	/* 3DA */

	par->misc = 0xE3;	/* enable CPU, ports 0x3Dx, positive sync */
	if (var->sync & FB_SYNC_HOR_HIGH_ACT)
		par->misc &= ~0x40;
	if (var->sync & FB_SYNC_VERT_HIGH_ACT)
		par->misc &= ~0x80;
	
	par->mode = mode;

	if (mode & MODE_8BPP)
		/* pixel clock == vga clock / 2 */
		vga16fb_clock_chip(par, var->pixclock, info, 1, 2);
	else
		/* pixel clock == vga clock */
		vga16fb_clock_chip(par, var->pixclock, info, 1, 1);
	
	var->red.offset = var->green.offset = var->blue.offset = 
	var->transp.offset = 0;
	var->red.length = var->green.length = var->blue.length =
		(par->isVGA) ? 6 : 2;
	var->transp.length = 0;
	var->activate = FB_ACTIVATE_NOW;
	var->height = -1;
	var->width = -1;
	var->accel_flags = 0;
	return 0;
}
#undef FAIL

static int vga16fb_set_par(struct fb_info *info)
{
	struct vga16fb_par *par = info->par;
	u8 gdc[VGA_GFX_C];
	u8 seq[VGA_SEQ_C];
	u8 atc[VGA_ATT_C];
	int fh, i;

	seq[VGA_SEQ_CLOCK_MODE] = 0x01 | par->clkdiv;
	if (par->mode & MODE_TEXT)
		seq[VGA_SEQ_PLANE_WRITE] = 0x03;
	else
		seq[VGA_SEQ_PLANE_WRITE] = 0x0F;
	seq[VGA_SEQ_CHARACTER_MAP] = 0x00;
	if (par->mode & MODE_TEXT)
		seq[VGA_SEQ_MEMORY_MODE] = 0x03;
	else if (par->mode & MODE_SKIP4)
		seq[VGA_SEQ_MEMORY_MODE] = 0x0E;
	else
		seq[VGA_SEQ_MEMORY_MODE] = 0x06;

	gdc[VGA_GFX_SR_VALUE] = 0x00;
	gdc[VGA_GFX_SR_ENABLE] = 0x00;
	gdc[VGA_GFX_COMPARE_VALUE] = 0x00;
	gdc[VGA_GFX_DATA_ROTATE] = 0x00;
	gdc[VGA_GFX_PLANE_READ] = 0;
	if (par->mode & MODE_TEXT) {
		gdc[VGA_GFX_MODE] = 0x10;
		gdc[VGA_GFX_MISC] = 0x06;
	} else {
		if (par->mode & MODE_CFB)
			gdc[VGA_GFX_MODE] = 0x40;
		else
			gdc[VGA_GFX_MODE] = 0x00;
		gdc[VGA_GFX_MISC] = 0x05;
	}
	gdc[VGA_GFX_COMPARE_MASK] = 0x0F;
	gdc[VGA_GFX_BIT_MASK] = 0xFF;

	for (i = 0x00; i < 0x10; i++)
		atc[i] = i;
	if (par->mode & MODE_TEXT)
		atc[VGA_ATC_MODE] = 0x04;
	else if (par->mode & MODE_8BPP)
		atc[VGA_ATC_MODE] = 0x41;
	else
		atc[VGA_ATC_MODE] = 0x81;
	atc[VGA_ATC_OVERSCAN] = 0x00;	/* 0 for EGA, 0xFF for VGA */
	atc[VGA_ATC_PLANE_ENABLE] = 0x0F;
	if (par->mode & MODE_8BPP)
		atc[VGA_ATC_PEL] = (info->var.xoffset & 3) << 1;
	else
		atc[VGA_ATC_PEL] = info->var.xoffset & 7;
	atc[VGA_ATC_COLOR_PAGE] = 0x00;
	
	if (par->mode & MODE_TEXT) {
		fh = 16; // FIXME !!! Fudge font height. 
		par->crtc[VGA_CRTC_MAX_SCAN] = (par->crtc[VGA_CRTC_MAX_SCAN] 
					       & ~0x1F) | (fh - 1);
	}

	vga_io_w(VGA_MIS_W, vga_io_r(VGA_MIS_R) | 0x01);

	/* Enable graphics register modification */
	if (!par->isVGA) {
		vga_io_w(EGA_GFX_E0, 0x00);
		vga_io_w(EGA_GFX_E1, 0x01);
	}
	
	/* update misc output register */
	vga_io_w(VGA_MIS_W, par->misc);
	
	/* synchronous reset on */
	vga_io_wseq(0x00, 0x01);

	if (par->isVGA)
		vga_io_w(VGA_PEL_MSK, par->pel_msk);

	/* write sequencer registers */
	vga_io_wseq(VGA_SEQ_CLOCK_MODE, seq[VGA_SEQ_CLOCK_MODE] | 0x20);
	for (i = 2; i < VGA_SEQ_C; i++) {
		vga_io_wseq(i, seq[i]);
	}
	
	/* synchronous reset off */
	vga_io_wseq(0x00, 0x03);

	/* deprotect CRT registers 0-7 */
	vga_io_wcrt(VGA_CRTC_V_SYNC_END, par->crtc[VGA_CRTC_V_SYNC_END]);

	/* write CRT registers */
	for (i = 0; i < VGA_CRTC_REGS; i++) {
		vga_io_wcrt(i, par->crtc[i]);
	}
	
	/* write graphics controller registers */
	for (i = 0; i < VGA_GFX_C; i++) {
		vga_io_wgfx(i, gdc[i]);
	}
	
	/* write attribute controller registers */
	for (i = 0; i < VGA_ATT_C; i++) {
		vga_io_r(VGA_IS1_RC);		/* reset flip-flop */
		vga_io_wattr(i, atc[i]);
	}

	/* Wait for screen to stabilize. */
	mdelay(50);

	vga_io_wseq(VGA_SEQ_CLOCK_MODE, seq[VGA_SEQ_CLOCK_MODE]);

	vga_io_r(VGA_IS1_RC);
	vga_io_w(VGA_ATT_IW, 0x20);

	vga16fb_update_fix(info);
	return 0;
}

static void ega16_setpalette(int regno, unsigned red, unsigned green, unsigned blue)
{
	static const unsigned char map[] = { 000, 001, 010, 011 };
	int val;
	
	if (regno >= 16)
		return;
	val = map[red>>14] | ((map[green>>14]) << 1) | ((map[blue>>14]) << 2);
	vga_io_r(VGA_IS1_RC);   /* ! 0x3BA */
	vga_io_wattr(regno, val);
	vga_io_r(VGA_IS1_RC);   /* some clones need it */
	vga_io_w(VGA_ATT_IW, 0x20); /* unblank screen */
}

static void vga16_setpalette(int regno, unsigned red, unsigned green, unsigned blue)
{
	outb(regno,       VGA_PEL_IW);
	outb(red   >> 10, VGA_PEL_D);
	outb(green >> 10, VGA_PEL_D);
	outb(blue  >> 10, VGA_PEL_D);
}

static int vga16fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			     unsigned blue, unsigned transp,
			     struct fb_info *info)
{
	struct vga16fb_par *par = info->par;
	int gray;

	/*
	 *  Set a single color register. The values supplied are
	 *  already rounded down to the hardware's capabilities
	 *  (according to the entries in the `var' structure). Return
	 *  != 0 for invalid regno.
	 */
	
	if (regno >= 256)
		return 1;

	gray = info->var.grayscale;
	
	if (gray) {
		/* gray = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}
	if (par->isVGA) 
		vga16_setpalette(regno,red,green,blue);
	else
		ega16_setpalette(regno,red,green,blue);
	return 0;
}

static int vga16fb_pan_display(struct fb_var_screeninfo *var,
			       struct fb_info *info) 
{
	vga16fb_pan_var(info, var);
	return 0;
}

/* The following VESA blanking code is taken from vgacon.c.  The VGA
   blanking code was originally by Huang shi chao, and modified by
   Christoph Rimek (chrimek@toppoint.de) and todd j. derr
   (tjd@barefoot.org) for Linux. */

static void vga_vesa_blank(struct vga16fb_par *par, int mode)
{
	unsigned char SeqCtrlIndex = vga_io_r(VGA_SEQ_I);
	unsigned char CrtCtrlIndex = vga_io_r(VGA_CRT_IC);
	
	/* save original values of VGA controller registers */
	if(!par->vesa_blanked) {
		par->vga_state.CrtMiscIO = vga_io_r(VGA_MIS_R);
		//sti();

		par->vga_state.HorizontalTotal = vga_io_rcrt(0x00);	/* HorizontalTotal */
		par->vga_state.HorizDisplayEnd = vga_io_rcrt(0x01);	/* HorizDisplayEnd */
		par->vga_state.StartHorizRetrace = vga_io_rcrt(0x04);	/* StartHorizRetrace */
		par->vga_state.EndHorizRetrace = vga_io_rcrt(0x05);	/* EndHorizRetrace */
		par->vga_state.Overflow = vga_io_rcrt(0x07);		/* Overflow */
		par->vga_state.StartVertRetrace = vga_io_rcrt(0x10);	/* StartVertRetrace */
		par->vga_state.EndVertRetrace = vga_io_rcrt(0x11);	/* EndVertRetrace */
		par->vga_state.ModeControl = vga_io_rcrt(0x17);	/* ModeControl */
		par->vga_state.ClockingMode = vga_io_rseq(0x01);	/* ClockingMode */
	}

	/* assure that video is enabled */
	/* "0x20" is VIDEO_ENABLE_bit in register 01 of sequencer */
	vga_io_wseq(0x01, par->vga_state.ClockingMode | 0x20);

	/* test for vertical retrace in process.... */
	if ((par->vga_state.CrtMiscIO & 0x80) == 0x80)
		vga_io_w(VGA_MIS_W, par->vga_state.CrtMiscIO & 0xef);

	/*
	 * Set <End of vertical retrace> to minimum (0) and
	 * <Start of vertical Retrace> to maximum (incl. overflow)
	 * Result: turn off vertical sync (VSync) pulse.
	 */
	if (mode & FB_BLANK_VSYNC_SUSPEND) {
		vga_io_wcrt(VGA_CRTC_V_SYNC_START, 0xff);
		vga_io_wcrt(VGA_CRTC_V_SYNC_END, 0x40);
		/* bits 9,10 of vert. retrace */
		vga_io_wcrt(VGA_CRTC_OVERFLOW, par->vga_state.Overflow | 0x84);
	}

	if (mode & FB_BLANK_HSYNC_SUSPEND) {
		/*
		 * Set <End of horizontal retrace> to minimum (0) and
		 *  <Start of horizontal Retrace> to maximum
		 * Result: turn off horizontal sync (HSync) pulse.
		 */
		vga_io_wcrt(VGA_CRTC_H_SYNC_START, 0xff);
		vga_io_wcrt(VGA_CRTC_H_SYNC_END, 0x00);
	}

	/* restore both index registers */
	outb_p(SeqCtrlIndex, VGA_SEQ_I);
	outb_p(CrtCtrlIndex, VGA_CRT_IC);
}

static void vga_vesa_unblank(struct vga16fb_par *par)
{
	unsigned char SeqCtrlIndex = vga_io_r(VGA_SEQ_I);
	unsigned char CrtCtrlIndex = vga_io_r(VGA_CRT_IC);
	
	/* restore original values of VGA controller registers */
	vga_io_w(VGA_MIS_W, par->vga_state.CrtMiscIO);

	/* HorizontalTotal */
	vga_io_wcrt(0x00, par->vga_state.HorizontalTotal);
	/* HorizDisplayEnd */
	vga_io_wcrt(0x01, par->vga_state.HorizDisplayEnd);
	/* StartHorizRetrace */
	vga_io_wcrt(0x04, par->vga_state.StartHorizRetrace);
	/* EndHorizRetrace */
	vga_io_wcrt(0x05, par->vga_state.EndHorizRetrace);
	/* Overflow */
	vga_io_wcrt(0x07, par->vga_state.Overflow);
	/* StartVertRetrace */
	vga_io_wcrt(0x10, par->vga_state.StartVertRetrace);
	/* EndVertRetrace */
	vga_io_wcrt(0x11, par->vga_state.EndVertRetrace);
	/* ModeControl */
	vga_io_wcrt(0x17, par->vga_state.ModeControl);
	/* ClockingMode */
	vga_io_wseq(0x01, par->vga_state.ClockingMode);

	/* restore index/control registers */
	vga_io_w(VGA_SEQ_I, SeqCtrlIndex);
	vga_io_w(VGA_CRT_IC, CrtCtrlIndex);
}

static void vga_pal_blank(void)
{
	int i;

	for (i=0; i<16; i++) {
		outb_p(i, VGA_PEL_IW);
		outb_p(0, VGA_PEL_D);
		outb_p(0, VGA_PEL_D);
		outb_p(0, VGA_PEL_D);
	}
}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */
static int vga16fb_blank(int blank, struct fb_info *info)
{
	struct vga16fb_par *par = info->par;

	switch (blank) {
	case FB_BLANK_UNBLANK:				/* Unblank */
		if (par->vesa_blanked) {
			vga_vesa_unblank(par);
			par->vesa_blanked = 0;
		}
		if (par->palette_blanked) {
			par->palette_blanked = 0;
		}
		break;
	case FB_BLANK_NORMAL:				/* blank */
		vga_pal_blank();
		par->palette_blanked = 1;
		break;
	default:			/* VESA blanking */
		vga_vesa_blank(par, blank);
		par->vesa_blanked = 1;
		break;
	}
	return 0;
}

static void vga_8planes_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	u32 dx = rect->dx, width = rect->width;
        char oldindex = getindex();
        char oldmode = setmode(0x40);
        char oldmask = selectmask();
        int line_ofs, height;
        char oldop, oldsr;
        char __iomem *where;

        dx /= 4;
        where = info->screen_base + dx + rect->dy * info->fix.line_length;

        if (rect->rop == ROP_COPY) {
                oldop = setop(0);
                oldsr = setsr(0);

                width /= 4;
                line_ofs = info->fix.line_length - width;
                setmask(0xff);

                height = rect->height;

                while (height--) {
                        int x;

                        /* we can do memset... */
                        for (x = width; x > 0; --x) {
                                writeb(rect->color, where);
                                where++;
                        }
                        where += line_ofs;
                }
        } else {
                char oldcolor = setcolor(0xf);
                int y;

                oldop = setop(0x18);
                oldsr = setsr(0xf);
                setmask(0x0F);
                for (y = 0; y < rect->height; y++) {
                        rmw(where);
                        rmw(where+1);
                        where += info->fix.line_length;
                }
                setcolor(oldcolor);
        }
        setmask(oldmask);
        setsr(oldsr);
        setop(oldop);
        setmode(oldmode);
        setindex(oldindex);
}

static void vga16fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	int x, x2, y2, vxres, vyres, width, height, line_ofs;
	char __iomem *dst;

	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;

	if (!rect->width || !rect->height || rect->dx > vxres || rect->dy > vyres)
		return;

	/* We could use hardware clipping but on many cards you get around
	 * hardware clipping by writing to framebuffer directly. */

	x2 = rect->dx + rect->width;
	y2 = rect->dy + rect->height;
	x2 = x2 < vxres ? x2 : vxres;
	y2 = y2 < vyres ? y2 : vyres;
	width = x2 - rect->dx;

	switch (info->fix.type) {
	case FB_TYPE_VGA_PLANES:
		if (info->fix.type_aux == FB_AUX_VGA_PLANES_VGA4) {

			height = y2 - rect->dy;
			width = rect->width/8;

			line_ofs = info->fix.line_length - width;
			dst = info->screen_base + (rect->dx/8) + rect->dy * info->fix.line_length;

			switch (rect->rop) {
			case ROP_COPY:
				setmode(0);
				setop(0);
				setsr(0xf);
				setcolor(rect->color);
				selectmask();

				setmask(0xff);

				while (height--) {
					for (x = 0; x < width; x++) {
						writeb(0, dst);
						dst++;
					}
					dst += line_ofs;
				}
				break;
			case ROP_XOR:
				setmode(0);
				setop(0x18);
				setsr(0xf);
				setcolor(0xf);
				selectmask();

				setmask(0xff);
				while (height--) {
					for (x = 0; x < width; x++) {
						rmw(dst);
						dst++;
					}
					dst += line_ofs;
				}
				break;
			}
		} else 
			vga_8planes_fillrect(info, rect);
		break;
	case FB_TYPE_PACKED_PIXELS:
	default:
		cfb_fillrect(info, rect);
		break;
	}
}

static void vga_8planes_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
        char oldindex = getindex();
        char oldmode = setmode(0x41);
        char oldop = setop(0);
        char oldsr = setsr(0xf);
        int height, line_ofs, x;
	u32 sx, dx, width;
	char __iomem *dest;
	char __iomem *src;

        height = area->height;

        sx = area->sx / 4;
        dx = area->dx / 4;
        width = area->width / 4;

        if (area->dy < area->sy || (area->dy == area->sy && dx < sx)) {
                line_ofs = info->fix.line_length - width;
                dest = info->screen_base + dx + area->dy * info->fix.line_length;
                src = info->screen_base + sx + area->sy * info->fix.line_length;
                while (height--) {
                        for (x = 0; x < width; x++) {
                                readb(src);
                                writeb(0, dest);
                                src++;
                                dest++;
                        }
                        src += line_ofs;
                        dest += line_ofs;
                }
        } else {
                line_ofs = info->fix.line_length - width;
                dest = info->screen_base + dx + width +
			(area->dy + height - 1) * info->fix.line_length;
                src = info->screen_base + sx + width +
			(area->sy + height - 1) * info->fix.line_length;
                while (height--) {
                        for (x = 0; x < width; x++) {
                                --src;
                                --dest;
                                readb(src);
                                writeb(0, dest);
                        }
                        src -= line_ofs;
                        dest -= line_ofs;
                }
        }

        setsr(oldsr);
        setop(oldop);
        setmode(oldmode);
        setindex(oldindex);
}

static void vga16fb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	u32 dx = area->dx, dy = area->dy, sx = area->sx, sy = area->sy; 
	int x, x2, y2, old_dx, old_dy, vxres, vyres;
	int height, width, line_ofs;
	char __iomem *dst = NULL;
	char __iomem *src = NULL;

	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;

	if (area->dx > vxres || area->sx > vxres || area->dy > vyres ||
	    area->sy > vyres)
		return;

	/* clip the destination */
	old_dx = area->dx;
	old_dy = area->dy;

	/*
	 * We could use hardware clipping but on many cards you get around
	 * hardware clipping by writing to framebuffer directly.
	 */
	x2 = area->dx + area->width;
	y2 = area->dy + area->height;
	dx = area->dx > 0 ? area->dx : 0;
	dy = area->dy > 0 ? area->dy : 0;
	x2 = x2 < vxres ? x2 : vxres;
	y2 = y2 < vyres ? y2 : vyres;
	width = x2 - dx;
	height = y2 - dy;

	if (sx + dx < old_dx || sy + dy < old_dy)
		return;

	/* update sx1,sy1 */
	sx += (dx - old_dx);
	sy += (dy - old_dy);

	/* the source must be completely inside the virtual screen */
	if (sx + width > vxres || sy + height > vyres)
		return;

	switch (info->fix.type) {
	case FB_TYPE_VGA_PLANES:
		if (info->fix.type_aux == FB_AUX_VGA_PLANES_VGA4) {
			width = width/8;
			height = height;
			line_ofs = info->fix.line_length - width;

			setmode(1);
			setop(0);
			setsr(0xf);

			if (dy < sy || (dy == sy && dx < sx)) {
				dst = info->screen_base + (dx/8) + dy * info->fix.line_length;
				src = info->screen_base + (sx/8) + sy * info->fix.line_length;
				while (height--) {
					for (x = 0; x < width; x++) {
						readb(src);
						writeb(0, dst);
						dst++;
						src++;
					}
					src += line_ofs;
					dst += line_ofs;
				}
			} else {
				dst = info->screen_base + (dx/8) + width + 
					(dy + height - 1) * info->fix.line_length;
				src = info->screen_base + (sx/8) + width + 
					(sy + height  - 1) * info->fix.line_length;
				while (height--) {
					for (x = 0; x < width; x++) {
						dst--;
						src--;
						readb(src);
						writeb(0, dst);
					}
					src -= line_ofs;
					dst -= line_ofs;
				}
			}
		} else 
			vga_8planes_copyarea(info, area);
		break;
	case FB_TYPE_PACKED_PIXELS:
	default:
		cfb_copyarea(info, area);
		break;
	}
}

#define TRANS_MASK_LOW  {0x0,0x8,0x4,0xC,0x2,0xA,0x6,0xE,0x1,0x9,0x5,0xD,0x3,0xB,0x7,0xF}
#define TRANS_MASK_HIGH {0x000, 0x800, 0x400, 0xC00, 0x200, 0xA00, 0x600, 0xE00, \
			 0x100, 0x900, 0x500, 0xD00, 0x300, 0xB00, 0x700, 0xF00}

#if defined(__LITTLE_ENDIAN)
static const u16 transl_l[] = TRANS_MASK_LOW;
static const u16 transl_h[] = TRANS_MASK_HIGH;
#elif defined(__BIG_ENDIAN)
static const u16 transl_l[] = TRANS_MASK_HIGH;
static const u16 transl_h[] = TRANS_MASK_LOW;
#else
#error "Only __BIG_ENDIAN and __LITTLE_ENDIAN are supported in vga-planes"
#endif

static void vga_8planes_imageblit(struct fb_info *info, const struct fb_image *image)
{
        char oldindex = getindex();
        char oldmode = setmode(0x40);
        char oldop = setop(0);
        char oldsr = setsr(0);
        char oldmask = selectmask();
        const char *cdat = image->data;
	u32 dx = image->dx;
        char __iomem *where;
        int y;

        dx /= 4;
        where = info->screen_base + dx + image->dy * info->fix.line_length;

        setmask(0xff);
        writeb(image->bg_color, where);
        readb(where);
        selectmask();
        setmask(image->fg_color ^ image->bg_color);
        setmode(0x42);
        setop(0x18);
        for (y = 0; y < image->height; y++, where += info->fix.line_length)
                writew(transl_h[cdat[y]&0xF] | transl_l[cdat[y] >> 4], where);
        setmask(oldmask);
        setsr(oldsr);
        setop(oldop);
        setmode(oldmode);
        setindex(oldindex);
}

static void vga_imageblit_expand(struct fb_info *info, const struct fb_image *image)
{
	char __iomem *where = info->screen_base + (image->dx/8) +
		image->dy * info->fix.line_length;
	struct vga16fb_par *par = info->par;
	char *cdat = (char *) image->data;
	char __iomem *dst;
	int x, y;

	switch (info->fix.type) {
	case FB_TYPE_VGA_PLANES:
		if (info->fix.type_aux == FB_AUX_VGA_PLANES_VGA4) {
			if (par->isVGA) {
				setmode(2);
				setop(0);
				setsr(0xf);
				setcolor(image->fg_color);
				selectmask();
				
				setmask(0xff);
				writeb(image->bg_color, where);
				rmb();
				readb(where); /* fill latches */
				setmode(3);
				wmb();
				for (y = 0; y < image->height; y++) {
					dst = where;
					for (x = image->width/8; x--;) 
						writeb(*cdat++, dst++);
					where += info->fix.line_length;
				}
				wmb();
			} else {
				setmode(0);
				setop(0);
				setsr(0xf);
				setcolor(image->bg_color);
				selectmask();
				
				setmask(0xff);
				for (y = 0; y < image->height; y++) {
					dst = where;
					for (x=image->width/8; x--;){
						rmw(dst);
						setcolor(image->fg_color);
						selectmask();
						if (*cdat) {
							setmask(*cdat++);
							rmw(dst++);
						}
					}
					where += info->fix.line_length;
				}
			}
		} else 
			vga_8planes_imageblit(info, image);
		break;
	case FB_TYPE_PACKED_PIXELS:
	default:
		cfb_imageblit(info, image);
		break;
	}
}

static void vga_imageblit_color(struct fb_info *info, const struct fb_image *image)
{
	/*
	 * Draw logo 
	 */
	struct vga16fb_par *par = info->par;
	char __iomem *where =
		info->screen_base + image->dy * info->fix.line_length +
		image->dx/8;
	const char *cdat = image->data;
	char __iomem *dst;
	int x, y;

	switch (info->fix.type) {
	case FB_TYPE_VGA_PLANES:
		if (info->fix.type_aux == FB_AUX_VGA_PLANES_VGA4 &&
		    par->isVGA) {
			setsr(0xf);
			setop(0);
			setmode(0);
			
			for (y = 0; y < image->height; y++) {
				for (x = 0; x < image->width; x++) {
					dst = where + x/8;

					setcolor(*cdat);
					selectmask();
					setmask(1 << (7 - (x % 8)));
					fb_readb(dst);
					fb_writeb(0, dst);

					cdat++;
				}
				where += info->fix.line_length;
			}
		}
		break;
	case FB_TYPE_PACKED_PIXELS:
		cfb_imageblit(info, image);
		break;
	default:
		break;
	}
}
				
static void vga16fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	if (image->depth == 1)
		vga_imageblit_expand(info, image);
	else
		vga_imageblit_color(info, image);
}

static void vga16fb_destroy(struct fb_info *info)
{
	iounmap(info->screen_base);
	fb_dealloc_cmap(&info->cmap);
	/* XXX unshare VGA regions */
	framebuffer_release(info);
}

static struct fb_ops vga16fb_ops = {
	.owner		= THIS_MODULE,
	.fb_open        = vga16fb_open,
	.fb_release     = vga16fb_release,
	.fb_destroy	= vga16fb_destroy,
	.fb_check_var	= vga16fb_check_var,
	.fb_set_par	= vga16fb_set_par,
	.fb_setcolreg 	= vga16fb_setcolreg,
	.fb_pan_display = vga16fb_pan_display,
	.fb_blank 	= vga16fb_blank,
	.fb_fillrect	= vga16fb_fillrect,
	.fb_copyarea	= vga16fb_copyarea,
	.fb_imageblit	= vga16fb_imageblit,
};

#ifndef MODULE
static int __init vga16fb_setup(char *options)
{
	char *this_opt;
	
	if (!options || !*options)
		return 0;
	
	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt) continue;
	}
	return 0;
}
#endif

static int vga16fb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	struct vga16fb_par *par;
	int i;
	int ret = 0;

	printk(KERN_DEBUG "vga16fb: initializing\n");
	info = framebuffer_alloc(sizeof(struct vga16fb_par), &dev->dev);

	if (!info) {
		ret = -ENOMEM;
		goto err_fb_alloc;
	}
	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		ret = -ENOMEM;
		goto err_ioremap;
	}

	/* XXX share VGA_FB_PHYS and I/O region with vgacon and others */
	info->screen_base = (void __iomem *)VGA_MAP_MEM(VGA_FB_PHYS, 0);

	if (!info->screen_base) {
		printk(KERN_ERR "vga16fb: unable to map device\n");
		ret = -ENOMEM;
		goto err_ioremap;
	}

	printk(KERN_INFO "vga16fb: mapped to 0x%p\n", info->screen_base);
	par = info->par;

	par->isVGA = screen_info.orig_video_isVGA;
	par->palette_blanked = 0;
	par->vesa_blanked = 0;

	i = par->isVGA? 6 : 2;
	
	vga16fb_defined.red.length   = i;
	vga16fb_defined.green.length = i;
	vga16fb_defined.blue.length  = i;	

	/* name should not depend on EGA/VGA */
	info->fbops = &vga16fb_ops;
	info->var = vga16fb_defined;
	info->fix = vga16fb_fix;
	/* supports rectangles with widths of multiples of 8 */
	info->pixmap.blit_x = 1 << 7 | 1 << 15 | 1 << 23 | 1 << 31;
	info->flags = FBINFO_FLAG_DEFAULT | FBINFO_MISC_FIRMWARE |
		FBINFO_HWACCEL_YPAN;

	i = (info->var.bits_per_pixel == 8) ? 256 : 16;
	ret = fb_alloc_cmap(&info->cmap, i, 0);
	if (ret) {
		printk(KERN_ERR "vga16fb: unable to allocate colormap\n");
		ret = -ENOMEM;
		goto err_alloc_cmap;
	}

	if (vga16fb_check_var(&info->var, info)) {
		printk(KERN_ERR "vga16fb: unable to validate variable\n");
		ret = -EINVAL;
		goto err_check_var;
	}

	vga16fb_update_fix(info);

	info->apertures->ranges[0].base = VGA_FB_PHYS;
	info->apertures->ranges[0].size = VGA_FB_PHYS_LEN;

	if (register_framebuffer(info) < 0) {
		printk(KERN_ERR "vga16fb: unable to register framebuffer\n");
		ret = -EINVAL;
		goto err_check_var;
	}

	fb_info(info, "%s frame buffer device\n", info->fix.id);
	platform_set_drvdata(dev, info);

	return 0;

 err_check_var:
	fb_dealloc_cmap(&info->cmap);
 err_alloc_cmap:
	iounmap(info->screen_base);
 err_ioremap:
	framebuffer_release(info);
 err_fb_alloc:
	return ret;
}

static int vga16fb_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);

	if (info)
		unregister_framebuffer(info);

	return 0;
}

static struct platform_driver vga16fb_driver = {
	.probe = vga16fb_probe,
	.remove = vga16fb_remove,
	.driver = {
		.name = "vga16fb",
	},
};

static struct platform_device *vga16fb_device;

static int __init vga16fb_init(void)
{
	int ret;
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("vga16fb", &option))
		return -ENODEV;

	vga16fb_setup(option);
#endif
	ret = platform_driver_register(&vga16fb_driver);

	if (!ret) {
		vga16fb_device = platform_device_alloc("vga16fb", 0);

		if (vga16fb_device)
			ret = platform_device_add(vga16fb_device);
		else
			ret = -ENOMEM;

		if (ret) {
			platform_device_put(vga16fb_device);
			platform_driver_unregister(&vga16fb_driver);
		}
	}

	return ret;
}

static void __exit vga16fb_exit(void)
{
	platform_device_unregister(vga16fb_device);
	platform_driver_unregister(&vga16fb_driver);
}

MODULE_DESCRIPTION("Legacy VGA framebuffer device driver");
MODULE_LICENSE("GPL");
module_init(vga16fb_init);
module_exit(vga16fb_exit);


/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

