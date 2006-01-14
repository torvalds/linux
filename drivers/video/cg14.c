/* cg14.c: CGFOURTEEN frame buffer driver
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 * Copyright (C) 1996,1998 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
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

static int cg14_setcolreg(unsigned, unsigned, unsigned, unsigned,
			 unsigned, struct fb_info *);

static int cg14_mmap(struct fb_info *, struct vm_area_struct *);
static int cg14_ioctl(struct fb_info *, unsigned int, unsigned long);
static int cg14_pan_display(struct fb_var_screeninfo *, struct fb_info *);

/*
 *  Frame buffer operations
 */

static struct fb_ops cg14_ops = {
	.owner			= THIS_MODULE,
	.fb_setcolreg		= cg14_setcolreg,
	.fb_pan_display		= cg14_pan_display,
	.fb_fillrect		= cfb_fillrect,
	.fb_copyarea		= cfb_copyarea,
	.fb_imageblit		= cfb_imageblit,
	.fb_mmap		= cg14_mmap,
	.fb_ioctl		= cg14_ioctl,
#ifdef CONFIG_COMPAT
	.fb_compat_ioctl	= sbusfb_compat_ioctl,
#endif
};

#define CG14_MCR_INTENABLE_SHIFT	7
#define CG14_MCR_INTENABLE_MASK		0x80
#define CG14_MCR_VIDENABLE_SHIFT	6
#define CG14_MCR_VIDENABLE_MASK		0x40
#define CG14_MCR_PIXMODE_SHIFT		4
#define CG14_MCR_PIXMODE_MASK		0x30
#define CG14_MCR_TMR_SHIFT		2
#define CG14_MCR_TMR_MASK		0x0c
#define CG14_MCR_TMENABLE_SHIFT		1
#define CG14_MCR_TMENABLE_MASK		0x02
#define CG14_MCR_RESET_SHIFT		0
#define CG14_MCR_RESET_MASK		0x01
#define CG14_REV_REVISION_SHIFT		4
#define CG14_REV_REVISION_MASK		0xf0
#define CG14_REV_IMPL_SHIFT		0
#define CG14_REV_IMPL_MASK		0x0f
#define CG14_VBR_FRAMEBASE_SHIFT	12
#define CG14_VBR_FRAMEBASE_MASK		0x00fff000
#define CG14_VMCR1_SETUP_SHIFT		0
#define CG14_VMCR1_SETUP_MASK		0x000001ff
#define CG14_VMCR1_VCONFIG_SHIFT	9
#define CG14_VMCR1_VCONFIG_MASK		0x00000e00
#define CG14_VMCR2_REFRESH_SHIFT	0
#define CG14_VMCR2_REFRESH_MASK		0x00000001
#define CG14_VMCR2_TESTROWCNT_SHIFT	1
#define CG14_VMCR2_TESTROWCNT_MASK	0x00000002
#define CG14_VMCR2_FBCONFIG_SHIFT	2
#define CG14_VMCR2_FBCONFIG_MASK	0x0000000c
#define CG14_VCR_REFRESHREQ_SHIFT	0
#define CG14_VCR_REFRESHREQ_MASK	0x000003ff
#define CG14_VCR1_REFRESHENA_SHIFT	10
#define CG14_VCR1_REFRESHENA_MASK	0x00000400
#define CG14_VCA_CAD_SHIFT		0
#define CG14_VCA_CAD_MASK		0x000003ff
#define CG14_VCA_VERS_SHIFT		10
#define CG14_VCA_VERS_MASK		0x00000c00
#define CG14_VCA_RAMSPEED_SHIFT		12
#define CG14_VCA_RAMSPEED_MASK		0x00001000
#define CG14_VCA_8MB_SHIFT		13
#define CG14_VCA_8MB_MASK		0x00002000

#define CG14_MCR_PIXMODE_8		0
#define CG14_MCR_PIXMODE_16		2
#define CG14_MCR_PIXMODE_32		3

