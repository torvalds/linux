/*
 *  Freescale i.MX Frame Buffer device driver
 *
 *  Copyright (C) 2004 Sascha Hauer, Pengutronix
 *   Based on acornfb.c Copyright (C) Russell King.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Please direct your questions and comments on this driver to the following
 * email address:
 *
 *	linux-arm-kernel@lists.arm.linux.org.uk
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/math64.h>

#include <mach/imxfb.h>
#include <mach/hardware.h>

/*
 * Complain if VAR is out of range.
 */
#define DEBUG_VAR 1

#define DRIVER_NAME "imx-fb"

#define LCDC_SSA	0x00

#define LCDC_SIZE	0x04
#define SIZE_XMAX(x)	((((x) >> 4) & 0x3f) << 20)

#ifdef CONFIG_ARCH_MX1
#define SIZE_YMAX(y)	((y) & 0x1ff)
#else
#define SIZE_YMAX(y)	((y) & 0x3ff)
#endif

#define LCDC_VPW	0x08
#define VPW_VPW(x)	((x) & 0x3ff)

#define LCDC_CPOS	0x0C
#define CPOS_CC1	(1<<31)
#define CPOS_CC0	(1<<30)
#define CPOS_OP		(1<<28)
#define CPOS_CXP(x)	(((x) & 3ff) << 16)

#ifdef CONFIG_ARCH_MX1
#define CPOS_CYP(y)	((y) & 0x1ff)
#else
#define CPOS_CYP(y)	((y) & 0x3ff)
#endif

#define LCDC_LCWHB	0x10
#define LCWHB_BK_EN	(1<<31)
#define LCWHB_CW(w)	(((w) & 0x1f) << 24)
#define LCWHB_CH(h)	(((h) & 0x1f) << 16)
#define LCWHB_BD(x)	((x) & 0xff)

#define LCDC_LCHCC	0x14

#ifdef CONFIG_ARCH_MX1
#define LCHCC_CUR_COL_R(r) (((r) & 0x1f) << 11)
#define LCHCC_CUR_COL_G(g) (((g) & 0x3f) << 5)
#define LCHCC_CUR_COL_B(b) ((b) & 0x1f)
#else
#define LCHCC_CUR_COL_R(r) (((r) & 0x3f) << 12)
#define LCHCC_CUR_COL_G(g) (((g) & 0x3f) << 6)
#define LCHCC_CUR_COL_B(b) ((b) & 0x3f)
#endif

#define LCDC_PCR	0x18

#define LCDC_HCR	0x1C
#define HCR_H_WIDTH(x)	(((x) & 0x3f) << 26)
#define HCR_H_WAIT_1(x)	(((x) & 0xff) << 8)
#define HCR_H_WAIT_2(x)	((x) & 0xff)

#define LCDC_VCR	0x20
#define VCR_V_WIDTH(x)	(((x) & 0x3f) << 26)
#define VCR_V_WAIT_1(x)	(((x) & 0xff) << 8)
#define VCR_V_WAIT_2(x)	((x) & 0xff)

#define LCDC_POS	0x24
#define POS_POS(x)	((x) & 1f)

#define LCDC_LSCR1	0x28
/* bit fields in imxfb.h */

#define LCDC_PWMR	0x2C
/* bit fields in imxfb.h */

#define LCDC_DMACR	0x30
/* bit fields in imxfb.h */

#define LCDC_RMCR	0x34

#ifdef CONFIG_ARCH_MX1
#define RMCR_LCDC_EN	(1<<1)
#else
#define RMCR_LCDC_EN	0
#endif

#define RMCR_SELF_REF	(1<<0)

#define LCDC_LCDICR	0x38
#define LCDICR_INT_SYN	(1<<2)
#define LCDICR_INT_CON	(1)

#define LCDC_LCDISR	0x40
#define LCDISR_UDR_ERR	(1<<3)
#define LCDISR_ERR_RES	(1<<2)
#define LCDISR_EOF	(1<<1)
#define LCDISR_BOF	(1<<0)

/* Used fb-mode. Can be set on kernel command line, therefore file-static. */
static const char *fb_mode;


/*
 * These are the bitfields for each
 * display depth that we support.
 */
struct imxfb_rgb {
	struct fb_bitfield	red;
	struct fb_bitfield	green;
	struct fb_bitfield	blue;
	struct fb_bitfield	transp;
};

struct imxfb_info {
	struct platform_device  *pdev;
	void __iomem		*regs;
	struct clk		*clk;

