/*
 * drivers/video/tegrafb.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
 *         Travis Geiselbrecht <travis@palm.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/fb.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/wait.h>
#include <asm/cacheflush.h>
#include <mach/tegra_fb.h>

#define DC_CMD_GENERAL_INCR_SYNCPT		0x000
#define DC_CMD_GENERAL_INCR_SYNCPT_CNTRL	0x001
#define DC_CMD_GENERAL_INCR_SYNCPT_ERROR	0x002
#define DC_CMD_WIN_A_INCR_SYNCPT		0x008
#define DC_CMD_WIN_A_INCR_SYNCPT_CNTRL		0x009
#define DC_CMD_WIN_A_INCR_SYNCPT_ERROR		0x00a
#define DC_CMD_WIN_B_INCR_SYNCPT		0x010
#define DC_CMD_WIN_B_INCR_SYNCPT_CNTRL		0x011
#define DC_CMD_WIN_B_INCR_SYNCPT_ERROR		0x012
#define DC_CMD_WIN_C_INCR_SYNCPT		0x018
#define DC_CMD_WIN_C_INCR_SYNCPT_CNTRL		0x019
#define DC_CMD_WIN_C_INCR_SYNCPT_ERROR		0x01a
#define DC_CMD_CONT_SYNCPT_VSYNC		0x028
#define DC_CMD_DISPLAY_COMMAND_OPTION0		0x031
#define DC_CMD_DISPLAY_COMMAND			0x032
#define DC_CMD_SIGNAL_RAISE			0x033
#define DC_CMD_INT_STATUS			0x037
#define DC_CMD_INT_MASK				0x038
#define DC_CMD_INT_ENABLE			0x039
#define DC_CMD_INT_TYPE				0x03a
#define DC_CMD_INT_POLARITY			0x03b
#define DC_CMD_SIGNAL_RAISE1			0x03c
#define DC_CMD_SIGNAL_RAISE2			0x03d
#define DC_CMD_SIGNAL_RAISE3			0x03e
#define DC_CMD_STATE_ACCESS			0x040
#define DC_CMD_STATE_CONTROL			0x041
#define DC_CMD_DISPLAY_WINDOW_HEADER		0x042
#define DC_CMD_REG_ACT_CONTROL			0x043
#define DC_WINC_A_COLOR_PALETTE			0x500
#define DC_WINC_A_PALETTE_COLOR_EXT		0x600
#define DC_WIN_A_WIN_OPTIONS			0x700
#define DC_WIN_A_BYTE_SWAP			0x701
#define DC_WIN_A_BUFFER_CONTROL			0x702
#define DC_WIN_A_COLOR_DEPTH			0x703
#define DC_WIN_A_POSITION			0x704
#define DC_WIN_A_SIZE				0x705
#define DC_WIN_A_PRESCALED_SIZE			0x706
#define DC_WIN_A_H_INITIAL_DDA			0x707
#define DC_WIN_A_V_INITIAL_DDA			0x708
#define DC_WIN_A_DDA_INCREMENT			0x709
#define DC_WIN_A_LINE_STRIDE			0x70a
#define DC_WIN_A_BUF_STRIDE			0x70b
#define DC_WINBUF_A_START_ADDR			0x800
#define DC_WINBUF_A_ADDR_H_OFFSET		0x806
#define DC_WINBUF_A_ADDR_V_OFFSET		0x808


#define DC_INT_CTXSW				(1<<0)
#define DC_INT_FRAME_END			(1<<1)
#define DC_INT_V_BLANK				(1<<2)
#define DC_INT_H_BLANK				(1<<3)
#define DC_INT_V_PULSE3				(1<<4)

#define DC_COLOR_DEPTH_P1			0
#define DC_COLOR_DEPTH_P2			1
#define DC_COLOR_DEPTH_P4			2
#define DC_COLOR_DEPTH_P8			3
#define DC_COLOR_DEPTH_B4G4R4A4			4
#define DC_COLOR_DEPTH_B5G5R5A			5
#define DC_COLOR_DEPTH_B5G6R5			6
#define DC_COLOR_DEPTH_AB5G5R5			7
#define DC_COLOR_DEPTH_B8G8R8A8			12
#define DC_COLOR_DEPTH_R8G8B8A8			13
#define DC_COLOR_DEPTH_B6x2G6x2R6x2A8		14
#define DC_COLOR_DEPTH_R6x2G6x2B6x2A8		15
#define DC_COLOR_DEPTH_YCbCr422			16
#define DC_COLOR_DEPTH_YUV422			17
#define DC_COLOR_DEPTH_YCbCr420P		18
#define DC_COLOR_DEPTH_YUV420P			19
#define DC_COLOR_DEPTH_YCbCr422P		20
#define DC_COLOR_DEPTH_YUV422P			21
#define DC_COLOR_DEPTH_YCbCr422R		22
#define DC_COLOR_DEPTH_YUV422R			23
#define DC_COLOR_DEPTH_YCbCr422RA		24
#define DC_COLOR_DEPTH_YUV422RA			25

struct tegra_fb_info {
	struct clk *clk;
	struct clk *host1x_clk;
	struct resource *reg_mem;
	struct resource *fb_mem;
	void __iomem *reg_base;
	wait_queue_head_t event_wq;
	unsigned int wait_condition;
	/* Resolution of the output to the LCD.  If different from the
	   framebuffer resolution, the Tegra display block will scale it */
	int lcd_xres;
	int lcd_yres;
	int irq;
};

