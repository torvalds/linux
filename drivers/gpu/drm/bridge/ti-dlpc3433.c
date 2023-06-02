// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 RenewOutReach
 * Copyright (C) 2021 Amarula Solutions(India)
 *
 * Author:
 * Jagan Teki <jagan@amarulasolutions.com>
 * Christopher Vollo <chris@renewoutreach.org>
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_mipi_dsi.h>

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

enum cmd_registers {
	WR_INPUT_SOURCE		= 0x05,	/* Write Input Source Select */
	WR_EXT_SOURCE_FMT	= 0x07, /* Write External Video Source Format */
	WR_IMAGE_CROP		= 0x10,	/* Write Image Crop */
	WR_DISPLAY_SIZE		= 0x12,	/* Write Display Size */
	WR_IMAGE_FREEZE		= 0x1A,	/* Write Image Freeze */
	WR_INPUT_IMAGE_SIZE	= 0x2E,	/* Write External Input Image Size */
	WR_RGB_LED_EN		= 0x52,	/* Write RGB LED Enable */
	WR_RGB_LED_CURRENT	= 0x54,	/* Write RGB LED Current */
	WR_RGB_LED_MAX_CURRENT	= 0x5C,	/* Write RGB LED Max Current */
	WR_DSI_HS_CLK		= 0xBD,	/* Write DSI HS Clock */
	RD_DEVICE_ID		= 0xD4,	/* Read Controller Device ID */
	WR_DSI_PORT_EN		= 0xD7,	/* Write DSI Port Enable */
};

enum input_source {
	INPUT_EXTERNAL_VIDEO	= 0,
	INPUT_TEST_PATTERN,
	INPUT_SPLASH_SCREEN,
};

#define DEV_ID_MASK		GENMASK(3, 0)
#define IMAGE_FREESE_EN		BIT(0)
#define DSI_PORT_EN		0
#define EXT_SOURCE_FMT_DSI	0
#define RED_LED_EN		BIT(0)
#define GREEN_LED_EN		BIT(1)
#define BLUE_LED_EN		BIT(2)
#define LED_MASK		GENMASK(2, 0)
#define MAX_BYTE_SIZE		8

struct dlpc {
	struct device		*dev;
	struct drm_bridge	bridge;
	struct drm_bridge	*next_bridge;
	struct device_node	*host_node;
	struct mipi_dsi_device	*dsi;
	struct drm_display_mode	mode;

	struct gpio_desc	*enable_gpio;
	struct regulator	*vcc_intf;
	struct regulator	*vcc_flsh;
	struct regmap		*regmap;
	unsigned int		dsi_lanes;
};

static inline struct dlpc *bridge_to_dlpc(struct drm_bridge *bridge)
{
	return container_of(bridge, struct dlpc, bridge);
}

static bool dlpc_writeable_noinc_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WR_IMAGE_CROP:
	case WR_DISPLAY_SIZE:
	case WR_INPUT_IMAGE_SIZE:
	case WR_DSI_HS_CLK:
		return true;
	default:
		return false;
	}
}

static const struct regmap_range dlpc_volatile_ranges[] = {
	{ .range_min = 0x10, .range_max = 0xBF },
};

static const struct regmap_access_table dlpc_volatile_table = {
	.yes_ranges = dlpc_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(dlpc_volatile_ranges),
};

static struct regmap_config dlpc_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= WR_DSI_PORT_EN,
	.writeable_noinc_reg	= dlpc_writeable_noinc_reg,
	.volatile_table		= &dlpc_volatile_table,
	.cache_type		= REGCACHE_RBTREE,
	.name			= "dlpc3433",
};

static void dlpc_atomic_enable(struct drm_bridge *bridge,
			       struct drm_bridge_state *old_bridge_state)
{
	struct dlpc *dlpc = bridge_to_dlpc(bridge);
	struct device *dev = dlpc->dev;
	struct drm_display_mode *mode = &dlpc->mode;
	struct regmap *regmap = dlpc->regmap;
	char buf[MAX_BYTE_SIZE];
	unsigned int devid;

	regmap_read(regmap, RD_DEVICE_ID, &devid);
	devid &= DEV_ID_MASK;

	DRM_DEV_DEBUG(dev, "DLPC3433 device id: 0x%02x\n", devid);

	if (devid != 0x01) {
		DRM_DEV_ERROR(dev, "Unsupported DLPC device id: 0x%02x\n", devid);
		return;
	}

	/* disable image freeze */
	regmap_write(regmap, WR_IMAGE_FREEZE, IMAGE_FREESE_EN);

	/* enable DSI port */
	regmap_write(regmap, WR_DSI_PORT_EN, DSI_PORT_EN);

	memset(buf, 0, MAX_BYTE_SIZE);

