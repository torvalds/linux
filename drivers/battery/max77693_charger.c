/*
 * max77693_charger.c
 *
 * Copyright (C) 2011 Samsung Electronics
 * SangYoung Son <hello.son@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/battery/samsung_battery.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/mfd/max77693.h>
#include <linux/mfd/max77693-private.h>
#include <linux/power/charger-manager.h>
#ifdef CONFIG_USB_HOST_NOTIFY
#include <linux/host_notify.h>
#include <plat/devs.h>
#endif
#include <plat/gpio-cfg.h>
#if defined(CONFIG_TARGET_LOCALE_KOR)
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif
#endif

/* MAX77693 Registers(defined @max77693-private.h) */

/* MAX77693_CHG_REG_CHG_INT */
#define MAX77693_BYP_I			(1 << 0)
#define MAX77693_THM_I			(1 << 2)
#define MAX77693_BAT_I			(1 << 3)
#define MAX77693_CHG_I			(1 << 4)
#define MAX77693_CHGIN_I		(1 << 6)

/* MAX77693_CHG_REG_CHG_INT_MASK */
#define MAX77693_BYP_IM			(1 << 0)
#define MAX77693_THM_IM			(1 << 2)
#define MAX77693_BAT_IM			(1 << 3)
#define MAX77693_CHG_IM			(1 << 4)
#define MAX77693_CHGIN_IM		(1 << 6)

/* MAX77693_CHG_REG_CHG_INT_OK */
#define MAX77693_BYP_OK			0x01
#define MAX77693_BYP_OK_SHIFT		0
#define MAX77693_THM_OK			0x04
#define MAX77693_THM_OK_SHIFT		2
#define MAX77693_BAT_OK			0x08
#define MAX77693_BAT_OK_SHIFT		3
#define MAX77693_CHG_OK			0x10
#define MAX77693_CHG_OK_SHIFT		4
#define MAX77693_CHGIN_OK		0x40
#define MAX77693_CHGIN_OK_SHIFT		6
#define MAX77693_DETBAT			0x80
#define MAX77693_DETBAT_SHIFT		7

/* MAX77693_CHG_REG_CHG_DTLS_00 */
#define MAX77693_THM_DTLS		0x07
#define MAX77693_THM_DTLS_SHIFT		0
#define MAX77693_CHGIN_DTLS		0x60
#define MAX77693_CHGIN_DTLS_SHIFT	5

/* MAX77693_CHG_REG_CHG_DTLS_01 */
#define MAX77693_CHG_DTLS		0x0F
#define MAX77693_CHG_DTLS_SHIFT		0
#define MAX77693_BAT_DTLS		0x70
#define MAX77693_BAT_DTLS_SHIFT		4

/* MAX77693_CHG_REG_CHG_DTLS_02 */
#define MAX77693_BYP_DTLS		0x0F
#define MAX77693_BYP_DTLS_SHIFT		0
#define MAX77693_BYP_DTLS0	0x1
#define MAX77693_BYP_DTLS1	0x2
#define MAX77693_BYP_DTLS2	0x4
#define MAX77693_BYP_DTLS3	0x8

/* MAX77693_CHG_REG_CHG_CNFG_00 */
#define MAX77693_MODE_DEFAULT	0x04
#define MAX77693_MODE_CHGR	0x01
#define MAX77693_MODE_OTG	0x02
#define MAX77693_MODE_BUCK	0x04

/* MAX77693_CHG_REG_CHG_CNFG_02 */
#define MAX77693_CHG_CC		0x3F

/* MAX77693_CHG_REG_CHG_CNFG_03 */
#define MAX77693_TO_ITH_MASK	0x06
#define MAX77693_TO_ITH_SHIFT	0
#define MAX77693_TO_TIME_MASK	0x38
#define MAX77693_TO_TIME_SHIFT	3

/* MAX77693_CHG_REG_CHG_CNFG_04 */
#define MAX77693_CHG_MINVSYS_MASK	0xE0
#define MAX77693_CHG_MINVSYS_SHIFT	5
#define MAX77693_CHG_MINVSYS_3_6V	0x06
#define MAX77693_CHG_CV_PRM_MASK		0x1F
#define MAX77693_CHG_CV_PRM_SHIFT		0
#define MAX77693_CHG_CV_PRM_4_20V		0x16
#define MAX77693_CHG_CV_PRM_4_35V		0x1D
#define MAX77693_CHG_CV_PRM_4_40V		0x1F

/* MAX77693_CHG_REG_CHG_CNFG_06 */
#define MAX77693_CHG_CHGPROT		0x0C
#define MAX77693_CHG_CHGPROT_SHIFT	2
#define MAX77693_CHG_CHGPROT_UNLOCK	0x03

/* MAX77693_CHG_REG_CHG_CNFG_09 */
#define MAX77693_CHG_CHGIN_LIM	0x7F

/* MAX77693_MUIC_REG_CDETCTRL1 */
#define MAX77693_CHGTYPMAN		0x02
#define MAX77693_CHGTYPMAN_SHIFT	1

/* MAX77693_MUIC_REG_STATUS2 */
#define MAX77693_VBVOLT			0x40
#define MAX77693_VBVOLT_SHIFT		6
#define MAX77693_DXOVP			0x20
#define MAX77693_DXOVP_SHIFT		5
#define MAX77693_CHGDETRUN		0x08
#define MAX77693_CHGDETRUN_SHIFT	3
#define MAX77693_CHGTYPE		0x07
#define MAX77693_CHGTYPE_SHIFT		0

/* irq */
#define IRQ_DEBOUNCE_TIME	20	/* msec */

/* charger unlock */
#define CHG_UNLOCK_RETRY	10
#define CHG_UNLOCK_DELAY	100

/* power stabe guarantee */
#define STABLE_POWER_DELAY	500

/* charger type detection */
#define DET_ERR_RETRY	5
#define DET_ERR_DELAY	200

/* soft charging */
#define SOFT_CHG_START_CURR	100	/* mA */
#define SOFT_CHG_START_DUR	100	/* ms */
#define SOFT_CHG_CURR_STEP	100	/* mA */
#define SOFT_CHG_STEP_DUR	20	/* ms */

/* soft regulation */
#define SW_REG_CURR_STEP_MA	100
#define SW_REG_CURR_MIN_MA	100
#define SW_REG_START_DELAY	500
#define SW_REG_STEP_DELAY	50

struct max77693_charger_data {
	struct max77693_dev	*max77693;

	struct power_supply	charger;

	struct delayed_work	update_work;
	struct delayed_work	softreg_work;

	/* mutex */
	struct mutex irq_lock;
	struct mutex ops_lock;

	/* wakelock */
	struct wake_lock update_wake_lock;
	struct wake_lock softreg_wake_lock;

	unsigned int	charging_state;
	unsigned int	charging_type;
	unsigned int	battery_state;
	unsigned int	battery_present;
	unsigned int	cable_type;
	unsigned int	cable_sub_type;
	unsigned int	cable_pwr_type;
	unsigned int	dock_type;
	unsigned int	charging_current;
	unsigned int	vbus_state;

	int		irq_bypass;
	int		irq_therm;
	int		irq_battery;
	int		irq_charge;
	int		irq_chargin;

	/* software regulation */
	bool		soft_reg_state;
	int		soft_reg_current;
	bool		soft_reg_ing;

	/* unsufficient power */
	bool		reg_loop_deted;

#ifdef CONFIG_BATTERY_WPC_CHARGER
	/* wireless charge, w(wpc), v(vbus) */
	int		wc_w_gpio;
	int		wc_w_irq;
	int		wc_w_state;
	int		wc_v_gpio;
	int		wc_v_irq;
	int		wc_v_state;
	bool		wc_pwr_det;
#endif

	struct max77693_charger_platform_data	*charger_pdata;

	int		irq;
	u8		irq_reg;
	int		irq_cnt;

#if defined(CONFIG_TARGET_LOCALE_KOR)
#ifdef CONFIG_DEBUG_FS
	struct dentry *charger_debugfs_dir;
#endif
#endif
};

static void max77693_dump_reg(struct max77693_charger_data *chg_data)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	u8 reg_data;
	u32 reg_addr;
	pr_info("%s\n", __func__);

	for (reg_addr = 0xB0; reg_addr <= 0xC5; reg_addr++) {
		max77693_read_reg(i2c, reg_addr, &reg_data);
		pr_info("max77693: c: 0x%02x(0x%02x)\n", reg_addr, reg_data);
	}
}

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
static bool max77693_charger_unlock(struct max77693_charger_data *chg_data);
static void max77693_charger_reg_init(struct max77693_charger_data *chg_data);

static void check_charger_unlock_state(struct max77693_charger_data *chg_data)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	bool need_reg_init = false;
	pr_debug("%s\n", __func__);

	need_reg_init = max77693_charger_unlock(chg_data);
	if (need_reg_init) {
		pr_err("%s: charger locked state, reg init\n", __func__);
		max77693_charger_reg_init(chg_data);
	}
}
#endif

