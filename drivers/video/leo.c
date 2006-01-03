/* leo.c: LEO frame buffer driver
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 * Copyright (C) 1996-1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1997 Michal Rehacek (Michal.Rehacek@st.mff.cuni.cz)
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

static int leo_setcolreg(unsigned, unsigned, unsigned, unsigned,
			 unsigned, struct fb_info *);
static int leo_blank(int, struct fb_info *);

static int leo_mmap(struct fb_info *, struct file *, struct vm_area_struct *);
static int leo_ioctl(struct inode *, struct file *, unsigned int,
		     unsigned long, struct fb_info *);
static int leo_pan_display(struct fb_var_screeninfo *, struct fb_info *);

/*
 *  Frame buffer operations
 */

static struct fb_ops leo_ops = {
	.owner			= THIS_MODULE,
	.fb_setcolreg		= leo_setcolreg,
	.fb_blank		= leo_blank,
	.fb_pan_display		= leo_pan_display,
	.fb_fillrect		= cfb_fillrect,
	.fb_copyarea		= cfb_copyarea,
	.fb_imageblit		= cfb_imageblit,
	.fb_mmap		= leo_mmap,
	.fb_ioctl		= leo_ioctl,
#ifdef CONFIG_COMPAT
	.fb_compat_ioctl	= sbusfb_compat_ioctl,
#endif
};

#define LEO_OFF_LC_SS0_KRN	0x00200000UL
#define LEO_OFF_LC_SS0_USR	0x00201000UL
#define LEO_OFF_LC_SS1_KRN	0x01200000UL
#define LEO_OFF_LC_SS1_USR	0x01201000UL
#define LEO_OFF_LD_SS0		0x00400000UL
#define LEO_OFF_LD_SS1		0x01400000UL
#define LEO_OFF_LD_GBL		0x00401000UL
#define LEO_OFF_LX_KRN		0x00600000UL
#define LEO_OFF_LX_CURSOR	0x00601000UL
#define LEO_OFF_SS0		0x00800000UL
#define LEO_OFF_SS1		0x01800000UL
#define LEO_OFF_UNK		0x00602000UL
#define LEO_OFF_UNK2		0x00000000UL

#define LEO_CUR_ENABLE		0x00000080
#define LEO_CUR_UPDATE		0x00000030
#define LEO_CUR_PROGRESS	0x00000006
#define LEO_CUR_UPDATECMAP	0x00000003

#define LEO_CUR_TYPE_MASK	0x00000000
#define LEO_CUR_TYPE_IMAGE	0x00000020
#define LEO_CUR_TYPE_CMAP	0x00000050

struct leo_cursor {
	u8		xxx0[16];
	volatile u32	cur_type;
	volatile u32	cur_misc;
	volatile u32	cur_cursxy;
	volatile u32	cur_data;
};

#define LEO_KRN_TYPE_CLUT0	0x00001000
#define LEO_KRN_TYPE_CLUT1	0x00001001
#define LEO_KRN_TYPE_CLUT2	0x00001002
#define LEO_KRN_TYPE_WID	0x00001003
#define LEO_KRN_TYPE_UNK	0x00001006
#define LEO_KRN_TYPE_VIDEO	0x00002003
#define LEO_KRN_TYPE_CLUTDATA	0x00004000
#define LEO_KRN_CSR_ENABLE	0x00000008
#define LEO_KRN_CSR_PROGRESS	0x00000004
#define LEO_KRN_CSR_UNK		0x00000002
#define LEO_KRN_CSR_UNK2	0x00000001

struct leo_lx_krn {
	volatile u32	krn_type;
	volatile u32	krn_csr;
	volatile u32	krn_value;
};

struct leo_lc_ss0_krn {
	volatile u32 	misc;
	u8		xxx0[0x800-4];
	volatile u32	rev;
};

struct leo_lc_ss0_usr {
	volatile u32	csr;
	volatile u32	addrspace;
	volatile u32 	fontmsk;
	volatile u32	fontt;
	volatile u32	extent;
	volatile u32	src;
	u32		dst;
	volatile u32	copy;
	volatile u32	fill;
};

struct leo_lc_ss1_krn {
	u8	unknown;
};

struct leo_lc_ss1_usr {
	u8	unknown;
};