	/* set image crop */
	buf[4] = mode->hdisplay & 0xff;
	buf[5] = (mode->hdisplay & 0xff00) >> 8;
	buf[6] = mode->vdisplay & 0xff;
	buf[7] = (mode->vdisplay & 0xff00) >> 8;
	regmap_noinc_write(regmap, WR_IMAGE_CROP, buf, MAX_BYTE_SIZE);

	/* set display size */
	buf[4] = mode->hdisplay & 0xff;
	buf[5] = (mode->hdisplay & 0xff00) >> 8;
	buf[6] = mode->vdisplay & 0xff;
	buf[7] = (mode->vdisplay & 0xff00) >> 8;
	regmap_noinc_write(regmap, WR_DISPLAY_SIZE, buf, MAX_BYTE_SIZE);

	/* set input image size */
	buf[0] = mode->hdisplay & 0xff;
	buf[1] = (mode->hdisplay & 0xff00) >> 8;
	buf[2] = mode->vdisplay & 0xff;
	buf[3] = (mode->vdisplay & 0xff00) >> 8;
	regmap_noinc_write(regmap, WR_INPUT_IMAGE_SIZE, buf, 4);

	/* set external video port */
	regmap_write(regmap, WR_INPUT_SOURCE, INPUT_EXTERNAL_VIDEO);

	/* set external video format select as DSI */
	regmap_write(regmap, WR_EXT_SOURCE_FMT, EXT_SOURCE_FMT_DSI);

	/* disable image freeze */
	regmap_write(regmap, WR_IMAGE_FREEZE, 0x00);

	/* enable RGB led */
	regmap_update_bits(regmap, WR_RGB_LED_EN, LED_MASK,
			   RED_LED_EN | GREEN_LED_EN | BLUE_LED_EN);

	msleep(10);
}

static void dlpc_atomic_pre_enable(struct drm_bridge *bridge,
				   struct drm_bridge_state *old_bridge_state)
{
	struct dlpc *dlpc = bridge_to_dlpc(bridge);
	int ret;

	gpiod_set_value(dlpc->enable_gpio, 1);

	msleep(500);

	ret = regulator_enable(dlpc->vcc_intf);
	if (ret)
		DRM_DEV_ERROR(dlpc->dev,
			      "failed to enable VCC_INTF regulator: %d\n", ret);

	ret = regulator_enable(dlpc->vcc_flsh);
	if (ret)
		DRM_DEV_ERROR(dlpc->dev,
			      "failed to enable VCC_FLSH regulator: %d\n", ret);

	msleep(10);
}

static void dlpc_atomic_post_disable(struct drm_bridge *bridge,
				     struct drm_bridge_state *old_bridge_state)
{
	struct dlpc *dlpc = bridge_to_dlpc(bridge);

	regulator_disable(dlpc->vcc_flsh);
	regulator_disable(dlpc->vcc_intf);

	msleep(10);

	gpiod_set_value(dlpc->enable_gpio, 0);

	msleep(500);
}

#define MAX_INPUT_SEL_FORMATS	1

static u32 *
dlpc_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
			       struct drm_bridge_state *bridge_state,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state,
			       u32 output_fmt,
			       unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kcalloc(MAX_INPUT_SEL_FORMATS, sizeof(*input_fmts),
			     GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	/* This is the DSI-end bus format */
	input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;
	*num_input_fmts = 1;

	return input_fmts;
}

static void dlpc_mode_set(struct drm_bridge *bridge,
			  const struct drm_display_mode *mode,
			  const struct drm_display_mode *adjusted_mode)
{
	struct dlpc *dlpc = bridge_to_dlpc(bridge);

	drm_mode_copy(&dlpc->mode, adjusted_mode);
}

static int dlpc_attach(struct drm_bridge *bridge,
		       enum drm_bridge_attach_flags flags)
{
	struct dlpc *dlpc = bridge_to_dlpc(bridge);

	return drm_bridge_attach(bridge->encoder, dlpc->next_bridge, bridge, flags);
}

static const struct drm_bridge_funcs dlpc_bridge_funcs = {
	.atomic_duplicate_state		= drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state		= drm_atomic_helper_bridge_destroy_state,
	.atomic_get_input_bus_fmts	= dlpc_atomic_get_input_bus_fmts,
	.atomic_reset			= drm_atomic_helper_bridge_reset,
	.atomic_pre_enable		= dlpc_atomic_pre_enable,
	.atomic_enable			= dlpc_atomic_enable,
	.atomic_post_disable		= dlpc_atomic_post_disable,
	.mode_set			= dlpc_mode_set,
	.attach				= dlpc_attach,
};

