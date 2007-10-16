/*
 *  linux/drivers/video/pm3fb.c -- 3DLabs Permedia3 frame buffer device
 *
 *  Copyright (C) 2001 Romain Dolbeau <romain@dolbeau.org>.
 *
 *  Ported to 2.6 kernel on 1 May 2007 by Krzysztof Helt <krzysztof.h1@wp.pl>
 *	based on pm2fb.c
 *
 *  Based on code written by:
 *	   Sven Luther, <luther@dpt-info.u-strasbg.fr>
 *	   Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *	   Russell King, <rmk@arm.linux.org.uk>
 *  Based on linux/drivers/video/skeletonfb.c:
 *	Copyright (C) 1997 Geert Uytterhoeven
 *  Based on linux/driver/video/pm2fb.c:
 *	Copyright (C) 1998-1999 Ilario Nardinocchi (nardinoc@CS.UniBO.IT)
 *	Copyright (C) 1999 Jakub Jelinek (jakub@redhat.com)
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <video/pm3fb.h>

#if !defined(CONFIG_PCI)
#error "Only generic PCI cards supported."
#endif

#undef PM3FB_MASTER_DEBUG
#ifdef PM3FB_MASTER_DEBUG
#define DPRINTK(a,b...)	printk(KERN_DEBUG "pm3fb: %s: " a, __FUNCTION__ , ## b)
#else
#define DPRINTK(a,b...)
#endif

/*
 * Driver data
 */
static char *mode_option __devinitdata;

/*
 * This structure defines the hardware state of the graphics card. Normally
 * you place this in a header file in linux/include/video. This file usually
 * also includes register information. That allows other driver subsystems
 * and userland applications the ability to use the same header file to
 * avoid duplicate work and easy porting of software.
 */
struct pm3_par {
	unsigned char	__iomem *v_regs;/* virtual address of p_regs */
	u32		video;		/* video flags before blanking */
	u32		base;		/* screen base (xoffset+yoffset) in 128 bits unit */
	u32		palette[16];
};

/*
 * Here we define the default structs fb_fix_screeninfo and fb_var_screeninfo
 * if we don't use modedb. If we do use modedb see pm3fb_init how to use it
 * to get a fb_var_screeninfo. Otherwise define a default var as well.
 */
static struct fb_fix_screeninfo pm3fb_fix __devinitdata = {
	.id =		"Permedia3",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_PSEUDOCOLOR,
	.xpanstep =	1,
	.ypanstep =	1,
	.ywrapstep =	0,
	.accel =	FB_ACCEL_3DLABS_PERMEDIA3,
};

/*
 * Utility functions
 */

static inline u32 PM3_READ_REG(struct pm3_par *par, s32 off)
{
	return fb_readl(par->v_regs + off);
}

static inline void PM3_WRITE_REG(struct pm3_par *par, s32 off, u32 v)
{
	fb_writel(v, par->v_regs + off);
}

static inline void PM3_WAIT(struct pm3_par *par, u32 n)
{
	while (PM3_READ_REG(par, PM3InFIFOSpace) < n);
}

static inline void PM3_WRITE_DAC_REG(struct pm3_par *par, unsigned r, u8 v)
{
	PM3_WAIT(par, 3);
	PM3_WRITE_REG(par, PM3RD_IndexHigh, (r >> 8) & 0xff);
	PM3_WRITE_REG(par, PM3RD_IndexLow, r & 0xff);
	wmb();
	PM3_WRITE_REG(par, PM3RD_IndexedData, v);
	wmb();
}

static inline void pm3fb_set_color(struct pm3_par *par, unsigned char regno,
			unsigned char r, unsigned char g, unsigned char b)
{
	PM3_WAIT(par, 4);
	PM3_WRITE_REG(par, PM3RD_PaletteWriteAddress, regno);
	wmb();
	PM3_WRITE_REG(par, PM3RD_PaletteData, r);
	wmb();
	PM3_WRITE_REG(par, PM3RD_PaletteData, g);
	wmb();
	PM3_WRITE_REG(par, PM3RD_PaletteData, b);
	wmb();
}

static void pm3fb_clear_colormap(struct pm3_par *par,
			unsigned char r, unsigned char g, unsigned char b)
{
	int i;

	for (i = 0; i < 256 ; i++)
		pm3fb_set_color(par, i, r, g, b);

}

/* Calculating various clock parameter */
static void pm3fb_calculate_clock(unsigned long reqclock,
				unsigned char *prescale,
				unsigned char *feedback,
				unsigned char *postscale)
{
	int f, pre, post;
	unsigned long freq;
	long freqerr = 1000;
	long currerr;

	for (f = 1; f < 256; f++) {
		for (pre = 1; pre < 256; pre++) {
			for (post = 0; post < 5; post++) {
				freq = ((2*PM3_REF_CLOCK * f) >> post) / pre;
				currerr = (reqclock > freq)
					? reqclock - freq
					: freq - reqclock;
				if (currerr < freqerr) {
					freqerr = currerr;
					*feedback = f;
					*prescale = pre;
					*postscale = post;
				}
			}
		}
	}
}

static inline int pm3fb_depth(const struct fb_var_screeninfo *var)
{
	if ( var->bits_per_pixel == 16 )
		return var->red.length + var->green.length
			+ var->blue.length;

	return var->bits_per_pixel;
}

static inline int pm3fb_shift_bpp(unsigned bpp, int v)
{
	switch (bpp) {
	case 8:
		return (v >> 4);
	case 16:
		return (v >> 3);
	case 32:
		return (v >> 2);
	}
	DPRINTK("Unsupported depth %u\n", bpp);
	return 0;
}

/* acceleration */
static int pm3fb_sync(struct fb_info *info)
{
	struct pm3_par *par = info->par;

	PM3_WAIT(par, 2);
	PM3_WRITE_REG(par, PM3FilterMode, PM3FilterModeSync);
	PM3_WRITE_REG(par, PM3Sync, 0);
	mb();
	do {
		while ((PM3_READ_REG(par, PM3OutFIFOWords)) == 0);
		rmb();
	} while ((PM3_READ_REG(par, PM3OutputFifo)) != PM3Sync_Tag);

	return 0;
}

