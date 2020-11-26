// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * datasheet: https://www.ti.com/lit/ds/symlink/sn65dsi86.pdf
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

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
#define SN_LN_ASSIGN_REG			0x59
#define  LN_ASSIGN_WIDTH			2
#define SN_ENH_FRAME_REG			0x5A
#define  VSTREAM_ENABLE				BIT(3)
#define  LN_POLRS_OFFSET			4
#define  LN_POLRS_MASK				0xf0
#define SN_DATA_FORMAT_REG			0x5B
#define  BPP_18_RGB				BIT(0)
#define SN_HPD_DISABLE_REG			0x5C
#define  HPD_DISABLE				BIT(0)
#define SN_GPIO_IO_REG				0x5E
#define  SN_GPIO_INPUT_SHIFT			4
#define  SN_GPIO_OUTPUT_SHIFT			0
#define SN_GPIO_CTRL_REG			0x5F
#define  SN_GPIO_MUX_INPUT			0
#define  SN_GPIO_MUX_OUTPUT			1
#define  SN_GPIO_MUX_SPECIAL			2
#define  SN_GPIO_MUX_MASK			0x3
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

#define SN_MAX_DP_LANES			4
#define SN_NUM_GPIOS			4
#define SN_GPIO_PHYSICAL_OFFSET		1

/**
 * struct ti_sn_bridge - Platform data for ti-sn65dsi86 driver.
 * @dev:          Pointer to our device.
 * @regmap:       Regmap for accessing i2c.
 * @aux:          Our aux channel.
 * @bridge:       Our bridge.
 * @connector:    Our connector.
 * @debugfs:      Used for managing our debugfs.
 * @host_node:    Remote DSI node.
 * @dsi:          Our MIPI DSI source.
 * @refclk:       Our reference clock.
 * @panel:        Our panel.
 * @enable_gpio:  The GPIO we toggle to enable the bridge.
 * @supplies:     Data for bulk enabling/disabling our regulators.
 * @dp_lanes:     Count of dp_lanes we're using.
 * @ln_assign:    Value to program to the LN_ASSIGN register.
 * @ln_polrs:     Value for the 4-bit LN_POLRS field of SN_ENH_FRAME_REG.
 *
 * @gchip:        If we expose our GPIOs, this is used.
 * @gchip_output: A cache of whether we've set GPIOs to output.  This
 *                serves double-duty of keeping track of the direction and
 *                also keeping track of whether we've incremented the
 *                pm_runtime reference count for this pin, which we do
 *                whenever a pin is configured as an output.  This is a
 *                bitmap so we can do atomic ops on it without an extra
 *                lock so concurrent users of our 4 GPIOs don't stomp on
 *                each other's read-modify-write.
 */
