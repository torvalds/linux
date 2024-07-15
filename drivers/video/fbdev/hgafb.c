/*
 * linux/drivers/video/hgafb.c -- Hercules graphics adaptor frame buffer device
 *
 *      Created 25 Nov 1999 by Ferenc Bakonyi (fero@drama.obuda.kando.hu)
 *      Based on skeletonfb.c by Geert Uytterhoeven and
 *               mdacon.c by Andrew Apted
 *
 * History:
 *
 * - Revision 0.1.8 (23 Oct 2002): Ported to new framebuffer api.
 *
 * - Revision 0.1.7 (23 Jan 2001): fix crash resulting from MDA only cards
 *				   being detected as Hercules.	 (Paul G.)
 * - Revision 0.1.6 (17 Aug 2000): new style structs
 *                                 documentation
 * - Revision 0.1.5 (13 Mar 2000): spinlocks instead of saveflags();cli();etc
 *                                 minor fixes
 * - Revision 0.1.4 (24 Jan 2000): fixed a bug in hga_card_detect() for
 *                                  HGA-only systems
 * - Revision 0.1.3 (22 Jan 2000): modified for the new fb_info structure
 *                                 screen is cleared after rmmod
 *                                 virtual resolutions
 *                                 module parameter 'nologo={0|1}'
 *                                 the most important: boot logo :)
 * - Revision 0.1.0  (6 Dec 1999): faster scrolling and minor fixes
 * - First release  (25 Nov 1999)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/vga.h>

#if 0
#define DPRINTK(args...) printk(KERN_DEBUG __FILE__": " ##args)
#else
#define DPRINTK(args...)
#endif

#if 0
#define CHKINFO(ret) if (info != &fb_info) { printk(KERN_DEBUG __FILE__": This should never happen, line:%d \n", __LINE__); return ret; }
#else
#define CHKINFO(ret)
#endif

/* Description of the hardware layout */

static void __iomem *hga_vram;			/* Base of video memory */
static unsigned long hga_vram_len;		/* Size of video memory */

#define HGA_ROWADDR(row) ((row%4)*8192 + (row>>2)*90)
#define HGA_TXT			0
#define HGA_GFX			1

static inline u8 __iomem * rowaddr(struct fb_info *info, u_int row)
{
	return info->screen_base + HGA_ROWADDR(row);
}

static int hga_mode = -1;			/* 0 = txt, 1 = gfx mode */

static enum { TYPE_HERC, TYPE_HERCPLUS, TYPE_HERCCOLOR } hga_type;
static char *hga_type_name;

#define HGA_INDEX_PORT		0x3b4		/* Register select port */
#define HGA_VALUE_PORT		0x3b5		/* Register value port */
#define HGA_MODE_PORT		0x3b8		/* Mode control port */
#define HGA_STATUS_PORT		0x3ba		/* Status and Config port */
#define HGA_GFX_PORT		0x3bf		/* Graphics control port */

/* HGA register values */

#define HGA_CURSOR_BLINKING	0x00
#define HGA_CURSOR_OFF		0x20
#define HGA_CURSOR_SLOWBLINK	0x60

#define HGA_MODE_GRAPHICS	0x02
#define HGA_MODE_VIDEO_EN	0x08
#define HGA_MODE_BLINK_EN	0x20
#define HGA_MODE_GFX_PAGE1	0x80

#define HGA_STATUS_HSYNC	0x01
#define HGA_STATUS_VSYNC	0x80
#define HGA_STATUS_VIDEO	0x08

#define HGA_CONFIG_COL132	0x08
#define HGA_GFX_MODE_EN		0x01
#define HGA_GFX_PAGE_EN		0x02

/* Global locks */

static DEFINE_SPINLOCK(hga_reg_lock);

/* Framebuffer driver structures */

static const struct fb_var_screeninfo hga_default_var = {
	.xres		= 720,
	.yres 		= 348,
	.xres_virtual 	= 720,
	.yres_virtual	= 348,
	.bits_per_pixel = 1,
	.red 		= {0, 1, 0},
	.green 		= {0, 1, 0},
	.blue 		= {0, 1, 0},
	.transp 	= {0, 0, 0},
	.height 	= -1,
	.width 		= -1,
};

