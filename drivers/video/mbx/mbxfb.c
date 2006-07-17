/*
 *  linux/drivers/video/mbx/mbxfb.c
 *
 *  Copyright (C) 2006 Compulab, Ltd.
 *  Mike Rapoport <mike@compulab.co.il>
 *
 *   Based on pxafb.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 *   Intel 2700G (Marathon) Graphics Accelerator Frame Buffer Driver
 *
 */

#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/io.h>

#include <video/mbxfb.h>

#include "regs.h"
#include "reg_bits.h"

static unsigned long virt_base_2700;

#define MIN_XRES	16
#define MIN_YRES	16
#define MAX_XRES	2048
#define MAX_YRES	2048

#define MAX_PALETTES	16

/* FIXME: take care of different chip revisions with different sizes
   of ODFB */
#define MEMORY_OFFSET	0x60000

struct mbxfb_info {
	struct device *dev;

	struct resource *fb_res;
	struct resource *fb_req;

	struct resource *reg_res;
	struct resource *reg_req;

	void __iomem *fb_virt_addr;
	unsigned long fb_phys_addr;

	void __iomem *reg_virt_addr;
	unsigned long reg_phys_addr;

	int (*platform_probe) (struct fb_info * fb);
	int (*platform_remove) (struct fb_info * fb);

	u32 pseudo_palette[MAX_PALETTES];
#ifdef CONFIG_FB_MBX_DEBUG
	void *debugfs_data;
#endif

};

static struct fb_var_screeninfo mbxfb_default __devinitdata = {
	.xres = 640,
	.yres = 480,
	.xres_virtual = 640,
	.yres_virtual = 480,
	.bits_per_pixel = 16,
	.red = {11, 5, 0},
	.green = {5, 6, 0},
	.blue = {0, 5, 0},
	.activate = FB_ACTIVATE_TEST,
	.height = -1,
	.width = -1,
	.pixclock = 40000,
	.left_margin = 48,
	.right_margin = 16,
	.upper_margin = 33,
	.lower_margin = 10,
	.hsync_len = 96,
	.vsync_len = 2,
	.vmode = FB_VMODE_NONINTERLACED,
	.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct fb_fix_screeninfo mbxfb_fix  __devinitdata = {
	.id = "MBX",
	.type = FB_TYPE_PACKED_PIXELS,
	.visual = FB_VISUAL_TRUECOLOR,
	.xpanstep = 0,
	.ypanstep = 0,
	.ywrapstep = 0,
	.accel = FB_ACCEL_NONE,
};

struct pixclock_div {
	u8 m;
	u8 n;
	u8 p;
};

static unsigned int mbxfb_get_pixclock(unsigned int pixclock_ps,
				       struct pixclock_div *div)
{
	u8 m, n, p;
	unsigned int err = 0;
	unsigned int min_err = ~0x0;
	unsigned int clk;
	unsigned int best_clk = 0;
	unsigned int ref_clk = 13000;	/* FIXME: take from platform data */
	unsigned int pixclock;

	/* convert pixclock to KHz */
	pixclock = PICOS2KHZ(pixclock_ps);

	for (m = 1; m < 64; m++) {
		for (n = 1; n < 8; n++) {
			for (p = 0; p < 8; p++) {
				clk = (ref_clk * m) / (n * (1 << p));
				err = (clk > pixclock) ? (clk - pixclock) :
					(pixclock - clk);
				if (err < min_err) {
					min_err = err;
					best_clk = clk;
					div->m = m;
					div->n = n;
					div->p = p;
				}
			}
		}
	}
	return KHZ2PICOS(best_clk);
}

static int mbxfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int trans, struct fb_info *info)
{
	u32 val, ret = 1;

	if (regno < MAX_PALETTES) {
		u32 *pal = info->pseudo_palette;

		val = (red & 0xf800) | ((green & 0xfc00) >> 5) |
			((blue & 0xf800) >> 11);
		pal[regno] = val;
		ret = 0;
	}

	return ret;
}

