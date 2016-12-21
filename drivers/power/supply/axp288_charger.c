/*
 * axp288_charger.c - X-power AXP288 PMIC Charger driver
 *
 * Copyright (C) 2014 Intel Corporation
 * Author: Ramakrishna Pallala <ramakrishna.pallala@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/usb/otg.h>
#include <linux/notifier.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/mfd/axp20x.h>
#include <linux/extcon.h>

#define PS_STAT_VBUS_TRIGGER		(1 << 0)
#define PS_STAT_BAT_CHRG_DIR		(1 << 2)
#define PS_STAT_VBAT_ABOVE_VHOLD	(1 << 3)
#define PS_STAT_VBUS_VALID		(1 << 4)
#define PS_STAT_VBUS_PRESENT		(1 << 5)

#define CHRG_STAT_BAT_SAFE_MODE		(1 << 3)
#define CHRG_STAT_BAT_VALID		(1 << 4)
#define CHRG_STAT_BAT_PRESENT		(1 << 5)
#define CHRG_STAT_CHARGING		(1 << 6)
#define CHRG_STAT_PMIC_OTP		(1 << 7)

#define VBUS_ISPOUT_CUR_LIM_MASK	0x03
#define VBUS_ISPOUT_CUR_LIM_BIT_POS	0
#define VBUS_ISPOUT_CUR_LIM_900MA	0x0	/* 900mA */
#define VBUS_ISPOUT_CUR_LIM_1500MA	0x1	/* 1500mA */
#define VBUS_ISPOUT_CUR_LIM_2000MA	0x2	/* 2000mA */
#define VBUS_ISPOUT_CUR_NO_LIM		0x3	/* 2500mA */
#define VBUS_ISPOUT_VHOLD_SET_MASK	0x31
#define VBUS_ISPOUT_VHOLD_SET_BIT_POS	0x3
#define VBUS_ISPOUT_VHOLD_SET_OFFSET	4000	/* 4000mV */
#define VBUS_ISPOUT_VHOLD_SET_LSB_RES	100	/* 100mV */
#define VBUS_ISPOUT_VHOLD_SET_4300MV	0x3	/* 4300mV */
#define VBUS_ISPOUT_VBUS_PATH_DIS	(1 << 7)

#define CHRG_CCCV_CC_MASK		0xf		/* 4 bits */
#define CHRG_CCCV_CC_BIT_POS		0
#define CHRG_CCCV_CC_OFFSET		200		/* 200mA */
#define CHRG_CCCV_CC_LSB_RES		200		/* 200mA */
#define CHRG_CCCV_ITERM_20P		(1 << 4)	/* 20% of CC */
#define CHRG_CCCV_CV_MASK		0x60		/* 2 bits */
#define CHRG_CCCV_CV_BIT_POS		5
#define CHRG_CCCV_CV_4100MV		0x0		/* 4.10V */
#define CHRG_CCCV_CV_4150MV		0x1		/* 4.15V */
#define CHRG_CCCV_CV_4200MV		0x2		/* 4.20V */
#define CHRG_CCCV_CV_4350MV		0x3		/* 4.35V */
#define CHRG_CCCV_CHG_EN		(1 << 7)

#define CNTL2_CC_TIMEOUT_MASK		0x3	/* 2 bits */
#define CNTL2_CC_TIMEOUT_OFFSET		6	/* 6 Hrs */
#define CNTL2_CC_TIMEOUT_LSB_RES	2	/* 2 Hrs */
#define CNTL2_CC_TIMEOUT_12HRS		0x3	/* 12 Hrs */
#define CNTL2_CHGLED_TYPEB		(1 << 4)
#define CNTL2_CHG_OUT_TURNON		(1 << 5)
#define CNTL2_PC_TIMEOUT_MASK		0xC0
#define CNTL2_PC_TIMEOUT_OFFSET		40	/* 40 mins */
#define CNTL2_PC_TIMEOUT_LSB_RES	10	/* 10 mins */
#define CNTL2_PC_TIMEOUT_70MINS		0x3

