// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for a 7-segment LED display
 *
 * The decimal point LED present on some devices is currently not
 * supported.
 *
 * Copyright (C) Allied Telesis Labs
 */

#include <linux/bitmap.h>
#include <linux/container_of.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/map_to_7segment.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "line-display.h"

struct seg_led_priv {
	struct linedisp linedisp;
	struct delayed_work work;
	struct gpio_descs *segment_gpios;
};

static void seg_led_update(struct work_struct *work)
{
	struct seg_led_priv *priv = container_of(work, struct seg_led_priv, work.work);
	struct linedisp *linedisp = &priv->linedisp;
	struct linedisp_map *map = linedisp->map;
	DECLARE_BITMAP(values, 8) = { };

	bitmap_set_value8(values, map_to_seg7(&map->map.seg7, linedisp->buf[0]), 0);

	gpiod_set_array_value_cansleep(priv->segment_gpios->ndescs, priv->segment_gpios->desc,
				       priv->segment_gpios->info, values);
}

static int seg_led_linedisp_get_map_type(struct linedisp *linedisp)
{
	struct seg_led_priv *priv = container_of(linedisp, struct seg_led_priv, linedisp);

	INIT_DELAYED_WORK(&priv->work, seg_led_update);
	return LINEDISP_MAP_SEG7;
}

static void seg_led_linedisp_update(struct linedisp *linedisp)
{
	struct seg_led_priv *priv = container_of(linedisp, struct seg_led_priv, linedisp);

	schedule_delayed_work(&priv->work, 0);
}

static const struct linedisp_ops seg_led_linedisp_ops = {
	.get_map_type = seg_led_linedisp_get_map_type,
	.update = seg_led_linedisp_update,
};

static int seg_led_probe(struct platform_device *pdev)
{
	struct seg_led_priv *priv;
	struct device *dev = &pdev->dev;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	priv->segment_gpios = devm_gpiod_get_array(dev, "segment", GPIOD_OUT_LOW);
	if (IS_ERR(priv->segment_gpios))
		return PTR_ERR(priv->segment_gpios);

	if (priv->segment_gpios->ndescs < 7 || priv->segment_gpios->ndescs > 8)
		return -EINVAL;

	return linedisp_register(&priv->linedisp, dev, 1, &seg_led_linedisp_ops);
}

static void seg_led_remove(struct platform_device *pdev)
{
	struct seg_led_priv *priv = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&priv->work);
	linedisp_unregister(&priv->linedisp);
}

static const struct of_device_id seg_led_of_match[] = {
	{ .compatible = "gpio-7-segment"},
	{}
};
MODULE_DEVICE_TABLE(of, seg_led_of_match);

static struct platform_driver seg_led_driver = {
	.probe = seg_led_probe,
	.remove_new = seg_led_remove,
	.driver = {
		.name = "seg-led-gpio",
		.of_match_table = seg_led_of_match,
	},
};
module_platform_driver(seg_led_driver);

MODULE_AUTHOR("Chris Packham <chris.packham@alliedtelesis.co.nz>");
MODULE_DESCRIPTION("7 segment LED driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(LINEDISP);
