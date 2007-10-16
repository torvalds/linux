/*
 * linux/drivers/video/sstfb.c -- voodoo graphics frame buffer
 *
 *     Copyright (c) 2000-2002 Ghozlane Toumi <gtoumi@laposte.net>
 *
 *     Created 15 Jan 2000 by Ghozlane Toumi
 *
 * Contributions (and many thanks) :
 *
 * 03/2001 James Simmons   <jsimmons@infradead.org>
 * 04/2001 Paul Mundt      <lethal@chaoticdreams.org>
 * 05/2001 Urs Ganse       <ursg@uni.de>
 *	(initial work on voodoo2 port, interlace)
 * 09/2002 Helge Deller    <deller@gmx.de>
 *	(enable driver on big-endian machines (hppa), ioctl fixes)
 * 12/2002 Helge Deller    <deller@gmx.de>
 *	(port driver to new frambuffer infrastructure)
 * 01/2003 Helge Deller    <deller@gmx.de>
 *	(initial work on fb hardware acceleration for voodoo2)
 * 08/2006 Alan Cox 	   <alan@redhat.com>
 *	Remove never finished and bogus 24/32bit support
 *	Clean up macro abuse
 *	Minor tidying for format.
 * 12/2006 Helge Deller    <deller@gmx.de>
 *	add /sys/class/graphics/fbX/vgapass sysfs-interface
 *	add module option "mode_option" to set initial screen mode
 *	use fbdev default videomode database
 *	remove debug functions from ioctl
 */

/*
 * The voodoo1 has the following memory mapped address space:
 * 0x000000 - 0x3fffff : registers              (4MB)
 * 0x400000 - 0x7fffff : linear frame buffer    (4MB)
 * 0x800000 - 0xffffff : texture memory         (8MB)
 */

/*
 * misc notes, TODOs, toASKs, and deep thoughts

-TODO: at one time or another test that the mode is acceptable by the monitor
-ASK: Can I choose different ordering for the color bitfields (rgba argb ...)
      which one should i use ? is there any preferred one ? It seems ARGB is
      the one ...
-TODO: in  set_var check the validity of timings (hsync vsync)...
-TODO: check and recheck the use of sst_wait_idle : we don't flush the fifo via
       a nop command. so it's ok as long as the commands we pass don't go
       through the fifo. warning: issuing a nop command seems to need pci_fifo
-FIXME: in case of failure in the init sequence, be sure we return to a safe
        state.
- FIXME: Use accelerator for 2D scroll
-FIXME: 4MB boards have banked memory (FbiInit2 bits 1 & 20)
 */

/*
 * debug info
 * SST_DEBUG : enable debugging
 * SST_DEBUG_REG : debug registers
 *   0 :  no debug
 *   1 : dac calls, [un]set_bits, FbiInit
 *   2 : insane debug level (log every register read/write)
 * SST_DEBUG_FUNC : functions
 *   0 : no debug
 *   1 : function call / debug ioctl
 *   2 : variables
 *   3 : flood . you don't want to do that. trust me.
 * SST_DEBUG_VAR : debug display/var structs
 *   0 : no debug
 *   1 : dumps display, fb_var
 *
 * sstfb specific ioctls:
 *   		toggle vga (0x46db) : toggle vga_pass_through
 */

#undef SST_DEBUG


/*
 * Includes
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/uaccess.h>
#include <video/sstfb.h>


/* initialized by setup */

static int vgapass;		/* enable VGA passthrough cable */
static int mem;			/* mem size in MB, 0 = autodetect */
static int clipping = 1;	/* use clipping (slower, safer) */
static int gfxclk;		/* force FBI freq in Mhz . Dangerous */
static int slowpci;		/* slow PCI settings */

/*
  Possible default video modes: 800x600@60, 640x480@75, 1024x768@76, 640x480@60
*/
#define DEFAULT_VIDEO_MODE "640x480@60"

static char *mode_option __devinitdata = DEFAULT_VIDEO_MODE;

enum {
	ID_VOODOO1 = 0,
	ID_VOODOO2 = 1,
};

#define IS_VOODOO2(par) ((par)->type == ID_VOODOO2)

static struct sst_spec voodoo_spec[] __devinitdata = {
 { .name = "Voodoo Graphics", .default_gfx_clock = 50000, .max_gfxclk = 60 },
 { .name = "Voodoo2",	      .default_gfx_clock = 75000, .max_gfxclk = 85 },
};


/*
 * debug functions
 */

#if (SST_DEBUG_REG > 0)
static void sst_dbg_print_read_reg(u32 reg, u32 val) {
	const char *regname;
	switch (reg) {
	case FBIINIT0:	regname = "FbiInit0"; break;
	case FBIINIT1:	regname = "FbiInit1"; break;
	case FBIINIT2:	regname = "FbiInit2"; break;
	case FBIINIT3:	regname = "FbiInit3"; break;
	case FBIINIT4:	regname = "FbiInit4"; break;
	case FBIINIT5:	regname = "FbiInit5"; break;
	case FBIINIT6:	regname = "FbiInit6"; break;
	default:	regname = NULL;       break;
	}
	if (regname == NULL)
		r_ddprintk("sst_read(%#x): %#x\n", reg, val);
	else
		r_dprintk(" sst_read(%s): %#x\n", regname, val);
}

static void sst_dbg_print_write_reg(u32 reg, u32 val) {
	const char *regname;
	switch (reg) {
	case FBIINIT0:	regname = "FbiInit0"; break;
	case FBIINIT1:	regname = "FbiInit1"; break;
	case FBIINIT2:	regname = "FbiInit2"; break;
	case FBIINIT3:	regname = "FbiInit3"; break;
	case FBIINIT4:	regname = "FbiInit4"; break;
	case FBIINIT5:	regname = "FbiInit5"; break;
	case FBIINIT6:	regname = "FbiInit6"; break;
	default:	regname = NULL;       break;
	}
	if (regname == NULL)
		r_ddprintk("sst_write(%#x, %#x)\n", reg, val);
	else
		r_dprintk(" sst_write(%s, %#x)\n", regname, val);
}
#else /*  (SST_DEBUG_REG > 0) */
#  define sst_dbg_print_read_reg(reg, val)	do {} while(0)
#  define sst_dbg_print_write_reg(reg, val)	do {} while(0)
#endif /*  (SST_DEBUG_REG > 0) */

/*
 * hardware access functions
 */

/* register access */
#define sst_read(reg)		__sst_read(par->mmio_vbase, reg)
#define sst_write(reg,val)	__sst_write(par->mmio_vbase, reg, val)
#define sst_set_bits(reg,val)	__sst_set_bits(par->mmio_vbase, reg, val)
#define sst_unset_bits(reg,val)	__sst_unset_bits(par->mmio_vbase, reg, val)
#define sst_dac_read(reg)	__sst_dac_read(par->mmio_vbase, reg)
#define sst_dac_write(reg,val)	__sst_dac_write(par->mmio_vbase, reg, val)
#define dac_i_read(reg)		__dac_i_read(par->mmio_vbase, reg)
#define dac_i_write(reg,val)	__dac_i_write(par->mmio_vbase, reg, val)

static inline u32 __sst_read(u8 __iomem *vbase, u32 reg)
{
	u32 ret = readl(vbase + reg);
	sst_dbg_print_read_reg(reg, ret);
	return ret;
}

static inline void __sst_write(u8 __iomem *vbase, u32 reg, u32 val)
{
	sst_dbg_print_write_reg(reg, val);
	writel(val, vbase + reg);
}