static int mbxfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct pixclock_div div;

	var->pixclock = mbxfb_get_pixclock(var->pixclock, &div);

	if (var->xres < MIN_XRES)
		var->xres = MIN_XRES;
	if (var->yres < MIN_YRES)
		var->yres = MIN_YRES;
	if (var->xres > MAX_XRES)
		return -EINVAL;
	if (var->yres > MAX_YRES)
		return -EINVAL;
	var->xres_virtual = max(var->xres_virtual, var->xres);
	var->yres_virtual = max(var->yres_virtual, var->yres);

	switch (var->bits_per_pixel) {
		/* 8 bits-per-pixel is not supported yet */
	case 8:
		return -EINVAL;
	case 16:
		var->green.length = (var->green.length == 5) ? 5 : 6;
		var->red.length = 5;
		var->blue.length = 5;
		var->transp.length = 6 - var->green.length;
		var->blue.offset = 0;
		var->green.offset = 5;
		var->red.offset = 5 + var->green.length;
		var->transp.offset = (5 + var->red.offset) & 15;
		break;
	case 24:		/* RGB 888   */
	case 32:		/* RGBA 8888 */
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.length = var->bits_per_pixel - 24;
		var->transp.offset = (var->transp.length) ? 24 : 0;
		break;
	}
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	return 0;
}

static int mbxfb_set_par(struct fb_info *info)
{
	struct fb_var_screeninfo *var = &info->var;
	struct pixclock_div div;
	ushort hbps, ht, hfps, has;
	ushort vbps, vt, vfps, vas;
	u32 gsctrl = readl(GSCTRL);
	u32 gsadr = readl(GSADR);

	info->fix.line_length = var->xres_virtual * var->bits_per_pixel / 8;

	/* setup color mode */
	gsctrl &= ~(FMsk(GSCTRL_GPIXFMT));
	/* FIXME: add *WORKING* support for 8-bits per color */
	if (info->var.bits_per_pixel == 8) {
		return -EINVAL;
	} else {
		fb_dealloc_cmap(&info->cmap);
		gsctrl &= ~GSCTRL_LUT_EN;

		info->fix.visual = FB_VISUAL_TRUECOLOR;
		switch (info->var.bits_per_pixel) {
		case 16:
			if (info->var.green.length == 5)
				gsctrl |= GSCTRL_GPIXFMT_ARGB1555;
			else
				gsctrl |= GSCTRL_GPIXFMT_RGB565;
			break;
		case 24:
			gsctrl |= GSCTRL_GPIXFMT_RGB888;
			break;
		case 32:
			gsctrl |= GSCTRL_GPIXFMT_ARGB8888;
			break;
		}
	}

	/* setup resolution */
	gsctrl &= ~(FMsk(GSCTRL_GSWIDTH) | FMsk(GSCTRL_GSHEIGHT));
	gsctrl |= Gsctrl_Width(info->var.xres - 1) |
		Gsctrl_Height(info->var.yres - 1);
	writel(gsctrl, GSCTRL);
	udelay(1000);

	gsadr &= ~(FMsk(GSADR_SRCSTRIDE));
	gsadr |= Gsadr_Srcstride(info->var.xres * info->var.bits_per_pixel /
				 (8 * 16) - 1);
	writel(gsadr, GSADR);
	udelay(1000);

	/* setup timings */
	var->pixclock = mbxfb_get_pixclock(info->var.pixclock, &div);

	writel((Disp_Pll_M(div.m) | Disp_Pll_N(div.n) |
		Disp_Pll_P(div.p) | DISP_PLL_EN), DISPPLL);

	hbps = var->hsync_len;
	has = hbps + var->left_margin;
	hfps = has + var->xres;
	ht = hfps + var->right_margin;

	vbps = var->vsync_len;
	vas = vbps + var->upper_margin;
	vfps = vas + var->yres;
	vt = vfps + var->lower_margin;

	writel((Dht01_Hbps(hbps) | Dht01_Ht(ht)), DHT01);
	writel((Dht02_Hlbs(has) | Dht02_Has(has)), DHT02);
	writel((Dht03_Hfps(hfps) | Dht03_Hrbs(hfps)), DHT03);
	writel((Dhdet_Hdes(has) | Dhdet_Hdef(hfps)), DHDET);

	writel((Dvt01_Vbps(vbps) | Dvt01_Vt(vt)), DVT01);
	writel((Dvt02_Vtbs(vas) | Dvt02_Vas(vas)), DVT02);
	writel((Dvt03_Vfps(vfps) | Dvt03_Vbbs(vfps)), DVT03);
	writel((Dvdet_Vdes(vas) | Dvdet_Vdef(vfps)), DVDET);
	writel((Dvectrl_Vevent(vfps) | Dvectrl_Vfetch(vbps)), DVECTRL);

	writel((readl(DSCTRL) | DSCTRL_SYNCGEN_EN), DSCTRL);

	return 0;
}

