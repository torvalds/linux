/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *	JZ4740 SoC LCD framebuffer driver
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of  the GNU General Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>

#include <linux/clk.h>
#include <linux/delay.h>

#include <linux/console.h>
#include <linux/fb.h>

#include <linux/dma-mapping.h>

#include <asm/mach-jz4740/jz4740_fb.h>

#define JZ_REG_LCD_CFG		0x00
#define JZ_REG_LCD_VSYNC	0x04
#define JZ_REG_LCD_HSYNC	0x08
#define JZ_REG_LCD_VAT		0x0C
#define JZ_REG_LCD_DAH		0x10
#define JZ_REG_LCD_DAV		0x14
#define JZ_REG_LCD_PS		0x18
#define JZ_REG_LCD_CLS		0x1C
#define JZ_REG_LCD_SPL		0x20
#define JZ_REG_LCD_REV		0x24
#define JZ_REG_LCD_CTRL		0x30
#define JZ_REG_LCD_STATE	0x34
#define JZ_REG_LCD_IID		0x38
#define JZ_REG_LCD_DA0		0x40
#define JZ_REG_LCD_SA0		0x44
#define JZ_REG_LCD_FID0		0x48
#define JZ_REG_LCD_CMD0		0x4C
#define JZ_REG_LCD_DA1		0x50
#define JZ_REG_LCD_SA1		0x54
#define JZ_REG_LCD_FID1		0x58
#define JZ_REG_LCD_CMD1		0x5C

#define JZ_LCD_CFG_SLCD			BIT(31)
#define JZ_LCD_CFG_PS_DISABLE		BIT(23)
#define JZ_LCD_CFG_CLS_DISABLE		BIT(22)
#define JZ_LCD_CFG_SPL_DISABLE		BIT(21)
#define JZ_LCD_CFG_REV_DISABLE		BIT(20)
#define JZ_LCD_CFG_HSYNCM		BIT(19)
#define JZ_LCD_CFG_PCLKM		BIT(18)
#define JZ_LCD_CFG_INV			BIT(17)
#define JZ_LCD_CFG_SYNC_DIR		BIT(16)
#define JZ_LCD_CFG_PS_POLARITY		BIT(15)
#define JZ_LCD_CFG_CLS_POLARITY		BIT(14)
#define JZ_LCD_CFG_SPL_POLARITY		BIT(13)
#define JZ_LCD_CFG_REV_POLARITY		BIT(12)
#define JZ_LCD_CFG_HSYNC_ACTIVE_LOW	BIT(11)
#define JZ_LCD_CFG_PCLK_FALLING_EDGE	BIT(10)
#define JZ_LCD_CFG_DE_ACTIVE_LOW	BIT(9)
#define JZ_LCD_CFG_VSYNC_ACTIVE_LOW	BIT(8)
#define JZ_LCD_CFG_18_BIT		BIT(7)
#define JZ_LCD_CFG_PDW			(BIT(5) | BIT(4))
#define JZ_LCD_CFG_MODE_MASK 0xf

#define JZ_LCD_CTRL_BURST_4		(0x0 << 28)
#define JZ_LCD_CTRL_BURST_8		(0x1 << 28)
#define JZ_LCD_CTRL_BURST_16		(0x2 << 28)
#define JZ_LCD_CTRL_RGB555		BIT(27)
#define JZ_LCD_CTRL_OFUP		BIT(26)
#define JZ_LCD_CTRL_FRC_GRAYSCALE_16	(0x0 << 24)
#define JZ_LCD_CTRL_FRC_GRAYSCALE_4	(0x1 << 24)
#define JZ_LCD_CTRL_FRC_GRAYSCALE_2	(0x2 << 24)
#define JZ_LCD_CTRL_PDD_MASK		(0xff << 16)
#define JZ_LCD_CTRL_EOF_IRQ		BIT(13)
#define JZ_LCD_CTRL_SOF_IRQ		BIT(12)
#define JZ_LCD_CTRL_OFU_IRQ		BIT(11)
#define JZ_LCD_CTRL_IFU0_IRQ		BIT(10)
#define JZ_LCD_CTRL_IFU1_IRQ		BIT(9)
#define JZ_LCD_CTRL_DD_IRQ		BIT(8)
#define JZ_LCD_CTRL_QDD_IRQ		BIT(7)
#define JZ_LCD_CTRL_REVERSE_ENDIAN	BIT(6)
#define JZ_LCD_CTRL_LSB_FISRT		BIT(5)
#define JZ_LCD_CTRL_DISABLE		BIT(4)
#define JZ_LCD_CTRL_ENABLE		BIT(3)
#define JZ_LCD_CTRL_BPP_1		0x0
#define JZ_LCD_CTRL_BPP_2		0x1
#define JZ_LCD_CTRL_BPP_4		0x2
#define JZ_LCD_CTRL_BPP_8		0x3
#define JZ_LCD_CTRL_BPP_15_16		0x4
#define JZ_LCD_CTRL_BPP_18_24		0x5

