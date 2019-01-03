// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define SN_DEVICE_REV_REG			0x08
#define SN_DPPLL_SRC_REG			0x0A
#define  DPPLL_CLK_SRC_DSICLK			BIT(0)
#define  REFCLK_FREQ_MASK			GENMASK(3, 1)
#define  REFCLK_FREQ(x)				((x) << 1)
#define  DPPLL_SRC_DP_PLL_LOCK			BIT(7)
#define SN_PLL_ENABLE_REG			0x0D
#define SN_DSI_LANES_REG			0x10
#define  CHA_DSI_LANES_MASK			GENMASK(4, 3)
#define  CHA_DSI_LANES(x)			((x) << 3)
#define SN_DSIA_CLK_FREQ_REG			0x12
#define SN_CHA_ACTIVE_LINE_LENGTH_LOW_REG	0x20
#define SN_CHA_VERTICAL_DISPLAY_SIZE_LOW_REG	0x24
#define SN_CHA_HSYNC_PULSE_WIDTH_LOW_REG	0x2C
#define SN_CHA_HSYNC_PULSE_WIDTH_HIGH_REG	0x2D
#define  CHA_HSYNC_POLARITY			BIT(7)
#define SN_CHA_VSYNC_PULSE_WIDTH_LOW_REG	0x30
#define SN_CHA_VSYNC_PULSE_WIDTH_HIGH_REG	0x31
#define  CHA_VSYNC_POLARITY			BIT(7)
#define SN_CHA_HORIZONTAL_BACK_PORCH_REG	0x34
#define SN_CHA_VERTICAL_BACK_PORCH_REG		0x36
#define SN_CHA_HORIZONTAL_FRONT_PORCH_REG	0x38
#define SN_CHA_VERTICAL_FRONT_PORCH_REG		0x3A
#define SN_ENH_FRAME_REG			0x5A
#define  VSTREAM_ENABLE				BIT(3)
#define SN_DATA_FORMAT_REG			0x5B
#define SN_HPD_DISABLE_REG			0x5C
#define  HPD_DISABLE				BIT(0)
#define SN_AUX_WDATA_REG(x)			(0x64 + (x))
#define SN_AUX_ADDR_19_16_REG			0x74
#define SN_AUX_ADDR_15_8_REG			0x75
#define SN_AUX_ADDR_7_0_REG			0x76
#define SN_AUX_LENGTH_REG			0x77
#define SN_AUX_CMD_REG				0x78
#define  AUX_CMD_SEND				BIT(0)
#define  AUX_CMD_REQ(x)				((x) << 4)
#define SN_AUX_RDATA_REG(x)			(0x79 + (x))
#define SN_SSC_CONFIG_REG			0x93
#define  DP_NUM_LANES_MASK			GENMASK(5, 4)
#define  DP_NUM_LANES(x)			((x) << 4)
#define SN_DATARATE_CONFIG_REG			0x94
#define  DP_DATARATE_MASK			GENMASK(7, 5)
#define  DP_DATARATE(x)				((x) << 5)
#define SN_ML_TX_MODE_REG			0x96
#define  ML_TX_MAIN_LINK_OFF			0
#define  ML_TX_NORMAL_MODE			BIT(0)
#define SN_AUX_CMD_STATUS_REG			0xF4
#define  AUX_IRQ_STATUS_AUX_RPLY_TOUT		BIT(3)
#define  AUX_IRQ_STATUS_AUX_SHORT		BIT(5)
#define  AUX_IRQ_STATUS_NAT_I2C_FAIL		BIT(6)

#define MIN_DSI_CLK_FREQ_MHZ	40

/* fudge factor required to account for 8b/10b encoding */
#define DP_CLK_FUDGE_NUM	10
#define DP_CLK_FUDGE_DEN	8

/* Matches DP_AUX_MAX_PAYLOAD_BYTES (for now) */
#define SN_AUX_MAX_PAYLOAD_BYTES	16

#define SN_REGULATOR_SUPPLY_NUM		4

struct ti_sn_bridge {
	struct device			*dev;
	struct regmap			*regmap;
	struct drm_dp_aux		aux;
	struct drm_bridge		bridge;
	struct drm_connector		connector;
	struct device_node		*host_node;
	struct mipi_dsi_device		*dsi;
	struct clk			*refclk;
	struct drm_panel		*panel;
	struct gpio_desc		*enable_gpio;
	struct regulator_bulk_data	supplies[SN_REGULATOR_SUPPLY_NUM];
};

