/*
 * linux/drivers/video/hitfb.c -- Hitachi LCD frame buffer device
 *
 * (C) 1999 Mihai Spatar
 * (C) 2000 YAEGASHI Takeshi
 * (C) 2003, 2004 Paul Mundt
 * (C) 2003, 2004, 2006 Andriy Skulysh
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>

#include <asm/machvec.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/hd64461.h>
#include <asm/cpu/dac.h>

#define	WIDTH 640

static struct fb_var_screeninfo hitfb_var __initdata = {
	.activate	= FB_ACTIVATE_NOW,
	.height		= -1,
	.width		= -1,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo hitfb_fix __initdata = {
	.id		= "Hitachi HD64461",
	.type		= FB_TYPE_PACKED_PIXELS,
	.accel		= FB_ACCEL_NONE,
};

static u32 pseudo_palette[16];
static struct fb_info fb_info;

static inline void hitfb_accel_wait(void)
{
	while (fb_readw(HD64461_GRCFGR) & HD64461_GRCFGR_ACCSTATUS) ;
}

static inline void hitfb_accel_start(int truecolor)
{
	if (truecolor) {
		fb_writew(6, HD64461_GRCFGR);
	} else {
		fb_writew(7, HD64461_GRCFGR);
	}
}

static inline void hitfb_accel_set_dest(int truecolor, u16 dx, u16 dy,
					u16 width, u16 height)
{
	u32 saddr = WIDTH * dy + dx;
	if (truecolor)
		saddr <<= 1;

	fb_writew(width-1, HD64461_BBTDWR);
	fb_writew(height-1, HD64461_BBTDHR);

	fb_writew(saddr & 0xffff, HD64461_BBTDSARL);
	fb_writew(saddr >> 16, HD64461_BBTDSARH);

}

static inline void hitfb_accel_bitblt(int truecolor, u16 sx, u16 sy, u16 dx,
				      u16 dy, u16 width, u16 height, u16 rop,
				      u32 mask_addr)
{
	u32 saddr, daddr;
	u32 maddr = 0;

	height--;
	width--;
	fb_writew(rop, HD64461_BBTROPR);
	if ((sy < dy) || ((sy == dy) && (sx <= dx))) {
		saddr = WIDTH * (sy + height) + sx + width;
		daddr = WIDTH * (dy + height) + dx + width;
		if (mask_addr) {
			if (truecolor)
				maddr = ((width >> 3) + 1) * (height + 1) - 1;
			else
				maddr =
				    (((width >> 4) + 1) * (height + 1) - 1) * 2;

			fb_writew((1 << 5) | 1, HD64461_BBTMDR);
		} else
			fb_writew(1, HD64461_BBTMDR);
	} else {
		saddr = WIDTH * sy + sx;
		daddr = WIDTH * dy + dx;
		if (mask_addr) {
			fb_writew((1 << 5), HD64461_BBTMDR);
		} else {
			fb_writew(0, HD64461_BBTMDR);
		}
	}
	if (truecolor) {
		saddr <<= 1;
		daddr <<= 1;
	}
	fb_writew(width, HD64461_BBTDWR);
	fb_writew(height, HD64461_BBTDHR);
	fb_writew(saddr & 0xffff, HD64461_BBTSSARL);
	fb_writew(saddr >> 16, HD64461_BBTSSARH);
	fb_writew(daddr & 0xffff, HD64461_BBTDSARL);
	fb_writew(daddr >> 16, HD64461_BBTDSARH);
	if (mask_addr) {
		maddr += mask_addr;
		fb_writew(maddr & 0xffff, HD64461_BBTMARL);
		fb_writew(maddr >> 16, HD64461_BBTMARH);
	}
	hitfb_accel_start(truecolor);
}

static void hitfb_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{
	if (rect->rop != ROP_COPY)
		cfb_fillrect(p, rect);
	else {
		hitfb_accel_wait();
		fb_writew(0x00f0, HD64461_BBTROPR);
		fb_writew(16, HD64461_BBTMDR);

		if (p->var.bits_per_pixel == 16) {
			fb_writew(((u32 *) (p->pseudo_palette))[rect->color],
				  HD64461_GRSCR);
			hitfb_accel_set_dest(1, rect->dx, rect->dy, rect->width,
					     rect->height);
			hitfb_accel_start(1);
		} else {
			fb_writew(rect->color, HD64461_GRSCR);
			hitfb_accel_set_dest(0, rect->dx, rect->dy, rect->width,
					     rect->height);
			hitfb_accel_start(0);
		}
	}
}

static void hitfb_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{
	hitfb_accel_wait();
	hitfb_accel_bitblt(p->var.bits_per_pixel == 16, area->sx, area->sy,
			   area->dx, area->dy, area->width, area->height,
			   0x00cc, 0);
}

static int hitfb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	int xoffset = var->xoffset;
	int yoffset = var->yoffset;

	if (xoffset != 0)
		return -EINVAL;

	fb_writew((yoffset*info->fix.line_length)>>10, HD64461_LCDCBAR);

	return 0;
}

int hitfb_blank(int blank_mode, struct fb_info *info)
{
	unsigned short v;

	if (blank_mode) {
		v = fb_readw(HD64461_LDR1);
		v &= ~HD64461_LDR1_DON;
		fb_writew(v, HD64461_LDR1);

		v = fb_readw(HD64461_LCDCCR);
		v |= HD64461_LCDCCR_MOFF;
		fb_writew(v, HD64461_LCDCCR);

		v = fb_readw(HD64461_STBCR);
		v |= HD64461_STBCR_SLCDST;
		fb_writew(v, HD64461_STBCR);
	} else {
		v = fb_readw(HD64461_STBCR);
		v &= ~HD64461_STBCR_SLCDST;
		fb_writew(v, HD64461_STBCR);

		v = fb_readw(HD64461_LCDCCR);
		v &= ~(HD64461_LCDCCR_MOFF | HD64461_LCDCCR_STREQ);
		fb_writew(v, HD64461_LCDCCR);

		do {
		    v = fb_readw(HD64461_LCDCCR);
		} while(v&HD64461_LCDCCR_STBACK);

		v = fb_readw(HD64461_LDR1);
		v |= HD64461_LDR1_DON;
		fb_writew(v, HD64461_LDR1);
	}
	return 0;
}

static int hitfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp, struct fb_info *info)
{
	if (regno >= 256)
		return 1;

	switch (info->var.bits_per_pixel) {
	case 8:
		fb_writew(regno << 8, HD64461_CPTWAR);
		fb_writew(red >> 10, HD64461_CPTWDR);
		fb_writew(green >> 10, HD64461_CPTWDR);
		fb_writew(blue >> 10, HD64461_CPTWDR);
		break;
	case 16:
		if (regno >= 16)
			return 1;
		((u32 *) (info->pseudo_palette))[regno] =
		    ((red & 0xf800)) |
		    ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
		break;
	}
	return 0;
}

static int hitfb_sync(struct fb_info *info)
{
	hitfb_accel_wait();

	return 0;
}

static int hitfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	int maxy;

	var->xres = info->var.xres;
	var->xres_virtual = info->var.xres;
	var->yres = info->var.yres;

	if ((var->bits_per_pixel != 8) && (var->bits_per_pixel != 16))
		var->bits_per_pixel = info->var.bits_per_pixel;

	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	maxy = info->fix.smem_len / var->xres;

	if (var->bits_per_pixel == 16)
		maxy /= 2;

	if (var->yres_virtual > maxy)
		var->yres_virtual = maxy;

	var->xoffset = 0;
	var->yoffset = 0;

	switch (var->bits_per_pixel) {
	case 8:
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 16:		/* RGB 565 */
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	}

	return 0;
}

