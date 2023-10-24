// SPDX-License-Identifier: GPL-2.0-only
/* tcx.c: TCX frame buffer driver
 *
 * Copyright (C) 2003, 2006 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996,1998 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
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

static int tcx_setcolreg(unsigned, unsigned, unsigned, unsigned,
			 unsigned, struct fb_info *);
static int tcx_blank(int, struct fb_info *);
static int tcx_pan_display(struct fb_var_screeninfo *, struct fb_info *);

static int tcx_sbusfb_mmap(struct fb_info *info, struct vm_area_struct *vma);
static int tcx_sbusfb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg);

/*
 *  Frame buffer operations
 */

static const struct fb_ops tcx_ops = {
	.owner			= THIS_MODULE,
	FB_DEFAULT_SBUS_OPS(tcx),
	.fb_setcolreg		= tcx_setcolreg,
	.fb_blank		= tcx_blank,
	.fb_pan_display		= tcx_pan_display,
};

/* THC definitions */
#define TCX_THC_MISC_REV_SHIFT       16
#define TCX_THC_MISC_REV_MASK        15
#define TCX_THC_MISC_VSYNC_DIS       (1 << 25)
#define TCX_THC_MISC_HSYNC_DIS       (1 << 24)
#define TCX_THC_MISC_RESET           (1 << 12)
#define TCX_THC_MISC_VIDEO           (1 << 10)
#define TCX_THC_MISC_SYNC            (1 << 9)
#define TCX_THC_MISC_VSYNC           (1 << 8)
#define TCX_THC_MISC_SYNC_ENAB       (1 << 7)
#define TCX_THC_MISC_CURS_RES        (1 << 6)
#define TCX_THC_MISC_INT_ENAB        (1 << 5)
#define TCX_THC_MISC_INT             (1 << 4)
#define TCX_THC_MISC_INIT            0x9f
#define TCX_THC_REV_REV_SHIFT        20
#define TCX_THC_REV_REV_MASK         15
#define TCX_THC_REV_MINREV_SHIFT     28
#define TCX_THC_REV_MINREV_MASK      15

/* The contents are unknown */
struct tcx_tec {
	u32 tec_matrix;
	u32 tec_clip;
	u32 tec_vdc;
};

struct tcx_thc {
	u32 thc_rev;
	u32 thc_pad0[511];
	u32 thc_hs;		/* hsync timing */
	u32 thc_hsdvs;
	u32 thc_hd;
	u32 thc_vs;		/* vsync timing */
	u32 thc_vd;
	u32 thc_refresh;
	u32 thc_misc;
	u32 thc_pad1[56];
	u32 thc_cursxy;	/* cursor x,y position (16 bits each) */
	u32 thc_cursmask[32];	/* cursor mask bits */
	u32 thc_cursbits[32];	/* what to show where mask enabled */
};

struct bt_regs {
	u32 addr;
	u32 color_map;
	u32 control;
	u32 cursor;
};

#define TCX_MMAP_ENTRIES 14

struct tcx_par {
	spinlock_t		lock;
	struct bt_regs		__iomem *bt;
	struct tcx_thc		__iomem *thc;
	struct tcx_tec		__iomem *tec;
	u32			__iomem *cplane;

	u32			flags;
#define TCX_FLAG_BLANKED	0x00000001

	unsigned long		which_io;

	struct sbus_mmap_map	mmap_map[TCX_MMAP_ENTRIES];
	int			lowdepth;
};

/* Reset control plane so that WID is 8-bit plane. */
static void __tcx_set_control_plane(struct fb_info *info)
{
	struct tcx_par *par = info->par;
	u32 __iomem *p, *pend;

	if (par->lowdepth)
		return;

	p = par->cplane;
	if (p == NULL)
		return;
	for (pend = p + info->fix.smem_len; p < pend; p++) {
		u32 tmp = sbus_readl(p);

		tmp &= 0xffffff;
		sbus_writel(tmp, p);
	}
}

static void tcx_reset(struct fb_info *info)
{
	struct tcx_par *par = (struct tcx_par *) info->par;
	unsigned long flags;

	spin_lock_irqsave(&par->lock, flags);
	__tcx_set_control_plane(info);
	spin_unlock_irqrestore(&par->lock, flags);
}

static int tcx_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	tcx_reset(info);
	return 0;
}

/**
 *      tcx_setcolreg - Optional function. Sets a color register.
 *      @regno: boolean, 0 copy local, 1 get_user() function
 *      @red: frame buffer colormap structure
 *      @green: The green value which can be up to 16 bits wide
 *      @blue:  The blue value which can be up to 16 bits wide.
 *      @transp: If supported the alpha value which can be up to 16 bits wide.
 *      @info: frame buffer info structure
 */