static const struct regmap_range ti_sn_bridge_volatile_ranges[] = {
	{ .range_min = 0, .range_max = 0xFF },
};

static const struct regmap_access_table ti_sn_bridge_volatile_table = {
	.yes_ranges = ti_sn_bridge_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(ti_sn_bridge_volatile_ranges),
};

static const struct regmap_config ti_sn_bridge_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &ti_sn_bridge_volatile_table,
	.cache_type = REGCACHE_NONE,
};

static void ti_sn_bridge_write_u16(struct ti_sn_bridge *pdata,
				   unsigned int reg, u16 val)
{
	regmap_write(pdata->regmap, reg, val & 0xFF);
	regmap_write(pdata->regmap, reg + 1, val >> 8);
}

static int __maybe_unused ti_sn_bridge_resume(struct device *dev)
{
	struct ti_sn_bridge *pdata = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(SN_REGULATOR_SUPPLY_NUM, pdata->supplies);
	if (ret) {
		DRM_ERROR("failed to enable supplies %d\n", ret);
		return ret;
	}

	gpiod_set_value(pdata->enable_gpio, 1);

	return ret;
}

static int __maybe_unused ti_sn_bridge_suspend(struct device *dev)
{
	struct ti_sn_bridge *pdata = dev_get_drvdata(dev);
	int ret;

	gpiod_set_value(pdata->enable_gpio, 0);

	ret = regulator_bulk_disable(SN_REGULATOR_SUPPLY_NUM, pdata->supplies);
	if (ret)
		DRM_ERROR("failed to disable supplies %d\n", ret);

	return ret;
}

static const struct dev_pm_ops ti_sn_bridge_pm_ops = {
	SET_RUNTIME_PM_OPS(ti_sn_bridge_suspend, ti_sn_bridge_resume, NULL)
};

/* Connector funcs */
static struct ti_sn_bridge *
connector_to_ti_sn_bridge(struct drm_connector *connector)
{
	return container_of(connector, struct ti_sn_bridge, connector);
}

static int ti_sn_bridge_connector_get_modes(struct drm_connector *connector)
{
	struct ti_sn_bridge *pdata = connector_to_ti_sn_bridge(connector);

	return drm_panel_get_modes(pdata->panel);
}

