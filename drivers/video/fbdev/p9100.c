// SPDX-License-Identifier: GPL-2.0-only
/* p9100.c: P9100 frame buffer driver
 *
 * Copyright (C) 2003, 2006 David S. Miller (davem@davemloft.net)
 * Copyright 1999 Derrick J Brashear (shadow@dementia.org)
 *
 * Driver layout based loosely on tgafb.c, see that file for credits.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/fbio.h>

#include "sbuslib.h"

/*
 * Local functions.
 */

static int p9100_setcolreg(unsigned, unsigned, unsigned, unsigned,
			   unsigned, struct fb_info *);
static int p9100_blank(int, struct fb_info *);

static int p9100_sbusfb_mmap(struct fb_info *info, struct vm_area_struct *vma);
static int p9100_sbusfb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg);

/*
 *  Frame buffer operations
 */

static const struct fb_ops p9100_ops = {
	.owner			= THIS_MODULE,
	FB_DEFAULT_SBUS_OPS(p9100),
	.fb_setcolreg		= p9100_setcolreg,
	.fb_blank		= p9100_blank,
};

/* P9100 control registers */
#define P9100_SYSCTL_OFF	0x0UL
#define P9100_VIDEOCTL_OFF	0x100UL
#define P9100_VRAMCTL_OFF 	0x180UL
#define P9100_RAMDAC_OFF 	0x200UL
#define P9100_VIDEOCOPROC_OFF 	0x400UL

/* P9100 command registers */
#define P9100_CMD_OFF 0x0UL

/* P9100 framebuffer memory */
#define P9100_FB_OFF 0x0UL

/* 3 bits: 2=8bpp 3=16bpp 5=32bpp 7=24bpp */
#define SYS_CONFIG_PIXELSIZE_SHIFT 26

#define SCREENPAINT_TIMECTL1_ENABLE_VIDEO 0x20 /* 0 = off, 1 = on */

struct p9100_regs {
	/* Registers for the system control */
	u32 sys_base;
	u32 sys_config;
	u32 sys_intr;
	u32 sys_int_ena;
	u32 sys_alt_rd;
	u32 sys_alt_wr;
	u32 sys_xxx[58];

	/* Registers for the video control */
	u32 vid_base;
	u32 vid_hcnt;
	u32 vid_htotal;
	u32 vid_hsync_rise;
	u32 vid_hblank_rise;
	u32 vid_hblank_fall;
	u32 vid_hcnt_preload;
	u32 vid_vcnt;
	u32 vid_vlen;
	u32 vid_vsync_rise;
	u32 vid_vblank_rise;
	u32 vid_vblank_fall;
	u32 vid_vcnt_preload;
	u32 vid_screenpaint_addr;
	u32 vid_screenpaint_timectl1;
	u32 vid_screenpaint_qsfcnt;
	u32 vid_screenpaint_timectl2;
	u32 vid_xxx[15];

	/* Registers for the video control */
	u32 vram_base;
	u32 vram_memcfg;
	u32 vram_refresh_pd;
	u32 vram_refresh_cnt;
	u32 vram_raslo_max;
	u32 vram_raslo_cur;
	u32 pwrup_cfg;
	u32 vram_xxx[25];

	/* Registers for IBM RGB528 Palette */
	u32 ramdac_cmap_wridx;
	u32 ramdac_palette_data;
	u32 ramdac_pixel_mask;
	u32 ramdac_palette_rdaddr;
	u32 ramdac_idx_lo;
	u32 ramdac_idx_hi;
	u32 ramdac_idx_data;
	u32 ramdac_idx_ctl;
	u32 ramdac_xxx[1784];
};

struct p9100_cmd_parameng {
	u32 parameng_status;
	u32 parameng_bltcmd;
	u32 parameng_quadcmd;
};

struct p9100_par {
	spinlock_t		lock;
	struct p9100_regs	__iomem *regs;

	u32			flags;
#define P9100_FLAG_BLANKED	0x00000001

	unsigned long		which_io;
};

/**
 *      p9100_setcolreg - Optional function. Sets a color register.
 *      @regno: boolean, 0 copy local, 1 get_user() function
 *      @red: frame buffer colormap structure
 *      @green: The green value which can be up to 16 bits wide
 *      @blue:  The blue value which can be up to 16 bits wide.
 *      @transp: If supported the alpha value which can be up to 16 bits wide.
 *      @info: frame buffer info structure
 */