static inline void __sst_set_bits(u8 __iomem *vbase, u32 reg, u32 val)
{
	r_dprintk("sst_set_bits(%#x, %#x)\n", reg, val);
	__sst_write(vbase, reg, __sst_read(vbase, reg) | val);
}

static inline void __sst_unset_bits(u8 __iomem *vbase, u32 reg, u32 val)
{
	r_dprintk("sst_unset_bits(%#x, %#x)\n", reg, val);
	__sst_write(vbase, reg, __sst_read(vbase, reg) & ~val);
}

/*
 * wait for the fbi chip. ASK: what happens if the fbi is stuck ?
 *
 * the FBI is supposed to be ready if we receive 5 time
 * in a row a "idle" answer to our requests
 */

#define sst_wait_idle() __sst_wait_idle(par->mmio_vbase)

static int __sst_wait_idle(u8 __iomem *vbase)
{
	int count = 0;

	/* if (doFBINOP) __sst_write(vbase, NOPCMD, 0); */

	while(1) {
		if (__sst_read(vbase, STATUS) & STATUS_FBI_BUSY) {
			f_dddprintk("status: busy\n");
/* FIXME basicaly, this is a busy wait. maybe not that good. oh well;
 * this is a small loop after all.
 * Or maybe we should use mdelay() or udelay() here instead ? */
			count = 0;
		} else {
			count++;
			f_dddprintk("status: idle(%d)\n", count);
		}
		if (count >= 5) return 1;
/* XXX  do something to avoid hanging the machine if the voodoo is out */
	}
}


/* dac access */
/* dac_read should be remaped to FbiInit2 (via the pci reg init_enable) */
static u8 __sst_dac_read(u8 __iomem *vbase, u8 reg)
{
	u8 ret;

	reg &= 0x07;
	__sst_write(vbase, DAC_DATA, ((u32)reg << 8) | DAC_READ_CMD );
	__sst_wait_idle(vbase);
	/* udelay(10); */
	ret = __sst_read(vbase, DAC_READ) & 0xff;
	r_dprintk("sst_dac_read(%#x): %#x\n", reg, ret);

	return ret;
}

static void __sst_dac_write(u8 __iomem *vbase, u8 reg, u8 val)
{
	r_dprintk("sst_dac_write(%#x, %#x)\n", reg, val);
	reg &= 0x07;
	__sst_write(vbase, DAC_DATA,(((u32)reg << 8)) | (u32)val);
	__sst_wait_idle(vbase);
}

/* indexed access to ti/att dacs */
static u32 __dac_i_read(u8 __iomem *vbase, u8 reg)
{
	u32 ret;

	__sst_dac_write(vbase, DACREG_ADDR_I, reg);
	ret = __sst_dac_read(vbase, DACREG_DATA_I);
	r_dprintk("sst_dac_read_i(%#x): %#x\n", reg, ret);
	return ret;
}
static void __dac_i_write(u8 __iomem *vbase, u8 reg,u8 val)
{
	r_dprintk("sst_dac_write_i(%#x, %#x)\n", reg, val);
	__sst_dac_write(vbase, DACREG_ADDR_I, reg);
	__sst_dac_write(vbase, DACREG_DATA_I, val);
}

/* compute the m,n,p  , returns the real freq
 * (ics datasheet :  N <-> N1 , P <-> N2)
 *
 * Fout= Fref * (M+2)/( 2^P * (N+2))
 *  we try to get close to the asked freq
 *  with P as high, and M as low as possible
 * range:
 * ti/att : 0 <= M <= 255; 0 <= P <= 3; 0<= N <= 63
 * ics    : 1 <= M <= 127; 0 <= P <= 3; 1<= N <= 31
 * we'll use the lowest limitation, should be precise enouth
 */
static int sst_calc_pll(const int freq, int *freq_out, struct pll_timing *t)
{
	int m, m2, n, p, best_err, fout;
	int best_n = -1;
	int best_m = -1;

	best_err = freq;
	p = 3;
	/* f * 2^P = vco should be less than VCOmax ~ 250 MHz for ics*/
	while (((1 << p) * freq > VCO_MAX) && (p >= 0))
		p--;
	if (p == -1)
		return -EINVAL;
	for (n = 1; n < 32; n++) {
		/* calc 2 * m so we can round it later*/
		m2 = (2 * freq * (1 << p) * (n + 2) ) / DAC_FREF - 4 ;

		m = (m2 % 2 ) ? m2/2+1 : m2/2 ;
		if (m >= 128)
			break;
		fout = (DAC_FREF * (m + 2)) / ((1 << p) * (n + 2));
		if ((abs(fout - freq) < best_err) && (m > 0)) {
			best_n = n;
			best_m = m;
			best_err = abs(fout - freq);
			/* we get the lowest m , allowing 0.5% error in freq*/
			if (200*best_err < freq) break;
		}
	}
	if (best_n == -1)  /* unlikely, but who knows ? */
		return -EINVAL;
	t->p = p;
	t->n = best_n;
	t->m = best_m;
	*freq_out = (DAC_FREF * (t->m + 2)) / ((1 << t->p) * (t->n + 2));
	f_ddprintk ("m: %d, n: %d, p: %d, F: %dKhz\n",
		  t->m, t->n, t->p, *freq_out);
	return 0;
}

/*
 * clear lfb screen
 */
static void sstfb_clear_screen(struct fb_info *info)
{
	/* clear screen */
	fb_memset(info->screen_base, 0, info->fix.smem_len);
}


/**
 *      sstfb_check_var - Optional function.  Validates a var passed in.
 *      @var: frame buffer variable screen structure
 *      @info: frame buffer structure that represents a single frame buffer
 *
 *	Limit to the abilities of a single chip as SLI is not supported
 *	by this driver.
 */

static int sstfb_check_var(struct fb_var_screeninfo *var,
		struct fb_info *info)
{
	struct sstfb_par *par = info->par;
	int hSyncOff   = var->xres + var->right_margin + var->left_margin;
	int vSyncOff   = var->yres + var->lower_margin + var->upper_margin;
	int vBackPorch = var->left_margin, yDim = var->yres;
	int vSyncOn    = var->vsync_len;
	int tiles_in_X, real_length;
	unsigned int freq;

	if (sst_calc_pll(PICOS2KHZ(var->pixclock), &freq, &par->pll)) {
		printk(KERN_ERR "sstfb: Pixclock at %ld KHZ out of range\n",
				PICOS2KHZ(var->pixclock));
		return -EINVAL;
	}
	var->pixclock = KHZ2PICOS(freq);
	
	if (var->vmode & FB_VMODE_INTERLACED)
		vBackPorch += (vBackPorch % 2);
	if (var->vmode & FB_VMODE_DOUBLE) {
		vBackPorch <<= 1;
		yDim <<=1;
		vSyncOn <<=1;
		vSyncOff <<=1;
	}

	switch (var->bits_per_pixel) {
	case 0 ... 16 :
		var->bits_per_pixel = 16;
		break;
	default :
		printk(KERN_ERR "sstfb: Unsupported bpp %d\n", var->bits_per_pixel);
		return -EINVAL;
	}
	
	/* validity tests */
	if (var->xres <= 1 || yDim <= 0 || var->hsync_len <= 1  ||
	    hSyncOff <= 1  || var->left_margin <= 2  || vSyncOn <= 0 ||
	    vSyncOff <= 0 || vBackPorch <= 0) {
		return -EINVAL;
	}