static enum drm_mode_status
ti_sn_bridge_connector_mode_valid(struct drm_connector *connector,
				  struct drm_display_mode *mode)
{
	/* maximum supported resolution is 4K at 60 fps */
	if (mode->clock > 594000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static struct drm_connector_helper_funcs ti_sn_bridge_connector_helper_funcs = {
	.get_modes = ti_sn_bridge_connector_get_modes,
	.mode_valid = ti_sn_bridge_connector_mode_valid,
};

static enum drm_connector_status
ti_sn_bridge_connector_detect(struct drm_connector *connector, bool force)
{
	/**
	 * TODO: Currently if drm_panel is present, then always
	 * return the status as connected. Need to add support to detect
	 * device state for hot pluggable scenarios.
	 */
	return connector_status_connected;
}

static const struct drm_connector_funcs ti_sn_bridge_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = ti_sn_bridge_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct ti_sn_bridge *bridge_to_ti_sn_bridge(struct drm_bridge *bridge)
{
	return container_of(bridge, struct ti_sn_bridge, bridge);
}

static int ti_sn_bridge_parse_regulators(struct ti_sn_bridge *pdata)
{
	unsigned int i;
	const char * const ti_sn_bridge_supply_names[] = {
		"vcca", "vcc", "vccio", "vpll",
	};

	for (i = 0; i < SN_REGULATOR_SUPPLY_NUM; i++)
		pdata->supplies[i].supply = ti_sn_bridge_supply_names[i];

	return devm_regulator_bulk_get(pdata->dev, SN_REGULATOR_SUPPLY_NUM,
				       pdata->supplies);
}

static int ti_sn_bridge_attach(struct drm_bridge *bridge)
{
	int ret, val;
	struct ti_sn_bridge *pdata = bridge_to_ti_sn_bridge(bridge);
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *dsi;
	const struct mipi_dsi_device_info info = { .type = "ti_sn_bridge",
						   .channel = 0,
						   .node = NULL,
						 };

	ret = drm_connector_init(bridge->dev, &pdata->connector,
				 &ti_sn_bridge_connector_funcs,
				 DRM_MODE_CONNECTOR_eDP);
	if (ret) {
		DRM_ERROR("Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(&pdata->connector,
				 &ti_sn_bridge_connector_helper_funcs);
	drm_connector_attach_encoder(&pdata->connector, bridge->encoder);

	/*
	 * TODO: ideally finding host resource and dsi dev registration needs
	 * to be done in bridge probe. But some existing DSI host drivers will
	 * wait for any of the drm_bridge/drm_panel to get added to the global
	 * bridge/panel list, before completing their probe. So if we do the
	 * dsi dev registration part in bridge probe, before populating in
	 * the global bridge list, then it will cause deadlock as dsi host probe
	 * will never complete, neither our bridge probe. So keeping it here
	 * will satisfy most of the existing host drivers. Once the host driver
	 * is fixed we can move the below code to bridge probe safely.
	 */
	host = of_find_mipi_dsi_host_by_node(pdata->host_node);
	if (!host) {
		DRM_ERROR("failed to find dsi host\n");
		ret = -ENODEV;
		goto err_dsi_host;
	}

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		DRM_ERROR("failed to create dsi device\n");
		ret = PTR_ERR(dsi);
		goto err_dsi_host;
	}

	/* TODO: setting to 4 lanes always for now */
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_EOT_PACKET | MIPI_DSI_MODE_VIDEO_HSE;

	/* check if continuous dsi clock is required or not */
	pm_runtime_get_sync(pdata->dev);
	regmap_read(pdata->regmap, SN_DPPLL_SRC_REG, &val);
	pm_runtime_put(pdata->dev);
	if (!(val & DPPLL_CLK_SRC_DSICLK))
		dsi->mode_flags |= MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		DRM_ERROR("failed to attach dsi to host\n");
		goto err_dsi_attach;
	}
	pdata->dsi = dsi;

	/* attach panel to bridge */
	drm_panel_attach(pdata->panel, &pdata->connector);

	return 0;

err_dsi_attach:
	mipi_dsi_device_unregister(dsi);
err_dsi_host:
	drm_connector_cleanup(&pdata->connector);
	return ret;
}

static void ti_sn_bridge_disable(struct drm_bridge *bridge)
{
	struct ti_sn_bridge *pdata = bridge_to_ti_sn_bridge(bridge);

	drm_panel_disable(pdata->panel);

	/* disable video stream */
	regmap_update_bits(pdata->regmap, SN_ENH_FRAME_REG, VSTREAM_ENABLE, 0);
	/* semi auto link training mode OFF */
	regmap_write(pdata->regmap, SN_ML_TX_MODE_REG, 0);
	/* disable DP PLL */
	regmap_write(pdata->regmap, SN_PLL_ENABLE_REG, 0);

	drm_panel_unprepare(pdata->panel);
}

static u32 ti_sn_bridge_get_dsi_freq(struct ti_sn_bridge *pdata)
{
	u32 bit_rate_khz, clk_freq_khz;
	struct drm_display_mode *mode =
		&pdata->bridge.encoder->crtc->state->adjusted_mode;

	bit_rate_khz = mode->clock *
			mipi_dsi_pixel_format_to_bpp(pdata->dsi->format);
	clk_freq_khz = bit_rate_khz / (pdata->dsi->lanes * 2);

	return clk_freq_khz;
}

/* clk frequencies supported by bridge in Hz in case derived from REFCLK pin */
static const u32 ti_sn_bridge_refclk_lut[] = {
	12000000,
	19200000,
	26000000,
	27000000,
	38400000,
};

/* clk frequencies supported by bridge in Hz in case derived from DACP/N pin */
static const u32 ti_sn_bridge_dsiclk_lut[] = {
	468000000,
	384000000,
	416000000,
	486000000,
	460800000,
};

static void ti_sn_bridge_set_refclk_freq(struct ti_sn_bridge *pdata)
{
	int i;
	u32 refclk_rate;
	const u32 *refclk_lut;
	size_t refclk_lut_size;

	if (pdata->refclk) {
		refclk_rate = clk_get_rate(pdata->refclk);
		refclk_lut = ti_sn_bridge_refclk_lut;
		refclk_lut_size = ARRAY_SIZE(ti_sn_bridge_refclk_lut);
		clk_prepare_enable(pdata->refclk);
	} else {
		refclk_rate = ti_sn_bridge_get_dsi_freq(pdata) * 1000;
		refclk_lut = ti_sn_bridge_dsiclk_lut;
		refclk_lut_size = ARRAY_SIZE(ti_sn_bridge_dsiclk_lut);
	}

	/* for i equals to refclk_lut_size means default frequency */
	for (i = 0; i < refclk_lut_size; i++)
		if (refclk_lut[i] == refclk_rate)
			break;

	regmap_update_bits(pdata->regmap, SN_DPPLL_SRC_REG, REFCLK_FREQ_MASK,
			   REFCLK_FREQ(i));
}

/**
 * LUT index corresponds to register value and
 * LUT values corresponds to dp data rate supported
 * by the bridge in Mbps unit.
 */
static const unsigned int ti_sn_bridge_dp_rate_lut[] = {
	0, 1620, 2160, 2430, 2700, 3240, 4320, 5400
};

static void ti_sn_bridge_set_dsi_dp_rate(struct ti_sn_bridge *pdata)
{
	unsigned int bit_rate_mhz, clk_freq_mhz, dp_rate_mhz;
	unsigned int val, i;
	struct drm_display_mode *mode =
		&pdata->bridge.encoder->crtc->state->adjusted_mode;

	/* set DSIA clk frequency */
	bit_rate_mhz = (mode->clock / 1000) *
			mipi_dsi_pixel_format_to_bpp(pdata->dsi->format);
	clk_freq_mhz = bit_rate_mhz / (pdata->dsi->lanes * 2);

	/* for each increment in val, frequency increases by 5MHz */
	val = (MIN_DSI_CLK_FREQ_MHZ / 5) +
		(((clk_freq_mhz - MIN_DSI_CLK_FREQ_MHZ) / 5) & 0xFF);
	regmap_write(pdata->regmap, SN_DSIA_CLK_FREQ_REG, val);

	/* set DP data rate */
	dp_rate_mhz = ((bit_rate_mhz / pdata->dsi->lanes) * DP_CLK_FUDGE_NUM) /
							DP_CLK_FUDGE_DEN;
	for (i = 0; i < ARRAY_SIZE(ti_sn_bridge_dp_rate_lut) - 1; i++)
		if (ti_sn_bridge_dp_rate_lut[i] > dp_rate_mhz)
			break;

	regmap_update_bits(pdata->regmap, SN_DATARATE_CONFIG_REG,
			   DP_DATARATE_MASK, DP_DATARATE(i));
}

static void ti_sn_bridge_set_video_timings(struct ti_sn_bridge *pdata)
{
	struct drm_display_mode *mode =
		&pdata->bridge.encoder->crtc->state->adjusted_mode;
	u8 hsync_polarity = 0, vsync_polarity = 0;

	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		hsync_polarity = CHA_HSYNC_POLARITY;
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		vsync_polarity = CHA_VSYNC_POLARITY;

	ti_sn_bridge_write_u16(pdata, SN_CHA_ACTIVE_LINE_LENGTH_LOW_REG,
			       mode->hdisplay);
	ti_sn_bridge_write_u16(pdata, SN_CHA_VERTICAL_DISPLAY_SIZE_LOW_REG,
			       mode->vdisplay);
	regmap_write(pdata->regmap, SN_CHA_HSYNC_PULSE_WIDTH_LOW_REG,
		     (mode->hsync_end - mode->hsync_start) & 0xFF);
	regmap_write(pdata->regmap, SN_CHA_HSYNC_PULSE_WIDTH_HIGH_REG,
		     (((mode->hsync_end - mode->hsync_start) >> 8) & 0x7F) |
		     hsync_polarity);
	regmap_write(pdata->regmap, SN_CHA_VSYNC_PULSE_WIDTH_LOW_REG,
		     (mode->vsync_end - mode->vsync_start) & 0xFF);
	regmap_write(pdata->regmap, SN_CHA_VSYNC_PULSE_WIDTH_HIGH_REG,
		     (((mode->vsync_end - mode->vsync_start) >> 8) & 0x7F) |
		     vsync_polarity);

	regmap_write(pdata->regmap, SN_CHA_HORIZONTAL_BACK_PORCH_REG,
		     (mode->htotal - mode->hsync_end) & 0xFF);
	regmap_write(pdata->regmap, SN_CHA_VERTICAL_BACK_PORCH_REG,
		     (mode->vtotal - mode->vsync_end) & 0xFF);

	regmap_write(pdata->regmap, SN_CHA_HORIZONTAL_FRONT_PORCH_REG,
		     (mode->hsync_start - mode->hdisplay) & 0xFF);
	regmap_write(pdata->regmap, SN_CHA_VERTICAL_FRONT_PORCH_REG,
		     (mode->vsync_start - mode->vdisplay) & 0xFF);

	usleep_range(10000, 10500); /* 10ms delay recommended by spec */
}

static void ti_sn_bridge_enable(struct drm_bridge *bridge)
{
	struct ti_sn_bridge *pdata = bridge_to_ti_sn_bridge(bridge);
	unsigned int val;
	int ret;

	/* DSI_A lane config */
	val = CHA_DSI_LANES(4 - pdata->dsi->lanes);
	regmap_update_bits(pdata->regmap, SN_DSI_LANES_REG,
			   CHA_DSI_LANES_MASK, val);

	/* DP lane config */
	val = DP_NUM_LANES(pdata->dsi->lanes - 1);
	regmap_update_bits(pdata->regmap, SN_SSC_CONFIG_REG, DP_NUM_LANES_MASK,
			   val);

	/* set dsi/dp clk frequency value */
	ti_sn_bridge_set_dsi_dp_rate(pdata);

	/* enable DP PLL */
	regmap_write(pdata->regmap, SN_PLL_ENABLE_REG, 1);

	ret = regmap_read_poll_timeout(pdata->regmap, SN_DPPLL_SRC_REG, val,
				       val & DPPLL_SRC_DP_PLL_LOCK, 1000,
				       50 * 1000);
	if (ret) {
		DRM_ERROR("DP_PLL_LOCK polling failed (%d)\n", ret);
		return;
	}

	/**
	 * The SN65DSI86 only supports ASSR Display Authentication method and
	 * this method is enabled by default. An eDP panel must support this
	 * authentication method. We need to enable this method in the eDP panel
	 * at DisplayPort address 0x0010A prior to link training.
	 */
	drm_dp_dpcd_writeb(&pdata->aux, DP_EDP_CONFIGURATION_SET,
			   DP_ALTERNATE_SCRAMBLER_RESET_ENABLE);

	/* Semi auto link training mode */
	regmap_write(pdata->regmap, SN_ML_TX_MODE_REG, 0x0A);
	ret = regmap_read_poll_timeout(pdata->regmap, SN_ML_TX_MODE_REG, val,
				       val == ML_TX_MAIN_LINK_OFF ||
				       val == ML_TX_NORMAL_MODE, 1000,
				       500 * 1000);
	if (ret) {
		DRM_ERROR("Training complete polling failed (%d)\n", ret);
		return;
	} else if (val == ML_TX_MAIN_LINK_OFF) {
		DRM_ERROR("Link training failed, link is off\n");
		return;
	}

	/* config video parameters */
	ti_sn_bridge_set_video_timings(pdata);

	/* enable video stream */
	regmap_update_bits(pdata->regmap, SN_ENH_FRAME_REG, VSTREAM_ENABLE,
			   VSTREAM_ENABLE);

	drm_panel_enable(pdata->panel);
}

static void ti_sn_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct ti_sn_bridge *pdata = bridge_to_ti_sn_bridge(bridge);

