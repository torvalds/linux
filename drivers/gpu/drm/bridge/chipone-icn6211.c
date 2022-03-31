// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Amarula Solutions(India)
 * Author: Jagan Teki <jagan@amarulasolutions.com>
 */

#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_mipi_dsi.h>

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#define VENDOR_ID		0x00
#define DEVICE_ID_H		0x01
#define DEVICE_ID_L		0x02
#define VERSION_ID		0x03
#define FIRMWARE_VERSION	0x08
#define CONFIG_FINISH		0x09
#define PD_CTRL(n)		(0x0a + ((n) & 0x3)) /* 0..3 */
#define RST_CTRL(n)		(0x0e + ((n) & 0x1)) /* 0..1 */
#define SYS_CTRL(n)		(0x10 + ((n) & 0x7)) /* 0..4 */
#define RGB_DRV(n)		(0x18 + ((n) & 0x3)) /* 0..3 */
#define RGB_DLY(n)		(0x1c + ((n) & 0x1)) /* 0..1 */
#define RGB_TEST_CTRL		0x1e
#define ATE_PLL_EN		0x1f
#define HACTIVE_LI		0x20
#define VACTIVE_LI		0x21
#define VACTIVE_HACTIVE_HI	0x22
#define HFP_LI			0x23
#define HSYNC_LI		0x24
#define HBP_LI			0x25
#define HFP_HSW_HBP_HI		0x26
#define VFP			0x27
#define VSYNC			0x28
#define VBP			0x29
#define BIST_POL		0x2a
#define BIST_POL_BIST_MODE(n)		(((n) & 0xf) << 4)
#define BIST_POL_BIST_GEN		BIT(3)
#define BIST_POL_HSYNC_POL		BIT(2)
#define BIST_POL_VSYNC_POL		BIT(1)
#define BIST_POL_DE_POL			BIT(0)
#define BIST_RED		0x2b
#define BIST_GREEN		0x2c
#define BIST_BLUE		0x2d
#define BIST_CHESS_X		0x2e
#define BIST_CHESS_Y		0x2f
#define BIST_CHESS_XY_H		0x30
#define BIST_FRAME_TIME_L	0x31
#define BIST_FRAME_TIME_H	0x32
#define FIFO_MAX_ADDR_LOW	0x33
#define SYNC_EVENT_DLY		0x34
#define HSW_MIN			0x35
#define HFP_MIN			0x36
#define LOGIC_RST_NUM		0x37
#define OSC_CTRL(n)		(0x48 + ((n) & 0x7)) /* 0..5 */
#define BG_CTRL			0x4e
#define LDO_PLL			0x4f
#define PLL_CTRL(n)		(0x50 + ((n) & 0xf)) /* 0..15 */
#define PLL_CTRL_6_EXTERNAL		0x90
#define PLL_CTRL_6_MIPI_CLK		0x92
#define PLL_CTRL_6_INTERNAL		0x93
#define PLL_REM(n)		(0x60 + ((n) & 0x3)) /* 0..2 */
#define PLL_DIV(n)		(0x63 + ((n) & 0x3)) /* 0..2 */
#define PLL_FRAC(n)		(0x66 + ((n) & 0x3)) /* 0..2 */
#define PLL_INT(n)		(0x69 + ((n) & 0x1)) /* 0..1 */
#define PLL_REF_DIV		0x6b
#define PLL_REF_DIV_P(n)		((n) & 0xf)
#define PLL_REF_DIV_Pe			BIT(4)
#define PLL_REF_DIV_S(n)		(((n) & 0x7) << 5)
#define PLL_SSC_P(n)		(0x6c + ((n) & 0x3)) /* 0..2 */
#define PLL_SSC_STEP(n)		(0x6f + ((n) & 0x3)) /* 0..2 */
#define PLL_SSC_OFFSET(n)	(0x72 + ((n) & 0x3)) /* 0..3 */
#define GPIO_OEN		0x79
#define MIPI_CFG_PW		0x7a
#define MIPI_CFG_PW_CONFIG_DSI		0xc1
#define MIPI_CFG_PW_CONFIG_I2C		0x3e
#define GPIO_SEL(n)		(0x7b + ((n) & 0x1)) /* 0..1 */
#define IRQ_SEL			0x7d
#define DBG_SEL			0x7e
#define DBG_SIGNAL		0x7f
#define MIPI_ERR_VECTOR_L	0x80
#define MIPI_ERR_VECTOR_H	0x81
#define MIPI_ERR_VECTOR_EN_L	0x82
#define MIPI_ERR_VECTOR_EN_H	0x83
#define MIPI_MAX_SIZE_L		0x84
#define MIPI_MAX_SIZE_H		0x85
#define DSI_CTRL		0x86
#define DSI_CTRL_UNKNOWN		0x28
#define DSI_CTRL_DSI_LANES(n)		((n) & 0x3)
#define MIPI_PN_SWAP		0x87
#define MIPI_PN_SWAP_CLK		BIT(4)
#define MIPI_PN_SWAP_D(n)		BIT((n) & 0x3)
#define MIPI_SOT_SYNC_BIT_(n)	(0x88 + ((n) & 0x1)) /* 0..1 */
#define MIPI_ULPS_CTRL		0x8a
#define MIPI_CLK_CHK_VAR	0x8e
#define MIPI_CLK_CHK_INI	0x8f
#define MIPI_T_TERM_EN		0x90
#define MIPI_T_HS_SETTLE	0x91
#define MIPI_T_TA_SURE_PRE	0x92
#define MIPI_T_LPX_SET		0x94
#define MIPI_T_CLK_MISS		0x95
#define MIPI_INIT_TIME_L	0x96
#define MIPI_INIT_TIME_H	0x97
#define MIPI_T_CLK_TERM_EN	0x99
#define MIPI_T_CLK_SETTLE	0x9a
#define MIPI_TO_HS_RX_L		0x9e
#define MIPI_TO_HS_RX_H		0x9f
#define MIPI_PHY_(n)		(0xa0 + ((n) & 0x7)) /* 0..5 */
#define MIPI_PD_RX		0xb0
#define MIPI_PD_TERM		0xb1
#define MIPI_PD_HSRX		0xb2
#define MIPI_PD_LPTX		0xb3
#define MIPI_PD_LPRX		0xb4
#define MIPI_PD_CK_LANE		0xb5
#define MIPI_FORCE_0		0xb6
#define MIPI_RST_CTRL		0xb7
#define MIPI_RST_NUM		0xb8
#define MIPI_DBG_SET_(n)	(0xc0 + ((n) & 0xf)) /* 0..9 */
#define MIPI_DBG_SEL		0xe0
#define MIPI_DBG_DATA		0xe1
#define MIPI_ATE_TEST_SEL	0xe2
#define MIPI_ATE_STATUS_(n)	(0xe3 + ((n) & 0x1)) /* 0..1 */
#define MIPI_ATE_STATUS_1	0xe4
#define ICN6211_MAX_REGISTER	MIPI_ATE_STATUS(1)

