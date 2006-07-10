/*
 *
 * tdfxfb.c
 *
 * Author: Hannu Mallat <hmallat@cc.hut.fi>
 *
 * Copyright © 1999 Hannu Mallat
 * All rights reserved
 *
 * Created      : Thu Sep 23 18:17:43 1999, hmallat
 * Last modified: Tue Nov  2 21:19:47 1999, hmallat
 *
 * Lots of the information here comes from the Daryll Strauss' Banshee 
 * patches to the XF86 server, and the rest comes from the 3dfx
 * Banshee specification. I'm very much indebted to Daryll for his
 * work on the X server.
 *
 * Voodoo3 support was contributed Harold Oga. Lots of additions
 * (proper acceleration, 24 bpp, hardware cursor) and bug fixes by Attila
 * Kesmarki. Thanks guys!
 *
 * Voodoo1 and Voodoo2 support aren't relevant to this driver as they
 * behave very differently from the Voodoo3/4/5. For anyone wanting to
 * use frame buffer on the Voodoo1/2, see the sstfb driver (which is
 * located at http://www.sourceforge.net/projects/sstfb).
 * 
 * While I _am_ grateful to 3Dfx for releasing the specs for Banshee,
 * I do wish the next version is a bit more complete. Without the XF86
 * patches I couldn't have gotten even this far... for instance, the
 * extensions to the VGA register set go completely unmentioned in the
 * spec! Also, lots of references are made to the 'SST core', but no
 * spec is publicly available, AFAIK.
 *
 * The structure of this driver comes pretty much from the Permedia
 * driver by Ilario Nardinocchi, which in turn is based on skeletonfb.
 * 
 * TODO:
 * - support for 16/32 bpp needs fixing (funky bootup penguin)
 * - multihead support (basically need to support an array of fb_infos)
 * - support other architectures (PPC, Alpha); does the fact that the VGA
 *   core can be accessed only thru I/O (not memory mapped) complicate
 *   things?
 *
 * Version history:
 *
 * 0.1.4 (released 2002-05-28) ported over to new fbdev api by James Simmons
 *
 * 0.1.3 (released 1999-11-02) added Attila's panning support, code
 *			       reorg, hwcursor address page size alignment
 *                             (for mmaping both frame buffer and regs),
 *                             and my changes to get rid of hardcoded
 *                             VGA i/o register locations (uses PCI
 *                             configuration info now)
 * 0.1.2 (released 1999-10-19) added Attila Kesmarki's bug fixes and
 *                             improvements
 * 0.1.1 (released 1999-10-07) added Voodoo3 support by Harold Oga.
 * 0.1.0 (released 1999-10-06) initial version
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/nvram.h>
#include <asm/io.h>
#include <linux/timer.h>
#include <linux/spinlock.h>

#include <video/tdfx.h>

#undef TDFXFB_DEBUG 
#ifdef TDFXFB_DEBUG
#define DPRINTK(a,b...) printk(KERN_DEBUG "fb: %s: " a, __FUNCTION__ , ## b)
#else
#define DPRINTK(a,b...)
#endif 

#define BANSHEE_MAX_PIXCLOCK 270000
#define VOODOO3_MAX_PIXCLOCK 300000
#define VOODOO5_MAX_PIXCLOCK 350000

static struct fb_fix_screeninfo tdfx_fix __devinitdata = {
	.id =		"3Dfx",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_PSEUDOCOLOR, 
	.ypanstep =	1,
	.ywrapstep =	1, 
	.accel =	FB_ACCEL_3DFX_BANSHEE
};

static struct fb_var_screeninfo tdfx_var __devinitdata = {
	/* "640x480, 8 bpp @ 60 Hz */
	.xres =		640,
	.yres =		480,
	.xres_virtual =	640,
	.yres_virtual =	1024,
	.bits_per_pixel =8,
	.red =		{0, 8, 0},
	.blue =		{0, 8, 0},
	.green =	{0, 8, 0},
	.activate =	FB_ACTIVATE_NOW,
	.height =	-1,
	.width =	-1,
	.accel_flags =	FB_ACCELF_TEXT,
	.pixclock =	39722,
	.left_margin =	40,
	.right_margin =	24,
	.upper_margin =	32,
	.lower_margin =	11,
	.hsync_len =	96,
	.vsync_len =	2,
	.vmode =	FB_VMODE_NONINTERLACED
};

/*
 * PCI driver prototypes
 */
static int __devinit tdfxfb_probe(struct pci_dev *pdev,
				  const struct pci_device_id *id);
static void __devexit tdfxfb_remove(struct pci_dev *pdev);

static struct pci_device_id tdfxfb_id_table[] = {
	{ PCI_VENDOR_ID_3DFX, PCI_DEVICE_ID_3DFX_BANSHEE,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_BASE_CLASS_DISPLAY << 16,
	  0xff0000, 0 },
	{ PCI_VENDOR_ID_3DFX, PCI_DEVICE_ID_3DFX_VOODOO3,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_BASE_CLASS_DISPLAY << 16,
	  0xff0000, 0 },
	{ PCI_VENDOR_ID_3DFX, PCI_DEVICE_ID_3DFX_VOODOO5,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_BASE_CLASS_DISPLAY << 16,
	  0xff0000, 0 },
	{ 0, }
};

static struct pci_driver tdfxfb_driver = {
	.name		= "tdfxfb",
	.id_table 	= tdfxfb_id_table,
	.probe 		= tdfxfb_probe,
	.remove 	= __devexit_p(tdfxfb_remove),
};

MODULE_DEVICE_TABLE(pci, tdfxfb_id_table);

/*
 *  Frame buffer device API
 */
static int tdfxfb_check_var(struct fb_var_screeninfo *var, struct fb_info *fb); 
static int tdfxfb_set_par(struct fb_info *info); 
static int tdfxfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue, 
			    u_int transp, struct fb_info *info); 
static int tdfxfb_blank(int blank, struct fb_info *info); 
static int tdfxfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info);
static int banshee_wait_idle(struct fb_info *info);
#ifdef CONFIG_FB_3DFX_ACCEL
static void tdfxfb_fillrect(struct fb_info *info, const struct fb_fillrect *rect);
static void tdfxfb_copyarea(struct fb_info *info, const struct fb_copyarea *area);  
static void tdfxfb_imageblit(struct fb_info *info, const struct fb_image *image); 
#endif /* CONFIG_FB_3DFX_ACCEL */

