// SPDX-License-Identifier: GPL-2.0-only
/*
 * Battery charger driver for RT5033
 *
 * Copyright (C) 2014 Samsung Electronics, Co., Ltd.
 * Author: Beomho Seo <beomho.seo@samsung.com>
 */

#include <linux/devm-helpers.h>
#include <linux/extcon.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/mfd/rt5033-private.h>

struct rt5033_charger_data {
	unsigned int pre_uamp;
	unsigned int pre_uvolt;
	unsigned int const_uvolt;
	unsigned int eoc_uamp;
	unsigned int fast_uamp;
};

struct rt5033_charger {
	struct device			*dev;
	struct regmap			*regmap;
	struct power_supply		*psy;
	struct rt5033_charger_data	chg;
	struct extcon_dev		*edev;
	struct notifier_block		extcon_nb;
	struct work_struct		extcon_work;
	struct mutex			lock;
	bool online;
	bool otg;
	bool mivr_enabled;
	u8 cv_regval;
};

static int rt5033_get_charger_state(struct rt5033_charger *charger)
{
	struct regmap *regmap = charger->regmap;
	unsigned int reg_data;
	int state;

	if (!regmap)
		return POWER_SUPPLY_STATUS_UNKNOWN;

	regmap_read(regmap, RT5033_REG_CHG_STAT, &reg_data);

	switch (reg_data & RT5033_CHG_STAT_MASK) {
	case RT5033_CHG_STAT_DISCHARGING:
		state = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case RT5033_CHG_STAT_CHARGING:
		state = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case RT5033_CHG_STAT_FULL:
		state = POWER_SUPPLY_STATUS_FULL;
		break;
	case RT5033_CHG_STAT_NOT_CHARGING:
		state = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	default:
		state = POWER_SUPPLY_STATUS_UNKNOWN;
	}

	/* For OTG mode, RT5033 would still report "charging" */
	if (charger->otg)
		state = POWER_SUPPLY_STATUS_DISCHARGING;

	return state;
}

