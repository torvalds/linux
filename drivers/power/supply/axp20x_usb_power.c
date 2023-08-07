// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AXP20x PMIC USB power supply status driver
 *
 * Copyright (C) 2015 Hans de Goede <hdegoede@redhat.com>
 * Copyright (C) 2014 Bruno Prémont <bonbons@linux-vserver.org>
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/devm-helpers.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/axp20x.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/iio/consumer.h>
#include <linux/workqueue.h>

#define DRVNAME "axp20x-usb-power-supply"

#define AXP192_USB_OTG_STATUS		0x04

#define AXP20X_PWR_STATUS_VBUS_PRESENT	BIT(5)
#define AXP20X_PWR_STATUS_VBUS_USED	BIT(4)

#define AXP20X_USB_STATUS_VBUS_VALID	BIT(2)

#define AXP20X_VBUS_VHOLD_uV(b)		(4000000 + (((b) >> 3) & 7) * 100000)
#define AXP20X_VBUS_VHOLD_MASK		GENMASK(5, 3)
#define AXP20X_VBUS_VHOLD_OFFSET	3

#define AXP20X_ADC_EN1_VBUS_CURR	BIT(2)
#define AXP20X_ADC_EN1_VBUS_VOLT	BIT(3)

/*
 * Note do not raise the debounce time, we must report Vusb high within
 * 100ms otherwise we get Vbus errors in musb.
 */
#define DEBOUNCE_TIME			msecs_to_jiffies(50)

struct axp_data {
	const struct power_supply_desc	*power_desc;
	const char * const		*irq_names;
	unsigned int			num_irq_names;
	const int			*curr_lim_table;
	struct reg_field		curr_lim_fld;
	struct reg_field		vbus_valid_bit;
	struct reg_field		vbus_mon_bit;
	struct reg_field		usb_bc_en_bit;
	struct reg_field		vbus_disable_bit;
	bool				vbus_needs_polling: 1;
};

struct axp20x_usb_power {
	struct regmap *regmap;
	struct regmap_field *curr_lim_fld;
	struct regmap_field *vbus_valid_bit;
	struct regmap_field *vbus_mon_bit;
	struct regmap_field *usb_bc_en_bit;
	struct regmap_field *vbus_disable_bit;
	struct power_supply *supply;
	const struct axp_data *axp_data;
	struct iio_channel *vbus_v;
	struct iio_channel *vbus_i;
	struct delayed_work vbus_detect;
	unsigned int old_status;
	unsigned int online;
	unsigned int num_irqs;
	unsigned int irqs[];
};

static bool axp20x_usb_vbus_needs_polling(struct axp20x_usb_power *power)
{
	/*
	 * Polling is only necessary while VBUS is offline. While online, a
	 * present->absent transition implies an online->offline transition
	 * and will trigger the VBUS_REMOVAL IRQ.
	 */
	if (power->axp_data->vbus_needs_polling && !power->online)
		return true;

	return false;
}

static irqreturn_t axp20x_usb_power_irq(int irq, void *devid)
{
	struct axp20x_usb_power *power = devid;

	power_supply_changed(power->supply);

	mod_delayed_work(system_power_efficient_wq, &power->vbus_detect, DEBOUNCE_TIME);

	return IRQ_HANDLED;
}

static void axp20x_usb_power_poll_vbus(struct work_struct *work)
{
	struct axp20x_usb_power *power =
		container_of(work, struct axp20x_usb_power, vbus_detect.work);
	unsigned int val;
	int ret;

	ret = regmap_read(power->regmap, AXP20X_PWR_INPUT_STATUS, &val);
	if (ret)
		goto out;

	val &= (AXP20X_PWR_STATUS_VBUS_PRESENT | AXP20X_PWR_STATUS_VBUS_USED);
	if (val != power->old_status)
		power_supply_changed(power->supply);

	power->old_status = val;
	power->online = val & AXP20X_PWR_STATUS_VBUS_USED;

out:
	if (axp20x_usb_vbus_needs_polling(power))
		mod_delayed_work(system_power_efficient_wq, &power->vbus_detect, DEBOUNCE_TIME);
}