static struct fb_ops tdfxfb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= tdfxfb_check_var,
	.fb_set_par	= tdfxfb_set_par,
	.fb_setcolreg	= tdfxfb_setcolreg,
	.fb_blank	= tdfxfb_blank,
	.fb_pan_display	= tdfxfb_pan_display,
	.fb_sync	= banshee_wait_idle,
#ifdef CONFIG_FB_3DFX_ACCEL
	.fb_fillrect	= tdfxfb_fillrect,
	.fb_copyarea	= tdfxfb_copyarea,
	.fb_imageblit	= tdfxfb_imageblit,
#else
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
#endif
};

/*
 * do_xxx: Hardware-specific functions
 */
static u32 do_calc_pll(int freq, int *freq_out);
static void  do_write_regs(struct fb_info *info, struct banshee_reg *reg);
static unsigned long do_lfb_size(struct tdfx_par *par, unsigned short);

/*
 * Driver data 
 */
static int  nopan   = 0;
static int  nowrap  = 1;      // not implemented (yet)
static char *mode_option __devinitdata = NULL;

/* ------------------------------------------------------------------------- 
 *                      Hardware-specific funcions
 * ------------------------------------------------------------------------- */

#ifdef VGA_REG_IO 
static inline  u8 vga_inb(struct tdfx_par *par, u32 reg) { return inb(reg); }

static inline void vga_outb(struct tdfx_par *par, u32 reg,  u8 val) { outb(val, reg); }
#else
static inline  u8 vga_inb(struct tdfx_par *par, u32 reg) { 
	return inb(par->iobase + reg - 0x300); 
}
static inline void vga_outb(struct tdfx_par *par, u32 reg,  u8 val) { 
	outb(val, par->iobase + reg - 0x300); 
}
#endif

static inline void gra_outb(struct tdfx_par *par, u32 idx, u8 val) {
	vga_outb(par, GRA_I, idx); vga_outb(par, GRA_D, val);
}

static inline void seq_outb(struct tdfx_par *par, u32 idx, u8 val) {
	vga_outb(par, SEQ_I, idx); vga_outb(par, SEQ_D, val);
}

static inline u8 seq_inb(struct tdfx_par *par, u32 idx) {
	vga_outb(par, SEQ_I, idx); return vga_inb(par, SEQ_D);
}

static inline void crt_outb(struct tdfx_par *par, u32 idx, u8 val) {
	vga_outb(par, CRT_I, idx); vga_outb(par, CRT_D, val);
}

static inline u8 crt_inb(struct tdfx_par *par, u32 idx) {
	vga_outb(par, CRT_I, idx); return vga_inb(par, CRT_D);
}

static inline void att_outb(struct tdfx_par *par, u32 idx, u8 val) 
{
	unsigned char tmp;
	
	tmp = vga_inb(par, IS1_R);
	vga_outb(par, ATT_IW, idx);
	vga_outb(par, ATT_IW, val);
}

static inline void vga_disable_video(struct tdfx_par *par)
{
	unsigned char s;

	s = seq_inb(par, 0x01) | 0x20;
	seq_outb(par, 0x00, 0x01);
	seq_outb(par, 0x01, s);
	seq_outb(par, 0x00, 0x03);
}

static inline void vga_enable_video(struct tdfx_par *par)
{
	unsigned char s;

	s = seq_inb(par, 0x01) & 0xdf;
	seq_outb(par, 0x00, 0x01);
	seq_outb(par, 0x01, s);
	seq_outb(par, 0x00, 0x03);
}

static inline void vga_enable_palette(struct tdfx_par *par)
{
	vga_inb(par, IS1_R);
	vga_outb(par, ATT_IW, 0x20);
}

static inline u32 tdfx_inl(struct tdfx_par *par, unsigned int reg) 
{
	return readl(par->regbase_virt + reg);
}

static inline void tdfx_outl(struct tdfx_par *par, unsigned int reg, u32 val)
{
	writel(val, par->regbase_virt + reg);
}

static inline void banshee_make_room(struct tdfx_par *par, int size)
{
	/* Note: The Voodoo3's onboard FIFO has 32 slots. This loop
	 * won't quit if you ask for more. */
	while((tdfx_inl(par, STATUS) & 0x1f) < size-1);
}
 
static int banshee_wait_idle(struct fb_info *info)
{
	struct tdfx_par *par = info->par;
	int i = 0;

	banshee_make_room(par, 1);
	tdfx_outl(par, COMMAND_3D, COMMAND_3D_NOP);

	while(1) {
		i = (tdfx_inl(par, STATUS) & STATUS_BUSY) ? 0 : i + 1;
		if(i == 3) break;
	}
	return 0;
}

/*
 * Set the color of a palette entry in 8bpp mode 
 */
static inline void do_setpalentry(struct tdfx_par *par, unsigned regno, u32 c)
{  
	banshee_make_room(par, 2);
	tdfx_outl(par, DACADDR, regno);
	tdfx_outl(par, DACDATA, c);
}

static u32 do_calc_pll(int freq, int* freq_out) 
{
	int m, n, k, best_m, best_n, best_k, best_error;
	int fref = 14318;
  
	best_error = freq;
	best_n = best_m = best_k = 0;

	for (k = 3; k >= 0; k--) {
		for (m = 63; m >= 0; m--) {
			/*
			 * Estimate value of n that produces target frequency
			 * with current m and k
			 */
			int n_estimated = (freq * (m + 2) * (1 << k) / fref) - 2;

			/* Search neighborhood of estimated n */
			for (n = max(0, n_estimated - 1);
					n <= min(255, n_estimated + 1); n++) {
				/*
				 * Calculate PLL freqency with current m, k and
				 * estimated n
				 */
				int f = fref * (n + 2) / (m + 2) / (1 << k);
				int error = abs (f - freq);

				/*
				 *  If this is the closest we've come to the
				 * target frequency then remember n, m and k
				 */
				if (error  < best_error) {
					best_error = error;
					best_n     = n;
					best_m     = m;
					best_k     = k;
				}
			}
		}
	}

	n = best_n;
	m = best_m;
	k = best_k;
	*freq_out = fref*(n + 2)/(m + 2)/(1 << k);

	return (n << 8) | (m << 2) | k;
}

