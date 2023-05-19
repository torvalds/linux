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
	struct drm_panel *panel;
	enum drm_connector_status status;

	struct device *dev;
	struct max96745 *parent;
	struct regmap *regmap;
	struct {
		struct gpio_desc *gpio;
		int irq;
		atomic_t triggered;
	} lock;
};

#define to_max96745_bridge(x)	container_of(x, struct max96745_bridge, x)

static bool max96745_bridge_link_locked(struct max96745_bridge *ser)
{
	u32 val;

	if (ser->lock.gpio)
		return gpiod_get_value_cansleep(ser->lock.gpio);

	if (regmap_read(ser->regmap, 0x002a, &val))
		return false;

	if (!FIELD_GET(LINK_LOCKED, val))
		return false;

	return true;
}

static int max96745_bridge_attach(struct drm_bridge *bridge,
				  enum drm_bridge_attach_flags flags)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);
	int ret;

	ret = drm_of_find_panel_or_bridge(bridge->of_node, 1, -1, &ser->panel,
					  NULL);
	if (ret < 0 && ret != -ENODEV)
		return ret;

	if (max96745_bridge_link_locked(ser))
		ser->status = connector_status_connected;
	else
		ser->status = connector_status_disconnected;

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
	struct max96745 *max96745 = ser->parent;
	struct drm_display_mode *mode = &bridge->encoder->crtc->state->adjusted_mode;
	u8 cxtp, tx_rate;
	u32 reg;

	regmap_read(ser->regmap, 0x0011, &reg);
	cxtp = FIELD_GET(CXTP_A, reg);
	regmap_read(ser->regmap, 0x0028, &reg);
	tx_rate = FIELD_GET(TX_RATE, reg);
	if (!cxtp && mode->clock > 95000 && tx_rate == 1) {
		regmap_update_bits(ser->regmap, 0x0028, TX_RATE,
				   FIELD_PREP(TX_RATE, 2));
		regmap_update_bits(ser->regmap, 0x0029, RESET_ONESHOT,
				   FIELD_PREP(RESET_ONESHOT, 1));
		if (regmap_read_poll_timeout(ser->regmap, 0x002a, reg,
					     reg & LINK_LOCKED, 10000, 200000))
			dev_err(ser->dev, "%s: GMSL link not locked\n", __func__);
	}

	if (ser->panel)
		drm_panel_enable(ser->panel);

	extcon_set_state_sync(max96745->extcon, EXTCON_JACK_VIDEO_OUT, true);
}

static void max96745_bridge_disable(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);
	struct max96745 *max96745 = ser->parent;

	extcon_set_state_sync(max96745->extcon, EXTCON_JACK_VIDEO_OUT, false);

	if (ser->panel)
		drm_panel_disable(ser->panel);
}

static void max96745_bridge_post_disable(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);
	u8 cxtp, tx_rate, link_locked;
	u32 reg;

	regmap_read(ser->regmap, 0x002a, &reg);
	link_locked = FIELD_GET(LINK_LOCKED, reg);

	if (ser->panel)
		drm_panel_unprepare(ser->panel);

	regmap_read(ser->regmap, 0x0011, &reg);
	cxtp = FIELD_GET(CXTP_A, reg);
	regmap_read(ser->regmap, 0x0028, &reg);
	tx_rate = FIELD_GET(TX_RATE, reg);
	if (!cxtp && tx_rate == 2) {
		regmap_update_bits(ser->regmap, 0x0028, TX_RATE,
				   FIELD_PREP(TX_RATE, 1));
		regmap_update_bits(ser->regmap, 0x0029, RESET_ONESHOT,
				   FIELD_PREP(RESET_ONESHOT, 1));
		if (link_locked) {
			if (regmap_read_poll_timeout(ser->regmap, 0x002a, reg,
					     reg & LINK_LOCKED, 10000, 200000))
				dev_err(ser->dev, "%s: GMSL link not locked\n", __func__);
		}
	}
}

static enum drm_connector_status
max96745_bridge_detect(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);
	struct max96745 *max96745 = ser->parent;
	enum drm_connector_status status = connector_status_connected;

	if (!drm_kms_helper_is_poll_worker())
		return ser->status;

	if (!max96745_bridge_link_locked(ser)) {
		status = connector_status_disconnected;
		goto out;
	}

	if (extcon_get_state(max96745->extcon, EXTCON_JACK_VIDEO_OUT)) {
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

out:
	ser->status = status;
	return status;
}

static int max96745_bridge_get_modes(struct drm_bridge *bridge,
				     struct drm_connector *connector)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);

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
	struct max96745 *max96745 = ser->parent;

	if (extcon_get_state(max96745->extcon, EXTCON_JACK_VIDEO_OUT))
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
	ser->parent = dev_get_drvdata(dev->parent);
	platform_set_drvdata(pdev, ser);

	ser->regmap = dev_get_regmap(dev->parent, NULL);
	if (!ser->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get regmap\n");

	ser->lock.gpio = devm_gpiod_get_optional(dev, "lock", GPIOD_IN);
	if (IS_ERR(ser->lock.gpio))
		return dev_err_probe(dev, PTR_ERR(ser->lock.gpio),
				     "failed to get lock GPIO\n");

	if (ser->lock.gpio) {
		ser->lock.irq = gpiod_to_irq(ser->lock.gpio);
		if (ser->lock.irq < 0)
			return ser->lock.irq;

		ret = devm_request_threaded_irq(dev, ser->lock.irq, NULL,
						max96745_bridge_lock_irq_handler,
						IRQF_TRIGGER_RISING | IRQF_ONESHOT,
						dev_name(dev), ser);
		if (ret)
			return dev_err_probe(dev, ret, "failed to request lock IRQ\n");
	}

	ser->bridge.funcs = &max96745_bridge_funcs;
	ser->bridge.of_node = dev->of_node;
	ser->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_MODES;

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