	if (IS_VOODOO2(par)) {
		/* Voodoo 2 limits */
		tiles_in_X = (var->xres + 63 ) / 64 * 2;		

		if (var->xres  > POW2(11) || yDim >= POW2(11)) {
			printk(KERN_ERR "sstfb: Unsupported resolution %dx%d\n",
			         var->xres, var->yres);
			return -EINVAL;
		}

		if (var->hsync_len > POW2(9) || hSyncOff > POW2(11) ||
		    var->left_margin - 2 >= POW2(9) || vSyncOn >= POW2(13) ||
		    vSyncOff >= POW2(13) || vBackPorch >= POW2(9) ||
		    tiles_in_X >= POW2(6) || tiles_in_X <= 0) {
			printk(KERN_ERR "sstfb: Unsupported timings\n");
			return -EINVAL;
		}
	} else {
		/* Voodoo limits */
		tiles_in_X = (var->xres + 63 ) / 64;

		if (var->vmode) {
			printk(KERN_ERR "sstfb: Interlace/doublescan not supported %#x\n",
				var->vmode);
			return -EINVAL;
		}
		if (var->xres > POW2(10) || var->yres >= POW2(10)) {
			printk(KERN_ERR "sstfb: Unsupported resolution %dx%d\n",
			         var->xres, var->yres);
			return -EINVAL;
		}
		if (var->hsync_len > POW2(8) || hSyncOff - 1 > POW2(10) ||
		    var->left_margin - 2 >= POW2(8) || vSyncOn >= POW2(12) ||
		    vSyncOff >= POW2(12) || vBackPorch >= POW2(8) ||
		    tiles_in_X >= POW2(4) || tiles_in_X <= 0) {
			printk(KERN_ERR "sstfb: Unsupported timings\n");
			return -EINVAL;
		}
	}

	/* it seems that the fbi uses tiles of 64x16 pixels to "map" the mem */
	/* FIXME: i don't like this... looks wrong */
	real_length = tiles_in_X  * (IS_VOODOO2(par) ? 32 : 64 )
	              * ((var->bits_per_pixel == 16) ? 2 : 4);

	if (real_length * yDim > info->fix.smem_len) {
		printk(KERN_ERR "sstfb: Not enough video memory\n");
		return -ENOMEM;
	}

	var->sync &= (FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT);
	var->vmode &= (FB_VMODE_INTERLACED | FB_VMODE_DOUBLE);
	var->xoffset = 0;
	var->yoffset = 0;
	var->height  = -1;
	var->width   = -1;

	/*
	 * correct the color bit fields
	 */
	/* var->{red|green|blue}.msb_right = 0; */