static void pm3fb_init_engine(struct fb_info *info)
{
	struct pm3_par *par = info->par;
	const u32 width = (info->var.xres_virtual + 7) & ~7;

	PM3_WAIT(par, 50);
	PM3_WRITE_REG(par, PM3FilterMode, PM3FilterModeSync);
	PM3_WRITE_REG(par, PM3StatisticMode, 0x0);
	PM3_WRITE_REG(par, PM3DeltaMode, 0x0);
	PM3_WRITE_REG(par, PM3RasterizerMode, 0x0);
	PM3_WRITE_REG(par, PM3ScissorMode, 0x0);
	PM3_WRITE_REG(par, PM3LineStippleMode, 0x0);
	PM3_WRITE_REG(par, PM3AreaStippleMode, 0x0);
	PM3_WRITE_REG(par, PM3GIDMode, 0x0);
	PM3_WRITE_REG(par, PM3DepthMode, 0x0);
	PM3_WRITE_REG(par, PM3StencilMode, 0x0);
	PM3_WRITE_REG(par, PM3StencilData, 0x0);
	PM3_WRITE_REG(par, PM3ColorDDAMode, 0x0);
	PM3_WRITE_REG(par, PM3TextureCoordMode, 0x0);
	PM3_WRITE_REG(par, PM3TextureIndexMode0, 0x0);
	PM3_WRITE_REG(par, PM3TextureIndexMode1, 0x0);
	PM3_WRITE_REG(par, PM3TextureReadMode, 0x0);
	PM3_WRITE_REG(par, PM3LUTMode, 0x0);
	PM3_WRITE_REG(par, PM3TextureFilterMode, 0x0);
	PM3_WRITE_REG(par, PM3TextureCompositeMode, 0x0);
	PM3_WRITE_REG(par, PM3TextureApplicationMode, 0x0);
	PM3_WRITE_REG(par, PM3TextureCompositeColorMode1, 0x0);
	PM3_WRITE_REG(par, PM3TextureCompositeAlphaMode1, 0x0);
	PM3_WRITE_REG(par, PM3TextureCompositeColorMode0, 0x0);
	PM3_WRITE_REG(par, PM3TextureCompositeAlphaMode0, 0x0);
	PM3_WRITE_REG(par, PM3FogMode, 0x0);
	PM3_WRITE_REG(par, PM3ChromaTestMode, 0x0);
	PM3_WRITE_REG(par, PM3AlphaTestMode, 0x0);
	PM3_WRITE_REG(par, PM3AntialiasMode, 0x0);
	PM3_WRITE_REG(par, PM3YUVMode, 0x0);
	PM3_WRITE_REG(par, PM3AlphaBlendColorMode, 0x0);
	PM3_WRITE_REG(par, PM3AlphaBlendAlphaMode, 0x0);
	PM3_WRITE_REG(par, PM3DitherMode, 0x0);
	PM3_WRITE_REG(par, PM3LogicalOpMode, 0x0);
	PM3_WRITE_REG(par, PM3RouterMode, 0x0);
	PM3_WRITE_REG(par, PM3Window, 0x0);

	PM3_WRITE_REG(par, PM3Config2D, 0x0);

	PM3_WRITE_REG(par, PM3SpanColorMask, 0xffffffff);

	PM3_WRITE_REG(par, PM3XBias, 0x0);
	PM3_WRITE_REG(par, PM3YBias, 0x0);
	PM3_WRITE_REG(par, PM3DeltaControl, 0x0);

	PM3_WRITE_REG(par, PM3BitMaskPattern, 0xffffffff);

	PM3_WRITE_REG(par, PM3FBDestReadEnables,
			   PM3FBDestReadEnables_E(0xff) |
			   PM3FBDestReadEnables_R(0xff) |
			   PM3FBDestReadEnables_ReferenceAlpha(0xff));
	PM3_WRITE_REG(par, PM3FBDestReadBufferAddr0, 0x0);
	PM3_WRITE_REG(par, PM3FBDestReadBufferOffset0, 0x0);
	PM3_WRITE_REG(par, PM3FBDestReadBufferWidth0,
			   PM3FBDestReadBufferWidth_Width(width));

	PM3_WRITE_REG(par, PM3FBDestReadMode,
			   PM3FBDestReadMode_ReadEnable |
			   PM3FBDestReadMode_Enable0);
	PM3_WRITE_REG(par, PM3FBSourceReadBufferAddr, 0x0);
	PM3_WRITE_REG(par, PM3FBSourceReadBufferOffset, 0x0);
	PM3_WRITE_REG(par, PM3FBSourceReadBufferWidth,
			   PM3FBSourceReadBufferWidth_Width(width));
	PM3_WRITE_REG(par, PM3FBSourceReadMode,
			   PM3FBSourceReadMode_Blocking |
			   PM3FBSourceReadMode_ReadEnable);

	PM3_WAIT(par, 2);
	{
		/* invert bits in bitmask */
		unsigned long rm = 1 | (3 << 7);
		switch (info->var.bits_per_pixel) {
		case 8:
			PM3_WRITE_REG(par, PM3PixelSize,
					   PM3PixelSize_GLOBAL_8BIT);
#ifdef __BIG_ENDIAN
			rm |= 3 << 15;
#endif
			break;
		case 16:
			PM3_WRITE_REG(par, PM3PixelSize,
					   PM3PixelSize_GLOBAL_16BIT);
#ifdef __BIG_ENDIAN
			rm |= 2 << 15;
#endif
			break;
		case 32:
			PM3_WRITE_REG(par, PM3PixelSize,
					   PM3PixelSize_GLOBAL_32BIT);
			break;
		default:
			DPRINTK(1, "Unsupported depth %d\n",
				info->var.bits_per_pixel);
			break;
		}
		PM3_WRITE_REG(par, PM3RasterizerMode, rm);
	}

	PM3_WAIT(par, 20);
	PM3_WRITE_REG(par, PM3FBSoftwareWriteMask, 0xffffffff);
	PM3_WRITE_REG(par, PM3FBHardwareWriteMask, 0xffffffff);
	PM3_WRITE_REG(par, PM3FBWriteMode,
			   PM3FBWriteMode_WriteEnable |
			   PM3FBWriteMode_OpaqueSpan |
			   PM3FBWriteMode_Enable0);
	PM3_WRITE_REG(par, PM3FBWriteBufferAddr0, 0x0);
	PM3_WRITE_REG(par, PM3FBWriteBufferOffset0, 0x0);
	PM3_WRITE_REG(par, PM3FBWriteBufferWidth0,
			   PM3FBWriteBufferWidth_Width(width));

	PM3_WRITE_REG(par, PM3SizeOfFramebuffer, 0x0);
	{
		/* size in lines of FB */
		unsigned long sofb = info->screen_size /
			info->fix.line_length;
		if (sofb > 4095)
			PM3_WRITE_REG(par, PM3SizeOfFramebuffer, 4095);
		else
			PM3_WRITE_REG(par, PM3SizeOfFramebuffer, sofb);

		switch (info->var.bits_per_pixel) {
		case 8:
			PM3_WRITE_REG(par, PM3DitherMode,
					   (1 << 10) | (2 << 3));
			break;
		case 16:
			PM3_WRITE_REG(par, PM3DitherMode,
					   (1 << 10) | (1 << 3));
			break;
		case 32:
			PM3_WRITE_REG(par, PM3DitherMode,
					   (1 << 10) | (0 << 3));
			break;
		default:
			DPRINTK(1, "Unsupported depth %d\n",
				info->current_par->depth);
			break;
		}
	}

	PM3_WRITE_REG(par, PM3dXDom, 0x0);
	PM3_WRITE_REG(par, PM3dXSub, 0x0);
	PM3_WRITE_REG(par, PM3dY, (1 << 16));
	PM3_WRITE_REG(par, PM3StartXDom, 0x0);
	PM3_WRITE_REG(par, PM3StartXSub, 0x0);
	PM3_WRITE_REG(par, PM3StartY, 0x0);
	PM3_WRITE_REG(par, PM3Count, 0x0);

/* Disable LocalBuffer. better safe than sorry */
	PM3_WRITE_REG(par, PM3LBDestReadMode, 0x0);
	PM3_WRITE_REG(par, PM3LBDestReadEnables, 0x0);
	PM3_WRITE_REG(par, PM3LBSourceReadMode, 0x0);
	PM3_WRITE_REG(par, PM3LBWriteMode, 0x0);

	pm3fb_sync(info);
}