static struct fb_fix_screeninfo hga_fix = {
	.id 		= "HGA",
	.type 		= FB_TYPE_PACKED_PIXELS,	/* (not sure) */
	.visual 	= FB_VISUAL_MONO10,
	.xpanstep 	= 8,
	.ypanstep 	= 8,
	.line_length 	= 90,
	.accel 		= FB_ACCEL_NONE
};

/* Don't assume that tty1 will be the initial current console. */
static int release_io_port = 0;
static int release_io_ports = 0;
static bool nologo = 0;

/* -------------------------------------------------------------------------
 *
 * Low level hardware functions
 *
 * ------------------------------------------------------------------------- */

static void write_hga_b(unsigned int val, unsigned char reg)
{
	outb_p(reg, HGA_INDEX_PORT);
	outb_p(val, HGA_VALUE_PORT);
}

static void write_hga_w(unsigned int val, unsigned char reg)
{
	outb_p(reg,   HGA_INDEX_PORT); outb_p(val >> 8,   HGA_VALUE_PORT);
	outb_p(reg+1, HGA_INDEX_PORT); outb_p(val & 0xff, HGA_VALUE_PORT);
}

static int test_hga_b(unsigned char val, unsigned char reg)
{
	outb_p(reg, HGA_INDEX_PORT);
	outb  (val, HGA_VALUE_PORT);
	udelay(20); val = (inb_p(HGA_VALUE_PORT) == val);
	return val;
}

static void hga_clear_screen(void)
{
	unsigned char fillchar = 0xbf; /* magic */
	unsigned long flags;

	spin_lock_irqsave(&hga_reg_lock, flags);
	if (hga_mode == HGA_TXT)
		fillchar = ' ';
	else if (hga_mode == HGA_GFX)
		fillchar = 0x00;
	spin_unlock_irqrestore(&hga_reg_lock, flags);
	if (fillchar != 0xbf)
		memset_io(hga_vram, fillchar, hga_vram_len);
}

static void hga_txt_mode(void)
{
	unsigned long flags;

	spin_lock_irqsave(&hga_reg_lock, flags);
	outb_p(HGA_MODE_VIDEO_EN | HGA_MODE_BLINK_EN, HGA_MODE_PORT);
	outb_p(0x00, HGA_GFX_PORT);
	outb_p(0x00, HGA_STATUS_PORT);

	write_hga_b(0x61, 0x00);	/* horizontal total */
	write_hga_b(0x50, 0x01);	/* horizontal displayed */
	write_hga_b(0x52, 0x02);	/* horizontal sync pos */
	write_hga_b(0x0f, 0x03);	/* horizontal sync width */

	write_hga_b(0x19, 0x04);	/* vertical total */
	write_hga_b(0x06, 0x05);	/* vertical total adjust */
	write_hga_b(0x19, 0x06);	/* vertical displayed */
	write_hga_b(0x19, 0x07);	/* vertical sync pos */

	write_hga_b(0x02, 0x08);	/* interlace mode */
	write_hga_b(0x0d, 0x09);	/* maximum scanline */
	write_hga_b(0x0c, 0x0a);	/* cursor start */
	write_hga_b(0x0d, 0x0b);	/* cursor end */

	write_hga_w(0x0000, 0x0c);	/* start address */
	write_hga_w(0x0000, 0x0e);	/* cursor location */

	hga_mode = HGA_TXT;
	spin_unlock_irqrestore(&hga_reg_lock, flags);
}

static void hga_gfx_mode(void)
{
	unsigned long flags;

	spin_lock_irqsave(&hga_reg_lock, flags);
	outb_p(0x00, HGA_STATUS_PORT);
	outb_p(HGA_GFX_MODE_EN, HGA_GFX_PORT);
	outb_p(HGA_MODE_VIDEO_EN | HGA_MODE_GRAPHICS, HGA_MODE_PORT);

	write_hga_b(0x35, 0x00);	/* horizontal total */
	write_hga_b(0x2d, 0x01);	/* horizontal displayed */
	write_hga_b(0x2e, 0x02);	/* horizontal sync pos */
	write_hga_b(0x07, 0x03);	/* horizontal sync width */

	write_hga_b(0x5b, 0x04);	/* vertical total */
	write_hga_b(0x02, 0x05);	/* vertical total adjust */
	write_hga_b(0x57, 0x06);	/* vertical displayed */
	write_hga_b(0x57, 0x07);	/* vertical sync pos */

	write_hga_b(0x02, 0x08);	/* interlace mode */
	write_hga_b(0x03, 0x09);	/* maximum scanline */
	write_hga_b(0x00, 0x0a);	/* cursor start */
	write_hga_b(0x00, 0x0b);	/* cursor end */

	write_hga_w(0x0000, 0x0c);	/* start address */
	write_hga_w(0x0000, 0x0e);	/* cursor location */

	hga_mode = HGA_GFX;
	spin_unlock_irqrestore(&hga_reg_lock, flags);
}

