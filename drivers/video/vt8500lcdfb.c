/*
 *  linux/drivers/video/vt8500lcdfb.c
 *
 *  Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 *
 * Based on skeletonfb.c and pxafb.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/wait.h>

#include <mach/vt8500fb.h>

#include "vt8500lcdfb.h"
#include "wmt_ge_rops.h"

#define to_vt8500lcd_info(__info) container_of(__info, \
						struct vt8500lcd_info, fb)

static int vt8500lcd_set_par(struct fb_info *info)
{
	struct vt8500lcd_info *fbi = to_vt8500lcd_info(info);
	int reg_bpp = 5; /* 16bpp */
	int i;
	unsigned long control0;

	if (!fbi)
		return -EINVAL;

	if (info->var.bits_per_pixel <= 8) {
		/* palettized */
		info->var.red.offset    = 0;
		info->var.red.length    = info->var.bits_per_pixel;
		info->var.red.msb_right = 0;

		info->var.green.offset  = 0;
		info->var.green.length  = info->var.bits_per_pixel;
		info->var.green.msb_right = 0;

		info->var.blue.offset   = 0;
		info->var.blue.length   = info->var.bits_per_pixel;
		info->var.blue.msb_right = 0;

		info->var.transp.offset = 0;
		info->var.transp.length = 0;
		info->var.transp.msb_right = 0;

		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		info->fix.line_length = info->var.xres_virtual /
						(8/info->var.bits_per_pixel);
	} else {
		/* non-palettized */
		info->var.transp.offset = 0;
		info->var.transp.length = 0;
		info->var.transp.msb_right = 0;

		if (info->var.bits_per_pixel == 16) {
			/* RGB565 */
			info->var.red.offset = 11;
			info->var.red.length = 5;
			info->var.red.msb_right = 0;
			info->var.green.offset = 5;
			info->var.green.length = 6;
			info->var.green.msb_right = 0;
			info->var.blue.offset = 0;
			info->var.blue.length = 5;
			info->var.blue.msb_right = 0;
		} else {
			/* Equal depths per channel */
			info->var.red.offset = info->var.bits_per_pixel
							* 2 / 3;
			info->var.red.length = info->var.bits_per_pixel / 3;
			info->var.red.msb_right = 0;
			info->var.green.offset = info->var.bits_per_pixel / 3;
			info->var.green.length = info->var.bits_per_pixel / 3;
			info->var.green.msb_right = 0;
			info->var.blue.offset = 0;
			info->var.blue.length = info->var.bits_per_pixel / 3;
			info->var.blue.msb_right = 0;
		}

		info->fix.visual = FB_VISUAL_TRUECOLOR;
		info->fix.line_length = info->var.bits_per_pixel > 16 ?
					info->var.xres_virtual << 2 :
					info->var.xres_virtual << 1;
	}

	for (i = 0; i < 8; i++) {
		if (bpp_values[i] == info->var.bits_per_pixel) {
			reg_bpp = i;
			continue;
		}
	}

	control0 = readl(fbi->regbase) & ~0xf;
	writel(0, fbi->regbase);
	while (readl(fbi->regbase + 0x38) & 0x10)
		/* wait */;
	writel((((info->var.hsync_len - 1) & 0x3f) << 26)
		| ((info->var.left_margin & 0xff) << 18)
		| (((info->var.xres - 1) & 0x3ff) << 8)
		| (info->var.right_margin & 0xff), fbi->regbase + 0x4);
	writel((((info->var.vsync_len - 1) & 0x3f) << 26)
		| ((info->var.upper_margin & 0xff) << 18)
		| (((info->var.yres - 1) & 0x3ff) << 8)
		| (info->var.lower_margin & 0xff), fbi->regbase + 0x8);
	writel((((info->var.yres - 1) & 0x400) << 2)
		| ((info->var.xres - 1) & 0x400), fbi->regbase + 0x10);
	writel(0x80000000, fbi->regbase + 0x20);
	writel(control0 | (reg_bpp << 1) | 0x100, fbi->regbase);

	return 0;
}

static inline u_int chan_to_field(u_int chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int vt8500lcd_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info) {
	struct vt8500lcd_info *fbi = to_vt8500lcd_info(info);
	int ret = 1;
	unsigned int val;
	if (regno >= 256)
		return -EINVAL;

	if (info->var.grayscale)
		red = green = blue =
			(19595 * red + 38470 * green + 7471 * blue) >> 16;

	switch (fbi->fb.fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		if (regno < 16) {
			u32 *pal = fbi->fb.pseudo_palette;

			val  = chan_to_field(red, &fbi->fb.var.red);
			val |= chan_to_field(green, &fbi->fb.var.green);
			val |= chan_to_field(blue, &fbi->fb.var.blue);

			pal[regno] = val;
			ret = 0;
		}
		break;

	case FB_VISUAL_STATIC_PSEUDOCOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		writew((red & 0xf800)
		      | ((green >> 5) & 0x7e0)
		      | ((blue >> 11) & 0x1f),
		       fbi->palette_cpu + sizeof(u16) * regno);
		break;
	}

	return ret;
}