	switch (var->bits_per_pixel) {
	case 16:	/* RGB 565  LfbMode 0 */
		var->red.length    = 5;
		var->green.length  = 6;
		var->blue.length   = 5;
		var->transp.length = 0;

		var->red.offset    = 11;
		var->green.offset  = 5;
		var->blue.offset   = 0;
		var->transp.offset = 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/**
 *      sstfb_set_par - Optional function.  Alters the hardware state.
 *      @info: frame buffer structure that represents a single frame buffer
 */
static int sstfb_set_par(struct fb_info *info)
{
	struct sstfb_par *par = info->par;
	u32 lfbmode, fbiinit1, fbiinit2, fbiinit3, fbiinit5, fbiinit6=0;
	struct pci_dev *sst_dev = par->dev;
	unsigned int freq;
	int ntiles;

	par->hSyncOff	= info->var.xres + info->var.right_margin + info->var.left_margin;

	par->yDim 	= info->var.yres;
	par->vSyncOn 	= info->var.vsync_len;
	par->vSyncOff	= info->var.yres + info->var.lower_margin + info->var.upper_margin;
	par->vBackPorch = info->var.upper_margin;

	/* We need par->pll */
	sst_calc_pll(PICOS2KHZ(info->var.pixclock), &freq, &par->pll);

	if (info->var.vmode & FB_VMODE_INTERLACED)
		par->vBackPorch += (par->vBackPorch % 2);
	if (info->var.vmode & FB_VMODE_DOUBLE) {
		par->vBackPorch <<= 1;
		par->yDim <<=1;
		par->vSyncOn <<=1;
		par->vSyncOff <<=1;
	}

	if (IS_VOODOO2(par)) {
		/* voodoo2 has 32 pixel wide tiles , BUT stange things
		   happen with odd number of tiles */
		par->tiles_in_X = (info->var.xres + 63 ) / 64 * 2;
	} else {
		/* voodoo1 has 64 pixels wide tiles. */
		par->tiles_in_X = (info->var.xres + 63 ) / 64;
	}

	f_ddprintk("hsync_len hSyncOff vsync_len vSyncOff\n");
	f_ddprintk("%-7d %-8d %-7d %-8d\n",
	           info->var.hsync_len, par->hSyncOff,
	           par->vSyncOn, par->vSyncOff);
	f_ddprintk("left_margin upper_margin xres yres Freq\n");
	f_ddprintk("%-10d %-10d %-4d %-4d %-8ld\n",
	           info->var.left_margin, info->var.upper_margin,
	           info->var.xres, info->var.yres, PICOS2KHZ(info->var.pixclock));

	sst_write(NOPCMD, 0);
	sst_wait_idle();
	pci_write_config_dword(sst_dev, PCI_INIT_ENABLE, PCI_EN_INIT_WR);
	sst_set_bits(FBIINIT1, VIDEO_RESET);
	sst_set_bits(FBIINIT0, FBI_RESET | FIFO_RESET);
	sst_unset_bits(FBIINIT2, EN_DRAM_REFRESH);
	sst_wait_idle();

	/*sst_unset_bits (FBIINIT0, FBI_RESET); / reenable FBI ? */

	sst_write(BACKPORCH, par->vBackPorch << 16 | (info->var.left_margin - 2));
	sst_write(VIDEODIMENSIONS, par->yDim << 16 | (info->var.xres - 1));
	sst_write(HSYNC, (par->hSyncOff - 1) << 16 | (info->var.hsync_len - 1));
	sst_write(VSYNC,       par->vSyncOff << 16 | par->vSyncOn);

	fbiinit2 = sst_read(FBIINIT2);
	fbiinit3 = sst_read(FBIINIT3);

	/* everything is reset. we enable fbiinit2/3 remap : dac acces ok */
	pci_write_config_dword(sst_dev, PCI_INIT_ENABLE,
	                       PCI_EN_INIT_WR | PCI_REMAP_DAC );

	par->dac_sw.set_vidmod(info, info->var.bits_per_pixel);

	/* set video clock */
	par->dac_sw.set_pll(info, &par->pll, VID_CLOCK);

	/* disable fbiinit2/3 remap */
	pci_write_config_dword(sst_dev, PCI_INIT_ENABLE,
	                       PCI_EN_INIT_WR);

	/* restore fbiinit2/3 */
	sst_write(FBIINIT2,fbiinit2);
	sst_write(FBIINIT3,fbiinit3);

	fbiinit1 = (sst_read(FBIINIT1) & VIDEO_MASK)
	            | EN_DATA_OE
	            | EN_BLANK_OE
	            | EN_HVSYNC_OE
	            | EN_DCLK_OE
		 /* | (15 << TILES_IN_X_SHIFT) */
	            | SEL_INPUT_VCLK_2X
		 /* | (2 << VCLK_2X_SEL_DEL_SHIFT)
	            | (2 << VCLK_DEL_SHIFT) */;
/* try with vclk_in_delay =0 (bits 29:30) , vclk_out_delay =0 (bits(27:28)
 in (near) future set them accordingly to revision + resolution (cf glide)
 first understand what it stands for :)
 FIXME: there are some artefacts... check for the vclk_in_delay
 lets try with 6ns delay in both vclk_out & in...
 doh... they're still there :\
*/

	ntiles = par->tiles_in_X;
	if (IS_VOODOO2(par)) {
		fbiinit1 |= ((ntiles & 0x20) >> 5) << TILES_IN_X_MSB_SHIFT
		            | ((ntiles & 0x1e) >> 1) << TILES_IN_X_SHIFT;
/* as the only value of importance for us in fbiinit6 is tiles in X (lsb),
   and as reading fbinit 6 will return crap (see FBIINIT6_DEFAULT) we just
   write our value. BTW due to the dac unable to read odd number of tiles, this
   field is always null ... */
		fbiinit6 = (ntiles & 0x1) << TILES_IN_X_LSB_SHIFT;
	}
	else
		fbiinit1 |= ntiles << TILES_IN_X_SHIFT;

	switch (info->var.bits_per_pixel) {
	case 16:
		fbiinit1 |=  SEL_SOURCE_VCLK_2X_SEL;
		break;
	default:
		return -EINVAL;
	}
	sst_write(FBIINIT1, fbiinit1);
	if (IS_VOODOO2(par)) {
		sst_write(FBIINIT6, fbiinit6);
		fbiinit5=sst_read(FBIINIT5) & FBIINIT5_MASK ;
		if (info->var.vmode & FB_VMODE_INTERLACED)
			fbiinit5 |= INTERLACE;
		if (info->var.vmode & FB_VMODE_DOUBLE)
			fbiinit5 |= VDOUBLESCAN;
		if (info->var.sync & FB_SYNC_HOR_HIGH_ACT)
			fbiinit5 |= HSYNC_HIGH;
		if (info->var.sync & FB_SYNC_VERT_HIGH_ACT)
			fbiinit5 |= VSYNC_HIGH;
		sst_write(FBIINIT5, fbiinit5);
	}
	sst_wait_idle();
	sst_unset_bits(FBIINIT1, VIDEO_RESET);
	sst_unset_bits(FBIINIT0, FBI_RESET | FIFO_RESET);
	sst_set_bits(FBIINIT2, EN_DRAM_REFRESH);
	/* disables fbiinit writes */
	pci_write_config_dword(sst_dev, PCI_INIT_ENABLE, PCI_EN_FIFO_WR);

	/* set lfbmode : set mode + front buffer for reads/writes
	   + disable pipeline */
	switch (info->var.bits_per_pixel) {
	case 16:
		lfbmode = LFB_565;
		break;
	default:
		return -EINVAL;
	}

#if defined(__BIG_ENDIAN)
	/* Enable byte-swizzle functionality in hardware.
	 * With this enabled, all our read- and write-accesses to
	 * the voodoo framebuffer can be done in native format, and
	 * the hardware will automatically convert it to little-endian.
	 * - tested on HP-PARISC, Helge Deller <deller@gmx.de> */
	lfbmode |= ( LFB_WORD_SWIZZLE_WR | LFB_BYTE_SWIZZLE_WR |
		     LFB_WORD_SWIZZLE_RD | LFB_BYTE_SWIZZLE_RD );
#endif
	
	if (clipping) {
		sst_write(LFBMODE, lfbmode | EN_PXL_PIPELINE);
	/*
	 * Set "clipping" dimensions. If clipping is disabled and
	 * writes to offscreen areas of the framebuffer are performed,
	 * the "behaviour is undefined" (_very_ undefined) - Urs
	 */
	/* btw, it requires enabling pixel pipeline in LFBMODE .
	   off screen read/writes will just wrap and read/print pixels
	   on screen. Ugly but not that dangerous */
		f_ddprintk("setting clipping dimensions 0..%d, 0..%d\n",
		            info->var.xres - 1, par->yDim - 1);

		sst_write(CLIP_LEFT_RIGHT, info->var.xres);
		sst_write(CLIP_LOWY_HIGHY, par->yDim);
		sst_set_bits(FBZMODE, EN_CLIPPING | EN_RGB_WRITE);
	} else {
		/* no clipping : direct access, no pipeline */
		sst_write(LFBMODE, lfbmode);
	}
	return 0;
}

/**
 *      sstfb_setcolreg - Optional function. Sets a color register.
 *      @regno: hardware colormap register
 *      @red: frame buffer colormap structure
 *      @green: The green value which can be up to 16 bits wide
 *      @blue:  The blue value which can be up to 16 bits wide.
 *      @transp: If supported the alpha value which can be up to 16 bits wide.
 *      @info: frame buffer info structure
 */
static int sstfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                           u_int transp, struct fb_info *info)
{
	struct sstfb_par *par = info->par;
	u32 col;

	f_dddprintk("sstfb_setcolreg\n");
	f_dddprintk("%-2d rgbt: %#x, %#x, %#x, %#x\n",
	            regno, red, green, blue, transp);
	if (regno > 15)
		return 0;

	red    >>= (16 - info->var.red.length);
	green  >>= (16 - info->var.green.length);
	blue   >>= (16 - info->var.blue.length);
	transp >>= (16 - info->var.transp.length);
	col = (red << info->var.red.offset)
	    | (green << info->var.green.offset)
	    | (blue  << info->var.blue.offset)
	    | (transp << info->var.transp.offset);
	
	par->palette[regno] = col;

	return 0;
}

static void sstfb_setvgapass( struct fb_info *info, int enable )
{
	struct sstfb_par *par = info->par;
	struct pci_dev *sst_dev = par->dev;
	u32 fbiinit0, tmp;

	enable = enable ? 1:0;
	if (par->vgapass == enable)
		return;
	par->vgapass = enable;

	pci_read_config_dword(sst_dev, PCI_INIT_ENABLE, &tmp);
	pci_write_config_dword(sst_dev, PCI_INIT_ENABLE,
			       tmp | PCI_EN_INIT_WR );
	fbiinit0 = sst_read (FBIINIT0);
	if (par->vgapass) {
		sst_write(FBIINIT0, fbiinit0 & ~DIS_VGA_PASSTHROUGH);
		printk(KERN_INFO "fb%d: Enabling VGA pass-through\n", info->node );
	} else {
		sst_write(FBIINIT0, fbiinit0 | DIS_VGA_PASSTHROUGH);
		printk(KERN_INFO "fb%d: Disabling VGA pass-through\n", info->node );
	}
	pci_write_config_dword(sst_dev, PCI_INIT_ENABLE, tmp);
}

static ssize_t store_vgapass(struct device *device, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct fb_info *info = dev_get_drvdata(device);
	char ** last = NULL;
	int val;

	val = simple_strtoul(buf, last, 0);
	sstfb_setvgapass(info, val);

	return count;
}

static ssize_t show_vgapass(struct device *device, struct device_attribute *attr,
			char *buf)
{
	struct fb_info *info = dev_get_drvdata(device);
	struct sstfb_par *par = info->par;
	return snprintf(buf, PAGE_SIZE, "%d\n", par->vgapass);
}

static struct device_attribute device_attrs[] = {
	__ATTR(vgapass, S_IRUGO|S_IWUSR, show_vgapass, store_vgapass)
	};

static int sstfb_ioctl(struct fb_info *info, unsigned int cmd,
			unsigned long arg)
{
	struct sstfb_par *par;
	u32 val;

	switch (cmd) {
	/* set/get VGA pass_through mode */
	case SSTFB_SET_VGAPASS:
		if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
			return -EFAULT;
		sstfb_setvgapass(info, val);
		return 0;
	case SSTFB_GET_VGAPASS:
		par = info->par;
		val = par->vgapass;
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			return -EFAULT;
		return 0;
	}

	return -EINVAL;
}


/*
 * Screen-to-Screen BitBlt 2D command (for the bmove fb op.) - Voodoo2 only
 */
#if 0
static void sstfb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct sstfb_par *par = info->par;
	u32 stride = info->fix.line_length;
   
	if (!IS_VOODOO2(par))
		return;