static int max77693_get_topoff_state(struct max77693_charger_data *chg_data)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	u8 reg_data;
	pr_debug("%s\n", __func__);

	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_DTLS_01, &reg_data);
	reg_data = ((reg_data & MAX77693_CHG_DTLS) >> MAX77693_CHG_DTLS_SHIFT);
	pr_debug("%s: CHG_DTLS(0x%02x)\n", __func__, reg_data);

	return (reg_data == 0x4);
}

static int max77693_get_battery_present(struct max77693_charger_data *chg_data)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	u8 reg_data;
	pr_debug("%s\n", __func__);

	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_INT_OK, &reg_data);
	pr_debug("%s: CHG_INT_OK(0x%02x)\n", __func__, reg_data);

	reg_data = ((reg_data & MAX77693_DETBAT) >> MAX77693_DETBAT_SHIFT);

	return !reg_data;
}

static int max77693_get_vbus_state(struct max77693_charger_data *chg_data)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	int state;
	u8 reg_data;
	pr_debug("%s\n", __func__);

	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_DTLS_00, &reg_data);
	reg_data = ((reg_data & MAX77693_CHGIN_DTLS) >>
				MAX77693_CHGIN_DTLS_SHIFT);
	pr_debug("%s: CHGIN_DTLS(0x%02x)\n", __func__, reg_data);

	switch (reg_data) {
	case 0x00:
		state = POWER_SUPPLY_VBUS_UVLO;
		break;
	case 0x01:
		state = POWER_SUPPLY_VBUS_WEAK;
		break;
	case 0x02:
		state = POWER_SUPPLY_VBUS_OVLO;
		break;
	case 0x03:
		state = POWER_SUPPLY_VBUS_GOOD;
		break;
	default:
		state = POWER_SUPPLY_VBUS_UNKNOWN;
		break;
	}

	chg_data->vbus_state = state;
	return state;
}

static int max77693_get_charger_type(struct max77693_charger_data *chg_data)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	int state;
	u8 reg_data;
	pr_debug("%s\n", __func__);

	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_DTLS_01, &reg_data);
	reg_data = ((reg_data & MAX77693_CHG_DTLS) >> MAX77693_CHG_DTLS_SHIFT);
	pr_debug("%s: CHG_DTLS(0x%02x)\n", __func__, reg_data);

	switch (reg_data) {
	case 0x0:
		state = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	case 0x1:
	case 0x2:
	case 0x3:
		state = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case 0x4:
	case 0x8:
	case 0xA:
	case 0xB:
		state = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	default:
		state = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		break;
	}

	chg_data->charging_type = state;
	return state;
}

static int max77693_get_charger_state(struct max77693_charger_data *chg_data)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	int state;
	u8 reg_data;
	pr_debug("%s\n", __func__);

	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_DTLS_01, &reg_data);
	reg_data = ((reg_data & MAX77693_CHG_DTLS) >> MAX77693_CHG_DTLS_SHIFT);
	pr_debug("%s: CHG_DTLS(0x%02x)\n", __func__, reg_data);

	switch (reg_data) {
	case 0x0:
	case 0x1:
	case 0x2:
		state = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case 0x3:
	case 0x4:
		state = POWER_SUPPLY_STATUS_FULL;
		break;
	case 0x5:
	case 0x6:
	case 0x7:
		state = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case 0x8:
	case 0xA:
	case 0xB:
		state = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	default:
		state = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	}

	chg_data->charging_state = state;
	return state;
}

static void max77693_set_charger_state(struct max77693_charger_data *chg_data,
							int enable)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	u8 reg_data;
	pr_debug("%s: enable(%d)\n", __func__, enable);

	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_00, &reg_data);

	if (enable)
		reg_data |= MAX77693_MODE_CHGR;
	else
		reg_data &= ~MAX77693_MODE_CHGR;

	pr_debug("%s: CHG_CNFG_00(0x%02x)\n", __func__, reg_data);
	max77693_write_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_00, reg_data);
}

static void max77693_set_buck(struct max77693_charger_data *chg_data,
							int enable)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	u8 reg_data;
	pr_debug("%s: enable(%d)\n", __func__, enable);

	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_00, &reg_data);

	if (enable)
		reg_data |= MAX77693_MODE_BUCK;
	else
		reg_data &= ~MAX77693_MODE_BUCK;

	pr_debug("%s: CHG_CNFG_00(0x%02x)\n", __func__, reg_data);
	max77693_write_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_00, reg_data);
}

int max77693_get_input_current(struct max77693_charger_data *chg_data)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	u8 reg_data;
	int get_current = 0;
	pr_debug("%s\n", __func__);

	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_09, &reg_data);
	pr_debug("%s: CHG_CNFG_09(0x%02x)\n", __func__, reg_data);

	get_current = reg_data * 20;

	pr_debug("%s: get input current: %dmA\n", __func__, get_current);
	return get_current;
}

void max77693_set_input_current(struct max77693_charger_data *chg_data,
						unsigned int set_current)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	int in_curr;
	u8 set_curr_reg, now_curr_reg;
	int step;
	pr_debug("%s: set input current as %dmA\n", __func__, set_current);

	mutex_lock(&chg_data->ops_lock);
#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	check_charger_unlock_state(chg_data);
#endif

	if (set_current == OFF_CURR) {
		max77693_write_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_09, set_current);

		if (chg_data->soft_reg_state == true) {
			pr_info("%s: exit soft regulation loop\n", __func__);
			chg_data->soft_reg_state = false;
		}

		mutex_unlock(&chg_data->ops_lock);
		return;
	}

	/* Set input current limit */
	if (chg_data->soft_reg_state) {
		pr_info("%s: now in soft regulation loop: %d\n", __func__,
						chg_data->soft_reg_current);
		in_curr = max77693_get_input_current(chg_data);
		if (in_curr == chg_data->soft_reg_current) {
			pr_debug("%s: same input current: %dmA\n",
						__func__, in_curr);
			mutex_unlock(&chg_data->ops_lock);
			return;
		}
		set_curr_reg = (chg_data->soft_reg_current / 20);
	} else
		set_curr_reg = (set_current / 20);

	/* soft charge, 1st step, under 100mA, over 50ms */
	max77693_write_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_09,
					(SOFT_CHG_START_CURR / 20));
	pr_debug("%s: soft charge, %dmA for %dms\n", __func__,
				SOFT_CHG_START_CURR, SOFT_CHG_START_DUR);
	msleep(SOFT_CHG_START_DUR);

	step = 0;
	do {
		now_curr_reg = ((SOFT_CHG_START_CURR +
				(SOFT_CHG_CURR_STEP * (++step))) / 20);
		now_curr_reg = MIN(now_curr_reg, set_curr_reg);
		pr_debug("%s: step%d: now curr(%dmA, 0x%x)\n", __func__, step,
			(SOFT_CHG_START_CURR + (SOFT_CHG_CURR_STEP * (step))),
								now_curr_reg);
		max77693_write_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_09,
								now_curr_reg);
		msleep(SOFT_CHG_STEP_DUR);
	} while (now_curr_reg < set_curr_reg);

	max77693_write_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_09, set_curr_reg);

	mutex_unlock(&chg_data->ops_lock);
}

int max77693_get_charge_current(struct max77693_charger_data *chg_data)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	u8 reg_data;
	int get_current = 0;
	pr_debug("%s\n", __func__);

	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_02, &reg_data);
	pr_debug("%s: CHG_CNFG_02(0x%02x)\n", __func__, reg_data);

	reg_data &= MAX77693_CHG_CC;
	get_current = chg_data->charging_current = reg_data * 333 / 10;

	pr_debug("%s: get charge current: %dmA\n", __func__, get_current);
	return get_current;
}

void max77693_set_charge_current(struct max77693_charger_data *chg_data,
						unsigned int set_current)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	u8 reg_data;
	pr_debug("%s: set charge current as %dmA\n", __func__, set_current);

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	check_charger_unlock_state(chg_data);
#endif

	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_02, &reg_data);

	reg_data &= ~MAX77693_CHG_CC;
	reg_data |= ((set_current * 3 / 100) << 0);

	pr_debug("%s: reg_data(0x%02x)\n", __func__, reg_data);

	max77693_write_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_02, reg_data);
}

void max77693_reset_chgtyp(struct max77693_charger_data *chg_data)
{
	u8 reg_data;
	pr_info("%s\n", __func__);

	/* reset charger detection mode */
	max77693_read_reg(chg_data->max77693->muic,
			  MAX77693_MUIC_REG_CDETCTRL1,
			  &reg_data);
	reg_data |= MAX77693_CHGTYPMAN;
	max77693_write_reg(chg_data->max77693->muic,
			  MAX77693_MUIC_REG_CDETCTRL1,
			  reg_data);
}