static int vt8500lcd_ioctl(struct fb_info *info, unsigned int cmd,
			 unsigned long arg)
{
	int ret = 0;
	struct vt8500lcd_info *fbi = to_vt8500lcd_info(info);

	if (cmd == FBIO_WAITFORVSYNC) {
		/* Unmask End of Frame interrupt */
		writel(0xffffffff ^ (1 << 3), fbi->regbase + 0x3c);
		ret = wait_event_interruptible_timeout(fbi->wait,
			readl(fbi->regbase + 0x38) & (1 << 3), HZ / 10);
		/* Mask back to reduce unwanted interrupt traffic */
		writel(0xffffffff, fbi->regbase + 0x3c);
		if (ret < 0)
			return ret;
		if (ret == 0)
			return -ETIMEDOUT;
	}

	return ret;
}

static int vt8500lcd_pan_display(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	unsigned pixlen = info->fix.line_length / info->var.xres_virtual;
	unsigned off = pixlen * var->xoffset
		      + info->fix.line_length * var->yoffset;
	struct vt8500lcd_info *fbi = to_vt8500lcd_info(info);

	writel((1 << 31)
		| (((var->xres_virtual - var->xres) * pixlen / 4) << 20)
		| (off >> 2), fbi->regbase + 0x20);
	return 0;
}

/*
 * vt8500lcd_blank():
 *	Blank the display by setting all palette values to zero.  Note,
 * 	True Color modes do not really use the palette, so this will not
 *      blank the display in all modes.
 */
static int vt8500lcd_blank(int blank, struct fb_info *info)
{
	int i;

	switch (blank) {
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_NORMAL:
		if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR ||
		    info->fix.visual == FB_VISUAL_STATIC_PSEUDOCOLOR)
			for (i = 0; i < 256; i++)
				vt8500lcd_setcolreg(i, 0, 0, 0, 0, info);
	case FB_BLANK_UNBLANK:
		if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR ||
		    info->fix.visual == FB_VISUAL_STATIC_PSEUDOCOLOR)
			fb_set_cmap(&info->cmap, info);
	}
	return 0;
}

static struct fb_ops vt8500lcd_ops = {
	.owner		= THIS_MODULE,
	.fb_set_par	= vt8500lcd_set_par,
	.fb_setcolreg	= vt8500lcd_setcolreg,
	.fb_fillrect	= wmt_ge_fillrect,
	.fb_copyarea	= wmt_ge_copyarea,
	.fb_imageblit	= sys_imageblit,
	.fb_sync	= wmt_ge_sync,
	.fb_ioctl	= vt8500lcd_ioctl,
	.fb_pan_display	= vt8500lcd_pan_display,
	.fb_blank	= vt8500lcd_blank,
};

static irqreturn_t vt8500lcd_handle_irq(int irq, void *dev_id)
{
	struct vt8500lcd_info *fbi = dev_id;

	if (readl(fbi->regbase + 0x38) & (1 << 3))
		wake_up_interruptible(&fbi->wait);

	writel(0xffffffff, fbi->regbase + 0x38);
	return IRQ_HANDLED;
}

