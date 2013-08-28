/*
 * File:         drivers/video/bf54x-lq043.c
 * Based on:
 * Author:       Michael Hennerich <hennerich@blackfin.uclinux.org>
 *
 * Created:
 * Description:  ADSP-BF54x Framebuffer driver
 *
 *
 * Modified:
 *               Copyright 2007-2008 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
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
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/device.h>
#include <linux/backlight.h>
#include <linux/lcd.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include <asm/blackfin.h>
#include <asm/irq.h>
#include <asm/dpmc.h>
#include <asm/dma-mapping.h>
#include <asm/dma.h>
#include <asm/gpio.h>
#include <asm/portmux.h>

#include <mach/bf54x-lq043.h>

#define NO_BL_SUPPORT

#define DRIVER_NAME "bf54x-lq043"
static char driver_name[] = DRIVER_NAME;

#define BFIN_LCD_NBR_PALETTE_ENTRIES	256

#define EPPI0_18 {P_PPI0_CLK, P_PPI0_FS1, P_PPI0_FS2, P_PPI0_D0, P_PPI0_D1, P_PPI0_D2, P_PPI0_D3, \
 P_PPI0_D4, P_PPI0_D5, P_PPI0_D6, P_PPI0_D7, P_PPI0_D8, P_PPI0_D9, P_PPI0_D10, \
 P_PPI0_D11, P_PPI0_D12, P_PPI0_D13, P_PPI0_D14, P_PPI0_D15, P_PPI0_D16, P_PPI0_D17, 0}

#define EPPI0_24 {P_PPI0_D18, P_PPI0_D19, P_PPI0_D20, P_PPI0_D21, P_PPI0_D22, P_PPI0_D23, 0}

struct bfin_bf54xfb_info {
	struct fb_info *fb;
	struct device *dev;

	struct bfin_bf54xfb_mach_info *mach_info;

	unsigned char *fb_buffer;	/* RGB Buffer */

	dma_addr_t dma_handle;
	int lq043_open_cnt;
	int irq;
	spinlock_t lock;	/* lock */
};

static int nocursor;
module_param(nocursor, int, 0644);
MODULE_PARM_DESC(nocursor, "cursor enable/disable");

static int outp_rgb666;
module_param(outp_rgb666, int, 0);
MODULE_PARM_DESC(outp_rgb666, "Output 18-bit RGB666");

#define LCD_X_RES		480	/*Horizontal Resolution */
#define LCD_Y_RES		272	/* Vertical Resolution */

#define LCD_BPP			24	/* Bit Per Pixel */
#define	DMA_BUS_SIZE		32

/* 	-- Horizontal synchronizing --
 *
 * Timing characteristics taken from the SHARP LQ043T1DG01 datasheet
 * (LCY-W-06602A Page 9 of 22)
 *
 * Clock Frequency 	1/Tc Min 7.83 Typ 9.00 Max 9.26 MHz
 *
 * Period 		TH - 525 - Clock
 * Pulse width 		THp - 41 - Clock
 * Horizontal period 	THd - 480 - Clock
 * Back porch 		THb - 2 - Clock
 * Front porch 		THf - 2 - Clock
 *
 * -- Vertical synchronizing --
 * Period 		TV - 286 - Line
 * Pulse width 		TVp - 10 - Line
 * Vertical period 	TVd - 272 - Line
 * Back porch 		TVb - 2 - Line
 * Front porch 		TVf - 2 - Line
 */

#define	LCD_CLK         	(8*1000*1000)	/* 8MHz */

/* # active data to transfer after Horizontal Delay clock */
#define EPPI_HCOUNT		LCD_X_RES

/* # active lines to transfer after Vertical Delay clock */
#define EPPI_VCOUNT		LCD_Y_RES

/* Samples per Line = 480 (active data) + 45 (padding) */
#define EPPI_LINE		525

/* Lines per Frame = 272 (active data) + 14 (padding) */
#define EPPI_FRAME		286

/* FS1 (Hsync) Width (Typical)*/
#define EPPI_FS1W_HBL		41

/* FS1 (Hsync) Period (Typical) */
#define EPPI_FS1P_AVPL		EPPI_LINE