#ifdef CONFIG_BATTERY_WPC_CHARGER
static bool max77693_get_wc_state(struct max77693_charger_data *chg_data)
{
	bool state;
	int wc_w_state, wc_v_state;
	pr_debug("%s\n", __func__);

	wc_w_state = !gpio_get_value(chg_data->wc_w_gpio);

	if (chg_data->wc_pwr_det == true) {
		wc_v_state = !gpio_get_value(chg_data->wc_v_gpio);
		if ((wc_w_state == CHARGE_ENABLE) &&
			(wc_v_state == CHARGE_DISABLE)) {
			pr_debug("%s: wpc(%d), vbus(%d), wc ok\n",
				__func__, wc_w_state, wc_v_state);
			state = true;
		} else {
			pr_debug("%s: wpc(%d), vbus(%d), wc not ok\n",
				__func__, wc_w_state, wc_v_state);
			state = false;
		}

		chg_data->wc_w_state = wc_w_state;
		chg_data->wc_v_state = wc_v_state;
	} else {
		if (wc_w_state == CHARGE_ENABLE) {
			pr_debug("%s: wpc(%d), wc ok\n",
					__func__, wc_w_state);
			state = true;
		} else {
			pr_debug("%s: wpc(%d), wc not ok\n",
					__func__, wc_w_state);
			state = false;
		}

		chg_data->wc_w_state = wc_w_state;
	}

	return state;
}
#endif

/* check chargable dock */
static int max77693_get_dock_type(struct max77693_charger_data *chg_data)
{
	int state = POWER_SUPPLY_TYPE_BATTERY;
	u8 reg_data;
	int muic_cb_typ;
	u8 dtls_00, chgin_dtls = 0;
	u8 mu_st2, vbvolt = 0;
	pr_debug("%s\n", __func__);

	muic_cb_typ = max77693_muic_get_charging_type();
	pr_debug("%s: muic cable type(%d)\n", __func__, muic_cb_typ);

	/* dock detect from muic */
	if ((muic_cb_typ == CABLE_TYPE_CARDOCK_MUIC) ||
		(muic_cb_typ == CABLE_TYPE_DESKDOCK_MUIC) ||
		(muic_cb_typ == CABLE_TYPE_SMARTDOCK_MUIC) ||
		(muic_cb_typ == CABLE_TYPE_SMARTDOCK_TA_MUIC) ||
		(muic_cb_typ == CABLE_TYPE_SMARTDOCK_USB_MUIC) ||
		(muic_cb_typ == CABLE_TYPE_AUDIODOCK_MUIC)) {

		chg_data->dock_type = muic_cb_typ;

		/* read chgin, but not use */
		max77693_read_reg(chg_data->max77693->i2c,
					MAX77693_CHG_REG_CHG_DTLS_00, &dtls_00);
		chgin_dtls = ((dtls_00 & MAX77693_CHGIN_DTLS) >>
					MAX77693_CHGIN_DTLS_SHIFT);

		/* check vbvolt */
		max77693_read_reg(chg_data->max77693->muic,
				  MAX77693_MUIC_REG_STATUS2, &mu_st2);
		vbvolt = ((mu_st2 & MAX77693_VBVOLT) >>
					MAX77693_VBVOLT_SHIFT);

		pr_info("%s: dock detected(%d), vbvolt(%d), chgin(0x%02x)\n",
				__func__, muic_cb_typ, vbvolt, chgin_dtls);

		if (vbvolt == ENABLE) {
			max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_CNFG_00, &reg_data);
			reg_data |= CHG_CNFG_00_DIS_MUIC_CTRL_MASK;
			max77693_write_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_CNFG_00, reg_data);
			state = POWER_SUPPLY_TYPE_DOCK;
		} else {
			max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_CNFG_00, &reg_data);
			reg_data &= ~CHG_CNFG_00_DIS_MUIC_CTRL_MASK;
			max77693_write_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_CNFG_00, reg_data);
			state = POWER_SUPPLY_TYPE_BATTERY;
		}
	} else {
		pr_debug("%s: dock not detected(%d), vbvolt(%d)\n", __func__,
						muic_cb_typ, vbvolt);
		chg_data->dock_type = 0;
	}

	return state;
}

static int max77693_get_cable_type(struct max77693_charger_data *chg_data)
{
	int state;
	u8 reg_data, mu_adc, mu_adc1k, otg;
	u8 dtls_00, chgin_dtls;
	u8 mu_st2, chgdetrun, vbvolt, chgtyp, dxovp;
	int muic_cb_typ;
	bool wc_state;
	bool retry_det;
	bool chg_det_erred = false; /* TEMP: set as true for logging */
	bool otg_detected = false;
	int retry_cnt = 0;
	pr_debug("%s\n", __func__);

	mutex_lock(&chg_data->ops_lock);

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	check_charger_unlock_state(chg_data);
#endif

	/* If OTG enabled, skip detecting charger cable */
	/* So mhl cable does not have adc ID that below condition   */
	/* can`t include adc1k mask */
	max77693_read_reg(chg_data->max77693->muic,
			  MAX77693_MUIC_REG_STATUS1, &reg_data);
	pr_debug("%s: MUIC_REG_STATUS1(0x%02x)\n", __func__, reg_data);
	mu_adc1k = reg_data & (0x1 << 7); /* STATUS1_ADC1K_MASK */
	mu_adc = reg_data & 0x1F;

	max77693_read_reg(chg_data->max77693->i2c,
			  MAX77693_CHG_REG_CHG_CNFG_00, &reg_data);
	pr_debug("%s: CHG_REG_CHG_CNFG_00(0x%02x)\n", __func__, reg_data);
	otg = reg_data & MAX77693_MODE_OTG;

#ifdef CONFIG_MACH_GC1
	/* In Factory mode using anyway Jig to switch between USB <--> UART
	 * sees a momentary 301K resistance as that of an OTG. Disabling
	 * charging INTRS now can lead to USB and MTP drivers not getting
	 * recognized in in subsequent switches.
	 * Factory Mode BOOT(on) USB
	 */
	if (mu_adc == 0x19) {
		pr_info("%s: jig usb cable(adc(0x%x))\n", __func__, mu_adc);
		state = POWER_SUPPLY_TYPE_USB;
		goto chg_det_finish;
	}
#endif

	muic_cb_typ = max77693_muic_get_charging_type();
	/* if type detection by otg, do not otg check */
	if ((muic_cb_typ != CABLE_TYPE_AUDIODOCK_MUIC) &&
		(((otg || (mu_adc == 0x00 && !mu_adc1k))))) {
		pr_info("%s: otg enabled(otg(0x%x), adc(0x%x))\n",
					__func__, otg, mu_adc);
		state = POWER_SUPPLY_TYPE_BATTERY;
		otg_detected = true;
		goto chg_det_finish;
	}

	/* dock charger */
	state = max77693_get_dock_type(chg_data);
	if (state == POWER_SUPPLY_TYPE_DOCK) {
		pr_info("%s: dock charger detected\n", __func__);
		goto chg_det_finish;
	}

	do {
		retry_det = false;

		max77693_read_reg(chg_data->max77693->i2c,
					MAX77693_CHG_REG_CHG_DTLS_00, &dtls_00);
		max77693_read_reg(chg_data->max77693->muic,
				  MAX77693_MUIC_REG_STATUS2, &mu_st2);
		chgin_dtls = ((dtls_00 & MAX77693_CHGIN_DTLS) >>
					MAX77693_CHGIN_DTLS_SHIFT);
		chgdetrun = ((mu_st2 & MAX77693_CHGDETRUN) >>
					MAX77693_CHGDETRUN_SHIFT);
		vbvolt = ((mu_st2 & MAX77693_VBVOLT) >>
					MAX77693_VBVOLT_SHIFT);
		chgtyp = ((mu_st2 & MAX77693_CHGTYPE) >>
					MAX77693_CHGTYPE_SHIFT);
		if (chg_det_erred)
			pr_err("%s: CHGIN(0x%x). MU_ST2(0x%x), "
				"CDR(0x%x), VB(0x%x), CHGTYP(0x%x)\n", __func__,
						chgin_dtls, mu_st2,
						chgdetrun, vbvolt, chgtyp);

		/* input power state */
		if (((chgin_dtls != 0x0) && (vbvolt == 0x1)) ||
			((chgin_dtls == 0x0) && (vbvolt == 0x0)) ||
			(chg_data->reg_loop_deted == true)) {
			pr_debug("%s: sync power: CHGIN(0x%x), VB(0x%x), REG(%d)\n",
						__func__, chgin_dtls, vbvolt,
						chg_data->reg_loop_deted);
		} else {
			pr_err("%s: async power: CHGIN(0x%x), VB(0x%x), REG(%d)\n",
						__func__, chgin_dtls, vbvolt,
						chg_data->reg_loop_deted);
			chg_det_erred = true;

			/* check chargable input power */
			if ((chgin_dtls == 0x0) &&
				(chg_data->cable_type ==
					POWER_SUPPLY_TYPE_BATTERY)) {
				pr_err("%s: unchargable power\n", __func__);
				state = POWER_SUPPLY_TYPE_BATTERY;
				goto chg_det_finish;
			}
		}

		/* charger detect running */
		if (chgdetrun == 0x1) {
			pr_info("%s: CDR(0x%x)\n", __func__, chgdetrun);
			goto chg_det_err;
		}

		/* muic power and charger type */
		if (((vbvolt == 0x1) && (chgtyp == 0x00)) ||
			((vbvolt == 0x0) && (chgtyp != 0x00))) {
			pr_info("%s: VB(0x%x), CHGTYP(0x%x)\n",
						__func__, vbvolt, chgtyp);

			/* check D+/D- ovp */
			dxovp = ((mu_st2 & MAX77693_DXOVP) >>
						MAX77693_DXOVP_SHIFT);
			if ((vbvolt == 0x1) && (dxovp)) {
				pr_err("%s: D+/D- ovp state\n", __func__);

				/* disable CHGIN protection FETs */
				max77693_read_reg(chg_data->max77693->i2c,
						MAX77693_CHG_REG_CHG_CNFG_00,
						&reg_data);
				reg_data |= CHG_CNFG_00_DIS_MUIC_CTRL_MASK;
				max77693_write_reg(chg_data->max77693->i2c,
						MAX77693_CHG_REG_CHG_CNFG_00,
						reg_data);

				chg_det_erred = true;
				state = POWER_SUPPLY_TYPE_MAINS;
				goto chg_det_finish;
			} else {
				pr_err("%s: async power & chgtyp\n", __func__);
				goto chg_det_err;
			}
		}

		/* charger type ok */
		if (chg_det_erred)
			pr_err("%s: chgtyp detect ok, "
				"CHGIN(0x%x). MU_ST2(0x%x), "
				"CDR(0x%x), VB(0x%x), CHGTYP(0x%x)\n",
				__func__, chgin_dtls,  mu_st2,
				chgdetrun, vbvolt, chgtyp);

		break;
chg_det_err:
		retry_det = true;
		chg_det_erred = true;

		pr_err("%s: chgtyp detect err, retry %d, "
			"CHGIN(0x%x). MU_ST2(0x%x), CDR(0x%x), VB(0x%x), CHGTYP(0x%x)\n",
					__func__, ++retry_cnt, chgin_dtls,
					mu_st2, chgdetrun, vbvolt, chgtyp);

		/* after 200ms * 5 */
		if (retry_cnt == DET_ERR_RETRY) {
			pr_info("%s: reset charger detection mode\n",
							__func__);

			/* reset charger detection mode */
			max77693_reset_chgtyp(chg_data);
		}
		msleep(DET_ERR_DELAY);
	} while ((retry_det == true) && (retry_cnt < DET_ERR_RETRY));

	switch (chgtyp) {
	case 0x0:		/* Noting attached */
		/* clear regulation loop flag */
		chg_data->reg_loop_deted = false;
		state = POWER_SUPPLY_TYPE_BATTERY;
		break;
	case 0x1:		/* USB cabled */
		state = POWER_SUPPLY_TYPE_USB;
#ifdef CONFIG_BATTERY_WPC_CHARGER
		wc_state = max77693_get_wc_state(chg_data);
		if (wc_state == true)
			state = POWER_SUPPLY_TYPE_WIRELESS;
#endif
		break;
	case 0x2:		/* Charging downstream port */
		state = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case 0x3:		/* Dedicated charger(up to 1.5A) */
	case 0x4:		/* Apple 500mA charger */
	case 0x5:		/* Apple 1A or 2A charger */
	case 0x6:		/* Special charger */
		state = POWER_SUPPLY_TYPE_MAINS;
		break;
	default:
		state = POWER_SUPPLY_TYPE_BATTERY;
		break;
	}

chg_det_finish:
	if (chg_det_erred)
		pr_err("%s: cable type(%d)\n", __func__, state);

	/* if cable is nothing,,, */
	if (state == POWER_SUPPLY_TYPE_BATTERY) {
		if (!otg_detected) {
			/* enable CHGIN protection FETs */
			max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_CNFG_00, &reg_data);
			reg_data &= ~CHG_CNFG_00_DIS_MUIC_CTRL_MASK;
			max77693_write_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_CNFG_00, reg_data);
		}

		/* clear soft reg state flag */
		chg_data->soft_reg_state = false;
	}

	chg_data->cable_type = state;

	mutex_unlock(&chg_data->ops_lock);

	return state;
}

