// SPDX-License-Identifier: GPL-2.0
/*
 * Chrager driver for Sgm4154x
 *
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 *
 * Author: Xu Shengfei <xsf@rock-chips.com>
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/types.h>

static int dbg_enable;

module_param_named(dbg_level, dbg_enable, int, 0644);

#define DBG(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)

#define SGM4154x_MANUFACTURER	"SGMICRO"
#define SGM4154x_NAME		"sgm41542"
#define SGM4154x_PN_ID		(BIT(6) | BIT(5) | BIT(3))

/* define register */
#define SGM4154x_CHRG_CTRL_0	0x00
#define SGM4154x_CHRG_CTRL_1	0x01
#define SGM4154x_CHRG_CTRL_2	0x02
#define SGM4154x_CHRG_CTRL_3	0x03
#define SGM4154x_CHRG_CTRL_4	0x04
#define SGM4154x_CHRG_CTRL_5	0x05
#define SGM4154x_CHRG_CTRL_6	0x06
#define SGM4154x_CHRG_CTRL_7	0x07
#define SGM4154x_CHRG_STAT	0x08
#define SGM4154x_CHRG_FAULT	0x09
#define SGM4154x_CHRG_CTRL_a	0x0a
#define SGM4154x_CHRG_CTRL_b	0x0b
#define SGM4154x_CHRG_CTRL_c	0x0c
#define SGM4154x_CHRG_CTRL_d	0x0d
#define SGM4154x_INPUT_DET	0x0e
#define SGM4154x_CHRG_CTRL_f	0x0f

/* charge status flags */
#define SGM4154x_CHRG_EN		BIT(4)
#define SGM4154x_HIZ_EN			BIT(7)
#define SGM4154x_TERM_EN		BIT(7)
#define SGM4154x_VAC_OVP_MASK		GENMASK(7, 6)
#define SGM4154x_DPDM_ONGOING		BIT(7)
#define SGM4154x_VBUS_GOOD		BIT(7)

#define SGM4154x_BOOSTV			GENMASK(5, 4)
#define SGM4154x_BOOST_LIM		BIT(7)
#define SGM4154x_OTG_EN			BIT(5)

/* Part ID */
#define SGM4154x_PN_MASK		GENMASK(6, 3)

/* WDT TIMER SET */
#define SGM4154x_WDT_TIMER_MASK		GENMASK(5, 4)
#define SGM4154x_WDT_TIMER_DISABLE	0
#define SGM4154x_WDT_TIMER_40S		BIT(4)
#define SGM4154x_WDT_TIMER_80S		BIT(5)
#define SGM4154x_WDT_TIMER_160S		(BIT(4) | BIT(5))

#define SGM4154x_WDT_RST_MASK		BIT(6)

/* SAFETY TIMER SET */
#define SGM4154x_SAFETY_TIMER_MASK	GENMASK(3, 3)
#define SGM4154x_SAFETY_TIMER_DISABLE	0
#define SGM4154x_SAFETY_TIMER_EN	BIT(3)
#define SGM4154x_SAFETY_TIMER_5H	0
#define SGM4154x_SAFETY_TIMER_10H	BIT(2)


/* recharge voltage */
#define SGM4154x_VRECHARGE		BIT(0)
#define SGM4154x_VRECHRG_STEP_mV	100
#define SGM4154x_VRECHRG_OFFSET_mV	100

/* charge status */
#define SGM4154x_VSYS_STAT		BIT(0)
#define SGM4154x_THERM_STAT		BIT(1)
#define SGM4154x_PG_STAT		BIT(2)
#define SGM4154x_CHG_STAT_MASK		GENMASK(4, 3)
#define SGM4154x_PRECHRG		BIT(3)
#define SGM4154x_FAST_CHRG		BIT(4)
#define SGM4154x_TERM_CHRG		(BIT(3) | BIT(4))

/* charge type */
#define SGM4154x_VBUS_STAT_MASK		GENMASK(7, 5)
#define SGM4154x_NOT_CHRGING		0
#define SGM4154x_USB_SDP		BIT(5)
#define SGM4154x_USB_CDP		BIT(6)
#define SGM4154x_USB_DCP		(BIT(5) | BIT(6))
#define SGM4154x_UNKNOWN		(BIT(7) | BIT(5))
#define SGM4154x_NON_STANDARD		(BIT(7) | BIT(6))
#define SGM4154x_OTG_MODE		(BIT(7) | BIT(6) | BIT(5))

/* TEMP Status */
#define SGM4154x_TEMP_MASK			GENMASK(2, 0)
#define SGM4154x_TEMP_NORMAL			BIT(0)
#define SGM4154x_TEMP_WARM			BIT(1)
#define SGM4154x_TEMP_COOL			(BIT(0) | BIT(1))
#define SGM4154x_TEMP_COLD			(BIT(0) | BIT(3))
#define SGM4154x_TEMP_HOT			(BIT(2) | BIT(3))