static void tegra_fb_writel(struct tegra_fb_info *tegra_fb, u32 val, unsigned long reg)
{
	writel(val, tegra_fb->reg_base + reg*sizeof(u32));
}

static u32 tegra_fb_readl(struct tegra_fb_info *tegra_fb, unsigned long reg)
{
	return readl(tegra_fb->reg_base + reg*sizeof(u32));
}

/* palette attary used by the fbcon */
u32 pseudo_palette[16];

irqreturn_t tegra_fb_irq(int irq, void *ptr)
{
	struct fb_info *info = ptr;
	struct tegra_fb_info *tegra_fb = info->par;

	u32 reg = tegra_fb_readl(tegra_fb, DC_CMD_INT_STATUS);
	tegra_fb_writel(tegra_fb, reg, DC_CMD_INT_STATUS);

	tegra_fb->wait_condition = 1;
	wake_up(&tegra_fb->event_wq);
	return IRQ_HANDLED;
}

static int tegra_fb_wait_for_event(struct tegra_fb_info *tegra_fb, unsigned long timeout, u32 irq_mask)
{
	u32 reg;

	tegra_fb->wait_condition = 0;

	reg = tegra_fb_readl(tegra_fb, DC_CMD_INT_MASK);
	reg |= irq_mask;
	tegra_fb_writel(tegra_fb, reg, DC_CMD_INT_MASK);

	/* Clear any pending interrupt */
	tegra_fb_writel(tegra_fb, irq_mask, DC_CMD_INT_STATUS);

	tegra_fb_writel(tegra_fb, irq_mask, DC_CMD_INT_ENABLE);

	/* Wait for the irq to fire */
	wait_event_interruptible_timeout(tegra_fb->event_wq, tegra_fb->wait_condition, timeout);

	tegra_fb_writel(tegra_fb, 0, DC_CMD_INT_ENABLE);

	if (!tegra_fb->wait_condition) {
		pr_warning("%s: wait for vsync timed out\n", __func__);
		return -ETIMEDOUT;
	}
	return 0;
}

/* Writes to many registers in the display block are buffered.
 * tegra_fb_activate requests an update of the main registers from their
 * double-buffered registers, which takes effect at the end of the next frame,
 * and then waits for the frame_end IRQ.
 */
