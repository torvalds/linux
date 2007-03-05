/*
 * linux/drivers/video/riva/fbdev.c - nVidia RIVA 128/TNT/TNT2 fb driver
 *
 * Maintained by Ani Joshi <ajoshi@shell.unixbox.com>
 *
 * Copyright 1999-2000 Jeff Garzik
 *
 * Contributors:
 *
 *	Ani Joshi:  Lots of debugging and cleanup work, really helped
 *	get the driver going
 *
 *	Ferenc Bakonyi:  Bug fixes, cleanup, modularization
 *
 *	Jindrich Makovicka:  Accel code help, hw cursor, mtrr
 *
 *	Paul Richards:  Bug fixes, updates
 *
 * Initial template from skeletonfb.c, created 28 Dec 1997 by Geert Uytterhoeven
 * Includes riva_hw.c from nVidia, see copyright below.
 * KGI code provided the basis for state storage, init, and mode switching.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Known bugs and issues:
 *	restoring text mode fails
 *	doublescan modes are broken
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/backlight.h>
#include <linux/bitrev.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif
#ifdef CONFIG_PPC_OF
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#endif
#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/machdep.h>
#include <asm/backlight.h>
#endif

#include "rivafb.h"
#include "nvreg.h"

#ifndef CONFIG_PCI		/* sanity check */
#error This driver requires PCI support.
#endif

/* version number of this driver */
#define RIVAFB_VERSION "0.9.5b"

/* ------------------------------------------------------------------------- *
 *
 * various helpful macros and constants
 *
 * ------------------------------------------------------------------------- */
#ifdef CONFIG_FB_RIVA_DEBUG
#define NVTRACE          printk
#else
#define NVTRACE          if(0) printk
#endif

#define NVTRACE_ENTER(...)  NVTRACE("%s START\n", __FUNCTION__)
#define NVTRACE_LEAVE(...)  NVTRACE("%s END\n", __FUNCTION__)

