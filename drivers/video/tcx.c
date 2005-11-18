/* tcx.c: TCX frame buffer driver
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
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
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/sbus.h>
#include <asm/oplib.h>
#include <asm/fbio.h>

#include "sbuslib.h"

/*
 * Local functions.
 */

static int tcx_setcolreg(unsigned, unsigned, unsigned, unsigned,
			 unsigned, struct fb_info *);
static int tcx_blank(int, struct fb_info *);

static int tcx_mmap(struct fb_info *, struct file *, struct vm_area_struct *);
static int tcx_ioctl(struct inode *, struct file *, unsigned int,
		     unsigned long, struct fb_info *);
static int tcx_pan_display(struct fb_var_screeninfo *, struct fb_info *);

/*
 *  Frame buffer operations
 */

static struct fb_ops tcx_ops = {
	.owner			= THIS_MODULE,
	.fb_setcolreg		= tcx_setcolreg,
	.fb_blank		= tcx_blank,
	.fb_pan_display		= tcx_pan_display,
	.fb_fillrect		= cfb_fillrect,
	.fb_copyarea		= cfb_copyarea,
	.fb_imageblit		= cfb_imageblit,
	.fb_mmap		= tcx_mmap,
	.fb_ioctl		= tcx_ioctl,
#ifdef CONFIG_COMPAT
	.fb_compat_ioctl	= sbusfb_compat_ioctl,
#endif
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
	volatile u32 tec_matrix;
	volatile u32 tec_clip;
	volatile u32 tec_vdc;
};

struct tcx_thc {
	volatile u32 thc_rev;
        u32 thc_pad0[511];
	volatile u32 thc_hs;		/* hsync timing */
	volatile u32 thc_hsdvs;
	volatile u32 thc_hd;
	volatile u32 thc_vs;		/* vsync timing */
	volatile u32 thc_vd;
	volatile u32 thc_refresh;
	volatile u32 thc_misc;
	u32 thc_pad1[56];
	volatile u32 thc_cursxy;	/* cursor x,y position (16 bits each) */
	volatile u32 thc_cursmask[32];	/* cursor mask bits */
	volatile u32 thc_cursbits[32];	/* what to show where mask enabled */
};

struct bt_regs {
	volatile u32 addr;
	volatile u32 color_map;
	volatile u32 control;
	volatile u32 cursor;
};

#define TCX_MMAP_ENTRIES 14

struct tcx_par {
	spinlock_t		lock;
	struct bt_regs		__iomem *bt;
	struct tcx_thc		__iomem *thc;
	struct tcx_tec		__iomem *tec;
	volatile u32		__iomem *cplane;

	u32			flags;
#define TCX_FLAG_BLANKED	0x00000001

	unsigned long		physbase;
	unsigned long		fbsize;

	struct sbus_mmap_map	mmap_map[TCX_MMAP_ENTRIES];
	int			lowdepth;

	struct sbus_dev		*sdev;
	struct list_head	list;
};

/* Reset control plane so that WID is 8-bit plane. */
static void __tcx_set_control_plane (struct tcx_par *par)
{
	volatile u32 __iomem *p, *pend;
        
	if (par->lowdepth)
		return;

	p = par->cplane;
	if (p == NULL)
		return;
	for (pend = p + par->fbsize; p < pend; p++) {
		u32 tmp = sbus_readl(p);

		tmp &= 0xffffff;
		sbus_writel(tmp, p);
	}
}
                                                
static void tcx_reset (struct fb_info *info)
{
	struct tcx_par *par = (struct tcx_par *) info->par;
	unsigned long flags;

	spin_lock_irqsave(&par->lock, flags);
	__tcx_set_control_plane(par);
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
 *      @blank_mode: the blank mode we want.
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
	};

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

static int tcx_mmap(struct fb_info *info, struct file *file, struct vm_area_struct *vma)
{
	struct tcx_par *par = (struct tcx_par *)info->par;

	return sbusfb_mmap_helper(par->mmap_map,
				  par->physbase, par->fbsize,
				  par->sdev->reg_addrs[0].which_io,
				  vma);
}

static int tcx_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg, struct fb_info *info)
{
	struct tcx_par *par = (struct tcx_par *) info->par;

	return sbusfb_ioctl_helper(cmd, arg, info,
				   FBTYPE_TCXCOLOR,
				   (par->lowdepth ? 8 : 24),
				   par->fbsize);
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

	strlcpy(info->fix.id, tcx_name, sizeof(info->fix.id));

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;

	info->fix.line_length = linebytes;

	info->fix.accel = FB_ACCEL_SUN_TCX;
}

struct all_info {
	struct fb_info info;
	struct tcx_par par;
	struct list_head list;
};
static LIST_HEAD(tcx_list);

