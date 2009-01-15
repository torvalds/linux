/*
 * File:         drivers/video/bfin-t350mcqb-fb.c
 * Based on:
 * Author:       Michael Hennerich <hennerich@blackfin.uclinux.org>
 *
 * Created:
 * Description:  Blackfin LCD Framebufer driver
 *
 *
 * Modified:
 *               Copyright 2004-2007 Analog Devices Inc.
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
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/backlight.h>
#include <linux/lcd.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include <asm/blackfin.h>
#include <asm/irq.h>
#include <asm/dma-mapping.h>
#include <asm/dma.h>
#include <asm/portmux.h>
#include <asm/gptimers.h>

#define NO_BL_SUPPORT

#define LCD_X_RES		320	/* Horizontal Resolution */
#define LCD_Y_RES		240	/* Vertical Resolution */
#define LCD_BPP			24	/* Bit Per Pixel */

#define	DMA_BUS_SIZE		16
#define	LCD_CLK         	(12*1000*1000)	/* 12MHz */

#define CLOCKS_PER_PIX		3

	/*
	 * HS and VS timing parameters (all in number of PPI clk ticks)
	 */

#define U_LINE		1				/* Blanking Lines */

#define H_ACTPIX	(LCD_X_RES * CLOCKS_PER_PIX)	/* active horizontal pixel */
#define H_PERIOD	(408 * CLOCKS_PER_PIX)		/* HS period */
#define H_PULSE		90				/* HS pulse width */
#define H_START		204				/* first valid pixel */

#define	V_LINES		(LCD_Y_RES + U_LINE)		/* total vertical lines */
#define V_PULSE		(3 * H_PERIOD)			/* VS pulse width (1-5 H_PERIODs) */
#define V_PERIOD	(H_PERIOD * V_LINES)		/* VS period */

#define ACTIVE_VIDEO_MEM_OFFSET	(U_LINE * H_ACTPIX)

#define BFIN_LCD_NBR_PALETTE_ENTRIES	256

#define DRIVER_NAME "bfin-t350mcqb"
static char driver_name[] = DRIVER_NAME;

struct bfin_t350mcqbfb_info {
	struct fb_info *fb;
	struct device *dev;
	unsigned char *fb_buffer;	/* RGB Buffer */
	dma_addr_t dma_handle;
	int lq043_mmap;
	int lq043_open_cnt;
	int irq;
	spinlock_t lock;	/* lock */
	u32 pseudo_pal[16];
};

static int nocursor;
module_param(nocursor, int, 0644);
MODULE_PARM_DESC(nocursor, "cursor enable/disable");

#define PPI_TX_MODE		0x2
#define PPI_XFER_TYPE_11	0xC
#define PPI_PORT_CFG_01		0x10
#define PPI_PACK_EN		0x80
#define PPI_POLS_1		0x8000

static void bfin_t350mcqb_config_ppi(struct bfin_t350mcqbfb_info *fbi)
{
	bfin_write_PPI_DELAY(H_START);
	bfin_write_PPI_COUNT(H_ACTPIX-1);
	bfin_write_PPI_FRAME(V_LINES);

	bfin_write_PPI_CONTROL(PPI_TX_MODE |	   /* output mode , PORT_DIR */
				PPI_XFER_TYPE_11 | /* sync mode XFR_TYPE */
				PPI_PORT_CFG_01 |  /* two frame sync PORT_CFG */
				PPI_PACK_EN |	   /* packing enabled PACK_EN */
				PPI_POLS_1);	   /* faling edge syncs POLS */
}

static inline void bfin_t350mcqb_disable_ppi(void)
{
	bfin_write_PPI_CONTROL(bfin_read_PPI_CONTROL() & ~PORT_EN);
}

static inline void bfin_t350mcqb_enable_ppi(void)
{
	bfin_write_PPI_CONTROL(bfin_read_PPI_CONTROL() | PORT_EN);
}

