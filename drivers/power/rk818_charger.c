/*
 * rk818 charger driver
 *
 * Copyright (C) 2016 Rockchip Electronics Co., Ltd
 * chenjh <chenjh@rock-chips.com>
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
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mfd/rk808.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/power/rk_usbbc.h>
#include <linux/regmap.h>
#include <linux/rk_keys.h>
#include <linux/rtc.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include "rk818_battery.h"

static int dbg_enable = 0;
module_param_named(dbg_level, dbg_enable, int, 0644);

#define DBG(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)

#define CG_INFO(fmt, args...) pr_info("rk818-charger: "fmt, ##args)

#define DEFAULT_CHRG_CURRENT	1400
#define DEFAULT_INPUT_CURRENT	2000
#define DEFAULT_CHRG_VOLTAGE	4200
#define SAMPLE_RES_10MR		10
#define SAMPLE_RES_20MR		20
#define SAMPLE_RES_DIV1		1
#define SAMPLE_RES_DIV2		2

/* RK818_USB_CTRL_REG */
#define INPUT_CUR450MA		(0x00)
#define INPUT_CUR80MA		(0x01)
#define INPUT_CUR850MA		(0x02)
#define INPUT_CUR1500MA		(0x05)
#define INPUT_CUR_MSK		(0x0f)
/* RK818_CHRG_CTRL_REG3 */
#define CHRG_FINISH_MODE_MSK	BIT(5)
#define CHRG_FINISH_ANA_SIGNAL	(0)
#define CHRG_FINISH_DIG_SIGNAL	BIT(5)
/* RK818_SUP_STS_REG */
#define BAT_EXS			BIT(7)
#define USB_VLIMIT_EN		BIT(3)
#define USB_CLIMIT_EN		BIT(2)
/* RK818_CHRG_CTRL_REG1 */
#define CHRG_EN			BIT(7)
#define CHRG_CUR_MSK		(0x0f)
/* RK818_INT_STS_MSK_REG2 */
#define CHRG_CVTLMT_INT_MSK	BIT(6)
#define PLUG_OUT_MSK		BIT(1)
#define PLUG_IN_MSK		BIT(0)
/* RK818_VB_MON_REG */
#define PLUG_IN_STS		BIT(6)
/* RK818_TS_CTRL_REG */
#define GG_EN			BIT(7)
#define TS2_FUN_ADC		BIT(5)
/* RK818_ADC_CTRL_REG */
#define ADC_TS2_EN		BIT(4)

#define CG_DRIVER_VERSION		"2.0"

#define DEFAULT_TS2_THRESHOLD_VOL      4350
#define DEFAULT_TS2_VALID_VOL          1000
#define DEFAULT_TS2_VOL_MULTI          0
#define DEFAULT_TS2_CHECK_CNT          5

enum charger_t {
	USB_TYPE_UNKNOWN_CHARGER,
	USB_TYPE_NONE_CHARGER,
	USB_TYPE_USB_CHARGER,
	USB_TYPE_AC_CHARGER,
	USB_TYPE_CDP_CHARGER,
	DC_TYPE_DC_CHARGER,
	DC_TYPE_NONE_CHARGER,
};

struct temp_chrg_table {
	int temperature;
	u32 chrg_current;
	u32 offset;
	u8 set_chrg_current;
};

struct charger_platform_data {
	u32 max_input_current;
	u32 max_chrg_current;
	u32 max_chrg_voltage;
	u32 pwroff_vol;
	u32 power_dc2otg;
	u32 dc_det_level;
	int dc_det_pin;
	bool support_dc_det;
	int virtual_power;
	int sample_res;
	int otg5v_suspend_enable;
	bool extcon;
	int ts2_vol_multi;
	struct temp_chrg_table *tc_table;
	u32 tc_count;
};

struct rk818_charger {
	struct platform_device *pdev;
	struct device *dev;
	struct rk808 *rk818;
	struct regmap *regmap;
	struct power_supply *ac_psy;
	struct power_supply *usb_psy;
	struct power_supply *bat_psy;
	struct extcon_dev *cable_edev;
	struct charger_platform_data *pdata;
	struct workqueue_struct *usb_charger_wq;
	struct workqueue_struct *dc_charger_wq;
	struct workqueue_struct *finish_sig_wq;
	struct workqueue_struct *ts2_wq;
	struct delayed_work dc_work;
	struct delayed_work usb_work;
	struct delayed_work host_work;
	struct delayed_work discnt_work;
	struct delayed_work finish_sig_work;
	struct delayed_work irq_work;
	struct delayed_work ts2_vol_work;
	struct notifier_block bc_nb;
	struct notifier_block cable_cg_nb;
	struct notifier_block cable_host_nb;
	struct notifier_block cable_discnt_nb;
	struct notifier_block temp_nb;
	unsigned int bc_event;
	enum charger_t usb_charger;
	enum charger_t dc_charger;
	struct regulator *otg5v_rdev;
	u8 ac_in;
	u8 usb_in;
	u8 otg_in;
	u8 dc_in;
	u8 prop_status;
	u8 chrg_voltage;
	u8 chrg_input;
	u8 chrg_current;
	u8 res_div;
	u8 sleep_set_off_reg1;
	u8 plugin_trigger;
	u8 plugout_trigger;
	int plugin_irq;
	int plugout_irq;
	int charger_changed;
};

static int rk818_reg_read(struct rk818_charger *cg, u8 reg)
{
	int ret, val;

	ret = regmap_read(cg->regmap, reg, &val);
	if (ret)
		dev_err(cg->dev, "i2c read reg: 0x%2x failed\n", reg);

	return val;
}

static int rk818_reg_write(struct rk818_charger *cg, u8 reg, u8 buf)
{
	int ret;

	ret = regmap_write(cg->regmap, reg, buf);
	if (ret)
		dev_err(cg->dev, "i2c write reg: 0x%2x failed\n", reg);

	return ret;
}

static int rk818_reg_set_bits(struct rk818_charger *cg, u8 reg, u8 mask, u8 buf)
{
	int ret;

	ret = regmap_update_bits(cg->regmap, reg, mask, buf);
	if (ret)
		dev_err(cg->dev, "i2c set reg: 0x%2x failed\n", reg);

	return ret;
}

static int rk818_reg_clear_bits(struct rk818_charger *cg, u8 reg, u8 mask)
{
	int ret;

	ret = regmap_update_bits(cg->regmap, reg, mask, 0);
	if (ret)
		dev_err(cg->dev, "i2c clr reg: 0x%02x failed\n", reg);

	return ret;
}

static int rk818_cg_online(struct rk818_charger *cg)
{
	return (cg->ac_in | cg->usb_in | cg->dc_in);
}

static int rk818_cg_get_dsoc(struct rk818_charger *cg)
{
	return rk818_reg_read(cg, RK818_SOC_REG);
}

static int rk818_cg_get_avg_current(struct rk818_charger *cg)
{
	int cur, val = 0;

	val |= rk818_reg_read(cg, RK818_BAT_CUR_AVG_REGL) << 0;
	val |= rk818_reg_read(cg, RK818_BAT_CUR_AVG_REGH) << 8;

	if (val & 0x800)
		val -= 4096;
	cur = val * cg->res_div * 1506 / 1000;

	return cur;
}

static int rk818_cg_get_ts2_voltage(struct rk818_charger *cg)
{
	u32 val = 0;
	int voltage;

	val |= rk818_reg_read(cg, RK818_TS2_ADC_REGL) << 0;
	val |= rk818_reg_read(cg, RK818_TS2_ADC_REGH) << 8;

	/* refer voltage 2.2V, 12bit adc accuracy */
	voltage = val * 2200 * cg->pdata->ts2_vol_multi / 4095;

	DBG("********* ts2 adc=%d, vol=%d\n", val, voltage);

	return voltage;
}

