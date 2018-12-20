// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>

#include <drm/drmP.h>
#include <drm/drm_of.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>

#define VENDOR_ID		0x0000
#define DEVICE_ID_H		0x0001
#define DEVICE_ID_L		0x0002
#define VERSION_ID		0x0003
#define FIRMWARE_VERSION	0x0008
#define CONFIG_FINISH		0x0009
#define PD_CTRL_0		0x000a
#define PD_CTRL_1		0x000b
#define PD_CTRL_2		0x000c
#define PD_CTRL_3		0x000d
#define RST_CTRL_0		0x000e
#define RST_CTRL_1		0x000f
#define SYS_CTRL_0		0x0010
#define SYS_CTRL_1		0x0011
#define SYS_CTRL_2		0x0012
#define SYS_CTRL_3		0x0013
#define SYS_CTRL_4		0x0014
#define RGB_DRV_0		0x0018
#define RGB_DRV_1		0x0019
#define RGB_DRV_2		0x001a
#define RGB_DRV_3		0x001b
#define RGB_DLY_0		0x001c
#define RGB_DLY_1		0x001d
#define RGB_TEST_CTRL		0x001e
#define ATE_PLL_EN		0x001f
#define HACTIVE_L		0x0020
#define VACTIVE_L		0x0021
#define VACTIVE_HACTIVE_H	0x0022
#define HFP_L			0x0023
#define HSW_L			0x0024
#define HBP_L			0x0025
#define HFP_HSW_HBP_H		0x0026
#define VFP			0x0027
#define VSW			0x0028
#define VBP			0x0029
#define BIST_POL		0x002a
#define BIST_RED		0x002b
#define BIST_GREEN		0x002c
#define BIST_BLUE		0x002d
#define BIST_CHESS_X		0x002e
#define BIST_CHESS_Y		0x002f
#define BIST_CHESS_XY_H		0x0030
#define BIST_FRAME_TIME_L	0x0031
#define BIST_FRAME_TIME_H	0x0032
#define FIFO_MAX_ADDR_LOW	0x0033
#define SYNC_EVENT_DLY_LOW	0x0034
#define HSW_MIN			0x0035
#define HFP_MIN			0x0036
#define LOGIC_RST_NUM		0x0037
#define OSC_CTRL_0		0x0048
#define	OSC_CTRL_1		0x0049
#define OSC_CTRL_2		0x004a
#define OSC_CTRL_3		0x004b
#define OSC_CTRL_4		0x004c
#define OSC_CTRL_5		0x004d
#define BG_CTRL			0x004e
#define LDO_PLL			0x004f
#define PLL_CTRL_0		0x0050
#define PLL_CTRL_1		0x0051
#define PLL_CTRL_2		0x0052
#define	PLL_CTRL_3		0x0053
#define PLL_CTRL_4		0x0054
#define PLL_CTRL_5		0x0055
#define PLL_CTRL_6		0x0056
#define PLL_CTRL_7		0x0057
#define PLL_CTRL_8		0x0058
#define PLL_CTRL_9		0x0059
#define PLL_CTRL_A		0x005a
#define PLL_CTRL_B		0x005b
#define PLL_CTRL_C		0x005c
#define PLL_CTRL_D		0x005d
#define PLL_CTRL_E		0x005e
#define PLL_CTRL_F		0x005f
#define PLL_REM_0		0x0060
#define PLL_REM_1		0x0061
#define PLL_REM_2		0x0062
#define PLL_DIV_0		0x0063
#define PLL_DIV_1		0x0064
#define PLL_DIV_2		0x0065
#define PLL_FRAC_0		0x0066
#define PLL_FRAC_1		0x0067
#define PLL_FRAC_2		0x0068
#define PLL_INT_0		0x0069
#define PLL_INT_1		0x006a
#define PLL_REF_DIV		0x006b
#define PLL_SSC_P0		0x006c
#define PLL_SSC_P1		0x006d
#define PLL_SSC_P2		0x006e
#define PLL_SSC_STEP0		0x006f
#define PLL_SSC_STEP1		0x0070
#define PLL_SSC_STEP2		0x0071
#define PLL_SSC_OFFSET0		0x0072
#define PLL_SSC_OFFSET1		0x0073
#define PLL_SSC_OFFSET2		0x0074
#define PLL_SSC_OFFSET3		0x0075
#define GPIO_OEN		0x0079
#define MIPI_CFG_PW		0x007a
#define GPIO_0_SEL		0x007b
#define GPIO_1_SEL		0x007c
#define IRQ_SEL			0x007d
#define DBG_SEL			0x007e
#define DBG_SIGNAL		0x007f
#define MIPI_ERR_VECTOR_L	0x0080
#define MIPI_ERR_VECTOR_H	0x0081
#define MIPI_ERR_VECTOR_EN_L	0x0082
#define MIPI_ERR_VECTOR_EN_H	0x0083
#define MIPI_MAX_SIZE_L		0x0084
#define MIPI_MAX_SIZE_H		0x0085
#define DSI_CTRL		0x0086
#define MIPI_PN_SWAP		0x0087
#define MIPI_SOT_SYNC_BIT_0	0x0088
#define MIPI_SOT_SYNC_BIT_1	0x0089
#define MIPI_ULPS_CTRL		0x008a
#define MIPI_CLK_CHK_VAR	0x008e
#define MIPI_CLK_CHK_INI	0x008f
#define MIPI_T_TERM_EN		0x0090
#define MIPI_T_HS_SETTLE	0x0091
#define MIPI_T_TA_SURE_PRE	0x0092
#define MIPI_T_LPX_SET		0x0094
#define MIPI_T_CLK_MISS		0x0095
#define MIPI_INIT_TIME_L	0x0096
#define MIPI_INIT_TIME_H	0x0097
#define MIPI_T_CLK_TERM_EN	0x0099
#define MIPI_T_CLK_SETTLE	0x009a
#define MIPI_TO_HS_RX_L		0x009e
#define MIPI_TO_HS_RX_H		0x009f
#define MIPI_PHY_0		0x00a0
#define MIPI_PHY_1		0x00a1
#define MIPI_PHY_2		0x00a2
#define MIPI_PHY_3		0x00a3
#define MIPI_PHY_4		0x00a4
#define MIPI_PHY_5		0x00a5
#define MIPI_PD_RX		0x00b0
#define MIPI_PD_TERM		0x00b1
#define MIPI_PD_HSRX		0x00b2
#define MIPI_PD_LPTX		0x00b3
#define MIPI_PD_LPRX		0x00b4
#define MIPI_PD_CK_LANE		0x00b5
#define MIPI_FORCE_0		0x00b6
#define MIPI_RST_CTRL		0x00b7
#define MIPI_RST_NUM		0x00b8
#define MIPI_DBG_SET_0		0x00c0
#define MIPI_DBG_SET_1		0x00c1
#define MIPI_DBG_SET_2		0x00c2
#define MIPI_DBG_SET_3		0x00c3
#define MIPI_DBG_SET_4		0x00c4
#define MIPI_DBG_SET_5		0x00c5
#define MIPI_DBG_SET_6		0x00c6
#define MIPI_DBG_SET_7		0x00c7
#define MIPI_DBG_SET_8		0x00c8
#define MIPI_DBG_SET_9		0x00c9
#define MIPI_DBG_SEL		0x00e0
#define MIPI_DBG_DATA		0x00e1
#define MIPI_ATE_TEST_SEL	0x00e2
#define MIPI_ATE_STATUS_0	0x00e3
#define MIPI_ATE_STATUS_1	0x00e4
#define ICN6211_MAX_REGISTER	MIPI_ATE_STATUS_1