static void pm3fb_fillrect (struct fb_info *info,
				const struct fb_fillrect *region)
{
	struct pm3_par *par = info->par;
	struct fb_fillrect modded;
	int vxres, vyres;
	u32 color = (info->fix.visual == FB_VISUAL_TRUECOLOR) ?
		((u32*)info->pseudo_palette)[region->color] : region->color;

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if ((info->flags & FBINFO_HWACCEL_DISABLED) ||
		region->rop != ROP_COPY ) {
		cfb_fillrect(info, region);
		return;
	}

	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;

	memcpy(&modded, region, sizeof(struct fb_fillrect));

	if(!modded.width || !modded.height ||
	   modded.dx >= vxres || modded.dy >= vyres)
		return;

	if(modded.dx + modded.width  > vxres)
		modded.width  = vxres - modded.dx;
	if(modded.dy + modded.height > vyres)
		modded.height = vyres - modded.dy;

	if(info->var.bits_per_pixel == 8)
		color |= color << 8;
	if(info->var.bits_per_pixel <= 16)
		color |= color << 16;

	PM3_WAIT(par, 4);
	/* ROP Ox3 is GXcopy */
	PM3_WRITE_REG(par, PM3Config2D,
			PM3Config2D_UseConstantSource |
			PM3Config2D_ForegroundROPEnable |
			(PM3Config2D_ForegroundROP(0x3)) |
			PM3Config2D_FBWriteEnable);

	PM3_WRITE_REG(par, PM3ForegroundColor, color);

	PM3_WRITE_REG(par, PM3RectanglePosition,
			(PM3RectanglePosition_XOffset(modded.dx)) |
			(PM3RectanglePosition_YOffset(modded.dy)));

	PM3_WRITE_REG(par, PM3Render2D,
		      PM3Render2D_XPositive |
		      PM3Render2D_YPositive |
		      PM3Render2D_Operation_Normal |
		      PM3Render2D_SpanOperation |
		      (PM3Render2D_Width(modded.width)) |
		      (PM3Render2D_Height(modded.height)));
}

static void pm3fb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area)
{
	struct pm3_par *par = info->par;
	struct fb_copyarea modded;
	u32 vxres, vyres;
	int x_align, o_x, o_y;

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_copyarea(info, area);
		return;
	}

	memcpy(&modded, area, sizeof(struct fb_copyarea));

	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;

	if(!modded.width || !modded.height ||
	   modded.sx >= vxres || modded.sy >= vyres ||
	   modded.dx >= vxres || modded.dy >= vyres)
		return;

	if(modded.sx + modded.width > vxres)
		modded.width = vxres - modded.sx;
	if(modded.dx + modded.width > vxres)
		modded.width = vxres - modded.dx;
	if(modded.sy + modded.height > vyres)
		modded.height = vyres - modded.sy;
	if(modded.dy + modded.height > vyres)
		modded.height = vyres - modded.dy;

	o_x = modded.sx - modded.dx;	/*(sx > dx ) ? (sx - dx) : (dx - sx); */
	o_y = modded.sy - modded.dy;	/*(sy > dy ) ? (sy - dy) : (dy - sy); */

	x_align = (modded.sx & 0x1f);

	PM3_WAIT(par, 6);

	PM3_WRITE_REG(par, PM3Config2D,
			PM3Config2D_UserScissorEnable |
			PM3Config2D_ForegroundROPEnable |
			PM3Config2D_Blocking |
			(PM3Config2D_ForegroundROP(0x3)) | /* Ox3 is GXcopy */
			PM3Config2D_FBWriteEnable);

	PM3_WRITE_REG(par, PM3ScissorMinXY,
			((modded.dy & 0x0fff) << 16) | (modded.dx & 0x0fff));
	PM3_WRITE_REG(par, PM3ScissorMaxXY,
			(((modded.dy + modded.height) & 0x0fff) << 16) |
			((modded.dx + modded.width) & 0x0fff));

	PM3_WRITE_REG(par, PM3FBSourceReadBufferOffset,
			PM3FBSourceReadBufferOffset_XOffset(o_x) |
			PM3FBSourceReadBufferOffset_YOffset(o_y));

	PM3_WRITE_REG(par, PM3RectanglePosition,
			(PM3RectanglePosition_XOffset(modded.dx - x_align)) |
			(PM3RectanglePosition_YOffset(modded.dy)));

	PM3_WRITE_REG(par, PM3Render2D,
			((modded.sx > modded.dx) ? PM3Render2D_XPositive : 0) |
			((modded.sy > modded.dy) ? PM3Render2D_YPositive : 0) |
			PM3Render2D_Operation_Normal |
			PM3Render2D_SpanOperation |
			PM3Render2D_FBSourceReadEnable |
			(PM3Render2D_Width(modded.width + x_align)) |
			(PM3Render2D_Height(modded.height)));
}

