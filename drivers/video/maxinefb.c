/*
 *      linux/drivers/video/maxinefb.c
 *
 *	DECstation 5000/xx onboard framebuffer support ... derived from:
 *	"HP300 Topcat framebuffer support (derived from macfb of all things)
 *	Phil Blundell <philb@gnu.org> 1998", the original code can be
 *      found in the file hpfb.c in the same directory.
 *
 *      DECstation related code Copyright (C) 1999,2000,2001 by
 *      Michael Engel <engel@unix-ag.org> and
 *      Karsten Merker <merker@linuxtag.org>.
 *      This file is subject to the terms and conditions of the GNU General
 *      Public License.  See the file COPYING in the main directory of this
 *      archive for more details.
 *
 */

/*
 * Changes:
 * 2001/01/27 removed debugging and testing code, fixed fb_ops
 *            initialization which had caused a crash before,
 *            general cleanup, first official release (KM)
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <video/maxinefb.h>

/* bootinfo.h defines the machine type values, needed when checking */
/* whether are really running on a maxine, KM                       */
#include <asm/bootinfo.h>

static struct fb_info fb_info;

static struct fb_var_screeninfo maxinefb_defined = {
	.xres =		1024,
	.yres =		768,
	.xres_virtual =	1024,
	.yres_virtual =	768,
	.bits_per_pixel =8,
	.activate =	FB_ACTIVATE_NOW,
	.height =	-1,
	.width =	-1,
	.vmode =	FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo maxinefb_fix = {
	.id =		"Maxine",
	.smem_len =	(1024*768),
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_PSEUDOCOLOR,
	.line_length =	1024,
};

/* Handle the funny Inmos RamDAC/video controller ... */

void maxinefb_ims332_write_register(int regno, register unsigned int val)
{
	register unsigned char *regs = (char *) MAXINEFB_IMS332_ADDRESS;
	unsigned char *wptr;

	wptr = regs + 0xa0000 + (regno << 4);
	*((volatile unsigned int *) (regs)) = (val >> 8) & 0xff00;
	*((volatile unsigned short *) (wptr)) = val;
}

unsigned int maxinefb_ims332_read_register(int regno)
{
	register unsigned char *regs = (char *) MAXINEFB_IMS332_ADDRESS;
	unsigned char *rptr;
	register unsigned int j, k;

	rptr = regs + 0x80000 + (regno << 4);
	j = *((volatile unsigned short *) rptr);
	k = *((volatile unsigned short *) regs);

	return (j & 0xffff) | ((k & 0xff00) << 8);
}

/* Set the palette */
static int maxinefb_setcolreg(unsigned regno, unsigned red, unsigned green,
			      unsigned blue, unsigned transp, struct fb_info *info)
{
	/* value to be written into the palette reg. */
	unsigned long hw_colorvalue = 0;

	if (regno > 255)
		return 1;

	red   >>= 8;    /* The cmap fields are 16 bits    */
	green >>= 8;    /* wide, but the harware colormap */
	blue  >>= 8;    /* registers are only 8 bits wide */

	hw_colorvalue = (blue << 16) + (green << 8) + (red);

	maxinefb_ims332_write_register(IMS332_REG_COLOR_PALETTE + regno,
				       hw_colorvalue);
	return 0;
}

static struct fb_ops maxinefb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= maxinefb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

int __init maxinefb_init(void)
{
	unsigned long fboff;
	unsigned long fb_start;
	int i;

	if (fb_get_options("maxinefb", NULL))
		return -ENODEV;

	/* Validate we're on the proper machine type */
	if (mips_machtype != MACH_DS5000_XX) {
		return -EINVAL;
	}

	printk(KERN_INFO "Maxinefb: Personal DECstation detected\n");
	printk(KERN_INFO "Maxinefb: initializing onboard framebuffer\n");

	/* Framebuffer display memory base address */
	fb_start = DS5000_xx_ONBOARD_FBMEM_START;

	/* Clear screen */
	for (fboff = fb_start; fboff < fb_start + 0x1ffff; fboff++)
		*(volatile unsigned char *)fboff = 0x0;

	maxinefb_fix.smem_start = fb_start;
	
	/* erase hardware cursor */
	for (i = 0; i < 512; i++) {
		maxinefb_ims332_write_register(IMS332_REG_CURSOR_RAM + i,
					       0);
		/*
		   if (i&0x8 == 0)
		   maxinefb_ims332_write_register (IMS332_REG_CURSOR_RAM + i, 0x0f);
		   else
		   maxinefb_ims332_write_register (IMS332_REG_CURSOR_RAM + i, 0xf0);
		 */
	}

	fb_info.fbops = &maxinefb_ops;
	fb_info.screen_base = (char *)maxinefb_fix.smem_start;
	fb_info.var = maxinefb_defined;
	fb_info.fix = maxinefb_fix;
	fb_info.flags = FBINFO_DEFAULT;

	fb_alloc_cmap(&fb_info.cmap, 256, 0);

	if (register_framebuffer(&fb_info) < 0)
		return 1;
	return 0;
}

static void __exit maxinefb_exit(void)
{
	unregister_framebuffer(&fb_info);
}

#ifdef MODULE
MODULE_LICENSE("GPL");
#endif
module_init(maxinefb_init);
module_exit(maxinefb_exit);

