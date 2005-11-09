/*
 * linux/drivers/video/neofb.c -- NeoMagic Framebuffer Driver
 *
 * Copyright (c) 2001-2002  Denis Oliver Kropp <dok@directfb.org>
 *
 *
 * Card specific code is based on XFree86's neomagic driver.
 * Framebuffer framework code is based on code of cyber2000fb.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 *
 * 0.4.1
 *  - Cosmetic changes (dok)
 *
 * 0.4
 *  - Toshiba Libretto support, allow modes larger than LCD size if
 *    LCD is disabled, keep BIOS settings if internal/external display
 *    haven't been enabled explicitly
 *                          (Thomas J. Moore <dark@mama.indstate.edu>)
 *
 * 0.3.3
 *  - Porting over to new fbdev api. (jsimmons)
 *  
 * 0.3.2
 *  - got rid of all floating point (dok) 
 *
 * 0.3.1
 *  - added module license (dok)
 *
 * 0.3
 *  - hardware accelerated clear and move for 2200 and above (dok)
 *  - maximum allowed dotclock is handled now (dok)
 *
 * 0.2.1
 *  - correct panning after X usage (dok)
 *  - added module and kernel parameters (dok)
 *  - no stretching if external display is enabled (dok)
 *
 * 0.2
 *  - initial version (dok)
 *
 *
 * TODO
 * - ioctl for internal/external switching
 * - blanking
 * - 32bit depth support, maybe impossible
 * - disable pan-on-sync, need specs
 *
 * BUGS
 * - white margin on bootup like with tdfxfb (colormap problem?)
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/init.h>
#ifdef CONFIG_TOSHIBA
#include <linux/toshiba.h>
extern int tosh_smm(SMMRegisters *regs);
#endif

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#include <video/vga.h>
#include <video/neomagic.h>

#define NEOFB_VERSION "0.4.2"

/* --------------------------------------------------------------------- */

static int internal;
static int external;
static int libretto;
static int nostretch;
static int nopciburst;
static char *mode_option __devinitdata = NULL;

#ifdef MODULE

MODULE_AUTHOR("(c) 2001-2002  Denis Oliver Kropp <dok@convergence.de>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FBDev driver for NeoMagic PCI Chips");
module_param(internal, bool, 0);
MODULE_PARM_DESC(internal, "Enable output on internal LCD Display.");
module_param(external, bool, 0);
MODULE_PARM_DESC(external, "Enable output on external CRT.");
module_param(libretto, bool, 0);
MODULE_PARM_DESC(libretto, "Force Libretto 100/110 800x480 LCD.");
module_param(nostretch, bool, 0);
MODULE_PARM_DESC(nostretch,
		 "Disable stretching of modes smaller than LCD.");
module_param(nopciburst, bool, 0);
MODULE_PARM_DESC(nopciburst, "Disable PCI burst mode.");
module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option, "Preferred video mode ('640x480-8@60', etc)");

#endif


/* --------------------------------------------------------------------- */

static biosMode bios8[] = {
	{320, 240, 0x40},
	{300, 400, 0x42},
	{640, 400, 0x20},
	{640, 480, 0x21},
	{800, 600, 0x23},
	{1024, 768, 0x25},
};

static biosMode bios16[] = {
	{320, 200, 0x2e},
	{320, 240, 0x41},
	{300, 400, 0x43},
	{640, 480, 0x31},
	{800, 600, 0x34},
	{1024, 768, 0x37},
};

static biosMode bios24[] = {
	{640, 480, 0x32},
	{800, 600, 0x35},
	{1024, 768, 0x38}
};

#ifdef NO_32BIT_SUPPORT_YET
/* FIXME: guessed values, wrong */
static biosMode bios32[] = {
	{640, 480, 0x33},
	{800, 600, 0x36},
	{1024, 768, 0x39}
};
#endif

static inline void write_le32(int regindex, u32 val, const struct neofb_par *par)
{
	writel(val, par->neo2200 + par->cursorOff + regindex);
}

static int neoFindMode(int xres, int yres, int depth)
{
	int xres_s;
	int i, size;
	biosMode *mode;

	switch (depth) {
	case 8:
		size = sizeof(bios8) / sizeof(biosMode);
		mode = bios8;
		break;
	case 16:
		size = sizeof(bios16) / sizeof(biosMode);
		mode = bios16;
		break;
	case 24:
		size = sizeof(bios24) / sizeof(biosMode);
		mode = bios24;
		break;
#ifdef NO_32BIT_SUPPORT_YET
	case 32:
		size = sizeof(bios32) / sizeof(biosMode);
		mode = bios32;
		break;
#endif
	default:
		return 0;
	}

	for (i = 0; i < size; i++) {
		if (xres <= mode[i].x_res) {
			xres_s = mode[i].x_res;
			for (; i < size; i++) {
				if (mode[i].x_res != xres_s)
					return mode[i - 1].mode;
				if (yres <= mode[i].y_res)
					return mode[i].mode;
			}
		}
	}
	return mode[size - 1].mode;
}

/*
 * neoCalcVCLK --
 *
 * Determine the closest clock frequency to the one requested.
 */
#define REF_FREQ 0xe517		/* 14.31818 in 20.12 fixed point */
#define MAX_N 127
#define MAX_D 31
#define MAX_F 1

static void neoCalcVCLK(const struct fb_info *info,
			struct neofb_par *par, long freq)
{
	int n, d, f;
	int n_best = 0, d_best = 0, f_best = 0;
	long f_best_diff = (0x7ffff << 12);	/* 20.12 */
	long f_target = (freq << 12) / 1000;	/* 20.12 */

	for (f = 0; f <= MAX_F; f++)
		for (n = 0; n <= MAX_N; n++)
			for (d = 0; d <= MAX_D; d++) {
				long f_out;	/* 20.12 */
				long f_diff;	/* 20.12 */

				f_out =
				    ((((n + 1) << 12) / ((d +
							  1) *
							 (1 << f))) >> 12)
				    * REF_FREQ;
				f_diff = abs(f_out - f_target);
				if (f_diff < f_best_diff) {
					f_best_diff = f_diff;
					n_best = n;
					d_best = d;
					f_best = f;
				}
			}

	if (info->fix.accel == FB_ACCEL_NEOMAGIC_NM2200 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2230 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2360 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2380) {
		/* NOT_DONE:  We are trying the full range of the 2200 clock.
		   We should be able to try n up to 2047 */
		par->VCLK3NumeratorLow = n_best;
		par->VCLK3NumeratorHigh = (f_best << 7);
	} else
		par->VCLK3NumeratorLow = n_best | (f_best << 7);

	par->VCLK3Denominator = d_best;

#ifdef NEOFB_DEBUG
	printk("neoVCLK: f:%d NumLow=%d NumHi=%d Den=%d Df=%d\n",
	       f_target >> 12,
	       par->VCLK3NumeratorLow,
	       par->VCLK3NumeratorHigh,
	       par->VCLK3Denominator, f_best_diff >> 12);
#endif
}

/*
 * vgaHWInit --
 *      Handle the initialization, etc. of a screen.
 *      Return FALSE on failure.
 */

static int vgaHWInit(const struct fb_var_screeninfo *var,
		     const struct fb_info *info,
		     struct neofb_par *par, struct xtimings *timings)
{
	par->MiscOutReg = 0x23;

	if (!(timings->sync & FB_SYNC_HOR_HIGH_ACT))
		par->MiscOutReg |= 0x40;

	if (!(timings->sync & FB_SYNC_VERT_HIGH_ACT))
		par->MiscOutReg |= 0x80;

	/*
	 * Time Sequencer
	 */
	par->Sequencer[0] = 0x00;
	par->Sequencer[1] = 0x01;
	par->Sequencer[2] = 0x0F;
	par->Sequencer[3] = 0x00;	/* Font select */
	par->Sequencer[4] = 0x0E;	/* Misc */

	/*
	 * CRTC Controller
	 */
	par->CRTC[0] = (timings->HTotal >> 3) - 5;
	par->CRTC[1] = (timings->HDisplay >> 3) - 1;
	par->CRTC[2] = (timings->HDisplay >> 3) - 1;
	par->CRTC[3] = (((timings->HTotal >> 3) - 1) & 0x1F) | 0x80;
	par->CRTC[4] = (timings->HSyncStart >> 3);
	par->CRTC[5] = ((((timings->HTotal >> 3) - 1) & 0x20) << 2)
	    | (((timings->HSyncEnd >> 3)) & 0x1F);
	par->CRTC[6] = (timings->VTotal - 2) & 0xFF;
	par->CRTC[7] = (((timings->VTotal - 2) & 0x100) >> 8)
	    | (((timings->VDisplay - 1) & 0x100) >> 7)
	    | ((timings->VSyncStart & 0x100) >> 6)
	    | (((timings->VDisplay - 1) & 0x100) >> 5)
	    | 0x10 | (((timings->VTotal - 2) & 0x200) >> 4)
	    | (((timings->VDisplay - 1) & 0x200) >> 3)
	    | ((timings->VSyncStart & 0x200) >> 2);
	par->CRTC[8] = 0x00;
	par->CRTC[9] = (((timings->VDisplay - 1) & 0x200) >> 4) | 0x40;

	if (timings->dblscan)
		par->CRTC[9] |= 0x80;