static int mbxfb_blank(int blank, struct fb_info *info)
{
	switch (blank) {
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_NORMAL:
		writel((readl(DSCTRL) & ~DSCTRL_SYNCGEN_EN), DSCTRL);
		udelay(1000);
		writel((readl(PIXCLK) & ~PIXCLK_EN), PIXCLK);
		udelay(1000);
		writel((readl(VOVRCLK) & ~VOVRCLK_EN), VOVRCLK);
		udelay(1000);
		break;
	case FB_BLANK_UNBLANK:
		writel((readl(DSCTRL) | DSCTRL_SYNCGEN_EN), DSCTRL);
		udelay(1000);
		writel((readl(PIXCLK) | PIXCLK_EN), PIXCLK);
		udelay(1000);
		break;
	}
	return 0;
}

static struct fb_ops mbxfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = mbxfb_check_var,
	.fb_set_par = mbxfb_set_par,
	.fb_setcolreg = mbxfb_setcolreg,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_blank = mbxfb_blank,
};

/*
  Enable external SDRAM controller. Assume that all clocks are active
  by now.
*/
static void __devinit setup_memc(struct fb_info *fbi)
{
	struct mbxfb_info *mfbi = fbi->par;
	unsigned long tmp;
	int i;

	/* FIXME: use platfrom specific parameters */
	/* setup SDRAM controller */
	writel((LMCFG_LMC_DS | LMCFG_LMC_TS | LMCFG_LMD_TS |
		LMCFG_LMA_TS),
	       LMCFG);
	udelay(1000);

	writel(LMPWR_MC_PWR_ACT, LMPWR);
	udelay(1000);

	/* setup SDRAM timings */
	writel((Lmtim_Tras(7) | Lmtim_Trp(3) | Lmtim_Trcd(3) |
		Lmtim_Trc(9) | Lmtim_Tdpl(2)),
	       LMTIM);
	udelay(1000);
	/* setup SDRAM refresh rate */
	writel(0xc2b, LMREFRESH);
	udelay(1000);
	/* setup SDRAM type parameters */
	writel((LMTYPE_CASLAT_3 | LMTYPE_BKSZ_2 | LMTYPE_ROWSZ_11 |
		LMTYPE_COLSZ_8),
	       LMTYPE);
	udelay(1000);
	/* enable memory controller */
	writel(LMPWR_MC_PWR_ACT, LMPWR);
	udelay(1000);

	/* perform dummy reads */
	for ( i = 0; i < 16; i++ ) {
		tmp = readl(fbi->screen_base);
	}
}

static void enable_clocks(struct fb_info *fbi)
{
	/* enable clocks */
	writel(SYSCLKSRC_PLL_2, SYSCLKSRC);
	udelay(1000);
	writel(PIXCLKSRC_PLL_1, PIXCLKSRC);
	udelay(1000);
	writel(0x00000000, CLKSLEEP);
	udelay(1000);
	writel((Core_Pll_M(0x17) | Core_Pll_N(0x3) | Core_Pll_P(0x0) |
		CORE_PLL_EN),
	       COREPLL);
	udelay(1000);
	writel((Disp_Pll_M(0x1b) | Disp_Pll_N(0x7) | Disp_Pll_P(0x1) |
		DISP_PLL_EN),
	       DISPPLL);

	writel(0x00000000, VOVRCLK);
	udelay(1000);
	writel(PIXCLK_EN, PIXCLK);
	udelay(1000);
	writel(MEMCLK_EN, MEMCLK);
	udelay(1000);
	writel(0x00000006, M24CLK);
	udelay(1000);
	writel(0x00000006, MBXCLK);
	udelay(1000);
	writel(SDCLK_EN, SDCLK);
	udelay(1000);
	writel(0x00000001, PIXCLKDIV);
	udelay(1000);
}

static void __devinit setup_graphics(struct fb_info *fbi)
{
	unsigned long gsctrl;

	gsctrl = GSCTRL_GAMMA_EN | Gsctrl_Width(fbi->var.xres - 1) |
		Gsctrl_Height(fbi->var.yres - 1);
	switch (fbi->var.bits_per_pixel) {
	case 16:
		if (fbi->var.green.length == 5)
			gsctrl |= GSCTRL_GPIXFMT_ARGB1555;
		else
			gsctrl |= GSCTRL_GPIXFMT_RGB565;
		break;
	case 24:
		gsctrl |= GSCTRL_GPIXFMT_RGB888;
		break;
	case 32:
		gsctrl |= GSCTRL_GPIXFMT_ARGB8888;
		break;
	}

	writel(gsctrl, GSCTRL);
	udelay(1000);
	writel(0x00000000, GBBASE);
	udelay(1000);
	writel(0x00ffffff, GDRCTRL);
	udelay(1000);
	writel((GSCADR_STR_EN | Gscadr_Gbase_Adr(0x6000)), GSCADR);
	udelay(1000);
	writel(0x00000000, GPLUT);
	udelay(1000);
}

