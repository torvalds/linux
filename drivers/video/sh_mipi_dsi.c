/*
 * Renesas SH-mobile MIPI DSI support
 *
 * Copyright (C) 2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/module.h>

#include <video/mipi_display.h>
#include <video/sh_mipi_dsi.h>
#include <video/sh_mobile_lcdc.h>

#define SYSCTRL		0x0000
#define SYSCONF		0x0004
#define TIMSET		0x0008
#define RESREQSET0	0x0018
#define RESREQSET1	0x001c
#define HSTTOVSET	0x0020
#define LPRTOVSET	0x0024
#define TATOVSET	0x0028
#define PRTOVSET	0x002c
#define DSICTRL		0x0030
#define DSIINTE		0x0060
#define PHYCTRL		0x0070

/* relative to linkbase */
#define DTCTR		0x0000
#define VMCTR1		0x0020
#define VMCTR2		0x0024
#define VMLEN1		0x0028
#define CMTSRTREQ	0x0070
#define CMTSRTCTR	0x00d0

/* E.g., sh7372 has 2 MIPI-DSIs - one for each LCDC */
#define MAX_SH_MIPI_DSI 2

struct sh_mipi {
	void __iomem	*base;
	void __iomem	*linkbase;
	struct clk	*dsit_clk;
	struct clk	*dsip_clk;
	struct device	*dev;

	void	*next_board_data;
	void	(*next_display_on)(void *board_data, struct fb_info *info);
	void	(*next_display_off)(void *board_data);
};

static struct sh_mipi *mipi_dsi[MAX_SH_MIPI_DSI];

/* Protect the above array */
static DEFINE_MUTEX(array_lock);

static struct sh_mipi *sh_mipi_by_handle(int handle)
{
	if (handle >= ARRAY_SIZE(mipi_dsi) || handle < 0)
		return NULL;

	return mipi_dsi[handle];
}

static int sh_mipi_send_short(struct sh_mipi *mipi, u8 dsi_cmd,
			      u8 cmd, u8 param)
{
	u32 data = (dsi_cmd << 24) | (cmd << 16) | (param << 8);
	int cnt = 100;

	/* transmit a short packet to LCD panel */
	iowrite32(1 | data, mipi->linkbase + CMTSRTCTR);
	iowrite32(1, mipi->linkbase + CMTSRTREQ);

	while ((ioread32(mipi->linkbase + CMTSRTREQ) & 1) && --cnt)
		udelay(1);

	return cnt ? 0 : -ETIMEDOUT;
}

#define LCD_CHAN2MIPI(c) ((c) < LCDC_CHAN_MAINLCD || (c) > LCDC_CHAN_SUBLCD ? \
				-EINVAL : (c) - 1)

static int sh_mipi_dcs(int handle, u8 cmd)
{
	struct sh_mipi *mipi = sh_mipi_by_handle(LCD_CHAN2MIPI(handle));
	if (!mipi)
		return -ENODEV;
	return sh_mipi_send_short(mipi, MIPI_DSI_DCS_SHORT_WRITE, cmd, 0);
}

static int sh_mipi_dcs_param(int handle, u8 cmd, u8 param)
{
	struct sh_mipi *mipi = sh_mipi_by_handle(LCD_CHAN2MIPI(handle));
	if (!mipi)
		return -ENODEV;
	return sh_mipi_send_short(mipi, MIPI_DSI_DCS_SHORT_WRITE_PARAM, cmd,
				  param);
}

static void sh_mipi_dsi_enable(struct sh_mipi *mipi, bool enable)
{
	/*
	 * enable LCDC data tx, transition to LPS after completion of each HS
	 * packet
	 */
	iowrite32(0x00000002 | enable, mipi->linkbase + DTCTR);
}

static void sh_mipi_shutdown(struct platform_device *pdev)
{
	struct sh_mipi *mipi = platform_get_drvdata(pdev);

	sh_mipi_dsi_enable(mipi, false);
}

static void mipi_display_on(void *arg, struct fb_info *info)
{
	struct sh_mipi *mipi = arg;

	pm_runtime_get_sync(mipi->dev);
	sh_mipi_dsi_enable(mipi, true);

	if (mipi->next_display_on)
		mipi->next_display_on(mipi->next_board_data, info);
}

