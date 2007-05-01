/* bw2.c: BWTWO frame buffer driver
 *
 * Copyright (C) 2003, 2006 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996,1998 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1997 Eddie C. Dost (ecd@skynet.be)
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
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/of_device.h>
#include <asm/fbio.h>

#include "sbuslib.h"

/*
 * Local functions.
 */

static int bw2_blank(int, struct fb_info *);

static int bw2_mmap(struct fb_info *, struct vm_area_struct *);
static int bw2_ioctl(struct fb_info *, unsigned int, unsigned long);

/*
 *  Frame buffer operations
 */

static struct fb_ops bw2_ops = {
	.owner			= THIS_MODULE,
	.fb_blank		= bw2_blank,
	.fb_fillrect		= cfb_fillrect,
	.fb_copyarea		= cfb_copyarea,
	.fb_imageblit		= cfb_imageblit,
	.fb_mmap		= bw2_mmap,
	.fb_ioctl		= bw2_ioctl,
#ifdef CONFIG_COMPAT
	.fb_compat_ioctl	= sbusfb_compat_ioctl,
#endif
};

/* OBio addresses for the bwtwo registers */
#define BWTWO_REGISTER_OFFSET 0x400000

struct bt_regs {
	u32 addr;
	u32 color_map;
	u32 control;
	u32 cursor;
};

struct bw2_regs {
	struct bt_regs	cmap;
	u8	control;
	u8	status;
	u8	cursor_start;
	u8	cursor_end;
	u8	h_blank_start;
	u8	h_blank_end;
	u8	h_sync_start;
	u8	h_sync_end;
	u8	comp_sync_end;
	u8	v_blank_start_high;
	u8	v_blank_start_low;
	u8	v_blank_end;
	u8	v_sync_start;
	u8	v_sync_end;
	u8	xfer_holdoff_start;
	u8	xfer_holdoff_end;
};

/* Status Register Constants */
#define BWTWO_SR_RES_MASK	0x70
#define BWTWO_SR_1600_1280	0x50
#define BWTWO_SR_1152_900_76_A	0x40
#define BWTWO_SR_1152_900_76_B	0x60
#define BWTWO_SR_ID_MASK	0x0f
#define BWTWO_SR_ID_MONO	0x02
#define BWTWO_SR_ID_MONO_ECL	0x03
#define BWTWO_SR_ID_MSYNC	0x04
#define BWTWO_SR_ID_NOCONN	0x0a

/* Control Register Constants */
#define BWTWO_CTL_ENABLE_INTS   0x80
#define BWTWO_CTL_ENABLE_VIDEO  0x40
#define BWTWO_CTL_ENABLE_TIMING 0x20
#define BWTWO_CTL_ENABLE_CURCMP 0x10
#define BWTWO_CTL_XTAL_MASK     0x0C
#define BWTWO_CTL_DIVISOR_MASK  0x03

/* Status Register Constants */
#define BWTWO_STAT_PENDING_INT  0x80
#define BWTWO_STAT_MSENSE_MASK  0x70
#define BWTWO_STAT_ID_MASK      0x0f

struct bw2_par {
	spinlock_t		lock;
	struct bw2_regs		__iomem *regs;

	u32			flags;
#define BW2_FLAG_BLANKED	0x00000001

	unsigned long		physbase;
	unsigned long		which_io;
	unsigned long		fbsize;
};

/**
 *      bw2_blank - Optional function.  Blanks the display.
 *      @blank_mode: the blank mode we want.
 *      @info: frame buffer structure that represents a single frame buffer
 */