	pm_runtime_get_sync(pdata->dev);

	/* configure bridge ref_clk */
	ti_sn_bridge_set_refclk_freq(pdata);

	/*
	 * HPD on this bridge chip is a bit useless.  This is an eDP bridge
	 * so the HPD is an internal signal that's only there to signal that
	 * the panel is done powering up.  ...but the bridge chip debounces
	 * this signal by between 100 ms and 400 ms (depending on process,
	 * voltage, and temperate--I measured it at about 200 ms).  One
	 * particular panel asserted HPD 84 ms after it was powered on meaning
	 * that we saw HPD 284 ms after power on.  ...but the same panel said
	 * that instead of looking at HPD you could just hardcode a delay of
	 * 200 ms.  We'll assume that the panel driver will have the hardcoded
	 * delay in its prepare and always disable HPD.
	 *
	 * If HPD somehow makes sense on some future panel we'll have to
	 * change this to be conditional on someone specifying that HPD should
	 * be used.
	 */
	regmap_update_bits(pdata->regmap, SN_HPD_DISABLE_REG, HPD_DISABLE,
			   HPD_DISABLE);

	drm_panel_prepare(pdata->panel);
}

static void ti_sn_bridge_post_disable(struct drm_bridge *bridge)
{
	struct ti_sn_bridge *pdata = bridge_to_ti_sn_bridge(bridge);

	if (pdata->refclk)
		clk_disable_unprepare(pdata->refclk);

	pm_runtime_put_sync(pdata->dev);
}