/* Horizontal Delay clock after assertion of Hsync (Typical) */
#define EPPI_HDELAY		43

/* FS2 (Vsync) Width    = FS1 (Hsync) Period * 10 */
#define EPPI_FS2W_LVB		(EPPI_LINE * 10)

 /* FS2 (Vsync) Period   = FS1 (Hsync) Period * Lines per Frame */
#define EPPI_FS2P_LAVF		(EPPI_LINE * EPPI_FRAME)

/* Vertical Delay after assertion of Vsync (2 Lines) */
#define EPPI_VDELAY		12

#define EPPI_CLIP		0xFF00FF00

/* EPPI Control register configuration value for RGB out
 * - EPPI as Output
 * GP 2 frame sync mode,
 * Internal Clock generation disabled, Internal FS generation enabled,
 * Receives samples on EPPI_CLK raising edge, Transmits samples on EPPI_CLK falling edge,
 * FS1 & FS2 are active high,
 * DLEN = 6 (24 bits for RGB888 out) or 5 (18 bits for RGB666 out)
 * DMA Unpacking disabled when RGB Formating is enabled, otherwise DMA unpacking enabled
 * Swapping Enabled,
 * One (DMA) Channel Mode,
 * RGB Formatting Enabled for RGB666 output, disabled for RGB888 output
 * Regular watermark - when FIFO is 100% full,
 * Urgent watermark - when FIFO is 75% full
 */

#define EPPI_CONTROL		(0x20136E2E | SWAPEN)

static inline u16 get_eppi_clkdiv(u32 target_ppi_clk)
{
	u32 sclk = get_sclk();

	/* EPPI_CLK = (SCLK) / (2 * (EPPI_CLKDIV[15:0] + 1)) */

	return (((sclk / target_ppi_clk) / 2) - 1);
}

static void config_ppi(struct bfin_bf54xfb_info *fbi)
{

	u16 eppi_clkdiv = get_eppi_clkdiv(LCD_CLK);

	bfin_write_EPPI0_FS1W_HBL(EPPI_FS1W_HBL);
	bfin_write_EPPI0_FS1P_AVPL(EPPI_FS1P_AVPL);
	bfin_write_EPPI0_FS2W_LVB(EPPI_FS2W_LVB);
	bfin_write_EPPI0_FS2P_LAVF(EPPI_FS2P_LAVF);
	bfin_write_EPPI0_CLIP(EPPI_CLIP);

	bfin_write_EPPI0_FRAME(EPPI_FRAME);
	bfin_write_EPPI0_LINE(EPPI_LINE);

	bfin_write_EPPI0_HCOUNT(EPPI_HCOUNT);
	bfin_write_EPPI0_HDELAY(EPPI_HDELAY);
	bfin_write_EPPI0_VCOUNT(EPPI_VCOUNT);
	bfin_write_EPPI0_VDELAY(EPPI_VDELAY);

	bfin_write_EPPI0_CLKDIV(eppi_clkdiv);

/*
 * DLEN = 6 (24 bits for RGB888 out) or 5 (18 bits for RGB666 out)
 * RGB Formatting Enabled for RGB666 output, disabled for RGB888 output
 */
	if (outp_rgb666)
		bfin_write_EPPI0_CONTROL((EPPI_CONTROL & ~DLENGTH) | DLEN_18 |
					 RGB_FMT_EN);
	else
		bfin_write_EPPI0_CONTROL(((EPPI_CONTROL & ~DLENGTH) | DLEN_24) &
					 ~RGB_FMT_EN);


}

static int config_dma(struct bfin_bf54xfb_info *fbi)
{

	set_dma_config(CH_EPPI0,
		       set_bfin_dma_config(DIR_READ, DMA_FLOW_AUTO,
					   INTR_DISABLE, DIMENSION_2D,
					   DATA_SIZE_32,
					   DMA_NOSYNC_KEEP_DMA_BUF));
	set_dma_x_count(CH_EPPI0, (LCD_X_RES * LCD_BPP) / DMA_BUS_SIZE);
	set_dma_x_modify(CH_EPPI0, DMA_BUS_SIZE / 8);
	set_dma_y_count(CH_EPPI0, LCD_Y_RES);
	set_dma_y_modify(CH_EPPI0, DMA_BUS_SIZE / 8);
	set_dma_start_addr(CH_EPPI0, (unsigned long)fbi->fb_buffer);

	return 0;
}

