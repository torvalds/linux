// SPDX-License-Identifier: GPL-2.0
/*
 * Based on max77650-charger.c
 *
 * Copyright (C) 2025 Dzmitry Sankouski <dsankouski@gmail.org>
 *
 * Battery charger driver for MAXIM 77705 charger/power-supply.
 */

#include <linux/devm-helpers.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/max77693-common.h>
#include <linux/mfd/max77705-private.h>
#include <linux/power/max77705_charger.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

static const char *max77705_charger_model		= "max77705";
static const char *max77705_charger_manufacturer	= "Maxim Integrated";

static const struct regmap_config max77705_chg_regmap_config = {
	.reg_base = MAX77705_CHG_REG_BASE,
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX77705_CHG_REG_SAFEOUT_CTRL,
};

static enum power_supply_property max77705_charger_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
};

static int max77705_chgin_irq(void *irq_drv_data)
{
	struct max77705_charger_data *charger = irq_drv_data;

	queue_work(charger->wqueue, &charger->chgin_work);

	return 0;
}

static const struct regmap_irq max77705_charger_irqs[] = {
	{ .mask = MAX77705_BYP_IM,   },
	{ .mask = MAX77705_INP_LIMIT_IM,   },
	{ .mask = MAX77705_BATP_IM,   },
	{ .mask = MAX77705_BAT_IM,   },
	{ .mask = MAX77705_CHG_IM,   },
	{ .mask = MAX77705_WCIN_IM,   },
	{ .mask = MAX77705_CHGIN_IM,   },
	{ .mask = MAX77705_AICL_IM,   },
};

static struct regmap_irq_chip max77705_charger_irq_chip = {
	.name			= "max77705-charger",
	.status_base		= MAX77705_CHG_REG_INT,
	.mask_base		= MAX77705_CHG_REG_INT_MASK,
	.handle_post_irq	= max77705_chgin_irq,
	.num_regs		= 1,
	.irqs			= max77705_charger_irqs,
	.num_irqs		= ARRAY_SIZE(max77705_charger_irqs),
};

static int max77705_charger_enable(struct max77705_charger_data *chg)
{
	int rv;

	rv = regmap_update_bits(chg->regmap, MAX77705_CHG_REG_CNFG_09,
				MAX77705_CHG_EN_MASK, MAX77705_CHG_EN_MASK);
	if (rv)
		dev_err(chg->dev, "unable to enable the charger: %d\n", rv);

	return rv;
}

static void max77705_charger_disable(void *data)
{
	struct max77705_charger_data *chg = data;
	int rv;

	rv = regmap_update_bits(chg->regmap,
				MAX77705_CHG_REG_CNFG_09,
				MAX77705_CHG_EN_MASK,
				MAX77705_CHG_DISABLE);
	if (rv)
		dev_err(chg->dev, "unable to disable the charger: %d\n", rv);
}

static int max77705_get_online(struct regmap *regmap, int *val)
{
	unsigned int data;
	int ret;

	ret = regmap_read(regmap, MAX77705_CHG_REG_INT_OK, &data);
	if (ret < 0)
		return ret;

	*val = !!(data & MAX77705_CHGIN_OK);

	return 0;
}

static int max77705_check_battery(struct max77705_charger_data *charger, int *val)
{
	unsigned int reg_data;
	unsigned int reg_data2;
	struct regmap *regmap = charger->regmap;

	regmap_read(regmap, MAX77705_CHG_REG_INT_OK, &reg_data);

	dev_dbg(charger->dev, "CHG_INT_OK(0x%x)\n", reg_data);

	regmap_read(regmap, MAX77705_CHG_REG_DETAILS_00, &reg_data2);

	dev_dbg(charger->dev, "CHG_DETAILS00(0x%x)\n", reg_data2);

	if ((reg_data & MAX77705_BATP_OK) || !(reg_data2 & MAX77705_BATP_DTLS))
		*val = true;
	else
		*val = false;

	return 0;
}