struct icn6211 {
	struct drm_bridge base;
	struct drm_connector connector;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct drm_display_mode mode;

	struct device *dev;
	struct i2c_client *client;
	struct mipi_dsi_device dsi;
	struct regmap *regmap;
	struct clk *refclk;	/* reference clock for RGB output clock */
	struct regulator *vdd1;	/* MIPI RX power supply, can be 1.8V-3.3V */
	struct regulator *vdd2; /* PLL power supply, can be 1.8V-3.3V */
	struct regulator *vdd3;	/* RGB output power supply, can be 1.8V-3.3V */
	struct gpio_desc *enable_gpio;	/* When EN is low, this chip is reset */
};

static inline struct icn6211 *bridge_to_icn6211(struct drm_bridge *b)
{
	return container_of(b, struct icn6211, base);
}

static inline struct icn6211 *connector_to_icn6211(struct drm_connector *c)
{
	return container_of(c, struct icn6211, connector);
}

static enum drm_connector_status
icn6211_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs icn6211_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = icn6211_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct drm_encoder *
icn6211_connector_best_encoder(struct drm_connector *connector)
{
	struct icn6211 *icn6211 = connector_to_icn6211(connector);

	return icn6211->base.encoder;
}

static int icn6211_connector_get_modes(struct drm_connector *connector)
{
	struct icn6211 *icn6211 = connector_to_icn6211(connector);

	return drm_panel_get_modes(icn6211->panel);
}