static void tegra_fb_activate(struct tegra_fb_info *tegra_fb)
{
	int vsync_count = 0;

	if (unlikely(tegra_fb_readl(tegra_fb, DC_CMD_STATE_CONTROL) & 3)) {
		pr_warning("%s: update already activated!\n", __func__);
		return;
	}

	tegra_fb_writel(tegra_fb, (1 << 8) | (1 << 9), DC_CMD_STATE_CONTROL);
	tegra_fb_writel(tegra_fb, (1 << 0) | (1 << 1), DC_CMD_STATE_CONTROL);
	while (tegra_fb_readl(tegra_fb, DC_CMD_STATE_CONTROL) & 3) {
		vsync_count++;
		if (tegra_fb_wait_for_event(tegra_fb, HZ/10, DC_INT_FRAME_END))
			break;
	}
	if (unlikely(vsync_count > 1))
		pr_warning("%s: waited for %d vsyncs\n", __func__, vsync_count);
}


static int tegra_fb_open(struct fb_info *info, int user)
{
	return 0;
}

static int tegra_fb_release(struct fb_info *info, int user)
{
	return 0;
}

static int tegra_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	if ((var->xres != info->var.xres) ||
	    (var->yres != info->var.yres) ||
	    (var->xres_virtual != info->var.xres_virtual) ||
	    (var->yres_virtual != info->var.yres_virtual) ||
	    (var->grayscale != info->var.grayscale))
		return -EINVAL;
	return 0;
}

static int tegra_fb_set_par(struct fb_info *info)
{
	struct tegra_fb_info *tegra_fb = info->par;
	struct fb_var_screeninfo *var = &info->var;
	struct fb_fix_screeninfo *fix = &info->fix;
	u32 color_depth;
	unsigned int h_dda;
	unsigned int v_dda;

	/* we only support RGB ordering for now */
	switch (var->bits_per_pixel) {
	case 32:
	case 24:
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 16;
		var->blue.length = 8;
		color_depth = DC_COLOR_DEPTH_B8G8R8A8;
		break;
	case 16:
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		color_depth = DC_COLOR_DEPTH_B5G6R5;
		break;
	default:
		return -EINVAL;
	}
	fix->line_length = var->xres * var->bits_per_pixel / 8;

	h_dda = (var->xres * 0x1000) / (tegra_fb->lcd_xres - 1);
	v_dda = (var->yres * 0x1000) / (tegra_fb->lcd_yres - 1);

	tegra_fb_writel(tegra_fb, tegra_fb->lcd_yres<<16 | tegra_fb->lcd_xres,
		DC_WIN_A_SIZE);
	tegra_fb_writel(tegra_fb, var->yres<<16 | fix->line_length,
		DC_WIN_A_PRESCALED_SIZE);
	tegra_fb_writel(tegra_fb, 0, DC_WIN_A_H_INITIAL_DDA);
	tegra_fb_writel(tegra_fb, 0, DC_WIN_A_V_INITIAL_DDA);
	tegra_fb_writel(tegra_fb, v_dda << 16 | h_dda, DC_WIN_A_DDA_INCREMENT);
	tegra_fb_writel(tegra_fb, color_depth, DC_WIN_A_COLOR_DEPTH);
	tegra_fb_writel(tegra_fb, fix->line_length, DC_WIN_A_LINE_STRIDE);
	return 0;
}

static int tegra_fb_setcolreg(unsigned regno, unsigned red, unsigned green,
	unsigned blue, unsigned transp, struct fb_info *info)
{
	struct fb_var_screeninfo *var = &info->var;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		u32 v;

		if (regno >= 16)
			return -EINVAL;

		v = (red << var->red.offset) |
			(green << var->green.offset) |
			(blue << var->blue.offset);

		((u32 *)info->pseudo_palette)[regno] = v;
	}

	return 0;
}

static int tegra_fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct tegra_fb_info *tegra_fb = info->par;
	char __iomem *flush_start;
	char __iomem *flush_end;
	u32 addr;

	flush_start = info->screen_base + (var->yoffset * info->fix.line_length);
	flush_end = flush_start + (var->yres * info->fix.line_length);

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;

	addr = info->fix.smem_start + (var->yoffset * info->fix.line_length) +
		(var->xoffset * (var->bits_per_pixel/8));

	tegra_fb_writel(tegra_fb, addr, DC_WINBUF_A_START_ADDR);
	tegra_fb_activate(tegra_fb);
	return 0;
}