struct cg14_regs{
	volatile u8 mcr;	/* Master Control Reg */
	volatile u8 ppr;	/* Packed Pixel Reg */
	volatile u8 tms[2];	/* Test Mode Status Regs */
	volatile u8 msr;	/* Master Status Reg */
	volatile u8 fsr;	/* Fault Status Reg */
	volatile u8 rev;	/* Revision & Impl */
	volatile u8 ccr;	/* Clock Control Reg */
	volatile u32 tmr;	/* Test Mode Read Back */
	volatile u8 mod;	/* Monitor Operation Data Reg */
	volatile u8 acr;	/* Aux Control */
	u8 xxx0[6];
	volatile u16 hct;	/* Hor Counter */
	volatile u16 vct;	/* Vert Counter */
	volatile u16 hbs;	/* Hor Blank Start */
	volatile u16 hbc;	/* Hor Blank Clear */
	volatile u16 hss;	/* Hor Sync Start */
	volatile u16 hsc;	/* Hor Sync Clear */
	volatile u16 csc;	/* Composite Sync Clear */
	volatile u16 vbs;	/* Vert Blank Start */
	volatile u16 vbc;	/* Vert Blank Clear */
	volatile u16 vss;	/* Vert Sync Start */
	volatile u16 vsc;	/* Vert Sync Clear */
	volatile u16 xcs;
	volatile u16 xcc;
	volatile u16 fsa;	/* Fault Status Address */
	volatile u16 adr;	/* Address Registers */
	u8 xxx1[0xce];
	volatile u8 pcg[0x100]; /* Pixel Clock Generator */
	volatile u32 vbr;	/* Frame Base Row */
	volatile u32 vmcr;	/* VBC Master Control */
	volatile u32 vcr;	/* VBC refresh */
	volatile u32 vca;	/* VBC Config */
};

#define CG14_CCR_ENABLE	0x04
#define CG14_CCR_SELECT 0x02	/* HW/Full screen */

struct cg14_cursor {
	volatile u32 cpl0[32];	/* Enable plane 0 */
	volatile u32 cpl1[32];  /* Color selection plane */
	volatile u8 ccr;	/* Cursor Control Reg */
	u8 xxx0[3];
	volatile u16 cursx;	/* Cursor x,y position */
	volatile u16 cursy;	/* Cursor x,y position */
	volatile u32 color0;
	volatile u32 color1;
	u32 xxx1[0x1bc];
	volatile u32 cpl0i[32];	/* Enable plane 0 autoinc */
	volatile u32 cpl1i[32]; /* Color selection autoinc */
};

struct cg14_dac {
	volatile u8 addr;	/* Address Register */
	u8 xxx0[255];
	volatile u8 glut;	/* Gamma table */
	u8 xxx1[255];
	volatile u8 select;	/* Register Select */
	u8 xxx2[255];
	volatile u8 mode;	/* Mode Register */
};

struct cg14_xlut{
	volatile u8 x_xlut [256];
	volatile u8 x_xlutd [256];
	u8 xxx0[0x600];
	volatile u8 x_xlut_inc [256];
	volatile u8 x_xlutd_inc [256];
};

/* Color look up table (clut) */
/* Each one of these arrays hold the color lookup table (for 256
 * colors) for each MDI page (I assume then there should be 4 MDI
 * pages, I still wonder what they are.  I have seen NeXTStep split
 * the screen in four parts, while operating in 24 bits mode.  Each
 * integer holds 4 values: alpha value (transparency channel, thanks
 * go to John Stone (johns@umr.edu) from OpenBSD), red, green and blue
 *
 * I currently use the clut instead of the Xlut
 */
struct cg14_clut {
	u32 c_clut [256];
	u32 c_clutd [256];    /* i wonder what the 'd' is for */
	u32 c_clut_inc [256];
	u32 c_clutd_inc [256];
};

#define CG14_MMAP_ENTRIES	16

struct cg14_par {
	spinlock_t		lock;
	struct cg14_regs	__iomem *regs;
	struct cg14_clut	__iomem *clut;
	struct cg14_cursor	__iomem *cursor;

	u32			flags;
#define CG14_FLAG_BLANKED	0x00000001

	unsigned long		physbase;
	unsigned long		iospace;
	unsigned long		fbsize;

	struct sbus_mmap_map	mmap_map[CG14_MMAP_ENTRIES];

	int			mode;
	int			ramsize;
	struct sbus_dev		*sdev;
};

static void __cg14_reset(struct cg14_par *par)
{
	struct cg14_regs __iomem *regs = par->regs;
	u8 val;

	val = sbus_readb(&regs->mcr);
	val &= ~(CG14_MCR_PIXMODE_MASK);
	sbus_writeb(val, &regs->mcr);
}