	sst_write(BLTSRCBASEADDR, 0);
	sst_write(BLTDSTBASEADDR, 0);
	sst_write(BLTROP, BLTROP_COPY);
	sst_write(BLTXYSTRIDES, stride | (stride << 16));
	sst_write(BLTSRCXY, area->sx | (area->sy << 16));
	sst_write(BLTDSTXY, area->dx | (area->dy << 16));
	sst_write(BLTSIZE, area->width | (area->height << 16));
	sst_write(BLTCOMMAND, BLT_SCR2SCR_BITBLT | LAUNCH_BITBLT |
		(BLT_16BPP_FMT << 3) /* | BIT(14) */ | BIT(15) );
	sst_wait_idle();
}
#endif


/*
 * FillRect 2D command (solidfill or invert (via ROP_XOR)) - Voodoo2 only
 */
#if 0
static void sstfb_fillrect(struct fb_info *info, const struct fb_fillrect *rect) 
{
	struct sstfb_par *par = info->par;
	u32 stride = info->fix.line_length;

	if (!IS_VOODOO2(par))
		return;
   	
	sst_write(BLTCLIPX, info->var.xres);
	sst_write(BLTCLIPY, info->var.yres);
	
	sst_write(BLTDSTBASEADDR, 0);
	sst_write(BLTCOLOR, rect->color);
	sst_write(BLTROP, rect->rop == ROP_COPY ? BLTROP_COPY : BLTROP_XOR);
	sst_write(BLTXYSTRIDES, stride | (stride << 16));
	sst_write(BLTDSTXY, rect->dx | (rect->dy << 16));
	sst_write(BLTSIZE, rect->width | (rect->height << 16));
	sst_write(BLTCOMMAND, BLT_RECFILL_BITBLT | LAUNCH_BITBLT
		 | (BLT_16BPP_FMT << 3) /* | BIT(14) */ | BIT(15) | BIT(16) );
	sst_wait_idle();
}
#endif



/* 
 * get lfb size 
 */
static int __devinit sst_get_memsize(struct fb_info *info, __u32 *memsize)
{
	u8 __iomem *fbbase_virt = info->screen_base;

	/* force memsize */
	if (mem >= 1  && mem <= 4) {
		*memsize = (mem * 0x100000);
		printk(KERN_INFO "supplied memsize: %#x\n", *memsize);
		return 1;
	}

	writel(0xdeadbeef, fbbase_virt);
	writel(0xdeadbeef, fbbase_virt+0x100000);
	writel(0xdeadbeef, fbbase_virt+0x200000);
	f_ddprintk("0MB: %#x, 1MB: %#x, 2MB: %#x\n",
	           readl(fbbase_virt), readl(fbbase_virt + 0x100000),
	           readl(fbbase_virt + 0x200000));

	writel(0xabcdef01, fbbase_virt);

	f_ddprintk("0MB: %#x, 1MB: %#x, 2MB: %#x\n",
	           readl(fbbase_virt), readl(fbbase_virt + 0x100000),
	           readl(fbbase_virt + 0x200000));

	/* checks for 4mb lfb, then 2, then defaults to 1 */
	if (readl(fbbase_virt + 0x200000) == 0xdeadbeef)
		*memsize = 0x400000;
	else if (readl(fbbase_virt + 0x100000) == 0xdeadbeef)
		*memsize = 0x200000;
	else
		*memsize = 0x100000;
	f_ddprintk("detected memsize: %dMB\n", *memsize >> 20);
	return 1;
}


/* 
 * DAC detection routines 
 */

/* fbi should be idle, and fifo emty and mem disabled */
/* supposed to detect AT&T ATT20C409 and Ti TVP3409 ramdacs */

static int __devinit sst_detect_att(struct fb_info *info)
{
	struct sstfb_par *par = info->par;
	int i, mir, dir;

	for (i = 0; i < 3; i++) {
		sst_dac_write(DACREG_WMA, 0); 	/* backdoor */
		sst_dac_read(DACREG_RMR);	/* read 4 times RMR */
		sst_dac_read(DACREG_RMR);
		sst_dac_read(DACREG_RMR);
		sst_dac_read(DACREG_RMR);
		/* the fifth time,  CR0 is read */
		sst_dac_read(DACREG_RMR);
		/* the 6th, manufacturer id register */
		mir = sst_dac_read(DACREG_RMR);
		/*the 7th, device ID register */
		dir = sst_dac_read(DACREG_RMR);
		f_ddprintk("mir: %#x, dir: %#x\n", mir, dir);
		if (mir == DACREG_MIR_ATT && dir == DACREG_DIR_ATT) {
			return 1;
		}
	}
	return 0;
}

static int __devinit sst_detect_ti(struct fb_info *info)
{
	struct sstfb_par *par = info->par;
	int i, mir, dir;

	for (i = 0; i<3; i++) {
		sst_dac_write(DACREG_WMA, 0); 	/* backdoor */
		sst_dac_read(DACREG_RMR);	/* read 4 times RMR */
		sst_dac_read(DACREG_RMR);
		sst_dac_read(DACREG_RMR);
		sst_dac_read(DACREG_RMR);
		/* the fifth time,  CR0 is read */
		sst_dac_read(DACREG_RMR);
		/* the 6th, manufacturer id register */
		mir = sst_dac_read(DACREG_RMR);
		/*the 7th, device ID register */
		dir = sst_dac_read(DACREG_RMR);
		f_ddprintk("mir: %#x, dir: %#x\n", mir, dir);
		if ((mir == DACREG_MIR_TI ) && (dir == DACREG_DIR_TI)) {
			return 1;
		}
	}
	return 0;
}

/*
 * try to detect ICS5342  ramdac
 * we get the 1st byte (M value) of preset f1,f7 and fB
 * why those 3 ? mmmh... for now, i'll do it the glide way...
 * and ask questions later. anyway, it seems that all the freq registers are
 * realy at their default state (cf specs) so i ask again, why those 3 regs ?
 * mmmmh.. it seems that's much more ugly than i thought. we use f0 and fA for
 * pll programming, so in fact, we *hope* that the f1, f7 & fB won't be
 * touched...
 * is it realy safe ? how can i reset this ramdac ? geee...
 */
static int __devinit sst_detect_ics(struct fb_info *info)
{
	struct sstfb_par *par = info->par;
	int m_clk0_1, m_clk0_7, m_clk1_b;
	int n_clk0_1, n_clk0_7, n_clk1_b;
	int i;

	for (i = 0; i<5; i++ ) {
		sst_dac_write(DACREG_ICS_PLLRMA, 0x1);	/* f1 */
		m_clk0_1 = sst_dac_read(DACREG_ICS_PLLDATA);
		n_clk0_1 = sst_dac_read(DACREG_ICS_PLLDATA);
		sst_dac_write(DACREG_ICS_PLLRMA, 0x7);	/* f7 */
		m_clk0_7 = sst_dac_read(DACREG_ICS_PLLDATA);
		n_clk0_7 = sst_dac_read(DACREG_ICS_PLLDATA);
		sst_dac_write(DACREG_ICS_PLLRMA, 0xb);	/* fB */
		m_clk1_b= sst_dac_read(DACREG_ICS_PLLDATA);
		n_clk1_b= sst_dac_read(DACREG_ICS_PLLDATA);
		f_ddprintk("m_clk0_1: %#x, m_clk0_7: %#x, m_clk1_b: %#x\n",
			m_clk0_1, m_clk0_7, m_clk1_b);
		f_ddprintk("n_clk0_1: %#x, n_clk0_7: %#x, n_clk1_b: %#x\n",
			n_clk0_1, n_clk0_7, n_clk1_b);
		if ((   m_clk0_1 == DACREG_ICS_PLL_CLK0_1_INI)
		    && (m_clk0_7 == DACREG_ICS_PLL_CLK0_7_INI)
		    && (m_clk1_b == DACREG_ICS_PLL_CLK1_B_INI)) {
			return 1;
		}
	}
	return 0;
}