static void bfin_t350mcqb_start_timers(void)
{
	unsigned long flags;

	local_irq_save(flags);
		enable_gptimers(TIMER1bit);
		enable_gptimers(TIMER0bit);
	local_irq_restore(flags);
}

static void bfin_t350mcqb_stop_timers(void)
{
	disable_gptimers(TIMER0bit | TIMER1bit);

	set_gptimer_status(0, TIMER_STATUS_TRUN0 | TIMER_STATUS_TRUN1 |
				TIMER_STATUS_TIMIL0 | TIMER_STATUS_TIMIL1 |
				 TIMER_STATUS_TOVF0 | TIMER_STATUS_TOVF1);

}

static void bfin_t350mcqb_init_timers(void)
{

	bfin_t350mcqb_stop_timers();

	set_gptimer_period(TIMER0_id, H_PERIOD);
	set_gptimer_pwidth(TIMER0_id, H_PULSE);
	set_gptimer_config(TIMER0_id, TIMER_MODE_PWM | TIMER_PERIOD_CNT |
				      TIMER_TIN_SEL | TIMER_CLK_SEL|
				      TIMER_EMU_RUN);

	set_gptimer_period(TIMER1_id, V_PERIOD);
	set_gptimer_pwidth(TIMER1_id, V_PULSE);
	set_gptimer_config(TIMER1_id, TIMER_MODE_PWM | TIMER_PERIOD_CNT |
				      TIMER_TIN_SEL | TIMER_CLK_SEL |
				      TIMER_EMU_RUN);

}

static void bfin_t350mcqb_config_dma(struct bfin_t350mcqbfb_info *fbi)
{

	set_dma_config(CH_PPI,
		       set_bfin_dma_config(DIR_READ, DMA_FLOW_AUTO,
					   INTR_DISABLE, DIMENSION_2D,
					   DATA_SIZE_16,
					   DMA_NOSYNC_KEEP_DMA_BUF));
	set_dma_x_count(CH_PPI, (LCD_X_RES * LCD_BPP) / DMA_BUS_SIZE);
	set_dma_x_modify(CH_PPI, DMA_BUS_SIZE / 8);
	set_dma_y_count(CH_PPI, V_LINES);

	set_dma_y_modify(CH_PPI, DMA_BUS_SIZE / 8);
	set_dma_start_addr(CH_PPI, (unsigned long)fbi->fb_buffer);

}

static	u16 ppi0_req_8[] = {P_PPI0_CLK, P_PPI0_FS1, P_PPI0_FS2,
			    P_PPI0_D0, P_PPI0_D1, P_PPI0_D2,
			    P_PPI0_D3, P_PPI0_D4, P_PPI0_D5,
			    P_PPI0_D6, P_PPI0_D7, 0};

static int bfin_t350mcqb_request_ports(int action)
{
	if (action) {
		if (peripheral_request_list(ppi0_req_8, DRIVER_NAME)) {
			printk(KERN_ERR "Requesting Peripherals faild\n");
			return -EFAULT;
		}
	} else
		peripheral_free_list(ppi0_req_8);

	return 0;
}

static int bfin_t350mcqb_fb_open(struct fb_info *info, int user)
{
	struct bfin_t350mcqbfb_info *fbi = info->par;

	spin_lock(&fbi->lock);
	fbi->lq043_open_cnt++;

	if (fbi->lq043_open_cnt <= 1) {

		bfin_t350mcqb_disable_ppi();
		SSYNC();

		bfin_t350mcqb_config_dma(fbi);
		bfin_t350mcqb_config_ppi(fbi);
		bfin_t350mcqb_init_timers();

		/* start dma */
		enable_dma(CH_PPI);
		bfin_t350mcqb_enable_ppi();
		bfin_t350mcqb_start_timers();
	}

	spin_unlock(&fbi->lock);

	return 0;
}