static int cg14_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct cg14_par *par = (struct cg14_par *) info->par;
	unsigned long flags;

	/* We just use this to catch switches out of
	 * graphics mode.
	 */
	spin_lock_irqsave(&par->lock, flags);
	__cg14_reset(par);
	spin_unlock_irqrestore(&par->lock, flags);

	if (var->xoffset || var->yoffset || var->vmode)
		return -EINVAL;
	return 0;
}

/**
 *      cg14_setcolreg - Optional function. Sets a color register.
 *      @regno: boolean, 0 copy local, 1 get_user() function
 *      @red: frame buffer colormap structure
 *      @green: The green value which can be up to 16 bits wide
 *      @blue:  The blue value which can be up to 16 bits wide.
 *      @transp: If supported the alpha value which can be up to 16 bits wide.
 *      @info: frame buffer info structure
 */
static int cg14_setcolreg(unsigned regno,
			  unsigned red, unsigned green, unsigned blue,
			  unsigned transp, struct fb_info *info)
{
	struct cg14_par *par = (struct cg14_par *) info->par;
	struct cg14_clut __iomem *clut = par->clut;
	unsigned long flags;
	u32 val;

	if (regno >= 256)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;
	val = (red | (green << 8) | (blue << 16));

	spin_lock_irqsave(&par->lock, flags);
	sbus_writel(val, &clut->c_clut[regno]);
	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

static int cg14_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct cg14_par *par = (struct cg14_par *) info->par;

	return sbusfb_mmap_helper(par->mmap_map,
				  par->physbase, par->fbsize,
				  par->iospace, vma);
}

static int cg14_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	struct cg14_par *par = (struct cg14_par *) info->par;
	struct cg14_regs __iomem *regs = par->regs;
	struct mdi_cfginfo kmdi, __user *mdii;
	unsigned long flags;
	int cur_mode, mode, ret = 0;

	switch (cmd) {
	case MDI_RESET:
		spin_lock_irqsave(&par->lock, flags);
		__cg14_reset(par);
		spin_unlock_irqrestore(&par->lock, flags);
		break;

	case MDI_GET_CFGINFO:
		memset(&kmdi, 0, sizeof(kmdi));

		spin_lock_irqsave(&par->lock, flags);
		kmdi.mdi_type = FBTYPE_MDICOLOR;
		kmdi.mdi_height = info->var.yres;
		kmdi.mdi_width = info->var.xres;
		kmdi.mdi_mode = par->mode;
		kmdi.mdi_pixfreq = 72; /* FIXME */
		kmdi.mdi_size = par->ramsize;
		spin_unlock_irqrestore(&par->lock, flags);

		mdii = (struct mdi_cfginfo __user *) arg;
		if (copy_to_user(mdii, &kmdi, sizeof(kmdi)))
			ret = -EFAULT;
		break;

	case MDI_SET_PIXELMODE:
		if (get_user(mode, (int __user *) arg)) {
			ret = -EFAULT;
			break;
		}

		spin_lock_irqsave(&par->lock, flags);
		cur_mode = sbus_readb(&regs->mcr);
		cur_mode &= ~CG14_MCR_PIXMODE_MASK;
		switch(mode) {
		case MDI_32_PIX:
			cur_mode |= (CG14_MCR_PIXMODE_32 <<
				     CG14_MCR_PIXMODE_SHIFT);
			break;

		case MDI_16_PIX:
			cur_mode |= (CG14_MCR_PIXMODE_16 <<
				     CG14_MCR_PIXMODE_SHIFT);
			break;

		case MDI_8_PIX:
			break;

		default:
			ret = -ENOSYS;
			break;
		};
		if (!ret) {
			sbus_writeb(cur_mode, &regs->mcr);
			par->mode = mode;
		}
		spin_unlock_irqrestore(&par->lock, flags);
		break;

	default:
		ret = sbusfb_ioctl_helper(cmd, arg, info,
					  FBTYPE_MDICOLOR, 8, par->fbsize);
		break;
	};

	return ret;
}

/*
 *  Initialisation
 */

static void cg14_init_fix(struct fb_info *info, int linebytes)
{
	struct cg14_par *par = (struct cg14_par *)info->par;
	const char *name;

	name = "cgfourteen";
	if (par->sdev)
		name = par->sdev->prom_name;

	strlcpy(info->fix.id, name, sizeof(info->fix.id));

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;

	info->fix.line_length = linebytes;

	info->fix.accel = FB_ACCEL_SUN_CG14;
}

