// SPDX-License-Identifier: GPL-2.0
/*
 * Charger driver for SGM4154x
 *
 * Copyright (c) 2026 Rockchip Electronics Co., Ltd.
 *
 * Author: Xu Shengfei <xsf@rock-chips.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/devm-helpers.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/types.h>

#define SGM4154X_MANUFACTURER			"SGMICRO"
#define SGM4154X_NAME				"sgm41542"

#define SGM4154X_CHRG_CTRL_0			0x00
#define SGM4154X_HIZ_EN				BIT(7)
#define SGM4154X_IINDPM_I_MASK			GENMASK(4, 0)
#define SGM4154X_IINDPM_I_MIN_UA		100000
#define SGM4154X_IINDPM_I_MAX_UA		3800000
#define SGM4154X_IINDPM_STEP_UA			100000
#define SGM4154X_IINDPM_DEF_UA			2400000

#define SGM4154X_CHRG_CTRL_1			0x01
#define SGM4154X_WDT_RST			BIT(6)
#define SGM4154X_OTG_EN				BIT(5)
#define SGM4154X_CHRG_EN			BIT(4)

#define SGM4154X_CHRG_CTRL_2			0x02
#define SGM4154X_BOOST_LIM			BIT(7)
#define SGM4154X_ICHRG_CUR_MASK			GENMASK(5, 0)
#define SGM4154X_ICHRG_I_STEP_UA		60000
#define SGM4154X_ICHRG_I_MIN_UA			0
#define SGM4154X_ICHRG_I_MAX_UA			3780000
#define SGM4154X_ICHRG_I_DEF_UA			2040000

#define SGM4154X_CHRG_CTRL_3			0x03
#define SGM4154X_PRECHRG_CUR_MASK		GENMASK(7, 4)
#define SGM4154X_PRECHRG_CURRENT_STEP_UA	60000
#define SGM4154X_PRECHRG_I_MIN_UA		60000
#define SGM4154X_PRECHRG_I_MAX_UA		780000
#define SGM4154X_PRECHRG_I_DEF_UA		180000
#define SGM4154X_TERMCHRG_CUR_MASK		GENMASK(3, 0)
#define SGM4154X_TERMCHRG_CURRENT_STEP_UA	60000
#define SGM4154X_TERMCHRG_I_MIN_UA		60000
#define SGM4154X_TERMCHRG_I_MAX_UA		960000
#define SGM4154X_TERMCHRG_I_DEF_UA		180000

#define SGM4154X_CHRG_CTRL_4			0x04
#define SGM4154X_VREG_V_MASK			GENMASK(7, 3)
#define SGM4154X_VREG_V_MAX_UV			4624000
#define SGM4154X_VREG_V_MIN_UV			3856000
#define SGM4154X_VREG_V_DEF_UV			4208000
#define SGM4154X_VREG_V_STEP_UV			32000
#define SGM4154X_VRECHARGE			BIT(0)
#define SGM4154X_VRECHRG_STEP_UV		100000
#define SGM4154X_VRECHRG_OFFSET_UV		100000

#define SGM4154X_CHRG_CTRL_5			0x05
#define SGM4154X_TERM_EN			BIT(7)
#define SGM4154X_WDT_TIMER_MASK			GENMASK(5, 4)

#define SGM4154X_CHRG_CTRL_6			0x06
#define SGM4154X_VAC_OVP_MASK			GENMASK(7, 6)
#define SGM4154X_OVP_14V			(BIT(7) | BIT(6))
#define SGM4154X_OVP_10_5V			BIT(7)
#define SGM4154X_OVP_6_5V			BIT(6)
#define SGM4154X_OVP_5_5V			0
#define SGM4154X_OVP_DEFAULT			SGM4154X_OVP_14V
#define SGM4154X_BOOSTV				GENMASK(5, 4)
#define SGM4154X_VINDPM_V_MASK			GENMASK(3, 0)
#define SGM4154X_VINDPM_V_MIN_UV		3900000
#define SGM4154X_VINDPM_V_MAX_UV		12000000
#define SGM4154X_VINDPM_STEP_UV			100000
#define SGM4154X_VINDPM_DEF_UV			4500000

#define SGM4154X_CHRG_CTRL_7			0x07

#define SGM4154X_CHRG_STAT		0x08
#define SGM4154X_VBUS_STAT_MASK		GENMASK(7, 5)
#define SGM4154X_OTG_MODE		(BIT(7) | BIT(6) | BIT(5))
#define SGM4154X_NON_STANDARD		(BIT(7) | BIT(6))
#define SGM4154X_UNKNOWN		(BIT(7) | BIT(5))
#define SGM4154X_USB_DCP		(BIT(6) | BIT(5))
#define SGM4154X_USB_CDP		BIT(6)
#define SGM4154X_USB_SDP		BIT(5)
#define SGM4154X_NOT_CHRGING		0
#define SGM4154X_CHG_STAT_MASK		GENMASK(4, 3)
#define SGM4154X_TERM_CHRG		(BIT(4) | BIT(3))
#define SGM4154X_FAST_CHRG		BIT(4)
#define SGM4154X_PRECHRG		BIT(3)
#define SGM4154X_PG_STAT		BIT(2)
#define SGM4154X_THERM_STAT		BIT(1)
#define SGM4154X_VSYS_STAT		BIT(0)

#define SGM4154X_CHRG_FAULT		0x09
#define SGM4154X_TEMP_MASK		GENMASK(2, 0)
#define SGM4154X_TEMP_HOT		(BIT(2) | BIT(1))
#define SGM4154X_TEMP_COLD		(BIT(2) | BIT(0))
#define SGM4154X_TEMP_COOL		(BIT(1) | BIT(0))
#define SGM4154X_TEMP_WARM		BIT(1)
#define SGM4154X_TEMP_NORMAL		BIT(0)

#define SGM4154X_CHRG_CTRL_A		0x0a
#define SGM4154X_VBUS_GOOD		BIT(7)
#define SGM4154X_VINDPM_INT_MASK	BIT(1)
#define SGM4154X_IINDPM_INT_MASK	BIT(0)

#define SGM4154X_CHRG_CTRL_B		0x0b
#define SGM4154X_PN_ID			(BIT(6) | BIT(5) | BIT(3))
#define SGM4154X_PN_MASK		GENMASK(6, 3)

#define SGM4154X_CHRG_CTRL_C		0x0c

#define SGM4154X_CHRG_CTRL_D		0x0d
#define SGM4154X_JEITA_EN		BIT(0)

#define SGM4154X_INPUT_DET		0x0e
#define SGM4154X_DPDM_ONGOING		BIT(7)

#define SGM4154X_CHRG_CTRL_F		0x0f
#define SGM4154X_VINDPM_OS_MASK	 GENMASK(1, 0)

#define SGM4154X_DEFAULT_INPUT_CUR	(500 * 1000)

struct sgm4154x_init_data {
	int ilim;	/* input current limit */
	int vlim;	/* minimum system voltage limit */
	int iterm;	/* termination current */
	int iprechg;	/* precharge current */
	int max_ichg;	/* maximum charge current */
	int max_vreg;	/* maximum charge voltage */
};

