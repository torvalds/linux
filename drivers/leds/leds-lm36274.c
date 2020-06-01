// SPDX-License-Identifier: GPL-2.0
// TI LM36274 LED chip family driver
// Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/leds.h>
#include <linux/leds-ti-lmu-common.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <linux/mfd/ti-lmu.h>
#include <linux/mfd/ti-lmu-register.h>

#include <uapi/linux/uleds.h>

#define LM36274_MAX_STRINGS	4
#define LM36274_BL_EN		BIT(4)

/**
 * struct lm36274
 * @pdev: platform device
 * @led_dev: led class device
 * @lmu_data: Register and setting values for common code
 * @regmap: Devices register map
 * @dev: Pointer to the devices device struct
 * @led_sources - The LED strings supported in this array
 * @num_leds - Number of LED strings are supported in this array
 */
struct lm36274 {
	struct platform_device *pdev;
	struct led_classdev led_dev;
	struct ti_lmu_bank lmu_data;
	struct regmap *regmap;
	struct device *dev;

	u32 led_sources[LM36274_MAX_STRINGS];
	int num_leds;
};

static int lm36274_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness brt_val)
{
	struct lm36274 *led = container_of(led_cdev, struct lm36274, led_dev);

	return ti_lmu_common_set_brightness(&led->lmu_data, brt_val);
}

static int lm36274_init(struct lm36274 *lm36274_data)
{
	int enable_val = 0;
	int i;

	for (i = 0; i < lm36274_data->num_leds; i++)
		enable_val |= (1 << lm36274_data->led_sources[i]);

	if (!enable_val) {
		dev_err(lm36274_data->dev, "No LEDs were enabled\n");
		return -EINVAL;
	}

	enable_val |= LM36274_BL_EN;

	return regmap_write(lm36274_data->regmap, LM36274_REG_BL_EN,
			    enable_val);
}

static int lm36274_parse_dt(struct lm36274 *lm36274_data)
{
	struct fwnode_handle *child = NULL;
	char label[LED_MAX_NAME_SIZE];
	struct device *dev = &lm36274_data->pdev->dev;
	const char *name;
	int child_cnt;
	int ret = -EINVAL;

	/* There should only be 1 node */
	child_cnt = device_get_child_node_count(dev);
	if (child_cnt != 1)
		return -EINVAL;

	device_for_each_child_node(dev, child) {
		ret = fwnode_property_read_string(child, "label", &name);
		if (ret)
			snprintf(label, sizeof(label),
				"%s::", lm36274_data->pdev->name);
		else
			snprintf(label, sizeof(label),
				 "%s:%s", lm36274_data->pdev->name, name);

		lm36274_data->num_leds = fwnode_property_count_u32(child, "led-sources");
		if (lm36274_data->num_leds <= 0)
			return -ENODEV;

		ret = fwnode_property_read_u32_array(child, "led-sources",
						     lm36274_data->led_sources,
						     lm36274_data->num_leds);
		if (ret) {
			dev_err(dev, "led-sources property missing\n");
			return ret;
		}

		fwnode_property_read_string(child, "linux,default-trigger",
					&lm36274_data->led_dev.default_trigger);

	}

	lm36274_data->lmu_data.regmap = lm36274_data->regmap;
	lm36274_data->lmu_data.max_brightness = MAX_BRIGHTNESS_11BIT;
	lm36274_data->lmu_data.msb_brightness_reg = LM36274_REG_BRT_MSB;
	lm36274_data->lmu_data.lsb_brightness_reg = LM36274_REG_BRT_LSB;

	lm36274_data->led_dev.name = label;
	lm36274_data->led_dev.max_brightness = MAX_BRIGHTNESS_11BIT;
	lm36274_data->led_dev.brightness_set_blocking = lm36274_brightness_set;

	return 0;
}

static int lm36274_probe(struct platform_device *pdev)
{
	struct ti_lmu *lmu = dev_get_drvdata(pdev->dev.parent);
	struct lm36274 *lm36274_data;
	int ret;

	lm36274_data = devm_kzalloc(&pdev->dev, sizeof(*lm36274_data),
				    GFP_KERNEL);
	if (!lm36274_data)
		return -ENOMEM;

	lm36274_data->pdev = pdev;
	lm36274_data->dev = lmu->dev;
	lm36274_data->regmap = lmu->regmap;
	platform_set_drvdata(pdev, lm36274_data);

	ret = lm36274_parse_dt(lm36274_data);
	if (ret) {
		dev_err(lm36274_data->dev, "Failed to parse DT node\n");
		return ret;
	}

	ret = lm36274_init(lm36274_data);
	if (ret) {
		dev_err(lm36274_data->dev, "Failed to init the device\n");
		return ret;
	}

	return led_classdev_register(lm36274_data->dev, &lm36274_data->led_dev);
}

static int lm36274_remove(struct platform_device *pdev)
{
	struct lm36274 *lm36274_data = platform_get_drvdata(pdev);

	led_classdev_unregister(&lm36274_data->led_dev);

	return 0;
}

static const struct of_device_id of_lm36274_leds_match[] = {
	{ .compatible = "ti,lm36274-backlight", },
	{},
};
MODULE_DEVICE_TABLE(of, of_lm36274_leds_match);

static struct platform_driver lm36274_driver = {
	.probe  = lm36274_probe,
	.remove = lm36274_remove,
	.driver = {
		.name = "lm36274-leds",
	},
};
module_platform_driver(lm36274_driver)

MODULE_DESCRIPTION("Texas Instruments LM36274 LED driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_LICENSE("GPL v2");