	/*
	 * These are the addresses we mapped
	 * the framebuffer memory region to.
	 */
	dma_addr_t		map_dma;
	u_char			*map_cpu;
	u_int			map_size;

	u_char			*screen_cpu;
	dma_addr_t		screen_dma;
	u_int			palette_size;

	dma_addr_t		dbar1;
	dma_addr_t		dbar2;

	u_int			pcr;
	u_int			pwmr;
	u_int			lscr1;
	u_int			dmacr;
	u_int			cmap_inverse:1,
				cmap_static:1,
				unused:30;

	struct imx_fb_videomode *mode;
	int			num_modes;

	void (*lcd_power)(int);
	void (*backlight_power)(int);
};

#define IMX_NAME	"IMX"

/*
 * Minimum X and Y resolutions
 */
#define MIN_XRES	64
#define MIN_YRES	64

/* Actually this really is 18bit support, the lowest 2 bits of each colour
 * are unused in hardware. We claim to have 24bit support to make software
 * like X work, which does not support 18bit.
 */
static struct imxfb_rgb def_rgb_18 = {
	.red	= {.offset = 16, .length = 8,},
	.green	= {.offset = 8, .length = 8,},
	.blue	= {.offset = 0, .length = 8,},
	.transp = {.offset = 0, .length = 0,},
};

static struct imxfb_rgb def_rgb_16_tft = {
	.red	= {.offset = 11, .length = 5,},
	.green	= {.offset = 5, .length = 6,},
	.blue	= {.offset = 0, .length = 5,},
	.transp = {.offset = 0, .length = 0,},
};

static struct imxfb_rgb def_rgb_16_stn = {
	.red	= {.offset = 8, .length = 4,},
	.green	= {.offset = 4, .length = 4,},
	.blue	= {.offset = 0, .length = 4,},
	.transp = {.offset = 0, .length = 0,},
};

static struct imxfb_rgb def_rgb_8 = {
	.red	= {.offset = 0, .length = 8,},
	.green	= {.offset = 0, .length = 8,},
	.blue	= {.offset = 0, .length = 8,},
	.transp = {.offset = 0, .length = 0,},
};

static int imxfb_activate_var(struct fb_var_screeninfo *var,
		struct fb_info *info);

static inline u_int chan_to_field(u_int chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int imxfb_setpalettereg(u_int regno, u_int red, u_int green, u_int blue,
		u_int trans, struct fb_info *info)
{
	struct imxfb_info *fbi = info->par;
	u_int val, ret = 1;

#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)
	if (regno < fbi->palette_size) {
		val = (CNVT_TOHW(red, 4) << 8) |
		      (CNVT_TOHW(green,4) << 4) |
		      CNVT_TOHW(blue,  4);

		writel(val, fbi->regs + 0x800 + (regno << 2));
		ret = 0;
	}
	return ret;
}

static int imxfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		   u_int trans, struct fb_info *info)
{
	struct imxfb_info *fbi = info->par;
	unsigned int val;
	int ret = 1;

	/*
	 * If inverse mode was selected, invert all the colours
	 * rather than the register number.  The register number
	 * is what you poke into the framebuffer to produce the
	 * colour you requested.
	 */
	if (fbi->cmap_inverse) {
		red   = 0xffff - red;
		green = 0xffff - green;
		blue  = 0xffff - blue;
	}

	/*
	 * If greyscale is true, then we convert the RGB value
	 * to greyscale no mater what visual we are using.
	 */
	if (info->var.grayscale)
		red = green = blue = (19595 * red + 38470 * green +
					7471 * blue) >> 16;

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/*
		 * 12 or 16-bit True Colour.  We encode the RGB value
		 * according to the RGB bitfield information.
		 */
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;

			val  = chan_to_field(red, &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue, &info->var.blue);

			pal[regno] = val;
			ret = 0;
		}
		break;

	case FB_VISUAL_STATIC_PSEUDOCOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		ret = imxfb_setpalettereg(regno, red, green, blue, trans, info);
		break;
	}

	return ret;
}

static const struct imx_fb_videomode *imxfb_find_mode(struct imxfb_info *fbi)
{
	struct imx_fb_videomode *m;
	int i;

	for (i = 0, m = &fbi->mode[0]; i < fbi->num_modes; i++, m++) {
		if (!strcmp(m->mode.name, fb_mode))
			return m;
	}
	return NULL;
}

