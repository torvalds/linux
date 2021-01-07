// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Samsung Electronics Co., Ltd
 *
 * Authors:
 *	Andrzej Hajda <a.hajda@samsung.com>
 *	Maciej Purski <m.purski@samsung.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#define FLD_MASK(start, end)    (((1 << ((start) - (end) + 1)) - 1) << (end))
#define FLD_VAL(val, start, end) (((val) << (end)) & FLD_MASK(start, end))

/* PPI layer registers */
#define PPI_STARTPPI		0x0104 /* START control bit */
#define PPI_LPTXTIMECNT		0x0114 /* LPTX timing signal */
#define PPI_LANEENABLE		0x0134 /* Enables each lane */
#define PPI_TX_RX_TA		0x013C /* BTA timing parameters */
#define PPI_D0S_CLRSIPOCOUNT	0x0164 /* Assertion timer for Lane 0 */
#define PPI_D1S_CLRSIPOCOUNT	0x0168 /* Assertion timer for Lane 1 */
#define PPI_D2S_CLRSIPOCOUNT	0x016C /* Assertion timer for Lane 2 */
#define PPI_D3S_CLRSIPOCOUNT	0x0170 /* Assertion timer for Lane 3 */
#define PPI_START_FUNCTION	1

/* DSI layer registers */
#define DSI_STARTDSI		0x0204 /* START control bit of DSI-TX */
#define DSI_LANEENABLE		0x0210 /* Enables each lane */
#define DSI_RX_START		1

/* Video path registers */
#define VP_CTRL			0x0450 /* Video Path Control */
#define VP_CTRL_MSF(v)		FLD_VAL(v, 0, 0) /* Magic square in RGB666 */
#define VP_CTRL_VTGEN(v)	FLD_VAL(v, 4, 4) /* Use chip clock for timing */
#define VP_CTRL_EVTMODE(v)	FLD_VAL(v, 5, 5) /* Event mode */
#define VP_CTRL_RGB888(v)	FLD_VAL(v, 8, 8) /* RGB888 mode */
#define VP_CTRL_VSDELAY(v)	FLD_VAL(v, 31, 20) /* VSYNC delay */
#define VP_CTRL_HSPOL		BIT(17) /* Polarity of HSYNC signal */
#define VP_CTRL_DEPOL		BIT(18) /* Polarity of DE signal */
#define VP_CTRL_VSPOL		BIT(19) /* Polarity of VSYNC signal */
#define VP_HTIM1		0x0454 /* Horizontal Timing Control 1 */
#define VP_HTIM1_HBP(v)		FLD_VAL(v, 24, 16)
#define VP_HTIM1_HSYNC(v)	FLD_VAL(v, 8, 0)
#define VP_HTIM2		0x0458 /* Horizontal Timing Control 2 */
#define VP_HTIM2_HFP(v)		FLD_VAL(v, 24, 16)
#define VP_HTIM2_HACT(v)	FLD_VAL(v, 10, 0)
#define VP_VTIM1		0x045C /* Vertical Timing Control 1 */
#define VP_VTIM1_VBP(v)		FLD_VAL(v, 23, 16)
#define VP_VTIM1_VSYNC(v)	FLD_VAL(v, 7, 0)
#define VP_VTIM2		0x0460 /* Vertical Timing Control 2 */
#define VP_VTIM2_VFP(v)		FLD_VAL(v, 23, 16)
#define VP_VTIM2_VACT(v)	FLD_VAL(v, 10, 0)
#define VP_VFUEN		0x0464 /* Video Frame Timing Update Enable */

/* LVDS registers */
#define LV_MX0003		0x0480 /* Mux input bit 0 to 3 */
#define LV_MX0407		0x0484 /* Mux input bit 4 to 7 */
#define LV_MX0811		0x0488 /* Mux input bit 8 to 11 */
#define LV_MX1215		0x048C /* Mux input bit 12 to 15 */
#define LV_MX1619		0x0490 /* Mux input bit 16 to 19 */
#define LV_MX2023		0x0494 /* Mux input bit 20 to 23 */
#define LV_MX2427		0x0498 /* Mux input bit 24 to 27 */
#define LV_MX(b0, b1, b2, b3)	(FLD_VAL(b0, 4, 0) | FLD_VAL(b1, 12, 8) | \
				FLD_VAL(b2, 20, 16) | FLD_VAL(b3, 28, 24))