struct sgm4154x_state {
	bool vsys_stat;
	bool therm_stat;
	bool online;
	u8 chrg_stat;
	bool chrg_en;
	bool vbus_gd;
	u8 chrg_type;
	u8 health;
	u8 chrg_fault;
	u8 ntc_fault;
};

struct sgm4154x_device {
	struct device *dev;
	struct power_supply *charger;
	struct regmap *regmap;
	struct sgm4154x_init_data init_data;
	struct sgm4154x_state state;
	struct regulator_dev *otg_rdev;
	struct mutex lock;
	bool watchdog_enable;
	struct workqueue_struct *sgm_monitor_wq;
	struct delayed_work sgm_delay_work;
};

enum SGM4154X_VINDPM_OS {
	VINDPM_OS_3900MV,
	VINDPM_OS_5900MV,
	VINDPM_OS_7500MV,
	VINDPM_OS_10500MV,
};

enum sgm4154x_wdt_values {
	SGM4154X_WDT_TIMER_DISABLE = 0,
	SGM4154X_WDT_TIMER_40S = 1,
	SGM4154X_WDT_TIMER_80S = 2,
	SGM4154X_WDT_TIMER_160S = 3,
};

static int sgm4154x_set_term_curr(struct sgm4154x_device *sgm, int cur_ua)
{
	int reg_val;
	int ret;

	cur_ua = clamp(cur_ua, SGM4154X_TERMCHRG_I_MIN_UA, SGM4154X_TERMCHRG_I_MAX_UA);
	reg_val = (cur_ua - SGM4154X_TERMCHRG_I_MIN_UA) / SGM4154X_TERMCHRG_CURRENT_STEP_UA;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_3,
				 SGM4154X_TERMCHRG_CUR_MASK,
				 reg_val);
	if (ret)
		dev_err(sgm->dev, "set term current error!\n");

	return ret;
}

