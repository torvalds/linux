// SPDX-License-Identifier: GPL-2.0
// TI LM36274 LED chip family driver
// Copyright (C) 2019 Texas Instruments Incorporated - https://www.ti.com/

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
	struct lm36274 *chip = container_of(led_cdev, struct lm36274, led_dev);

	return ti_lmu_common_set_brightness(&chip->lmu_data, brt_val);
}

static int lm36274_init(struct lm36274 *chip)
{
	int enable_val = 0;
	int i;

	for (i = 0; i < chip->num_leds; i++)
		enable_val |= (1 << chip->led_sources[i]);

	if (!enable_val) {
		dev_err(chip->dev, "No LEDs were enabled\n");
		return -EINVAL;
	}

	enable_val |= LM36274_BL_EN;

	return regmap_write(chip->regmap, LM36274_REG_BL_EN, enable_val);
}

static int lm36274_parse_dt(struct lm36274 *chip,
			    struct led_init_data *init_data)
{
	struct device *dev = &chip->pdev->dev;
	struct fwnode_handle *child;
	int ret;

	/* There should only be 1 node */
	if (device_get_child_node_count(dev) != 1)
		return -EINVAL;

	child = device_get_next_child_node(dev, NULL);

	init_data->fwnode = child;
	init_data->devicename = chip->pdev->name;
	/* for backwards compatibility when `label` property is not present */
	init_data->default_label = ":";

	chip->num_leds = fwnode_property_count_u32(child, "led-sources");
	if (chip->num_leds <= 0) {
		ret = -ENODEV;
		goto err;
	}

	ret = fwnode_property_read_u32_array(child, "led-sources",
					     chip->led_sources, chip->num_leds);
	if (ret) {
		dev_err(dev, "led-sources property missing\n");
		goto err;
	}

	fwnode_property_read_string(child, "linux,default-trigger",
				    &chip->led_dev.default_trigger);

	chip->lmu_data.regmap = chip->regmap;
	chip->lmu_data.max_brightness = MAX_BRIGHTNESS_11BIT;
	chip->lmu_data.msb_brightness_reg = LM36274_REG_BRT_MSB;
	chip->lmu_data.lsb_brightness_reg = LM36274_REG_BRT_LSB;

	chip->led_dev.max_brightness = MAX_BRIGHTNESS_11BIT;
	chip->led_dev.brightness_set_blocking = lm36274_brightness_set;

	return 0;
err:
	fwnode_handle_put(child);
	return ret;
}

static int lm36274_probe(struct platform_device *pdev)
{
	struct ti_lmu *lmu = dev_get_drvdata(pdev->dev.parent);
	struct led_init_data init_data = {};
	struct lm36274 *chip;
	int ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->pdev = pdev;
	chip->dev = lmu->dev;
	chip->regmap = lmu->regmap;
	platform_set_drvdata(pdev, chip);

	ret = lm36274_parse_dt(chip, &init_data);
	if (ret) {
		dev_err(chip->dev, "Failed to parse DT node\n");
		return ret;
	}

	ret = lm36274_init(chip);
	if (ret) {
		dev_err(chip->dev, "Failed to init the device\n");
		return ret;
	}

	ret = led_classdev_register_ext(chip->dev, &chip->led_dev, &init_data);
	if (ret)
		dev_err(chip->dev, "Failed to register LED for node %pfw\n",
			init_data.fwnode);

	fwnode_handle_put(init_data.fwnode);

	return ret;
}

static int lm36274_remove(struct platform_device *pdev)
{
	struct lm36274 *chip = platform_get_drvdata(pdev);

	led_classdev_unregister(&chip->led_dev);

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