#define CHRG_ILIM_TEMP_LOOP_EN		(1 << 3)
#define CHRG_VBUS_ILIM_MASK		0xf0
#define CHRG_VBUS_ILIM_BIT_POS		4
#define CHRG_VBUS_ILIM_100MA		0x0	/* 100mA */
#define CHRG_VBUS_ILIM_500MA		0x1	/* 500mA */
#define CHRG_VBUS_ILIM_900MA		0x2	/* 900mA */
#define CHRG_VBUS_ILIM_1500MA		0x3	/* 1500mA */
#define CHRG_VBUS_ILIM_2000MA		0x4	/* 2000mA */
#define CHRG_VBUS_ILIM_2500MA		0x5	/* 2500mA */
#define CHRG_VBUS_ILIM_3000MA		0x6	/* 3000mA */

#define CHRG_VLTFC_0C			0xA5	/* 0 DegC */
#define CHRG_VHTFC_45C			0x1F	/* 45 DegC */

#define BAT_IRQ_CFG_CHRG_DONE		(1 << 2)
#define BAT_IRQ_CFG_CHRG_START		(1 << 3)
#define BAT_IRQ_CFG_BAT_SAFE_EXIT	(1 << 4)
#define BAT_IRQ_CFG_BAT_SAFE_ENTER	(1 << 5)
#define BAT_IRQ_CFG_BAT_DISCON		(1 << 6)
#define BAT_IRQ_CFG_BAT_CONN		(1 << 7)
#define BAT_IRQ_CFG_BAT_MASK		0xFC

#define TEMP_IRQ_CFG_QCBTU		(1 << 4)
#define TEMP_IRQ_CFG_CBTU		(1 << 5)
#define TEMP_IRQ_CFG_QCBTO		(1 << 6)
#define TEMP_IRQ_CFG_CBTO		(1 << 7)
#define TEMP_IRQ_CFG_MASK		0xF0

#define FG_CNTL_OCV_ADJ_EN		(1 << 3)

#define CV_4100MV			4100	/* 4100mV */
#define CV_4150MV			4150	/* 4150mV */
#define CV_4200MV			4200	/* 4200mV */
#define CV_4350MV			4350	/* 4350mV */

#define CC_200MA			200	/*  200mA */
#define CC_600MA			600	/*  600mA */
#define CC_800MA			800	/*  800mA */
#define CC_1000MA			1000	/* 1000mA */
#define CC_1600MA			1600	/* 1600mA */
#define CC_2000MA			2000	/* 2000mA */

#define ILIM_100MA			100	/* 100mA */
#define ILIM_500MA			500	/* 500mA */
#define ILIM_900MA			900	/* 900mA */
#define ILIM_1500MA			1500	/* 1500mA */
#define ILIM_2000MA			2000	/* 2000mA */
#define ILIM_2500MA			2500	/* 2500mA */
#define ILIM_3000MA			3000	/* 3000mA */

#define AXP288_EXTCON_DEV_NAME		"axp288_extcon"
#define USB_HOST_EXTCON_DEV_NAME	"INT3496:00"

enum {
	VBUS_OV_IRQ = 0,
	CHARGE_DONE_IRQ,
	CHARGE_CHARGING_IRQ,
	BAT_SAFE_QUIT_IRQ,
	BAT_SAFE_ENTER_IRQ,
	QCBTU_IRQ,
	CBTU_IRQ,
	QCBTO_IRQ,
	CBTO_IRQ,
	CHRG_INTR_END,
};

struct axp288_chrg_info {
	struct platform_device *pdev;
	struct regmap *regmap;
	struct regmap_irq_chip_data *regmap_irqc;
	int irq[CHRG_INTR_END];
	struct power_supply *psy_usb;
	struct mutex lock;

	/* OTG/Host mode */
	struct {
		struct work_struct work;
		struct extcon_dev *cable;
		struct notifier_block id_nb;
		bool id_short;
	} otg;