/*
 *  imxfb_check_var():
 *    Round up in the following order: bits_per_pixel, xres,
 *    yres, xres_virtual, yres_virtual, xoffset, yoffset, grayscale,
 *    bitfields, horizontal timing, vertical timing.
 */
static int imxfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct imxfb_info *fbi = info->par;
	struct imxfb_rgb *rgb;
	const struct imx_fb_videomode *imxfb_mode;
	unsigned long lcd_clk;
	unsigned long long tmp;
	u32 pcr = 0;

	if (var->xres < MIN_XRES)
		var->xres = MIN_XRES;
	if (var->yres < MIN_YRES)
		var->yres = MIN_YRES;

	imxfb_mode = imxfb_find_mode(fbi);
	if (!imxfb_mode)
		return -EINVAL;

	var->xres		= imxfb_mode->mode.xres;
	var->yres		= imxfb_mode->mode.yres;
	var->bits_per_pixel	= imxfb_mode->bpp;
	var->pixclock		= imxfb_mode->mode.pixclock;
	var->hsync_len		= imxfb_mode->mode.hsync_len;
	var->left_margin	= imxfb_mode->mode.left_margin;
	var->right_margin	= imxfb_mode->mode.right_margin;
	var->vsync_len		= imxfb_mode->mode.vsync_len;
	var->upper_margin	= imxfb_mode->mode.upper_margin;
	var->lower_margin	= imxfb_mode->mode.lower_margin;
	var->sync		= imxfb_mode->mode.sync;
	var->xres_virtual	= max(var->xres_virtual, var->xres);
	var->yres_virtual	= max(var->yres_virtual, var->yres);

	pr_debug("var->bits_per_pixel=%d\n", var->bits_per_pixel);

	lcd_clk = clk_get_rate(fbi->clk);

	tmp = var->pixclock * (unsigned long long)lcd_clk;

	do_div(tmp, 1000000);

	if (do_div(tmp, 1000000) > 500000)
		tmp++;

	pcr = (unsigned int)tmp;

	if (--pcr > 0x3F) {
		pcr = 0x3F;
		printk(KERN_WARNING "Must limit pixel clock to %luHz\n",
				lcd_clk / pcr);
	}

	switch (var->bits_per_pixel) {
	case 32:
		pcr |= PCR_BPIX_18;
		rgb = &def_rgb_18;
		break;
	case 16:
	default:
		if (cpu_is_mx1())
			pcr |= PCR_BPIX_12;
		else
			pcr |= PCR_BPIX_16;

		if (imxfb_mode->pcr & PCR_TFT)
			rgb = &def_rgb_16_tft;
		else
			rgb = &def_rgb_16_stn;
		break;
	case 8:
		pcr |= PCR_BPIX_8;
		rgb = &def_rgb_8;
		break;
	}

	/* add sync polarities */
	pcr |= imxfb_mode->pcr & ~(0x3f | (7 << 25));

	fbi->pcr = pcr;

	/*
	 * Copy the RGB parameters for this display
	 * from the machine specific parameters.
	 */
	var->red    = rgb->red;
	var->green  = rgb->green;
	var->blue   = rgb->blue;
	var->transp = rgb->transp;

	pr_debug("RGBT length = %d:%d:%d:%d\n",
		var->red.length, var->green.length, var->blue.length,
		var->transp.length);

	pr_debug("RGBT offset = %d:%d:%d:%d\n",
		var->red.offset, var->green.offset, var->blue.offset,
		var->transp.offset);

	return 0;
}

/*
 * imxfb_set_par():
 *	Set the user defined part of the display for the specified console
 */
static int imxfb_set_par(struct fb_info *info)
{
	struct imxfb_info *fbi = info->par;
	struct fb_var_screeninfo *var = &info->var;

	if (var->bits_per_pixel == 16 || var->bits_per_pixel == 32)
		info->fix.visual = FB_VISUAL_TRUECOLOR;
	else if (!fbi->cmap_static)
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	else {
		/*
		 * Some people have weird ideas about wanting static
		 * pseudocolor maps.  I suspect their user space
		 * applications are broken.
		 */
		info->fix.visual = FB_VISUAL_STATIC_PSEUDOCOLOR;
	}

	info->fix.line_length = var->xres_virtual * var->bits_per_pixel / 8;
	fbi->palette_size = var->bits_per_pixel == 8 ? 256 : 16;

	imxfb_activate_var(var, info);

	return 0;
}