static const struct drm_bridge_funcs ti_sn_bridge_funcs = {
	.attach = ti_sn_bridge_attach,
	.pre_enable = ti_sn_bridge_pre_enable,
	.enable = ti_sn_bridge_enable,
	.disable = ti_sn_bridge_disable,
	.post_disable = ti_sn_bridge_post_disable,
};

static struct ti_sn_bridge *aux_to_ti_sn_bridge(struct drm_dp_aux *aux)
{
	return container_of(aux, struct ti_sn_bridge, aux);
}

static ssize_t ti_sn_aux_transfer(struct drm_dp_aux *aux,
				  struct drm_dp_aux_msg *msg)
{
	struct ti_sn_bridge *pdata = aux_to_ti_sn_bridge(aux);
	u32 request = msg->request & ~DP_AUX_I2C_MOT;
	u32 request_val = AUX_CMD_REQ(msg->request);
	u8 *buf = (u8 *)msg->buffer;
	unsigned int val;
	int ret, i;

	if (msg->size > SN_AUX_MAX_PAYLOAD_BYTES)
		return -EINVAL;

	switch (request) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		regmap_write(pdata->regmap, SN_AUX_CMD_REG, request_val);
		break;
	default:
		return -EINVAL;
	}

	regmap_write(pdata->regmap, SN_AUX_ADDR_19_16_REG,
		     (msg->address >> 16) & 0xF);
	regmap_write(pdata->regmap, SN_AUX_ADDR_15_8_REG,
		     (msg->address >> 8) & 0xFF);
	regmap_write(pdata->regmap, SN_AUX_ADDR_7_0_REG, msg->address & 0xFF);

	regmap_write(pdata->regmap, SN_AUX_LENGTH_REG, msg->size);

	if (request == DP_AUX_NATIVE_WRITE || request == DP_AUX_I2C_WRITE) {
		for (i = 0; i < msg->size; i++)
			regmap_write(pdata->regmap, SN_AUX_WDATA_REG(i),
				     buf[i]);
	}

	regmap_write(pdata->regmap, SN_AUX_CMD_REG, request_val | AUX_CMD_SEND);

	ret = regmap_read_poll_timeout(pdata->regmap, SN_AUX_CMD_REG, val,
				       !(val & AUX_CMD_SEND), 200,
				       50 * 1000);
	if (ret)
		return ret;

	ret = regmap_read(pdata->regmap, SN_AUX_CMD_STATUS_REG, &val);
	if (ret)
		return ret;
	else if ((val & AUX_IRQ_STATUS_NAT_I2C_FAIL)
		 || (val & AUX_IRQ_STATUS_AUX_RPLY_TOUT)
		 || (val & AUX_IRQ_STATUS_AUX_SHORT))
		return -ENXIO;

	if (request == DP_AUX_NATIVE_WRITE || request == DP_AUX_I2C_WRITE)
		return msg->size;

	for (i = 0; i < msg->size; i++) {
		unsigned int val;
		ret = regmap_read(pdata->regmap, SN_AUX_RDATA_REG(i),
				  &val);
		if (ret)
			return ret;

		WARN_ON(val & ~0xFF);
		buf[i] = (u8)(val & 0xFF);
	}

	return msg->size;
}