static int request_ports(struct bfin_bf54xfb_info *fbi)
{

	u16 eppi_req_18[] = EPPI0_18;
	u16 disp = fbi->mach_info->disp;

	if (gpio_request_one(disp, GPIOF_OUT_INIT_HIGH, DRIVER_NAME)) {
		printk(KERN_ERR "Requesting GPIO %d failed\n", disp);
		return -EFAULT;
	}

	if (peripheral_request_list(eppi_req_18, DRIVER_NAME)) {
		printk(KERN_ERR "Requesting Peripherals failed\n");
		gpio_free(disp);
		return -EFAULT;
	}

	if (!outp_rgb666) {

		u16 eppi_req_24[] = EPPI0_24;

		if (peripheral_request_list(eppi_req_24, DRIVER_NAME)) {
			printk(KERN_ERR "Requesting Peripherals failed\n");
			peripheral_free_list(eppi_req_18);
			gpio_free(disp);
			return -EFAULT;
		}
	}

	return 0;
}

static void free_ports(struct bfin_bf54xfb_info *fbi)
{

	u16 eppi_req_18[] = EPPI0_18;

	gpio_free(fbi->mach_info->disp);

	peripheral_free_list(eppi_req_18);

	if (!outp_rgb666) {
		u16 eppi_req_24[] = EPPI0_24;
		peripheral_free_list(eppi_req_24);
	}
}

static int bfin_bf54x_fb_open(struct fb_info *info, int user)
{
	struct bfin_bf54xfb_info *fbi = info->par;

	spin_lock(&fbi->lock);
	fbi->lq043_open_cnt++;

	if (fbi->lq043_open_cnt <= 1) {

		bfin_write_EPPI0_CONTROL(0);
		SSYNC();

		config_dma(fbi);
		config_ppi(fbi);

		/* start dma */
		enable_dma(CH_EPPI0);
		bfin_write_EPPI0_CONTROL(bfin_read_EPPI0_CONTROL() | EPPI_EN);
	}

	spin_unlock(&fbi->lock);

	return 0;
}

static int bfin_bf54x_fb_release(struct fb_info *info, int user)
{
	struct bfin_bf54xfb_info *fbi = info->par;

	spin_lock(&fbi->lock);

	fbi->lq043_open_cnt--;

	if (fbi->lq043_open_cnt <= 0) {

		bfin_write_EPPI0_CONTROL(0);
		SSYNC();
		disable_dma(CH_EPPI0);
	}

	spin_unlock(&fbi->lock);

	return 0;
}

static int bfin_bf54x_fb_check_var(struct fb_var_screeninfo *var,
				   struct fb_info *info)
{

	switch (var->bits_per_pixel) {
	case 24:/* TRUECOLOUR, 16m */
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = var->green.length = var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		var->transp.msb_right = 0;
		var->red.msb_right = 0;
		var->green.msb_right = 0;
		var->blue.msb_right = 0;
		break;
	default:
		pr_debug("%s: depth not supported: %u BPP\n", __func__,
			 var->bits_per_pixel);
		return -EINVAL;
	}

	if (info->var.xres != var->xres || info->var.yres != var->yres ||
	    info->var.xres_virtual != var->xres_virtual ||
	    info->var.yres_virtual != var->yres_virtual) {
		pr_debug("%s: Resolution not supported: X%u x Y%u \n",
			 __func__, var->xres, var->yres);
		return -EINVAL;
	}

	/*
	 *  Memory limit
	 */

	if ((info->fix.line_length * var->yres_virtual) > info->fix.smem_len) {
		pr_debug("%s: Memory Limit requested yres_virtual = %u\n",
			 __func__, var->yres_virtual);
		return -ENOMEM;
	}

	return 0;
}

int bfin_bf54x_fb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	if (nocursor)
		return 0;
	else
		return -EINVAL;	/* just to force soft_cursor() call */
}

static int bfin_bf54x_fb_setcolreg(u_int regno, u_int red, u_int green,
				   u_int blue, u_int transp,
				   struct fb_info *info)
{
	if (regno >= BFIN_LCD_NBR_PALETTE_ENTRIES)
		return -EINVAL;