static const struct drm_connector_helper_funcs
icn6211_connector_helper_funcs = {
	.get_modes = icn6211_connector_get_modes,
	.best_encoder = icn6211_connector_best_encoder,
};

static void icn6211_bridge_disable(struct drm_bridge *bridge)
{
	struct icn6211 *icn6211 = bridge_to_icn6211(bridge);

	if (icn6211->panel)
		drm_panel_disable(icn6211->panel);
}

static void icn6211_bridge_enable(struct drm_bridge *bridge)
{
	struct icn6211 *icn6211 = bridge_to_icn6211(bridge);

	if (icn6211->panel)
		drm_panel_enable(icn6211->panel);
}

static void icn6211_bridge_post_disable(struct drm_bridge *bridge)
{
	struct icn6211 *icn6211 = bridge_to_icn6211(bridge);

	if (icn6211->panel)
		drm_panel_unprepare(icn6211->panel);

	if (icn6211->enable_gpio)
		gpiod_direction_output(icn6211->enable_gpio, 0);

	regulator_disable(icn6211->vdd3);
	regulator_disable(icn6211->vdd2);
	regulator_disable(icn6211->vdd1);

	clk_disable_unprepare(icn6211->refclk);
}

static void icn6211_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct icn6211 *icn6211 = bridge_to_icn6211(bridge);
	const struct drm_display_mode *mode = &icn6211->mode;
	u32 hactive, hfp, hsw, hbp, vactive, vfp, vsw, vbp;
	u8 hactive_l, hactive_h, hfp_l, hfp_h, hbp_l, hbp_h, hsw_l, hsw_h;
	u8 vactive_l, vactive_h;
	u32 device_id_h, device_id_l;
	u32 pll_refdiv, pll_extra_div, pll_dv, pll_int;
	unsigned long refclk = clk_get_rate(icn6211->refclk);
	int ret;

	clk_prepare_enable(icn6211->refclk);

	ret = regulator_enable(icn6211->vdd1);
	if (ret)
		dev_err(icn6211->dev,
			"failed to enable vdd1 supply: %d\n", ret);

	ret = regulator_enable(icn6211->vdd2);
	if (ret)
		dev_err(icn6211->dev,
			"failed to enable vdd2 supply: %d\n", ret);

	ret = regulator_enable(icn6211->vdd3);
	if (ret)
		dev_err(icn6211->dev,
			"failed to enable vdd3 supply: %d\n", ret);

	if (icn6211->enable_gpio)
		gpiod_direction_output(icn6211->enable_gpio, 1);

	usleep_range(10000, 11000);

	regmap_read(icn6211->regmap, DEVICE_ID_H, &device_id_h);
	regmap_read(icn6211->regmap, DEVICE_ID_L, &device_id_l);
	dev_info(icn6211->dev, "The ID of device: 0x%04x\n",
		 (device_id_h << 8) | device_id_l);

	hactive = mode->hdisplay;
	hfp = mode->hsync_start - mode->hdisplay;
	hsw = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;
	vactive = mode->vdisplay;
	vfp = mode->vsync_start - mode->vdisplay;
	vsw = mode->vsync_end - mode->vsync_start;
	vbp = mode->vtotal - mode->vsync_end;

	hactive_l = hactive & 0xff;
	hactive_h = (hactive >> 8) & 0xf;
	vactive_l = vactive & 0xff;
	vactive_h = (vactive >> 8) & 0xf;
	hfp_l = hfp & 0xff;
	hfp_h = (hfp >> 8) & 0x3;
	hsw_l = hsw & 0xff;
	hsw_h = (hsw >> 8) & 0x3;
	hbp_l = hbp & 0xff;
	hbp_h = (hbp >> 8) & 0x3;

	regmap_write(icn6211->regmap, HACTIVE_L, hactive_l);
	regmap_write(icn6211->regmap, VACTIVE_L, vactive_l);
	regmap_write(icn6211->regmap, VACTIVE_HACTIVE_H,
		     (vactive_h << 4) | hactive_h);
	regmap_write(icn6211->regmap, HFP_L, hfp_l);
	regmap_write(icn6211->regmap, HSW_L, hsw_l);
	regmap_write(icn6211->regmap, HBP_L, hbp_l);
	regmap_write(icn6211->regmap, HFP_HSW_HBP_H,
		     (hfp_h << 4) | (hsw_h << 2) | hbp_h);
	regmap_write(icn6211->regmap, VFP, vfp);
	regmap_write(icn6211->regmap, VSW, vsw);
	regmap_write(icn6211->regmap, VBP, vbp);
	regmap_write(icn6211->regmap, SYNC_EVENT_DLY_LOW, 0x80);
	regmap_write(icn6211->regmap, HFP_MIN, hfp);
	regmap_write(icn6211->regmap, MIPI_PD_CK_LANE, 0xa0);
	regmap_write(icn6211->regmap, PLL_CTRL_C, 0xff);
	regmap_write(icn6211->regmap, BIST_POL, 0x01);
	regmap_write(icn6211->regmap, PLL_CTRL_6, 0x90);

	/*
	 * FIXME:
	 * fout = fin / pll_refdiv / pll_extra_div * pll_int / pll_dv / 2
	 */
	pll_refdiv = 1;
	pll_extra_div = 2;

	if (mode->clock <= 44000) {
		pll_dv = 8;
		regmap_write(icn6211->regmap, PLL_REF_DIV, 0x71);
	} else if (mode->clock <= 88000) {
		pll_dv = 4;
		regmap_write(icn6211->regmap, PLL_REF_DIV, 0x51);
	} else {
		pll_dv = 2;
		regmap_write(icn6211->regmap, PLL_REF_DIV, 0x31);
	}

	pll_int = DIV_ROUND_UP(mode->clock * 1000 * 2 * pll_dv,
			       refclk / pll_refdiv / pll_extra_div);
	regmap_write(icn6211->regmap, PLL_INT_0, pll_int);

	dev_dbg(icn6211->dev,
		"pll_refdiv=%d, pll_extra_div=%d, pll_int=%d, pll_dv=%d\n",
		pll_refdiv, pll_extra_div, pll_int, pll_dv);
	dev_dbg(icn6211->dev, "fin=%ld, fout=%ld\n", refclk,
		refclk / pll_refdiv / pll_extra_div * pll_int / pll_dv / 2);

	/*
	 * TODO:
	 *			RGB color swap mode
	 * RGB_SWAP	000	001	010	011	100	101
	 * Group_0	Red	Red	Green	Green	Blue	Blue
	 * Group_1	Green	Blue	Red	Blue	Red	Green
	 * Group_2	Blue	Green	Blue	Red	Green	Red
	 *
	 *			Data bit order mode
	 * BIT_ORDER	000	 001	  010	   011	    100	     101
	 * Group_X[7]	invalid	 invalid  Color[5] Color[0] Color[7] Color[0]
	 * Group_X[6]	invalid	 invalid  Color[4] Color[1] Color[6] Color[1]
	 * Group_X[5]	Color[5] Color[0] Color[3] Color[2] Color[5] Color[2]
	 * Group_X[4]	Color[4] Color[1] Color[2] Color[3] Color[4] Color[3]
	 * Group_X[3]	Color[3] Color[2] Color[1] Color[4] Color[3] Color[4]
	 * Group_X[2]	Color[2] Color[3] Color[0] Color[5] Color[2] Color[5]
	 * Group_X[1]	Color[1] Color[4] invaild  invaild  Color[1] Color[6]
	 * Group_X[0]	Color[0] Color[5] invaild  invaild  Color[0] Color[7]
	 *
	 * Note: Group_0[7:0] = DATA[7:0]
	 *	 Group_1[7:0] = DATA[15:8]
	 *	 Group_2[7:0] = DATA[23:16]
	 */
	regmap_write(icn6211->regmap, SYS_CTRL_0, 0x45);
	regmap_write(icn6211->regmap, SYS_CTRL_1, 0x88);
	regmap_write(icn6211->regmap, MIPI_FORCE_0, 0x20);
	regmap_write(icn6211->regmap, PLL_CTRL_1, 0x20);
	regmap_write(icn6211->regmap, CONFIG_FINISH, 0x10);

	if (icn6211->panel)
		drm_panel_prepare(icn6211->panel);
}