#ifdef CONFIG_FB_RIVA_DEBUG
#define assert(expr) \
	if(!(expr)) { \
	printk( "Assertion failed! %s,%s,%s,line=%d\n",\
	#expr,__FILE__,__FUNCTION__,__LINE__); \
	BUG(); \
	}
#else
#define assert(expr)
#endif

#define PFX "rivafb: "

/* macro that allows you to set overflow bits */
#define SetBitField(value,from,to) SetBF(to,GetBF(value,from))
#define SetBit(n)		(1<<(n))
#define Set8Bits(value)		((value)&0xff)

/* HW cursor parameters */
#define MAX_CURS		32

/* ------------------------------------------------------------------------- *
 *
 * prototypes
 *
 * ------------------------------------------------------------------------- */

static int rivafb_blank(int blank, struct fb_info *info);

/* ------------------------------------------------------------------------- *
 *
 * card identification
 *
 * ------------------------------------------------------------------------- */

static struct pci_device_id rivafb_pci_tbl[] = {
	{ PCI_VENDOR_ID_NVIDIA_SGS, PCI_DEVICE_ID_NVIDIA_SGS_RIVA128,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_TNT,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_TNT2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_UTNT2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_VTNT2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_UVTNT2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_ITNT2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_SDR,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_DDR,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_MX,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_MX2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_GO,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO2_MXR,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_GTS,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_GTS2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE2_ULTRA,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO2_PRO,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_MX_460,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_MX_440,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	// NF2/IGP version, GeForce 4 MX, NV18
	{ PCI_VENDOR_ID_NVIDIA, 0x01f0,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_MX_420,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_440_GO,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_420_GO,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_420_GO_M32,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_500XGL,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_440_GO_M64,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_200,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_550XGL,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_500_GOGL,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_IGEFORCE2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE3,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE3_1,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE3_2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO_DDC,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_TI_4600,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_TI_4400,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE4_TI_4200,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
 	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_900XGL,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_750XGL,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_QUADRO4_700XGL,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_GEFORCE_FX_GO_5200,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, } /* terminate list */
};
MODULE_DEVICE_TABLE(pci, rivafb_pci_tbl);

/* ------------------------------------------------------------------------- *
 *
 * global variables
 *
 * ------------------------------------------------------------------------- */

/* command line data, set in rivafb_setup() */
static int flatpanel __devinitdata = -1; /* Autodetect later */
static int forceCRTC __devinitdata = -1;
static int noaccel   __devinitdata = 0;
#ifdef CONFIG_MTRR
static int nomtrr __devinitdata = 0;
#endif
#ifdef CONFIG_PMAC_BACKLIGHT
static int backlight __devinitdata = 1;
#else
static int backlight __devinitdata = 0;
#endif

static char *mode_option __devinitdata = NULL;
static int  strictmode       = 0;

static struct fb_fix_screeninfo __devinitdata rivafb_fix = {
	.type		= FB_TYPE_PACKED_PIXELS,
	.xpanstep	= 1,
	.ypanstep	= 1,
};

static struct fb_var_screeninfo __devinitdata rivafb_default_var = {
	.xres		= 640,
	.yres		= 480,
	.xres_virtual	= 640,
	.yres_virtual	= 480,
	.bits_per_pixel	= 8,
	.red		= {0, 8, 0},
	.green		= {0, 8, 0},
	.blue		= {0, 8, 0},
	.transp		= {0, 0, 0},
	.activate	= FB_ACTIVATE_NOW,
	.height		= -1,
	.width		= -1,
	.pixclock	= 39721,
	.left_margin	= 40,
	.right_margin	= 24,
	.upper_margin	= 32,
	.lower_margin	= 11,
	.hsync_len	= 96,
	.vsync_len	= 2,
	.vmode		= FB_VMODE_NONINTERLACED
};

/* from GGI */
static const struct riva_regs reg_template = {
	{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,	/* ATTR */
	 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	 0x41, 0x01, 0x0F, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* CRT  */
	 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE3,	/* 0x10 */
	 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 0x20 */
	 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 0x30 */
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00,							/* 0x40 */
	 },
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F,	/* GRA  */
	 0xFF},
	{0x03, 0x01, 0x0F, 0x00, 0x0E},				/* SEQ  */
	0xEB							/* MISC */
};

/*
 * Backlight control
 */
#ifdef CONFIG_FB_RIVA_BACKLIGHT
/* We do not have any information about which values are allowed, thus
 * we used safe values.
 */
#define MIN_LEVEL 0x158
#define MAX_LEVEL 0x534
#define LEVEL_STEP ((MAX_LEVEL - MIN_LEVEL) / FB_BACKLIGHT_MAX)

static struct backlight_properties riva_bl_data;

static int riva_bl_get_level_brightness(struct riva_par *par,
		int level)
{
	struct fb_info *info = pci_get_drvdata(par->pdev);
	int nlevel;

	/* Get and convert the value */
	/* No locking on bl_curve since accessing a single value */
	nlevel = MIN_LEVEL + info->bl_curve[level] * LEVEL_STEP;

	if (nlevel < 0)
		nlevel = 0;
	else if (nlevel < MIN_LEVEL)
		nlevel = MIN_LEVEL;
	else if (nlevel > MAX_LEVEL)
		nlevel = MAX_LEVEL;

	return nlevel;
}

static int riva_bl_update_status(struct backlight_device *bd)
{
	struct riva_par *par = class_get_devdata(&bd->class_dev);
	U032 tmp_pcrt, tmp_pmc;
	int level;

	if (bd->props.power != FB_BLANK_UNBLANK ||
	    bd->props.fb_blank != FB_BLANK_UNBLANK)
		level = 0;
	else
		level = bd->props.brightness;

	tmp_pmc = par->riva.PMC[0x10F0/4] & 0x0000FFFF;
	tmp_pcrt = par->riva.PCRTC0[0x081C/4] & 0xFFFFFFFC;
	if(level > 0) {
		tmp_pcrt |= 0x1;
		tmp_pmc |= (1 << 31); /* backlight bit */
		tmp_pmc |= riva_bl_get_level_brightness(par, level) << 16; /* level */
	}
	par->riva.PCRTC0[0x081C/4] = tmp_pcrt;
	par->riva.PMC[0x10F0/4] = tmp_pmc;

	return 0;
}

static int riva_bl_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static struct backlight_ops riva_bl_ops = {
	.get_brightness = riva_bl_get_brightness,
	.update_status	= riva_bl_update_status,
};

static void riva_bl_init(struct riva_par *par)
{
	struct fb_info *info = pci_get_drvdata(par->pdev);
	struct backlight_device *bd;
	char name[12];

	if (!par->FlatPanel)
		return;

#ifdef CONFIG_PMAC_BACKLIGHT
	if (!machine_is(powermac) ||
	    !pmac_has_backlight_type("mnca"))
		return;
#endif

	snprintf(name, sizeof(name), "rivabl%d", info->node);

	bd = backlight_device_register(name, info->dev, par, &riva_bl_ops);
	if (IS_ERR(bd)) {
		info->bl_dev = NULL;
		printk(KERN_WARNING "riva: Backlight registration failed\n");
		goto error;
	}

	info->bl_dev = bd;
	fb_bl_default_curve(info, 0,
		MIN_LEVEL * FB_BACKLIGHT_MAX / MAX_LEVEL,
		FB_BACKLIGHT_MAX);

	bd->props.max_brightness = FB_BACKLIGHT_LEVELS - 1;
	bd->props.brightness = riva_bl_data.max_brightness;
	bd->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(bd);

	printk("riva: Backlight initialized (%s)\n", name);

	return;

error:
	return;
}

static void riva_bl_exit(struct fb_info *info)
{
	struct backlight_device *bd = info->bl_dev;

	backlight_device_unregister(bd);
	printk("riva: Backlight unloaded\n");
}
#else
static inline void riva_bl_init(struct riva_par *par) {}
static inline void riva_bl_exit(struct fb_info *info) {}
#endif /* CONFIG_FB_RIVA_BACKLIGHT */

/* ------------------------------------------------------------------------- *
 *
 * MMIO access macros
 *
 * ------------------------------------------------------------------------- */

static inline void CRTCout(struct riva_par *par, unsigned char index,
			   unsigned char val)
{
	VGA_WR08(par->riva.PCIO, 0x3d4, index);
	VGA_WR08(par->riva.PCIO, 0x3d5, val);
}

static inline unsigned char CRTCin(struct riva_par *par,
				   unsigned char index)
{
	VGA_WR08(par->riva.PCIO, 0x3d4, index);
	return (VGA_RD08(par->riva.PCIO, 0x3d5));
}

static inline void GRAout(struct riva_par *par, unsigned char index,
			  unsigned char val)
{
	VGA_WR08(par->riva.PVIO, 0x3ce, index);
	VGA_WR08(par->riva.PVIO, 0x3cf, val);
}

static inline unsigned char GRAin(struct riva_par *par,
				  unsigned char index)
{
	VGA_WR08(par->riva.PVIO, 0x3ce, index);
	return (VGA_RD08(par->riva.PVIO, 0x3cf));
}

static inline void SEQout(struct riva_par *par, unsigned char index,
			  unsigned char val)
{
	VGA_WR08(par->riva.PVIO, 0x3c4, index);
	VGA_WR08(par->riva.PVIO, 0x3c5, val);
}

static inline unsigned char SEQin(struct riva_par *par,
				  unsigned char index)
{
	VGA_WR08(par->riva.PVIO, 0x3c4, index);
	return (VGA_RD08(par->riva.PVIO, 0x3c5));
}

static inline void ATTRout(struct riva_par *par, unsigned char index,
			   unsigned char val)
{
	VGA_WR08(par->riva.PCIO, 0x3c0, index);
	VGA_WR08(par->riva.PCIO, 0x3c0, val);
}

static inline unsigned char ATTRin(struct riva_par *par,
				   unsigned char index)
{
	VGA_WR08(par->riva.PCIO, 0x3c0, index);
	return (VGA_RD08(par->riva.PCIO, 0x3c1));
}

static inline void MISCout(struct riva_par *par, unsigned char val)
{
	VGA_WR08(par->riva.PVIO, 0x3c2, val);
}

static inline unsigned char MISCin(struct riva_par *par)
{
	return (VGA_RD08(par->riva.PVIO, 0x3cc));
}

static inline void reverse_order(u32 *l)
{
	u8 *a = (u8 *)l;
	a[0] = bitrev8(a[0]);
	a[1] = bitrev8(a[1]);
	a[2] = bitrev8(a[2]);
	a[3] = bitrev8(a[3]);
}

/* ------------------------------------------------------------------------- *
 *
 * cursor stuff
 *
 * ------------------------------------------------------------------------- */

/**
 * rivafb_load_cursor_image - load cursor image to hardware
 * @data: address to monochrome bitmap (1 = foreground color, 0 = background)
 * @par:  pointer to private data
 * @w:    width of cursor image in pixels
 * @h:    height of cursor image in scanlines
 * @bg:   background color (ARGB1555) - alpha bit determines opacity
 * @fg:   foreground color (ARGB1555)
 *
 * DESCRIPTiON:
 * Loads cursor image based on a monochrome source and mask bitmap.  The
 * image bits determines the color of the pixel, 0 for background, 1 for
 * foreground.  Only the affected region (as determined by @w and @h 
 * parameters) will be updated.
 *
 * CALLED FROM:
 * rivafb_cursor()
 */
static void rivafb_load_cursor_image(struct riva_par *par, u8 *data8,
				     u16 bg, u16 fg, u32 w, u32 h)
{
	int i, j, k = 0;
	u32 b, tmp;
	u32 *data = (u32 *)data8;
	bg = le16_to_cpu(bg);
	fg = le16_to_cpu(fg);

	w = (w + 1) & ~1;

	for (i = 0; i < h; i++) {
		b = *data++;
		reverse_order(&b);
		
		for (j = 0; j < w/2; j++) {
			tmp = 0;
#if defined (__BIG_ENDIAN)
			tmp = (b & (1 << 31)) ? fg << 16 : bg << 16;
			b <<= 1;
			tmp |= (b & (1 << 31)) ? fg : bg;
			b <<= 1;
#else
			tmp = (b & 1) ? fg : bg;
			b >>= 1;
			tmp |= (b & 1) ? fg << 16 : bg << 16;
			b >>= 1;
#endif
			writel(tmp, &par->riva.CURSOR[k++]);
		}
		k += (MAX_CURS - w)/2;
	}
}

/* ------------------------------------------------------------------------- *
 *
 * general utility functions
 *
 * ------------------------------------------------------------------------- */

/**
 * riva_wclut - set CLUT entry
 * @chip: pointer to RIVA_HW_INST object
 * @regnum: register number
 * @red: red component
 * @green: green component
 * @blue: blue component
 *
 * DESCRIPTION:
 * Sets color register @regnum.
 *
 * CALLED FROM:
 * rivafb_setcolreg()
 */
static void riva_wclut(RIVA_HW_INST *chip,
		       unsigned char regnum, unsigned char red,
		       unsigned char green, unsigned char blue)
{
	VGA_WR08(chip->PDIO, 0x3c8, regnum);
	VGA_WR08(chip->PDIO, 0x3c9, red);
	VGA_WR08(chip->PDIO, 0x3c9, green);
	VGA_WR08(chip->PDIO, 0x3c9, blue);
}

/**
 * riva_rclut - read fromCLUT register
 * @chip: pointer to RIVA_HW_INST object
 * @regnum: register number
 * @red: red component
 * @green: green component
 * @blue: blue component
 *
 * DESCRIPTION:
 * Reads red, green, and blue from color register @regnum.
 *
 * CALLED FROM:
 * rivafb_setcolreg()
 */
static void riva_rclut(RIVA_HW_INST *chip,
		       unsigned char regnum, unsigned char *red,
		       unsigned char *green, unsigned char *blue)
{
	
	VGA_WR08(chip->PDIO, 0x3c7, regnum);
	*red = VGA_RD08(chip->PDIO, 0x3c9);
	*green = VGA_RD08(chip->PDIO, 0x3c9);
	*blue = VGA_RD08(chip->PDIO, 0x3c9);
}

/**
 * riva_save_state - saves current chip state
 * @par: pointer to riva_par object containing info for current riva board
 * @regs: pointer to riva_regs object
 *
 * DESCRIPTION:
 * Saves current chip state to @regs.
 *
 * CALLED FROM:
 * rivafb_probe()
 */
/* from GGI */
static void riva_save_state(struct riva_par *par, struct riva_regs *regs)
{
	int i;

	NVTRACE_ENTER();
	par->riva.LockUnlock(&par->riva, 0);

	par->riva.UnloadStateExt(&par->riva, &regs->ext);

	regs->misc_output = MISCin(par);

	for (i = 0; i < NUM_CRT_REGS; i++)
		regs->crtc[i] = CRTCin(par, i);

	for (i = 0; i < NUM_ATC_REGS; i++)
		regs->attr[i] = ATTRin(par, i);

	for (i = 0; i < NUM_GRC_REGS; i++)
		regs->gra[i] = GRAin(par, i);

	for (i = 0; i < NUM_SEQ_REGS; i++)
		regs->seq[i] = SEQin(par, i);
	NVTRACE_LEAVE();
}

/**
 * riva_load_state - loads current chip state
 * @par: pointer to riva_par object containing info for current riva board
 * @regs: pointer to riva_regs object
 *
 * DESCRIPTION:
 * Loads chip state from @regs.
 *
 * CALLED FROM:
 * riva_load_video_mode()
 * rivafb_probe()
 * rivafb_remove()
 */
/* from GGI */
static void riva_load_state(struct riva_par *par, struct riva_regs *regs)
{
	RIVA_HW_STATE *state = &regs->ext;
	int i;

	NVTRACE_ENTER();
	CRTCout(par, 0x11, 0x00);

	par->riva.LockUnlock(&par->riva, 0);

	par->riva.LoadStateExt(&par->riva, state);

	MISCout(par, regs->misc_output);

	for (i = 0; i < NUM_CRT_REGS; i++) {
		switch (i) {
		case 0x19:
		case 0x20 ... 0x40:
			break;
		default:
			CRTCout(par, i, regs->crtc[i]);
		}
	}

	for (i = 0; i < NUM_ATC_REGS; i++)
		ATTRout(par, i, regs->attr[i]);

	for (i = 0; i < NUM_GRC_REGS; i++)
		GRAout(par, i, regs->gra[i]);

	for (i = 0; i < NUM_SEQ_REGS; i++)
		SEQout(par, i, regs->seq[i]);
	NVTRACE_LEAVE();
}

/**
 * riva_load_video_mode - calculate timings
 * @info: pointer to fb_info object containing info for current riva board
 *
 * DESCRIPTION:
 * Calculate some timings and then send em off to riva_load_state().
 *
 * CALLED FROM:
 * rivafb_set_par()
 */
static int riva_load_video_mode(struct fb_info *info)
{
	int bpp, width, hDisplaySize, hDisplay, hStart,
	    hEnd, hTotal, height, vDisplay, vStart, vEnd, vTotal, dotClock;
	int hBlankStart, hBlankEnd, vBlankStart, vBlankEnd;
	int rc;
	struct riva_par *par = info->par;
	struct riva_regs newmode;
	
	NVTRACE_ENTER();
	/* time to calculate */
	rivafb_blank(FB_BLANK_NORMAL, info);

	bpp = info->var.bits_per_pixel;
	if (bpp == 16 && info->var.green.length == 5)
		bpp = 15;
	width = info->var.xres_virtual;
	hDisplaySize = info->var.xres;
	hDisplay = (hDisplaySize / 8) - 1;
	hStart = (hDisplaySize + info->var.right_margin) / 8 - 1;
	hEnd = (hDisplaySize + info->var.right_margin +
		info->var.hsync_len) / 8 - 1;
	hTotal = (hDisplaySize + info->var.right_margin +
		  info->var.hsync_len + info->var.left_margin) / 8 - 5;
	hBlankStart = hDisplay;
	hBlankEnd = hTotal + 4;

	height = info->var.yres_virtual;
	vDisplay = info->var.yres - 1;
	vStart = info->var.yres + info->var.lower_margin - 1;
	vEnd = info->var.yres + info->var.lower_margin +
	       info->var.vsync_len - 1;
	vTotal = info->var.yres + info->var.lower_margin +
		 info->var.vsync_len + info->var.upper_margin + 2;
	vBlankStart = vDisplay;
	vBlankEnd = vTotal + 1;
	dotClock = 1000000000 / info->var.pixclock;

	memcpy(&newmode, &reg_template, sizeof(struct riva_regs));

	if ((info->var.vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED)
		vTotal |= 1;

	if (par->FlatPanel) {
		vStart = vTotal - 3;
		vEnd = vTotal - 2;
		vBlankStart = vStart;
		hStart = hTotal - 3;
		hEnd = hTotal - 2;
		hBlankEnd = hTotal + 4;
	}

	newmode.crtc[0x0] = Set8Bits (hTotal); 
	newmode.crtc[0x1] = Set8Bits (hDisplay);
	newmode.crtc[0x2] = Set8Bits (hBlankStart);
	newmode.crtc[0x3] = SetBitField (hBlankEnd, 4: 0, 4:0) | SetBit (7);
	newmode.crtc[0x4] = Set8Bits (hStart);
	newmode.crtc[0x5] = SetBitField (hBlankEnd, 5: 5, 7:7)
		| SetBitField (hEnd, 4: 0, 4:0);
	newmode.crtc[0x6] = SetBitField (vTotal, 7: 0, 7:0);
	newmode.crtc[0x7] = SetBitField (vTotal, 8: 8, 0:0)
		| SetBitField (vDisplay, 8: 8, 1:1)
		| SetBitField (vStart, 8: 8, 2:2)
		| SetBitField (vBlankStart, 8: 8, 3:3)
		| SetBit (4)
		| SetBitField (vTotal, 9: 9, 5:5)
		| SetBitField (vDisplay, 9: 9, 6:6)
		| SetBitField (vStart, 9: 9, 7:7);
	newmode.crtc[0x9] = SetBitField (vBlankStart, 9: 9, 5:5)
		| SetBit (6);
	newmode.crtc[0x10] = Set8Bits (vStart);
	newmode.crtc[0x11] = SetBitField (vEnd, 3: 0, 3:0)
		| SetBit (5);
	newmode.crtc[0x12] = Set8Bits (vDisplay);
	newmode.crtc[0x13] = (width / 8) * ((bpp + 1) / 8);
	newmode.crtc[0x15] = Set8Bits (vBlankStart);
	newmode.crtc[0x16] = Set8Bits (vBlankEnd);

	newmode.ext.screen = SetBitField(hBlankEnd,6:6,4:4)
		| SetBitField(vBlankStart,10:10,3:3)
		| SetBitField(vStart,10:10,2:2)
		| SetBitField(vDisplay,10:10,1:1)
		| SetBitField(vTotal,10:10,0:0);
	newmode.ext.horiz  = SetBitField(hTotal,8:8,0:0) 
		| SetBitField(hDisplay,8:8,1:1)
		| SetBitField(hBlankStart,8:8,2:2)
		| SetBitField(hStart,8:8,3:3);
	newmode.ext.extra  = SetBitField(vTotal,11:11,0:0)
		| SetBitField(vDisplay,11:11,2:2)
		| SetBitField(vStart,11:11,4:4)
		| SetBitField(vBlankStart,11:11,6:6); 

	if ((info->var.vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		int tmp = (hTotal >> 1) & ~1;
		newmode.ext.interlace = Set8Bits(tmp);
		newmode.ext.horiz |= SetBitField(tmp, 8:8,4:4);
	} else 
		newmode.ext.interlace = 0xff; /* interlace off */

	if (par->riva.Architecture >= NV_ARCH_10)
		par->riva.CURSOR = (U032 __iomem *)(info->screen_base + par->riva.CursorStart);

	if (info->var.sync & FB_SYNC_HOR_HIGH_ACT)
		newmode.misc_output &= ~0x40;
	else
		newmode.misc_output |= 0x40;
	if (info->var.sync & FB_SYNC_VERT_HIGH_ACT)
		newmode.misc_output &= ~0x80;
	else
		newmode.misc_output |= 0x80;	

	rc = CalcStateExt(&par->riva, &newmode.ext, bpp, width,
			  hDisplaySize, height, dotClock);
	if (rc)
		goto out;

	newmode.ext.scale = NV_RD32(par->riva.PRAMDAC, 0x00000848) &
		0xfff000ff;
	if (par->FlatPanel == 1) {
		newmode.ext.pixel |= (1 << 7);
		newmode.ext.scale |= (1 << 8);
	}
	if (par->SecondCRTC) {
		newmode.ext.head  = NV_RD32(par->riva.PCRTC0, 0x00000860) &
			~0x00001000;
		newmode.ext.head2 = NV_RD32(par->riva.PCRTC0, 0x00002860) |
			0x00001000;
		newmode.ext.crtcOwner = 3;
		newmode.ext.pllsel |= 0x20000800;
		newmode.ext.vpll2 = newmode.ext.vpll;
	} else if (par->riva.twoHeads) {
		newmode.ext.head  =  NV_RD32(par->riva.PCRTC0, 0x00000860) |
			0x00001000;
		newmode.ext.head2 =  NV_RD32(par->riva.PCRTC0, 0x00002860) &
			~0x00001000;
		newmode.ext.crtcOwner = 0;
		newmode.ext.vpll2 = NV_RD32(par->riva.PRAMDAC0, 0x00000520);
	}
	if (par->FlatPanel == 1) {
		newmode.ext.pixel |= (1 << 7);
		newmode.ext.scale |= (1 << 8);
	}
	newmode.ext.cursorConfig = 0x02000100;
	par->current_state = newmode;
	riva_load_state(par, &par->current_state);
	par->riva.LockUnlock(&par->riva, 0); /* important for HW cursor */

out:
	rivafb_blank(FB_BLANK_UNBLANK, info);
	NVTRACE_LEAVE();

	return rc;
}

static void riva_update_var(struct fb_var_screeninfo *var,
			    const struct fb_videomode *modedb)
{
	NVTRACE_ENTER();
	var->xres = var->xres_virtual = modedb->xres;
	var->yres = modedb->yres;
        if (var->yres_virtual < var->yres)
	    var->yres_virtual = var->yres;
        var->xoffset = var->yoffset = 0;
        var->pixclock = modedb->pixclock;
        var->left_margin = modedb->left_margin;
        var->right_margin = modedb->right_margin;
        var->upper_margin = modedb->upper_margin;
        var->lower_margin = modedb->lower_margin;
        var->hsync_len = modedb->hsync_len;
        var->vsync_len = modedb->vsync_len;
        var->sync = modedb->sync;
        var->vmode = modedb->vmode;
	NVTRACE_LEAVE();
}

/**
 * rivafb_do_maximize - 
 * @info: pointer to fb_info object containing info for current riva board
 * @var:
 * @nom:
 * @den:
 *
 * DESCRIPTION:
 * .
 *
 * RETURNS:
 * -EINVAL on failure, 0 on success
 * 
 *
 * CALLED FROM:
 * rivafb_check_var()
 */
static int rivafb_do_maximize(struct fb_info *info,
			      struct fb_var_screeninfo *var,
			      int nom, int den)
{
	static struct {
		int xres, yres;
	} modes[] = {
		{1600, 1280},
		{1280, 1024},
		{1024, 768},
		{800, 600},
		{640, 480},
		{-1, -1}
	};
	int i;

	NVTRACE_ENTER();
	/* use highest possible virtual resolution */
	if (var->xres_virtual == -1 && var->yres_virtual == -1) {
		printk(KERN_WARNING PFX
		       "using maximum available virtual resolution\n");
		for (i = 0; modes[i].xres != -1; i++) {
			if (modes[i].xres * nom / den * modes[i].yres <
			    info->fix.smem_len)
				break;
		}
		if (modes[i].xres == -1) {
			printk(KERN_ERR PFX
			       "could not find a virtual resolution that fits into video memory!!\n");
			NVTRACE("EXIT - EINVAL error\n");
			return -EINVAL;
		}
		var->xres_virtual = modes[i].xres;
		var->yres_virtual = modes[i].yres;

		printk(KERN_INFO PFX
		       "virtual resolution set to maximum of %dx%d\n",
		       var->xres_virtual, var->yres_virtual);
	} else if (var->xres_virtual == -1) {
		var->xres_virtual = (info->fix.smem_len * den /
			(nom * var->yres_virtual)) & ~15;
		printk(KERN_WARNING PFX
		       "setting virtual X resolution to %d\n", var->xres_virtual);
	} else if (var->yres_virtual == -1) {
		var->xres_virtual = (var->xres_virtual + 15) & ~15;
		var->yres_virtual = info->fix.smem_len * den /
			(nom * var->xres_virtual);
		printk(KERN_WARNING PFX
		       "setting virtual Y resolution to %d\n", var->yres_virtual);
	} else {
		var->xres_virtual = (var->xres_virtual + 15) & ~15;
		if (var->xres_virtual * nom / den * var->yres_virtual > info->fix.smem_len) {
			printk(KERN_ERR PFX
			       "mode %dx%dx%d rejected...resolution too high to fit into video memory!\n",
			       var->xres, var->yres, var->bits_per_pixel);
			NVTRACE("EXIT - EINVAL error\n");
			return -EINVAL;
		}
	}
	
	if (var->xres_virtual * nom / den >= 8192) {
		printk(KERN_WARNING PFX
		       "virtual X resolution (%d) is too high, lowering to %d\n",
		       var->xres_virtual, 8192 * den / nom - 16);
		var->xres_virtual = 8192 * den / nom - 16;
	}
	
	if (var->xres_virtual < var->xres) {
		printk(KERN_ERR PFX
		       "virtual X resolution (%d) is smaller than real\n", var->xres_virtual);
		return -EINVAL;
	}

	if (var->yres_virtual < var->yres) {
		printk(KERN_ERR PFX
		       "virtual Y resolution (%d) is smaller than real\n", var->yres_virtual);
		return -EINVAL;
	}
	if (var->yres_virtual > 0x7fff/nom)
		var->yres_virtual = 0x7fff/nom;
	if (var->xres_virtual > 0x7fff/nom)
		var->xres_virtual = 0x7fff/nom;
	NVTRACE_LEAVE();
	return 0;
}

static void
riva_set_pattern(struct riva_par *par, int clr0, int clr1, int pat0, int pat1)
{
	RIVA_FIFO_FREE(par->riva, Patt, 4);
	NV_WR32(&par->riva.Patt->Color0, 0, clr0);
	NV_WR32(&par->riva.Patt->Color1, 0, clr1);
	NV_WR32(par->riva.Patt->Monochrome, 0, pat0);
	NV_WR32(par->riva.Patt->Monochrome, 4, pat1);
}

/* acceleration routines */
static inline void wait_for_idle(struct riva_par *par)
{
	while (par->riva.Busy(&par->riva));
}

/*
 * Set ROP.  Translate X rop into ROP3.  Internal routine.
 */
static void
riva_set_rop_solid(struct riva_par *par, int rop)
{
	riva_set_pattern(par, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
        RIVA_FIFO_FREE(par->riva, Rop, 1);
        NV_WR32(&par->riva.Rop->Rop3, 0, rop);

}

static void riva_setup_accel(struct fb_info *info)
{
	struct riva_par *par = info->par;

	RIVA_FIFO_FREE(par->riva, Clip, 2);
	NV_WR32(&par->riva.Clip->TopLeft, 0, 0x0);
	NV_WR32(&par->riva.Clip->WidthHeight, 0,
		(info->var.xres_virtual & 0xffff) |
		(info->var.yres_virtual << 16));
	riva_set_rop_solid(par, 0xcc);
	wait_for_idle(par);
}

/**
 * riva_get_cmap_len - query current color map length
 * @var: standard kernel fb changeable data
 *
 * DESCRIPTION:
 * Get current color map length.
 *
 * RETURNS:
 * Length of color map
 *
 * CALLED FROM:
 * rivafb_setcolreg()
 */
static int riva_get_cmap_len(const struct fb_var_screeninfo *var)
{
	int rc = 256;		/* reasonable default */

	switch (var->green.length) {
	case 8:
		rc = 256;	/* 256 entries (2^8), 8 bpp and RGB8888 */
		break;
	case 5:
		rc = 32;	/* 32 entries (2^5), 16 bpp, RGB555 */
		break;
	case 6:
		rc = 64;	/* 64 entries (2^6), 16 bpp, RGB565 */
		break;		
	default:
		/* should not occur */
		break;
	}
	return rc;
}

/* ------------------------------------------------------------------------- *
 *
 * framebuffer operations
 *
 * ------------------------------------------------------------------------- */

static int rivafb_open(struct fb_info *info, int user)
{
	struct riva_par *par = info->par;

	NVTRACE_ENTER();
	mutex_lock(&par->open_lock);
	if (!par->ref_count) {
#ifdef CONFIG_X86
		memset(&par->state, 0, sizeof(struct vgastate));
		par->state.flags = VGA_SAVE_MODE  | VGA_SAVE_FONTS;
		/* save the DAC for Riva128 */
		if (par->riva.Architecture == NV_ARCH_03)
			par->state.flags |= VGA_SAVE_CMAP;
		save_vga(&par->state);
#endif
		/* vgaHWunlock() + riva unlock (0x7F) */
		CRTCout(par, 0x11, 0xFF);
		par->riva.LockUnlock(&par->riva, 0);
	
		riva_save_state(par, &par->initial_state);
	}
	par->ref_count++;
	mutex_unlock(&par->open_lock);
	NVTRACE_LEAVE();
	return 0;
}

static int rivafb_release(struct fb_info *info, int user)
{
	struct riva_par *par = info->par;

	NVTRACE_ENTER();
	mutex_lock(&par->open_lock);
	if (!par->ref_count) {
		mutex_unlock(&par->open_lock);
		return -EINVAL;
	}
	if (par->ref_count == 1) {
		par->riva.LockUnlock(&par->riva, 0);
		par->riva.LoadStateExt(&par->riva, &par->initial_state.ext);
		riva_load_state(par, &par->initial_state);
#ifdef CONFIG_X86
		restore_vga(&par->state);
#endif
		par->riva.LockUnlock(&par->riva, 1);
	}
	par->ref_count--;
	mutex_unlock(&par->open_lock);
	NVTRACE_LEAVE();
	return 0;
}

static int rivafb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	const struct fb_videomode *mode;
	struct riva_par *par = info->par;
	int nom, den;		/* translating from pixels->bytes */
	int mode_valid = 0;
	
	NVTRACE_ENTER();
	switch (var->bits_per_pixel) {
	case 1 ... 8:
		var->red.offset = var->green.offset = var->blue.offset = 0;
		var->red.length = var->green.length = var->blue.length = 8;
		var->bits_per_pixel = 8;
		nom = den = 1;
		break;
	case 9 ... 15:
		var->green.length = 5;
		/* fall through */
	case 16:
		var->bits_per_pixel = 16;
		/* The Riva128 supports RGB555 only */
		if (par->riva.Architecture == NV_ARCH_03)
			var->green.length = 5;
		if (var->green.length == 5) {
			/* 0rrrrrgg gggbbbbb */
			var->red.offset = 10;
			var->green.offset = 5;
			var->blue.offset = 0;
			var->red.length = 5;
			var->green.length = 5;
			var->blue.length = 5;
		} else {
			/* rrrrrggg gggbbbbb */
			var->red.offset = 11;
			var->green.offset = 5;
			var->blue.offset = 0;
			var->red.length = 5;
			var->green.length = 6;
			var->blue.length = 5;
		}
		nom = 2;
		den = 1;
		break;
	case 17 ... 32:
		var->red.length = var->green.length = var->blue.length = 8;
		var->bits_per_pixel = 32;
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		nom = 4;
		den = 1;
		break;
	default:
		printk(KERN_ERR PFX
		       "mode %dx%dx%d rejected...color depth not supported.\n",
		       var->xres, var->yres, var->bits_per_pixel);
		NVTRACE("EXIT, returning -EINVAL\n");
		return -EINVAL;
	}

	if (!strictmode) {
		if (!info->monspecs.vfmax || !info->monspecs.hfmax ||
		    !info->monspecs.dclkmax || !fb_validate_mode(var, info))
			mode_valid = 1;
	}

	/* calculate modeline if supported by monitor */
	if (!mode_valid && info->monspecs.gtf) {
		if (!fb_get_mode(FB_MAXTIMINGS, 0, var, info))
			mode_valid = 1;
	}

	if (!mode_valid) {
		mode = fb_find_best_mode(var, &info->modelist);
		if (mode) {
			riva_update_var(var, mode);
			mode_valid = 1;
		}
	}

	if (!mode_valid && info->monspecs.modedb_len)
		return -EINVAL;

	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	if (var->yres_virtual <= var->yres)
		var->yres_virtual = -1;
	if (rivafb_do_maximize(info, var, nom, den) < 0)
		return -EINVAL;

	if (var->xoffset < 0)
		var->xoffset = 0;
	if (var->yoffset < 0)
		var->yoffset = 0;

	/* truncate xoffset and yoffset to maximum if too high */
	if (var->xoffset > var->xres_virtual - var->xres)
		var->xoffset = var->xres_virtual - var->xres - 1;

	if (var->yoffset > var->yres_virtual - var->yres)
		var->yoffset = var->yres_virtual - var->yres - 1;

	var->red.msb_right = 
	    var->green.msb_right =
	    var->blue.msb_right =
	    var->transp.offset = var->transp.length = var->transp.msb_right = 0;
	NVTRACE_LEAVE();
	return 0;
}

static int rivafb_set_par(struct fb_info *info)
{
	struct riva_par *par = info->par;
	int rc = 0;

	NVTRACE_ENTER();
	/* vgaHWunlock() + riva unlock (0x7F) */
	CRTCout(par, 0x11, 0xFF);
	par->riva.LockUnlock(&par->riva, 0);
	rc = riva_load_video_mode(info);
	if (rc)
		goto out;
	if(!(info->flags & FBINFO_HWACCEL_DISABLED))
		riva_setup_accel(info);
	
	par->cursor_reset = 1;
	info->fix.line_length = (info->var.xres_virtual * (info->var.bits_per_pixel >> 3));
	info->fix.visual = (info->var.bits_per_pixel == 8) ?
				FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;

	if (info->flags & FBINFO_HWACCEL_DISABLED)
		info->pixmap.scan_align = 1;
	else
		info->pixmap.scan_align = 4;

out:
	NVTRACE_LEAVE();
	return rc;
}

/**
 * rivafb_pan_display
 * @var: standard kernel fb changeable data
 * @con: TODO
 * @info: pointer to fb_info object containing info for current riva board
 *
 * DESCRIPTION:
 * Pan (or wrap, depending on the `vmode' field) the display using the
 * `xoffset' and `yoffset' fields of the `var' structure.
 * If the values don't fit, return -EINVAL.
 *
 * This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
 */
static int rivafb_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct riva_par *par = info->par;
	unsigned int base;

	NVTRACE_ENTER();
	base = var->yoffset * info->fix.line_length + var->xoffset;
	par->riva.SetStartAddress(&par->riva, base);
	NVTRACE_LEAVE();
	return 0;
}

static int rivafb_blank(int blank, struct fb_info *info)
{
	struct riva_par *par= info->par;
	unsigned char tmp, vesa;

	tmp = SEQin(par, 0x01) & ~0x20;	/* screen on/off */
	vesa = CRTCin(par, 0x1a) & ~0xc0;	/* sync on/off */

	NVTRACE_ENTER();

	if (blank)
		tmp |= 0x20;

	switch (blank) {
	case FB_BLANK_UNBLANK:
	case FB_BLANK_NORMAL:
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		vesa |= 0x80;
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		vesa |= 0x40;
		break;
	case FB_BLANK_POWERDOWN:
		vesa |= 0xc0;
		break;
	}

	SEQout(par, 0x01, tmp);
	CRTCout(par, 0x1a, vesa);

	NVTRACE_LEAVE();

	return 0;
}

/**
 * rivafb_setcolreg
 * @regno: register index
 * @red: red component
 * @green: green component
 * @blue: blue component
 * @transp: transparency
 * @info: pointer to fb_info object containing info for current riva board
 *
 * DESCRIPTION:
 * Set a single color register. The values supplied have a 16 bit
 * magnitude.
 *
 * RETURNS:
 * Return != 0 for invalid regno.
 *
 * CALLED FROM:
 * fbcmap.c:fb_set_cmap()
 */
static int rivafb_setcolreg(unsigned regno, unsigned red, unsigned green,
			  unsigned blue, unsigned transp,
			  struct fb_info *info)
{
	struct riva_par *par = info->par;
	RIVA_HW_INST *chip = &par->riva;
	int i;

	if (regno >= riva_get_cmap_len(&info->var))
			return -EINVAL;

	if (info->var.grayscale) {
		/* gray = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue =
		    (red * 77 + green * 151 + blue * 28) >> 8;
	}

	if (regno < 16 && info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		((u32 *) info->pseudo_palette)[regno] =
			(regno << info->var.red.offset) |
			(regno << info->var.green.offset) |
			(regno << info->var.blue.offset);
		/*
		 * The Riva128 2D engine requires color information in
		 * TrueColor format even if framebuffer is in DirectColor
		 */
		if (par->riva.Architecture == NV_ARCH_03) {
			switch (info->var.bits_per_pixel) {
			case 16:
				par->palette[regno] = ((red & 0xf800) >> 1) |
					((green & 0xf800) >> 6) |
					((blue & 0xf800) >> 11);
				break;
			case 32:
				par->palette[regno] = ((red & 0xff00) << 8) |
					((green & 0xff00)) |
					((blue & 0xff00) >> 8);
				break;
			}
		}
	}

	switch (info->var.bits_per_pixel) {
	case 8:
		/* "transparent" stuff is completely ignored. */
		riva_wclut(chip, regno, red >> 8, green >> 8, blue >> 8);
		break;
	case 16:
		if (info->var.green.length == 5) {
			for (i = 0; i < 8; i++) {
				riva_wclut(chip, regno*8+i, red >> 8,
					   green >> 8, blue >> 8);
			}
		} else {
			u8 r, g, b;

			if (regno < 32) {
				for (i = 0; i < 8; i++) {
					riva_wclut(chip, regno*8+i,
						   red >> 8, green >> 8,
						   blue >> 8);
				}
			}
			riva_rclut(chip, regno*4, &r, &g, &b);
			for (i = 0; i < 4; i++)
				riva_wclut(chip, regno*4+i, r,
					   green >> 8, b);
		}
		break;
	case 32:
		riva_wclut(chip, regno, red >> 8, green >> 8, blue >> 8);
		break;
	default:
		/* do nothing */
		break;
	}
	return 0;
}

/**
 * rivafb_fillrect - hardware accelerated color fill function
 * @info: pointer to fb_info structure
 * @rect: pointer to fb_fillrect structure
 *
 * DESCRIPTION:
 * This function fills up a region of framebuffer memory with a solid
 * color with a choice of two different ROP's, copy or invert.
 *
 * CALLED FROM:
 * framebuffer hook
 */
static void rivafb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct riva_par *par = info->par;
	u_int color, rop = 0;

	if ((info->flags & FBINFO_HWACCEL_DISABLED)) {
		cfb_fillrect(info, rect);
		return;
	}

	if (info->var.bits_per_pixel == 8)
		color = rect->color;
	else {
		if (par->riva.Architecture != NV_ARCH_03)
			color = ((u32 *)info->pseudo_palette)[rect->color];
		else
			color = par->palette[rect->color];
	}

	switch (rect->rop) {
	case ROP_XOR:
		rop = 0x66;
		break;
	case ROP_COPY:
	default:
		rop = 0xCC;
		break;
	}

	riva_set_rop_solid(par, rop);

	RIVA_FIFO_FREE(par->riva, Bitmap, 1);
	NV_WR32(&par->riva.Bitmap->Color1A, 0, color);

	RIVA_FIFO_FREE(par->riva, Bitmap, 2);
	NV_WR32(&par->riva.Bitmap->UnclippedRectangle[0].TopLeft, 0,
		(rect->dx << 16) | rect->dy);
	mb();
	NV_WR32(&par->riva.Bitmap->UnclippedRectangle[0].WidthHeight, 0,
		(rect->width << 16) | rect->height);
	mb();
	riva_set_rop_solid(par, 0xcc);

}

/**
 * rivafb_copyarea - hardware accelerated blit function
 * @info: pointer to fb_info structure
 * @region: pointer to fb_copyarea structure
 *
 * DESCRIPTION:
 * This copies an area of pixels from one location to another
 *
 * CALLED FROM:
 * framebuffer hook
 */
static void rivafb_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	struct riva_par *par = info->par;

	if ((info->flags & FBINFO_HWACCEL_DISABLED)) {
		cfb_copyarea(info, region);
		return;
	}

	RIVA_FIFO_FREE(par->riva, Blt, 3);
	NV_WR32(&par->riva.Blt->TopLeftSrc, 0,
		(region->sy << 16) | region->sx);
	NV_WR32(&par->riva.Blt->TopLeftDst, 0,
		(region->dy << 16) | region->dx);
	mb();
	NV_WR32(&par->riva.Blt->WidthHeight, 0,
		(region->height << 16) | region->width);
	mb();
}