static void do_write_regs(struct fb_info *info, struct banshee_reg* reg) 
{
	struct tdfx_par *par = info->par;
	int i;

	banshee_wait_idle(info);

	tdfx_outl(par, MISCINIT1, tdfx_inl(par, MISCINIT1) | 0x01);

	crt_outb(par, 0x11, crt_inb(par, 0x11) & 0x7f); /* CRT unprotect */

	banshee_make_room(par, 3);
	tdfx_outl(par, VGAINIT1,	reg->vgainit1 &  0x001FFFFF);
	tdfx_outl(par, VIDPROCCFG,	reg->vidcfg   & ~0x00000001);
#if 0
	tdfx_outl(par, PLLCTRL1, reg->mempll);
	tdfx_outl(par, PLLCTRL2, reg->gfxpll);
#endif
	tdfx_outl(par, PLLCTRL0,	reg->vidpll);

	vga_outb(par, MISC_W, reg->misc[0x00] | 0x01);

	for (i = 0; i < 5; i++)
		seq_outb(par, i, reg->seq[i]);

	for (i = 0; i < 25; i++)
		crt_outb(par, i, reg->crt[i]);

	for (i = 0; i < 9; i++)
		gra_outb(par, i, reg->gra[i]);

	for (i = 0; i < 21; i++)
		att_outb(par, i, reg->att[i]);

	crt_outb(par, 0x1a, reg->ext[0]);
	crt_outb(par, 0x1b, reg->ext[1]);

	vga_enable_palette(par);
	vga_enable_video(par);

	banshee_make_room(par, 11);
	tdfx_outl(par, 	VGAINIT0,      reg->vgainit0);
	tdfx_outl(par,	DACMODE,       reg->dacmode);
	tdfx_outl(par,	VIDDESKSTRIDE, reg->stride);
	tdfx_outl(par,	HWCURPATADDR,  0);
   
	tdfx_outl(par,	VIDSCREENSIZE,reg->screensize);
	tdfx_outl(par,	VIDDESKSTART,	reg->startaddr);
	tdfx_outl(par,	VIDPROCCFG,	reg->vidcfg);
	tdfx_outl(par,	VGAINIT1,	reg->vgainit1);  
	tdfx_outl(par,	MISCINIT0,	reg->miscinit0);	

	banshee_make_room(par,	8);
	tdfx_outl(par,	SRCBASE,         reg->srcbase);
	tdfx_outl(par,	DSTBASE,         reg->dstbase);
	tdfx_outl(par,	COMMANDEXTRA_2D, 0);
	tdfx_outl(par,	CLIP0MIN,        0);
	tdfx_outl(par,	CLIP0MAX,        0x0fff0fff);
	tdfx_outl(par,	CLIP1MIN,        0);
	tdfx_outl(par,	CLIP1MAX,        0x0fff0fff);
	tdfx_outl(par,	SRCXY,	   0);

	banshee_wait_idle(info);
}

static unsigned long do_lfb_size(struct tdfx_par *par, unsigned short dev_id) 
{
	u32 draminit0;
	u32 draminit1;
	u32 miscinit1;

	int num_chips;
	int chip_size; /* in MB */
	u32 lfbsize;
	int has_sgram;

	draminit0 = tdfx_inl(par, DRAMINIT0);  
	draminit1 = tdfx_inl(par, DRAMINIT1);

	num_chips = (draminit0 & DRAMINIT0_SGRAM_NUM) ? 8 : 4;
 
	if (dev_id < PCI_DEVICE_ID_3DFX_VOODOO5) {
		/* Banshee/Voodoo3 */
		has_sgram = draminit1 & DRAMINIT1_MEM_SDRAM;
		chip_size = has_sgram ? ((draminit0 & DRAMINIT0_SGRAM_TYPE) ? 2 : 1)
				      : 2;
	} else {
		/* Voodoo4/5 */
		has_sgram = 0;
		chip_size = 1 << ((draminit0 & DRAMINIT0_SGRAM_TYPE_MASK) >> DRAMINIT0_SGRAM_TYPE_SHIFT);
	}
	lfbsize = num_chips * chip_size * 1024 * 1024;

	/* disable block writes for SDRAM */
	miscinit1 = tdfx_inl(par, MISCINIT1);
	miscinit1 |= has_sgram ? 0 : MISCINIT1_2DBLOCK_DIS;
	miscinit1 |= MISCINIT1_CLUT_INV;

	banshee_make_room(par, 1); 
	tdfx_outl(par, MISCINIT1, miscinit1);
	return lfbsize;
}

/* ------------------------------------------------------------------------- */

