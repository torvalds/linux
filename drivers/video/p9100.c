/* p9100.c: P9100 frame buffer driver
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
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
#include <asm/sbus.h>
#include <asm/oplib.h>
#include <asm/fbio.h>

#include "sbuslib.h"

/*
 * Local functions.
 */

static int p9100_setcolreg(unsigned, unsigned, unsigned, unsigned,
			   unsigned, struct fb_info *);
static int p9100_blank(int, struct fb_info *);

static int p9100_mmap(struct fb_info *, struct file *, struct vm_area_struct *);
static int p9100_ioctl(struct inode *, struct file *, unsigned int,
		       unsigned long, struct fb_info *);

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
	volatile u32 sys_base;
	volatile u32 sys_config;
	volatile u32 sys_intr;
	volatile u32 sys_int_ena;
	volatile u32 sys_alt_rd;
	volatile u32 sys_alt_wr;
	volatile u32 sys_xxx[58];

	/* Registers for the video control */
	volatile u32 vid_base;
	volatile u32 vid_hcnt;
	volatile u32 vid_htotal;
	volatile u32 vid_hsync_rise;
	volatile u32 vid_hblank_rise;
	volatile u32 vid_hblank_fall;
	volatile u32 vid_hcnt_preload;
	volatile u32 vid_vcnt;
	volatile u32 vid_vlen;
	volatile u32 vid_vsync_rise;
	volatile u32 vid_vblank_rise;
	volatile u32 vid_vblank_fall;
	volatile u32 vid_vcnt_preload;
	volatile u32 vid_screenpaint_addr;
	volatile u32 vid_screenpaint_timectl1;
	volatile u32 vid_screenpaint_qsfcnt;
	volatile u32 vid_screenpaint_timectl2;
	volatile u32 vid_xxx[15];

	/* Registers for the video control */
	volatile u32 vram_base;
	volatile u32 vram_memcfg;
	volatile u32 vram_refresh_pd;
	volatile u32 vram_refresh_cnt;
	volatile u32 vram_raslo_max;
	volatile u32 vram_raslo_cur;
	volatile u32 pwrup_cfg;
	volatile u32 vram_xxx[25];

	/* Registers for IBM RGB528 Palette */
	volatile u32 ramdac_cmap_wridx; 
	volatile u32 ramdac_palette_data;
	volatile u32 ramdac_pixel_mask;
	volatile u32 ramdac_palette_rdaddr;
	volatile u32 ramdac_idx_lo;
	volatile u32 ramdac_idx_hi;
	volatile u32 ramdac_idx_data;
	volatile u32 ramdac_idx_ctl;
	volatile u32 ramdac_xxx[1784];
};

struct p9100_cmd_parameng {
	volatile u32 parameng_status;
	volatile u32 parameng_bltcmd;
	volatile u32 parameng_quadcmd;
};

struct p9100_par {
	spinlock_t		lock;
	struct p9100_regs	__iomem *regs;

	u32			flags;
#define P9100_FLAG_BLANKED	0x00000001

	unsigned long		physbase;
	unsigned long		fbsize;

	struct sbus_dev		*sdev;
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

static int p9100_mmap(struct fb_info *info, struct file *file, struct vm_area_struct *vma)
{
	struct p9100_par *par = (struct p9100_par *)info->par;

	return sbusfb_mmap_helper(p9100_mmap_map,
				  par->physbase, par->fbsize,
				  par->sdev->reg_addrs[0].which_io,
				  vma);
}

static int p9100_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		       unsigned long arg, struct fb_info *info)
{
	struct p9100_par *par = (struct p9100_par *) info->par;

	/* Make it look like a cg3. */
	return sbusfb_ioctl_helper(cmd, arg, info,
				   FBTYPE_SUN3COLOR, 8, par->fbsize);
}

/*
 *  Initialisation
 */