struct chipone {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_bridge *panel_bridge;
	struct gpio_desc *enable_gpio;
	struct regulator *vdd1;
	struct regulator *vdd2;
	struct regulator *vdd3;
};

static inline struct chipone *bridge_to_chipone(struct drm_bridge *bridge)
{
	return container_of(bridge, struct chipone, bridge);
}

static struct drm_display_mode *bridge_to_mode(struct drm_bridge *bridge)
{
	return &bridge->encoder->crtc->state->adjusted_mode;
}

static inline int chipone_dsi_write(struct chipone *icn,  const void *seq,
				    size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(icn->dev);

	return mipi_dsi_generic_write(dsi, seq, len);
}

#define ICN6211_DSI(icn, seq...)				\
	{							\
		const u8 d[] = { seq };				\
		chipone_dsi_write(icn, d, ARRAY_SIZE(d));	\
	}

static void chipone_enable(struct drm_bridge *bridge)
{
	struct chipone *icn = bridge_to_chipone(bridge);
	struct drm_display_mode *mode = bridge_to_mode(bridge);

	ICN6211_DSI(icn, MIPI_CFG_PW, MIPI_CFG_PW_CONFIG_DSI);

	ICN6211_DSI(icn, HACTIVE_LI, mode->hdisplay & 0xff);

	ICN6211_DSI(icn, VACTIVE_LI, mode->vdisplay & 0xff);

	/*
	 * lsb nibble: 2nd nibble of hdisplay
	 * msb nibble: 2nd nibble of vdisplay
	 */
	ICN6211_DSI(icn, VACTIVE_HACTIVE_HI,
		    ((mode->hdisplay >> 8) & 0xf) |
		    (((mode->vdisplay >> 8) & 0xf) << 4));

	ICN6211_DSI(icn, HFP_LI, mode->hsync_start - mode->hdisplay);

	ICN6211_DSI(icn, HSYNC_LI, mode->hsync_end - mode->hsync_start);

	ICN6211_DSI(icn, HBP_LI, mode->htotal - mode->hsync_end);

	ICN6211_DSI(icn, HFP_HSW_HBP_HI, 0x00);

	ICN6211_DSI(icn, VFP, mode->vsync_start - mode->vdisplay);

	ICN6211_DSI(icn, VSYNC, mode->vsync_end - mode->vsync_start);

	ICN6211_DSI(icn, VBP, mode->vtotal - mode->vsync_end);

	/* dsi specific sequence */
	ICN6211_DSI(icn, SYNC_EVENT_DLY, 0x80);
	ICN6211_DSI(icn, HFP_MIN, 0x28);
	ICN6211_DSI(icn, MIPI_PD_CK_LANE, 0xa0);
	ICN6211_DSI(icn, PLL_CTRL(12), 0xff);
	ICN6211_DSI(icn, BIST_POL, BIST_POL_DE_POL);
	ICN6211_DSI(icn, PLL_CTRL(6), PLL_CTRL_6_MIPI_CLK);
	ICN6211_DSI(icn, PLL_REF_DIV, 0x71);
	ICN6211_DSI(icn, PLL_INT(0), 0x2b);
	ICN6211_DSI(icn, SYS_CTRL(0), 0x40);
	ICN6211_DSI(icn, SYS_CTRL(1), 0x98);

	/* icn6211 specific sequence */
	ICN6211_DSI(icn, MIPI_FORCE_0, 0x20);
	ICN6211_DSI(icn, PLL_CTRL(1), 0x20);
	ICN6211_DSI(icn, CONFIG_FINISH, 0x10);

	usleep_range(10000, 11000);
}