static int sgm4154x_set_prechrg_curr(struct sgm4154x_device *sgm, int cur_ua)
{
	int reg_val;
	int ret;

	cur_ua = clamp(cur_ua, SGM4154X_PRECHRG_I_MIN_UA, SGM4154X_PRECHRG_I_MAX_UA);
	reg_val = (cur_ua - SGM4154X_PRECHRG_I_MIN_UA) / SGM4154X_PRECHRG_CURRENT_STEP_UA;

	reg_val = FIELD_PREP(SGM4154X_PRECHRG_CUR_MASK, reg_val);
	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_3,
				 SGM4154X_PRECHRG_CUR_MASK,
				 reg_val);
	if (ret)
		dev_err(sgm->dev, "set precharge current error!\n");

	return ret;
}

static int sgm4154x_set_ichrg_curr(struct sgm4154x_device *sgm, int cur_ua)
{
	int reg_val;
	int ret;

	cur_ua = clamp(cur_ua, SGM4154X_ICHRG_I_MIN_UA, sgm->init_data.max_ichg);
	reg_val = cur_ua / SGM4154X_ICHRG_I_STEP_UA;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_2,
				 SGM4154X_ICHRG_CUR_MASK,
				 reg_val);
	if (ret)
		dev_err(sgm->dev, "set icharge current error!\n");

	return ret;
}

static int sgm4154x_get_ichrg_curr(struct sgm4154x_device *sgm)
{
	u32 reg;
	int ret, val;

	ret = regmap_read(sgm->regmap, SGM4154X_CHRG_CTRL_2, &reg);
	if (ret) {
		dev_err(sgm->dev, "get charge current error!\n");
		return ret;
	}

	val = FIELD_GET(SGM4154X_ICHRG_CUR_MASK, reg);

	return val * SGM4154X_ICHRG_I_STEP_UA;
}

static int sgm4154x_set_chrg_volt(struct sgm4154x_device *sgm, int chrg_volt)
{
	int reg_val;
	int ret;

	/*
	 * Note that the value of 0x01111 represents a "special value"
	 * corresponding to 4352000uV instead of the expected 4336000uV,
	 * per the datasheet. All other values are as expected. So not
	 * only do we need to clamp between max and min values, but
	 * also clamp anything below 4352000uv to 4304000uv to prevent
	 * overcharging.
	 */
	chrg_volt = clamp(chrg_volt, SGM4154X_VREG_V_MIN_UV, sgm->init_data.max_vreg);
	if (chrg_volt < 4352000)
		chrg_volt = clamp(chrg_volt, SGM4154X_VREG_V_MIN_UV, 4304000);
	reg_val = (chrg_volt - SGM4154X_VREG_V_MIN_UV) / SGM4154X_VREG_V_STEP_UV;
	reg_val = FIELD_PREP(SGM4154X_VREG_V_MASK, reg_val);
	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_4,
				 SGM4154X_VREG_V_MASK,
				 reg_val);
	if (ret)
		dev_err(sgm->dev, "set charge voltage error!\n");

	return ret;
}

static int sgm4154x_get_chrg_volt(struct sgm4154x_device *sgm)
{
	u32 reg;
	int ret, val;

	ret = regmap_read(sgm->regmap, SGM4154X_CHRG_CTRL_4, &reg);
	if (ret) {
		dev_err(sgm->dev, "get charge voltage error!\n");
		return ret;
	}

	val = FIELD_GET(SGM4154X_VREG_V_MASK, reg);

	/*
	 * 0x01111 is a special value meaning 4352000uV, all other
	 * values are as expected based on the offset and step values.
	 */
	if (val == 0x0f)
		return 4352000;

	return val * SGM4154X_VREG_V_STEP_UV + SGM4154X_VREG_V_MIN_UV;
}