static void icn6211_bridge_mode_set(struct drm_bridge *bridge,
				    struct drm_display_mode *mode,
				    struct drm_display_mode *adj)
{
	struct icn6211 *icn6211 = bridge_to_icn6211(bridge);

	drm_mode_copy(&icn6211->mode, adj);
}

static int icn6211_bridge_attach(struct drm_bridge *bridge)
{
	struct icn6211 *icn6211 = bridge_to_icn6211(bridge);
	struct drm_connector *connector = &icn6211->connector;
	struct drm_device *drm = bridge->dev;
	struct mipi_dsi_host *host = bridge->driver_private;
	struct mipi_dsi_device *dsi = &icn6211->dsi;
	int ret;

	dsi->lanes = 4;
	dsi->channel = 0;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET;
	dsi->host = host;

	ret = mipi_dsi_attach(dsi);
	if (ret) {
		dev_err(icn6211->dev, "failed to attach dsi host: %d\n", ret);
		return ret;
	}

	if (icn6211->bridge) {
		icn6211->bridge->encoder = bridge->encoder;

		ret = drm_bridge_attach(drm, icn6211->bridge);
		if (ret) {
			dev_err(icn6211->dev,
				"failed to attach bridge: %d\n", ret);
			return ret;
		}

		bridge->next = icn6211->bridge;
	} else {
		ret = drm_connector_init(drm, connector,
					 &icn6211_connector_funcs,
					 DRM_MODE_CONNECTOR_DPI);
		if (ret) {
			dev_err(icn6211->dev,
				"failed to initialize connector\n");
			return ret;
		}

		drm_connector_helper_add(connector,
					 &icn6211_connector_helper_funcs);
		drm_mode_connector_attach_encoder(connector, bridge->encoder);
		drm_panel_attach(icn6211->panel, connector);
	}

	return 0;
}