static void imxfb_enable_controller(struct imxfb_info *fbi)
{
	pr_debug("Enabling LCD controller\n");

	writel(fbi->screen_dma, fbi->regs + LCDC_SSA);

	/* panning offset 0 (0 pixel offset)        */
	writel(0x00000000, fbi->regs + LCDC_POS);

	/* disable hardware cursor */
	writel(readl(fbi->regs + LCDC_CPOS) & ~(CPOS_CC0 | CPOS_CC1),
		fbi->regs + LCDC_CPOS);

	writel(RMCR_LCDC_EN, fbi->regs + LCDC_RMCR);

	clk_enable(fbi->clk);

	if (fbi->backlight_power)
		fbi->backlight_power(1);
	if (fbi->lcd_power)
		fbi->lcd_power(1);
}

static void imxfb_disable_controller(struct imxfb_info *fbi)
{
	pr_debug("Disabling LCD controller\n");

	if (fbi->backlight_power)
		fbi->backlight_power(0);
	if (fbi->lcd_power)
		fbi->lcd_power(0);

	clk_disable(fbi->clk);

	writel(0, fbi->regs + LCDC_RMCR);
}

static int imxfb_blank(int blank, struct fb_info *info)
{
	struct imxfb_info *fbi = info->par;

	pr_debug("imxfb_blank: blank=%d\n", blank);

	switch (blank) {
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_NORMAL:
		imxfb_disable_controller(fbi);
		break;

	case FB_BLANK_UNBLANK:
		imxfb_enable_controller(fbi);
		break;
	}
	return 0;
}

static struct fb_ops imxfb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= imxfb_check_var,
	.fb_set_par	= imxfb_set_par,
	.fb_setcolreg	= imxfb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_blank	= imxfb_blank,
};

/*
 * imxfb_activate_var():
 *	Configures LCD Controller based on entries in var parameter.  Settings are
 *	only written to the controller if changes were made.
 */
static int imxfb_activate_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct imxfb_info *fbi = info->par;

	pr_debug("var: xres=%d hslen=%d lm=%d rm=%d\n",
		var->xres, var->hsync_len,
		var->left_margin, var->right_margin);
	pr_debug("var: yres=%d vslen=%d um=%d bm=%d\n",
		var->yres, var->vsync_len,
		var->upper_margin, var->lower_margin);

#if DEBUG_VAR
	if (var->xres < 16        || var->xres > 1024)
		printk(KERN_ERR "%s: invalid xres %d\n",
			info->fix.id, var->xres);
	if (var->hsync_len < 1    || var->hsync_len > 64)
		printk(KERN_ERR "%s: invalid hsync_len %d\n",
			info->fix.id, var->hsync_len);
	if (var->left_margin > 255)
		printk(KERN_ERR "%s: invalid left_margin %d\n",
			info->fix.id, var->left_margin);
	if (var->right_margin > 255)
		printk(KERN_ERR "%s: invalid right_margin %d\n",
			info->fix.id, var->right_margin);
	if (var->yres < 1 || var->yres > 511)
		printk(KERN_ERR "%s: invalid yres %d\n",
			info->fix.id, var->yres);
	if (var->vsync_len > 100)
		printk(KERN_ERR "%s: invalid vsync_len %d\n",
			info->fix.id, var->vsync_len);
	if (var->upper_margin > 63)
		printk(KERN_ERR "%s: invalid upper_margin %d\n",
			info->fix.id, var->upper_margin);
	if (var->lower_margin > 255)
		printk(KERN_ERR "%s: invalid lower_margin %d\n",
			info->fix.id, var->lower_margin);
#endif

	/* physical screen start address	    */
	writel(VPW_VPW(var->xres * var->bits_per_pixel / 8 / 4),
		fbi->regs + LCDC_VPW);

	writel(HCR_H_WIDTH(var->hsync_len - 1) |
		HCR_H_WAIT_1(var->right_margin - 1) |
		HCR_H_WAIT_2(var->left_margin - 3),
		fbi->regs + LCDC_HCR);

	writel(VCR_V_WIDTH(var->vsync_len) |
		VCR_V_WAIT_1(var->lower_margin) |
		VCR_V_WAIT_2(var->upper_margin),
		fbi->regs + LCDC_VCR);

	writel(SIZE_XMAX(var->xres) | SIZE_YMAX(var->yres),
			fbi->regs + LCDC_SIZE);

	writel(fbi->pcr, fbi->regs + LCDC_PCR);
	writel(fbi->pwmr, fbi->regs + LCDC_PWMR);
	writel(fbi->lscr1, fbi->regs + LCDC_LSCR1);
	writel(fbi->dmacr, fbi->regs + LCDC_DMACR);

	return 0;
}