static int p9100_setcolreg(unsigned regno,
			   unsigned red, unsigned green, unsigned blue,
			   unsigned transp, struct fb_info *info)
{
	struct p9100_par *par = (struct p9100_par *) info->par;
	struct p9100_regs __iomem *regs = par->regs;
	unsigned long flags;

	if (regno >= 256)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	spin_lock_irqsave(&par->lock, flags);

	sbus_writel((regno << 16), &regs->ramdac_cmap_wridx);
	sbus_writel((red << 16), &regs->ramdac_palette_data);
	sbus_writel((green << 16), &regs->ramdac_palette_data);
	sbus_writel((blue << 16), &regs->ramdac_palette_data);

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

/**
 *      p9100_blank - Optional function.  Blanks the display.
 *      @blank: the blank mode we want.
 *      @info: frame buffer structure that represents a single frame buffer
 */
static int
p9100_blank(int blank, struct fb_info *info)
{
	struct p9100_par *par = (struct p9100_par *) info->par;
	struct p9100_regs __iomem *regs = par->regs;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&par->lock, flags);

	switch (blank) {
	case FB_BLANK_UNBLANK: /* Unblanking */
		val = sbus_readl(&regs->vid_screenpaint_timectl1);
		val |= SCREENPAINT_TIMECTL1_ENABLE_VIDEO;
		sbus_writel(val, &regs->vid_screenpaint_timectl1);
		par->flags &= ~P9100_FLAG_BLANKED;
		break;

	case FB_BLANK_NORMAL: /* Normal blanking */
	case FB_BLANK_VSYNC_SUSPEND: /* VESA blank (vsync off) */
	case FB_BLANK_HSYNC_SUSPEND: /* VESA blank (hsync off) */
	case FB_BLANK_POWERDOWN: /* Poweroff */
		val = sbus_readl(&regs->vid_screenpaint_timectl1);
		val &= ~SCREENPAINT_TIMECTL1_ENABLE_VIDEO;
		sbus_writel(val, &regs->vid_screenpaint_timectl1);
		par->flags |= P9100_FLAG_BLANKED;
		break;
	}

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

static const struct sbus_mmap_map p9100_mmap_map[] = {
	{ CG3_MMAP_OFFSET,	0,		SBUS_MMAP_FBSIZE(1) },
	{ 0,			0,		0		    }
};

static int p9100_sbusfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct p9100_par *par = (struct p9100_par *)info->par;

	return sbusfb_mmap_helper(p9100_mmap_map,
				  info->fix.smem_start, info->fix.smem_len,
				  par->which_io, vma);
}

static int p9100_sbusfb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	/* Make it look like a cg3. */
	return sbusfb_ioctl_helper(cmd, arg, info,
				   FBTYPE_SUN3COLOR, 8, info->fix.smem_len);
}

/*
 *  Initialisation
 */

static void p9100_init_fix(struct fb_info *info, int linebytes, struct device_node *dp)
{
	snprintf(info->fix.id, sizeof(info->fix.id), "%pOFn", dp);

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;

	info->fix.line_length = linebytes;

	info->fix.accel = FB_ACCEL_SUN_CGTHREE;
}

static int p9100_probe(struct platform_device *op)
{
	struct device_node *dp = op->dev.of_node;
	struct fb_info *info;
	struct p9100_par *par;
	int linebytes, err;

	info = framebuffer_alloc(sizeof(struct p9100_par), &op->dev);

	err = -ENOMEM;
	if (!info)
		goto out_err;
	par = info->par;

	spin_lock_init(&par->lock);

	/* This is the framebuffer and the only resource apps can mmap.  */
	info->fix.smem_start = op->resource[2].start;
	par->which_io = op->resource[2].flags & IORESOURCE_BITS;

	sbusfb_fill_var(&info->var, dp, 8);
	info->var.red.length = 8;
	info->var.green.length = 8;
	info->var.blue.length = 8;

	linebytes = of_getintprop_default(dp, "linebytes", info->var.xres);
	info->fix.smem_len = PAGE_ALIGN(linebytes * info->var.yres);

	par->regs = of_ioremap(&op->resource[0], 0,
			       sizeof(struct p9100_regs), "p9100 regs");
	if (!par->regs)
		goto out_release_fb;

	info->fbops = &p9100_ops;
	info->screen_base = of_ioremap(&op->resource[2], 0,
				       info->fix.smem_len, "p9100 ram");
	if (!info->screen_base)
		goto out_unmap_regs;

	p9100_blank(FB_BLANK_UNBLANK, info);

	if (fb_alloc_cmap(&info->cmap, 256, 0))
		goto out_unmap_screen;

	p9100_init_fix(info, linebytes, dp);

	err = register_framebuffer(info);
	if (err < 0)
		goto out_dealloc_cmap;

	fb_set_cmap(&info->cmap, info);

	dev_set_drvdata(&op->dev, info);

	printk(KERN_INFO "%pOF: p9100 at %lx:%lx\n",
	       dp,
	       par->which_io, info->fix.smem_start);

	return 0;

out_dealloc_cmap:
	fb_dealloc_cmap(&info->cmap);

out_unmap_screen:
	of_iounmap(&op->resource[2], info->screen_base, info->fix.smem_len);

out_unmap_regs:
	of_iounmap(&op->resource[0], par->regs, sizeof(struct p9100_regs));

out_release_fb:
	framebuffer_release(info);

out_err:
	return err;
}

static void p9100_remove(struct platform_device *op)
{
	struct fb_info *info = dev_get_drvdata(&op->dev);
	struct p9100_par *par = info->par;

	unregister_framebuffer(info);
	fb_dealloc_cmap(&info->cmap);

	of_iounmap(&op->resource[0], par->regs, sizeof(struct p9100_regs));
	of_iounmap(&op->resource[2], info->screen_base, info->fix.smem_len);

	framebuffer_release(info);
}

static const struct of_device_id p9100_match[] = {
	{
		.name = "p9100",
	},
	{},
};
MODULE_DEVICE_TABLE(of, p9100_match);

static struct platform_driver p9100_driver = {
	.driver = {
		.name = "p9100",
		.of_match_table = p9100_match,
	},
	.probe		= p9100_probe,
	.remove		= p9100_remove,
};

static int __init p9100_init(void)
{
	if (fb_get_options("p9100fb", NULL))
		return -ENODEV;

	return platform_driver_register(&p9100_driver);
}

static void __exit p9100_exit(void)
{
	platform_driver_unregister(&p9100_driver);
}

module_init(p9100_init);
module_exit(p9100_exit);

MODULE_DESCRIPTION("framebuffer driver for P9100 chipsets");
MODULE_AUTHOR("David S. Miller <davem@davemloft.net>");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
