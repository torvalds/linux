/*
 * Silicon Motion SM7XX frame buffer device
 *
 * Copyright (C) 2006 Silicon Motion Technology Corp.
 * Authors:  Ge Wang, gewang@siliconmotion.com
 *	     Boyod boyod.yang@siliconmotion.com.cn
 *
 * Copyright (C) 2009 Lemote, Inc.
 * Author:   Wu Zhangjin, wuzhangjin@gmail.com
 *
 * Copyright (C) 2011 Igalia, S.L.
 * Author:   Javier M. Mellid <jmunhoz@igalia.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Framebuffer driver for Silicon Motion SM710, SM712, SM721 and SM722 chips
 */

#include <linux/io.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/screen_info.h>

#ifdef CONFIG_PM
#include <linux/pm.h>
#endif

#include "sm7xx.h"

/*
* Private structure
*/
struct smtcfb_info {
	struct pci_dev *pdev;
	struct fb_info fb;
	u16 chip_id;
	u8  chip_rev_id;

	void __iomem *lfb;	/* linear frame buffer */
	void __iomem *dp_regs;	/* drawing processor control regs */
	void __iomem *vp_regs;	/* video processor control regs */
	void __iomem *cp_regs;	/* capture processor control regs */
	void __iomem *mmio;	/* memory map IO port */

	u_int width;
	u_int height;
	u_int hz;

	u32 colreg[17];
};

void __iomem *smtc_RegBaseAddress;	/* Memory Map IO starting address */
void __iomem *smtc_VRAMBaseAddress;	/* video memory starting address */

static struct fb_var_screeninfo smtcfb_var = {
	.xres           = 1024,
	.yres           = 600,
	.xres_virtual   = 1024,
	.yres_virtual   = 600,
	.bits_per_pixel = 16,
	.red            = {16, 8, 0},
	.green          = {8, 8, 0},
	.blue           = {0, 8, 0},
	.activate       = FB_ACTIVATE_NOW,
	.height         = -1,
	.width          = -1,
	.vmode          = FB_VMODE_NONINTERLACED,
	.nonstd         = 0,
	.accel_flags    = FB_ACCELF_TEXT,
};

static struct fb_fix_screeninfo smtcfb_fix = {
	.id             = "smXXXfb",
	.type           = FB_TYPE_PACKED_PIXELS,
	.visual         = FB_VISUAL_TRUECOLOR,
	.line_length    = 800 * 3,
	.accel          = FB_ACCEL_SMI_LYNX,
	.type_aux       = 0,
	.xpanstep       = 0,
	.ypanstep       = 0,
	.ywrapstep      = 0,
};

struct vesa_mode {
	char index[6];
	u16  lfb_width;
	u16  lfb_height;
	u16  lfb_depth;
};

static struct vesa_mode vesa_mode_table[] = {
	{"0x301", 640,  480,  8},
	{"0x303", 800,  600,  8},
	{"0x305", 1024, 768,  8},
	{"0x307", 1280, 1024, 8},

	{"0x311", 640,  480,  16},
	{"0x314", 800,  600,  16},
	{"0x317", 1024, 768,  16},
	{"0x31A", 1280, 1024, 16},

	{"0x312", 640,  480,  24},
	{"0x315", 800,  600,  24},
	{"0x318", 1024, 768,  24},
	{"0x31B", 1280, 1024, 24},
};

struct screen_info smtc_scr_info;

/* process command line options, get vga parameter */
static int __init sm7xx_vga_setup(char *options)
{
	int i;

	if (!options || !*options)
		return -EINVAL;

	smtc_scr_info.lfb_width = 0;
	smtc_scr_info.lfb_height = 0;
	smtc_scr_info.lfb_depth = 0;

	pr_debug("sm7xx_vga_setup = %s\n", options);

	for (i = 0; i < ARRAY_SIZE(vesa_mode_table); i++) {
		if (strstr(options, vesa_mode_table[i].index)) {
			smtc_scr_info.lfb_width  = vesa_mode_table[i].lfb_width;
			smtc_scr_info.lfb_height = vesa_mode_table[i].lfb_height;
			smtc_scr_info.lfb_depth  = vesa_mode_table[i].lfb_depth;
			return 0;
		}
	}

	return -1;
}
__setup("vga=", sm7xx_vga_setup);