static void __devinit setup_display(struct fb_info *fbi)
{
	unsigned long dsctrl = 0;

	dsctrl = DSCTRL_BLNK_POL;
	if (fbi->var.sync & FB_SYNC_HOR_HIGH_ACT)
		dsctrl |= DSCTRL_HS_POL;
	if (fbi->var.sync & FB_SYNC_VERT_HIGH_ACT)
		dsctrl |= DSCTRL_VS_POL;
	writel(dsctrl, DSCTRL);
	udelay(1000);
	writel(0xd0303010, DMCTRL);
	udelay(1000);
	writel((readl(DSCTRL) | DSCTRL_SYNCGEN_EN), DSCTRL);
}

static void __devinit enable_controller(struct fb_info *fbi)
{
	writel(SYSRST_RST, SYSRST);
	udelay(1000);


	enable_clocks(fbi);
	setup_memc(fbi);
	setup_graphics(fbi);
	setup_display(fbi);
}

#ifdef CONFIG_PM
/*
 * Power management hooks.  Note that we won't be called from IRQ context,
 * unlike the blank functions above, so we may sleep.
 */
static int mbxfb_suspend(struct platform_device *dev, pm_message_t state)
{
	/* make frame buffer memory enter self-refresh mode */
	writel(LMPWR_MC_PWR_SRM, LMPWR);
	while (LMPWRSTAT != LMPWRSTAT_MC_PWR_SRM)
		; /* empty statement */

	/* reset the device, since it's initial state is 'mostly sleeping' */
	writel(SYSRST_RST, SYSRST);
	return 0;
}

static int mbxfb_resume(struct platform_device *dev)
{
	struct fb_info *fbi = platform_get_drvdata(dev);

	enable_clocks(fbi);
/* 	setup_graphics(fbi); */
/* 	setup_display(fbi); */

	writel((readl(DSCTRL) | DSCTRL_SYNCGEN_EN), DSCTRL);
	return 0;
}
#else
#define mbxfb_suspend	NULL
#define mbxfb_resume	NULL
#endif

/* debugfs entries */
#ifndef CONFIG_FB_MBX_DEBUG
#define mbxfb_debugfs_init(x)	do {} while(0)
#define mbxfb_debugfs_remove(x)	do {} while(0)
#endif

#define res_size(_r) (((_r)->end - (_r)->start) + 1)

