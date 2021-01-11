/*
 * rk817 charger driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd
 * xsf <xsf@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/delay.h>
#include <linux/extcon.h>
#include <linux/gpio.h>
#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/mfd/rk808.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/power/rk_usbbc.h>
#include <linux/regmap.h>
#include <linux/rtc.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

static int dbg_enable;
module_param_named(dbg_level, dbg_enable, int, 0644);

#define DBG(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)

#define CHARGE_DRIVER_VERSION		"1.0"

#define DISABLE	0x00
#define ENABLE	0x01
#define OTG_SLP_ENABLE	0x01
#define OTG_SLP_DISABLE	0x00
#define OTG_ENABLE		0x11
#define OTG_DISABLE		0x10
#define RK817_BOOST_ENABLE	0x11
#define RK817_BOOST_DISABLE	0x10
#define OTG_MODE		0x01
#define OTG_MODE_ON		0x01
#define DEFAULT_INPUT_VOLTAGE	4500
#define DEFAULT_INPUT_CURRENT	2000
#define DEFAULT_CHRG_VOLTAGE	4200
#define DEFAULT_CHRG_CURRENT	1400
#define DEFAULT_CHRG_TERM_MODE	1
#define DEFAULT_CHRG_TERM_CUR		150
#define SAMPLE_RES_10MR		10
#define SAMPLE_RES_20MR		20
#define SAMPLE_RES_DIV1		1
#define SAMPLE_RES_DIV2		2

#define INPUT_450MA		450
#define INPUT_1500MA	1500

#define CURRENT_TO_ADC(current, samp_res)	\
	(current * 1000 * samp_res / 172)

enum charge_current {
	CHRG_CUR_1000MA,
	CHRG_CUR_1500MA,
	CHRG_CUR_2000MA,
	CHRG_CUR_2500MA,
	CHRG_CUR_2750MA,
	CHRG_CUR_3000MA,
	CHRG_CUR_3500MA,
	CHRG_CUR_500MA,
};

enum charge_voltage {
	CHRG_VOL_4100MV,
	CHRG_VOL_4150MV,
	CHRG_VOL_4200MV,
	CHRG_VOL_4250MV,
	CHRG_VOL_4300MV,
	CHRG_VOL_4350MV,
	CHRG_VOL_4400MV,
	CHRG_VOL_4450MV,
};

enum input_voltage {
	INPUT_VOL_4000MV,
	INPUT_VOL_4100MV,
	INPUT_VOL_4200MV,
	INPUT_VOL_4300MV,
	INPUT_VOL_4400MV,
	INPUT_VOL_4500MV,
	INPUT_VOL_4600MV,
	INPUT_VOL_4700MV,
};

enum input_current {
	INPUT_CUR_450MA,
	INPUT_CUR_80MA,
	INPUT_CUR_850MA,
	INPUT_CUR_1500MA,
	INPUT_CUR_1750MA,
	INPUT_CUR_2000MA,
	INPUT_CUR_2500MA,
	INPUT_CUR_3000MA,
};

enum charge_clk {
	CHRG_CLK_1M,
	CHRG_CLK_2M,
};

enum charge_term_sel {
	CHRG_TERM_150MA,
	CHRG_TERM_200MA,
	CHRG_TERM_300MA,
	CHRG_TERM_400MA,
};

enum charge_timer_trickle {
	CHRG_TIMER_TRIKL_30MIN,
	CHRG_TIMER_TRIKL_45MIN,
	CHRG_TIMER_TRIKL_60MIN,
	CHRG_TIMER_TRIKL_90MIN,
	CHRG_TIMER_TRIKL_120MIN,
	CHRG_TIMER_TRIKL_150MIN,
	CHRG_TIMER_TRIKL_180MIN,
	CHRG_TIMER_TRIKL_210MIN,
};

enum charge_timer_cccv {
	CHRG_TIMER_CCCV_4H,
	CHRG_TIMER_CCCV_5H,
	CHRG_TIMER_CCCV_6H,
	CHRG_TIMER_CCCV_8H,
	CHRG_TIMER_CCCV_10H,
	CHRG_TIMER_CCCV_12H,
	CHRG_TIMER_CCCV_14H,
	CHRG_TIMER_CCCV_16H,
};

enum charge_status {
	CHRG_OFF,
	DEAD_CHRG,
	TRICKLE_CHRG,
	CC_OR_CV_CHRG,
	CHRG_TERM,
	USB_OVER_VOL,
	BAT_TMP_ERR,
	BAT_TIM_ERR,
};

enum discharge_ilimit {
	DISCHRG_2000MA,
	DISCHRG_2500MA,
	DISCHRG_3000MA,
	DISCHRG_3500MA,
	DISCHRG_4000MA,
};

enum bat_system_comp_time {
	DLY_20US,
	DLY_10US,
	DLY_40US,
	DLY_20US_AGAIN,
};

enum charge_term_mode {
	CHRG_ANALOG,
	CHRG_DIGITAL,
};

enum charger_t {
	USB_TYPE_UNKNOWN_CHARGER,
	USB_TYPE_NONE_CHARGER,
	USB_TYPE_USB_CHARGER,
	USB_TYPE_AC_CHARGER,
	USB_TYPE_CDP_CHARGER,
	DC_TYPE_DC_CHARGER,
	DC_TYPE_NONE_CHARGER,
};

enum charger_state {
	OFFLINE = 0,
	ONLINE
};

enum rk817_charge_fields {
	BOOST_EN, OTG_EN, OTG_SLP_EN, CHRG_CLK_SEL,
	CHRG_EN, CHRG_VOL_SEL, CHRG_CT_EN, CHRG_CUR_SEL,
	USB_VLIM_EN, USB_VLIM_SEL, USB_ILIM_EN, USB_ILIM_SEL,
	SYS_CAN_SD,  USB_SYS_EN, BAT_OVP_EN, CHRG_TERM_ANA_DIG,
	CHRG_TERM_ANA_SEL,
	CHRG_TERM_DIG,
	BAT_HTS_TS, BAT_LTS_TS,
	CHRG_TIMER_TRIKL_EN, CHRG_TIMER_TRIKL,
	CHRG_TIMER_CCCV_EN, CHRG_TIMER_CCCV,
	BAT_EXS, CHG_STS, BAT_OVP_STS, CHRG_IN_CLAMP,
	USB_EXS, USB_EFF,
	BAT_DIS_ILIM_STS, BAT_SYS_CMP_DLY, BAT_DIS_ILIM_EN,
	BAT_DISCHRG_ILIM,
	PLUG_IN_STS, SOC_REG0, SOC_REG1, SOC_REG2,
	F_MAX_FIELDS
};

static const struct reg_field rk817_charge_reg_fields[] = {
	[SOC_REG0] = REG_FIELD(0x9A, 0, 7),
	[SOC_REG1] = REG_FIELD(0x9B, 0, 7),
	[SOC_REG2] = REG_FIELD(0x9C, 0, 7),
	[BOOST_EN] = REG_FIELD(0xB4, 1, 5),
	[OTG_EN] = REG_FIELD(0xB4, 2, 6),
	[OTG_SLP_EN] = REG_FIELD(0xB5, 6, 6),
	[CHRG_EN] = REG_FIELD(0xE4, 7, 7),
	[CHRG_VOL_SEL] = REG_FIELD(0xE4, 4, 6),
	[CHRG_CT_EN] = REG_FIELD(0xE4, 3, 3),
	[CHRG_CUR_SEL] = REG_FIELD(0xE4, 0, 2),

	[USB_VLIM_EN] = REG_FIELD(0xE5, 7, 7),
	[USB_VLIM_SEL] = REG_FIELD(0xE5, 4, 6),
	[USB_ILIM_EN] = REG_FIELD(0xE5, 3, 3),
	[USB_ILIM_SEL] = REG_FIELD(0xE5, 0, 2),

	[SYS_CAN_SD] = REG_FIELD(0xE6, 7, 7),
	[USB_SYS_EN] = REG_FIELD(0xE6, 6, 6),
	[BAT_OVP_EN] = REG_FIELD(0xE6, 3, 3),
	[CHRG_TERM_ANA_DIG] = REG_FIELD(0xE6, 2, 2),
	[CHRG_TERM_ANA_SEL] = REG_FIELD(0xE6, 0, 1),

	[CHRG_TERM_DIG] = REG_FIELD(0xE7, 0, 7),

	[BAT_HTS_TS] = REG_FIELD(0xE8, 0, 7),

	[BAT_LTS_TS] = REG_FIELD(0xE9, 0, 7),

	[CHRG_TIMER_TRIKL_EN] = REG_FIELD(0xEA, 7, 7),
	[CHRG_TIMER_TRIKL] = REG_FIELD(0xEA, 4, 6),
	[CHRG_TIMER_CCCV_EN] = REG_FIELD(0xEA, 3, 3),
	[CHRG_TIMER_CCCV] = REG_FIELD(0xEA, 0, 2),

	[BAT_EXS] = REG_FIELD(0xEB, 7, 7),
	[CHG_STS] = REG_FIELD(0xEB, 4, 6),
	[BAT_OVP_STS] = REG_FIELD(0xEB, 3, 3),
	[CHRG_IN_CLAMP] = REG_FIELD(0xEB, 2, 2),
	[USB_EXS] = REG_FIELD(0xEB, 1, 1),
	[USB_EFF] = REG_FIELD(0xEB, 0, 0),

	[BAT_DIS_ILIM_STS] = REG_FIELD(0xEC, 6, 6),
	[BAT_SYS_CMP_DLY] = REG_FIELD(0xEC, 4, 5),
	[BAT_DIS_ILIM_EN] = REG_FIELD(0xEC, 3, 3),
	[BAT_DISCHRG_ILIM] = REG_FIELD(0xEC, 0, 2),
	[PLUG_IN_STS] = REG_FIELD(0xf0, 6, 6),
	[CHRG_CLK_SEL] = REG_FIELD(0xF3, 6, 6),
};

struct charger_platform_data {
	u32 max_input_current;
	u32 min_input_voltage;

	u32 max_chrg_current;
	u32 max_chrg_voltage;

	u32 chrg_finish_cur;
	u32 chrg_term_mode;

	u32 power_dc2otg;
	u32 dc_det_level;
	int dc_det_pin;
	bool support_dc_det;
	int virtual_power;
	int sample_res;
	int otg5v_suspend_enable;
	bool extcon;
	int gate_function_disable;
};

struct rk817_charger {
	struct i2c_client *client;
	struct platform_device *pdev;
	struct device *dev;
	struct rk808 *rk817;
	struct regmap *regmap;
	struct regmap_field *rmap_fields[F_MAX_FIELDS];

	struct power_supply *ac_psy;
	struct power_supply *usb_psy;
	struct extcon_dev *cable_edev;
	struct charger_platform_data *pdata;
	struct workqueue_struct *usb_charger_wq;
	struct workqueue_struct *dc_charger_wq;
	struct delayed_work dc_work;
	struct delayed_work usb_work;
	struct delayed_work host_work;
	struct delayed_work discnt_work;
	struct delayed_work irq_work;
	struct notifier_block bc_nb;
	struct notifier_block cable_cg_nb;
	struct notifier_block cable_host_nb;
	struct notifier_block cable_discnt_nb;
	unsigned int bc_event;
	enum charger_t usb_charger;
	enum charger_t dc_charger;
	struct regulator *otg5v_rdev;
	u8 ac_in;
	u8 usb_in;
	u8 otg_in;
	u8 dc_in;
	u8 prop_status;

	u32 max_input_current;
	u32 min_input_voltage;

	u32 max_chrg_current;
	u32 max_chrg_voltage;

	u32 chrg_finish_cur;
	u32 chrg_term_mode;

	u8 res_div;
	u8 otg_slp_state;
	u8 plugin_trigger;
	u8 plugout_trigger;
	int plugin_irq;
	int plugout_irq;
};

static enum power_supply_property rk817_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static enum power_supply_property rk817_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static int rk817_charge_ac_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct rk817_charger *charge = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (charge->pdata->virtual_power)
			val->intval = 1;
		else
			val->intval = (charge->ac_in | charge->dc_in);

		DBG("ac report online: %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (charge->pdata->virtual_power)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = charge->prop_status;

		DBG("report prop: %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = charge->max_chrg_voltage * 1000;	/* uV */
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = charge->max_chrg_current * 1000;	/* uA */
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rk817_charge_usb_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct rk817_charger *charge = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (charge->pdata->virtual_power)
			val->intval = 1;
		else
			val->intval = charge->usb_in;

		DBG("usb report online: %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (charge->pdata->virtual_power)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = charge->prop_status;

		DBG("report prop: %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = charge->max_chrg_voltage;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = charge->max_chrg_current;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct power_supply_desc rk817_ac_desc = {
	.name		= "ac",
	.type		= POWER_SUPPLY_TYPE_MAINS,
	.properties	= rk817_ac_props,
	.num_properties	= ARRAY_SIZE(rk817_ac_props),
	.get_property	= rk817_charge_ac_get_property,
};

static const struct power_supply_desc rk817_usb_desc = {
	.name		= "usb",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= rk817_usb_props,
	.num_properties	= ARRAY_SIZE(rk817_usb_props),
	.get_property	= rk817_charge_usb_get_property,
};

static int rk817_charge_init_power_supply(struct rk817_charger *charge)
{
	struct power_supply_config psy_cfg = { .drv_data = charge, };

	charge->usb_psy = devm_power_supply_register(charge->dev,
						     &rk817_usb_desc,
						     &psy_cfg);
	if (IS_ERR(charge->usb_psy)) {
		dev_err(charge->dev, "register usb power supply fail\n");
		return PTR_ERR(charge->usb_psy);
	}

	charge->ac_psy = devm_power_supply_register(charge->dev, &rk817_ac_desc,
						&psy_cfg);
	if (IS_ERR(charge->ac_psy)) {
		dev_err(charge->dev, "register ac power supply fail\n");
		return PTR_ERR(charge->ac_psy);
	}

	return 0;
}

static int rk817_charge_field_read(struct rk817_charger *charge,
				   enum rk817_charge_fields field_id)
{
	int ret;
	int val;

	ret = regmap_field_read(charge->rmap_fields[field_id], &val);
	if (ret < 0)
		return ret;

	return val;
}

static int rk817_charge_field_write(struct rk817_charger *charge,
				    enum rk817_charge_fields field_id,
				    unsigned int val)
{
	return regmap_field_write(charge->rmap_fields[field_id], val);
}

static int rk817_charge_get_otg_state(struct rk817_charger *charge)
{
	return regulator_is_enabled(charge->otg5v_rdev);
}

static void rk817_charge_boost_disable(struct rk817_charger *charge)
{
	rk817_charge_field_write(charge, BOOST_EN, RK817_BOOST_DISABLE);
}

static void rk817_charge_boost_enable(struct rk817_charger *charge)
{
	rk817_charge_field_write(charge, BOOST_EN, RK817_BOOST_ENABLE);
}

static void rk817_charge_otg_disable(struct rk817_charger *charge)
{
	int ret;

	ret = regulator_disable(charge->otg5v_rdev);

	if (ret) {
		DBG("disable otg5v failed:%d\n", ret);
		return;
	}

	return;
}

static void rk817_charge_otg_enable(struct rk817_charger *charge)
{
	int ret;

	ret = regulator_enable(charge->otg5v_rdev);

	if (ret) {
		DBG("enable otg5v failed:%d\n", ret);
		return;
	}

	return;
}

#ifdef CONFIG_PM_SLEEP
static int rk817_charge_get_otg_slp_state(struct rk817_charger *charge)
{
	return (rk817_charge_field_read(charge, OTG_SLP_EN) & OTG_SLP_ENABLE);
}

static void rk817_charge_otg_slp_disable(struct rk817_charger *charge)
{
	rk817_charge_field_write(charge, OTG_SLP_EN, OTG_SLP_DISABLE);
}

static void rk817_charge_otg_slp_enable(struct rk817_charger *charge)
{
	rk817_charge_field_write(charge, OTG_SLP_EN, OTG_SLP_ENABLE);
}
#endif

static int rk817_charge_get_charge_state(struct rk817_charger *charge)
{
	return rk817_charge_field_read(charge, CHRG_EN);
}

static void rk817_charge_enable_charge(struct rk817_charger *charge)
{
	rk817_charge_field_write(charge, CHRG_EN, ENABLE);
}

static void rk817_charge_usb_to_sys_enable(struct rk817_charger *charge)
{
	rk817_charge_field_write(charge, USB_SYS_EN, ENABLE);
}

static void rk817_charge_sys_can_sd_disable(struct rk817_charger *charge)
{
	rk817_charge_field_write(charge, SYS_CAN_SD, DISABLE);
}

static int rk817_charge_get_charge_status(struct rk817_charger *charge)
{
	int status;

	status = rk817_charge_field_read(charge, CHG_STS);

	switch (status) {
	case CHRG_OFF:
		DBG("charge off...\n");
		break;
	case DEAD_CHRG:
		DBG("dead charge...\n");
		break;
	case TRICKLE_CHRG:
		DBG("trickle charge...\n");
		break;
	case CC_OR_CV_CHRG:
		DBG("CC or CV charge...\n");
		break;
	case CHRG_TERM:
		DBG("charge TERM...\n");
		break;
	case USB_OVER_VOL:
		DBG("USB over voltage...\n");
		break;
	case BAT_TMP_ERR:
		DBG("battery temperature error...\n");
		break;
	case BAT_TIM_ERR:
		DBG("battery timer error..\n");
		break;
	default:
		break;
	}

	return status;
}

static int rk817_charge_get_plug_in_status(struct rk817_charger *charge)
{
	return rk817_charge_field_read(charge, PLUG_IN_STS);
}

static void rk817_charge_set_charge_clock(struct rk817_charger *charge,
					  enum charge_clk clock)
{
	rk817_charge_field_write(charge, CHRG_CLK_SEL, clock);
}

static int is_battery_exist(struct rk817_charger *charge)
{
	return rk817_charge_field_read(charge, BAT_EXS);
}

static void rk817_charge_set_chrg_voltage(struct rk817_charger *charge,
					  int chrg_vol)
{
	int voltage;

	if (chrg_vol < 4100 || chrg_vol > 4500) {
		dev_err(charge->dev, "the charge voltage is error!\n");
	} else {
		voltage = (chrg_vol - 4100) / 50;
		rk817_charge_field_write(charge,
					 CHRG_VOL_SEL,
					 CHRG_VOL_4100MV + voltage);
	}
}

static void rk817_charge_set_chrg_current(struct rk817_charger *charge,
					  int chrg_current)
{
	if (chrg_current < 500 || chrg_current > 3500)
		dev_err(charge->dev, "the charge current is error!\n");

	if (chrg_current < 1000)
		rk817_charge_field_write(charge, CHRG_CUR_SEL, CHRG_CUR_500MA);
	else if (chrg_current < 1500)
		rk817_charge_field_write(charge, CHRG_CUR_SEL, CHRG_CUR_1000MA);
	else if (chrg_current < 2000)
		rk817_charge_field_write(charge, CHRG_CUR_SEL, CHRG_CUR_1500MA);
	else if (chrg_current < 2500)
		rk817_charge_field_write(charge, CHRG_CUR_SEL, CHRG_CUR_2000MA);
	else if (chrg_current < 3000)
		rk817_charge_field_write(charge, CHRG_CUR_SEL, CHRG_CUR_2500MA);
	else if (chrg_current < 3500)
		rk817_charge_field_write(charge, CHRG_CUR_SEL, CHRG_CUR_3000MA);
	else
		rk817_charge_field_write(charge, CHRG_CUR_SEL, CHRG_CUR_3500MA);
}

static void rk817_charge_vlimit_enable(struct rk817_charger *charge)
{
	rk817_charge_field_write(charge, USB_VLIM_EN, ENABLE);
}

static void rk817_charge_set_input_voltage(struct rk817_charger *charge,
					   int input_voltage)
{
	int voltage;

	if (input_voltage < 4000)
		dev_err(charge->dev, "the input voltage is error.\n");

	voltage = INPUT_VOL_4000MV + (input_voltage - 4000) / 100;

	rk817_charge_field_write(charge, USB_VLIM_SEL, voltage);
	rk817_charge_vlimit_enable(charge);
}

static void rk817_charge_ilimit_enable(struct rk817_charger *charge)
{
	rk817_charge_field_write(charge, USB_ILIM_EN, ENABLE);
}

static void rk817_charge_set_input_current(struct rk817_charger *charge,
					   int input_current)
{
	if (input_current < 80 || input_current > 3000)
		dev_err(charge->dev, "the input current is error.\n");

	if (input_current < 450)
		rk817_charge_field_write(charge, USB_ILIM_SEL,
					 INPUT_CUR_80MA);
	else if (input_current < 850)
		rk817_charge_field_write(charge, USB_ILIM_SEL,
					 INPUT_CUR_450MA);
	else if (input_current < 1500)
		rk817_charge_field_write(charge, USB_ILIM_SEL,
					 INPUT_CUR_850MA);
	else if (input_current < 1750)
		rk817_charge_field_write(charge, USB_ILIM_SEL,
					 INPUT_CUR_1500MA);
	else if (input_current < 2000)
		rk817_charge_field_write(charge, USB_ILIM_SEL,
					 INPUT_CUR_1750MA);
	else if (input_current < 2500)
		rk817_charge_field_write(charge, USB_ILIM_SEL,
					 INPUT_CUR_2000MA);
	else if (input_current < 3000)
		rk817_charge_field_write(charge, USB_ILIM_SEL,
					 INPUT_CUR_2500MA);
	else
		rk817_charge_field_write(charge, USB_ILIM_SEL,
					 INPUT_CUR_3000MA);

	rk817_charge_ilimit_enable(charge);
}

static void rk817_charge_set_chrg_term_mod(struct rk817_charger *charge,
					   int mode)
{
	rk817_charge_field_write(charge, CHRG_TERM_ANA_DIG, mode);
}

static void rk817_charge_set_term_current_analog(struct rk817_charger *charge,
						 int chrg_current)
{
	int value;

	if (chrg_current < 200)
		value = CHRG_TERM_150MA;
	else if (chrg_current < 300)
		value = CHRG_TERM_200MA;
	else if (chrg_current < 400)
		value = CHRG_TERM_300MA;
	else
		value = CHRG_TERM_400MA;

	rk817_charge_field_write(charge,
				 CHRG_TERM_ANA_SEL,
				 value);
}

static void rk817_charge_set_term_current_digital(struct rk817_charger *charge,
						  int chrg_current)
{
	int value;
	u8 current_adc;

	value = CURRENT_TO_ADC(chrg_current, charge->res_div);

	value &= (0xff << 5);
	current_adc = value >> 5;
	rk817_charge_field_write(charge, CHRG_TERM_DIG, current_adc);
}

static void rk817_charge_set_chrg_finish_condition(struct rk817_charger *charge)
{
	if (charge->chrg_term_mode == CHRG_ANALOG)
		rk817_charge_set_term_current_analog(charge,
						     charge->chrg_finish_cur);
	else
		rk817_charge_set_term_current_digital(charge,
						      charge->chrg_finish_cur);

	rk817_charge_set_chrg_term_mod(charge, charge->chrg_term_mode);
}

static int rk817_charge_online(struct rk817_charger *charge)
{
	return (charge->ac_in | charge->usb_in | charge->dc_in);
}

static int rk817_charge_get_dsoc(struct rk817_charger *charge)
{
	int soc_save;

	soc_save = rk817_charge_field_read(charge, SOC_REG0);
	soc_save |= (rk817_charge_field_read(charge, SOC_REG1) << 8);
	soc_save |= (rk817_charge_field_read(charge, SOC_REG2) << 16);

	return soc_save / 1000;
}

static void rk817_charge_set_otg_in(struct rk817_charger *charge, int online)
{
	charge->otg_in = online;
}

static void rk817_charge_set_chrg_param(struct rk817_charger *charge,
					enum charger_t charger)
{
	switch (charger) {
	case USB_TYPE_NONE_CHARGER:
		charge->usb_in = 0;
		charge->ac_in = 0;
		if (charge->dc_in == 0) {
			charge->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
			rk817_charge_set_input_current(charge, INPUT_450MA);
		}
		power_supply_changed(charge->usb_psy);
		power_supply_changed(charge->ac_psy);
		break;
	case USB_TYPE_USB_CHARGER:
		charge->usb_in = 1;
		charge->ac_in = 0;
		charge->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		if (charge->dc_in == 0)
			rk817_charge_set_input_current(charge, INPUT_450MA);
		power_supply_changed(charge->usb_psy);
		power_supply_changed(charge->ac_psy);
		break;
	case USB_TYPE_AC_CHARGER:
	case USB_TYPE_CDP_CHARGER:
		charge->ac_in = 1;
		charge->usb_in = 0;
		charge->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		if (charger == USB_TYPE_AC_CHARGER)
			rk817_charge_set_input_current(charge,
						       charge->max_input_current);
		else
			rk817_charge_set_input_current(charge,
						       INPUT_1500MA);
		power_supply_changed(charge->usb_psy);
		power_supply_changed(charge->ac_psy);
		break;
	case DC_TYPE_DC_CHARGER:
		charge->dc_in = 1;
		charge->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		rk817_charge_set_input_current(charge,
					       charge->max_input_current);
		power_supply_changed(charge->usb_psy);
		power_supply_changed(charge->ac_psy);
		break;
	case DC_TYPE_NONE_CHARGER:
		charge->dc_in = 0;
		if (!rk817_charge_get_plug_in_status(charge)) {
			charge->ac_in = 0;
			charge->usb_in = 0;
			charge->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
			rk817_charge_set_input_current(charge, INPUT_450MA);
		} else if (charge->usb_in) {
			rk817_charge_set_input_current(charge, INPUT_450MA);
			charge->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		}
		power_supply_changed(charge->usb_psy);
		power_supply_changed(charge->ac_psy);
		break;
	default:
		charge->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
		rk817_charge_set_input_current(charge, INPUT_450MA);
		break;
	}

	if (rk817_charge_online(charge) && rk817_charge_get_dsoc(charge) == 100)
		charge->prop_status = POWER_SUPPLY_STATUS_FULL;
}

static void rk817_charge_set_otg_state(struct rk817_charger *charge, int state)
{
	switch (state) {
	case USB_OTG_POWER_ON:
		if (charge->otg_in) {
			DBG("otg5v is on yet, ignore..\n");
		} else {

			if (!rk817_charge_get_otg_state(charge)) {
				rk817_charge_otg_enable(charge);
				if (!rk817_charge_get_otg_state(charge)) {
					DBG("enable otg5v failed\n");
					return;
				}
			}
			disable_irq(charge->plugin_irq);
			disable_irq(charge->plugout_irq);
			DBG("enable otg5v\n");
		}
		break;

	case USB_OTG_POWER_OFF:
		if (!charge->otg_in) {
			DBG("otg5v is off yet, ignore..\n");
		} else {

			if (rk817_charge_get_otg_state(charge)) {
				rk817_charge_otg_disable(charge);
				if (rk817_charge_get_otg_state(charge)) {
					DBG("disable otg5v failed\n");
					return;
				}
			}
			enable_irq(charge->plugin_irq);
			enable_irq(charge->plugout_irq);
			DBG("disable otg5v\n");
		}
		break;
	default:
		dev_err(charge->dev, "error otg type\n");
		break;
	}
}

static irqreturn_t rk817_charge_dc_det_isr(int irq, void *charger)
{
	struct rk817_charger *charge = (struct rk817_charger *)charger;

	if (gpio_get_value(charge->pdata->dc_det_pin))
		irq_set_irq_type(irq, IRQF_TRIGGER_LOW);
	else
		irq_set_irq_type(irq, IRQF_TRIGGER_HIGH);

	queue_delayed_work(charge->dc_charger_wq, &charge->dc_work,
			   msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

static enum charger_t rk817_charge_get_dc_state(struct rk817_charger *charge)
{
	int level;

	if (!gpio_is_valid(charge->pdata->dc_det_pin))
		return DC_TYPE_NONE_CHARGER;

	level = gpio_get_value(charge->pdata->dc_det_pin);

	return (level == charge->pdata->dc_det_level) ?
		DC_TYPE_DC_CHARGER : DC_TYPE_NONE_CHARGER;
}

static void rk817_charge_dc_det_worker(struct work_struct *work)
{
	enum charger_t charger;
	struct rk817_charger *charge = container_of(work,
			struct rk817_charger, dc_work.work);

	charger = rk817_charge_get_dc_state(charge);
	if (charger == DC_TYPE_DC_CHARGER) {
		DBG("detect dc charger in..\n");
		rk817_charge_set_chrg_param(charge, DC_TYPE_DC_CHARGER);
		/* check otg supply */
		if (charge->otg_in && charge->pdata->power_dc2otg) {
			DBG("otg power from dc adapter\n");
			rk817_charge_set_otg_state(charge, USB_OTG_POWER_OFF);
		}

		rk817_charge_boost_disable(charge);
	} else {
		DBG("detect dc charger out..\n");
		rk817_charge_set_chrg_param(charge, DC_TYPE_NONE_CHARGER);
		rk817_charge_boost_enable(charge);
		/* check otg supply, power on anyway */
		if (charge->otg_in)
			rk817_charge_set_otg_state(charge, USB_OTG_POWER_ON);
	}
}

static int rk817_charge_init_dc(struct rk817_charger *charge)
{
	int ret, level;
	unsigned long irq_flags;
	unsigned int dc_det_irq;

	charge->dc_charger_wq = alloc_ordered_workqueue("%s",
				WQ_MEM_RECLAIM | WQ_FREEZABLE,
				"rk817-dc-wq");
	INIT_DELAYED_WORK(&charge->dc_work, rk817_charge_dc_det_worker);
	charge->dc_charger = DC_TYPE_NONE_CHARGER;

	if (!charge->pdata->support_dc_det)
		return 0;

	ret = devm_gpio_request(charge->dev,
				charge->pdata->dc_det_pin,
				"rk817_dc_det");
	if (ret < 0) {
		dev_err(charge->dev, "failed to request gpio %d\n",
			charge->pdata->dc_det_pin);
		return ret;
	}

	ret = gpio_direction_input(charge->pdata->dc_det_pin);
	if (ret) {
		dev_err(charge->dev, "failed to set gpio input\n");
		return ret;
	}

	level = gpio_get_value(charge->pdata->dc_det_pin);
	if (level == charge->pdata->dc_det_level)
		charge->dc_charger = DC_TYPE_DC_CHARGER;
	else
		charge->dc_charger = DC_TYPE_NONE_CHARGER;

	if (level)
		irq_flags = IRQF_TRIGGER_LOW;
	else
		irq_flags = IRQF_TRIGGER_HIGH;

	dc_det_irq = gpio_to_irq(charge->pdata->dc_det_pin);
	ret = devm_request_irq(charge->dev, dc_det_irq, rk817_charge_dc_det_isr,
			       irq_flags, "rk817_dc_det", charge);
	if (ret != 0) {
		dev_err(charge->dev, "rk817_dc_det_irq request failed!\n");
		return ret;
	}

	enable_irq_wake(dc_det_irq);

	if (charge->dc_charger != DC_TYPE_NONE_CHARGER)
		rk817_charge_set_chrg_param(charge, charge->dc_charger);

	return 0;
}

static void rk817_charge_host_evt_worker(struct work_struct *work)
{
	struct rk817_charger *charge = container_of(work,
			struct rk817_charger, host_work.work);
	struct extcon_dev *edev = charge->cable_edev;

	/* Determine cable/charger type */
	if (extcon_get_state(edev, EXTCON_USB_VBUS_EN) > 0) {
		DBG("receive type-c notifier event: OTG ON...\n");
		if (charge->dc_in && charge->pdata->power_dc2otg) {
			if (charge->otg_in)
				rk817_charge_set_otg_state(charge,
							   USB_OTG_POWER_OFF);
			DBG("otg power from dc adapter\n");
		} else {
			rk817_charge_set_otg_state(charge, USB_OTG_POWER_ON);
		}
		rk817_charge_set_otg_in(charge, ONLINE);
	} else if (extcon_get_state(edev, EXTCON_USB_VBUS_EN) == 0) {
		DBG("receive type-c notifier event: OTG OFF...\n");
		rk817_charge_set_otg_state(charge, USB_OTG_POWER_OFF);
		rk817_charge_set_otg_in(charge, OFFLINE);
	}
}

static void rk817_charger_evt_worker(struct work_struct *work)
{
	struct rk817_charger *charge = container_of(work,
				struct rk817_charger, usb_work.work);
	struct extcon_dev *edev = charge->cable_edev;
	enum charger_t charger = USB_TYPE_UNKNOWN_CHARGER;
	static const char * const event[] = {"UN", "NONE", "USB",
					     "AC", "CDP1.5A"};

	/* Determine cable/charger type */
	if (extcon_get_state(edev, EXTCON_CHG_USB_SDP) > 0)
		charger = USB_TYPE_USB_CHARGER;
	else if (extcon_get_state(edev, EXTCON_CHG_USB_DCP) > 0)
		charger = USB_TYPE_AC_CHARGER;
	else if (extcon_get_state(edev, EXTCON_CHG_USB_CDP) > 0)
		charger = USB_TYPE_CDP_CHARGER;

	if (charger != USB_TYPE_UNKNOWN_CHARGER) {
		DBG("receive type-c notifier event: %s...\n",
		    event[charger]);
		charge->usb_charger = charger;
		rk817_charge_set_chrg_param(charge, charger);
	}
}

static void rk817_charge_discnt_evt_worker(struct work_struct *work)
{
	struct rk817_charger *charge = container_of(work,
			struct rk817_charger, discnt_work.work);

	if (extcon_get_state(charge->cable_edev, EXTCON_USB) == 0) {
		DBG("receive type-c notifier event: DISCNT...\n");

		rk817_charge_set_chrg_param(charge, USB_TYPE_NONE_CHARGER);
	}
}

static void rk817_charge_bc_evt_worker(struct work_struct *work)
{
	struct rk817_charger *charge = container_of(work,
						    struct rk817_charger,
						    usb_work.work);
	static const char * const event_name[] = {"DISCNT", "USB", "AC",
						  "CDP1.5A", "UNKNOWN",
						  "OTG ON", "OTG OFF"};

	switch (charge->bc_event) {
	case USB_BC_TYPE_DISCNT:
		rk817_charge_set_chrg_param(charge, USB_TYPE_NONE_CHARGER);
		break;
	case USB_BC_TYPE_SDP:
		rk817_charge_set_chrg_param(charge, USB_TYPE_USB_CHARGER);
		break;
	case USB_BC_TYPE_DCP:
		rk817_charge_set_chrg_param(charge, USB_TYPE_AC_CHARGER);
		break;
	case USB_BC_TYPE_CDP:
		rk817_charge_set_chrg_param(charge, USB_TYPE_CDP_CHARGER);
		break;
	case USB_OTG_POWER_ON:
		if (charge->pdata->power_dc2otg && charge->dc_in)
			DBG("otg power from dc adapter\n");
		else
			rk817_charge_set_otg_state(charge, USB_OTG_POWER_ON);
		break;
	case USB_OTG_POWER_OFF:
		rk817_charge_set_otg_state(charge, USB_OTG_POWER_OFF);
		break;
	default:
		break;
	}

	DBG("receive bc notifier event: %s..\n", event_name[charge->bc_event]);
}

static int rk817_charger_evt_notifier(struct notifier_block *nb,
				      unsigned long event, void *ptr)
{
	struct rk817_charger *charge =
		container_of(nb, struct rk817_charger, cable_cg_nb);

	queue_delayed_work(charge->usb_charger_wq, &charge->usb_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static int rk817_charge_host_evt_notifier(struct notifier_block *nb,
					  unsigned long event, void *ptr)
{
	struct rk817_charger *charge =
		container_of(nb, struct rk817_charger, cable_host_nb);

	queue_delayed_work(charge->usb_charger_wq, &charge->host_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static int rk817_charge_discnt_evt_notfier(struct notifier_block *nb,
					   unsigned long event, void *ptr)
{
	struct rk817_charger *charge =
		container_of(nb, struct rk817_charger, cable_discnt_nb);

	queue_delayed_work(charge->usb_charger_wq, &charge->discnt_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static int rk817_charge_bc_evt_notifier(struct notifier_block *nb,
					unsigned long event, void *ptr)
{
	struct rk817_charger *charge =
		container_of(nb, struct rk817_charger, bc_nb);

	charge->bc_event = event;
	queue_delayed_work(charge->usb_charger_wq, &charge->usb_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static int rk817_charge_usb_init(struct rk817_charger *charge)
{
	enum charger_t charger;
	enum bc_port_type bc_type;
	struct extcon_dev *edev;
	struct device *dev = charge->dev;
	int ret;

	charge->usb_charger_wq = alloc_ordered_workqueue("%s",
				WQ_MEM_RECLAIM | WQ_FREEZABLE,
				"rk817-usb-wq");

	/* type-C */
	if (charge->pdata->extcon) {
		edev = extcon_get_edev_by_phandle(dev, 0);
		if (IS_ERR(edev)) {
			if (PTR_ERR(edev) != -EPROBE_DEFER)
				dev_err(dev, "Invalid or missing extcon\n");
			return PTR_ERR(edev);
		}

		/* Register chargers  */
		INIT_DELAYED_WORK(&charge->usb_work, rk817_charger_evt_worker);
		charge->cable_cg_nb.notifier_call = rk817_charger_evt_notifier;
		ret = extcon_register_notifier(edev, EXTCON_CHG_USB_SDP,
					       &charge->cable_cg_nb);
		if (ret < 0) {
			dev_err(dev, "failed to register notifier for SDP\n");
			return ret;
		}

		ret = extcon_register_notifier(edev, EXTCON_CHG_USB_DCP,
					       &charge->cable_cg_nb);
		if (ret < 0) {
			dev_err(dev, "failed to register notifier for DCP\n");
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_SDP,
						   &charge->cable_cg_nb);
			return ret;
		}

		ret = extcon_register_notifier(edev, EXTCON_CHG_USB_CDP,
					       &charge->cable_cg_nb);
		if (ret < 0) {
			dev_err(dev, "failed to register notifier for CDP\n");
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_SDP,
						   &charge->cable_cg_nb);
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_DCP,
						   &charge->cable_cg_nb);
			return ret;
		}

		/* Register host */
		INIT_DELAYED_WORK(&charge->host_work,
				  rk817_charge_host_evt_worker);
		charge->cable_host_nb.notifier_call =
			rk817_charge_host_evt_notifier;
		ret = extcon_register_notifier(edev, EXTCON_USB_VBUS_EN,
					       &charge->cable_host_nb);
		if (ret < 0) {
			dev_err(dev, "failed to register notifier for HOST\n");
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_SDP,
						   &charge->cable_cg_nb);
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_DCP,
						   &charge->cable_cg_nb);
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_CDP,
						   &charge->cable_cg_nb);

			return ret;
		}

		/* Register discnt usb */
		INIT_DELAYED_WORK(&charge->discnt_work,
				  rk817_charge_discnt_evt_worker);
		charge->cable_discnt_nb.notifier_call =
			rk817_charge_discnt_evt_notfier;
		ret = extcon_register_notifier(edev, EXTCON_USB,
					       &charge->cable_discnt_nb);
		if (ret < 0) {
			dev_err(dev, "failed to register notifier for HOST\n");
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_SDP,
						   &charge->cable_cg_nb);
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_DCP,
						   &charge->cable_cg_nb);
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_CDP,
						   &charge->cable_cg_nb);
			extcon_unregister_notifier(edev, EXTCON_USB_VBUS_EN,
						   &charge->cable_host_nb);
			return ret;
		}

		charge->cable_edev = edev;

		DBG("register typec extcon evt notifier\n");
	} else {
		INIT_DELAYED_WORK(&charge->usb_work,
				  rk817_charge_bc_evt_worker);
		charge->bc_nb.notifier_call = rk817_charge_bc_evt_notifier;
		ret = rk_bc_detect_notifier_register(&charge->bc_nb, &bc_type);
		if (ret) {
			dev_err(dev, "failed to register notifier for bc\n");
			return -EINVAL;
	}

		switch (bc_type) {
		case USB_BC_TYPE_DISCNT:
			charger = USB_TYPE_NONE_CHARGER;
			break;
		case USB_BC_TYPE_SDP:
		case USB_BC_TYPE_CDP:
			charger = USB_TYPE_USB_CHARGER;
			break;
		case USB_BC_TYPE_DCP:
			charger = USB_TYPE_AC_CHARGER;
			break;
		default:
			charger = USB_TYPE_NONE_CHARGER;
			break;
		}

		charge->usb_charger = charger;
		if (charge->dc_charger != DC_TYPE_NONE_CHARGER)
			rk817_charge_set_chrg_param(charge,
						    charge->usb_charger);

		DBG("register bc evt notifier\n");
	}

	return 0;
}

static void rk817_charge_pre_init(struct rk817_charger *charge)
{
	charge->max_chrg_current = charge->pdata->max_chrg_current;
	charge->max_input_current = charge->pdata->max_input_current;
	charge->max_chrg_voltage = charge->pdata->max_chrg_voltage;
	charge->min_input_voltage = charge->pdata->min_input_voltage;
	charge->chrg_finish_cur = charge->pdata->chrg_finish_cur;
	charge->chrg_term_mode = charge->pdata->chrg_term_mode;

	rk817_charge_set_input_voltage(charge, charge->min_input_voltage);

	rk817_charge_set_chrg_voltage(charge, charge->max_chrg_voltage);
	rk817_charge_set_chrg_current(charge, charge->max_chrg_current);

	rk817_charge_set_chrg_finish_condition(charge);

	if (rk817_charge_get_otg_state(charge))
		rk817_charge_otg_disable(charge);
	rk817_charge_field_write(charge, OTG_EN, OTG_DISABLE);
	rk817_charge_set_otg_in(charge, OFFLINE);

	if (!charge->pdata->gate_function_disable)
		rk817_charge_sys_can_sd_disable(charge);
	rk817_charge_usb_to_sys_enable(charge);
	rk817_charge_enable_charge(charge);

	rk817_charge_set_charge_clock(charge, CHRG_CLK_2M);
}

static void rk817_chage_debug(struct rk817_charger *charge)
{
	rk817_charge_get_charge_status(charge);
	DBG("OTG state : %d\n", rk817_charge_get_otg_state(charge));
	DBG("charge state: %d\n", rk817_charge_get_charge_state(charge));
	DBG("max_chrg_current: %d\n"
	    "max_input_current: %d\n"
	    "min_input_voltage: %d\n"
	    "max_chrg_voltage: %d\n"
	    "max_chrg_finish_cur: %d\n"
	    "chrg_term_mode: %d\n",
	    charge->max_chrg_current,
	    charge->max_input_current,
	    charge->min_input_voltage,
	    charge->max_chrg_voltage,
	    charge->chrg_finish_cur,
	    charge->chrg_term_mode);
}

static int rk817_charge_get_otg5v_regulator(struct rk817_charger *charge)
{
	int ret;

	charge->otg5v_rdev = devm_regulator_get(charge->dev, "otg_switch");
	if (IS_ERR(charge->otg5v_rdev)) {
		ret = PTR_ERR(charge->otg5v_rdev);
		dev_warn(charge->dev, "failed to get otg regulator: %d\n", ret);
	}

	return 0;
}

#ifdef CONFIG_OF
static int rk817_charge_parse_dt(struct rk817_charger *charge)
{
	struct charger_platform_data *pdata;
	enum of_gpio_flags flags;
	struct device *dev = charge->dev;
	struct device_node *np = charge->dev->of_node;
	int ret;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	charge->pdata = pdata;
	pdata->max_chrg_current = DEFAULT_CHRG_CURRENT;
	pdata->max_input_current = DEFAULT_INPUT_CURRENT;
	pdata->max_chrg_voltage = DEFAULT_CHRG_VOLTAGE;
	pdata->min_input_voltage = DEFAULT_INPUT_VOLTAGE;
	pdata->chrg_finish_cur = DEFAULT_CHRG_TERM_CUR;
	pdata->chrg_term_mode = DEFAULT_CHRG_TERM_MODE;

	pdata->extcon = of_property_read_bool(np, "extcon");

	ret = of_property_read_u32(np, "max_chrg_current",
				   &pdata->max_chrg_current);
	if (ret < 0)
		dev_err(dev, "max_chrg_current missing!\n");

	ret = of_property_read_u32(np, "max_input_current",
				   &pdata->max_input_current);
	if (ret < 0)
		dev_err(dev, "max_input_current missing!\n");

	ret = of_property_read_u32(np, "max_chrg_voltage",
				   &pdata->max_chrg_voltage);
	if (ret < 0)
		dev_err(dev, "max_chrg_voltage missing!\n");

	ret = of_property_read_u32(np, "min_input_voltage",
				   &pdata->min_input_voltage);
	if (ret < 0)
		dev_WARN(dev, "min_input_voltage missing!\n");

	ret = of_property_read_u32(np, "chrg_finish_cur",
				   &pdata->chrg_finish_cur);

	if (ret < 0)
		dev_WARN(dev, "chrg_term_mode missing!\n");

	ret = of_property_read_u32(np, "chrg_term_mode",
				   &pdata->chrg_term_mode);
	if (ret < 0)
		dev_WARN(dev, "chrg_term_mode missing!\n");

	ret = of_property_read_u32(np, "virtual_power", &pdata->virtual_power);
	if (ret < 0)
		dev_err(dev, "virtual_power missing!\n");

	ret = of_property_read_u32(np, "power_dc2otg", &pdata->power_dc2otg);
	if (ret < 0)
		dev_err(dev, "power_dc2otg missing!\n");

	ret = of_property_read_u32(np, "sample_res", &pdata->sample_res);
	if (ret < 0) {
		pdata->sample_res = SAMPLE_RES_10MR;
		dev_err(dev, "sample_res missing!\n");
	}

	ret = of_property_read_u32(np, "otg5v_suspend_enable",
				   &pdata->otg5v_suspend_enable);
	if (ret < 0) {
		pdata->otg5v_suspend_enable = 1;
		dev_err(dev, "otg5v_suspend_enable missing!\n");
	}

	ret = of_property_read_u32(np, "gate_function_disable",
				   &pdata->gate_function_disable);
	if (ret < 0)
		dev_err(dev, "gate_function_disable missing!\n");

	if (!is_battery_exist(charge))
		pdata->virtual_power = 1;

	charge->res_div = (charge->pdata->sample_res == SAMPLE_RES_10MR) ?
		       SAMPLE_RES_DIV1 : SAMPLE_RES_DIV2;

	if (!of_find_property(np, "dc_det_gpio", &ret)) {
		pdata->support_dc_det = false;
		DBG("not support dc\n");
	} else {
		pdata->support_dc_det = true;
		pdata->dc_det_pin = of_get_named_gpio_flags(np, "dc_det_gpio",
							    0, &flags);
		if (gpio_is_valid(pdata->dc_det_pin)) {
			DBG("support dc\n");
			pdata->dc_det_level = (flags & OF_GPIO_ACTIVE_LOW) ?
					       0 : 1;
		} else {
			dev_err(dev, "invalid dc det gpio!\n");
			return -EINVAL;
		}
	}

	DBG("input_current:%d\n"
		"input_min_voltage: %d\n"
	    "chrg_current:%d\n"
	    "chrg_voltage:%d\n"
	    "sample_res:%d\n"
	    "extcon:%d\n"
	    "virtual_power:%d\n"
	    "power_dc2otg:%d\n",
	    pdata->max_input_current, pdata->min_input_voltage,
	    pdata->max_chrg_current,  pdata->max_chrg_voltage,
	    pdata->sample_res, pdata->extcon,
	    pdata->virtual_power, pdata->power_dc2otg);

	return 0;
}
#else
static int rk817_charge_parse_dt(struct rk817_charger *charge)
{
	return -ENODEV;
}
#endif

static void rk817_charge_irq_delay_work(struct work_struct *work)
{
	struct rk817_charger *charge = container_of(work,
			struct rk817_charger, irq_work.work);

	if (charge->plugin_trigger) {
		DBG("pmic: plug in\n");
		charge->plugin_trigger = 0;
		if (charge->pdata->extcon)
			queue_delayed_work(charge->usb_charger_wq, &charge->usb_work,
					   msecs_to_jiffies(10));
	} else if (charge->plugout_trigger) {
		DBG("pmic: plug out\n");
		charge->plugout_trigger = 0;
		rk817_charge_set_chrg_param(charge, USB_TYPE_NONE_CHARGER);
		rk817_charge_set_chrg_param(charge, DC_TYPE_NONE_CHARGER);
	} else {
		DBG("pmic: unknown irq\n");
	}
}

static irqreturn_t rk817_plug_in_isr(int irq, void *cg)
{
	struct rk817_charger *charge;

	charge = (struct rk817_charger *)cg;
	charge->plugin_trigger = 1;
	queue_delayed_work(charge->usb_charger_wq, &charge->irq_work,
			   msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

static irqreturn_t rk817_plug_out_isr(int irq, void *cg)
{
	struct rk817_charger *charge;

	charge = (struct rk817_charger *)cg;
	charge->plugout_trigger = 1;
	queue_delayed_work(charge->usb_charger_wq, &charge->irq_work,
			   msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

static int rk817_charge_init_irqs(struct rk817_charger *charge)
{
	struct rk808 *rk817 = charge->rk817;
	struct platform_device *pdev = charge->pdev;
	int ret, plug_in_irq, plug_out_irq;

	plug_in_irq = regmap_irq_get_virq(rk817->irq_data, RK817_IRQ_PLUG_IN);
	if (plug_in_irq < 0) {
		dev_err(charge->dev, "plug_in_irq request failed!\n");
		return plug_in_irq;
	}

	plug_out_irq = regmap_irq_get_virq(rk817->irq_data, RK817_IRQ_PLUG_OUT);
	if (plug_out_irq < 0) {
		dev_err(charge->dev, "plug_out_irq request failed!\n");
		return plug_out_irq;
	}

	ret = devm_request_threaded_irq(charge->dev, plug_in_irq, NULL,
					rk817_plug_in_isr,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"rk817_plug_in", charge);
	if (ret) {
		dev_err(&pdev->dev, "plug_in_irq request failed!\n");
		return ret;
	}

	ret = devm_request_threaded_irq(charge->dev, plug_out_irq, NULL,
					rk817_plug_out_isr,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"rk817_plug_out", charge);
	if (ret) {
		dev_err(&pdev->dev, "plug_out_irq request failed!\n");
		return ret;
	}

	charge->plugin_irq = plug_in_irq;
	charge->plugout_irq = plug_out_irq;

	INIT_DELAYED_WORK(&charge->irq_work, rk817_charge_irq_delay_work);

	return 0;
}

static const struct of_device_id rk817_charge_of_match[] = {
	{ .compatible = "rk817,charger", },
	{ },
};

static int rk817_charge_probe(struct platform_device *pdev)
{
	struct rk808 *rk817 = dev_get_drvdata(pdev->dev.parent);
	const struct of_device_id *of_id =
			of_match_device(rk817_charge_of_match, &pdev->dev);
	struct i2c_client *client = rk817->i2c;
	struct rk817_charger *charge;
	int i;
	int ret;

	if (!of_id) {
		dev_err(&pdev->dev, "Failed to find matching dt id\n");
		return -ENODEV;
	}

	charge = devm_kzalloc(&pdev->dev, sizeof(*charge), GFP_KERNEL);
	if (!charge)
		return -EINVAL;

	charge->rk817 = rk817;
	charge->pdev = pdev;
	charge->dev = &pdev->dev;
	charge->client = client;
	platform_set_drvdata(pdev, charge);

	charge->regmap = rk817->regmap;
	if (IS_ERR(charge->regmap)) {
		dev_err(charge->dev, "Failed to initialize regmap\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(rk817_charge_reg_fields); i++) {
		const struct reg_field *reg_fields = rk817_charge_reg_fields;

		charge->rmap_fields[i] =
			devm_regmap_field_alloc(charge->dev,
						charge->regmap,
						reg_fields[i]);
		if (IS_ERR(charge->rmap_fields[i])) {
			dev_err(charge->dev, "cannot allocate regmap field\n");
			return PTR_ERR(charge->rmap_fields[i]);
		}
	}

	ret = rk817_charge_parse_dt(charge);
	if (ret < 0) {
		dev_err(charge->dev, "charge parse dt failed!\n");
		return ret;
	}
	rk817_charge_get_otg5v_regulator(charge);

	rk817_charge_pre_init(charge);

	ret = rk817_charge_init_power_supply(charge);
	if (ret) {
		dev_err(charge->dev, "init power supply fail!\n");
		return ret;
	}

	ret = rk817_charge_init_dc(charge);
	if (ret) {
		dev_err(charge->dev, "init dc failed!\n");
		return ret;
	}

	ret = rk817_charge_usb_init(charge);
	if (ret) {
		dev_err(charge->dev, "init usb failed!\n");
		return ret;
	}

	ret = rk817_charge_init_irqs(charge);
	if (ret) {
		dev_err(charge->dev, "init irqs failed!\n");
		goto irq_fail;
	}

	if (charge->pdata->extcon) {
		schedule_delayed_work(&charge->host_work, 0);
		schedule_delayed_work(&charge->usb_work, 0);
	}

	rk817_chage_debug(charge);
	DBG("driver version: %s\n", CHARGE_DRIVER_VERSION);

	return 0;
irq_fail:
	if (charge->pdata->extcon) {
		cancel_delayed_work_sync(&charge->host_work);
		cancel_delayed_work_sync(&charge->discnt_work);
	}

	cancel_delayed_work_sync(&charge->usb_work);
	cancel_delayed_work_sync(&charge->dc_work);
	cancel_delayed_work_sync(&charge->irq_work);
	destroy_workqueue(charge->usb_charger_wq);
	destroy_workqueue(charge->dc_charger_wq);

	if (charge->pdata->extcon) {
		extcon_unregister_notifier(charge->cable_edev,
					   EXTCON_CHG_USB_SDP,
					   &charge->cable_cg_nb);
		extcon_unregister_notifier(charge->cable_edev,
					   EXTCON_CHG_USB_DCP,
					   &charge->cable_cg_nb);
		extcon_unregister_notifier(charge->cable_edev,
					   EXTCON_CHG_USB_CDP,
					   &charge->cable_cg_nb);
		extcon_unregister_notifier(charge->cable_edev,
					   EXTCON_USB_VBUS_EN,
					   &charge->cable_host_nb);
		extcon_unregister_notifier(charge->cable_edev,
					   EXTCON_USB,
					   &charge->cable_discnt_nb);
	} else {
		rk_bc_detect_notifier_unregister(&charge->bc_nb);
	}

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int  rk817_charge_pm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk817_charger *charge = dev_get_drvdata(&pdev->dev);

	charge->otg_slp_state = rk817_charge_get_otg_slp_state(charge);

	/* enable sleep boost5v and otg5v */
	if (charge->pdata->otg5v_suspend_enable) {
		if ((charge->otg_in && !charge->dc_in) ||
		    (charge->otg_in && charge->dc_in &&
		    !charge->pdata->power_dc2otg)) {
			rk817_charge_otg_slp_enable(charge);
			DBG("suspend: otg 5v on\n");
			return 0;
		}
	}

	/* disable sleep otg5v */
	rk817_charge_otg_slp_disable(charge);
	DBG("suspend: otg 5v off\n");
	return 0;
}

static int rk817_charge_pm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk817_charger *charge = dev_get_drvdata(&pdev->dev);

	/* resume sleep boost5v and otg5v */
	if (charge->otg_slp_state)
		rk817_charge_otg_slp_enable(charge);

	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(rk817_charge_pm_ops,
	rk817_charge_pm_suspend, rk817_charge_pm_resume);

static void rk817_charger_shutdown(struct platform_device *dev)
{
	struct rk817_charger *charge =  platform_get_drvdata(dev);

	/* type-c only */
	if (charge->pdata->extcon) {
		cancel_delayed_work_sync(&charge->host_work);
		cancel_delayed_work_sync(&charge->discnt_work);
	}

	rk817_charge_set_otg_state(charge, USB_OTG_POWER_OFF);
	rk817_charge_boost_disable(charge);
	disable_irq(charge->plugin_irq);
	disable_irq(charge->plugout_irq);

	cancel_delayed_work_sync(&charge->usb_work);
	cancel_delayed_work_sync(&charge->dc_work);
	cancel_delayed_work_sync(&charge->irq_work);
	flush_workqueue(charge->usb_charger_wq);
	flush_workqueue(charge->dc_charger_wq);

	if (charge->pdata->extcon) {
		extcon_unregister_notifier(charge->cable_edev,
					   EXTCON_CHG_USB_SDP,
					   &charge->cable_cg_nb);
		extcon_unregister_notifier(charge->cable_edev,
					   EXTCON_CHG_USB_DCP,
					   &charge->cable_cg_nb);
		extcon_unregister_notifier(charge->cable_edev,
					   EXTCON_CHG_USB_CDP,
					   &charge->cable_cg_nb);
		extcon_unregister_notifier(charge->cable_edev,
					   EXTCON_USB_VBUS_EN,
					   &charge->cable_host_nb);
		extcon_unregister_notifier(charge->cable_edev, EXTCON_USB,
					   &charge->cable_discnt_nb);
	} else {
		rk_bc_detect_notifier_unregister(&charge->bc_nb);
	}

	DBG("shutdown: ac=%d usb=%d dc=%d otg=%d\n",
	    charge->ac_in, charge->usb_in, charge->dc_in, charge->otg_in);
}

static struct platform_driver rk817_charge_driver = {
	.probe = rk817_charge_probe,
	.shutdown = rk817_charger_shutdown,
	.driver = {
		.name = "rk817-charger",
		.pm = &rk817_charge_pm_ops,
		.of_match_table = of_match_ptr(rk817_charge_of_match),
	},
};

static int __init rk817_charge_init(void)
{
	return platform_driver_register(&rk817_charge_driver);
}
fs_initcall_sync(rk817_charge_init);

static void __exit rk817_charge_exit(void)
{
	platform_driver_unregister(&rk817_charge_driver);
}
module_exit(rk817_charge_exit);

MODULE_DESCRIPTION("RK817 Charge driver");
MODULE_LICENSE("GPL");