	/* SDP/CDP/DCP USB charging cable notifications */
	struct {
		struct extcon_dev *edev;
		bool connected;
		enum power_supply_type chg_type;
		struct notifier_block nb;
		struct work_struct work;
	} cable;

	int inlmt;
	int cc;
	int cv;
	int max_cc;
	int max_cv;
	bool is_charger_enabled;
};

static inline int axp288_charger_set_cc(struct axp288_chrg_info *info, int cc)
{
	u8 reg_val;
	int ret;

	if (cc < CHRG_CCCV_CC_OFFSET)
		cc = CHRG_CCCV_CC_OFFSET;
	else if (cc > info->max_cc)
		cc = info->max_cc;

	reg_val = (cc - CHRG_CCCV_CC_OFFSET) / CHRG_CCCV_CC_LSB_RES;
	cc = (reg_val * CHRG_CCCV_CC_LSB_RES) + CHRG_CCCV_CC_OFFSET;
	reg_val = reg_val << CHRG_CCCV_CC_BIT_POS;

	ret = regmap_update_bits(info->regmap,
				AXP20X_CHRG_CTRL1,
				CHRG_CCCV_CC_MASK, reg_val);
	if (ret >= 0)
		info->cc = cc;

	return ret;
}

static inline int axp288_charger_set_cv(struct axp288_chrg_info *info, int cv)
{
	u8 reg_val;
	int ret;

	if (cv <= CV_4100MV) {
		reg_val = CHRG_CCCV_CV_4100MV;
		cv = CV_4100MV;
	} else if (cv <= CV_4150MV) {
		reg_val = CHRG_CCCV_CV_4150MV;
		cv = CV_4150MV;
	} else if (cv <= CV_4200MV) {
		reg_val = CHRG_CCCV_CV_4200MV;
		cv = CV_4200MV;
	} else {
		reg_val = CHRG_CCCV_CV_4350MV;
		cv = CV_4350MV;
	}

	reg_val = reg_val << CHRG_CCCV_CV_BIT_POS;

	ret = regmap_update_bits(info->regmap,
				AXP20X_CHRG_CTRL1,
				CHRG_CCCV_CV_MASK, reg_val);

	if (ret >= 0)
		info->cv = cv;

	return ret;
}

static inline int axp288_charger_set_vbus_inlmt(struct axp288_chrg_info *info,
					   int inlmt)
{
	int ret;
	unsigned int val;
	u8 reg_val;

	/* Read in limit register */
	ret = regmap_read(info->regmap, AXP20X_CHRG_BAK_CTRL, &val);
	if (ret < 0)
		goto set_inlmt_fail;

	if (inlmt <= ILIM_100MA) {
		reg_val = CHRG_VBUS_ILIM_100MA;
		inlmt = ILIM_100MA;
	} else if (inlmt <= ILIM_500MA) {
		reg_val = CHRG_VBUS_ILIM_500MA;
		inlmt = ILIM_500MA;
	} else if (inlmt <= ILIM_900MA) {
		reg_val = CHRG_VBUS_ILIM_900MA;
		inlmt = ILIM_900MA;
	} else if (inlmt <= ILIM_1500MA) {
		reg_val = CHRG_VBUS_ILIM_1500MA;
		inlmt = ILIM_1500MA;
	} else if (inlmt <= ILIM_2000MA) {
		reg_val = CHRG_VBUS_ILIM_2000MA;
		inlmt = ILIM_2000MA;
	} else if (inlmt <= ILIM_2500MA) {
		reg_val = CHRG_VBUS_ILIM_2500MA;
		inlmt = ILIM_2500MA;
	} else {
		reg_val = CHRG_VBUS_ILIM_3000MA;
		inlmt = ILIM_3000MA;
	}

	reg_val = (val & ~CHRG_VBUS_ILIM_MASK)
			| (reg_val << CHRG_VBUS_ILIM_BIT_POS);
	ret = regmap_write(info->regmap, AXP20X_CHRG_BAK_CTRL, reg_val);
	if (ret >= 0)
		info->inlmt = inlmt;
	else
		dev_err(&info->pdev->dev, "charger BAK control %d\n", ret);


set_inlmt_fail:
	return ret;
}