static void chipone_pre_enable(struct drm_bridge *bridge)
{
	struct chipone *icn = bridge_to_chipone(bridge);
	int ret;

	if (icn->vdd1) {
		ret = regulator_enable(icn->vdd1);
		if (ret)
			DRM_DEV_ERROR(icn->dev,
				      "failed to enable VDD1 regulator: %d\n", ret);
	}

	if (icn->vdd2) {
		ret = regulator_enable(icn->vdd2);
		if (ret)
			DRM_DEV_ERROR(icn->dev,
				      "failed to enable VDD2 regulator: %d\n", ret);
	}

	if (icn->vdd3) {
		ret = regulator_enable(icn->vdd3);
		if (ret)
			DRM_DEV_ERROR(icn->dev,
				      "failed to enable VDD3 regulator: %d\n", ret);
	}

	gpiod_set_value(icn->enable_gpio, 1);

	usleep_range(10000, 11000);
}

static void chipone_post_disable(struct drm_bridge *bridge)
{
	struct chipone *icn = bridge_to_chipone(bridge);

	if (icn->vdd1)
		regulator_disable(icn->vdd1);

	if (icn->vdd2)
		regulator_disable(icn->vdd2);

	if (icn->vdd3)
		regulator_disable(icn->vdd3);

	gpiod_set_value(icn->enable_gpio, 0);
}

