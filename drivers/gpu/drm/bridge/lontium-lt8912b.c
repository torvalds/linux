// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/media-bus-format.h>
#include <linux/regmap.h>

#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>

#include <video/videomode.h>

#define I2C_MAIN 0
#define I2C_ADDR_MAIN 0x48

#define I2C_CEC_DSI 1
#define I2C_ADDR_CEC_DSI 0x49

#define I2C_MAX_IDX 2

struct lt8912 {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_connector connector;

	struct i2c_client *i2c_client[I2C_MAX_IDX];
	struct regmap *regmap[I2C_MAX_IDX];

	struct device_node *host_node;
	struct drm_bridge *hdmi_port;

	struct mipi_dsi_device *dsi;

	struct gpio_desc *gp_reset;

	struct videomode mode;

	struct regulator_bulk_data supplies[7];

	u8 data_lanes;
	bool is_power_on;
};

static int lt8912_write_init_config(struct lt8912 *lt)
{
	const struct reg_sequence seq[] = {
		/* Digital clock en*/
		{0x08, 0xff},
		{0x09, 0xff},
		{0x0a, 0xff},
		{0x0b, 0x7c},
		{0x0c, 0xff},
		{0x42, 0x04},

		/*Tx Analog*/
		{0x31, 0xb1},
		{0x32, 0xb1},
		{0x33, 0x0e},
		{0x37, 0x00},
		{0x38, 0x22},
		{0x60, 0x82},

		/*Cbus Analog*/
		{0x39, 0x45},
		{0x3a, 0x00},
		{0x3b, 0x00},

		/*HDMI Pll Analog*/
		{0x44, 0x31},
		{0x55, 0x44},
		{0x57, 0x01},
		{0x5a, 0x02},

		/*MIPI Analog*/
		{0x3e, 0xd6},
		{0x3f, 0xd4},
		{0x41, 0x3c},
		{0xB2, 0x00},
	};

	return regmap_multi_reg_write(lt->regmap[I2C_MAIN], seq, ARRAY_SIZE(seq));
}

static int lt8912_write_mipi_basic_config(struct lt8912 *lt)
{
	const struct reg_sequence seq[] = {
		{0x12, 0x04},
		{0x14, 0x00},
		{0x15, 0x00},
		{0x1a, 0x03},
		{0x1b, 0x03},
	};

	return regmap_multi_reg_write(lt->regmap[I2C_CEC_DSI], seq, ARRAY_SIZE(seq));
};

static int lt8912_write_dds_config(struct lt8912 *lt)
{
	const struct reg_sequence seq[] = {
		{0x4e, 0xff},
		{0x4f, 0x56},
		{0x50, 0x69},
		{0x51, 0x80},
		{0x1f, 0x5e},
		{0x20, 0x01},
		{0x21, 0x2c},
		{0x22, 0x01},
		{0x23, 0xfa},
		{0x24, 0x00},
		{0x25, 0xc8},
		{0x26, 0x00},
		{0x27, 0x5e},
		{0x28, 0x01},
		{0x29, 0x2c},
		{0x2a, 0x01},
		{0x2b, 0xfa},
		{0x2c, 0x00},
		{0x2d, 0xc8},
		{0x2e, 0x00},
		{0x42, 0x64},
		{0x43, 0x00},
		{0x44, 0x04},
		{0x45, 0x00},
		{0x46, 0x59},
		{0x47, 0x00},
		{0x48, 0xf2},
		{0x49, 0x06},
		{0x4a, 0x00},
		{0x4b, 0x72},
		{0x4c, 0x45},
		{0x4d, 0x00},
		{0x52, 0x08},
		{0x53, 0x00},
		{0x54, 0xb2},
		{0x55, 0x00},
		{0x56, 0xe4},
		{0x57, 0x0d},
		{0x58, 0x00},
		{0x59, 0xe4},
		{0x5a, 0x8a},
		{0x5b, 0x00},
		{0x5c, 0x34},
		{0x1e, 0x4f},
		{0x51, 0x00},
	};

	return regmap_multi_reg_write(lt->regmap[I2C_CEC_DSI], seq, ARRAY_SIZE(seq));
}