static int axp288_charger_vbus_path_select(struct axp288_chrg_info *info,
								bool enable)
{
	int ret;

	if (enable)
		ret = regmap_update_bits(info->regmap, AXP20X_VBUS_IPSOUT_MGMT,
					VBUS_ISPOUT_VBUS_PATH_DIS, 0);
	else
		ret = regmap_update_bits(info->regmap, AXP20X_VBUS_IPSOUT_MGMT,
			VBUS_ISPOUT_VBUS_PATH_DIS, VBUS_ISPOUT_VBUS_PATH_DIS);

	if (ret < 0)
		dev_err(&info->pdev->dev, "axp288 vbus path select %d\n", ret);


	return ret;
}

static int axp288_charger_enable_charger(struct axp288_chrg_info *info,
								bool enable)
{
	int ret;

	if (enable == info->is_charger_enabled)
		return 0;

	if (enable)
		ret = regmap_update_bits(info->regmap, AXP20X_CHRG_CTRL1,
				CHRG_CCCV_CHG_EN, CHRG_CCCV_CHG_EN);
	else
		ret = regmap_update_bits(info->regmap, AXP20X_CHRG_CTRL1,
				CHRG_CCCV_CHG_EN, 0);
	if (ret < 0)
		dev_err(&info->pdev->dev, "axp288 enable charger %d\n", ret);
	else
		info->is_charger_enabled = enable;

	return ret;
}

static int axp288_charger_is_present(struct axp288_chrg_info *info)
{
	int ret, present = 0;
	unsigned int val;

	ret = regmap_read(info->regmap, AXP20X_PWR_INPUT_STATUS, &val);
	if (ret < 0)
		return ret;

	if (val & PS_STAT_VBUS_PRESENT)
		present = 1;
	return present;
}

static int axp288_charger_is_online(struct axp288_chrg_info *info)
{
	int ret, online = 0;
	unsigned int val;

	ret = regmap_read(info->regmap, AXP20X_PWR_INPUT_STATUS, &val);
	if (ret < 0)
		return ret;

	if (val & PS_STAT_VBUS_VALID)
		online = 1;
	return online;
}

static int axp288_get_charger_health(struct axp288_chrg_info *info)
{
	int ret, pwr_stat, chrg_stat;
	int health = POWER_SUPPLY_HEALTH_UNKNOWN;
	unsigned int val;

	ret = regmap_read(info->regmap, AXP20X_PWR_INPUT_STATUS, &val);
	if ((ret < 0) || !(val & PS_STAT_VBUS_PRESENT))
		goto health_read_fail;
	else
		pwr_stat = val;

	ret = regmap_read(info->regmap, AXP20X_PWR_OP_MODE, &val);
	if (ret < 0)
		goto health_read_fail;
	else
		chrg_stat = val;

	if (!(pwr_stat & PS_STAT_VBUS_VALID))
		health = POWER_SUPPLY_HEALTH_DEAD;
	else if (chrg_stat & CHRG_STAT_PMIC_OTP)
		health = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (chrg_stat & CHRG_STAT_BAT_SAFE_MODE)
		health = POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE;
	else
		health = POWER_SUPPLY_HEALTH_GOOD;

health_read_fail:
	return health;
}