static struct sbus_mmap_map __cg14_mmap_map[CG14_MMAP_ENTRIES] __initdata = {
	{
		.voff	= CG14_REGS,
		.poff	= 0x80000000,
		.size	= 0x1000
	},
	{
		.voff	= CG14_XLUT,
		.poff	= 0x80003000,
		.size	= 0x1000
	},
	{
		.voff	= CG14_CLUT1,
		.poff	= 0x80004000,
		.size	= 0x1000
	},
	{
		.voff	= CG14_CLUT2,
		.poff	= 0x80005000,
		.size	= 0x1000
	},
	{
		.voff	= CG14_CLUT3,
		.poff	= 0x80006000,
		.size	= 0x1000
	},
	{
		.voff	= CG3_MMAP_OFFSET - 0x7000,
		.poff	= 0x80000000,
		.size	= 0x7000
	},
	{
		.voff	= CG3_MMAP_OFFSET,
		.poff	= 0x00000000,
		.size	= SBUS_MMAP_FBSIZE(1)
	},
	{
		.voff	= MDI_CURSOR_MAP,
		.poff	= 0x80001000,
		.size	= 0x1000
	},
	{
		.voff	= MDI_CHUNKY_BGR_MAP,
		.poff	= 0x01000000,
		.size	= 0x400000
	},
	{
		.voff	= MDI_PLANAR_X16_MAP,
		.poff	= 0x02000000,
		.size	= 0x200000
	},
	{
		.voff	= MDI_PLANAR_C16_MAP,
		.poff	= 0x02800000,
		.size	= 0x200000
	},
	{
		.voff	= MDI_PLANAR_X32_MAP,
		.poff	= 0x03000000,
		.size	= 0x100000
	},
	{
		.voff	= MDI_PLANAR_B32_MAP,
		.poff	= 0x03400000,
		.size	= 0x100000
	},
	{
		.voff	= MDI_PLANAR_G32_MAP,
		.poff	= 0x03800000,
		.size	= 0x100000
	},
	{
		.voff	= MDI_PLANAR_R32_MAP,
		.poff	= 0x03c00000,
		.size	= 0x100000
	},
	{ .size = 0 }
};

struct all_info {
	struct fb_info info;
	struct cg14_par par;
	struct list_head list;
};
static LIST_HEAD(cg14_list);