static void sm712_setpalette(int regno, unsigned red, unsigned green,
			     unsigned blue, struct fb_info *info)
{
	/* set bit 5:4 = 01 (write LCD RAM only) */
	smtc_seqw(0x66, (smtc_seqr(0x66) & 0xC3) | 0x10);

	smtc_mmiowb(regno, dac_reg);
	smtc_mmiowb(red >> 10, dac_val);
	smtc_mmiowb(green >> 10, dac_val);
	smtc_mmiowb(blue >> 10, dac_val);
}

/* chan_to_field
 *
 * convert a colour value into a field position
 *
 * from pxafb.c
 */

static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int smtc_blank(int blank_mode, struct fb_info *info)
{
	/* clear DPMS setting */
	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		/* Screen On: HSync: On, VSync : On */
		smtc_seqw(0x01, (smtc_seqr(0x01) & (~0x20)));
		smtc_seqw(0x6a, 0x16);
		smtc_seqw(0x6b, 0x02);
		smtc_seqw(0x21, (smtc_seqr(0x21) & 0x77));
		smtc_seqw(0x22, (smtc_seqr(0x22) & (~0x30)));
		smtc_seqw(0x23, (smtc_seqr(0x23) & (~0xc0)));
		smtc_seqw(0x24, (smtc_seqr(0x24) | 0x01));
		smtc_seqw(0x31, (smtc_seqr(0x31) | 0x03));
		break;
	case FB_BLANK_NORMAL:
		/* Screen Off: HSync: On, VSync : On   Soft blank */
		smtc_seqw(0x01, (smtc_seqr(0x01) & (~0x20)));
		smtc_seqw(0x6a, 0x16);
		smtc_seqw(0x6b, 0x02);
		smtc_seqw(0x22, (smtc_seqr(0x22) & (~0x30)));
		smtc_seqw(0x23, (smtc_seqr(0x23) & (~0xc0)));
		smtc_seqw(0x24, (smtc_seqr(0x24) | 0x01));
		smtc_seqw(0x31, ((smtc_seqr(0x31) & (~0x07)) | 0x00));
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		/* Screen On: HSync: On, VSync : Off */
		smtc_seqw(0x01, (smtc_seqr(0x01) | 0x20));
		smtc_seqw(0x20, (smtc_seqr(0x20) & (~0xB0)));
		smtc_seqw(0x6a, 0x0c);
		smtc_seqw(0x6b, 0x02);
		smtc_seqw(0x21, (smtc_seqr(0x21) | 0x88));
		smtc_seqw(0x22, ((smtc_seqr(0x22) & (~0x30)) | 0x20));
		smtc_seqw(0x23, ((smtc_seqr(0x23) & (~0xc0)) | 0x20));
		smtc_seqw(0x24, (smtc_seqr(0x24) & (~0x01)));
		smtc_seqw(0x31, ((smtc_seqr(0x31) & (~0x07)) | 0x00));
		smtc_seqw(0x34, (smtc_seqr(0x34) | 0x80));
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		/* Screen On: HSync: Off, VSync : On */
		smtc_seqw(0x01, (smtc_seqr(0x01) | 0x20));
		smtc_seqw(0x20, (smtc_seqr(0x20) & (~0xB0)));
		smtc_seqw(0x6a, 0x0c);
		smtc_seqw(0x6b, 0x02);
		smtc_seqw(0x21, (smtc_seqr(0x21) | 0x88));
		smtc_seqw(0x22, ((smtc_seqr(0x22) & (~0x30)) | 0x10));
		smtc_seqw(0x23, ((smtc_seqr(0x23) & (~0xc0)) | 0xD8));
		smtc_seqw(0x24, (smtc_seqr(0x24) & (~0x01)));
		smtc_seqw(0x31, ((smtc_seqr(0x31) & (~0x07)) | 0x00));
		smtc_seqw(0x34, (smtc_seqr(0x34) | 0x80));
		break;
	case FB_BLANK_POWERDOWN:
		/* Screen On: HSync: Off, VSync : Off */
		smtc_seqw(0x01, (smtc_seqr(0x01) | 0x20));
		smtc_seqw(0x20, (smtc_seqr(0x20) & (~0xB0)));
		smtc_seqw(0x6a, 0x0c);
		smtc_seqw(0x6b, 0x02);
		smtc_seqw(0x21, (smtc_seqr(0x21) | 0x88));
		smtc_seqw(0x22, ((smtc_seqr(0x22) & (~0x30)) | 0x30));
		smtc_seqw(0x23, ((smtc_seqr(0x23) & (~0xc0)) | 0xD8));
		smtc_seqw(0x24, (smtc_seqr(0x24) & (~0x01)));
		smtc_seqw(0x31, ((smtc_seqr(0x31) & (~0x07)) | 0x00));
		smtc_seqw(0x34, (smtc_seqr(0x34) | 0x80));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int smtc_setcolreg(unsigned regno, unsigned red, unsigned green,
			  unsigned blue, unsigned trans, struct fb_info *info)
{
	struct smtcfb_info *sfb;
	u32 val;

	sfb = info->par;

	if (regno > 255)
		return 1;

	switch (sfb->fb.fix.visual) {
	case FB_VISUAL_DIRECTCOLOR:
	case FB_VISUAL_TRUECOLOR:
		/*
		 * 16/32 bit true-colour, use pseudo-palette for 16 base color
		 */
		if (regno < 16) {
			if (sfb->fb.var.bits_per_pixel == 16) {
				u32 *pal = sfb->fb.pseudo_palette;
				val = chan_to_field(red, &sfb->fb.var.red);
				val |= chan_to_field(green, \
						&sfb->fb.var.green);
				val |= chan_to_field(blue, &sfb->fb.var.blue);
#ifdef __BIG_ENDIAN
				pal[regno] =
				    ((red & 0xf800) >> 8) |
				    ((green & 0xe000) >> 13) |
				    ((green & 0x1c00) << 3) |
				    ((blue & 0xf800) >> 3);
#else
				pal[regno] = val;
#endif
			} else {
				u32 *pal = sfb->fb.pseudo_palette;
				val = chan_to_field(red, &sfb->fb.var.red);
				val |= chan_to_field(green, \
						&sfb->fb.var.green);
				val |= chan_to_field(blue, &sfb->fb.var.blue);
#ifdef __BIG_ENDIAN
				val =
				    (val & 0xff00ff00 >> 8) |
				    (val & 0x00ff00ff << 8);
#endif
				pal[regno] = val;
			}
		}
		break;

	case FB_VISUAL_PSEUDOCOLOR:
		/* color depth 8 bit */
		sm712_setpalette(regno, red, green, blue, info);
		break;

	default:
		return 1;	/* unknown type */
	}

	return 0;

}

#ifdef __BIG_ENDIAN
static ssize_t smtcfb_read(struct fb_info *info, char __user *buf, size_t
				count, loff_t *ppos)
{
	unsigned long p = *ppos;

	u32 *buffer, *dst;
	u32 __iomem *src;
	int c, i, cnt = 0, err = 0;
	unsigned long total_size;

	if (!info || !info->screen_base)
		return -ENODEV;

	if (info->state != FBINFO_STATE_RUNNING)
		return -EPERM;

	total_size = info->screen_size;

	if (total_size == 0)
		total_size = info->fix.smem_len;

	if (p >= total_size)
		return 0;

	if (count >= total_size)
		count = total_size;

	if (count + p > total_size)
		count = total_size - p;

	buffer = kmalloc((count > PAGE_SIZE) ? PAGE_SIZE : count, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	src = (u32 __iomem *) (info->screen_base + p);

	if (info->fbops->fb_sync)
		info->fbops->fb_sync(info);

	while (count) {
		c = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		dst = buffer;
		for (i = c >> 2; i--;) {
			*dst = fb_readl(src++);
			*dst =
			    (*dst & 0xff00ff00 >> 8) |
			    (*dst & 0x00ff00ff << 8);
			dst++;
		}
		if (c & 3) {
			u8 *dst8 = (u8 *) dst;
			u8 __iomem *src8 = (u8 __iomem *) src;

			for (i = c & 3; i--;) {
				if (i & 1) {
					*dst8++ = fb_readb(++src8);
				} else {
					*dst8++ = fb_readb(--src8);
					src8 += 2;
				}
			}
			src = (u32 __iomem *) src8;
		}

		if (copy_to_user(buf, buffer, c)) {
			err = -EFAULT;
			break;
		}
		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

	kfree(buffer);

	return (err) ? err : cnt;
}

static ssize_t
smtcfb_write(struct fb_info *info, const char __user *buf, size_t count,
	     loff_t *ppos)
{
	unsigned long p = *ppos;

	u32 *buffer, *src;
	u32 __iomem *dst;
	int c, i, cnt = 0, err = 0;
	unsigned long total_size;

	if (!info || !info->screen_base)
		return -ENODEV;

	if (info->state != FBINFO_STATE_RUNNING)
		return -EPERM;

	total_size = info->screen_size;

	if (total_size == 0)
		total_size = info->fix.smem_len;

	if (p > total_size)
		return -EFBIG;

	if (count > total_size) {
		err = -EFBIG;
		count = total_size;
	}

	if (count + p > total_size) {
		if (!err)
			err = -ENOSPC;

		count = total_size - p;
	}

	buffer = kmalloc((count > PAGE_SIZE) ? PAGE_SIZE : count, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	dst = (u32 __iomem *) (info->screen_base + p);

	if (info->fbops->fb_sync)
		info->fbops->fb_sync(info);

	while (count) {
		c = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		src = buffer;

		if (copy_from_user(src, buf, c)) {
			err = -EFAULT;
			break;
		}

		for (i = c >> 2; i--;) {
			fb_writel((*src & 0xff00ff00 >> 8) |
				  (*src & 0x00ff00ff << 8), dst++);
			src++;
		}
		if (c & 3) {
			u8 *src8 = (u8 *) src;
			u8 __iomem *dst8 = (u8 __iomem *) dst;

			for (i = c & 3; i--;) {
				if (i & 1) {
					fb_writeb(*src8++, ++dst8);
				} else {
					fb_writeb(*src8++, --dst8);
					dst8 += 2;
				}
			}
			dst = (u32 __iomem *) dst8;
		}

		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

	kfree(buffer);

	return (cnt) ? cnt : err;
}
#endif	/* ! __BIG_ENDIAN */

static void sm7xx_set_timing(struct smtcfb_info *sfb)
{
	int i = 0, j = 0;
	u32 m_nScreenStride;

	dev_dbg(&sfb->pdev->dev,
		"sfb->width=%d sfb->height=%d "
		"sfb->fb.var.bits_per_pixel=%d sfb->hz=%d\n",
		sfb->width, sfb->height, sfb->fb.var.bits_per_pixel, sfb->hz);

	for (j = 0; j < numVGAModes; j++) {
		if (VGAMode[j].mmSizeX == sfb->width &&
		    VGAMode[j].mmSizeY == sfb->height &&
		    VGAMode[j].bpp == sfb->fb.var.bits_per_pixel &&
		    VGAMode[j].hz == sfb->hz) {

			dev_dbg(&sfb->pdev->dev,
				"VGAMode[j].mmSizeX=%d VGAMode[j].mmSizeY=%d "
				"VGAMode[j].bpp=%d VGAMode[j].hz=%d\n",
				VGAMode[j].mmSizeX, VGAMode[j].mmSizeY,
				VGAMode[j].bpp, VGAMode[j].hz);

			dev_dbg(&sfb->pdev->dev, "VGAMode index=%d\n", j);

			smtc_mmiowb(0x0, 0x3c6);

			smtc_seqw(0, 0x1);

			smtc_mmiowb(VGAMode[j].Init_MISC, 0x3c2);

			/* init SEQ register SR00 - SR04 */
			for (i = 0; i < SIZE_SR00_SR04; i++)
				smtc_seqw(i, VGAMode[j].Init_SR00_SR04[i]);

			/* init SEQ register SR10 - SR24 */
			for (i = 0; i < SIZE_SR10_SR24; i++)
				smtc_seqw(i + 0x10,
					  VGAMode[j].Init_SR10_SR24[i]);

			/* init SEQ register SR30 - SR75 */
			for (i = 0; i < SIZE_SR30_SR75; i++)
				if (((i + 0x30) != 0x62) \
					&& ((i + 0x30) != 0x6a) \
					&& ((i + 0x30) != 0x6b))
					smtc_seqw(i + 0x30,
						VGAMode[j].Init_SR30_SR75[i]);

			/* init SEQ register SR80 - SR93 */
			for (i = 0; i < SIZE_SR80_SR93; i++)
				smtc_seqw(i + 0x80,
					  VGAMode[j].Init_SR80_SR93[i]);

			/* init SEQ register SRA0 - SRAF */
			for (i = 0; i < SIZE_SRA0_SRAF; i++)
				smtc_seqw(i + 0xa0,
					  VGAMode[j].Init_SRA0_SRAF[i]);

			/* init Graphic register GR00 - GR08 */
			for (i = 0; i < SIZE_GR00_GR08; i++)
				smtc_grphw(i, VGAMode[j].Init_GR00_GR08[i]);

			/* init Attribute register AR00 - AR14 */
			for (i = 0; i < SIZE_AR00_AR14; i++)
				smtc_attrw(i, VGAMode[j].Init_AR00_AR14[i]);

			/* init CRTC register CR00 - CR18 */
			for (i = 0; i < SIZE_CR00_CR18; i++)
				smtc_crtcw(i, VGAMode[j].Init_CR00_CR18[i]);

			/* init CRTC register CR30 - CR4D */
			for (i = 0; i < SIZE_CR30_CR4D; i++)
				smtc_crtcw(i + 0x30,
					   VGAMode[j].Init_CR30_CR4D[i]);

			/* init CRTC register CR90 - CRA7 */
			for (i = 0; i < SIZE_CR90_CRA7; i++)
				smtc_crtcw(i + 0x90,
					   VGAMode[j].Init_CR90_CRA7[i]);
		}
	}
	smtc_mmiowb(0x67, 0x3c2);

	/* set VPR registers */
	writel(0x0, sfb->vp_regs + 0x0C);
	writel(0x0, sfb->vp_regs + 0x40);

	/* set data width */
	m_nScreenStride =
		(sfb->width * sfb->fb.var.bits_per_pixel) / 64;
	switch (sfb->fb.var.bits_per_pixel) {
	case 8:
		writel(0x0, sfb->vp_regs + 0x0);
		break;
	case 16:
		writel(0x00020000, sfb->vp_regs + 0x0);
		break;
	case 24:
		writel(0x00040000, sfb->vp_regs + 0x0);
		break;
	case 32:
		writel(0x00030000, sfb->vp_regs + 0x0);
		break;
	}
	writel((u32) (((m_nScreenStride + 2) << 16) | m_nScreenStride),
	       sfb->vp_regs + 0x10);

}

static void smtc_set_timing(struct smtcfb_info *sfb)
{
	switch (sfb->chip_id) {
	case 0x710:
	case 0x712:
	case 0x720:
		sm7xx_set_timing(sfb);
		break;
	}
}

void smtcfb_setmode(struct smtcfb_info *sfb)
{
	switch (sfb->fb.var.bits_per_pixel) {
	case 32:
		sfb->fb.fix.visual       = FB_VISUAL_TRUECOLOR;
		sfb->fb.fix.line_length  = sfb->fb.var.xres * 4;
		sfb->fb.var.red.length   = 8;
		sfb->fb.var.green.length = 8;
		sfb->fb.var.blue.length  = 8;
		sfb->fb.var.red.offset   = 16;
		sfb->fb.var.green.offset = 8;
		sfb->fb.var.blue.offset  = 0;
		break;
	case 24:
		sfb->fb.fix.visual       = FB_VISUAL_TRUECOLOR;
		sfb->fb.fix.line_length  = sfb->fb.var.xres * 3;
		sfb->fb.var.red.length   = 8;
		sfb->fb.var.green.length = 8;
		sfb->fb.var.blue.length  = 8;
		sfb->fb.var.red.offset   = 16;
		sfb->fb.var.green.offset = 8;
		sfb->fb.var.blue.offset  = 0;
		break;
	case 8:
		sfb->fb.fix.visual       = FB_VISUAL_PSEUDOCOLOR;
		sfb->fb.fix.line_length  = sfb->fb.var.xres;
		sfb->fb.var.red.length   = 3;
		sfb->fb.var.green.length = 3;
		sfb->fb.var.blue.length  = 2;
		sfb->fb.var.red.offset   = 5;
		sfb->fb.var.green.offset = 2;
		sfb->fb.var.blue.offset  = 0;
		break;
	case 16:
	default:
		sfb->fb.fix.visual       = FB_VISUAL_TRUECOLOR;
		sfb->fb.fix.line_length  = sfb->fb.var.xres * 2;
		sfb->fb.var.red.length   = 5;
		sfb->fb.var.green.length = 6;
		sfb->fb.var.blue.length  = 5;
		sfb->fb.var.red.offset   = 11;
		sfb->fb.var.green.offset = 5;
		sfb->fb.var.blue.offset  = 0;
		break;
	}

	sfb->width  = sfb->fb.var.xres;
	sfb->height = sfb->fb.var.yres;
	sfb->hz = 60;
	smtc_set_timing(sfb);
}

static int smtc_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	/* sanity checks */
	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;

	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	/* set valid default bpp */
	if ((var->bits_per_pixel != 8)  && (var->bits_per_pixel != 16) &&
	    (var->bits_per_pixel != 24) && (var->bits_per_pixel != 32))
		var->bits_per_pixel = 16;

	return 0;
}

static int smtc_set_par(struct fb_info *info)
{
	smtcfb_setmode(info->par);

	return 0;
}

static struct fb_ops smtcfb_ops = {
	.owner        = THIS_MODULE,
	.fb_check_var = smtc_check_var,
	.fb_set_par   = smtc_set_par,
	.fb_setcolreg = smtc_setcolreg,
	.fb_blank     = smtc_blank,
	.fb_fillrect  = cfb_fillrect,
	.fb_imageblit = cfb_imageblit,
	.fb_copyarea  = cfb_copyarea,
#ifdef __BIG_ENDIAN
	.fb_read      = smtcfb_read,
	.fb_write     = smtcfb_write,
#endif
};

/*
 * alloc struct smtcfb_info and assign default values
 */
static struct smtcfb_info *smtc_alloc_fb_info(struct pci_dev *pdev)
{
	struct smtcfb_info *sfb;

	sfb = kzalloc(sizeof(*sfb), GFP_KERNEL);

	if (!sfb)
		return NULL;

	sfb->pdev = pdev;

	sfb->fb.flags          = FBINFO_FLAG_DEFAULT;
	sfb->fb.fbops          = &smtcfb_ops;
	sfb->fb.fix            = smtcfb_fix;
	sfb->fb.var            = smtcfb_var;
	sfb->fb.pseudo_palette = sfb->colreg;
	sfb->fb.par            = sfb;

	return sfb;
}

/*
 * free struct smtcfb_info
 */
static void smtc_free_fb_info(struct smtcfb_info *sfb)
{
	kfree(sfb);
}

/*
 * Unmap in the memory mapped IO registers
 */

static void smtc_unmap_mmio(struct smtcfb_info *sfb)
{
	if (sfb && smtc_RegBaseAddress)
		smtc_RegBaseAddress = NULL;
}

/*
 * Map in the screen memory
 */

static int smtc_map_smem(struct smtcfb_info *sfb,
		struct pci_dev *pdev, u_long smem_len)
{

	sfb->fb.fix.smem_start = pci_resource_start(pdev, 0);

#ifdef __BIG_ENDIAN
	if (sfb->fb.var.bits_per_pixel == 32)
		sfb->fb.fix.smem_start += 0x800000;
#endif

	sfb->fb.fix.smem_len = smem_len;

	sfb->fb.screen_base = smtc_VRAMBaseAddress;

	if (!sfb->fb.screen_base) {
		dev_err(&pdev->dev,
			"%s: unable to map screen memory\n", sfb->fb.fix.id);
		return -ENOMEM;
	}

	return 0;
}

/*
 * Unmap in the screen memory
 *
 */
static void smtc_unmap_smem(struct smtcfb_info *sfb)
{
	if (sfb && sfb->fb.screen_base) {
		iounmap(sfb->fb.screen_base);
		sfb->fb.screen_base = NULL;
	}
}

/*
 * We need to wake up the device and make sure its in linear memory mode.
 */
static inline void sm7xx_init_hw(void)
{
	outb_p(0x18, 0x3c4);
	outb_p(0x11, 0x3c5);
}

static int __devinit smtcfb_pci_probe(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	struct smtcfb_info *sfb;
	u_long smem_size = 0x00800000;	/* default 8MB */
	int err;
	unsigned long mmio_base;

	dev_info(&pdev->dev, "Silicon Motion display driver.");

	err = pci_enable_device(pdev);	/* enable SMTC chip */
	if (err)
		return err;

	sprintf(smtcfb_fix.id, "sm%Xfb", ent->device);

	sfb = smtc_alloc_fb_info(pdev);

	if (!sfb) {
		err = -ENOMEM;
		goto failed_free;
	}

	sfb->chip_id = ent->device;

	pci_set_drvdata(pdev, sfb);

	sm7xx_init_hw();

	/* get mode parameter from smtc_scr_info */
	if (smtc_scr_info.lfb_width != 0) {
		sfb->fb.var.xres = smtc_scr_info.lfb_width;
		sfb->fb.var.yres = smtc_scr_info.lfb_height;
		sfb->fb.var.bits_per_pixel = smtc_scr_info.lfb_depth;
	} else {
		/* default resolution 1024x600 16bit mode */
		sfb->fb.var.xres = SCREEN_X_RES;
		sfb->fb.var.yres = SCREEN_Y_RES;
		sfb->fb.var.bits_per_pixel = SCREEN_BPP;
	}

#ifdef __BIG_ENDIAN
	if (sfb->fb.var.bits_per_pixel == 24)
		sfb->fb.var.bits_per_pixel = (smtc_scr_info.lfb_depth = 32);
#endif
	/* Map address and memory detection */
	mmio_base = pci_resource_start(pdev, 0);
	pci_read_config_byte(pdev, PCI_REVISION_ID, &sfb->chip_rev_id);

	switch (sfb->chip_id) {
	case 0x710:
	case 0x712:
		sfb->fb.fix.mmio_start = mmio_base + 0x00400000;
		sfb->fb.fix.mmio_len = 0x00400000;
		smem_size = SM712_VIDEOMEMORYSIZE;
#ifdef __BIG_ENDIAN
		sfb->lfb = (smtc_VRAMBaseAddress =
		    ioremap(mmio_base, 0x00c00000));
#else
		sfb->lfb = (smtc_VRAMBaseAddress =
		    ioremap(mmio_base, 0x00800000));
#endif
		sfb->mmio = (smtc_RegBaseAddress =
		    smtc_VRAMBaseAddress + 0x00700000);
		sfb->dp_regs = smtc_VRAMBaseAddress + 0x00408000;
		sfb->vp_regs = sfb->lfb + 0x0040c000;
#ifdef __BIG_ENDIAN
		if (sfb->fb.var.bits_per_pixel == 32) {
			smtc_VRAMBaseAddress += 0x800000;
			sfb->lfb += 0x800000;
			dev_info(&pdev->dev,
				 "smtc_VRAMBaseAddress=%p sfb->lfb=%p",
				  smtc_VRAMBaseAddress, sfb->lfb);
		}
#endif
		if (!smtc_RegBaseAddress) {
			dev_err(&pdev->dev,
				"%s: unable to map memory mapped IO!",
				sfb->fb.fix.id);
			err = -ENOMEM;
			goto failed_fb;
		}

		/* set MCLK = 14.31818 * (0x16 / 0x2) */
		smtc_seqw(0x6a, 0x16);
		smtc_seqw(0x6b, 0x02);
		smtc_seqw(0x62, 0x3e);
		/* enable PCI burst */
		smtc_seqw(0x17, 0x20);
		/* enable word swap */
#ifdef __BIG_ENDIAN
		if (sfb->fb.var.bits_per_pixel == 32)
			smtc_seqw(0x17, 0x30);
#endif
		break;
	case 0x720:
		sfb->fb.fix.mmio_start = mmio_base;
		sfb->fb.fix.mmio_len = 0x00200000;
		smem_size = SM722_VIDEOMEMORYSIZE;
		sfb->dp_regs = ioremap(mmio_base, 0x00a00000);
		sfb->lfb = (smtc_VRAMBaseAddress =
		    sfb->dp_regs + 0x00200000);
		sfb->mmio = (smtc_RegBaseAddress =
		    sfb->dp_regs + 0x000c0000);
		sfb->vp_regs = sfb->dp_regs + 0x800;

		smtc_seqw(0x62, 0xff);
		smtc_seqw(0x6a, 0x0d);
		smtc_seqw(0x6b, 0x02);
		break;
	default:
		dev_err(&pdev->dev,
			"No valid Silicon Motion display chip was detected!");

		goto failed_fb;
	}

	/* can support 32 bpp */
	if (15 == sfb->fb.var.bits_per_pixel)
		sfb->fb.var.bits_per_pixel = 16;

	sfb->fb.var.xres_virtual = sfb->fb.var.xres;
	sfb->fb.var.yres_virtual = sfb->fb.var.yres;
	err = smtc_map_smem(sfb, pdev, smem_size);
	if (err)
		goto failed;

	smtcfb_setmode(sfb);

	err = register_framebuffer(&sfb->fb);
	if (err < 0)
		goto failed;

	dev_info(&pdev->dev,
		 "Silicon Motion SM%X Rev%X primary display mode %dx%d-%d Init Complete.",
		 sfb->chip_id, sfb->chip_rev_id, sfb->fb.var.xres,
		 sfb->fb.var.yres, sfb->fb.var.bits_per_pixel);

	return 0;

failed:
	dev_err(&pdev->dev, "Silicon Motion, Inc. primary display init fail.");

	smtc_unmap_smem(sfb);
	smtc_unmap_mmio(sfb);
failed_fb:
	smtc_free_fb_info(sfb);

failed_free:
	pci_disable_device(pdev);

	return err;
}

/*
 * 0x710 (LynxEM)
 * 0x712 (LynxEM+)
 * 0x720 (Lynx3DM, Lynx3DM+)
 */
static DEFINE_PCI_DEVICE_TABLE(smtcfb_pci_table) = {
	{ PCI_DEVICE(0x126f, 0x710), },
	{ PCI_DEVICE(0x126f, 0x712), },
	{ PCI_DEVICE(0x126f, 0x720), },
	{0,}
};

static void __devexit smtcfb_pci_remove(struct pci_dev *pdev)
{
	struct smtcfb_info *sfb;

	sfb = pci_get_drvdata(pdev);
	pci_set_drvdata(pdev, NULL);
	smtc_unmap_smem(sfb);
	smtc_unmap_mmio(sfb);
	unregister_framebuffer(&sfb->fb);
	smtc_free_fb_info(sfb);
}

#ifdef CONFIG_PM
static int smtcfb_pci_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct smtcfb_info *sfb;

	sfb = pci_get_drvdata(pdev);

	/* set the hw in sleep mode use external clock and self memory refresh
	 * so that we can turn off internal PLLs later on
	 */
	smtc_seqw(0x20, (smtc_seqr(0x20) | 0xc0));
	smtc_seqw(0x69, (smtc_seqr(0x69) & 0xf7));

	console_lock();
	fb_set_suspend(&sfb->fb, 1);
	console_unlock();

	/* additionally turn off all function blocks including internal PLLs */
	smtc_seqw(0x21, 0xff);

	return 0;
}

static int smtcfb_pci_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct smtcfb_info *sfb;

	sfb = pci_get_drvdata(pdev);

	/* reinit hardware */
	sm7xx_init_hw();
	switch (sfb->chip_id) {
	case 0x710:
	case 0x712:
		/* set MCLK = 14.31818 *  (0x16 / 0x2) */
		smtc_seqw(0x6a, 0x16);
		smtc_seqw(0x6b, 0x02);
		smtc_seqw(0x62, 0x3e);
		/* enable PCI burst */
		smtc_seqw(0x17, 0x20);
#ifdef __BIG_ENDIAN
		if (sfb->fb.var.bits_per_pixel == 32)
			smtc_seqw(0x17, 0x30);
#endif
		break;
	case 0x720:
		smtc_seqw(0x62, 0xff);
		smtc_seqw(0x6a, 0x0d);
		smtc_seqw(0x6b, 0x02);
		break;
	}

	smtc_seqw(0x34, (smtc_seqr(0x34) | 0xc0));
	smtc_seqw(0x33, ((smtc_seqr(0x33) | 0x08) & 0xfb));

	smtcfb_setmode(sfb);

	console_lock();
	fb_set_suspend(&sfb->fb, 0);
	console_unlock();

	return 0;
}

static const struct dev_pm_ops sm7xx_pm_ops = {
	.suspend = smtcfb_pci_suspend,
	.resume = smtcfb_pci_resume,
	.freeze = smtcfb_pci_suspend,
	.thaw = smtcfb_pci_resume,
	.poweroff = smtcfb_pci_suspend,
	.restore = smtcfb_pci_resume,
};

#define SM7XX_PM_OPS (&sm7xx_pm_ops)

#else  /* !CONFIG_PM */

#define SM7XX_PM_OPS NULL

#endif /* !CONFIG_PM */

static struct pci_driver smtcfb_driver = {
	.name = "smtcfb",
	.id_table = smtcfb_pci_table,
	.probe = smtcfb_pci_probe,
	.remove = __devexit_p(smtcfb_pci_remove),
	.driver.pm  = SM7XX_PM_OPS,
};

module_pci_driver(smtcfb_driver);

MODULE_AUTHOR("Siliconmotion ");
MODULE_DESCRIPTION("Framebuffer driver for SMI Graphic Cards");
MODULE_LICENSE("GPL");
