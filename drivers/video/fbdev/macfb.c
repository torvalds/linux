/*
 * macfb.c: Generic framebuffer for Macs whose colourmaps/modes we
 * don't know how to set.
 *
 * (c) 1999 David Huggins-Daines <dhd@debian.org>
 *
 * Primarily based on vesafb.c, by Gerd Knorr
 * (c) 1998 Gerd Knorr <kraxel@cs.tu-berlin.de>
 *
 * Also uses information and code from:
 *
 * The original macfb.c from Linux/mac68k 2.0, by Alan Cox, Juergen
 * Mellinger, Mikael Forselius, Michael Schmitz, and others.
 *
 * valkyriefb.c, by Martin Costabel, Kevin Schoedel, Barry Nathan, Dan
 * Jacobowitz, Paul Mackerras, Fabio Riccardi, and Geert Uytterhoeven.
 *
 * The VideoToolbox "Bugs" web page at
 * http://rajsky.psych.nyu.edu/Tips/VideoBugs.html
 *
 * This code is free software.  You may copy, modify, and distribute
 * it subject to the terms and conditions of the GNU General Public
 * License, version 2, or any later version, at your convenience.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/nubus.h>
#include <linux/init.h>
#include <linux/fb.h>

#include <asm/setup.h>
#include <asm/macintosh.h>
#include <asm/io.h>

/* Common DAC base address for the LC, RBV, Valkyrie, and IIvx */
#define DAC_BASE 0x50f24000

/* Some addresses for the DAFB */
#define DAFB_BASE 0xf9800200

/* Address for the built-in Civic framebuffer in Quadra AVs */
#define CIVIC_BASE 0x50f30800

/* GSC (Gray Scale Controller) base address */
#define GSC_BASE 0x50F20000

/* CSC (Color Screen Controller) base address */
#define CSC_BASE 0x50F20000

static int (*macfb_setpalette)(unsigned int regno, unsigned int red,
			       unsigned int green, unsigned int blue,
			       struct fb_info *info);

static struct {
	unsigned char addr;
	unsigned char lut;
} __iomem *v8_brazil_cmap_regs;

static struct {
	unsigned char addr;
	char pad1[3]; /* word aligned */
	unsigned char lut;
	char pad2[3]; /* word aligned */
	unsigned char cntl; /* a guess as to purpose */
} __iomem *rbv_cmap_regs;

static struct {
	unsigned long reset;
	unsigned long pad1[3];
	unsigned char pad2[3];
	unsigned char lut;
} __iomem *dafb_cmap_regs;

static struct {
	unsigned char addr;	/* OFFSET: 0x00 */
	unsigned char pad1[15];
	unsigned char lut;	/* OFFSET: 0x10 */
	unsigned char pad2[15];
	unsigned char status;	/* OFFSET: 0x20 */
	unsigned char pad3[7];
	unsigned long vbl_addr;	/* OFFSET: 0x28 */
	unsigned int  status2;	/* OFFSET: 0x2C */
} __iomem *civic_cmap_regs;

static struct {
	char pad1[0x40];
	unsigned char clut_waddr;	/* 0x40 */
	char pad2;
	unsigned char clut_data;	/* 0x42 */
	char pad3[0x3];
	unsigned char clut_raddr;	/* 0x46 */
} __iomem *csc_cmap_regs;

/* The registers in these structs are in NuBus slot space */
struct mdc_cmap_regs {
	char pad1[0x200200];
	unsigned char addr;
	char pad2[6];
	unsigned char lut;
};

struct toby_cmap_regs {
	char pad1[0x90018];
	unsigned char lut; /* TFBClutWDataReg, offset 0x90018 */
	char pad2[3];
	unsigned char addr; /* TFBClutAddrReg, offset 0x9001C */
};

struct jet_cmap_regs {
	char pad1[0xe0e000];
	unsigned char addr;
	unsigned char lut;
};

#define PIXEL_TO_MM(a)	(((a)*10)/28)	/* width in mm at 72 dpi */