static void
p9100_init_fix(struct fb_info *info, int linebytes)
{
	struct p9100_par *par = (struct p9100_par *)info->par;

	strlcpy(info->fix.id, par->sdev->prom_name, sizeof(info->fix.id));

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;

	info->fix.line_length = linebytes;

	info->fix.accel = FB_ACCEL_SUN_CGTHREE;
}

struct all_info {
	struct fb_info info;
	struct p9100_par par;
	struct list_head list;
};
static LIST_HEAD(p9100_list);

static void p9100_init_one(struct sbus_dev *sdev)
{
	struct all_info *all;
	int linebytes;

	all = kmalloc(sizeof(*all), GFP_KERNEL);
	if (!all) {
		printk(KERN_ERR "p9100: Cannot allocate memory.\n");
		return;
	}
	memset(all, 0, sizeof(*all));

	INIT_LIST_HEAD(&all->list);

	spin_lock_init(&all->par.lock);
	all->par.sdev = sdev;

	/* This is the framebuffer and the only resource apps can mmap.  */
	all->par.physbase = sdev->reg_addrs[2].phys_addr;

	sbusfb_fill_var(&all->info.var, sdev->prom_node, 8);
	all->info.var.red.length = 8;
	all->info.var.green.length = 8;
	all->info.var.blue.length = 8;

	linebytes = prom_getintdefault(sdev->prom_node, "linebytes",
				       all->info.var.xres);
	all->par.fbsize = PAGE_ALIGN(linebytes * all->info.var.yres);

	all->par.regs = sbus_ioremap(&sdev->resource[0], 0,
			     sizeof(struct p9100_regs), "p9100 regs");

	all->info.flags = FBINFO_DEFAULT;
	all->info.fbops = &p9100_ops;
#ifdef CONFIG_SPARC32
	all->info.screen_base = (char __iomem *)
		prom_getintdefault(sdev->prom_node, "address", 0);
#endif
	if (!all->info.screen_base)
		all->info.screen_base = sbus_ioremap(&sdev->resource[2], 0,
				     all->par.fbsize, "p9100 ram");
	all->info.par = &all->par;

	p9100_blank(0, &all->info);

	if (fb_alloc_cmap(&all->info.cmap, 256, 0)) {
		printk(KERN_ERR "p9100: Could not allocate color map.\n");
		kfree(all);
		return;
	}

	p9100_init_fix(&all->info, linebytes);

	if (register_framebuffer(&all->info) < 0) {
		printk(KERN_ERR "p9100: Could not register framebuffer.\n");
		fb_dealloc_cmap(&all->info.cmap);
		kfree(all);
		return;
	}
	fb_set_cmap(&all->info.cmap, &all->info);

	list_add(&all->list, &p9100_list);

	printk("p9100: %s at %lx:%lx\n",
	       sdev->prom_name,
	       (long) sdev->reg_addrs[0].which_io,
	       (long) sdev->reg_addrs[0].phys_addr);
}

int __init p9100_init(void)
{
	struct sbus_bus *sbus;
	struct sbus_dev *sdev;

	if (fb_get_options("p9100fb", NULL))
		return -ENODEV;

	for_all_sbusdev(sdev, sbus) {
		if (!strcmp(sdev->prom_name, "p9100"))
			p9100_init_one(sdev);
	}

	return 0;
}

void __exit p9100_exit(void)
{
	struct list_head *pos, *tmp;

	list_for_each_safe(pos, tmp, &p9100_list) {
		struct all_info *all = list_entry(pos, typeof(*all), list);

		unregister_framebuffer(&all->info);
		fb_dealloc_cmap(&all->info.cmap);
		kfree(all);
	}
}

int __init
p9100_setup(char *arg)
{
	/* No cmdline options yet... */
	return 0;
}

module_init(p9100_init);

#ifdef MODULE
module_exit(p9100_exit);
#endif

MODULE_DESCRIPTION("framebuffer driver for P9100 chipsets");
MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_LICENSE("GPL");
