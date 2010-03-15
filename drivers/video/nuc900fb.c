/*
 *
 * Copyright (c) 2009 Nuvoton technology corporation
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *  Description:
 *    Nuvoton LCD Controller Driver
 *  Author:
 *    Wang Qiang (rurality.linux@gmail.com) 2009/12/11
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/io.h>
#include <linux/pm.h>
#include <linux/device.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/regs-ldm.h>
#include <mach/fb.h>
#include <mach/clkdev.h>

#include "nuc900fb.h"


/*
 *  Initialize the nuc900 video (dual) buffer address
 */
static void nuc900fb_set_lcdaddr(struct fb_info *info)
{
	struct nuc900fb_info *fbi = info->par;
	void __iomem *regs = fbi->io;
	unsigned long vbaddr1, vbaddr2;

	vbaddr1  = info->fix.smem_start;
	vbaddr2  = info->fix.smem_start;
	vbaddr2 += info->fix.line_length * info->var.yres;

	/* set frambuffer start phy addr*/
	writel(vbaddr1, regs + REG_LCM_VA_BADDR0);
	writel(vbaddr2, regs + REG_LCM_VA_BADDR1);

	writel(fbi->regs.lcd_va_fbctrl, regs + REG_LCM_VA_FBCTRL);
	writel(fbi->regs.lcd_va_scale, regs + REG_LCM_VA_SCALE);
}

/*
 *	calculate divider for lcd div
 */
static unsigned int nuc900fb_calc_pixclk(struct nuc900fb_info *fbi,
					 unsigned long pixclk)
{
	unsigned long clk = fbi->clk_rate;
	unsigned long long div;

	/* pixclk is in picseconds. our clock is in Hz*/
	/* div = (clk * pixclk)/10^12 */
	div = (unsigned long long)clk * pixclk;
	div >>= 12;
	do_div(div, 625 * 625UL * 625);

	dev_dbg(fbi->dev, "pixclk %ld, divisor is %lld\n", pixclk, div);

	return div;
}

/*
 *	Check the video params of 'var'.
 */
static int nuc900fb_check_var(struct fb_var_screeninfo *var,
			       struct fb_info *info)
{
	struct nuc900fb_info *fbi = info->par;
	struct nuc900fb_mach_info *mach_info = fbi->dev->platform_data;
	struct nuc900fb_display *display = NULL;
	struct nuc900fb_display *default_display = mach_info->displays +
						   mach_info->default_display;
	int i;

	dev_dbg(fbi->dev, "check_var(var=%p, info=%p)\n", var, info);

	/* validate x/y resolution */
	/* choose default mode if possible */
	if (var->xres == default_display->xres &&
	    var->yres == default_display->yres &&
	    var->bits_per_pixel == default_display->bpp)
		display = default_display;
	else
		for (i = 0; i < mach_info->num_displays; i++)
			if (var->xres == mach_info->displays[i].xres &&
			    var->yres == mach_info->displays[i].yres &&
			    var->bits_per_pixel == mach_info->displays[i].bpp) {
				display = mach_info->displays + i;
				break;
			}

	if (display == NULL) {
		printk(KERN_ERR "wrong resolution or depth %dx%d at %d bit per pixel\n",
			var->xres, var->yres, var->bits_per_pixel);
		return -EINVAL;
	}

	/* it should be the same size as the display */
	var->xres_virtual	= display->xres;
	var->yres_virtual	= display->yres;
	var->height		= display->height;
	var->width		= display->width;

	/* copy lcd settings */
	var->pixclock		= display->pixclock;
	var->left_margin	= display->left_margin;
	var->right_margin	= display->right_margin;
	var->upper_margin	= display->upper_margin;
	var->lower_margin	= display->lower_margin;
	var->vsync_len		= display->vsync_len;
	var->hsync_len		= display->hsync_len;

	var->transp.offset	= 0;
	var->transp.length	= 0;

	fbi->regs.lcd_dccs = display->dccs;
	fbi->regs.lcd_device_ctrl = display->devctl;
	fbi->regs.lcd_va_fbctrl = display->fbctrl;
	fbi->regs.lcd_va_scale = display->scale;