	if (info->var.grayscale) {
		/* grayscale = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}

	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {

		u32 value;
		/* Place color in the pseudopalette */
		if (regno > 16)
			return -EINVAL;

		red >>= (16 - info->var.red.length);
		green >>= (16 - info->var.green.length);
		blue >>= (16 - info->var.blue.length);

		value = (red << info->var.red.offset) |
		    (green << info->var.green.offset) |
		    (blue << info->var.blue.offset);
		value &= 0xFFFFFF;

		((u32 *) (info->pseudo_palette))[regno] = value;

	}

	return 0;
}

static struct fb_ops bfin_bf54x_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = bfin_bf54x_fb_open,
	.fb_release = bfin_bf54x_fb_release,
	.fb_check_var = bfin_bf54x_fb_check_var,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_cursor = bfin_bf54x_fb_cursor,
	.fb_setcolreg = bfin_bf54x_fb_setcolreg,
};

#ifndef NO_BL_SUPPORT
static int bl_get_brightness(struct backlight_device *bd)
{
	return 0;
}

static const struct backlight_ops bfin_lq043fb_bl_ops = {
	.get_brightness = bl_get_brightness,
};

static struct backlight_device *bl_dev;

static int bfin_lcd_get_power(struct lcd_device *dev)
{
	return 0;
}

static int bfin_lcd_set_power(struct lcd_device *dev, int power)
{
	return 0;
}

static int bfin_lcd_get_contrast(struct lcd_device *dev)
{
	return 0;
}

static int bfin_lcd_set_contrast(struct lcd_device *dev, int contrast)
{

	return 0;
}

static int bfin_lcd_check_fb(struct lcd_device *dev, struct fb_info *fi)
{
	if (!fi || (fi == &bfin_bf54x_fb))
		return 1;
	return 0;
}

static struct lcd_ops bfin_lcd_ops = {
	.get_power = bfin_lcd_get_power,
	.set_power = bfin_lcd_set_power,
	.get_contrast = bfin_lcd_get_contrast,
	.set_contrast = bfin_lcd_set_contrast,
	.check_fb = bfin_lcd_check_fb,
};

static struct lcd_device *lcd_dev;
#endif

static irqreturn_t bfin_bf54x_irq_error(int irq, void *dev_id)
{
	/*struct bfin_bf54xfb_info *info = dev_id;*/

	u16 status = bfin_read_EPPI0_STATUS();

	bfin_write_EPPI0_STATUS(0xFFFF);

	if (status) {
		bfin_write_EPPI0_CONTROL(bfin_read_EPPI0_CONTROL() & ~EPPI_EN);
		disable_dma(CH_EPPI0);

		/* start dma */
		enable_dma(CH_EPPI0);
		bfin_write_EPPI0_CONTROL(bfin_read_EPPI0_CONTROL() | EPPI_EN);
		bfin_write_EPPI0_STATUS(0xFFFF);
	}

	return IRQ_HANDLED;
}