static void mipi_display_off(void *arg)
{
	struct sh_mipi *mipi = arg;

	if (mipi->next_display_off)
		mipi->next_display_off(mipi->next_board_data);

	sh_mipi_dsi_enable(mipi, false);
	pm_runtime_put(mipi->dev);
}

static int __init sh_mipi_setup(struct sh_mipi *mipi,
				struct sh_mipi_dsi_info *pdata)
{
	void __iomem *base = mipi->base;
	struct sh_mobile_lcdc_chan_cfg *ch = pdata->lcd_chan;
	u32 pctype, datatype, pixfmt, linelength, vmctr2 = 0x00e00000;
	bool yuv;

	/*
	 * Select data format. MIPI DSI is not hot-pluggable, so, we just use
	 * the default videomode. If this ever becomes a problem, We'll have to
	 * move this to mipi_display_on() above and use info->var.xres
	 */
	switch (pdata->data_format) {
	case MIPI_RGB888:
		pctype = 0;
		datatype = MIPI_DSI_PACKED_PIXEL_STREAM_24;
		pixfmt = MIPI_DCS_PIXEL_FMT_24BIT;
		linelength = ch->lcd_cfg[0].xres * 3;
		yuv = false;
		break;
	case MIPI_RGB565:
		pctype = 1;
		datatype = MIPI_DSI_PACKED_PIXEL_STREAM_16;
		pixfmt = MIPI_DCS_PIXEL_FMT_16BIT;
		linelength = ch->lcd_cfg[0].xres * 2;
		yuv = false;
		break;
	case MIPI_RGB666_LP:
		pctype = 2;
		datatype = MIPI_DSI_PIXEL_STREAM_3BYTE_18;
		pixfmt = MIPI_DCS_PIXEL_FMT_24BIT;
		linelength = ch->lcd_cfg[0].xres * 3;
		yuv = false;
		break;
	case MIPI_RGB666:
		pctype = 3;
		datatype = MIPI_DSI_PACKED_PIXEL_STREAM_18;
		pixfmt = MIPI_DCS_PIXEL_FMT_18BIT;
		linelength = (ch->lcd_cfg[0].xres * 18 + 7) / 8;
		yuv = false;
		break;
	case MIPI_BGR888:
		pctype = 8;
		datatype = MIPI_DSI_PACKED_PIXEL_STREAM_24;
		pixfmt = MIPI_DCS_PIXEL_FMT_24BIT;
		linelength = ch->lcd_cfg[0].xres * 3;
		yuv = false;
		break;
	case MIPI_BGR565:
		pctype = 9;
		datatype = MIPI_DSI_PACKED_PIXEL_STREAM_16;
		pixfmt = MIPI_DCS_PIXEL_FMT_16BIT;
		linelength = ch->lcd_cfg[0].xres * 2;
		yuv = false;
		break;
	case MIPI_BGR666_LP:
		pctype = 0xa;
		datatype = MIPI_DSI_PIXEL_STREAM_3BYTE_18;
		pixfmt = MIPI_DCS_PIXEL_FMT_24BIT;
		linelength = ch->lcd_cfg[0].xres * 3;
		yuv = false;
		break;
	case MIPI_BGR666:
		pctype = 0xb;
		datatype = MIPI_DSI_PACKED_PIXEL_STREAM_18;
		pixfmt = MIPI_DCS_PIXEL_FMT_18BIT;
		linelength = (ch->lcd_cfg[0].xres * 18 + 7) / 8;
		yuv = false;
		break;
	case MIPI_YUYV:
		pctype = 4;
		datatype = MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR16;
		pixfmt = MIPI_DCS_PIXEL_FMT_16BIT;
		linelength = ch->lcd_cfg[0].xres * 2;
		yuv = true;
		break;
	case MIPI_UYVY:
		pctype = 5;
		datatype = MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR16;
		pixfmt = MIPI_DCS_PIXEL_FMT_16BIT;
		linelength = ch->lcd_cfg[0].xres * 2;
		yuv = true;
		break;
	case MIPI_YUV420_L:
		pctype = 6;
		datatype = MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR12;
		pixfmt = MIPI_DCS_PIXEL_FMT_12BIT;
		linelength = (ch->lcd_cfg[0].xres * 12 + 7) / 8;
		yuv = true;
		break;
	case MIPI_YUV420:
		pctype = 7;
		datatype = MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR12;
		pixfmt = MIPI_DCS_PIXEL_FMT_12BIT;
		/* Length of U/V line */
		linelength = (ch->lcd_cfg[0].xres + 1) / 2;
		yuv = true;
		break;
	default:
		return -EINVAL;
	}