/* Input bit numbers used in mux registers */
enum {
	LVI_R0,
	LVI_R1,
	LVI_R2,
	LVI_R3,
	LVI_R4,
	LVI_R5,
	LVI_R6,
	LVI_R7,
	LVI_G0,
	LVI_G1,
	LVI_G2,
	LVI_G3,
	LVI_G4,
	LVI_G5,
	LVI_G6,
	LVI_G7,
	LVI_B0,
	LVI_B1,
	LVI_B2,
	LVI_B3,
	LVI_B4,
	LVI_B5,
	LVI_B6,
	LVI_B7,
	LVI_HS,
	LVI_VS,
	LVI_DE,
	LVI_L0
};

#define LV_CFG			0x049C /* LVDS Configuration */
#define LV_PHY0			0x04A0 /* LVDS PHY 0 */
#define LV_PHY0_RST(v)		FLD_VAL(v, 22, 22) /* PHY reset */
#define LV_PHY0_IS(v)		FLD_VAL(v, 15, 14)
#define LV_PHY0_ND(v)		FLD_VAL(v, 4, 0) /* Frequency range select */
#define LV_PHY0_PRBS_ON(v)	FLD_VAL(v, 20, 16) /* Clock/Data Flag pins */

/* System registers */
#define SYS_RST			0x0504 /* System Reset */
#define SYS_ID			0x0580 /* System ID */

#define SYS_RST_I2CS		BIT(0) /* Reset I2C-Slave controller */
#define SYS_RST_I2CM		BIT(1) /* Reset I2C-Master controller */
#define SYS_RST_LCD		BIT(2) /* Reset LCD controller */
#define SYS_RST_BM		BIT(3) /* Reset Bus Management controller */
#define SYS_RST_DSIRX		BIT(4) /* Reset DSI-RX and App controller */
#define SYS_RST_REG		BIT(5) /* Reset Register module */

#define LPX_PERIOD		2
#define TTA_SURE		3
#define TTA_GET			0x20000

/* Lane enable PPI and DSI register bits */
#define LANEENABLE_CLEN		BIT(0)
#define LANEENABLE_L0EN		BIT(1)
#define LANEENABLE_L1EN		BIT(2)
#define LANEENABLE_L2EN		BIT(3)
#define LANEENABLE_L3EN		BIT(4)

/* LVCFG fields */
#define LV_CFG_LVEN		BIT(0)
#define LV_CFG_LVDLINK		BIT(1)
#define LV_CFG_CLKPOL1		BIT(2)
#define LV_CFG_CLKPOL2		BIT(3)

static const char * const tc358764_supplies[] = {
	"vddc", "vddio", "vddlvds"
};

struct tc358764 {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct regulator_bulk_data supplies[ARRAY_SIZE(tc358764_supplies)];
	struct gpio_desc *gpio_reset;
	struct drm_panel *panel;
	int error;
};

static int tc358764_clear_error(struct tc358764 *ctx)
{
	int ret = ctx->error;

	ctx->error = 0;
	return ret;
}

static void tc358764_read(struct tc358764 *ctx, u16 addr, u32 *val)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error)
		return;

	cpu_to_le16s(&addr);
	ret = mipi_dsi_generic_read(dsi, &addr, sizeof(addr), val, sizeof(*val));
	if (ret >= 0)
		le32_to_cpus(val);

	dev_dbg(ctx->dev, "read: %d, addr: %d\n", addr, *val);
}

static void tc358764_write(struct tc358764 *ctx, u16 addr, u32 val)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	u8 data[6];

	if (ctx->error)
		return;

	data[0] = addr;
	data[1] = addr >> 8;
	data[2] = val;
	data[3] = val >> 8;
	data[4] = val >> 16;
	data[5] = val >> 24;

	ret = mipi_dsi_generic_write(dsi, data, sizeof(data));
	if (ret < 0)
		ctx->error = ret;
}

static inline struct tc358764 *bridge_to_tc358764(struct drm_bridge *bridge)
{
	return container_of(bridge, struct tc358764, bridge);
}

static inline
struct tc358764 *connector_to_tc358764(struct drm_connector *connector)
{
	return container_of(connector, struct tc358764, connector);
}