static int ti_sn_bridge_parse_dsi_host(struct ti_sn_bridge *pdata)
{
	struct device_node *np = pdata->dev->of_node;

	pdata->host_node = of_graph_get_remote_node(np, 0, 0);

	if (!pdata->host_node) {
		DRM_ERROR("remote dsi host node not found\n");
		return -ENODEV;
	}

	return 0;
}

static int ti_sn_bridge_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct ti_sn_bridge *pdata;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		DRM_ERROR("device doesn't support I2C\n");
		return -ENODEV;
	}

	pdata = devm_kzalloc(&client->dev, sizeof(struct ti_sn_bridge),
			     GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->regmap = devm_regmap_init_i2c(client,
					     &ti_sn_bridge_regmap_config);
	if (IS_ERR(pdata->regmap)) {
		DRM_ERROR("regmap i2c init failed\n");
		return PTR_ERR(pdata->regmap);
	}

	pdata->dev = &client->dev;

	ret = drm_of_find_panel_or_bridge(pdata->dev->of_node, 1, 0,
					  &pdata->panel, NULL);
	if (ret) {
		DRM_ERROR("could not find any panel node\n");
		return ret;
	}

	dev_set_drvdata(&client->dev, pdata);

	pdata->enable_gpio = devm_gpiod_get(pdata->dev, "enable",
					    GPIOD_OUT_LOW);
	if (IS_ERR(pdata->enable_gpio)) {
		DRM_ERROR("failed to get enable gpio from DT\n");
		ret = PTR_ERR(pdata->enable_gpio);
		return ret;
	}

	ret = ti_sn_bridge_parse_regulators(pdata);
	if (ret) {
		DRM_ERROR("failed to parse regulators\n");
		return ret;
	}

	pdata->refclk = devm_clk_get(pdata->dev, "refclk");
	if (IS_ERR(pdata->refclk)) {
		ret = PTR_ERR(pdata->refclk);
		if (ret == -EPROBE_DEFER)
			return ret;
		DRM_DEBUG_KMS("refclk not found\n");
		pdata->refclk = NULL;
	}

	ret = ti_sn_bridge_parse_dsi_host(pdata);
	if (ret)
		return ret;

	pm_runtime_enable(pdata->dev);

	i2c_set_clientdata(client, pdata);

	pdata->aux.name = "ti-sn65dsi86-aux";
	pdata->aux.dev = pdata->dev;
	pdata->aux.transfer = ti_sn_aux_transfer;
	drm_dp_aux_register(&pdata->aux);

	pdata->bridge.funcs = &ti_sn_bridge_funcs;
	pdata->bridge.of_node = client->dev.of_node;

	drm_bridge_add(&pdata->bridge);

	return 0;
}