	if ((yuv && ch->interface_type != YUV422) ||
	    (!yuv && ch->interface_type != RGB24))
		return -EINVAL;

	/* reset DSI link */
	iowrite32(0x00000001, base + SYSCTRL);
	/* Hold reset for 100 cycles of the slowest of bus, HS byte and LP clock */
	udelay(50);
	iowrite32(0x00000000, base + SYSCTRL);

	/* setup DSI link */

	/*
	 * Default = ULPS enable |
	 *	Contention detection enabled |
	 *	EoT packet transmission enable |
	 *	CRC check enable |
	 *	ECC check enable
	 * additionally enable first two lanes
	 */
	iowrite32(0x00003703, base + SYSCONF);
	/*
	 * T_wakeup = 0x7000
	 * T_hs-trail = 3
	 * T_hs-prepare = 3
	 * T_clk-trail = 3
	 * T_clk-prepare = 2
	 */
	iowrite32(0x70003332, base + TIMSET);
	/* no responses requested */
	iowrite32(0x00000000, base + RESREQSET0);
	/* request response to packets of type 0x28 */
	iowrite32(0x00000100, base + RESREQSET1);
	/* High-speed transmission timeout, default 0xffffffff */
	iowrite32(0x0fffffff, base + HSTTOVSET);
	/* LP reception timeout, default 0xffffffff */
	iowrite32(0x0fffffff, base + LPRTOVSET);
	/* Turn-around timeout, default 0xffffffff */
	iowrite32(0x0fffffff, base + TATOVSET);
	/* Peripheral reset timeout, default 0xffffffff */
	iowrite32(0x0fffffff, base + PRTOVSET);
	/* Enable timeout counters */
	iowrite32(0x00000f00, base + DSICTRL);
	/* Interrupts not used, disable all */
	iowrite32(0, base + DSIINTE);
	/* DSI-Tx bias on */
	iowrite32(0x00000001, base + PHYCTRL);
	udelay(200);
	/* Deassert resets, power on, set multiplier */
	iowrite32(0x03070b01, base + PHYCTRL);

	/* setup l-bridge */

	/*
	 * Enable transmission of all packets,
	 * transmit LPS after each HS packet completion
	 */
	iowrite32(0x00000006, mipi->linkbase + DTCTR);
	/* VSYNC width = 2 (<< 17) */
	iowrite32((ch->lcd_cfg[0].vsync_len << pdata->vsynw_offset) |
		  (pdata->clksrc << 16) | (pctype << 12) | datatype,
		  mipi->linkbase + VMCTR1);

	/*
	 * Non-burst mode with sync pulses: VSE and HSE are output,
	 * HSA period allowed, no commands in LP
	 */
	if (pdata->flags & SH_MIPI_DSI_HSABM)
		vmctr2 |= 0x20;
	if (pdata->flags & SH_MIPI_DSI_HSPBM)
		vmctr2 |= 0x10;
	iowrite32(vmctr2, mipi->linkbase + VMCTR2);

	/*
	 * 0x660 = 1632 bytes per line (RGB24, 544 pixels: see
	 * sh_mobile_lcdc_info.ch[0].lcd_cfg[0].xres), HSALEN = 1 - default
	 * (unused if VMCTR2[HSABM] = 0)
	 */
	iowrite32(1 | (linelength << 16), mipi->linkbase + VMLEN1);

	msleep(5);

	/* setup LCD panel */

