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
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/of_device.h>
#include <asm/fbio.h>

#include "sbuslib.h"

/*
 * Local functions.
 */

static int p9100_setcolreg(unsigned, unsigned, unsigned, unsigned,
			   unsigned, struct fb_info *);
static int p9100_blank(int, struct fb_info *);

static int p9100_mmap(struct fb_info *, struct vm_area_struct *);
static int p9100_ioctl(struct fb_info *, unsigned int, unsigned long);

/*
 *  Frame buffer operations
 */

static struct fb_ops p9100_ops = {
	.owner			= THIS_MODULE,
	.fb_setcolreg		= p9100_setcolreg,
	.fb_blank		= p9100_blank,
	.fb_fillrect		= cfb_fillrect,
	.fb_copyarea		= cfb_copyarea,
	.fb_imageblit		= cfb_imageblit,
	.fb_mmap		= p9100_mmap,
	.fb_ioctl		= p9100_ioctl,
#ifdef CONFIG_COMPAT
	.fb_compat_ioctl	= sbusfb_compat_ioctl,
#endif
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

	unsigned long		physbase;
	unsigned long		which_io;
	unsigned long		fbsize;
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
 *      @blank_mode: the blank mode we want.
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

static struct sbus_mmap_map p9100_mmap_map[] = {
	{ CG3_MMAP_OFFSET,	0,		SBUS_MMAP_FBSIZE(1) },
	{ 0,			0,		0		    }
};

static int p9100_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct p9100_par *par = (struct p9100_par *)info->par;

	return sbusfb_mmap_helper(p9100_mmap_map,
				  par->physbase, par->fbsize,
				  par->which_io, vma);
}

static int p9100_ioctl(struct fb_info *info, unsigned int cmd,
		       unsigned long arg)
{
	struct p9100_par *par = (struct p9100_par *) info->par;

	/* Make it look like a cg3. */
	return sbusfb_ioctl_helper(cmd, arg, info,
				   FBTYPE_SUN3COLOR, 8, par->fbsize);
}

/*
 *  Initialisation
 */

static void p9100_init_fix(struct fb_info *info, int linebytes, struct device_node *dp)
{
	strlcpy(info->fix.id, dp->name, sizeof(info->fix.id));

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;

	info->fix.line_length = linebytes;

	info->fix.accel = FB_ACCEL_SUN_CGTHREE;
}

struct all_info {
	struct fb_info info;
	struct p9100_par par;
};

static int __devinit p9100_init_one(struct of_device *op)
{
	struct device_node *dp = op->node;
	struct all_info *all;
	int linebytes, err;

	all = kzalloc(sizeof(*all), GFP_KERNEL);
	if (!all)
		return -ENOMEM;

	spin_lock_init(&all->par.lock);

	/* This is the framebuffer and the only resource apps can mmap.  */
	all->par.physbase = op->resource[2].start;
	all->par.which_io = op->resource[2].flags & IORESOURCE_BITS;

	sbusfb_fill_var(&all->info.var, dp->node, 8);
	all->info.var.red.length = 8;
	all->info.var.green.length = 8;
	all->info.var.blue.length = 8;

	linebytes = of_getintprop_default(dp, "linebytes",
					  all->info.var.xres);
	all->par.fbsize = PAGE_ALIGN(linebytes * all->info.var.yres);

	all->par.regs = of_ioremap(&op->resource[0], 0,
				   sizeof(struct p9100_regs), "p9100 regs");
	if (!all->par.regs) {
		kfree(all);
		return -ENOMEM;
	}

	all->info.flags = FBINFO_DEFAULT;
	all->info.fbops = &p9100_ops;
	all->info.screen_base = of_ioremap(&op->resource[2], 0,
					   all->par.fbsize, "p9100 ram");
	if (!all->info.screen_base) {
		of_iounmap(&op->resource[0],
			   all->par.regs, sizeof(struct p9100_regs));
		kfree(all);
		return -ENOMEM;
	}
	all->info.par = &all->par;

	p9100_blank(0, &all->info);

	if (fb_alloc_cmap(&all->info.cmap, 256, 0)) {
		of_iounmap(&op->resource[0],
			   all->par.regs, sizeof(struct p9100_regs));
		of_iounmap(&op->resource[2],
			   all->info.screen_base, all->par.fbsize);
		kfree(all);
		return -ENOMEM;
	}

	p9100_init_fix(&all->info, linebytes, dp);

	err = register_framebuffer(&all->info);
	if (err < 0) {
		fb_dealloc_cmap(&all->info.cmap);
		of_iounmap(&op->resource[0],
			   all->par.regs, sizeof(struct p9100_regs));
		of_iounmap(&op->resource[2],
			   all->info.screen_base, all->par.fbsize);
		kfree(all);
		return err;
	}
	fb_set_cmap(&all->info.cmap, &all->info);

	dev_set_drvdata(&op->dev, all);

	printk("%s: p9100 at %lx:%lx\n",
	       dp->full_name,
	       all->par.which_io, all->par.physbase);

	return 0;
}

static int __devinit p9100_probe(struct of_device *dev, const struct of_device_id *match)
{
	struct of_device *op = to_of_device(&dev->dev);

	return p9100_init_one(op);
}

static int __devexit p9100_remove(struct of_device *op)
{
	struct all_info *all = dev_get_drvdata(&op->dev);

	unregister_framebuffer(&all->info);
	fb_dealloc_cmap(&all->info.cmap);

	of_iounmap(&op->resource[0], all->par.regs, sizeof(struct p9100_regs));
	of_iounmap(&op->resource[2], all->info.screen_base, all->par.fbsize);

	kfree(all);

	dev_set_drvdata(&op->dev, NULL);

	return 0;
}

static struct of_device_id p9100_match[] = {
	{
		.name = "p9100",
	},
	{},
};
MODULE_DEVICE_TABLE(of, p9100_match);

static struct of_platform_driver p9100_driver = {
	.name		= "p9100",
	.match_table	= p9100_match,
	.probe		= p9100_probe,
	.remove		= __devexit_p(p9100_remove),
};

static int __init p9100_init(void)
{
	if (fb_get_options("p9100fb", NULL))
		return -ENODEV;

	return of_register_driver(&p9100_driver, &of_bus_type);
}

static void __exit p9100_exit(void)
{
	of_unregister_driver(&p9100_driver);
}

module_init(p9100_init);
module_exit(p9100_exit);

MODULE_DESCRIPTION("framebuffer driver for P9100 chipsets");
MODULE_AUTHOR("David S. Miller <davem@davemloft.net>");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