struct ti_sn_bridge {
	struct device			*dev;
	struct regmap			*regmap;
	struct drm_dp_aux		aux;
	struct drm_bridge		bridge;
	struct drm_connector		connector;
	struct dentry			*debugfs;
	struct device_node		*host_node;
	struct mipi_dsi_device		*dsi;
	struct clk			*refclk;
	struct drm_panel		*panel;
	struct gpio_desc		*enable_gpio;
	struct regulator_bulk_data	supplies[SN_REGULATOR_SUPPLY_NUM];
	int				dp_lanes;
	u8				ln_assign;
	u8				ln_polrs;

#if defined(CONFIG_OF_GPIO)
	struct gpio_chip		gchip;
	DECLARE_BITMAP(gchip_output, SN_NUM_GPIOS);
#endif
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
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static int status_show(struct seq_file *s, void *data)
{
	struct ti_sn_bridge *pdata = s->private;
	unsigned int reg, val;

	seq_puts(s, "STATUS REGISTERS:\n");

	pm_runtime_get_sync(pdata->dev);

	/* IRQ Status Registers, see Table 31 in datasheet */
	for (reg = 0xf0; reg <= 0xf8; reg++) {
		regmap_read(pdata->regmap, reg, &val);
		seq_printf(s, "[0x%02x] = 0x%08x\n", reg, val);
	}

	pm_runtime_put(pdata->dev);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(status);

static void ti_sn_debugfs_init(struct ti_sn_bridge *pdata)
{
	pdata->debugfs = debugfs_create_dir(dev_name(pdata->dev), NULL);

	debugfs_create_file("status", 0600, pdata->debugfs, pdata,
			&status_fops);
}

static void ti_sn_debugfs_remove(struct ti_sn_bridge *pdata)
{
	debugfs_remove_recursive(pdata->debugfs);
	pdata->debugfs = NULL;
}

/* Connector funcs */
static struct ti_sn_bridge *
connector_to_ti_sn_bridge(struct drm_connector *connector)
{
	return container_of(connector, struct ti_sn_bridge, connector);
}

static int ti_sn_bridge_connector_get_modes(struct drm_connector *connector)
{
	struct ti_sn_bridge *pdata = connector_to_ti_sn_bridge(connector);

	return drm_panel_get_modes(pdata->panel, connector);
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

static int ti_sn_bridge_attach(struct drm_bridge *bridge,
			       enum drm_bridge_attach_flags flags)
{
	int ret, val;
	struct ti_sn_bridge *pdata = bridge_to_ti_sn_bridge(bridge);
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *dsi;
	const struct mipi_dsi_device_info info = { .type = "ti_sn_bridge",
						   .channel = 0,
						   .node = NULL,
						 };

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR) {
		DRM_ERROR("Fix bridge driver to make connector optional!");
		return -EINVAL;
	}

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

	/* TODO: setting to 4 MIPI lanes always for now */
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO;

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

static void ti_sn_bridge_set_dsi_rate(struct ti_sn_bridge *pdata)
{
	unsigned int bit_rate_mhz, clk_freq_mhz;
	unsigned int val;
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
}

static unsigned int ti_sn_bridge_get_bpp(struct ti_sn_bridge *pdata)
{
	if (pdata->connector.display_info.bpc <= 6)
		return 18;
	else
		return 24;
}

/*
 * LUT index corresponds to register value and
 * LUT values corresponds to dp data rate supported
 * by the bridge in Mbps unit.
 */
static const unsigned int ti_sn_bridge_dp_rate_lut[] = {
	0, 1620, 2160, 2430, 2700, 3240, 4320, 5400
};

static int ti_sn_bridge_calc_min_dp_rate_idx(struct ti_sn_bridge *pdata)
{
	unsigned int bit_rate_khz, dp_rate_mhz;
	unsigned int i;
	struct drm_display_mode *mode =
		&pdata->bridge.encoder->crtc->state->adjusted_mode;

	/* Calculate minimum bit rate based on our pixel clock. */
	bit_rate_khz = mode->clock * ti_sn_bridge_get_bpp(pdata);

	/* Calculate minimum DP data rate, taking 80% as per DP spec */
	dp_rate_mhz = DIV_ROUND_UP(bit_rate_khz * DP_CLK_FUDGE_NUM,
				   1000 * pdata->dp_lanes * DP_CLK_FUDGE_DEN);

	for (i = 1; i < ARRAY_SIZE(ti_sn_bridge_dp_rate_lut) - 1; i++)
		if (ti_sn_bridge_dp_rate_lut[i] >= dp_rate_mhz)
			break;

	return i;
}

static void ti_sn_bridge_read_valid_rates(struct ti_sn_bridge *pdata,
					  bool rate_valid[])
{
	unsigned int rate_per_200khz;
	unsigned int rate_mhz;
	u8 dpcd_val;
	int ret;
	int i, j;

	ret = drm_dp_dpcd_readb(&pdata->aux, DP_EDP_DPCD_REV, &dpcd_val);
	if (ret != 1) {
		DRM_DEV_ERROR(pdata->dev,
			      "Can't read eDP rev (%d), assuming 1.1\n", ret);
		dpcd_val = DP_EDP_11;
	}

	if (dpcd_val >= DP_EDP_14) {
		/* eDP 1.4 devices must provide a custom table */
		__le16 sink_rates[DP_MAX_SUPPORTED_RATES];

		ret = drm_dp_dpcd_read(&pdata->aux, DP_SUPPORTED_LINK_RATES,
				       sink_rates, sizeof(sink_rates));

		if (ret != sizeof(sink_rates)) {
			DRM_DEV_ERROR(pdata->dev,
				"Can't read supported rate table (%d)\n", ret);

			/* By zeroing we'll fall back to DP_MAX_LINK_RATE. */
			memset(sink_rates, 0, sizeof(sink_rates));
		}

		for (i = 0; i < ARRAY_SIZE(sink_rates); i++) {
			rate_per_200khz = le16_to_cpu(sink_rates[i]);

			if (!rate_per_200khz)
				break;

			rate_mhz = rate_per_200khz * 200 / 1000;
			for (j = 0;
			     j < ARRAY_SIZE(ti_sn_bridge_dp_rate_lut);
			     j++) {
				if (ti_sn_bridge_dp_rate_lut[j] == rate_mhz)
					rate_valid[j] = true;
			}
		}

		for (i = 0; i < ARRAY_SIZE(ti_sn_bridge_dp_rate_lut); i++) {
			if (rate_valid[i])
				return;
		}
		DRM_DEV_ERROR(pdata->dev,
			      "No matching eDP rates in table; falling back\n");
	}

	/* On older versions best we can do is use DP_MAX_LINK_RATE */
	ret = drm_dp_dpcd_readb(&pdata->aux, DP_MAX_LINK_RATE, &dpcd_val);
	if (ret != 1) {
		DRM_DEV_ERROR(pdata->dev,
			      "Can't read max rate (%d); assuming 5.4 GHz\n",
			      ret);
		dpcd_val = DP_LINK_BW_5_4;
	}

	switch (dpcd_val) {
	default:
		DRM_DEV_ERROR(pdata->dev,
			      "Unexpected max rate (%#x); assuming 5.4 GHz\n",
			      (int)dpcd_val);
		fallthrough;
	case DP_LINK_BW_5_4:
		rate_valid[7] = 1;
		fallthrough;
	case DP_LINK_BW_2_7:
		rate_valid[4] = 1;
		fallthrough;
	case DP_LINK_BW_1_62:
		rate_valid[1] = 1;
		break;
	}
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

static unsigned int ti_sn_get_max_lanes(struct ti_sn_bridge *pdata)
{
	u8 data;
	int ret;

	ret = drm_dp_dpcd_readb(&pdata->aux, DP_MAX_LANE_COUNT, &data);
	if (ret != 1) {
		DRM_DEV_ERROR(pdata->dev,
			      "Can't read lane count (%d); assuming 4\n", ret);
		return 4;
	}

	return data & DP_LANE_COUNT_MASK;
}

static int ti_sn_link_training(struct ti_sn_bridge *pdata, int dp_rate_idx,
			       const char **last_err_str)
{
	unsigned int val;
	int ret;

	/* set dp clk frequency value */
	regmap_update_bits(pdata->regmap, SN_DATARATE_CONFIG_REG,
			   DP_DATARATE_MASK, DP_DATARATE(dp_rate_idx));

	/* enable DP PLL */
	regmap_write(pdata->regmap, SN_PLL_ENABLE_REG, 1);

	ret = regmap_read_poll_timeout(pdata->regmap, SN_DPPLL_SRC_REG, val,
				       val & DPPLL_SRC_DP_PLL_LOCK, 1000,
				       50 * 1000);
	if (ret) {
		*last_err_str = "DP_PLL_LOCK polling failed";
		goto exit;
	}

	/* Semi auto link training mode */
	regmap_write(pdata->regmap, SN_ML_TX_MODE_REG, 0x0A);
	ret = regmap_read_poll_timeout(pdata->regmap, SN_ML_TX_MODE_REG, val,
				       val == ML_TX_MAIN_LINK_OFF ||
				       val == ML_TX_NORMAL_MODE, 1000,
				       500 * 1000);
	if (ret) {
		*last_err_str = "Training complete polling failed";
	} else if (val == ML_TX_MAIN_LINK_OFF) {
		*last_err_str = "Link training failed, link is off";
		ret = -EIO;
	}

exit:
	/* Disable the PLL if we failed */
	if (ret)
		regmap_write(pdata->regmap, SN_PLL_ENABLE_REG, 0);

	return ret;
}

static void ti_sn_bridge_enable(struct drm_bridge *bridge)
{
	struct ti_sn_bridge *pdata = bridge_to_ti_sn_bridge(bridge);
	bool rate_valid[ARRAY_SIZE(ti_sn_bridge_dp_rate_lut)] = { };
	const char *last_err_str = "No supported DP rate";
	int dp_rate_idx;
	unsigned int val;
	int ret = -EINVAL;
	int max_dp_lanes;

	max_dp_lanes = ti_sn_get_max_lanes(pdata);
	pdata->dp_lanes = min(pdata->dp_lanes, max_dp_lanes);

	/* DSI_A lane config */
	val = CHA_DSI_LANES(SN_MAX_DP_LANES - pdata->dsi->lanes);
	regmap_update_bits(pdata->regmap, SN_DSI_LANES_REG,
			   CHA_DSI_LANES_MASK, val);

	regmap_write(pdata->regmap, SN_LN_ASSIGN_REG, pdata->ln_assign);
	regmap_update_bits(pdata->regmap, SN_ENH_FRAME_REG, LN_POLRS_MASK,
			   pdata->ln_polrs << LN_POLRS_OFFSET);

	/* set dsi clk frequency value */
	ti_sn_bridge_set_dsi_rate(pdata);

	/**
	 * The SN65DSI86 only supports ASSR Display Authentication method and
	 * this method is enabled by default. An eDP panel must support this
	 * authentication method. We need to enable this method in the eDP panel
	 * at DisplayPort address 0x0010A prior to link training.
	 */
	drm_dp_dpcd_writeb(&pdata->aux, DP_EDP_CONFIGURATION_SET,
			   DP_ALTERNATE_SCRAMBLER_RESET_ENABLE);

	/* Set the DP output format (18 bpp or 24 bpp) */
	val = (ti_sn_bridge_get_bpp(pdata) == 18) ? BPP_18_RGB : 0;
	regmap_update_bits(pdata->regmap, SN_DATA_FORMAT_REG, BPP_18_RGB, val);

	/* DP lane config */
	val = DP_NUM_LANES(min(pdata->dp_lanes, 3));
	regmap_update_bits(pdata->regmap, SN_SSC_CONFIG_REG, DP_NUM_LANES_MASK,
			   val);

	ti_sn_bridge_read_valid_rates(pdata, rate_valid);

	/* Train until we run out of rates */
	for (dp_rate_idx = ti_sn_bridge_calc_min_dp_rate_idx(pdata);
	     dp_rate_idx < ARRAY_SIZE(ti_sn_bridge_dp_rate_lut);
	     dp_rate_idx++) {
		if (!rate_valid[dp_rate_idx])
			continue;

		ret = ti_sn_link_training(pdata, dp_rate_idx, &last_err_str);
		if (!ret)
			break;
	}
	if (ret) {
		DRM_DEV_ERROR(pdata->dev, "%s (%d)\n", last_err_str, ret);
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

	/* Clear old status bits before start so we don't get confused */
	regmap_write(pdata->regmap, SN_AUX_CMD_STATUS_REG,
		     AUX_IRQ_STATUS_NAT_I2C_FAIL |
		     AUX_IRQ_STATUS_AUX_RPLY_TOUT |
		     AUX_IRQ_STATUS_AUX_SHORT);

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

#if defined(CONFIG_OF_GPIO)

static int tn_sn_bridge_of_xlate(struct gpio_chip *chip,
				 const struct of_phandle_args *gpiospec,
				 u32 *flags)
{
	if (WARN_ON(gpiospec->args_count < chip->of_gpio_n_cells))
		return -EINVAL;

	if (gpiospec->args[0] > chip->ngpio || gpiospec->args[0] < 1)
		return -EINVAL;

	if (flags)
		*flags = gpiospec->args[1];

	return gpiospec->args[0] - SN_GPIO_PHYSICAL_OFFSET;
}

static int ti_sn_bridge_gpio_get_direction(struct gpio_chip *chip,
					   unsigned int offset)
{
	struct ti_sn_bridge *pdata = gpiochip_get_data(chip);

	/*
	 * We already have to keep track of the direction because we use
	 * that to figure out whether we've powered the device.  We can
	 * just return that rather than (maybe) powering up the device
	 * to ask its direction.
	 */
	return test_bit(offset, pdata->gchip_output) ?
		GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static int ti_sn_bridge_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct ti_sn_bridge *pdata = gpiochip_get_data(chip);
	unsigned int val;
	int ret;

	/*
	 * When the pin is an input we don't forcibly keep the bridge
	 * powered--we just power it on to read the pin.  NOTE: part of
	 * the reason this works is that the bridge defaults (when
	 * powered back on) to all 4 GPIOs being configured as GPIO input.
	 * Also note that if something else is keeping the chip powered the
	 * pm_runtime functions are lightweight increments of a refcount.
	 */
	pm_runtime_get_sync(pdata->dev);
	ret = regmap_read(pdata->regmap, SN_GPIO_IO_REG, &val);
	pm_runtime_put(pdata->dev);

	if (ret)
		return ret;

	return !!(val & BIT(SN_GPIO_INPUT_SHIFT + offset));
}

static void ti_sn_bridge_gpio_set(struct gpio_chip *chip, unsigned int offset,
				  int val)
{
	struct ti_sn_bridge *pdata = gpiochip_get_data(chip);
	int ret;

	if (!test_bit(offset, pdata->gchip_output)) {
		dev_err(pdata->dev, "Ignoring GPIO set while input\n");
		return;
	}

	val &= 1;
	ret = regmap_update_bits(pdata->regmap, SN_GPIO_IO_REG,
				 BIT(SN_GPIO_OUTPUT_SHIFT + offset),
				 val << (SN_GPIO_OUTPUT_SHIFT + offset));
	if (ret)
		dev_warn(pdata->dev,
			 "Failed to set bridge GPIO %u: %d\n", offset, ret);
}

static int ti_sn_bridge_gpio_direction_input(struct gpio_chip *chip,
					     unsigned int offset)
{
	struct ti_sn_bridge *pdata = gpiochip_get_data(chip);
	int shift = offset * 2;
	int ret;

	if (!test_and_clear_bit(offset, pdata->gchip_output))
		return 0;

	ret = regmap_update_bits(pdata->regmap, SN_GPIO_CTRL_REG,
				 SN_GPIO_MUX_MASK << shift,
				 SN_GPIO_MUX_INPUT << shift);
	if (ret) {
		set_bit(offset, pdata->gchip_output);
		return ret;
	}

	/*
	 * NOTE: if nobody else is powering the device this may fully power
	 * it off and when it comes back it will have lost all state, but
	 * that's OK because the default is input and we're now an input.
	 */
	pm_runtime_put(pdata->dev);

	return 0;
}

static int ti_sn_bridge_gpio_direction_output(struct gpio_chip *chip,
					      unsigned int offset, int val)
{
	struct ti_sn_bridge *pdata = gpiochip_get_data(chip);
	int shift = offset * 2;
	int ret;

	if (test_and_set_bit(offset, pdata->gchip_output))
		return 0;

	pm_runtime_get_sync(pdata->dev);

	/* Set value first to avoid glitching */
	ti_sn_bridge_gpio_set(chip, offset, val);

	/* Set direction */
	ret = regmap_update_bits(pdata->regmap, SN_GPIO_CTRL_REG,
				 SN_GPIO_MUX_MASK << shift,
				 SN_GPIO_MUX_OUTPUT << shift);
	if (ret) {
		clear_bit(offset, pdata->gchip_output);
		pm_runtime_put(pdata->dev);
	}

	return ret;
}

static void ti_sn_bridge_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	/* We won't keep pm_runtime if we're input, so switch there on free */
	ti_sn_bridge_gpio_direction_input(chip, offset);
}

static const char * const ti_sn_bridge_gpio_names[SN_NUM_GPIOS] = {
	"GPIO1", "GPIO2", "GPIO3", "GPIO4"
};

static int ti_sn_setup_gpio_controller(struct ti_sn_bridge *pdata)
{
	int ret;

	/* Only init if someone is going to use us as a GPIO controller */
	if (!of_property_read_bool(pdata->dev->of_node, "gpio-controller"))
		return 0;

	pdata->gchip.label = dev_name(pdata->dev);
	pdata->gchip.parent = pdata->dev;
	pdata->gchip.owner = THIS_MODULE;
	pdata->gchip.of_xlate = tn_sn_bridge_of_xlate;
	pdata->gchip.of_gpio_n_cells = 2;
	pdata->gchip.free = ti_sn_bridge_gpio_free;
	pdata->gchip.get_direction = ti_sn_bridge_gpio_get_direction;
	pdata->gchip.direction_input = ti_sn_bridge_gpio_direction_input;
	pdata->gchip.direction_output = ti_sn_bridge_gpio_direction_output;
	pdata->gchip.get = ti_sn_bridge_gpio_get;
	pdata->gchip.set = ti_sn_bridge_gpio_set;
	pdata->gchip.can_sleep = true;
	pdata->gchip.names = ti_sn_bridge_gpio_names;
	pdata->gchip.ngpio = SN_NUM_GPIOS;
	pdata->gchip.base = -1;
	ret = devm_gpiochip_add_data(pdata->dev, &pdata->gchip, pdata);
	if (ret)
		dev_err(pdata->dev, "can't add gpio chip\n");

	return ret;
}

#else

static inline int ti_sn_setup_gpio_controller(struct ti_sn_bridge *pdata)
{
	return 0;
}

#endif

static void ti_sn_bridge_parse_lanes(struct ti_sn_bridge *pdata,
				     struct device_node *np)
{
	u32 lane_assignments[SN_MAX_DP_LANES] = { 0, 1, 2, 3 };
	u32 lane_polarities[SN_MAX_DP_LANES] = { };
	struct device_node *endpoint;
	u8 ln_assign = 0;
	u8 ln_polrs = 0;
	int dp_lanes;
	int i;

	/*
	 * Read config from the device tree about lane remapping and lane
	 * polarities.  These are optional and we assume identity map and
	 * normal polarity if nothing is specified.  It's OK to specify just
	 * data-lanes but not lane-polarities but not vice versa.
	 *
	 * Error checking is light (we just make sure we don't crash or
	 * buffer overrun) and we assume dts is well formed and specifying
	 * mappings that the hardware supports.
	 */
	endpoint = of_graph_get_endpoint_by_regs(np, 1, -1);
	dp_lanes = of_property_count_u32_elems(endpoint, "data-lanes");
	if (dp_lanes > 0 && dp_lanes <= SN_MAX_DP_LANES) {
		of_property_read_u32_array(endpoint, "data-lanes",
					   lane_assignments, dp_lanes);
		of_property_read_u32_array(endpoint, "lane-polarities",
					   lane_polarities, dp_lanes);
	} else {
		dp_lanes = SN_MAX_DP_LANES;
	}
	of_node_put(endpoint);

	/*
	 * Convert into register format.  Loop over all lanes even if
	 * data-lanes had fewer elements so that we nicely initialize
	 * the LN_ASSIGN register.
	 */
	for (i = SN_MAX_DP_LANES - 1; i >= 0; i--) {
		ln_assign = ln_assign << LN_ASSIGN_WIDTH | lane_assignments[i];
		ln_polrs = ln_polrs << 1 | lane_polarities[i];
	}

	/* Stash in our struct for when we power on */
	pdata->dp_lanes = dp_lanes;
	pdata->ln_assign = ln_assign;
	pdata->ln_polrs = ln_polrs;
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

	ti_sn_bridge_parse_lanes(pdata, client->dev.of_node);

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

	ret = ti_sn_setup_gpio_controller(pdata);
	if (ret) {
		pm_runtime_disable(pdata->dev);
		return ret;
	}

	i2c_set_clientdata(client, pdata);

	pdata->aux.name = "ti-sn65dsi86-aux";
	pdata->aux.dev = pdata->dev;
	pdata->aux.transfer = ti_sn_aux_transfer;
	drm_dp_aux_register(&pdata->aux);

	pdata->bridge.funcs = &ti_sn_bridge_funcs;
	pdata->bridge.of_node = client->dev.of_node;

	drm_bridge_add(&pdata->bridge);

	ti_sn_debugfs_init(pdata);

	return 0;
}

static int ti_sn_bridge_remove(struct i2c_client *client)
{
	struct ti_sn_bridge *pdata = i2c_get_clientdata(client);

	if (!pdata)
		return -EINVAL;

	ti_sn_debugfs_remove(pdata);

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
