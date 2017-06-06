/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rockchip/cpu.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>
#include <linux/phy/phy.h>

#include <drm/drm_of.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder_slave.h>
#include <drm/bridge/dw_hdmi.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define RK3228_GRF_SOC_CON2		0x0408
#define RK3228_DDC_MASK_EN		((3 << 13) | (3 << (13 + 16)))
#define RK3228_GRF_SOC_CON6		0x0418
#define RK3228_IO_3V_DOMAIN		((7 << 4) | (7 << (4 + 16)))

#define RK3288_GRF_SOC_CON6		0x025C
#define RK3288_HDMI_LCDC_SEL		BIT(4)
#define RK3399_GRF_SOC_CON20		0x6250
#define RK3399_HDMI_LCDC_SEL		BIT(6)

#define RK3328_GRF_SOC_CON2		0x0408
#define RK3328_DDC_MASK_EN		((3 << 10) | (3 << (10 + 16)))
#define RK3328_GRF_SOC_CON3		0x040c
#define RK3328_IO_CTRL_BY_HDMI		(0xf0000000 | BIT(13) | BIT(12))
#define RK3328_GRF_SOC_CON4		0x0410
#define RK3328_IO_3V_DOMAIN		(7 << (9 + 16))
#define RK3328_IO_5V_DOMAIN		((7 << 9) | (3 << (9 + 16)))
#define RK3328_HPD_3V			(BIT(8 + 16) | BIT(13 + 16))

#define HIWORD_UPDATE(val, mask)	(val | (mask) << 16)

struct rockchip_hdmi {
	struct device *dev;
	struct regmap *regmap;
	void __iomem *hdmiphy;
	struct drm_encoder encoder;
	enum dw_hdmi_devtype dev_type;
	struct clk *vpll_clk;
	struct clk *grf_clk;
	struct clk *hdmiphy_pclk;
	struct clk *hdmiphy_clk;
	struct clk *hclk_vio;
	struct clk_hw	hdmiphy_clkhw;

	struct phy *phy;
};

#define to_rockchip_hdmi(x)	container_of(x, struct rockchip_hdmi, x)

/*
 * Inno HDMI phy pre pll output 4 clock: tmdsclock_tx/tmdsclock_ctrl/
 * preclock_ctrl/pclk_vop.
 * tmdsclock_tx is used for transmit hdmi signal, it's is equal to
 * tmdsclock.
 *	Ftmdsclock_tx = Fvco / (4 * tmds_div_a * tmds_div_b)
 * tmdsclock_ctrl is output to dw-hdmi controller, it's is equal to
 * tmdsclock.
 *	Ftmdsclock_ctrl = Fvco / (4 * tmds_div_a * tmds_div_c)
 * preclock_ctrl is output to dw-hdmi controller, it's equal to mode
 * pixel clock.
 *	Fpreclock_ctrl = (pclk_div_a == 1) ?
 *			 Fvco / (pclk_div_b * pclk_div_c) :
 *			 Fvco / (pclk_div_a * pclk_div_c) :
 * pclk_vop is clock source of dclk_lcdc, it's equal to mode pixel clock.
 *	Fpclk_vop = (vco_div_5 == 1) ? Fvco / 5 : (pclk_div_a == 1) ?
 *		    Fvco / (2 * pclk_div_b * pclk_div_d) :
 *		    Fvco / (2 * pclk_div_a * pclk_div_d)
 * Fvco = Fref * (nf + frac / (1 << 24)) / nd
 */
struct inno_pre_pll_config {
	unsigned long	pixclock;
	u32		tmdsclock;
	u8		nd;
	u16		nf;
	u8		tmds_div_a;
	u8		tmds_div_b;
	u8		tmds_div_c;
	u8		pclk_div_a;
	u8		pclk_div_b;
	u8		pclk_div_c;
	u8		pclk_div_d;
	u8		vco_div_5;
	u32		frac;
};

struct inno_post_pll_config {
	unsigned long	tmdsclock;
	u8		nd;
	u16		nf;
	u8		no;
	u8		version;
};

struct inno_phy_config {
	unsigned long	tmdsclock;
	u8		regs[14];
};