	par->CRTC[10] = 0x00;
	par->CRTC[11] = 0x00;
	par->CRTC[12] = 0x00;
	par->CRTC[13] = 0x00;
	par->CRTC[14] = 0x00;
	par->CRTC[15] = 0x00;
	par->CRTC[16] = timings->VSyncStart & 0xFF;
	par->CRTC[17] = (timings->VSyncEnd & 0x0F) | 0x20;
	par->CRTC[18] = (timings->VDisplay - 1) & 0xFF;
	par->CRTC[19] = var->xres_virtual >> 4;
	par->CRTC[20] = 0x00;
	par->CRTC[21] = (timings->VDisplay - 1) & 0xFF;
	par->CRTC[22] = (timings->VTotal - 1) & 0xFF;
	par->CRTC[23] = 0xC3;
	par->CRTC[24] = 0xFF;

	/*
	 * are these unnecessary?
	 * vgaHWHBlankKGA(mode, regp, 0, KGA_FIX_OVERSCAN | KGA_ENABLE_ON_ZERO);
	 * vgaHWVBlankKGA(mode, regp, 0, KGA_FIX_OVERSCAN | KGA_ENABLE_ON_ZERO);
	 */

	/*
	 * Graphics Display Controller
	 */
	par->Graphics[0] = 0x00;
	par->Graphics[1] = 0x00;
	par->Graphics[2] = 0x00;
	par->Graphics[3] = 0x00;
	par->Graphics[4] = 0x00;
	par->Graphics[5] = 0x40;
	par->Graphics[6] = 0x05;	/* only map 64k VGA memory !!!! */
	par->Graphics[7] = 0x0F;
	par->Graphics[8] = 0xFF;


	par->Attribute[0] = 0x00;	/* standard colormap translation */
	par->Attribute[1] = 0x01;
	par->Attribute[2] = 0x02;
	par->Attribute[3] = 0x03;
	par->Attribute[4] = 0x04;
	par->Attribute[5] = 0x05;
	par->Attribute[6] = 0x06;
	par->Attribute[7] = 0x07;
	par->Attribute[8] = 0x08;
	par->Attribute[9] = 0x09;
	par->Attribute[10] = 0x0A;
	par->Attribute[11] = 0x0B;
	par->Attribute[12] = 0x0C;
	par->Attribute[13] = 0x0D;
	par->Attribute[14] = 0x0E;
	par->Attribute[15] = 0x0F;
	par->Attribute[16] = 0x41;
	par->Attribute[17] = 0xFF;
	par->Attribute[18] = 0x0F;
	par->Attribute[19] = 0x00;
	par->Attribute[20] = 0x00;
	return 0;
}

static void vgaHWLock(struct vgastate *state)
{
	/* Protect CRTC[0-7] */
	vga_wcrt(state->vgabase, 0x11, vga_rcrt(state->vgabase, 0x11) | 0x80);
}

static void vgaHWUnlock(void)
{
	/* Unprotect CRTC[0-7] */
	vga_wcrt(NULL, 0x11, vga_rcrt(NULL, 0x11) & ~0x80);
}

static void neoLock(struct vgastate *state)
{
	vga_wgfx(state->vgabase, 0x09, 0x00);
	vgaHWLock(state);
}

static void neoUnlock(void)
{
	vgaHWUnlock();
	vga_wgfx(NULL, 0x09, 0x26);
}

/*
 * VGA Palette management
 */
static int paletteEnabled = 0;

static inline void VGAenablePalette(void)
{
	vga_r(NULL, VGA_IS1_RC);
	vga_w(NULL, VGA_ATT_W, 0x00);
	paletteEnabled = 1;
}

static inline void VGAdisablePalette(void)
{
	vga_r(NULL, VGA_IS1_RC);
	vga_w(NULL, VGA_ATT_W, 0x20);
	paletteEnabled = 0;
}

static inline void VGAwATTR(u8 index, u8 value)
{
	if (paletteEnabled)
		index &= ~0x20;
	else
		index |= 0x20;

	vga_r(NULL, VGA_IS1_RC);
	vga_wattr(NULL, index, value);
}

static void vgaHWProtect(int on)
{
	unsigned char tmp;

	if (on) {
		/*
		 * Turn off screen and disable sequencer.
		 */
		tmp = vga_rseq(NULL, 0x01);
		vga_wseq(NULL, 0x00, 0x01);		/* Synchronous Reset */
		vga_wseq(NULL, 0x01, tmp | 0x20);	/* disable the display */

		VGAenablePalette();
	} else {
		/*
		 * Reenable sequencer, then turn on screen.
		 */
		tmp = vga_rseq(NULL, 0x01);
		vga_wseq(NULL, 0x01, tmp & ~0x20);	/* reenable display */
		vga_wseq(NULL, 0x00, 0x03);		/* clear synchronousreset */

		VGAdisablePalette();
	}
}

static void vgaHWRestore(const struct fb_info *info,
			 const struct neofb_par *par)
{
	int i;

	vga_w(NULL, VGA_MIS_W, par->MiscOutReg);

	for (i = 1; i < 5; i++)
		vga_wseq(NULL, i, par->Sequencer[i]);

	/* Ensure CRTC registers 0-7 are unlocked by clearing bit 7 or CRTC[17] */
	vga_wcrt(NULL, 17, par->CRTC[17] & ~0x80);

	for (i = 0; i < 25; i++)
		vga_wcrt(NULL, i, par->CRTC[i]);

	for (i = 0; i < 9; i++)
		vga_wgfx(NULL, i, par->Graphics[i]);

	VGAenablePalette();

	for (i = 0; i < 21; i++)
		VGAwATTR(i, par->Attribute[i]);

	VGAdisablePalette();
}


/* -------------------- Hardware specific routines ------------------------- */

/*
 * Hardware Acceleration for Neo2200+
 */
static inline int neo2200_sync(struct fb_info *info)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	int waitcycles;

	while (readl(&par->neo2200->bltStat) & 1)
		waitcycles++;
	return 0;
}

static inline void neo2200_wait_fifo(struct fb_info *info,
				     int requested_fifo_space)
{
	//  ndev->neo.waitfifo_calls++;
	//  ndev->neo.waitfifo_sum += requested_fifo_space;

	/* FIXME: does not work
	   if (neo_fifo_space < requested_fifo_space)
	   {
	   neo_fifo_waitcycles++;

	   while (1)
	   {
	   neo_fifo_space = (neo2200->bltStat >> 8);
	   if (neo_fifo_space >= requested_fifo_space)
	   break;
	   }
	   }
	   else
	   {
	   neo_fifo_cache_hits++;
	   }

	   neo_fifo_space -= requested_fifo_space;
	 */

	neo2200_sync(info);
}

static inline void neo2200_accel_init(struct fb_info *info,
				      struct fb_var_screeninfo *var)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	Neo2200 __iomem *neo2200 = par->neo2200;
	u32 bltMod, pitch;

	neo2200_sync(info);

	switch (var->bits_per_pixel) {
	case 8:
		bltMod = NEO_MODE1_DEPTH8;
		pitch = var->xres_virtual;
		break;
	case 15:
	case 16:
		bltMod = NEO_MODE1_DEPTH16;
		pitch = var->xres_virtual * 2;
		break;
	case 24:
		bltMod = NEO_MODE1_DEPTH24;
		pitch = var->xres_virtual * 3;
		break;
	default:
		printk(KERN_ERR
		       "neofb: neo2200_accel_init: unexpected bits per pixel!\n");
		return;
	}

	writel(bltMod << 16, &neo2200->bltStat);
	writel((pitch << 16) | pitch, &neo2200->pitch);
}

/* --------------------------------------------------------------------- */

static int
neofb_open(struct fb_info *info, int user)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	int cnt = atomic_read(&par->ref_count);

	if (!cnt) {
		memset(&par->state, 0, sizeof(struct vgastate));
		par->state.flags = VGA_SAVE_MODE | VGA_SAVE_FONTS;
		save_vga(&par->state);
	}
	atomic_inc(&par->ref_count);
	return 0;
}

static int
neofb_release(struct fb_info *info, int user)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	int cnt = atomic_read(&par->ref_count);

	if (!cnt)
		return -EINVAL;
	if (cnt == 1) {
		restore_vga(&par->state);
	}
	atomic_dec(&par->ref_count);
	return 0;
}