static void pm3fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct pm3_par *par = info->par;
	u32 height = image->height;
	u32 fgx, bgx;
	const u32 *src = (const u32*)image->data;

	switch (info->fix.visual) {
		case FB_VISUAL_PSEUDOCOLOR:
			fgx = image->fg_color;
			bgx = image->bg_color;
			break;
		case FB_VISUAL_TRUECOLOR:
		default:
			fgx = par->palette[image->fg_color];
			bgx = par->palette[image->bg_color];
			break;
	}
	if (image->depth != 1 || (image->width & 0x1f)) {
		return cfb_imageblit(info, image);
	}
	if (info->var.bits_per_pixel == 8) {
		fgx |= fgx << 8;
		bgx |= bgx << 8;
	}
	if (info->var.bits_per_pixel <= 16) {
		fgx |= fgx << 16;
		bgx |= bgx << 16;
	}

	PM3_WAIT(par, 5);

	PM3_WRITE_REG(par, PM3ForegroundColor, fgx);
	PM3_WRITE_REG(par, PM3BackgroundColor, bgx);

	/* ROP Ox3 is GXcopy */
	PM3_WRITE_REG(par, PM3Config2D,
			PM3Config2D_UseConstantSource |
			PM3Config2D_ForegroundROPEnable |
			(PM3Config2D_ForegroundROP(0x3)) |
			PM3Config2D_OpaqueSpan |
			PM3Config2D_FBWriteEnable);
	PM3_WRITE_REG(par, PM3RectanglePosition,
			(PM3RectanglePosition_XOffset(image->dx)) |
			(PM3RectanglePosition_YOffset(image->dy)));
	PM3_WRITE_REG(par, PM3Render2D,
			PM3Render2D_XPositive |
			PM3Render2D_YPositive |
			PM3Render2D_Operation_SyncOnBitMask |
			PM3Render2D_SpanOperation |
			(PM3Render2D_Width(image->width)) |
			(PM3Render2D_Height(image->height)));


	while (height--) {
		u32 width = (image->width + 31) >> 5;

		while (width >= PM3_FIFO_SIZE) {
			int i = PM3_FIFO_SIZE - 1;

			PM3_WAIT(par, PM3_FIFO_SIZE);
			while (i--) {
				PM3_WRITE_REG(par, PM3BitMaskPattern, *src);
				src++;
			}
			width -= PM3_FIFO_SIZE - 1;
		}

		PM3_WAIT(par, width + 1);
		while (width--) {
			PM3_WRITE_REG(par, PM3BitMaskPattern, *src);
			src++;
		}
	}
}
/* end of acceleration functions */