static int lt8912_write_rxlogicres_config(struct lt8912 *lt)
{
	int ret;

	ret = regmap_write(lt->regmap[I2C_MAIN], 0x03, 0x7f);
	usleep_range(10000, 20000);
	ret |= regmap_write(lt->regmap[I2C_MAIN], 0x03, 0xff);

	return ret;
};

/* enable LVDS output with some hardcoded configuration, not required for the HDMI output */
static int lt8912_write_lvds_config(struct lt8912 *lt)
{
	const struct reg_sequence seq[] = {
		// lvds power up
		{0x44, 0x30},
		{0x51, 0x05},

		// core pll bypass
		{0x50, 0x24}, // cp=50uA
		{0x51, 0x2d}, // Pix_clk as reference, second order passive LPF PLL
		{0x52, 0x04}, // loopdiv=0, use second-order PLL
		{0x69, 0x0e}, // CP_PRESET_DIV_RATIO
		{0x69, 0x8e},
		{0x6a, 0x00},
		{0x6c, 0xb8}, // RGD_CP_SOFT_K_EN,RGD_CP_SOFT_K[13:8]
		{0x6b, 0x51},

		{0x04, 0xfb}, // core pll reset
		{0x04, 0xff},

		// scaler bypass
		{0x7f, 0x00}, // disable scaler
		{0xa8, 0x13}, // 0x13: JEIDA, 0x33: VESA

		{0x02, 0xf7}, // lvds pll reset
		{0x02, 0xff},
		{0x03, 0xcf},
		{0x03, 0xff},
	};

	return regmap_multi_reg_write(lt->regmap[I2C_MAIN], seq, ARRAY_SIZE(seq));
};

static inline struct lt8912 *bridge_to_lt8912(struct drm_bridge *b)
{
	return container_of(b, struct lt8912, bridge);
}

static inline struct lt8912 *connector_to_lt8912(struct drm_connector *c)
{
	return container_of(c, struct lt8912, connector);
}

static const struct regmap_config lt8912_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
};

static int lt8912_init_i2c(struct lt8912 *lt, struct i2c_client *client)
{
	unsigned int i;
	/*
	 * At this time we only initialize 2 chips, but the lt8912 provides
	 * a third interface for the audio over HDMI configuration.
	 */
	struct i2c_board_info info[] = {
		{ I2C_BOARD_INFO("lt8912p0", I2C_ADDR_MAIN), },
		{ I2C_BOARD_INFO("lt8912p1", I2C_ADDR_CEC_DSI), },
	};

	if (!lt)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(info); i++) {
		if (i > 0) {
			lt->i2c_client[i] = i2c_new_dummy_device(client->adapter,
								 info[i].addr);
			if (IS_ERR(lt->i2c_client[i]))
				return PTR_ERR(lt->i2c_client[i]);
		}

		lt->regmap[i] = devm_regmap_init_i2c(lt->i2c_client[i],
						     &lt8912_regmap_config);
		if (IS_ERR(lt->regmap[i]))
			return PTR_ERR(lt->regmap[i]);
	}
	return 0;
}

static int lt8912_free_i2c(struct lt8912 *lt)
{
	unsigned int i;

	for (i = 1; i < I2C_MAX_IDX; i++)
		i2c_unregister_device(lt->i2c_client[i]);

	return 0;
}

static int lt8912_hard_power_on(struct lt8912 *lt)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(lt->supplies), lt->supplies);
	if (ret)
		return ret;

	gpiod_set_value_cansleep(lt->gp_reset, 0);
	msleep(20);

	return 0;
}

static void lt8912_hard_power_off(struct lt8912 *lt)
{
	gpiod_set_value_cansleep(lt->gp_reset, 1);
	msleep(20);

	regulator_bulk_disable(ARRAY_SIZE(lt->supplies), lt->supplies);

	lt->is_power_on = false;
}