#ifdef CONFIG_PM
/*
 * Power management hooks.  Note that we won't be called from IRQ context,
 * unlike the blank functions above, so we may sleep.
 */
static int imxfb_suspend(struct platform_device *dev, pm_message_t state)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct imxfb_info *fbi = info->par;

	pr_debug("%s\n", __func__);

	imxfb_disable_controller(fbi);
	return 0;
}

static int imxfb_resume(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct imxfb_info *fbi = info->par;

	pr_debug("%s\n", __func__);

	imxfb_enable_controller(fbi);
	return 0;
}
#else
#define imxfb_suspend	NULL
#define imxfb_resume	NULL
#endif

static int __init imxfb_init_fbinfo(struct platform_device *pdev)
{
	struct imx_fb_platform_data *pdata = pdev->dev.platform_data;
	struct fb_info *info = dev_get_drvdata(&pdev->dev);
	struct imxfb_info *fbi = info->par;
	struct imx_fb_videomode *m;
	int i;

	pr_debug("%s\n",__func__);

	info->pseudo_palette = kmalloc(sizeof(u32) * 16, GFP_KERNEL);
	if (!info->pseudo_palette)
		return -ENOMEM;

	memset(fbi, 0, sizeof(struct imxfb_info));

	strlcpy(info->fix.id, IMX_NAME, sizeof(info->fix.id));

	info->fix.type			= FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux		= 0;
	info->fix.xpanstep		= 0;
	info->fix.ypanstep		= 0;
	info->fix.ywrapstep		= 0;
	info->fix.accel			= FB_ACCEL_NONE;

	info->var.nonstd		= 0;
	info->var.activate		= FB_ACTIVATE_NOW;
	info->var.height		= -1;
	info->var.width	= -1;
	info->var.accel_flags		= 0;
	info->var.vmode			= FB_VMODE_NONINTERLACED;

	info->fbops			= &imxfb_ops;
	info->flags			= FBINFO_FLAG_DEFAULT |
					  FBINFO_READS_FAST;
	info->var.grayscale		= pdata->cmap_greyscale;
	fbi->cmap_inverse		= pdata->cmap_inverse;
	fbi->cmap_static		= pdata->cmap_static;
	fbi->lscr1			= pdata->lscr1;
	fbi->dmacr			= pdata->dmacr;
	fbi->pwmr			= pdata->pwmr;
	fbi->lcd_power			= pdata->lcd_power;
	fbi->backlight_power		= pdata->backlight_power;

	for (i = 0, m = &pdata->mode[0]; i < pdata->num_modes; i++, m++)
		info->fix.smem_len = max_t(size_t, info->fix.smem_len,
				m->mode.xres * m->mode.yres * m->bpp / 8);

	return 0;
}