static inline void convert_bgcolor_16(u32 *col)
{
	*col = ((*col & 0x0000F800) << 8)
		| ((*col & 0x00007E0) << 5)
		| ((*col & 0x0000001F) << 3)
		|	   0xFF000000;
	mb();
}

/**
 * rivafb_imageblit: hardware accelerated color expand function
 * @info: pointer to fb_info structure
 * @image: pointer to fb_image structure
 *
 * DESCRIPTION:
 * If the source is a monochrome bitmap, the function fills up a a region
 * of framebuffer memory with pixels whose color is determined by the bit
 * setting of the bitmap, 1 - foreground, 0 - background.
 *
 * If the source is not a monochrome bitmap, color expansion is not done.
 * In this case, it is channeled to a software function.
 *
 * CALLED FROM:
 * framebuffer hook
 */
static void rivafb_imageblit(struct fb_info *info, 
			     const struct fb_image *image)
{
	struct riva_par *par = info->par;
	u32 fgx = 0, bgx = 0, width, tmp;
	u8 *cdat = (u8 *) image->data;
	volatile u32 __iomem *d;
	int i, size;

	if ((info->flags & FBINFO_HWACCEL_DISABLED) || image->depth != 1) {
		cfb_imageblit(info, image);
		return;
	}

	switch (info->var.bits_per_pixel) {
	case 8:
		fgx = image->fg_color;
		bgx = image->bg_color;
		break;
	case 16:
	case 32:
		if (par->riva.Architecture != NV_ARCH_03) {
			fgx = ((u32 *)info->pseudo_palette)[image->fg_color];
			bgx = ((u32 *)info->pseudo_palette)[image->bg_color];
		} else {
			fgx = par->palette[image->fg_color];
			bgx = par->palette[image->bg_color];
		}
		if (info->var.green.length == 6)
			convert_bgcolor_16(&bgx);	
		break;
	}

	RIVA_FIFO_FREE(par->riva, Bitmap, 7);
	NV_WR32(&par->riva.Bitmap->ClipE.TopLeft, 0,
		(image->dy << 16) | (image->dx & 0xFFFF));
	NV_WR32(&par->riva.Bitmap->ClipE.BottomRight, 0,
		(((image->dy + image->height) << 16) |
		 ((image->dx + image->width) & 0xffff)));
	NV_WR32(&par->riva.Bitmap->Color0E, 0, bgx);
	NV_WR32(&par->riva.Bitmap->Color1E, 0, fgx);
	NV_WR32(&par->riva.Bitmap->WidthHeightInE, 0,
		(image->height << 16) | ((image->width + 31) & ~31));
	NV_WR32(&par->riva.Bitmap->WidthHeightOutE, 0,
		(image->height << 16) | ((image->width + 31) & ~31));
	NV_WR32(&par->riva.Bitmap->PointE, 0,
		(image->dy << 16) | (image->dx & 0xFFFF));

	d = &par->riva.Bitmap->MonochromeData01E;

	width = (image->width + 31)/32;
	size = width * image->height;
	while (size >= 16) {
		RIVA_FIFO_FREE(par->riva, Bitmap, 16);
		for (i = 0; i < 16; i++) {
			tmp = *((u32 *)cdat);
			cdat = (u8 *)((u32 *)cdat + 1);
			reverse_order(&tmp);
			NV_WR32(d, i*4, tmp);
		}
		size -= 16;
	}
	if (size) {
		RIVA_FIFO_FREE(par->riva, Bitmap, size);
		for (i = 0; i < size; i++) {
			tmp = *((u32 *) cdat);
			cdat = (u8 *)((u32 *)cdat + 1);
			reverse_order(&tmp);
			NV_WR32(d, i*4, tmp);
		}
	}
}