static int
bw2_blank(int blank, struct fb_info *info)
{
	struct bw2_par *par = (struct bw2_par *) info->par;
	struct bw2_regs __iomem *regs = par->regs;
	unsigned long flags;
	u8 val;

	spin_lock_irqsave(&par->lock, flags);

	switch (blank) {
	case FB_BLANK_UNBLANK: /* Unblanking */
		val = sbus_readb(&regs->control);
		val |= BWTWO_CTL_ENABLE_VIDEO;
		sbus_writeb(val, &regs->control);
		par->flags &= ~BW2_FLAG_BLANKED;
		break;

	case FB_BLANK_NORMAL: /* Normal blanking */
	case FB_BLANK_VSYNC_SUSPEND: /* VESA blank (vsync off) */
	case FB_BLANK_HSYNC_SUSPEND: /* VESA blank (hsync off) */
	case FB_BLANK_POWERDOWN: /* Poweroff */
		val = sbus_readb(&regs->control);
		val &= ~BWTWO_CTL_ENABLE_VIDEO;
		sbus_writeb(val, &regs->control);
		par->flags |= BW2_FLAG_BLANKED;
		break;
	}

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

static struct sbus_mmap_map bw2_mmap_map[] = {
	{
		.size = SBUS_MMAP_FBSIZE(1)
	},
	{ .size = 0 }
};

static int bw2_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct bw2_par *par = (struct bw2_par *)info->par;

	return sbusfb_mmap_helper(bw2_mmap_map,
				  par->physbase, par->fbsize,
				  par->which_io,
				  vma);
}

static int bw2_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	struct bw2_par *par = (struct bw2_par *) info->par;

	return sbusfb_ioctl_helper(cmd, arg, info,
				   FBTYPE_SUN2BW, 1, par->fbsize);
}

/*
 *  Initialisation
 */

static void __devinit bw2_init_fix(struct fb_info *info, int linebytes)
{
	strlcpy(info->fix.id, "bwtwo", sizeof(info->fix.id));

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_MONO01;

	info->fix.line_length = linebytes;

	info->fix.accel = FB_ACCEL_SUN_BWTWO;
}

static u8 bw2regs_1600[] __devinitdata = {
	0x14, 0x8b,	0x15, 0x28,	0x16, 0x03,	0x17, 0x13,
	0x18, 0x7b,	0x19, 0x05,	0x1a, 0x34,	0x1b, 0x2e,
	0x1c, 0x00,	0x1d, 0x0a,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x21,	0
};

static u8 bw2regs_ecl[] __devinitdata = {
	0x14, 0x65,	0x15, 0x1e,	0x16, 0x04,	0x17, 0x0c,
	0x18, 0x5e,	0x19, 0x03,	0x1a, 0xa7,	0x1b, 0x23,
	0x1c, 0x00,	0x1d, 0x08,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x20,	0
};

static u8 bw2regs_analog[] __devinitdata = {
	0x14, 0xbb,	0x15, 0x2b,	0x16, 0x03,	0x17, 0x13,
	0x18, 0xb0,	0x19, 0x03,	0x1a, 0xa6,	0x1b, 0x22,
	0x1c, 0x01,	0x1d, 0x05,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x20,	0
};

static u8 bw2regs_76hz[] __devinitdata = {
	0x14, 0xb7,	0x15, 0x27,	0x16, 0x03,	0x17, 0x0f,
	0x18, 0xae,	0x19, 0x03,	0x1a, 0xae,	0x1b, 0x2a,
	0x1c, 0x01,	0x1d, 0x09,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x24,	0
};

static u8 bw2regs_66hz[] __devinitdata = {
	0x14, 0xbb,	0x15, 0x2b,	0x16, 0x04,	0x17, 0x14,
	0x18, 0xae,	0x19, 0x03,	0x1a, 0xa8,	0x1b, 0x24,
	0x1c, 0x01,	0x1d, 0x05,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x20,	0
};

static void __devinit bw2_do_default_mode(struct bw2_par *par,
					  struct fb_info *info,
					  int *linebytes)
{
	u8 status, mon;
	u8 *p;

	status = sbus_readb(&par->regs->status);
	mon = status & BWTWO_SR_RES_MASK;
	switch (status & BWTWO_SR_ID_MASK) {
	case BWTWO_SR_ID_MONO_ECL:
		if (mon == BWTWO_SR_1600_1280) {
			p = bw2regs_1600;
			info->var.xres = info->var.xres_virtual = 1600;
			info->var.yres = info->var.yres_virtual = 1280;
			*linebytes = 1600 / 8;
		} else
			p = bw2regs_ecl;
		break;

	case BWTWO_SR_ID_MONO:
		p = bw2regs_analog;
		break;

	case BWTWO_SR_ID_MSYNC:
		if (mon == BWTWO_SR_1152_900_76_A ||
		    mon == BWTWO_SR_1152_900_76_B)
			p = bw2regs_76hz;
		else
			p = bw2regs_66hz;
		break;

	case BWTWO_SR_ID_NOCONN:
		return;

	default:
		prom_printf("bw2: can't handle SR %02x\n",
			    status);
		prom_halt();
	}
	for ( ; *p; p += 2) {
		u8 __iomem *regp = &((u8 __iomem *)par->regs)[p[0]];
		sbus_writeb(p[1], regp);
	}
}