static int bfin_bf54x_probe(struct platform_device *pdev)
{
#ifndef NO_BL_SUPPORT
	struct backlight_properties props;
#endif
	struct bfin_bf54xfb_info *info;
	struct fb_info *fbinfo;
	int ret;

	printk(KERN_INFO DRIVER_NAME ": FrameBuffer initializing...\n");

	if (request_dma(CH_EPPI0, "CH_EPPI0") < 0) {
		printk(KERN_ERR DRIVER_NAME
		       ": couldn't request CH_EPPI0 DMA\n");
		ret = -EFAULT;
		goto out1;
	}

	fbinfo =
	    framebuffer_alloc(sizeof(struct bfin_bf54xfb_info), &pdev->dev);
	if (!fbinfo) {
		ret = -ENOMEM;
		goto out2;
	}

	info = fbinfo->par;
	info->fb = fbinfo;
	info->dev = &pdev->dev;
	spin_lock_init(&info->lock);

	platform_set_drvdata(pdev, fbinfo);

	strcpy(fbinfo->fix.id, driver_name);

	info->mach_info = pdev->dev.platform_data;

	if (info->mach_info == NULL) {
		dev_err(&pdev->dev,
			"no platform data for lcd, cannot attach\n");
		ret = -EINVAL;
		goto out3;
	}

	fbinfo->fix.type = FB_TYPE_PACKED_PIXELS;
	fbinfo->fix.type_aux = 0;
	fbinfo->fix.xpanstep = 0;
	fbinfo->fix.ypanstep = 0;
	fbinfo->fix.ywrapstep = 0;
	fbinfo->fix.accel = FB_ACCEL_NONE;
	fbinfo->fix.visual = FB_VISUAL_TRUECOLOR;

	fbinfo->var.nonstd = 0;
	fbinfo->var.activate = FB_ACTIVATE_NOW;
	fbinfo->var.height = info->mach_info->height;
	fbinfo->var.width = info->mach_info->width;
	fbinfo->var.accel_flags = 0;
	fbinfo->var.vmode = FB_VMODE_NONINTERLACED;

	fbinfo->fbops = &bfin_bf54x_fb_ops;
	fbinfo->flags = FBINFO_FLAG_DEFAULT;

	fbinfo->var.xres = info->mach_info->xres.defval;
	fbinfo->var.xres_virtual = info->mach_info->xres.defval;
	fbinfo->var.yres = info->mach_info->yres.defval;
	fbinfo->var.yres_virtual = info->mach_info->yres.defval;
	fbinfo->var.bits_per_pixel = info->mach_info->bpp.defval;

	fbinfo->var.upper_margin = 0;
	fbinfo->var.lower_margin = 0;
	fbinfo->var.vsync_len = 0;

	fbinfo->var.left_margin = 0;
	fbinfo->var.right_margin = 0;
	fbinfo->var.hsync_len = 0;

	fbinfo->var.red.offset = 16;
	fbinfo->var.green.offset = 8;
	fbinfo->var.blue.offset = 0;
	fbinfo->var.transp.offset = 0;
	fbinfo->var.red.length = 8;
	fbinfo->var.green.length = 8;
	fbinfo->var.blue.length = 8;
	fbinfo->var.transp.length = 0;
	fbinfo->fix.smem_len = info->mach_info->xres.max *
	    info->mach_info->yres.max * info->mach_info->bpp.max / 8;

	fbinfo->fix.line_length = fbinfo->var.xres_virtual *
	    fbinfo->var.bits_per_pixel / 8;

	info->fb_buffer =
	    dma_alloc_coherent(NULL, fbinfo->fix.smem_len, &info->dma_handle,
			       GFP_KERNEL);

	if (NULL == info->fb_buffer) {
		printk(KERN_ERR DRIVER_NAME
		       ": couldn't allocate dma buffer.\n");
		ret = -ENOMEM;
		goto out3;
	}

	fbinfo->screen_base = (void *)info->fb_buffer;
	fbinfo->fix.smem_start = (int)info->fb_buffer;

	fbinfo->fbops = &bfin_bf54x_fb_ops;

	fbinfo->pseudo_palette = devm_kzalloc(&pdev->dev, sizeof(u32) * 16,
					      GFP_KERNEL);
	if (!fbinfo->pseudo_palette) {
		printk(KERN_ERR DRIVER_NAME
		       "Fail to allocate pseudo_palette\n");

		ret = -ENOMEM;
		goto out4;
	}

	if (fb_alloc_cmap(&fbinfo->cmap, BFIN_LCD_NBR_PALETTE_ENTRIES, 0)
	    < 0) {
		printk(KERN_ERR DRIVER_NAME
		       "Fail to allocate colormap (%d entries)\n",
		       BFIN_LCD_NBR_PALETTE_ENTRIES);
		ret = -EFAULT;
		goto out4;
	}

	if (request_ports(info)) {
		printk(KERN_ERR DRIVER_NAME ": couldn't request gpio port.\n");
		ret = -EFAULT;
		goto out6;
	}

	info->irq = platform_get_irq(pdev, 0);
	if (info->irq < 0) {
		ret = -EINVAL;
		goto out7;
	}

	if (request_irq(info->irq, bfin_bf54x_irq_error, 0,
			"PPI ERROR", info) < 0) {
		printk(KERN_ERR DRIVER_NAME
		       ": unable to request PPI ERROR IRQ\n");
		ret = -EFAULT;
		goto out7;
	}

	if (register_framebuffer(fbinfo) < 0) {
		printk(KERN_ERR DRIVER_NAME
		       ": unable to register framebuffer.\n");
		ret = -EINVAL;
		goto out8;
	}
#ifndef NO_BL_SUPPORT
	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = 255;
	bl_dev = backlight_device_register("bf54x-bl", NULL, NULL,
					   &bfin_lq043fb_bl_ops, &props);
	if (IS_ERR(bl_dev)) {
		printk(KERN_ERR DRIVER_NAME
			": unable to register backlight.\n");
		ret = -EINVAL;
		unregister_framebuffer(fbinfo);
		goto out8;
	}

	lcd_dev = lcd_device_register(DRIVER_NAME, &pdev->dev, NULL, &bfin_lcd_ops);
	lcd_dev->props.max_contrast = 255, printk(KERN_INFO "Done.\n");
#endif

	return 0;

out8:
	free_irq(info->irq, info);
out7:
	free_ports(info);
out6:
	fb_dealloc_cmap(&fbinfo->cmap);
out4:
	dma_free_coherent(NULL, fbinfo->fix.smem_len, info->fb_buffer,
			  info->dma_handle);
out3:
	framebuffer_release(fbinfo);
out2:
	free_dma(CH_EPPI0);
out1:

	return ret;
}