/**
 * rivafb_cursor - hardware cursor function
 * @info: pointer to info structure
 * @cursor: pointer to fbcursor structure
 *
 * DESCRIPTION:
 * A cursor function that supports displaying a cursor image via hardware.
 * Within the kernel, copy and invert rops are supported.  If exported
 * to user space, only the copy rop will be supported.
 *
 * CALLED FROM
 * framebuffer hook
 */
static int rivafb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct riva_par *par = info->par;
	u8 data[MAX_CURS * MAX_CURS/8];
	int i, set = cursor->set;
	u16 fg, bg;

	if (cursor->image.width > MAX_CURS || cursor->image.height > MAX_CURS)
		return -ENXIO;

	par->riva.ShowHideCursor(&par->riva, 0);

	if (par->cursor_reset) {
		set = FB_CUR_SETALL;
		par->cursor_reset = 0;
	}

	if (set & FB_CUR_SETSIZE)
		memset_io(par->riva.CURSOR, 0, MAX_CURS * MAX_CURS * 2);

	if (set & FB_CUR_SETPOS) {
		u32 xx, yy, temp;

		yy = cursor->image.dy - info->var.yoffset;
		xx = cursor->image.dx - info->var.xoffset;
		temp = xx & 0xFFFF;
		temp |= yy << 16;

		NV_WR32(par->riva.PRAMDAC, 0x0000300, temp);
	}


	if (set & (FB_CUR_SETSHAPE | FB_CUR_SETCMAP | FB_CUR_SETIMAGE)) {
		u32 bg_idx = cursor->image.bg_color;
		u32 fg_idx = cursor->image.fg_color;
		u32 s_pitch = (cursor->image.width+7) >> 3;
		u32 d_pitch = MAX_CURS/8;
		u8 *dat = (u8 *) cursor->image.data;
		u8 *msk = (u8 *) cursor->mask;
		u8 *src;
		
		src = kmalloc(s_pitch * cursor->image.height, GFP_ATOMIC);

		if (src) {
			switch (cursor->rop) {
			case ROP_XOR:
				for (i = 0; i < s_pitch * cursor->image.height; i++)
					src[i] = dat[i] ^ msk[i];
				break;
			case ROP_COPY:
			default:
				for (i = 0; i < s_pitch * cursor->image.height; i++)
					src[i] = dat[i] & msk[i];
				break;
			}

			fb_pad_aligned_buffer(data, d_pitch, src, s_pitch,
						cursor->image.height);

			bg = ((info->cmap.red[bg_idx] & 0xf8) << 7) |
				((info->cmap.green[bg_idx] & 0xf8) << 2) |
				((info->cmap.blue[bg_idx] & 0xf8) >> 3) |
				1 << 15;

			fg = ((info->cmap.red[fg_idx] & 0xf8) << 7) |
				((info->cmap.green[fg_idx] & 0xf8) << 2) |
				((info->cmap.blue[fg_idx] & 0xf8) >> 3) |
				1 << 15;

			par->riva.LockUnlock(&par->riva, 0);

			rivafb_load_cursor_image(par, data, bg, fg,
						 cursor->image.width,
						 cursor->image.height);
			kfree(src);
		}
	}

	if (cursor->enable)
		par->riva.ShowHideCursor(&par->riva, 1);

	return 0;
}

