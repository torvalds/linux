/* cg6.c: CGSIX (GX, GXplus, TGX) frame buffer driver
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

static int cg6_setcolreg(unsigned, unsigned, unsigned, unsigned,
			 unsigned, struct fb_info *);
static int cg6_blank(int, struct fb_info *);

static void cg6_imageblit(struct fb_info *, const struct fb_image *);
static void cg6_fillrect(struct fb_info *, const struct fb_fillrect *);
static int cg6_sync(struct fb_info *);
static int cg6_mmap(struct fb_info *, struct file *, struct vm_area_struct *);
static int cg6_ioctl(struct inode *, struct file *, unsigned int,
		     unsigned long, struct fb_info *);

/*
 *  Frame buffer operations
 */

static struct fb_ops cg6_ops = {
	.owner			= THIS_MODULE,
	.fb_setcolreg		= cg6_setcolreg,
	.fb_blank		= cg6_blank,
	.fb_fillrect		= cg6_fillrect,
	.fb_copyarea		= cfb_copyarea,
	.fb_imageblit		= cg6_imageblit,
	.fb_sync		= cg6_sync,
	.fb_mmap		= cg6_mmap,
	.fb_ioctl		= cg6_ioctl,
#ifdef CONFIG_COMPAT
	.fb_compat_ioctl	= sbusfb_compat_ioctl,
#endif
};

/* Offset of interesting structures in the OBIO space */
/*
 * Brooktree is the video dac and is funny to program on the cg6.
 * (it's even funnier on the cg3)
 * The FBC could be the frame buffer control
 * The FHC could is the frame buffer hardware control.
 */
#define CG6_ROM_OFFSET       0x0UL
#define CG6_BROOKTREE_OFFSET 0x200000UL
#define CG6_DHC_OFFSET       0x240000UL
#define CG6_ALT_OFFSET       0x280000UL
#define CG6_FHC_OFFSET       0x300000UL
#define CG6_THC_OFFSET       0x301000UL
#define CG6_FBC_OFFSET       0x700000UL
#define CG6_TEC_OFFSET       0x701000UL
#define CG6_RAM_OFFSET       0x800000UL

/* FHC definitions */
#define CG6_FHC_FBID_SHIFT           24
#define CG6_FHC_FBID_MASK            255
#define CG6_FHC_REV_SHIFT            20
#define CG6_FHC_REV_MASK             15
#define CG6_FHC_FROP_DISABLE         (1 << 19)
#define CG6_FHC_ROW_DISABLE          (1 << 18)
#define CG6_FHC_SRC_DISABLE          (1 << 17)
#define CG6_FHC_DST_DISABLE          (1 << 16)
#define CG6_FHC_RESET                (1 << 15)
#define CG6_FHC_LITTLE_ENDIAN        (1 << 13)
#define CG6_FHC_RES_MASK             (3 << 11)
#define CG6_FHC_1024                 (0 << 11)
#define CG6_FHC_1152                 (1 << 11)
#define CG6_FHC_1280                 (2 << 11)
#define CG6_FHC_1600                 (3 << 11)
#define CG6_FHC_CPU_MASK             (3 << 9)
#define CG6_FHC_CPU_SPARC            (0 << 9)
#define CG6_FHC_CPU_68020            (1 << 9)
#define CG6_FHC_CPU_386              (2 << 9)
#define CG6_FHC_TEST		     (1 << 8)
#define CG6_FHC_TEST_X_SHIFT	     4
#define CG6_FHC_TEST_X_MASK	     15
#define CG6_FHC_TEST_Y_SHIFT	     0
#define CG6_FHC_TEST_Y_MASK	     15

/* FBC mode definitions */
#define CG6_FBC_BLIT_IGNORE		0x00000000
#define CG6_FBC_BLIT_NOSRC		0x00100000
#define CG6_FBC_BLIT_SRC		0x00200000
#define CG6_FBC_BLIT_ILLEGAL		0x00300000
#define CG6_FBC_BLIT_MASK		0x00300000

#define CG6_FBC_VBLANK			0x00080000