static int sgm4154x_set_input_volt_lim(struct sgm4154x_device *sgm,
				       unsigned int vindpm)
{
	enum SGM4154X_VINDPM_OS os_val;
	unsigned int offset;
	u8 reg_val;
	int ret;


	if (vindpm < SGM4154X_VINDPM_V_MIN_UV ||
	    vindpm > SGM4154X_VINDPM_V_MAX_UV) {
		dev_err(sgm->dev, "input voltage limit %u outside range\n", vindpm);
		return -EINVAL;
	}

	/*
	 * Supported ranges per the datasheet are as follows:
	 * 3.9v - 5.4v with a 3.9v offset
	 * 5.9v - 7.4v with a 5.9v offset
	 * 7.5v - 9.0v with a 7.5v offset
	 * 10.5v - 12.0v with a 10.5v offset
	 * Step size is a constant 100mv
	 */
	if (vindpm < 5900000) {
		offset = 3900000;
		vindpm = clamp(vindpm, offset, 5400000);
		os_val = VINDPM_OS_3900MV;
	} else if (vindpm < 7500000) {
		offset = 5900000;
		vindpm = clamp(vindpm, offset, 7400000);
		os_val = VINDPM_OS_5900MV;
	} else if (vindpm < 10500000) {
		offset = 7500000;
		vindpm = clamp(vindpm, offset, 9000000);
		os_val = VINDPM_OS_7500MV;
	} else {
		offset = 10500000;
		vindpm = clamp(vindpm, offset, 12000000);
		os_val = VINDPM_OS_10500MV;
	}

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_F,
				 SGM4154X_VINDPM_OS_MASK,
				 os_val);
	if (ret) {
		dev_err(sgm->dev, "set vin dpm error!\n");
		return ret;
	}

	reg_val = (vindpm - offset) / SGM4154X_VINDPM_STEP_UV;

	ret = regmap_update_bits(sgm->regmap, SGM4154X_CHRG_CTRL_6,
				 SGM4154X_VINDPM_V_MASK, reg_val);
	if (ret)
		dev_err(sgm->dev, "input voltage error!\n");

	return ret;
}

static int sgm4154x_set_input_curr_lim(struct sgm4154x_device *sgm, int iindpm)
{
	int reg_val;
	int ret;

	if (iindpm < SGM4154X_IINDPM_I_MIN_UA)
		return -EINVAL;

	/*
	 * Protect against a timing-issue edge case if a sysfs write occurs in the middle
	 * of a probe (very unlikely).
	 */
	if (sgm->init_data.ilim < SGM4154X_IINDPM_I_MIN_UA)
		return -EINVAL;

	/*
	 * Per the datasheet, values between 100000uA and 3100000uA work
	 * as expected with the register defined as having a step of
	 * 100000 and a min/max of 100000 (0x00) through 3100000 (0x1e).
	 * The register value of 0x1f however corresponds to 3800000uA not
	 * 3200000uA as one would expect.
	 */
	if ((iindpm > SGM4154X_IINDPM_I_MAX_UA) || (iindpm > sgm->init_data.ilim))
		iindpm = min(SGM4154X_IINDPM_I_MAX_UA, sgm->init_data.ilim);

	if (iindpm > 3100000 && iindpm < SGM4154X_IINDPM_I_MAX_UA)
		iindpm = 3100000;

	if (iindpm == SGM4154X_IINDPM_I_MAX_UA)
		reg_val = 0x1f;
	else
		reg_val = (iindpm - SGM4154X_IINDPM_I_MIN_UA) / SGM4154X_IINDPM_STEP_UA;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_0,
				 SGM4154X_IINDPM_I_MASK,
				 reg_val);
	if (ret)
		dev_err(sgm->dev, "set input current limit error!\n");

	return ret;
}

static int sgm4154x_get_input_curr_lim(struct sgm4154x_device *sgm)
{
	int ret;
	int ilim;

	ret = regmap_read(sgm->regmap, SGM4154X_CHRG_CTRL_0, &ilim);
	if (ret) {
		dev_err(sgm->dev, "get input current limit error!\n");
		return ret;
	}

	ilim &= SGM4154X_IINDPM_I_MASK;

	/* Max value is not 3200000uA as expected but is 3800000uA */
	if (ilim == SGM4154X_IINDPM_I_MASK)
		return SGM4154X_IINDPM_I_MAX_UA;

	ilim = ilim * SGM4154X_IINDPM_STEP_UA + SGM4154X_IINDPM_I_MIN_UA;

	return ilim;
}

static int sgm4154x_watchdog_timer_reset(struct sgm4154x_device *sgm)
{
	int ret;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_1,
				 SGM4154X_WDT_RST,
				 SGM4154X_WDT_RST);

	if (ret)
		dev_err(sgm->dev, "set watchdog timer error!\n");

	return ret;
}

static int sgm4154x_set_watchdog_timer(struct sgm4154x_device *sgm, u8 time)
{
	int ret;

	if (time > SGM4154X_WDT_TIMER_160S)
		return -EINVAL;

	time = FIELD_PREP(SGM4154X_WDT_TIMER_MASK, time);
	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_5,
				 SGM4154X_WDT_TIMER_MASK,
				 time);

	if (ret) {
		dev_err(sgm->dev, "set watchdog timer error!\n");
		return ret;
	}

	if (time) {
		if (!sgm->watchdog_enable)
			queue_delayed_work(sgm->sgm_monitor_wq,
					   &sgm->sgm_delay_work,
					   msecs_to_jiffies(1000 * 5));
		sgm->watchdog_enable = true;
	} else {
		sgm->watchdog_enable = false;
		sgm4154x_watchdog_timer_reset(sgm);
	}

	return ret;
}

