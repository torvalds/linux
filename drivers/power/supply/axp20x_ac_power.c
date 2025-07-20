// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AXP20X and AXP22X PMICs' ACIN power supply driver
 *
 * Copyright (C) 2016 Free Electrons
 *	Quentin Schulz <quentin.schulz@free-electrons.com>
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/axp20x.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/iio/consumer.h>

#define AXP20X_PWR_STATUS_ACIN_PRESENT	BIT(7)
#define AXP20X_PWR_STATUS_ACIN_AVAIL	BIT(6)

#define AXP813_ACIN_PATH_SEL		BIT(7)
#define AXP813_ACIN_PATH_SEL_TO_BIT(x)	(!!(x) << 7)

#define AXP813_VHOLD_MASK		GENMASK(5, 3)
#define AXP813_VHOLD_UV_TO_BIT(x)	((((x) / 100000) - 40) << 3)
#define AXP813_VHOLD_REG_TO_UV(x)	\
	(((((x) & AXP813_VHOLD_MASK) >> 3) + 40) * 100000)

#define AXP813_CURR_LIMIT_MASK		GENMASK(2, 0)
#define AXP813_CURR_LIMIT_UA_TO_BIT(x)	(((x) / 500000) - 3)
#define AXP813_CURR_LIMIT_REG_TO_UA(x)	\
	((((x) & AXP813_CURR_LIMIT_MASK) + 3) * 500000)

#define DRVNAME "axp20x-ac-power-supply"

struct axp20x_ac_power {
	struct regmap *regmap;
	struct power_supply *supply;
	struct iio_channel *acin_v;
	struct iio_channel *acin_i;
	bool has_acin_path_sel;
	unsigned int num_irqs;
	unsigned int irqs[] __counted_by(num_irqs);
};

static irqreturn_t axp20x_ac_power_irq(int irq, void *devid)
{
	struct axp20x_ac_power *power = devid;

	power_supply_changed(power->supply);

	return IRQ_HANDLED;
}