static int max77705_get_charge_type(struct max77705_charger_data *charger, int *val)
{
	struct regmap *regmap = charger->regmap;
	unsigned int reg_data;

	regmap_read(regmap, MAX77705_CHG_REG_CNFG_09, &reg_data);
	if (!MAX77705_CHARGER_CHG_CHARGING(reg_data)) {
		*val = POWER_SUPPLY_CHARGE_TYPE_NONE;
		return 0;
	}

	regmap_read(regmap, MAX77705_CHG_REG_DETAILS_01, &reg_data);
	reg_data &= MAX77705_CHG_DTLS;

	switch (reg_data) {
	case 0x0:
	case MAX77705_CHARGER_CONSTANT_CURRENT:
	case MAX77705_CHARGER_CONSTANT_VOLTAGE:
		*val = POWER_SUPPLY_CHARGE_TYPE_FAST;
		return 0;
	default:
		*val = POWER_SUPPLY_CHARGE_TYPE_NONE;
		return 0;
	}

	return 0;
}

static int max77705_get_status(struct max77705_charger_data *charger, int *val)
{
	struct regmap *regmap = charger->regmap;
	unsigned int reg_data;

	regmap_read(regmap, MAX77705_CHG_REG_CNFG_09, &reg_data);
	if (!MAX77705_CHARGER_CHG_CHARGING(reg_data)) {
		*val = POWER_SUPPLY_CHARGE_TYPE_NONE;
		return 0;
	}

	regmap_read(regmap, MAX77705_CHG_REG_DETAILS_01, &reg_data);
	reg_data &= MAX77705_CHG_DTLS;

	switch (reg_data) {
	case 0x0:
	case MAX77705_CHARGER_CONSTANT_CURRENT:
	case MAX77705_CHARGER_CONSTANT_VOLTAGE:
		*val = POWER_SUPPLY_STATUS_CHARGING;
		return 0;
	case MAX77705_CHARGER_END_OF_CHARGE:
	case MAX77705_CHARGER_DONE:
		*val = POWER_SUPPLY_STATUS_FULL;
		return 0;
	/* those values hard coded as in vendor kernel, because of */
	/* failure to determine it's actual meaning. */
	case 0x05:
	case 0x06:
	case 0x07:
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	case 0x08:
	case 0xA:
	case 0xB:
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
		return 0;
	default:
		*val = POWER_SUPPLY_STATUS_UNKNOWN;
		return 0;
	}

	return 0;
}

static int max77705_get_vbus_state(struct regmap *regmap, int *value)
{
	int ret;
	unsigned int charge_dtls;

	ret = regmap_read(regmap, MAX77705_CHG_REG_DETAILS_00, &charge_dtls);
	if (ret)
		return ret;

	charge_dtls = ((charge_dtls & MAX77705_CHGIN_DTLS) >>
			MAX77705_CHGIN_DTLS_SHIFT);

	switch (charge_dtls) {
	case 0x00:
		*value = POWER_SUPPLY_HEALTH_UNDERVOLTAGE;
		break;
	case 0x01:
		*value = POWER_SUPPLY_HEALTH_UNDERVOLTAGE;
		break;
	case 0x02:
		*value = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		break;
	case 0x03:
		*value = POWER_SUPPLY_HEALTH_GOOD;
		break;
	default:
		return 0;
	}
	return 0;
}