static int sgm4154x_enable_charger(struct sgm4154x_device *sgm)
{
	int ret;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_1,
				 SGM4154X_CHRG_EN,
				 SGM4154X_CHRG_EN);
	if (ret)
		dev_err(sgm->dev, "enable charger error!\n");

	return ret;
}

static int sgm4154x_disable_charger(struct sgm4154x_device *sgm)
{
	int ret;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_1,
				 SGM4154X_CHRG_EN,
				 0);
	if (ret)
		dev_err(sgm->dev, "disable charger error!\n");

	return ret;
}

static int sgm4154x_set_vac_ovp(struct sgm4154x_device *sgm)
{
	int reg_val;
	int ret;

	reg_val = SGM4154X_OVP_DEFAULT & SGM4154X_VAC_OVP_MASK;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_6,
				 SGM4154X_VAC_OVP_MASK,
				 reg_val);
	if (ret)
		dev_err(sgm->dev, "set vac ovp error!\n");

	return ret;
}

static int sgm4154x_set_recharge_volt_ua(struct sgm4154x_device *sgm, int recharge_volt)
{
	int reg_val;
	int ret;

	reg_val = (recharge_volt - SGM4154X_VRECHRG_OFFSET_UV) / SGM4154X_VRECHRG_STEP_UV;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_4,
				 SGM4154X_VRECHARGE,
				 reg_val);
	if (ret)
		dev_err(sgm->dev, "set recharger error!\n");

	return ret;
}