	/* set R/G/B possions */
	switch (var->bits_per_pixel) {
	case 1:
	case 2:
	case 4:
	case 8:
	default:
		var->red.offset 	= 0;
		var->red.length 	= var->bits_per_pixel;
		var->green 		= var->red;
		var->blue		= var->red;
		break;
	case 12:
		var->red.length		= 4;
		var->green.length	= 4;
		var->blue.length	= 4;
		var->red.offset		= 8;
		var->green.offset	= 4;
		var->blue.offset	= 0;
		break;
	case 16:
		var->red.length		= 5;
		var->green.length	= 6;
		var->blue.length	= 5;
		var->red.offset		= 11;
		var->green.offset	= 5;
		var->blue.offset	= 0;
		break;
	case 18:
		var->red.length		= 6;
		var->green.length	= 6;
		var->blue.length	= 6;
		var->red.offset		= 12;
		var->green.offset	= 6;
		var->blue.offset	= 0;
		break;
	case 32:
		var->red.length		= 8;
		var->green.length	= 8;
		var->blue.length	= 8;
		var->red.offset		= 16;
		var->green.offset	= 8;
		var->blue.offset	= 0;
		break;
	}

	return 0;
}

/*
 *	Calculate lcd register values from var setting & save into hw
 */
static void nuc900fb_calculate_lcd_regs(const struct fb_info *info,
					struct nuc900fb_hw *regs)
{
	const struct fb_var_screeninfo *var = &info->var;
	int vtt = var->height + var->upper_margin + var->lower_margin;
	int htt = var->width + var->left_margin + var->right_margin;
	int hsync = var->width + var->right_margin;
	int vsync = var->height + var->lower_margin;

	regs->lcd_crtc_size = LCM_CRTC_SIZE_VTTVAL(vtt) |
			      LCM_CRTC_SIZE_HTTVAL(htt);
	regs->lcd_crtc_dend = LCM_CRTC_DEND_VDENDVAL(var->height) |
			      LCM_CRTC_DEND_HDENDVAL(var->width);
	regs->lcd_crtc_hr = LCM_CRTC_HR_EVAL(var->width + 5) |
			    LCM_CRTC_HR_SVAL(var->width + 1);
	regs->lcd_crtc_hsync = LCM_CRTC_HSYNC_EVAL(hsync + var->hsync_len) |
			       LCM_CRTC_HSYNC_SVAL(hsync);
	regs->lcd_crtc_vr = LCM_CRTC_VR_EVAL(vsync + var->vsync_len) |
			    LCM_CRTC_VR_SVAL(vsync);

}

/*
 *	Activate (set) the controller from the given framebuffer
 *	information
 */
static void nuc900fb_activate_var(struct fb_info *info)
{
	struct nuc900fb_info *fbi = info->par;
	void __iomem *regs = fbi->io;
	struct fb_var_screeninfo *var = &info->var;
	int clkdiv;

	clkdiv = nuc900fb_calc_pixclk(fbi, var->pixclock) - 1;
	if (clkdiv < 0)
		clkdiv = 0;

	nuc900fb_calculate_lcd_regs(info, &fbi->regs);

	/* set the new lcd registers*/

	dev_dbg(fbi->dev, "new lcd register set:\n");
	dev_dbg(fbi->dev, "dccs       = 0x%08x\n", fbi->regs.lcd_dccs);
	dev_dbg(fbi->dev, "dev_ctl    = 0x%08x\n", fbi->regs.lcd_device_ctrl);
	dev_dbg(fbi->dev, "crtc_size  = 0x%08x\n", fbi->regs.lcd_crtc_size);
	dev_dbg(fbi->dev, "crtc_dend  = 0x%08x\n", fbi->regs.lcd_crtc_dend);
	dev_dbg(fbi->dev, "crtc_hr    = 0x%08x\n", fbi->regs.lcd_crtc_hr);
	dev_dbg(fbi->dev, "crtc_hsync = 0x%08x\n", fbi->regs.lcd_crtc_hsync);
	dev_dbg(fbi->dev, "crtc_vr    = 0x%08x\n", fbi->regs.lcd_crtc_vr);

	writel(fbi->regs.lcd_device_ctrl, regs + REG_LCM_DEV_CTRL);
	writel(fbi->regs.lcd_crtc_size, regs + REG_LCM_CRTC_SIZE);
	writel(fbi->regs.lcd_crtc_dend, regs + REG_LCM_CRTC_DEND);
	writel(fbi->regs.lcd_crtc_hr, regs + REG_LCM_CRTC_HR);
	writel(fbi->regs.lcd_crtc_hsync, regs + REG_LCM_CRTC_HSYNC);
	writel(fbi->regs.lcd_crtc_vr, regs + REG_LCM_CRTC_VR);

	/* set lcd address pointers */
	nuc900fb_set_lcdaddr(info);

	writel(fbi->regs.lcd_dccs, regs + REG_LCM_DCCS);
}