static int max77705_get_battery_health(struct max77705_charger_data *charger,
					int *value)
{
	struct regmap *regmap = charger->regmap;
	unsigned int bat_dtls;

	regmap_read(regmap, MAX77705_CHG_REG_DETAILS_01, &bat_dtls);
	bat_dtls = ((bat_dtls & MAX77705_BAT_DTLS) >> MAX77705_BAT_DTLS_SHIFT);

	switch (bat_dtls) {
	case MAX77705_BATTERY_NOBAT:
		dev_dbg(charger->dev, "%s: No battery and the charger is suspended\n",
			__func__);
		*value = POWER_SUPPLY_HEALTH_NO_BATTERY;
		break;
	case MAX77705_BATTERY_PREQUALIFICATION:
		dev_dbg(charger->dev, "%s: battery is okay but its voltage is low(~VPQLB)\n",
			__func__);
		break;
	case MAX77705_BATTERY_DEAD:
		dev_dbg(charger->dev, "%s: battery dead\n", __func__);
		*value = POWER_SUPPLY_HEALTH_DEAD;
		break;
	case MAX77705_BATTERY_GOOD:
	case MAX77705_BATTERY_LOWVOLTAGE:
		*value = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case MAX77705_BATTERY_OVERVOLTAGE:
		dev_dbg(charger->dev, "%s: battery ovp\n", __func__);
		*value = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		break;
	default:
		dev_dbg(charger->dev, "%s: battery unknown\n", __func__);
		*value = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		break;
	}

	return 0;
}

static int max77705_get_health(struct max77705_charger_data *charger, int *val)
{
	struct regmap *regmap = charger->regmap;
	int ret, is_online = 0;

	ret = max77705_get_online(regmap, &is_online);
	if (ret)
		return ret;
	if (is_online) {
		ret = max77705_get_vbus_state(regmap, val);
		if (ret || (*val != POWER_SUPPLY_HEALTH_GOOD))
			return ret;
	}
	return max77705_get_battery_health(charger, val);
}

static int max77705_get_input_current(struct max77705_charger_data *charger,
					int *val)
{
	unsigned int reg_data;
	int get_current = 0;
	struct regmap *regmap = charger->regmap;

	regmap_read(regmap, MAX77705_CHG_REG_CNFG_09, &reg_data);

	reg_data &= MAX77705_CHG_CHGIN_LIM_MASK;

	if (reg_data <= 3)
		get_current = MAX77705_CURRENT_CHGIN_MIN;
	else if (reg_data >= MAX77705_CHG_CHGIN_LIM_MASK)
		get_current = MAX77705_CURRENT_CHGIN_MAX;
	else
		get_current = (reg_data + 1) * MAX77705_CURRENT_CHGIN_STEP;

	*val = get_current;

	return 0;
}

static int max77705_get_charge_current(struct max77705_charger_data *charger,
					int *val)
{
	unsigned int reg_data;
	struct regmap *regmap = charger->regmap;

	regmap_read(regmap, MAX77705_CHG_REG_CNFG_02, &reg_data);
	reg_data &= MAX77705_CHG_CC;

	*val = reg_data <= 0x2 ? MAX77705_CURRENT_CHGIN_MIN : reg_data * MAX77705_CURRENT_CHG_STEP;

	return 0;
}

static int max77705_set_float_voltage(struct max77705_charger_data *charger,
					int float_voltage)
{
	int float_voltage_mv;
	unsigned int reg_data = 0;
	struct regmap *regmap = charger->regmap;

	float_voltage_mv = float_voltage / 1000;
	reg_data = float_voltage_mv <= 4000 ? 0x0 :
		float_voltage_mv >= 4500 ? 0x23 :
		(float_voltage_mv <= 4200) ? (float_voltage_mv - 4000) / 50 :
		(((float_voltage_mv - 4200) / 10) + 0x04);

	return regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_04,
				MAX77705_CHG_CV_PRM_MASK,
				(reg_data << MAX77705_CHG_CV_PRM_SHIFT));
}

static int max77705_get_float_voltage(struct max77705_charger_data *charger,
					int *val)
{
	unsigned int reg_data = 0;
	int voltage_mv;
	struct regmap *regmap = charger->regmap;

	regmap_read(regmap, MAX77705_CHG_REG_CNFG_04, &reg_data);
	reg_data &= MAX77705_CHG_PRM_MASK;
	voltage_mv = reg_data <= 0x04 ? reg_data * 50 + 4000 :
					(reg_data - 4) * 10 + 4200;
	*val = voltage_mv * 1000;

	return 0;
}