static int ti_sn_bridge_remove(struct i2c_client *client)
{
	struct ti_sn_bridge *pdata = i2c_get_clientdata(client);

	if (!pdata)
		return -EINVAL;

	of_node_put(pdata->host_node);

	pm_runtime_disable(pdata->dev);

	if (pdata->dsi) {
		mipi_dsi_detach(pdata->dsi);
		mipi_dsi_device_unregister(pdata->dsi);
	}

	drm_bridge_remove(&pdata->bridge);

	return 0;
}

static struct i2c_device_id ti_sn_bridge_id[] = {
	{ "ti,sn65dsi86", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, ti_sn_bridge_id);

static const struct of_device_id ti_sn_bridge_match_table[] = {
	{.compatible = "ti,sn65dsi86"},
	{},
};
MODULE_DEVICE_TABLE(of, ti_sn_bridge_match_table);

static struct i2c_driver ti_sn_bridge_driver = {
	.driver = {
		.name = "ti_sn65dsi86",
		.of_match_table = ti_sn_bridge_match_table,
		.pm = &ti_sn_bridge_pm_ops,
	},
	.probe = ti_sn_bridge_probe,
	.remove = ti_sn_bridge_remove,
	.id_table = ti_sn_bridge_id,
};
module_i2c_driver(ti_sn_bridge_driver);

MODULE_AUTHOR("Sandeep Panda <spanda@codeaurora.org>");
MODULE_DESCRIPTION("sn65dsi86 DSI to eDP bridge driver");
MODULE_LICENSE("GPL v2");