static void hga_show_logo(struct fb_info *info)
{
/*
	void __iomem *dest = hga_vram;
	char *logo = linux_logo_bw;
	int x, y;

	for (y = 134; y < 134 + 80 ; y++) * this needs some cleanup *
		for (x = 0; x < 10 ; x++)
			writeb(~*(logo++),(dest + HGA_ROWADDR(y) + x + 40));
*/
}

static void hga_pan(unsigned int xoffset, unsigned int yoffset)
{
	unsigned int base;
	unsigned long flags;

	base = (yoffset / 8) * 90 + xoffset;
	spin_lock_irqsave(&hga_reg_lock, flags);
	write_hga_w(base, 0x0c);	/* start address */
	spin_unlock_irqrestore(&hga_reg_lock, flags);
	DPRINTK("hga_pan: base:%d\n", base);
}

static void hga_blank(int blank_mode)
{
	unsigned long flags;

	spin_lock_irqsave(&hga_reg_lock, flags);
	if (blank_mode) {
		outb_p(0x00, HGA_MODE_PORT);	/* disable video */
	} else {
		outb_p(HGA_MODE_VIDEO_EN | HGA_MODE_GRAPHICS, HGA_MODE_PORT);
	}
	spin_unlock_irqrestore(&hga_reg_lock, flags);
}

static int hga_card_detect(void)
{
	int count = 0;
	void __iomem *p, *q;
	unsigned short p_save, q_save;

	hga_vram_len  = 0x08000;

	hga_vram = ioremap(0xb0000, hga_vram_len);
	if (!hga_vram)
		return -ENOMEM;

	if (request_region(0x3b0, 12, "hgafb"))
		release_io_ports = 1;
	if (request_region(0x3bf, 1, "hgafb"))
		release_io_port = 1;

	/* do a memory check */

	p = hga_vram;
	q = hga_vram + 0x01000;

	p_save = readw(p); q_save = readw(q);

	writew(0xaa55, p); if (readw(p) == 0xaa55) count++;
	writew(0x55aa, p); if (readw(p) == 0x55aa) count++;
	writew(p_save, p);

	if (count != 2)
		goto error;

	/* Ok, there is definitely a card registering at the correct
	 * memory location, so now we do an I/O port test.
	 */

	if (!test_hga_b(0x66, 0x0f))	    /* cursor low register */
		goto error;

	if (!test_hga_b(0x99, 0x0f))     /* cursor low register */
		goto error;

	/* See if the card is a Hercules, by checking whether the vsync
	 * bit of the status register is changing.  This test lasts for
	 * approximately 1/10th of a second.
	 */

	p_save = q_save = inb_p(HGA_STATUS_PORT) & HGA_STATUS_VSYNC;

	for (count=0; count < 50000 && p_save == q_save; count++) {
		q_save = inb(HGA_STATUS_PORT) & HGA_STATUS_VSYNC;
		udelay(2);
	}

	if (p_save == q_save)
		goto error;

	switch (inb_p(HGA_STATUS_PORT) & 0x70) {
		case 0x10:
			hga_type = TYPE_HERCPLUS;
			hga_type_name = "HerculesPlus";
			break;
		case 0x50:
			hga_type = TYPE_HERCCOLOR;
			hga_type_name = "HerculesColor";
			break;
		default:
			hga_type = TYPE_HERC;
			hga_type_name = "Hercules";
			break;
	}
	return 0;
error:
	if (release_io_ports)
		release_region(0x3b0, 12);
	if (release_io_port)
		release_region(0x3bf, 1);

	iounmap(hga_vram);

	pr_err("hgafb: HGA card not detected.\n");

	return -EINVAL;
}