static int __devinit mbxfb_probe(struct platform_device *dev)
{
	int ret;
	struct fb_info *fbi;
	struct mbxfb_info *mfbi;
	struct mbxfb_platform_data *pdata;

	dev_dbg(dev, "mbxfb_probe\n");

	fbi = framebuffer_alloc(sizeof(struct mbxfb_info), &dev->dev);
	if (fbi == NULL) {
		dev_err(&dev->dev, "framebuffer_alloc failed\n");
		return -ENOMEM;
	}

	mfbi = fbi->par;
	fbi->pseudo_palette = mfbi->pseudo_palette;
	pdata = dev->dev.platform_data;
	if (pdata->probe)
		mfbi->platform_probe = pdata->probe;
	if (pdata->remove)
		mfbi->platform_remove = pdata->remove;

	mfbi->fb_res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	mfbi->reg_res = platform_get_resource(dev, IORESOURCE_MEM, 1);

	if (!mfbi->fb_res || !mfbi->reg_res) {
		dev_err(&dev->dev, "no resources found\n");
		ret = -ENODEV;
		goto err1;
	}

	mfbi->fb_req = request_mem_region(mfbi->fb_res->start,
					  res_size(mfbi->fb_res), dev->name);
	if (mfbi->fb_req == NULL) {
		dev_err(&dev->dev, "failed to claim framebuffer memory\n");
		ret = -EINVAL;
		goto err1;
	}
	mfbi->fb_phys_addr = mfbi->fb_res->start;

	mfbi->reg_req = request_mem_region(mfbi->reg_res->start,
					   res_size(mfbi->reg_res), dev->name);
	if (mfbi->reg_req == NULL) {
		dev_err(&dev->dev, "failed to claim Marathon registers\n");
		ret = -EINVAL;
		goto err2;
	}
	mfbi->reg_phys_addr = mfbi->reg_res->start;

	mfbi->reg_virt_addr = ioremap_nocache(mfbi->reg_phys_addr,
					      res_size(mfbi->reg_req));
	if (!mfbi->reg_virt_addr) {
		dev_err(&dev->dev, "failed to ioremap Marathon registers\n");
		ret = -EINVAL;
		goto err3;
	}
	virt_base_2700 = (unsigned long)mfbi->reg_virt_addr;

	mfbi->fb_virt_addr = ioremap_nocache(mfbi->fb_phys_addr,
					     res_size(mfbi->fb_req));
	if (!mfbi->reg_virt_addr) {
		dev_err(&dev->dev, "failed to ioremap frame buffer\n");
		ret = -EINVAL;
		goto err4;
	}

	/* FIXME: get from platform */
	fbi->screen_base = (char __iomem *)(mfbi->fb_virt_addr + 0x60000);
	fbi->screen_size = 8 * 1024 * 1024;	/* 8 Megs */
	fbi->fbops = &mbxfb_ops;

	fbi->var = mbxfb_default;
	fbi->fix = mbxfb_fix;
	fbi->fix.smem_start = mfbi->fb_phys_addr + 0x60000;
	fbi->fix.smem_len = 8 * 1024 * 1024;
	fbi->fix.line_length = 640 * 2;

	ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (ret < 0) {
		dev_err(&dev->dev, "fb_alloc_cmap failed\n");
		ret = -EINVAL;
		goto err5;
	}

	platform_set_drvdata(dev, fbi);

	printk(KERN_INFO "fb%d: mbx frame buffer device\n", fbi->node);

	if (mfbi->platform_probe)
		mfbi->platform_probe(fbi);

	enable_controller(fbi);

	mbxfb_debugfs_init(fbi);

	ret = register_framebuffer(fbi);
	if (ret < 0) {
		dev_err(&dev->dev, "register_framebuffer failed\n");
		ret = -EINVAL;
		goto err6;
	}

	return 0;

err6:
	fb_dealloc_cmap(&fbi->cmap);
err5:
	iounmap(mfbi->fb_virt_addr);
err4:
	iounmap(mfbi->reg_virt_addr);
err3:
	release_mem_region(mfbi->reg_res->start, res_size(mfbi->reg_res));
err2:
	release_mem_region(mfbi->fb_res->start, res_size(mfbi->fb_res));
err1:
	framebuffer_release(fbi);

	return ret;
}

static int __devexit mbxfb_remove(struct platform_device *dev)
{
	struct fb_info *fbi = platform_get_drvdata(dev);

	writel(SYSRST_RST, SYSRST);
	udelay(1000);

	mbxfb_debugfs_remove(fbi);

	if (fbi) {
		struct mbxfb_info *mfbi = fbi->par;

		unregister_framebuffer(fbi);
		if (mfbi) {
			if (mfbi->platform_remove)
				mfbi->platform_remove(fbi);

			if (mfbi->fb_virt_addr)
				iounmap(mfbi->fb_virt_addr);
			if (mfbi->reg_virt_addr)
				iounmap(mfbi->reg_virt_addr);
			if (mfbi->reg_req)
				release_mem_region(mfbi->reg_req->start,
						   res_size(mfbi->reg_req));
			if (mfbi->fb_req)
				release_mem_region(mfbi->fb_req->start,
						   res_size(mfbi->fb_req));
		}
		framebuffer_release(fbi);
	}

	return 0;
}

static struct platform_driver mbxfb_driver = {
	.probe = mbxfb_probe,
	.remove = mbxfb_remove,
	.suspend = mbxfb_suspend,
	.resume = mbxfb_resume,
	.driver = {
		.name = "mbx-fb",
	},
};

int __devinit mbxfb_init(void)
{
	return platform_driver_register(&mbxfb_driver);
}

static void __devexit mbxfb_exit(void)
{
	platform_driver_unregister(&mbxfb_driver);
}

module_init(mbxfb_init);
module_exit(mbxfb_exit);

MODULE_DESCRIPTION("loadable framebuffer driver for Marathon device");
MODULE_AUTHOR("Mike Rapoport, Compulab");
MODULE_LICENSE("GPL");