static int lt8912_video_setup(struct lt8912 *lt)
{
	u32 hactive, h_total, hpw, hfp, hbp;
	u32 vactive, v_total, vpw, vfp, vbp;
	u8 settle = 0x08;
	int ret, hsync_activehigh, vsync_activehigh;

	if (!lt)
		return -EINVAL;

	hactive = lt->mode.hactive;
	hfp = lt->mode.hfront_porch;
	hpw = lt->mode.hsync_len;
	hbp = lt->mode.hback_porch;
	h_total = hactive + hfp + hpw + hbp;
	hsync_activehigh = lt->mode.flags & DISPLAY_FLAGS_HSYNC_HIGH;

	vactive = lt->mode.vactive;
	vfp = lt->mode.vfront_porch;
	vpw = lt->mode.vsync_len;
	vbp = lt->mode.vback_porch;
	v_total = vactive + vfp + vpw + vbp;
	vsync_activehigh = lt->mode.flags & DISPLAY_FLAGS_VSYNC_HIGH;

	if (vactive <= 600)
		settle = 0x04;
	else if (vactive == 1080)
		settle = 0x0a;

	ret = regmap_write(lt->regmap[I2C_CEC_DSI], 0x10, 0x01);
	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x11, settle);
	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x18, hpw);
	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x19, vpw);
	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x1c, hactive & 0xff);
	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x1d, hactive >> 8);

	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x2f, 0x0c);

	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x34, h_total & 0xff);
	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x35, h_total >> 8);

	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x36, v_total & 0xff);
	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x37, v_total >> 8);

	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x38, vbp & 0xff);
	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x39, vbp >> 8);

	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x3a, vfp & 0xff);
	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x3b, vfp >> 8);

	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x3c, hbp & 0xff);
	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x3d, hbp >> 8);

	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x3e, hfp & 0xff);
	ret |= regmap_write(lt->regmap[I2C_CEC_DSI], 0x3f, hfp >> 8);

	ret |= regmap_update_bits(lt->regmap[I2C_MAIN], 0xab, BIT(0),
				  vsync_activehigh ? BIT(0) : 0);
	ret |= regmap_update_bits(lt->regmap[I2C_MAIN], 0xab, BIT(1),
				  hsync_activehigh ? BIT(1) : 0);
	ret |= regmap_update_bits(lt->regmap[I2C_MAIN], 0xb2, BIT(0),
				  lt->connector.display_info.is_hdmi ? BIT(0) : 0);

	return ret;
}

static int lt8912_soft_power_on(struct lt8912 *lt)
{
	if (!lt->is_power_on) {
		u32 lanes = lt->data_lanes;

		lt8912_write_init_config(lt);
		regmap_write(lt->regmap[I2C_CEC_DSI], 0x13, lanes & 3);

		lt8912_write_mipi_basic_config(lt);

		lt->is_power_on = true;
	}

	return 0;
}

static int lt8912_video_on(struct lt8912 *lt)
{
	int ret;

	ret = lt8912_video_setup(lt);
	if (ret < 0)
		goto end;

	ret = lt8912_write_dds_config(lt);
	if (ret < 0)
		goto end;

	ret = lt8912_write_rxlogicres_config(lt);
	if (ret < 0)
		goto end;

	ret = lt8912_write_lvds_config(lt);
	if (ret < 0)
		goto end;

end:
	return ret;
}

static enum drm_connector_status lt8912_check_cable_status(struct lt8912 *lt)
{
	int ret;
	unsigned int reg_val;

	ret = regmap_read(lt->regmap[I2C_MAIN], 0xC1, &reg_val);
	if (ret)
		return connector_status_unknown;

	if (reg_val & BIT(7))
		return connector_status_connected;

	return connector_status_disconnected;
}

static enum drm_connector_status
lt8912_connector_detect(struct drm_connector *connector, bool force)
{
	struct lt8912 *lt = connector_to_lt8912(connector);

	if (lt->hdmi_port->ops & DRM_BRIDGE_OP_DETECT)
		return drm_bridge_detect(lt->hdmi_port, connector);

	return lt8912_check_cable_status(lt);
}