struct leo_ld {
	u8		xxx0[0xe00];
	volatile u32	csr;
	volatile u32	wid;
	volatile u32	wmask;
	volatile u32	widclip;
	volatile u32	vclipmin;
	volatile u32	vclipmax;
	volatile u32	pickmin;	/* SS1 only */
	volatile u32	pickmax;	/* SS1 only */
	volatile u32	fg;
	volatile u32	bg;
	volatile u32	src;		/* Copy/Scroll (SS0 only) */
	volatile u32	dst;		/* Copy/Scroll/Fill (SS0 only) */
	volatile u32	extent;		/* Copy/Scroll/Fill size (SS0 only) */
	u32		xxx1[3];
	volatile u32	setsem;		/* SS1 only */
	volatile u32	clrsem;		/* SS1 only */
	volatile u32	clrpick;	/* SS1 only */
	volatile u32	clrdat;		/* SS1 only */
	volatile u32	alpha;		/* SS1 only */
	u8		xxx2[0x2c];
	volatile u32	winbg;
	volatile u32	planemask;
	volatile u32	rop;
	volatile u32	z;
	volatile u32	dczf;		/* SS1 only */
	volatile u32	dczb;		/* SS1 only */
	volatile u32	dcs;		/* SS1 only */
	volatile u32	dczs;		/* SS1 only */
	volatile u32	pickfb;		/* SS1 only */
	volatile u32	pickbb;		/* SS1 only */
	volatile u32	dcfc;		/* SS1 only */
	volatile u32	forcecol;	/* SS1 only */
	volatile u32	door[8];	/* SS1 only */
	volatile u32	pick[5];	/* SS1 only */
};

#define LEO_SS1_MISC_ENABLE	0x00000001
#define LEO_SS1_MISC_STEREO	0x00000002
struct leo_ld_ss1 {
	u8		xxx0[0xef4];
	volatile u32	ss1_misc;
};

struct leo_ld_gbl {
	u8	unknown;
};

struct leo_par {
	spinlock_t		lock;
	struct leo_lx_krn	__iomem *lx_krn;
	struct leo_lc_ss0_usr	__iomem *lc_ss0_usr;
	struct leo_ld_ss0	__iomem *ld_ss0;
	struct leo_ld_ss1	__iomem *ld_ss1;
	struct leo_cursor	__iomem *cursor;
	u32			extent;
	u32			clut_data[256];

	u32			flags;
#define LEO_FLAG_BLANKED	0x00000001

	unsigned long		physbase;
	unsigned long		fbsize;

	struct sbus_dev		*sdev;
};

static void leo_wait(struct leo_lx_krn __iomem *lx_krn)
{
	int i;
	
	for (i = 0;
	     (sbus_readl(&lx_krn->krn_csr) & LEO_KRN_CSR_PROGRESS) && i < 300000;
	     i++)
		udelay (1); /* Busy wait at most 0.3 sec */
	return;
}

/**
 *      leo_setcolreg - Optional function. Sets a color register.
 *      @regno: boolean, 0 copy local, 1 get_user() function
 *      @red: frame buffer colormap structure
 *      @green: The green value which can be up to 16 bits wide
 *      @blue:  The blue value which can be up to 16 bits wide.
 *      @transp: If supported the alpha value which can be up to 16 bits wide.
 *      @info: frame buffer info structure
 */