static int axp20x_usb_power_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct axp20x_usb_power *power = power_supply_get_drvdata(psy);
	unsigned int input, v;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		ret = regmap_read(power->regmap, AXP20X_VBUS_IPSOUT_MGMT, &v);
		if (ret)
			return ret;

		val->intval = AXP20X_VBUS_VHOLD_uV(v);
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (IS_ENABLED(CONFIG_AXP20X_ADC)) {
			ret = iio_read_channel_processed(power->vbus_v,
							 &val->intval);
			if (ret)
				return ret;

			/*
			 * IIO framework gives mV but Power Supply framework
			 * gives uV.
			 */
			val->intval *= 1000;
			return 0;
		}

		ret = axp20x_read_variable_width(power->regmap,
						 AXP20X_VBUS_V_ADC_H, 12);
		if (ret < 0)
			return ret;

		val->intval = ret * 1700; /* 1 step = 1.7 mV */
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		ret = regmap_field_read(power->curr_lim_fld, &v);
		if (ret)
			return ret;

		val->intval = power->axp_data->curr_lim_table[v];
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (IS_ENABLED(CONFIG_AXP20X_ADC)) {
			ret = iio_read_channel_processed(power->vbus_i,
							 &val->intval);
			if (ret)
				return ret;

			/*
			 * IIO framework gives mA but Power Supply framework
			 * gives uA.
			 */
			val->intval *= 1000;
			return 0;
		}

		ret = axp20x_read_variable_width(power->regmap,
						 AXP20X_VBUS_I_ADC_H, 12);
		if (ret < 0)
			return ret;

		val->intval = ret * 375; /* 1 step = 0.375 mA */
		return 0;
	default:
		break;
	}

	/* All the properties below need the input-status reg value */
	ret = regmap_read(power->regmap, AXP20X_PWR_INPUT_STATUS, &input);
	if (ret)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		if (!(input & AXP20X_PWR_STATUS_VBUS_PRESENT)) {
			val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
			break;
		}

		val->intval = POWER_SUPPLY_HEALTH_GOOD;

		if (power->vbus_valid_bit) {
			ret = regmap_field_read(power->vbus_valid_bit, &v);
			if (ret)
				return ret;

			if (v == 0)
				val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		}

		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = !!(input & AXP20X_PWR_STATUS_VBUS_PRESENT);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = !!(input & AXP20X_PWR_STATUS_VBUS_USED);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int axp20x_usb_power_set_voltage_min(struct axp20x_usb_power *power,
					    int intval)
{
	int val;

	switch (intval) {
	case 4000000:
	case 4100000:
	case 4200000:
	case 4300000:
	case 4400000:
	case 4500000:
	case 4600000:
	case 4700000:
		val = (intval - 4000000) / 100000;
		return regmap_update_bits(power->regmap,
					  AXP20X_VBUS_IPSOUT_MGMT,
					  AXP20X_VBUS_VHOLD_MASK,
					  val << AXP20X_VBUS_VHOLD_OFFSET);
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int axp20x_usb_power_set_current_max(struct axp20x_usb_power *power, int intval)
{
	const unsigned int max = GENMASK(power->axp_data->curr_lim_fld.msb,
					 power->axp_data->curr_lim_fld.lsb);

	if (intval == -1)
		return -EINVAL;

	for (unsigned int i = 0; i <= max; ++i)
		if (power->axp_data->curr_lim_table[i] == intval)
			return regmap_field_write(power->curr_lim_fld, i);

	return -EINVAL;
}

static int axp20x_usb_power_set_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 const union power_supply_propval *val)
{
	struct axp20x_usb_power *power = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (!power->vbus_disable_bit)
			return -EINVAL;

		return regmap_field_write(power->vbus_disable_bit, !val->intval);

	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		return axp20x_usb_power_set_voltage_min(power, val->intval);

	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return axp20x_usb_power_set_current_max(power, val->intval);

	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int axp20x_usb_power_prop_writeable(struct power_supply *psy,
					   enum power_supply_property psp)
{
	struct axp20x_usb_power *power = power_supply_get_drvdata(psy);

	/*
	 * The VBUS path select flag works differently on AXP288 and newer:
	 *  - On AXP20x and AXP22x, the flag enables VBUS (ignoring N_VBUSEN).
	 *  - On AXP288 and AXP8xx, the flag disables VBUS (ignoring N_VBUSEN).
	 * We only expose the control on variants where it can be used to force
	 * the VBUS input offline.
	 */
	if (psp == POWER_SUPPLY_PROP_ONLINE)
		return power->vbus_disable_bit != NULL;

	return psp == POWER_SUPPLY_PROP_VOLTAGE_MIN ||
	       psp == POWER_SUPPLY_PROP_CURRENT_MAX;
}

static enum power_supply_property axp20x_usb_power_properties[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

static enum power_supply_property axp22x_usb_power_properties[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static const struct power_supply_desc axp20x_usb_power_desc = {
	.name = "axp20x-usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = axp20x_usb_power_properties,
	.num_properties = ARRAY_SIZE(axp20x_usb_power_properties),
	.property_is_writeable = axp20x_usb_power_prop_writeable,
	.get_property = axp20x_usb_power_get_property,
	.set_property = axp20x_usb_power_set_property,
};

static const struct power_supply_desc axp22x_usb_power_desc = {
	.name = "axp20x-usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = axp22x_usb_power_properties,
	.num_properties = ARRAY_SIZE(axp22x_usb_power_properties),
	.property_is_writeable = axp20x_usb_power_prop_writeable,
	.get_property = axp20x_usb_power_get_property,
	.set_property = axp20x_usb_power_set_property,
};

static const char * const axp20x_irq_names[] = {
	"VBUS_PLUGIN",
	"VBUS_REMOVAL",
	"VBUS_VALID",
	"VBUS_NOT_VALID",
};

static const char * const axp22x_irq_names[] = {
	"VBUS_PLUGIN",
	"VBUS_REMOVAL",
};

static int axp192_usb_curr_lim_table[] = {
	-1,
	-1,
	500000,
	100000,
};

static int axp20x_usb_curr_lim_table[] = {
	900000,
	500000,
	100000,
	-1,
};

static int axp221_usb_curr_lim_table[] = {
	900000,
	500000,
	-1,
	-1,
};

static int axp813_usb_curr_lim_table[] = {
	900000,
	1500000,
	2000000,
	2500000,
};

static const struct axp_data axp192_data = {
	.power_desc	= &axp20x_usb_power_desc,
	.irq_names	= axp20x_irq_names,
	.num_irq_names	= ARRAY_SIZE(axp20x_irq_names),
	.curr_lim_table = axp192_usb_curr_lim_table,
	.curr_lim_fld   = REG_FIELD(AXP20X_VBUS_IPSOUT_MGMT, 0, 1),
	.vbus_valid_bit = REG_FIELD(AXP192_USB_OTG_STATUS, 2, 2),
	.vbus_mon_bit   = REG_FIELD(AXP20X_VBUS_MON, 3, 3),
};

static const struct axp_data axp202_data = {
	.power_desc	= &axp20x_usb_power_desc,
	.irq_names	= axp20x_irq_names,
	.num_irq_names	= ARRAY_SIZE(axp20x_irq_names),
	.curr_lim_table = axp20x_usb_curr_lim_table,
	.curr_lim_fld   = REG_FIELD(AXP20X_VBUS_IPSOUT_MGMT, 0, 1),
	.vbus_valid_bit = REG_FIELD(AXP20X_USB_OTG_STATUS, 2, 2),
	.vbus_mon_bit   = REG_FIELD(AXP20X_VBUS_MON, 3, 3),
};

static const struct axp_data axp221_data = {
	.power_desc	= &axp22x_usb_power_desc,
	.irq_names	= axp22x_irq_names,
	.num_irq_names	= ARRAY_SIZE(axp22x_irq_names),
	.curr_lim_table = axp221_usb_curr_lim_table,
	.curr_lim_fld   = REG_FIELD(AXP20X_VBUS_IPSOUT_MGMT, 0, 1),
	.vbus_needs_polling = true,
};

static const struct axp_data axp223_data = {
	.power_desc	= &axp22x_usb_power_desc,
	.irq_names	= axp22x_irq_names,
	.num_irq_names	= ARRAY_SIZE(axp22x_irq_names),
	.curr_lim_table = axp20x_usb_curr_lim_table,
	.curr_lim_fld   = REG_FIELD(AXP20X_VBUS_IPSOUT_MGMT, 0, 1),
	.vbus_needs_polling = true,
};

static const struct axp_data axp813_data = {
	.power_desc	= &axp22x_usb_power_desc,
	.irq_names	= axp22x_irq_names,
	.num_irq_names	= ARRAY_SIZE(axp22x_irq_names),
	.curr_lim_table = axp813_usb_curr_lim_table,
	.curr_lim_fld   = REG_FIELD(AXP20X_VBUS_IPSOUT_MGMT, 0, 1),
	.usb_bc_en_bit	= REG_FIELD(AXP288_BC_GLOBAL, 0, 0),
	.vbus_disable_bit = REG_FIELD(AXP20X_VBUS_IPSOUT_MGMT, 7, 7),
	.vbus_needs_polling = true,
};

#ifdef CONFIG_PM_SLEEP
static int axp20x_usb_power_suspend(struct device *dev)
{
	struct axp20x_usb_power *power = dev_get_drvdata(dev);
	int i = 0;

	/*
	 * Allow wake via VBUS_PLUGIN only.
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

static int axp20x_usb_power_resume(struct device *dev)
{
	struct axp20x_usb_power *power = dev_get_drvdata(dev);
	int i = 0;

	if (device_may_wakeup(&power->supply->dev))
		disable_irq_wake(power->irqs[i++]);
	while (i < power->num_irqs)
		enable_irq(power->irqs[i++]);

	mod_delayed_work(system_power_efficient_wq, &power->vbus_detect, DEBOUNCE_TIME);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(axp20x_usb_power_pm_ops, axp20x_usb_power_suspend,
						  axp20x_usb_power_resume);

static int configure_iio_channels(struct platform_device *pdev,
				  struct axp20x_usb_power *power)
{
	power->vbus_v = devm_iio_channel_get(&pdev->dev, "vbus_v");
	if (IS_ERR(power->vbus_v)) {
		if (PTR_ERR(power->vbus_v) == -ENODEV)
			return -EPROBE_DEFER;
		return PTR_ERR(power->vbus_v);
	}

	power->vbus_i = devm_iio_channel_get(&pdev->dev, "vbus_i");
	if (IS_ERR(power->vbus_i)) {
		if (PTR_ERR(power->vbus_i) == -ENODEV)
			return -EPROBE_DEFER;
		return PTR_ERR(power->vbus_i);
	}

	return 0;
}

static int configure_adc_registers(struct axp20x_usb_power *power)
{
	/* Enable vbus voltage and current measurement */
	return regmap_update_bits(power->regmap, AXP20X_ADC_EN1,
				  AXP20X_ADC_EN1_VBUS_CURR |
				  AXP20X_ADC_EN1_VBUS_VOLT,
				  AXP20X_ADC_EN1_VBUS_CURR |
				  AXP20X_ADC_EN1_VBUS_VOLT);
}

static int axp20x_regmap_field_alloc_optional(struct device *dev,
					      struct regmap *regmap,
					      struct reg_field fdesc,
					      struct regmap_field **fieldp)
{
	struct regmap_field *field;

	if (fdesc.reg == 0) {
		*fieldp = NULL;
		return 0;
	}

	field = devm_regmap_field_alloc(dev, regmap, fdesc);
	if (IS_ERR(field))
		return PTR_ERR(field);

	*fieldp = field;
	return 0;
}

static int axp20x_usb_power_probe(struct platform_device *pdev)
{
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	struct power_supply_config psy_cfg = {};
	struct axp20x_usb_power *power;
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

	platform_set_drvdata(pdev, power);

	power->axp_data = axp_data;
	power->regmap = axp20x->regmap;
	power->num_irqs = axp_data->num_irq_names;

	power->curr_lim_fld = devm_regmap_field_alloc(&pdev->dev, power->regmap,
						      axp_data->curr_lim_fld);
	if (IS_ERR(power->curr_lim_fld))
		return PTR_ERR(power->curr_lim_fld);

	ret = axp20x_regmap_field_alloc_optional(&pdev->dev, power->regmap,
						 axp_data->vbus_valid_bit,
						 &power->vbus_valid_bit);
	if (ret)
		return ret;

	ret = axp20x_regmap_field_alloc_optional(&pdev->dev, power->regmap,
						 axp_data->vbus_mon_bit,
						 &power->vbus_mon_bit);
	if (ret)
		return ret;

	ret = axp20x_regmap_field_alloc_optional(&pdev->dev, power->regmap,
						 axp_data->usb_bc_en_bit,
						 &power->usb_bc_en_bit);
	if (ret)
		return ret;

	ret = axp20x_regmap_field_alloc_optional(&pdev->dev, power->regmap,
						 axp_data->vbus_disable_bit,
						 &power->vbus_disable_bit);
	if (ret)
		return ret;

	ret = devm_delayed_work_autocancel(&pdev->dev, &power->vbus_detect,
					   axp20x_usb_power_poll_vbus);
	if (ret)
		return ret;

	if (power->vbus_mon_bit) {
		/* Enable vbus valid checking */
		ret = regmap_field_write(power->vbus_mon_bit, 1);
		if (ret)
			return ret;

		if (IS_ENABLED(CONFIG_AXP20X_ADC))
			ret = configure_iio_channels(pdev, power);
		else
			ret = configure_adc_registers(power);

		if (ret)
			return ret;
	}

	if (power->usb_bc_en_bit) {
		/* Enable USB Battery Charging specification detection */
		ret = regmap_field_write(power->usb_bc_en_bit, 1);
		if (ret)
			return ret;
	}

	psy_cfg.of_node = pdev->dev.of_node;
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
						   axp20x_usb_power_irq, 0,
						   DRVNAME, power);
		if (ret < 0) {
			dev_err(&pdev->dev, "Error requesting %s IRQ: %d\n",
				axp_data->irq_names[i], ret);
			return ret;
		}
	}

	if (axp20x_usb_vbus_needs_polling(power))
		queue_delayed_work(system_power_efficient_wq, &power->vbus_detect, 0);

	return 0;
}

static const struct of_device_id axp20x_usb_power_match[] = {
	{
		.compatible = "x-powers,axp192-usb-power-supply",
		.data = &axp192_data,
	}, {
		.compatible = "x-powers,axp202-usb-power-supply",
		.data = &axp202_data,
	}, {
		.compatible = "x-powers,axp221-usb-power-supply",
		.data = &axp221_data,
	}, {
		.compatible = "x-powers,axp223-usb-power-supply",
		.data = &axp223_data,
	}, {
		.compatible = "x-powers,axp813-usb-power-supply",
		.data = &axp813_data,
	}, { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, axp20x_usb_power_match);

static struct platform_driver axp20x_usb_power_driver = {
	.probe = axp20x_usb_power_probe,
	.driver = {
		.name		= DRVNAME,
		.of_match_table	= axp20x_usb_power_match,
		.pm		= &axp20x_usb_power_pm_ops,
	},
};

module_platform_driver(axp20x_usb_power_driver);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("AXP20x PMIC USB power supply status driver");
MODULE_LICENSE("GPL");