static int
neofb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	unsigned int pixclock = var->pixclock;
	struct xtimings timings;
	int memlen, vramlen;
	int mode_ok = 0;

	DBG("neofb_check_var");

	if (!pixclock)
		pixclock = 10000;	/* 10ns = 100MHz */
	timings.pixclock = 1000000000 / pixclock;
	if (timings.pixclock < 1)
		timings.pixclock = 1;

	if (timings.pixclock > par->maxClock)
		return -EINVAL;

	timings.dblscan = var->vmode & FB_VMODE_DOUBLE;
	timings.interlaced = var->vmode & FB_VMODE_INTERLACED;
	timings.HDisplay = var->xres;
	timings.HSyncStart = timings.HDisplay + var->right_margin;
	timings.HSyncEnd = timings.HSyncStart + var->hsync_len;
	timings.HTotal = timings.HSyncEnd + var->left_margin;
	timings.VDisplay = var->yres;
	timings.VSyncStart = timings.VDisplay + var->lower_margin;
	timings.VSyncEnd = timings.VSyncStart + var->vsync_len;
	timings.VTotal = timings.VSyncEnd + var->upper_margin;
	timings.sync = var->sync;

	/* Is the mode larger than the LCD panel? */
	if (par->internal_display &&
            ((var->xres > par->NeoPanelWidth) ||
	     (var->yres > par->NeoPanelHeight))) {
		printk(KERN_INFO
		       "Mode (%dx%d) larger than the LCD panel (%dx%d)\n",
		       var->xres, var->yres, par->NeoPanelWidth,
		       par->NeoPanelHeight);
		return -EINVAL;
	}

	/* Is the mode one of the acceptable sizes? */
	if (!par->internal_display)
		mode_ok = 1;
	else {
		switch (var->xres) {
		case 1280:
			if (var->yres == 1024)
				mode_ok = 1;
			break;
		case 1024:
			if (var->yres == 768)
				mode_ok = 1;
			break;
		case 800:
			if (var->yres == (par->libretto ? 480 : 600))
				mode_ok = 1;
			break;
		case 640:
			if (var->yres == 480)
				mode_ok = 1;
			break;
		}
	}

	if (!mode_ok) {
		printk(KERN_INFO
		       "Mode (%dx%d) won't display properly on LCD\n",
		       var->xres, var->yres);
		return -EINVAL;
	}

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;

	switch (var->bits_per_pixel) {
	case 8:		/* PSEUDOCOLOUR, 256 */
		var->transp.offset = 0;
		var->transp.length = 0;
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		break;

	case 16:		/* DIRECTCOLOUR, 64k */
		var->transp.offset = 0;
		var->transp.length = 0;
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		break;

	case 24:		/* TRUECOLOUR, 16m */
		var->transp.offset = 0;
		var->transp.length = 0;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		break;

#ifdef NO_32BIT_SUPPORT_YET
	case 32:		/* TRUECOLOUR, 16m */
		var->transp.offset = 24;
		var->transp.length = 8;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		break;
#endif
	default:
		printk(KERN_WARNING "neofb: no support for %dbpp\n",
		       var->bits_per_pixel);
		return -EINVAL;
	}

	vramlen = info->fix.smem_len;
	if (vramlen > 4 * 1024 * 1024)
		vramlen = 4 * 1024 * 1024;

	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;
	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;

	memlen = var->xres_virtual * var->bits_per_pixel * var->yres_virtual >> 3;

	if (memlen > vramlen) {
		var->yres_virtual =  vramlen * 8 / (var->xres_virtual *
				   	var->bits_per_pixel);
		memlen = var->xres_virtual * var->bits_per_pixel *
				var->yres_virtual / 8;
	}

	/* we must round yres/xres down, we already rounded y/xres_virtual up
	   if it was possible. We should return -EINVAL, but I disagree */
	if (var->yres_virtual < var->yres)
		var->yres = var->yres_virtual;
	if (var->xres_virtual < var->xres)
		var->xres = var->xres_virtual;
	if (var->xoffset + var->xres > var->xres_virtual)
		var->xoffset = var->xres_virtual - var->xres;
	if (var->yoffset + var->yres > var->yres_virtual)
		var->yoffset = var->yres_virtual - var->yres;

	var->nonstd = 0;
	var->height = -1;
	var->width = -1;

	if (var->bits_per_pixel >= 24 || !par->neo2200)
		var->accel_flags &= ~FB_ACCELF_TEXT;
	return 0;
}