static void tegra_fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	cfb_fillrect(info, rect);
}

static void tegra_fb_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	cfb_copyarea(info, region);
}

static void tegra_fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	cfb_imageblit(info, image);
}

#ifdef DEBUG
#define DUMP_REG(a) dev_dbg(info->dev, "%-32s\t%03x\t%08x\n", #a, a, tegra_fb_readl(tegra_fb, a));
static void dump_regs(struct fb_info *info)
{
	struct tegra_fb_info *tegra_fb = info->par;

	DUMP_REG(DC_CMD_GENERAL_INCR_SYNCPT);
	DUMP_REG(DC_CMD_GENERAL_INCR_SYNCPT_CNTRL);
	DUMP_REG(DC_CMD_GENERAL_INCR_SYNCPT_ERROR);
	DUMP_REG(DC_CMD_WIN_A_INCR_SYNCPT);
	DUMP_REG(DC_CMD_WIN_A_INCR_SYNCPT_CNTRL);
	DUMP_REG(DC_CMD_WIN_A_INCR_SYNCPT_ERROR);
	DUMP_REG(DC_CMD_WIN_B_INCR_SYNCPT);
	DUMP_REG(DC_CMD_WIN_B_INCR_SYNCPT_CNTRL);
	DUMP_REG(DC_CMD_WIN_B_INCR_SYNCPT_ERROR);
	DUMP_REG(DC_CMD_WIN_C_INCR_SYNCPT);
	DUMP_REG(DC_CMD_WIN_C_INCR_SYNCPT_CNTRL);
	DUMP_REG(DC_CMD_WIN_C_INCR_SYNCPT_ERROR);
	DUMP_REG(DC_CMD_CONT_SYNCPT_VSYNC);
	DUMP_REG(DC_CMD_DISPLAY_COMMAND_OPTION0);
	DUMP_REG(DC_CMD_DISPLAY_COMMAND);
	DUMP_REG(DC_CMD_SIGNAL_RAISE);
	DUMP_REG(DC_CMD_INT_STATUS);
	DUMP_REG(DC_CMD_INT_MASK);
	DUMP_REG(DC_CMD_INT_ENABLE);
	DUMP_REG(DC_CMD_INT_TYPE);
	DUMP_REG(DC_CMD_INT_POLARITY);
	DUMP_REG(DC_CMD_SIGNAL_RAISE1);
	DUMP_REG(DC_CMD_SIGNAL_RAISE2);
	DUMP_REG(DC_CMD_SIGNAL_RAISE3);
	DUMP_REG(DC_CMD_STATE_ACCESS);
	DUMP_REG(DC_CMD_STATE_CONTROL);
	DUMP_REG(DC_CMD_DISPLAY_WINDOW_HEADER);
	DUMP_REG(DC_CMD_REG_ACT_CONTROL);
	tegra_fb_writel(tegra_fb, 1<<4, DC_CMD_DISPLAY_WINDOW_HEADER);
	DUMP_REG(DC_CMD_DISPLAY_WINDOW_HEADER);
	DUMP_REG(DC_WINC_A_COLOR_PALETTE);
	DUMP_REG(DC_WINC_A_PALETTE_COLOR_EXT);
	DUMP_REG(DC_WIN_A_WIN_OPTIONS);
	DUMP_REG(DC_WIN_A_BYTE_SWAP);
	DUMP_REG(DC_WIN_A_BUFFER_CONTROL);
	DUMP_REG(DC_WIN_A_COLOR_DEPTH);
	DUMP_REG(DC_WIN_A_POSITION);
	DUMP_REG(DC_WIN_A_SIZE);
	DUMP_REG(DC_WIN_A_PRESCALED_SIZE);
	DUMP_REG(DC_WIN_A_H_INITIAL_DDA);
	DUMP_REG(DC_WIN_A_V_INITIAL_DDA);
	DUMP_REG(DC_WIN_A_DDA_INCREMENT);
	DUMP_REG(DC_WIN_A_LINE_STRIDE);
	DUMP_REG(DC_WIN_A_BUF_STRIDE);
	DUMP_REG(DC_WINBUF_A_START_ADDR);
	DUMP_REG(DC_WINBUF_A_ADDR_H_OFFSET);
	DUMP_REG(DC_WINBUF_A_ADDR_V_OFFSET);
}
#endif