#define JZ_LCD_CMD_SOF_IRQ BIT(31)
#define JZ_LCD_CMD_EOF_IRQ BIT(30)
#define JZ_LCD_CMD_ENABLE_PAL BIT(28)

#define JZ_LCD_SYNC_MASK 0x3ff

#define JZ_LCD_STATE_DISABLED BIT(0)

struct jzfb_framedesc {
	uint32_t next;
	uint32_t addr;
	uint32_t id;
	uint32_t cmd;
} __packed;

struct jzfb {
	struct fb_info *fb;
	struct platform_device *pdev;
	void __iomem *base;
	struct resource *mem;
	struct jz4740_fb_platform_data *pdata;

	size_t vidmem_size;
	void *vidmem;
	dma_addr_t vidmem_phys;
	struct jzfb_framedesc *framedesc;
	dma_addr_t framedesc_phys;

	struct clk *ldclk;
	struct clk *lpclk;

	unsigned is_enabled:1;
	struct mutex lock;

	uint32_t pseudo_palette[16];
};

static const struct fb_fix_screeninfo jzfb_fix = {
	.id		= "JZ4740 FB",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.xpanstep	= 0,
	.ypanstep	= 0,
	.ywrapstep	= 0,
	.accel		= FB_ACCEL_NONE,
};

/* Based on CNVT_TOHW macro from skeletonfb.c */
static inline uint32_t jzfb_convert_color_to_hw(unsigned val,
	struct fb_bitfield *bf)
{
	return (((val << bf->length) + 0x7FFF - val) >> 16) << bf->offset;
}

static int jzfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			unsigned blue, unsigned transp, struct fb_info *fb)
{
	uint32_t color;

	if (regno >= 16)
		return -EINVAL;

	color = jzfb_convert_color_to_hw(red, &fb->var.red);
	color |= jzfb_convert_color_to_hw(green, &fb->var.green);
	color |= jzfb_convert_color_to_hw(blue, &fb->var.blue);
	color |= jzfb_convert_color_to_hw(transp, &fb->var.transp);

	((uint32_t *)(fb->pseudo_palette))[regno] = color;

	return 0;
}

static int jzfb_get_controller_bpp(struct jzfb *jzfb)
{
	switch (jzfb->pdata->bpp) {
	case 18:
	case 24:
		return 32;
	case 15:
		return 16;
	default:
		return jzfb->pdata->bpp;
	}
}

static struct fb_videomode *jzfb_get_mode(struct jzfb *jzfb,
	struct fb_var_screeninfo *var)
{
	size_t i;
	struct fb_videomode *mode = jzfb->pdata->modes;

	for (i = 0; i < jzfb->pdata->num_modes; ++i, ++mode) {
		if (mode->xres == var->xres && mode->yres == var->yres)
			return mode;
	}

	return NULL;
}