static int rivafb_sync(struct fb_info *info)
{
	struct riva_par *par = info->par;

	wait_for_idle(par);
	return 0;
}

/* ------------------------------------------------------------------------- *
 *
 * initialization helper functions
 *
 * ------------------------------------------------------------------------- */

/* kernel interface */
static struct fb_ops riva_fb_ops = {
	.owner 		= THIS_MODULE,
	.fb_open	= rivafb_open,
	.fb_release	= rivafb_release,
	.fb_check_var 	= rivafb_check_var,
	.fb_set_par 	= rivafb_set_par,
	.fb_setcolreg 	= rivafb_setcolreg,
	.fb_pan_display	= rivafb_pan_display,
	.fb_blank 	= rivafb_blank,
	.fb_fillrect 	= rivafb_fillrect,
	.fb_copyarea 	= rivafb_copyarea,
	.fb_imageblit 	= rivafb_imageblit,
	.fb_cursor	= rivafb_cursor,	
	.fb_sync 	= rivafb_sync,
};

static int __devinit riva_set_fbinfo(struct fb_info *info)
{
	unsigned int cmap_len;
	struct riva_par *par = info->par;

	NVTRACE_ENTER();
	info->flags = FBINFO_DEFAULT
		    | FBINFO_HWACCEL_XPAN
		    | FBINFO_HWACCEL_YPAN
		    | FBINFO_HWACCEL_COPYAREA
		    | FBINFO_HWACCEL_FILLRECT
	            | FBINFO_HWACCEL_IMAGEBLIT;

	/* Accel seems to not work properly on NV30 yet...*/
	if ((par->riva.Architecture == NV_ARCH_30) || noaccel) {
	    	printk(KERN_DEBUG PFX "disabling acceleration\n");
  		info->flags |= FBINFO_HWACCEL_DISABLED;
	}

	info->var = rivafb_default_var;
	info->fix.visual = (info->var.bits_per_pixel == 8) ?
				FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;

	info->pseudo_palette = par->pseudo_palette;

	cmap_len = riva_get_cmap_len(&info->var);
	fb_alloc_cmap(&info->cmap, cmap_len, 0);	

	info->pixmap.size = 8 * 1024;
	info->pixmap.buf_align = 4;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	info->var.yres_virtual = -1;
	NVTRACE_LEAVE();
	return (rivafb_check_var(&info->var, info));
}