static int neofb_set_par(struct fb_info *info)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	struct xtimings timings;
	unsigned char temp;
	int i, clock_hi = 0;
	int lcd_stretch;
	int hoffset, voffset;

	DBG("neofb_set_par");

	neoUnlock();

	vgaHWProtect(1);	/* Blank the screen */

	timings.dblscan = info->var.vmode & FB_VMODE_DOUBLE;
	timings.interlaced = info->var.vmode & FB_VMODE_INTERLACED;
	timings.HDisplay = info->var.xres;
	timings.HSyncStart = timings.HDisplay + info->var.right_margin;
	timings.HSyncEnd = timings.HSyncStart + info->var.hsync_len;
	timings.HTotal = timings.HSyncEnd + info->var.left_margin;
	timings.VDisplay = info->var.yres;
	timings.VSyncStart = timings.VDisplay + info->var.lower_margin;
	timings.VSyncEnd = timings.VSyncStart + info->var.vsync_len;
	timings.VTotal = timings.VSyncEnd + info->var.upper_margin;
	timings.sync = info->var.sync;
	timings.pixclock = PICOS2KHZ(info->var.pixclock);

	if (timings.pixclock < 1)
		timings.pixclock = 1;

	/*
	 * This will allocate the datastructure and initialize all of the
	 * generic VGA registers.
	 */

	if (vgaHWInit(&info->var, info, par, &timings))
		return -EINVAL;

	/*
	 * The default value assigned by vgaHW.c is 0x41, but this does
	 * not work for NeoMagic.
	 */
	par->Attribute[16] = 0x01;

	switch (info->var.bits_per_pixel) {
	case 8:
		par->CRTC[0x13] = info->var.xres_virtual >> 3;
		par->ExtCRTOffset = info->var.xres_virtual >> 11;
		par->ExtColorModeSelect = 0x11;
		break;
	case 16:
		par->CRTC[0x13] = info->var.xres_virtual >> 2;
		par->ExtCRTOffset = info->var.xres_virtual >> 10;
		par->ExtColorModeSelect = 0x13;
		break;
	case 24:
		par->CRTC[0x13] = (info->var.xres_virtual * 3) >> 3;
		par->ExtCRTOffset = (info->var.xres_virtual * 3) >> 11;
		par->ExtColorModeSelect = 0x14;
		break;
#ifdef NO_32BIT_SUPPORT_YET
	case 32:		/* FIXME: guessed values */
		par->CRTC[0x13] = info->var.xres_virtual >> 1;
		par->ExtCRTOffset = info->var.xres_virtual >> 9;
		par->ExtColorModeSelect = 0x15;
		break;
#endif
	default:
		break;
	}

	par->ExtCRTDispAddr = 0x10;

	/* Vertical Extension */
	par->VerticalExt = (((timings.VTotal - 2) & 0x400) >> 10)
	    | (((timings.VDisplay - 1) & 0x400) >> 9)
	    | (((timings.VSyncStart) & 0x400) >> 8)
	    | (((timings.VSyncStart) & 0x400) >> 7);

	/* Fast write bursts on unless disabled. */
	if (par->pci_burst)
		par->SysIfaceCntl1 = 0x30;
	else
		par->SysIfaceCntl1 = 0x00;

	par->SysIfaceCntl2 = 0xc0;	/* VESA Bios sets this to 0x80! */

	/* Enable any user specified display devices. */
	par->PanelDispCntlReg1 = 0x00;
	if (par->internal_display)
		par->PanelDispCntlReg1 |= 0x02;
	if (par->external_display)
		par->PanelDispCntlReg1 |= 0x01;

	/* If the user did not specify any display devices, then... */
	if (par->PanelDispCntlReg1 == 0x00) {
		/* Default to internal (i.e., LCD) only. */
		par->PanelDispCntlReg1 |= 0x02;
	}

	/* If we are using a fixed mode, then tell the chip we are. */
	switch (info->var.xres) {
	case 1280:
		par->PanelDispCntlReg1 |= 0x60;
		break;
	case 1024:
		par->PanelDispCntlReg1 |= 0x40;
		break;
	case 800:
		par->PanelDispCntlReg1 |= 0x20;
		break;
	case 640:
	default:
		break;
	}

	/* Setup shadow register locking. */
	switch (par->PanelDispCntlReg1 & 0x03) {
	case 0x01:		/* External CRT only mode: */
		par->GeneralLockReg = 0x00;
		/* We need to program the VCLK for external display only mode. */
		par->ProgramVCLK = 1;
		break;
	case 0x02:		/* Internal LCD only mode: */
	case 0x03:		/* Simultaneous internal/external (LCD/CRT) mode: */
		par->GeneralLockReg = 0x01;
		/* Don't program the VCLK when using the LCD. */
		par->ProgramVCLK = 0;
		break;
	}

	/*
	 * If the screen is to be stretched, turn on stretching for the
	 * various modes.
	 *
	 * OPTION_LCD_STRETCH means stretching should be turned off!
	 */
	par->PanelDispCntlReg2 = 0x00;
	par->PanelDispCntlReg3 = 0x00;

	if (par->lcd_stretch && (par->PanelDispCntlReg1 == 0x02) &&	/* LCD only */
	    (info->var.xres != par->NeoPanelWidth)) {
		switch (info->var.xres) {
		case 320:	/* Needs testing.  KEM -- 24 May 98 */
		case 400:	/* Needs testing.  KEM -- 24 May 98 */
		case 640:
		case 800:
		case 1024:
			lcd_stretch = 1;
			par->PanelDispCntlReg2 |= 0xC6;
			break;
		default:
			lcd_stretch = 0;
			/* No stretching in these modes. */
		}
	} else
		lcd_stretch = 0;

	/*
	 * If the screen is to be centerd, turn on the centering for the
	 * various modes.
	 */
	par->PanelVertCenterReg1 = 0x00;
	par->PanelVertCenterReg2 = 0x00;
	par->PanelVertCenterReg3 = 0x00;
	par->PanelVertCenterReg4 = 0x00;
	par->PanelVertCenterReg5 = 0x00;
	par->PanelHorizCenterReg1 = 0x00;
	par->PanelHorizCenterReg2 = 0x00;
	par->PanelHorizCenterReg3 = 0x00;
	par->PanelHorizCenterReg4 = 0x00;
	par->PanelHorizCenterReg5 = 0x00;


	if (par->PanelDispCntlReg1 & 0x02) {
		if (info->var.xres == par->NeoPanelWidth) {
			/*
			 * No centering required when the requested display width
			 * equals the panel width.
			 */
		} else {
			par->PanelDispCntlReg2 |= 0x01;
			par->PanelDispCntlReg3 |= 0x10;

			/* Calculate the horizontal and vertical offsets. */
			if (!lcd_stretch) {
				hoffset =
				    ((par->NeoPanelWidth -
				      info->var.xres) >> 4) - 1;
				voffset =
				    ((par->NeoPanelHeight -
				      info->var.yres) >> 1) - 2;
			} else {
				/* Stretched modes cannot be centered. */
				hoffset = 0;
				voffset = 0;
			}

			switch (info->var.xres) {
			case 320:	/* Needs testing.  KEM -- 24 May 98 */
				par->PanelHorizCenterReg3 = hoffset;
				par->PanelVertCenterReg2 = voffset;
				break;
			case 400:	/* Needs testing.  KEM -- 24 May 98 */
				par->PanelHorizCenterReg4 = hoffset;
				par->PanelVertCenterReg1 = voffset;
				break;
			case 640:
				par->PanelHorizCenterReg1 = hoffset;
				par->PanelVertCenterReg3 = voffset;
				break;
			case 800:
				par->PanelHorizCenterReg2 = hoffset;
				par->PanelVertCenterReg4 = voffset;
				break;
			case 1024:
				par->PanelHorizCenterReg5 = hoffset;
				par->PanelVertCenterReg5 = voffset;
				break;
			case 1280:
			default:
				/* No centering in these modes. */
				break;
			}
		}
	}

	par->biosMode =
	    neoFindMode(info->var.xres, info->var.yres,
			info->var.bits_per_pixel);

	/*
	 * Calculate the VCLK that most closely matches the requested dot
	 * clock.
	 */
	neoCalcVCLK(info, par, timings.pixclock);

	/* Since we program the clocks ourselves, always use VCLK3. */
	par->MiscOutReg |= 0x0C;

	/* alread unlocked above */
	/* BOGUS  vga_wgfx(NULL, 0x09, 0x26); */

	/* don't know what this is, but it's 0 from bootup anyway */
	vga_wgfx(NULL, 0x15, 0x00);

	/* was set to 0x01 by my bios in text and vesa modes */
	vga_wgfx(NULL, 0x0A, par->GeneralLockReg);

	/*
	 * The color mode needs to be set before calling vgaHWRestore
	 * to ensure the DAC is initialized properly.
	 *
	 * NOTE: Make sure we don't change bits make sure we don't change
	 * any reserved bits.
	 */
	temp = vga_rgfx(NULL, 0x90);
	switch (info->fix.accel) {
	case FB_ACCEL_NEOMAGIC_NM2070:
		temp &= 0xF0;	/* Save bits 7:4 */
		temp |= (par->ExtColorModeSelect & ~0xF0);
		break;
	case FB_ACCEL_NEOMAGIC_NM2090:
	case FB_ACCEL_NEOMAGIC_NM2093:
	case FB_ACCEL_NEOMAGIC_NM2097:
	case FB_ACCEL_NEOMAGIC_NM2160:
	case FB_ACCEL_NEOMAGIC_NM2200:
	case FB_ACCEL_NEOMAGIC_NM2230:
	case FB_ACCEL_NEOMAGIC_NM2360:
	case FB_ACCEL_NEOMAGIC_NM2380:
		temp &= 0x70;	/* Save bits 6:4 */
		temp |= (par->ExtColorModeSelect & ~0x70);
		break;
	}

	vga_wgfx(NULL, 0x90, temp);

	/*
	 * In some rare cases a lockup might occur if we don't delay
	 * here. (Reported by Miles Lane)
	 */
	//mdelay(200);

	/*
	 * Disable horizontal and vertical graphics and text expansions so
	 * that vgaHWRestore works properly.
	 */
	temp = vga_rgfx(NULL, 0x25);
	temp &= 0x39;
	vga_wgfx(NULL, 0x25, temp);

	/*
	 * Sleep for 200ms to make sure that the two operations above have
	 * had time to take effect.
	 */
	mdelay(200);

	/*
	 * This function handles restoring the generic VGA registers.  */
	vgaHWRestore(info, par);

	/* linear colormap for non palettized modes */
	switch (info->var.bits_per_pixel) {
	case 8:
		/* PseudoColor, 256 */
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	case 16:
		/* TrueColor, 64k */
		info->fix.visual = FB_VISUAL_TRUECOLOR;

		for (i = 0; i < 64; i++) {
			outb(i, 0x3c8);

			outb(i << 1, 0x3c9);
			outb(i, 0x3c9);
			outb(i << 1, 0x3c9);
		}
		break;
	case 24:
#ifdef NO_32BIT_SUPPORT_YET
	case 32:
#endif
		/* TrueColor, 16m */
		info->fix.visual = FB_VISUAL_TRUECOLOR;

		for (i = 0; i < 256; i++) {
			outb(i, 0x3c8);

			outb(i, 0x3c9);
			outb(i, 0x3c9);
			outb(i, 0x3c9);
		}
		break;
	}

	vga_wgfx(NULL, 0x0E, par->ExtCRTDispAddr);
	vga_wgfx(NULL, 0x0F, par->ExtCRTOffset);
	temp = vga_rgfx(NULL, 0x10);
	temp &= 0x0F;		/* Save bits 3:0 */
	temp |= (par->SysIfaceCntl1 & ~0x0F);	/* VESA Bios sets bit 1! */
	vga_wgfx(NULL, 0x10, temp);

	vga_wgfx(NULL, 0x11, par->SysIfaceCntl2);
	vga_wgfx(NULL, 0x15, 0 /*par->SingleAddrPage */ );
	vga_wgfx(NULL, 0x16, 0 /*par->DualAddrPage */ );

	temp = vga_rgfx(NULL, 0x20);
	switch (info->fix.accel) {
	case FB_ACCEL_NEOMAGIC_NM2070:
		temp &= 0xFC;	/* Save bits 7:2 */
		temp |= (par->PanelDispCntlReg1 & ~0xFC);
		break;
	case FB_ACCEL_NEOMAGIC_NM2090:
	case FB_ACCEL_NEOMAGIC_NM2093:
	case FB_ACCEL_NEOMAGIC_NM2097:
	case FB_ACCEL_NEOMAGIC_NM2160:
		temp &= 0xDC;	/* Save bits 7:6,4:2 */
		temp |= (par->PanelDispCntlReg1 & ~0xDC);
		break;
	case FB_ACCEL_NEOMAGIC_NM2200:
	case FB_ACCEL_NEOMAGIC_NM2230:
	case FB_ACCEL_NEOMAGIC_NM2360:
	case FB_ACCEL_NEOMAGIC_NM2380:
		temp &= 0x98;	/* Save bits 7,4:3 */
		temp |= (par->PanelDispCntlReg1 & ~0x98);
		break;
	}
	vga_wgfx(NULL, 0x20, temp);

	temp = vga_rgfx(NULL, 0x25);
	temp &= 0x38;		/* Save bits 5:3 */
	temp |= (par->PanelDispCntlReg2 & ~0x38);
	vga_wgfx(NULL, 0x25, temp);

	if (info->fix.accel != FB_ACCEL_NEOMAGIC_NM2070) {
		temp = vga_rgfx(NULL, 0x30);
		temp &= 0xEF;	/* Save bits 7:5 and bits 3:0 */
		temp |= (par->PanelDispCntlReg3 & ~0xEF);
		vga_wgfx(NULL, 0x30, temp);
	}

	vga_wgfx(NULL, 0x28, par->PanelVertCenterReg1);
	vga_wgfx(NULL, 0x29, par->PanelVertCenterReg2);
	vga_wgfx(NULL, 0x2a, par->PanelVertCenterReg3);

	if (info->fix.accel != FB_ACCEL_NEOMAGIC_NM2070) {
		vga_wgfx(NULL, 0x32, par->PanelVertCenterReg4);
		vga_wgfx(NULL, 0x33, par->PanelHorizCenterReg1);
		vga_wgfx(NULL, 0x34, par->PanelHorizCenterReg2);
		vga_wgfx(NULL, 0x35, par->PanelHorizCenterReg3);
	}

	if (info->fix.accel == FB_ACCEL_NEOMAGIC_NM2160)
		vga_wgfx(NULL, 0x36, par->PanelHorizCenterReg4);

	if (info->fix.accel == FB_ACCEL_NEOMAGIC_NM2200 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2230 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2360 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2380) {
		vga_wgfx(NULL, 0x36, par->PanelHorizCenterReg4);
		vga_wgfx(NULL, 0x37, par->PanelVertCenterReg5);
		vga_wgfx(NULL, 0x38, par->PanelHorizCenterReg5);

		clock_hi = 1;
	}

	/* Program VCLK3 if needed. */
	if (par->ProgramVCLK && ((vga_rgfx(NULL, 0x9B) != par->VCLK3NumeratorLow)
				 || (vga_rgfx(NULL, 0x9F) != par->VCLK3Denominator)
				 || (clock_hi && ((vga_rgfx(NULL, 0x8F) & ~0x0f)
						  != (par->VCLK3NumeratorHigh &
						      ~0x0F))))) {
		vga_wgfx(NULL, 0x9B, par->VCLK3NumeratorLow);
		if (clock_hi) {
			temp = vga_rgfx(NULL, 0x8F);
			temp &= 0x0F;	/* Save bits 3:0 */
			temp |= (par->VCLK3NumeratorHigh & ~0x0F);
			vga_wgfx(NULL, 0x8F, temp);
		}
		vga_wgfx(NULL, 0x9F, par->VCLK3Denominator);
	}

	if (par->biosMode)
		vga_wcrt(NULL, 0x23, par->biosMode);

	vga_wgfx(NULL, 0x93, 0xc0);	/* Gives 5x faster framebuffer writes !!! */

	/* Program vertical extension register */
	if (info->fix.accel == FB_ACCEL_NEOMAGIC_NM2200 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2230 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2360 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2380) {
		vga_wcrt(NULL, 0x70, par->VerticalExt);
	}

	vgaHWProtect(0);	/* Turn on screen */

	/* Calling this also locks offset registers required in update_start */
	neoLock(&par->state);

	info->fix.line_length =
	    info->var.xres_virtual * (info->var.bits_per_pixel >> 3);

	switch (info->fix.accel) {
		case FB_ACCEL_NEOMAGIC_NM2200:
		case FB_ACCEL_NEOMAGIC_NM2230: 
		case FB_ACCEL_NEOMAGIC_NM2360: 
		case FB_ACCEL_NEOMAGIC_NM2380: 
			neo2200_accel_init(info, &info->var);
			break;
		default:
			break;
	}	
	return 0;
}