static int jzfb_check_var(struct fb_var_screeninfo *var, struct fb_info *fb)
{
	struct jzfb *jzfb = fb->par;
	struct fb_videomode *mode;

	if (var->bits_per_pixel != jzfb_get_controller_bpp(jzfb) &&
		var->bits_per_pixel != jzfb->pdata->bpp)
		return -EINVAL;

	mode = jzfb_get_mode(jzfb, var);
	if (mode == NULL)
		return -EINVAL;

	fb_videomode_to_var(var, mode);

	switch (jzfb->pdata->bpp) {
	case 8:
		break;
	case 15:
		var->red.offset = 10;
		var->red.length = 5;
		var->green.offset = 6;
		var->green.length = 5;
		var->blue.offset = 0;
		var->blue.length = 5;
		break;
	case 16:
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		break;
	case 18:
		var->red.offset = 16;
		var->red.length = 6;
		var->green.offset = 8;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 6;
		var->bits_per_pixel = 32;
		break;
	case 32:
	case 24:
		var->transp.offset = 24;
		var->transp.length = 8;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->bits_per_pixel = 32;
		break;
	default:
		break;
	}

	return 0;
}

static int jzfb_set_par(struct fb_info *info)
{
	struct jzfb *jzfb = info->par;
	struct jz4740_fb_platform_data *pdata = jzfb->pdata;
	struct fb_var_screeninfo *var = &info->var;
	struct fb_videomode *mode;
	uint16_t hds, vds;
	uint16_t hde, vde;
	uint16_t ht, vt;
	uint32_t ctrl;
	uint32_t cfg;
	unsigned long rate;

	mode = jzfb_get_mode(jzfb, var);
	if (mode == NULL)
		return -EINVAL;

	if (mode == info->mode)
		return 0;

	info->mode = mode;

	hds = mode->hsync_len + mode->left_margin;
	hde = hds + mode->xres;
	ht = hde + mode->right_margin;

	vds = mode->vsync_len + mode->upper_margin;
	vde = vds + mode->yres;
	vt = vde + mode->lower_margin;

	ctrl = JZ_LCD_CTRL_OFUP | JZ_LCD_CTRL_BURST_16;

	switch (pdata->bpp) {
	case 1:
		ctrl |= JZ_LCD_CTRL_BPP_1;
		break;
	case 2:
		ctrl |= JZ_LCD_CTRL_BPP_2;
		break;
	case 4:
		ctrl |= JZ_LCD_CTRL_BPP_4;
		break;
	case 8:
		ctrl |= JZ_LCD_CTRL_BPP_8;
	break;
	case 15:
		ctrl |= JZ_LCD_CTRL_RGB555; /* Falltrough */
	case 16:
		ctrl |= JZ_LCD_CTRL_BPP_15_16;
		break;
	case 18:
	case 24:
	case 32:
		ctrl |= JZ_LCD_CTRL_BPP_18_24;
		break;
	default:
		break;
	}

	cfg = pdata->lcd_type & 0xf;

	if (!(mode->sync & FB_SYNC_HOR_HIGH_ACT))
		cfg |= JZ_LCD_CFG_HSYNC_ACTIVE_LOW;

	if (!(mode->sync & FB_SYNC_VERT_HIGH_ACT))
		cfg |= JZ_LCD_CFG_VSYNC_ACTIVE_LOW;

	if (pdata->pixclk_falling_edge)
		cfg |= JZ_LCD_CFG_PCLK_FALLING_EDGE;

	if (pdata->date_enable_active_low)
		cfg |= JZ_LCD_CFG_DE_ACTIVE_LOW;

	if (pdata->lcd_type == JZ_LCD_TYPE_GENERIC_18_BIT)
		cfg |= JZ_LCD_CFG_18_BIT;

	if (mode->pixclock) {
		rate = PICOS2KHZ(mode->pixclock) * 1000;
		mode->refresh = rate / vt / ht;
	} else {
		if (pdata->lcd_type == JZ_LCD_TYPE_8BIT_SERIAL)
			rate = mode->refresh * (vt + 2 * mode->xres) * ht;
		else
			rate = mode->refresh * vt * ht;

		mode->pixclock = KHZ2PICOS(rate / 1000);
	}

	mutex_lock(&jzfb->lock);
	if (!jzfb->is_enabled)
		clk_enable(jzfb->ldclk);
	else
		ctrl |= JZ_LCD_CTRL_ENABLE;

	switch (pdata->lcd_type) {
	case JZ_LCD_TYPE_SPECIAL_TFT_1:
	case JZ_LCD_TYPE_SPECIAL_TFT_2:
	case JZ_LCD_TYPE_SPECIAL_TFT_3:
		writel(pdata->special_tft_config.spl, jzfb->base + JZ_REG_LCD_SPL);
		writel(pdata->special_tft_config.cls, jzfb->base + JZ_REG_LCD_CLS);
		writel(pdata->special_tft_config.ps, jzfb->base + JZ_REG_LCD_PS);
		writel(pdata->special_tft_config.ps, jzfb->base + JZ_REG_LCD_REV);
		break;
	default:
		cfg |= JZ_LCD_CFG_PS_DISABLE;
		cfg |= JZ_LCD_CFG_CLS_DISABLE;
		cfg |= JZ_LCD_CFG_SPL_DISABLE;
		cfg |= JZ_LCD_CFG_REV_DISABLE;
		break;
	}

	writel(mode->hsync_len, jzfb->base + JZ_REG_LCD_HSYNC);
	writel(mode->vsync_len, jzfb->base + JZ_REG_LCD_VSYNC);

	writel((ht << 16) | vt, jzfb->base + JZ_REG_LCD_VAT);

	writel((hds << 16) | hde, jzfb->base + JZ_REG_LCD_DAH);
	writel((vds << 16) | vde, jzfb->base + JZ_REG_LCD_DAV);

	writel(cfg, jzfb->base + JZ_REG_LCD_CFG);

	writel(ctrl, jzfb->base + JZ_REG_LCD_CTRL);

	if (!jzfb->is_enabled)
		clk_disable_unprepare(jzfb->ldclk);

	mutex_unlock(&jzfb->lock);

	clk_set_rate(jzfb->lpclk, rate);
	clk_set_rate(jzfb->ldclk, rate * 3);

	return 0;
}