static int hitfb_set_par(struct fb_info *info)
{
	unsigned short ldr3;

	switch (info->var.bits_per_pixel) {
	case 8:
		info->fix.line_length = info->var.xres;
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		info->fix.ypanstep = 16;
		break;
	case 16:
		info->fix.line_length = info->var.xres*2;
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		info->fix.ypanstep = 8;
		break;
	}

	fb_writew(info->fix.line_length, HD64461_LCDCLOR);
	ldr3 = fb_readw(HD64461_LDR3);
	ldr3 &= ~15;
	ldr3 |= (info->var.bits_per_pixel == 8) ? 4 : 8;
	fb_writew(ldr3, HD64461_LDR3);
	return 0;
}

static struct fb_ops hitfb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= hitfb_check_var,
	.fb_set_par		= hitfb_set_par,
	.fb_setcolreg	= hitfb_setcolreg,
	.fb_blank	= hitfb_blank,
	.fb_sync	= hitfb_sync,
	.fb_pan_display = hitfb_pan_display,
	.fb_fillrect	= hitfb_fillrect,
	.fb_copyarea	= hitfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static int __init hitfb_probe(struct platform_device *dev)
{
	unsigned short lcdclor, ldr3, ldvndr;

	if (fb_get_options("hitfb", NULL))
		return -ENODEV;

	hitfb_fix.mmio_start = CONFIG_HD64461_IOBASE+0x1000;
	hitfb_fix.mmio_len = 0x1000;
	hitfb_fix.smem_start = CONFIG_HD64461_IOBASE + 0x02000000;
	hitfb_fix.smem_len = 512 * 1024;

	lcdclor = fb_readw(HD64461_LCDCLOR);
	ldvndr = fb_readw(HD64461_LDVNDR);
	ldr3 = fb_readw(HD64461_LDR3);

	switch (ldr3 & 15) {
	default:
	case 4:
		hitfb_var.bits_per_pixel = 8;
		hitfb_var.xres = lcdclor;
		break;
	case 8:
		hitfb_var.bits_per_pixel = 16;
		hitfb_var.xres = lcdclor / 2;
		break;
	}
	hitfb_fix.line_length = lcdclor;
	hitfb_fix.visual = (hitfb_var.bits_per_pixel == 8) ?
	    FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;
	hitfb_var.yres = ldvndr + 1;
	hitfb_var.xres_virtual = hitfb_var.xres;
	hitfb_var.yres_virtual = hitfb_fix.smem_len / lcdclor;
	switch (hitfb_var.bits_per_pixel) {
	case 8:
		hitfb_var.red.offset = 0;
		hitfb_var.red.length = 8;
		hitfb_var.green.offset = 0;
		hitfb_var.green.length = 8;
		hitfb_var.blue.offset = 0;
		hitfb_var.blue.length = 8;
		hitfb_var.transp.offset = 0;
		hitfb_var.transp.length = 0;
		break;
	case 16:		/* RGB 565 */
		hitfb_var.red.offset = 11;
		hitfb_var.red.length = 5;
		hitfb_var.green.offset = 5;
		hitfb_var.green.length = 6;
		hitfb_var.blue.offset = 0;
		hitfb_var.blue.length = 5;
		hitfb_var.transp.offset = 0;
		hitfb_var.transp.length = 0;
		break;
	}

	fb_info.fbops = &hitfb_ops;
	fb_info.var = hitfb_var;
	fb_info.fix = hitfb_fix;
	fb_info.pseudo_palette = pseudo_palette;
	fb_info.flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN |
		FBINFO_HWACCEL_FILLRECT | FBINFO_HWACCEL_COPYAREA;

	fb_info.screen_base = (void *)hitfb_fix.smem_start;

	fb_alloc_cmap(&fb_info.cmap, 256, 0);

	if (register_framebuffer(&fb_info) < 0)
		return -EINVAL;

	printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       fb_info.node, fb_info.fix.id);
	return 0;
}