static int max77705_chg_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct max77705_charger_data *charger = power_supply_get_drvdata(psy);
	struct regmap *regmap = charger->regmap;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		return max77705_get_online(regmap, &val->intval);
	case POWER_SUPPLY_PROP_PRESENT:
		return max77705_check_battery(charger, &val->intval);
	case POWER_SUPPLY_PROP_STATUS:
		return max77705_get_status(charger, &val->intval);
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		return max77705_get_charge_type(charger, &val->intval);
	case POWER_SUPPLY_PROP_HEALTH:
		return max77705_get_health(charger, &val->intval);
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return max77705_get_input_current(charger, &val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return max77705_get_charge_current(charger, &val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return max77705_get_float_voltage(charger, &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = charger->bat_info->voltage_max_design_uv;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = max77705_charger_model;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = max77705_charger_manufacturer;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct power_supply_desc max77705_charger_psy_desc = {
	.name = "max77705-charger",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties = max77705_charger_props,
	.num_properties = ARRAY_SIZE(max77705_charger_props),
	.get_property = max77705_chg_get_property,
};

static void max77705_chgin_isr_work(struct work_struct *work)
{
	struct max77705_charger_data *charger =
		container_of(work, struct max77705_charger_data, chgin_work);

	power_supply_changed(charger->psy_chg);
}

static void max77705_charger_initialize(struct max77705_charger_data *chg)
{
	u8 reg_data;
	struct power_supply_battery_info *info;
	struct regmap *regmap = chg->regmap;

	if (power_supply_get_battery_info(chg->psy_chg, &info) < 0)
		return;

	chg->bat_info = info;

	/* unlock charger setting protect */
	/* slowest LX slope */
	reg_data = MAX77705_CHGPROT_MASK | MAX77705_SLOWEST_LX_SLOPE;
	regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_06, reg_data,
						reg_data);

	/* fast charge timer disable */
	/* restart threshold disable */
	/* pre-qual charge disable */
	reg_data = (MAX77705_FCHGTIME_DISABLE << MAX77705_FCHGTIME_SHIFT) |
			(MAX77705_CHG_RSTRT_DISABLE << MAX77705_CHG_RSTRT_SHIFT) |
			(MAX77705_CHG_PQEN_DISABLE << MAX77705_PQEN_SHIFT);
	regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_01,
						(MAX77705_FCHGTIME_MASK |
						MAX77705_CHG_RSTRT_MASK |
						MAX77705_PQEN_MASK),
						reg_data);

	/* OTG off(UNO on), boost off */
	regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_00,
				MAX77705_OTG_CTRL, 0);

	/* charge current 450mA(default) */
	/* otg current limit 900mA */
	regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_02,
				MAX77705_OTG_ILIM_MASK,
				MAX77705_OTG_ILIM_900 << MAX77705_OTG_ILIM_SHIFT);

	/* BAT to SYS OCP 4.80A */
	regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_05,
				MAX77705_REG_B2SOVRC_MASK,
				MAX77705_B2SOVRC_4_8A << MAX77705_REG_B2SOVRC_SHIFT);
	/* top off current 150mA */
	/* top off timer 30min */
	reg_data = (MAX77705_TO_ITH_150MA << MAX77705_TO_ITH_SHIFT) |
			(MAX77705_TO_TIME_30M << MAX77705_TO_TIME_SHIFT) |
			(MAX77705_SYS_TRACK_DISABLE << MAX77705_SYS_TRACK_DIS_SHIFT);
	regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_03,
			   (MAX77705_TO_ITH_MASK |
			   MAX77705_TO_TIME_MASK |
			   MAX77705_SYS_TRACK_DIS_MASK), reg_data);

	/* cv voltage 4.2V or 4.35V */
	/* MINVSYS 3.6V(default) */
	if (info->voltage_max_design_uv < 0) {
		dev_warn(chg->dev, "missing battery:voltage-max-design-microvolt\n");
		max77705_set_float_voltage(chg, 4200000);
	} else {
		max77705_set_float_voltage(chg, info->voltage_max_design_uv);
	}

	regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_12,
				MAX77705_VCHGIN_REG_MASK, MAX77705_VCHGIN_4_5);
	regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_12,
				MAX77705_WCIN_REG_MASK, MAX77705_WCIN_4_5);

	/* Watchdog timer */
	regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_00,
				MAX77705_WDTEN_MASK, 0);

	/* Active Discharge Enable */
	regmap_update_bits(regmap, MAX77705_PMIC_REG_MAINCTRL1, 1, 1);

	/* VBYPSET=5.0V */
	regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_11, MAX77705_VBYPSET_MASK, 0);

	/* Switching Frequency : 1.5MHz */
	regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_08, MAX77705_REG_FSW_MASK,
				(MAX77705_CHG_FSW_1_5MHz << MAX77705_REG_FSW_SHIFT));

	/* Auto skip mode */
	regmap_update_bits(regmap, MAX77705_CHG_REG_CNFG_12, MAX77705_REG_DISKIP_MASK,
				(MAX77705_AUTO_SKIP << MAX77705_REG_DISKIP_SHIFT));
}