static int dlpc3433_parse_dt(struct dlpc *dlpc)
{
	struct device *dev = dlpc->dev;
	struct device_node *endpoint;
	int ret;

	dlpc->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(dlpc->enable_gpio))
		return PTR_ERR(dlpc->enable_gpio);

	dlpc->vcc_intf = devm_regulator_get(dlpc->dev, "vcc_intf");
	if (IS_ERR(dlpc->vcc_intf))
		return dev_err_probe(dev, PTR_ERR(dlpc->vcc_intf),
				     "failed to get VCC_INTF supply\n");

	dlpc->vcc_flsh = devm_regulator_get(dlpc->dev, "vcc_flsh");
	if (IS_ERR(dlpc->vcc_flsh))
		return dev_err_probe(dev, PTR_ERR(dlpc->vcc_flsh),
				     "failed to get VCC_FLSH supply\n");

	dlpc->next_bridge = devm_drm_of_get_bridge(dev, dev->of_node, 1, 0);
	if (IS_ERR(dlpc->next_bridge))
		return PTR_ERR(dlpc->next_bridge);

	endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 0, 0);
	dlpc->dsi_lanes = of_property_count_u32_elems(endpoint, "data-lanes");
	if (dlpc->dsi_lanes < 0 || dlpc->dsi_lanes > 4) {
		ret = -EINVAL;
		goto err_put_endpoint;
	}

	dlpc->host_node = of_graph_get_remote_port_parent(endpoint);
	if (!dlpc->host_node) {
		ret = -ENODEV;
		goto err_put_host;
	}

	of_node_put(endpoint);

	return 0;

err_put_host:
	of_node_put(dlpc->host_node);
err_put_endpoint:
	of_node_put(endpoint);
	return ret;
}

static int dlpc_host_attach(struct dlpc *dlpc)
{
	struct device *dev = dlpc->dev;
	struct mipi_dsi_host *host;
	struct mipi_dsi_device_info info = {
		.type = "dlpc3433",
		.channel = 0,
		.node = NULL,
	};

	host = of_find_mipi_dsi_host_by_node(dlpc->host_node);
	if (!host) {
		DRM_DEV_ERROR(dev, "failed to find dsi host\n");
		return -EPROBE_DEFER;
	}

	dlpc->dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dlpc->dsi)) {
		DRM_DEV_ERROR(dev, "failed to create dsi device\n");
		return PTR_ERR(dlpc->dsi);
	}

	dlpc->dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST;
	dlpc->dsi->format = MIPI_DSI_FMT_RGB565;
	dlpc->dsi->lanes = dlpc->dsi_lanes;

	return devm_mipi_dsi_attach(dev, dlpc->dsi);
}

static int dlpc3433_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct dlpc *dlpc;
	int ret;

	dlpc = devm_kzalloc(dev, sizeof(*dlpc), GFP_KERNEL);
	if (!dlpc)
		return -ENOMEM;

	dlpc->dev = dev;

	dlpc->regmap = devm_regmap_init_i2c(client, &dlpc_regmap_config);
	if (IS_ERR(dlpc->regmap))
		return PTR_ERR(dlpc->regmap);

	ret = dlpc3433_parse_dt(dlpc);
	if (ret)
		return ret;

	dev_set_drvdata(dev, dlpc);
	i2c_set_clientdata(client, dlpc);

	dlpc->bridge.funcs = &dlpc_bridge_funcs;
	dlpc->bridge.of_node = dev->of_node;
	drm_bridge_add(&dlpc->bridge);

	ret = dlpc_host_attach(dlpc);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to attach dsi host\n");
		goto err_remove_bridge;
	}

	return 0;

err_remove_bridge:
	drm_bridge_remove(&dlpc->bridge);
	return ret;
}

static void dlpc3433_remove(struct i2c_client *client)
{
	struct dlpc *dlpc = i2c_get_clientdata(client);

	drm_bridge_remove(&dlpc->bridge);
	of_node_put(dlpc->host_node);
}

static const struct i2c_device_id dlpc3433_id[] = {
	{ "ti,dlpc3433", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, dlpc3433_id);

static const struct of_device_id dlpc3433_match_table[] = {
	{ .compatible = "ti,dlpc3433" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dlpc3433_match_table);

static struct i2c_driver dlpc3433_driver = {
	.probe = dlpc3433_probe,
	.remove = dlpc3433_remove,
	.id_table = dlpc3433_id,
	.driver = {
		.name = "ti-dlpc3433",
		.of_match_table = dlpc3433_match_table,
	},
};
module_i2c_driver(dlpc3433_driver);

MODULE_AUTHOR("Jagan Teki <jagan@amarulasolutions.com>");
MODULE_AUTHOR("Christopher Vollo <chris@renewoutreach.org>");
MODULE_DESCRIPTION("TI DLPC3433 MIPI DSI Display Controller Bridge");
MODULE_LICENSE("GPL");