#define CG6_FBC_MODE_IGNORE		0x00000000
#define CG6_FBC_MODE_COLOR8		0x00020000
#define CG6_FBC_MODE_COLOR1		0x00040000
#define CG6_FBC_MODE_HRMONO		0x00060000
#define CG6_FBC_MODE_MASK		0x00060000

#define CG6_FBC_DRAW_IGNORE		0x00000000
#define CG6_FBC_DRAW_RENDER		0x00008000
#define CG6_FBC_DRAW_PICK		0x00010000
#define CG6_FBC_DRAW_ILLEGAL		0x00018000
#define CG6_FBC_DRAW_MASK		0x00018000

#define CG6_FBC_BWRITE0_IGNORE		0x00000000
#define CG6_FBC_BWRITE0_ENABLE		0x00002000
#define CG6_FBC_BWRITE0_DISABLE		0x00004000
#define CG6_FBC_BWRITE0_ILLEGAL		0x00006000
#define CG6_FBC_BWRITE0_MASK		0x00006000

#define CG6_FBC_BWRITE1_IGNORE		0x00000000
#define CG6_FBC_BWRITE1_ENABLE		0x00000800
#define CG6_FBC_BWRITE1_DISABLE		0x00001000
#define CG6_FBC_BWRITE1_ILLEGAL		0x00001800
#define CG6_FBC_BWRITE1_MASK		0x00001800

#define CG6_FBC_BREAD_IGNORE		0x00000000
#define CG6_FBC_BREAD_0			0x00000200
#define CG6_FBC_BREAD_1			0x00000400
#define CG6_FBC_BREAD_ILLEGAL		0x00000600
#define CG6_FBC_BREAD_MASK		0x00000600

#define CG6_FBC_BDISP_IGNORE		0x00000000
#define CG6_FBC_BDISP_0			0x00000080
#define CG6_FBC_BDISP_1			0x00000100
#define CG6_FBC_BDISP_ILLEGAL		0x00000180
#define CG6_FBC_BDISP_MASK		0x00000180

#define CG6_FBC_INDEX_MOD		0x00000040
#define CG6_FBC_INDEX_MASK		0x00000030

/* THC definitions */
#define CG6_THC_MISC_REV_SHIFT       16
#define CG6_THC_MISC_REV_MASK        15
#define CG6_THC_MISC_RESET           (1 << 12)
#define CG6_THC_MISC_VIDEO           (1 << 10)
#define CG6_THC_MISC_SYNC            (1 << 9)
#define CG6_THC_MISC_VSYNC           (1 << 8)
#define CG6_THC_MISC_SYNC_ENAB       (1 << 7)
#define CG6_THC_MISC_CURS_RES        (1 << 6)
#define CG6_THC_MISC_INT_ENAB        (1 << 5)
#define CG6_THC_MISC_INT             (1 << 4)
#define CG6_THC_MISC_INIT            0x9f

/* The contents are unknown */
struct cg6_tec {
	volatile int tec_matrix;
	volatile int tec_clip;
	volatile int tec_vdc;
};

struct cg6_thc {
        uint thc_pad0[512];
	volatile uint thc_hs;		/* hsync timing */
	volatile uint thc_hsdvs;
	volatile uint thc_hd;
	volatile uint thc_vs;		/* vsync timing */
	volatile uint thc_vd;
	volatile uint thc_refresh;
	volatile uint thc_misc;
	uint thc_pad1[56];
	volatile uint thc_cursxy;	/* cursor x,y position (16 bits each) */
	volatile uint thc_cursmask[32];	/* cursor mask bits */
	volatile uint thc_cursbits[32];	/* what to show where mask enabled */
};