static u64 get_boot_sec(void)
{
	struct timespec ts;

	get_monotonic_boottime(&ts);

	return ts.tv_sec;
}

static int rk818_cg_lowpwr_check(struct rk818_charger *cg)
{
	u8 buf;
	static u64 time;
	int current_avg, dsoc, fake_offline = 0;

	buf = rk818_reg_read(cg, RK818_TS_CTRL_REG);
	if (!(buf & GG_EN))
		return fake_offline;

	dsoc = rk818_cg_get_dsoc(cg);
	current_avg = rk818_cg_get_avg_current(cg);
	if ((current_avg < 0) && (dsoc == 0)) {
		if (!time)
			time = get_boot_sec();
		if ((get_boot_sec() - time) >= 30) {
			fake_offline = 1;
			CG_INFO("low power....soc=%d, current=%d\n",
				dsoc, current_avg);
		}
	} else {
		time = 0;
		fake_offline = 0;
	}

	DBG("<%s>. t=%lld, dsoc=%d, current=%d, fake_offline=%d\n",
	    __func__, get_boot_sec() - time, dsoc, current_avg, fake_offline);

	return fake_offline;
}

static int rk818_cg_get_bat_psy(struct device *dev, void *data)
{
	struct rk818_charger *cg = data;
	struct power_supply *psy = dev_get_drvdata(dev);

	if (psy->desc->type == POWER_SUPPLY_TYPE_BATTERY) {
		cg->bat_psy = psy;
		return 1;
	}

	return 0;
}

static void rk818_cg_get_psy(struct rk818_charger *cg)
{
	if (!cg->bat_psy)
		class_for_each_device(power_supply_class, NULL, (void *)cg,
				      rk818_cg_get_bat_psy);
}

static int rk818_cg_get_bat_max_cur(struct rk818_charger *cg)
{
	union power_supply_propval val;
	int ret;

	rk818_cg_get_psy(cg);

	if (!cg->bat_psy)
		return cg->pdata->max_chrg_current;

	ret = cg->bat_psy->desc->get_property(cg->bat_psy,
					      POWER_SUPPLY_PROP_CURRENT_MAX,
					      &val);
	if (!ret && val.intval)
		return val.intval;

	return cg->pdata->max_chrg_current;
}

static int rk818_cg_get_bat_max_vol(struct rk818_charger *cg)
{
	union power_supply_propval val;
	int ret;

	rk818_cg_get_psy(cg);

	if (!cg->bat_psy)
		return cg->pdata->max_chrg_voltage;

	ret = cg->bat_psy->desc->get_property(cg->bat_psy,
					      POWER_SUPPLY_PROP_VOLTAGE_MAX,
					      &val);
	if (!ret && val.intval)
		return val.intval;

	return cg->pdata->max_chrg_voltage;
}