#ifdef CONFIG_PPC_OF
static int __devinit riva_get_EDID_OF(struct fb_info *info, struct pci_dev *pd)
{
	struct riva_par *par = info->par;
	struct device_node *dp;
	const unsigned char *pedid = NULL;
	const unsigned char *disptype = NULL;
	static char *propnames[] = {
		"DFP,EDID", "LCD,EDID", "EDID", "EDID1", "EDID,B", "EDID,A", NULL };
	int i;

	NVTRACE_ENTER();
	dp = pci_device_to_OF_node(pd);
	for (; dp != NULL; dp = dp->child) {
		disptype = get_property(dp, "display-type", NULL);
		if (disptype == NULL)
			continue;
		if (strncmp(disptype, "LCD", 3) != 0)
			continue;
		for (i = 0; propnames[i] != NULL; ++i) {
			pedid = get_property(dp, propnames[i], NULL);
			if (pedid != NULL) {
				par->EDID = (unsigned char *)pedid;
				NVTRACE("LCD found.\n");
				return 1;
			}
		}
	}
	NVTRACE_LEAVE();
	return 0;
}
#endif /* CONFIG_PPC_OF */

#if defined(CONFIG_FB_RIVA_I2C) && !defined(CONFIG_PPC_OF)
static int __devinit riva_get_EDID_i2c(struct fb_info *info)
{
	struct riva_par *par = info->par;
	struct fb_var_screeninfo var;
	int i;

	NVTRACE_ENTER();
	riva_create_i2c_busses(par);
	for (i = 0; i < par->bus; i++) {
		riva_probe_i2c_connector(par, i+1, &par->EDID);
		if (par->EDID && !fb_parse_edid(par->EDID, &var)) {
			printk(PFX "Found EDID Block from BUS %i\n", i);
			break;
		}
	}

	NVTRACE_LEAVE();
	return (par->EDID) ? 1 : 0;
}
#endif /* CONFIG_FB_RIVA_I2C */