static void jzfb_enable(struct jzfb *jzfb)
{
	uint32_t ctrl;

	clk_prepare_enable(jzfb->ldclk);

	pinctrl_pm_select_default_state(&jzfb->pdev->dev);

	writel(0, jzfb->base + JZ_REG_LCD_STATE);

	writel(jzfb->framedesc->next, jzfb->base + JZ_REG_LCD_DA0);

	ctrl = readl(jzfb->base + JZ_REG_LCD_CTRL);
	ctrl |= JZ_LCD_CTRL_ENABLE;
	ctrl &= ~JZ_LCD_CTRL_DISABLE;
	writel(ctrl, jzfb->base + JZ_REG_LCD_CTRL);
}

static void jzfb_disable(struct jzfb *jzfb)
{
	uint32_t ctrl;

	ctrl = readl(jzfb->base + JZ_REG_LCD_CTRL);
	ctrl |= JZ_LCD_CTRL_DISABLE;
	writel(ctrl, jzfb->base + JZ_REG_LCD_CTRL);
	do {
		ctrl = readl(jzfb->base + JZ_REG_LCD_STATE);
	} while (!(ctrl & JZ_LCD_STATE_DISABLED));

	pinctrl_pm_select_sleep_state(&jzfb->pdev->dev);

	clk_disable_unprepare(jzfb->ldclk);
}

static int jzfb_blank(int blank_mode, struct fb_info *info)
{
	struct jzfb *jzfb = info->par;

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		mutex_lock(&jzfb->lock);
		if (jzfb->is_enabled) {
			mutex_unlock(&jzfb->lock);
			return 0;
		}

		jzfb_enable(jzfb);
		jzfb->is_enabled = 1;

		mutex_unlock(&jzfb->lock);
		break;
	default:
		mutex_lock(&jzfb->lock);
		if (!jzfb->is_enabled) {
			mutex_unlock(&jzfb->lock);
			return 0;
		}

		jzfb_disable(jzfb);
		jzfb->is_enabled = 0;

		mutex_unlock(&jzfb->lock);
		break;
	}

	return 0;
}