static int tcx_setcolreg(unsigned regno,
			 unsigned red, unsigned green, unsigned blue,
			 unsigned transp, struct fb_info *info)
{
	struct tcx_par *par = (struct tcx_par *) info->par;
	struct bt_regs __iomem *bt = par->bt;
	unsigned long flags;

	if (regno >= 256)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	spin_lock_irqsave(&par->lock, flags);

	sbus_writel(regno << 24, &bt->addr);
	sbus_writel(red << 24, &bt->color_map);
	sbus_writel(green << 24, &bt->color_map);
	sbus_writel(blue << 24, &bt->color_map);

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

/**
 *      tcx_blank - Optional function.  Blanks the display.
 *      @blank: the blank mode we want.
 *      @info: frame buffer structure that represents a single frame buffer
 */
static int
tcx_blank(int blank, struct fb_info *info)
{
	struct tcx_par *par = (struct tcx_par *) info->par;
	struct tcx_thc __iomem *thc = par->thc;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&par->lock, flags);

	val = sbus_readl(&thc->thc_misc);

	switch (blank) {
	case FB_BLANK_UNBLANK: /* Unblanking */
		val &= ~(TCX_THC_MISC_VSYNC_DIS |
			 TCX_THC_MISC_HSYNC_DIS);
		val |= TCX_THC_MISC_VIDEO;
		par->flags &= ~TCX_FLAG_BLANKED;
		break;

	case FB_BLANK_NORMAL: /* Normal blanking */
		val &= ~TCX_THC_MISC_VIDEO;
		par->flags |= TCX_FLAG_BLANKED;
		break;

	case FB_BLANK_VSYNC_SUSPEND: /* VESA blank (vsync off) */
		val |= TCX_THC_MISC_VSYNC_DIS;
		break;
	case FB_BLANK_HSYNC_SUSPEND: /* VESA blank (hsync off) */
		val |= TCX_THC_MISC_HSYNC_DIS;
		break;

	case FB_BLANK_POWERDOWN: /* Poweroff */
		break;
	}

	sbus_writel(val, &thc->thc_misc);

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

static struct sbus_mmap_map __tcx_mmap_map[TCX_MMAP_ENTRIES] = {
	{
		.voff	= TCX_RAM8BIT,
		.size	= SBUS_MMAP_FBSIZE(1)
	},
	{
		.voff	= TCX_RAM24BIT,
		.size	= SBUS_MMAP_FBSIZE(4)
	},
	{
		.voff	= TCX_UNK3,
		.size	= SBUS_MMAP_FBSIZE(8)
	},
	{
		.voff	= TCX_UNK4,
		.size	= SBUS_MMAP_FBSIZE(8)
	},
	{
		.voff	= TCX_CONTROLPLANE,
		.size	= SBUS_MMAP_FBSIZE(4)
	},
	{
		.voff	= TCX_UNK6,
		.size	= SBUS_MMAP_FBSIZE(8)
	},
	{
		.voff	= TCX_UNK7,
		.size	= SBUS_MMAP_FBSIZE(8)
	},
	{
		.voff	= TCX_TEC,
		.size	= PAGE_SIZE
	},
	{
		.voff	= TCX_BTREGS,
		.size	= PAGE_SIZE
	},
	{
		.voff	= TCX_THC,
		.size	= PAGE_SIZE
	},
	{
		.voff	= TCX_DHC,
		.size	= PAGE_SIZE
	},
	{
		.voff	= TCX_ALT,
		.size	= PAGE_SIZE
	},
	{
		.voff	= TCX_UNK2,
		.size	= 0x20000
	},
	{ .size = 0 }
};

static int tcx_sbusfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct tcx_par *par = (struct tcx_par *)info->par;

	return sbusfb_mmap_helper(par->mmap_map,
				  info->fix.smem_start, info->fix.smem_len,
				  par->which_io, vma);
}

static int tcx_sbusfb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	struct tcx_par *par = (struct tcx_par *) info->par;

	return sbusfb_ioctl_helper(cmd, arg, info,
				   FBTYPE_TCXCOLOR,
				   (par->lowdepth ? 8 : 24),
				   info->fix.smem_len);
}

/*
 *  Initialisation
 */

static void
tcx_init_fix(struct fb_info *info, int linebytes)
{
	struct tcx_par *par = (struct tcx_par *)info->par;
	const char *tcx_name;

	if (par->lowdepth)
		tcx_name = "TCX8";
	else
		tcx_name = "TCX24";

	strscpy(info->fix.id, tcx_name, sizeof(info->fix.id));

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;

	info->fix.line_length = linebytes;

	info->fix.accel = FB_ACCEL_SUN_TCX;
}

static void tcx_unmap_regs(struct platform_device *op, struct fb_info *info,
			   struct tcx_par *par)
{
	if (par->tec)
		of_iounmap(&op->resource[7],
			   par->tec, sizeof(struct tcx_tec));
	if (par->thc)
		of_iounmap(&op->resource[9],
			   par->thc, sizeof(struct tcx_thc));
	if (par->bt)
		of_iounmap(&op->resource[8],
			   par->bt, sizeof(struct bt_regs));
	if (par->cplane)
		of_iounmap(&op->resource[4],
			   par->cplane, info->fix.smem_len * sizeof(u32));
	if (info->screen_base)
		of_iounmap(&op->resource[0],
			   info->screen_base, info->fix.smem_len);
}

