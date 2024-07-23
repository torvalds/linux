// SPDX-License-Identifier: GPL-2.0
/*
 * Battery driver for the Ingenic JZ47xx SoCs
 * Copyright (c) 2019 Artur Rojek <contact@artur-rojek.eu>
 *
 * based on drivers/power/supply/jz4740-battery.c
 */

#include <linux/iio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>

struct ingenic_battery {
	struct device *dev;
	struct iio_channel *channel;
	struct power_supply_desc desc;
	struct power_supply *battery;
	struct power_supply_battery_info *info;
};

static int ingenic_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct ingenic_battery *bat = power_supply_get_drvdata(psy);
	struct power_supply_battery_info *info = bat->info;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		ret = iio_read_channel_processed_scale(bat->channel,
						       &val->intval,
						       1000);
		if (val->intval < info->voltage_min_design_uv)
			val->intval = POWER_SUPPLY_HEALTH_DEAD;
		else if (val->intval > info->voltage_max_design_uv)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		return ret;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = iio_read_channel_processed_scale(bat->channel,
						       &val->intval,
						       1000);
		return ret;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = info->voltage_min_design_uv;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = info->voltage_max_design_uv;
		return 0;
	default:
		return -EINVAL;
	}
}

/* Set the most appropriate IIO channel voltage reference scale
 * based on the battery's max voltage.
 */
static int ingenic_battery_set_scale(struct ingenic_battery *bat)
{
	const int *scale_raw;
	int scale_len, scale_type, best_idx = -1, best_mV, max_raw, i, ret;
	u64 max_mV;

	ret = iio_read_max_channel_raw(bat->channel, &max_raw);
	if (ret) {
		dev_err(bat->dev, "Unable to read max raw channel value\n");
		return ret;
	}

	ret = iio_read_avail_channel_attribute(bat->channel, &scale_raw,
					       &scale_type, &scale_len,
					       IIO_CHAN_INFO_SCALE);
	if (ret < 0) {
		dev_err(bat->dev, "Unable to read channel avail scale\n");
		return ret;
	}
	if (ret != IIO_AVAIL_LIST || scale_type != IIO_VAL_FRACTIONAL_LOG2)
		return -EINVAL;

	max_mV = bat->info->voltage_max_design_uv / 1000;

	for (i = 0; i < scale_len; i += 2) {
		u64 scale_mV = (max_raw * scale_raw[i]) >> scale_raw[i + 1];

		if (scale_mV < max_mV)
			continue;

		if (best_idx >= 0 && scale_mV > best_mV)
			continue;

		best_mV = scale_mV;
		best_idx = i;
	}

	if (best_idx < 0) {
		dev_err(bat->dev, "Unable to find matching voltage scale\n");
		return -EINVAL;
	}

	/* Only set scale if there is more than one (fractional) entry */
	if (scale_len > 2) {
		ret = iio_write_channel_attribute(bat->channel,
						  scale_raw[best_idx],
						  scale_raw[best_idx + 1],
						  IIO_CHAN_INFO_SCALE);
		if (ret)
			return ret;
	}

	return 0;
}

static enum power_supply_property ingenic_battery_properties[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
};

static int ingenic_battery_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ingenic_battery *bat;
	struct power_supply_config psy_cfg = {};
	struct power_supply_desc *desc;
	int ret;

	bat = devm_kzalloc(dev, sizeof(*bat), GFP_KERNEL);
	if (!bat)
		return -ENOMEM;

	bat->dev = dev;
	bat->channel = devm_iio_channel_get(dev, "battery");
	if (IS_ERR(bat->channel))
		return PTR_ERR(bat->channel);

	desc = &bat->desc;
	desc->name = "jz-battery";
	desc->type = POWER_SUPPLY_TYPE_BATTERY;
	desc->properties = ingenic_battery_properties;
	desc->num_properties = ARRAY_SIZE(ingenic_battery_properties);
	desc->get_property = ingenic_battery_get_property;
	psy_cfg.drv_data = bat;
	psy_cfg.of_node = dev->of_node;

	bat->battery = devm_power_supply_register(dev, desc, &psy_cfg);
	if (IS_ERR(bat->battery))
		return dev_err_probe(dev, PTR_ERR(bat->battery),
				     "Unable to register battery\n");

	ret = power_supply_get_battery_info(bat->battery, &bat->info);
	if (ret) {
		dev_err(dev, "Unable to get battery info: %d\n", ret);
		return ret;
	}
	if (bat->info->voltage_min_design_uv < 0) {
		dev_err(dev, "Unable to get voltage min design\n");
		return bat->info->voltage_min_design_uv;
	}
	if (bat->info->voltage_max_design_uv < 0) {
		dev_err(dev, "Unable to get voltage max design\n");
		return bat->info->voltage_max_design_uv;
	}

	return ingenic_battery_set_scale(bat);
}

#ifdef CONFIG_OF
static const struct of_device_id ingenic_battery_of_match[] = {
	{ .compatible = "ingenic,jz4740-battery", },
	{ },
};
MODULE_DEVICE_TABLE(of, ingenic_battery_of_match);
#endif

static struct platform_driver ingenic_battery_driver = {
	.driver = {
		.name = "ingenic-battery",
		.of_match_table = of_match_ptr(ingenic_battery_of_match),
	},
	.probe = ingenic_battery_probe,
};
module_platform_driver(ingenic_battery_driver);

MODULE_DESCRIPTION("Battery driver for Ingenic JZ47xx SoCs");
MODULE_AUTHOR("Artur Rojek <contact@artur-rojek.eu>");
MODULE_LICENSE("GPL");