struct cg6_fbc {
	u32		xxx0[1];
	volatile u32	mode;
	volatile u32	clip;
	u32		xxx1[1];	    
	volatile u32	s;
	volatile u32	draw;
	volatile u32	blit;
	volatile u32	font;
	u32		xxx2[24];
	volatile u32	x0, y0, z0, color0;
	volatile u32	x1, y1, z1, color1;
	volatile u32	x2, y2, z2, color2;
	volatile u32	x3, y3, z3, color3;
	volatile u32	offx, offy;
	u32		xxx3[2];
	volatile u32	incx, incy;
	u32		xxx4[2];
	volatile u32	clipminx, clipminy;
	u32		xxx5[2];
	volatile u32	clipmaxx, clipmaxy;
	u32		xxx6[2];
	volatile u32	fg;
	volatile u32	bg;
	volatile u32	alu;
	volatile u32	pm;
	volatile u32	pixelm;
	u32		xxx7[2];
	volatile u32	patalign;
	volatile u32	pattern[8];
	u32		xxx8[432];
	volatile u32	apointx, apointy, apointz;
	u32		xxx9[1];
	volatile u32	rpointx, rpointy, rpointz;
	u32		xxx10[5];
	volatile u32	pointr, pointg, pointb, pointa;
	volatile u32	alinex, aliney, alinez;
	u32		xxx11[1];
	volatile u32	rlinex, rliney, rlinez;
	u32		xxx12[5];
	volatile u32	liner, lineg, lineb, linea;
	volatile u32	atrix, atriy, atriz;
	u32		xxx13[1];
	volatile u32	rtrix, rtriy, rtriz;
	u32		xxx14[5];
	volatile u32	trir, trig, trib, tria;
	volatile u32	aquadx, aquady, aquadz;
	u32		xxx15[1];
	volatile u32	rquadx, rquady, rquadz;
	u32		xxx16[5];
	volatile u32	quadr, quadg, quadb, quada;
	volatile u32	arectx, arecty, arectz;
	u32		xxx17[1];
	volatile u32	rrectx, rrecty, rrectz;
	u32		xxx18[5];
	volatile u32	rectr, rectg, rectb, recta;
};

struct bt_regs {
	volatile u32 addr;
	volatile u32 color_map;
	volatile u32 control;
	volatile u32 cursor;
};

struct cg6_par {
	spinlock_t		lock;
	struct bt_regs		__iomem *bt;
	struct cg6_fbc		__iomem *fbc;
	struct cg6_thc		__iomem *thc;
	struct cg6_tec		__iomem *tec;
	volatile u32		__iomem *fhc;

	u32			flags;
#define CG6_FLAG_BLANKED	0x00000001

	unsigned long		physbase;
	unsigned long		fbsize;

	struct sbus_dev		*sdev;
	struct list_head	list;
};

static int cg6_sync(struct fb_info *info)
{
	struct cg6_par *par = (struct cg6_par *) info->par;
	struct cg6_fbc __iomem *fbc = par->fbc;
	int limit = 10000;

	do {
		if (!(sbus_readl(&fbc->s) & 0x10000000))
			break;
		udelay(10);
	} while (--limit > 0);

	return 0;
}

/**
 *      cg6_fillrect - REQUIRED function. Can use generic routines if 
 *                     non acclerated hardware and packed pixel based.
 *                     Draws a rectangle on the screen.               
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *      @rect: structure defining the rectagle and operation.
 */
static void cg6_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct cg6_par *par = (struct cg6_par *) info->par;
	struct cg6_fbc __iomem *fbc = par->fbc;
	unsigned long flags;
	s32 val;

	/* XXX doesn't handle ROP_XOR */

	spin_lock_irqsave(&par->lock, flags);
	cg6_sync(info);
	sbus_writel(rect->color, &fbc->fg);
	sbus_writel(~(u32)0, &fbc->pixelm);
	sbus_writel(0xea80ff00, &fbc->alu);
	sbus_writel(0, &fbc->s);
	sbus_writel(0, &fbc->clip);
	sbus_writel(~(u32)0, &fbc->pm);
	sbus_writel(rect->dy, &fbc->arecty);
	sbus_writel(rect->dx, &fbc->arectx);
	sbus_writel(rect->dy + rect->height, &fbc->arecty);
	sbus_writel(rect->dx + rect->width, &fbc->arectx);
	do {
		val = sbus_readl(&fbc->draw);
	} while (val < 0 && (val & 0x20000000));
	spin_unlock_irqrestore(&par->lock, flags);
}