static const struct drm_connector_funcs lt8912_connector_funcs = {
	.detect = lt8912_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int lt8912_connector_get_modes(struct drm_connector *connector)
{
	const struct drm_edid *drm_edid;
	struct lt8912 *lt = connector_to_lt8912(connector);
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	int ret, num;

	drm_edid = drm_bridge_edid_read(lt->hdmi_port, connector);
	drm_edid_connector_update(connector, drm_edid);
	if (!drm_edid)
		return 0;

	num = drm_edid_connector_add_modes(connector);

	ret = drm_display_info_set_bus_formats(&connector->display_info,
					       &bus_format, 1);
	if (ret < 0)
		num = 0;

	drm_edid_free(drm_edid);
	return num;
}

static const struct drm_connector_helper_funcs lt8912_connector_helper_funcs = {
	.get_modes = lt8912_connector_get_modes,
};

static void lt8912_bridge_mode_set(struct drm_bridge *bridge,
				   const struct drm_display_mode *mode,
				   const struct drm_display_mode *adj)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);

	drm_display_mode_to_videomode(adj, &lt->mode);
}

static void lt8912_bridge_enable(struct drm_bridge *bridge)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);

	lt8912_video_on(lt);
}

static int lt8912_attach_dsi(struct lt8912 *lt)
{
	struct device *dev = lt->dev;
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *dsi;
	int ret = -1;
	const struct mipi_dsi_device_info info = { .type = "lt8912",
						   .channel = 0,
						   .node = NULL,
						 };

	host = of_find_mipi_dsi_host_by_node(lt->host_node);
	if (!host)
		return dev_err_probe(dev, -EPROBE_DEFER, "failed to find dsi host\n");

	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi)) {
		ret = PTR_ERR(dsi);
		dev_err(dev, "failed to create dsi device (%d)\n", ret);
		return ret;
	}

	lt->dsi = dsi;

	dsi->lanes = lt->data_lanes;
	dsi->format = MIPI_DSI_FMT_RGB888;

	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
			  MIPI_DSI_MODE_LPM |
			  MIPI_DSI_MODE_NO_EOT_PACKET;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach dsi to host\n");
		return ret;
	}

	return 0;
}

static void lt8912_bridge_hpd_cb(void *data, enum drm_connector_status status)
{
	struct lt8912 *lt = data;

	if (lt->bridge.dev)
		drm_helper_hpd_irq_event(lt->bridge.dev);
}

static int lt8912_bridge_connector_init(struct drm_bridge *bridge)
{
	int ret;
	struct lt8912 *lt = bridge_to_lt8912(bridge);
	struct drm_connector *connector = &lt->connector;

	if (lt->hdmi_port->ops & DRM_BRIDGE_OP_HPD) {
		drm_bridge_hpd_enable(lt->hdmi_port, lt8912_bridge_hpd_cb, lt);
		connector->polled = DRM_CONNECTOR_POLL_HPD;
	} else {
		connector->polled = DRM_CONNECTOR_POLL_CONNECT |
				    DRM_CONNECTOR_POLL_DISCONNECT;
	}

	ret = drm_connector_init(bridge->dev, connector,
				 &lt8912_connector_funcs,
				 lt->hdmi_port->type);
	if (ret)
		goto exit;

	drm_connector_helper_add(connector, &lt8912_connector_helper_funcs);

	connector->dpms = DRM_MODE_DPMS_OFF;
	drm_connector_attach_encoder(connector, bridge->encoder);

exit:
	return ret;
}

static int lt8912_bridge_attach(struct drm_bridge *bridge,
				struct drm_encoder *encoder,
				enum drm_bridge_attach_flags flags)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);
	int ret;

	ret = drm_bridge_attach(encoder, lt->hdmi_port, bridge,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret < 0) {
		dev_err(lt->dev, "Failed to attach next bridge (%d)\n", ret);
		return ret;
	}

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)) {
		ret = lt8912_bridge_connector_init(bridge);
		if (ret) {
			dev_err(lt->dev, "Failed to init bridge ! (%d)\n", ret);
			return ret;
		}
	}

	ret = lt8912_hard_power_on(lt);
	if (ret)
		return ret;

	ret = lt8912_soft_power_on(lt);
	if (ret)
		goto error;

	return 0;

