// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MP2629 battery charger driver
 *
 * Copyright 2020 Monolithic Power Systems, Inc
 *
 * Author: Saravanan Sekar <sravanhome@gmail.com>
 */

#include <linux/bits.h>
#include <linux/iio/consumer.h>
#include <linux/iio/types.h>
#include <linux/interrupt.h>
#include <linux/mfd/mp2629.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

#define MP2629_REG_INPUT_ILIM		0x00
#define MP2629_REG_INPUT_VLIM		0x01
#define MP2629_REG_CHARGE_CTRL		0x04
#define MP2629_REG_CHARGE_ILIM		0x05
#define MP2629_REG_PRECHARGE		0x06
#define MP2629_REG_TERM_CURRENT		0x06
#define MP2629_REG_CHARGE_VLIM		0x07
#define MP2629_REG_TIMER_CTRL		0x08
#define MP2629_REG_IMPEDANCE_COMP	0x09
#define MP2629_REG_INTERRUPT		0x0b
#define MP2629_REG_STATUS		0x0c
#define MP2629_REG_FAULT		0x0d

#define MP2629_MASK_INPUT_TYPE		GENMASK(7, 5)
#define MP2629_MASK_CHARGE_TYPE		GENMASK(4, 3)
#define MP2629_MASK_CHARGE_CTRL		GENMASK(5, 4)
#define MP2629_MASK_WDOG_CTRL		GENMASK(5, 4)
#define MP2629_MASK_IMPEDANCE		GENMASK(7, 4)

#define MP2629_INPUTSOURCE_CHANGE	GENMASK(7, 5)
#define MP2629_CHARGING_CHANGE		GENMASK(4, 3)
#define MP2629_FAULT_BATTERY		BIT(3)
#define MP2629_FAULT_THERMAL		BIT(4)
#define MP2629_FAULT_INPUT		BIT(5)
#define MP2629_FAULT_OTG		BIT(6)

#define MP2629_MAX_BATT_CAPACITY	100

#define MP2629_PROPS(_idx, _min, _max, _step)		\
	[_idx] = {					\
		.min	= _min,				\
		.max	= _max,				\
		.step	= _step,			\
}

enum mp2629_source_type {
	MP2629_SOURCE_TYPE_NO_INPUT,
	MP2629_SOURCE_TYPE_NON_STD,
	MP2629_SOURCE_TYPE_SDP,
	MP2629_SOURCE_TYPE_CDP,
	MP2629_SOURCE_TYPE_DCP,
	MP2629_SOURCE_TYPE_OTG = 7,
};

enum mp2629_field {
	INPUT_ILIM,
	INPUT_VLIM,
	CHARGE_ILIM,
	CHARGE_VLIM,
	PRECHARGE,
	TERM_CURRENT,
	MP2629_MAX_FIELD
};

struct mp2629_charger {
	struct device *dev;
	int status;
	int fault;

	struct regmap *regmap;
	struct regmap_field *regmap_fields[MP2629_MAX_FIELD];
	struct mutex lock;
	struct power_supply *usb;
	struct power_supply *battery;
	struct iio_channel *iiochan[MP2629_ADC_CHAN_END];
};

struct mp2629_prop {
	int reg;
	int mask;
	int min;
	int max;
	int step;
	int shift;
};

static enum power_supply_usb_type mp2629_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_UNKNOWN
};

static enum power_supply_property mp2629_charger_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
};

static enum power_supply_property mp2629_charger_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
};

static struct mp2629_prop props[] = {
	MP2629_PROPS(INPUT_ILIM, 100000, 3250000, 50000),
	MP2629_PROPS(INPUT_VLIM, 3800000, 5300000, 100000),
	MP2629_PROPS(CHARGE_ILIM, 320000, 4520000, 40000),
	MP2629_PROPS(CHARGE_VLIM, 3400000, 4670000, 10000),
	MP2629_PROPS(PRECHARGE, 120000, 720000, 40000),
	MP2629_PROPS(TERM_CURRENT, 80000, 680000, 40000),
};

static const struct reg_field mp2629_reg_fields[] = {
	[INPUT_ILIM]	= REG_FIELD(MP2629_REG_INPUT_ILIM, 0, 5),
	[INPUT_VLIM]	= REG_FIELD(MP2629_REG_INPUT_VLIM, 0, 3),
	[CHARGE_ILIM]	= REG_FIELD(MP2629_REG_CHARGE_ILIM, 0, 6),
	[CHARGE_VLIM]	= REG_FIELD(MP2629_REG_CHARGE_VLIM, 1, 7),
	[PRECHARGE]	= REG_FIELD(MP2629_REG_PRECHARGE, 4, 7),
	[TERM_CURRENT]	= REG_FIELD(MP2629_REG_TERM_CURRENT, 0, 3),
};