/**
 *      cg6_imageblit - REQUIRED function. Can use generic routines if
 *                      non acclerated hardware and packed pixel based.
 *                      Copies a image from system memory to the screen. 
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *      @image: structure defining the image.
 */
static void cg6_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct cg6_par *par = (struct cg6_par *) info->par;
	struct cg6_fbc __iomem *fbc = par->fbc;
	const u8 *data = image->data;
	unsigned long flags;
	u32 x, y;
	int i, width;

	if (image->depth > 1) {
		cfb_imageblit(info, image);
		return;
	}

	spin_lock_irqsave(&par->lock, flags);

	cg6_sync(info);

	sbus_writel(image->fg_color, &fbc->fg);
	sbus_writel(image->bg_color, &fbc->bg);
	sbus_writel(0x140000, &fbc->mode);
	sbus_writel(0xe880fc30, &fbc->alu);
	sbus_writel(~(u32)0, &fbc->pixelm);
	sbus_writel(0, &fbc->s);
	sbus_writel(0, &fbc->clip);
	sbus_writel(0xff, &fbc->pm);
	sbus_writel(32, &fbc->incx);
	sbus_writel(0, &fbc->incy);

	x = image->dx;
	y = image->dy;
	for (i = 0; i < image->height; i++) {
		width = image->width;

		while (width >= 32) {
			u32 val;

			sbus_writel(y, &fbc->y0);
			sbus_writel(x, &fbc->x0);
			sbus_writel(x + 32 - 1, &fbc->x1);
			
			val = ((u32)data[0] << 24) |
			      ((u32)data[1] << 16) |
			      ((u32)data[2] <<  8) |
			      ((u32)data[3] <<  0);
			sbus_writel(val, &fbc->font);

			data += 4;
			x += 32;
			width -= 32;
		}
		if (width) {
			u32 val;

			sbus_writel(y, &fbc->y0);
			sbus_writel(x, &fbc->x0);
			sbus_writel(x + width - 1, &fbc->x1);
			if (width <= 8) {
				val = (u32) data[0] << 24;
				data += 1;
			} else if (width <= 16) {
				val = ((u32) data[0] << 24) |
				      ((u32) data[1] << 16);
				data += 2;
			} else {
				val = ((u32) data[0] << 24) |
				      ((u32) data[1] << 16) |
				      ((u32) data[2] <<  8);
				data += 3;
			}
			sbus_writel(val, &fbc->font);
		}

		y += 1;
		x = image->dx;
	}

	spin_unlock_irqrestore(&par->lock, flags);
}

/**
 *      cg6_setcolreg - Optional function. Sets a color register.
 *      @regno: boolean, 0 copy local, 1 get_user() function
 *      @red: frame buffer colormap structure
 *      @green: The green value which can be up to 16 bits wide
 *      @blue:  The blue value which can be up to 16 bits wide.
 *      @transp: If supported the alpha value which can be up to 16 bits wide.
 *      @info: frame buffer info structure
 */