static void cg14_init_one(struct sbus_dev *sdev, int node, int parent_node)
{
	struct all_info *all;
	unsigned long phys, rphys;
	u32 bases[6];
	int is_8mb, linebytes, i;

	if (!sdev) {
		if (prom_getproperty(node, "address",
				     (char *) &bases[0], sizeof(bases)) <= 0
		    || !bases[0]) {
			printk(KERN_ERR "cg14: Device is not mapped.\n");
			return;
		}
		if (__get_iospace(bases[0]) != __get_iospace(bases[1])) {
			printk(KERN_ERR "cg14: I/O spaces don't match.\n");
			return;
		}
	}

	all = kmalloc(sizeof(*all), GFP_KERNEL);
	if (!all) {
		printk(KERN_ERR "cg14: Cannot allocate memory.\n");
		return;
	}
	memset(all, 0, sizeof(*all));

	INIT_LIST_HEAD(&all->list);

	spin_lock_init(&all->par.lock);

	sbusfb_fill_var(&all->info.var, node, 8);
	all->info.var.red.length = 8;
	all->info.var.green.length = 8;
	all->info.var.blue.length = 8;

	linebytes = prom_getintdefault(node, "linebytes",
				       all->info.var.xres);
	all->par.fbsize = PAGE_ALIGN(linebytes * all->info.var.yres);

	all->par.sdev = sdev;
	if (sdev) {
		rphys = sdev->reg_addrs[0].phys_addr;
		all->par.physbase = phys = sdev->reg_addrs[1].phys_addr;
		all->par.iospace = sdev->reg_addrs[0].which_io;

		all->par.regs = sbus_ioremap(&sdev->resource[0], 0,
				     sizeof(struct cg14_regs),
				     "cg14 regs");
		all->par.clut = sbus_ioremap(&sdev->resource[0], CG14_CLUT1,
				     sizeof(struct cg14_clut),
				     "cg14 clut");
		all->par.cursor = sbus_ioremap(&sdev->resource[0], CG14_CURSORREGS,
				     sizeof(struct cg14_cursor),
				     "cg14 cursor");
		all->info.screen_base = sbus_ioremap(&sdev->resource[1], 0,
				     all->par.fbsize, "cg14 ram");
	} else {
		rphys = __get_phys(bases[0]);
		all->par.physbase = phys = __get_phys(bases[1]);
		all->par.iospace = __get_iospace(bases[0]);
		all->par.regs = (struct cg14_regs __iomem *)(unsigned long)bases[0];
		all->par.clut = (struct cg14_clut __iomem *)((unsigned long)bases[0] +
						     CG14_CLUT1);
		all->par.cursor =
			(struct cg14_cursor __iomem *)((unsigned long)bases[0] +
					       CG14_CURSORREGS);

		all->info.screen_base = (char __iomem *)(unsigned long)bases[1];
	}

	prom_getproperty(node, "reg", (char *) &bases[0], sizeof(bases));
	is_8mb = (bases[5] == 0x800000);

	if (sizeof(all->par.mmap_map) != sizeof(__cg14_mmap_map)) {
		extern void __cg14_mmap_sized_wrongly(void);

		__cg14_mmap_sized_wrongly();
	}
		
	memcpy(&all->par.mmap_map, &__cg14_mmap_map, sizeof(all->par.mmap_map));
	for (i = 0; i < CG14_MMAP_ENTRIES; i++) {
		struct sbus_mmap_map *map = &all->par.mmap_map[i];

		if (!map->size)
			break;
		if (map->poff & 0x80000000)
			map->poff = (map->poff & 0x7fffffff) + rphys - phys;
		if (is_8mb &&
		    map->size >= 0x100000 &&
		    map->size <= 0x400000)
			map->size *= 2;
	}

	all->par.mode = MDI_8_PIX;
	all->par.ramsize = (is_8mb ? 0x800000 : 0x400000);

	all->info.flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;
	all->info.fbops = &cg14_ops;
	all->info.par = &all->par;

	__cg14_reset(&all->par);

	if (fb_alloc_cmap(&all->info.cmap, 256, 0)) {
		printk(KERN_ERR "cg14: Could not allocate color map.\n");
		kfree(all);
		return;
	}
	fb_set_cmap(&all->info.cmap, &all->info);

	cg14_init_fix(&all->info, linebytes);

	if (register_framebuffer(&all->info) < 0) {
		printk(KERN_ERR "cg14: Could not register framebuffer.\n");
		fb_dealloc_cmap(&all->info.cmap);
		kfree(all);
		return;
	}

	list_add(&all->list, &cg14_list);

	printk("cg14: cgfourteen at %lx:%lx, %dMB\n",
	       all->par.iospace, all->par.physbase, all->par.ramsize >> 20);

}

int __init cg14_init(void)
{
	struct sbus_bus *sbus;
	struct sbus_dev *sdev;

	if (fb_get_options("cg14fb", NULL))
		return -ENODEV;

#ifdef CONFIG_SPARC32
	{
		int root, node;

		root = prom_getchild(prom_root_node);
		root = prom_searchsiblings(root, "obio");
		if (root) {
			node = prom_searchsiblings(prom_getchild(root),
						   "cgfourteen");
			if (node)
				cg14_init_one(NULL, node, root);
		}
	}
#endif
	for_all_sbusdev(sdev, sbus) {
		if (!strcmp(sdev->prom_name, "cgfourteen"))
			cg14_init_one(sdev, sdev->prom_node, sbus->prom_node);
	}

	return 0;
}

void __exit cg14_exit(void)
{
	struct list_head *pos, *tmp;

	list_for_each_safe(pos, tmp, &cg14_list) {
		struct all_info *all = list_entry(pos, typeof(*all), list);

		unregister_framebuffer(&all->info);
		fb_dealloc_cmap(&all->info.cmap);
		kfree(all);
	}
}

int __init
cg14_setup(char *arg)
{
	/* No cmdline options yet... */
	return 0;
}

module_init(cg14_init);

#ifdef MODULE
module_exit(cg14_exit);
#endif

MODULE_DESCRIPTION("framebuffer driver for CGfourteen chipsets");
MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_LICENSE("GPL");