/*
 * gfx, video, pci fifo should be reset, dram refresh disabled
 * see detect_dac
 */

static int sst_set_pll_att_ti(struct fb_info *info, 
		const struct pll_timing *t, const int clock)
{
	struct sstfb_par *par = info->par;
	u8 cr0, cc;

	/* enable indexed mode */
	sst_dac_write(DACREG_WMA, 0); 	/* backdoor */
	sst_dac_read(DACREG_RMR);	/* 1 time:  RMR */
	sst_dac_read(DACREG_RMR);	/* 2 RMR */
	sst_dac_read(DACREG_RMR);	/* 3 //  */
	sst_dac_read(DACREG_RMR);	/* 4 //  */
	cr0 = sst_dac_read(DACREG_RMR);	/* 5 CR0 */

	sst_dac_write(DACREG_WMA, 0);
	sst_dac_read(DACREG_RMR);
	sst_dac_read(DACREG_RMR);
	sst_dac_read(DACREG_RMR);
	sst_dac_read(DACREG_RMR);
	sst_dac_write(DACREG_RMR, (cr0 & 0xf0)
	              | DACREG_CR0_EN_INDEXED
	              | DACREG_CR0_8BIT
	              | DACREG_CR0_PWDOWN );
	/* so, now we are in indexed mode . dunno if its common, but
	   i find this way of doing things a little bit weird :p */

	udelay(300);
	cc = dac_i_read(DACREG_CC_I);
	switch (clock) {
	case VID_CLOCK:
		dac_i_write(DACREG_AC0_I, t->m);
		dac_i_write(DACREG_AC1_I, t->p << 6 | t->n);
		dac_i_write(DACREG_CC_I,
		            (cc & 0x0f) | DACREG_CC_CLKA | DACREG_CC_CLKA_C);
		break;
	case GFX_CLOCK:
		dac_i_write(DACREG_BD0_I, t->m);
		dac_i_write(DACREG_BD1_I, t->p << 6 | t->n);
		dac_i_write(DACREG_CC_I,
		            (cc & 0xf0) | DACREG_CC_CLKB | DACREG_CC_CLKB_D);
		break;
	default:
		dprintk("%s: wrong clock code '%d'\n",
		        __FUNCTION__, clock);
		return 0;
		}
	udelay(300);

	/* power up the dac & return to "normal" non-indexed mode */
	dac_i_write(DACREG_CR0_I,
	            cr0 & ~DACREG_CR0_PWDOWN & ~DACREG_CR0_EN_INDEXED);
	return 1;
}

static int sst_set_pll_ics(struct fb_info *info,
		const struct pll_timing *t, const int clock)
{
	struct sstfb_par *par = info->par;
	u8 pll_ctrl;

	sst_dac_write(DACREG_ICS_PLLRMA, DACREG_ICS_PLL_CTRL);
	pll_ctrl = sst_dac_read(DACREG_ICS_PLLDATA);
	switch(clock) {
	case VID_CLOCK:
		sst_dac_write(DACREG_ICS_PLLWMA, 0x0);	/* CLK0, f0 */
		sst_dac_write(DACREG_ICS_PLLDATA, t->m);
		sst_dac_write(DACREG_ICS_PLLDATA, t->p << 5 | t->n);
		/* selects freq f0 for clock 0 */
		sst_dac_write(DACREG_ICS_PLLWMA, DACREG_ICS_PLL_CTRL);
		sst_dac_write(DACREG_ICS_PLLDATA,
		              (pll_ctrl & 0xd8)
		              | DACREG_ICS_CLK0
		              | DACREG_ICS_CLK0_0);
		break;
	case GFX_CLOCK :
		sst_dac_write(DACREG_ICS_PLLWMA, 0xa);	/* CLK1, fA */
		sst_dac_write(DACREG_ICS_PLLDATA, t->m);
		sst_dac_write(DACREG_ICS_PLLDATA, t->p << 5 | t->n);
		/* selects freq fA for clock 1 */
		sst_dac_write(DACREG_ICS_PLLWMA, DACREG_ICS_PLL_CTRL);
		sst_dac_write(DACREG_ICS_PLLDATA,
		              (pll_ctrl & 0xef) | DACREG_ICS_CLK1_A);
		break;
	default:
		dprintk("%s: wrong clock code '%d'\n",
		        __FUNCTION__, clock);
		return 0;
		}
	udelay(300);
	return 1;
}

static void sst_set_vidmod_att_ti(struct fb_info *info, const int bpp)
{
	struct sstfb_par *par = info->par;
	u8 cr0;

	sst_dac_write(DACREG_WMA, 0); 	/* backdoor */
	sst_dac_read(DACREG_RMR);	/* read 4 times RMR */
	sst_dac_read(DACREG_RMR);
	sst_dac_read(DACREG_RMR);
	sst_dac_read(DACREG_RMR);
	/* the fifth time,  CR0 is read */
	cr0 = sst_dac_read(DACREG_RMR);

	sst_dac_write(DACREG_WMA, 0); 	/* backdoor */
	sst_dac_read(DACREG_RMR);	/* read 4 times RMR */
	sst_dac_read(DACREG_RMR);
	sst_dac_read(DACREG_RMR);
	sst_dac_read(DACREG_RMR);
	/* cr0 */
	switch(bpp) {
	case 16:
		sst_dac_write(DACREG_RMR, (cr0 & 0x0f) | DACREG_CR0_16BPP);
		break;
	default:
		dprintk("%s: bad depth '%u'\n", __FUNCTION__, bpp);
		break;
	}
}

static void sst_set_vidmod_ics(struct fb_info *info, const int bpp)
{
	struct sstfb_par *par = info->par;

	switch(bpp) {
	case 16:
		sst_dac_write(DACREG_ICS_CMD, DACREG_ICS_CMD_16BPP);
		break;
	default:
		dprintk("%s: bad depth '%u'\n", __FUNCTION__, bpp);
		break;
	}
}

/*
 * detect dac type
 * prerequisite : write to FbiInitx enabled, video and fbi and pci fifo reset,
 * dram refresh disabled, FbiInit remaped.
 * TODO: mmh.. maybe i shoud put the "prerequisite" in the func ...
 */


static struct dac_switch dacs[] __devinitdata = {
	{	.name		= "TI TVP3409",
		.detect		= sst_detect_ti,
		.set_pll	= sst_set_pll_att_ti,
		.set_vidmod	= sst_set_vidmod_att_ti },

	{	.name		= "AT&T ATT20C409",
		.detect		= sst_detect_att,
		.set_pll	= sst_set_pll_att_ti,
		.set_vidmod	= sst_set_vidmod_att_ti },
	{	.name		= "ICS ICS5342",
		.detect		= sst_detect_ics,
		.set_pll	= sst_set_pll_ics,
		.set_vidmod	= sst_set_vidmod_ics },
};

static int __devinit sst_detect_dactype(struct fb_info *info, struct sstfb_par *par)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(dacs); i++) {
		ret = dacs[i].detect(info);
		if (ret)
			break;
	}
	if (!ret)
		return 0;
	f_dprintk("%s found %s\n", __FUNCTION__, dacs[i].name);
	par->dac_sw = dacs[i];
	return 1;
}

/*
 * Internal Routines
 */