static int tc358764_init(struct tc358764 *ctx)
{
	u32 v = 0;

	tc358764_read(ctx, SYS_ID, &v);
	if (ctx->error)
		return tc358764_clear_error(ctx);
	dev_info(ctx->dev, "ID: %#x\n", v);

	/* configure PPI counters */
	tc358764_write(ctx, PPI_TX_RX_TA, TTA_GET | TTA_SURE);
	tc358764_write(ctx, PPI_LPTXTIMECNT, LPX_PERIOD);
	tc358764_write(ctx, PPI_D0S_CLRSIPOCOUNT, 5);
	tc358764_write(ctx, PPI_D1S_CLRSIPOCOUNT, 5);
	tc358764_write(ctx, PPI_D2S_CLRSIPOCOUNT, 5);
	tc358764_write(ctx, PPI_D3S_CLRSIPOCOUNT, 5);

	/* enable four data lanes and clock lane */
	tc358764_write(ctx, PPI_LANEENABLE, LANEENABLE_L3EN | LANEENABLE_L2EN |
		       LANEENABLE_L1EN | LANEENABLE_L0EN | LANEENABLE_CLEN);
	tc358764_write(ctx, DSI_LANEENABLE, LANEENABLE_L3EN | LANEENABLE_L2EN |
		       LANEENABLE_L1EN | LANEENABLE_L0EN | LANEENABLE_CLEN);

	/* start */
	tc358764_write(ctx, PPI_STARTPPI, PPI_START_FUNCTION);
	tc358764_write(ctx, DSI_STARTDSI, DSI_RX_START);

	/* configure video path */
	tc358764_write(ctx, VP_CTRL, VP_CTRL_VSDELAY(15) | VP_CTRL_RGB888(1) |
		       VP_CTRL_EVTMODE(1) | VP_CTRL_HSPOL | VP_CTRL_VSPOL);

	/* reset PHY */
	tc358764_write(ctx, LV_PHY0, LV_PHY0_RST(1) |
		       LV_PHY0_PRBS_ON(4) | LV_PHY0_IS(2) | LV_PHY0_ND(6));
	tc358764_write(ctx, LV_PHY0, LV_PHY0_PRBS_ON(4) | LV_PHY0_IS(2) |
		       LV_PHY0_ND(6));

	/* reset bridge */
	tc358764_write(ctx, SYS_RST, SYS_RST_LCD);

	/* set bit order */
	tc358764_write(ctx, LV_MX0003, LV_MX(LVI_R0, LVI_R1, LVI_R2, LVI_R3));
	tc358764_write(ctx, LV_MX0407, LV_MX(LVI_R4, LVI_R7, LVI_R5, LVI_G0));
	tc358764_write(ctx, LV_MX0811, LV_MX(LVI_G1, LVI_G2, LVI_G6, LVI_G7));
	tc358764_write(ctx, LV_MX1215, LV_MX(LVI_G3, LVI_G4, LVI_G5, LVI_B0));
	tc358764_write(ctx, LV_MX1619, LV_MX(LVI_B6, LVI_B7, LVI_B1, LVI_B2));
	tc358764_write(ctx, LV_MX2023, LV_MX(LVI_B3, LVI_B4, LVI_B5, LVI_L0));
	tc358764_write(ctx, LV_MX2427, LV_MX(LVI_HS, LVI_VS, LVI_DE, LVI_R6));
	tc358764_write(ctx, LV_CFG, LV_CFG_CLKPOL2 | LV_CFG_CLKPOL1 |
		       LV_CFG_LVEN);

	return tc358764_clear_error(ctx);
}

static void tc358764_reset(struct tc358764 *ctx)
{
	gpiod_set_value(ctx->gpio_reset, 1);
	usleep_range(1000, 2000);
	gpiod_set_value(ctx->gpio_reset, 0);
	usleep_range(1000, 2000);
}

static int tc358764_get_modes(struct drm_connector *connector)
{
	struct tc358764 *ctx = connector_to_tc358764(connector);

	return drm_panel_get_modes(ctx->panel, connector);
}

static const
struct drm_connector_helper_funcs tc358764_connector_helper_funcs = {
	.get_modes = tc358764_get_modes,
};

static const struct drm_connector_funcs tc358764_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void tc358764_disable(struct drm_bridge *bridge)
{
	struct tc358764 *ctx = bridge_to_tc358764(bridge);
	int ret = drm_panel_disable(bridge_to_tc358764(bridge)->panel);

	if (ret < 0)
		dev_err(ctx->dev, "error disabling panel (%d)\n", ret);
}

static void tc358764_post_disable(struct drm_bridge *bridge)
{
	struct tc358764 *ctx = bridge_to_tc358764(bridge);
	int ret;

	ret = drm_panel_unprepare(ctx->panel);
	if (ret < 0)
		dev_err(ctx->dev, "error unpreparing panel (%d)\n", ret);
	tc358764_reset(ctx);
	usleep_range(10000, 15000);
	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		dev_err(ctx->dev, "error disabling regulators (%d)\n", ret);
}