static int max77693_get_battery_state(struct max77693_charger_data *chg_data)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	int state;
	int vbus_state;
	int chg_state;
	bool low_bat = false;
	u8 reg_data;
	pr_debug("%s\n", __func__);

	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_DTLS_01, &reg_data);
	reg_data = ((reg_data & MAX77693_BAT_DTLS) >> MAX77693_BAT_DTLS_SHIFT);
	pr_debug("%s: BAT_DTLS(0x%02x)\n", __func__, reg_data);

	switch (reg_data) {
	case 0x01:
		pr_info("%s: battery is okay "
			"but its voltage is low(~VPQLB)\n", __func__);
		low_bat = true;
		state = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case 0x02:
		pr_info("%s: battery dead\n", __func__);
		state = POWER_SUPPLY_HEALTH_DEAD;
		break;
	case 0x03:
		state = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case 0x04:
		pr_info("%s: battery is okay "
			"but its voltage is low(VPQLB~VSYSMIN)\n", __func__);
		low_bat = true;
		state = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case 0x05:
		pr_info("%s: battery ovp\n", __func__);
		state = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		break;
	default:
		state = POWER_SUPPLY_HEALTH_UNKNOWN;
		break;
	}

	if (state == POWER_SUPPLY_HEALTH_GOOD) {
		/* VBUS OVP state return battery OVP state */
		vbus_state = max77693_get_vbus_state(chg_data);

		/* read CHG_DTLS and detecting battery terminal error */
		chg_state = max77693_get_charger_state(chg_data);

		/* OVP is higher priority */
		if (vbus_state == POWER_SUPPLY_VBUS_OVLO) {
			pr_info("%s: vbus ovp\n", __func__);
			state = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		} else if ((low_bat == true) &&
			(chg_state == POWER_SUPPLY_STATUS_FULL)) {
			pr_info("%s: battery terminal error\n", __func__);
			state = POWER_SUPPLY_HEALTH_UNDERVOLTAGE;
		}
	}

	chg_data->battery_state = state;
	return state;
}

/* extended online type */
static int max77693_get_online_type(struct max77693_charger_data *chg_data)
{
	int m_typ;
	int state = 0;
	pr_info("%s\n", __func__);

	m_typ = max77693_get_cable_type(chg_data);

	pr_info("%s: main(%d), sub(%d), pwr(%d)\n", __func__, m_typ,
					chg_data->cable_sub_type,
					chg_data->cable_pwr_type);

	state = ((m_typ << ONLINE_TYPE_MAIN_SHIFT) |
		(chg_data->cable_sub_type << ONLINE_TYPE_SUB_SHIFT) |
		(chg_data->cable_pwr_type << ONLINE_TYPE_PWR_SHIFT));

	pr_info("%s: online(0x%08x)\n", __func__, state);

	return state;
}

void max77693_set_online_type(struct max77693_charger_data *chg_data, int data)
{
	int m_typ, s_typ, p_typ;
	pr_info("%s: type(0x%08x)\n", __func__, data);

	/* | 31-24: RSVD | 23-16: MAIN TYPE |
		15-8: SUB TYPE | 7-0: POWER TYPE | */
	data &= ~(ONLINE_TYPE_RSVD_MASK);
	m_typ = ((data & ONLINE_TYPE_MAIN_MASK) >> ONLINE_TYPE_MAIN_SHIFT);
	chg_data->cable_sub_type = s_typ =
		((data & ONLINE_TYPE_SUB_MASK) >> ONLINE_TYPE_SUB_SHIFT);
	chg_data->cable_pwr_type = p_typ =
		((data & ONLINE_TYPE_PWR_MASK) >> ONLINE_TYPE_PWR_SHIFT);
	pr_info("%s: main(%d), sub(%d), pwr(%d)\n", __func__,
					m_typ, s_typ, p_typ);

	cancel_delayed_work(&chg_data->update_work);
	wake_lock(&chg_data->update_wake_lock);
	schedule_delayed_work(&chg_data->update_work,
			msecs_to_jiffies(STABLE_POWER_DELAY));
}

/* get cable type from muic */
void max77693_set_muic_cb_type(struct max77693_charger_data *chg_data, int data)
{
	pr_info("%s: muic cable type(%d)\n", __func__, data);

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
	check_charger_unlock_state(chg_data);
#endif

	cancel_delayed_work(&chg_data->update_work);
	wake_lock(&chg_data->update_wake_lock);
	schedule_delayed_work(&chg_data->update_work,
			msecs_to_jiffies(STABLE_POWER_DELAY));
}

static bool max77693_charger_unlock(struct max77693_charger_data *chg_data)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	u8 reg_data;
	u8 chgprot;
	int retry_cnt = 0;
	bool need_init = false;
	pr_debug("%s\n", __func__);

	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_06, &reg_data);
	chgprot = ((reg_data & MAX77693_CHG_CHGPROT) >>
				MAX77693_CHG_CHGPROT_SHIFT);

	if (chgprot == MAX77693_CHG_CHGPROT_UNLOCK) {
		pr_debug("%s: unlocked state, return\n", __func__);
		need_init = false;
		goto unlock_finish;
	}

	do {
		max77693_write_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_06,
					(MAX77693_CHG_CHGPROT_UNLOCK <<
					MAX77693_CHG_CHGPROT_SHIFT));

		max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_06, &reg_data);
		chgprot = ((reg_data & MAX77693_CHG_CHGPROT) >>
					MAX77693_CHG_CHGPROT_SHIFT);

		if (chgprot != MAX77693_CHG_CHGPROT_UNLOCK) {
			pr_err("%s: unlock err, chgprot(0x%x), retry(%d)\n",
					__func__, chgprot, retry_cnt);
			msleep(CHG_UNLOCK_DELAY);
		} else {
			pr_info("%s: unlock success, chgprot(0x%x)\n",
							__func__, chgprot);
			need_init = true;
			break;
		}
	} while ((chgprot != MAX77693_CHG_CHGPROT_UNLOCK) &&
				(++retry_cnt < CHG_UNLOCK_RETRY));