static int __devinit sst_init(struct fb_info *info, struct sstfb_par *par)
{
	u32 fbiinit0, fbiinit1, fbiinit4;
	struct pci_dev *dev = par->dev;
	struct pll_timing gfx_timings;
	struct sst_spec *spec;
	int Fout;
	int gfx_clock;

	spec = &voodoo_spec[par->type];
	f_ddprintk(" fbiinit0   fbiinit1   fbiinit2   fbiinit3   fbiinit4  "
	           " fbiinit6\n");
	f_ddprintk("%0#10x %0#10x %0#10x %0#10x %0#10x %0#10x\n",
	            sst_read(FBIINIT0), sst_read(FBIINIT1), sst_read(FBIINIT2),
	            sst_read(FBIINIT3), sst_read(FBIINIT4), sst_read(FBIINIT6));
	/* disable video clock */
	pci_write_config_dword(dev, PCI_VCLK_DISABLE, 0);

	/* enable writing to init registers, disable pci fifo */
	pci_write_config_dword(dev, PCI_INIT_ENABLE, PCI_EN_INIT_WR);
	/* reset video */
	sst_set_bits(FBIINIT1, VIDEO_RESET);
	sst_wait_idle();
	/* reset gfx + pci fifo */
	sst_set_bits(FBIINIT0, FBI_RESET | FIFO_RESET);
	sst_wait_idle();

	/* unreset fifo */
	/*sst_unset_bits(FBIINIT0, FIFO_RESET);
	sst_wait_idle();*/
	/* unreset FBI */
	/*sst_unset_bits(FBIINIT0, FBI_RESET);
	sst_wait_idle();*/

	/* disable dram refresh */
	sst_unset_bits(FBIINIT2, EN_DRAM_REFRESH);
	sst_wait_idle();
	/* remap fbinit2/3 to dac */
	pci_write_config_dword(dev, PCI_INIT_ENABLE,
				PCI_EN_INIT_WR | PCI_REMAP_DAC );
	/* detect dac type */
	if (!sst_detect_dactype(info, par)) {
		printk(KERN_ERR "sstfb: unknown dac type.\n");
		//FIXME watch it: we are not in a safe state, bad bad bad.
		return 0;
	}

	/* set graphic clock */
	gfx_clock = spec->default_gfx_clock;
	if ((gfxclk >10 ) && (gfxclk < spec->max_gfxclk)) {
		printk(KERN_INFO "sstfb: Using supplied graphic freq : %dMHz\n", gfxclk);
		 gfx_clock = gfxclk *1000;
	} else if (gfxclk) {
		printk(KERN_WARNING "sstfb: %dMhz is way out of spec! Using default\n", gfxclk);
	}

	sst_calc_pll(gfx_clock, &Fout, &gfx_timings);
	par->dac_sw.set_pll(info, &gfx_timings, GFX_CLOCK);

	/* disable fbiinit remap */
	pci_write_config_dword(dev, PCI_INIT_ENABLE,
	                       PCI_EN_INIT_WR| PCI_EN_FIFO_WR );
	/* defaults init registers */
	/* FbiInit0: unreset gfx, unreset fifo */
	fbiinit0 = FBIINIT0_DEFAULT;
	fbiinit1 = FBIINIT1_DEFAULT;
	fbiinit4 = FBIINIT4_DEFAULT;
	par->vgapass = vgapass;
	if (par->vgapass)
		fbiinit0 &= ~DIS_VGA_PASSTHROUGH;
	else
		fbiinit0 |= DIS_VGA_PASSTHROUGH;
	if (slowpci) {
		fbiinit1 |= SLOW_PCI_WRITES;
		fbiinit4 |= SLOW_PCI_READS;
	} else {
		fbiinit1 &= ~SLOW_PCI_WRITES;
		fbiinit4 &= ~SLOW_PCI_READS;
	}
	sst_write(FBIINIT0, fbiinit0);
	sst_wait_idle();
	sst_write(FBIINIT1, fbiinit1);
	sst_wait_idle();
	sst_write(FBIINIT2, FBIINIT2_DEFAULT);
	sst_wait_idle();
	sst_write(FBIINIT3, FBIINIT3_DEFAULT);
	sst_wait_idle();
	sst_write(FBIINIT4, fbiinit4);
	sst_wait_idle();
	if (IS_VOODOO2(par)) {
		sst_write(FBIINIT6, FBIINIT6_DEFAULT);
		sst_wait_idle();
	}

	pci_write_config_dword(dev, PCI_INIT_ENABLE, PCI_EN_FIFO_WR);
	pci_write_config_dword(dev, PCI_VCLK_ENABLE, 0);
	return 1;
}

static void  __devexit sst_shutdown(struct fb_info *info)
{
	struct sstfb_par *par = info->par;
	struct pci_dev *dev = par->dev;
	struct pll_timing gfx_timings;
	int Fout;

	/* reset video, gfx, fifo, disable dram + remap fbiinit2/3 */
	pci_write_config_dword(dev, PCI_INIT_ENABLE, PCI_EN_INIT_WR);
	sst_set_bits(FBIINIT1, VIDEO_RESET | EN_BLANKING);
	sst_unset_bits(FBIINIT2, EN_DRAM_REFRESH);
	sst_set_bits(FBIINIT0, FBI_RESET | FIFO_RESET);
	sst_wait_idle();
	pci_write_config_dword(dev, PCI_INIT_ENABLE,
	                       PCI_EN_INIT_WR | PCI_REMAP_DAC);
	/* set 20Mhz gfx clock */
	sst_calc_pll(20000, &Fout, &gfx_timings);
	par->dac_sw.set_pll(info, &gfx_timings, GFX_CLOCK);
	/* TODO maybe shutdown the dac, vrefresh and so on... */
	pci_write_config_dword(dev, PCI_INIT_ENABLE,
	                       PCI_EN_INIT_WR);
	sst_unset_bits(FBIINIT0, FBI_RESET | FIFO_RESET | DIS_VGA_PASSTHROUGH);
	pci_write_config_dword(dev, PCI_VCLK_DISABLE,0);
	/* maybe keep fbiinit* and PCI_INIT_enable in the fb_info struct
	 * from start ? */
	pci_write_config_dword(dev, PCI_INIT_ENABLE, 0);

}

/*
 * Interface to the world
 */
static int  __devinit sstfb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt) continue;

		f_ddprintk("option %s\n", this_opt);

		if (!strcmp(this_opt, "vganopass"))
			vgapass = 0;
		else if (!strcmp(this_opt, "vgapass"))
			vgapass = 1;
		else if (!strcmp(this_opt, "clipping"))
		        clipping = 1;
		else if (!strcmp(this_opt, "noclipping"))
		        clipping = 0;
		else if (!strcmp(this_opt, "fastpci"))
		        slowpci = 0;
		else if (!strcmp(this_opt, "slowpci"))
		        slowpci = 1;
		else if (!strncmp(this_opt, "mem:",4))
			mem = simple_strtoul (this_opt+4, NULL, 0);
		else if (!strncmp(this_opt, "gfxclk:",7))
			gfxclk = simple_strtoul (this_opt+7, NULL, 0);
		else
			mode_option = this_opt;
	}
	return 0;
}


static struct fb_ops sstfb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= sstfb_check_var,
	.fb_set_par	= sstfb_set_par,
	.fb_setcolreg	= sstfb_setcolreg,
	.fb_fillrect	= cfb_fillrect, /* sstfb_fillrect */
	.fb_copyarea	= cfb_copyarea, /* sstfb_copyarea */
	.fb_imageblit	= cfb_imageblit,
	.fb_ioctl	= sstfb_ioctl,
};