static int axp288_charger_usb_set_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    const union power_supply_propval *val)
{
	struct axp288_chrg_info *info = power_supply_get_drvdata(psy);
	int ret = 0;
	int scaled_val;

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		scaled_val = min(val->intval, info->max_cc);
		scaled_val = DIV_ROUND_CLOSEST(scaled_val, 1000);
		ret = axp288_charger_set_cc(info, scaled_val);
		if (ret < 0)
			dev_warn(&info->pdev->dev, "set charge current failed\n");
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		scaled_val = min(val->intval, info->max_cv);
		scaled_val = DIV_ROUND_CLOSEST(scaled_val, 1000);
		ret = axp288_charger_set_cv(info, scaled_val);
		if (ret < 0)
			dev_warn(&info->pdev->dev, "set charge voltage failed\n");
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int axp288_charger_usb_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	struct axp288_chrg_info *info = power_supply_get_drvdata(psy);
	int ret = 0;

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		/* Check for OTG case first */
		if (info->otg.id_short) {
			val->intval = 0;
			break;
		}
		ret = axp288_charger_is_present(info);
		if (ret < 0)
			goto psy_get_prop_fail;
		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		/* Check for OTG case first */
		if (info->otg.id_short) {
			val->intval = 0;
			break;
		}
		ret = axp288_charger_is_online(info);
		if (ret < 0)
			goto psy_get_prop_fail;
		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = axp288_get_charger_health(info);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		val->intval = info->cc * 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = info->max_cc * 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = info->cv * 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = info->max_cv * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		val->intval = info->inlmt * 1000;
		break;
	default:
		ret = -EINVAL;
		goto psy_get_prop_fail;
	}

psy_get_prop_fail:
	mutex_unlock(&info->lock);
	return ret;
}

static int axp288_charger_property_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = 1;
		break;
	default:
		ret = 0;
	}

	return ret;
}

static enum power_supply_property axp288_usb_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
};

static const struct power_supply_desc axp288_charger_desc = {
	.name			= "axp288_charger",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= axp288_usb_props,
	.num_properties		= ARRAY_SIZE(axp288_usb_props),
	.get_property		= axp288_charger_usb_get_property,
	.set_property		= axp288_charger_usb_set_property,
	.property_is_writeable	= axp288_charger_property_is_writeable,
};

static irqreturn_t axp288_charger_irq_thread_handler(int irq, void *dev)
{
	struct axp288_chrg_info *info = dev;
	int i;

	for (i = 0; i < CHRG_INTR_END; i++) {
		if (info->irq[i] == irq)
			break;
	}

	if (i >= CHRG_INTR_END) {
		dev_warn(&info->pdev->dev, "spurious interrupt!!\n");
		return IRQ_NONE;
	}

	switch (i) {
	case VBUS_OV_IRQ:
		dev_dbg(&info->pdev->dev, "VBUS Over Voltage INTR\n");
		break;
	case CHARGE_DONE_IRQ:
		dev_dbg(&info->pdev->dev, "Charging Done INTR\n");
		break;
	case CHARGE_CHARGING_IRQ:
		dev_dbg(&info->pdev->dev, "Start Charging IRQ\n");
		break;
	case BAT_SAFE_QUIT_IRQ:
		dev_dbg(&info->pdev->dev,
			"Quit Safe Mode(restart timer) Charging IRQ\n");
		break;
	case BAT_SAFE_ENTER_IRQ:
		dev_dbg(&info->pdev->dev,
			"Enter Safe Mode(timer expire) Charging IRQ\n");
		break;
	case QCBTU_IRQ:
		dev_dbg(&info->pdev->dev,
			"Quit Battery Under Temperature(CHRG) INTR\n");
		break;
	case CBTU_IRQ:
		dev_dbg(&info->pdev->dev,
			"Hit Battery Under Temperature(CHRG) INTR\n");
		break;
	case QCBTO_IRQ:
		dev_dbg(&info->pdev->dev,
			"Quit Battery Over Temperature(CHRG) INTR\n");
		break;
	case CBTO_IRQ:
		dev_dbg(&info->pdev->dev,
			"Hit Battery Over Temperature(CHRG) INTR\n");
		break;
	default:
		dev_warn(&info->pdev->dev, "Spurious Interrupt!!!\n");
		goto out;
	}

	power_supply_changed(info->psy_usb);
out:
	return IRQ_HANDLED;
}