static int jzfb_alloc_devmem(struct jzfb *jzfb)
{
	int max_videosize = 0;
	struct fb_videomode *mode = jzfb->pdata->modes;
	void *page;
	int i;

	for (i = 0; i < jzfb->pdata->num_modes; ++mode, ++i) {
		if (max_videosize < mode->xres * mode->yres)
			max_videosize = mode->xres * mode->yres;
	}

	max_videosize *= jzfb_get_controller_bpp(jzfb) >> 3;

	jzfb->framedesc = dma_alloc_coherent(&jzfb->pdev->dev,
					sizeof(*jzfb->framedesc),
					&jzfb->framedesc_phys, GFP_KERNEL);

	if (!jzfb->framedesc)
		return -ENOMEM;

	jzfb->vidmem_size = PAGE_ALIGN(max_videosize);
	jzfb->vidmem = dma_alloc_coherent(&jzfb->pdev->dev,
					jzfb->vidmem_size,
					&jzfb->vidmem_phys, GFP_KERNEL);

	if (!jzfb->vidmem)
		goto err_free_framedesc;

	for (page = jzfb->vidmem;
		 page < jzfb->vidmem + PAGE_ALIGN(jzfb->vidmem_size);
		 page += PAGE_SIZE) {
		SetPageReserved(virt_to_page(page));
	}

	jzfb->framedesc->next = jzfb->framedesc_phys;
	jzfb->framedesc->addr = jzfb->vidmem_phys;
	jzfb->framedesc->id = 0xdeafbead;
	jzfb->framedesc->cmd = 0;
	jzfb->framedesc->cmd |= max_videosize / 4;

	return 0;

err_free_framedesc:
	dma_free_coherent(&jzfb->pdev->dev, sizeof(*jzfb->framedesc),
				jzfb->framedesc, jzfb->framedesc_phys);
	return -ENOMEM;
}

static void jzfb_free_devmem(struct jzfb *jzfb)
{
	dma_free_coherent(&jzfb->pdev->dev, jzfb->vidmem_size,
				jzfb->vidmem, jzfb->vidmem_phys);
	dma_free_coherent(&jzfb->pdev->dev, sizeof(*jzfb->framedesc),
				jzfb->framedesc, jzfb->framedesc_phys);
}

static struct  fb_ops jzfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = jzfb_check_var,
	.fb_set_par = jzfb_set_par,
	.fb_blank = jzfb_blank,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
	.fb_setcolreg = jzfb_setcolreg,
};