static char *adc_chan_name[] = {
	"mp2629-batt-volt",
	"mp2629-system-volt",
	"mp2629-input-volt",
	"mp2629-batt-current",
	"mp2629-input-current",
};

static int mp2629_read_adc(struct mp2629_charger *charger,
			   enum mp2629_adc_chan ch,
			   union power_supply_propval *val)
{
	int ret;
	int chval;

	ret = iio_read_channel_processed(charger->iiochan[ch], &chval);
	if (ret)
		return ret;

	val->intval = chval * 1000;

	return 0;
}

static int mp2629_get_prop(struct mp2629_charger *charger,
			   enum mp2629_field fld,
			   union power_supply_propval *val)
{
	int ret;
	unsigned int rval;

	ret = regmap_field_read(charger->regmap_fields[fld], &rval);
	if (ret)
		return ret;

	val->intval = rval * props[fld].step + props[fld].min;

	return 0;
}

static int mp2629_set_prop(struct mp2629_charger *charger,
			   enum mp2629_field fld,
			   const union power_supply_propval *val)
{
	unsigned int rval;

	if (val->intval < props[fld].min || val->intval > props[fld].max)
		return -EINVAL;

	rval = (val->intval - props[fld].min) / props[fld].step;
	return regmap_field_write(charger->regmap_fields[fld], rval);
}

static int mp2629_get_battery_capacity(struct mp2629_charger *charger,
				       union power_supply_propval *val)
{
	union power_supply_propval vnow, vlim;
	int ret;

	ret = mp2629_read_adc(charger, MP2629_BATT_VOLT, &vnow);
	if (ret)
		return ret;

	ret = mp2629_get_prop(charger, CHARGE_VLIM, &vlim);
	if (ret)
		return ret;

	val->intval = (vnow.intval * 100) / vlim.intval;
	val->intval = min(val->intval, MP2629_MAX_BATT_CAPACITY);

	return 0;
}

static int mp2629_charger_battery_get_prop(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct mp2629_charger *charger = dev_get_drvdata(psy->dev.parent);
	unsigned int rval;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = mp2629_read_adc(charger, MP2629_BATT_VOLT, val);
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = mp2629_read_adc(charger, MP2629_BATT_CURRENT, val);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = 4520000;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = 4670000;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		ret = mp2629_get_battery_capacity(charger, val);
		break;

	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = mp2629_get_prop(charger, TERM_CURRENT, val);
		break;

	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		ret = mp2629_get_prop(charger, PRECHARGE, val);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = mp2629_get_prop(charger, CHARGE_VLIM, val);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = mp2629_get_prop(charger, CHARGE_ILIM, val);
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (!charger->fault)
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		if (MP2629_FAULT_BATTERY & charger->fault)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else if (MP2629_FAULT_THERMAL & charger->fault)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (MP2629_FAULT_INPUT & charger->fault)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		break;

	case POWER_SUPPLY_PROP_STATUS:
		ret = regmap_read(charger->regmap, MP2629_REG_STATUS, &rval);
		if (ret)
			break;

		rval = (rval & MP2629_MASK_CHARGE_TYPE) >> 3;
		switch (rval) {
		case 0x00:
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			break;
		case 0x01:
		case 0x10:
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case 0x11:
			val->intval = POWER_SUPPLY_STATUS_FULL;
		}
		break;

	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		ret = regmap_read(charger->regmap, MP2629_REG_STATUS, &rval);
		if (ret)
			break;

		rval = (rval & MP2629_MASK_CHARGE_TYPE) >> 3;
		switch (rval) {
		case 0x00:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
			break;
		case 0x01:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case 0x10:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_STANDARD;
			break;
		default:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		}
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static int mp2629_charger_battery_set_prop(struct power_supply *psy,
					enum power_supply_property psp,
					const union power_supply_propval *val)
{
	struct mp2629_charger *charger = dev_get_drvdata(psy->dev.parent);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return mp2629_set_prop(charger, TERM_CURRENT, val);

	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		return mp2629_set_prop(charger, PRECHARGE, val);

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return mp2629_set_prop(charger, CHARGE_VLIM, val);

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return mp2629_set_prop(charger, CHARGE_ILIM, val);

	default:
		return -EINVAL;
	}
}