static void neofb_update_start(struct fb_info *info,
			       struct fb_var_screeninfo *var)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	struct vgastate *state = &par->state;
	int oldExtCRTDispAddr;
	int Base;

	DBG("neofb_update_start");

	Base = (var->yoffset * var->xres_virtual + var->xoffset) >> 2;
	Base *= (var->bits_per_pixel + 7) / 8;

	neoUnlock();

	/*
	 * These are the generic starting address registers.
	 */
	vga_wcrt(state->vgabase, 0x0C, (Base & 0x00FF00) >> 8);
	vga_wcrt(state->vgabase, 0x0D, (Base & 0x00FF));

	/*
	 * Make sure we don't clobber some other bits that might already
	 * have been set. NOTE: NM2200 has a writable bit 3, but it shouldn't
	 * be needed.
	 */
	oldExtCRTDispAddr = vga_rgfx(NULL, 0x0E);
	vga_wgfx(state->vgabase, 0x0E, (((Base >> 16) & 0x0f) | (oldExtCRTDispAddr & 0xf0)));

	neoLock(state);
}

/*
 *    Pan or Wrap the Display
 */
static int neofb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	u_int y_bottom;

	y_bottom = var->yoffset;

	if (!(var->vmode & FB_VMODE_YWRAP))
		y_bottom += var->yres;

	if (var->xoffset > (var->xres_virtual - var->xres))
		return -EINVAL;
	if (y_bottom > info->var.yres_virtual)
		return -EINVAL;

	neofb_update_start(info, var);

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;

	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;
	return 0;
}

static int neofb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *fb)
{
	if (regno >= fb->cmap.len || regno > 255)
		return -EINVAL;

	switch (fb->var.bits_per_pixel) {
	case 8:
		outb(regno, 0x3c8);

		outb(red >> 10, 0x3c9);
		outb(green >> 10, 0x3c9);
		outb(blue >> 10, 0x3c9);
		break;
	case 16:
		((u32 *) fb->pseudo_palette)[regno] =
				((red & 0xf800)) | ((green & 0xfc00) >> 5) |
				((blue & 0xf800) >> 11);
		break;
	case 24:
		((u32 *) fb->pseudo_palette)[regno] =
				((red & 0xff00) << 8) | ((green & 0xff00)) |
				((blue & 0xff00) >> 8);
		break;
#ifdef NO_32BIT_SUPPORT_YET
	case 32:
		((u32 *) fb->pseudo_palette)[regno] =
				((transp & 0xff00) << 16) | ((red & 0xff00) << 8) |
				((green & 0xff00)) | ((blue & 0xff00) >> 8);
		break;
#endif
	default:
		return 1;
	}
	return 0;
}

/*
 *    (Un)Blank the display.
 */
static int neofb_blank(int blank_mode, struct fb_info *info)
{
	/*
	 *  Blank the screen if blank_mode != 0, else unblank.
	 *  Return 0 if blanking succeeded, != 0 if un-/blanking failed due to
	 *  e.g. a video mode which doesn't support it. Implements VESA suspend
	 *  and powerdown modes for monitors, and backlight control on LCDs.
	 *    blank_mode == 0: unblanked (backlight on)
	 *    blank_mode == 1: blank (backlight on)
	 *    blank_mode == 2: suspend vsync (backlight off)
	 *    blank_mode == 3: suspend hsync (backlight off)
	 *    blank_mode == 4: powerdown (backlight off)
	 *
	 *  wms...Enable VESA DPMS compatible powerdown mode
	 *  run "setterm -powersave powerdown" to take advantage
	 */
	struct neofb_par *par = (struct neofb_par *)info->par;
	int seqflags, lcdflags, dpmsflags, reg;

	switch (blank_mode) {
	case FB_BLANK_POWERDOWN:	/* powerdown - both sync lines down */
		seqflags = VGA_SR01_SCREEN_OFF; /* Disable sequencer */
		lcdflags = 0;			/* LCD off */
		dpmsflags = NEO_GR01_SUPPRESS_HSYNC |
			    NEO_GR01_SUPPRESS_VSYNC;
#ifdef CONFIG_TOSHIBA
		/* Do we still need this ? */
		/* attempt to turn off backlight on toshiba; also turns off external */
		{
			SMMRegisters regs;

			regs.eax = 0xff00; /* HCI_SET */
			regs.ebx = 0x0002; /* HCI_BACKLIGHT */
			regs.ecx = 0x0000; /* HCI_DISABLE */
			tosh_smm(&regs);
		}
#endif
		break;
	case FB_BLANK_HSYNC_SUSPEND:		/* hsync off */
		seqflags = VGA_SR01_SCREEN_OFF;	/* Disable sequencer */
		lcdflags = 0;			/* LCD off */
		dpmsflags = NEO_GR01_SUPPRESS_HSYNC;
		break;
	case FB_BLANK_VSYNC_SUSPEND:		/* vsync off */
		seqflags = VGA_SR01_SCREEN_OFF;	/* Disable sequencer */
		lcdflags = 0;			/* LCD off */
		dpmsflags = NEO_GR01_SUPPRESS_VSYNC;
		break;
	case FB_BLANK_NORMAL:		/* just blank screen (backlight stays on) */
		seqflags = VGA_SR01_SCREEN_OFF;	/* Disable sequencer */
		lcdflags = par->PanelDispCntlReg1 & 0x02; /* LCD normal */
		dpmsflags = 0;			/* no hsync/vsync suppression */
		break;
	case FB_BLANK_UNBLANK:		/* unblank */
		seqflags = 0;			/* Enable sequencer */
		lcdflags = par->PanelDispCntlReg1 & 0x02; /* LCD normal */
		dpmsflags = 0x00;	/* no hsync/vsync suppression */
#ifdef CONFIG_TOSHIBA
		/* Do we still need this ? */
		/* attempt to re-enable backlight/external on toshiba */
		{
			SMMRegisters regs;

			regs.eax = 0xff00; /* HCI_SET */
			regs.ebx = 0x0002; /* HCI_BACKLIGHT */
			regs.ecx = 0x0001; /* HCI_ENABLE */
			tosh_smm(&regs);
		}
#endif
		break;
	default:	/* Anything else we don't understand; return 1 to tell
			 * fb_blank we didn't aactually do anything */
		return 1;
	}

	neoUnlock();
	reg = (vga_rseq(NULL, 0x01) & ~0x20) | seqflags;
	vga_wseq(NULL, 0x01, reg);
	reg = (vga_rgfx(NULL, 0x20) & ~0x02) | lcdflags;
	vga_wgfx(NULL, 0x20, reg);
	reg = (vga_rgfx(NULL, 0x01) & ~0xF0) | 0x80 | dpmsflags;
	vga_wgfx(NULL, 0x01, reg);
	neoLock(&par->state);
	return 0;
}

static void
neo2200_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	u_long dst, rop;

	dst = rect->dx + rect->dy * info->var.xres_virtual;
	rop = rect->rop ? 0x060000 : 0x0c0000;

	neo2200_wait_fifo(info, 4);

	/* set blt control */
	writel(NEO_BC3_FIFO_EN |
	       NEO_BC0_SRC_IS_FG | NEO_BC3_SKIP_MAPPING |
	       //               NEO_BC3_DST_XY_ADDR  |
	       //               NEO_BC3_SRC_XY_ADDR  |
	       rop, &par->neo2200->bltCntl);

	switch (info->var.bits_per_pixel) {
	case 8:
		writel(rect->color, &par->neo2200->fgColor);
		break;
	case 16:
	case 24:
		writel(((u32 *) (info->pseudo_palette))[rect->color],
		       &par->neo2200->fgColor);
		break;
	}

	writel(dst * ((info->var.bits_per_pixel + 7) >> 3),
	       &par->neo2200->dstStart);
	writel((rect->height << 16) | (rect->width & 0xffff),
	       &par->neo2200->xyExt);
}