/**
 *	hgafb_open - open the framebuffer device
 *	@info: pointer to fb_info object containing info for current hga board
 *	@init: open by console system or userland.
 *
 *	Returns: %0
 */

static int hgafb_open(struct fb_info *info, int init)
{
	hga_gfx_mode();
	hga_clear_screen();
	if (!nologo) hga_show_logo(info);
	return 0;
}

/**
 *	hgafb_release - open the framebuffer device
 *	@info: pointer to fb_info object containing info for current hga board
 *	@init: open by console system or userland.
 *
 *	Returns: %0
 */

static int hgafb_release(struct fb_info *info, int init)
{
	hga_txt_mode();
	hga_clear_screen();
	return 0;
}

/**
 *	hgafb_setcolreg - set color registers
 *	@regno:register index to set
 *	@red:red value, unused
 *	@green:green value, unused
 *	@blue:blue value, unused
 *	@transp:transparency value, unused
 *	@info:unused
 *
 *	This callback function is used to set the color registers of a HGA
 *	board. Since we have only two fixed colors only @regno is checked.
 *	A zero is returned on success and 1 for failure.
 *
 *	Returns: %0
 */

static int hgafb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info)
{
	if (regno > 1)
		return 1;
	return 0;
}

/**
 *	hgafb_pan_display - pan or wrap the display
 *	@var:contains new xoffset, yoffset and vmode values
 *	@info:pointer to fb_info object containing info for current hga board
 *
 *	This function looks only at xoffset, yoffset and the %FB_VMODE_YWRAP
 *	flag in @var. If input parameters are correct it calls hga_pan() to
 *	program the hardware. @info->var is updated to the new values.
 *
 *	Returns: %0 on success or %-EINVAL for failure.
 */

static int hgafb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset >= info->var.yres_virtual ||
		    var->xoffset)
			return -EINVAL;
	} else {
		if (var->xoffset + info->var.xres > info->var.xres_virtual
		 || var->yoffset + info->var.yres > info->var.yres_virtual
		 || var->yoffset % 8)
			return -EINVAL;
	}

	hga_pan(var->xoffset, var->yoffset);
	return 0;
}

/**
 *	hgafb_blank - (un)blank the screen
 *	@blank_mode:blanking method to use
 *	@info:unused
 *
 *	Blank the screen if blank_mode != 0, else unblank.
 *	Implements VESA suspend and powerdown modes on hardware that supports
 *	disabling hsync/vsync:
 *		@blank_mode == 2 means suspend vsync,
 *		@blank_mode == 3 means suspend hsync,
 *		@blank_mode == 4 means powerdown.
 *
 * Returns: %0
 */

static int hgafb_blank(int blank_mode, struct fb_info *info)
{
	hga_blank(blank_mode);
	return 0;
}

/*
 * Accel functions
 */
static void hgafb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	u_int rows, y;
	u8 __iomem *dest;

	y = rect->dy;

	for (rows = rect->height; rows--; y++) {
		dest = rowaddr(info, y) + (rect->dx >> 3);
		switch (rect->rop) {
		case ROP_COPY:
			memset_io(dest, rect->color, (rect->width >> 3));
			break;
		case ROP_XOR:
			fb_writeb(~(fb_readb(dest)), dest);
			break;
		}
	}
}

static void hgafb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	u_int rows, y1, y2;
	u8 __iomem *src;
	u8 __iomem *dest;

	if (area->dy <= area->sy) {
		y1 = area->sy;
		y2 = area->dy;

		for (rows = area->height; rows--; ) {
			src = rowaddr(info, y1) + (area->sx >> 3);
			dest = rowaddr(info, y2) + (area->dx >> 3);
			memmove(dest, src, (area->width >> 3));
			y1++;
			y2++;
		}
	} else {
		y1 = area->sy + area->height - 1;
		y2 = area->dy + area->height - 1;

		for (rows = area->height; rows--;) {
			src = rowaddr(info, y1) + (area->sx >> 3);
			dest = rowaddr(info, y2) + (area->dx >> 3);
			memmove(dest, src, (area->width >> 3));
			y1--;
			y2--;
		}
	}
}