static const struct inno_pre_pll_config inno_pre_pll_cfg[] = {
	{27000000,	27000000,	1,	90,	3,	2,
		2,	10,	3,	3,	4,	0,	0},
	{27000000,	33750000,	1,	90,	1,	3,
		3,	10,	3,	3,	4,	0,	0},
	{40000000,	40000000,	1,	80,	2,	2,
		2,	12,	2,	2,	2,	0,	0},
	{59341000,	59341000,	1,	98,	3,	1,
		2,	1,	3,	3,	4,	0,	0xE6AE6B},
	{59400000,	59400000,	1,	99,	3,	1,
		1,	1,	3,	3,	4,	0,	0},
	{59341000,	74176250,	1,	98,	0,	3,
		3,	1,	3,	3,	4,	0,	0xE6AE6B},
	{59400000,	74250000,	1,	99,	1,	2,
		2,	1,	3,	3,	4,	0,	0},
	{74176000,	74176000,	1,	98,	1,	2,
		2,	1,	2,	3,	4,	0,	0xE6AE6B},
	{74250000,	74250000,	1,	99,	1,	2,
		2,	1,	2,	3,	4,	0,	0},
	{74176000,	92720000,	4,	494,	1,	2,
		2,	1,	3,	3,	4,	0,	0x816817},
	{74250000,	92812500,	4,	495,	1,	2,
		2,	1,	3,	3,	4,	0,	0},
	{148352000,	148352000,	1,	98,	1,	1,
		1,	1,	2,	2,	2,	0,	0xE6AE6B},
	{148500000,	148500000,	1,	99,	1,	1,
		1,	1,	2,	2,	2,	0,	0},
	{148352000,	185440000,	4,	494,	0,	2,
		2,	1,	3,	2,	2,	0,	0x816817},
	{148500000,	185625000,	4,	495,	0,	2,
		2,	1,	3,	2,	2,	0,	0},
	{296703000,	296703000,	1,	98,	0,	1,
		1,	1,	0,	2,	2,	0,	0xE6AE6B},
	{297000000,	297000000,	1,	99,	0,	1,
		1,	1,	0,	2,	2,	0,	0},
	{296703000,	370878750,	4,	494,	1,	2,
		0,	1,	3,	1,	1,	0,	0x816817},
	{297000000,	371250000,	4,	495,	1,	2,
		0,	1,	3,	1,	1,	0,	0},
	{593407000,	296703500,	1,	98,	0,	1,
		1,	1,	0,	2,	1,	0,	0xE6AE6B},
	{594000000,	297000000,	1,	99,	0,	1,
		1,	1,	0,	2,	1,	0,	0},
	{593407000,	370879375,	4,	494,	1,	2,
		0,	1,	3,	1,	1,	1,	0x816817},
	{594000000,	371250000,	4,	495,	1,	2,
		0,	1,	3,	1,	1,	1,	0},
	{593407000,	593407000,	1,	98,	0,	2,
		0,	1,	0,	1,	1,	0,	0xE6AE6B},
	{594000000,	594000000,	1,	99,	0,	2,
		0,	1,	0,	1,	1,	0,	0},
	{~0UL,		0,		0,	0,	0,	0,
		0,	0,	0,	0,	0,	0,	0},
};

static const struct inno_post_pll_config inno_post_pll_cfg[] = {
	{33750000,	1,	40,	8,	1},
	{33750000,	1,	80,	8,	2},
	{74250000,	1,	40,	8,	1},
	{74250000,	18,	80,	8,	2},
	{148500000,	2,	40,	4,	3},
	{297000000,	4,	40,	2,	3},
	{371250000,	8,	40,	1,	3},
	{~0UL,		0,	0,	0,	0},
};

static const struct inno_phy_config inno_phy_cfg[] = {
	{	165000000, {
			0x07, 0x08, 0x08, 0x08, 0x00, 0x00, 0x08, 0x08, 0x08,
			0x00, 0xac, 0xcc, 0xcc, 0xcc,
		},
	}, {
		340000000, {
			0x0b, 0x0d, 0x0d, 0x0d, 0x07, 0x15, 0x08, 0x08, 0x08,
			0x3f, 0xac, 0xcc, 0xcd, 0xdd,
		},
	}, {
		594000000, {
			0x10, 0x1a, 0x1a, 0x1a, 0x07, 0x15, 0x08, 0x08, 0x08,
			0x00, 0xac, 0xcc, 0xcc, 0xcc,
		},
	}, {
		~0UL, {
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00,
		},
	}
};

static void inno_phy_writel(struct rockchip_hdmi *hdmi, int offset, u8 val)
{
	writel(val, hdmi->hdmiphy + (offset << 2));
}

static u8 inno_phy_readl(struct rockchip_hdmi *hdmi, int offset)
{
	return readl(hdmi->hdmiphy + (offset << 2));
}

static void
inno_phy_modeb(struct rockchip_hdmi *hdmi, u8 data, u8 mask, int offset)
{
	u8 val = inno_phy_readl(hdmi, offset) & ~mask;

	val |= data & mask;
	inno_phy_writel(hdmi, offset, val);
}

static int inno_dw_hdmi_phy_read(struct dw_hdmi *hdmi, void *data, int offset)
{
	struct rockchip_hdmi *rockchip_hdmi = (struct rockchip_hdmi *)data;

	return inno_phy_readl(rockchip_hdmi, offset);
}

static void
inno_dw_hdmi_phy_write(struct dw_hdmi *hdmi, void *data, int val, int offset)
{
	struct rockchip_hdmi *rockchip_hdmi = (struct rockchip_hdmi *)data;

	inno_phy_writel(rockchip_hdmi, offset, val);
}

