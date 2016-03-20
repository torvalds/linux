/*
 * Battery driver for Maxim MAX8925
 *
 * Copyright (c) 2009-2010 Marvell International Ltd.
 *	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/mfd/max8925.h>

/* registers in GPM */
#define MAX8925_OUT5VEN			0x54
#define MAX8925_OUT3VEN			0x58
#define MAX8925_CHG_CNTL1		0x7c

/* bits definition */
#define MAX8925_CHG_STAT_VSYSLOW	(1 << 0)
#define MAX8925_CHG_STAT_MODE_MASK	(3 << 2)
#define MAX8925_CHG_STAT_EN_MASK	(1 << 4)
#define MAX8925_CHG_MBDET		(1 << 1)
#define MAX8925_CHG_AC_RANGE_MASK	(3 << 6)

/* registers in ADC */
#define MAX8925_ADC_RES_CNFG1		0x06
#define MAX8925_ADC_AVG_CNFG1		0x07
#define MAX8925_ADC_ACQ_CNFG1		0x08
#define MAX8925_ADC_ACQ_CNFG2		0x09
/* 2 bytes registers in below. MSB is 1st, LSB is 2nd. */
#define MAX8925_ADC_AUX2		0x62
#define MAX8925_ADC_VCHG		0x64
#define MAX8925_ADC_VBBATT		0x66
#define MAX8925_ADC_VMBATT		0x68
#define MAX8925_ADC_ISNS		0x6a
#define MAX8925_ADC_THM			0x6c
#define MAX8925_ADC_TDIE		0x6e
#define MAX8925_CMD_AUX2		0xc8
#define MAX8925_CMD_VCHG		0xd0
#define MAX8925_CMD_VBBATT		0xd8
#define MAX8925_CMD_VMBATT		0xe0
#define MAX8925_CMD_ISNS		0xe8
#define MAX8925_CMD_THM			0xf0
#define MAX8925_CMD_TDIE		0xf8

enum {
	MEASURE_AUX2,
	MEASURE_VCHG,
	MEASURE_VBBATT,
	MEASURE_VMBATT,
	MEASURE_ISNS,
	MEASURE_THM,
	MEASURE_TDIE,
	MEASURE_MAX,
};

struct max8925_power_info {
	struct max8925_chip	*chip;
	struct i2c_client	*gpm;
	struct i2c_client	*adc;

	struct power_supply	*ac;
	struct power_supply	*usb;
	struct power_supply	*battery;
	int			irq_base;
	unsigned		ac_online:1;
	unsigned		usb_online:1;
	unsigned		bat_online:1;
	unsigned		chg_mode:2;
	unsigned		batt_detect:1;	/* detecing MB by ID pin */
	unsigned		topoff_threshold:2;
	unsigned		fast_charge:3;
	unsigned		no_temp_support:1;
	unsigned		no_insert_detect:1;

	int (*set_charger) (int);
};

static int __set_charger(struct max8925_power_info *info, int enable)
{
	struct max8925_chip *chip = info->chip;
	if (enable) {
		/* enable charger in platform */
		if (info->set_charger)
			info->set_charger(1);
		/* enable charger */
		max8925_set_bits(info->gpm, MAX8925_CHG_CNTL1, 1 << 7, 0);
	} else {
		/* disable charge */
		max8925_set_bits(info->gpm, MAX8925_CHG_CNTL1, 1 << 7, 1 << 7);
		if (info->set_charger)
			info->set_charger(0);
	}
	dev_dbg(chip->dev, "%s\n", (enable) ? "Enable charger"
		: "Disable charger");
	return 0;
}