static int cg6_setcolreg(unsigned regno,
			 unsigned red, unsigned green, unsigned blue,
			 unsigned transp, struct fb_info *info)
{
	struct cg6_par *par = (struct cg6_par *) info->par;
	struct bt_regs __iomem *bt = par->bt;
	unsigned long flags;

	if (regno >= 256)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	spin_lock_irqsave(&par->lock, flags);

	sbus_writel((u32)regno << 24, &bt->addr);
	sbus_writel((u32)red << 24, &bt->color_map);
	sbus_writel((u32)green << 24, &bt->color_map);
	sbus_writel((u32)blue << 24, &bt->color_map);

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

/**
 *      cg6_blank - Optional function.  Blanks the display.
 *      @blank_mode: the blank mode we want.
 *      @info: frame buffer structure that represents a single frame buffer
 */
static int
cg6_blank(int blank, struct fb_info *info)
{
	struct cg6_par *par = (struct cg6_par *) info->par;
	struct cg6_thc __iomem *thc = par->thc;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&par->lock, flags);

	switch (blank) {
	case FB_BLANK_UNBLANK: /* Unblanking */
		val = sbus_readl(&thc->thc_misc);
		val |= CG6_THC_MISC_VIDEO;
		sbus_writel(val, &thc->thc_misc);
		par->flags &= ~CG6_FLAG_BLANKED;
		break;

	case FB_BLANK_NORMAL: /* Normal blanking */
	case FB_BLANK_VSYNC_SUSPEND: /* VESA blank (vsync off) */
	case FB_BLANK_HSYNC_SUSPEND: /* VESA blank (hsync off) */
	case FB_BLANK_POWERDOWN: /* Poweroff */
		val = sbus_readl(&thc->thc_misc);
		val &= ~CG6_THC_MISC_VIDEO;
		sbus_writel(val, &thc->thc_misc);
		par->flags |= CG6_FLAG_BLANKED;
		break;
	}

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

static struct sbus_mmap_map cg6_mmap_map[] = {
	{
		.voff	= CG6_FBC,
		.poff	= CG6_FBC_OFFSET,
		.size	= PAGE_SIZE
	},
	{
		.voff	= CG6_TEC,
		.poff	= CG6_TEC_OFFSET,
		.size	= PAGE_SIZE
	},
	{
		.voff	= CG6_BTREGS,
		.poff	= CG6_BROOKTREE_OFFSET,
		.size	= PAGE_SIZE
	},
	{
		.voff	= CG6_FHC,
		.poff	= CG6_FHC_OFFSET,
		.size	= PAGE_SIZE
	},
	{
		.voff	= CG6_THC,
		.poff	= CG6_THC_OFFSET,
		.size	= PAGE_SIZE
	},
	{
		.voff	= CG6_ROM,
		.poff	= CG6_ROM_OFFSET,
		.size	= 0x10000
	},
	{
		.voff	= CG6_RAM,
		.poff	= CG6_RAM_OFFSET,
		.size	= SBUS_MMAP_FBSIZE(1)
	},
	{
		.voff	= CG6_DHC,
		.poff	= CG6_DHC_OFFSET,
		.size	= 0x40000
	},
	{ .size	= 0 }
};

static int cg6_mmap(struct fb_info *info, struct file *file, struct vm_area_struct *vma)
{
	struct cg6_par *par = (struct cg6_par *)info->par;

	return sbusfb_mmap_helper(cg6_mmap_map,
				  par->physbase, par->fbsize,
				  par->sdev->reg_addrs[0].which_io,
				  vma);
}

static int cg6_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg, struct fb_info *info)
{
	struct cg6_par *par = (struct cg6_par *) info->par;

	return sbusfb_ioctl_helper(cmd, arg, info,
				   FBTYPE_SUNFAST_COLOR, 8, par->fbsize);
}

/*
 *  Initialisation
 */

static void
cg6_init_fix(struct fb_info *info, int linebytes)
{
	struct cg6_par *par = (struct cg6_par *)info->par;
	const char *cg6_cpu_name, *cg6_card_name;
	u32 conf;

	conf = sbus_readl(par->fhc);
	switch(conf & CG6_FHC_CPU_MASK) {
	case CG6_FHC_CPU_SPARC:
		cg6_cpu_name = "sparc";
		break;
	case CG6_FHC_CPU_68020:
		cg6_cpu_name = "68020";
		break;
	default:
		cg6_cpu_name = "i386";
		break;
	};
	if (((conf >> CG6_FHC_REV_SHIFT) & CG6_FHC_REV_MASK) >= 11) {
		if (par->fbsize <= 0x100000) {
			cg6_card_name = "TGX";
		} else {
			cg6_card_name = "TGX+";
		}
	} else {
		if (par->fbsize <= 0x100000) {
			cg6_card_name = "GX";
		} else {
			cg6_card_name = "GX+";
		}
	}

	sprintf(info->fix.id, "%s %s", cg6_card_name, cg6_cpu_name);
	info->fix.id[sizeof(info->fix.id)-1] = 0;

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;

	info->fix.line_length = linebytes;

	info->fix.accel = FB_ACCEL_SUN_CGSIX;
}