static int tdfxfb_check_var(struct fb_var_screeninfo *var,struct fb_info *info) 
{
	struct tdfx_par *par = info->par;
	u32 lpitch;

	if (var->bits_per_pixel != 8  && var->bits_per_pixel != 16 &&
	    var->bits_per_pixel != 24 && var->bits_per_pixel != 32) {
		DPRINTK("depth not supported: %u\n", var->bits_per_pixel);
		return -EINVAL;
	}

	if (var->xres != var->xres_virtual)
		var->xres_virtual = var->xres;

	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;

	if (var->xoffset) {
		DPRINTK("xoffset not supported\n");
		return -EINVAL;
	}

	/* Banshee doesn't support interlace, but Voodoo4/5 and probably Voodoo3 do. */
	/* no direct information about device id now? use max_pixclock for this... */
	if (((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) &&
			(par->max_pixclock < VOODOO3_MAX_PIXCLOCK)) {
		DPRINTK("interlace not supported\n");
		return -EINVAL;
	}

	var->xres = (var->xres + 15) & ~15; /* could sometimes be 8 */
	lpitch = var->xres * ((var->bits_per_pixel + 7)>>3);
  
	if (var->xres < 320 || var->xres > 2048) {
		DPRINTK("width not supported: %u\n", var->xres);
		return -EINVAL;
	}
  
	if (var->yres < 200 || var->yres > 2048) {
		DPRINTK("height not supported: %u\n", var->yres);
		return -EINVAL;
	}
  
	if (lpitch * var->yres_virtual > info->fix.smem_len) {
		var->yres_virtual = info->fix.smem_len/lpitch;
		if (var->yres_virtual < var->yres) {
			DPRINTK("no memory for screen (%ux%ux%u)\n",
			var->xres, var->yres_virtual, var->bits_per_pixel);
			return -EINVAL;
		}
	}
  
	if (PICOS2KHZ(var->pixclock) > par->max_pixclock) {
		DPRINTK("pixclock too high (%ldKHz)\n",PICOS2KHZ(var->pixclock));
		return -EINVAL;
	}

	switch(var->bits_per_pixel) {
		case 8:
			var->red.length = var->green.length = var->blue.length = 8;
			break;
		case 16:
			var->red.offset   = 11;
			var->red.length   = 5;
			var->green.offset = 5;
			var->green.length = 6;
			var->blue.offset  = 0;
			var->blue.length  = 5;
			break;
		case 24:
			var->red.offset=16;
			var->green.offset=8;
			var->blue.offset=0;
			var->red.length = var->green.length = var->blue.length = 8;
		case 32:
			var->red.offset   = 16;
			var->green.offset = 8;
			var->blue.offset  = 0;
			var->red.length = var->green.length = var->blue.length = 8;
			break;
	}
	var->height = var->width = -1;
  
	var->accel_flags = FB_ACCELF_TEXT;
	
	DPRINTK("Checking graphics mode at %dx%d depth %d\n",  var->xres, var->yres, var->bits_per_pixel);
	return 0;
}

static int tdfxfb_set_par(struct fb_info *info)
{
	struct tdfx_par *par = info->par;
	u32 hdispend, hsyncsta, hsyncend, htotal;
	u32 hd, hs, he, ht, hbs, hbe;
	u32 vd, vs, ve, vt, vbs, vbe;
	struct banshee_reg reg;
	int fout, freq;
	u32 wd, cpp;
  
	par->baseline  = 0;
 
	memset(&reg, 0, sizeof(reg));
	cpp = (info->var.bits_per_pixel + 7)/8;
 
	reg.vidcfg = VIDCFG_VIDPROC_ENABLE | VIDCFG_DESK_ENABLE | VIDCFG_CURS_X11 | ((cpp - 1) << VIDCFG_PIXFMT_SHIFT) | (cpp != 1 ? VIDCFG_CLUT_BYPASS : 0);

	/* PLL settings */
	freq = PICOS2KHZ(info->var.pixclock);

	reg.dacmode = 0;
	reg.vidcfg  &= ~VIDCFG_2X;

	hdispend = info->var.xres;
	hsyncsta = hdispend + info->var.right_margin;
	hsyncend = hsyncsta + info->var.hsync_len;
	htotal   = hsyncend + info->var.left_margin;	

	if (freq > par->max_pixclock/2) {
		freq = freq > par->max_pixclock ? par->max_pixclock : freq;
		reg.dacmode |= DACMODE_2X;
		reg.vidcfg  |= VIDCFG_2X;
		hdispend >>= 1;
		hsyncsta >>= 1;
		hsyncend >>= 1;
		htotal   >>= 1;
	}
  
	hd  = wd = (hdispend >> 3) - 1;
	hs  = (hsyncsta >> 3) - 1;
	he  = (hsyncend >> 3) - 1;
	ht  = (htotal >> 3) - 1;
	hbs = hd;
	hbe = ht;

	if ((info->var.vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
		vbs = vd = (info->var.yres << 1) - 1;
		vs  = vd + (info->var.lower_margin << 1);
		ve  = vs + (info->var.vsync_len << 1);
		vbe = vt = ve + (info->var.upper_margin << 1) - 1;
	} else {
		vbs = vd = info->var.yres - 1;
		vs  = vd + info->var.lower_margin;
		ve  = vs + info->var.vsync_len;
		vbe = vt = ve + info->var.upper_margin - 1;
	}
  
	/* this is all pretty standard VGA register stuffing */
	reg.misc[0x00] = 0x0f | 
			(info->var.xres < 400 ? 0xa0 :
			 info->var.xres < 480 ? 0x60 :
			 info->var.xres < 768 ? 0xe0 : 0x20);
     
	reg.gra[0x00] = 0x00;
	reg.gra[0x01] = 0x00;
	reg.gra[0x02] = 0x00;
	reg.gra[0x03] = 0x00;
	reg.gra[0x04] = 0x00;
	reg.gra[0x05] = 0x40;
	reg.gra[0x06] = 0x05;
	reg.gra[0x07] = 0x0f;
	reg.gra[0x08] = 0xff;

	reg.att[0x00] = 0x00;
	reg.att[0x01] = 0x01;
	reg.att[0x02] = 0x02;
	reg.att[0x03] = 0x03;
	reg.att[0x04] = 0x04;
	reg.att[0x05] = 0x05;
	reg.att[0x06] = 0x06;
	reg.att[0x07] = 0x07;
	reg.att[0x08] = 0x08;
	reg.att[0x09] = 0x09;
	reg.att[0x0a] = 0x0a;
	reg.att[0x0b] = 0x0b;
	reg.att[0x0c] = 0x0c;
	reg.att[0x0d] = 0x0d;
	reg.att[0x0e] = 0x0e;
	reg.att[0x0f] = 0x0f;
	reg.att[0x10] = 0x41;
	reg.att[0x11] = 0x00;
	reg.att[0x12] = 0x0f;
	reg.att[0x13] = 0x00;
	reg.att[0x14] = 0x00;

	reg.seq[0x00] = 0x03;
	reg.seq[0x01] = 0x01; /* fixme: clkdiv2? */
	reg.seq[0x02] = 0x0f;
	reg.seq[0x03] = 0x00;
	reg.seq[0x04] = 0x0e;

	reg.crt[0x00] = ht - 4;
	reg.crt[0x01] = hd;
	reg.crt[0x02] = hbs;
	reg.crt[0x03] = 0x80 | (hbe & 0x1f);
	reg.crt[0x04] = hs;
	reg.crt[0x05] = ((hbe & 0x20) << 2) | (he & 0x1f); 
	reg.crt[0x06] = vt;
	reg.crt[0x07] = ((vs & 0x200) >> 2) |
			((vd & 0x200) >> 3) |
			((vt & 0x200) >> 4) | 0x10 |
			((vbs & 0x100) >> 5) |
			((vs  & 0x100) >> 6) |
			((vd  & 0x100) >> 7) |
			((vt  & 0x100) >> 8);
	reg.crt[0x08] = 0x00;
	reg.crt[0x09] = 0x40 | ((vbs & 0x200) >> 4); 
	reg.crt[0x0a] = 0x00;
	reg.crt[0x0b] = 0x00;
	reg.crt[0x0c] = 0x00;
	reg.crt[0x0d] = 0x00;
	reg.crt[0x0e] = 0x00;
	reg.crt[0x0f] = 0x00;
	reg.crt[0x10] = vs;
	reg.crt[0x11] = (ve & 0x0f) | 0x20; 
	reg.crt[0x12] = vd;
	reg.crt[0x13] = wd;
	reg.crt[0x14] = 0x00;
	reg.crt[0x15] = vbs;
	reg.crt[0x16] = vbe + 1; 
	reg.crt[0x17] = 0xc3;
	reg.crt[0x18] = 0xff;
 
	/* Banshee's nonvga stuff */
	reg.ext[0x00] = (((ht  & 0x100) >> 8) | 
			((hd  & 0x100) >> 6) |
			((hbs & 0x100) >> 4) |
			((hbe &  0x40) >> 1) |
			((hs  & 0x100) >> 2) |
			((he  &  0x20) << 2)); 
	reg.ext[0x01] = (((vt  & 0x400) >> 10) |
			((vd  & 0x400) >>  8) | 
			((vbs & 0x400) >>  6) |
			((vbe & 0x400) >>  4));

	reg.vgainit0 = 	VGAINIT0_8BIT_DAC     |
			VGAINIT0_EXT_ENABLE   |
			VGAINIT0_WAKEUP_3C3   |
			VGAINIT0_ALT_READBACK |
			VGAINIT0_EXTSHIFTOUT;
	reg.vgainit1 = tdfx_inl(par, VGAINIT1) & 0x1fffff;

	reg.cursloc   = 0;
   
	reg.cursc0    = 0; 
	reg.cursc1    = 0xffffff;
   
	reg.stride    = info->var.xres * cpp;
	reg.startaddr = par->baseline * reg.stride;
	reg.srcbase   = reg.startaddr;
	reg.dstbase   = reg.startaddr;

	/* PLL settings */
	freq = PICOS2KHZ(info->var.pixclock);

	reg.dacmode &= ~DACMODE_2X;
	reg.vidcfg  &= ~VIDCFG_2X;
	if (freq > par->max_pixclock/2) {
		freq = freq > par->max_pixclock ? par->max_pixclock : freq;
		reg.dacmode |= DACMODE_2X;
		reg.vidcfg  |= VIDCFG_2X;
	}
	reg.vidpll = do_calc_pll(freq, &fout);
#if 0
	reg.mempll = do_calc_pll(..., &fout);
	reg.gfxpll = do_calc_pll(..., &fout);
#endif

	if ((info->var.vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
		reg.screensize = info->var.xres | (info->var.yres << 13);
		reg.vidcfg |= VIDCFG_HALF_MODE;
		reg.crt[0x09] |= 0x80;
	} else {
		reg.screensize = info->var.xres | (info->var.yres << 12);
		reg.vidcfg &= ~VIDCFG_HALF_MODE;
	}
	if ((info->var.vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED)
		reg.vidcfg |= VIDCFG_INTERLACE;
	reg.miscinit0 = tdfx_inl(par, MISCINIT0);

#if defined(__BIG_ENDIAN)
	switch (info->var.bits_per_pixel) {
		case 8:
		case 24:
			reg.miscinit0 &= ~(1 << 30);
			reg.miscinit0 &= ~(1 << 31);
			break;
		case 16:
			reg.miscinit0 |= (1 << 30);
			reg.miscinit0 |= (1 << 31);
			break;
		case 32:
			reg.miscinit0 |= (1 << 30);
			reg.miscinit0 &= ~(1 << 31);
			break;
	}
#endif 
	do_write_regs(info, &reg);

	/* Now change fb_fix_screeninfo according to changes in par */
	info->fix.line_length = info->var.xres * ((info->var.bits_per_pixel + 7)>>3);
	info->fix.visual = (info->var.bits_per_pixel == 8) 
				? FB_VISUAL_PSEUDOCOLOR
				: FB_VISUAL_TRUECOLOR;
	DPRINTK("Graphics mode is now set at %dx%d depth %d\n", info->var.xres, info->var.yres, info->var.bits_per_pixel);
	return 0;	
}

/* A handy macro shamelessly pinched from matroxfb */
#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)

static int tdfxfb_setcolreg(unsigned regno, unsigned red, unsigned green,  
			    unsigned blue,unsigned transp,struct fb_info *info) 
{
	struct tdfx_par *par = info->par;
	u32 rgbcol;
   
	if (regno >= info->cmap.len || regno > 255) return 1;
   
	switch (info->fix.visual) {
	case FB_VISUAL_PSEUDOCOLOR:
		rgbcol =(((u32)red   & 0xff00) << 8) |
			(((u32)green & 0xff00) << 0) |
			(((u32)blue  & 0xff00) >> 8);
		do_setpalentry(par, regno, rgbcol);
		break;
	/* Truecolor has no hardware color palettes. */
	case FB_VISUAL_TRUECOLOR:
		if (regno < 16) {
			rgbcol = (CNVT_TOHW( red, info->var.red.length) <<
				  info->var.red.offset) |
				(CNVT_TOHW( green, info->var.green.length) <<
				 info->var.green.offset) |
				(CNVT_TOHW( blue, info->var.blue.length) <<
				 info->var.blue.offset) |
				(CNVT_TOHW( transp, info->var.transp.length) <<
				 info->var.transp.offset);
			par->palette[regno] = rgbcol;
		}

		break;
	default:
		DPRINTK("bad depth %u\n", info->var.bits_per_pixel);
		break;
	}

	return 0;
}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */
static int tdfxfb_blank(int blank, struct fb_info *info)
{ 
	struct tdfx_par *par = info->par;
	u32 dacmode, state = 0, vgablank = 0;

	dacmode = tdfx_inl(par, DACMODE);

	switch (blank) {
		case FB_BLANK_UNBLANK: /* Screen: On; HSync: On, VSync: On */
			state    = 0;
			vgablank = 0;
			break;
		case FB_BLANK_NORMAL: /* Screen: Off; HSync: On, VSync: On */
			state    = 0;
			vgablank = 1;
			break;
		case FB_BLANK_VSYNC_SUSPEND: /* Screen: Off; HSync: On, VSync: Off */
			state    = BIT(3);
			vgablank = 1;
			break;
		case FB_BLANK_HSYNC_SUSPEND: /* Screen: Off; HSync: Off, VSync: On */
			state    = BIT(1);
			vgablank = 1;
			break;
		case FB_BLANK_POWERDOWN: /* Screen: Off; HSync: Off, VSync: Off */
			state    = BIT(1) | BIT(3);
			vgablank = 1;
			break;
	}

	dacmode &= ~(BIT(1) | BIT(3));
	dacmode |= state;
	banshee_make_room(par, 1); 
	tdfx_outl(par, DACMODE, dacmode);
	if (vgablank) 
		vga_disable_video(par);
	else
		vga_enable_video(par);
	return 0;
}

/*   
 * Set the starting position of the visible screen to var->yoffset
 */   
static int tdfxfb_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info) 
{
	struct tdfx_par *par = info->par;
	u32 addr;  	

	if (nopan || var->xoffset || (var->yoffset > var->yres_virtual))
		return -EINVAL;
	if ((var->yoffset + var->yres > var->yres_virtual && nowrap))
		return -EINVAL;

	addr = var->yoffset * info->fix.line_length;
	banshee_make_room(par, 1);
	tdfx_outl(par, VIDDESKSTART, addr);
   
	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset; 
	return 0;
}

#ifdef CONFIG_FB_3DFX_ACCEL
/*
 * FillRect 2D command (solidfill or invert (via ROP_XOR))   
 */
static void tdfxfb_fillrect(struct fb_info *info, const struct fb_fillrect *rect) 
{
	struct tdfx_par *par = info->par;
	u32 bpp = info->var.bits_per_pixel;
	u32 stride = info->fix.line_length;
	u32 fmt= stride | ((bpp+((bpp==8) ? 0 : 8)) << 13); 
	int tdfx_rop;
   	
	if (rect->rop == ROP_COPY) 
		tdfx_rop = TDFX_ROP_COPY;
	else 			 
		tdfx_rop = TDFX_ROP_XOR;

	banshee_make_room(par, 5);
	tdfx_outl(par,	DSTFORMAT, fmt);
	if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR) {
		tdfx_outl(par,	COLORFORE, rect->color);
	} else { /* FB_VISUAL_TRUECOLOR */
		tdfx_outl(par, COLORFORE, par->palette[rect->color]);
	}
	tdfx_outl(par,	COMMAND_2D, COMMAND_2D_FILLRECT | (tdfx_rop << 24));
	tdfx_outl(par,	DSTSIZE,    rect->width | (rect->height << 16));
	tdfx_outl(par,	LAUNCH_2D,  rect->dx | (rect->dy << 16));
}

/*
 * Screen-to-Screen BitBlt 2D command (for the bmove fb op.) 
 */
static void tdfxfb_copyarea(struct fb_info *info, const struct fb_copyarea *area)  
{
	struct tdfx_par *par = info->par;
   	u32 sx = area->sx, sy = area->sy, dx = area->dx, dy = area->dy;
	u32 bpp = info->var.bits_per_pixel;
	u32 stride = info->fix.line_length;
	u32 blitcmd = COMMAND_2D_S2S_BITBLT | (TDFX_ROP_COPY << 24);
	u32 fmt = stride | ((bpp+((bpp==8) ? 0 : 8)) << 13); 
	
	if (area->sx <= area->dx) {
		//-X 
		blitcmd |= BIT(14);
		sx += area->width - 1;
		dx += area->width - 1;
	}
	if (area->sy <= area->dy) {
		//-Y  
		blitcmd |= BIT(15);
		sy += area->height - 1;
		dy += area->height - 1;
	}
   
	banshee_make_room(par, 6);

	tdfx_outl(par,	SRCFORMAT, fmt);
	tdfx_outl(par,	DSTFORMAT, fmt);
	tdfx_outl(par,	COMMAND_2D, blitcmd); 
	tdfx_outl(par,	DSTSIZE,   area->width | (area->height << 16));
	tdfx_outl(par,	DSTXY,     dx | (dy << 16));
	tdfx_outl(par,	LAUNCH_2D, sx | (sy << 16)); 
}

static void tdfxfb_imageblit(struct fb_info *info, const struct fb_image *image) 
{
	struct tdfx_par *par = info->par;
	int size = image->height * ((image->width * image->depth + 7)>>3);
	int fifo_free;
	int i, stride = info->fix.line_length;
	u32 bpp = info->var.bits_per_pixel;
	u32 dstfmt = stride | ((bpp+((bpp==8) ? 0 : 8)) << 13); 
	u8 *chardata = (u8 *) image->data;
	u32 srcfmt;

	if (image->depth != 1) {
		//banshee_make_room(par, 6 + ((size + 3) >> 2));
		//srcfmt = stride | ((bpp+((bpp==8) ? 0 : 8)) << 13) | 0x400000;
		cfb_imageblit(info, image);
		return;
	} else {
		banshee_make_room(par, 8);
		switch (info->fix.visual) {
			case FB_VISUAL_PSEUDOCOLOR:
		tdfx_outl(par, COLORFORE, image->fg_color);
		tdfx_outl(par, COLORBACK, image->bg_color);
				break;
			case FB_VISUAL_TRUECOLOR:
			default:
				tdfx_outl(par, COLORFORE,
					  par->palette[image->fg_color]);
				tdfx_outl(par, COLORBACK,
					  par->palette[image->bg_color]);
		}
#ifdef __BIG_ENDIAN
		srcfmt = 0x400000 | BIT(20);
#else
		srcfmt = 0x400000;
#endif
	}	

	tdfx_outl(par,	SRCXY,     0);
	tdfx_outl(par,	DSTXY,     image->dx | (image->dy << 16));
	tdfx_outl(par,	COMMAND_2D, COMMAND_2D_H2S_BITBLT | (TDFX_ROP_COPY << 24));
	tdfx_outl(par,	SRCFORMAT, srcfmt);
	tdfx_outl(par,	DSTFORMAT, dstfmt);
	tdfx_outl(par,	DSTSIZE,   image->width | (image->height << 16));

	/* A count of how many free FIFO entries we've requested.
	 * When this goes negative, we need to request more. */
	fifo_free = 0;

	/* Send four bytes at a time of data */	
	for (i = (size >> 2) ; i > 0; i--) { 
		if(--fifo_free < 0) {
			fifo_free=31;
			banshee_make_room(par,fifo_free);
		}
		tdfx_outl(par,	LAUNCH_2D,*(u32*)chardata);
		chardata += 4; 
	}	

	/* Send the leftovers now */	
	banshee_make_room(par,3);
	i = size%4;	
	switch (i) {
		case 0: break;
		case 1:  tdfx_outl(par,	LAUNCH_2D,*chardata); break;
		case 2:  tdfx_outl(par,	LAUNCH_2D,*(u16*)chardata); break;
		case 3:  tdfx_outl(par,	LAUNCH_2D,*(u16*)chardata | ((chardata[3]) << 24)); break;
	}
}
#endif /* CONFIG_FB_3DFX_ACCEL */

#ifdef TDFX_HARDWARE_CURSOR
static int tdfxfb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct tdfx_par *par = info->par;
	unsigned long flags;

	/*
	 * If the cursor is not be changed this means either we want the 
	 * current cursor state (if enable is set) or we want to query what
	 * we can do with the cursor (if enable is not set) 
 	 */
	if (!cursor->set) return 0;

	/* Too large of a cursor :-( */
	if (cursor->image.width > 64 || cursor->image.height > 64)
		return -ENXIO;

	/* 
	 * If we are going to be changing things we should disable
	 * the cursor first 
	 */
	if (info->cursor.enable) {
		spin_lock_irqsave(&par->DAClock, flags);
		info->cursor.enable = 0;
		del_timer(&(par->hwcursor.timer));
		tdfx_outl(par, VIDPROCCFG, par->hwcursor.disable);
		spin_unlock_irqrestore(&par->DAClock, flags);
	}

	/* Disable the Cursor */
	if ((cursor->set && FB_CUR_SETCUR) && !cursor->enable)
		return 0;

	/* fix cursor color - XFree86 forgets to restore it properly */
	if (cursor->set && FB_CUR_SETCMAP) {
		struct fb_cmap cmap = cursor->image.cmap;
		unsigned long bg_color, fg_color;

		cmap.len = 2; /* Voodoo 3+ only support 2 color cursors */
		fg_color = ((cmap.red[cmap.start] << 16) |
			    (cmap.green[cmap.start] << 8)  |
			    (cmap.blue[cmap.start]));
		bg_color = ((cmap.red[cmap.start+1] << 16) |
			    (cmap.green[cmap.start+1] << 8) |
			    (cmap.blue[cmap.start+1]));
		fb_copy_cmap(&cmap, &info->cursor.image.cmap);
		spin_lock_irqsave(&par->DAClock, flags);
		banshee_make_room(par, 2);
		tdfx_outl(par, HWCURC0, bg_color);
		tdfx_outl(par, HWCURC1, fg_color);
		spin_unlock_irqrestore(&par->DAClock, flags);
	}

	if (cursor->set && FB_CUR_SETPOS) {
		int x, y;

		x = cursor->image.dx;
		y = cursor->image.dy;
		y -= info->var.yoffset;
		info->cursor.image.dx = x;
		info->cursor.image.dy = y;
		x += 63;
		y += 63;
		spin_lock_irqsave(&par->DAClock, flags);
		banshee_make_room(par, 1);
		tdfx_outl(par, HWCURLOC, (y << 16) + x);
		spin_unlock_irqrestore(&par->DAClock, flags);
	}

	/* Not supported so we fake it */
	if (cursor->set && FB_CUR_SETHOT) {
		info->cursor.hot.x = cursor->hot.x;
		info->cursor.hot.y = cursor->hot.y;
	}

	if (cursor->set && FB_CUR_SETSHAPE) {
		/*
	 	 * Voodoo 3 and above cards use 2 monochrome cursor patterns.
		 *    The reason is so the card can fetch 8 words at a time
		 * and are stored on chip for use for the next 8 scanlines.
		 * This reduces the number of times for access to draw the
		 * cursor for each screen refresh.
		 *    Each pattern is a bitmap of 64 bit wide and 64 bit high
		 * (total of 8192 bits or 1024 Kbytes). The two patterns are
		 * stored in such a way that pattern 0 always resides in the
		 * lower half (least significant 64 bits) of a 128 bit word
		 * and pattern 1 the upper half. If you examine the data of
		 * the cursor image the graphics card uses then from the
		 * begining you see line one of pattern 0, line one of
		 * pattern 1, line two of pattern 0, line two of pattern 1,
		 * etc etc. The linear stride for the cursor is always 16 bytes
		 * (128 bits) which is the maximum cursor width times two for
		 * the two monochrome patterns.
		 */
		u8 *cursorbase = (u8 *) info->cursor.image.data;
		char *bitmap = (char *)cursor->image.data;
		char *mask = (char *) cursor->mask;
		int i, j, k, h = 0;

		for (i = 0; i < 64; i++) {
			if (i < cursor->image.height) {
				j = (cursor->image.width + 7) >> 3;
				k = 8 - j;

				for (;j > 0; j--) {
				/* Pattern 0. Copy the cursor bitmap to it */
					fb_writeb(*bitmap, cursorbase + h);
					bitmap++;
				/* Pattern 1. Copy the cursor mask to it */
					fb_writeb(*mask, cursorbase + h + 8);
					mask++;
					h++;
				}
				for (;k > 0; k--) {
					fb_writeb(0, cursorbase + h);
					fb_writeb(~0, cursorbase + h + 8);
					h++;
				}
			} else {
				fb_writel(0, cursorbase + h);
				fb_writel(0, cursorbase + h + 4);
				fb_writel(~0, cursorbase + h + 8);
				fb_writel(~0, cursorbase + h + 12);
				h += 16;
			}
		}
	}
	/* Turn the cursor on */
	cursor->enable = 1;
	info->cursor = *cursor;
	mod_timer(&par->hwcursor.timer, jiffies+HZ/2);
	spin_lock_irqsave(&par->DAClock, flags);
	banshee_make_room(par, 1);
	tdfx_outl(par, VIDPROCCFG, par->hwcursor.enable);
	spin_unlock_irqrestore(&par->DAClock, flags);
	return 0;
}
#endif

/**
 *      tdfxfb_probe - Device Initializiation
 *
 *      @pdev:  PCI Device to initialize
 *      @id:    PCI Device ID
 *
 *      Initializes and allocates resources for PCI device @pdev.
 *
 */
static int __devinit tdfxfb_probe(struct pci_dev *pdev,
                                  const struct pci_device_id *id)
{
	struct tdfx_par *default_par;
	struct fb_info *info;
	int err, lpitch;

	if ((err = pci_enable_device(pdev))) {
		printk(KERN_WARNING "tdfxfb: Can't enable pdev: %d\n", err);
		return err;
	}

	info = framebuffer_alloc(sizeof(struct tdfx_par), &pdev->dev);

	if (!info)
		return -ENOMEM;
		
	default_par = info->par;
 
	/* Configure the default fb_fix_screeninfo first */
	switch (pdev->device) {
		case PCI_DEVICE_ID_3DFX_BANSHEE:	
			strcat(tdfx_fix.id, " Banshee");
			default_par->max_pixclock = BANSHEE_MAX_PIXCLOCK;
			break;
		case PCI_DEVICE_ID_3DFX_VOODOO3:
			strcat(tdfx_fix.id, " Voodoo3");
			default_par->max_pixclock = VOODOO3_MAX_PIXCLOCK;
			break;
		case PCI_DEVICE_ID_3DFX_VOODOO5:
			strcat(tdfx_fix.id, " Voodoo5");
			default_par->max_pixclock = VOODOO5_MAX_PIXCLOCK;
			break;
	}

	tdfx_fix.mmio_start = pci_resource_start(pdev, 0);
	tdfx_fix.mmio_len = pci_resource_len(pdev, 0);
	default_par->regbase_virt = ioremap_nocache(tdfx_fix.mmio_start, tdfx_fix.mmio_len);
	if (!default_par->regbase_virt) {
		printk("fb: Can't remap %s register area.\n", tdfx_fix.id);
		goto out_err;
	}
    
	if (!request_mem_region(pci_resource_start(pdev, 0),
	    pci_resource_len(pdev, 0), "tdfx regbase")) {
		printk(KERN_WARNING "tdfxfb: Can't reserve regbase\n");
		goto out_err;
	} 

	tdfx_fix.smem_start = pci_resource_start(pdev, 1);
	if (!(tdfx_fix.smem_len = do_lfb_size(default_par, pdev->device))) {
		printk("fb: Can't count %s memory.\n", tdfx_fix.id);
		release_mem_region(pci_resource_start(pdev, 0),
				   pci_resource_len(pdev, 0));
		goto out_err;	
	}

	if (!request_mem_region(pci_resource_start(pdev, 1),
	     pci_resource_len(pdev, 1), "tdfx smem")) {
		printk(KERN_WARNING "tdfxfb: Can't reserve smem\n");
		release_mem_region(pci_resource_start(pdev, 0),
				   pci_resource_len(pdev, 0));
		goto out_err;
	}

	info->screen_base = ioremap_nocache(tdfx_fix.smem_start, 
					    tdfx_fix.smem_len);
	if (!info->screen_base) {
		printk("fb: Can't remap %s framebuffer.\n", tdfx_fix.id);
		release_mem_region(pci_resource_start(pdev, 1),
				   pci_resource_len(pdev, 1));
		release_mem_region(pci_resource_start(pdev, 0),
				   pci_resource_len(pdev, 0));
		goto out_err;
	}

	default_par->iobase = pci_resource_start(pdev, 2);
    
	if (!request_region(pci_resource_start(pdev, 2),
	    pci_resource_len(pdev, 2), "tdfx iobase")) {
		printk(KERN_WARNING "tdfxfb: Can't reserve iobase\n");
		release_mem_region(pci_resource_start(pdev, 1),
				   pci_resource_len(pdev, 1));
		release_mem_region(pci_resource_start(pdev, 0),
				   pci_resource_len(pdev, 0));
		goto out_err;
	}

	printk("fb: %s memory = %dK\n", tdfx_fix.id, tdfx_fix.smem_len >> 10);

	tdfx_fix.ypanstep	= nopan ? 0 : 1;
	tdfx_fix.ywrapstep	= nowrap ? 0 : 1;
   
	info->fbops		= &tdfxfb_ops;
	info->fix		= tdfx_fix; 	
	info->pseudo_palette	= default_par->palette;
	info->flags		= FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;
#ifdef CONFIG_FB_3DFX_ACCEL
	info->flags             |= FBINFO_HWACCEL_FILLRECT |
		FBINFO_HWACCEL_COPYAREA | FBINFO_HWACCEL_IMAGEBLIT;
#endif

	if (!mode_option)
		mode_option = "640x480@60";
	 
	err = fb_find_mode(&info->var, info, mode_option, NULL, 0, NULL, 8); 
	if (!err || err == 4)
		info->var = tdfx_var;

	/* maximize virtual vertical length */
	lpitch = info->var.xres_virtual * ((info->var.bits_per_pixel + 7) >> 3);
	info->var.yres_virtual = info->fix.smem_len/lpitch;
	if (info->var.yres_virtual < info->var.yres)
		goto out_err;

#ifdef CONFIG_FB_3DFX_ACCEL
	/*
	 * FIXME: Limit var->yres_virtual to 4096 because of screen artifacts
	 * during scrolling. This is only present if 2D acceleration is
	 * enabled.
	 */
	if (info->var.yres_virtual > 4096)
		info->var.yres_virtual = 4096;
#endif /* CONFIG_FB_3DFX_ACCEL */

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0) {
		printk(KERN_WARNING "tdfxfb: Can't allocate color map\n");
		goto out_err;
	}

	if (register_framebuffer(info) < 0) {
		printk("tdfxfb: can't register framebuffer\n");
		fb_dealloc_cmap(&info->cmap);
		goto out_err;
	}
	/*
	 * Our driver data
	 */
	pci_set_drvdata(pdev, info);
	return 0; 

out_err:
	/*
	 * Cleanup after anything that was remapped/allocated.
	 */
	if (default_par->regbase_virt)
		iounmap(default_par->regbase_virt);
	if (info->screen_base)
		iounmap(info->screen_base);
	framebuffer_release(info);
	return -ENXIO;
}

