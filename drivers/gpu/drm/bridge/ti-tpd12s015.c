// SPDX-License-Identifier: GPL-2.0
/*
 * TPD12S015 HDMI ESD protection & level shifter chip driver
 *
 * Copyright (C) 2019 Texas Instruments Incorporated
 *
 * Based on the omapdrm-specific encoder-opa362 driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#include <drm/drm_bridge.h>

struct tpd12s015_device {
	struct drm_bridge bridge;

	struct gpio_desc *ct_cp_hpd_gpio;
	struct gpio_desc *ls_oe_gpio;
	struct gpio_desc *hpd_gpio;
	int hpd_irq;

	struct drm_bridge *next_bridge;
};

static inline struct tpd12s015_device *to_tpd12s015(struct drm_bridge *bridge)
{
	return container_of(bridge, struct tpd12s015_device, bridge);
}

static int tpd12s015_attach(struct drm_bridge *bridge,
			    enum drm_bridge_attach_flags flags)
{
	struct tpd12s015_device *tpd = to_tpd12s015(bridge);
	int ret;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR))
		return -EINVAL;

	ret = drm_bridge_attach(bridge->encoder, tpd->next_bridge,
				bridge, flags);
	if (ret < 0)
		return ret;

	gpiod_set_value_cansleep(tpd->ls_oe_gpio, 1);

	/* DC-DC converter needs at max 300us to get to 90% of 5V. */
	usleep_range(300, 1000);

	return 0;
}

static void tpd12s015_detach(struct drm_bridge *bridge)
{
	struct tpd12s015_device *tpd = to_tpd12s015(bridge);

	gpiod_set_value_cansleep(tpd->ls_oe_gpio, 0);
}

static enum drm_connector_status tpd12s015_detect(struct drm_bridge *bridge)
{
	struct tpd12s015_device *tpd = to_tpd12s015(bridge);

	if (gpiod_get_value_cansleep(tpd->hpd_gpio))
		return connector_status_connected;
	else
		return connector_status_disconnected;
}

static void tpd12s015_hpd_enable(struct drm_bridge *bridge)
{
	struct tpd12s015_device *tpd = to_tpd12s015(bridge);

	gpiod_set_value_cansleep(tpd->ct_cp_hpd_gpio, 1);
}

static void tpd12s015_hpd_disable(struct drm_bridge *bridge)
{
	struct tpd12s015_device *tpd = to_tpd12s015(bridge);

	gpiod_set_value_cansleep(tpd->ct_cp_hpd_gpio, 0);
}

static const struct drm_bridge_funcs tpd12s015_bridge_funcs = {
	.attach			= tpd12s015_attach,
	.detach			= tpd12s015_detach,
	.detect			= tpd12s015_detect,
	.hpd_enable		= tpd12s015_hpd_enable,
	.hpd_disable		= tpd12s015_hpd_disable,
};

static irqreturn_t tpd12s015_hpd_isr(int irq, void *data)
{
	struct tpd12s015_device *tpd = data;
	struct drm_bridge *bridge = &tpd->bridge;

	drm_bridge_hpd_notify(bridge, tpd12s015_detect(bridge));

	return IRQ_HANDLED;
}

static int tpd12s015_probe(struct platform_device *pdev)
{
	struct tpd12s015_device *tpd;
	struct device_node *node;
	struct gpio_desc *gpio;
	int ret;

	tpd = devm_kzalloc(&pdev->dev, sizeof(*tpd), GFP_KERNEL);
	if (!tpd)
		return -ENOMEM;

	platform_set_drvdata(pdev, tpd);

	tpd->bridge.funcs = &tpd12s015_bridge_funcs;
	tpd->bridge.of_node = pdev->dev.of_node;
	tpd->bridge.type = DRM_MODE_CONNECTOR_HDMIA;
	tpd->bridge.ops = DRM_BRIDGE_OP_DETECT;

	/* Get the next bridge, connected to port@1. */
	node = of_graph_get_remote_node(pdev->dev.of_node, 1, -1);
	if (!node)
		return -ENODEV;

	tpd->next_bridge = of_drm_find_bridge(node);
	of_node_put(node);

	if (!tpd->next_bridge)
		return -EPROBE_DEFER;

	/* Get the control and HPD GPIOs. */
	gpio = devm_gpiod_get_index_optional(&pdev->dev, NULL, 0,
					     GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	tpd->ct_cp_hpd_gpio = gpio;

	gpio = devm_gpiod_get_index_optional(&pdev->dev, NULL, 1,
					     GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	tpd->ls_oe_gpio = gpio;

	gpio = devm_gpiod_get_index(&pdev->dev, NULL, 2, GPIOD_IN);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	tpd->hpd_gpio = gpio;

	/* Register the IRQ if the HPD GPIO is IRQ-capable. */
	tpd->hpd_irq = gpiod_to_irq(tpd->hpd_gpio);
	if (tpd->hpd_irq >= 0) {
		ret = devm_request_threaded_irq(&pdev->dev, tpd->hpd_irq, NULL,
						tpd12s015_hpd_isr,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"tpd12s015 hpd", tpd);
		if (ret)
			return ret;

		tpd->bridge.ops |= DRM_BRIDGE_OP_HPD;
	}

	/* Register the DRM bridge. */
	drm_bridge_add(&tpd->bridge);

	return 0;
}

static void tpd12s015_remove(struct platform_device *pdev)
{
	struct tpd12s015_device *tpd = platform_get_drvdata(pdev);

	drm_bridge_remove(&tpd->bridge);
}

static const struct of_device_id tpd12s015_of_match[] = {
	{ .compatible = "ti,tpd12s015", },
	{},
};

MODULE_DEVICE_TABLE(of, tpd12s015_of_match);

static struct platform_driver tpd12s015_driver = {
	.probe	= tpd12s015_probe,
	.remove_new = tpd12s015_remove,
	.driver	= {
		.name	= "tpd12s015",
		.of_match_table = tpd12s015_of_match,
	},
};

module_platform_driver(tpd12s015_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("TPD12S015 HDMI level shifter and ESD protection driver");
MODULE_LICENSE("GPL");