	/* cf. drivers/video/omap/lcd_mipid.c */
	sh_mipi_dcs(ch->chan, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(120);
	/*
	 * [7] - Page Address Mode
	 * [6] - Column Address Mode
	 * [5] - Page / Column Address Mode
	 * [4] - Display Device Line Refresh Order
	 * [3] - RGB/BGR Order
	 * [2] - Display Data Latch Data Order
	 * [1] - Flip Horizontal
	 * [0] - Flip Vertical
	 */
	sh_mipi_dcs_param(ch->chan, MIPI_DCS_SET_ADDRESS_MODE, 0x00);
	/* cf. set_data_lines() */
	sh_mipi_dcs_param(ch->chan, MIPI_DCS_SET_PIXEL_FORMAT,
			  pixfmt << 4);
	sh_mipi_dcs(ch->chan, MIPI_DCS_SET_DISPLAY_ON);

	return 0;
}

static int __init sh_mipi_probe(struct platform_device *pdev)
{
	struct sh_mipi *mipi;
	struct sh_mipi_dsi_info *pdata = pdev->dev.platform_data;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct resource *res2 = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	unsigned long rate, f_current;
	int idx = pdev->id, ret;
	char dsip_clk[] = "dsi.p_clk";

	if (!res || !res2 || idx >= ARRAY_SIZE(mipi_dsi) || !pdata)
		return -ENODEV;

	mutex_lock(&array_lock);
	if (idx < 0)
		for (idx = 0; idx < ARRAY_SIZE(mipi_dsi) && mipi_dsi[idx]; idx++)
			;

	if (idx == ARRAY_SIZE(mipi_dsi)) {
		ret = -EBUSY;
		goto efindslot;
	}

	mipi = kzalloc(sizeof(*mipi), GFP_KERNEL);
	if (!mipi) {
		ret = -ENOMEM;
		goto ealloc;
	}

	if (!request_mem_region(res->start, resource_size(res), pdev->name)) {
		dev_err(&pdev->dev, "MIPI register region already claimed\n");
		ret = -EBUSY;
		goto ereqreg;
	}

	mipi->base = ioremap(res->start, resource_size(res));
	if (!mipi->base) {
		ret = -ENOMEM;
		goto emap;
	}

	if (!request_mem_region(res2->start, resource_size(res2), pdev->name)) {
		dev_err(&pdev->dev, "MIPI register region 2 already claimed\n");
		ret = -EBUSY;
		goto ereqreg2;
	}

	mipi->linkbase = ioremap(res2->start, resource_size(res2));
	if (!mipi->linkbase) {
		ret = -ENOMEM;
		goto emap2;
	}

	mipi->dev = &pdev->dev;

	mipi->dsit_clk = clk_get(&pdev->dev, "dsit_clk");
	if (IS_ERR(mipi->dsit_clk)) {
		ret = PTR_ERR(mipi->dsit_clk);
		goto eclktget;
	}

	f_current = clk_get_rate(mipi->dsit_clk);
	/* 80MHz required by the datasheet */
	rate = clk_round_rate(mipi->dsit_clk, 80000000);
	if (rate > 0 && rate != f_current)
		ret = clk_set_rate(mipi->dsit_clk, rate);
	else
		ret = rate;
	if (ret < 0)
		goto esettrate;

	dev_dbg(&pdev->dev, "DSI-T clk %lu -> %lu\n", f_current, rate);

	sprintf(dsip_clk, "dsi%1.1dp_clk", idx);
	mipi->dsip_clk = clk_get(&pdev->dev, dsip_clk);
	if (IS_ERR(mipi->dsip_clk)) {
		ret = PTR_ERR(mipi->dsip_clk);
		goto eclkpget;
	}

	f_current = clk_get_rate(mipi->dsip_clk);
	/* Between 10 and 50MHz */
	rate = clk_round_rate(mipi->dsip_clk, 24000000);
	if (rate > 0 && rate != f_current)
		ret = clk_set_rate(mipi->dsip_clk, rate);
	else
		ret = rate;
	if (ret < 0)
		goto esetprate;

	dev_dbg(&pdev->dev, "DSI-P clk %lu -> %lu\n", f_current, rate);

	msleep(10);

	ret = clk_enable(mipi->dsit_clk);
	if (ret < 0)
		goto eclkton;

	ret = clk_enable(mipi->dsip_clk);
	if (ret < 0)
		goto eclkpon;

	mipi_dsi[idx] = mipi;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_resume(&pdev->dev);

	ret = sh_mipi_setup(mipi, pdata);
	if (ret < 0)
		goto emipisetup;

	mutex_unlock(&array_lock);
	platform_set_drvdata(pdev, mipi);

	/* Save original LCDC callbacks */
	mipi->next_board_data = pdata->lcd_chan->board_cfg.board_data;
	mipi->next_display_on = pdata->lcd_chan->board_cfg.display_on;
	mipi->next_display_off = pdata->lcd_chan->board_cfg.display_off;

	/* Set up LCDC callbacks */
	pdata->lcd_chan->board_cfg.board_data = mipi;
	pdata->lcd_chan->board_cfg.display_on = mipi_display_on;
	pdata->lcd_chan->board_cfg.display_off = mipi_display_off;
	pdata->lcd_chan->board_cfg.owner = THIS_MODULE;

	return 0;

emipisetup:
	mipi_dsi[idx] = NULL;
	pm_runtime_disable(&pdev->dev);
	clk_disable(mipi->dsip_clk);
eclkpon:
	clk_disable(mipi->dsit_clk);
eclkton:
esetprate:
	clk_put(mipi->dsip_clk);
eclkpget:
esettrate:
	clk_put(mipi->dsit_clk);
eclktget:
	iounmap(mipi->linkbase);
emap2:
	release_mem_region(res2->start, resource_size(res2));
ereqreg2:
	iounmap(mipi->base);
emap:
	release_mem_region(res->start, resource_size(res));
ereqreg:
	kfree(mipi);
ealloc:
efindslot:
	mutex_unlock(&array_lock);

	return ret;
}

static int __exit sh_mipi_remove(struct platform_device *pdev)
{
	struct sh_mipi_dsi_info *pdata = pdev->dev.platform_data;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct resource *res2 = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	struct sh_mipi *mipi = platform_get_drvdata(pdev);
	int i, ret;

	mutex_lock(&array_lock);

	for (i = 0; i < ARRAY_SIZE(mipi_dsi) && mipi_dsi[i] != mipi; i++)
		;

	if (i == ARRAY_SIZE(mipi_dsi)) {
		ret = -EINVAL;
	} else {
		ret = 0;
		mipi_dsi[i] = NULL;
	}

	mutex_unlock(&array_lock);

	if (ret < 0)
		return ret;

	pdata->lcd_chan->board_cfg.owner = NULL;
	pdata->lcd_chan->board_cfg.display_on = NULL;
	pdata->lcd_chan->board_cfg.display_off = NULL;
	pdata->lcd_chan->board_cfg.board_data = NULL;

	pm_runtime_disable(&pdev->dev);
	clk_disable(mipi->dsip_clk);
	clk_disable(mipi->dsit_clk);
	clk_put(mipi->dsit_clk);
	clk_put(mipi->dsip_clk);
	iounmap(mipi->linkbase);
	if (res2)
		release_mem_region(res2->start, resource_size(res2));
	iounmap(mipi->base);
	if (res)
		release_mem_region(res->start, resource_size(res));
	platform_set_drvdata(pdev, NULL);
	kfree(mipi);

	return 0;
}

static struct platform_driver sh_mipi_driver = {
	.remove		= __exit_p(sh_mipi_remove),
	.shutdown	= sh_mipi_shutdown,
	.driver = {
		.name	= "sh-mipi-dsi",
	},
};

static int __init sh_mipi_init(void)
{
	return platform_driver_probe(&sh_mipi_driver, sh_mipi_probe);
}
module_init(sh_mipi_init);

static void __exit sh_mipi_exit(void)
{
	platform_driver_unregister(&sh_mipi_driver);
}
module_exit(sh_mipi_exit);

MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_DESCRIPTION("SuperH / ARM-shmobile MIPI DSI driver");
MODULE_LICENSE("GPL v2");