static int
inno_dw_hdmi_phy_init(struct dw_hdmi *dw_hdmi, void *data,
		      struct drm_display_mode *mode)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	const struct inno_post_pll_config *inno_pll_config = inno_post_pll_cfg;
	const struct inno_phy_config *inno_phy_config = inno_phy_cfg;
	u32 val, i, chipversion = 1;

	if (hdmi->dev_type == RK3228_HDMI) {
		phy_power_on(hdmi->phy);
		return 0;
	}

	if (rockchip_get_cpu_version())
		chipversion = 2;

	for (; inno_pll_config->tmdsclock != ~0UL; inno_pll_config++)
		if (inno_pll_config->tmdsclock >= (mode->crtc_clock * 1000) &&
		    inno_pll_config->version & chipversion)
			break;
	for (; inno_phy_config->tmdsclock != ~0UL; inno_phy_config++)
		if (inno_phy_config->tmdsclock >= (mode->crtc_clock * 1000))
			break;
	if (inno_pll_config->tmdsclock == ~0UL &&
	    inno_phy_config->tmdsclock == ~0UL)
		return -EINVAL;

	/* set pdata_en to 0 */
	inno_phy_modeb(hdmi, 0, 1, 0x02);
	/* Power off post PLL */
	inno_phy_modeb(hdmi, 1, 1, 0xaa);

	val = inno_pll_config->nf & 0xff;
	inno_phy_writel(hdmi, 0xac, val);
	if (inno_pll_config->no == 1) {
		inno_phy_writel(hdmi, 0xaa, 2);
		val = (inno_pll_config->nf >> 8) |
		      inno_pll_config->nd;
		inno_phy_writel(hdmi, 0xab, val);
	} else {
		val = (inno_pll_config->no / 2) - 1;
		inno_phy_writel(hdmi, 0xad, val);
		val = (inno_pll_config->nf >> 8) |
		      inno_pll_config->nd;
		inno_phy_writel(hdmi, 0xab, val);
		inno_phy_writel(hdmi, 0xaa, 0x0e);
	}

	for (i = 0; i < 14; i++)
		inno_phy_writel(hdmi, 0xb5 + i, inno_phy_config->regs[i]);

	/* bit[7:6] of reg c8/c9/ca/c8 is ESD detect threshold:
	 * 00 - 340mV
	 * 01 - 280mV
	 * 10 - 260mV
	 * 11 - 240mV
	 * default is 240mV, now we set it to 340mV
	 */
	inno_phy_writel(hdmi, 0xc8, 0);
	inno_phy_writel(hdmi, 0xc9, 0);
	inno_phy_writel(hdmi, 0xca, 0);
	inno_phy_writel(hdmi, 0xcb, 0);

	if (inno_phy_config->tmdsclock > 340000000) {
		/* Set termination resistor to 100ohm */
		val = clk_get_rate(hdmi->hdmiphy_pclk) / 100000;
		inno_phy_writel(hdmi, 0xc5, ((val >> 8) & 0xff) | 0x80);
		inno_phy_writel(hdmi, 0xc6, val & 0xff);
		inno_phy_writel(hdmi, 0xc7, 3 << 1);
		inno_phy_writel(hdmi, 0xc5, ((val >> 8) & 0xff));
	} else if (inno_phy_config->tmdsclock > 165000000) {
		inno_phy_writel(hdmi, 0xc5, 0x81);
		/* clk termination resistor is 50ohm
		 * data termination resistor is 150ohm
		 */
		inno_phy_writel(hdmi, 0xc8, 0x30);
		inno_phy_writel(hdmi, 0xc9, 0x10);
		inno_phy_writel(hdmi, 0xca, 0x10);
		inno_phy_writel(hdmi, 0xcb, 0x10);
	} else {
		inno_phy_writel(hdmi, 0xc5, 0x81);
	}

	/* Power up post PLL */
	inno_phy_modeb(hdmi, 0, 1, 0xaa);
	/* Power up tmds driver */
	inno_phy_modeb(hdmi, 4, 4, 0xb0);
	inno_phy_writel(hdmi, 0xb2, 0x0f);

	/* Wait for post PLL lock */
	for (i = 0; i < 5; ++i) {
		if (inno_phy_readl(hdmi, 0xaf) & 1)
			break;
		usleep_range(1000, 2000);
	}
	if (!(inno_phy_readl(hdmi, 0xaf) & 1)) {
		dev_err(hdmi->dev, "HDMI PHY Post PLL unlock\n");
		return -ETIMEDOUT;
	}
	if (inno_phy_config->tmdsclock > 340000000)
		msleep(100);
	/* set pdata_en to 1 */
	inno_phy_modeb(hdmi, 1, 1, 0x02);
	return 0;
}

static void inno_dw_hdmi_phy_disable(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	if (hdmi->dev_type == RK3228_HDMI) {
		phy_power_off(hdmi->phy);
		return;
	}

	/* Power off driver */
	inno_phy_writel(hdmi, 0xb2, 0);
	/* Power off band gap */
	inno_phy_modeb(hdmi, 0, 4, 0xb0);
	/* Power off post pll */
	inno_phy_modeb(hdmi, 1, 1, 0xaa);
}

static enum drm_connector_status
inno_dw_hdmi_phy_read_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	enum drm_connector_status status;

	status = dw_hdmi_phy_read_hpd(dw_hdmi, data);

	if (hdmi->dev_type == RK3228_HDMI)
		return status;

	if (status == connector_status_connected)
		regmap_write(hdmi->regmap,
			     RK3328_GRF_SOC_CON4,
			     RK3328_IO_5V_DOMAIN);
	else
		regmap_write(hdmi->regmap,
			     RK3328_GRF_SOC_CON4,
			     RK3328_IO_3V_DOMAIN);
	return status;
}

static int inno_dw_hdmi_phy_pll_prepare(struct clk_hw *hw)
{
	struct rockchip_hdmi *hdmi =
		container_of(hw, struct rockchip_hdmi, hdmiphy_clkhw);

	if (inno_phy_readl(hdmi, 0xa0) & 1)
		inno_phy_modeb(hdmi, 0, 1, 0xa0);
	return 0;
}

static void inno_dw_hdmi_phy_pll_unprepare(struct clk_hw *hw)
{
	struct rockchip_hdmi *hdmi =
		container_of(hw, struct rockchip_hdmi, hdmiphy_clkhw);

	if ((inno_phy_readl(hdmi, 0xa0) & 1) == 0)
		inno_phy_modeb(hdmi, 1, 1, 0xa0);
}