static void axp288_charger_extcon_evt_worker(struct work_struct *work)
{
	struct axp288_chrg_info *info =
	    container_of(work, struct axp288_chrg_info, cable.work);
	int ret, current_limit;
	struct extcon_dev *edev = info->cable.edev;
	bool old_connected = info->cable.connected;
	enum power_supply_type old_chg_type = info->cable.chg_type;

	/* Determine cable/charger type */
	if (extcon_get_state(edev, EXTCON_CHG_USB_SDP) > 0) {
		dev_dbg(&info->pdev->dev, "USB SDP charger  is connected");
		info->cable.connected = true;
		info->cable.chg_type = POWER_SUPPLY_TYPE_USB;
	} else if (extcon_get_state(edev, EXTCON_CHG_USB_CDP) > 0) {
		dev_dbg(&info->pdev->dev, "USB CDP charger is connected");
		info->cable.connected = true;
		info->cable.chg_type = POWER_SUPPLY_TYPE_USB_CDP;
	} else if (extcon_get_state(edev, EXTCON_CHG_USB_DCP) > 0) {
		dev_dbg(&info->pdev->dev, "USB DCP charger is connected");
		info->cable.connected = true;
		info->cable.chg_type = POWER_SUPPLY_TYPE_USB_DCP;
	} else {
		if (old_connected)
			dev_dbg(&info->pdev->dev, "USB charger disconnected");
		info->cable.connected = false;
		info->cable.chg_type = POWER_SUPPLY_TYPE_USB;
	}

	/* Cable status changed */
	if (old_connected == info->cable.connected &&
	    old_chg_type == info->cable.chg_type)
		return;

	mutex_lock(&info->lock);

	if (info->cable.connected) {
		axp288_charger_enable_charger(info, false);

		switch (info->cable.chg_type) {
		case POWER_SUPPLY_TYPE_USB:
			current_limit = ILIM_500MA;
			break;
		case POWER_SUPPLY_TYPE_USB_CDP:
			current_limit = ILIM_1500MA;
			break;
		case POWER_SUPPLY_TYPE_USB_DCP:
			current_limit = ILIM_2000MA;
			break;
		default:
			/* Unknown */
			current_limit = 0;
			break;
		}

		/* Set vbus current limit first, then enable charger */
		ret = axp288_charger_set_vbus_inlmt(info, current_limit);
		if (ret == 0)
			axp288_charger_enable_charger(info, true);
		else
			dev_err(&info->pdev->dev,
				"error setting current limit (%d)", ret);
	} else {
		axp288_charger_enable_charger(info, false);
	}

	mutex_unlock(&info->lock);

	power_supply_changed(info->psy_usb);
}

static int axp288_charger_handle_cable_evt(struct notifier_block *nb,
					  unsigned long event, void *param)
{
	struct axp288_chrg_info *info =
	    container_of(nb, struct axp288_chrg_info, cable.nb);

	schedule_work(&info->cable.work);

	return NOTIFY_OK;
}

static void axp288_charger_otg_evt_worker(struct work_struct *work)
{
	struct axp288_chrg_info *info =
	    container_of(work, struct axp288_chrg_info, otg.work);
	int ret;

	/* Disable VBUS path before enabling the 5V boost */
	ret = axp288_charger_vbus_path_select(info, !info->otg.id_short);
	if (ret < 0)
		dev_warn(&info->pdev->dev, "vbus path disable failed\n");
}

static int axp288_charger_handle_otg_evt(struct notifier_block *nb,
				   unsigned long event, void *param)
{
	struct axp288_chrg_info *info =
	    container_of(nb, struct axp288_chrg_info, otg.id_nb);
	struct extcon_dev *edev = info->otg.cable;
	int usb_host = extcon_get_state(edev, EXTCON_USB_HOST);

	dev_dbg(&info->pdev->dev, "external connector USB-Host is %s\n",
				usb_host ? "attached" : "detached");

	/*
	 * Set usb_id_short flag to avoid running charger detection logic
	 * in case usb host.
	 */
	info->otg.id_short = usb_host;
	schedule_work(&info->otg.work);

	return NOTIFY_OK;
}

