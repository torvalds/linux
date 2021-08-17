// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Spreadtrum Communications Inc.

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/usb/phy.h>
#include <linux/regmap.h>
#include <linux/notifier.h>
#include <linux/of.h>

/* PMIC global registers definition */
#define SC2731_CHARGE_STATUS		0xedc
#define SC2731_CHARGE_FULL		BIT(4)
#define SC2731_MODULE_EN1		0xc0c
#define SC2731_CHARGE_EN		BIT(5)

/* SC2731 switch charger registers definition */
#define SC2731_CHG_CFG0			0x0
#define SC2731_CHG_CFG1			0x4
#define SC2731_CHG_CFG2			0x8
#define SC2731_CHG_CFG3			0xc
#define SC2731_CHG_CFG4			0x10
#define SC2731_CHG_CFG5			0x28

/* SC2731_CHG_CFG0 register definition */
#define SC2731_PRECHG_RNG_SHIFT		11
#define SC2731_PRECHG_RNG_MASK		GENMASK(12, 11)

#define SC2731_TERMINATION_VOL_MASK	GENMASK(2, 1)
#define SC2731_TERMINATION_VOL_SHIFT	1
#define SC2731_TERMINATION_VOL_CAL_MASK	GENMASK(8, 3)
#define SC2731_TERMINATION_VOL_CAL_SHIFT	3
#define SC2731_TERMINATION_CUR_MASK	GENMASK(2, 0)

#define SC2731_CC_EN			BIT(13)
#define SC2731_CHARGER_PD		BIT(0)

/* SC2731_CHG_CFG1 register definition */
#define SC2731_CUR_MASK			GENMASK(5, 0)

/* SC2731_CHG_CFG5 register definition */
#define SC2731_CUR_LIMIT_SHIFT		8
#define SC2731_CUR_LIMIT_MASK		GENMASK(9, 8)

/* Default current definition (unit is mA) */
#define SC2731_CURRENT_LIMIT_100	100
#define SC2731_CURRENT_LIMIT_500	500
#define SC2731_CURRENT_LIMIT_900	900
#define SC2731_CURRENT_LIMIT_2000	2000
#define SC2731_CURRENT_PRECHG		450
#define SC2731_CURRENT_STEP		50

struct sc2731_charger_info {
	struct device *dev;
	struct regmap *regmap;
	struct usb_phy *usb_phy;
	struct notifier_block usb_notify;
	struct power_supply *psy_usb;
	struct work_struct work;
	struct mutex lock;
	bool charging;
	u32 base;
	u32 limit;
};

static void sc2731_charger_stop_charge(struct sc2731_charger_info *info)
{
	regmap_update_bits(info->regmap, info->base + SC2731_CHG_CFG0,
			   SC2731_CC_EN, 0);

	regmap_update_bits(info->regmap, info->base + SC2731_CHG_CFG0,
			   SC2731_CHARGER_PD, SC2731_CHARGER_PD);
}

static int sc2731_charger_start_charge(struct sc2731_charger_info *info)
{
	int ret;

	/* Enable charger constant current mode */
	ret = regmap_update_bits(info->regmap, info->base + SC2731_CHG_CFG0,
				 SC2731_CC_EN, SC2731_CC_EN);
	if (ret)
		return ret;

	/* Start charging */
	return regmap_update_bits(info->regmap, info->base + SC2731_CHG_CFG0,
				  SC2731_CHARGER_PD, 0);
}

static int sc2731_charger_set_current_limit(struct sc2731_charger_info *info,
					    u32 limit)
{
	u32 val;

	if (limit <= SC2731_CURRENT_LIMIT_100)
		val = 0;
	else if (limit <= SC2731_CURRENT_LIMIT_500)
		val = 3;
	else if (limit <= SC2731_CURRENT_LIMIT_900)
		val = 2;
	else
		val = 1;

	return regmap_update_bits(info->regmap, info->base + SC2731_CHG_CFG5,
				  SC2731_CUR_LIMIT_MASK,
				  val << SC2731_CUR_LIMIT_SHIFT);
}

static int sc2731_charger_set_current(struct sc2731_charger_info *info, u32 cur)
{
	u32 val;
	int ret;

	if (cur > SC2731_CURRENT_LIMIT_2000)
		cur = SC2731_CURRENT_LIMIT_2000;
	else if (cur < SC2731_CURRENT_PRECHG)
		cur = SC2731_CURRENT_PRECHG;

	/* Calculate the step value, each step is 50 mA */
	val = (cur - SC2731_CURRENT_PRECHG) / SC2731_CURRENT_STEP;

	/* Set pre-charge current as 450 mA */
	ret = regmap_update_bits(info->regmap, info->base + SC2731_CHG_CFG0,
				 SC2731_PRECHG_RNG_MASK,
				 0x3 << SC2731_PRECHG_RNG_SHIFT);
	if (ret)
		return ret;

	return regmap_update_bits(info->regmap, info->base + SC2731_CHG_CFG1,
				  SC2731_CUR_MASK, val);
}