static int jzfb_probe(struct platform_device *pdev)
{
	int ret;
	struct jzfb *jzfb;
	struct fb_info *fb;
	struct jz4740_fb_platform_data *pdata = pdev->dev.platform_data;
	struct resource *mem;

	if (!pdata) {
		dev_err(&pdev->dev, "Missing platform data\n");
		return -ENXIO;
	}

	fb = framebuffer_alloc(sizeof(struct jzfb), &pdev->dev);
	if (!fb) {
		dev_err(&pdev->dev, "Failed to allocate framebuffer device\n");
		return -ENOMEM;
	}

	fb->fbops = &jzfb_ops;
	fb->flags = FBINFO_DEFAULT;

	jzfb = fb->par;
	jzfb->pdev = pdev;
	jzfb->pdata = pdata;

	jzfb->ldclk = devm_clk_get(&pdev->dev, "lcd");
	if (IS_ERR(jzfb->ldclk)) {
		ret = PTR_ERR(jzfb->ldclk);
		dev_err(&pdev->dev, "Failed to get lcd clock: %d\n", ret);
		goto err_framebuffer_release;
	}

	jzfb->lpclk = devm_clk_get(&pdev->dev, "lcd_pclk");
	if (IS_ERR(jzfb->lpclk)) {
		ret = PTR_ERR(jzfb->lpclk);
		dev_err(&pdev->dev, "Failed to get lcd pixel clock: %d\n", ret);
		goto err_framebuffer_release;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	jzfb->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(jzfb->base)) {
		ret = PTR_ERR(jzfb->base);
		goto err_framebuffer_release;
	}

	platform_set_drvdata(pdev, jzfb);

	mutex_init(&jzfb->lock);

	fb_videomode_to_modelist(pdata->modes, pdata->num_modes,
				 &fb->modelist);
	fb_videomode_to_var(&fb->var, pdata->modes);
	fb->var.bits_per_pixel = pdata->bpp;
	jzfb_check_var(&fb->var, fb);

	ret = jzfb_alloc_devmem(jzfb);
	if (ret) {
		dev_err(&pdev->dev, "Failed to allocate video memory\n");
		goto err_framebuffer_release;
	}

	fb->fix = jzfb_fix;
	fb->fix.line_length = fb->var.bits_per_pixel * fb->var.xres / 8;
	fb->fix.mmio_start = mem->start;
	fb->fix.mmio_len = resource_size(mem);
	fb->fix.smem_start = jzfb->vidmem_phys;
	fb->fix.smem_len =  fb->fix.line_length * fb->var.yres;
	fb->screen_base = jzfb->vidmem;
	fb->pseudo_palette = jzfb->pseudo_palette;

	fb_alloc_cmap(&fb->cmap, 256, 0);

	clk_prepare_enable(jzfb->ldclk);
	jzfb->is_enabled = 1;

	writel(jzfb->framedesc->next, jzfb->base + JZ_REG_LCD_DA0);

	fb->mode = NULL;
	jzfb_set_par(fb);

	ret = register_framebuffer(fb);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register framebuffer: %d\n", ret);
		goto err_free_devmem;
	}

	jzfb->fb = fb;

	return 0;

err_free_devmem:
	fb_dealloc_cmap(&fb->cmap);
	jzfb_free_devmem(jzfb);
err_framebuffer_release:
	framebuffer_release(fb);
	return ret;
}

static int jzfb_remove(struct platform_device *pdev)
{
	struct jzfb *jzfb = platform_get_drvdata(pdev);

	jzfb_blank(FB_BLANK_POWERDOWN, jzfb->fb);

	fb_dealloc_cmap(&jzfb->fb->cmap);
	jzfb_free_devmem(jzfb);

	framebuffer_release(jzfb->fb);

	return 0;
}

#ifdef CONFIG_PM

static int jzfb_suspend(struct device *dev)
{
	struct jzfb *jzfb = dev_get_drvdata(dev);

	console_lock();
	fb_set_suspend(jzfb->fb, 1);
	console_unlock();

	mutex_lock(&jzfb->lock);
	if (jzfb->is_enabled)
		jzfb_disable(jzfb);
	mutex_unlock(&jzfb->lock);

	return 0;
}

static int jzfb_resume(struct device *dev)
{
	struct jzfb *jzfb = dev_get_drvdata(dev);
	clk_prepare_enable(jzfb->ldclk);

	mutex_lock(&jzfb->lock);
	if (jzfb->is_enabled)
		jzfb_enable(jzfb);
	mutex_unlock(&jzfb->lock);

	console_lock();
	fb_set_suspend(jzfb->fb, 0);
	console_unlock();

	return 0;
}

static const struct dev_pm_ops jzfb_pm_ops = {
	.suspend	= jzfb_suspend,
	.resume		= jzfb_resume,
	.poweroff	= jzfb_suspend,
	.restore	= jzfb_resume,
};

#define JZFB_PM_OPS (&jzfb_pm_ops)

#else
#define JZFB_PM_OPS NULL
#endif

static struct platform_driver jzfb_driver = {
	.probe = jzfb_probe,
	.remove = jzfb_remove,
	.driver = {
		.name = "jz4740-fb",
		.pm = JZFB_PM_OPS,
	},
};
module_platform_driver(jzfb_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("JZ4740 SoC LCD framebuffer driver");
MODULE_ALIAS("platform:jz4740-fb");