static struct fb_var_screeninfo macfb_defined = {
	.bits_per_pixel	= 8,
	.activate	= FB_ACTIVATE_NOW,
	.width		= -1,
	.height		= -1,
	.right_margin	= 32,
	.upper_margin	= 16,
	.lower_margin	= 4,
	.vsync_len	= 4,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo macfb_fix = {
	.type	= FB_TYPE_PACKED_PIXELS,
	.accel	= FB_ACCEL_NONE,
};

static void *slot_addr;
static struct fb_info fb_info;
static u32 pseudo_palette[16];
static int inverse;
static int vidtest;

/*
 * Unlike the Valkyrie, the DAFB cannot set individual colormap
 * registers.  Therefore, we do what the MacOS driver does (no
 * kidding!) and simply set them one by one until we hit the one we
 * want.
 */
static int dafb_setpalette(unsigned int regno, unsigned int red,
			   unsigned int green, unsigned int blue,
			   struct fb_info *info)
{
	static int lastreg = -1;
	unsigned long flags;

	local_irq_save(flags);

	/*
	 * fbdev will set an entire colourmap, but X won't.  Hopefully
	 * this should accommodate both of them
	 */
	if (regno != lastreg + 1) {
		int i;

		/* Stab in the dark trying to reset the CLUT pointer */
		nubus_writel(0, &dafb_cmap_regs->reset);
		nop();

		/* Loop until we get to the register we want */
		for (i = 0; i < regno; i++) {
			nubus_writeb(info->cmap.red[i] >> 8,
				     &dafb_cmap_regs->lut);
			nop();
			nubus_writeb(info->cmap.green[i] >> 8,
				     &dafb_cmap_regs->lut);
			nop();
			nubus_writeb(info->cmap.blue[i] >> 8,
				     &dafb_cmap_regs->lut);
			nop();
		}
	}

	nubus_writeb(red, &dafb_cmap_regs->lut);
	nop();
	nubus_writeb(green, &dafb_cmap_regs->lut);
	nop();
	nubus_writeb(blue, &dafb_cmap_regs->lut);

	local_irq_restore(flags);
	lastreg = regno;
	return 0;
}

/* V8 and Brazil seem to use the same DAC.  Sonora does as well. */
static int v8_brazil_setpalette(unsigned int regno, unsigned int red,
				unsigned int green, unsigned int blue,
				struct fb_info *info)
{
	unsigned int bpp = info->var.bits_per_pixel;
	unsigned long flags;

	if (bpp > 8)
		return 1; /* failsafe */

	local_irq_save(flags);

	/* On these chips, the CLUT register numbers are spread out
	 * across the register space.  Thus:
	 * In 8bpp, all regnos are valid.
	 * In 4bpp, the regnos are 0x0f, 0x1f, 0x2f, etc, etc
	 * In 2bpp, the regnos are 0x3f, 0x7f, 0xbf, 0xff
	 */
	regno = (regno << (8 - bpp)) | (0xFF >> bpp);
	nubus_writeb(regno, &v8_brazil_cmap_regs->addr);
	nop();

	/* send one color channel at a time */
	nubus_writeb(red, &v8_brazil_cmap_regs->lut);
	nop();
	nubus_writeb(green, &v8_brazil_cmap_regs->lut);
	nop();
	nubus_writeb(blue, &v8_brazil_cmap_regs->lut);

	local_irq_restore(flags);
	return 0;
}

/* RAM-Based Video */
static int rbv_setpalette(unsigned int regno, unsigned int red,
			  unsigned int green, unsigned int blue,
			  struct fb_info *info)
{
	unsigned long flags;

	if (info->var.bits_per_pixel > 8)
		return 1; /* failsafe */

	local_irq_save(flags);

	/* From the VideoToolbox driver.  Seems to be saying that
	 * regno #254 and #255 are the important ones for 1-bit color,
	 * regno #252-255 are the important ones for 2-bit color, etc.
	 */
	regno += 256 - (1 << info->var.bits_per_pixel);

	/* reset clut? (VideoToolbox sez "not necessary") */
	nubus_writeb(0xFF, &rbv_cmap_regs->cntl);
	nop();

	/* tell clut which address to use. */
	nubus_writeb(regno, &rbv_cmap_regs->addr);
	nop();

	/* send one color channel at a time. */
	nubus_writeb(red, &rbv_cmap_regs->lut);
	nop();
	nubus_writeb(green, &rbv_cmap_regs->lut);
	nop();
	nubus_writeb(blue, &rbv_cmap_regs->lut);