static int sc2731_charger_get_status(struct sc2731_charger_info *info)
{
	u32 val;
	int ret;

	ret = regmap_read(info->regmap, SC2731_CHARGE_STATUS, &val);
	if (ret)
		return ret;

	if (val & SC2731_CHARGE_FULL)
		return POWER_SUPPLY_STATUS_FULL;

	return POWER_SUPPLY_STATUS_CHARGING;
}

static int sc2731_charger_get_current(struct sc2731_charger_info *info,
				      u32 *cur)
{
	int ret;
	u32 val;

	ret = regmap_read(info->regmap, info->base + SC2731_CHG_CFG1, &val);
	if (ret)
		return ret;

	val &= SC2731_CUR_MASK;
	*cur = val * SC2731_CURRENT_STEP + SC2731_CURRENT_PRECHG;

	return 0;
}

static int sc2731_charger_get_current_limit(struct sc2731_charger_info *info,
					    u32 *cur)
{
	int ret;
	u32 val;

	ret = regmap_read(info->regmap, info->base + SC2731_CHG_CFG5, &val);
	if (ret)
		return ret;

	val = (val & SC2731_CUR_LIMIT_MASK) >> SC2731_CUR_LIMIT_SHIFT;

	switch (val) {
	case 0:
		*cur = SC2731_CURRENT_LIMIT_100;
		break;

	case 1:
		*cur = SC2731_CURRENT_LIMIT_2000;
		break;

	case 2:
		*cur = SC2731_CURRENT_LIMIT_900;
		break;

	case 3:
		*cur = SC2731_CURRENT_LIMIT_500;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int
sc2731_charger_usb_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct sc2731_charger_info *info = power_supply_get_drvdata(psy);
	int ret;

	mutex_lock(&info->lock);

	if (!info->charging) {
		mutex_unlock(&info->lock);
		return -ENODEV;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = sc2731_charger_set_current(info, val->intval / 1000);
		if (ret < 0)
			dev_err(info->dev, "set charge current failed\n");
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = sc2731_charger_set_current_limit(info,
						       val->intval / 1000);
		if (ret < 0)
			dev_err(info->dev, "set input current limit failed\n");
		break;

	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int sc2731_charger_usb_get_property(struct power_supply *psy,
					   enum power_supply_property psp,
					   union power_supply_propval *val)
{
	struct sc2731_charger_info *info = power_supply_get_drvdata(psy);
	int ret = 0;
	u32 cur;

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (info->charging)
			val->intval = sc2731_charger_get_status(info);
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = sc2731_charger_get_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur * 1000;
		}
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = sc2731_charger_get_current_limit(info, &cur);
			if (ret)
				goto out;

			val->intval = cur * 1000;
		}
		break;

	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int sc2731_charger_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}

static enum power_supply_property sc2731_usb_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
};

static const struct power_supply_desc sc2731_charger_desc = {
	.name			= "sc2731_charger",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= sc2731_usb_props,
	.num_properties		= ARRAY_SIZE(sc2731_usb_props),
	.get_property		= sc2731_charger_usb_get_property,
	.set_property		= sc2731_charger_usb_set_property,
	.property_is_writeable	= sc2731_charger_property_is_writeable,
};

static void sc2731_charger_work(struct work_struct *data)
{
	struct sc2731_charger_info *info =
		container_of(data, struct sc2731_charger_info, work);
	int ret;

	mutex_lock(&info->lock);

	if (info->limit > 0 && !info->charging) {
		/* set current limitation and start to charge */
		ret = sc2731_charger_set_current_limit(info, info->limit);
		if (ret)
			goto out;

		ret = sc2731_charger_set_current(info, info->limit);
		if (ret)
			goto out;

		ret = sc2731_charger_start_charge(info);
		if (ret)
			goto out;

		info->charging = true;
	} else if (!info->limit && info->charging) {
		/* Stop charging */
		info->charging = false;
		sc2731_charger_stop_charge(info);
	}

out:
	mutex_unlock(&info->lock);
}

static int sc2731_charger_usb_change(struct notifier_block *nb,
				     unsigned long limit, void *data)
{
	struct sc2731_charger_info *info =
		container_of(nb, struct sc2731_charger_info, usb_notify);

	info->limit = limit;

	schedule_work(&info->work);

	return NOTIFY_OK;
}