static int mp2629_charger_usb_get_prop(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct mp2629_charger *charger = dev_get_drvdata(psy->dev.parent);
	unsigned int rval;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = regmap_read(charger->regmap, MP2629_REG_STATUS, &rval);
		if (ret)
			break;

		val->intval = !!(rval & MP2629_MASK_INPUT_TYPE);
		break;

	case POWER_SUPPLY_PROP_USB_TYPE:
		ret = regmap_read(charger->regmap, MP2629_REG_STATUS, &rval);
		if (ret)
			break;

		rval = (rval & MP2629_MASK_INPUT_TYPE) >> 5;
		switch (rval) {
		case MP2629_SOURCE_TYPE_SDP:
			val->intval = POWER_SUPPLY_USB_TYPE_SDP;
			break;
		case MP2629_SOURCE_TYPE_CDP:
			val->intval = POWER_SUPPLY_USB_TYPE_CDP;
			break;
		case MP2629_SOURCE_TYPE_DCP:
			val->intval = POWER_SUPPLY_USB_TYPE_DCP;
			break;
		case MP2629_SOURCE_TYPE_OTG:
			val->intval = POWER_SUPPLY_USB_TYPE_PD_DRP;
			break;
		default:
			val->intval = POWER_SUPPLY_USB_TYPE_UNKNOWN;
			break;
		}
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = mp2629_read_adc(charger, MP2629_INPUT_VOLT, val);
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = mp2629_read_adc(charger, MP2629_INPUT_CURRENT, val);
		break;

	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = mp2629_get_prop(charger, INPUT_VLIM, val);
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = mp2629_get_prop(charger, INPUT_ILIM, val);
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static int mp2629_charger_usb_set_prop(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct mp2629_charger *charger = dev_get_drvdata(psy->dev.parent);

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		return mp2629_set_prop(charger, INPUT_VLIM, val);

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return mp2629_set_prop(charger, INPUT_ILIM, val);

	default:
		return -EINVAL;
	}
}

static int mp2629_charger_battery_prop_writeable(struct power_supply *psy,
				     enum power_supply_property psp)
{
	return (psp == POWER_SUPPLY_PROP_PRECHARGE_CURRENT) ||
	       (psp == POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT) ||
	       (psp == POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT) ||
	       (psp == POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE);
}

static int mp2629_charger_usb_prop_writeable(struct power_supply *psy,
				     enum power_supply_property psp)
{
	return (psp == POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT) ||
	       (psp == POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
}

static irqreturn_t mp2629_irq_handler(int irq, void *dev_id)
{
	struct mp2629_charger *charger = dev_id;
	unsigned int rval;
	int ret;

	mutex_lock(&charger->lock);

	ret = regmap_read(charger->regmap, MP2629_REG_FAULT, &rval);
	if (ret)
		goto unlock;

	if (rval) {
		charger->fault = rval;
		if (MP2629_FAULT_BATTERY & rval)
			dev_err(charger->dev, "Battery fault OVP\n");
		else if (MP2629_FAULT_THERMAL & rval)
			dev_err(charger->dev, "Thermal shutdown fault\n");
		else if (MP2629_FAULT_INPUT & rval)
			dev_err(charger->dev, "no input or input OVP\n");
		else if (MP2629_FAULT_OTG & rval)
			dev_err(charger->dev, "VIN overloaded\n");

		goto unlock;
	}

	ret = regmap_read(charger->regmap, MP2629_REG_STATUS, &rval);
	if (ret)
		goto unlock;

	if (rval & MP2629_INPUTSOURCE_CHANGE)
		power_supply_changed(charger->usb);
	else if (rval & MP2629_CHARGING_CHANGE)
		power_supply_changed(charger->battery);

unlock:
	mutex_unlock(&charger->lock);

	return IRQ_HANDLED;
}

static const struct power_supply_desc mp2629_usb_desc = {
	.name		= "mp2629_usb",
	.type		= POWER_SUPPLY_TYPE_USB,
	.usb_types      = mp2629_usb_types,
	.num_usb_types  = ARRAY_SIZE(mp2629_usb_types),
	.properties	= mp2629_charger_usb_props,
	.num_properties	= ARRAY_SIZE(mp2629_charger_usb_props),
	.get_property	= mp2629_charger_usb_get_prop,
	.set_property	= mp2629_charger_usb_set_prop,
	.property_is_writeable = mp2629_charger_usb_prop_writeable,
};

static const struct power_supply_desc mp2629_battery_desc = {
	.name		= "mp2629_battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= mp2629_charger_bat_props,
	.num_properties	= ARRAY_SIZE(mp2629_charger_bat_props),
	.get_property	= mp2629_charger_battery_get_prop,
	.set_property	= mp2629_charger_battery_set_prop,
	.property_is_writeable = mp2629_charger_battery_prop_writeable,
};

static ssize_t batt_impedance_compensation_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct mp2629_charger *charger = dev_get_drvdata(dev->parent);
	unsigned int rval;
	int ret;