static int leo_setcolreg(unsigned regno,
			 unsigned red, unsigned green, unsigned blue,
			 unsigned transp, struct fb_info *info)
{
	struct leo_par *par = (struct leo_par *) info->par;
        struct leo_lx_krn __iomem *lx_krn = par->lx_krn;
	unsigned long flags;
	u32 val;
	int i;

	if (regno >= 256)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	par->clut_data[regno] = red | (green << 8) | (blue << 16);

	spin_lock_irqsave(&par->lock, flags);

	leo_wait(lx_krn);

	sbus_writel(LEO_KRN_TYPE_CLUTDATA, &lx_krn->krn_type);
	for (i = 0; i < 256; i++)
		sbus_writel(par->clut_data[i], &lx_krn->krn_value);
	sbus_writel(LEO_KRN_TYPE_CLUT0, &lx_krn->krn_type);

	val = sbus_readl(&lx_krn->krn_csr);
	val |= (LEO_KRN_CSR_UNK | LEO_KRN_CSR_UNK2);
	sbus_writel(val, &lx_krn->krn_csr);

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

/**
 *      leo_blank - Optional function.  Blanks the display.
 *      @blank_mode: the blank mode we want.
 *      @info: frame buffer structure that represents a single frame buffer
 */
static int leo_blank(int blank, struct fb_info *info)
{
	struct leo_par *par = (struct leo_par *) info->par;
	struct leo_lx_krn __iomem *lx_krn = par->lx_krn;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&par->lock, flags);

	switch (blank) {
	case FB_BLANK_UNBLANK: /* Unblanking */
		val = sbus_readl(&lx_krn->krn_csr);
		val |= LEO_KRN_CSR_ENABLE;
		sbus_writel(val, &lx_krn->krn_csr);
		par->flags &= ~LEO_FLAG_BLANKED;
		break;

	case FB_BLANK_NORMAL: /* Normal blanking */
	case FB_BLANK_VSYNC_SUSPEND: /* VESA blank (vsync off) */
	case FB_BLANK_HSYNC_SUSPEND: /* VESA blank (hsync off) */
	case FB_BLANK_POWERDOWN: /* Poweroff */
		val = sbus_readl(&lx_krn->krn_csr);
		val &= ~LEO_KRN_CSR_ENABLE;
		sbus_writel(val, &lx_krn->krn_csr);
		par->flags |= LEO_FLAG_BLANKED;
		break;
	}

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

static struct sbus_mmap_map leo_mmap_map[] = {
	{
		.voff	= LEO_SS0_MAP,
		.poff	= LEO_OFF_SS0,
		.size	= 0x800000
	},
	{
		.voff	= LEO_LC_SS0_USR_MAP,
		.poff	= LEO_OFF_LC_SS0_USR,
		.size	= 0x1000
	},
	{
		.voff	= LEO_LD_SS0_MAP,
		.poff	= LEO_OFF_LD_SS0,
		.size	= 0x1000
	},
	{
		.voff	= LEO_LX_CURSOR_MAP,
		.poff	= LEO_OFF_LX_CURSOR,
		.size	= 0x1000
	},
	{
		.voff	= LEO_SS1_MAP,
		.poff	= LEO_OFF_SS1,
		.size	= 0x800000
	},
	{
		.voff	= LEO_LC_SS1_USR_MAP,
		.poff	= LEO_OFF_LC_SS1_USR,
		.size	= 0x1000
	},
	{
		.voff	= LEO_LD_SS1_MAP,
		.poff	= LEO_OFF_LD_SS1,
		.size	= 0x1000
	},
	{
		.voff	= LEO_UNK_MAP,
		.poff	= LEO_OFF_UNK,
		.size	= 0x1000
	},
	{
		.voff	= LEO_LX_KRN_MAP,
		.poff	= LEO_OFF_LX_KRN,
		.size	= 0x1000
	},
	{
		.voff	= LEO_LC_SS0_KRN_MAP,
		.poff	= LEO_OFF_LC_SS0_KRN,
		.size	= 0x1000
	},
	{
		.voff	= LEO_LC_SS1_KRN_MAP,
		.poff	= LEO_OFF_LC_SS1_KRN,
		.size	= 0x1000
	},
	{
		.voff	= LEO_LD_GBL_MAP,
		.poff	= LEO_OFF_LD_GBL,
		.size	= 0x1000
	},
	{
		.voff	= LEO_UNK2_MAP,
		.poff	= LEO_OFF_UNK2,
		.size	= 0x100000
	},
	{ .size = 0 }
};

static int leo_mmap(struct fb_info *info, struct file *file, struct vm_area_struct *vma)
{
	struct leo_par *par = (struct leo_par *)info->par;

	return sbusfb_mmap_helper(leo_mmap_map,
				  par->physbase, par->fbsize,
				  par->sdev->reg_addrs[0].which_io,
				  vma);
}

static int leo_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg, struct fb_info *info)
{
	struct leo_par *par = (struct leo_par *) info->par;

	return sbusfb_ioctl_helper(cmd, arg, info,
				   FBTYPE_SUNLEO, 32, par->fbsize);
}

/*
 *  Initialisation
 */

static void
leo_init_fix(struct fb_info *info)
{
	struct leo_par *par = (struct leo_par *)info->par;

	strlcpy(info->fix.id, par->sdev->prom_name, sizeof(info->fix.id));

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;

	info->fix.line_length = 8192;

	info->fix.accel = FB_ACCEL_SUN_LEO;
}