/* Initialize Brooktree DAC */
static void cg6_bt_init(struct cg6_par *par)
{
	struct bt_regs __iomem *bt = par->bt;

	sbus_writel(0x04 << 24, &bt->addr);         /* color planes */
	sbus_writel(0xff << 24, &bt->control);
	sbus_writel(0x05 << 24, &bt->addr);
	sbus_writel(0x00 << 24, &bt->control);
	sbus_writel(0x06 << 24, &bt->addr);         /* overlay plane */
	sbus_writel(0x73 << 24, &bt->control);
	sbus_writel(0x07 << 24, &bt->addr);
	sbus_writel(0x00 << 24, &bt->control);
}

static void cg6_chip_init(struct fb_info *info)
{
	struct cg6_par *par = (struct cg6_par *) info->par;
	struct cg6_tec __iomem *tec = par->tec;
	struct cg6_fbc __iomem *fbc = par->fbc;
	u32 rev, conf, mode, tmp;
	int i;
	
	/* Turn off stuff in the Transform Engine. */
	sbus_writel(0, &tec->tec_matrix);
	sbus_writel(0, &tec->tec_clip);
	sbus_writel(0, &tec->tec_vdc);

	/* Take care of bugs in old revisions. */
	rev = (sbus_readl(par->fhc) >> CG6_FHC_REV_SHIFT) & CG6_FHC_REV_MASK;
	if (rev < 5) {
		conf = (sbus_readl(par->fhc) & CG6_FHC_RES_MASK) |
			CG6_FHC_CPU_68020 | CG6_FHC_TEST |
			(11 << CG6_FHC_TEST_X_SHIFT) |
			(11 << CG6_FHC_TEST_Y_SHIFT);
		if (rev < 2)
			conf |= CG6_FHC_DST_DISABLE;
		sbus_writel(conf, par->fhc);
	}

	/* Set things in the FBC. Bad things appear to happen if we do
	 * back to back store/loads on the mode register, so copy it
	 * out instead. */
	mode = sbus_readl(&fbc->mode);
	do {
		i = sbus_readl(&fbc->s);
	} while (i & 0x10000000);
	mode &= ~(CG6_FBC_BLIT_MASK | CG6_FBC_MODE_MASK |
		       CG6_FBC_DRAW_MASK | CG6_FBC_BWRITE0_MASK |
		       CG6_FBC_BWRITE1_MASK | CG6_FBC_BREAD_MASK |
		       CG6_FBC_BDISP_MASK);
	mode |= (CG6_FBC_BLIT_SRC | CG6_FBC_MODE_COLOR8 |
		      CG6_FBC_DRAW_RENDER | CG6_FBC_BWRITE0_ENABLE |
		      CG6_FBC_BWRITE1_DISABLE | CG6_FBC_BREAD_0 |
		      CG6_FBC_BDISP_0);
	sbus_writel(mode, &fbc->mode);

	sbus_writel(0, &fbc->clip);
	sbus_writel(0, &fbc->offx);
	sbus_writel(0, &fbc->offy);
	sbus_writel(0, &fbc->clipminx);
	sbus_writel(0, &fbc->clipminy);
	sbus_writel(info->var.xres - 1, &fbc->clipmaxx);
	sbus_writel(info->var.yres - 1, &fbc->clipmaxy);
}

struct all_info {
	struct fb_info info;
	struct cg6_par par;
	struct list_head list;
};
static LIST_HEAD(cg6_list);