static void
neo2200_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	u32 sx = area->sx, sy = area->sy, dx = area->dx, dy = area->dy;
	struct neofb_par *par = (struct neofb_par *) info->par;
	u_long src, dst, bltCntl;

	bltCntl = NEO_BC3_FIFO_EN | NEO_BC3_SKIP_MAPPING | 0x0C0000;

	if ((dy > sy) || ((dy == sy) && (dx > sx))) {
		/* Start with the lower right corner */
		sy += (area->height - 1);
		dy += (area->height - 1);
		sx += (area->width - 1);
		dx += (area->width - 1);

		bltCntl |= NEO_BC0_X_DEC | NEO_BC0_DST_Y_DEC | NEO_BC0_SRC_Y_DEC;
	}

	src = sx * (info->var.bits_per_pixel >> 3) + sy*info->fix.line_length;
	dst = dx * (info->var.bits_per_pixel >> 3) + dy*info->fix.line_length;

	neo2200_wait_fifo(info, 4);

	/* set blt control */
	writel(bltCntl, &par->neo2200->bltCntl);

	writel(src, &par->neo2200->srcStart);
	writel(dst, &par->neo2200->dstStart);
	writel((area->height << 16) | (area->width & 0xffff),
	       &par->neo2200->xyExt);
}

static void
neo2200_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	int s_pitch = (image->width * image->depth + 7) >> 3;
	int scan_align = info->pixmap.scan_align - 1;
	int buf_align = info->pixmap.buf_align - 1;
	int bltCntl_flags, d_pitch, data_len;

	// The data is padded for the hardware
	d_pitch = (s_pitch + scan_align) & ~scan_align;
	data_len = ((d_pitch * image->height) + buf_align) & ~buf_align;

	neo2200_sync(info);

	if (image->depth == 1) {
		if (info->var.bits_per_pixel == 24 && image->width < 16) {
			/* FIXME. There is a bug with accelerated color-expanded
			 * transfers in 24 bit mode if the image being transferred
			 * is less than 16 bits wide. This is due to insufficient
			 * padding when writing the image. We need to adjust
			 * struct fb_pixmap. Not yet done. */
			return cfb_imageblit(info, image);
		}
		bltCntl_flags = NEO_BC0_SRC_MONO;
	} else if (image->depth == info->var.bits_per_pixel) {
		bltCntl_flags = 0;
	} else {
		/* We don't currently support hardware acceleration if image
		 * depth is different from display */
		return cfb_imageblit(info, image);
	}

	switch (info->var.bits_per_pixel) {
	case 8:
		writel(image->fg_color, &par->neo2200->fgColor);
		writel(image->bg_color, &par->neo2200->bgColor);
		break;
	case 16:
	case 24:
		writel(((u32 *) (info->pseudo_palette))[image->fg_color],
		       &par->neo2200->fgColor);
		writel(((u32 *) (info->pseudo_palette))[image->bg_color],
		       &par->neo2200->bgColor);
		break;
	}

	writel(NEO_BC0_SYS_TO_VID |
		NEO_BC3_SKIP_MAPPING | bltCntl_flags |
		// NEO_BC3_DST_XY_ADDR |
		0x0c0000, &par->neo2200->bltCntl);

	writel(0, &par->neo2200->srcStart);
//      par->neo2200->dstStart = (image->dy << 16) | (image->dx & 0xffff);
	writel(((image->dx & 0xffff) * (info->var.bits_per_pixel >> 3) +
		image->dy * info->fix.line_length), &par->neo2200->dstStart);
	writel((image->height << 16) | (image->width & 0xffff),
	       &par->neo2200->xyExt);

	memcpy_toio(par->mmio_vbase + 0x100000, image->data, data_len);
}

static void
neofb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	switch (info->fix.accel) {
		case FB_ACCEL_NEOMAGIC_NM2200:
		case FB_ACCEL_NEOMAGIC_NM2230: 
		case FB_ACCEL_NEOMAGIC_NM2360: 
		case FB_ACCEL_NEOMAGIC_NM2380:
			neo2200_fillrect(info, rect);
			break;
		default:
			cfb_fillrect(info, rect);
			break;
	}	
}

static void
neofb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	switch (info->fix.accel) {
		case FB_ACCEL_NEOMAGIC_NM2200:
		case FB_ACCEL_NEOMAGIC_NM2230: 
		case FB_ACCEL_NEOMAGIC_NM2360: 
		case FB_ACCEL_NEOMAGIC_NM2380: 
			neo2200_copyarea(info, area);
			break;
		default:
			cfb_copyarea(info, area);
			break;
	}	
}

static void
neofb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	switch (info->fix.accel) {
		case FB_ACCEL_NEOMAGIC_NM2200:
		case FB_ACCEL_NEOMAGIC_NM2230:
		case FB_ACCEL_NEOMAGIC_NM2360:
		case FB_ACCEL_NEOMAGIC_NM2380:
			neo2200_imageblit(info, image);
			break;
		default:
			cfb_imageblit(info, image);
			break;
	}
}

static int 
neofb_sync(struct fb_info *info)
{
	switch (info->fix.accel) {
		case FB_ACCEL_NEOMAGIC_NM2200:
		case FB_ACCEL_NEOMAGIC_NM2230: 
		case FB_ACCEL_NEOMAGIC_NM2360: 
		case FB_ACCEL_NEOMAGIC_NM2380: 
			neo2200_sync(info);
			break;
		default:
			break;
	}
	return 0;		
}

/*
static void
neofb_draw_cursor(struct fb_info *info, u8 *dst, u8 *src, unsigned int width)
{
	//memset_io(info->sprite.addr, 0xff, 1);
}

static int
neofb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct neofb_par *par = (struct neofb_par *) info->par;

	* Disable cursor *
	write_le32(NEOREG_CURSCNTL, ~NEO_CURS_ENABLE, par);

	if (cursor->set & FB_CUR_SETPOS) {
		u32 x = cursor->image.dx;
		u32 y = cursor->image.dy;

		info->cursor.image.dx = x;
		info->cursor.image.dy = y;
		write_le32(NEOREG_CURSX, x, par);
		write_le32(NEOREG_CURSY, y, par);
	}

	if (cursor->set & FB_CUR_SETSIZE) {
		info->cursor.image.height = cursor->image.height;
		info->cursor.image.width = cursor->image.width;
	}

	if (cursor->set & FB_CUR_SETHOT)
		info->cursor.hot = cursor->hot;

	if (cursor->set & FB_CUR_SETCMAP) {
		if (cursor->image.depth == 1) {
			u32 fg = cursor->image.fg_color;
			u32 bg = cursor->image.bg_color;

			info->cursor.image.fg_color = fg;
			info->cursor.image.bg_color = bg;

			fg = ((fg & 0xff0000) >> 16) | ((fg & 0xff) << 16) | (fg & 0xff00);
			bg = ((bg & 0xff0000) >> 16) | ((bg & 0xff) << 16) | (bg & 0xff00);
			write_le32(NEOREG_CURSFGCOLOR, fg, par);
			write_le32(NEOREG_CURSBGCOLOR, bg, par);
		}
	}

	if (cursor->set & FB_CUR_SETSHAPE)
		fb_load_cursor_image(info);

	if (info->cursor.enable)
		write_le32(NEOREG_CURSCNTL, NEO_CURS_ENABLE, par);
	return 0;
}
*/

static struct fb_ops neofb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= neofb_open,
	.fb_release	= neofb_release,
	.fb_check_var	= neofb_check_var,
	.fb_set_par	= neofb_set_par,
	.fb_setcolreg	= neofb_setcolreg,
	.fb_pan_display	= neofb_pan_display,
	.fb_blank	= neofb_blank,
	.fb_sync	= neofb_sync,
	.fb_fillrect	= neofb_fillrect,
	.fb_copyarea	= neofb_copyarea,
	.fb_imageblit	= neofb_imageblit,
};

/* --------------------------------------------------------------------- */

static struct fb_videomode __devinitdata mode800x480 = {
	.xres           = 800,
	.yres           = 480,
	.pixclock       = 25000,
	.left_margin    = 88,
	.right_margin   = 40,
	.upper_margin   = 23,
	.lower_margin   = 1,
	.hsync_len      = 128,
	.vsync_len      = 4,
	.sync           = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode          = FB_VMODE_NONINTERLACED
};

static int __devinit neo_map_mmio(struct fb_info *info,
				  struct pci_dev *dev)
{
	struct neofb_par *par = (struct neofb_par *) info->par;

	DBG("neo_map_mmio");