static int bfin_bf54x_remove(struct platform_device *pdev)
{

	struct fb_info *fbinfo = platform_get_drvdata(pdev);
	struct bfin_bf54xfb_info *info = fbinfo->par;

	free_dma(CH_EPPI0);
	free_irq(info->irq, info);

	if (info->fb_buffer != NULL)
		dma_free_coherent(NULL, fbinfo->fix.smem_len, info->fb_buffer,
				  info->dma_handle);

	fb_dealloc_cmap(&fbinfo->cmap);

#ifndef NO_BL_SUPPORT
	lcd_device_unregister(lcd_dev);
	backlight_device_unregister(bl_dev);
#endif

	unregister_framebuffer(fbinfo);

	free_ports(info);

	printk(KERN_INFO DRIVER_NAME ": Unregister LCD driver.\n");

	return 0;
}

#ifdef CONFIG_PM
static int bfin_bf54x_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct fb_info *fbinfo = platform_get_drvdata(pdev);

	bfin_write_EPPI0_CONTROL(bfin_read_EPPI0_CONTROL() & ~EPPI_EN);
	disable_dma(CH_EPPI0);
	bfin_write_EPPI0_STATUS(0xFFFF);

	return 0;
}

static int bfin_bf54x_resume(struct platform_device *pdev)
{
	struct fb_info *fbinfo = platform_get_drvdata(pdev);
	struct bfin_bf54xfb_info *info = fbinfo->par;

	if (info->lq043_open_cnt) {

		bfin_write_EPPI0_CONTROL(0);
		SSYNC();

		config_dma(info);
		config_ppi(info);

		/* start dma */
		enable_dma(CH_EPPI0);
		bfin_write_EPPI0_CONTROL(bfin_read_EPPI0_CONTROL() | EPPI_EN);
	}

	return 0;
}
#else
#define bfin_bf54x_suspend	NULL
#define bfin_bf54x_resume	NULL
#endif

static struct platform_driver bfin_bf54x_driver = {
	.probe = bfin_bf54x_probe,
	.remove = bfin_bf54x_remove,
	.suspend = bfin_bf54x_suspend,
	.resume = bfin_bf54x_resume,
	.driver = {
		   .name = DRIVER_NAME,
		   .owner = THIS_MODULE,
		   },
};

static int __init bfin_bf54x_driver_init(void)
{
	return platform_driver_register(&bfin_bf54x_driver);
}

static void __exit bfin_bf54x_driver_cleanup(void)
{
	platform_driver_unregister(&bfin_bf54x_driver);
}

MODULE_DESCRIPTION("Blackfin BF54x TFT LCD Driver");
MODULE_LICENSE("GPL");

module_init(bfin_bf54x_driver_init);
module_exit(bfin_bf54x_driver_cleanup);