/* write the mode to registers */
static void pm3fb_write_mode(struct fb_info *info)
{
	struct pm3_par *par = info->par;
	char tempsync = 0x00, tempmisc = 0x00;
	const u32 hsstart = info->var.right_margin;
	const u32 hsend = hsstart + info->var.hsync_len;
	const u32 hbend = hsend + info->var.left_margin;
	const u32 xres = (info->var.xres + 31) & ~31;
	const u32 htotal = xres + hbend;
	const u32 vsstart = info->var.lower_margin;
	const u32 vsend = vsstart + info->var.vsync_len;
	const u32 vbend = vsend + info->var.upper_margin;
	const u32 vtotal = info->var.yres + vbend;
	const u32 width = (info->var.xres_virtual + 7) & ~7;
	const unsigned bpp = info->var.bits_per_pixel;

	PM3_WAIT(par, 20);
	PM3_WRITE_REG(par, PM3MemBypassWriteMask, 0xffffffff);
	PM3_WRITE_REG(par, PM3Aperture0, 0x00000000);
	PM3_WRITE_REG(par, PM3Aperture1, 0x00000000);
	PM3_WRITE_REG(par, PM3FIFODis, 0x00000007);

	PM3_WRITE_REG(par, PM3HTotal,
			   pm3fb_shift_bpp(bpp, htotal - 1));
	PM3_WRITE_REG(par, PM3HsEnd,
			   pm3fb_shift_bpp(bpp, hsend));
	PM3_WRITE_REG(par, PM3HsStart,
			   pm3fb_shift_bpp(bpp, hsstart));
	PM3_WRITE_REG(par, PM3HbEnd,
			   pm3fb_shift_bpp(bpp, hbend));
	PM3_WRITE_REG(par, PM3HgEnd,
			   pm3fb_shift_bpp(bpp, hbend));
	PM3_WRITE_REG(par, PM3ScreenStride,
			   pm3fb_shift_bpp(bpp, width));
	PM3_WRITE_REG(par, PM3VTotal, vtotal - 1);
	PM3_WRITE_REG(par, PM3VsEnd, vsend - 1);
	PM3_WRITE_REG(par, PM3VsStart, vsstart - 1);
	PM3_WRITE_REG(par, PM3VbEnd, vbend);

	switch (bpp) {
	case 8:
		PM3_WRITE_REG(par, PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_8BIT);
		PM3_WRITE_REG(par, PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_8BIT);
		break;

	case 16:
#ifndef __BIG_ENDIAN
		PM3_WRITE_REG(par, PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_16BIT);
		PM3_WRITE_REG(par, PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_16BIT);
#else
		PM3_WRITE_REG(par, PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_16BIT |
				   PM3ByApertureMode_BYTESWAP_BADC);
		PM3_WRITE_REG(par, PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_16BIT |
				   PM3ByApertureMode_BYTESWAP_BADC);
#endif /* ! __BIG_ENDIAN */
		break;

	case 32:
#ifndef __BIG_ENDIAN
		PM3_WRITE_REG(par, PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_32BIT);
		PM3_WRITE_REG(par, PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_32BIT);
#else
		PM3_WRITE_REG(par, PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_32BIT |
				   PM3ByApertureMode_BYTESWAP_DCBA);
		PM3_WRITE_REG(par, PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_32BIT |
				   PM3ByApertureMode_BYTESWAP_DCBA);
#endif /* ! __BIG_ENDIAN */
		break;

	default:
		DPRINTK("Unsupported depth %d\n", bpp);
		break;
	}

	/*
	 * Oxygen VX1 - it appears that setting PM3VideoControl and
	 * then PM3RD_SyncControl to the same SYNC settings undoes
	 * any net change - they seem to xor together.  Only set the
	 * sync options in PM3RD_SyncControl.  --rmk
	 */
	{
		unsigned int video = par->video;

		video &= ~(PM3VideoControl_HSYNC_MASK |
			   PM3VideoControl_VSYNC_MASK);
		video |= PM3VideoControl_HSYNC_ACTIVE_HIGH |
			 PM3VideoControl_VSYNC_ACTIVE_HIGH;
		PM3_WRITE_REG(par, PM3VideoControl, video);
	}
	PM3_WRITE_REG(par, PM3VClkCtl,
			   (PM3_READ_REG(par, PM3VClkCtl) & 0xFFFFFFFC));
	PM3_WRITE_REG(par, PM3ScreenBase, par->base);
	PM3_WRITE_REG(par, PM3ChipConfig,
			   (PM3_READ_REG(par, PM3ChipConfig) & 0xFFFFFFFD));

	wmb();
	{
		unsigned char uninitialized_var(m);	/* ClkPreScale */
		unsigned char uninitialized_var(n);	/* ClkFeedBackScale */
		unsigned char uninitialized_var(p);	/* ClkPostScale */
		unsigned long pixclock = PICOS2KHZ(info->var.pixclock);

		(void)pm3fb_calculate_clock(pixclock, &m, &n, &p);

		DPRINTK("Pixclock: %ld, Pre: %d, Feedback: %d, Post: %d\n",
			pixclock, (int) m, (int) n, (int) p);

		PM3_WRITE_DAC_REG(par, PM3RD_DClk0PreScale, m);
		PM3_WRITE_DAC_REG(par, PM3RD_DClk0FeedbackScale, n);
		PM3_WRITE_DAC_REG(par, PM3RD_DClk0PostScale, p);
	}
	/*
	   PM3_WRITE_DAC_REG(par, PM3RD_IndexControl, 0x00);
	 */
	/*
	   PM3_SLOW_WRITE_REG(par, PM3RD_IndexControl, 0x00);
	 */
	if ((par->video & PM3VideoControl_HSYNC_MASK) ==
	    PM3VideoControl_HSYNC_ACTIVE_HIGH)
		tempsync |= PM3RD_SyncControl_HSYNC_ACTIVE_HIGH;
	if ((par->video & PM3VideoControl_VSYNC_MASK) ==
	    PM3VideoControl_VSYNC_ACTIVE_HIGH)
		tempsync |= PM3RD_SyncControl_VSYNC_ACTIVE_HIGH;

	PM3_WRITE_DAC_REG(par, PM3RD_SyncControl, tempsync);
	DPRINTK("PM3RD_SyncControl: %d\n", tempsync);

	PM3_WRITE_DAC_REG(par, PM3RD_DACControl, 0x00);

	switch (pm3fb_depth(&info->var)) {
	case 8:
		PM3_WRITE_DAC_REG(par, PM3RD_PixelSize,
				  PM3RD_PixelSize_8_BIT_PIXELS);
		PM3_WRITE_DAC_REG(par, PM3RD_ColorFormat,
				  PM3RD_ColorFormat_CI8_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW);
		tempmisc |= PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE;
		break;
	case 12:
		PM3_WRITE_DAC_REG(par, PM3RD_PixelSize,
				  PM3RD_PixelSize_16_BIT_PIXELS);
		PM3_WRITE_DAC_REG(par, PM3RD_ColorFormat,
				  PM3RD_ColorFormat_4444_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW |
				  PM3RD_ColorFormat_LINEAR_COLOR_EXT_ENABLE);
		tempmisc |= PM3RD_MiscControl_DIRECTCOLOR_ENABLE |
			PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE;
		break;
	case 15:
		PM3_WRITE_DAC_REG(par, PM3RD_PixelSize,
				  PM3RD_PixelSize_16_BIT_PIXELS);
		PM3_WRITE_DAC_REG(par, PM3RD_ColorFormat,
				  PM3RD_ColorFormat_5551_FRONT_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW |
				  PM3RD_ColorFormat_LINEAR_COLOR_EXT_ENABLE);
		tempmisc |= PM3RD_MiscControl_DIRECTCOLOR_ENABLE |
			PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE;
		break;
	case 16:
		PM3_WRITE_DAC_REG(par, PM3RD_PixelSize,
				  PM3RD_PixelSize_16_BIT_PIXELS);
		PM3_WRITE_DAC_REG(par, PM3RD_ColorFormat,
				  PM3RD_ColorFormat_565_FRONT_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW |
				  PM3RD_ColorFormat_LINEAR_COLOR_EXT_ENABLE);
		tempmisc |= PM3RD_MiscControl_DIRECTCOLOR_ENABLE |
			PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE;
		break;
	case 32:
		PM3_WRITE_DAC_REG(par, PM3RD_PixelSize,
				  PM3RD_PixelSize_32_BIT_PIXELS);
		PM3_WRITE_DAC_REG(par, PM3RD_ColorFormat,
				  PM3RD_ColorFormat_8888_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW);
		tempmisc |= PM3RD_MiscControl_DIRECTCOLOR_ENABLE |
			PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE;
		break;
	}
	PM3_WRITE_DAC_REG(par, PM3RD_MiscControl, tempmisc);
}

/*
 * hardware independent functions
 */