	local_irq_restore(flags);
	return 0;
}

/* Macintosh Display Card (8*24) */
static int mdc_setpalette(unsigned int regno, unsigned int red,
			  unsigned int green, unsigned int blue,
			  struct fb_info *info)
{
	struct mdc_cmap_regs *cmap_regs = slot_addr;
	unsigned long flags;

	local_irq_save(flags);

	/* the nop's are there to order writes. */
	nubus_writeb(regno, &cmap_regs->addr);
	nop();
	nubus_writeb(red, &cmap_regs->lut);
	nop();
	nubus_writeb(green, &cmap_regs->lut);
	nop();
	nubus_writeb(blue, &cmap_regs->lut);

	local_irq_restore(flags);
	return 0;
}

/* Toby frame buffer */
static int toby_setpalette(unsigned int regno, unsigned int red,
			   unsigned int green, unsigned int blue,
			   struct fb_info *info)
{
	struct toby_cmap_regs *cmap_regs = slot_addr;
	unsigned int bpp = info->var.bits_per_pixel;
	unsigned long flags;

	red = ~red;
	green = ~green;
	blue = ~blue;
	regno = (regno << (8 - bpp)) | (0xFF >> bpp);

	local_irq_save(flags);

	nubus_writeb(regno, &cmap_regs->addr);
	nop();
	nubus_writeb(red, &cmap_regs->lut);
	nop();
	nubus_writeb(green, &cmap_regs->lut);
	nop();
	nubus_writeb(blue, &cmap_regs->lut);

	local_irq_restore(flags);
	return 0;
}

/* Jet frame buffer */
static int jet_setpalette(unsigned int regno, unsigned int red,
			  unsigned int green, unsigned int blue,
			  struct fb_info *info)
{
	struct jet_cmap_regs *cmap_regs = slot_addr;
	unsigned long flags;

	local_irq_save(flags);

	nubus_writeb(regno, &cmap_regs->addr);
	nop();
	nubus_writeb(red, &cmap_regs->lut);
	nop();
	nubus_writeb(green, &cmap_regs->lut);
	nop();
	nubus_writeb(blue, &cmap_regs->lut);

	local_irq_restore(flags);
	return 0;
}

/*
 * Civic framebuffer -- Quadra AV built-in video.  A chip
 * called Sebastian holds the actual color palettes, and
 * apparently, there are two different banks of 512K RAM
 * which can act as separate framebuffers for doing video
 * input and viewing the screen at the same time!  The 840AV
 * Can add another 1MB RAM to give the two framebuffers
 * 1MB RAM apiece.
 */
static int civic_setpalette(unsigned int regno, unsigned int red,
			    unsigned int green, unsigned int blue,
			    struct fb_info *info)
{
	unsigned long flags;
	int clut_status;
	
	if (info->var.bits_per_pixel > 8)
		return 1; /* failsafe */

	local_irq_save(flags);

	/* Set the register address */
	nubus_writeb(regno, &civic_cmap_regs->addr);
	nop();

	/*
	 * Grab a status word and do some checking;
	 * Then finally write the clut!
	 */
	clut_status =  nubus_readb(&civic_cmap_regs->status2);

	if ((clut_status & 0x0008) == 0)
	{
#if 0
		if ((clut_status & 0x000D) != 0)
		{
			nubus_writeb(0x00, &civic_cmap_regs->lut);
			nop();
			nubus_writeb(0x00, &civic_cmap_regs->lut);
			nop();
		}
#endif

		nubus_writeb(red, &civic_cmap_regs->lut);
		nop();
		nubus_writeb(green, &civic_cmap_regs->lut);
		nop();
		nubus_writeb(blue, &civic_cmap_regs->lut);
		nop();
		nubus_writeb(0x00, &civic_cmap_regs->lut);
	}
	else
	{
		unsigned char junk;

		junk = nubus_readb(&civic_cmap_regs->lut);
		nop();
		junk = nubus_readb(&civic_cmap_regs->lut);
		nop();
		junk = nubus_readb(&civic_cmap_regs->lut);
		nop();
		junk = nubus_readb(&civic_cmap_regs->lut);
		nop();

		if ((clut_status & 0x000D) != 0)
		{
			nubus_writeb(0x00, &civic_cmap_regs->lut);
			nop();
			nubus_writeb(0x00, &civic_cmap_regs->lut);
			nop();
		}

		nubus_writeb(red, &civic_cmap_regs->lut);
		nop();
		nubus_writeb(green, &civic_cmap_regs->lut);
		nop();
		nubus_writeb(blue, &civic_cmap_regs->lut);
		nop();
		nubus_writeb(junk, &civic_cmap_regs->lut);
	}