static int __devinit vt8500lcd_probe(struct platform_device *pdev)
{
	struct vt8500lcd_info *fbi;
	struct resource *res;
	struct vt8500fb_platform_data *pdata = pdev->dev.platform_data;
	void *addr;
	int irq, ret;

	ret = -ENOMEM;
	fbi = NULL;

	fbi = kzalloc(sizeof(struct vt8500lcd_info) + sizeof(u32) * 16,
							GFP_KERNEL);
	if (!fbi) {
		dev_err(&pdev->dev, "Failed to initialize framebuffer device\n");
		ret = -ENOMEM;
		goto failed;
	}

	strcpy(fbi->fb.fix.id, "VT8500 LCD");

	fbi->fb.fix.type	= FB_TYPE_PACKED_PIXELS;
	fbi->fb.fix.xpanstep	= 0;
	fbi->fb.fix.ypanstep	= 1;
	fbi->fb.fix.ywrapstep	= 0;
	fbi->fb.fix.accel	= FB_ACCEL_NONE;

	fbi->fb.var.nonstd	= 0;
	fbi->fb.var.activate	= FB_ACTIVATE_NOW;
	fbi->fb.var.height	= -1;
	fbi->fb.var.width	= -1;
	fbi->fb.var.vmode	= FB_VMODE_NONINTERLACED;

	fbi->fb.fbops		= &vt8500lcd_ops;
	fbi->fb.flags		= FBINFO_DEFAULT
				| FBINFO_HWACCEL_COPYAREA
				| FBINFO_HWACCEL_FILLRECT
				| FBINFO_HWACCEL_YPAN
				| FBINFO_VIRTFB
				| FBINFO_PARTIAL_PAN_OK;
	fbi->fb.node		= -1;

	addr = fbi;
	addr = addr + sizeof(struct vt8500lcd_info);
	fbi->fb.pseudo_palette	= addr;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no I/O memory resource defined\n");
		ret = -ENODEV;
		goto failed_fbi;
	}

	res = request_mem_region(res->start, resource_size(res), "vt8500lcd");
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to request I/O memory\n");
		ret = -EBUSY;
		goto failed_fbi;
	}

	fbi->regbase = ioremap(res->start, resource_size(res));
	if (fbi->regbase == NULL) {
		dev_err(&pdev->dev, "failed to map I/O memory\n");
		ret = -EBUSY;
		goto failed_free_res;
	}

	fbi->fb.fix.smem_start	= pdata->video_mem_phys;
	fbi->fb.fix.smem_len	= pdata->video_mem_len;
	fbi->fb.screen_base	= pdata->video_mem_virt;

	fbi->palette_size	= PAGE_ALIGN(512);
	fbi->palette_cpu	= dma_alloc_coherent(&pdev->dev,
						     fbi->palette_size,
						     &fbi->palette_phys,
						     GFP_KERNEL);
	if (fbi->palette_cpu == NULL) {
		dev_err(&pdev->dev, "Failed to allocate palette buffer\n");
		ret = -ENOMEM;
		goto failed_free_io;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no IRQ defined\n");
		ret = -ENODEV;
		goto failed_free_palette;
	}

	ret = request_irq(irq, vt8500lcd_handle_irq, IRQF_DISABLED, "LCD", fbi);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed: %d\n", ret);
		ret = -EBUSY;
		goto failed_free_palette;
	}

	init_waitqueue_head(&fbi->wait);

	if (fb_alloc_cmap(&fbi->fb.cmap, 256, 0) < 0) {
		dev_err(&pdev->dev, "Failed to allocate color map\n");
		ret = -ENOMEM;
		goto failed_free_irq;
	}

	fb_videomode_to_var(&fbi->fb.var, &pdata->mode);
	fbi->fb.var.bits_per_pixel	= pdata->bpp;
	fbi->fb.var.xres_virtual	= pdata->xres_virtual;
	fbi->fb.var.yres_virtual	= pdata->yres_virtual;

	ret = vt8500lcd_set_par(&fbi->fb);
	if (ret) {
		dev_err(&pdev->dev, "Failed to set parameters\n");
		goto failed_free_cmap;
	}

	writel(fbi->fb.fix.smem_start >> 22, fbi->regbase + 0x1c);
	writel((fbi->palette_phys & 0xfffffe00) | 1, fbi->regbase + 0x18);

	platform_set_drvdata(pdev, fbi);

	ret = register_framebuffer(&fbi->fb);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Failed to register framebuffer device: %d\n", ret);
		goto failed_free_cmap;
	}

	/*
	 * Ok, now enable the LCD controller
	 */
	writel(readl(fbi->regbase) | 1, fbi->regbase);

	return 0;

failed_free_cmap:
	if (fbi->fb.cmap.len)
		fb_dealloc_cmap(&fbi->fb.cmap);
failed_free_irq:
	free_irq(irq, fbi);
failed_free_palette:
	dma_free_coherent(&pdev->dev, fbi->palette_size,
			  fbi->palette_cpu, fbi->palette_phys);
failed_free_io:
	iounmap(fbi->regbase);
failed_free_res:
	release_mem_region(res->start, resource_size(res));
failed_fbi:
	platform_set_drvdata(pdev, NULL);
	kfree(fbi);
failed:
	return ret;
}

static int __devexit vt8500lcd_remove(struct platform_device *pdev)
{
	struct vt8500lcd_info *fbi = platform_get_drvdata(pdev);
	struct resource *res;
	int irq;

	unregister_framebuffer(&fbi->fb);

	writel(0, fbi->regbase);

	if (fbi->fb.cmap.len)
		fb_dealloc_cmap(&fbi->fb.cmap);

	irq = platform_get_irq(pdev, 0);
	free_irq(irq, fbi);

	dma_free_coherent(&pdev->dev, fbi->palette_size,
			  fbi->palette_cpu, fbi->palette_phys);

	iounmap(fbi->regbase);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	kfree(fbi);

	return 0;
}

static struct platform_driver vt8500lcd_driver = {
	.probe		= vt8500lcd_probe,
	.remove		= __devexit_p(vt8500lcd_remove),
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "vt8500-lcd",
	},
};

static int __init vt8500lcd_init(void)
{
	return platform_driver_register(&vt8500lcd_driver);
}

static void __exit vt8500lcd_exit(void)
{
	platform_driver_unregister(&vt8500lcd_driver);
}

module_init(vt8500lcd_init);
module_exit(vt8500lcd_exit);

MODULE_AUTHOR("Alexey Charkov <alchark@gmail.com>");
MODULE_DESCRIPTION("LCD controller driver for VIA VT8500");
MODULE_LICENSE("GPL");
