/* bw2.c: BWTWO frame buffer driver
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
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
#include <asm/sbus.h>
#include <asm/oplib.h>
#include <asm/fbio.h>

#ifdef CONFIG_SPARC32
#include <asm/sun4paddr.h>
#endif

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
	volatile u32 addr;
	volatile u32 color_map;
	volatile u32 control;
	volatile u32 cursor;
};

struct bw2_regs {
	struct bt_regs	cmap;
	volatile u8	control;
	volatile u8	status;
	volatile u8	cursor_start;
	volatile u8	cursor_end;
	volatile u8	h_blank_start;
	volatile u8	h_blank_end;
	volatile u8	h_sync_start;
	volatile u8	h_sync_end;
	volatile u8	comp_sync_end;
	volatile u8	v_blank_start_high;
	volatile u8	v_blank_start_low;
	volatile u8	v_blank_end;
	volatile u8	v_sync_start;
	volatile u8	v_sync_end;
	volatile u8	xfer_holdoff_start;
	volatile u8	xfer_holdoff_end;
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
	unsigned long		fbsize;

	struct sbus_dev		*sdev;
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
				  (par->sdev ?
				   par->sdev->reg_addrs[0].which_io :
				   0),
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

static void
bw2_init_fix(struct fb_info *info, int linebytes)
{
	strlcpy(info->fix.id, "bwtwo", sizeof(info->fix.id));

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_MONO01;

	info->fix.line_length = linebytes;

	info->fix.accel = FB_ACCEL_SUN_BWTWO;
}

static u8 bw2regs_1600[] __initdata = {
	0x14, 0x8b,	0x15, 0x28,	0x16, 0x03,	0x17, 0x13,
	0x18, 0x7b,	0x19, 0x05,	0x1a, 0x34,	0x1b, 0x2e,
	0x1c, 0x00,	0x1d, 0x0a,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x21,	0
};

static u8 bw2regs_ecl[] __initdata = {
	0x14, 0x65,	0x15, 0x1e,	0x16, 0x04,	0x17, 0x0c,
	0x18, 0x5e,	0x19, 0x03,	0x1a, 0xa7,	0x1b, 0x23,
	0x1c, 0x00,	0x1d, 0x08,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x20,	0
};

static u8 bw2regs_analog[] __initdata = {
	0x14, 0xbb,	0x15, 0x2b,	0x16, 0x03,	0x17, 0x13,
	0x18, 0xb0,	0x19, 0x03,	0x1a, 0xa6,	0x1b, 0x22,
	0x1c, 0x01,	0x1d, 0x05,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x20,	0
};

static u8 bw2regs_76hz[] __initdata = {
	0x14, 0xb7,	0x15, 0x27,	0x16, 0x03,	0x17, 0x0f,
	0x18, 0xae,	0x19, 0x03,	0x1a, 0xae,	0x1b, 0x2a,
	0x1c, 0x01,	0x1d, 0x09,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x24,	0
};

static u8 bw2regs_66hz[] __initdata = {
	0x14, 0xbb,	0x15, 0x2b,	0x16, 0x04,	0x17, 0x14,
	0x18, 0xae,	0x19, 0x03,	0x1a, 0xa8,	0x1b, 0x24,
	0x1c, 0x01,	0x1d, 0x05,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x20,	0
};

static void bw2_do_default_mode(struct bw2_par *par, struct fb_info *info,
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
	struct list_head list;
};
static LIST_HEAD(bw2_list);

static void bw2_init_one(struct sbus_dev *sdev)
{
	struct all_info *all;
	struct resource *resp;
#ifdef CONFIG_SUN4
	struct resource res;
#endif
	int linebytes;

	all = kmalloc(sizeof(*all), GFP_KERNEL);
	if (!all) {
		printk(KERN_ERR "bw2: Cannot allocate memory.\n");
		return;
	}
	memset(all, 0, sizeof(*all));

	INIT_LIST_HEAD(&all->list);

	spin_lock_init(&all->par.lock);
	all->par.sdev = sdev;

#ifdef CONFIG_SUN4
	if (!sdev) {
		all->par.physbase = sun4_bwtwo_physaddr;
		res.start = sun4_bwtwo_physaddr;
		res.end = res.start + BWTWO_REGISTER_OFFSET + sizeof(struct bw2_regs) - 1;
		res.flags = IORESOURCE_IO;
		resp = &res;
		all->info.var.xres = all->info.var.xres_virtual = 1152;
		all->info.var.yres = all->info.var.yres_virtual = 900;
		all->info.var.bits_per_pixel = 1;
		linebytes = 1152 / 8;
	} else
#else
	{
		if (!sdev)
			BUG();
		all->par.physbase = sdev->reg_addrs[0].phys_addr;
		resp = &sdev->resource[0];
		sbusfb_fill_var(&all->info.var, (sdev ? sdev->prom_node : 0), 1);
		linebytes = prom_getintdefault(sdev->prom_node, "linebytes",
					       all->info.var.xres);
	}
#endif
	all->info.var.red.length = all->info.var.green.length =
		all->info.var.blue.length = all->info.var.bits_per_pixel;
	all->info.var.red.offset = all->info.var.green.offset =
		all->info.var.blue.offset = 0;

	all->par.regs = sbus_ioremap(resp, BWTWO_REGISTER_OFFSET,
			     sizeof(struct bw2_regs), "bw2 regs");

	if (sdev && !prom_getbool(sdev->prom_node, "width"))
		bw2_do_default_mode(&all->par, &all->info, &linebytes);

	all->par.fbsize = PAGE_ALIGN(linebytes * all->info.var.yres);

	all->info.flags = FBINFO_DEFAULT;
	all->info.fbops = &bw2_ops;
#if defined(CONFIG_SPARC32)
	if (sdev)
		all->info.screen_base = (char __iomem *)
			prom_getintdefault(sdev->prom_node, "address", 0);
#endif
	if (!all->info.screen_base)
		all->info.screen_base =
			sbus_ioremap(resp, 0, all->par.fbsize, "bw2 ram");
	all->info.par = &all->par;

	bw2_blank(0, &all->info);

	bw2_init_fix(&all->info, linebytes);

	if (register_framebuffer(&all->info) < 0) {
		printk(KERN_ERR "bw2: Could not register framebuffer.\n");
		kfree(all);
		return;
	}

	list_add(&all->list, &bw2_list);

	printk("bw2: bwtwo at %lx:%lx\n",
	       (long) (sdev ? sdev->reg_addrs[0].which_io : 0),
	       (long) all->par.physbase);
}

int __init bw2_init(void)
{
	struct sbus_bus *sbus;
	struct sbus_dev *sdev;

	if (fb_get_options("bw2fb", NULL))
		return -ENODEV;

#ifdef CONFIG_SUN4
	bw2_init_one(NULL);
#endif
	for_all_sbusdev(sdev, sbus) {
		if (!strcmp(sdev->prom_name, "bwtwo"))
			bw2_init_one(sdev);
	}

	return 0;
}

void __exit bw2_exit(void)
{
	struct list_head *pos, *tmp;

	list_for_each_safe(pos, tmp, &bw2_list) {
		struct all_info *all = list_entry(pos, typeof(*all), list);

		unregister_framebuffer(&all->info);
		kfree(all);
	}
}

int __init
bw2_setup(char *arg)
{
	/* No cmdline options yet... */
	return 0;
}

module_init(bw2_init);

#ifdef MODULE
module_exit(bw2_exit);
#endif

MODULE_DESCRIPTION("framebuffer driver for BWTWO chipsets");
MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_LICENSE("GPL");