	switch (info->fix.accel) {
		case FB_ACCEL_NEOMAGIC_NM2070:
			info->fix.mmio_start = pci_resource_start(dev, 0)+
				0x100000;
			break;
		case FB_ACCEL_NEOMAGIC_NM2090:
		case FB_ACCEL_NEOMAGIC_NM2093:
			info->fix.mmio_start = pci_resource_start(dev, 0)+
				0x200000;
			break;
		case FB_ACCEL_NEOMAGIC_NM2160:
		case FB_ACCEL_NEOMAGIC_NM2097:
		case FB_ACCEL_NEOMAGIC_NM2200:
		case FB_ACCEL_NEOMAGIC_NM2230:
		case FB_ACCEL_NEOMAGIC_NM2360:
		case FB_ACCEL_NEOMAGIC_NM2380:
			info->fix.mmio_start = pci_resource_start(dev, 1);
			break;
		default:
			info->fix.mmio_start = pci_resource_start(dev, 0);
	}
	info->fix.mmio_len = MMIO_SIZE;

	if (!request_mem_region
	    (info->fix.mmio_start, MMIO_SIZE, "memory mapped I/O")) {
		printk("neofb: memory mapped IO in use\n");
		return -EBUSY;
	}

	par->mmio_vbase = ioremap(info->fix.mmio_start, MMIO_SIZE);
	if (!par->mmio_vbase) {
		printk("neofb: unable to map memory mapped IO\n");
		release_mem_region(info->fix.mmio_start,
				   info->fix.mmio_len);
		return -ENOMEM;
	} else
		printk(KERN_INFO "neofb: mapped io at %p\n",
		       par->mmio_vbase);
	return 0;
}

static void neo_unmap_mmio(struct fb_info *info)
{
	struct neofb_par *par = (struct neofb_par *) info->par;

	DBG("neo_unmap_mmio");

	iounmap(par->mmio_vbase);
	par->mmio_vbase = NULL;

	release_mem_region(info->fix.mmio_start,
			   info->fix.mmio_len);
}

static int __devinit neo_map_video(struct fb_info *info,
				   struct pci_dev *dev, int video_len)
{
	//unsigned long addr;

	DBG("neo_map_video");

	info->fix.smem_start = pci_resource_start(dev, 0);
	info->fix.smem_len = video_len;

	if (!request_mem_region(info->fix.smem_start, info->fix.smem_len,
				"frame buffer")) {
		printk("neofb: frame buffer in use\n");
		return -EBUSY;
	}

	info->screen_base =
	    ioremap(info->fix.smem_start, info->fix.smem_len);
	if (!info->screen_base) {
		printk("neofb: unable to map screen memory\n");
		release_mem_region(info->fix.smem_start,
				   info->fix.smem_len);
		return -ENOMEM;
	} else
		printk(KERN_INFO "neofb: mapped framebuffer at %p\n",
		       info->screen_base);

#ifdef CONFIG_MTRR
	((struct neofb_par *)(info->par))->mtrr =
		mtrr_add(info->fix.smem_start, pci_resource_len(dev, 0),
				MTRR_TYPE_WRCOMB, 1);
#endif

	/* Clear framebuffer, it's all white in memory after boot */
	memset_io(info->screen_base, 0, info->fix.smem_len);

	/* Allocate Cursor drawing pad.
	info->fix.smem_len -= PAGE_SIZE;
	addr = info->fix.smem_start + info->fix.smem_len;
	write_le32(NEOREG_CURSMEMPOS, ((0x000f & (addr >> 10)) << 8) |
					((0x0ff0 & (addr >> 10)) >> 4), par);
	addr = (unsigned long) info->screen_base + info->fix.smem_len;
	info->sprite.addr = (u8 *) addr; */
	return 0;
}

static void neo_unmap_video(struct fb_info *info)
{
	DBG("neo_unmap_video");

#ifdef CONFIG_MTRR
	{
		struct neofb_par *par = (struct neofb_par *) info->par;

		mtrr_del(par->mtrr, info->fix.smem_start,
			 info->fix.smem_len);
	}
#endif
	iounmap(info->screen_base);
	info->screen_base = NULL;

	release_mem_region(info->fix.smem_start,
			   info->fix.smem_len);
}

static int __devinit neo_scan_monitor(struct fb_info *info)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	unsigned char type, display;
	int w;

	// Eventually we will have i2c support.
	info->monspecs.modedb = kmalloc(sizeof(struct fb_videomode), GFP_KERNEL);
	if (!info->monspecs.modedb)
		return -ENOMEM;
	info->monspecs.modedb_len = 1;

	/* Determine the panel type */
	vga_wgfx(NULL, 0x09, 0x26);
	type = vga_rgfx(NULL, 0x21);
	display = vga_rgfx(NULL, 0x20);
	if (!par->internal_display && !par->external_display) {
		par->internal_display = display & 2 || !(display & 3) ? 1 : 0;
		par->external_display = display & 1;
		printk (KERN_INFO "Autodetected %s display\n",
			par->internal_display && par->external_display ? "simultaneous" :
			par->internal_display ? "internal" : "external");
	}

	/* Determine panel width -- used in NeoValidMode. */
	w = vga_rgfx(NULL, 0x20);
	vga_wgfx(NULL, 0x09, 0x00);
	switch ((w & 0x18) >> 3) {
	case 0x00:
		// 640x480@60
		par->NeoPanelWidth = 640;
		par->NeoPanelHeight = 480;
		memcpy(info->monspecs.modedb, &vesa_modes[3], sizeof(struct fb_videomode));
		break;
	case 0x01:
		par->NeoPanelWidth = 800;
		if (par->libretto) {
			par->NeoPanelHeight = 480;
			memcpy(info->monspecs.modedb, &mode800x480, sizeof(struct fb_videomode));
		} else {
			// 800x600@60
			par->NeoPanelHeight = 600;
			memcpy(info->monspecs.modedb, &vesa_modes[8], sizeof(struct fb_videomode));
		}
		break;
	case 0x02:
		// 1024x768@60
		par->NeoPanelWidth = 1024;
		par->NeoPanelHeight = 768;
		memcpy(info->monspecs.modedb, &vesa_modes[13], sizeof(struct fb_videomode));
		break;
	case 0x03:
		/* 1280x1024@60 panel support needs to be added */
#ifdef NOT_DONE
		par->NeoPanelWidth = 1280;
		par->NeoPanelHeight = 1024;
		memcpy(info->monspecs.modedb, &vesa_modes[20], sizeof(struct fb_videomode));
		break;
#else
		printk(KERN_ERR
		       "neofb: Only 640x480, 800x600/480 and 1024x768 panels are currently supported\n");
		return -1;
#endif
	default:
		// 640x480@60
		par->NeoPanelWidth = 640;
		par->NeoPanelHeight = 480;
		memcpy(info->monspecs.modedb, &vesa_modes[3], sizeof(struct fb_videomode));
		break;
	}

	printk(KERN_INFO "Panel is a %dx%d %s %s display\n",
	       par->NeoPanelWidth,
	       par->NeoPanelHeight,
	       (type & 0x02) ? "color" : "monochrome",
	       (type & 0x10) ? "TFT" : "dual scan");
	return 0;
}

static int __devinit neo_init_hw(struct fb_info *info)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	int videoRam = 896;
	int maxClock = 65000;
	int CursorMem = 1024;
	int CursorOff = 0x100;
	int linearSize = 1024;
	int maxWidth = 1024;
	int maxHeight = 1024;

	DBG("neo_init_hw");

	neoUnlock();

#if 0
	printk(KERN_DEBUG "--- Neo extended register dump ---\n");
	for (int w = 0; w < 0x85; w++)
		printk(KERN_DEBUG "CR %p: %p\n", (void *) w,
		       (void *) vga_rcrt(NULL, w);
	for (int w = 0; w < 0xC7; w++)
		printk(KERN_DEBUG "GR %p: %p\n", (void *) w,
		       (void *) vga_rgfx(NULL, w));
#endif
	switch (info->fix.accel) {
	case FB_ACCEL_NEOMAGIC_NM2070:
		videoRam = 896;
		maxClock = 65000;
		CursorMem = 2048;
		CursorOff = 0x100;
		linearSize = 1024;
		maxWidth = 1024;
		maxHeight = 1024;
		break;
	case FB_ACCEL_NEOMAGIC_NM2090:
	case FB_ACCEL_NEOMAGIC_NM2093:
		videoRam = 1152;
		maxClock = 80000;
		CursorMem = 2048;
		CursorOff = 0x100;
		linearSize = 2048;
		maxWidth = 1024;
		maxHeight = 1024;
		break;
	case FB_ACCEL_NEOMAGIC_NM2097:
		videoRam = 1152;
		maxClock = 80000;
		CursorMem = 1024;
		CursorOff = 0x100;
		linearSize = 2048;
		maxWidth = 1024;
		maxHeight = 1024;
		break;
	case FB_ACCEL_NEOMAGIC_NM2160:
		videoRam = 2048;
		maxClock = 90000;
		CursorMem = 1024;
		CursorOff = 0x100;
		linearSize = 2048;
		maxWidth = 1024;
		maxHeight = 1024;
		break;
	case FB_ACCEL_NEOMAGIC_NM2200:
		videoRam = 2560;
		maxClock = 110000;
		CursorMem = 1024;
		CursorOff = 0x1000;
		linearSize = 4096;
		maxWidth = 1280;
		maxHeight = 1024;	/* ???? */

		par->neo2200 = (Neo2200 __iomem *) par->mmio_vbase;
		break;
	case FB_ACCEL_NEOMAGIC_NM2230:
		videoRam = 3008;
		maxClock = 110000;
		CursorMem = 1024;
		CursorOff = 0x1000;
		linearSize = 4096;
		maxWidth = 1280;
		maxHeight = 1024;	/* ???? */

		par->neo2200 = (Neo2200 __iomem *) par->mmio_vbase;
		break;
	case FB_ACCEL_NEOMAGIC_NM2360:
		videoRam = 4096;
		maxClock = 110000;
		CursorMem = 1024;
		CursorOff = 0x1000;
		linearSize = 4096;
		maxWidth = 1280;
		maxHeight = 1024;	/* ???? */

		par->neo2200 = (Neo2200 __iomem *) par->mmio_vbase;
		break;
	case FB_ACCEL_NEOMAGIC_NM2380:
		videoRam = 6144;
		maxClock = 110000;
		CursorMem = 1024;
		CursorOff = 0x1000;
		linearSize = 8192;
		maxWidth = 1280;
		maxHeight = 1024;	/* ???? */

		par->neo2200 = (Neo2200 __iomem *) par->mmio_vbase;
		break;
	}
/*
	info->sprite.size = CursorMem;
	info->sprite.scan_align = 1;
	info->sprite.buf_align = 1;
	info->sprite.flags = FB_PIXMAP_IO;
	info->sprite.outbuf = neofb_draw_cursor;
*/
	par->maxClock = maxClock;
	par->cursorOff = CursorOff;
	return ((videoRam * 1024));
}


