/*
 * Driver for rockchip rk1000 tv encoder
 *  Copyright (C) 2017 Fuzhou Rockchip Electronics Co.Ltd
 *  Author:
 *      Algea Cao <algea.cao@rock-chips.com>
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drmP.h>

#include "../rockchip/rockchip_drm_drv.h"

#define TVE_POWCR 0x03
#define TVE_OFF 0X07
#define TVE_ON 0x03

static const struct drm_display_mode rk1000_cvbs_mode[2] = {
	{ DRM_MODE("720x576", DRM_MODE_TYPE_DRIVER, 27000, 720, 732,
		   738, 864, 0, 576, 582, 588, 625, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
		   .vrefresh = 50, 0, },
	{ DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 27000, 720, 736,
		   742, 858, 0, 480, 486, 492, 529, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
		   .vrefresh = 60, 0, },
};

struct rk1000_tve {
	struct device *dev;
	struct i2c_client *client;
	struct drm_connector connector;
	struct drm_bridge bridge;
	struct drm_encoder *encoder;
	int mode;
	struct regmap *ctlmap;
	struct regmap *tvemap;
	u32 preferred_mode;
	struct rockchip_drm_sub_dev sub_dev;
};

enum {
	CVBS_NTSC = 0,
	CVBS_PAL,
};

static const u8 pal_tve_regs[] = {0x06, 0x00, 0x00, 0x03, 0x00, 0x00};
static const u8 pal_ctl_regs[] = {0x41, 0x01};

static const u8 ntsc_tve_regs[] = {0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
static const u8 ntsc_ctl_regs[] = {0x43, 0x01};

static const struct regmap_range rk1000_tve_volatile_ranges[] = {
	{ .range_min = 0x00, .range_max = 0x05 },
};

static const struct regmap_access_table rk1000_tve_reg_table = {
	.yes_ranges = rk1000_tve_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(rk1000_tve_volatile_ranges),
};

static const struct regmap_config rk1000_tve_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.volatile_table = &rk1000_tve_reg_table,
};

static const struct regmap_range rk1000_ctl_volatile_ranges[] = {
	{ .range_min = 0x03, .range_max = 0x04 },
};

static const struct regmap_access_table rk1000_ctl_reg_table = {
	.yes_ranges = rk1000_ctl_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(rk1000_ctl_volatile_ranges),
};

static const struct regmap_config rk1000_ctl_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.volatile_table = &rk1000_ctl_reg_table,
};

static struct rk1000_tve *bridge_to_rk1000(struct drm_bridge *bridge)
{
	return container_of(bridge, struct rk1000_tve, bridge);
}

static struct rk1000_tve *connector_to_rk1000(struct drm_connector *connector)
{
	return container_of(connector, struct rk1000_tve, connector);
}

static int rk1000_tv_write_block(struct rk1000_tve *rk1000,
				 u8 reg, const u8 *buf, u8 len)
{
	int i, ret = 0;

	for (i = 0; i < len; i++) {
		ret = regmap_write(rk1000->tvemap, reg + i, buf[i]);
		if (ret)
			break;
	}

	return ret;
}

static int rk1000_control_write_block(struct rk1000_tve *rk1000,
				      u8 reg, const u8 *buf, u8 len)
{
	int i, ret = 0;

	for (i = 0; i < len; i++) {
		ret = regmap_write(rk1000->ctlmap, reg + i, buf[i]);
		if (ret)
			break;
	}

	return ret;
}

static int rk1000_mode_set(struct rk1000_tve *rk1000)
{
	int ret;
	const u8 *tv_regs, *control_regs;

	switch (rk1000->mode) {
	case CVBS_PAL:
		dev_dbg(rk1000->dev, "rk1000 PAL\n");
		tv_regs = pal_tve_regs;
		control_regs = pal_ctl_regs;
		break;
	case CVBS_NTSC:
		dev_dbg(rk1000->dev, "rk1000 NTSC\n");
		tv_regs = ntsc_tve_regs;
		control_regs = ntsc_ctl_regs;
		break;
	default:
		dev_dbg(rk1000->dev, "mode select err\n");
		return -EINVAL;
	}

	ret = rk1000_tv_write_block(rk1000, 0, tv_regs, 6);
	if (ret) {
		dev_err(rk1000->dev, "rk1000_tv_write_block err!\n");
		return ret;
	}

	ret = rk1000_control_write_block(rk1000, 0x03, control_regs, 2);
	if (ret < 0) {
		dev_err(rk1000->dev, "rk1000 control write block err\n");
		return ret;
	}

	return ret;
}

static int rk1000_tve_disable(struct rk1000_tve *rk1000)
{
	int ret;
	u8 val;

	val = TVE_OFF;
	ret = rk1000_tv_write_block(rk1000, TVE_POWCR, &val, 1);

	return ret;
}

static int rk1000_tve_enable(struct rk1000_tve *rk1000)
{
	int ret;
	u8 val;

	dev_dbg(rk1000->dev, "%s\n", __func__);
	ret = rk1000_mode_set(rk1000);
	if (ret)
		return ret;

	val = TVE_ON;
	ret = rk1000_tv_write_block(rk1000, TVE_POWCR, &val, 1);
	if (ret)
		return ret;

	return 0;
}

static enum drm_mode_status
rk1000_mode_valid(struct drm_connector *connector,
		  struct drm_display_mode *mode)
{
	return MODE_OK;
}

static int
rk1000_get_modes(struct drm_connector *connector)
{
	int count;
	struct rk1000_tve *rk1000 = connector_to_rk1000(connector);

	for (count = 0; count < ARRAY_SIZE(rk1000_cvbs_mode); count++) {
		struct drm_display_mode *mode_ptr;

		mode_ptr = drm_mode_duplicate(connector->dev,
					      &rk1000_cvbs_mode[count]);
		if (!mode_ptr) {
			dev_err(rk1000->dev, "mode duplicate failed\n");
			return -ENOMEM;
		}
		if (rk1000->preferred_mode == count)
			mode_ptr->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode_ptr);
	}

	return count;
}

static enum drm_connector_status
rk1000_connector_detect(struct drm_connector *connector,
			bool force)
{
	return connector_status_connected;
}

static struct drm_encoder *rk1000_best_encoder(struct drm_connector *connector)
{
	struct rk1000_tve *rk1000 = connector_to_rk1000(connector);

	return rk1000->encoder;
}

static
const struct drm_connector_helper_funcs rk1000_connector_helper_funcs = {
	.get_modes = rk1000_get_modes,
	.mode_valid = rk1000_mode_valid,
	.best_encoder = rk1000_best_encoder,
};

static const struct drm_connector_funcs rk1000_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = rk1000_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void
rk1000_bridge_mode_set(struct drm_bridge *bridge,
		       struct drm_display_mode *mode,
		       struct drm_display_mode *adjusted_mode)
{
	struct rk1000_tve *rk1000;

	rk1000 = bridge_to_rk1000(bridge);
	dev_dbg(rk1000->dev, "encoder mode set:%s\n", adjusted_mode->name);

	if (adjusted_mode->vdisplay == 576)
		rk1000->mode = CVBS_PAL;
	else
		rk1000->mode = CVBS_NTSC;
}

static void rk1000_bridge_enable(struct drm_bridge *bridge)
{
	int ret;
	struct rk1000_tve *rk1000 = bridge_to_rk1000(bridge);

	dev_dbg(rk1000->dev, "%s\n",  __func__);
	ret = rk1000_tve_enable(rk1000);
	if (ret)
		dev_err(rk1000->dev, "rk1000 enable failed\n");
}

static void rk1000_bridge_disable(struct drm_bridge *bridge)
{
	struct rk1000_tve *rk1000 = bridge_to_rk1000(bridge);

	dev_dbg(rk1000->dev, "%s\n",  __func__);
	rk1000_tve_disable(rk1000);
}

static int rk1000_bridge_attach(struct drm_bridge *bridge)
{
	struct rk1000_tve *rk1000 = bridge_to_rk1000(bridge);
	int ret;

	if (!bridge->encoder) {
		dev_err(rk1000->dev, "Parent encoder object not found\n");
		return -ENODEV;
	}

	rk1000->encoder = bridge->encoder;
	drm_connector_helper_add(&rk1000->connector,
				 &rk1000_connector_helper_funcs);

	ret = drm_connector_init(bridge->dev, &rk1000->connector,
				 &rk1000_connector_funcs,
				 DRM_MODE_CONNECTOR_TV);
	if (ret) {
		dev_err(rk1000->dev, "Failed to initialize connector\n");
		return ret;
	}
	ret = drm_connector_attach_encoder(&rk1000->connector,
					   bridge->encoder);
	if (ret)
		dev_err(rk1000->dev, "rk1000 attach failed ret:%d", ret);

	rk1000->sub_dev.connector = &rk1000->connector;
	rk1000->sub_dev.of_node = rk1000->dev->of_node;
	rockchip_drm_register_sub_dev(&rk1000->sub_dev);

	return ret;
}

static void rk1000_bridge_detach(struct drm_bridge *bridge)
{
	struct rk1000_tve *rk1000 = bridge_to_rk1000(bridge);

	rockchip_drm_unregister_sub_dev(&rk1000->sub_dev);
}

static struct drm_bridge_funcs rk1000_bridge_funcs = {
	.enable = rk1000_bridge_enable,
	.disable = rk1000_bridge_disable,
	.mode_set = rk1000_bridge_mode_set,
	.attach = rk1000_bridge_attach,
	.detach = rk1000_bridge_detach,
};

static int rk1000_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct device_node *np;
	struct device_node *ctl;
	struct rk1000_tve *rk1000;
	struct i2c_client *ctl_client;

	rk1000 = devm_kzalloc(&client->dev, sizeof(*rk1000), GFP_KERNEL);
	if (!rk1000)
		return -ENOMEM;

	np = client->dev.of_node;
	rk1000->client = client;
	rk1000->dev = &client->dev;
	rk1000->mode = CVBS_PAL;

	rk1000->tvemap = devm_regmap_init_i2c(client,
					      &rk1000_tve_regmap_config);
	if (IS_ERR(rk1000->tvemap)) {
		ret = PTR_ERR(rk1000->tvemap);
		dev_err(rk1000->dev, "Failed to initialize tve regmap: %d\n",
			ret);
		return ret;
	}

	ctl = of_parse_phandle(np, "rockchip,ctl", 0);
	if (!ctl) {
		dev_err(rk1000->dev, "rk1000 can't find control node\n");
		return -EINVAL;
	}

	ctl_client = of_find_i2c_device_by_node(ctl);
	if (!ctl_client) {
		dev_err(rk1000->dev, "rk1000 can't find control client\n");
		return -EPROBE_DEFER;
	}

	rk1000->ctlmap = devm_regmap_init_i2c(ctl_client,
					      &rk1000_ctl_regmap_config);
	if (IS_ERR(rk1000->ctlmap)) {
		ret = PTR_ERR(rk1000->ctlmap);
		dev_err(rk1000->dev, "Failed to initialize ctl regmap: %d\n",
			ret);
		return ret;
	}

	ret = of_property_read_u32(np, "rockchip,tvemode",
				   &rk1000->preferred_mode);
	if (ret < 0) {
		rk1000->preferred_mode = 0;
	} else if (rk1000->preferred_mode > 1) {
		dev_err(rk1000->dev, "rk1000 tve mode value invalid\n");
		return -EINVAL;
	}

	rk1000->bridge.funcs = &rk1000_bridge_funcs;
	rk1000->bridge.of_node = rk1000->dev->of_node;

	drm_bridge_add(&rk1000->bridge);

	i2c_set_clientdata(client, rk1000);
	dev_dbg(rk1000->dev, "rk1000 probe ok\n");

	return 0;
}

static int rk1000_remove(struct i2c_client *client)
{
	struct rk1000_tve *rk1000 = i2c_get_clientdata(client);

	drm_bridge_remove(&rk1000->bridge);

	return 0;
}

static const struct i2c_device_id rk1000_i2c_id[] = {
	{ "rk1000-tve", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, rk1000_i2c_id);

static const struct of_device_id rk1000_dt_ids[] = {
	{ .compatible = "rockchip,rk1000-tve" },
	{ }
};

MODULE_DEVICE_TABLE(of, rk1000_dt_ids);

static struct i2c_driver rk1000_driver = {
	.driver = {
		.name = "rk1000-tve",
		.of_match_table = of_match_ptr(rk1000_dt_ids),
	},
	.id_table = rk1000_i2c_id,
	.probe = rk1000_probe,
	.remove = rk1000_remove,
};
module_i2c_driver(rk1000_driver);

MODULE_AUTHOR("Algea Cao <Algea.cao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP RK1000 Driver");
MODULE_LICENSE("GPL v2");