static int charger_init_hw_regs(struct axp288_chrg_info *info)
{
	int ret, cc, cv;
	unsigned int val;

	/* Program temperature thresholds */
	ret = regmap_write(info->regmap, AXP20X_V_LTF_CHRG, CHRG_VLTFC_0C);
	if (ret < 0) {
		dev_err(&info->pdev->dev, "register(%x) write error(%d)\n",
							AXP20X_V_LTF_CHRG, ret);
		return ret;
	}

	ret = regmap_write(info->regmap, AXP20X_V_HTF_CHRG, CHRG_VHTFC_45C);
	if (ret < 0) {
		dev_err(&info->pdev->dev, "register(%x) write error(%d)\n",
							AXP20X_V_HTF_CHRG, ret);
		return ret;
	}

	/* Do not turn-off charger o/p after charge cycle ends */
	ret = regmap_update_bits(info->regmap,
				AXP20X_CHRG_CTRL2,
				CNTL2_CHG_OUT_TURNON, 1);
	if (ret < 0) {
		dev_err(&info->pdev->dev, "register(%x) write error(%d)\n",
						AXP20X_CHRG_CTRL2, ret);
		return ret;
	}

	/* Enable interrupts */
	ret = regmap_update_bits(info->regmap,
				AXP20X_IRQ2_EN,
				BAT_IRQ_CFG_BAT_MASK, 1);
	if (ret < 0) {
		dev_err(&info->pdev->dev, "register(%x) write error(%d)\n",
						AXP20X_IRQ2_EN, ret);
		return ret;
	}

	ret = regmap_update_bits(info->regmap, AXP20X_IRQ3_EN,
				TEMP_IRQ_CFG_MASK, 1);
	if (ret < 0) {
		dev_err(&info->pdev->dev, "register(%x) write error(%d)\n",
						AXP20X_IRQ3_EN, ret);
		return ret;
	}

	/* Setup ending condition for charging to be 10% of I(chrg) */
	ret = regmap_update_bits(info->regmap,
				AXP20X_CHRG_CTRL1,
				CHRG_CCCV_ITERM_20P, 0);
	if (ret < 0) {
		dev_err(&info->pdev->dev, "register(%x) write error(%d)\n",
						AXP20X_CHRG_CTRL1, ret);
		return ret;
	}

	/* Disable OCV-SOC curve calibration */
	ret = regmap_update_bits(info->regmap,
				AXP20X_CC_CTRL,
				FG_CNTL_OCV_ADJ_EN, 0);
	if (ret < 0) {
		dev_err(&info->pdev->dev, "register(%x) write error(%d)\n",
						AXP20X_CC_CTRL, ret);
		return ret;
	}

	/* Read current charge voltage and current limit */
	ret = regmap_read(info->regmap, AXP20X_CHRG_CTRL1, &val);
	if (ret < 0) {
		dev_err(&info->pdev->dev, "register(%x) read error(%d)\n",
			AXP20X_CHRG_CTRL1, ret);
		return ret;
	}

	/* Determine charge voltage */
	cv = (val & CHRG_CCCV_CV_MASK) >> CHRG_CCCV_CV_BIT_POS;
	switch (cv) {
	case CHRG_CCCV_CV_4100MV:
		info->cv = CV_4100MV;
		break;
	case CHRG_CCCV_CV_4150MV:
		info->cv = CV_4150MV;
		break;
	case CHRG_CCCV_CV_4200MV:
		info->cv = CV_4200MV;
		break;
	case CHRG_CCCV_CV_4350MV:
		info->cv = CV_4350MV;
		break;
	}

	/* Determine charge current limit */
	cc = (ret & CHRG_CCCV_CC_MASK) >> CHRG_CCCV_CC_BIT_POS;
	cc = (cc * CHRG_CCCV_CC_LSB_RES) + CHRG_CCCV_CC_OFFSET;
	info->cc = cc;

	/*
	 * Do not allow the user to configure higher settings then those
	 * set by the firmware
	 */
	info->max_cv = info->cv;
	info->max_cc = info->cc;

	return 0;
}