static enum power_supply_property rk818_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static enum power_supply_property rk818_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static int rk818_cg_ac_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	struct rk818_charger *cg = power_supply_get_drvdata(psy);
	int fake_offline = 0, ret = 0;

	if (rk818_cg_online(cg))
		fake_offline = rk818_cg_lowpwr_check(cg);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (cg->pdata->virtual_power)
			val->intval = 1;
		else if (fake_offline)
			val->intval = 0;
		else
			val->intval = (cg->ac_in | cg->dc_in);

		DBG("report online: %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (cg->pdata->virtual_power)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (fake_offline)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else
			val->intval = cg->prop_status;

		DBG("report prop: %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = rk818_cg_get_bat_max_vol(cg);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = rk818_cg_get_bat_max_cur(cg);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rk818_cg_usb_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	struct rk818_charger *cg = power_supply_get_drvdata(psy);
	int fake_offline, ret = 0;

	if (rk818_cg_online(cg))
		fake_offline = rk818_cg_lowpwr_check(cg);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (cg->pdata->virtual_power)
			val->intval = 1;
		else if (fake_offline)
			val->intval = 0;
		else
			val->intval = cg->usb_in;

		DBG("report online: %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (cg->pdata->virtual_power)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (fake_offline)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else
			val->intval = cg->prop_status;

		DBG("report prop: %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = rk818_cg_get_bat_max_vol(cg);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = rk818_cg_get_bat_max_cur(cg);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct power_supply_desc rk818_ac_desc = {
	.name		= "ac",
	.type		= POWER_SUPPLY_TYPE_MAINS,
	.properties	= rk818_ac_props,
	.num_properties	= ARRAY_SIZE(rk818_ac_props),
	.get_property	= rk818_cg_ac_get_property,
};

static const struct power_supply_desc rk818_usb_desc = {
	.name		= "usb",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= rk818_usb_props,
	.num_properties	= ARRAY_SIZE(rk818_usb_props),
	.get_property	= rk818_cg_usb_get_property,
};

static int rk818_cg_init_power_supply(struct rk818_charger *cg)
{
	struct power_supply_config psy_cfg = { .drv_data = cg, };

	cg->usb_psy = devm_power_supply_register(cg->dev, &rk818_usb_desc,
						 &psy_cfg);
	if (IS_ERR(cg->usb_psy)) {
		dev_err(cg->dev, "register usb power supply fail\n");
		return PTR_ERR(cg->usb_psy);
	}

	cg->ac_psy = devm_power_supply_register(cg->dev, &rk818_ac_desc,
						&psy_cfg);
	if (IS_ERR(cg->ac_psy)) {
		dev_err(cg->dev, "register ac power supply fail\n");
		return PTR_ERR(cg->ac_psy);
	}

	return 0;
}

static void rk818_cg_pr_info(struct rk818_charger *cg)
{
	u8 usb_ctrl, chrg_ctrl1;

	usb_ctrl = rk818_reg_read(cg, RK818_USB_CTRL_REG);
	chrg_ctrl1 = rk818_reg_read(cg, RK818_CHRG_CTRL_REG1);
	CG_INFO("ac=%d usb=%d dc=%d otg=%d v=%d chrg=%d input=%d virt=%d\n",
		cg->ac_in, cg->usb_in, cg->dc_in, cg->otg_in,
		chrg_vol_sel_array[(chrg_ctrl1 & 0x70) >> 4],
		chrg_cur_sel_array[chrg_ctrl1 & 0x0f] * cg->res_div,
		chrg_cur_input_array[usb_ctrl & 0x0f],
		cg->pdata->virtual_power);
}

static bool is_battery_exist(struct rk818_charger *cg)
{
	return (rk818_reg_read(cg, RK818_SUP_STS_REG) & BAT_EXS) ? true : false;
}

static void rk818_cg_set_chrg_current(struct rk818_charger *cg,
				      u8 chrg_current)
{
	u8 chrg_ctrl_reg1;

	chrg_ctrl_reg1 = rk818_reg_read(cg, RK818_CHRG_CTRL_REG1);
	chrg_ctrl_reg1 &= ~CHRG_CUR_MSK;
	chrg_ctrl_reg1 |= (chrg_current);
	rk818_reg_write(cg, RK818_CHRG_CTRL_REG1, chrg_ctrl_reg1);
}

static void rk818_cg_set_input_current(struct rk818_charger *cg,
				       int input_current)
{
	u8 usb_ctrl;

	if (cg->pdata->virtual_power) {
		CG_INFO("warning: virtual power mode...\n");
		input_current = cg->chrg_input;
	}

	usb_ctrl = rk818_reg_read(cg, RK818_USB_CTRL_REG);
	usb_ctrl &= ~INPUT_CUR_MSK;
	usb_ctrl |= (input_current);
	rk818_reg_write(cg, RK818_USB_CTRL_REG, usb_ctrl);
}

static void rk818_cg_set_finish_sig(struct rk818_charger *cg, int mode)
{
	u8 buf;

	buf = rk818_reg_read(cg, RK818_CHRG_CTRL_REG3);
	buf &= ~CHRG_FINISH_MODE_MSK;
	buf |= mode;
	rk818_reg_write(cg, RK818_CHRG_CTRL_REG3, buf);
}

static void rk818_cg_finish_sig_work(struct work_struct *work)
{
	struct rk818_charger *cg;

	cg = container_of(work, struct rk818_charger, finish_sig_work.work);
	if (rk818_cg_online(cg))
		rk818_cg_set_finish_sig(cg, CHRG_FINISH_DIG_SIGNAL);
	else
		rk818_cg_set_finish_sig(cg, CHRG_FINISH_ANA_SIGNAL);
}

static void rk818_cg_set_chrg_param(struct rk818_charger *cg,
				    enum charger_t charger)
{
	u8 buf;

	switch (charger) {
	case USB_TYPE_NONE_CHARGER:
		cg->usb_in = 0;
		cg->ac_in = 0;
		if (cg->dc_in == 0) {
			cg->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
			rk818_cg_set_chrg_current(cg, cg->chrg_current);
			rk818_cg_set_input_current(cg, INPUT_CUR450MA);
		}
		power_supply_changed(cg->usb_psy);
		power_supply_changed(cg->ac_psy);
		break;
	case USB_TYPE_USB_CHARGER:
		cg->usb_in = 1;
		cg->ac_in = 0;
		cg->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		if (cg->dc_in == 0) {
			rk818_cg_set_chrg_current(cg, cg->chrg_current);
			rk818_cg_set_input_current(cg, INPUT_CUR450MA);
		}
		power_supply_changed(cg->usb_psy);
		power_supply_changed(cg->ac_psy);
		break;
	case USB_TYPE_AC_CHARGER:
	case USB_TYPE_CDP_CHARGER:
		cg->ac_in = 1;
		cg->usb_in = 0;
		cg->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		if (charger == USB_TYPE_AC_CHARGER) {
			if (cg->pdata->ts2_vol_multi) {
				rk818_cg_set_chrg_current(cg, cg->chrg_current);
				rk818_cg_set_input_current(cg, INPUT_CUR450MA);
				queue_delayed_work(cg->ts2_wq,
						   &cg->ts2_vol_work,
						   msecs_to_jiffies(0));
			} else {
				rk818_cg_set_chrg_current(cg, cg->chrg_current);
				rk818_cg_set_input_current(cg, cg->chrg_input);
			}
		} else {
			rk818_cg_set_chrg_current(cg, cg->chrg_current);
			rk818_cg_set_input_current(cg, INPUT_CUR1500MA);
		}
		power_supply_changed(cg->usb_psy);
		power_supply_changed(cg->ac_psy);
		break;
	case DC_TYPE_DC_CHARGER:
		cg->dc_in = 1;
		cg->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		if (cg->pdata->ts2_vol_multi) {
			rk818_cg_set_chrg_current(cg, cg->chrg_current);
			rk818_cg_set_input_current(cg, INPUT_CUR450MA);
			queue_delayed_work(cg->ts2_wq,
					   &cg->ts2_vol_work,
					   msecs_to_jiffies(0));
		} else {
			rk818_cg_set_chrg_current(cg, cg->chrg_current);
			rk818_cg_set_input_current(cg, cg->chrg_input);
		}
		power_supply_changed(cg->usb_psy);
		power_supply_changed(cg->ac_psy);
		break;
	case DC_TYPE_NONE_CHARGER:
		cg->dc_in = 0;
		buf = rk818_reg_read(cg, RK818_VB_MON_REG);
		if ((buf & PLUG_IN_STS) == 0) {
			cg->ac_in = 0;
			cg->usb_in = 0;
			cg->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
			rk818_cg_set_chrg_current(cg, cg->chrg_current);
			rk818_cg_set_input_current(cg, INPUT_CUR450MA);
		} else if (cg->usb_in) {
			rk818_cg_set_chrg_current(cg, cg->chrg_current);
			rk818_cg_set_input_current(cg, INPUT_CUR450MA);
			cg->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		}
		power_supply_changed(cg->usb_psy);
		power_supply_changed(cg->ac_psy);
		break;
	default:
		cg->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	}

	cg->charger_changed = 1;

	if (rk818_cg_online(cg) && rk818_cg_get_dsoc(cg) == 100)
		cg->prop_status = POWER_SUPPLY_STATUS_FULL;

	if (cg->finish_sig_wq)
		queue_delayed_work(cg->finish_sig_wq, &cg->finish_sig_work,
				   msecs_to_jiffies(1000));
}

static void rk818_cg_set_otg_state(struct rk818_charger *cg, int state)
{
	int ret;

	switch (state) {
	case USB_OTG_POWER_ON:
		if (cg->otg_in) {
			CG_INFO("otg5v is on yet, ignore..\n");
		} else {
			cg->otg_in = 1;
			if (IS_ERR(cg->otg5v_rdev)) {
				CG_INFO("not get otg_switch regulator!\n");
				return;
			}

			if (!regulator_is_enabled(cg->otg5v_rdev)) {
				ret = regulator_enable(cg->otg5v_rdev);
				if (ret) {
					CG_INFO("enable otg5v failed:%d\n",
						ret);
					return;
				}
			}
			disable_irq(cg->plugin_irq);
			disable_irq(cg->plugout_irq);
			CG_INFO("enable otg5v\n");
		}
		break;

	case USB_OTG_POWER_OFF:
		if (!cg->otg_in) {
			CG_INFO("otg5v is off yet, ignore..\n");
		} else {
			cg->otg_in = 0;
			if (IS_ERR(cg->otg5v_rdev)) {
				CG_INFO("not get otg_switch regulator!\n");
				return;
			}

			if (regulator_is_enabled(cg->otg5v_rdev)) {
				ret = regulator_disable(cg->otg5v_rdev);
				if (ret) {
					CG_INFO("disable otg5v failed: %d\n",
						ret);
					return;
				}
			}
			enable_irq(cg->plugin_irq);
			enable_irq(cg->plugout_irq);
			CG_INFO("disable otg5v\n");
		}
		break;
	default:
		dev_err(cg->dev, "error otg type\n");
		break;
	}
}

static enum charger_t rk818_cg_get_dc_state(struct rk818_charger *cg)
{
	int level;

	if (!gpio_is_valid(cg->pdata->dc_det_pin))
		return DC_TYPE_NONE_CHARGER;

	level = gpio_get_value(cg->pdata->dc_det_pin);

	return (level == cg->pdata->dc_det_level) ?
		DC_TYPE_DC_CHARGER : DC_TYPE_NONE_CHARGER;
}

static void rk818_cg_dc_det_worker(struct work_struct *work)
{
	enum charger_t charger;
	struct rk818_charger *cg = container_of(work,
			struct rk818_charger, dc_work.work);

	charger = rk818_cg_get_dc_state(cg);
	if (charger == DC_TYPE_DC_CHARGER) {
		CG_INFO("detect dc charger in..\n");
		rk818_cg_set_chrg_param(cg, DC_TYPE_DC_CHARGER);
		/* check otg supply */
		if (cg->otg_in && cg->pdata->power_dc2otg) {
			CG_INFO("otg power from dc adapter\n");
			rk818_cg_set_otg_state(cg, USB_OTG_POWER_OFF);
		}
	} else {
		CG_INFO("detect dc charger out..\n");
		rk818_cg_set_chrg_param(cg, DC_TYPE_NONE_CHARGER);
		/* check otg supply, power on anyway */
		if (cg->otg_in)
			rk818_cg_set_otg_state(cg, USB_OTG_POWER_ON);
	}

	rk_send_wakeup_key();
	rk818_cg_pr_info(cg);
}

static u8 rk818_cg_decode_chrg_vol(struct rk818_charger *cg, u32 chrg_vol)
{
	u8 val = 0, index;

	for (index = 0; index < ARRAY_SIZE(chrg_vol_sel_array); index++) {
		if (chrg_vol < chrg_vol_sel_array[index])
			break;
		val = index << 4;
	}

	DBG("<%s>. vol=0x%x\n", __func__, val);
	return val;
}

static u8 rk818_cg_decode_input_current(struct rk818_charger *cg,
					u32 input_current)
{
	u8 val = 0, index;

	for (index = 2; index < ARRAY_SIZE(chrg_cur_input_array); index++) {
		if (input_current < 850 && input_current > 80) {
			val = 0x0;	/* 450mA */
			break;
		} else if (input_current <= 80) {
			val = 0x1;	/* 80mA */
			break;
		} else {
			if (input_current < chrg_cur_input_array[index])
				break;
			val = index <<  0;
		}
	}

	DBG("<%s>. input=0x%x\n", __func__, val);
	return val;
}

static u8 rk818_cg_decode_chrg_current(struct rk818_charger *cg,
				       u32 chrg_current)
{
	u8 val = 0, index;

	if (cg->pdata->sample_res == SAMPLE_RES_10MR) {
		if (chrg_current > 2000)
			chrg_current /= cg->res_div;
		else
			chrg_current = 1000;
	}

	for (index = 0; index < ARRAY_SIZE(chrg_cur_sel_array); index++) {
		if (chrg_current < chrg_cur_sel_array[index])
			break;
		val = index << 0;
	}

	DBG("<%s>. sel=0x%x\n", __func__, val);
	return val;
}

static void rk818_cg_init_config(struct rk818_charger *cg)
{
	u8 usb_ctrl, sup_sts, chrg_ctrl1;

	cg->chrg_voltage = rk818_cg_decode_chrg_vol(cg,
				cg->pdata->max_chrg_voltage);
	cg->chrg_current = rk818_cg_decode_chrg_current(cg,
				cg->pdata->max_chrg_current);
	cg->chrg_input = rk818_cg_decode_input_current(cg,
				cg->pdata->max_input_current);

	sup_sts = rk818_reg_read(cg, RK818_SUP_STS_REG);
	usb_ctrl = rk818_reg_read(cg, RK818_USB_CTRL_REG);

	/* set charge current and voltage */
	usb_ctrl &= ~INPUT_CUR_MSK;
	usb_ctrl |= cg->chrg_input;
	chrg_ctrl1 = (CHRG_EN | cg->chrg_voltage | cg->chrg_current);

	/* disable voltage limit and enable input current limit */
	sup_sts &= ~USB_VLIMIT_EN;
	sup_sts |= USB_CLIMIT_EN;

	rk818_reg_write(cg, RK818_SUP_STS_REG, sup_sts);
	rk818_reg_write(cg, RK818_USB_CTRL_REG, usb_ctrl);
	rk818_reg_write(cg, RK818_CHRG_CTRL_REG1, chrg_ctrl1);
}

static void rk818_ts2_vol_work(struct work_struct *work)
{
	struct rk818_charger *cg;
	int ts2_vol, input_current, invalid_cnt = 0, confirm_cnt = 0;

	cg = container_of(work, struct rk818_charger, ts2_vol_work.work);

	input_current = INPUT_CUR80MA;
	while (input_current < cg->chrg_input) {
		msleep(100);
		ts2_vol = rk818_cg_get_ts2_voltage(cg);

		/* filter invalid voltage */
		if (ts2_vol <= DEFAULT_TS2_VALID_VOL) {
			invalid_cnt++;
			DBG("%s: invalid ts2 voltage: %d\n, cnt=%d",
			    __func__, ts2_vol, invalid_cnt);
			if (invalid_cnt < DEFAULT_TS2_CHECK_CNT)
				continue;

			/* if fail, set max input current as default */
			input_current = cg->chrg_input;
			rk818_cg_set_input_current(cg, input_current);
			break;
		}

		/* update input current */
		if (ts2_vol >= DEFAULT_TS2_THRESHOLD_VOL) {
			/* update input current */
			input_current++;
			rk818_cg_set_input_current(cg, input_current);
			DBG("********* input=%d\n",
			    chrg_cur_input_array[input_current & 0x0f]);
		} else {
			/* confirm lower threshold voltage */
			confirm_cnt++;
			if (confirm_cnt < DEFAULT_TS2_CHECK_CNT) {
				DBG("%s: confirm ts2 voltage: %d\n, cnt=%d",
				    __func__, ts2_vol, confirm_cnt);
				continue;
			}

			/* trigger threshold, so roll back 1 step */
			input_current--;
			if (input_current == INPUT_CUR80MA ||
			    input_current < 0)
				input_current = INPUT_CUR450MA;
			rk818_cg_set_input_current(cg, input_current);
			break;
		}
	}

	if (input_current != cg->chrg_input)
		CG_INFO("adjust input current: %dma\n",
			chrg_cur_input_array[input_current & 0x0f]);
}

static int rk818_cg_charger_evt_notifier(struct notifier_block *nb,
					 unsigned long event, void *ptr)
{
	struct rk818_charger *cg =
		container_of(nb, struct rk818_charger, cable_cg_nb);

	queue_delayed_work(cg->usb_charger_wq, &cg->usb_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static int rk818_cg_discnt_evt_notfier(struct notifier_block *nb,
				       unsigned long event, void *ptr)
{
	struct rk818_charger *cg =
		container_of(nb, struct rk818_charger, cable_discnt_nb);

	queue_delayed_work(cg->usb_charger_wq, &cg->discnt_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static int rk818_cg_host_evt_notifier(struct notifier_block *nb,
				      unsigned long event, void *ptr)
{
	struct rk818_charger *cg =
		container_of(nb, struct rk818_charger, cable_host_nb);

	queue_delayed_work(cg->usb_charger_wq, &cg->host_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static int rk818_cg_bc_evt_notifier(struct notifier_block *nb,
				    unsigned long event, void *ptr)
{
	struct rk818_charger *cg =
		container_of(nb, struct rk818_charger, bc_nb);

	cg->bc_event = event;
	queue_delayed_work(cg->usb_charger_wq, &cg->usb_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static void rk818_cg_bc_evt_worker(struct work_struct *work)
{
	struct rk818_charger *cg = container_of(work,
					struct rk818_charger, usb_work.work);
	const char *event_name[] = {"DISCNT", "USB", "AC", "CDP1.5A",
				    "UNKNOWN", "OTG ON", "OTG OFF"};

	switch (cg->bc_event) {
	case USB_BC_TYPE_DISCNT:
		rk818_cg_set_chrg_param(cg, USB_TYPE_NONE_CHARGER);
		break;
	case USB_BC_TYPE_SDP:
		rk818_cg_set_chrg_param(cg, USB_TYPE_USB_CHARGER);
		break;
	case USB_BC_TYPE_DCP:
		rk818_cg_set_chrg_param(cg, USB_TYPE_AC_CHARGER);
		break;
	case USB_BC_TYPE_CDP:
		rk818_cg_set_chrg_param(cg, USB_TYPE_CDP_CHARGER);
		break;
	case USB_OTG_POWER_ON:
		if (cg->pdata->power_dc2otg && cg->dc_in)
			CG_INFO("otg power from dc adapter\n");
		else
			rk818_cg_set_otg_state(cg, USB_OTG_POWER_ON);
		break;
	case USB_OTG_POWER_OFF:
		rk818_cg_set_otg_state(cg, USB_OTG_POWER_OFF);
		break;
	default:
		break;
	}

	CG_INFO("receive bc notifier event: %s..\n", event_name[cg->bc_event]);

	rk818_cg_pr_info(cg);
}

static void rk818_cg_irq_delay_work(struct work_struct *work)
{
	struct rk818_charger *cg = container_of(work,
			struct rk818_charger, irq_work.work);

	if (cg->plugin_trigger) {
		CG_INFO("pmic: plug in\n");
		cg->plugin_trigger = 0;
		rk_send_wakeup_key();
		if (cg->pdata->extcon)
			queue_delayed_work(cg->usb_charger_wq, &cg->usb_work,
					   msecs_to_jiffies(10));
	} else if (cg->plugout_trigger) {
		CG_INFO("pmic: plug out\n");
		cg->plugout_trigger = 0;
		rk818_cg_set_chrg_param(cg, USB_TYPE_NONE_CHARGER);
		rk818_cg_set_chrg_param(cg, DC_TYPE_NONE_CHARGER);
		rk_send_wakeup_key();
		rk818_cg_pr_info(cg);
	} else {
		CG_INFO("pmic: unknown irq\n");
	}
}

static irqreturn_t rk818_plug_in_isr(int irq, void *cg)
{
	struct rk818_charger *icg;

	icg = (struct rk818_charger *)cg;
	icg->plugin_trigger = 1;
	queue_delayed_work(icg->usb_charger_wq, &icg->irq_work,
			   msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

static irqreturn_t rk818_plug_out_isr(int irq, void *cg)
{
	struct rk818_charger *icg;

	icg = (struct rk818_charger *)cg;
	icg->plugout_trigger = 1;
	queue_delayed_work(icg->usb_charger_wq, &icg->irq_work,
			   msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

static irqreturn_t rk818_dc_det_isr(int irq, void *charger)
{
	struct rk818_charger *cg = (struct rk818_charger *)charger;

	if (gpio_get_value(cg->pdata->dc_det_pin))
		irq_set_irq_type(irq, IRQF_TRIGGER_LOW);
	else
		irq_set_irq_type(irq, IRQF_TRIGGER_HIGH);

	queue_delayed_work(cg->dc_charger_wq, &cg->dc_work,
			   msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

static int rk818_cg_init_irqs(struct rk818_charger *cg)
{
	struct rk808 *rk818 = cg->rk818;
	struct platform_device *pdev = cg->pdev;
	int ret, plug_in_irq, plug_out_irq;

	plug_in_irq = regmap_irq_get_virq(rk818->irq_data, RK818_IRQ_PLUG_IN);
	if (plug_in_irq < 0) {
		dev_err(cg->dev, "plug_in_irq request failed!\n");
		return plug_in_irq;
	}

	plug_out_irq = regmap_irq_get_virq(rk818->irq_data, RK818_IRQ_PLUG_OUT);
	if (plug_out_irq < 0) {
		dev_err(cg->dev, "plug_out_irq request failed!\n");
		return plug_out_irq;
	}

	ret = devm_request_threaded_irq(cg->dev, plug_in_irq, NULL,
					rk818_plug_in_isr,
					IRQF_TRIGGER_RISING,
					"rk818_plug_in", cg);
	if (ret) {
		dev_err(&pdev->dev, "plug_in_irq request failed!\n");
		return ret;
	}

	ret = devm_request_threaded_irq(cg->dev, plug_out_irq, NULL,
					rk818_plug_out_isr,
					IRQF_TRIGGER_FALLING,
					"rk818_plug_out", cg);
	if (ret) {
		dev_err(&pdev->dev, "plug_out_irq request failed!\n");
		return ret;
	}

	cg->plugin_irq = plug_in_irq;
	cg->plugout_irq = plug_out_irq;

	INIT_DELAYED_WORK(&cg->irq_work, rk818_cg_irq_delay_work);

	return 0;
}

static int rk818_cg_init_dc(struct rk818_charger *cg)
{
	int ret, level;
	unsigned long irq_flags;
	unsigned int dc_det_irq;

	cg->dc_charger_wq = alloc_ordered_workqueue("%s",
				WQ_MEM_RECLAIM | WQ_FREEZABLE,
				"rk818-dc-wq");
	INIT_DELAYED_WORK(&cg->dc_work, rk818_cg_dc_det_worker);
	cg->dc_charger = DC_TYPE_NONE_CHARGER;

	if (!cg->pdata->support_dc_det)
		return 0;

	ret = devm_gpio_request(cg->dev, cg->pdata->dc_det_pin, "rk818_dc_det");
	if (ret < 0) {
		dev_err(cg->dev, "failed to request gpio %d\n",
			cg->pdata->dc_det_pin);
		return ret;
	}

	ret = gpio_direction_input(cg->pdata->dc_det_pin);
	if (ret) {
		dev_err(cg->dev, "failed to set gpio input\n");
		return ret;
	}

	level = gpio_get_value(cg->pdata->dc_det_pin);
	if (level == cg->pdata->dc_det_level)
		cg->dc_charger = DC_TYPE_DC_CHARGER;
	else
		cg->dc_charger = DC_TYPE_NONE_CHARGER;

	if (level)
		irq_flags = IRQF_TRIGGER_LOW;
	else
		irq_flags = IRQF_TRIGGER_HIGH;

	dc_det_irq = gpio_to_irq(cg->pdata->dc_det_pin);
	ret = devm_request_irq(cg->dev, dc_det_irq, rk818_dc_det_isr,
			       irq_flags, "rk818_dc_det", cg);
	if (ret != 0) {
		dev_err(cg->dev, "rk818_dc_det_irq request failed!\n");
		return ret;
	}

	enable_irq_wake(dc_det_irq);

	return 0;
}

static void rk818_cg_discnt_evt_worker(struct work_struct *work)
{
	struct rk818_charger *cg = container_of(work,
			struct rk818_charger, discnt_work.work);

	if (extcon_get_cable_state_(cg->cable_edev, EXTCON_USB) == 0) {
		CG_INFO("receive type-c notifier event: DISCNT...\n");
		rk818_cg_set_chrg_param(cg, USB_TYPE_NONE_CHARGER);
		rk818_cg_pr_info(cg);
	}
}

static void rk818_cg_host_evt_worker(struct work_struct *work)
{
	struct rk818_charger *cg = container_of(work,
			struct rk818_charger, host_work.work);
	struct extcon_dev *edev = cg->cable_edev;

	/* Determine cable/charger type */
	if (extcon_get_cable_state_(edev, EXTCON_USB_VBUS_EN) > 0) {
		CG_INFO("receive type-c notifier event: OTG ON...\n");
		if (cg->dc_in && cg->pdata->power_dc2otg)
			CG_INFO("otg power from dc adapter\n");
		else
			rk818_cg_set_otg_state(cg, USB_OTG_POWER_ON);
	} else if (extcon_get_cable_state_(edev, EXTCON_USB_VBUS_EN) == 0) {
		CG_INFO("receive type-c notifier event: OTG OFF...\n");
		rk818_cg_set_otg_state(cg, USB_OTG_POWER_OFF);
	}

	rk818_cg_pr_info(cg);
}

static void rk818_cg_charger_evt_worker(struct work_struct *work)
{
	struct rk818_charger *cg = container_of(work,
				struct rk818_charger, usb_work.work);
	struct extcon_dev *edev = cg->cable_edev;
	enum charger_t charger = USB_TYPE_UNKNOWN_CHARGER;
	const char *event[] = {"UN", "NONE", "USB", "AC", "CDP1.5A"};

	/* Determine cable/charger type */
	if (extcon_get_cable_state_(edev, EXTCON_CHG_USB_SDP) > 0)
		charger = USB_TYPE_USB_CHARGER;
	else if (extcon_get_cable_state_(edev, EXTCON_CHG_USB_DCP) > 0)
		charger = USB_TYPE_AC_CHARGER;
	else if (extcon_get_cable_state_(edev, EXTCON_CHG_USB_CDP) > 0)
		charger = USB_TYPE_CDP_CHARGER;

	if (charger != USB_TYPE_UNKNOWN_CHARGER) {
		CG_INFO("receive type-c notifier event: %s...\n",
			event[charger]);
		cg->usb_charger = charger;
		rk818_cg_set_chrg_param(cg, charger);
		rk818_cg_pr_info(cg);
	}
}

static long rk818_cg_init_usb(struct rk818_charger *cg)
{
	enum charger_t charger;
	enum bc_port_type bc_type;
	struct extcon_dev *edev;
	struct device *dev = cg->dev;
	int ret;

	cg->usb_charger_wq = alloc_ordered_workqueue("%s",
				WQ_MEM_RECLAIM | WQ_FREEZABLE,
				"rk818-usb-wq");
	cg->usb_charger = USB_TYPE_NONE_CHARGER;

	/* type-C */
	if (cg->pdata->extcon) {
		edev = extcon_get_edev_by_phandle(dev->parent, 0);
		if (IS_ERR(edev)) {
			if (PTR_ERR(edev) != -EPROBE_DEFER)
				dev_err(dev, "Invalid or missing extcon\n");
			return PTR_ERR(edev);
		}

		/* Register chargers  */
		INIT_DELAYED_WORK(&cg->usb_work, rk818_cg_charger_evt_worker);
		cg->cable_cg_nb.notifier_call = rk818_cg_charger_evt_notifier;
		ret = extcon_register_notifier(edev, EXTCON_CHG_USB_SDP,
					       &cg->cable_cg_nb);
		if (ret < 0) {
			dev_err(dev, "failed to register notifier for SDP\n");
			return ret;
		}

		ret = extcon_register_notifier(edev, EXTCON_CHG_USB_DCP,
					       &cg->cable_cg_nb);
		if (ret < 0) {
			dev_err(dev, "failed to register notifier for DCP\n");
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_SDP,
						   &cg->cable_cg_nb);
			return ret;
		}

		ret = extcon_register_notifier(edev, EXTCON_CHG_USB_CDP,
					       &cg->cable_cg_nb);
		if (ret < 0) {
			dev_err(dev, "failed to register notifier for CDP\n");
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_SDP,
						   &cg->cable_cg_nb);
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_DCP,
						   &cg->cable_cg_nb);
			return ret;
		}

		/* Register host */
		INIT_DELAYED_WORK(&cg->host_work, rk818_cg_host_evt_worker);
		cg->cable_host_nb.notifier_call = rk818_cg_host_evt_notifier;
		ret = extcon_register_notifier(edev, EXTCON_USB_VBUS_EN,
					       &cg->cable_host_nb);
		if (ret < 0) {
			dev_err(dev, "failed to register notifier for HOST\n");
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_SDP,
						   &cg->cable_cg_nb);
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_DCP,
						   &cg->cable_cg_nb);
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_CDP,
						   &cg->cable_cg_nb);

			return ret;
		}

		/* Register discnt usb */
		INIT_DELAYED_WORK(&cg->discnt_work, rk818_cg_discnt_evt_worker);
		cg->cable_discnt_nb.notifier_call = rk818_cg_discnt_evt_notfier;
		ret = extcon_register_notifier(edev, EXTCON_USB,
					       &cg->cable_discnt_nb);
		if (ret < 0) {
			dev_err(dev, "failed to register notifier for HOST\n");
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_SDP,
						   &cg->cable_cg_nb);
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_DCP,
						   &cg->cable_cg_nb);
			extcon_unregister_notifier(edev, EXTCON_CHG_USB_CDP,
						   &cg->cable_cg_nb);
			extcon_unregister_notifier(edev, EXTCON_USB_VBUS_EN,
						   &cg->cable_host_nb);
			return ret;
		}

		cg->cable_edev = edev;

		schedule_delayed_work(&cg->host_work, 0);
		schedule_delayed_work(&cg->usb_work, 0);

		CG_INFO("register typec extcon evt notifier\n");
	} else {
		INIT_DELAYED_WORK(&cg->usb_work, rk818_cg_bc_evt_worker);
		cg->bc_nb.notifier_call = rk818_cg_bc_evt_notifier;
		ret = rk_bc_detect_notifier_register(&cg->bc_nb, &bc_type);
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

		cg->usb_charger = charger;
		CG_INFO("register bc evt notifier\n");
	}

	return 0;
}

static void rk818_cg_init_finish_sig(struct rk818_charger *cg)
{
	if (rk818_cg_online(cg))
		rk818_cg_set_finish_sig(cg, CHRG_FINISH_DIG_SIGNAL);
	else
		rk818_cg_set_finish_sig(cg, CHRG_FINISH_ANA_SIGNAL);

	cg->finish_sig_wq = alloc_ordered_workqueue("%s",
				WQ_MEM_RECLAIM | WQ_FREEZABLE,
				"rk818-finish-sig-wq");
	INIT_DELAYED_WORK(&cg->finish_sig_work, rk818_cg_finish_sig_work);
}

static void rk818_cg_init_ts2_detect(struct rk818_charger *cg)
{
	u8 buf;

	cg->ts2_wq = alloc_ordered_workqueue("%s",
				WQ_MEM_RECLAIM | WQ_FREEZABLE,
				"rk818-ts2-wq");
	INIT_DELAYED_WORK(&cg->ts2_vol_work, rk818_ts2_vol_work);

	if (!cg->pdata->ts2_vol_multi)
		return;

	/* TS2 adc mode */
	buf = rk818_reg_read(cg, RK818_TS_CTRL_REG);
	buf |= TS2_FUN_ADC;
	rk818_reg_write(cg, RK818_TS_CTRL_REG, buf);

	/* TS2 adc enable */
	buf = rk818_reg_read(cg, RK818_ADC_CTRL_REG);
	buf |= ADC_TS2_EN;
	rk818_reg_write(cg, RK818_ADC_CTRL_REG, buf);

	CG_INFO("enable ts2 voltage detect, multi=%d\n",
		cg->pdata->ts2_vol_multi);
}

static void rk818_cg_init_charger_state(struct rk818_charger *cg)
{
	rk818_cg_init_config(cg);
	rk818_cg_init_finish_sig(cg);
	rk818_cg_set_chrg_param(cg, cg->dc_charger);
	rk818_cg_set_chrg_param(cg, cg->usb_charger);
	CG_INFO("ac=%d, usb=%d, dc=%d, otg=%d\n",
		cg->ac_in, cg->usb_in, cg->dc_in, cg->otg_in);
}

static int rk818_cg_temperature_notifier_call(struct notifier_block *nb,
					      unsigned long temp, void *data)
{
	struct rk818_charger *cg =
		container_of(nb, struct rk818_charger, temp_nb);
	static int temp_triggered, config_index = -1;
	int i, up_temp, down_temp, cfg_temp, cfg_offset, cfg_current;
	int now_temp = temp;
	u8 usb_ctrl, chrg_ctrl1;

	DBG("%s: receive notify temperature = %d\n", __func__, now_temp);
	for (i = 0; i < cg->pdata->tc_count; i++) {
		up_temp = 0;
		down_temp = 0;
		cfg_temp = cg->pdata->tc_table[i].temperature;
		cfg_offset = cg->pdata->tc_table[i].offset;
		cfg_current = cg->pdata->tc_table[i].chrg_current;

		/* positive: [temp, temp+offset] */
		if (cfg_temp >= 0)
			up_temp = cfg_temp + cfg_offset;
		/* negative: [temp-offset, temp] */
		if (cfg_temp < 0)
			down_temp = cfg_temp - cfg_offset;

		if ((now_temp >= 0 && now_temp <= up_temp &&
		     now_temp >= cfg_temp) ||
		    (now_temp < 0 && now_temp >= down_temp &&
		     now_temp <= cfg_temp)) {
			/* if not charger or temp changed, not update */
			if (config_index == i && !cg->charger_changed)
				return NOTIFY_DONE;

			config_index = i;
			cg->charger_changed = 0;
			temp_triggered = 1;

			if (cg->pdata->tc_table[i].set_chrg_current) {
				rk818_cg_set_chrg_current(cg, cfg_current);
				CG_INFO("temperature = %d'C[%d~%d'C], "
					"chrg current = %d\n",
					now_temp,
					(now_temp >= 0 ? cfg_temp : down_temp),
					(now_temp >= 0 ? up_temp : cfg_temp),
					chrg_cur_sel_array[cfg_current] *
					cg->res_div);
			} else {
				rk818_cg_set_input_current(cg, cfg_current);
				CG_INFO("temperature = %d'C[%d~%d'C], "
					"input current = %d\n",
					now_temp,
					(now_temp >= 0 ? cfg_temp : down_temp),
					(now_temp >= 0 ? up_temp : cfg_temp),
					chrg_cur_input_array[cfg_current]);
			}
			return NOTIFY_DONE;
		}
	}

	/*
	 * means: current temperature now covers above case, temperature rolls
	 * back to normal range, so restore default value
	 */
	if (temp_triggered) {
		temp_triggered = 0;
		config_index = -1;
		rk818_cg_set_chrg_current(cg, cg->chrg_current);
		rk818_cg_set_input_current(cg, cg->chrg_input);
		usb_ctrl = rk818_reg_read(cg, RK818_USB_CTRL_REG);
		chrg_ctrl1 = rk818_reg_read(cg, RK818_CHRG_CTRL_REG1);
		CG_INFO("roll back temp %d'C, current chrg = %d, input = %d\n",
			now_temp,
			chrg_cur_sel_array[(chrg_ctrl1 & 0x0f)] * cg->res_div,
			chrg_cur_input_array[(usb_ctrl & 0x0f)]);
	}

	return NOTIFY_DONE;
}

static int parse_temperature_chrg_table(struct rk818_charger *cg,
					struct device_node *np)
{
	int size, count;
	int i, sign, chrg_current;
	const __be32 *list;

	if (!of_find_property(np, "temperature_chrg_table", &size))
		return 0;

	list = of_get_property(np, "temperature_chrg_table", &size);
	size /= sizeof(u32);
	if (!size || (size % 4)) {
		dev_err(cg->dev,
			"invalid temperature_chrg_table: size=%d\n", size);
		return -EINVAL;
	}

	count = size / 4;
	cg->pdata->tc_count = count;
	cg->pdata->tc_table = devm_kzalloc(cg->dev,
					   count * sizeof(*cg->pdata->tc_table),
					   GFP_KERNEL);
	if (!cg->pdata->tc_table)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		/* temperature */
		sign = be32_to_cpu(*list++);
		cg->pdata->tc_table[i].temperature = sign ?
				-be32_to_cpu(*list++) : be32_to_cpu(*list++);
		/*
		 * because charge current lowest level is 1000mA:
		 * higher than or equal 1000ma, select charge current;
		 * lower than 1000ma, must select input current.
		 */
		chrg_current = be32_to_cpu(*list++);
		if (chrg_current >= 1000) {
			cg->pdata->tc_table[i].set_chrg_current = 1;
			cg->pdata->tc_table[i].chrg_current =
				rk818_cg_decode_chrg_current(cg, chrg_current);
		} else {
			cg->pdata->tc_table[i].chrg_current =
				rk818_cg_decode_input_current(cg, chrg_current);
		}

		/* temperature offset */
		cg->pdata->tc_table[i].offset = be32_to_cpu(*list++);

		DBG("temp=%d, chrg=0x%x, offset=%d\n",
		    cg->pdata->tc_table[i].temperature,
		    cg->pdata->tc_table[i].chrg_current,
		    cg->pdata->tc_table[i].offset);
	}

	return 0;
}

static int rk818_cg_register_temp_notifier(struct rk818_charger *cg)
{
	int ret;

	if (!cg->pdata->tc_count)
		return 0;
	cg->temp_nb.notifier_call = rk818_cg_temperature_notifier_call,
	ret = rk818_bat_temp_notifier_register(&cg->temp_nb);
	if (ret) {
		dev_err(cg->dev,
			"battery temperature notify register failed:%d\n", ret);
		return ret;
	}

	CG_INFO("enable set charge current by temperature\n");

	return 0;
}

static int rk818_cg_get_otg5v_regulator(struct rk818_charger *cg)
{
	int ret;

	/* not necessary */
	cg->otg5v_rdev = devm_regulator_get(cg->dev, "otg_switch");
	if (IS_ERR(cg->otg5v_rdev)) {
		ret = PTR_ERR(cg->otg5v_rdev);
		dev_warn(cg->dev, "failed to get otg regulator: %d\n", ret);
	}

	return 0;
}

#ifdef CONFIG_OF
static int rk818_cg_parse_dt(struct rk818_charger *cg)
{
	struct device_node *np;
	struct charger_platform_data *pdata;
	enum of_gpio_flags flags;
	struct device *dev = cg->dev;
	int ret;

	np = of_find_node_by_name(cg->pdev->dev.of_node, "battery");
	if (!np) {
		dev_err(dev, "battery node not found!\n");
		return -ENODEV;
	}

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	cg->pdata = pdata;
	pdata->max_chrg_current = DEFAULT_CHRG_CURRENT;
	pdata->max_input_current = DEFAULT_INPUT_CURRENT;
	pdata->max_chrg_voltage = DEFAULT_CHRG_VOLTAGE;

	pdata->extcon = device_property_read_bool(dev->parent, "extcon");

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

	ret = of_property_read_u32(np, "virtual_power", &pdata->virtual_power);
	if (ret < 0)
		dev_err(dev, "virtual_power missing!\n");

	ret = of_property_read_u32(np, "power_dc2otg", &pdata->power_dc2otg);
	if (ret < 0)
		dev_err(dev, "power_dc2otg missing!\n");

	ret = of_property_read_u32(np, "sample_res", &pdata->sample_res);
	if (ret < 0) {
		pdata->sample_res = SAMPLE_RES_20MR;
		dev_err(dev, "sample_res missing!\n");
	}

	ret = of_property_read_u32(np, "otg5v_suspend_enable",
				   &pdata->otg5v_suspend_enable);
	if (ret < 0) {
		pdata->otg5v_suspend_enable = 1;
		dev_err(dev, "otg5v_suspend_enable missing!\n");
	}

	ret = of_property_read_u32(np, "ts2_vol_multi",
				   &pdata->ts2_vol_multi);

	if (!is_battery_exist(cg))
		pdata->virtual_power = 1;

	cg->res_div = (cg->pdata->sample_res == SAMPLE_RES_20MR) ?
		       SAMPLE_RES_DIV1 : SAMPLE_RES_DIV2;

	if (!of_find_property(np, "dc_det_gpio", &ret)) {
		pdata->support_dc_det = false;
		CG_INFO("not support dc\n");
	} else {
		pdata->support_dc_det = true;
		pdata->dc_det_pin = of_get_named_gpio_flags(np, "dc_det_gpio",
							    0, &flags);
		if (gpio_is_valid(pdata->dc_det_pin)) {
			CG_INFO("support dc\n");
			pdata->dc_det_level = (flags & OF_GPIO_ACTIVE_LOW) ?
					       0 : 1;
		} else {
			dev_err(dev, "invalid dc det gpio!\n");
			return -EINVAL;
		}
	}

	ret = parse_temperature_chrg_table(cg, np);
	if (ret)
		return ret;

	DBG("input_current:%d\n"
	    "chrg_current:%d\n"
	    "chrg_voltage:%d\n"
	    "sample_res:%d\n"
	    "extcon:%d\n"
	    "ts2_vol_multi:%d\n"
	    "virtual_power:%d\n"
	    "power_dc2otg:%d\n",
	    pdata->max_input_current, pdata->max_chrg_current,
	    pdata->max_chrg_voltage, pdata->sample_res, pdata->extcon,
	    pdata->ts2_vol_multi, pdata->virtual_power, pdata->power_dc2otg);

	return 0;
}
#else
static int rk818_cg_parse_dt(struct rk818_charger *cg)
{
	return -ENODEV;
}
#endif

static int rk818_charger_probe(struct platform_device *pdev)
{
	struct rk808 *rk818 = dev_get_drvdata(pdev->dev.parent);
	struct rk818_charger *cg;
	int ret;

	cg = devm_kzalloc(&pdev->dev, sizeof(*cg), GFP_KERNEL);
	if (!cg)
		return -ENOMEM;

	cg->rk818 = rk818;
	cg->pdev = pdev;
	cg->dev = &pdev->dev;
	cg->regmap = rk818->regmap;
	platform_set_drvdata(pdev, cg);

	ret = rk818_cg_parse_dt(cg);
	if (ret < 0) {
		dev_err(cg->dev, "parse dt failed!\n");
		return ret;
	}

	rk818_cg_init_ts2_detect(cg);
	rk818_cg_get_otg5v_regulator(cg);

	ret = rk818_cg_init_dc(cg);
	if (ret) {
		dev_err(cg->dev, "init dc failed!\n");
		return ret;
	}

	ret = rk818_cg_init_usb(cg);
	if (ret) {
		dev_err(cg->dev, "init usb failed!\n");
		return ret;
	}

	ret = rk818_cg_init_power_supply(cg);
	if (ret) {
		dev_err(cg->dev, "init power supply fail!\n");
		return ret;
	}

	rk818_cg_init_charger_state(cg);

	ret = rk818_cg_register_temp_notifier(cg);
	if (ret) {
		dev_err(cg->dev, "register temp notify failed!\n");
		goto notify_fail;
	}

	ret = rk818_cg_init_irqs(cg);
	if (ret) {
		dev_err(cg->dev, "init irqs failed!\n");
		goto irq_fail;
	}

	CG_INFO("driver version: %s\n", CG_DRIVER_VERSION);

	return 0;

irq_fail:
	rk818_bat_temp_notifier_unregister(&cg->temp_nb);

notify_fail:
	/* type-c only */
	if (cg->pdata->extcon) {
		cancel_delayed_work_sync(&cg->host_work);
		cancel_delayed_work_sync(&cg->discnt_work);
	}

	cancel_delayed_work_sync(&cg->usb_work);
	cancel_delayed_work_sync(&cg->dc_work);
	cancel_delayed_work_sync(&cg->finish_sig_work);
	cancel_delayed_work_sync(&cg->irq_work);
	cancel_delayed_work_sync(&cg->ts2_vol_work);
	destroy_workqueue(cg->ts2_wq);
	destroy_workqueue(cg->usb_charger_wq);
	destroy_workqueue(cg->dc_charger_wq);
	destroy_workqueue(cg->finish_sig_wq);

	if (cg->pdata->extcon) {
		extcon_unregister_notifier(cg->cable_edev, EXTCON_CHG_USB_SDP,
					   &cg->cable_cg_nb);
		extcon_unregister_notifier(cg->cable_edev, EXTCON_CHG_USB_DCP,
					   &cg->cable_cg_nb);
		extcon_unregister_notifier(cg->cable_edev, EXTCON_CHG_USB_CDP,
					   &cg->cable_cg_nb);
		extcon_unregister_notifier(cg->cable_edev, EXTCON_USB_VBUS_EN,
					   &cg->cable_host_nb);
		extcon_unregister_notifier(cg->cable_edev, EXTCON_USB,
					   &cg->cable_discnt_nb);
	} else {
		rk_bc_detect_notifier_unregister(&cg->bc_nb);
	}

	return ret;
}

static void rk818_charger_shutdown(struct platform_device *pdev)
{
	struct rk818_charger *cg = platform_get_drvdata(pdev);

	/* type-c only */
	if (cg->pdata->extcon) {
		cancel_delayed_work_sync(&cg->host_work);
		cancel_delayed_work_sync(&cg->discnt_work);
	}

	cancel_delayed_work_sync(&cg->usb_work);
	cancel_delayed_work_sync(&cg->dc_work);
	cancel_delayed_work_sync(&cg->finish_sig_work);
	cancel_delayed_work_sync(&cg->irq_work);
	cancel_delayed_work_sync(&cg->ts2_vol_work);
	destroy_workqueue(cg->ts2_wq);
	destroy_workqueue(cg->usb_charger_wq);
	destroy_workqueue(cg->dc_charger_wq);
	destroy_workqueue(cg->finish_sig_wq);

	if (cg->pdata->extcon) {
		extcon_unregister_notifier(cg->cable_edev, EXTCON_CHG_USB_SDP,
					   &cg->cable_cg_nb);
		extcon_unregister_notifier(cg->cable_edev, EXTCON_CHG_USB_DCP,
					   &cg->cable_cg_nb);
		extcon_unregister_notifier(cg->cable_edev, EXTCON_CHG_USB_CDP,
					   &cg->cable_cg_nb);
		extcon_unregister_notifier(cg->cable_edev, EXTCON_USB_VBUS_EN,
					   &cg->cable_host_nb);
		extcon_unregister_notifier(cg->cable_edev, EXTCON_USB,
					   &cg->cable_discnt_nb);
	} else {
		rk_bc_detect_notifier_unregister(&cg->bc_nb);
	}

	rk818_bat_temp_notifier_unregister(&cg->temp_nb);

	rk818_cg_set_otg_state(cg, USB_OTG_POWER_OFF);
	rk818_cg_set_finish_sig(cg, CHRG_FINISH_ANA_SIGNAL);

	CG_INFO("shutdown: ac=%d usb=%d dc=%d otg=%d\n",
		cg->ac_in, cg->usb_in, cg->dc_in, cg->otg_in);
}

static int rk818_charger_suspend(struct platform_device *pdev,
				 pm_message_t state)
{
	struct rk818_charger *cg = platform_get_drvdata(pdev);

	cg->sleep_set_off_reg1 = rk818_reg_read(cg, RK818_SLEEP_SET_OFF_REG1);

	/* enable sleep boost5v and otg5v */
	if (cg->pdata->otg5v_suspend_enable) {
		if ((cg->otg_in && !cg->dc_in) ||
		    (cg->otg_in && cg->dc_in && !cg->pdata->power_dc2otg)) {
			rk818_reg_clear_bits(cg, RK818_SLEEP_SET_OFF_REG1,
					     OTG_BOOST_SLP_OFF);
			CG_INFO("suspend: otg 5v on\n");
			return 0;
		}
	}

	/* disable sleep otg5v */
	rk818_reg_set_bits(cg, RK818_SLEEP_SET_OFF_REG1,
			   OTG_SLP_SET_OFF, OTG_SLP_SET_OFF);
	CG_INFO("suspend: otg 5v off\n");

	return 0;
}

static int rk818_charger_resume(struct platform_device *pdev)
{
	struct rk818_charger *cg = platform_get_drvdata(pdev);

	/* resume sleep boost5v and otg5v */
	rk818_reg_set_bits(cg, RK818_SLEEP_SET_OFF_REG1,
			   OTG_BOOST_SLP_OFF, cg->sleep_set_off_reg1);

	return 0;
}

static struct platform_driver rk818_charger_driver = {
	.probe = rk818_charger_probe,
	.suspend = rk818_charger_suspend,
	.resume = rk818_charger_resume,
	.shutdown = rk818_charger_shutdown,
	.driver = {
		.name	= "rk818-charger",
	},
};

static int __init charger_init(void)
{
	return platform_driver_register(&rk818_charger_driver);
}
module_init(charger_init);

static void __exit charger_exit(void)
{
	platform_driver_unregister(&rk818_charger_driver);
}
module_exit(charger_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rk818-charger");
MODULE_AUTHOR("chenjh<chenjh@rock-chips.com>");