static void __devinit riva_update_default_var(struct fb_var_screeninfo *var,
					      struct fb_info *info)
{
	struct fb_monspecs *specs = &info->monspecs;
	struct fb_videomode modedb;

	NVTRACE_ENTER();
	/* respect mode options */
	if (mode_option) {
		fb_find_mode(var, info, mode_option,
			     specs->modedb, specs->modedb_len,
			     NULL, 8);
	} else if (specs->modedb != NULL) {
		/* get preferred timing */
		if (info->monspecs.misc & FB_MISC_1ST_DETAIL) {
			int i;

			for (i = 0; i < specs->modedb_len; i++) {
				if (specs->modedb[i].flag & FB_MODE_IS_FIRST) {
					modedb = specs->modedb[i];
					break;
				}
			}
		} else {
			/* otherwise, get first mode in database */
			modedb = specs->modedb[0];
		}
		var->bits_per_pixel = 8;
		riva_update_var(var, &modedb);
	}
	NVTRACE_LEAVE();
}


static void __devinit riva_get_EDID(struct fb_info *info, struct pci_dev *pdev)
{
	NVTRACE_ENTER();
#ifdef CONFIG_PPC_OF
	if (!riva_get_EDID_OF(info, pdev))
		printk(PFX "could not retrieve EDID from OF\n");
#elif defined(CONFIG_FB_RIVA_I2C)
	if (!riva_get_EDID_i2c(info))
		printk(PFX "could not retrieve EDID from DDC/I2C\n");
#endif
	NVTRACE_LEAVE();
}


static void __devinit riva_get_edidinfo(struct fb_info *info)
{
	struct fb_var_screeninfo *var = &rivafb_default_var;
	struct riva_par *par = info->par;

	fb_edid_to_monspecs(par->EDID, &info->monspecs);
	fb_videomode_to_modelist(info->monspecs.modedb, info->monspecs.modedb_len,
				 &info->modelist);
	riva_update_default_var(var, info);

	/* if user specified flatpanel, we respect that */
	if (info->monspecs.input & FB_DISP_DDI)
		par->FlatPanel = 1;
}

/* ------------------------------------------------------------------------- *
 *
 * PCI bus
 *
 * ------------------------------------------------------------------------- */

static u32 __devinit riva_get_arch(struct pci_dev *pd)
{
    	u32 arch = 0;

	switch (pd->device & 0x0ff0) {
		case 0x0100:   /* GeForce 256 */
		case 0x0110:   /* GeForce2 MX */
		case 0x0150:   /* GeForce2 */
		case 0x0170:   /* GeForce4 MX */
		case 0x0180:   /* GeForce4 MX (8x AGP) */
		case 0x01A0:   /* nForce */
		case 0x01F0:   /* nForce2 */
		     arch =  NV_ARCH_10;
		     break;
		case 0x0200:   /* GeForce3 */
		case 0x0250:   /* GeForce4 Ti */
		case 0x0280:   /* GeForce4 Ti (8x AGP) */
		     arch =  NV_ARCH_20;
		     break;
		case 0x0300:   /* GeForceFX 5800 */
		case 0x0310:   /* GeForceFX 5600 */
		case 0x0320:   /* GeForceFX 5200 */
		case 0x0330:   /* GeForceFX 5900 */
		case 0x0340:   /* GeForceFX 5700 */
		     arch =  NV_ARCH_30;
		     break;
		case 0x0020:   /* TNT, TNT2 */
		     arch =  NV_ARCH_04;
		     break;
		case 0x0010:   /* Riva128 */
		     arch =  NV_ARCH_03;
		     break;
		default:   /* unknown architecture */
		     break;
	}
	return arch;
}