unlock_finish:
	return need_init;
}

static void max77693_charger_reg_init(struct max77693_charger_data *chg_data)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	u8 reg_data;
	pr_debug("%s\n", __func__);

	/*
	 * fast charge timer 10hrs
	 * restart threshold disable
	 * pre-qual charge enable(default)
	 */
	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_01, &reg_data);
	reg_data = (0x04 << 0) | (0x03 << 4);
	max77693_write_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_01, reg_data);

	/*
	 * charge current 466mA(default)
	 * otg current limit 900mA
	 */
	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_02, &reg_data);
	reg_data = (1 << 7);
	max77693_write_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_02, reg_data);

	/*
	 * top off current 100mA
	 * top off timer 0min
	 */
	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_03, &reg_data);
	if (chg_data->max77693->pmic_rev == MAX77693_REV_PASS1) {
		reg_data = (0x03 << 0);		/* 125mA */
		reg_data |= (0x00 << 3);	/* 0min */
	} else {
#if defined(USE_2STEP_TERM)	/* now only T0 */
		reg_data = (0x04 << 0);		/* 200mA */
		reg_data |= (0x04 << 3);	/* 40min */
#else
#if defined(CONFIG_MACH_GC1)
		reg_data = (0x02 << 0);		/* 150mA */
		reg_data |= (0x00 << 3);	/* 0min */
#else	/* M0, C1,,, */
		reg_data = (0x00 << 0);		/* 100mA */
		reg_data |= (0x00 << 3);	/* 0min */
#endif
#endif
	}
	max77693_write_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_03, reg_data);

	/*
	 * cv voltage 4.2V or 4.35V
	 * MINVSYS 3.6V(default)
	 */
	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_04, &reg_data);
	reg_data &= (~MAX77693_CHG_MINVSYS_MASK);
	reg_data |= (MAX77693_CHG_MINVSYS_3_6V << MAX77693_CHG_MINVSYS_SHIFT);
	reg_data &= (~MAX77693_CHG_CV_PRM_MASK);
#if defined(CONFIG_MACH_M0)
	if ((system_rev != 3) && (system_rev >= 1))
		reg_data |= (MAX77693_CHG_CV_PRM_4_35V << 0);
	else
		reg_data |= (MAX77693_CHG_CV_PRM_4_20V << 0);
#else	/* C1, C2, M3, T0, ... */
		reg_data |= (MAX77693_CHG_CV_PRM_4_35V << 0);
#endif

	/*
	 * For GC1 Model,  MINVSYS is 3.4V.
	 *  For GC1 Model  PRMV( Primary Charge Regn. Voltage) = 4.2V.
	 * Actual expected regulated voltage needs to be 4.2V but due to
	 * internal resistance and circuit deviation we might have to set the
	 * benchmark a bit higher sometimes. (4.225V now)
	 */
#if defined(CONFIG_MACH_GC1)
	reg_data &= (~MAX77693_CHG_CV_PRM_MASK);
	reg_data |= (0x16 << MAX77693_CHG_CV_PRM_SHIFT);
	reg_data &= (~MAX77693_CHG_MINVSYS_MASK);
	reg_data |= (0x4 << MAX77693_CHG_MINVSYS_SHIFT);
#endif
	pr_info("%s: battery cv voltage %s, (sysrev %d)\n", __func__,
		(((reg_data & MAX77693_CHG_CV_PRM_MASK) == \
		(MAX77693_CHG_CV_PRM_4_35V << MAX77693_CHG_CV_PRM_SHIFT)) ?
					"4.35V" : "4.2V"), system_rev);
	max77693_write_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_04, reg_data);

	max77693_dump_reg(chg_data);
}

static void max77693_reduce_input(struct max77693_charger_data *chg_data,
							unsigned int curr)
{
	struct i2c_client *i2c = chg_data->max77693->i2c;
	u8 reg_data;
	pr_debug("%s: reduce %dmA\n", __func__, curr);

	max77693_read_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_09, &reg_data);
	reg_data &= MAX77693_CHG_CHGIN_LIM;
	chg_data->soft_reg_current = reg_data * 20;

	if (chg_data->soft_reg_current < curr) {
		pr_err("%s: recude curr(%d) is under now curr(%d)\n", __func__,
					curr, chg_data->soft_reg_current);
		return;
	}

	chg_data->soft_reg_current -= curr;
	chg_data->soft_reg_current = max(chg_data->soft_reg_current,
						SW_REG_CURR_MIN_MA);
	pr_info("%s: %dmA to %dmA\n", __func__,
			reg_data * 20, chg_data->soft_reg_current);

	reg_data = (chg_data->soft_reg_current / 20);
	pr_debug("%s: reg_data(0x%02x)\n", __func__, reg_data);
	max77693_write_reg(i2c, MAX77693_CHG_REG_CHG_CNFG_09, reg_data);
}

static void max77693_update_work(struct work_struct *work)
{
	struct max77693_charger_data *chg_data = container_of(work,
						struct max77693_charger_data,
						update_work.work);
	struct power_supply *battery_psy = power_supply_get_by_name("battery");
	union power_supply_propval value;
	int vbus_state;
	pr_debug("%s\n", __func__);

#if defined(CONFIG_CHARGER_MANAGER)
	/* Notify charger-manager */
	enum cm_event_types type;

	/* only consider battery in/out and external power in/out */
	/* It seems that charger interrupt does not work at all */
	switch (chg_data->irq - chg_data->max77693->irq_base) {
	case MAX77693_CHG_IRQ_BAT_I:
		type = max77693_get_battery_present(chg_data) ?
			CM_EVENT_BATT_IN : CM_EVENT_BATT_OUT;
		cm_notify_event(&chg_data->charger, type, NULL);
		break;
	case MAX77693_CHG_IRQ_CHGIN_I:
		cm_notify_event(&chg_data->charger,
			CM_EVENT_EXT_PWR_IN_OUT, NULL);
		break;
	default:
		break;
	}
#else
	if (!battery_psy) {
		pr_err("%s: fail to get battery power supply\n", __func__);
		wake_unlock(&chg_data->update_wake_lock);
		return;
	}

	switch (chg_data->irq - chg_data->max77693->irq_base) {
	case MAX77693_CHG_IRQ_CHGIN_I:
		vbus_state = max77693_get_vbus_state(chg_data);
		if (vbus_state == POWER_SUPPLY_VBUS_WEAK) {
			pr_info("%s: vbus weak\n", __func__);
			wake_lock(&chg_data->softreg_wake_lock);
			schedule_delayed_work(&chg_data->softreg_work,
					msecs_to_jiffies(SW_REG_START_DELAY));
		} else
			pr_debug("%s: vbus not weak\n", __func__);
		break;
	default:
		break;

	}

	battery_psy->set_property(battery_psy,
				POWER_SUPPLY_PROP_STATUS,
				&value);
#endif

	wake_unlock(&chg_data->update_wake_lock);
}