static int sc2731_charger_hw_init(struct sc2731_charger_info *info)
{
	struct power_supply_battery_info bat_info = { };
	u32 term_currrent, term_voltage, cur_val, vol_val;
	int ret;

	/* Enable charger module */
	ret = regmap_update_bits(info->regmap, SC2731_MODULE_EN1,
				 SC2731_CHARGE_EN, SC2731_CHARGE_EN);
	if (ret)
		return ret;

	ret = power_supply_get_battery_info(info->psy_usb, &bat_info);
	if (ret) {
		dev_warn(info->dev, "no battery information is supplied\n");

		/*
		 * If no battery information is supplied, we should set
		 * default charge termination current to 120 mA, and default
		 * charge termination voltage to 4.35V.
		 */
		cur_val = 0x2;
		vol_val = 0x1;
	} else {
		term_currrent = bat_info.charge_term_current_ua / 1000;

		if (term_currrent <= 90)
			cur_val = 0;
		else if (term_currrent >= 265)
			cur_val = 0x7;
		else
			cur_val = ((term_currrent - 90) / 25) + 1;

		term_voltage = bat_info.constant_charge_voltage_max_uv / 1000;

		if (term_voltage > 4500)
			term_voltage = 4500;

		if (term_voltage > 4200)
			vol_val = (term_voltage - 4200) / 100;
		else
			vol_val = 0;

		power_supply_put_battery_info(info->psy_usb, &bat_info);
	}

	/* Set charge termination current */
	ret = regmap_update_bits(info->regmap, info->base + SC2731_CHG_CFG2,
				 SC2731_TERMINATION_CUR_MASK, cur_val);
	if (ret)
		goto error;

	/* Set charge termination voltage */
	ret = regmap_update_bits(info->regmap, info->base + SC2731_CHG_CFG0,
				 SC2731_TERMINATION_VOL_MASK |
				 SC2731_TERMINATION_VOL_CAL_MASK,
				 (vol_val << SC2731_TERMINATION_VOL_SHIFT) |
				 (0x6 << SC2731_TERMINATION_VOL_CAL_SHIFT));
	if (ret)
		goto error;

	return 0;

error:
	regmap_update_bits(info->regmap, SC2731_MODULE_EN1, SC2731_CHARGE_EN, 0);
	return ret;
}

static void sc2731_charger_detect_status(struct sc2731_charger_info *info)
{
	unsigned int min, max;

	/*
	 * If the USB charger status has been USB_CHARGER_PRESENT before
	 * registering the notifier, we should start to charge with getting
	 * the charge current.
	 */
	if (info->usb_phy->chg_state != USB_CHARGER_PRESENT)
		return;

	usb_phy_get_charger_current(info->usb_phy, &min, &max);
	info->limit = min;

	schedule_work(&info->work);
}

static int sc2731_charger_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sc2731_charger_info *info;
	struct power_supply_config charger_cfg = { };
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->lock);
	info->dev = &pdev->dev;
	INIT_WORK(&info->work, sc2731_charger_work);

	info->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!info->regmap) {
		dev_err(&pdev->dev, "failed to get charger regmap\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(np, "reg", &info->base);
	if (ret) {
		dev_err(&pdev->dev, "failed to get register address\n");
		return -ENODEV;
	}

	charger_cfg.drv_data = info;
	charger_cfg.of_node = np;
	info->psy_usb = devm_power_supply_register(&pdev->dev,
						   &sc2731_charger_desc,
						   &charger_cfg);
	if (IS_ERR(info->psy_usb)) {
		dev_err(&pdev->dev, "failed to register power supply\n");
		return PTR_ERR(info->psy_usb);
	}

	ret = sc2731_charger_hw_init(info);
	if (ret)
		return ret;

	info->usb_phy = devm_usb_get_phy_by_phandle(&pdev->dev, "phys", 0);
	if (IS_ERR(info->usb_phy)) {
		dev_err(&pdev->dev, "failed to find USB phy\n");
		return PTR_ERR(info->usb_phy);
	}

	info->usb_notify.notifier_call = sc2731_charger_usb_change;
	ret = usb_register_notifier(info->usb_phy, &info->usb_notify);
	if (ret) {
		dev_err(&pdev->dev, "failed to register notifier: %d\n", ret);
		return ret;
	}

	sc2731_charger_detect_status(info);

	return 0;
}

static int sc2731_charger_remove(struct platform_device *pdev)
{
	struct sc2731_charger_info *info = platform_get_drvdata(pdev);

	usb_unregister_notifier(info->usb_phy, &info->usb_notify);

	return 0;
}

static const struct of_device_id sc2731_charger_of_match[] = {
	{ .compatible = "sprd,sc2731-charger", },
	{ }
};
MODULE_DEVICE_TABLE(of, sc2731_charger_of_match);

static struct platform_driver sc2731_charger_driver = {
	.driver = {
		.name = "sc2731-charger",
		.of_match_table = sc2731_charger_of_match,
	},
	.probe = sc2731_charger_probe,
	.remove = sc2731_charger_remove,
};

module_platform_driver(sc2731_charger_driver);

MODULE_DESCRIPTION("Spreadtrum SC2731 Charger Driver");
MODULE_LICENSE("GPL v2");