static int __devinit rivafb_probe(struct pci_dev *pd,
			     	const struct pci_device_id *ent)
{
	struct riva_par *default_par;
	struct fb_info *info;
	int ret;

	NVTRACE_ENTER();
	assert(pd != NULL);

	info = framebuffer_alloc(sizeof(struct riva_par), &pd->dev);
	if (!info) {
		printk (KERN_ERR PFX "could not allocate memory\n");
		ret = -ENOMEM;
		goto err_ret;
	}
	default_par = info->par;
	default_par->pdev = pd;

	info->pixmap.addr = kzalloc(8 * 1024, GFP_KERNEL);
	if (info->pixmap.addr == NULL) {
	    	ret = -ENOMEM;
		goto err_framebuffer_release;
	}

	ret = pci_enable_device(pd);
	if (ret < 0) {
		printk(KERN_ERR PFX "cannot enable PCI device\n");
		goto err_free_pixmap;
	}

	ret = pci_request_regions(pd, "rivafb");
	if (ret < 0) {
		printk(KERN_ERR PFX "cannot request PCI regions\n");
		goto err_disable_device;
	}

	mutex_init(&default_par->open_lock);
	default_par->riva.Architecture = riva_get_arch(pd);

	default_par->Chipset = (pd->vendor << 16) | pd->device;
	printk(KERN_INFO PFX "nVidia device/chipset %X\n",default_par->Chipset);
	
	if(default_par->riva.Architecture == 0) {
		printk(KERN_ERR PFX "unknown NV_ARCH\n");
		ret=-ENODEV;
		goto err_release_region;
	}
	if(default_par->riva.Architecture == NV_ARCH_10 ||
	   default_par->riva.Architecture == NV_ARCH_20 ||
	   default_par->riva.Architecture == NV_ARCH_30) {
		sprintf(rivafb_fix.id, "NV%x", (pd->device & 0x0ff0) >> 4);
	} else {
		sprintf(rivafb_fix.id, "NV%x", default_par->riva.Architecture);
	}

	default_par->FlatPanel = flatpanel;
	if (flatpanel == 1)
		printk(KERN_INFO PFX "flatpanel support enabled\n");
	default_par->forceCRTC = forceCRTC;
	
	rivafb_fix.mmio_len = pci_resource_len(pd, 0);
	rivafb_fix.smem_len = pci_resource_len(pd, 1);

	{
		/* enable IO and mem if not already done */
		unsigned short cmd;

		pci_read_config_word(pd, PCI_COMMAND, &cmd);
		cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
		pci_write_config_word(pd, PCI_COMMAND, cmd);
	}
	
	rivafb_fix.mmio_start = pci_resource_start(pd, 0);
	rivafb_fix.smem_start = pci_resource_start(pd, 1);

	default_par->ctrl_base = ioremap(rivafb_fix.mmio_start,
					 rivafb_fix.mmio_len);
	if (!default_par->ctrl_base) {
		printk(KERN_ERR PFX "cannot ioremap MMIO base\n");
		ret = -EIO;
		goto err_release_region;
	}

	switch (default_par->riva.Architecture) {
	case NV_ARCH_03:
		/* Riva128's PRAMIN is in the "framebuffer" space
		 * Since these cards were never made with more than 8 megabytes
		 * we can safely allocate this separately.
		 */
		default_par->riva.PRAMIN = ioremap(rivafb_fix.smem_start + 0x00C00000, 0x00008000);
		if (!default_par->riva.PRAMIN) {
			printk(KERN_ERR PFX "cannot ioremap PRAMIN region\n");
			ret = -EIO;
			goto err_iounmap_ctrl_base;
		}
		break;
	case NV_ARCH_04:
	case NV_ARCH_10:
	case NV_ARCH_20:
	case NV_ARCH_30:
		default_par->riva.PCRTC0 =
			(u32 __iomem *)(default_par->ctrl_base + 0x00600000);
		default_par->riva.PRAMIN =
			(u32 __iomem *)(default_par->ctrl_base + 0x00710000);
		break;
	}
	riva_common_setup(default_par);

	if (default_par->riva.Architecture == NV_ARCH_03) {
		default_par->riva.PCRTC = default_par->riva.PCRTC0
		                        = default_par->riva.PGRAPH;
	}

	rivafb_fix.smem_len = riva_get_memlen(default_par) * 1024;
	default_par->dclk_max = riva_get_maxdclk(default_par) * 1000;
	info->screen_base = ioremap(rivafb_fix.smem_start,
				    rivafb_fix.smem_len);
	if (!info->screen_base) {
		printk(KERN_ERR PFX "cannot ioremap FB base\n");
		ret = -EIO;
		goto err_iounmap_pramin;
	}

#ifdef CONFIG_MTRR
	if (!nomtrr) {
		default_par->mtrr.vram = mtrr_add(rivafb_fix.smem_start,
					   	  rivafb_fix.smem_len,
					    	  MTRR_TYPE_WRCOMB, 1);
		if (default_par->mtrr.vram < 0) {
			printk(KERN_ERR PFX "unable to setup MTRR\n");
		} else {
			default_par->mtrr.vram_valid = 1;
			/* let there be speed */
			printk(KERN_INFO PFX "RIVA MTRR set to ON\n");
		}
	}
#endif /* CONFIG_MTRR */

	info->fbops = &riva_fb_ops;
	info->fix = rivafb_fix;
	riva_get_EDID(info, pd);
	riva_get_edidinfo(info);

	ret=riva_set_fbinfo(info);
	if (ret < 0) {
		printk(KERN_ERR PFX "error setting initial video mode\n");
		goto err_iounmap_screen_base;
	}

	fb_destroy_modedb(info->monspecs.modedb);
	info->monspecs.modedb = NULL;

	pci_set_drvdata(pd, info);

	if (backlight)
		riva_bl_init(info->par);

	ret = register_framebuffer(info);
	if (ret < 0) {
		printk(KERN_ERR PFX
			"error registering riva framebuffer\n");
		goto err_iounmap_screen_base;
	}

	printk(KERN_INFO PFX
		"PCI nVidia %s framebuffer ver %s (%dMB @ 0x%lX)\n",
		info->fix.id,
		RIVAFB_VERSION,
		info->fix.smem_len / (1024 * 1024),
		info->fix.smem_start);

	NVTRACE_LEAVE();
	return 0;

err_iounmap_screen_base:
#ifdef CONFIG_FB_RIVA_I2C
	riva_delete_i2c_busses(info->par);
#endif
	iounmap(info->screen_base);
err_iounmap_pramin:
	if (default_par->riva.Architecture == NV_ARCH_03) 
		iounmap(default_par->riva.PRAMIN);
err_iounmap_ctrl_base:
	iounmap(default_par->ctrl_base);
err_release_region:
	pci_release_regions(pd);
err_disable_device:
err_free_pixmap:
	kfree(info->pixmap.addr);
err_framebuffer_release:
	framebuffer_release(info);
err_ret:
	return ret;
}

static void __exit rivafb_remove(struct pci_dev *pd)
{
	struct fb_info *info = pci_get_drvdata(pd);
	struct riva_par *par = info->par;
	
	NVTRACE_ENTER();

#ifdef CONFIG_FB_RIVA_I2C
	riva_delete_i2c_busses(par);
	kfree(par->EDID);
#endif

	unregister_framebuffer(info);

	riva_bl_exit(info);

#ifdef CONFIG_MTRR
	if (par->mtrr.vram_valid)
		mtrr_del(par->mtrr.vram, info->fix.smem_start,
			 info->fix.smem_len);
#endif /* CONFIG_MTRR */

	iounmap(par->ctrl_base);
	iounmap(info->screen_base);
	if (par->riva.Architecture == NV_ARCH_03)
		iounmap(par->riva.PRAMIN);
	pci_release_regions(pd);
	kfree(info->pixmap.addr);
	framebuffer_release(info);
	pci_set_drvdata(pd, NULL);
	NVTRACE_LEAVE();
}

/* ------------------------------------------------------------------------- *
 *
 * initialization
 *
 * ------------------------------------------------------------------------- */

#ifndef MODULE
static int __init rivafb_setup(char *options)
{
	char *this_opt;

	NVTRACE_ENTER();
	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!strncmp(this_opt, "forceCRTC", 9)) {
			char *p;
			
			p = this_opt + 9;
			if (!*p || !*(++p)) continue; 
			forceCRTC = *p - '0';
			if (forceCRTC < 0 || forceCRTC > 1) 
				forceCRTC = -1;
		} else if (!strncmp(this_opt, "flatpanel", 9)) {
			flatpanel = 1;
		} else if (!strncmp(this_opt, "backlight:", 10)) {
			backlight = simple_strtoul(this_opt+10, NULL, 0);
#ifdef CONFIG_MTRR
		} else if (!strncmp(this_opt, "nomtrr", 6)) {
			nomtrr = 1;
#endif
		} else if (!strncmp(this_opt, "strictmode", 10)) {
			strictmode = 1;
		} else if (!strncmp(this_opt, "noaccel", 7)) {
			noaccel = 1;
		} else
			mode_option = this_opt;
	}
	NVTRACE_LEAVE();
	return 0;
}
#endif /* !MODULE */

static struct pci_driver rivafb_driver = {
	.name		= "rivafb",
	.id_table	= rivafb_pci_tbl,
	.probe		= rivafb_probe,
	.remove		= __exit_p(rivafb_remove),
};



/* ------------------------------------------------------------------------- *
 *
 * modularization
 *
 * ------------------------------------------------------------------------- */

static int __devinit rivafb_init(void)
{
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("rivafb", &option))
		return -ENODEV;
	rivafb_setup(option);
#endif
	return pci_register_driver(&rivafb_driver);
}


module_init(rivafb_init);

#ifdef MODULE
static void __exit rivafb_exit(void)
{
	pci_unregister_driver(&rivafb_driver);
}

module_exit(rivafb_exit);
#endif /* MODULE */

module_param(noaccel, bool, 0);
MODULE_PARM_DESC(noaccel, "bool: disable acceleration");
module_param(flatpanel, int, 0);
MODULE_PARM_DESC(flatpanel, "Enables experimental flat panel support for some chipsets. (0 or 1=enabled) (default=0)");
module_param(forceCRTC, int, 0);
MODULE_PARM_DESC(forceCRTC, "Forces usage of a particular CRTC in case autodetection fails. (0 or 1) (default=autodetect)");
#ifdef CONFIG_MTRR
module_param(nomtrr, bool, 0);
MODULE_PARM_DESC(nomtrr, "Disables MTRR support (0 or 1=disabled) (default=0)");
#endif
module_param(strictmode, bool, 0);
MODULE_PARM_DESC(strictmode, "Only use video modes from EDID");

MODULE_AUTHOR("Ani Joshi, maintainer");
MODULE_DESCRIPTION("Framebuffer driver for nVidia Riva 128, TNT, TNT2, and the GeForce series");
MODULE_LICENSE("GPL");