static void tcx_init_one(struct sbus_dev *sdev)
{
	struct all_info *all;
	int linebytes, i;

	all = kmalloc(sizeof(*all), GFP_KERNEL);
	if (!all) {
		printk(KERN_ERR "tcx: Cannot allocate memory.\n");
		return;
	}
	memset(all, 0, sizeof(*all));

	INIT_LIST_HEAD(&all->list);

	spin_lock_init(&all->par.lock);
	all->par.sdev = sdev;

	all->par.lowdepth = prom_getbool(sdev->prom_node, "tcx-8-bit");

	sbusfb_fill_var(&all->info.var, sdev->prom_node, 8);
	all->info.var.red.length = 8;
	all->info.var.green.length = 8;
	all->info.var.blue.length = 8;

	linebytes = prom_getintdefault(sdev->prom_node, "linebytes",
				       all->info.var.xres);
	all->par.fbsize = PAGE_ALIGN(linebytes * all->info.var.yres);

	all->par.tec = sbus_ioremap(&sdev->resource[7], 0,
			     sizeof(struct tcx_tec), "tcx tec");
	all->par.thc = sbus_ioremap(&sdev->resource[9], 0,
			     sizeof(struct tcx_thc), "tcx thc");
	all->par.bt = sbus_ioremap(&sdev->resource[8], 0,
			     sizeof(struct bt_regs), "tcx dac");
	memcpy(&all->par.mmap_map, &__tcx_mmap_map, sizeof(all->par.mmap_map));
	if (!all->par.lowdepth) {
		all->par.cplane = sbus_ioremap(&sdev->resource[4], 0,
				     all->par.fbsize * sizeof(u32), "tcx cplane");
	} else {
		all->par.mmap_map[1].size = SBUS_MMAP_EMPTY;
		all->par.mmap_map[4].size = SBUS_MMAP_EMPTY;
		all->par.mmap_map[5].size = SBUS_MMAP_EMPTY;
		all->par.mmap_map[6].size = SBUS_MMAP_EMPTY;
	}

	all->par.physbase = 0;
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
		};
		all->par.mmap_map[i].poff = sdev->reg_addrs[j].phys_addr;
	}

	all->info.flags = FBINFO_DEFAULT;
	all->info.fbops = &tcx_ops;
#ifdef CONFIG_SPARC32
	all->info.screen_base = (char __iomem *)
		prom_getintdefault(sdev->prom_node, "address", 0);
#endif
	if (!all->info.screen_base)
		all->info.screen_base = sbus_ioremap(&sdev->resource[0], 0,
				     all->par.fbsize, "tcx ram");
	all->info.par = &all->par;

	/* Initialize brooktree DAC. */
	sbus_writel(0x04 << 24, &all->par.bt->addr);         /* color planes */
	sbus_writel(0xff << 24, &all->par.bt->control);
	sbus_writel(0x05 << 24, &all->par.bt->addr);
	sbus_writel(0x00 << 24, &all->par.bt->control);
	sbus_writel(0x06 << 24, &all->par.bt->addr);         /* overlay plane */
	sbus_writel(0x73 << 24, &all->par.bt->control);
	sbus_writel(0x07 << 24, &all->par.bt->addr);
	sbus_writel(0x00 << 24, &all->par.bt->control);

	tcx_reset(&all->info);

	tcx_blank(0, &all->info);

	if (fb_alloc_cmap(&all->info.cmap, 256, 0)) {
		printk(KERN_ERR "tcx: Could not allocate color map.\n");
		kfree(all);
		return;
	}

	fb_set_cmap(&all->info.cmap, &all->info);
	tcx_init_fix(&all->info, linebytes);

	if (register_framebuffer(&all->info) < 0) {
		printk(KERN_ERR "tcx: Could not register framebuffer.\n");
		fb_dealloc_cmap(&all->info.cmap);
		kfree(all);
		return;
	}

	list_add(&all->list, &tcx_list);

	printk("tcx: %s at %lx:%lx, %s\n",
	       sdev->prom_name,
	       (long) sdev->reg_addrs[0].which_io,
	       (long) sdev->reg_addrs[0].phys_addr,
	       all->par.lowdepth ? "8-bit only" : "24-bit depth");
}

int __init tcx_init(void)
{
	struct sbus_bus *sbus;
	struct sbus_dev *sdev;

	if (fb_get_options("tcxfb", NULL))
		return -ENODEV;

	for_all_sbusdev(sdev, sbus) {
		if (!strcmp(sdev->prom_name, "SUNW,tcx"))
			tcx_init_one(sdev);
	}

	return 0;
}

void __exit tcx_exit(void)
{
	struct list_head *pos, *tmp;

	list_for_each_safe(pos, tmp, &tcx_list) {
		struct all_info *all = list_entry(pos, typeof(*all), list);

		unregister_framebuffer(&all->info);
		fb_dealloc_cmap(&all->info.cmap);
		kfree(all);
	}
}

int __init
tcx_setup(char *arg)
{
	/* No cmdline options yet... */
	return 0;
}

module_init(tcx_init);

#ifdef MODULE
module_exit(tcx_exit);
#endif

MODULE_DESCRIPTION("framebuffer driver for TCX chipsets");
MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_LICENSE("GPL");