static int tcx_probe(struct platform_device *op)
{
	struct device_node *dp = op->dev.of_node;
	struct fb_info *info;
	struct tcx_par *par;
	int linebytes, i, err;

	info = framebuffer_alloc(sizeof(struct tcx_par), &op->dev);

	err = -ENOMEM;
	if (!info)
		goto out_err;
	par = info->par;

	spin_lock_init(&par->lock);

	par->lowdepth = of_property_read_bool(dp, "tcx-8-bit");

	sbusfb_fill_var(&info->var, dp, 8);
	info->var.red.length = 8;
	info->var.green.length = 8;
	info->var.blue.length = 8;

	linebytes = of_getintprop_default(dp, "linebytes",
					  info->var.xres);
	info->fix.smem_len = PAGE_ALIGN(linebytes * info->var.yres);

	par->tec = of_ioremap(&op->resource[7], 0,
				  sizeof(struct tcx_tec), "tcx tec");
	par->thc = of_ioremap(&op->resource[9], 0,
				  sizeof(struct tcx_thc), "tcx thc");
	par->bt = of_ioremap(&op->resource[8], 0,
				 sizeof(struct bt_regs), "tcx dac");
	info->screen_base = of_ioremap(&op->resource[0], 0,
					   info->fix.smem_len, "tcx ram");
	if (!par->tec || !par->thc ||
	    !par->bt || !info->screen_base)
		goto out_unmap_regs;

	memcpy(&par->mmap_map, &__tcx_mmap_map, sizeof(par->mmap_map));
	if (!par->lowdepth) {
		par->cplane = of_ioremap(&op->resource[4], 0,
					     info->fix.smem_len * sizeof(u32),
					     "tcx cplane");
		if (!par->cplane)
			goto out_unmap_regs;
	} else {
		par->mmap_map[1].size = SBUS_MMAP_EMPTY;
		par->mmap_map[4].size = SBUS_MMAP_EMPTY;
		par->mmap_map[5].size = SBUS_MMAP_EMPTY;
		par->mmap_map[6].size = SBUS_MMAP_EMPTY;
	}

	info->fix.smem_start = op->resource[0].start;
	par->which_io = op->resource[0].flags & IORESOURCE_BITS;

	for (i = 0; i < TCX_MMAP_ENTRIES; i++) {
		int j;

		switch (i) {
		case 10:
			j = 12;
			break;

		case 11: case 12:
			j = i - 1;
			break;

		default:
			j = i;
			break;
		}
		par->mmap_map[i].poff = op->resource[j].start;
	}

	info->fbops = &tcx_ops;

	/* Initialize brooktree DAC. */
	sbus_writel(0x04 << 24, &par->bt->addr);         /* color planes */
	sbus_writel(0xff << 24, &par->bt->control);
	sbus_writel(0x05 << 24, &par->bt->addr);
	sbus_writel(0x00 << 24, &par->bt->control);
	sbus_writel(0x06 << 24, &par->bt->addr);         /* overlay plane */
	sbus_writel(0x73 << 24, &par->bt->control);
	sbus_writel(0x07 << 24, &par->bt->addr);
	sbus_writel(0x00 << 24, &par->bt->control);

	tcx_reset(info);

	tcx_blank(FB_BLANK_UNBLANK, info);

	if (fb_alloc_cmap(&info->cmap, 256, 0))
		goto out_unmap_regs;

	fb_set_cmap(&info->cmap, info);
	tcx_init_fix(info, linebytes);

	err = register_framebuffer(info);
	if (err < 0)
		goto out_dealloc_cmap;

	dev_set_drvdata(&op->dev, info);

	printk(KERN_INFO "%pOF: TCX at %lx:%lx, %s\n",
	       dp,
	       par->which_io,
	       info->fix.smem_start,
	       par->lowdepth ? "8-bit only" : "24-bit depth");

	return 0;

out_dealloc_cmap:
	fb_dealloc_cmap(&info->cmap);

out_unmap_regs:
	tcx_unmap_regs(op, info, par);
	framebuffer_release(info);

out_err:
	return err;
}

static void tcx_remove(struct platform_device *op)
{
	struct fb_info *info = dev_get_drvdata(&op->dev);
	struct tcx_par *par = info->par;

	unregister_framebuffer(info);
	fb_dealloc_cmap(&info->cmap);

	tcx_unmap_regs(op, info, par);

	framebuffer_release(info);
}

static const struct of_device_id tcx_match[] = {
	{
		.name = "SUNW,tcx",
	},
	{},
};
MODULE_DEVICE_TABLE(of, tcx_match);

static struct platform_driver tcx_driver = {
	.driver = {
		.name = "tcx",
		.of_match_table = tcx_match,
	},
	.probe		= tcx_probe,
	.remove_new	= tcx_remove,
};

static int __init tcx_init(void)
{
	if (fb_get_options("tcxfb", NULL))
		return -ENODEV;

	return platform_driver_register(&tcx_driver);
}

static void __exit tcx_exit(void)
{
	platform_driver_unregister(&tcx_driver);
}

module_init(tcx_init);
module_exit(tcx_exit);

MODULE_DESCRIPTION("framebuffer driver for TCX chipsets");
MODULE_AUTHOR("David S. Miller <davem@davemloft.net>");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