error:
	lt8912_hard_power_off(lt);
	return ret;
}

static void lt8912_bridge_detach(struct drm_bridge *bridge)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);

	lt8912_hard_power_off(lt);

	if (lt->connector.dev && lt->hdmi_port->ops & DRM_BRIDGE_OP_HPD)
		drm_bridge_hpd_disable(lt->hdmi_port);
}

static enum drm_mode_status
lt8912_bridge_mode_valid(struct drm_bridge *bridge,
			 const struct drm_display_info *info,
			 const struct drm_display_mode *mode)
{
	if (mode->clock > 150000)
		return MODE_CLOCK_HIGH;

	if (mode->hdisplay > 1920)
		return MODE_BAD_HVALUE;

	if (mode->vdisplay > 1080)
		return MODE_BAD_VVALUE;

	return MODE_OK;
}

static enum drm_connector_status
lt8912_bridge_detect(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);

	if (lt->hdmi_port->ops & DRM_BRIDGE_OP_DETECT)
		return drm_bridge_detect(lt->hdmi_port, connector);

	return lt8912_check_cable_status(lt);
}

static const struct drm_edid *lt8912_bridge_edid_read(struct drm_bridge *bridge,
						      struct drm_connector *connector)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);

	/*
	 * edid must be read through the ddc bus but it must be
	 * given to the hdmi connector node.
	 */
	if (lt->hdmi_port->ops & DRM_BRIDGE_OP_EDID)
		return drm_bridge_edid_read(lt->hdmi_port, connector);

	dev_warn(lt->dev, "The connected bridge does not supports DRM_BRIDGE_OP_EDID\n");
	return NULL;
}

static const struct drm_bridge_funcs lt8912_bridge_funcs = {
	.attach = lt8912_bridge_attach,
	.detach = lt8912_bridge_detach,
	.mode_valid = lt8912_bridge_mode_valid,
	.mode_set = lt8912_bridge_mode_set,
	.enable = lt8912_bridge_enable,
	.detect = lt8912_bridge_detect,
	.edid_read = lt8912_bridge_edid_read,
};

static int lt8912_bridge_resume(struct device *dev)
{
	struct lt8912 *lt = dev_get_drvdata(dev);
	int ret;

	ret = lt8912_hard_power_on(lt);
	if (ret)
		return ret;

	ret = lt8912_soft_power_on(lt);
	if (ret)
		return ret;

	return lt8912_video_on(lt);
}