/*
 *      Alters the hardware state.
 *
 */
static int nuc900fb_set_par(struct fb_info *info)
{
	struct fb_var_screeninfo *var = &info->var;

	switch (var->bits_per_pixel) {
	case 32:
	case 24:
	case 18:
	case 16:
	case 12:
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	case 1:
		info->fix.visual = FB_VISUAL_MONO01;
		break;
	default:
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	}

	info->fix.line_length = (var->xres_virtual * var->bits_per_pixel) / 8;

	/* activate this new configuration */
	nuc900fb_activate_var(info);
	return 0;
}

static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int nuc900fb_setcolreg(unsigned regno,
			       unsigned red, unsigned green, unsigned blue,
			       unsigned transp, struct fb_info *info)
{
	unsigned int val;

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/* true-colour, use pseuo-palette */
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;

			val  = chan_to_field(red, &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue, &info->var.blue);
			pal[regno] = val;
		}
		break;

	default:
		return 1;   /* unknown type */
	}
	return 0;
}

/**
 *      nuc900fb_blank
 *
 */
static int nuc900fb_blank(int blank_mode, struct fb_info *info)
{

	return 0;
}

static struct fb_ops nuc900fb_ops = {
	.owner			= THIS_MODULE,
	.fb_check_var		= nuc900fb_check_var,
	.fb_set_par		= nuc900fb_set_par,
	.fb_blank		= nuc900fb_blank,
	.fb_setcolreg		= nuc900fb_setcolreg,
	.fb_fillrect		= cfb_fillrect,
	.fb_copyarea		= cfb_copyarea,
	.fb_imageblit		= cfb_imageblit,
};


static inline void modify_gpio(void __iomem *reg,
			       unsigned long set, unsigned long mask)
{
	unsigned long tmp;
	tmp = readl(reg) & ~mask;
	writel(tmp | set, reg);
}

/*
 * Initialise LCD-related registers
 */
static int nuc900fb_init_registers(struct fb_info *info)
{
	struct nuc900fb_info *fbi = info->par;
	struct nuc900fb_mach_info *mach_info = fbi->dev->platform_data;
	void __iomem *regs = fbi->io;

	/*reset the display engine*/
	writel(0, regs + REG_LCM_DCCS);
	writel(readl(regs + REG_LCM_DCCS) | LCM_DCCS_ENG_RST,
	       regs + REG_LCM_DCCS);
	ndelay(100);
	writel(readl(regs + REG_LCM_DCCS) & (~LCM_DCCS_ENG_RST),
	       regs + REG_LCM_DCCS);
	ndelay(100);

	writel(0, regs + REG_LCM_DEV_CTRL);

	/* config gpio output */
	modify_gpio(W90X900_VA_GPIO + 0x54, mach_info->gpio_dir,
		    mach_info->gpio_dir_mask);
	modify_gpio(W90X900_VA_GPIO + 0x58, mach_info->gpio_data,
		    mach_info->gpio_data_mask);

	return 0;
}


/*
 *    Alloc the SDRAM region of NUC900 for the frame buffer.
 *    The buffer should be a non-cached, non-buffered, memory region
 *    to allow palette and pixel writes without flushing the cache.
 */