static int pm3fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	u32 lpitch;
	unsigned bpp = var->red.length + var->green.length
			+ var->blue.length + var->transp.length;

	if ( bpp != var->bits_per_pixel ) {
		/* set predefined mode for bits_per_pixel settings */

		switch(var->bits_per_pixel) {
		case 8:
			var->red.length = var->green.length = var->blue.length = 8;
			var->red.offset = var->green.offset = var->blue.offset = 0;
			var->transp.offset = 0;
			var->transp.length = 0;
			break;
		case 16:
			var->red.length = var->blue.length = 5;
			var->green.length = 6;
			var->transp.length = 0;
			break;
		case 32:
			var->red.length = var->green.length = var->blue.length = 8;
			var->transp.length = 8;
			break;
		default:
			DPRINTK("depth not supported: %u\n", var->bits_per_pixel);
			return -EINVAL;
		}
	}
	/* it is assumed BGRA order */
	if (var->bits_per_pixel > 8 )
	{
		var->blue.offset = 0;
		var->green.offset = var->blue.length;
		var->red.offset = var->green.offset + var->green.length;
		var->transp.offset = var->red.offset + var->red.length;
	}
	var->height = var->width = -1;

	if (var->xres != var->xres_virtual) {
		DPRINTK("virtual x resolution != physical x resolution not supported\n");
		return -EINVAL;
	}

	if (var->yres > var->yres_virtual) {
		DPRINTK("virtual y resolution < physical y resolution not possible\n");
		return -EINVAL;
	}

	if (var->xoffset) {
		DPRINTK("xoffset not supported\n");
		return -EINVAL;
	}

	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		DPRINTK("interlace not supported\n");
		return -EINVAL;
	}

	var->xres = (var->xres + 31) & ~31; /* could sometimes be 8 */
	lpitch = var->xres * ((var->bits_per_pixel + 7)>>3);

	if (var->xres < 200 || var->xres > 2048) {
		DPRINTK("width not supported: %u\n", var->xres);
		return -EINVAL;
	}

	if (var->yres < 200 || var->yres > 4095) {
		DPRINTK("height not supported: %u\n", var->yres);
		return -EINVAL;
	}

	if (lpitch * var->yres_virtual > info->fix.smem_len) {
		DPRINTK("no memory for screen (%ux%ux%u)\n",
			var->xres, var->yres_virtual, var->bits_per_pixel);
		return -EINVAL;
	}

	if (PICOS2KHZ(var->pixclock) > PM3_MAX_PIXCLOCK) {
		DPRINTK("pixclock too high (%ldKHz)\n", PICOS2KHZ(var->pixclock));
		return -EINVAL;
	}

	var->accel_flags = 0;	/* Can't mmap if this is on */

	DPRINTK("Checking graphics mode at %dx%d depth %d\n",
		var->xres, var->yres, var->bits_per_pixel);
	return 0;
}

static int pm3fb_set_par(struct fb_info *info)
{
	struct pm3_par *par = info->par;
	const u32 xres = (info->var.xres + 31) & ~31;
	const unsigned bpp = info->var.bits_per_pixel;

	par->base = pm3fb_shift_bpp(bpp,(info->var.yoffset * xres)
					+ info->var.xoffset);
	par->video = 0;

	if (info->var.sync & FB_SYNC_HOR_HIGH_ACT)
		par->video |= PM3VideoControl_HSYNC_ACTIVE_HIGH;
	else
		par->video |= PM3VideoControl_HSYNC_ACTIVE_LOW;

	if (info->var.sync & FB_SYNC_VERT_HIGH_ACT)
		par->video |= PM3VideoControl_VSYNC_ACTIVE_HIGH;
	else
		par->video |= PM3VideoControl_VSYNC_ACTIVE_LOW;

	if ((info->var.vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE)
		par->video |= PM3VideoControl_LINE_DOUBLE_ON;
	else
		par->video |= PM3VideoControl_LINE_DOUBLE_OFF;

	if ((info->var.activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW)
		par->video |= PM3VideoControl_ENABLE;
	else {
		par->video |= PM3VideoControl_DISABLE;
		DPRINTK("PM3Video disabled\n");
	}
	switch (bpp) {
	case 8:
		par->video |= PM3VideoControl_PIXELSIZE_8BIT;
		break;
	case 16:
		par->video |= PM3VideoControl_PIXELSIZE_16BIT;
		break;
	case 32:
		par->video |= PM3VideoControl_PIXELSIZE_32BIT;
		break;
	default:
		DPRINTK("Unsupported depth\n");
		break;
	}

	info->fix.visual =
		(bpp == 8) ? FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;
	info->fix.line_length = ((info->var.xres_virtual + 7)  & ~7)
					* bpp / 8;

/*	pm3fb_clear_memory(info, 0);*/
	pm3fb_clear_colormap(par, 0, 0, 0);
	PM3_WRITE_DAC_REG(par, PM3RD_CursorMode,
			  PM3RD_CursorMode_CURSOR_DISABLE);
	pm3fb_init_engine(info);
	pm3fb_write_mode(info);
	return 0;
}

static int pm3fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
	struct pm3_par *par = info->par;

	if (regno >= 256)  /* no. of hw registers */
	   return -EINVAL;

	/* grayscale works only partially under directcolor */
	if (info->var.grayscale) {
	   /* grayscale = 0.30*R + 0.59*G + 0.11*B */
	   red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}

	/* Directcolor:
	 *   var->{color}.offset contains start of bitfield
	 *   var->{color}.length contains length of bitfield
	 *   {hardwarespecific} contains width of DAC
	 *   pseudo_palette[X] is programmed to (X << red.offset) |
	 *					(X << green.offset) |
	 *					(X << blue.offset)
	 *   RAMDAC[X] is programmed to (red, green, blue)
	 *   color depth = SUM(var->{color}.length)
	 *
	 * Pseudocolor:
	 *	var->{color}.offset is 0
	 *	var->{color}.length contains width of DAC or the number of unique
	 *			colors available (color depth)
	 *	pseudo_palette is not used
	 *	RAMDAC[X] is programmed to (red, green, blue)
	 *	color depth = var->{color}.length
	 */

	/*
	 * This is the point where the color is converted to something that
	 * is acceptable by the hardware.
	 */
#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)
	red = CNVT_TOHW(red, info->var.red.length);
	green = CNVT_TOHW(green, info->var.green.length);
	blue = CNVT_TOHW(blue, info->var.blue.length);
	transp = CNVT_TOHW(transp, info->var.transp.length);
#undef CNVT_TOHW

	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		u32 v;

		if (regno >= 16)
			return -EINVAL;

		v = (red << info->var.red.offset) |
			(green << info->var.green.offset) |
			(blue << info->var.blue.offset) |
			(transp << info->var.transp.offset);

		switch (info->var.bits_per_pixel) {
		case 8:
			break;
		case 16:
		case 32:
			((u32*)(info->pseudo_palette))[regno] = v;
			break;
		}
		return 0;
	}
	else if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR)
		pm3fb_set_color(par, regno, red, green, blue);

	return 0;
}