static int axp20x_ac_power_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct axp20x_ac_power *power = power_supply_get_drvdata(psy);
	int ret, reg;

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		ret = regmap_read(power->regmap, AXP20X_PWR_INPUT_STATUS, &reg);
		if (ret)
			return ret;

		if (reg & AXP20X_PWR_STATUS_ACIN_PRESENT) {
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
			return 0;
		}

		val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
		return 0;

	case POWER_SUPPLY_PROP_PRESENT:
		ret = regmap_read(power->regmap, AXP20X_PWR_INPUT_STATUS, &reg);
		if (ret)
			return ret;

		val->intval = !!(reg & AXP20X_PWR_STATUS_ACIN_PRESENT);
		return 0;

	case POWER_SUPPLY_PROP_ONLINE:
		ret = regmap_read(power->regmap, AXP20X_PWR_INPUT_STATUS, &reg);
		if (ret)
			return ret;

		val->intval = !!(reg & AXP20X_PWR_STATUS_ACIN_AVAIL);

		/* ACIN_PATH_SEL disables ACIN even if ACIN_AVAIL is set. */
		if (val->intval && power->has_acin_path_sel) {
			ret = regmap_read(power->regmap, AXP813_ACIN_PATH_CTRL,
					  &reg);
			if (ret)
				return ret;

			val->intval = !!(reg & AXP813_ACIN_PATH_SEL);
		}

		return 0;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = iio_read_channel_processed(power->acin_v, &val->intval);
		if (ret)
			return ret;

		/* IIO framework gives mV but Power Supply framework gives uV */
		val->intval *= 1000;

		return 0;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = iio_read_channel_processed(power->acin_i, &val->intval);
		if (ret)
			return ret;

		/* IIO framework gives mA but Power Supply framework gives uA */
		val->intval *= 1000;

		return 0;

	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		ret = regmap_read(power->regmap, AXP813_ACIN_PATH_CTRL, &reg);
		if (ret)
			return ret;

		val->intval = AXP813_VHOLD_REG_TO_UV(reg);

		return 0;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = regmap_read(power->regmap, AXP813_ACIN_PATH_CTRL, &reg);
		if (ret)
			return ret;

		val->intval = AXP813_CURR_LIMIT_REG_TO_UA(reg);
		/* AXP813 datasheet defines values 11x as 4000mA */
		if (val->intval > 4000000)
			val->intval = 4000000;

		return 0;

	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int axp813_ac_power_set_property(struct power_supply *psy,
					enum power_supply_property psp,
					const union power_supply_propval *val)
{
	struct axp20x_ac_power *power = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		return regmap_update_bits(power->regmap, AXP813_ACIN_PATH_CTRL,
					  AXP813_ACIN_PATH_SEL,
					  AXP813_ACIN_PATH_SEL_TO_BIT(val->intval));

	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		if (val->intval < 4000000 || val->intval > 4700000)
			return -EINVAL;

		return regmap_update_bits(power->regmap, AXP813_ACIN_PATH_CTRL,
					  AXP813_VHOLD_MASK,
					  AXP813_VHOLD_UV_TO_BIT(val->intval));

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (val->intval < 1500000 || val->intval > 4000000)
			return -EINVAL;

		return regmap_update_bits(power->regmap, AXP813_ACIN_PATH_CTRL,
					  AXP813_CURR_LIMIT_MASK,
					  AXP813_CURR_LIMIT_UA_TO_BIT(val->intval));

	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int axp813_ac_power_prop_writeable(struct power_supply *psy,
					  enum power_supply_property psp)
{
	return psp == POWER_SUPPLY_PROP_ONLINE ||
	       psp == POWER_SUPPLY_PROP_VOLTAGE_MIN ||
	       psp == POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT;
}

static enum power_supply_property axp20x_ac_power_properties[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

static enum power_supply_property axp22x_ac_power_properties[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property axp813_ac_power_properties[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
};

static const struct power_supply_desc axp20x_ac_power_desc = {
	.name = "axp20x-ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = axp20x_ac_power_properties,
	.num_properties = ARRAY_SIZE(axp20x_ac_power_properties),
	.get_property = axp20x_ac_power_get_property,
};

static const struct power_supply_desc axp22x_ac_power_desc = {
	.name = "axp22x-ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = axp22x_ac_power_properties,
	.num_properties = ARRAY_SIZE(axp22x_ac_power_properties),
	.get_property = axp20x_ac_power_get_property,
};

static const struct power_supply_desc axp813_ac_power_desc = {
	.name = "axp813-ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = axp813_ac_power_properties,
	.num_properties = ARRAY_SIZE(axp813_ac_power_properties),
	.property_is_writeable = axp813_ac_power_prop_writeable,
	.get_property = axp20x_ac_power_get_property,
	.set_property = axp813_ac_power_set_property,
};

static const char * const axp20x_irq_names[] = {
	"ACIN_PLUGIN",
	"ACIN_REMOVAL",
};

struct axp_data {
	const struct power_supply_desc	*power_desc;
	const char * const		*irq_names;
	unsigned int			num_irq_names;
	bool				acin_adc;
	bool				acin_path_sel;
};

static const struct axp_data axp20x_data = {
	.power_desc	= &axp20x_ac_power_desc,
	.irq_names	= axp20x_irq_names,
	.num_irq_names	= ARRAY_SIZE(axp20x_irq_names),
	.acin_adc	= true,
	.acin_path_sel	= false,
};

static const struct axp_data axp22x_data = {
	.power_desc	= &axp22x_ac_power_desc,
	.irq_names	= axp20x_irq_names,
	.num_irq_names	= ARRAY_SIZE(axp20x_irq_names),
	.acin_adc	= false,
	.acin_path_sel	= false,
};

static const struct axp_data axp813_data = {
	.power_desc	= &axp813_ac_power_desc,
	.irq_names	= axp20x_irq_names,
	.num_irq_names	= ARRAY_SIZE(axp20x_irq_names),
	.acin_adc	= false,
	.acin_path_sel	= true,
};

#ifdef CONFIG_PM_SLEEP
static int axp20x_ac_power_suspend(struct device *dev)
{
	struct axp20x_ac_power *power = dev_get_drvdata(dev);
	int i = 0;

	/*
	 * Allow wake via ACIN_PLUGIN only.
	 *
	 * As nested threaded IRQs are not automatically disabled during
	 * suspend, we must explicitly disable the remainder of the IRQs.
	 */
	if (device_may_wakeup(&power->supply->dev))
		enable_irq_wake(power->irqs[i++]);
	while (i < power->num_irqs)
		disable_irq(power->irqs[i++]);

	return 0;
}

static int axp20x_ac_power_resume(struct device *dev)
{
	struct axp20x_ac_power *power = dev_get_drvdata(dev);
	int i = 0;

	if (device_may_wakeup(&power->supply->dev))
		disable_irq_wake(power->irqs[i++]);
	while (i < power->num_irqs)
		enable_irq(power->irqs[i++]);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(axp20x_ac_power_pm_ops, axp20x_ac_power_suspend,
						 axp20x_ac_power_resume);

static int axp20x_ac_power_probe(struct platform_device *pdev)
{
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	struct power_supply_config psy_cfg = {};
	struct axp20x_ac_power *power;
	const struct axp_data *axp_data;
	int i, irq, ret;

	if (!of_device_is_available(pdev->dev.of_node))
		return -ENODEV;

	if (!axp20x) {
		dev_err(&pdev->dev, "Parent drvdata not set\n");
		return -EINVAL;
	}

	axp_data = of_device_get_match_data(&pdev->dev);

	power = devm_kzalloc(&pdev->dev,
			     struct_size(power, irqs, axp_data->num_irq_names),
			     GFP_KERNEL);
	if (!power)
		return -ENOMEM;

	if (axp_data->acin_adc) {
		power->acin_v = devm_iio_channel_get(&pdev->dev, "acin_v");
		if (IS_ERR(power->acin_v)) {
			if (PTR_ERR(power->acin_v) == -ENODEV)
				return -EPROBE_DEFER;
			return PTR_ERR(power->acin_v);
		}

		power->acin_i = devm_iio_channel_get(&pdev->dev, "acin_i");
		if (IS_ERR(power->acin_i)) {
			if (PTR_ERR(power->acin_i) == -ENODEV)
				return -EPROBE_DEFER;
			return PTR_ERR(power->acin_i);
		}
	}

	power->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	power->has_acin_path_sel = axp_data->acin_path_sel;
	power->num_irqs = axp_data->num_irq_names;

	platform_set_drvdata(pdev, power);

	psy_cfg.fwnode = dev_fwnode(&pdev->dev);
	psy_cfg.drv_data = power;

	power->supply = devm_power_supply_register(&pdev->dev,
						   axp_data->power_desc,
						   &psy_cfg);
	if (IS_ERR(power->supply))
		return PTR_ERR(power->supply);

	/* Request irqs after registering, as irqs may trigger immediately */
	for (i = 0; i < axp_data->num_irq_names; i++) {
		irq = platform_get_irq_byname(pdev, axp_data->irq_names[i]);
		if (irq < 0)
			return irq;

		power->irqs[i] = regmap_irq_get_virq(axp20x->regmap_irqc, irq);
		ret = devm_request_any_context_irq(&pdev->dev, power->irqs[i],
						   axp20x_ac_power_irq, 0,
						   DRVNAME, power);
		if (ret < 0) {
			dev_err(&pdev->dev, "Error requesting %s IRQ: %d\n",
				axp_data->irq_names[i], ret);
			return ret;
		}
	}

	return 0;
}

static const struct of_device_id axp20x_ac_power_match[] = {
	{
		.compatible = "x-powers,axp202-ac-power-supply",
		.data = &axp20x_data,
	}, {
		.compatible = "x-powers,axp221-ac-power-supply",
		.data = &axp22x_data,
	}, {
		.compatible = "x-powers,axp813-ac-power-supply",
		.data = &axp813_data,
	}, { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, axp20x_ac_power_match);

static struct platform_driver axp20x_ac_power_driver = {
	.probe = axp20x_ac_power_probe,
	.driver = {
		.name		= DRVNAME,
		.of_match_table	= axp20x_ac_power_match,
		.pm		= &axp20x_ac_power_pm_ops,
	},
};

module_platform_driver(axp20x_ac_power_driver);

MODULE_AUTHOR("Quentin Schulz <quentin.schulz@free-electrons.com>");
MODULE_DESCRIPTION("AXP20X and AXP22X PMICs' AC power supply driver");
MODULE_LICENSE("GPL");