static void hgafb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	u8 __iomem *dest;
	u8 *cdat = (u8 *) image->data;
	u_int rows, y = image->dy;
	u_int x;
	u8 d;

	for (rows = image->height; rows--; y++) {
		for (x = 0; x < image->width; x+= 8) {
			d = *cdat++;
			dest = rowaddr(info, y) + ((image->dx + x)>> 3);
			fb_writeb(d, dest);
		}
	}
}

static const struct fb_ops hgafb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= hgafb_open,
	.fb_release	= hgafb_release,
	__FB_DEFAULT_IOMEM_OPS_RDWR,
	.fb_setcolreg	= hgafb_setcolreg,
	.fb_pan_display	= hgafb_pan_display,
	.fb_blank	= hgafb_blank,
	.fb_fillrect	= hgafb_fillrect,
	.fb_copyarea	= hgafb_copyarea,
	.fb_imageblit	= hgafb_imageblit,
	__FB_DEFAULT_IOMEM_OPS_MMAP,
};

/* ------------------------------------------------------------------------- *
 *
 * Functions in fb_info
 *
 * ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */

	/*
	 *  Initialization
	 */

static int hgafb_probe(struct platform_device *pdev)
{
	struct fb_info *info;
	int ret;

	ret = hga_card_detect();
	if (ret)
		return ret;

	printk(KERN_INFO "hgafb: %s with %ldK of memory detected.\n",
		hga_type_name, hga_vram_len/1024);

	info = framebuffer_alloc(0, &pdev->dev);
	if (!info) {
		iounmap(hga_vram);
		return -ENOMEM;
	}

	hga_fix.smem_start = (unsigned long)hga_vram;
	hga_fix.smem_len = hga_vram_len;

	info->flags = FBINFO_HWACCEL_YPAN;
	info->var = hga_default_var;
	info->fix = hga_fix;
	info->monspecs.hfmin = 0;
	info->monspecs.hfmax = 0;
	info->monspecs.vfmin = 10000;
	info->monspecs.vfmax = 10000;
	info->monspecs.dpms = 0;
	info->fbops = &hgafb_ops;
	info->screen_base = hga_vram;

        if (register_framebuffer(info) < 0) {
		framebuffer_release(info);
		iounmap(hga_vram);
		return -EINVAL;
	}

	fb_info(info, "%s frame buffer device\n", info->fix.id);
	platform_set_drvdata(pdev, info);
	return 0;
}

static void hgafb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);

	hga_txt_mode();
	hga_clear_screen();

	if (info) {
		unregister_framebuffer(info);
		framebuffer_release(info);
	}

	iounmap(hga_vram);

	if (release_io_ports)
		release_region(0x3b0, 12);

	if (release_io_port)
		release_region(0x3bf, 1);
}

static struct platform_driver hgafb_driver = {
	.probe = hgafb_probe,
	.remove_new = hgafb_remove,
	.driver = {
		.name = "hgafb",
	},
};

static struct platform_device *hgafb_device;

static int __init hgafb_init(void)
{
	int ret;

	if (fb_get_options("hgafb", NULL))
		return -ENODEV;

	ret = platform_driver_register(&hgafb_driver);

	if (!ret) {
		hgafb_device = platform_device_register_simple("hgafb", 0, NULL, 0);

		if (IS_ERR(hgafb_device)) {
			platform_driver_unregister(&hgafb_driver);
			ret = PTR_ERR(hgafb_device);
		}
	}

	return ret;
}

static void __exit hgafb_exit(void)
{
	platform_device_unregister(hgafb_device);
	platform_driver_unregister(&hgafb_driver);
}

/* -------------------------------------------------------------------------
 *
 *  Modularization
 *
 * ------------------------------------------------------------------------- */

MODULE_AUTHOR("Ferenc Bakonyi <fero@drama.obuda.kando.hu>");
MODULE_DESCRIPTION("FBDev driver for Hercules Graphics Adaptor");
MODULE_LICENSE("GPL");

module_param(nologo, bool, 0);
MODULE_PARM_DESC(nologo, "Disables startup logo if != 0 (default=0)");
module_init(hgafb_init);
module_exit(hgafb_exit);