static void max77693_softreg_work(struct work_struct *work)
{
	struct max77693_charger_data *chg_data = container_of(work,
						struct max77693_charger_data,
						softreg_work.work);
	u8 int_ok;
	u8 dtls_00, chgin_dtls;
	u8 dtls_01, chg_dtls;
	u8 dtls_02, byp_dtls;
	u8 mu_st2, vbvolt;
	u8 cnfg_09;
	int in_curr = 0;
	pr_debug("%s\n", __func__);

	mutex_lock(&chg_data->ops_lock);

	/* charger */
	max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_INT_OK, &int_ok);
	max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_DTLS_00, &dtls_00);
	max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_DTLS_01, &dtls_01);
	max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_DTLS_02, &dtls_02);

	chgin_dtls = ((dtls_00 & MAX77693_CHGIN_DTLS) >>
				MAX77693_CHGIN_DTLS_SHIFT);
	chg_dtls = ((dtls_01 & MAX77693_CHG_DTLS) >>
				MAX77693_CHG_DTLS_SHIFT);
	byp_dtls = ((dtls_02 & MAX77693_BYP_DTLS) >>
				MAX77693_BYP_DTLS_SHIFT);

	max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_CNFG_09, &cnfg_09);
	cnfg_09 &= MAX77693_CHG_CHGIN_LIM;
	in_curr = cnfg_09 * 20;

	/* muic */
	max77693_read_reg(chg_data->max77693->muic,
				MAX77693_MUIC_REG_STATUS2, &mu_st2);
	vbvolt = ((mu_st2 & MAX77693_VBVOLT) >>
				MAX77693_VBVOLT_SHIFT);

	pr_info("%s: INT_OK(0x%x), CHGIN(0x%x), CHG(0x%x), "
				"BYP(0x%x), ST2(0x%x), IN_CURR(%d)\n", __func__,
				int_ok, chgin_dtls, chg_dtls,
				byp_dtls, mu_st2, in_curr);

	if ((in_curr > SW_REG_CURR_STEP_MA) && (chg_dtls != 0x8) &&
		((byp_dtls & MAX77693_BYP_DTLS3) ||
		((chgin_dtls != 0x3) && (vbvolt == 0x1)))) {
		pr_info("%s: unstable power\n", __func__);

		/* set soft regulation progress */
		chg_data->soft_reg_ing = true;

		/* enable soft regulation loop */
		chg_data->soft_reg_state = true;

		max77693_reduce_input(chg_data, SW_REG_CURR_STEP_MA);

		/* cancel update wq */
		cancel_delayed_work(&chg_data->update_work);

		/* schedule softreg wq */
		wake_lock(&chg_data->softreg_wake_lock);
		schedule_delayed_work(&chg_data->softreg_work,
				msecs_to_jiffies(SW_REG_STEP_DELAY));
	} else {
		/* check cable detached */
		if ((!in_curr) ||
			((chgin_dtls == 0x0) && (vbvolt == 0x0)) ||
			((byp_dtls == 0x0) && (chg_dtls == 0x8))) {
			pr_info("%s: maybe cable is detached\n", __func__);

			cancel_delayed_work(&chg_data->update_work);
			wake_lock(&chg_data->update_wake_lock);
			schedule_delayed_work(&chg_data->update_work,
					msecs_to_jiffies(STABLE_POWER_DELAY));
		}

		/* for margin */
		if (chg_data->soft_reg_ing == true) {
			pr_info("%s: stable power, reduce 1 more step "
						"for margin\n", __func__);
			max77693_reduce_input(chg_data, SW_REG_CURR_STEP_MA);
			chg_data->soft_reg_ing = false;
		}

		wake_unlock(&chg_data->softreg_wake_lock);
	}

	mutex_unlock(&chg_data->ops_lock);
}

/* Support property from charger */
static enum power_supply_property max77693_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL
};

static int max77693_charger_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct max77693_charger_data *chg_data = container_of(psy,
						  struct max77693_charger_data,
						  charger);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = max77693_get_charger_state(chg_data);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = max77693_get_charger_type(chg_data);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = max77693_get_battery_state(chg_data);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = max77693_get_battery_present(chg_data);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
#if defined(EXTENDED_ONLINE_TYPE)
		val->intval = max77693_get_online_type(chg_data);
#else
		val->intval = max77693_get_cable_type(chg_data);
#endif
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = max77693_get_input_current(chg_data);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = max77693_get_charge_current(chg_data);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = max77693_get_topoff_state(chg_data);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int max77693_charger_set_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    const union power_supply_propval *val)
{
	struct max77693_charger_data *chg_data = container_of(psy,
						  struct max77693_charger_data,
						  charger);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		max77693_set_charger_state(chg_data, val->intval);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
#if !defined(USE_CHGIN_INTR)
		max77693_set_muic_cb_type(chg_data, val->intval);
#else
#if defined(EXTENDED_ONLINE_TYPE)
		max77693_set_online_type(chg_data, val->intval);
#else
		return -EINVAL;
#endif
#endif
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		max77693_set_input_current(chg_data, val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		max77693_set_charge_current(chg_data, val->intval);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static irqreturn_t max77693_bypass_irq(int irq, void *data)
{
	struct max77693_charger_data *chg_data = data;
	bool need_reg_init = false;
	u8 int_ok, dtls_02, cnfg_00;
	u8 byp_dtls;
#ifdef CONFIG_USB_HOST_NOTIFY
	struct host_notifier_platform_data *host_noti_pdata =
			host_notifier_device.dev.platform_data;
#endif
	pr_info("%s: irq(%d)\n", __func__, irq);

	mutex_lock(&chg_data->irq_lock);

	/* check and unlock */
	need_reg_init = max77693_charger_unlock(chg_data);
	if (need_reg_init) {
		pr_err("%s: charger locked state, reg init\n", __func__);
		max77693_charger_reg_init(chg_data);
	}

	max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_INT_OK,
				&int_ok);

	max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_DTLS_02,
				&dtls_02);

	byp_dtls = ((dtls_02 & MAX77693_BYP_DTLS) >>
				MAX77693_BYP_DTLS_SHIFT);
	pr_info("%s: INT_OK(0x%02x), BYP_DTLS(0x%02x)\n",
				__func__, int_ok, byp_dtls);

	switch (byp_dtls) {
	case 0x0:
		pr_info("%s: bypass node is okay\n", __func__);
		break;
	case 0x1:
		pr_err("%s: bypass overcurrent limit\n", __func__);
#ifdef CONFIG_USB_HOST_NOTIFY
		host_state_notify(&host_noti_pdata->ndev,
					NOTIFY_HOST_OVERCURRENT);
#endif
		max77693_read_reg(chg_data->max77693->i2c,
					MAX77693_CHG_REG_CHG_CNFG_00,
					&cnfg_00);
		cnfg_00 &= ~(CHG_CNFG_00_OTG_MASK
				| CHG_CNFG_00_BOOST_MASK
				| CHG_CNFG_00_DIS_MUIC_CTRL_MASK);
		cnfg_00 |= CHG_CNFG_00_BUCK_MASK;
		max77693_write_reg(chg_data->max77693->i2c,
					MAX77693_CHG_REG_CHG_CNFG_00,
					cnfg_00);
		break;
	case 0x8:
		pr_err("%s: chgin regulation loop is active\n", __func__);
		if (chg_data->cable_type != POWER_SUPPLY_TYPE_WIRELESS) {
			/* software regulation */
			wake_lock(&chg_data->softreg_wake_lock);
			schedule_delayed_work(&chg_data->softreg_work,
					msecs_to_jiffies(SW_REG_START_DELAY));
		} else
			pr_err("%s: now in wireless charging, "
				" do not sw regulation\n", __func__);

		break;
	default:
		pr_info("%s: bypass reserved\n", __func__);
		break;
	}

	cancel_delayed_work(&chg_data->update_work);
	wake_lock(&chg_data->update_wake_lock);
	schedule_delayed_work(&chg_data->update_work,
			msecs_to_jiffies(STABLE_POWER_DELAY));

	mutex_unlock(&chg_data->irq_lock);

	return IRQ_HANDLED;
}

/* TEMP: count same state irq occured */
static irqreturn_t max77693_charger_irq(int irq, void *data)
{
	struct max77693_charger_data *chg_data = data;
	bool need_reg_init = false;
	u8 prev_int_ok, int_ok;
	u8 dtls_00, thm_dtls, chgin_dtls;
	u8 dtls_01, chg_dtls, bat_dtls;
	u8 mu_st2, vbvolt;
	pr_info("%s: irq(%d)\n", __func__, irq);

	mutex_lock(&chg_data->irq_lock);

	/* check and unlock */
	need_reg_init = max77693_charger_unlock(chg_data);
	if (need_reg_init) {
		pr_err("%s: charger locked state, reg init\n", __func__);
		max77693_charger_reg_init(chg_data);
	}

	max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_INT_OK,
				&prev_int_ok);

	msleep(IRQ_DEBOUNCE_TIME);
	max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_INT_OK,
				&int_ok);
	if ((chg_data->irq_reg == int_ok) && (prev_int_ok != int_ok)) {
		pr_info("%s: irq debounced(0x%x, 0x%x, 0x%x), return\n",
			__func__, chg_data->irq_reg, prev_int_ok, int_ok);
		mutex_unlock(&chg_data->irq_lock);
		return IRQ_HANDLED;
	}

	chg_data->irq_reg = int_ok;
	chg_data->irq = irq;

	/* charger */
	max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_DTLS_00, &dtls_00);

	max77693_read_reg(chg_data->max77693->i2c,
				MAX77693_CHG_REG_CHG_DTLS_01, &dtls_01);

	thm_dtls = ((dtls_00 & MAX77693_THM_DTLS) >>
				MAX77693_THM_DTLS_SHIFT);
	chgin_dtls = ((dtls_00 & MAX77693_CHGIN_DTLS) >>
				MAX77693_CHGIN_DTLS_SHIFT);
	chg_dtls = ((dtls_01 & MAX77693_CHG_DTLS) >>
				MAX77693_CHG_DTLS_SHIFT);
	bat_dtls = ((dtls_01 & MAX77693_BAT_DTLS) >>
				MAX77693_BAT_DTLS_SHIFT);

	/* muic */
	max77693_read_reg(chg_data->max77693->muic,
				MAX77693_MUIC_REG_STATUS2, &mu_st2);
	vbvolt = ((mu_st2 & MAX77693_VBVOLT) >>
				MAX77693_VBVOLT_SHIFT);
	pr_info("%s: INT_OK(0x%x), THM(0x%x), CHGIN(0x%x), CHG(0x%x), BAT(0x%x), "
						"ST2(0x%x)\n", __func__,
						int_ok, thm_dtls, chgin_dtls,
						chg_dtls, bat_dtls, mu_st2);