static int axp288_charger_probe(struct platform_device *pdev)
{
	int ret, i, pirq;
	struct axp288_chrg_info *info;
	struct device *dev = &pdev->dev;
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	struct power_supply_config charger_cfg = {};
	unsigned int cable_ids[] = { EXTCON_CHG_USB_SDP, EXTCON_CHG_USB_CDP,
				     EXTCON_CHG_USB_DCP };

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->pdev = pdev;
	info->regmap = axp20x->regmap;
	info->regmap_irqc = axp20x->regmap_irqc;

	info->cable.edev = extcon_get_extcon_dev(AXP288_EXTCON_DEV_NAME);
	if (info->cable.edev == NULL) {
		dev_dbg(&pdev->dev, "%s is not ready, probe deferred\n",
			AXP288_EXTCON_DEV_NAME);
		return -EPROBE_DEFER;
	}

	info->otg.cable = extcon_get_extcon_dev(USB_HOST_EXTCON_DEV_NAME);
	if (info->otg.cable == NULL) {
		dev_dbg(dev, "EXTCON_USB_HOST is not ready, probe deferred\n");
		return -EPROBE_DEFER;
	}

	platform_set_drvdata(pdev, info);
	mutex_init(&info->lock);

	ret = charger_init_hw_regs(info);
	if (ret)
		return ret;

	/* Register with power supply class */
	charger_cfg.drv_data = info;
	info->psy_usb = devm_power_supply_register(dev, &axp288_charger_desc,
						   &charger_cfg);
	if (IS_ERR(info->psy_usb)) {
		ret = PTR_ERR(info->psy_usb);
		dev_err(dev, "failed to register power supply: %d\n", ret);
		return ret;
	}

	/* Register for extcon notification */
	INIT_WORK(&info->cable.work, axp288_charger_extcon_evt_worker);
	info->cable.nb.notifier_call = axp288_charger_handle_cable_evt;
	for (i = 0; i < ARRAY_SIZE(cable_ids); i++) {
		ret = devm_extcon_register_notifier(dev, info->cable.edev,
						cable_ids[i], &info->cable.nb);
		if (ret) {
			dev_err(dev, "failed to register extcon notifier for %u: %d\n",
				cable_ids[i], ret);
			return ret;
		}
	}

	/* Register for OTG notification */
	INIT_WORK(&info->otg.work, axp288_charger_otg_evt_worker);
	info->otg.id_nb.notifier_call = axp288_charger_handle_otg_evt;
	ret = devm_extcon_register_notifier(&pdev->dev, info->otg.cable,
					EXTCON_USB_HOST, &info->otg.id_nb);
	if (ret) {
		dev_err(dev, "failed to register EXTCON_USB_HOST notifier\n");
		return ret;
	}
	info->otg.id_short = extcon_get_cable_state_(info->otg.cable,
						     EXTCON_USB_HOST);

	/* Register charger interrupts */
	for (i = 0; i < CHRG_INTR_END; i++) {
		pirq = platform_get_irq(info->pdev, i);
		info->irq[i] = regmap_irq_get_virq(info->regmap_irqc, pirq);
		if (info->irq[i] < 0) {
			dev_warn(&info->pdev->dev,
				"failed to get virtual interrupt=%d\n", pirq);
			return info->irq[i];
		}
		ret = devm_request_threaded_irq(&info->pdev->dev, info->irq[i],
					NULL, axp288_charger_irq_thread_handler,
					IRQF_ONESHOT, info->pdev->name, info);
		if (ret) {
			dev_err(&pdev->dev, "failed to request interrupt=%d\n",
								info->irq[i]);
			return ret;
		}
	}

	return 0;
}

static struct platform_driver axp288_charger_driver = {
	.probe = axp288_charger_probe,
	.driver = {
		.name = "axp288_charger",
	},
};

module_platform_driver(axp288_charger_driver);

MODULE_AUTHOR("Ramakrishna Pallala <ramakrishna.pallala@intel.com>");
MODULE_DESCRIPTION("X-power AXP288 Charger Driver");
MODULE_LICENSE("GPL v2");