static void leo_wid_put(struct fb_info *info, struct fb_wid_list *wl)
{
	struct leo_par *par = (struct leo_par *) info->par;
	struct leo_lx_krn __iomem *lx_krn = par->lx_krn;
	struct fb_wid_item *wi;
	unsigned long flags;
	u32 val;
	int i, j;

	spin_lock_irqsave(&par->lock, flags);

	leo_wait(lx_krn);

	for (i = 0, wi = wl->wl_list; i < wl->wl_count; i++, wi++) {
		switch(wi->wi_type) {
		case FB_WID_DBL_8:
			j = (wi->wi_index & 0xf) + 0x40;
			break;

		case FB_WID_DBL_24:
			j = wi->wi_index & 0x3f;
			break;

		default:
			continue;
		};
		sbus_writel(0x5800 + j, &lx_krn->krn_type);
		sbus_writel(wi->wi_values[0], &lx_krn->krn_value);
	}
	sbus_writel(LEO_KRN_TYPE_WID, &lx_krn->krn_type);

	val = sbus_readl(&lx_krn->krn_csr);
	val |= (LEO_KRN_CSR_UNK | LEO_KRN_CSR_UNK2);
	sbus_writel(val, &lx_krn->krn_csr);

	spin_unlock_irqrestore(&par->lock, flags);
}

static void leo_init_wids(struct fb_info *info)
{
	struct fb_wid_item wi;
	struct fb_wid_list wl;

	wl.wl_count = 1;
	wl.wl_list = &wi;
	wi.wi_type = FB_WID_DBL_8;
	wi.wi_index = 0;
	wi.wi_values [0] = 0x2c0;
	leo_wid_put(info, &wl);
	wi.wi_index = 1;
	wi.wi_values [0] = 0x30;
	leo_wid_put(info, &wl);
	wi.wi_index = 2;
	wi.wi_values [0] = 0x20;
	leo_wid_put(info, &wl);
	wi.wi_type = FB_WID_DBL_24;
	wi.wi_index = 1;
	wi.wi_values [0] = 0x30;
	leo_wid_put(info, &wl);

}

static void leo_switch_from_graph(struct fb_info *info)
{
	struct leo_par *par = (struct leo_par *) info->par;
	struct leo_ld __iomem *ss = (struct leo_ld __iomem *) par->ld_ss0;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&par->lock, flags);

	par->extent = ((info->var.xres - 1) |
		       ((info->var.yres - 1) << 16));

	sbus_writel(0xffffffff, &ss->wid);
	sbus_writel(0xffff, &ss->wmask);
	sbus_writel(0, &ss->vclipmin);
	sbus_writel(par->extent, &ss->vclipmax);
	sbus_writel(0, &ss->fg);
	sbus_writel(0xff000000, &ss->planemask);
	sbus_writel(0x310850, &ss->rop);
	sbus_writel(0, &ss->widclip);
	sbus_writel((info->var.xres-1) | ((info->var.yres-1) << 11),
		    &par->lc_ss0_usr->extent);
	sbus_writel(4, &par->lc_ss0_usr->addrspace);
	sbus_writel(0x80000000, &par->lc_ss0_usr->fill);
	sbus_writel(0, &par->lc_ss0_usr->fontt);
	do {
		val = sbus_readl(&par->lc_ss0_usr->csr);
	} while (val & 0x20000000);

	spin_unlock_irqrestore(&par->lock, flags);
}

static int leo_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	/* We just use this to catch switches out of
	 * graphics mode.
	 */
	leo_switch_from_graph(info);

	if (var->xoffset || var->yoffset || var->vmode)
		return -EINVAL;
	return 0;
}

static void leo_init_hw(struct fb_info *info)
{
	struct leo_par *par = (struct leo_par *) info->par;
	u32 val;

	val = sbus_readl(&par->ld_ss1->ss1_misc);
	val |= LEO_SS1_MISC_ENABLE;
	sbus_writel(val, &par->ld_ss1->ss1_misc);

	leo_switch_from_graph(info);
}