static int __init nuc900fb_map_video_memory(struct fb_info *info)
{
	struct nuc900fb_info *fbi = info->par;
	dma_addr_t map_dma;
	unsigned long map_size = PAGE_ALIGN(info->fix.smem_len);

	dev_dbg(fbi->dev, "nuc900fb_map_video_memory(fbi=%p) map_size %lu\n",
		fbi, map_size);

	info->screen_base = dma_alloc_writecombine(fbi->dev, map_size,
							&map_dma, GFP_KERNEL);

	if (!info->screen_base)
		return -ENOMEM;

	memset(info->screen_base, 0x00, map_size);
	info->fix.smem_start = map_dma;

	return 0;
}

static inline void nuc900fb_unmap_video_memory(struct fb_info *info)
{
	struct nuc900fb_info *fbi = info->par;
	dma_free_writecombine(fbi->dev, PAGE_ALIGN(info->fix.smem_len),
			      info->screen_base, info->fix.smem_start);
}

static irqreturn_t nuc900fb_irqhandler(int irq, void *dev_id)
{
	struct nuc900fb_info *fbi = dev_id;
	void __iomem *regs = fbi->io;
	void __iomem *irq_base = fbi->irq_base;
	unsigned long lcdirq = readl(regs + REG_LCM_INT_CS);

	if (lcdirq & LCM_INT_CS_DISP_F_STATUS) {
		writel(readl(irq_base) | 1<<30, irq_base);

		/* wait VA_EN low */
		if ((readl(regs + REG_LCM_DCCS) &
		    LCM_DCCS_SINGLE) == LCM_DCCS_SINGLE)
			while ((readl(regs + REG_LCM_DCCS) &
			       LCM_DCCS_VA_EN) == LCM_DCCS_VA_EN)
				;
		/* display_out-enable */
		writel(readl(regs + REG_LCM_DCCS) | LCM_DCCS_DISP_OUT_EN,
			regs + REG_LCM_DCCS);
		/* va-enable*/
		writel(readl(regs + REG_LCM_DCCS) | LCM_DCCS_VA_EN,
			regs + REG_LCM_DCCS);
	} else if (lcdirq & LCM_INT_CS_UNDERRUN_INT) {
		writel(readl(irq_base) | LCM_INT_CS_UNDERRUN_INT, irq_base);
	} else if (lcdirq & LCM_INT_CS_BUS_ERROR_INT) {
		writel(readl(irq_base) | LCM_INT_CS_BUS_ERROR_INT, irq_base);
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_CPU_FREQ

static int nuc900fb_cpufreq_transition(struct notifier_block *nb,
				       unsigned long val, void *data)
{
	struct nuc900fb_info *info;
	struct fb_info *fbinfo;
	long delta_f;
	info = container_of(nb, struct nuc900fb_info, freq_transition);
	fbinfo = platform_get_drvdata(to_platform_device(info->dev));

	delta_f = info->clk_rate - clk_get_rate(info->clk);

	if ((val == CPUFREQ_POSTCHANGE && delta_f > 0) ||
	   (val == CPUFREQ_PRECHANGE && delta_f < 0)) {
		info->clk_rate = clk_get_rate(info->clk);
		nuc900fb_activate_var(fbinfo);
	}

	return 0;
}

static inline int nuc900fb_cpufreq_register(struct nuc900fb_info *fbi)
{
	fbi->freq_transition.notifier_call = nuc900fb_cpufreq_transition;
	return cpufreq_register_notifier(&fbi->freq_transition,
				  CPUFREQ_TRANSITION_NOTIFIER);
}

static inline void nuc900fb_cpufreq_deregister(struct nuc900fb_info *fbi)
{
	cpufreq_unregister_notifier(&fbi->freq_transition,
				    CPUFREQ_TRANSITION_NOTIFIER);
}
#else
static inline int nuc900fb_cpufreq_transition(struct notifier_block *nb,
				       unsigned long val, void *data)
{
	return 0;
}

static inline int nuc900fb_cpufreq_register(struct nuc900fb_info *fbi)
{
	return 0;
}

static inline void nuc900fb_cpufreq_deregister(struct nuc900fb_info *info)
{
}
#endif

static char driver_name[] = "nuc900fb";

static int __devinit nuc900fb_probe(struct platform_device *pdev)
{
	struct nuc900fb_info *fbi;
	struct nuc900fb_display *display;
	struct fb_info	   *fbinfo;
	struct nuc900fb_mach_info *mach_info;
	struct resource *res;
	int ret;
	int irq;
	int i;
	int size;

	dev_dbg(&pdev->dev, "devinit\n");
	mach_info = pdev->dev.platform_data;
	if (mach_info == NULL) {
		dev_err(&pdev->dev,
			"no platform data for lcd, cannot attach\n");
		return -EINVAL;
	}

	if (mach_info->default_display > mach_info->num_displays) {
		dev_err(&pdev->dev,
			"default display No. is %d but only %d displays \n",
			mach_info->default_display, mach_info->num_displays);
		return -EINVAL;
	}


	display = mach_info->displays + mach_info->default_display;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq for device\n");
		return -ENOENT;
	}

	fbinfo = framebuffer_alloc(sizeof(struct nuc900fb_info), &pdev->dev);
	if (!fbinfo)
		return -ENOMEM;

	platform_set_drvdata(pdev, fbinfo);

	fbi = fbinfo->par;
	fbi->dev = &pdev->dev;

#ifdef CONFIG_CPU_NUC950
	fbi->drv_type = LCDDRV_NUC950;
#endif

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	size = (res->end - res->start) + 1;
	fbi->mem = request_mem_region(res->start, size, pdev->name);
	if (fbi->mem == NULL) {
		dev_err(&pdev->dev, "failed to alloc memory region\n");
		ret = -ENOENT;
		goto free_fb;
	}

	fbi->io = ioremap(res->start, size);
	if (fbi->io == NULL) {
		dev_err(&pdev->dev, "ioremap() of lcd registers failed\n");
		ret = -ENXIO;
		goto release_mem_region;
	}

	fbi->irq_base = fbi->io + REG_LCM_INT_CS;


	/* Stop the LCD */
	writel(0, fbi->io + REG_LCM_DCCS);

	/* fill the fbinfo*/
	strcpy(fbinfo->fix.id, driver_name);
	fbinfo->fix.type		= FB_TYPE_PACKED_PIXELS;
	fbinfo->fix.type_aux		= 0;
	fbinfo->fix.xpanstep		= 0;
	fbinfo->fix.ypanstep		= 0;
	fbinfo->fix.ywrapstep		= 0;
	fbinfo->fix.accel		= FB_ACCEL_NONE;
	fbinfo->var.nonstd		= 0;
	fbinfo->var.activate		= FB_ACTIVATE_NOW;
	fbinfo->var.accel_flags		= 0;
	fbinfo->var.vmode		= FB_VMODE_NONINTERLACED;
	fbinfo->fbops			= &nuc900fb_ops;
	fbinfo->flags			= FBINFO_FLAG_DEFAULT;
	fbinfo->pseudo_palette		= &fbi->pseudo_pal;

	ret = request_irq(irq, nuc900fb_irqhandler, IRQF_DISABLED,
			  pdev->name, fbinfo);
	if (ret) {
		dev_err(&pdev->dev, "cannot register irq handler %d -err %d\n",
			irq, ret);
		ret = -EBUSY;
		goto release_regs;
	}

	nuc900_driver_clksrc_div(&pdev->dev, "ext", 0x2);

	fbi->clk = clk_get(&pdev->dev, NULL);
	if (!fbi->clk || IS_ERR(fbi->clk)) {
		printk(KERN_ERR "nuc900-lcd:failed to get lcd clock source\n");
		ret = -ENOENT;
		goto release_irq;
	}

	clk_enable(fbi->clk);
	dev_dbg(&pdev->dev, "got and enabled clock\n");

	fbi->clk_rate = clk_get_rate(fbi->clk);

	/* calutate the video buffer size */
	for (i = 0; i < mach_info->num_displays; i++) {
		unsigned long smem_len = mach_info->displays[i].xres;
		smem_len *= mach_info->displays[i].yres;
		smem_len *= mach_info->displays[i].bpp;
		smem_len >>= 3;
		if (fbinfo->fix.smem_len < smem_len)
			fbinfo->fix.smem_len = smem_len;
	}

	/* Initialize Video Memory */
	ret = nuc900fb_map_video_memory(fbinfo);
	if (ret) {
		printk(KERN_ERR "Failed to allocate video RAM: %x\n", ret);
		goto release_clock;
	}

	dev_dbg(&pdev->dev, "got video memory\n");

	fbinfo->var.xres = display->xres;
	fbinfo->var.yres = display->yres;
	fbinfo->var.bits_per_pixel = display->bpp;

	nuc900fb_init_registers(fbinfo);

	nuc900fb_check_var(&fbinfo->var, fbinfo);

	ret = nuc900fb_cpufreq_register(fbi);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register cpufreq\n");
		goto free_video_memory;
	}

	ret = register_framebuffer(fbinfo);
	if (ret) {
		printk(KERN_ERR "failed to register framebuffer device: %d\n",
			ret);
		goto free_cpufreq;
	}

	printk(KERN_INFO "fb%d: %s frame buffer device\n",
		fbinfo->node, fbinfo->fix.id);

	return 0;

free_cpufreq:
	nuc900fb_cpufreq_deregister(fbi);
free_video_memory:
	nuc900fb_unmap_video_memory(fbinfo);
release_clock:
	clk_disable(fbi->clk);
	clk_put(fbi->clk);
release_irq:
	free_irq(irq, fbi);
release_regs:
	iounmap(fbi->io);
release_mem_region:
	release_mem_region((unsigned long)fbi->mem, size);
free_fb:
	framebuffer_release(fbinfo);
	return ret;
}