static int max77705_charger_probe(struct i2c_client *i2c)
{
	struct power_supply_config pscfg = {};
	struct max77705_charger_data *chg;
	struct device *dev;
	struct regmap_irq_chip_data *irq_data;
	int ret;

	dev = &i2c->dev;

	chg = devm_kzalloc(dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;

	chg->dev = dev;
	i2c_set_clientdata(i2c, chg);

	chg->regmap = devm_regmap_init_i2c(i2c, &max77705_chg_regmap_config);
	if (IS_ERR(chg->regmap))
		return PTR_ERR(chg->regmap);

	ret = regmap_update_bits(chg->regmap,
				MAX77705_CHG_REG_INT_MASK,
				MAX77705_CHGIN_IM, 0);
	if (ret)
		return ret;

	pscfg.fwnode = dev_fwnode(dev);
	pscfg.drv_data = chg;

	chg->psy_chg = devm_power_supply_register(dev, &max77705_charger_psy_desc, &pscfg);
	if (IS_ERR(chg->psy_chg))
		return PTR_ERR(chg->psy_chg);

	max77705_charger_irq_chip.irq_drv_data = chg;
	ret = devm_regmap_add_irq_chip(chg->dev, chg->regmap, i2c->irq,
					IRQF_ONESHOT | IRQF_SHARED, 0,
					&max77705_charger_irq_chip,
					&irq_data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add irq chip\n");

	chg->wqueue = create_singlethread_workqueue(dev_name(dev));
	if (IS_ERR(chg->wqueue))
		return dev_err_probe(dev, PTR_ERR(chg->wqueue), "failed to create workqueue\n");

	ret = devm_work_autocancel(dev, &chg->chgin_work, max77705_chgin_isr_work);
	if (ret)
		return dev_err_probe(dev, ret, "failed to initialize interrupt work\n");

	max77705_charger_initialize(chg);

	ret = max77705_charger_enable(chg);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable charge\n");

	return devm_add_action_or_reset(dev, max77705_charger_disable, chg);
}

static const struct of_device_id max77705_charger_of_match[] = {
	{ .compatible = "maxim,max77705-charger" },
	{ }
};
MODULE_DEVICE_TABLE(of, max77705_charger_of_match);

static struct i2c_driver max77705_charger_driver = {
	.driver = {
		.name = "max77705-charger",
		.of_match_table = max77705_charger_of_match,
	},
	.probe = max77705_charger_probe,
};
module_i2c_driver(max77705_charger_driver);

MODULE_AUTHOR("Dzmitry Sankouski <dsankouski@gmail.com>");
MODULE_DESCRIPTION("Maxim MAX77705 charger driver");
MODULE_LICENSE("GPL");