static void tc358764_pre_enable(struct drm_bridge *bridge)
{
	struct tc358764 *ctx = bridge_to_tc358764(bridge);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		dev_err(ctx->dev, "error enabling regulators (%d)\n", ret);
	usleep_range(10000, 15000);
	tc358764_reset(ctx);
	ret = tc358764_init(ctx);
	if (ret < 0)
		dev_err(ctx->dev, "error initializing bridge (%d)\n", ret);
	ret = drm_panel_prepare(ctx->panel);
	if (ret < 0)
		dev_err(ctx->dev, "error preparing panel (%d)\n", ret);
}

static void tc358764_enable(struct drm_bridge *bridge)
{
	struct tc358764 *ctx = bridge_to_tc358764(bridge);
	int ret = drm_panel_enable(ctx->panel);

	if (ret < 0)
		dev_err(ctx->dev, "error enabling panel (%d)\n", ret);
}

static int tc358764_attach(struct drm_bridge *bridge,
			   enum drm_bridge_attach_flags flags)
{
	struct tc358764 *ctx = bridge_to_tc358764(bridge);
	struct drm_device *drm = bridge->dev;
	int ret;

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR) {
		DRM_ERROR("Fix bridge driver to make connector optional!");
		return -EINVAL;
	}

	ctx->connector.polled = DRM_CONNECTOR_POLL_HPD;
	ret = drm_connector_init(drm, &ctx->connector,
				 &tc358764_connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		return ret;
	}

	drm_connector_helper_add(&ctx->connector,
				 &tc358764_connector_helper_funcs);
	drm_connector_attach_encoder(&ctx->connector, bridge->encoder);
	ctx->connector.funcs->reset(&ctx->connector);
	drm_connector_register(&ctx->connector);

	return 0;
}

static void tc358764_detach(struct drm_bridge *bridge)
{
	struct tc358764 *ctx = bridge_to_tc358764(bridge);

	drm_connector_unregister(&ctx->connector);
	ctx->panel = NULL;
	drm_connector_put(&ctx->connector);
}

static const struct drm_bridge_funcs tc358764_bridge_funcs = {
	.disable = tc358764_disable,
	.post_disable = tc358764_post_disable,
	.enable = tc358764_enable,
	.pre_enable = tc358764_pre_enable,
	.attach = tc358764_attach,
	.detach = tc358764_detach,
};

static int tc358764_parse_dt(struct tc358764 *ctx)
{
	struct device *dev = ctx->dev;
	int ret;

	ctx->gpio_reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->gpio_reset)) {
		dev_err(dev, "no reset GPIO pin provided\n");
		return PTR_ERR(ctx->gpio_reset);
	}

	ret = drm_of_find_panel_or_bridge(ctx->dev->of_node, 1, 0, &ctx->panel,
					  NULL);
	if (ret && ret != -EPROBE_DEFER)
		dev_err(dev, "cannot find panel (%d)\n", ret);

	return ret;
}

static int tc358764_configure_regulators(struct tc358764 *ctx)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); ++i)
		ctx->supplies[i].supply = tc358764_supplies[i];

	ret = devm_regulator_bulk_get(ctx->dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		dev_err(ctx->dev, "failed to get regulators: %d\n", ret);

	return ret;
}

static int tc358764_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct tc358764 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(struct tc358764), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST
		| MIPI_DSI_MODE_VIDEO_AUTO_VERT | MIPI_DSI_MODE_LPM;

	ret = tc358764_parse_dt(ctx);
	if (ret < 0)
		return ret;

	ret = tc358764_configure_regulators(ctx);
	if (ret < 0)
		return ret;

	ctx->bridge.funcs = &tc358764_bridge_funcs;
	ctx->bridge.of_node = dev->of_node;

	drm_bridge_add(&ctx->bridge);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_bridge_remove(&ctx->bridge);
		dev_err(dev, "failed to attach dsi\n");
	}

	return ret;
}

static int tc358764_remove(struct mipi_dsi_device *dsi)
{
	struct tc358764 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_bridge_remove(&ctx->bridge);

	return 0;
}

static const struct of_device_id tc358764_of_match[] = {
	{ .compatible = "toshiba,tc358764" },
	{ }
};
MODULE_DEVICE_TABLE(of, tc358764_of_match);

static struct mipi_dsi_driver tc358764_driver = {
	.probe = tc358764_probe,
	.remove = tc358764_remove,
	.driver = {
		.name = "tc358764",
		.owner = THIS_MODULE,
		.of_match_table = tc358764_of_match,
	},
};
module_mipi_dsi_driver(tc358764_driver);

MODULE_AUTHOR("Andrzej Hajda <a.hajda@samsung.com>");
MODULE_AUTHOR("Maciej Purski <m.purski@samsung.com>");
MODULE_DESCRIPTION("MIPI-DSI based Driver for TC358764 DSI/LVDS Bridge");
MODULE_LICENSE("GPL v2");