/*
 * shutdown the lcd controller
 */
static void nuc900fb_stop_lcd(struct fb_info *info)
{
	struct nuc900fb_info *fbi = info->par;
	void __iomem *regs = fbi->io;

	writel((~LCM_DCCS_DISP_INT_EN) | (~LCM_DCCS_VA_EN) | (~LCM_DCCS_OSD_EN),
		regs + REG_LCM_DCCS);
}

/*
 *  Cleanup
 */
static int nuc900fb_remove(struct platform_device *pdev)
{
	struct fb_info *fbinfo = platform_get_drvdata(pdev);
	struct nuc900fb_info *fbi = fbinfo->par;
	int irq;

	nuc900fb_stop_lcd(fbinfo);
	msleep(1);

	nuc900fb_unmap_video_memory(fbinfo);

	iounmap(fbi->io);

	irq = platform_get_irq(pdev, 0);
	free_irq(irq, fbi);

	release_resource(fbi->mem);
	kfree(fbi->mem);

	platform_set_drvdata(pdev, NULL);
	framebuffer_release(fbinfo);

	return 0;
}

#ifdef CONFIG_PM

/*
 *	suspend and resume support for the lcd controller
 */

static int nuc900fb_suspend(struct platform_device *dev, pm_message_t state)
{
	struct fb_info	   *fbinfo = platform_get_drvdata(dev);
	struct nuc900fb_info *info = fbinfo->par;

	nuc900fb_stop_lcd();
	msleep(1);
	clk_disable(info->clk);
	return 0;
}