static int inno_dw_hdmi_phy_is_prepared(struct clk_hw *hw)
{
	struct rockchip_hdmi *hdmi =
		container_of(hw, struct rockchip_hdmi, hdmiphy_clkhw);

	return (inno_phy_readl(hdmi, 0xa0) & 1) ? 0 : 1;
}

static unsigned long
inno_dw_hdmi_phy_pll_recalc_rate(struct clk_hw *hw,
				 unsigned long parent_rate)
{
	struct rockchip_hdmi *hdmi =
		container_of(hw, struct rockchip_hdmi, hdmiphy_clkhw);
	unsigned long rate, vco, frac;
	u8 nd, no_a, no_b, no_c, no_d;
	u16 nf;

	nd = inno_phy_readl(hdmi, 0xa1) & 0x3f;
	nf = ((inno_phy_readl(hdmi, 0xa2) & 0x0f) << 8) |
	     inno_phy_readl(hdmi, 0xa3);
	vco = parent_rate * nf;
	if ((inno_phy_readl(hdmi, 0xa2) & 0x30) == 0) {
		frac = inno_phy_readl(hdmi, 0xd3) |
		       (inno_phy_readl(hdmi, 0xd2) << 8) |
		       (inno_phy_readl(hdmi, 0xd1) << 16);
		vco += DIV_ROUND_CLOSEST(parent_rate * frac, (1 << 24));
	}
	if (inno_phy_readl(hdmi, 0xa0) & 2) {
		rate = vco / (nd * 5);
	} else {
		no_a = inno_phy_readl(hdmi, 0xa5) & 0x1f;
		no_b = ((inno_phy_readl(hdmi, 0xa5) >> 5) & 7) + 2;
		no_c = (1 << ((inno_phy_readl(hdmi, 0xa6) >> 5) & 7));
		no_d = inno_phy_readl(hdmi, 0xa6) & 0x1f;
		if (no_a == 1)
			rate = vco / (nd * no_b * no_d * 2);
		else
			rate = vco / (nd * no_a * no_d * 2);
	}

	return rate;
}

static long
inno_dw_hdmi_phy_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate)
{
	const struct inno_pre_pll_config *inno_pll_config = inno_pre_pll_cfg;

	for (; inno_pll_config->pixclock != ~0UL; inno_pll_config++)
		if (inno_pll_config->pixclock == rate)
			break;
	/* XXX: Limit pixel clock under 300MHz */
	if (inno_pll_config->pixclock < 300000000)
		return inno_pll_config->pixclock;
	else
		return ~0UL;
}

static int
inno_dw_hdmi_phy_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct rockchip_hdmi *hdmi =
		container_of(hw, struct rockchip_hdmi, hdmiphy_clkhw);
	const struct inno_pre_pll_config *inno_pll_config = inno_pre_pll_cfg;
	u32 val, i;

	for (; inno_pll_config->pixclock != ~0UL; inno_pll_config++)
		if (inno_pll_config->pixclock == rate &&
		    inno_pll_config->tmdsclock == rate)
			break;
	if (inno_pll_config->pixclock == ~0UL) {
		dev_err(hdmi->dev, "unsupported rate %lu\n", rate);
		return -EINVAL;
	}
	/* Power off PLL */
	inno_phy_modeb(hdmi, 1, 1, 0xa0);
	/* Configure pre-pll */
	inno_phy_modeb(hdmi, (inno_pll_config->vco_div_5 & 1) << 1, 2, 0xa0);
	inno_phy_writel(hdmi, 0xa1, inno_pll_config->nd);
	if (inno_pll_config->frac)
		val = ((inno_pll_config->nf >> 8) & 0x0f) | 0xc0;
	else
		val = ((inno_pll_config->nf >> 8) & 0x0f) | 0xf0;
	inno_phy_writel(hdmi, 0xa2, val);
	val = inno_pll_config->nf & 0xff;
	inno_phy_writel(hdmi, 0xa3, val);
	val = (inno_pll_config->pclk_div_a & 0x1f) |
	      ((inno_pll_config->pclk_div_b & 3) << 5);
	inno_phy_writel(hdmi, 0xa5, val);
	val = (inno_pll_config->pclk_div_d & 0x1f) |
	      ((inno_pll_config->pclk_div_c & 3) << 5);
	inno_phy_writel(hdmi, 0xa6, val);
	val = ((inno_pll_config->tmds_div_a & 3) << 4) |
	      ((inno_pll_config->tmds_div_b & 3) << 2) |
	      (inno_pll_config->tmds_div_c & 3);
	inno_phy_writel(hdmi, 0xa4, val);

	if (inno_pll_config->frac) {
		val = inno_pll_config->frac & 0xff;
		inno_phy_writel(hdmi, 0xd3, val);
		val = (inno_pll_config->frac >> 8) & 0xff;
		inno_phy_writel(hdmi, 0xd2, val);
		val = (inno_pll_config->frac >> 16) & 0xff;
		inno_phy_writel(hdmi, 0xd1, val);
	} else {
		inno_phy_writel(hdmi, 0xd3, 0);
		inno_phy_writel(hdmi, 0xd2, 0);
		inno_phy_writel(hdmi, 0xd1, 0);
	}

	/* Power up PLL */
	inno_phy_modeb(hdmi, 0, 1, 0xa0);

	/* Wait for PLL lock */
	for (i = 0; i < 5; ++i) {
		if (inno_phy_readl(hdmi, 0xa9) & 1)
			break;
		usleep_range(1000, 2000);
	}
	return 0;
}

