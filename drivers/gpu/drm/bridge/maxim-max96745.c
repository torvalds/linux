// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim MAX96745 GMSL2 Serializer with eDP1.4a/DP1.4 Input
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_of.h>
#include <drm/drm_connector.h>
#include <drm/drm_probe_helper.h>

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/extcon-provider.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/max96745.h>

struct max96745_bridge {
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_panel *panel;
	enum drm_connector_status status;

	struct device *dev;
	struct regmap *regmap;
	struct {
		struct gpio_desc *gpio;
		int irq;
		atomic_t triggered;
	} lock;
	struct extcon_dev *extcon;
};

#define to_max96745_bridge(x)	container_of(x, struct max96745_bridge, x)

static const unsigned int max96745_bridge_cable[] = {
	EXTCON_JACK_VIDEO_OUT,
	EXTCON_NONE,
};

static bool max96745_bridge_link_locked(struct max96745_bridge *ser)
{
	u32 val;

	if (regmap_read(ser->regmap, 0x002a, &val))
		return false;

	if (!FIELD_GET(LINK_LOCKED, val))
		return false;

	return true;
}

static bool max96745_bridge_vid_tx_active(struct max96745_bridge *ser)
{
	u32 val;

	if (regmap_read(ser->regmap, 0x0107, &val))
		return false;

	if (!FIELD_GET(VID_TX_ACTIVE_A | VID_TX_ACTIVE_B, val))
		return false;

	return true;
}

static int max96745_bridge_attach(struct drm_bridge *bridge,
				  enum drm_bridge_attach_flags flags)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);
	int ret;

	ret = drm_of_find_panel_or_bridge(bridge->of_node, 1, -1, &ser->panel,
					  &ser->next_bridge);
	if (ret < 0 && ret != -ENODEV)
		return ret;

	if (ser->next_bridge) {
		ret = drm_bridge_attach(bridge->encoder, ser->next_bridge,
					bridge, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
		if (ret)
			return ret;
	}

	if (max96745_bridge_link_locked(ser))
		ser->status = connector_status_connected;
	else
		ser->status = connector_status_disconnected;

	extcon_set_state(ser->extcon, EXTCON_JACK_VIDEO_OUT,
			 max96745_bridge_vid_tx_active(ser));

	return 0;
}

static void max96745_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);

	if (ser->panel)
		drm_panel_prepare(ser->panel);
}

static void max96745_bridge_enable(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);

	if (ser->panel)
		drm_panel_enable(ser->panel);

	extcon_set_state_sync(ser->extcon, EXTCON_JACK_VIDEO_OUT, true);
}

static void max96745_bridge_disable(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);

	extcon_set_state_sync(ser->extcon, EXTCON_JACK_VIDEO_OUT, false);

	if (ser->panel)
		drm_panel_disable(ser->panel);
}

static void max96745_bridge_post_disable(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);

	if (ser->panel)
		drm_panel_unprepare(ser->panel);
}

static enum drm_connector_status
max96745_bridge_detect(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);
	enum drm_connector_status status = connector_status_connected;

	if (!drm_kms_helper_is_poll_worker())
		return ser->status;

	if (!max96745_bridge_link_locked(ser)) {
		status = connector_status_disconnected;
		goto out;
	}

	if (extcon_get_state(ser->extcon, EXTCON_JACK_VIDEO_OUT)) {
		u32 dprx_trn_status2;

		if (atomic_cmpxchg(&ser->lock.triggered, 1, 0)) {
			status = connector_status_disconnected;
			goto out;
		}

		if (regmap_read(ser->regmap, 0x641a, &dprx_trn_status2)) {
			status = connector_status_disconnected;
			goto out;
		}

		if ((dprx_trn_status2 & DPRX_TRAIN_STATE) != DPRX_TRAIN_STATE) {
			dev_err(ser->dev, "Training State: 0x%lx\n",
				FIELD_GET(DPRX_TRAIN_STATE, dprx_trn_status2));
			status = connector_status_disconnected;
			goto out;
		}
	} else {
		atomic_set(&ser->lock.triggered, 0);
	}

	if (ser->next_bridge && (ser->next_bridge->ops & DRM_BRIDGE_OP_DETECT))
		status = drm_bridge_detect(ser->next_bridge);