static int __init imxfb_probe(struct platform_device *pdev)
{
	struct imxfb_info *fbi;
	struct fb_info *info;
	struct imx_fb_platform_data *pdata;
	struct resource *res;
	int ret, i;

	dev_info(&pdev->dev, "i.MX Framebuffer driver\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev,"No platform_data available\n");
		return -ENOMEM;
	}

	info = framebuffer_alloc(sizeof(struct imxfb_info), &pdev->dev);
	if (!info)
		return -ENOMEM;

	fbi = info->par;

	if (!fb_mode)
		fb_mode = pdata->mode[0].mode.name;

	platform_set_drvdata(pdev, info);

	ret = imxfb_init_fbinfo(pdev);
	if (ret < 0)
		goto failed_init;

	res = request_mem_region(res->start, resource_size(res),
				DRIVER_NAME);
	if (!res) {
		ret = -EBUSY;
		goto failed_req;
	}

	fbi->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(fbi->clk)) {
		ret = PTR_ERR(fbi->clk);
		dev_err(&pdev->dev, "unable to get clock: %d\n", ret);
		goto failed_getclock;
	}

	fbi->regs = ioremap(res->start, resource_size(res));
	if (fbi->regs == NULL) {
		dev_err(&pdev->dev, "Cannot map frame buffer registers\n");
		goto failed_ioremap;
	}

	if (!pdata->fixed_screen_cpu) {
		fbi->map_size = PAGE_ALIGN(info->fix.smem_len);
		fbi->map_cpu = dma_alloc_writecombine(&pdev->dev,
				fbi->map_size, &fbi->map_dma, GFP_KERNEL);

		if (!fbi->map_cpu) {
			dev_err(&pdev->dev, "Failed to allocate video RAM: %d\n", ret);
			ret = -ENOMEM;
			goto failed_map;
		}

		info->screen_base = fbi->map_cpu;
		fbi->screen_cpu = fbi->map_cpu;
		fbi->screen_dma = fbi->map_dma;
		info->fix.smem_start = fbi->screen_dma;
	} else {
		/* Fixed framebuffer mapping enables location of the screen in eSRAM */
		fbi->map_cpu = pdata->fixed_screen_cpu;
		fbi->map_dma = pdata->fixed_screen_dma;
		info->screen_base = fbi->map_cpu;
		fbi->screen_cpu = fbi->map_cpu;
		fbi->screen_dma = fbi->map_dma;
		info->fix.smem_start = fbi->screen_dma;
	}

	if (pdata->init) {
		ret = pdata->init(fbi->pdev);
		if (ret)
			goto failed_platform_init;
	}

	fbi->mode = pdata->mode;
	fbi->num_modes = pdata->num_modes;

	INIT_LIST_HEAD(&info->modelist);
	for (i = 0; i < pdata->num_modes; i++)
		fb_add_videomode(&pdata->mode[i].mode, &info->modelist);

	/*
	 * This makes sure that our colour bitfield
	 * descriptors are correctly initialised.
	 */
	imxfb_check_var(&info->var, info);

	ret = fb_alloc_cmap(&info->cmap, 1 << info->var.bits_per_pixel, 0);
	if (ret < 0)
		goto failed_cmap;

	imxfb_set_par(info);
	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register framebuffer\n");
		goto failed_register;
	}

	imxfb_enable_controller(fbi);

	return 0;

failed_register:
	fb_dealloc_cmap(&info->cmap);
failed_cmap:
	if (pdata->exit)
		pdata->exit(fbi->pdev);
failed_platform_init:
	if (!pdata->fixed_screen_cpu)
		dma_free_writecombine(&pdev->dev,fbi->map_size,fbi->map_cpu,
			fbi->map_dma);
failed_map:
	clk_put(fbi->clk);
failed_getclock:
	iounmap(fbi->regs);
failed_ioremap:
	release_mem_region(res->start, resource_size(res));
failed_req:
	kfree(info->pseudo_palette);
failed_init:
	platform_set_drvdata(pdev, NULL);
	framebuffer_release(info);
	return ret;
}

static int __devexit imxfb_remove(struct platform_device *pdev)
{
	struct imx_fb_platform_data *pdata;
	struct fb_info *info = platform_get_drvdata(pdev);
	struct imxfb_info *fbi = info->par;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	imxfb_disable_controller(fbi);

	unregister_framebuffer(info);

	pdata = pdev->dev.platform_data;
	if (pdata->exit)
		pdata->exit(fbi->pdev);

	fb_dealloc_cmap(&info->cmap);
	kfree(info->pseudo_palette);
	framebuffer_release(info);

	iounmap(fbi->regs);
	release_mem_region(res->start, resource_size(res));
	clk_disable(fbi->clk);
	clk_put(fbi->clk);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

void  imxfb_shutdown(struct platform_device * dev)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct imxfb_info *fbi = info->par;
	imxfb_disable_controller(fbi);
}

static struct platform_driver imxfb_driver = {
	.suspend	= imxfb_suspend,
	.resume		= imxfb_resume,
	.remove		= __devexit_p(imxfb_remove),
	.shutdown	= imxfb_shutdown,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

static int imxfb_setup(void)
{
#ifndef MODULE
	char *opt, *options = NULL;

	if (fb_get_options("imxfb", &options))
		return -ENODEV;

	if (!options || !*options)
		return 0;

	while ((opt = strsep(&options, ",")) != NULL) {
		if (!*opt)
			continue;
		else
			fb_mode = opt;
	}
#endif
	return 0;
}

int __init imxfb_init(void)
{
	int ret = imxfb_setup();

	if (ret < 0)
		return ret;

	return platform_driver_probe(&imxfb_driver, imxfb_probe);
}

static void __exit imxfb_cleanup(void)
{
	platform_driver_unregister(&imxfb_driver);
}

module_init(imxfb_init);
module_exit(imxfb_cleanup);

MODULE_DESCRIPTION("Motorola i.MX framebuffer driver");
MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_LICENSE("GPL");