	local_irq_restore(flags);
	return 0;
}

/*
 * The CSC is the framebuffer on the PowerBook 190 series
 * (and the 5300 too, but that's a PowerMac). This function
 * brought to you in part by the ECSC driver for MkLinux.
 */
static int csc_setpalette(unsigned int regno, unsigned int red,
			  unsigned int green, unsigned int blue,
			  struct fb_info *info)
{
	unsigned long flags;

	local_irq_save(flags);

	udelay(1); /* mklinux on PB 5300 waits for 260 ns */
	nubus_writeb(regno, &csc_cmap_regs->clut_waddr);
	nubus_writeb(red, &csc_cmap_regs->clut_data);
	nubus_writeb(green, &csc_cmap_regs->clut_data);
	nubus_writeb(blue, &csc_cmap_regs->clut_data);

	local_irq_restore(flags);
	return 0;
}

static int macfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *fb_info)
{
	/*
	 * Set a single color register. The values supplied are
	 * already rounded down to the hardware's capabilities
	 * (according to the entries in the `var' structure).
	 * Return non-zero for invalid regno.
	 */
	
	if (regno >= fb_info->cmap.len)
		return 1;

	if (fb_info->var.bits_per_pixel <= 8) {
		switch (fb_info->var.bits_per_pixel) {
		case 1:
			/* We shouldn't get here */
			break;
		case 2:
		case 4:
		case 8:
			if (macfb_setpalette)
				macfb_setpalette(regno, red >> 8, green >> 8,
						 blue >> 8, fb_info);
			else
				return 1;
			break;
		}
	} else if (regno < 16) {
		switch (fb_info->var.bits_per_pixel) {
		case 16:
			if (fb_info->var.red.offset == 10) {
				/* 1:5:5:5 */
				((u32*) (fb_info->pseudo_palette))[regno] =
					((red   & 0xf800) >>  1) |
					((green & 0xf800) >>  6) |
					((blue  & 0xf800) >> 11) |
					((transp != 0) << 15);
			} else {
				/* 0:5:6:5 */
				((u32*) (fb_info->pseudo_palette))[regno] =
					((red   & 0xf800) >>  0) |
					((green & 0xfc00) >>  5) |
					((blue  & 0xf800) >> 11);
			}
			break;
		/*
		 * 24-bit colour almost doesn't exist on 68k Macs --
		 * http://support.apple.com/kb/TA28634 (Old Article: 10992)
		 */
		case 24:
		case 32:
			red   >>= 8;
			green >>= 8;
			blue  >>= 8;
			((u32 *)(fb_info->pseudo_palette))[regno] =
				(red   << fb_info->var.red.offset) |
				(green << fb_info->var.green.offset) |
				(blue  << fb_info->var.blue.offset);
			break;
		}
	}

	return 0;
}

static struct fb_ops macfb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= macfb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static void __init macfb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;

		if (!strcmp(this_opt, "inverse"))
			inverse = 1;
		else
			if (!strcmp(this_opt, "vidtest"))
				vidtest = 1; /* enable experimental CLUT code */
	}
}

static void __init iounmap_macfb(void)
{
	if (dafb_cmap_regs)
		iounmap(dafb_cmap_regs);
	if (v8_brazil_cmap_regs)
		iounmap(v8_brazil_cmap_regs);
	if (rbv_cmap_regs)
		iounmap(rbv_cmap_regs);
	if (civic_cmap_regs)
		iounmap(civic_cmap_regs);
	if (csc_cmap_regs)
		iounmap(csc_cmap_regs);
}