static int pm3fb_pan_display(struct fb_var_screeninfo *var,
				 struct fb_info *info)
{
	struct pm3_par *par = info->par;
	const u32 xres = (var->xres + 31) & ~31;

	par->base = pm3fb_shift_bpp(var->bits_per_pixel,
					(var->yoffset * xres)
					+ var->xoffset);
	PM3_WAIT(par, 1);
	PM3_WRITE_REG(par, PM3ScreenBase, par->base);
	return 0;
}

static int pm3fb_blank(int blank_mode, struct fb_info *info)
{
	struct pm3_par *par = info->par;
	u32 video = par->video;

	/*
	 * Oxygen VX1 - it appears that setting PM3VideoControl and
	 * then PM3RD_SyncControl to the same SYNC settings undoes
	 * any net change - they seem to xor together.  Only set the
	 * sync options in PM3RD_SyncControl.  --rmk
	 */
	video &= ~(PM3VideoControl_HSYNC_MASK |
		   PM3VideoControl_VSYNC_MASK);
	video |= PM3VideoControl_HSYNC_ACTIVE_HIGH |
		 PM3VideoControl_VSYNC_ACTIVE_HIGH;

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		video |= PM3VideoControl_ENABLE;
		break;
	case FB_BLANK_NORMAL:
		video &= ~(PM3VideoControl_ENABLE);
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		video &= ~(PM3VideoControl_HSYNC_MASK |
			  PM3VideoControl_BLANK_ACTIVE_LOW);
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		video &= ~(PM3VideoControl_VSYNC_MASK |
			  PM3VideoControl_BLANK_ACTIVE_LOW);
		break;
	case FB_BLANK_POWERDOWN:
		video &= ~(PM3VideoControl_HSYNC_MASK |
			  PM3VideoControl_VSYNC_MASK |
			  PM3VideoControl_BLANK_ACTIVE_LOW);
		break;
	default:
		DPRINTK("Unsupported blanking %d\n", blank_mode);
		return 1;
	}

	PM3_WAIT(par, 1);
	PM3_WRITE_REG(par,PM3VideoControl, video);
	return 0;
}

	/*
	 *  Frame buffer operations
	 */

static struct fb_ops pm3fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= pm3fb_check_var,
	.fb_set_par	= pm3fb_set_par,
	.fb_setcolreg	= pm3fb_setcolreg,
	.fb_pan_display	= pm3fb_pan_display,
	.fb_fillrect	= pm3fb_fillrect,
	.fb_copyarea	= pm3fb_copyarea,
	.fb_imageblit	= pm3fb_imageblit,
	.fb_blank	= pm3fb_blank,
	.fb_sync	= pm3fb_sync,
};

/* ------------------------------------------------------------------------- */

	/*
	 *  Initialization
	 */

/* mmio register are already mapped when this function is called */
/* the pm3fb_fix.smem_start is also set */
static unsigned long pm3fb_size_memory(struct pm3_par *par)
{
	unsigned long	memsize = 0, tempBypass, i, temp1, temp2;
	unsigned char	__iomem *screen_mem;

	pm3fb_fix.smem_len = 64 * 1024l * 1024; /* request full aperture size */
	/* Linear frame buffer - request region and map it. */
	if (!request_mem_region(pm3fb_fix.smem_start, pm3fb_fix.smem_len,
				 "pm3fb smem")) {
		printk(KERN_WARNING "pm3fb: Can't reserve smem.\n");
		return 0;
	}
	screen_mem =
		ioremap_nocache(pm3fb_fix.smem_start, pm3fb_fix.smem_len);
	if (!screen_mem) {
		printk(KERN_WARNING "pm3fb: Can't ioremap smem area.\n");
		release_mem_region(pm3fb_fix.smem_start, pm3fb_fix.smem_len);
		return 0;
	}

	/* TODO: card-specific stuff, *before* accessing *any* FB memory */
	/* For Appian Jeronimo 2000 board second head */

	tempBypass = PM3_READ_REG(par, PM3MemBypassWriteMask);

	DPRINTK("PM3MemBypassWriteMask was: 0x%08lx\n", tempBypass);

	PM3_WAIT(par, 1);
	PM3_WRITE_REG(par, PM3MemBypassWriteMask, 0xFFFFFFFF);

	/* pm3 split up memory, replicates, and do a lot of nasty stuff IMHO ;-) */
	for (i = 0; i < 32; i++) {
		fb_writel(i * 0x00345678,
			  (screen_mem + (i * 1048576)));
		mb();
		temp1 = fb_readl((screen_mem + (i * 1048576)));

		/* Let's check for wrapover, write will fail at 16MB boundary */
		if (temp1 == (i * 0x00345678))
			memsize = i;
		else
			break;
	}

	DPRINTK("First detect pass already got %ld MB\n", memsize + 1);

	if (memsize + 1 == i) {
		for (i = 0; i < 32; i++) {
			/* Clear first 32MB ; 0 is 0, no need to byteswap */
			writel(0x0000000, (screen_mem + (i * 1048576)));
		}
		wmb();

		for (i = 32; i < 64; i++) {
			fb_writel(i * 0x00345678,
				  (screen_mem + (i * 1048576)));
			mb();
			temp1 =
			    fb_readl((screen_mem + (i * 1048576)));
			temp2 =
			    fb_readl((screen_mem + ((i - 32) * 1048576)));
			/* different value, different RAM... */
			if ((temp1 == (i * 0x00345678)) && (temp2 == 0))
				memsize = i;
			else
				break;
		}
	}
	DPRINTK("Second detect pass got %ld MB\n", memsize + 1);

	PM3_WAIT(par, 1);
	PM3_WRITE_REG(par, PM3MemBypassWriteMask, tempBypass);

	iounmap(screen_mem);
	release_mem_region(pm3fb_fix.smem_start, pm3fb_fix.smem_len);
	memsize = 1048576 * (memsize + 1);

	DPRINTK("Returning 0x%08lx bytes\n", memsize);

	return memsize;
}