static int sgm4154x_get_state(struct sgm4154x_device *sgm,
			      struct sgm4154x_state *state)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(sgm->regmap, SGM4154X_CHRG_STAT, &reg);
	if (ret) {
		dev_err(sgm->dev, "read SGM4154X_CHRG_STAT fail\n");
		return ret;
	}
	state->chrg_type = reg & SGM4154X_VBUS_STAT_MASK;
	state->chrg_stat = reg & SGM4154X_CHG_STAT_MASK;
	state->online = !!(reg & SGM4154X_PG_STAT);
	state->therm_stat = !!(reg & SGM4154X_THERM_STAT);
	state->vsys_stat = !!(reg & SGM4154X_VSYS_STAT);

	ret = regmap_read(sgm->regmap, SGM4154X_CHRG_FAULT, &reg);
	if (ret) {
		dev_err(sgm->dev, "read SGM4154X_CHRG_FAULT fail\n");
		return ret;
	}
	state->chrg_fault = reg;
	state->ntc_fault = reg & SGM4154X_TEMP_MASK;
	state->health = state->ntc_fault;

	ret = regmap_read(sgm->regmap, SGM4154X_CHRG_CTRL_A, &reg);
	if (ret) {
		dev_err(sgm->dev, "read SGM4154X_CHRG_CTRL_A fail\n");
		return ret;
	}
	state->vbus_gd = !!(reg & SGM4154X_VBUS_GOOD);

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

	guard(mutex)(&sgm->lock);

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (val->intval) {
			ret = sgm4154x_enable_charger(sgm);
			sgm4154x_set_watchdog_timer(sgm, SGM4154X_WDT_TIMER_40S);
		} else {
			sgm4154x_set_watchdog_timer(sgm, SGM4154X_WDT_TIMER_DISABLE);
			ret = sgm4154x_disable_charger(sgm);
		}
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = sgm4154x_set_input_curr_lim(sgm, val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = sgm4154x_set_ichrg_curr(sgm, val->intval);
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
	int ret;

	scoped_guard(mutex, &sgm->lock) {
		ret = sgm4154x_get_state(sgm, &state);
		if (ret) {
			dev_err(sgm->dev, "get state error!\n");
			return ret;
		}
		sgm->state = state;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (!state.chrg_type || (state.chrg_type == SGM4154X_OTG_MODE))
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (!state.chrg_stat)
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else if (state.chrg_stat == SGM4154X_TERM_CHRG)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		switch (state.chrg_stat) {
		case SGM4154X_PRECHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case SGM4154X_FAST_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
			break;
		case SGM4154X_TERM_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case SGM4154X_NOT_CHRGING:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
			break;
		default:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		}
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = SGM4154X_MANUFACTURER;
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = SGM4154X_NAME;
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = state.online;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = state.vbus_gd;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = sgm4154x_get_chrg_volt(sgm);
		if (val->intval < 0)
			return -EINVAL;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		val->intval = sgm4154x_get_ichrg_curr(sgm);
		if (val->intval < 0)
			return -EINVAL;
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		val->intval = sgm->init_data.vlim;
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

static irqreturn_t sgm4154x_irq_handler_thread(int irq, void *private)
{
	struct sgm4154x_device *sgm = private;
	struct sgm4154x_state oldstate, state;
	int ret;

	guard(mutex)(&sgm->lock);

	oldstate = sgm->state;

	ret = sgm4154x_get_state(sgm, &state);
	if (ret) {
		dev_err(sgm->dev, "get state error!\n");
		return IRQ_NONE;
	}

	sgm->state = state;

	if (state.vbus_gd && !oldstate.vbus_gd) {
		if (sgm->init_data.ilim >= SGM4154X_DEFAULT_INPUT_CUR) {
			ret = sgm4154x_set_input_curr_lim(sgm, sgm->init_data.ilim);
			if (ret) {
				dev_err(sgm->dev, "set input current error!\n");
				/*
				 * Reading IRQ clears interrupt, so return handled
				 * even if error.
				 */
			}
		}
	}

	power_supply_changed(sgm->charger);

	return IRQ_HANDLED;
}

static int sgm4154x_hw_init(struct power_supply *psy)
{
	struct sgm4154x_device *sgm = power_supply_get_drvdata(psy);
	struct power_supply_battery_info *bat_info;
	int ret;
	u32 val;

	/*
	 * If unable to read devicetree info, use default/reset
	 * values from hardware. If the devicetree info has data out
	 * of range for any of the values, use the default value.
	 */
	sgm->init_data.iprechg = SGM4154X_PRECHRG_I_DEF_UA;
	sgm->init_data.iterm = SGM4154X_TERMCHRG_I_DEF_UA;
	sgm->init_data.max_ichg = SGM4154X_ICHRG_I_DEF_UA;
	sgm->init_data.max_vreg = SGM4154X_VREG_V_DEF_UV;
	sgm->init_data.vlim = SGM4154X_VINDPM_DEF_UV;
	sgm->init_data.ilim = SGM4154X_IINDPM_DEF_UA;

	ret = power_supply_get_battery_info(psy, &bat_info);
	if (ret)
		dev_warn(sgm->dev, "sgm4154x: cannot read battery info\n");
	else {
		if ((bat_info->constant_charge_current_max_ua >= SGM4154X_ICHRG_I_MIN_UA) &&
			(bat_info->constant_charge_current_max_ua <= SGM4154X_ICHRG_I_MAX_UA))
			sgm->init_data.max_ichg = bat_info->constant_charge_current_max_ua;
		if ((bat_info->constant_charge_voltage_max_uv >= SGM4154X_VREG_V_MIN_UV) &&
			(bat_info->constant_charge_voltage_max_uv <= SGM4154X_VREG_V_MAX_UV))
			sgm->init_data.max_vreg = bat_info->constant_charge_voltage_max_uv;
		if ((bat_info->charge_term_current_ua >= SGM4154X_TERMCHRG_I_MIN_UA) &&
			(bat_info->charge_term_current_ua <= SGM4154X_TERMCHRG_I_MAX_UA))
			sgm->init_data.iterm = bat_info->charge_term_current_ua;
		if ((bat_info->precharge_current_ua >= SGM4154X_PRECHRG_I_MIN_UA) &&
			(bat_info->precharge_current_ua <= SGM4154X_PRECHRG_I_MAX_UA))
			sgm->init_data.iprechg = bat_info->precharge_current_ua;

		power_supply_put_battery_info(psy, bat_info);
	}

	ret = device_property_read_u32(sgm->dev,
				       "input-voltage-limit-microvolt",
				       &val);
	if (!ret)
		sgm->init_data.vlim = clamp(val, SGM4154X_VINDPM_V_MIN_UV,
					    SGM4154X_VINDPM_V_MAX_UV);

	ret = device_property_read_u32(sgm->dev,
				       "input-current-limit-microamp",
				       &val);
	if (!ret)
		sgm->init_data.ilim = clamp(val, SGM4154X_IINDPM_I_MIN_UA,
					    SGM4154X_IINDPM_I_MAX_UA);

	ret = sgm4154x_set_watchdog_timer(sgm, SGM4154X_WDT_TIMER_DISABLE);
	if (ret)
		return ret;

	ret = sgm4154x_set_prechrg_curr(sgm, sgm->init_data.iprechg);
	if (ret)
		return ret;

	ret = sgm4154x_set_chrg_volt(sgm, sgm->init_data.max_vreg);
	if (ret)
		return ret;

	ret = sgm4154x_set_term_curr(sgm, sgm->init_data.iterm);
	if (ret)
		return ret;

	ret = sgm4154x_set_ichrg_curr(sgm, sgm->init_data.max_ichg);
	if (ret)
		return ret;

	ret = sgm4154x_set_input_volt_lim(sgm, sgm->init_data.vlim);
	if (ret)
		return ret;

	ret = sgm4154x_set_input_curr_lim(sgm, sgm->init_data.ilim);
	if (ret)
		return ret;

	ret = sgm4154x_set_vac_ovp(sgm);
	if (ret)
		return ret;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_D,
				 SGM4154X_JEITA_EN,
				 0);
	if (ret)
		return ret;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_A,
				 SGM4154X_IINDPM_INT_MASK,
				 SGM4154X_IINDPM_INT_MASK);
	if (ret)
		return ret;

	ret = regmap_update_bits(sgm->regmap,
				 SGM4154X_CHRG_CTRL_A,
				 SGM4154X_VINDPM_INT_MASK,
				 SGM4154X_VINDPM_INT_MASK);
	if (ret)
		return ret;

	/*
	 * Recharge microvolt set to 200000 by BSP driver instead of hardware
	 * default value of 100000.
	 */
	ret = sgm4154x_set_recharge_volt_ua(sgm, 200000);

	return ret;
}