static irqreturn_t max8925_charger_handler(int irq, void *data)
{
	struct max8925_power_info *info = (struct max8925_power_info *)data;
	struct max8925_chip *chip = info->chip;

	switch (irq - chip->irq_base) {
	case MAX8925_IRQ_VCHG_DC_R:
		info->ac_online = 1;
		__set_charger(info, 1);
		dev_dbg(chip->dev, "Adapter inserted\n");
		break;
	case MAX8925_IRQ_VCHG_DC_F:
		info->ac_online = 0;
		__set_charger(info, 0);
		dev_dbg(chip->dev, "Adapter removed\n");
		break;
	case MAX8925_IRQ_VCHG_THM_OK_F:
		/* Battery is not ready yet */
		dev_dbg(chip->dev, "Battery temperature is out of range\n");
	case MAX8925_IRQ_VCHG_DC_OVP:
		dev_dbg(chip->dev, "Error detection\n");
		__set_charger(info, 0);
		break;
	case MAX8925_IRQ_VCHG_THM_OK_R:
		/* Battery is ready now */
		dev_dbg(chip->dev, "Battery temperature is in range\n");
		break;
	case MAX8925_IRQ_VCHG_SYSLOW_R:
		/* VSYS is low */
		dev_info(chip->dev, "Sys power is too low\n");
		break;
	case MAX8925_IRQ_VCHG_SYSLOW_F:
		dev_dbg(chip->dev, "Sys power is above low threshold\n");
		break;
	case MAX8925_IRQ_VCHG_DONE:
		__set_charger(info, 0);
		dev_dbg(chip->dev, "Charging is done\n");
		break;
	case MAX8925_IRQ_VCHG_TOPOFF:
		dev_dbg(chip->dev, "Charging in top-off mode\n");
		break;
	case MAX8925_IRQ_VCHG_TMR_FAULT:
		__set_charger(info, 0);
		dev_dbg(chip->dev, "Safe timer is expired\n");
		break;
	case MAX8925_IRQ_VCHG_RST:
		__set_charger(info, 0);
		dev_dbg(chip->dev, "Charger is reset\n");
		break;
	}
	return IRQ_HANDLED;
}

static int start_measure(struct max8925_power_info *info, int type)
{
	unsigned char buf[2] = {0, 0};
	int meas_cmd;
	int meas_reg = 0, ret;

	switch (type) {
	case MEASURE_VCHG:
		meas_cmd = MAX8925_CMD_VCHG;
		meas_reg = MAX8925_ADC_VCHG;
		break;
	case MEASURE_VBBATT:
		meas_cmd = MAX8925_CMD_VBBATT;
		meas_reg = MAX8925_ADC_VBBATT;
		break;
	case MEASURE_VMBATT:
		meas_cmd = MAX8925_CMD_VMBATT;
		meas_reg = MAX8925_ADC_VMBATT;
		break;
	case MEASURE_ISNS:
		meas_cmd = MAX8925_CMD_ISNS;
		meas_reg = MAX8925_ADC_ISNS;
		break;
	default:
		return -EINVAL;
	}

	max8925_reg_write(info->adc, meas_cmd, 0);
	max8925_bulk_read(info->adc, meas_reg, 2, buf);
	ret = ((buf[0]<<8) | buf[1]) >> 4;

	return ret;
}

static int max8925_ac_get_prop(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	struct max8925_power_info *info = dev_get_drvdata(psy->dev.parent);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = info->ac_online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (info->ac_online) {
			ret = start_measure(info, MEASURE_VCHG);
			if (ret >= 0) {
				val->intval = ret * 2000;	/* unit is uV */
				goto out;
			}
		}
		ret = -ENODATA;
		break;
	default:
		ret = -ENODEV;
		break;
	}
out:
	return ret;
}

static enum power_supply_property max8925_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static int max8925_usb_get_prop(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct max8925_power_info *info = dev_get_drvdata(psy->dev.parent);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = info->usb_online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (info->usb_online) {
			ret = start_measure(info, MEASURE_VCHG);
			if (ret >= 0) {
				val->intval = ret * 2000;	/* unit is uV */
				goto out;
			}
		}
		ret = -ENODATA;
		break;
	default:
		ret = -ENODEV;
		break;
	}
out:
	return ret;
}

static enum power_supply_property max8925_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static int max8925_bat_get_prop(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct max8925_power_info *info = dev_get_drvdata(psy->dev.parent);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = info->bat_online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (info->bat_online) {
			ret = start_measure(info, MEASURE_VMBATT);
			if (ret >= 0) {
				val->intval = ret * 2000;	/* unit is uV */
				ret = 0;
				break;
			}
		}
		ret = -ENODATA;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (info->bat_online) {
			ret = start_measure(info, MEASURE_ISNS);
			if (ret >= 0) {
				/* assume r_sns is 0.02 */
				ret = ((ret * 6250) - 3125) /* uA */;
				val->intval = 0;
				if (ret > 0)
					val->intval = ret; /* unit is mA */
				ret = 0;
				break;
			}
		}
		ret = -ENODATA;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		if (!info->bat_online) {
			ret = -ENODATA;
			break;
		}
		ret = max8925_reg_read(info->gpm, MAX8925_CHG_STATUS);
		ret = (ret & MAX8925_CHG_STAT_MODE_MASK) >> 2;
		switch (ret) {
		case 1:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
			break;
		case 0:
		case 2:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case 3:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
			break;
		}
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (!info->bat_online) {
			ret = -ENODATA;
			break;
		}
		ret = max8925_reg_read(info->gpm, MAX8925_CHG_STATUS);
		if (info->usb_online || info->ac_online) {
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			if (ret & MAX8925_CHG_STAT_EN_MASK)
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
		} else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		ret = 0;
		break;
	default:
		ret = -ENODEV;
		break;
	}
	return ret;
}