/* precharge current */
#define SGM4154x_PRECHRG_CUR_MASK		GENMASK(7, 4)
#define SGM4154x_PRECHRG_CURRENT_STEP_uA	60000
#define SGM4154x_PRECHRG_I_MIN_uA		60000
#define SGM4154x_PRECHRG_I_MAX_uA		780000
#define SGM4154x_PRECHRG_I_DEF_uA		180000

/* termination current */
#define SGM4154x_TERMCHRG_CUR_MASK		GENMASK(3, 0)
#define SGM4154x_TERMCHRG_CURRENT_STEP_uA	60000
#define SGM4154x_TERMCHRG_I_MIN_uA		60000
#define SGM4154x_TERMCHRG_I_MAX_uA		960000
#define SGM4154x_TERMCHRG_I_DEF_uA		180000

/* charge current */
#define SGM4154x_ICHRG_CUR_MASK		GENMASK(5, 0)
#define SGM4154x_ICHRG_I_STEP_uA	60000
#define SGM4154x_ICHRG_I_MIN_uA		0
#define SGM4154x_ICHRG_I_MAX_uA		3780000
#define SGM4154x_ICHRG_I_DEF_uA		2040000

/* charge voltage */
#define SGM4154x_VREG_V_MASK		GENMASK(7, 3)
#define SGM4154x_VREG_V_MAX_uV		4624000
#define SGM4154x_VREG_V_MIN_uV		3856000
#define SGM4154x_VREG_V_DEF_uV		4208000
#define SGM4154x_VREG_V_STEP_uV		32000

/* VREG Fine Tuning */
#define SGM4154x_VREG_FT_MASK		GENMASK(7, 6)
#define SGM4154x_VREG_FT_UP_8mV		BIT(6)
#define SGM4154x_VREG_FT_DN_8mV		BIT(7)
#define SGM4154x_VREG_FT_DN_16mV	(BIT(7) | BIT(6))

/* iindpm current */
#define SGM4154x_IINDPM_I_MASK		GENMASK(4, 0)
#define SGM4154x_IINDPM_I_MIN_uA	100000
#define SGM4154x_IINDPM_I_MAX_uA	3800000
#define SGM4154x_IINDPM_STEP_uA		100000
#define SGM4154x_IINDPM_DEF_uA		2400000

/* vindpm voltage */
#define SGM4154x_VINDPM_V_MASK		GENMASK(3, 0)
#define SGM4154x_VINDPM_V_MIN_uV	3900000
#define SGM4154x_VINDPM_V_MAX_uV	12000000
#define SGM4154x_VINDPM_STEP_uV		100000
#define SGM4154x_VINDPM_DEF_uV		4500000
#define SGM4154x_VINDPM_OS_MASK		GENMASK(1, 0)

/* DP DM SEL */
#define SGM4154x_DP_VSEL_MASK		GENMASK(4, 3)
#define SGM4154x_DM_VSEL_MASK		GENMASK(2, 1)

/* PUMPX SET */
#define SGM4154x_EN_PUMPX		BIT(7)
#define SGM4154x_PUMPX_UP		BIT(6)
#define SGM4154x_PUMPX_DN		BIT(5)

struct sgm4154x_init_data {
	int ichg;	/* charge current */
	int ilim;	/* input current */
	int vreg;	/* regulation voltage */
	int iterm;	/* termination current */
	int iprechg;	/* precharge current */
	int vlim;	/* minimum system voltage limit */
	int max_ichg;
	int max_vreg;
};

struct sgm4154x_state {
	bool vsys_stat;
	bool therm_stat;
	bool online;
	u8 chrg_stat;
	u8 vbus_status;

	bool chrg_en;
	bool hiz_en;
	bool term_en;
	bool vbus_gd;
	u8 chrg_type;
	u8 health;
	u8 chrg_fault;
	u8 ntc_fault;
};

struct sgm4154x_device {
	struct i2c_client *client;
	struct device *dev;
	struct power_supply *charger;
	struct mutex lock;
	struct mutex i2c_rw_lock;
	struct regmap *regmap;

	char model_name[I2C_NAME_SIZE];
	int device_id;

	struct sgm4154x_init_data init_data;
	struct sgm4154x_state state;
	u32 watchdog_timer;
	struct regulator_dev *otg_rdev;
	struct notifier_block pm_nb;
	int input_current;
	bool sgm4154x_suspend_flag;
};

/* SGM4154x REG06 BOOST_LIM[5:4], uV */
static const unsigned int BOOST_VOLT_LIMIT[] = {
	4850000, 5000000, 5150000, 5300000
};

static const unsigned int BOOST_CURRENT_LIMIT[] = {
	1200000, 2000000
};

enum SGM4154x_VINDPM_OS {
	VINDPM_OS_3900mV,
	VINDPM_OS_5900mV,
	VINDPM_OS_7500mV,
	VINDPM_OS_10500mV,
};