static struct fb_ops tegra_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = tegra_fb_open,
	.fb_release = tegra_fb_release,
	.fb_check_var = tegra_fb_check_var,
	.fb_set_par = tegra_fb_set_par,
	.fb_setcolreg = tegra_fb_setcolreg,
	.fb_pan_display = tegra_fb_pan_display,
	.fb_fillrect = tegra_fb_fillrect,
	.fb_copyarea = tegra_fb_copyarea,
	.fb_imageblit = tegra_fb_imageblit,
};

static int tegra_plat_probe(struct platform_device *pdev)
{
	struct fb_info *info;
	struct tegra_fb_info *tegra_fb;
	struct clk *clk;
	struct clk *host1x_clk;
	struct resource	*res;
	struct resource *reg_mem;
	struct resource *fb_mem;
	int ret = 0;
	void __iomem *reg_base;
	void __iomem *fb_base;
	unsigned long fb_size;
	unsigned long fb_phys;
	int irq;
	const struct tegra_fb_lcd_data *lcd_data = pdev->dev.platform_data;

	info = framebuffer_alloc(sizeof(struct tegra_fb_info), &pdev->dev);
	if (!info) {
		ret = -ENOMEM;
		goto err;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev, "no irq\n");
		ret = -ENOENT;
		goto err_free;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no mem resource\n");
		ret = -ENOENT;
		goto err_free;
	}

	reg_mem = request_mem_region(res->start, resource_size(res), pdev->name);
	if (!reg_mem) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		ret = -EBUSY;
		goto err_free;
	}

	reg_base = ioremap(res->start, resource_size(res));
	if (!reg_base) {
		dev_err(&pdev->dev, "registers can't be mapped\n");
		ret = -EBUSY;
		goto err_release_resource_reg;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(&pdev->dev, "no mem resource\n");
		ret = -ENOENT;
		goto err_iounmap_reg;
	}

	fb_mem = request_mem_region(res->start, resource_size(res), pdev->name);
	if (!fb_mem) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		ret = -EBUSY;
		goto err_iounmap_reg;
	}

	fb_size = resource_size(res);
	fb_phys = res->start;
	fb_base = ioremap_nocache(fb_phys, fb_size);
	if (!fb_base) {
		dev_err(&pdev->dev, "fb can't be mapped\n");
		ret = -EBUSY;
		goto err_release_resource_fb;
	}

	host1x_clk = clk_get(&pdev->dev, "host1x");
	if (!host1x_clk) {
		dev_err(&pdev->dev, "can't get host1x clock\n");
		ret = -ENOENT;
		goto err_iounmap_fb;
	}
	clk_enable(host1x_clk);

	clk = clk_get(&pdev->dev, NULL);
	if (!clk) {
		dev_err(&pdev->dev, "can't get clock\n");
		ret = -ENOENT;
		goto err_put_host1x_clk;
	}
	clk_enable(clk);

	tegra_fb = info->par;
	tegra_fb->clk = clk;
	tegra_fb->host1x_clk = host1x_clk;
	tegra_fb->fb_mem = fb_mem;
	tegra_fb->reg_mem = reg_mem;
	tegra_fb->reg_base = reg_base;
	tegra_fb->irq = irq;
	tegra_fb->lcd_xres = lcd_data->lcd_xres;
	tegra_fb->lcd_yres = lcd_data->lcd_yres;

	info->fbops = &tegra_fb_ops;
	info->pseudo_palette = pseudo_palette;
	info->screen_base = fb_base;
	info->screen_size = fb_size;

	strlcpy(info->fix.id, "tegra_fb", sizeof(info->fix.id));
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.xpanstep   = 1;
	info->fix.ypanstep   = 1;
	info->fix.accel	  = FB_ACCEL_NONE;
	info->fix.smem_start = fb_phys;
	info->fix.smem_len = fb_size;

	info->var.xres			= lcd_data->fb_xres;
	info->var.yres			= lcd_data->fb_yres;
	info->var.xres_virtual		= lcd_data->fb_xres;
	info->var.yres_virtual		= lcd_data->fb_yres*2;
	info->var.bits_per_pixel	= lcd_data->bits_per_pixel;
	info->var.activate		= FB_ACTIVATE_NOW;
	info->var.height		= -1;
	info->var.width			= -1;
	info->var.pixclock		= 24500;
	info->var.left_margin		= 0;
	info->var.right_margin		= 0;
	info->var.upper_margin		= 0;
	info->var.lower_margin		= 0;
	info->var.hsync_len		= 0;
	info->var.vsync_len		= 0;
	info->var.vmode			= FB_VMODE_NONINTERLACED;

	if (request_irq(irq, tegra_fb_irq, IRQF_DISABLED,
			dev_name(&pdev->dev), info)) {
		pr_debug("%s: request_irq %d failed\n",
			pdev->name, irq);
		ret = -EBUSY;
		goto err_put_clk;
	}

	init_waitqueue_head(&tegra_fb->event_wq);

	/* Enable writes to Window A */
	tegra_fb_writel(tegra_fb, 1<<4, DC_CMD_DISPLAY_WINDOW_HEADER);