static void cg6_init_one(struct sbus_dev *sdev)
{
	struct all_info *all;
	int linebytes;

	all = kmalloc(sizeof(*all), GFP_KERNEL);
	if (!all) {
		printk(KERN_ERR "cg6: Cannot allocate memory.\n");
		return;
	}
	memset(all, 0, sizeof(*all));

	INIT_LIST_HEAD(&all->list);

	spin_lock_init(&all->par.lock);
	all->par.sdev = sdev;

	all->par.physbase = sdev->reg_addrs[0].phys_addr;

	sbusfb_fill_var(&all->info.var, sdev->prom_node, 8);
	all->info.var.red.length = 8;
	all->info.var.green.length = 8;
	all->info.var.blue.length = 8;

	linebytes = prom_getintdefault(sdev->prom_node, "linebytes",
				       all->info.var.xres);
	all->par.fbsize = PAGE_ALIGN(linebytes * all->info.var.yres);
	if (prom_getbool(sdev->prom_node, "dblbuf"))
		all->par.fbsize *= 4;

	all->par.fbc = sbus_ioremap(&sdev->resource[0], CG6_FBC_OFFSET,
			     4096, "cgsix fbc");
	all->par.tec = sbus_ioremap(&sdev->resource[0], CG6_TEC_OFFSET,
			     sizeof(struct cg6_tec), "cgsix tec");
	all->par.thc = sbus_ioremap(&sdev->resource[0], CG6_THC_OFFSET,
			     sizeof(struct cg6_thc), "cgsix thc");
	all->par.bt = sbus_ioremap(&sdev->resource[0], CG6_BROOKTREE_OFFSET,
			     sizeof(struct bt_regs), "cgsix dac");
	all->par.fhc = sbus_ioremap(&sdev->resource[0], CG6_FHC_OFFSET,
			     sizeof(u32), "cgsix fhc");

	all->info.flags = FBINFO_DEFAULT | FBINFO_HWACCEL_IMAGEBLIT |
                          FBINFO_HWACCEL_COPYAREA | FBINFO_HWACCEL_FILLRECT;
	all->info.fbops = &cg6_ops;
#ifdef CONFIG_SPARC32
	all->info.screen_base = (char __iomem *)
		prom_getintdefault(sdev->prom_node, "address", 0);
#endif
	if (!all->info.screen_base)
		all->info.screen_base = 
			sbus_ioremap(&sdev->resource[0], CG6_RAM_OFFSET,
				     all->par.fbsize, "cgsix ram");
	all->info.par = &all->par;

	all->info.var.accel_flags = FB_ACCELF_TEXT;

	cg6_bt_init(&all->par);
	cg6_chip_init(&all->info);
	cg6_blank(0, &all->info);

	if (fb_alloc_cmap(&all->info.cmap, 256, 0)) {
		printk(KERN_ERR "cg6: Could not allocate color map.\n");
		kfree(all);
		return;
	}

	fb_set_cmap(&all->info.cmap, &all->info);
	cg6_init_fix(&all->info, linebytes);

	if (register_framebuffer(&all->info) < 0) {
		printk(KERN_ERR "cg6: Could not register framebuffer.\n");
		fb_dealloc_cmap(&all->info.cmap);
		kfree(all);
		return;
	}

	list_add(&all->list, &cg6_list);

	printk("cg6: CGsix [%s] at %lx:%lx\n",
	       all->info.fix.id,
	       (long) sdev->reg_addrs[0].which_io,
	       (long) sdev->reg_addrs[0].phys_addr);
}

int __init cg6_init(void)
{
	struct sbus_bus *sbus;
	struct sbus_dev *sdev;

	if (fb_get_options("cg6fb", NULL))
		return -ENODEV;

	for_all_sbusdev(sdev, sbus) {
		if (!strcmp(sdev->prom_name, "cgsix") ||
		    !strcmp(sdev->prom_name, "cgthree+"))
			cg6_init_one(sdev);
	}

	return 0;
}

void __exit cg6_exit(void)
{
	struct list_head *pos, *tmp;

	list_for_each_safe(pos, tmp, &cg6_list) {
		struct all_info *all = list_entry(pos, typeof(*all), list);

		unregister_framebuffer(&all->info);
		fb_dealloc_cmap(&all->info.cmap);
		kfree(all);
	}
}

int __init
cg6_setup(char *arg)
{
	/* No cmdline options yet... */
	return 0;
}

module_init(cg6_init);

#ifdef MODULE
module_exit(cg6_exit);
#endif

MODULE_DESCRIPTION("framebuffer driver for CGsix chipsets");
MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_LICENSE("GPL");