static int __devinit pm3fb_probe(struct pci_dev *dev,
				  const struct pci_device_id *ent)
{
	struct fb_info *info;
	struct pm3_par *par;
	struct device* device = &dev->dev; /* for pci drivers */
	int err, retval = -ENXIO;

	err = pci_enable_device(dev);
	if (err) {
		printk(KERN_WARNING "pm3fb: Can't enable PCI dev: %d\n", err);
		return err;
	}
	/*
	 * Dynamically allocate info and par
	 */
	info = framebuffer_alloc(sizeof(struct pm3_par), device);

	if (!info)
		return -ENOMEM;
	par = info->par;

	/*
	 * Here we set the screen_base to the virtual memory address
	 * for the framebuffer.
	 */
	pm3fb_fix.mmio_start = pci_resource_start(dev, 0);
	pm3fb_fix.mmio_len = PM3_REGS_SIZE;

	/* Registers - request region and map it. */
	if (!request_mem_region(pm3fb_fix.mmio_start, pm3fb_fix.mmio_len,
				 "pm3fb regbase")) {
		printk(KERN_WARNING "pm3fb: Can't reserve regbase.\n");
		goto err_exit_neither;
	}
	par->v_regs =
		ioremap_nocache(pm3fb_fix.mmio_start, pm3fb_fix.mmio_len);
	if (!par->v_regs) {
		printk(KERN_WARNING "pm3fb: Can't remap %s register area.\n",
			pm3fb_fix.id);
		release_mem_region(pm3fb_fix.mmio_start, pm3fb_fix.mmio_len);
		goto err_exit_neither;
	}

#if defined(__BIG_ENDIAN)
	pm3fb_fix.mmio_start += PM3_REGS_SIZE;
	DPRINTK("Adjusting register base for big-endian.\n");
#endif
	/* Linear frame buffer - request region and map it. */
	pm3fb_fix.smem_start = pci_resource_start(dev, 1);
	pm3fb_fix.smem_len = pm3fb_size_memory(par);
	if (!pm3fb_fix.smem_len)
	{
		printk(KERN_WARNING "pm3fb: Can't find memory on board.\n");
		goto err_exit_mmio;
	}
	if (!request_mem_region(pm3fb_fix.smem_start, pm3fb_fix.smem_len,
				 "pm3fb smem")) {
		printk(KERN_WARNING "pm3fb: Can't reserve smem.\n");
		goto err_exit_mmio;
	}
	info->screen_base =
		ioremap_nocache(pm3fb_fix.smem_start, pm3fb_fix.smem_len);
	if (!info->screen_base) {
		printk(KERN_WARNING "pm3fb: Can't ioremap smem area.\n");
		release_mem_region(pm3fb_fix.smem_start, pm3fb_fix.smem_len);
		goto err_exit_mmio;
	}
	info->screen_size = pm3fb_fix.smem_len;

	info->fbops = &pm3fb_ops;

	par->video = PM3_READ_REG(par, PM3VideoControl);

	info->fix = pm3fb_fix;
	info->pseudo_palette = par->palette;
	info->flags = FBINFO_DEFAULT |
/*			FBINFO_HWACCEL_YPAN |*/
			FBINFO_HWACCEL_COPYAREA |
			FBINFO_HWACCEL_IMAGEBLIT |
			FBINFO_HWACCEL_FILLRECT;

	/*
	 * This should give a reasonable default video mode. The following is
	 * done when we can set a video mode.
	 */
	if (!mode_option)
		mode_option = "640x480@60";

	retval = fb_find_mode(&info->var, info, mode_option, NULL, 0, NULL, 8);

	if (!retval || retval == 4) {
		retval = -EINVAL;
		goto err_exit_both;
	}

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0) {
		retval = -ENOMEM;
		goto err_exit_both;
	}

	/*
	 * For drivers that can...
	 */
	pm3fb_check_var(&info->var, info);

	if (register_framebuffer(info) < 0) {
		retval = -EINVAL;
		goto err_exit_all;
	}
	printk(KERN_INFO "fb%d: %s frame buffer device\n", info->node,
	   info->fix.id);
	pci_set_drvdata(dev, info);
	return 0;

 err_exit_all:
	fb_dealloc_cmap(&info->cmap);
 err_exit_both:
	iounmap(info->screen_base);
	release_mem_region(pm3fb_fix.smem_start, pm3fb_fix.smem_len);
 err_exit_mmio:
	iounmap(par->v_regs);
	release_mem_region(pm3fb_fix.mmio_start, pm3fb_fix.mmio_len);
 err_exit_neither:
	framebuffer_release(info);
	return retval;
}

	/*
	 *  Cleanup
	 */
static void __devexit pm3fb_remove(struct pci_dev *dev)
{
	struct fb_info *info = pci_get_drvdata(dev);

	if (info) {
		struct fb_fix_screeninfo *fix = &info->fix;
		struct pm3_par *par = info->par;

		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);

		iounmap(info->screen_base);
		release_mem_region(fix->smem_start, fix->smem_len);
		iounmap(par->v_regs);
		release_mem_region(fix->mmio_start, fix->mmio_len);

		pci_set_drvdata(dev, NULL);
		framebuffer_release(info);
	}
}

static struct pci_device_id pm3fb_id_table[] = {
	{ PCI_VENDOR_ID_3DLABS, 0x0a,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, }
};

/* For PCI drivers */
static struct pci_driver pm3fb_driver = {
	.name =		"pm3fb",
	.id_table =	pm3fb_id_table,
	.probe =	pm3fb_probe,
	.remove =	__devexit_p(pm3fb_remove),
};

MODULE_DEVICE_TABLE(pci, pm3fb_id_table);

static int __init pm3fb_init(void)
{
#ifndef MODULE
	if (fb_get_options("pm3fb", NULL))
		return -ENODEV;
#endif
	return pci_register_driver(&pm3fb_driver);
}

static void __exit pm3fb_exit(void)
{
	pci_unregister_driver(&pm3fb_driver);
}

module_init(pm3fb_init);
module_exit(pm3fb_exit);

MODULE_LICENSE("GPL");