	ret = regmap_read(charger->regmap, MP2629_REG_IMPEDANCE_COMP, &rval);
	if (ret)
		return ret;

	rval = (rval >> 4) * 10;
	return sprintf(buf, "%d mohm\n", rval);
}

static ssize_t batt_impedance_compensation_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t count)
{
	struct mp2629_charger *charger = dev_get_drvdata(dev->parent);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val > 140)
		return -ERANGE;

	/* multiples of 10 mohm so round off */
	val = val / 10;
	ret = regmap_update_bits(charger->regmap, MP2629_REG_IMPEDANCE_COMP,
					MP2629_MASK_IMPEDANCE, val << 4);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(batt_impedance_compensation);

static struct attribute *mp2629_charger_sysfs_attrs[] = {
	&dev_attr_batt_impedance_compensation.attr,
	NULL
};
ATTRIBUTE_GROUPS(mp2629_charger_sysfs);

static void mp2629_charger_disable(void *data)
{
	struct mp2629_charger *charger = data;

	regmap_update_bits(charger->regmap, MP2629_REG_CHARGE_CTRL,
					MP2629_MASK_CHARGE_CTRL, 0);
}

static int mp2629_charger_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mp2629_data *ddata = dev_get_drvdata(dev->parent);
	struct mp2629_charger *charger;
	struct power_supply_config psy_cfg = {};
	int ret, i, irq;

	charger = devm_kzalloc(dev, sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	charger->regmap = ddata->regmap;
	charger->dev = dev;
	platform_set_drvdata(pdev, charger);

	irq = platform_get_irq(to_platform_device(dev->parent), 0);
	if (irq < 0)
		return irq;

	for (i = 0; i < MP2629_MAX_FIELD; i++) {
		charger->regmap_fields[i] = devm_regmap_field_alloc(dev,
					charger->regmap, mp2629_reg_fields[i]);
		if (IS_ERR(charger->regmap_fields[i])) {
			dev_err(dev, "regmap field alloc fail %d\n", i);
			return PTR_ERR(charger->regmap_fields[i]);
		}
	}

	for (i = 0; i < MP2629_ADC_CHAN_END; i++) {
		charger->iiochan[i] = devm_iio_channel_get(dev,
							adc_chan_name[i]);
		if (IS_ERR(charger->iiochan[i])) {
			dev_err(dev, "iio chan get %s err\n", adc_chan_name[i]);
			return PTR_ERR(charger->iiochan[i]);
		}
	}

	ret = devm_add_action_or_reset(dev, mp2629_charger_disable, charger);
	if (ret)
		return ret;

	charger->usb = devm_power_supply_register(dev, &mp2629_usb_desc, NULL);
	if (IS_ERR(charger->usb)) {
		dev_err(dev, "power supply register usb failed\n");
		return PTR_ERR(charger->usb);
	}

	psy_cfg.drv_data = charger;
	psy_cfg.attr_grp = mp2629_charger_sysfs_groups;
	charger->battery = devm_power_supply_register(dev,
					 &mp2629_battery_desc, &psy_cfg);
	if (IS_ERR(charger->battery)) {
		dev_err(dev, "power supply register battery failed\n");
		return PTR_ERR(charger->battery);
	}

	ret = regmap_update_bits(charger->regmap, MP2629_REG_CHARGE_CTRL,
					MP2629_MASK_CHARGE_CTRL, BIT(4));
	if (ret) {
		dev_err(dev, "enable charge fail: %d\n", ret);
		return ret;
	}

	regmap_update_bits(charger->regmap, MP2629_REG_TIMER_CTRL,
					MP2629_MASK_WDOG_CTRL, 0);

	mutex_init(&charger->lock);

	ret = devm_request_threaded_irq(dev, irq, NULL,	mp2629_irq_handler,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"mp2629-charger", charger);
	if (ret) {
		dev_err(dev, "failed to request gpio IRQ\n");
		return ret;
	}

	regmap_update_bits(charger->regmap, MP2629_REG_INTERRUPT,
				GENMASK(6, 5), BIT(6) | BIT(5));

	return 0;
}

static const struct of_device_id mp2629_charger_of_match[] = {
	{ .compatible = "mps,mp2629_charger"},
	{}
};
MODULE_DEVICE_TABLE(of, mp2629_charger_of_match);

static struct platform_driver mp2629_charger_driver = {
	.driver = {
		.name = "mp2629_charger",
		.of_match_table = mp2629_charger_of_match,
	},
	.probe		= mp2629_charger_probe,
};
module_platform_driver(mp2629_charger_driver);

MODULE_AUTHOR("Saravanan Sekar <sravanhome@gmail.com>");
MODULE_DESCRIPTION("MP2629 Charger driver");
MODULE_LICENSE("GPL");