#ifdef DEBUG
	info->dev = &pdev->dev;
	dev_dbg(info->dev, "Framebuffer registers before init:\n");
	dump_regs(info);
#endif

	tegra_fb_set_par(info);

#ifdef DEBUG
	dev_dbg(info->dev, "Framebuffer registers after init:\n");
	dump_regs(info);
#endif

	dev_info(&pdev->dev, "base address: %08x (%08x)\n",
			(unsigned int)info->fix.smem_start,
			(unsigned int)info->screen_base);

	if (register_framebuffer(info)) {
		dev_err(&pdev->dev, "failed to register framebuffer\n");
		ret = -ENODEV;
		goto err_free_irq;
	}
	platform_set_drvdata(pdev, info);
	return 0;

err_free_irq:
	free_irq(irq, info);
err_put_clk:
	clk_disable(clk);
	clk_put(clk);
err_put_host1x_clk:
	clk_disable(host1x_clk);
	clk_put(host1x_clk);
err_iounmap_fb:
	iounmap(fb_base);
err_release_resource_fb:
	release_resource(fb_mem);
err_iounmap_reg:
	iounmap(reg_base);
err_release_resource_reg:
	release_resource(reg_mem);
err_free:
	framebuffer_release(info);
err:
	return ret;
}

static int tegra_plat_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	struct tegra_fb_info *tegra_fb = info->par;
	unregister_framebuffer(info);
	free_irq(tegra_fb->irq, info);
	clk_disable(tegra_fb->clk);
	clk_put(tegra_fb->clk);
	clk_disable(tegra_fb->host1x_clk);
	clk_put(tegra_fb->host1x_clk);
	iounmap(info->screen_base);
	release_resource(tegra_fb->fb_mem);
	iounmap(tegra_fb->reg_base);
	release_resource(tegra_fb->reg_mem);
	framebuffer_release(info);
	return 0;
}

struct platform_driver tegra_platform_driver = {
	.probe = tegra_plat_probe,
	.remove = tegra_plat_remove,
	.driver = {
		.name = "tegrafb",
		.owner = THIS_MODULE,
	},
};

static int __init tegra_fb_init(void)
{
	int e;
	e = platform_driver_register(&tegra_platform_driver);
	if (e) {
		pr_info("tegrafb: platform_driver_register failed\n");
		return e;
	}
	return e;
}

static void __exit tegra_exit(void)
{
	platform_driver_unregister(&tegra_platform_driver);
}

module_exit(tegra_exit);
module_init(tegra_fb_init);