static int __devexit hitfb_remove(struct platform_device *dev)
{
	return unregister_framebuffer(&fb_info);
}

#ifdef CONFIG_PM
static int hitfb_suspend(struct platform_device *dev, pm_message_t state)
{
	u16 v;

	hitfb_blank(1,0);
	v = fb_readw(HD64461_STBCR);
	v |= HD64461_STBCR_SLCKE_IST;
	fb_writew(v, HD64461_STBCR);

	return 0;
}

static int hitfb_resume(struct platform_device *dev)
{
	u16 v;

	v = fb_readw(HD64461_STBCR);
	v &= ~HD64461_STBCR_SLCKE_OST;
	msleep(100);
	v = fb_readw(HD64461_STBCR);
	v &= ~HD64461_STBCR_SLCKE_IST;
	fb_writew(v, HD64461_STBCR);
	hitfb_blank(0,0);

	return 0;
}
#endif

static struct platform_driver hitfb_driver = {
	.probe		= hitfb_probe,
	.remove		= __devexit_p(hitfb_remove),
#ifdef CONFIG_PM
	.suspend	= hitfb_suspend,
	.resume		= hitfb_resume,
#endif
	.driver		= {
		.name	= "hitfb",
	},
};

static struct platform_device hitfb_device = {
	.name	= "hitfb",
	.id	= -1,
};

static int __init hitfb_init(void)
{
	int ret;

	ret = platform_driver_register(&hitfb_driver);
	if (!ret) {
		ret = platform_device_register(&hitfb_device);
		if (ret)
			platform_driver_unregister(&hitfb_driver);
	}
	return ret;
}


static void __exit hitfb_exit(void)
{
	platform_device_unregister(&hitfb_device);
	platform_driver_unregister(&hitfb_driver);
}

module_init(hitfb_init);
module_exit(hitfb_exit);

MODULE_LICENSE("GPL");