static enum power_supply_property max8925_battery_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_STATUS,
};

static const struct power_supply_desc ac_desc = {
	.name		= "max8925-ac",
	.type		= POWER_SUPPLY_TYPE_MAINS,
	.properties	= max8925_ac_props,
	.num_properties	= ARRAY_SIZE(max8925_ac_props),
	.get_property	= max8925_ac_get_prop,
};

static const struct power_supply_desc usb_desc = {
	.name		= "max8925-usb",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= max8925_usb_props,
	.num_properties	= ARRAY_SIZE(max8925_usb_props),
	.get_property	= max8925_usb_get_prop,
};

static const struct power_supply_desc battery_desc = {
	.name		= "max8925-battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= max8925_battery_props,
	.num_properties	= ARRAY_SIZE(max8925_battery_props),
	.get_property	= max8925_bat_get_prop,
};

#define REQUEST_IRQ(_irq, _name)					\
do {									\
	ret = request_threaded_irq(chip->irq_base + _irq, NULL,		\
				    max8925_charger_handler,		\
				    IRQF_ONESHOT, _name, info);		\
	if (ret)							\
		dev_err(chip->dev, "Failed to request IRQ #%d: %d\n",	\
			_irq, ret);					\
} while (0)

static int max8925_init_charger(struct max8925_chip *chip,
					  struct max8925_power_info *info)
{
	int ret;

	REQUEST_IRQ(MAX8925_IRQ_VCHG_DC_OVP, "ac-ovp");
	if (!info->no_insert_detect) {
		REQUEST_IRQ(MAX8925_IRQ_VCHG_DC_F, "ac-remove");
		REQUEST_IRQ(MAX8925_IRQ_VCHG_DC_R, "ac-insert");
	}
	if (!info->no_temp_support) {
		REQUEST_IRQ(MAX8925_IRQ_VCHG_THM_OK_R, "batt-temp-in-range");
		REQUEST_IRQ(MAX8925_IRQ_VCHG_THM_OK_F, "batt-temp-out-range");
	}
	REQUEST_IRQ(MAX8925_IRQ_VCHG_SYSLOW_F, "vsys-high");
	REQUEST_IRQ(MAX8925_IRQ_VCHG_SYSLOW_R, "vsys-low");
	REQUEST_IRQ(MAX8925_IRQ_VCHG_RST, "charger-reset");
	REQUEST_IRQ(MAX8925_IRQ_VCHG_DONE, "charger-done");
	REQUEST_IRQ(MAX8925_IRQ_VCHG_TOPOFF, "charger-topoff");
	REQUEST_IRQ(MAX8925_IRQ_VCHG_TMR_FAULT, "charger-timer-expire");

	info->usb_online = 0;
	info->bat_online = 0;

	/* check for power - can miss interrupt at boot time */
	if (start_measure(info, MEASURE_VCHG) * 2000 > 500000)
		info->ac_online = 1;
	else
		info->ac_online = 0;

	ret = max8925_reg_read(info->gpm, MAX8925_CHG_STATUS);
	if (ret >= 0) {
		/*
		 * If battery detection is enabled, ID pin of battery is
		 * connected to MBDET pin of MAX8925. It could be used to
		 * detect battery presence.
		 * Otherwise, we have to assume that battery is always on.
		 */
		if (info->batt_detect)
			info->bat_online = (ret & MAX8925_CHG_MBDET) ? 0 : 1;
		else
			info->bat_online = 1;
		if (ret & MAX8925_CHG_AC_RANGE_MASK)
			info->ac_online = 1;
		else
			info->ac_online = 0;
	}
	/* disable charge */
	max8925_set_bits(info->gpm, MAX8925_CHG_CNTL1, 1 << 7, 1 << 7);
	/* set charging current in charge topoff mode */
	max8925_set_bits(info->gpm, MAX8925_CHG_CNTL1, 3 << 5,
			 info->topoff_threshold << 5);
	/* set charing current in fast charge mode */
	max8925_set_bits(info->gpm, MAX8925_CHG_CNTL1, 7, info->fast_charge);

	return 0;
}