static enum power_supply_property sgm4154x_power_supply_props[] = {
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

static struct power_supply_desc sgm4154x_power_supply_desc = {
	.name = "sgm4154x-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.init = sgm4154x_hw_init,
	.properties = sgm4154x_power_supply_props,
	.num_properties = ARRAY_SIZE(sgm4154x_power_supply_props),
	.get_property = sgm4154x_charger_get_property,
	.set_property = sgm4154x_charger_set_property,
	.property_is_writeable = sgm4154x_property_is_writeable,
};

static const struct regmap_config sgm4154x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = SGM4154X_CHRG_CTRL_F,
	.cache_type = REGCACHE_NONE,
};

static const u32 sgm4154x_chg_otg_cur_ua[] = {
	1200000, 2000000,
};

static const struct regulator_ops sgm4154x_vbus_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_current_limit = regulator_set_current_limit_regmap,
	.get_current_limit = regulator_get_current_limit_regmap,
};

static const struct regulator_desc sgm4154x_otg_rdesc = {
	.of_match = "otg-vbus",
	.name = "otg-vbus",
	.regulators_node = of_match_ptr("regulators"),
	.ops = &sgm4154x_vbus_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.min_uV = 4850000,
	.uV_step = 150000,
	.n_voltages = 4,
	.vsel_reg = SGM4154X_CHRG_CTRL_6,
	.vsel_mask = SGM4154X_BOOSTV,
	.enable_reg = SGM4154X_CHRG_CTRL_1,
	.enable_mask = SGM4154X_OTG_EN,
	.curr_table = sgm4154x_chg_otg_cur_ua,
	.n_current_limits = ARRAY_SIZE(sgm4154x_chg_otg_cur_ua),
	.csel_reg = SGM4154X_CHRG_CTRL_2,
	.csel_mask = SGM4154X_BOOST_LIM,
};

static int sgm4154x_vbus_regulator_register(struct sgm4154x_device *sgm)
{
	struct regulator_config config = {
		.dev = sgm->dev,
		.regmap = sgm->regmap,
		.driver_data = sgm,
	};

	sgm->otg_rdev = devm_regulator_register(sgm->dev,
						&sgm4154x_otg_rdesc,
						&config);

	return PTR_ERR_OR_ZERO(sgm->otg_rdev);
}

static int sgm4154x_hw_chipid_detect(struct sgm4154x_device *sgm)
{
	int ret;
	int val;

	ret = regmap_read(sgm->regmap, SGM4154X_CHRG_CTRL_B, &val);
	if (ret)
		return ret;

	if ((val & SGM4154X_PN_MASK) != SGM4154X_PN_ID)
		dev_warn(sgm->dev, "sgm4154x device ID mismatch\n");

	return 0;
}

static void sgm_charger_work(struct work_struct *work)
{
	struct sgm4154x_device *sgm =
		container_of(work,
			     struct sgm4154x_device,
			     sgm_delay_work.work);

	sgm4154x_watchdog_timer_reset(sgm);
	if (sgm->watchdog_enable)
		queue_delayed_work(sgm->sgm_monitor_wq,
				   &sgm->sgm_delay_work,
				   msecs_to_jiffies(1000 * 5));
}

static void sgm4154x_disable_irq_wake(void *data)
{
	struct sgm4154x_device *sgm = data;
	struct i2c_client *client = to_i2c_client(sgm->dev);

	disable_irq_wake(client->irq);
}