static const struct clk_ops inno_dw_hdmi_phy_pll_clk_norate_ops = {
	.prepare = inno_dw_hdmi_phy_pll_prepare,
	.unprepare = inno_dw_hdmi_phy_pll_unprepare,
	.is_prepared = inno_dw_hdmi_phy_is_prepared,
	.recalc_rate = inno_dw_hdmi_phy_pll_recalc_rate,
	.round_rate = inno_dw_hdmi_phy_pll_round_rate,
	.set_rate = inno_dw_hdmi_phy_pll_set_rate,
};

static void inno_dw_hdmi_phy_clk_unregister(void *data)
{
	struct rockchip_hdmi *hdmi = data;

	of_clk_del_provider(hdmi->dev->of_node);
	clk_unregister(hdmi->hdmiphy_clk);
}

static int inno_dw_hdmi_phy_clk_register(struct rockchip_hdmi *hdmi)
{
	struct device_node *node = hdmi->dev->of_node;
	struct clk_init_data init;
	const char *clk_name = "xin24m";
	int ret;

	init.flags = 0;
	init.name = "hdmi_phy";
	init.ops = &inno_dw_hdmi_phy_pll_clk_norate_ops;
	init.parent_names = &clk_name;
	init.num_parents = 1;

	/* optional override of the clockname */
	of_property_read_string(node, "clock-output-names", &init.name);

	hdmi->hdmiphy_clkhw.init = &init;

	/* register the clock */
	hdmi->hdmiphy_clk = clk_register(hdmi->dev, &hdmi->hdmiphy_clkhw);
	if (IS_ERR(hdmi->hdmiphy_clk)) {
		ret = PTR_ERR(hdmi->hdmiphy_clk);
		goto err_ret;
	}
	ret = of_clk_add_provider(node, of_clk_src_simple_get,
				  hdmi->hdmiphy_clk);
	if (ret < 0)
		goto err_clk_provider;

	ret = devm_add_action(hdmi->dev, inno_dw_hdmi_phy_clk_unregister,
			      hdmi);
	if (ret < 0)
		goto err_unreg_action;
	return 0;
err_unreg_action:
	of_clk_del_provider(node);
err_clk_provider:
	clk_unregister(hdmi->hdmiphy_clk);
err_ret:
	return ret;
}

static int rk3328_hdmi_init(struct rockchip_hdmi *hdmi)
{
	int ret;

	ret = clk_prepare_enable(hdmi->grf_clk);
	if (ret < 0) {
		dev_err(hdmi->dev, "failed to enable grfclk %d\n", ret);
		return -EPROBE_DEFER;
	}
	/* Map HPD pin to 3V io */
	regmap_write(hdmi->regmap,
		     RK3328_GRF_SOC_CON4,
		     RK3328_IO_3V_DOMAIN |
		     RK3328_HPD_3V);
	/* Map ddc pin to 5V io */
	regmap_write(hdmi->regmap,
		     RK3328_GRF_SOC_CON3,
		     RK3328_IO_CTRL_BY_HDMI);
	regmap_write(hdmi->regmap,
		     RK3328_GRF_SOC_CON2,
		     RK3328_DDC_MASK_EN |
		     BIT(18));
	clk_disable_unprepare(hdmi->grf_clk);
	/*
	 * Use phy internal register control
	 * rxsense/poweron/pllpd/pdataen signal.
	 */
	inno_phy_writel(hdmi, 0x01, 0x07);
	inno_phy_writel(hdmi, 0x02, 0x91);
	return 0;
}

/*
 * There are some rates that would be ranged for better clock jitter at
 * Chrome OS tree, like 25.175Mhz would range to 25.170732Mhz. But due
 * to the clock is aglined to KHz in struct drm_display_mode, this would
 * bring some inaccurate error if we still run the compute_n math, so
 * let's just code an const table for it until we can actually get the
 * right clock rate.
 */
static const struct dw_hdmi_audio_tmds_n rockchip_werid_tmds_n_table[] = {
	/* 25176471 for 25.175 MHz = 428000000 / 17. */
	{ .tmds = 25177000, .n_32k = 4352, .n_44k1 = 14994, .n_48k = 6528, },
	/* 57290323 for 57.284 MHz */
	{ .tmds = 57291000, .n_32k = 3968, .n_44k1 = 4557, .n_48k = 5952, },
	/* 74437500 for 74.44 MHz = 297750000 / 4 */
	{ .tmds = 74438000, .n_32k = 8192, .n_44k1 = 18816, .n_48k = 4096, },
	/* 118666667 for 118.68 MHz */
	{ .tmds = 118667000, .n_32k = 4224, .n_44k1 = 5292, .n_48k = 6336, },
	/* 121714286 for 121.75 MHz */
	{ .tmds = 121715000, .n_32k = 4480, .n_44k1 = 6174, .n_48k = 6272, },
	/* 136800000 for 136.75 MHz */
	{ .tmds = 136800000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },
	/* End of table */
	{ .tmds = 0,         .n_32k = 0,    .n_44k1 = 0,    .n_48k = 0, },
};