static int __init macfb_init(void)
{
	int video_cmap_len, video_is_nubus = 0;
	struct nubus_rsrc *ndev = NULL;
	char *option = NULL;
	int err;

	if (fb_get_options("macfb", &option))
		return -ENODEV;
	macfb_setup(option);

	if (!MACH_IS_MAC) 
		return -ENODEV;

	if (mac_bi_data.id == MAC_MODEL_Q630 ||
	    mac_bi_data.id == MAC_MODEL_P588)
		return -ENODEV; /* See valkyriefb.c */

	macfb_defined.xres = mac_bi_data.dimensions & 0xFFFF;
	macfb_defined.yres = mac_bi_data.dimensions >> 16;
	macfb_defined.bits_per_pixel = mac_bi_data.videodepth;

	macfb_fix.line_length = mac_bi_data.videorow;
	macfb_fix.smem_len    = macfb_fix.line_length * macfb_defined.yres;
	/* Note: physical address (since 2.1.127) */
	macfb_fix.smem_start  = mac_bi_data.videoaddr;

	/*
	 * This is actually redundant with the initial mappings.
	 * However, there are some non-obvious aspects to the way
	 * those mappings are set up, so this is in fact the safest
	 * way to ensure that this driver will work on every possible Mac
	 */
	fb_info.screen_base = ioremap(mac_bi_data.videoaddr,
				      macfb_fix.smem_len);
	if (!fb_info.screen_base)
		return -ENODEV;

	pr_info("macfb: framebuffer at 0x%08lx, mapped to 0x%p, size %dk\n",
	        macfb_fix.smem_start, fb_info.screen_base,
	        macfb_fix.smem_len / 1024);
	pr_info("macfb: mode is %dx%dx%d, linelength=%d\n",
	        macfb_defined.xres, macfb_defined.yres,
	        macfb_defined.bits_per_pixel, macfb_fix.line_length);

	/* Fill in the available video resolution */
	macfb_defined.xres_virtual = macfb_defined.xres;
	macfb_defined.yres_virtual = macfb_defined.yres;
	macfb_defined.height       = PIXEL_TO_MM(macfb_defined.yres);
	macfb_defined.width        = PIXEL_TO_MM(macfb_defined.xres);

	/* Some dummy values for timing to make fbset happy */
	macfb_defined.pixclock     = 10000000 / macfb_defined.xres *
				     1000 / macfb_defined.yres;
	macfb_defined.left_margin  = (macfb_defined.xres / 8) & 0xf8;
	macfb_defined.hsync_len    = (macfb_defined.xres / 8) & 0xf8;

	switch (macfb_defined.bits_per_pixel) {
	case 1:
		macfb_defined.red.length = macfb_defined.bits_per_pixel;
		macfb_defined.green.length = macfb_defined.bits_per_pixel;
		macfb_defined.blue.length = macfb_defined.bits_per_pixel;
		video_cmap_len = 2;
		macfb_fix.visual = FB_VISUAL_MONO01;
		break;
	case 2:
	case 4:
	case 8:
		macfb_defined.red.length = macfb_defined.bits_per_pixel;
		macfb_defined.green.length = macfb_defined.bits_per_pixel;
		macfb_defined.blue.length = macfb_defined.bits_per_pixel;
		video_cmap_len = 1 << macfb_defined.bits_per_pixel;
		macfb_fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	case 16:
		macfb_defined.transp.offset = 15;
		macfb_defined.transp.length = 1;
		macfb_defined.red.offset = 10;
		macfb_defined.red.length = 5;
		macfb_defined.green.offset = 5;
		macfb_defined.green.length = 5;
		macfb_defined.blue.offset = 0;
		macfb_defined.blue.length = 5;
		video_cmap_len = 16;
		/*
		 * Should actually be FB_VISUAL_DIRECTCOLOR, but this
		 * works too
		 */
		macfb_fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	case 24:
	case 32:
		macfb_defined.red.offset = 16;
		macfb_defined.red.length = 8;
		macfb_defined.green.offset = 8;
		macfb_defined.green.length = 8;
		macfb_defined.blue.offset = 0;
		macfb_defined.blue.length = 8;
		video_cmap_len = 16;
		macfb_fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	default:
		pr_err("macfb: unknown or unsupported bit depth: %d\n",
		       macfb_defined.bits_per_pixel);
		err = -EINVAL;
		goto fail_unmap;
	}
	
	/*
	 * We take a wild guess that if the video physical address is
	 * in nubus slot space, that the nubus card is driving video.
	 * Penguin really ought to tell us whether we are using internal
	 * video or not.
	 * Hopefully we only find one of them.  Otherwise our NuBus
	 * code is really broken :-)
	 */

	for_each_func_rsrc(ndev) {
		unsigned long base = ndev->board->slot_addr;

		if (mac_bi_data.videoaddr < base ||
		    mac_bi_data.videoaddr - base > 0xFFFFFF)
			continue;

		if (ndev->category != NUBUS_CAT_DISPLAY ||
		    ndev->type != NUBUS_TYPE_VIDEO)
			continue;

		video_is_nubus = 1;
		slot_addr = (unsigned char *)base;

		switch(ndev->dr_hw) {
		case NUBUS_DRHW_APPLE_MDC:
			strcpy(macfb_fix.id, "Mac Disp. Card");
			macfb_setpalette = mdc_setpalette;
			macfb_defined.activate = FB_ACTIVATE_NOW;
			break;
		case NUBUS_DRHW_APPLE_TFB:
			strcpy(macfb_fix.id, "Toby");
			macfb_setpalette = toby_setpalette;
			macfb_defined.activate = FB_ACTIVATE_NOW;
			break;
		case NUBUS_DRHW_APPLE_JET:
			strcpy(macfb_fix.id, "Jet");
			macfb_setpalette = jet_setpalette;
			macfb_defined.activate = FB_ACTIVATE_NOW;
			break;
		default:
			strcpy(macfb_fix.id, "Generic NuBus");
			break;
		}
	}

	/* If it's not a NuBus card, it must be internal video */
	if (!video_is_nubus)
		switch (mac_bi_data.id) {
		/*
		 * DAFB Quadras
		 * Note: these first four have the v7 DAFB, which is
		 * known to be rather unlike the ones used in the
		 * other models
		 */
		case MAC_MODEL_P475:
		case MAC_MODEL_P475F:
		case MAC_MODEL_P575:
		case MAC_MODEL_Q605:

		case MAC_MODEL_Q800:
		case MAC_MODEL_Q650:
		case MAC_MODEL_Q610:
		case MAC_MODEL_C650:
		case MAC_MODEL_C610:
		case MAC_MODEL_Q700:
		case MAC_MODEL_Q900:
		case MAC_MODEL_Q950:
			strcpy(macfb_fix.id, "DAFB");
			macfb_setpalette = dafb_setpalette;
			dafb_cmap_regs = ioremap(DAFB_BASE, 0x1000);
			macfb_defined.activate = FB_ACTIVATE_NOW;
			break;

		/*
		 * LC II uses the V8 framebuffer
		 */
		case MAC_MODEL_LCII:
			strcpy(macfb_fix.id, "V8");
			macfb_setpalette = v8_brazil_setpalette;
			v8_brazil_cmap_regs = ioremap(DAC_BASE, 0x1000);
			macfb_defined.activate = FB_ACTIVATE_NOW;
			break;

		/*
		 * IIvi, IIvx use the "Brazil" framebuffer (which is
		 * very much like the V8, it seems, and probably uses
		 * the same DAC)
		 */
		case MAC_MODEL_IIVI:
		case MAC_MODEL_IIVX:
		case MAC_MODEL_P600:
			strcpy(macfb_fix.id, "Brazil");
			macfb_setpalette = v8_brazil_setpalette;
			v8_brazil_cmap_regs = ioremap(DAC_BASE, 0x1000);
			macfb_defined.activate = FB_ACTIVATE_NOW;
			break;

		/*
		 * LC III (and friends) use the Sonora framebuffer
		 * Incidentally this is also used in the non-AV models
		 * of the x100 PowerMacs
		 * These do in fact seem to use the same DAC interface
		 * as the LC II.
		 */
		case MAC_MODEL_LCIII:
		case MAC_MODEL_P520:
		case MAC_MODEL_P550:
		case MAC_MODEL_P460:
			strcpy(macfb_fix.id, "Sonora");
			macfb_setpalette = v8_brazil_setpalette;
			v8_brazil_cmap_regs = ioremap(DAC_BASE, 0x1000);
			macfb_defined.activate = FB_ACTIVATE_NOW;
			break;

		/*
		 * IIci and IIsi use the infamous RBV chip
		 * (the IIsi is just a rebadged and crippled
		 * IIci in a different case, BTW)
		 */
		case MAC_MODEL_IICI:
		case MAC_MODEL_IISI:
			strcpy(macfb_fix.id, "RBV");
			macfb_setpalette = rbv_setpalette;
			rbv_cmap_regs = ioremap(DAC_BASE, 0x1000);
			macfb_defined.activate = FB_ACTIVATE_NOW;
			break;

		/*
		 * AVs use the Civic framebuffer
		 */
		case MAC_MODEL_Q840:
		case MAC_MODEL_C660:
			strcpy(macfb_fix.id, "Civic");
			macfb_setpalette = civic_setpalette;
			civic_cmap_regs = ioremap(CIVIC_BASE, 0x1000);
			macfb_defined.activate = FB_ACTIVATE_NOW;
			break;

		
		/*
		 * Assorted weirdos
		 * We think this may be like the LC II
		 */
		case MAC_MODEL_LC:
			strcpy(macfb_fix.id, "LC");
			if (vidtest) {
				macfb_setpalette = v8_brazil_setpalette;
				v8_brazil_cmap_regs =
					ioremap(DAC_BASE, 0x1000);
				macfb_defined.activate = FB_ACTIVATE_NOW;
			}
			break;

		/*
		 * We think this may be like the LC II
		 */
		case MAC_MODEL_CCL:
			strcpy(macfb_fix.id, "Color Classic");
			if (vidtest) {
				macfb_setpalette = v8_brazil_setpalette;
				v8_brazil_cmap_regs =
					ioremap(DAC_BASE, 0x1000);
				macfb_defined.activate = FB_ACTIVATE_NOW;
			}
			break;

		/*
		 * And we *do* mean "weirdos"
		 */
		case MAC_MODEL_TV:
			strcpy(macfb_fix.id, "Mac TV");
			break;

		/*
		 * These don't have colour, so no need to worry
		 */
		case MAC_MODEL_SE30:
		case MAC_MODEL_CLII:
			strcpy(macfb_fix.id, "Monochrome");
			break;

		/*
		 * Powerbooks are particularly difficult.  Many of
		 * them have separate framebuffers for external and
		 * internal video, which is admittedly pretty cool,
		 * but will be a bit of a headache to support here.
		 * Also, many of them are grayscale, and we don't
		 * really support that.
		 */

		/*
		 * Slot 0 ROM says TIM. No external video. B&W.
		 */
		case MAC_MODEL_PB140:
		case MAC_MODEL_PB145:
		case MAC_MODEL_PB170:
			strcpy(macfb_fix.id, "DDC");
			break;

		/*
		 * Internal is GSC, External (if present) is ViSC
		 */
		case MAC_MODEL_PB150:	/* no external video */
		case MAC_MODEL_PB160:
		case MAC_MODEL_PB165:
		case MAC_MODEL_PB180:
		case MAC_MODEL_PB210:
		case MAC_MODEL_PB230:
			strcpy(macfb_fix.id, "GSC");
			break;

		/*
		 * Internal is TIM, External is ViSC
		 */
		case MAC_MODEL_PB165C:
		case MAC_MODEL_PB180C:
			strcpy(macfb_fix.id, "TIM");
			break;

		/*
		 * Internal is CSC, External is Keystone+Ariel.
		 */
		case MAC_MODEL_PB190:	/* external video is optional */
		case MAC_MODEL_PB520:
		case MAC_MODEL_PB250:
		case MAC_MODEL_PB270C:
		case MAC_MODEL_PB280:
		case MAC_MODEL_PB280C:
			strcpy(macfb_fix.id, "CSC");
			macfb_setpalette = csc_setpalette;
			csc_cmap_regs = ioremap(CSC_BASE, 0x1000);
			macfb_defined.activate = FB_ACTIVATE_NOW;
			break;

		default:
			strcpy(macfb_fix.id, "Unknown");
			break;
		}

	fb_info.fbops		= &macfb_ops;
	fb_info.var		= macfb_defined;
	fb_info.fix		= macfb_fix;
	fb_info.pseudo_palette	= pseudo_palette;
	fb_info.flags		= FBINFO_DEFAULT;

	err = fb_alloc_cmap(&fb_info.cmap, video_cmap_len, 0);
	if (err)
		goto fail_unmap;

	err = register_framebuffer(&fb_info);
	if (err)
		goto fail_dealloc;

	fb_info(&fb_info, "%s frame buffer device\n", fb_info.fix.id);

	return 0;

fail_dealloc:
	fb_dealloc_cmap(&fb_info.cmap);
fail_unmap:
	iounmap(fb_info.screen_base);
	iounmap_macfb();
	return err;
}

module_init(macfb_init);
MODULE_LICENSE("GPL");