static int rt5033_get_charger_type(struct rt5033_charger *charger)
{
	struct regmap *regmap = charger->regmap;
	unsigned int reg_data;
	int state;

	regmap_read(regmap, RT5033_REG_CHG_STAT, &reg_data);

	switch (reg_data & RT5033_CHG_STAT_TYPE_MASK) {
	case RT5033_CHG_STAT_TYPE_FAST:
		state = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case RT5033_CHG_STAT_TYPE_PRE:
		state = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	default:
		state = POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	return state;
}

static int rt5033_get_charger_current_limit(struct rt5033_charger *charger)
{
	struct regmap *regmap = charger->regmap;
	unsigned int state, reg_data, data;

	regmap_read(regmap, RT5033_REG_CHG_CTRL5, &reg_data);

	state = (reg_data & RT5033_CHGCTRL5_ICHG_MASK)
		 >> RT5033_CHGCTRL5_ICHG_SHIFT;

	data = RT5033_CHARGER_FAST_CURRENT_MIN +
		RT5033_CHARGER_FAST_CURRENT_STEP_NUM * state;

	return data;
}

static int rt5033_get_charger_const_voltage(struct rt5033_charger *charger)
{
	struct regmap *regmap = charger->regmap;
	unsigned int state, reg_data, data;

	regmap_read(regmap, RT5033_REG_CHG_CTRL2, &reg_data);

	state = (reg_data & RT5033_CHGCTRL2_CV_MASK)
		 >> RT5033_CHGCTRL2_CV_SHIFT;

	data = RT5033_CHARGER_CONST_VOLTAGE_LIMIT_MIN +
		RT5033_CHARGER_CONST_VOLTAGE_STEP_NUM * state;

	return data;
}

static inline int rt5033_init_const_charge(struct rt5033_charger *charger)
{
	struct rt5033_charger_data *chg = &charger->chg;
	int ret;
	unsigned int val;
	u8 reg_data;

	/* Set constant voltage mode */
	if (chg->const_uvolt < RT5033_CHARGER_CONST_VOLTAGE_LIMIT_MIN ||
	    chg->const_uvolt > RT5033_CHARGER_CONST_VOLTAGE_LIMIT_MAX) {
		dev_err(charger->dev,
			"Value 'constant-charge-voltage-max-microvolt' out of range\n");
		return -EINVAL;
	}

	if (chg->const_uvolt == RT5033_CHARGER_CONST_VOLTAGE_LIMIT_MIN)
		reg_data = 0x00;
	else if (chg->const_uvolt == RT5033_CHARGER_CONST_VOLTAGE_LIMIT_MAX)
		reg_data = RT5033_CV_MAX_VOLTAGE;
	else {
		val = chg->const_uvolt;
		val -= RT5033_CHARGER_CONST_VOLTAGE_LIMIT_MIN;
		val /= RT5033_CHARGER_CONST_VOLTAGE_STEP_NUM;
		reg_data = val;
	}

	ret = regmap_update_bits(charger->regmap, RT5033_REG_CHG_CTRL2,
			RT5033_CHGCTRL2_CV_MASK,
			reg_data << RT5033_CHGCTRL2_CV_SHIFT);
	if (ret) {
		dev_err(charger->dev, "Failed regmap update\n");
		return -EINVAL;
	}

	/* Store that value for later usage */
	charger->cv_regval = reg_data;

	/* Set end of charge current */
	if (chg->eoc_uamp < RT5033_CHARGER_EOC_MIN ||
	    chg->eoc_uamp > RT5033_CHARGER_EOC_MAX) {
		dev_err(charger->dev,
			"Value 'charge-term-current-microamp' out of range\n");
		return -EINVAL;
	}

	if (chg->eoc_uamp == RT5033_CHARGER_EOC_MIN)
		reg_data = 0x01;
	else if (chg->eoc_uamp == RT5033_CHARGER_EOC_MAX)
		reg_data = 0x07;
	else {
		val = chg->eoc_uamp;
		if (val < RT5033_CHARGER_EOC_REF) {
			val -= RT5033_CHARGER_EOC_MIN;
			val /= RT5033_CHARGER_EOC_STEP_NUM1;
			reg_data = 0x01 + val;
		} else if (val > RT5033_CHARGER_EOC_REF) {
			val -= RT5033_CHARGER_EOC_REF;
			val /= RT5033_CHARGER_EOC_STEP_NUM2;
			reg_data = 0x04 + val;
		} else {
			reg_data = 0x04;
		}
	}

	ret = regmap_update_bits(charger->regmap, RT5033_REG_CHG_CTRL4,
			RT5033_CHGCTRL4_EOC_MASK, reg_data);
	if (ret) {
		dev_err(charger->dev, "Failed regmap update\n");
		return -EINVAL;
	}

	return 0;
}

static inline int rt5033_init_fast_charge(struct rt5033_charger *charger)
{
	struct rt5033_charger_data *chg = &charger->chg;
	int ret;
	unsigned int val;
	u8 reg_data;

	/* Set limit input current */
	ret = regmap_update_bits(charger->regmap, RT5033_REG_CHG_CTRL1,
			RT5033_CHGCTRL1_IAICR_MASK, RT5033_AICR_2000_MODE);
	if (ret) {
		dev_err(charger->dev, "Failed regmap update\n");
		return -EINVAL;
	}

	/* Set fast-charge mode charging current */
	if (chg->fast_uamp < RT5033_CHARGER_FAST_CURRENT_MIN ||
	    chg->fast_uamp > RT5033_CHARGER_FAST_CURRENT_MAX) {
		dev_err(charger->dev,
			"Value 'constant-charge-current-max-microamp' out of range\n");
		return -EINVAL;
	}

	if (chg->fast_uamp == RT5033_CHARGER_FAST_CURRENT_MIN)
		reg_data = 0x00;
	else if (chg->fast_uamp == RT5033_CHARGER_FAST_CURRENT_MAX)
		reg_data = RT5033_CHG_MAX_CURRENT;
	else {
		val = chg->fast_uamp;
		val -= RT5033_CHARGER_FAST_CURRENT_MIN;
		val /= RT5033_CHARGER_FAST_CURRENT_STEP_NUM;
		reg_data = val;
	}

	ret = regmap_update_bits(charger->regmap, RT5033_REG_CHG_CTRL5,
			RT5033_CHGCTRL5_ICHG_MASK,
			reg_data << RT5033_CHGCTRL5_ICHG_SHIFT);
	if (ret) {
		dev_err(charger->dev, "Failed regmap update\n");
		return -EINVAL;
	}

	return 0;
}

static inline int rt5033_init_pre_charge(struct rt5033_charger *charger)
{
	struct rt5033_charger_data *chg = &charger->chg;
	int ret;
	unsigned int val;
	u8 reg_data;

	/* Set pre-charge threshold voltage */
	if (chg->pre_uvolt < RT5033_CHARGER_PRE_THRESHOLD_LIMIT_MIN ||
	    chg->pre_uvolt > RT5033_CHARGER_PRE_THRESHOLD_LIMIT_MAX) {
		dev_err(charger->dev,
			"Value 'precharge-upper-limit-microvolt' out of range\n");
		return -EINVAL;
	}

	if (chg->pre_uvolt == RT5033_CHARGER_PRE_THRESHOLD_LIMIT_MIN)
		reg_data = 0x00;
	else if (chg->pre_uvolt == RT5033_CHARGER_PRE_THRESHOLD_LIMIT_MAX)
		reg_data = 0x0f;
	else {
		val = chg->pre_uvolt;
		val -= RT5033_CHARGER_PRE_THRESHOLD_LIMIT_MIN;
		val /= RT5033_CHARGER_PRE_THRESHOLD_STEP_NUM;
		reg_data = val;
	}

	ret = regmap_update_bits(charger->regmap, RT5033_REG_CHG_CTRL5,
			RT5033_CHGCTRL5_VPREC_MASK, reg_data);
	if (ret) {
		dev_err(charger->dev, "Failed regmap update\n");
		return -EINVAL;
	}

	/* Set pre-charge mode charging current */
	if (chg->pre_uamp < RT5033_CHARGER_PRE_CURRENT_LIMIT_MIN ||
	    chg->pre_uamp > RT5033_CHARGER_PRE_CURRENT_LIMIT_MAX) {
		dev_err(charger->dev,
			"Value 'precharge-current-microamp' out of range\n");
		return -EINVAL;
	}

	if (chg->pre_uamp == RT5033_CHARGER_PRE_CURRENT_LIMIT_MIN)
		reg_data = 0x00;
	else if (chg->pre_uamp == RT5033_CHARGER_PRE_CURRENT_LIMIT_MAX)
		reg_data = RT5033_CHG_MAX_PRE_CURRENT;
	else {
		val = chg->pre_uamp;
		val -= RT5033_CHARGER_PRE_CURRENT_LIMIT_MIN;
		val /= RT5033_CHARGER_PRE_CURRENT_STEP_NUM;
		reg_data = val;
	}

	ret = regmap_update_bits(charger->regmap, RT5033_REG_CHG_CTRL4,
			RT5033_CHGCTRL4_IPREC_MASK,
			reg_data << RT5033_CHGCTRL4_IPREC_SHIFT);
	if (ret) {
		dev_err(charger->dev, "Failed regmap update\n");
		return -EINVAL;
	}

	return 0;
}

static int rt5033_charger_reg_init(struct rt5033_charger *charger)
{
	int ret = 0;

	/* Enable charging termination */
	ret = regmap_update_bits(charger->regmap, RT5033_REG_CHG_CTRL1,
			RT5033_CHGCTRL1_TE_EN_MASK, RT5033_TE_ENABLE);
	if (ret) {
		dev_err(charger->dev, "Failed to enable charging termination.\n");
		return -EINVAL;
	}

	/*
	 * Disable minimum input voltage regulation (MIVR), this improves
	 * the charging performance.
	 */
	ret = regmap_update_bits(charger->regmap, RT5033_REG_CHG_CTRL4,
			RT5033_CHGCTRL4_MIVR_MASK, RT5033_CHARGER_MIVR_DISABLE);
	if (ret) {
		dev_err(charger->dev, "Failed to disable MIVR.\n");
		return -EINVAL;
	}

	ret = rt5033_init_pre_charge(charger);
	if (ret)
		return ret;

	ret = rt5033_init_fast_charge(charger);
	if (ret)
		return ret;

	ret = rt5033_init_const_charge(charger);
	if (ret)
		return ret;

	return 0;
}

static int rt5033_charger_set_otg(struct rt5033_charger *charger)
{
	int ret;

	mutex_lock(&charger->lock);

	/* Set OTG boost v_out to 5 volts */
	ret = regmap_update_bits(charger->regmap, RT5033_REG_CHG_CTRL2,
			RT5033_CHGCTRL2_CV_MASK,
			0x37 << RT5033_CHGCTRL2_CV_SHIFT);
	if (ret) {
		dev_err(charger->dev, "Failed set OTG boost v_out\n");
		ret = -EINVAL;
		goto out_unlock;
	}

	/* Set operation mode to OTG */
	ret = regmap_update_bits(charger->regmap, RT5033_REG_CHG_CTRL1,
			RT5033_CHGCTRL1_MODE_MASK, RT5033_BOOST_MODE);
	if (ret) {
		dev_err(charger->dev, "Failed to update OTG mode.\n");
		ret = -EINVAL;
		goto out_unlock;
	}

	/* In case someone switched from charging to OTG directly */
	if (charger->online)
		charger->online = false;

	charger->otg = true;

out_unlock:
	mutex_unlock(&charger->lock);

	return ret;
}

static int rt5033_charger_unset_otg(struct rt5033_charger *charger)
{
	int ret;
	u8 data;

	/* Restore constant voltage for charging */
	data = charger->cv_regval;
	ret = regmap_update_bits(charger->regmap, RT5033_REG_CHG_CTRL2,
			RT5033_CHGCTRL2_CV_MASK,
			data << RT5033_CHGCTRL2_CV_SHIFT);
	if (ret) {
		dev_err(charger->dev, "Failed to restore constant voltage\n");
		return -EINVAL;
	}

	/* Set operation mode to charging */
	ret = regmap_update_bits(charger->regmap, RT5033_REG_CHG_CTRL1,
			RT5033_CHGCTRL1_MODE_MASK, RT5033_CHARGER_MODE);
	if (ret) {
		dev_err(charger->dev, "Failed to update charger mode.\n");
		return -EINVAL;
	}

	charger->otg = false;

	return 0;
}

static int rt5033_charger_set_charging(struct rt5033_charger *charger)
{
	int ret;

	mutex_lock(&charger->lock);

	/* In case someone switched from OTG to charging directly */
	if (charger->otg) {
		ret = rt5033_charger_unset_otg(charger);
		if (ret) {
			mutex_unlock(&charger->lock);
			return -EINVAL;
		}
	}

	charger->online = true;

	mutex_unlock(&charger->lock);

	return 0;
}

static int rt5033_charger_set_mivr(struct rt5033_charger *charger)
{
	int ret;

	mutex_lock(&charger->lock);

	/*
	 * When connected via USB connector type SDP (Standard Downstream Port),
	 * the minimum input voltage regulation (MIVR) should be enabled. It
	 * prevents an input voltage drop due to insufficient current provided
	 * by the adapter or USB input. As a downside, it may reduces the
	 * charging current and thus slows the charging.
	 */
	ret = regmap_update_bits(charger->regmap, RT5033_REG_CHG_CTRL4,
			RT5033_CHGCTRL4_MIVR_MASK, RT5033_CHARGER_MIVR_4600MV);
	if (ret) {
		dev_err(charger->dev, "Failed to set MIVR level.\n");
		mutex_unlock(&charger->lock);
		return -EINVAL;
	}

	charger->mivr_enabled = true;

	mutex_unlock(&charger->lock);

	/* Beyond this, do the same steps like setting charging */
	rt5033_charger_set_charging(charger);

	return 0;
}

static int rt5033_charger_set_disconnect(struct rt5033_charger *charger)
{
	int ret = 0;

	mutex_lock(&charger->lock);

	/* Disable MIVR if enabled */
	if (charger->mivr_enabled) {
		ret = regmap_update_bits(charger->regmap,
				RT5033_REG_CHG_CTRL4,
				RT5033_CHGCTRL4_MIVR_MASK,
				RT5033_CHARGER_MIVR_DISABLE);
		if (ret) {
			dev_err(charger->dev, "Failed to disable MIVR.\n");
			ret = -EINVAL;
			goto out_unlock;
		}

		charger->mivr_enabled = false;
	}

	if (charger->otg) {
		ret = rt5033_charger_unset_otg(charger);
		if (ret) {
			ret = -EINVAL;
			goto out_unlock;
		}
	}

	if (charger->online)
		charger->online = false;

out_unlock:
	mutex_unlock(&charger->lock);

	return ret;
}

static enum power_supply_property rt5033_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_ONLINE,
};