static int __devinit sstfb_probe(struct pci_dev *pdev,
			const struct pci_device_id *id)
{
	struct fb_info *info;
	struct fb_fix_screeninfo *fix;
	struct sstfb_par *par;
	struct sst_spec *spec;
	int err;

	/* Enable device in PCI config. */
	if ((err=pci_enable_device(pdev))) {
		printk(KERN_ERR "cannot enable device\n");
		return err;
	}

	/* Allocate the fb and par structures.  */
	info = framebuffer_alloc(sizeof(struct sstfb_par), &pdev->dev);
	if (!info)
		return -ENOMEM;

	pci_set_drvdata(pdev, info);
	
	par  = info->par;
	fix  = &info->fix;
	
	par->type = id->driver_data;
	spec = &voodoo_spec[par->type];
	f_ddprintk("found device : %s\n", spec->name);

	par->dev = pdev;
	par->revision = pdev->revision;

	fix->mmio_start = pci_resource_start(pdev,0);
	fix->mmio_len	= 0x400000;
	fix->smem_start = fix->mmio_start + 0x400000;

	if (!request_mem_region(fix->mmio_start, fix->mmio_len, "sstfb MMIO")) {
		printk(KERN_ERR "sstfb: cannot reserve mmio memory\n");
		goto fail_mmio_mem;
	}

	if (!request_mem_region(fix->smem_start, 0x400000,"sstfb FB")) {
		printk(KERN_ERR "sstfb: cannot reserve fb memory\n");
		goto fail_fb_mem;
	}

	par->mmio_vbase = ioremap_nocache(fix->mmio_start,
					fix->mmio_len);
	if (!par->mmio_vbase) {
		printk(KERN_ERR "sstfb: cannot remap register area %#lx\n",
		        fix->mmio_start);
		goto fail_mmio_remap;
	}
	info->screen_base = ioremap_nocache(fix->smem_start, 0x400000);
	if (!info->screen_base) {
		printk(KERN_ERR "sstfb: cannot remap framebuffer %#lx\n",
		        fix->smem_start);
		goto fail_fb_remap;
	}

	if (!sst_init(info, par)) {
		printk(KERN_ERR "sstfb: Init failed\n");
		goto fail;
	}
	sst_get_memsize(info, &fix->smem_len);
	strlcpy(fix->id, spec->name, sizeof(fix->id));

	printk(KERN_INFO "%s (revision %d) with %s dac\n",
		fix->id, par->revision, par->dac_sw.name);
	printk(KERN_INFO "framebuffer at %#lx, mapped to 0x%p, size %dMB\n",
	        fix->smem_start, info->screen_base,
	        fix->smem_len >> 20);

	f_ddprintk("regbase_virt: %#lx\n", par->mmio_vbase);
	f_ddprintk("membase_phys: %#lx\n", fix->smem_start);
	f_ddprintk("fbbase_virt: %p\n", info->screen_base);

	info->flags	= FBINFO_DEFAULT;
	info->fbops	= &sstfb_ops;
	info->pseudo_palette = par->palette;

	fix->type	= FB_TYPE_PACKED_PIXELS;
	fix->visual	= FB_VISUAL_TRUECOLOR;
	fix->accel	= FB_ACCEL_NONE;  /* FIXME */
	/*
	 * According to the specs, the linelength must be of 1024 *pixels*
	 * and the 24bpp mode is in fact a 32 bpp mode (and both are in
	 * fact dithered to 16bit).
	 */
	fix->line_length = 2048; /* default value, for 24 or 32bit: 4096 */
	
	fb_find_mode(&info->var, info, mode_option, NULL, 0, NULL, 16);

	if (sstfb_check_var(&info->var, info)) {
		printk(KERN_ERR "sstfb: invalid video mode.\n");
		goto fail;
	}

	if (sstfb_set_par(info)) {
		printk(KERN_ERR "sstfb: can't set default video mode.\n");
		goto fail;
	}
	
	fb_alloc_cmap(&info->cmap, 256, 0);

	/* register fb */
	info->device = &pdev->dev;
	if (register_framebuffer(info) < 0) {
		printk(KERN_ERR "sstfb: can't register framebuffer.\n");
		goto fail;
	}

	sstfb_clear_screen(info);

	if (device_create_file(info->dev, &device_attrs[0]))
		printk(KERN_WARNING "sstfb: can't create sysfs entry.\n");


	printk(KERN_INFO "fb%d: %s frame buffer device at 0x%p\n",
	       info->node, fix->id, info->screen_base);

	return 0;

fail:
	fb_dealloc_cmap(&info->cmap);
	iounmap(info->screen_base);
fail_fb_remap:
	iounmap(par->mmio_vbase);
fail_mmio_remap:
	release_mem_region(fix->smem_start, 0x400000);
fail_fb_mem:
	release_mem_region(fix->mmio_start, info->fix.mmio_len);
fail_mmio_mem:
	framebuffer_release(info);
	return -ENXIO; 	/* no voodoo detected */
}

static void __devexit sstfb_remove(struct pci_dev *pdev)
{
	struct sstfb_par *par;
	struct fb_info *info;

	info = pci_get_drvdata(pdev);
	par = info->par;
	
	device_remove_file(info->dev, &device_attrs[0]);
	sst_shutdown(info);
	iounmap(info->screen_base);
	iounmap(par->mmio_vbase);
	release_mem_region(info->fix.smem_start, 0x400000);
	release_mem_region(info->fix.mmio_start, info->fix.mmio_len);
	fb_dealloc_cmap(&info->cmap);
	unregister_framebuffer(info);
	framebuffer_release(info);
}


static const struct pci_device_id sstfb_id_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_3DFX, PCI_DEVICE_ID_3DFX_VOODOO ),
		.driver_data = ID_VOODOO1, },
	{ PCI_DEVICE(PCI_VENDOR_ID_3DFX, PCI_DEVICE_ID_3DFX_VOODOO2),
		.driver_data = ID_VOODOO2, },
	{ 0 },
};

static struct pci_driver sstfb_driver = {
	.name		= "sstfb",
	.id_table	= sstfb_id_tbl,
	.probe		= sstfb_probe,
	.remove		= __devexit_p(sstfb_remove),
};


static int __devinit sstfb_init(void)
{
	char *option = NULL;

	if (fb_get_options("sstfb", &option))
		return -ENODEV;
	sstfb_setup(option);

	return pci_register_driver(&sstfb_driver);
}

static void __devexit sstfb_exit(void)
{
	pci_unregister_driver(&sstfb_driver);
}


module_init(sstfb_init);
module_exit(sstfb_exit);

MODULE_AUTHOR("(c) 2000,2002 Ghozlane Toumi <gtoumi@laposte.net>");
MODULE_DESCRIPTION("FBDev driver for 3dfx Voodoo Graphics and Voodoo2 based video boards");
MODULE_LICENSE("GPL");

module_param(mem, int, 0);
MODULE_PARM_DESC(mem, "Size of frame buffer memory in MB (1, 2, 4 MB, default=autodetect)");
module_param(vgapass, bool, 0);
MODULE_PARM_DESC(vgapass, "Enable VGA PassThrough mode (0 or 1) (default=0)");
module_param(clipping, bool, 0);
MODULE_PARM_DESC(clipping, "Enable clipping (slower, safer) (0 or 1) (default=1)");
module_param(gfxclk, int, 0);
MODULE_PARM_DESC(gfxclk, "Force graphic chip frequency in MHz. DANGEROUS. (default=auto)");
module_param(slowpci, bool, 0);
MODULE_PARM_DESC(slowpci, "Uses slow PCI settings (0 or 1) (default=0)");
module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option, "Initial video mode (default=" DEFAULT_VIDEO_MODE ")");