#if defined(USE_CHGIN_INTR)
	if (((chgin_dtls == 0x0) || (chgin_dtls == 0x1)) &&
			(vbvolt == 0x1) && (chg_dtls != 0x8)) {
		pr_info("%s: abnormal power state: chgin(%d), vb(%d), chg(%d)\n",
					__func__, chgin_dtls, vbvolt, chg_dtls);

		/* enable soft regulation loop */
		chg_data->soft_reg_state = true;

		/* first, reduce */
		max77693_reduce_input(chg_data, SW_REG_CURR_STEP_MA);

		/* software regulation */
		wake_lock(&chg_data->softreg_wake_lock);
		schedule_delayed_work(&chg_data->softreg_work,
				msecs_to_jiffies(SW_REG_STEP_DELAY));
	}
#endif

	cancel_delayed_work(&chg_data->update_work);
	wake_lock(&chg_data->update_wake_lock);
	schedule_delayed_work(&chg_data->update_work,
			msecs_to_jiffies(STABLE_POWER_DELAY));

	mutex_unlock(&chg_data->irq_lock);

	return IRQ_HANDLED;
}

#ifdef CONFIG_BATTERY_WPC_CHARGER
static irqreturn_t wpc_charger_irq(int irq, void *data)
{
	struct max77693_charger_data *chg_data = data;
	bool need_reg_init = false;
	int wc_w_state, wc_v_state, wc_v_pud_state;
	pr_info("%s: irq(%d)\n", __func__, irq);

	mutex_lock(&chg_data->irq_lock);

	/* check and unlock */
	need_reg_init = max77693_charger_unlock(chg_data);
	if (need_reg_init) {
		pr_err("%s: charger locked state, reg init\n", __func__);
		max77693_charger_reg_init(chg_data);
	}

	wc_w_state = wc_v_state = 0;

	wc_w_state = !gpio_get_value(chg_data->wc_w_gpio);
	if (chg_data->wc_pwr_det == true) {
		if ((chg_data->wc_w_state == 0) && (wc_w_state == 1)) {
			pr_info("%s: wpc activated, set V_INT as PN\n",
								__func__);
			s3c_gpio_setpull(chg_data->wc_v_gpio,
						S3C_GPIO_PULL_NONE);
			mutex_unlock(&chg_data->irq_lock);
			enable_irq(chg_data->wc_v_irq);
			mutex_lock(&chg_data->irq_lock);
		} else if ((chg_data->wc_w_state == 1) && (wc_w_state == 0)) {
			mutex_unlock(&chg_data->irq_lock);
			disable_irq_nosync(chg_data->wc_v_irq);
			mutex_lock(&chg_data->irq_lock);
			pr_info("%s: wpc deactivated, set V_INT as PD\n",
								__func__);
			s3c_gpio_setpull(chg_data->wc_v_gpio,
						S3C_GPIO_PULL_DOWN);
		} else {
			wc_v_pud_state = s3c_gpio_getpull(chg_data->wc_v_gpio);
			pr_info("%s: wpc not changed, V_INT(0x%x)\n", __func__,
								wc_v_pud_state);
		}

		wc_v_state = !gpio_get_value(chg_data->wc_v_gpio);
		pr_info("%s: w(%d to %d), v(%d to %d)\n", __func__,
				chg_data->wc_w_state, wc_w_state,
				chg_data->wc_v_state, wc_v_state);

		if ((wc_w_state == 1) && (chg_data->wc_v_state != wc_v_state)) {
			pr_info("%s: wc power path is changed\n", __func__);

			/* limit input current as 100mA */
			max77693_set_input_current(chg_data, 100);

			/* reset charger detection mode */
			max77693_reset_chgtyp(chg_data);

			cancel_delayed_work(&chg_data->update_work);
			wake_lock(&chg_data->update_wake_lock);
			schedule_delayed_work(&chg_data->update_work,
				msecs_to_jiffies(STABLE_POWER_DELAY));
		}

		chg_data->wc_w_state = wc_w_state;
		chg_data->wc_v_state = wc_v_state;
	} else {
		pr_info("%s: w(%d to %d)\n", __func__,
				chg_data->wc_w_state, wc_w_state);

		chg_data->wc_w_state = wc_w_state;
	}

	mutex_unlock(&chg_data->irq_lock);

	return IRQ_HANDLED;
}
#endif

static void max77693_charger_initialize(struct max77693_charger_data *chg_data)
{
	struct max77693_charger_platform_data *charger_pdata =
					chg_data->charger_pdata;
	struct i2c_client *i2c = chg_data->max77693->i2c;
	int i;

	for (i = 0; i < charger_pdata->num_init_data; i++)
		max77693_write_reg(i2c, charger_pdata->init_data[i].addr,
				charger_pdata->init_data[i].data);
}

#if defined(CONFIG_TARGET_LOCALE_KOR)
#ifdef CONFIG_DEBUG_FS
static int max77693_debugfs_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static ssize_t max77693_debugfs_read_registers(struct file *filp,
	char __user *buffer, size_t count, loff_t *ppos)
{
	struct max77693_charger_data *chg_data = filp->private_data;
	int i = 0;
	char *buf;
	size_t len = 0;
	ssize_t ret;
	u8 val;

	if (!chg_data) {
		pr_err("%s : chg_data is null\n", __func__);
		return 0;
	}

	if (*ppos != 0)
		return 0;

	if (count < sizeof(buf))
		return -ENOSPC;

	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0xB0; i <= 0xC6; i++) {
		max77693_read_reg(chg_data->max77693->i2c, i, &val);
		len += snprintf(buf + len, PAGE_SIZE - len,
			"%x=%02x", i, val);
		if (i == 0xC6)
			len += snprintf(buf + len, PAGE_SIZE - len, "\n");
		else
			len += snprintf(buf + len, PAGE_SIZE - len, " ");
	}

	ret = simple_read_from_buffer(buffer, len, ppos, buf, PAGE_SIZE);
	kfree(buf);

	return ret;
}

static const struct file_operations max77693_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = max77693_debugfs_open,
	.read = max77693_debugfs_read_registers,
};
#endif
#endif