static int rt5033_charger_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct rt5033_charger *charger = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = rt5033_get_charger_state(charger);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = rt5033_get_charger_type(charger);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = rt5033_get_charger_current_limit(charger);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = rt5033_get_charger_const_voltage(charger);
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = RT5033_CHARGER_MODEL;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = RT5033_MANUFACTURER;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = charger->online;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rt5033_charger_dt_init(struct rt5033_charger *charger)
{
	struct rt5033_charger_data *chg = &charger->chg;
	struct power_supply_battery_info *info;
	int ret;

	ret = power_supply_get_battery_info(charger->psy, &info);
	if (ret)
		return dev_err_probe(charger->dev, -EINVAL,
				     "missing battery info\n");

	/* Assign data. Validity will be checked in the init functions. */
	chg->pre_uamp = info->precharge_current_ua;
	chg->fast_uamp = info->constant_charge_current_max_ua;
	chg->eoc_uamp = info->charge_term_current_ua;
	chg->pre_uvolt = info->precharge_voltage_max_uv;
	chg->const_uvolt = info->constant_charge_voltage_max_uv;

	return 0;
}

static void rt5033_charger_extcon_work(struct work_struct *work)
{
	struct rt5033_charger *charger =
		container_of(work, struct rt5033_charger, extcon_work);
	struct extcon_dev *edev = charger->edev;
	int connector, state;
	int ret;

	for (connector = EXTCON_USB_HOST; connector <= EXTCON_CHG_USB_PD;
	     connector++) {
		state = extcon_get_state(edev, connector);
		if (state == 1)
			break;
	}

	/*
	 * Adding a delay between extcon notification and extcon action. This
	 * makes extcon action execution more reliable. Without the delay the
	 * execution sometimes fails, possibly because the chip is busy or not
	 * ready.
	 */
	msleep(100);

	switch (connector) {
	case EXTCON_CHG_USB_SDP:
		ret = rt5033_charger_set_mivr(charger);
		if (ret) {
			dev_err(charger->dev, "failed to set USB mode\n");
			break;
		}
		dev_info(charger->dev, "USB mode. connector type: %d\n",
			 connector);
		break;
	case EXTCON_CHG_USB_DCP:
	case EXTCON_CHG_USB_CDP:
	case EXTCON_CHG_USB_ACA:
	case EXTCON_CHG_USB_FAST:
	case EXTCON_CHG_USB_SLOW:
	case EXTCON_CHG_WPT:
	case EXTCON_CHG_USB_PD:
		ret = rt5033_charger_set_charging(charger);
		if (ret) {
			dev_err(charger->dev, "failed to set charging\n");
			break;
		}
		dev_info(charger->dev, "charging. connector type: %d\n",
			 connector);
		break;
	case EXTCON_USB_HOST:
		ret = rt5033_charger_set_otg(charger);
		if (ret) {
			dev_err(charger->dev, "failed to set OTG\n");
			break;
		}
		dev_info(charger->dev, "OTG enabled\n");
		break;
	default:
		ret = rt5033_charger_set_disconnect(charger);
		if (ret) {
			dev_err(charger->dev, "failed to set disconnect\n");
			break;
		}
		dev_info(charger->dev, "disconnected\n");
		break;
	}

	power_supply_changed(charger->psy);
}