static const struct drm_bridge_funcs icn6211_bridge_funcs = {
	.attach = icn6211_bridge_attach,
	.mode_set = icn6211_bridge_mode_set,
	.pre_enable = icn6211_bridge_pre_enable,
	.enable = icn6211_bridge_enable,
	.disable = icn6211_bridge_disable,
	.post_disable = icn6211_bridge_post_disable,
};

static const struct regmap_config icn6211_regmap_config = {
	.name = "icn6211",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = ICN6211_MAX_REGISTER,
};

static int icn6211_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct icn6211 *icn6211;
	int ret;

	icn6211 = devm_kzalloc(dev, sizeof(*icn6211), GFP_KERNEL);
	if (!icn6211)
		return -ENOMEM;

	icn6211->dev = dev;
	icn6211->client = client;
	i2c_set_clientdata(client, icn6211);

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1,
					  &icn6211->panel, &icn6211->bridge);
	if (ret)
		return ret;

	icn6211->refclk = devm_clk_get(dev, "refclk");
	if (IS_ERR(icn6211->refclk)) {
		ret = PTR_ERR(icn6211->refclk);
		dev_err(dev, "failed to get ref clk: %d\n", ret);
		return ret;
	}

	icn6211->vdd1 = devm_regulator_get(dev, "vdd1");
	if (IS_ERR(icn6211->vdd1))
		return PTR_ERR(icn6211->vdd1);

	icn6211->vdd2 = devm_regulator_get(dev, "vdd2");
	if (IS_ERR(icn6211->vdd2))
		return PTR_ERR(icn6211->vdd2);

	icn6211->vdd3 = devm_regulator_get(dev, "vdd3");
	if (IS_ERR(icn6211->vdd3))
		return PTR_ERR(icn6211->vdd3);

	icn6211->enable_gpio = devm_gpiod_get_optional(dev, "enable", 0);
	if (IS_ERR(icn6211->enable_gpio)) {
		ret = PTR_ERR(icn6211->enable_gpio);
		dev_err(dev, "failed to request enable GPIO: %d\n", ret);
		return ret;
	}

	icn6211->regmap = devm_regmap_init_i2c(client, &icn6211_regmap_config);
	if (IS_ERR(icn6211->regmap)) {
		ret = PTR_ERR(icn6211->regmap);
		dev_err(dev, "failed to initialize regmap: %d\n", ret);
		return ret;
	}

	icn6211->base.funcs = &icn6211_bridge_funcs;
	icn6211->base.of_node = dev->of_node;
	ret = drm_bridge_add(&icn6211->base);
	if (ret) {
		dev_err(dev, "failed to add drm_bridge: %d\n", ret);
		return ret;
	}

	return 0;
}

static int icn6211_i2c_remove(struct i2c_client *client)
{
	struct icn6211 *icn6211 = i2c_get_clientdata(client);

	drm_bridge_remove(&icn6211->base);

	return 0;
}

static const struct i2c_device_id icn6211_i2c_table[] = {
	{ "icn6211", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, icn6211_i2c_table);

static const struct of_device_id icn6211_of_match[] = {
	{ .compatible = "chipone,icn6211" },
	{}
};
MODULE_DEVICE_TABLE(of, icn6211_of_match);

static struct i2c_driver icn6211_i2c_driver = {
	.driver = {
		.name = "icn6211",
		.of_match_table = icn6211_of_match,
	},
	.probe = icn6211_i2c_probe,
	.remove = icn6211_i2c_remove,
	.id_table = icn6211_i2c_table,
};
module_i2c_driver(icn6211_i2c_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Chipone ICN6211 MIPI-DSI to RGB bridge chip driver");
MODULE_LICENSE("GPL v2");