static __devinit int max77693_charger_probe(struct platform_device *pdev)
{
	struct max77693_charger_data *chg_data;
	struct max77693_dev *max77693 = dev_get_drvdata(pdev->dev.parent);
	struct max77693_platform_data *pdata = dev_get_platdata(max77693->dev);
	int ret;
	pr_info("%s: charger init\n", __func__);

	if (!pdata) {
		pr_err("%s: no platform data\n", __func__);
		return -ENODEV;
	}

	chg_data = kzalloc(sizeof(struct max77693_charger_data), GFP_KERNEL);
	if (!chg_data)
		return -ENOMEM;

	platform_set_drvdata(pdev, chg_data);
	chg_data->max77693 = max77693;

	mutex_init(&chg_data->irq_lock);
	mutex_init(&chg_data->ops_lock);

	wake_lock_init(&chg_data->update_wake_lock, WAKE_LOCK_SUSPEND,
		       "charger-update");
	wake_lock_init(&chg_data->softreg_wake_lock, WAKE_LOCK_SUSPEND,
		       "charger-softreg");

	/* unlock charger setting protect */
	max77693_charger_unlock(chg_data);

	chg_data->charger_pdata = pdata->charger_data;
	if (!pdata->charger_data->init_data)
		max77693_charger_reg_init(chg_data);
	else
		max77693_charger_initialize(chg_data);

	chg_data->irq_bypass = max77693->irq_base + MAX77693_CHG_IRQ_BYP_I;
	chg_data->irq_therm = max77693->irq_base + MAX77693_CHG_IRQ_THM_I;
	chg_data->irq_battery = max77693->irq_base + MAX77693_CHG_IRQ_BAT_I;
	chg_data->irq_charge = max77693->irq_base + MAX77693_CHG_IRQ_CHG_I;
	chg_data->irq_chargin = max77693->irq_base + MAX77693_CHG_IRQ_CHGIN_I;

	INIT_DELAYED_WORK(&chg_data->update_work, max77693_update_work);
	INIT_DELAYED_WORK(&chg_data->softreg_work, max77693_softreg_work);

	chg_data->charger.name = "max77693-charger",
	chg_data->charger.type = POWER_SUPPLY_TYPE_BATTERY,
	chg_data->charger.properties = max77693_charger_props,
	chg_data->charger.num_properties = ARRAY_SIZE(max77693_charger_props),
	chg_data->charger.get_property = max77693_charger_get_property,
	chg_data->charger.set_property = max77693_charger_set_property,

	ret = power_supply_register(&pdev->dev, &chg_data->charger);
	if (ret) {
		pr_err("%s: failed: power supply register\n", __func__);
		goto err_kfree;
	}

	ret = request_threaded_irq(chg_data->irq_bypass, NULL,
			max77693_bypass_irq, 0, "bypass-irq", chg_data);
	if (ret < 0)
		pr_err("%s: fail to request bypass IRQ: %d: %d\n",
				__func__, chg_data->irq_bypass, ret);

	ret = request_threaded_irq(chg_data->irq_battery, NULL,
			max77693_charger_irq, 0, "battery-irq", chg_data);
	if (ret < 0)
		pr_err("%s: fail to request battery IRQ: %d: %d\n",
				__func__, chg_data->irq_battery, ret);

	ret = request_threaded_irq(chg_data->irq_charge, NULL,
			max77693_charger_irq, 0, "charge-irq", chg_data);
	if (ret < 0)
		pr_err("%s: fail to request charge IRQ: %d: %d\n",
				__func__, chg_data->irq_charge, ret);

#if defined(USE_CHGIN_INTR)
	ret = request_threaded_irq(chg_data->irq_chargin, NULL,
			max77693_charger_irq, 0, "chargin-irq", chg_data);
	if (ret < 0)
		pr_err("%s: fail to request charge IRQ: %d: %d\n",
				__func__, chg_data->irq_chargin, ret);
#endif

#ifdef CONFIG_BATTERY_WPC_CHARGER
	chg_data->wc_pwr_det = chg_data->charger_pdata->wc_pwr_det;
#if defined(CONFIG_MACH_M0) || \
	defined(CONFIG_MACH_GRANDE) || defined(CONFIG_MACH_IRON)
	if (system_rev >= 0xA)
		chg_data->wc_pwr_det = true;
#endif
#if defined(CONFIG_MACH_C1)
	if (system_rev >= 0x6)
		chg_data->wc_pwr_det = true;
#endif
#if defined(CONFIG_MACH_M3)
		chg_data->wc_pwr_det = true;
#endif

	if (chg_data->wc_pwr_det)
		pr_info("%s: support wc power detection\n", __func__);
	else
		pr_info("%s: not support wc power detection\n", __func__);

	/* wpc 5V interrupt */
	if (!chg_data->charger_pdata->wpc_irq_gpio) {
		pr_err("%s: no irq gpio, do not support wpc\n", __func__);
		goto wpc_init_finish;
	}

	chg_data->wc_w_gpio = chg_data->charger_pdata->wpc_irq_gpio;
	chg_data->wc_w_irq = gpio_to_irq(chg_data->wc_w_gpio);
	ret = gpio_request(chg_data->wc_w_gpio, "wpc_charger-irq");
	if (ret < 0) {
		pr_err("%s: failed requesting gpio %d\n", __func__,
				chg_data->wc_w_gpio);
		goto wpc_init_finish;
	}
	gpio_direction_input(chg_data->wc_w_gpio);
	gpio_free(chg_data->wc_w_gpio);

	ret = request_threaded_irq(chg_data->wc_w_irq, NULL,
			wpc_charger_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
			IRQF_ONESHOT,
			"wpc-int", chg_data);
	if (ret)
		pr_err("can NOT request irq 'WPC_INT' %d",
		       chg_data->wc_w_irq);

	chg_data->wc_w_state = !gpio_get_value(chg_data->wc_w_gpio);

	/* vbus 5V interrupt */
	if (chg_data->wc_pwr_det == false) {
		pr_info("%s: wpc(%d)\n", __func__, chg_data->wc_w_state);
		goto wpc_init_finish;
	}

	if (!chg_data->charger_pdata->vbus_irq_gpio) {
		pr_err("%s: no irq gpio, not support wc power detection\n",
								__func__);
		goto wpc_init_finish;
	}

	chg_data->wc_v_gpio = chg_data->charger_pdata->vbus_irq_gpio;
	chg_data->wc_v_irq = gpio_to_irq(chg_data->wc_v_gpio);
	ret = gpio_request(chg_data->wc_v_gpio, "vbus_charger-irq");
	if (ret < 0) {
		pr_err("%s: failed requesting gpio %d\n", __func__,
				chg_data->wc_v_gpio);
		goto wpc_init_finish;
	}
	gpio_direction_input(chg_data->wc_v_gpio);
	gpio_free(chg_data->wc_v_gpio);

	ret = request_threaded_irq(chg_data->wc_v_irq, NULL,
			wpc_charger_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
			IRQF_ONESHOT,
			"vbus-int", chg_data);
	if (ret)
		pr_err("can NOT request irq 'V_BUS_INT' %d",
		       chg_data->wc_v_irq);

	if (chg_data->wc_w_state == 1) {
		pr_info("%s: wpc active, set V_BUS_INT as PN\n", __func__);
		s3c_gpio_setpull(chg_data->wc_v_gpio, S3C_GPIO_PULL_NONE);
	} else {
		disable_irq(chg_data->wc_v_irq);
		pr_info("%s: wpc deactive, set V_BUS_INT as PD\n", __func__);
		s3c_gpio_setpull(chg_data->wc_v_gpio, S3C_GPIO_PULL_DOWN);
	}

	chg_data->wc_v_state = !gpio_get_value(chg_data->wc_v_gpio);
	pr_info("%s: wpc(%d), vbus(%d)\n", __func__,
		chg_data->wc_w_state, chg_data->wc_v_state);

wpc_init_finish:
#endif

#if defined(CONFIG_TARGET_LOCALE_KOR)
#ifdef CONFIG_DEBUG_FS
	chg_data->charger_debugfs_dir =
		debugfs_create_dir("charger_debug", NULL);
	if (chg_data->charger_debugfs_dir) {
		if (!debugfs_create_file("max77693_regs", 0644,
			chg_data->charger_debugfs_dir,
			chg_data, &max77693_debugfs_fops))
			pr_err("%s : debugfs_create_file, error\n", __func__);
	} else
		pr_err("%s : debugfs_create_dir, error\n", __func__);
#endif
#endif

	pr_info("%s: probe complete\n", __func__);

	return 0;

err_kfree:
	wake_lock_destroy(&chg_data->update_wake_lock);
	wake_lock_destroy(&chg_data->softreg_wake_lock);

	mutex_destroy(&chg_data->ops_lock);
	mutex_destroy(&chg_data->irq_lock);
	kfree(chg_data);
	return ret;
}

static int __devexit max77693_charger_remove(struct platform_device *pdev)
{
	struct max77693_charger_data *chg_data = platform_get_drvdata(pdev);

	wake_lock_destroy(&chg_data->update_wake_lock);
	wake_lock_destroy(&chg_data->softreg_wake_lock);

	mutex_destroy(&chg_data->ops_lock);
	mutex_destroy(&chg_data->irq_lock);

	power_supply_unregister(&chg_data->charger);

	kfree(chg_data);

	return 0;
}

/*
 * WORKAROUND:
 * Several interrupts occur while charging through TA.
 * Suspended state cannot be maintained by the interrupts.
 */
#ifdef CONFIG_SLP
static u8 saved_int_mask;
#endif
static int max77693_charger_suspend(struct device *dev)
{
	struct max77693_dev *max77693 = dev_get_drvdata(dev->parent);
	u8 int_mask;
	pr_info("%s\n", __func__);

#ifdef CONFIG_SLP
	/* Save the masking value */
	max77693_read_reg(max77693->i2c,
			MAX77693_CHG_REG_CHG_INT_MASK,
			&saved_int_mask);

	/* Mask all the interrupts related to charger */
	int_mask = 0xff;
	max77693_write_reg(max77693->i2c,
			MAX77693_CHG_REG_CHG_INT_MASK,
			int_mask);
#else
#if defined(USE_CHGIN_INTR)
	/* disable chgin irq */
	max77693_read_reg(max77693->i2c,
			MAX77693_CHG_REG_CHG_INT_MASK,
			&int_mask);
	int_mask |= MAX77693_CHGIN_IM;
	max77693_write_reg(max77693->i2c,
			MAX77693_CHG_REG_CHG_INT_MASK,
			int_mask);
#endif
#endif
	return 0;
}

static int max77693_charger_resume(struct device *dev)
{
	struct max77693_dev *max77693 = dev_get_drvdata(dev->parent);
	u8 int_mask;
	pr_info("%s\n", __func__);

#ifdef CONFIG_SLP
	/* Restore the saved masking value */
	max77693_write_reg(max77693->i2c,
			MAX77693_CHG_REG_CHG_INT_MASK,
			saved_int_mask);
#else
#if defined(USE_CHGIN_INTR)
	/* enable chgin irq */
	max77693_read_reg(max77693->i2c,
			MAX77693_CHG_REG_CHG_INT_MASK,
			&int_mask);
	int_mask &= (~MAX77693_CHGIN_IM);
	max77693_write_reg(max77693->i2c,
			MAX77693_CHG_REG_CHG_INT_MASK,
			int_mask);
#endif
#endif

	return 0;
}

static SIMPLE_DEV_PM_OPS(max77693_charger_pm_ops, max77693_charger_suspend,
			max77693_charger_resume);

static struct platform_driver max77693_charger_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "max77693-charger",
		.pm	= &max77693_charger_pm_ops,
	},
	.probe		= max77693_charger_probe,
	.remove		= __devexit_p(max77693_charger_remove),
};

static int __init max77693_charger_init(void)
{
	return platform_driver_register(&max77693_charger_driver);
}

static void __exit max77693_charger_exit(void)
{
	platform_driver_unregister(&max77693_charger_driver);
}

module_init(max77693_charger_init);
module_exit(max77693_charger_exit);

MODULE_AUTHOR("SangYoung Son <hello.son@samsung.com>");
MODULE_DESCRIPTION("max77693 Charger driver");
MODULE_LICENSE("GPL");