static int rt5033_charger_extcon_notifier(struct notifier_block *nb,
					  unsigned long event, void *param)
{
	struct rt5033_charger *charger =
		container_of(nb, struct rt5033_charger, extcon_nb);

	schedule_work(&charger->extcon_work);

	return NOTIFY_OK;
}

static const struct power_supply_desc rt5033_charger_desc = {
	.name = "rt5033-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = rt5033_charger_props,
	.num_properties = ARRAY_SIZE(rt5033_charger_props),
	.get_property = rt5033_charger_get_property,
};

static int rt5033_charger_probe(struct platform_device *pdev)
{
	struct rt5033_charger *charger;
	struct power_supply_config psy_cfg = {};
	struct device_node *np_conn, *np_edev;
	int ret;

	charger = devm_kzalloc(&pdev->dev, sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	platform_set_drvdata(pdev, charger);
	charger->dev = &pdev->dev;
	charger->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	mutex_init(&charger->lock);

	psy_cfg.of_node = pdev->dev.of_node;
	psy_cfg.drv_data = charger;

	charger->psy = devm_power_supply_register(charger->dev,
						  &rt5033_charger_desc,
						  &psy_cfg);
	if (IS_ERR(charger->psy))
		return dev_err_probe(charger->dev, PTR_ERR(charger->psy),
				     "Failed to register power supply\n");

	ret = rt5033_charger_dt_init(charger);
	if (ret)
		return ret;

	ret = rt5033_charger_reg_init(charger);
	if (ret)
		return ret;

	/*
	 * Extcon support is not vital for the charger to work. If no extcon
	 * is available, just emit a warning and leave the probe function.
	 */
	np_conn = of_parse_phandle(pdev->dev.of_node, "richtek,usb-connector", 0);
	np_edev = of_get_parent(np_conn);
	charger->edev = extcon_find_edev_by_node(np_edev);
	if (IS_ERR(charger->edev)) {
		dev_warn(charger->dev, "no extcon device found in device-tree\n");
		goto out;
	}

	ret = devm_work_autocancel(charger->dev, &charger->extcon_work,
				   rt5033_charger_extcon_work);
	if (ret) {
		dev_err(charger->dev, "failed to initialize extcon work\n");
		return ret;
	}

	charger->extcon_nb.notifier_call = rt5033_charger_extcon_notifier;
	ret = devm_extcon_register_notifier_all(charger->dev, charger->edev,
						&charger->extcon_nb);
	if (ret) {
		dev_err(charger->dev, "failed to register extcon notifier\n");
		return ret;
	}
out:
	return 0;
}

static const struct platform_device_id rt5033_charger_id[] = {
	{ "rt5033-charger", },
	{ }
};
MODULE_DEVICE_TABLE(platform, rt5033_charger_id);

static const struct of_device_id rt5033_charger_of_match[] = {
	{ .compatible = "richtek,rt5033-charger", },
	{ }
};
MODULE_DEVICE_TABLE(of, rt5033_charger_of_match);

static struct platform_driver rt5033_charger_driver = {
	.driver = {
		.name = "rt5033-charger",
		.of_match_table = rt5033_charger_of_match,
	},
	.probe = rt5033_charger_probe,
	.id_table = rt5033_charger_id,
};
module_platform_driver(rt5033_charger_driver);

MODULE_DESCRIPTION("Richtek RT5033 charger driver");
MODULE_AUTHOR("Beomho Seo <beomho.seo@samsung.com>");
MODULE_LICENSE("GPL v2");