static int bfin_t350mcqb_fb_release(struct fb_info *info, int user)
{
	struct bfin_t350mcqbfb_info *fbi = info->par;

	spin_lock(&fbi->lock);

	fbi->lq043_open_cnt--;
	fbi->lq043_mmap = 0;

	if (fbi->lq043_open_cnt <= 0) {
		bfin_t350mcqb_disable_ppi();
		SSYNC();
		disable_dma(CH_PPI);
		bfin_t350mcqb_stop_timers();
		memset(fbi->fb_buffer, 0, info->fix.smem_len);
	}

	spin_unlock(&fbi->lock);

	return 0;
}

static int bfin_t350mcqb_fb_check_var(struct fb_var_screeninfo *var,
				   struct fb_info *info)
{

	if (var->bits_per_pixel != LCD_BPP) {
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

static int bfin_t350mcqb_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct bfin_t350mcqbfb_info *fbi = info->par;

	if (fbi->lq043_mmap)
		return -1;

	spin_lock(&fbi->lock);
	fbi->lq043_mmap = 1;
	spin_unlock(&fbi->lock);

	vma->vm_start = (unsigned long)(fbi->fb_buffer + ACTIVE_VIDEO_MEM_OFFSET);

	vma->vm_end = vma->vm_start + info->fix.smem_len;
	/* For those who don't understand how mmap works, go read
	 *   Documentation/nommu-mmap.txt.
	 * For those that do, you will know that the VM_MAYSHARE flag
	 * must be set in the vma->vm_flags structure on noMMU
	 *   Other flags can be set, and are documented in
	 *   include/linux/mm.h
	 */
	vma->vm_flags |= VM_MAYSHARE | VM_SHARED;

	return 0;
}

int bfin_t350mcqb_fb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	if (nocursor)
		return 0;
	else
		return -EINVAL;	/* just to force soft_cursor() call */
}

static int bfin_t350mcqb_fb_setcolreg(u_int regno, u_int red, u_int green,
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

static struct fb_ops bfin_t350mcqb_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = bfin_t350mcqb_fb_open,
	.fb_release = bfin_t350mcqb_fb_release,
	.fb_check_var = bfin_t350mcqb_fb_check_var,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_mmap = bfin_t350mcqb_fb_mmap,
	.fb_cursor = bfin_t350mcqb_fb_cursor,
	.fb_setcolreg = bfin_t350mcqb_fb_setcolreg,
};

#ifndef NO_BL_SUPPORT
static int bl_get_brightness(struct backlight_device *bd)
{
	return 0;
}