static void leo_fixup_var_rgb(struct fb_var_screeninfo *var)
{
	var->red.offset = 0;
	var->red.length = 8;
	var->green.offset = 8;
	var->green.length = 8;
	var->blue.offset = 16;
	var->blue.length = 8;
	var->transp.offset = 0;
	var->transp.length = 0;
}

struct all_info {
	struct fb_info info;
	struct leo_par par;
	struct list_head list;
};
static LIST_HEAD(leo_list);

static void leo_init_one(struct sbus_dev *sdev)
{
	struct all_info *all;
	int linebytes;

	all = kmalloc(sizeof(*all), GFP_KERNEL);
	if (!all) {
		printk(KERN_ERR "leo: Cannot allocate memory.\n");
		return;
	}
	memset(all, 0, sizeof(*all));

	INIT_LIST_HEAD(&all->list);

	spin_lock_init(&all->par.lock);
	all->par.sdev = sdev;

	all->par.physbase = sdev->reg_addrs[0].phys_addr;

	sbusfb_fill_var(&all->info.var, sdev->prom_node, 32);
	leo_fixup_var_rgb(&all->info.var);

	linebytes = prom_getintdefault(sdev->prom_node, "linebytes",
				       all->info.var.xres);
	all->par.fbsize = PAGE_ALIGN(linebytes * all->info.var.yres);

#ifdef CONFIG_SPARC32
	all->info.screen_base = (char __iomem *)
		prom_getintdefault(sdev->prom_node, "address", 0);
#endif
	if (!all->info.screen_base)
		all->info.screen_base = 
			sbus_ioremap(&sdev->resource[0], LEO_OFF_SS0,
				     0x800000, "leo ram");

	all->par.lc_ss0_usr =
		sbus_ioremap(&sdev->resource[0], LEO_OFF_LC_SS0_USR,
			     0x1000, "leolc ss0usr");
	all->par.ld_ss0 =
		sbus_ioremap(&sdev->resource[0], LEO_OFF_LD_SS0,
			     0x1000, "leold ss0");
	all->par.ld_ss1 =
		sbus_ioremap(&sdev->resource[0], LEO_OFF_LD_SS1,
			     0x1000, "leold ss1");
	all->par.lx_krn =
		sbus_ioremap(&sdev->resource[0], LEO_OFF_LX_KRN,
			     0x1000, "leolx krn");
	all->par.cursor =
		sbus_ioremap(&sdev->resource[0], LEO_OFF_LX_CURSOR,
			     sizeof(struct leo_cursor), "leolx cursor");

	all->info.flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;
	all->info.fbops = &leo_ops;
	all->info.par = &all->par;

	leo_init_wids(&all->info);
	leo_init_hw(&all->info);

	leo_blank(0, &all->info);

	if (fb_alloc_cmap(&all->info.cmap, 256, 0)) {
		printk(KERN_ERR "leo: Could not allocate color map.\n");
		kfree(all);
		return;
	}

	leo_init_fix(&all->info);

	if (register_framebuffer(&all->info) < 0) {
		printk(KERN_ERR "leo: Could not register framebuffer.\n");
		fb_dealloc_cmap(&all->info.cmap);
		kfree(all);
		return;
	}

	list_add(&all->list, &leo_list);

	printk("leo: %s at %lx:%lx\n",
	       sdev->prom_name,
	       (long) sdev->reg_addrs[0].which_io,
	       (long) sdev->reg_addrs[0].phys_addr);
}

int __init leo_init(void)
{
	struct sbus_bus *sbus;
	struct sbus_dev *sdev;

	if (fb_get_options("leofb", NULL))
		return -ENODEV;

	for_all_sbusdev(sdev, sbus) {
		if (!strcmp(sdev->prom_name, "leo"))
			leo_init_one(sdev);
	}

	return 0;
}

void __exit leo_exit(void)
{
	struct list_head *pos, *tmp;

	list_for_each_safe(pos, tmp, &leo_list) {
		struct all_info *all = list_entry(pos, typeof(*all), list);

		unregister_framebuffer(&all->info);
		fb_dealloc_cmap(&all->info.cmap);
		kfree(all);
	}
}

int __init
leo_setup(char *arg)
{
	/* No cmdline options yet... */
	return 0;
}

module_init(leo_init);
#ifdef MODULE
module_exit(leo_exit);
#endif

MODULE_DESCRIPTION("framebuffer driver for LEO chipsets");
MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_LICENSE("GPL");