static int sgm4154x_set_term_curr(struct sgm4154x_device *sgm, int uA)
{
	int reg_val;
	int ret;

	if (uA < SGM4154x_TERMCHRG_I_MIN_uA)
		uA = SGM4154x_TERMCHRG_I_MIN_uA;
	else if (uA > SGM4154x_TERMCHRG_I_MAX_uA)
		uA = SGM4154x_TERMCHRG_I_MAX_uA;

	reg_val = (uA - SGM4154x_TERMCHRG_I_MIN_uA) / SGM4154x_TERMCHRG_CURRENT_STEP_uA;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_3,
				 SGM4154x_TERMCHRG_CUR_MASK,
				 reg_val);
	if (ret) {
		dev_err(sgm->dev, "set term current error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_set_prechrg_curr(struct sgm4154x_device *sgm, int uA)
{
	int reg_val;
	int ret;

	if (uA < SGM4154x_PRECHRG_I_MIN_uA)
		uA = SGM4154x_PRECHRG_I_MIN_uA;
	else if (uA > SGM4154x_PRECHRG_I_MAX_uA)
		uA = SGM4154x_PRECHRG_I_MAX_uA;

	reg_val = (uA - SGM4154x_PRECHRG_I_MIN_uA) / SGM4154x_PRECHRG_CURRENT_STEP_uA;

	reg_val = reg_val << 4;
	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_3,
				 SGM4154x_PRECHRG_CUR_MASK,
				 reg_val);
	if (ret) {
		dev_err(sgm->dev, "set precharge current error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_set_ichrg_curr(struct sgm4154x_device *sgm, int uA)
{
	int reg_val;
	int ret;

	if (uA < SGM4154x_ICHRG_I_MIN_uA)
		uA = SGM4154x_ICHRG_I_MIN_uA;
	else if (uA > sgm->init_data.max_ichg)
		uA = sgm->init_data.max_ichg;

	reg_val = uA / SGM4154x_ICHRG_I_STEP_uA;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_2,
				 SGM4154x_ICHRG_CUR_MASK,
				 reg_val);
	if (ret) {
		dev_err(sgm->dev, "set icharge current error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_set_chrg_volt(struct sgm4154x_device *sgm, int chrg_volt)
{
	int reg_val;
	int ret;

	if (chrg_volt < SGM4154x_VREG_V_MIN_uV)
		chrg_volt = SGM4154x_VREG_V_MIN_uV;
	else if (chrg_volt > sgm->init_data.max_vreg)
		chrg_volt = sgm->init_data.max_vreg;

	reg_val = (chrg_volt - SGM4154x_VREG_V_MIN_uV) / SGM4154x_VREG_V_STEP_uV;
	reg_val = reg_val << 3;
	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_4,
				 SGM4154x_VREG_V_MASK,
				 reg_val);

	if (ret) {
		dev_err(sgm->dev, "set charge voltage error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_set_vindpm_offset_os(struct sgm4154x_device *sgm,
					 enum SGM4154x_VINDPM_OS offset_os)
{
	int ret;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_f,
				 SGM4154x_VINDPM_OS_MASK,
				 offset_os);

	if (ret) {
		dev_err(sgm->dev, "set vindpm offset os error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_set_input_volt_lim(struct sgm4154x_device *sgm,
				       unsigned int vindpm)
{
	enum SGM4154x_VINDPM_OS os_val;
	unsigned int offset;
	u8 reg_val;
	int ret;


	if (vindpm < SGM4154x_VINDPM_V_MIN_uV ||
	    vindpm > SGM4154x_VINDPM_V_MAX_uV)
		return -EINVAL;

	if (vindpm < 5900000) {
		os_val = VINDPM_OS_3900mV;
		offset = 3900000;
	} else if (vindpm >= 5900000 && vindpm < 7500000) {
		os_val = VINDPM_OS_5900mV;
		offset = 5900000;
	} else if (vindpm >= 7500000 && vindpm < 10500000) {
		os_val = VINDPM_OS_7500mV;
		offset = 7500000;
	} else {
		os_val = VINDPM_OS_10500mV;
		offset = 10500000;
	}

	ret = sgm4154x_set_vindpm_offset_os(sgm, os_val);
	if (ret) {
		dev_err(sgm->dev, "set vin dpm error!\n");
		return ret;
	}

	reg_val = (vindpm - offset) / SGM4154x_VINDPM_STEP_uV;

	ret = regmap_update_bits(sgm->regmap, SGM4154x_CHRG_CTRL_6,
				 SGM4154x_VINDPM_V_MASK, reg_val);
	if (ret) {
		dev_err(sgm->dev, "input voltage error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_set_input_curr_lim(struct sgm4154x_device *sgm, int iindpm)
{
	int reg_val;
	int ret;

	if (iindpm > sgm->init_data.ilim)
		iindpm = sgm->init_data.ilim;
	sgm->input_current = iindpm;
	if (iindpm < SGM4154x_IINDPM_I_MIN_uA ||
			iindpm > SGM4154x_IINDPM_I_MAX_uA)
		return -EINVAL;

	if (iindpm >= SGM4154x_IINDPM_I_MIN_uA && iindpm <= 3100000)
		reg_val = (iindpm-SGM4154x_IINDPM_I_MIN_uA) / SGM4154x_IINDPM_STEP_uA;
	else if (iindpm > 3100000 && iindpm < SGM4154x_IINDPM_I_MAX_uA)
		reg_val = 0x1E;
	else
		reg_val = 0x1F;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_0,
				 SGM4154x_IINDPM_I_MASK,
				 reg_val);
	if (ret) {
		dev_err(sgm->dev, "set input current limit error!\n");
		return ret;
	}

	return ret;

}

static int sgm4154x_get_input_curr_lim(struct sgm4154x_device *sgm)
{
	int ret;
	int ilim;

	ret = regmap_read(sgm->regmap, SGM4154x_CHRG_CTRL_0, &ilim);
	if (ret) {
		dev_err(sgm->dev, "get input current limit error!\n");
		return ret;
	}
	if (SGM4154x_IINDPM_I_MASK == (ilim & SGM4154x_IINDPM_I_MASK))
		return SGM4154x_IINDPM_I_MAX_uA;

	ilim = (ilim & SGM4154x_IINDPM_I_MASK) * SGM4154x_IINDPM_STEP_uA + SGM4154x_IINDPM_I_MIN_uA;

	return ilim;
}

static int sgm4154x_set_watchdog_timer(struct sgm4154x_device *sgm, int time)
{
	u8 reg_val;
	int ret;

	if (time == 0)
		reg_val = SGM4154x_WDT_TIMER_DISABLE;
	else if (time == 40)
		reg_val = SGM4154x_WDT_TIMER_40S;
	else if (time == 80)
		reg_val = SGM4154x_WDT_TIMER_80S;
	else
		reg_val = SGM4154x_WDT_TIMER_160S;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_5,
				 SGM4154x_WDT_TIMER_MASK,
				 reg_val);

	if (ret) {
		dev_err(sgm->dev, "set watchdog timer error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_enable_charger(struct sgm4154x_device *sgm)
{
	int ret;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_1,
				 SGM4154x_CHRG_EN,
				 SGM4154x_CHRG_EN);
	if (ret) {
		dev_err(sgm->dev, "enable charger error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_disable_charger(struct sgm4154x_device *sgm)
{
	int ret;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_1,
				 SGM4154x_CHRG_EN,
				 0);
	if (ret) {
		dev_err(sgm->dev, "disable charger error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_set_vac_ovp(struct sgm4154x_device *sgm)
{
	int reg_val;
	int ret;

	reg_val = 0xFF & SGM4154x_VAC_OVP_MASK;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_6,
				 SGM4154x_VAC_OVP_MASK,
				 reg_val);
	if (ret) {
		dev_err(sgm->dev, "set vac ovp error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_set_recharge_volt(struct sgm4154x_device *sgm, int recharge_volt)
{
	int reg_val;
	int ret;

	reg_val = (recharge_volt - SGM4154x_VRECHRG_OFFSET_mV) / SGM4154x_VRECHRG_STEP_mV;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_4,
				 SGM4154x_VRECHARGE,
				 reg_val);
	if (ret) {
		dev_err(sgm->dev, "set recharger error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_get_state(struct sgm4154x_device *sgm,
			      struct sgm4154x_state *state)
{
	int chrg_param_0, chrg_param_1, chrg_param_2;
	int chrg_stat;
	int fault;
	int ret;

	ret = regmap_read(sgm->regmap, SGM4154x_CHRG_STAT, &chrg_stat);
	if (ret) {
		pr_err("read SGM4154x_CHRG_STAT fail\n");
		return ret;
	}

	DBG("SGM4154x_CHRG_STAT[0x%x]: 0x%x\n", SGM4154x_CHRG_STAT, chrg_stat);
	state->chrg_type = chrg_stat & SGM4154x_VBUS_STAT_MASK;
	state->chrg_stat = chrg_stat & SGM4154x_CHG_STAT_MASK;
	state->online = !!(chrg_stat & SGM4154x_PG_STAT);
	state->therm_stat = !!(chrg_stat & SGM4154x_THERM_STAT);
	state->vsys_stat = !!(chrg_stat & SGM4154x_VSYS_STAT);

	ret = regmap_read(sgm->regmap, SGM4154x_CHRG_FAULT, &fault);
	if (ret) {
		pr_err("read SGM4154x_CHRG_FAULT fail\n");
		return ret;
	}
	DBG("SGM4154x_CHRG_FAULT[0x%x]: 0x%x\n", SGM4154x_CHRG_FAULT, fault);

	state->chrg_fault = fault;
	state->ntc_fault = fault & SGM4154x_TEMP_MASK;
	state->health = state->ntc_fault;
	ret = regmap_read(sgm->regmap, SGM4154x_CHRG_CTRL_0, &chrg_param_0);
	if (ret) {
		pr_err("read SGM4154x_CHRG_CTRL_0 fail\n");
		return ret;
	}
	state->hiz_en = !!(chrg_param_0 & SGM4154x_HIZ_EN);
	DBG("SGM4154x_CHRG_CTRL_0[0x%x]: 0x%x\n", SGM4154x_CHRG_CTRL_0, chrg_param_0);

	ret = regmap_read(sgm->regmap, SGM4154x_CHRG_CTRL_5, &chrg_param_1);
	if (ret) {
		pr_err("read SGM4154x_CHRG_CTRL_5 fail\n");
		return ret;
	}
	state->term_en = !!(chrg_param_1 & SGM4154x_TERM_EN);
	DBG("SGM4154x_CHRG_CTRL_5[0x%x]: 0x%x\n", SGM4154x_CHRG_CTRL_5, chrg_param_1);

	ret = regmap_read(sgm->regmap, SGM4154x_CHRG_CTRL_a, &chrg_param_2);
	if (ret) {
		pr_err("read SGM4154x_CHRG_CTRL_a fail\n");
		return ret;
	}
	state->vbus_gd = !!(chrg_param_2 & SGM4154x_VBUS_GOOD);
	DBG("SGM4154x_CHRG_CTRL_a[0x%x]: 0x%x\n", SGM4154x_CHRG_CTRL_a, chrg_param_2);

	DBG("chrg_type: 0x%x\n", state->chrg_type);
	DBG("chrg_stat: 0x%x\n", state->chrg_stat);
	DBG("online: 0x%x\n", state->online);
	DBG("therm_stat: 0x%x\n", state->therm_stat);
	DBG("vsys_stat: 0x%x\n", state->vsys_stat);
	DBG("chrg_fault: 0x%x\n", state->chrg_fault);
	DBG("ntc_fault: 0x%x\n", state->ntc_fault);
	DBG("health: 0x%x\n", state->health);
	DBG("hiz_en: 0x%x\n", state->hiz_en);
	DBG("term_en: 0x%x\n", state->term_en);
	DBG("vbus_gd: 0x%x\n", state->vbus_gd);

	return ret;
}

static int sgm4154x_property_is_writeable(struct power_supply *psy,
					  enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_ONLINE:
		return true;
	default:
		return false;
	}
}

static int sgm4154x_charger_set_property(struct power_supply *psy,
					 enum power_supply_property prop,
					 const union power_supply_propval *val)
{
	struct sgm4154x_device *sgm = power_supply_get_drvdata(psy);
	int ret = -EINVAL;

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		DBG("ONLINE: %d", val->intval);
		if (val->intval)
			ret = sgm4154x_enable_charger(sgm);
		else
			ret = sgm4154x_disable_charger(sgm);
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		DBG("INPUT_CURRENT_LIMIT: %d\n", val->intval);
		ret = sgm4154x_set_input_curr_lim(sgm, val->intval);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = sgm4154x_set_chrg_volt(sgm, val->intval);
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static int sgm4154x_charger_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct sgm4154x_device *sgm = power_supply_get_drvdata(psy);
	struct sgm4154x_state state;
	int ret = 0;

	mutex_lock(&sgm->lock);
	ret = sgm4154x_get_state(sgm, &state);
	if (ret) {
		dev_err(sgm->dev, "get state error!\n");
		mutex_unlock(&sgm->lock);
		return ret;
	}
	sgm->state = state;
	mutex_unlock(&sgm->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (!state.chrg_type || (state.chrg_type == SGM4154x_OTG_MODE))
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (!state.chrg_stat)
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else if (state.chrg_stat == SGM4154x_TERM_CHRG)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		switch (state.chrg_stat) {
		case SGM4154x_PRECHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case SGM4154x_FAST_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
			break;
		case SGM4154x_TERM_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case SGM4154x_NOT_CHRGING:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
			break;
		default:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		}
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = SGM4154x_MANUFACTURER;
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = SGM4154x_NAME;
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = state.online;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = state.vbus_gd;
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = sgm->init_data.max_vreg;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		val->intval = SGM4154x_ICHRG_I_MAX_uA;
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		val->intval = 12 * 1000 * 1000;
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		 val->intval = sgm4154x_get_input_curr_lim(sgm);
		if (val->intval < 0)
			return  -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static ssize_t registers_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct sgm4154x_device *sgm4154x = dev_get_drvdata(dev);
	u8 tmpbuf[30];
	int idx = 0;
	u8 addr;
	int val;
	int len;
	int ret;

	for (addr = 0x0; addr <= SGM4154x_CHRG_CTRL_f; addr++) {
		ret = regmap_read(sgm4154x->regmap, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, 30,
				       "Reg[%.2X] = 0x%.2x\n",
				       addr,
				       val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t registers_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct sgm4154x_device *sgm4154x = dev_get_drvdata(dev);
	unsigned int reg;
	int ret;
	int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= SGM4154x_CHRG_CTRL_f)
		regmap_write(sgm4154x->regmap, (unsigned char)reg, val);

	return count;
}
static DEVICE_ATTR_RW(registers);

static void sgm4154x_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}

static irqreturn_t sgm4154x_irq_handler_thread(int irq, void *private)
{
	struct sgm4154x_device *sgm4154x = private;
	struct sgm4154x_state state;
	int ret;

	ret = sgm4154x_get_state(sgm4154x, &state);
	if (ret) {
		dev_err(sgm4154x->dev, "get state error!\n");
		return IRQ_NONE;
	}
	sgm4154x->state = state;
	if (state.vbus_gd) {
		ret = sgm4154x_set_input_curr_lim(sgm4154x, sgm4154x->input_current);
		if (ret) {
			dev_err(sgm4154x->dev, "set input current error!\n");
			return IRQ_NONE;
		}
	}
	power_supply_changed(sgm4154x->charger);

	return IRQ_HANDLED;
}

static enum power_supply_property sgm4154x_power_supply_props[] = {
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_PRESENT
};

static char *sgm4154x_charger_supplied_to[] = {
	"usb",
};

static struct power_supply_desc sgm4154x_power_supply_desc = {
	.name = "sgm4154x-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = sgm4154x_power_supply_props,
	.num_properties = ARRAY_SIZE(sgm4154x_power_supply_props),
	.get_property = sgm4154x_charger_get_property,
	.set_property = sgm4154x_charger_set_property,
	.property_is_writeable = sgm4154x_property_is_writeable,
};

static bool sgm4154x_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SGM4154x_CHRG_CTRL_0 ... SGM4154x_CHRG_CTRL_f:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config sgm4154x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = SGM4154x_CHRG_CTRL_f,

	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = sgm4154x_is_volatile_reg,
};

static int sgm4154x_power_supply_init(struct sgm4154x_device *sgm,
				      struct device *dev)
{
	struct power_supply_config psy_cfg = { .drv_data = sgm,
					       .of_node = dev->of_node, };

	psy_cfg.supplied_to = sgm4154x_charger_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(sgm4154x_charger_supplied_to);
	psy_cfg.of_node = dev->of_node;
	sgm->charger = devm_power_supply_register(sgm->dev,
						  &sgm4154x_power_supply_desc,
						  &psy_cfg);
	if (IS_ERR(sgm->charger))
		return -EINVAL;

	return 0;
}

static int sgm4154x_hw_init(struct sgm4154x_device *sgm)
{
	struct power_supply_battery_info bat_info = { };
	int ret = 0;

	ret = power_supply_get_battery_info(sgm->charger, &bat_info);
	if (ret) {
		pr_info("sgm4154x: no battery information is supplied\n");
		/*
		 * If no battery information is supplied, we should set
		 * default charge termination current to 120 mA, and default
		 * charge termination voltage to 4.35V.
		 */
		bat_info.constant_charge_current_max_ua =
			SGM4154x_ICHRG_I_DEF_uA;
		bat_info.constant_charge_voltage_max_uv =
			SGM4154x_VREG_V_DEF_uV;
		bat_info.precharge_current_ua =
			SGM4154x_PRECHRG_I_DEF_uA;
		bat_info.charge_term_current_ua =
			SGM4154x_TERMCHRG_I_DEF_uA;
		sgm->init_data.max_ichg =
			SGM4154x_ICHRG_I_MAX_uA;
		sgm->init_data.max_vreg = SGM4154x_VREG_V_DEF_uV;
	}
	if (!bat_info.constant_charge_current_max_ua)
		bat_info.constant_charge_current_max_ua =
					SGM4154x_ICHRG_I_MAX_uA;
	if (!bat_info.constant_charge_voltage_max_uv)
		bat_info.constant_charge_voltage_max_uv =
			SGM4154x_VREG_V_DEF_uV;
	if (!bat_info.precharge_current_ua)
		bat_info.precharge_current_ua =
			SGM4154x_PRECHRG_I_DEF_uA;
	if (!bat_info.charge_term_current_ua)
		bat_info.charge_term_current_ua =
			SGM4154x_TERMCHRG_I_DEF_uA;
	if (!sgm->init_data.max_ichg)
		sgm->init_data.max_ichg =
			SGM4154x_ICHRG_I_MAX_uA;

	if (bat_info.constant_charge_voltage_max_uv)
		sgm->init_data.max_vreg = bat_info.constant_charge_voltage_max_uv;

	ret = sgm4154x_set_watchdog_timer(sgm, 0);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_ichrg_curr(sgm,
				      bat_info.constant_charge_current_max_ua);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_prechrg_curr(sgm, bat_info.precharge_current_ua);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_chrg_volt(sgm,
				     sgm->init_data.max_vreg);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_term_curr(sgm,
				     bat_info.charge_term_current_ua);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_input_volt_lim(sgm, sgm->init_data.vlim);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_input_curr_lim(sgm, 500 * 1000);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_vac_ovp(sgm);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_recharge_volt(sgm, 200);
	if (ret)
		goto err_out;

	DBG("ichrg_curr:%d\n"
	    "prechrg_curr:%d\n"
	    "chrg_vol:%d\n"
	    "term_curr:%d\n"
	    "input_curr_lim:%d\n",
	    bat_info.constant_charge_current_max_ua,
	    bat_info.precharge_current_ua,
	    bat_info.constant_charge_voltage_max_uv,
	    bat_info.charge_term_current_ua,
	    sgm->init_data.ilim);

	return 0;

err_out:
	return ret;
}

static int sgm4154x_parse_dt(struct sgm4154x_device *sgm)
{
	int ret;

	ret = device_property_read_u32(sgm->dev,
				       "input-voltage-limit-microvolt",
				       &sgm->init_data.vlim);
	if (ret)
		sgm->init_data.vlim = SGM4154x_VINDPM_DEF_uV;

	if (sgm->init_data.vlim > SGM4154x_VINDPM_V_MAX_uV ||
	    sgm->init_data.vlim < SGM4154x_VINDPM_V_MIN_uV)
		return -EINVAL;

	ret = device_property_read_u32(sgm->dev,
				       "input-current-limit-microamp",
				       &sgm->init_data.ilim);
	if (ret)
		sgm->init_data.ilim = SGM4154x_IINDPM_DEF_uA;

	if (sgm->init_data.ilim > SGM4154x_IINDPM_I_MAX_uA ||
	    sgm->init_data.ilim < SGM4154x_IINDPM_I_MIN_uA)
		return -EINVAL;

	return 0;
}

static int sgm4154x_set_otg_voltage(struct sgm4154x_device *sgm, int uv)
{
	int ret = 0;
	int reg_val = -1;
	int i = 0;

	while (i < 4) {
		if (uv == BOOST_VOLT_LIMIT[i]) {
			reg_val = i;
			break;
		}
		i++;
	}
	if (reg_val < 0)
		return reg_val;
	reg_val = reg_val << 4;
	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_6,
				 SGM4154x_BOOSTV,
				 reg_val);
	if (ret) {
		dev_err(sgm->dev, "set otg voltage error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_set_otg_current(struct sgm4154x_device *sgm, int ua)
{
	int ret = 0;

	if (ua == BOOST_CURRENT_LIMIT[0]) {
		ret = regmap_update_bits(sgm->regmap,
					 SGM4154x_CHRG_CTRL_2,
					 SGM4154x_BOOST_LIM,
					 0);
		if (ret) {
			dev_err(sgm->dev, "set boost current limit error!\n");
			return ret;
		}
	} else if (ua == BOOST_CURRENT_LIMIT[1]) {
		ret = regmap_update_bits(sgm->regmap,
					 SGM4154x_CHRG_CTRL_2,
					 SGM4154x_BOOST_LIM,
					 BIT(7));
		if (ret) {
			dev_err(sgm->dev, "set boost current limit error!\n");
			return ret;
		}
	}

	return ret;
}

static int sgm4154x_enable_vbus(struct regulator_dev *rdev)
{
	struct sgm4154x_device *sgm = rdev_get_drvdata(rdev);
	int ret = 0;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_1,
				 SGM4154x_OTG_EN,
				 SGM4154x_OTG_EN);
	if (ret) {
		dev_err(sgm->dev, "set OTG enable error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_disable_vbus(struct regulator_dev *rdev)
{
	struct sgm4154x_device *sgm = rdev_get_drvdata(rdev);
	int ret = 0;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154x_CHRG_CTRL_1,
				 SGM4154x_OTG_EN,
				 0);
	if (ret) {
		dev_err(sgm->dev, "set OTG disable error!\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_is_enabled_vbus(struct regulator_dev *rdev)
{
	struct sgm4154x_device *sgm = rdev_get_drvdata(rdev);
	int temp = 0;
	int ret = 0;

	ret = regmap_read(sgm->regmap, SGM4154x_CHRG_CTRL_1, &temp);
	if (ret) {
		dev_err(sgm->dev, "get vbus status error!\n");
		return ret;
	}

	return (temp & SGM4154x_OTG_EN) ? 1 : 0;
}

static const struct regulator_ops sgm4154x_vbus_ops = {
	.enable = sgm4154x_enable_vbus,
	.disable = sgm4154x_disable_vbus,
	.is_enabled = sgm4154x_is_enabled_vbus,
};

static struct regulator_desc sgm4154x_otg_rdesc = {
	.of_match = "otg-vbus",
	.name = "otg-vbus",
	.regulators_node = of_match_ptr("regulators"),
	.ops = &sgm4154x_vbus_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int sgm4154x_vbus_regulator_register(struct sgm4154x_device *sgm)
{
	struct device_node *np;
	struct regulator_config config = {};
	int ret = 0;

	np = of_get_child_by_name(sgm->dev->of_node, "regulators");
	if (!np) {
		dev_warn(sgm->dev, "cannot find regulators node\n");
		return -ENXIO;
	}

	/* otg regulator */
	config.dev = sgm->dev;
	config.driver_data = sgm;
	sgm->otg_rdev = devm_regulator_register(sgm->dev,
						&sgm4154x_otg_rdesc,
						&config);
	if (IS_ERR(sgm->otg_rdev))
		ret = PTR_ERR(sgm->otg_rdev);

	return ret;
}

static int sgm4154x_suspend_notifier(struct notifier_block *nb,
				     unsigned long event,
				     void *dummy)
{
	struct sgm4154x_device *sgm = container_of(nb, struct sgm4154x_device, pm_nb);

	switch (event) {

	case PM_SUSPEND_PREPARE:
		sgm->sgm4154x_suspend_flag = 1;
		return NOTIFY_OK;

	case PM_POST_SUSPEND:
		sgm->sgm4154x_suspend_flag = 0;
		return NOTIFY_OK;

	default:
		return NOTIFY_DONE;
	}
}

static int sgm4154x_hw_chipid_detect(struct sgm4154x_device *sgm)
{
	int ret = 0;
	int val = 0;

	ret = regmap_read(sgm->regmap, SGM4154x_CHRG_CTRL_b, &val);
	if (ret < 0)
		return ret;

	return val;
}

static int sgm4154x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct sgm4154x_device *sgm;
	int ret;

	sgm = devm_kzalloc(dev, sizeof(*sgm), GFP_KERNEL);
	if (!sgm)
		return -ENOMEM;

	sgm->client = client;
	sgm->dev = dev;

	mutex_init(&sgm->lock);

	strncpy(sgm->model_name, id->name, I2C_NAME_SIZE);

	sgm->regmap = devm_regmap_init_i2c(client, &sgm4154x_regmap_config);
	if (IS_ERR(sgm->regmap)) {
		dev_err(dev, "Failed to allocate register map\n");
		return PTR_ERR(sgm->regmap);
	}

	i2c_set_clientdata(client, sgm);

	ret = sgm4154x_parse_dt(sgm);
	if (ret) {
		dev_err(dev, "Failed to read device tree properties%d\n", ret);
		return ret;
	}

	ret = sgm4154x_hw_chipid_detect(sgm);
	if ((ret & SGM4154x_PN_MASK) != SGM4154x_PN_ID) {
		pr_info("[%s] device not found !\n", __func__);
		return ret;
	}

	device_init_wakeup(dev, 1);

	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						sgm4154x_irq_handler_thread,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"sgm41542-irq", sgm);
		if (ret)
			goto error_out;
		enable_irq_wake(client->irq);
	}

	sgm->pm_nb.notifier_call = sgm4154x_suspend_notifier;
	register_pm_notifier(&sgm->pm_nb);

	ret = sgm4154x_power_supply_init(sgm, dev);
	if (ret) {
		dev_err(dev, "Failed to register power supply\n");
		goto error_out;
	}

	ret = sgm4154x_hw_init(sgm);
	if (ret) {
		dev_err(dev, "Cannot initialize the chip.\n");
		goto error_out;
	}

	/* OTG setting 5V/1.2A */
	ret = sgm4154x_set_otg_voltage(sgm, 5000000);
	if (ret) {
		dev_err(sgm->dev, "set OTG voltage error!\n");
		return ret;
	}

	ret = sgm4154x_set_otg_current(sgm, 1200000);
	if (ret) {
		dev_err(sgm->dev, "set OTG current error!\n");
		return ret;
	}

	sgm4154x_vbus_regulator_register(sgm);
	sgm4154x_create_device_node(sgm->dev);
	return ret;
error_out:

	return ret;
}

static int sgm4154x_charger_remove(struct i2c_client *client)
{
	struct sgm4154x_device *sgm = i2c_get_clientdata(client);

	regulator_unregister(sgm->otg_rdev);
	power_supply_unregister(sgm->charger);
	mutex_destroy(&sgm->lock);

	return 0;
}

static void sgm4154x_charger_shutdown(struct i2c_client *client)
{
	struct sgm4154x_device *sgm = i2c_get_clientdata(client);
	int ret = 0;

	ret = sgm4154x_disable_charger(sgm);
	if (ret)
		pr_err("Failed to disable charger, ret = %d\n", ret);
}

static const struct i2c_device_id sgm4154x_i2c_ids[] = {
	{ "sgm41542", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sgm4154x_i2c_ids);

static const struct of_device_id sgm4154x_of_match[] = {
	{ .compatible = "sgm,sgm41542", },
	{ },
};
MODULE_DEVICE_TABLE(of, sgm4154x_of_match);

static struct i2c_driver sgm4154x_driver = {
	.driver = {
		.name = "sgm4154x-charger",
		.of_match_table = sgm4154x_of_match,
	},
	.probe = sgm4154x_probe,
	.remove = sgm4154x_charger_remove,
	.shutdown = sgm4154x_charger_shutdown,
	.id_table = sgm4154x_i2c_ids,
};
module_i2c_driver(sgm4154x_driver);

MODULE_AUTHOR("Xu Shengfei <xsf@rock-chips.com>");
MODULE_DESCRIPTION("sgm4154x charger driver");
MODULE_LICENSE("GPL");