static const struct dw_hdmi_mpll_config rockchip_mpll_cfg[] = {
	{
		30666000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40f3, 0x0000 },
		},
	},  {
		36800000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		46000000, {
			{ 0x00b3, 0x0000 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		61333000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		73600000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x4061, 0x0002 },
		},
	},  {
		92000000, {
			{ 0x0072, 0x0001 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	},  {
		122666000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	},  {
		147200000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4064, 0x0003 },
		},
	},  {
		184000000, {
			{ 0x0051, 0x0002 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	},  {
		226666000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	},  {
		272000000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		340000000, {
			{ 0x0040, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		600000000, {
			{ 0x1a40, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		~0UL, {
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
		},
	}
};

static const struct dw_hdmi_curr_ctrl rockchip_cur_ctr[] = {
	/*      pixelclk    bpp8    bpp10   bpp12 */
	{
		600000000, { 0x0000, 0x0000, 0x0000 },
	},  {
		~0UL,      { 0x0000, 0x0000, 0x0000},
	}
};

static struct dw_hdmi_phy_config rockchip_phy_config[] = {
	/*pixelclk   symbol   term   vlev*/
	{ 74250000,  0x8009, 0x0004, 0x0272},
	{ 165000000, 0x802b, 0x0004, 0x0209},
	{ 297000000, 0x8039, 0x0005, 0x028d},
	{ 594000000, 0x8039, 0x0000, 0x019d},
	{ ~0UL,	     0x0000, 0x0000, 0x0000}
};

static int rockchip_hdmi_update_phy_table(struct rockchip_hdmi *hdmi,
					  u32 *config,
					  int phy_table_size)
{
	int i;

	if (phy_table_size > ARRAY_SIZE(rockchip_phy_config)) {
		dev_err(hdmi->dev, "phy table array number is out of range\n");
		return -E2BIG;
	}

	for (i = 0; i < phy_table_size; i++) {
		if (config[i * 4] != 0)
			rockchip_phy_config[i].mpixelclock = (u64)config[i * 4];
		else
			rockchip_phy_config[i].mpixelclock = ~0UL;
		rockchip_phy_config[i].term = (u16)config[i * 4 + 1];
		rockchip_phy_config[i].sym_ctr = (u16)config[i * 4 + 2];
		rockchip_phy_config[i].vlev_ctr = (u16)config[i * 4 + 3];
	}

	return 0;
}

static int rockchip_hdmi_parse_dt(struct rockchip_hdmi *hdmi)
{
	struct device_node *np = hdmi->dev->of_node;
	int ret, val, phy_table_size;
	u32 *phy_config;

	hdmi->regmap = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(hdmi->regmap)) {
		dev_err(hdmi->dev, "Unable to get rockchip,grf\n");
		return PTR_ERR(hdmi->regmap);
	}

	hdmi->vpll_clk = devm_clk_get(hdmi->dev, "vpll");
	if (PTR_ERR(hdmi->vpll_clk) == -ENOENT) {
		hdmi->vpll_clk = NULL;
	} else if (PTR_ERR(hdmi->vpll_clk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->vpll_clk)) {
		dev_err(hdmi->dev, "failed to get grf clock\n");
		return PTR_ERR(hdmi->vpll_clk);
	}

	hdmi->grf_clk = devm_clk_get(hdmi->dev, "grf");
	if (PTR_ERR(hdmi->grf_clk) == -ENOENT) {
		hdmi->grf_clk = NULL;
	} else if (PTR_ERR(hdmi->grf_clk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->grf_clk)) {
		dev_err(hdmi->dev, "failed to get grf clock\n");
		return PTR_ERR(hdmi->grf_clk);
	}

	hdmi->hclk_vio = devm_clk_get(hdmi->dev, "hclk_vio");
	if (PTR_ERR(hdmi->hclk_vio) == -ENOENT) {
		hdmi->hclk_vio = NULL;
	} else if (PTR_ERR(hdmi->hclk_vio) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->hclk_vio)) {
		dev_err(hdmi->dev, "failed to get hclk_vio clock\n");
		return PTR_ERR(hdmi->hclk_vio);
	}

	hdmi->hdmiphy_pclk = devm_clk_get(hdmi->dev, "pclk_hdmiphy");
	if (PTR_ERR(hdmi->hdmiphy_pclk) == -ENOENT) {
		hdmi->hdmiphy_pclk = NULL;
	} else if (PTR_ERR(hdmi->hdmiphy_pclk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->hdmiphy_pclk)) {
		dev_err(hdmi->dev, "failed to get pclk_hdmiphy clock\n");
		return PTR_ERR(hdmi->hdmiphy_pclk);
	}

	ret = clk_prepare_enable(hdmi->vpll_clk);
	if (ret) {
		dev_err(hdmi->dev, "Failed to enable HDMI vpll: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->hclk_vio);
	if (ret) {
		dev_err(hdmi->dev, "Failed to eanble HDMI hclk_vio: %d\n",
			ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->hdmiphy_pclk);
	if (ret) {
		dev_err(hdmi->dev, "Failed to eanble HDMI pclk_hdmiphy: %d\n",
			ret);
		return ret;
	}

	if (of_get_property(np, "rockchip,phy-table", &val)) {
		phy_config = kmalloc(val, GFP_KERNEL);
		if (!phy_config) {
			/* use default table when kmalloc failed. */
			dev_err(hdmi->dev, "kmalloc phy table failed\n");

			return -ENOMEM;
		}
		phy_table_size = val / 16;
		of_property_read_u32_array(np, "rockchip,phy-table",
					   phy_config, val / sizeof(u32));
		ret = rockchip_hdmi_update_phy_table(hdmi, phy_config,
						     phy_table_size);
		if (ret) {
			kfree(phy_config);
			return ret;
		}
		kfree(phy_config);
	} else {
		dev_dbg(hdmi->dev, "use default hdmi phy table\n");
	}

	return 0;
}

static enum drm_mode_status
dw_hdmi_rockchip_mode_valid(struct drm_connector *connector,
			    struct drm_display_mode *mode)
{
	struct drm_encoder *encoder = connector->encoder;
	enum drm_mode_status status = MODE_OK;
	struct drm_device *dev = connector->dev;
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;

	/*
	 * Pixel clocks we support are always < 2GHz and so fit in an
	 * int.  We should make sure source rate does too so we don't get
	 * overflow when we multiply by 1000.
	 */
	if (mode->clock > INT_MAX / 1000)
		return MODE_BAD;

	if (!encoder) {
		const struct drm_connector_helper_funcs *funcs;

		funcs = connector->helper_private;
		if (funcs->atomic_best_encoder)
			encoder = funcs->atomic_best_encoder(connector,
							     connector->state);
		else
			encoder = funcs->best_encoder(connector);
	}

	if (!encoder || !encoder->possible_crtcs)
		return MODE_BAD;
	/*
	 * ensure all drm display mode can work, if someone want support more
	 * resolutions, please limit the possible_crtc, only connect to
	 * needed crtc.
	 */
	drm_for_each_crtc(crtc, connector->dev) {
		int pipe = drm_crtc_index(crtc);
		const struct rockchip_crtc_funcs *funcs =
						priv->crtc_funcs[pipe];

		if (!(encoder->possible_crtcs & drm_crtc_mask(crtc)))
			continue;
		if (!funcs || !funcs->mode_valid)
			continue;

		status = funcs->mode_valid(crtc, mode,
					   DRM_MODE_CONNECTOR_HDMIA);
		if (status != MODE_OK)
			return status;
	}

	return status;
}

static const struct drm_encoder_funcs dw_hdmi_rockchip_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static void dw_hdmi_rockchip_encoder_disable(struct drm_encoder *encoder)
{
}

static void dw_hdmi_rockchip_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	struct drm_crtc *crtc = encoder->crtc;
	u32 lcdsel_grf_reg, lcdsel_mask;
	u32 val;
	int mux;
	int ret;

	if (WARN_ON(!crtc || !crtc->state))
		return;

	clk_set_rate(hdmi->vpll_clk,
		     crtc->state->adjusted_mode.crtc_clock * 1000);

	switch (hdmi->dev_type) {
	case RK3288_HDMI:
		lcdsel_grf_reg = RK3288_GRF_SOC_CON6;
		lcdsel_mask = RK3288_HDMI_LCDC_SEL;
		break;
	case RK3399_HDMI:
		lcdsel_grf_reg = RK3399_GRF_SOC_CON20;
		lcdsel_mask = RK3399_HDMI_LCDC_SEL;
		break;
	default:
		return;
	};

	mux = drm_of_encoder_active_endpoint_id(hdmi->dev->of_node, encoder);
	if (mux)
		val = HIWORD_UPDATE(lcdsel_mask, lcdsel_mask);
	else
		val = HIWORD_UPDATE(0, lcdsel_mask);

	ret = clk_prepare_enable(hdmi->grf_clk);
	if (ret < 0) {
		dev_err(hdmi->dev, "failed to enable grfclk %d\n", ret);
		return;
	}

	regmap_write(hdmi->regmap, lcdsel_grf_reg, val);
	dev_dbg(hdmi->dev, "vop %s output to hdmi\n",
		(mux) ? "LIT" : "BIG");

	clk_disable_unprepare(hdmi->grf_clk);
}

static int
dw_hdmi_rockchip_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);

	if (crtc_state->mode.flags & DRM_MODE_FLAG_420_MASK) {
		s->output_mode = ROCKCHIP_OUT_MODE_YUV420;
		s->bus_format = MEDIA_BUS_FMT_UYYVYY8_0_5X24;
	} else {
		s->output_mode = ROCKCHIP_OUT_MODE_AAAA;
		s->bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	}
	s->output_type = DRM_MODE_CONNECTOR_HDMIA;

	return 0;
}

static const struct drm_encoder_helper_funcs dw_hdmi_rockchip_encoder_helper_funcs = {
	.enable     = dw_hdmi_rockchip_encoder_enable,
	.disable    = dw_hdmi_rockchip_encoder_disable,
	.atomic_check = dw_hdmi_rockchip_encoder_atomic_check,
};

static const struct dw_hdmi_phy_ops inno_dw_hdmi_phy_ops = {
	.init		= inno_dw_hdmi_phy_init,
	.disable	= inno_dw_hdmi_phy_disable,
	.read_hpd	= inno_dw_hdmi_phy_read_hpd,
	.read		= inno_dw_hdmi_phy_read,
	.write		= inno_dw_hdmi_phy_write,
};

static const struct dw_hdmi_plat_data rk3228_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.phy_ops    = &inno_dw_hdmi_phy_ops,
	.phy_name   = "inno_dw_hdmi_phy",
	.dev_type   = RK3228_HDMI,
};

static const struct dw_hdmi_plat_data rk3288_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.dev_type   = RK3288_HDMI,
	.tmds_n_table = rockchip_werid_tmds_n_table,
};

static const struct dw_hdmi_plat_data rk3328_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.phy_ops    = &inno_dw_hdmi_phy_ops,
	.phy_name   = "inno_dw_hdmi_phy2",
	.dev_type   = RK3328_HDMI,
};