static int lt8912_bridge_suspend(struct device *dev)
{
	struct lt8912 *lt = dev_get_drvdata(dev);

	lt8912_hard_power_off(lt);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(lt8912_bridge_pm_ops, lt8912_bridge_suspend, lt8912_bridge_resume);

static int lt8912_get_regulators(struct lt8912 *lt)
{
	unsigned int i;
	const char * const supply_names[] = {
		"vdd", "vccmipirx", "vccsysclk", "vcclvdstx",
		"vcchdmitx", "vcclvdspll", "vcchdmipll"
	};

	for (i = 0; i < ARRAY_SIZE(lt->supplies); i++)
		lt->supplies[i].supply = supply_names[i];

	return devm_regulator_bulk_get(lt->dev, ARRAY_SIZE(lt->supplies),
				       lt->supplies);
}

static int lt8912_parse_dt(struct lt8912 *lt)
{
	struct gpio_desc *gp_reset;
	struct device *dev = lt->dev;
	int ret;
	int data_lanes;
	struct device_node *port_node;

	gp_reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gp_reset)) {
		ret = PTR_ERR(gp_reset);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get reset gpio: %d\n", ret);
		return ret;
	}
	lt->gp_reset = gp_reset;

	data_lanes = drm_of_get_data_lanes_count_ep(dev->of_node, 0, -1, 1, 4);
	if (data_lanes < 0) {
		dev_err(lt->dev, "%s: Bad data-lanes property\n", __func__);
		return data_lanes;
	}

	lt->data_lanes = data_lanes;

	lt->host_node = of_graph_get_remote_node(dev->of_node, 0, -1);
	if (!lt->host_node) {
		dev_err(lt->dev, "%s: Failed to get remote port\n", __func__);
		return -ENODEV;
	}

	port_node = of_graph_get_remote_node(dev->of_node, 1, -1);
	if (!port_node) {
		dev_err(lt->dev, "%s: Failed to get connector port\n", __func__);
		ret = -ENODEV;
		goto err_free_host_node;
	}

	lt->hdmi_port = of_drm_find_bridge(port_node);
	if (!lt->hdmi_port) {
		ret = -EPROBE_DEFER;
		dev_err_probe(lt->dev, ret, "%s: Failed to get hdmi port\n", __func__);
		goto err_free_host_node;
	}

	if (!of_device_is_compatible(port_node, "hdmi-connector")) {
		dev_err(lt->dev, "%s: Failed to get hdmi port\n", __func__);
		ret = -EINVAL;
		goto err_free_host_node;
	}

	ret = lt8912_get_regulators(lt);
	if (ret)
		goto err_free_host_node;

	of_node_put(port_node);
	return 0;

err_free_host_node:
	of_node_put(port_node);
	of_node_put(lt->host_node);
	return ret;
}

static int lt8912_put_dt(struct lt8912 *lt)
{
	of_node_put(lt->host_node);
	return 0;
}

static int lt8912_probe(struct i2c_client *client)
{
	static struct lt8912 *lt;
	int ret = 0;
	struct device *dev = &client->dev;

	lt = devm_drm_bridge_alloc(dev, struct lt8912, bridge,
				   &lt8912_bridge_funcs);
	if (IS_ERR(lt))
		return PTR_ERR(lt);

	lt->dev = dev;
	lt->i2c_client[0] = client;

	ret = lt8912_parse_dt(lt);
	if (ret)
		goto err_dt_parse;

	ret = lt8912_init_i2c(lt, client);
	if (ret)
		goto err_i2c;

	i2c_set_clientdata(client, lt);

	lt->bridge.of_node = dev->of_node;
	lt->bridge.ops = (DRM_BRIDGE_OP_EDID |
			  DRM_BRIDGE_OP_DETECT);

	drm_bridge_add(&lt->bridge);

	ret = lt8912_attach_dsi(lt);
	if (ret)
		goto err_attach;

	return 0;

err_attach:
	drm_bridge_remove(&lt->bridge);
	lt8912_free_i2c(lt);
err_i2c:
	lt8912_put_dt(lt);
err_dt_parse:
	return ret;
}

static void lt8912_remove(struct i2c_client *client)
{
	struct lt8912 *lt = i2c_get_clientdata(client);

	drm_bridge_remove(&lt->bridge);
	lt8912_free_i2c(lt);
	lt8912_put_dt(lt);
}

static const struct of_device_id lt8912_dt_match[] = {
	{.compatible = "lontium,lt8912b"},
	{}
};
MODULE_DEVICE_TABLE(of, lt8912_dt_match);

static const struct i2c_device_id lt8912_id[] = {
	{ "lt8912" },
	{}
};
MODULE_DEVICE_TABLE(i2c, lt8912_id);

static struct i2c_driver lt8912_i2c_driver = {
	.driver = {
		.name = "lt8912",
		.of_match_table = lt8912_dt_match,
		.pm = pm_sleep_ptr(&lt8912_bridge_pm_ops),
	},
	.probe = lt8912_probe,
	.remove = lt8912_remove,
	.id_table = lt8912_id,
};
module_i2c_driver(lt8912_i2c_driver);

MODULE_AUTHOR("Adrien Grassein <adrien.grassein@gmail.com>");
MODULE_DESCRIPTION("lt8912 drm driver");
MODULE_LICENSE("GPL v2");