struct all_info {
	struct fb_info info;
	struct bw2_par par;
};

static int __devinit bw2_init_one(struct of_device *op)
{
	struct device_node *dp = op->node;
	struct all_info *all;
	int linebytes, err;

	all = kzalloc(sizeof(*all), GFP_KERNEL);
	if (!all)
		return -ENOMEM;

	spin_lock_init(&all->par.lock);

	all->par.physbase = op->resource[0].start;
	all->par.which_io = op->resource[0].flags & IORESOURCE_BITS;

	sbusfb_fill_var(&all->info.var, dp->node, 1);
	linebytes = of_getintprop_default(dp, "linebytes",
					  all->info.var.xres);

	all->info.var.red.length = all->info.var.green.length =
		all->info.var.blue.length = all->info.var.bits_per_pixel;
	all->info.var.red.offset = all->info.var.green.offset =
		all->info.var.blue.offset = 0;

	all->par.regs = of_ioremap(&op->resource[0], BWTWO_REGISTER_OFFSET,
				   sizeof(struct bw2_regs), "bw2 regs");

	if (!of_find_property(dp, "width", NULL))
		bw2_do_default_mode(&all->par, &all->info, &linebytes);

	all->par.fbsize = PAGE_ALIGN(linebytes * all->info.var.yres);

	all->info.flags = FBINFO_DEFAULT;
	all->info.fbops = &bw2_ops;

	all->info.screen_base =
		of_ioremap(&op->resource[0], 0, all->par.fbsize, "bw2 ram");
	all->info.par = &all->par;

	bw2_blank(0, &all->info);

	bw2_init_fix(&all->info, linebytes);

	err= register_framebuffer(&all->info);
	if (err < 0) {
		of_iounmap(&op->resource[0],
			   all->par.regs, sizeof(struct bw2_regs));
		of_iounmap(&op->resource[0],
			   all->info.screen_base, all->par.fbsize);
		kfree(all);
		return err;
	}

	dev_set_drvdata(&op->dev, all);

	printk("%s: bwtwo at %lx:%lx\n",
	       dp->full_name,
	       all->par.which_io, all->par.physbase);

	return 0;
}

static int __devinit bw2_probe(struct of_device *dev, const struct of_device_id *match)
{
	struct of_device *op = to_of_device(&dev->dev);

	return bw2_init_one(op);
}

static int __devexit bw2_remove(struct of_device *op)
{
	struct all_info *all = dev_get_drvdata(&op->dev);

	unregister_framebuffer(&all->info);

	of_iounmap(&op->resource[0], all->par.regs, sizeof(struct bw2_regs));
	of_iounmap(&op->resource[0], all->info.screen_base, all->par.fbsize);

	kfree(all);

	dev_set_drvdata(&op->dev, NULL);

	return 0;
}

static struct of_device_id bw2_match[] = {
	{
		.name = "bwtwo",
	},
	{},
};
MODULE_DEVICE_TABLE(of, bw2_match);

static struct of_platform_driver bw2_driver = {
	.name		= "bw2",
	.match_table	= bw2_match,
	.probe		= bw2_probe,
	.remove		= __devexit_p(bw2_remove),
};

static int __init bw2_init(void)
{
	if (fb_get_options("bw2fb", NULL))
		return -ENODEV;

	return of_register_driver(&bw2_driver, &of_bus_type);
}

static void __exit bw2_exit(void)
{
	return of_unregister_driver(&bw2_driver);
}


module_init(bw2_init);
module_exit(bw2_exit);

MODULE_DESCRIPTION("framebuffer driver for BWTWO chipsets");
MODULE_AUTHOR("David S. Miller <davem@davemloft.net>");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