static int sgm4154x_probe(struct i2c_client *client)
{
	struct power_supply_config psy_cfg = {};
	struct device *dev = &client->dev;
	struct sgm4154x_device *sgm;
	int ret;

	sgm = devm_kzalloc(dev, sizeof(*sgm), GFP_KERNEL);
	if (!sgm)
		return -ENOMEM;

	sgm->dev = dev;

	sgm->regmap = devm_regmap_init_i2c(client, &sgm4154x_regmap_config);
	if (IS_ERR(sgm->regmap))
		return dev_err_probe(dev, PTR_ERR(sgm->regmap),
				     "Failed to allocate register map\n");

	i2c_set_clientdata(client, sgm);

	ret = devm_mutex_init(dev, &sgm->lock);
	if (ret)
		return ret;

	ret = sgm4154x_hw_chipid_detect(sgm);
	if (ret)
		return dev_err_probe(dev, ret, "Unable to read HW ID\n");

	devm_device_init_wakeup(dev);

	sgm->sgm_monitor_wq = devm_alloc_ordered_workqueue(dev, "sgm-monitor-wq",
			WQ_MEM_RECLAIM | WQ_FREEZABLE);
	if (!sgm->sgm_monitor_wq)
		return -EINVAL;

	ret = devm_delayed_work_autocancel(dev, &sgm->sgm_delay_work,
					   sgm_charger_work);
	if (ret)
		return dev_err_probe(dev, ret, "Unable to register delayed work\n");

	psy_cfg.drv_data = sgm;
	psy_cfg.fwnode = dev_fwnode(dev);

	sgm->charger = devm_power_supply_register(sgm->dev,
						  &sgm4154x_power_supply_desc,
						  &psy_cfg);
	if (IS_ERR(sgm->charger))
		return dev_err_probe(dev, PTR_ERR(sgm->charger),
				     "Failed to register power supply\n");

	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						sgm4154x_irq_handler_thread,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"sgm41542-irq", sgm);
		if (ret)
			return ret;

		ret = enable_irq_wake(client->irq);
		if (!ret) {
			ret = devm_add_action_or_reset(dev, sgm4154x_disable_irq_wake, sgm);
			if (ret)
				return ret;
		}
	}

	ret = sgm4154x_vbus_regulator_register(sgm);
	if (ret) {
		return dev_err_probe(dev, ret,
				     "Unable to register VBUS regulator\n");
	}

	return 0;
}

static int __maybe_unused sgm4154x_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sgm4154x_device *sgm = i2c_get_clientdata(client);
	bool watchdog = sgm->watchdog_enable;
	int ret;

	/*
	 * Disable watchdog during suspend and stop delayed work. When
	 * delayed work is restarted after resume watchdog will be
	 * re-enabled if it was previously enabled. Retain the current
	 * state of the watchdog timer though, so it can be re-enabled
	 * if necessary by the work queue once resume is triggered.
	 */
	ret = sgm4154x_set_watchdog_timer(sgm, SGM4154X_WDT_TIMER_DISABLE);
	if (ret)
		return ret;
	cancel_delayed_work_sync(&sgm->sgm_delay_work);
	sgm->watchdog_enable = watchdog;
	return 0;
}

static int __maybe_unused sgm4154x_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sgm4154x_device *sgm = i2c_get_clientdata(client);
	int ret;

	if (sgm->watchdog_enable) {
		ret = sgm4154x_set_watchdog_timer(sgm, SGM4154X_WDT_TIMER_40S);
		if (ret)
			return ret;
	}

	queue_delayed_work(sgm->sgm_monitor_wq,
			   &sgm->sgm_delay_work, 0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(sgm4154x_pm_ops, sgm4154x_suspend, sgm4154x_resume);

static const struct i2c_device_id sgm4154x_i2c_ids[] = {
	{ "sgm41542" },
	{ },
};
MODULE_DEVICE_TABLE(i2c, sgm4154x_i2c_ids);

static const struct of_device_id sgm4154x_of_match[] = {
	{ .compatible = "sgmicro,sgm41542", },
	{ },
};
MODULE_DEVICE_TABLE(of, sgm4154x_of_match);

static struct i2c_driver sgm4154x_driver = {
	.driver = {
		.name = "sgm4154x-charger",
		.of_match_table = sgm4154x_of_match,
		.pm = &sgm4154x_pm_ops,
	},
	.probe = sgm4154x_probe,
	.id_table = sgm4154x_i2c_ids,
};

module_i2c_driver(sgm4154x_driver);

MODULE_AUTHOR("Xu Shengfei <xsf@rock-chips.com>");
MODULE_DESCRIPTION("sgm4154x charger driver");
MODULE_LICENSE("GPL");