out:
	ser->status = status;
	return status;
}

static int max96745_bridge_get_modes(struct drm_bridge *bridge,
				     struct drm_connector *connector)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);

	if (ser->next_bridge)
		return drm_bridge_get_modes(ser->next_bridge, connector);

	if (ser->panel)
		return drm_panel_get_modes(ser->panel, connector);

	return drm_add_modes_noedid(connector, 1920, 1080);
}

static const struct drm_bridge_funcs max96745_bridge_funcs = {
	.attach = max96745_bridge_attach,
	.detect = max96745_bridge_detect,
	.get_modes = max96745_bridge_get_modes,
	.pre_enable = max96745_bridge_pre_enable,
	.enable = max96745_bridge_enable,
	.disable = max96745_bridge_disable,
	.post_disable = max96745_bridge_post_disable,
	.atomic_get_input_bus_fmts = drm_atomic_helper_bridge_propagate_bus_fmt,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static irqreturn_t max96745_bridge_lock_irq_handler(int irq, void *arg)
{
	struct max96745_bridge *ser = arg;

	if (extcon_get_state(ser->extcon, EXTCON_JACK_VIDEO_OUT))
		atomic_set(&ser->lock.triggered, 1);

	return IRQ_HANDLED;
}

static int max96745_bridge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max96745_bridge *ser;
	int ret;

	ser = devm_kzalloc(dev, sizeof(*ser), GFP_KERNEL);
	if (!ser)
		return -ENOMEM;

	ser->dev = dev;
	platform_set_drvdata(pdev, ser);

	ser->regmap = dev_get_regmap(dev->parent, NULL);
	if (!ser->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get regmap\n");

	ser->lock.gpio = devm_gpiod_get(dev, "lock", GPIOD_IN);
	if (IS_ERR(ser->lock.gpio))
		return dev_err_probe(dev, PTR_ERR(ser->lock.gpio),
				     "failed to get lock GPIO\n");

	ser->extcon = devm_extcon_dev_allocate(dev, max96745_bridge_cable);
	if (IS_ERR(ser->extcon))
		return dev_err_probe(dev, PTR_ERR(ser->extcon),
				     "failed to allocate extcon device\n");

	ret = devm_extcon_dev_register(dev, ser->extcon);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register extcon device\n");

	ser->lock.irq = gpiod_to_irq(ser->lock.gpio);
	if (ser->lock.irq < 0)
		return ser->lock.irq;

	ret = devm_request_threaded_irq(dev, ser->lock.irq, NULL,
					max96745_bridge_lock_irq_handler,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					dev_name(dev), ser);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request lock IRQ\n");

	ser->bridge.funcs = &max96745_bridge_funcs;
	ser->bridge.of_node = dev->of_node;
	ser->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_MODES;
	ser->bridge.type = DRM_MODE_CONNECTOR_LVDS;

	drm_bridge_add(&ser->bridge);

	return 0;
}

static int max96745_bridge_remove(struct platform_device *pdev)
{
	struct max96745_bridge *ser = platform_get_drvdata(pdev);

	drm_bridge_remove(&ser->bridge);

	return 0;
}

static const struct of_device_id max96745_bridge_of_match[] = {
	{ .compatible = "maxim,max96745-bridge", },
	{}
};
MODULE_DEVICE_TABLE(of, max96745_bridge_of_match);

static struct platform_driver max96745_bridge_driver = {
	.driver = {
		.name = "max96745-bridge",
		.of_match_table = of_match_ptr(max96745_bridge_of_match),
	},
	.probe = max96745_bridge_probe,
	.remove = max96745_bridge_remove,
};

module_platform_driver(max96745_bridge_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX96745 GMSL2 Serializer with eDP1.4a/DP1.4 Input");
MODULE_LICENSE("GPL");