static const struct dw_hdmi_plat_data rk3368_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.dev_type   = RK3368_HDMI,
};

static const struct dw_hdmi_plat_data rk3399_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.dev_type   = RK3399_HDMI,
};

static const struct of_device_id dw_hdmi_rockchip_dt_ids[] = {
	{ .compatible = "rockchip,rk3228-dw-hdmi",
	  .data = &rk3228_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3288-dw-hdmi",
	  .data = &rk3288_hdmi_drv_data
	},
	{
	  .compatible = "rockchip,rk3328-dw-hdmi",
	  .data = &rk3328_hdmi_drv_data
	},
	{
	 .compatible = "rockchip,rk3368-dw-hdmi",
	 .data = &rk3368_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3399-dw-hdmi",
	  .data = &rk3399_hdmi_drv_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, dw_hdmi_rockchip_dt_ids);

static int dw_hdmi_rockchip_bind(struct device *dev, struct device *master,
				 void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_hdmi_plat_data *plat_data;
	const struct of_device_id *match;
	struct drm_device *drm = data;
	struct drm_encoder *encoder;
	struct rockchip_hdmi *hdmi;
	struct resource *iores, *phyres;
	int irq;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	match = of_match_node(dw_hdmi_rockchip_dt_ids, pdev->dev.of_node);
	plat_data = (struct dw_hdmi_plat_data *)match->data;
	hdmi->dev = &pdev->dev;
	hdmi->dev_type = plat_data->dev_type;
	encoder = &hdmi->encoder;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores)
		return -ENXIO;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	ret = rockchip_hdmi_parse_dt(hdmi);
	if (ret) {
		dev_err(hdmi->dev, "Unable to parse OF data\n");
		return ret;
	}

	if (hdmi->dev_type == RK3328_HDMI) {
		phyres = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		hdmi->hdmiphy = devm_ioremap_resource(dev, phyres);
		if (IS_ERR(hdmi->hdmiphy))
			return PTR_ERR(hdmi->hdmiphy);
		plat_data->phy_data = hdmi;
		ret = rk3328_hdmi_init(hdmi);
		if (ret < 0)
			return ret;
		inno_dw_hdmi_phy_clk_register(hdmi);
	} else if (hdmi->dev_type == RK3228_HDMI) {
		hdmi->phy = devm_phy_get(dev, "hdmi_phy");
		if (IS_ERR(hdmi->phy)) {
			ret = PTR_ERR(hdmi->phy);
			dev_err(dev, "failed to get phy: %d\n", ret);
			return ret;
		}
		plat_data->phy_data = hdmi;
		regmap_write(hdmi->regmap, RK3228_GRF_SOC_CON2,
			     RK3228_DDC_MASK_EN);
		regmap_write(hdmi->regmap, RK3228_GRF_SOC_CON6,
			     RK3228_IO_3V_DOMAIN);
	}

	drm_encoder_helper_add(encoder, &dw_hdmi_rockchip_encoder_helper_funcs);
	drm_encoder_init(drm, encoder, &dw_hdmi_rockchip_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);

	ret = dw_hdmi_bind(dev, master, data, encoder, iores, irq, plat_data);

	/*
	 * If dw_hdmi_bind() fails we'll never call dw_hdmi_unbind(),
	 * which would have called the encoder cleanup.  Do it manually.
	 */
	if (ret)
		drm_encoder_cleanup(encoder);

	return ret;
}