static struct backlight_ops bfin_lq043fb_bl_ops = {
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
	if (!fi || (fi == &bfin_t350mcqb_fb))
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

static irqreturn_t bfin_t350mcqb_irq_error(int irq, void *dev_id)
{
	/*struct bfin_t350mcqbfb_info *info = (struct bfin_t350mcqbfb_info *)dev_id;*/

	u16 status = bfin_read_PPI_STATUS();
	bfin_write_PPI_STATUS(0xFFFF);

	if (status) {
		bfin_t350mcqb_disable_ppi();
		disable_dma(CH_PPI);

		/* start dma */
		enable_dma(CH_PPI);
		bfin_t350mcqb_enable_ppi();
		bfin_write_PPI_STATUS(0xFFFF);
	}

	return IRQ_HANDLED;
}

static int __init bfin_t350mcqb_probe(struct platform_device *pdev)
{
	struct bfin_t350mcqbfb_info *info;
	struct fb_info *fbinfo;
	int ret;

	printk(KERN_INFO DRIVER_NAME ": %dx%d %d-bit RGB FrameBuffer initializing...\n",
					 LCD_X_RES, LCD_Y_RES, LCD_BPP);

	if (request_dma(CH_PPI, "CH_PPI") < 0) {
		printk(KERN_ERR DRIVER_NAME
		       ": couldn't request CH_PPI DMA\n");
		ret = -EFAULT;
		goto out1;
	}

	fbinfo =
	    framebuffer_alloc(sizeof(struct bfin_t350mcqbfb_info), &pdev->dev);
	if (!fbinfo) {
		ret = -ENOMEM;
		goto out2;
	}

	info = fbinfo->par;
	info->fb = fbinfo;
	info->dev = &pdev->dev;

	platform_set_drvdata(pdev, fbinfo);

	strcpy(fbinfo->fix.id, driver_name);

	fbinfo->fix.type = FB_TYPE_PACKED_PIXELS;
	fbinfo->fix.type_aux = 0;
	fbinfo->fix.xpanstep = 0;
	fbinfo->fix.ypanstep = 0;
	fbinfo->fix.ywrapstep = 0;
	fbinfo->fix.accel = FB_ACCEL_NONE;
	fbinfo->fix.visual = FB_VISUAL_TRUECOLOR;

	fbinfo->var.nonstd = 0;
	fbinfo->var.activate = FB_ACTIVATE_NOW;
	fbinfo->var.height = -1;
	fbinfo->var.width = -1;
	fbinfo->var.accel_flags = 0;
	fbinfo->var.vmode = FB_VMODE_NONINTERLACED;

	fbinfo->var.xres = LCD_X_RES;
	fbinfo->var.xres_virtual = LCD_X_RES;
	fbinfo->var.yres = LCD_Y_RES;
	fbinfo->var.yres_virtual = LCD_Y_RES;
	fbinfo->var.bits_per_pixel = LCD_BPP;

	fbinfo->var.red.offset = 0;
	fbinfo->var.green.offset = 8;
	fbinfo->var.blue.offset = 16;
	fbinfo->var.transp.offset = 0;
	fbinfo->var.red.length = 8;
	fbinfo->var.green.length = 8;
	fbinfo->var.blue.length = 8;
	fbinfo->var.transp.length = 0;
	fbinfo->fix.smem_len = LCD_X_RES * LCD_Y_RES * LCD_BPP / 8;

	fbinfo->fix.line_length = fbinfo->var.xres_virtual *
	    fbinfo->var.bits_per_pixel / 8;


	fbinfo->fbops = &bfin_t350mcqb_fb_ops;
	fbinfo->flags = FBINFO_FLAG_DEFAULT;

	info->fb_buffer =
	    dma_alloc_coherent(NULL, fbinfo->fix.smem_len, &info->dma_handle,
			       GFP_KERNEL);

	if (NULL == info->fb_buffer) {
		printk(KERN_ERR DRIVER_NAME
		       ": couldn't allocate dma buffer.\n");
		ret = -ENOMEM;
		goto out3;
	}

	memset(info->fb_buffer, 0, fbinfo->fix.smem_len);

	fbinfo->screen_base = (void *)info->fb_buffer + ACTIVE_VIDEO_MEM_OFFSET;
	fbinfo->fix.smem_start = (int)info->fb_buffer + ACTIVE_VIDEO_MEM_OFFSET;

	fbinfo->fbops = &bfin_t350mcqb_fb_ops;

	fbinfo->pseudo_palette = &info->pseudo_pal;

	if (fb_alloc_cmap(&fbinfo->cmap, BFIN_LCD_NBR_PALETTE_ENTRIES, 0)
	    < 0) {
		printk(KERN_ERR DRIVER_NAME
		       "Fail to allocate colormap (%d entries)\n",
		       BFIN_LCD_NBR_PALETTE_ENTRIES);
		ret = -EFAULT;
		goto out4;
	}

	if (bfin_t350mcqb_request_ports(1)) {
		printk(KERN_ERR DRIVER_NAME ": couldn't request gpio port.\n");
		ret = -EFAULT;
		goto out6;
	}

	info->irq = platform_get_irq(pdev, 0);
	if (info->irq < 0) {
		ret = -EINVAL;
		goto out7;
	}

	ret = request_irq(info->irq, bfin_t350mcqb_irq_error, IRQF_DISABLED,
			"PPI ERROR", info);
	if (ret < 0) {
		printk(KERN_ERR DRIVER_NAME
		       ": unable to request PPI ERROR IRQ\n");
		goto out7;
	}

	if (register_framebuffer(fbinfo) < 0) {
		printk(KERN_ERR DRIVER_NAME
		       ": unable to register framebuffer.\n");
		ret = -EINVAL;
		goto out8;
	}
#ifndef NO_BL_SUPPORT
	bl_dev =
	    backlight_device_register("bf52x-bl", NULL, NULL,
				      &bfin_lq043fb_bl_ops);
	bl_dev->props.max_brightness = 255;

	lcd_dev = lcd_device_register(DRIVER_NAME, NULL, &bfin_lcd_ops);
	lcd_dev->props.max_contrast = 255, printk(KERN_INFO "Done.\n");
#endif

	return 0;

out8:
	free_irq(info->irq, info);
out7:
	bfin_t350mcqb_request_ports(0);
out6:
	fb_dealloc_cmap(&fbinfo->cmap);
out4:
	dma_free_coherent(NULL, fbinfo->fix.smem_len, info->fb_buffer,
			  info->dma_handle);
out3:
	framebuffer_release(fbinfo);
out2:
	free_dma(CH_PPI);
out1:
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int bfin_t350mcqb_remove(struct platform_device *pdev)
{

	struct fb_info *fbinfo = platform_get_drvdata(pdev);
	struct bfin_t350mcqbfb_info *info = fbinfo->par;

	unregister_framebuffer(fbinfo);

	free_dma(CH_PPI);
	free_irq(info->irq, info);

	if (info->fb_buffer != NULL)
		dma_free_coherent(NULL, fbinfo->fix.smem_len, info->fb_buffer,
				  info->dma_handle);

	fb_dealloc_cmap(&fbinfo->cmap);

#ifndef NO_BL_SUPPORT
	lcd_device_unregister(lcd_dev);
	backlight_device_unregister(bl_dev);
#endif

	bfin_t350mcqb_request_ports(0);

	platform_set_drvdata(pdev, NULL);
	framebuffer_release(fbinfo);

	printk(KERN_INFO DRIVER_NAME ": Unregister LCD driver.\n");

	return 0;
}

#ifdef CONFIG_PM
static int bfin_t350mcqb_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct fb_info *fbinfo = platform_get_drvdata(pdev);
	struct bfin_t350mcqbfb_info *info = fbinfo->par;

	bfin_t350mcqb_disable_ppi();
	disable_dma(CH_PPI);
	bfin_write_PPI_STATUS(0xFFFF);

	return 0;
}

static int bfin_t350mcqb_resume(struct platform_device *pdev)
{
	struct fb_info *fbinfo = platform_get_drvdata(pdev);
	struct bfin_t350mcqbfb_info *info = fbinfo->par;

	enable_dma(CH_PPI);
	bfin_t350mcqb_enable_ppi();

	return 0;
}
#else
#define bfin_t350mcqb_suspend	NULL
#define bfin_t350mcqb_resume	NULL
#endif

static struct platform_driver bfin_t350mcqb_driver = {
	.probe = bfin_t350mcqb_probe,
	.remove = bfin_t350mcqb_remove,
	.suspend = bfin_t350mcqb_suspend,
	.resume = bfin_t350mcqb_resume,
	.driver = {
		   .name = DRIVER_NAME,
		   .owner = THIS_MODULE,
		   },
};

static int __devinit bfin_t350mcqb_driver_init(void)
{
	return platform_driver_register(&bfin_t350mcqb_driver);
}

static void __exit bfin_t350mcqb_driver_cleanup(void)
{
	platform_driver_unregister(&bfin_t350mcqb_driver);
}

MODULE_DESCRIPTION("Blackfin TFT LCD Driver");
MODULE_LICENSE("GPL");

module_init(bfin_t350mcqb_driver_init);
module_exit(bfin_t350mcqb_driver_cleanup);