static struct fb_info *__devinit neo_alloc_fb_info(struct pci_dev *dev, const struct
						   pci_device_id *id)
{
	struct fb_info *info;
	struct neofb_par *par;

	info = framebuffer_alloc(sizeof(struct neofb_par) + sizeof(u32) * 256, &dev->dev);

	if (!info)
		return NULL;

	par = info->par;

	info->fix.accel = id->driver_data;

	par->pci_burst = !nopciburst;
	par->lcd_stretch = !nostretch;
	par->libretto = libretto;

	par->internal_display = internal;
	par->external_display = external;
	info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;

	switch (info->fix.accel) {
	case FB_ACCEL_NEOMAGIC_NM2070:
		sprintf(info->fix.id, "MagicGraph 128");
		break;
	case FB_ACCEL_NEOMAGIC_NM2090:
		sprintf(info->fix.id, "MagicGraph 128V");
		break;
	case FB_ACCEL_NEOMAGIC_NM2093:
		sprintf(info->fix.id, "MagicGraph 128ZV");
		break;
	case FB_ACCEL_NEOMAGIC_NM2097:
		sprintf(info->fix.id, "MagicGraph 128ZV+");
		break;
	case FB_ACCEL_NEOMAGIC_NM2160:
		sprintf(info->fix.id, "MagicGraph 128XD");
		break;
	case FB_ACCEL_NEOMAGIC_NM2200:
		sprintf(info->fix.id, "MagicGraph 256AV");
		info->flags |= FBINFO_HWACCEL_IMAGEBLIT |
		               FBINFO_HWACCEL_COPYAREA |
                	       FBINFO_HWACCEL_FILLRECT;
		break;
	case FB_ACCEL_NEOMAGIC_NM2230:
		sprintf(info->fix.id, "MagicGraph 256AV+");
		info->flags |= FBINFO_HWACCEL_IMAGEBLIT |
		               FBINFO_HWACCEL_COPYAREA |
                	       FBINFO_HWACCEL_FILLRECT;
		break;
	case FB_ACCEL_NEOMAGIC_NM2360:
		sprintf(info->fix.id, "MagicGraph 256ZX");
		info->flags |= FBINFO_HWACCEL_IMAGEBLIT |
		               FBINFO_HWACCEL_COPYAREA |
                	       FBINFO_HWACCEL_FILLRECT;
		break;
	case FB_ACCEL_NEOMAGIC_NM2380:
		sprintf(info->fix.id, "MagicGraph 256XL+");
		info->flags |= FBINFO_HWACCEL_IMAGEBLIT |
		               FBINFO_HWACCEL_COPYAREA |
                	       FBINFO_HWACCEL_FILLRECT;
		break;
	}

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux = 0;
	info->fix.xpanstep = 0;
	info->fix.ypanstep = 4;
	info->fix.ywrapstep = 0;
	info->fix.accel = id->driver_data;

	info->fbops = &neofb_ops;
	info->pseudo_palette = (void *) (par + 1);
	return info;
}

static void neo_free_fb_info(struct fb_info *info)
{
	if (info) {
		/*
		 * Free the colourmap
		 */
		fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}
}

/* --------------------------------------------------------------------- */

static int __devinit neofb_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	struct fb_info *info;
	u_int h_sync, v_sync;
	int video_len, err;

	DBG("neofb_probe");

	err = pci_enable_device(dev);
	if (err)
		return err;

	err = -ENOMEM;
	info = neo_alloc_fb_info(dev, id);
	if (!info)
		return err;

	err = neo_map_mmio(info, dev);
	if (err)
		goto err_map_mmio;

	err = neo_scan_monitor(info);
	if (err)
		goto err_scan_monitor;

	video_len = neo_init_hw(info);
	if (video_len < 0) {
		err = video_len;
		goto err_init_hw;
	}

	err = neo_map_video(info, dev, video_len);
	if (err)
		goto err_init_hw;

	if (!fb_find_mode(&info->var, info, mode_option, NULL, 0,
			info->monspecs.modedb, 16)) {
		printk(KERN_ERR "neofb: Unable to find usable video mode.\n");
		goto err_map_video;
	}

	/*
	 * Calculate the hsync and vsync frequencies.  Note that
	 * we split the 1e12 constant up so that we can preserve
	 * the precision and fit the results into 32-bit registers.
	 *  (1953125000 * 512 = 1e12)
	 */
	h_sync = 1953125000 / info->var.pixclock;
	h_sync =
	    h_sync * 512 / (info->var.xres + info->var.left_margin +
			    info->var.right_margin + info->var.hsync_len);
	v_sync =
	    h_sync / (info->var.yres + info->var.upper_margin +
		      info->var.lower_margin + info->var.vsync_len);

	printk(KERN_INFO "neofb v" NEOFB_VERSION
	       ": %dkB VRAM, using %dx%d, %d.%03dkHz, %dHz\n",
	       info->fix.smem_len >> 10, info->var.xres,
	       info->var.yres, h_sync / 1000, h_sync % 1000, v_sync);

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0)
		goto err_map_video;

	err = register_framebuffer(info);
	if (err < 0)
		goto err_reg_fb;

	printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       info->node, info->fix.id);

	/*
	 * Our driver data
	 */
	pci_set_drvdata(dev, info);
	return 0;

err_reg_fb:
	fb_dealloc_cmap(&info->cmap);
err_map_video:
	neo_unmap_video(info);
err_init_hw:
	fb_destroy_modedb(info->monspecs.modedb);
err_scan_monitor:
	neo_unmap_mmio(info);
err_map_mmio:
	neo_free_fb_info(info);
	return err;
}

static void __devexit neofb_remove(struct pci_dev *dev)
{
	struct fb_info *info = pci_get_drvdata(dev);

	DBG("neofb_remove");

	if (info) {
		/*
		 * If unregister_framebuffer fails, then
		 * we will be leaving hooks that could cause
		 * oopsen laying around.
		 */
		if (unregister_framebuffer(info))
			printk(KERN_WARNING
			       "neofb: danger danger!  Oopsen imminent!\n");

		neo_unmap_video(info);
		fb_destroy_modedb(info->monspecs.modedb);
		neo_unmap_mmio(info);
		neo_free_fb_info(info);

		/*
		 * Ensure that the driver data is no longer
		 * valid.
		 */
		pci_set_drvdata(dev, NULL);
	}
}

static struct pci_device_id neofb_devices[] = {
	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2070,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2070},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2090,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2090},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2093,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2093},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2097,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2097},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2160,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2160},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2200,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2200},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2230,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2230},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2360,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2360},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2380,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2380},

	{0, 0, 0, 0, 0, 0, 0}
};

MODULE_DEVICE_TABLE(pci, neofb_devices);

static struct pci_driver neofb_driver = {
	.name =		"neofb",
	.id_table =	neofb_devices,
	.probe =	neofb_probe,
	.remove =	__devexit_p(neofb_remove)
};

/* ************************* init in-kernel code ************************** */

#ifndef MODULE
static int __init neofb_setup(char *options)
{
	char *this_opt;

	DBG("neofb_setup");

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;

		if (!strncmp(this_opt, "internal", 8))
			internal = 1;
		else if (!strncmp(this_opt, "external", 8))
			external = 1;
		else if (!strncmp(this_opt, "nostretch", 9))
			nostretch = 1;
		else if (!strncmp(this_opt, "nopciburst", 10))
			nopciburst = 1;
		else if (!strncmp(this_opt, "libretto", 8))
			libretto = 1;
		else
			mode_option = this_opt;
	}
	return 0;
}
#endif  /*  MODULE  */

static int __init neofb_init(void)
{
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("neofb", &option))
		return -ENODEV;
	neofb_setup(option);
#endif
	return pci_register_driver(&neofb_driver);
}

module_init(neofb_init);

#ifdef MODULE
static void __exit neofb_exit(void)
{
	pci_unregister_driver(&neofb_driver);
}

module_exit(neofb_exit);
#endif				/* MODULE */