static void dw_hdmi_rockchip_unbind(struct device *dev, struct device *master,
				    void *data)
{
	return dw_hdmi_unbind(dev, master, data);
}

static const struct component_ops dw_hdmi_rockchip_ops = {
	.bind	= dw_hdmi_rockchip_bind,
	.unbind	= dw_hdmi_rockchip_unbind,
};

static int dw_hdmi_rockchip_probe(struct platform_device *pdev)
{
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	return component_add(&pdev->dev, &dw_hdmi_rockchip_ops);
}

static int dw_hdmi_rockchip_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dw_hdmi_rockchip_ops);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int dw_hdmi_rockchip_suspend(struct device *dev)
{
	dw_hdmi_suspend(dev);
	pm_runtime_put_sync(dev);

	return 0;
}

static int dw_hdmi_rockchip_resume(struct device *dev)
{
	pm_runtime_get_sync(dev);
	dw_hdmi_resume(dev);

	return  0;
}

static const struct dev_pm_ops dw_hdmi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw_hdmi_rockchip_suspend,
				dw_hdmi_rockchip_resume)
};

static struct platform_driver dw_hdmi_rockchip_pltfm_driver = {
	.probe  = dw_hdmi_rockchip_probe,
	.remove = dw_hdmi_rockchip_remove,
	.driver = {
		.name = "dwhdmi-rockchip",
		.of_match_table = dw_hdmi_rockchip_dt_ids,
		.pm = &dw_hdmi_pm_ops,
	},
};

module_platform_driver(dw_hdmi_rockchip_pltfm_driver);

MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com>");
MODULE_AUTHOR("Yakir Yang <ykk@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Specific DW-HDMI Driver Extension");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dwhdmi-rockchip");