static int nuc900fb_resume(struct platform_device *dev)
{
	struct fb_info	   *fbinfo = platform_get_drvdata(dev);
	struct nuc900fb_info *fbi = fbinfo->par;

	printk(KERN_INFO "nuc900fb resume\n");

	clk_enable(fbi->clk);
	msleep(1);

	nuc900fb_init_registers(fbinfo);
	nuc900fb_activate_var(bfinfo);

	return 0;
}

#else
#define nuc900fb_suspend NULL
#define nuc900fb_resume  NULL
#endif

static struct platform_driver nuc900fb_driver = {
	.probe		= nuc900fb_probe,
	.remove		= nuc900fb_remove,
	.suspend	= nuc900fb_suspend,
	.resume		= nuc900fb_resume,
	.driver		= {
		.name	= "nuc900-lcd",
		.owner	= THIS_MODULE,
	},
};

int __devinit nuc900fb_init(void)
{
	return platform_driver_register(&nuc900fb_driver);
}

static void __exit nuc900fb_cleanup(void)
{
	platform_driver_unregister(&nuc900fb_driver);
}

module_init(nuc900fb_init);
module_exit(nuc900fb_cleanup);

MODULE_DESCRIPTION("Framebuffer driver for the NUC900");
MODULE_LICENSE("GPL");