#ifndef MODULE
static void tdfxfb_setup(char *options)
{
	char* this_opt;

	if (!options || !*options)
		return;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		if(!strcmp(this_opt, "nopan")) {
			nopan = 1;
		} else if(!strcmp(this_opt, "nowrap")) {
			nowrap = 1;
		} else {
			mode_option = this_opt;
		}
	}
}
#endif

/**
 *      tdfxfb_remove - Device removal
 *
 *      @pdev:  PCI Device to cleanup
 *
 *      Releases all resources allocated during the course of the driver's
 *      lifetime for the PCI device @pdev.
 *
 */
static void __devexit tdfxfb_remove(struct pci_dev *pdev)
{
	struct fb_info *info = pci_get_drvdata(pdev);
	struct tdfx_par *par = info->par;

	unregister_framebuffer(info);
	iounmap(par->regbase_virt);
	iounmap(info->screen_base);

	/* Clean up after reserved regions */
	release_region(pci_resource_start(pdev, 2),
		       pci_resource_len(pdev, 2));
	release_mem_region(pci_resource_start(pdev, 1),
			   pci_resource_len(pdev, 1));
	release_mem_region(pci_resource_start(pdev, 0),
			   pci_resource_len(pdev, 0));
	pci_set_drvdata(pdev, NULL);
	framebuffer_release(info);
}

static int __init tdfxfb_init(void)
{
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("tdfxfb", &option))
		return -ENODEV;

	tdfxfb_setup(option);
#endif
        return pci_register_driver(&tdfxfb_driver);
}

static void __exit tdfxfb_exit(void)
{
        pci_unregister_driver(&tdfxfb_driver);
}

MODULE_AUTHOR("Hannu Mallat <hmallat@cc.hut.fi>");
MODULE_DESCRIPTION("3Dfx framebuffer device driver");
MODULE_LICENSE("GPL");
 
module_init(tdfxfb_init);
module_exit(tdfxfb_exit);