static int chipone_attach(struct drm_bridge *bridge, enum drm_bridge_attach_flags flags)
{
	struct chipone *icn = bridge_to_chipone(bridge);

	return drm_bridge_attach(bridge->encoder, icn->panel_bridge, bridge, flags);
}

static const struct drm_bridge_funcs chipone_bridge_funcs = {
	.attach = chipone_attach,
	.post_disable = chipone_post_disable,
	.pre_enable = chipone_pre_enable,
	.enable = chipone_enable,
};

static int chipone_parse_dt(struct chipone *icn)
{
	struct device *dev = icn->dev;
	struct drm_panel *panel;
	int ret;

	icn->vdd1 = devm_regulator_get_optional(dev, "vdd1");
	if (IS_ERR(icn->vdd1)) {
		ret = PTR_ERR(icn->vdd1);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		icn->vdd1 = NULL;
		DRM_DEV_DEBUG(dev, "failed to get VDD1 regulator: %d\n", ret);
	}

	icn->vdd2 = devm_regulator_get_optional(dev, "vdd2");
	if (IS_ERR(icn->vdd2)) {
		ret = PTR_ERR(icn->vdd2);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		icn->vdd2 = NULL;
		DRM_DEV_DEBUG(dev, "failed to get VDD2 regulator: %d\n", ret);
	}

	icn->vdd3 = devm_regulator_get_optional(dev, "vdd3");
	if (IS_ERR(icn->vdd3)) {
		ret = PTR_ERR(icn->vdd3);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		icn->vdd3 = NULL;
		DRM_DEV_DEBUG(dev, "failed to get VDD3 regulator: %d\n", ret);
	}

	icn->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(icn->enable_gpio)) {
		DRM_DEV_ERROR(dev, "failed to get enable GPIO\n");
		return PTR_ERR(icn->enable_gpio);
	}

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, 0, &panel, NULL);
	if (ret)
		return ret;

	icn->panel_bridge = devm_drm_panel_bridge_add(dev, panel);
	if (IS_ERR(icn->panel_bridge))
		return PTR_ERR(icn->panel_bridge);

	return 0;
}

static int chipone_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct chipone *icn;
	int ret;

	icn = devm_kzalloc(dev, sizeof(struct chipone), GFP_KERNEL);
	if (!icn)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, icn);
	icn->dev = dev;

	ret = chipone_parse_dt(icn);
	if (ret)
		return ret;

	icn->bridge.funcs = &chipone_bridge_funcs;
	icn->bridge.type = DRM_MODE_CONNECTOR_DPI;
	icn->bridge.of_node = dev->of_node;

	drm_bridge_add(&icn->bridge);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_bridge_remove(&icn->bridge);
		dev_err(dev, "failed to attach dsi\n");
	}

	return ret;
}

static int chipone_remove(struct mipi_dsi_device *dsi)
{
	struct chipone *icn = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_bridge_remove(&icn->bridge);

	return 0;
}

static const struct of_device_id chipone_of_match[] = {
	{ .compatible = "chipone,icn6211", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, chipone_of_match);

static struct mipi_dsi_driver chipone_driver = {
	.probe = chipone_probe,
	.remove = chipone_remove,
	.driver = {
		.name = "chipone-icn6211",
		.owner = THIS_MODULE,
		.of_match_table = chipone_of_match,
	},
};
module_mipi_dsi_driver(chipone_driver);

MODULE_AUTHOR("Jagan Teki <jagan@amarulasolutions.com>");
MODULE_DESCRIPTION("Chipone ICN6211 MIPI-DSI to RGB Converter Bridge");
MODULE_LICENSE("GPL");