static int max8925_deinit_charger(struct max8925_power_info *info)
{
	struct max8925_chip *chip = info->chip;
	int irq;

	irq = chip->irq_base + MAX8925_IRQ_VCHG_DC_OVP;
	for (; irq <= chip->irq_base + MAX8925_IRQ_VCHG_TMR_FAULT; irq++)
		free_irq(irq, info);

	return 0;
}

#ifdef CONFIG_OF
static struct max8925_power_pdata *
max8925_power_dt_init(struct platform_device *pdev)
{
	struct device_node *nproot = pdev->dev.parent->of_node;
	struct device_node *np;
	int batt_detect;
	int topoff_threshold;
	int fast_charge;
	int no_temp_support;
	int no_insert_detect;
	struct max8925_power_pdata *pdata;

	if (!nproot)
		return pdev->dev.platform_data;

	np = of_get_child_by_name(nproot, "charger");
	if (!np) {
		dev_err(&pdev->dev, "failed to find charger node\n");
		return NULL;
	}

	pdata = devm_kzalloc(&pdev->dev,
			sizeof(struct max8925_power_pdata),
			GFP_KERNEL);
	if (!pdata)
		goto ret;

	of_property_read_u32(np, "topoff-threshold", &topoff_threshold);
	of_property_read_u32(np, "batt-detect", &batt_detect);
	of_property_read_u32(np, "fast-charge", &fast_charge);
	of_property_read_u32(np, "no-insert-detect", &no_insert_detect);
	of_property_read_u32(np, "no-temp-support", &no_temp_support);

	pdata->batt_detect = batt_detect;
	pdata->fast_charge = fast_charge;
	pdata->topoff_threshold = topoff_threshold;
	pdata->no_insert_detect = no_insert_detect;
	pdata->no_temp_support = no_temp_support;

ret:
	of_node_put(np);
	return pdata;
}
#else
static struct max8925_power_pdata *
max8925_power_dt_init(struct platform_device *pdev)
{
	return pdev->dev.platform_data;
}
#endif

static int max8925_power_probe(struct platform_device *pdev)
{
	struct max8925_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct power_supply_config psy_cfg = {}; /* Only for ac and usb */
	struct max8925_power_pdata *pdata = NULL;
	struct max8925_power_info *info;
	int ret;

	pdata = max8925_power_dt_init(pdev);
	if (!pdata) {
		dev_err(&pdev->dev, "platform data isn't assigned to "
			"power supply\n");
		return -EINVAL;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(struct max8925_power_info),
				GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->chip = chip;
	info->gpm = chip->i2c;
	info->adc = chip->adc;
	platform_set_drvdata(pdev, info);

	psy_cfg.supplied_to = pdata->supplied_to;
	psy_cfg.num_supplicants = pdata->num_supplicants;

	info->ac = power_supply_register(&pdev->dev, &ac_desc, &psy_cfg);
	if (IS_ERR(info->ac)) {
		ret = PTR_ERR(info->ac);
		goto out;
	}
	info->ac->dev.parent = &pdev->dev;

	info->usb = power_supply_register(&pdev->dev, &usb_desc, &psy_cfg);
	if (IS_ERR(info->usb)) {
		ret = PTR_ERR(info->usb);
		goto out_usb;
	}
	info->usb->dev.parent = &pdev->dev;

	info->battery = power_supply_register(&pdev->dev, &battery_desc, NULL);
	if (IS_ERR(info->battery)) {
		ret = PTR_ERR(info->battery);
		goto out_battery;
	}
	info->battery->dev.parent = &pdev->dev;

	info->batt_detect = pdata->batt_detect;
	info->topoff_threshold = pdata->topoff_threshold;
	info->fast_charge = pdata->fast_charge;
	info->set_charger = pdata->set_charger;
	info->no_temp_support = pdata->no_temp_support;
	info->no_insert_detect = pdata->no_insert_detect;

	max8925_init_charger(chip, info);
	return 0;
out_battery:
	power_supply_unregister(info->battery);
out_usb:
	power_supply_unregister(info->ac);
out:
	return ret;
}

static int max8925_power_remove(struct platform_device *pdev)
{
	struct max8925_power_info *info = platform_get_drvdata(pdev);

	if (info) {
		power_supply_unregister(info->ac);
		power_supply_unregister(info->usb);
		power_supply_unregister(info->battery);
		max8925_deinit_charger(info);
	}
	return 0;
}

static struct platform_driver max8925_power_driver = {
	.probe	= max8925_power_probe,
	.remove	= max8925_power_remove,
	.driver	= {
		.name	= "max8925-power",
	},
};

module_platform_driver(max8925_power_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Power supply driver for MAX8925");
MODULE_ALIAS("platform:max8925-power");
