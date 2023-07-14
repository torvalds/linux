// SPDX-License-Identifier: GPL-2.0
// BQ2515X Battery Charger Driver
// Copyright (C) 2020 Texas Instruments Incorporated - https://www.ti.com/

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define BQ2515X_MANUFACTURER "Texas Instruments"

#define BQ2515X_STAT0		0x00
#define BQ2515X_STAT1		0x01
#define BQ2515X_STAT2		0x02
#define BQ2515X_FLAG0		0x03
#define BQ2515X_FLAG1		0x04
#define BQ2515X_FLAG2		0x05
#define BQ2515X_FLAG3		0x06
#define BQ2515X_MASK0		0x07
#define BQ2515X_MASK1		0x08
#define BQ2515X_MASK2		0x09
#define BQ2515X_MASK3		0x0a
#define BQ2515X_VBAT_CTRL	0x12
#define BQ2515X_ICHG_CTRL	0x13
#define BQ2515X_PCHRGCTRL	0x14
#define BQ2515X_TERMCTRL	0x15
#define BQ2515X_BUVLO		0x16
#define BQ2515X_CHARGERCTRL0	0x17
#define BQ2515X_CHARGERCTRL1	0x18
#define BQ2515X_ILIMCTRL	0x19
#define BQ2515X_LDOCTRL		0x1d
#define BQ2515X_MRCTRL		0x30
#define BQ2515X_ICCTRL0		0x35
#define BQ2515X_ICCTRL1		0x36
#define BQ2515X_ICCTRL2		0x37
#define BQ2515X_ADCCTRL0	0x40
#define BQ2515X_ADCCTRL1	0x41
#define BQ2515X_ADC_VBAT_M	0x42
#define BQ2515X_ADC_VBAT_L	0x43
#define BQ2515X_ADC_TS_M	0x44
#define BQ2515X_ADC_TS_L	0x45
#define BQ2515X_ADC_ICHG_M	0x46
#define BQ2515X_ADC_ICHG_L	0x47
#define BQ2515X_ADC_ADCIN_M	0x48
#define BQ2515X_ADC_ADCIN_L	0x49
#define BQ2515X_ADC_VIN_M	0x4a
#define BQ2515X_ADC_VIN_L	0x4b
#define BQ2515X_ADC_PMID_M	0x4c
#define BQ2515X_ADC_PMID_L	0x4d
#define BQ2515X_ADC_IIN_M	0x4e
#define BQ2515X_ADC_IIN_L	0x4f
#define BQ2515X_ADC_COMP1_M	0x52
#define BQ2515X_ADC_COMP1_L	0X53
#define BQ2515X_ADC_COMP2_M	0X54
#define BQ2515X_ADC_COMP2_L	0x55
#define BQ2515X_ADC_COMP3_M	0x56
#define BQ2515X_ADC_COMP3_L	0x57
#define BQ2515X_ADC_READ_EN	0x58
#define BQ2515X_TS_FASTCHGCTRL	0x61
#define BQ2515X_TS_COLD		0x62
#define BQ2515X_TS_COOL		0x63
#define BQ2515X_TS_WARM		0x64
#define BQ2515X_TS_HOT		0x65
#define BQ2515X_DEVICE_ID	0x6f

#define BQ2515X_DEFAULT_ICHG_UA		10000
#define BQ25150_DEFAULT_ILIM_UA		100000
#define BQ25155_DEFAULT_ILIM_UA		500000
#define BQ2515X_DEFAULT_VBAT_REG_UV	4200000
#define BQ2515X_DEFAULT_IPRECHARGE_UA	2500

#define BQ2515X_DIVISOR				65536
#define BQ2515X_VBAT_BASE_VOLT			3600000
#define BQ2515X_VBAT_REG_MAX			4600000
#define BQ2515X_VBAT_REG_MIN			3600000
#define BQ2515X_VBAT_STEP_UV			10000
#define BQ2515X_UV_FACTOR			1000000
#define BQ2515X_VBAT_MULTIPLIER			6
#define BQ2515X_ICHG_DIVISOR			52429
#define BQ2515X_ICHG_CURR_STEP_THRESH_UA	318750
#define BQ2515X_ICHG_MIN_UA			0
#define BQ2515X_ICHG_MAX_UA			500000
#define BQ2515X_ICHG_RNG_1B0_UA			1250
#define BQ2515X_ICHG_RNG_1B1_UA			2500
#define BQ2515X_VLOWV_SEL_1B0_UV		3000000
#define BQ2515X_VLOWV_SEL_1B1_UV		2800000
#define BQ2515X_PRECHRG_ICHRG_RNGE_1875_UA	18750
#define BQ2515X_PRECHRG_ICHRG_RNGE_3750_UA	37500
#define BQ2515X_TWAKE2_MIN_US			1700000
#define BQ2515X_TWAKE2_MAX_US			2300000

#define BQ2515X_ILIM_150MA	0x2
#define BQ2515X_ILIM_MASK	0x7
#define BQ2515X_ILIM_MIN	50000
#define BQ2515X_ILIM_MAX	600000
#define BQ2515X_HEALTH_MASK	0xf
#define BQ2515X_ICHGRNG_MASK	0x80
#define BQ2515X_STAT0_MASK	0x0f
#define BQ2515X_STAT1_MASK	0x1f
#define BQ2515X_PRECHARGE_MASK	0x1f

#define BQ2515X_TS_HOT_STAT		BIT(0)
#define BQ2515X_TS_WARM_STAT		BIT(1)
#define BQ2515X_TS_COOL_STAT		BIT(2)
#define BQ2515X_TS_COLD_STAT		BIT(3)
#define BQ2515X_SAFETY_TIMER_EXP	BIT(5)

#define BQ2515X_EN_VBAT_READ		BIT(3)
#define BQ2515X_EN_ICHG_READ		BIT(5)

#define BQ2515X_VIN_GOOD		BIT(0)
#define BQ2515X_CHRG_DONE		BIT(5)
#define BQ2515X_CV_CHRG_MODE		BIT(6)

#define BQ2515X_VIN_OVP_FAULT_STAT	BIT(7)

#define BQ2515X_WATCHDOG_DISABLE	BIT(4)

#define BQ2515X_ICHARGE_RANGE		BIT(7)

#define BQ2515X_VLOWV_SEL		BIT(5)

#define BQ2515X_CHARGER_DISABLE		BIT(0)

#define BQ2515X_HWRESET_14S_WD		BIT(1)

static const int bq2515x_ilim_lvl_values[] = {
	50000, 100000, 150000, 200000, 300000, 400000, 500000, 600000
};

/**
 * struct bq2515x_init_data -
 * @ilim: input current limit
 * @ichg: fast charge current
 * @vbatreg: battery regulation voltage
 * @iprechg: precharge current
 */
struct bq2515x_init_data {
	int ilim;
	int ichg;
	int vbatreg;
	int iprechg;
};

enum bq2515x_id {
	BQ25150,
	BQ25155,
};

/**
 * struct bq2515x_device -
 * @mains: mains properties
 * @battery: battery properties
 * @regmap: register map structure
 * @dev: device structure
 *
 * @reset_gpio: manual reset (MR) pin
 * @powerdown_gpio: low power mode pin
 * @ac_detect_gpio: power good (PG) pin
 * @ce_gpio: charge enable (CE) pin
 *
 * @model_name: string value describing device model
 * @device_id: value of device_id
 * @mains_online: boolean value indicating power supply online
 *
 * @init_data: charger initialization data structure
 */
struct bq2515x_device {
	struct power_supply *mains;
	struct power_supply *battery;
	struct regmap *regmap;
	struct device *dev;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *powerdown_gpio;
	struct gpio_desc *ac_detect_gpio;
	struct gpio_desc *ce_gpio;

	char model_name[I2C_NAME_SIZE];
	int device_id;
	bool mains_online;

	struct bq2515x_init_data init_data;
};

static const struct reg_default bq25150_reg_defaults[] = {
	{BQ2515X_FLAG0, 0x0},
	{BQ2515X_FLAG1, 0x0},
	{BQ2515X_FLAG2, 0x0},
	{BQ2515X_FLAG3, 0x0},
	{BQ2515X_MASK0, 0x0},
	{BQ2515X_MASK1, 0x0},
	{BQ2515X_MASK2, 0x71},
	{BQ2515X_MASK3, 0x0},
	{BQ2515X_VBAT_CTRL, 0x3C},
	{BQ2515X_ICHG_CTRL, 0x8},
	{BQ2515X_PCHRGCTRL, 0x2},
	{BQ2515X_TERMCTRL, 0x14},
	{BQ2515X_BUVLO, 0x0},
	{BQ2515X_CHARGERCTRL0, 0x82},
	{BQ2515X_CHARGERCTRL1, 0x42},
	{BQ2515X_ILIMCTRL, 0x1},
	{BQ2515X_LDOCTRL, 0xB0},
	{BQ2515X_MRCTRL, 0x2A},
	{BQ2515X_ICCTRL0, 0x10},
	{BQ2515X_ICCTRL1, 0x0},
	{BQ2515X_ICCTRL2, 0x0},
	{BQ2515X_ADCCTRL0, 0x2},
	{BQ2515X_ADCCTRL1, 0x40},
	{BQ2515X_ADC_COMP1_M, 0x23},
	{BQ2515X_ADC_COMP1_L, 0x20},
	{BQ2515X_ADC_COMP2_M, 0x38},
	{BQ2515X_ADC_COMP2_L, 0x90},
	{BQ2515X_ADC_COMP3_M, 0x0},
	{BQ2515X_ADC_COMP3_L, 0x0},
	{BQ2515X_ADC_READ_EN, 0x0},
	{BQ2515X_TS_FASTCHGCTRL, 0x34},
	{BQ2515X_TS_COLD, 0x7C},
	{BQ2515X_TS_COOL, 0x6D},
	{BQ2515X_TS_WARM, 0x38},
	{BQ2515X_TS_HOT, 0x27},
	{BQ2515X_DEVICE_ID, 0x20},
};

static const struct reg_default bq25155_reg_defaults[] = {
	{BQ2515X_FLAG0, 0x0},
	{BQ2515X_FLAG1, 0x0},
	{BQ2515X_FLAG2, 0x0},
	{BQ2515X_FLAG3, 0x0},
	{BQ2515X_MASK0, 0x0},
	{BQ2515X_MASK1, 0x0},
	{BQ2515X_MASK2, 0x71},
	{BQ2515X_MASK3, 0x0},
	{BQ2515X_VBAT_CTRL, 0x3C},
	{BQ2515X_ICHG_CTRL, 0x8},
	{BQ2515X_PCHRGCTRL, 0x2},
	{BQ2515X_TERMCTRL, 0x14},
	{BQ2515X_BUVLO, 0x0},
	{BQ2515X_CHARGERCTRL0, 0x82},
	{BQ2515X_CHARGERCTRL1, 0xC2},
	{BQ2515X_ILIMCTRL, 0x6},
	{BQ2515X_LDOCTRL, 0xB0},
	{BQ2515X_MRCTRL, 0x2A},
	{BQ2515X_ICCTRL0, 0x10},
	{BQ2515X_ICCTRL1, 0x0},
	{BQ2515X_ICCTRL2, 0x40},
	{BQ2515X_ADCCTRL0, 0x2},
	{BQ2515X_ADCCTRL1, 0x40},
	{BQ2515X_ADC_COMP1_M, 0x23},
	{BQ2515X_ADC_COMP1_L, 0x20},
	{BQ2515X_ADC_COMP2_M, 0x38},
	{BQ2515X_ADC_COMP2_L, 0x90},
	{BQ2515X_ADC_COMP3_M, 0x0},
	{BQ2515X_ADC_COMP3_L, 0x0},
	{BQ2515X_ADC_READ_EN, 0x0},
	{BQ2515X_TS_FASTCHGCTRL, 0x34},
	{BQ2515X_TS_COLD, 0x7C},
	{BQ2515X_TS_COOL, 0x6D},
	{BQ2515X_TS_WARM, 0x38},
	{BQ2515X_TS_HOT, 0x27},
	{BQ2515X_DEVICE_ID, 0x35},
};

static int bq2515x_wake_up(struct bq2515x_device *bq2515x)
{
	int ret;
	int val;

	/* Read the STAT register if we can read it then the device is out
	 * of ship mode.  If the register cannot be read then attempt to wake
	 * it up and enable the ADC.
	 */
	ret = regmap_read(bq2515x->regmap, BQ2515X_STAT0, &val);
	if (ret)
		return ret;

	/* Need to toggle LP and bring device out of ship mode. The device
	 * will exit the ship mode when the MR pin is held low for at least
	 * t_WAKE2 as shown in section 8.3.7.1 of the datasheet.
	 */
	gpiod_set_value_cansleep(bq2515x->powerdown_gpio, 0);

	gpiod_set_value_cansleep(bq2515x->reset_gpio, 0);
	usleep_range(BQ2515X_TWAKE2_MIN_US, BQ2515X_TWAKE2_MAX_US);
	gpiod_set_value_cansleep(bq2515x->reset_gpio, 1);

	return regmap_write(bq2515x->regmap, BQ2515X_ADC_READ_EN,
				(BQ2515X_EN_VBAT_READ | BQ2515X_EN_ICHG_READ));
}

static int bq2515x_update_ps_status(struct bq2515x_device *bq2515x)
{
	bool dc = false;
	unsigned int val;
	int ret;

	if (bq2515x->ac_detect_gpio)
		val = gpiod_get_value_cansleep(bq2515x->ac_detect_gpio);
	else {
		ret = regmap_read(bq2515x->regmap, BQ2515X_STAT0, &val);
		if (ret)
			return ret;
	}

	dc = val & BQ2515X_VIN_GOOD;

	ret = bq2515x->mains_online != dc;

	bq2515x->mains_online = dc;

	return ret;
}

static int bq2515x_disable_watchdog_timers(struct bq2515x_device *bq2515x)
{
	int ret;

	ret = regmap_update_bits(bq2515x->regmap, BQ2515X_CHARGERCTRL0,
			BQ2515X_WATCHDOG_DISABLE, BQ2515X_WATCHDOG_DISABLE);
	if (ret)
		return ret;

	return regmap_update_bits(bq2515x->regmap, BQ2515X_ICCTRL2,
						BQ2515X_HWRESET_14S_WD, 0);
}

static int bq2515x_get_battery_voltage_now(struct bq2515x_device *bq2515x)
{
	int ret;
	int vbat_msb;
	int vbat_lsb;
	uint32_t vbat_measurement;

	if (!bq2515x->mains_online)
		bq2515x_wake_up(bq2515x);

	ret = regmap_read(bq2515x->regmap, BQ2515X_ADC_VBAT_M, &vbat_msb);
	if (ret)
		return ret;

	ret = regmap_read(bq2515x->regmap, BQ2515X_ADC_VBAT_L, &vbat_lsb);
	if (ret)
		return ret;

	vbat_measurement = (vbat_msb << 8) | vbat_lsb;

	return vbat_measurement * (BQ2515X_UV_FACTOR / BQ2515X_DIVISOR) *
						BQ2515X_VBAT_MULTIPLIER;
}

static int bq2515x_get_battery_current_now(struct bq2515x_device *bq2515x)
{
	int ret;
	int ichg_msb;
	int ichg_lsb;
	uint32_t ichg_measurement;
	u16 ichg_multiplier = BQ2515X_ICHG_RNG_1B0_UA;
	unsigned int ichg_reg_code, reg_code;
	unsigned int icharge_range = 0, pchrgctrl;
	unsigned int buvlo, vlowv_sel, vlowv = BQ2515X_VLOWV_SEL_1B0_UV;

	if (!bq2515x->mains_online)
		return -ENODATA;

	ret = regmap_read(bq2515x->regmap, BQ2515X_ADC_ICHG_M, &ichg_msb);
	if (ret)
		return ret;

	ret = regmap_read(bq2515x->regmap, BQ2515X_ADC_ICHG_L, &ichg_lsb);
	if (ret)
		return ret;

	ichg_measurement = (ichg_msb << 8) | ichg_lsb;

	ret = regmap_read(bq2515x->regmap, BQ2515X_BUVLO, &buvlo);
	if (ret)
		return ret;

	vlowv_sel = buvlo & BQ2515X_VLOWV_SEL;

	if (vlowv_sel)
		vlowv = BQ2515X_VLOWV_SEL_1B1_UV;

	if (bq2515x_get_battery_voltage_now(bq2515x) < vlowv) {
		ret = regmap_read(bq2515x->regmap, BQ2515X_PCHRGCTRL,
								&pchrgctrl);
		if (ret)
			return ret;

		reg_code = pchrgctrl & BQ2515X_PRECHARGE_MASK;
	} else {
		ret = regmap_read(bq2515x->regmap, BQ2515X_ICHG_CTRL,
							&ichg_reg_code);
		if (ret)
			return ret;

		reg_code = ichg_reg_code;
	}

	ret = regmap_read(bq2515x->regmap, BQ2515X_PCHRGCTRL, &pchrgctrl);
	if (ret)
		return ret;

	icharge_range = pchrgctrl & BQ2515X_ICHARGE_RANGE;

	if (icharge_range)
		ichg_multiplier = BQ2515X_ICHG_RNG_1B1_UA;

	return reg_code * (ichg_multiplier * ichg_measurement /
							BQ2515X_ICHG_DIVISOR);
}

static bool bq2515x_get_charge_disable(struct bq2515x_device *bq2515x)
{
	int ret;
	int ce_pin;
	int icctrl2;
	int charger_disable;

	ce_pin = gpiod_get_value_cansleep(bq2515x->ce_gpio);

	ret = regmap_read(bq2515x->regmap, BQ2515X_ICCTRL2, &icctrl2);
	if (ret)
		return ret;

	charger_disable = icctrl2 & BQ2515X_CHARGER_DISABLE;

	if (charger_disable || ce_pin)
		return true;

	return false;
}

static int bq2515x_set_charge_disable(struct bq2515x_device *bq2515x, int val)
{
	gpiod_set_value_cansleep(bq2515x->ce_gpio, val);

	return regmap_update_bits(bq2515x->regmap, BQ2515X_ICCTRL2,
					BQ2515X_CHARGER_DISABLE, val);
}

static int bq2515x_get_const_charge_current(struct bq2515x_device *bq2515x)
{
	int ret;
	u16 ichg_multiplier = BQ2515X_ICHG_RNG_1B0_UA;
	unsigned int ichg_reg_code;
	unsigned int pchrgctrl;
	unsigned int icharge_range;

	ret = regmap_read(bq2515x->regmap, BQ2515X_ICHG_CTRL, &ichg_reg_code);
	if (ret)
		return ret;

	ret = regmap_read(bq2515x->regmap, BQ2515X_PCHRGCTRL, &pchrgctrl);
	if (ret)
		return ret;

	icharge_range = pchrgctrl & BQ2515X_ICHARGE_RANGE;

	if (icharge_range)
		ichg_multiplier = BQ2515X_ICHG_RNG_1B1_UA;

	return ichg_reg_code * ichg_multiplier;
}

static int bq2515x_set_const_charge_current(struct bq2515x_device *bq2515x,
								int val)
{
	int ret;
	unsigned int ichg_reg_code;
	u16 ichg_multiplier = BQ2515X_ICHG_RNG_1B0_UA;
	unsigned int icharge_range = 0;

	if (val > BQ2515X_ICHG_MAX_UA || val < BQ2515X_ICHG_MIN_UA)
		return -EINVAL;

	if (val > BQ2515X_ICHG_CURR_STEP_THRESH_UA) {
		ichg_multiplier = BQ2515X_ICHG_RNG_1B1_UA;
		icharge_range = BQ2515X_ICHARGE_RANGE;
	}

	bq2515x_set_charge_disable(bq2515x, 1);

	ret = regmap_update_bits(bq2515x->regmap, BQ2515X_PCHRGCTRL,
					BQ2515X_ICHARGE_RANGE, icharge_range);
	if (ret)
		return ret;

	ichg_reg_code = val / ichg_multiplier;

	ret = regmap_write(bq2515x->regmap, BQ2515X_ICHG_CTRL, ichg_reg_code);
	if (ret)
		return ret;

	return bq2515x_set_charge_disable(bq2515x, 0);
}

static int bq2515x_get_precharge_current(struct bq2515x_device *bq2515x)
{
	int ret;
	unsigned int pchrgctrl;
	unsigned int icharge_range;
	u16 precharge_multiplier = BQ2515X_ICHG_RNG_1B0_UA;
	unsigned int precharge_reg_code;

	ret = regmap_read(bq2515x->regmap, BQ2515X_PCHRGCTRL, &pchrgctrl);
	if (ret)
		return ret;

	icharge_range = pchrgctrl & BQ2515X_ICHARGE_RANGE;

	if (icharge_range)
		precharge_multiplier = BQ2515X_ICHG_RNG_1B1_UA;

	precharge_reg_code = pchrgctrl & BQ2515X_PRECHARGE_MASK;

	return precharge_reg_code * precharge_multiplier;
}

static int bq2515x_set_precharge_current(struct bq2515x_device *bq2515x,
					int val)
{
	int ret;
	unsigned int pchrgctrl;
	unsigned int icharge_range;
	unsigned int precharge_reg_code;
	unsigned int precharge_multiplier = BQ2515X_ICHG_RNG_1B0_UA;
	unsigned int precharge_max_ua = BQ2515X_PRECHRG_ICHRG_RNGE_1875_UA;

	ret = regmap_read(bq2515x->regmap, BQ2515X_PCHRGCTRL, &pchrgctrl);
	if (ret)
		return ret;

	icharge_range = pchrgctrl & BQ2515X_ICHARGE_RANGE;

	if (icharge_range) {
		precharge_max_ua = BQ2515X_PRECHRG_ICHRG_RNGE_3750_UA;
		precharge_multiplier = BQ2515X_ICHG_RNG_1B1_UA;
	} else {
		precharge_max_ua = BQ2515X_PRECHRG_ICHRG_RNGE_1875_UA;
		precharge_multiplier = BQ2515X_ICHG_RNG_1B0_UA;
	}
	if (val > precharge_max_ua || val < BQ2515X_ICHG_MIN_UA)
		return -EINVAL;

	precharge_reg_code = val / precharge_multiplier;

	ret = bq2515x_set_charge_disable(bq2515x, 1);
	if (ret)
		return ret;

	ret = regmap_update_bits(bq2515x->regmap, BQ2515X_PCHRGCTRL,
				BQ2515X_PRECHARGE_MASK, precharge_reg_code);
	if (ret)
		return ret;

	return bq2515x_set_charge_disable(bq2515x, 0);
}

static int bq2515x_charging_status(struct bq2515x_device *bq2515x,
				   union power_supply_propval *val)
{
	bool status0_no_fault;
	bool status1_no_fault;
	bool ce_status;
	bool charge_done;
	unsigned int status;
	int ret;

	if (!bq2515x->mains_online) {
		val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		return 0;
	}

	ret = regmap_read(bq2515x->regmap, BQ2515X_STAT0, &status);
	if (ret)
		return ret;

	/*
	 * The code block below is used to determine if any faults from the
	 * STAT0 register are disbaling charging or if the charge has completed
	 * according to the CHARGE_DONE_STAT bit.
	 */
	if (((status & BQ2515X_STAT0_MASK) == true) &
			((status & BQ2515X_CHRG_DONE) == false)) {
		status0_no_fault = true;
		charge_done = false;
	} else if (status & BQ2515X_CHRG_DONE) {
		charge_done = true;
		status0_no_fault = false;
	} else {
		status0_no_fault = false;
		charge_done = false;
	}

	ret = regmap_read(bq2515x->regmap, BQ2515X_STAT1, &status);
	if (ret)
		return ret;
	/*
	 * The code block below is used to determine if any faults from the
	 * STAT1 register are disbaling charging
	 */
	if ((status & BQ2515X_STAT1_MASK) == false)
		status1_no_fault = true;
	else
		status1_no_fault = false;

	ce_status = (!bq2515x_get_charge_disable(bq2515x));

	/*
	 * If there are no faults and charging is enabled, then status is
	 * charging. Otherwise, if charging is complete, then status is full.
	 * Otherwise, if a fault exists or charging is disabled, then status is
	 * not charging
	 */
	if (status0_no_fault & status1_no_fault & ce_status)
		val->intval = POWER_SUPPLY_STATUS_CHARGING;
	else if (charge_done)
		val->intval = POWER_SUPPLY_STATUS_FULL;
	else if (!(status0_no_fault & status1_no_fault & ce_status))
		val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;

	return 0;
}

static int bq2515x_get_batt_reg(struct bq2515x_device *bq2515x)
{
	int vbat_reg_code;
	int ret;

	ret = regmap_read(bq2515x->regmap, BQ2515X_VBAT_CTRL, &vbat_reg_code);
	if (ret)
		return ret;

	return BQ2515X_VBAT_BASE_VOLT + vbat_reg_code * BQ2515X_VBAT_STEP_UV;
}

static int bq2515x_set_batt_reg(struct bq2515x_device *bq2515x, int val)
{
	int vbat_reg_code;

	if (val > BQ2515X_VBAT_REG_MAX || val < BQ2515X_VBAT_REG_MIN)
		return -EINVAL;

	vbat_reg_code = (val - BQ2515X_VBAT_BASE_VOLT) / BQ2515X_VBAT_STEP_UV;

	return regmap_write(bq2515x->regmap, BQ2515X_VBAT_CTRL, vbat_reg_code);
}

static int bq2515x_get_ilim_lvl(struct bq2515x_device *bq2515x)
{
	int ret;
	int ilimctrl;

	ret = regmap_read(bq2515x->regmap, BQ2515X_ILIMCTRL, &ilimctrl);
	if (ret)
		return ret;

	return bq2515x_ilim_lvl_values[ilimctrl & BQ2515X_ILIM_MASK];
}

static int bq2515x_set_ilim_lvl(struct bq2515x_device *bq2515x, int val)
{
	int i = 0;
	unsigned int array_size = ARRAY_SIZE(bq2515x_ilim_lvl_values);

	for (i = array_size - 1; i > 0; i--) {
		if (val >= bq2515x_ilim_lvl_values[i])
			break;
	}
	return regmap_write(bq2515x->regmap, BQ2515X_ILIMCTRL, i);
}

static int bq2515x_power_supply_property_is_writeable(struct power_supply *psy,
					enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		return true;
	default:
		return false;
	}
}

static int bq2515x_charger_get_health(struct bq2515x_device *bq2515x,
				      union power_supply_propval *val)
{
	int health = POWER_SUPPLY_HEALTH_GOOD;
	int ret;
	unsigned int stat1;
	unsigned int flag3;

	if (!bq2515x->mains_online)
		bq2515x_wake_up(bq2515x);

	ret = regmap_read(bq2515x->regmap, BQ2515X_FLAG3, &flag3);
	if (ret)
		return ret;

	ret = regmap_read(bq2515x->regmap, BQ2515X_STAT1, &stat1);
	if (ret)
		return ret;

	if (stat1 & BQ2515X_HEALTH_MASK) {
		switch (stat1 & BQ2515X_HEALTH_MASK) {
		case BQ2515X_TS_HOT_STAT:
			health = POWER_SUPPLY_HEALTH_HOT;
			break;
		case BQ2515X_TS_WARM_STAT:
			health = POWER_SUPPLY_HEALTH_WARM;
			break;
		case BQ2515X_TS_COOL_STAT:
			health = POWER_SUPPLY_HEALTH_COOL;
			break;
		case BQ2515X_TS_COLD_STAT:
			health = POWER_SUPPLY_HEALTH_COLD;
			break;
		default:
			health = POWER_SUPPLY_HEALTH_UNKNOWN;
			break;
		}
	}

	if (stat1 & BQ2515X_VIN_OVP_FAULT_STAT)
		health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;

	if (flag3 & BQ2515X_SAFETY_TIMER_EXP)
		health = POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE;

	val->intval = health;
	return 0;
}

static int bq2515x_mains_set_property(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *val)
{
	struct bq2515x_device *bq2515x = power_supply_get_drvdata(psy);
	int ret;

	switch (prop) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = bq2515x_set_batt_reg(bq2515x, val->intval);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = bq2515x_set_const_charge_current(bq2515x, val->intval);
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = bq2515x_set_ilim_lvl(bq2515x, val->intval);
		break;

	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		ret = bq2515x_set_precharge_current(bq2515x, val->intval);
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static int bq2515x_mains_get_property(struct power_supply *psy,
				     enum power_supply_property prop,
				     union power_supply_propval *val)
{
	struct bq2515x_device *bq2515x = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (prop) {

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = bq2515x_get_const_charge_current(bq2515x);
		if (ret < 0)
			return ret;

		val->intval = ret;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = bq2515x_get_batt_reg(bq2515x);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;

	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		ret = bq2515x_get_precharge_current(bq2515x);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = bq2515x->mains_online;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		ret = bq2515x_charger_get_health(bq2515x, val);
		if (ret)
			val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = bq2515x_get_ilim_lvl(bq2515x);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = bq2515x->model_name;
		break;

	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = BQ2515X_MANUFACTURER;
		break;

	case POWER_SUPPLY_PROP_STATUS:
		ret = bq2515x_charging_status(bq2515x, val);
		if (ret)
			return ret;
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static int bq2515x_battery_get_property(struct power_supply *psy,
				       enum power_supply_property prop,
				       union power_supply_propval *val)
{
	struct bq2515x_device *bq2515x = power_supply_get_drvdata(psy);
	int ret;

	ret = bq2515x_update_ps_status(bq2515x);
	if (ret)
		return ret;

	switch (prop) {

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		ret = bq2515x->init_data.vbatreg;
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		ret = bq2515x->init_data.ichg;
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = bq2515x_get_battery_voltage_now(bq2515x);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = bq2515x_get_battery_current_now(bq2515x);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static const enum power_supply_property bq2515x_battery_properties[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
};

static const enum power_supply_property bq2515x_mains_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
};

static const struct power_supply_desc bq2515x_mains_desc = {
	.name			= "bq2515x-mains",
	.type			= POWER_SUPPLY_TYPE_MAINS,
	.get_property		= bq2515x_mains_get_property,
	.set_property		= bq2515x_mains_set_property,
	.properties		= bq2515x_mains_properties,
	.num_properties		= ARRAY_SIZE(bq2515x_mains_properties),
	.property_is_writeable	= bq2515x_power_supply_property_is_writeable,
};

static const struct power_supply_desc bq2515x_battery_desc = {
	.name			= "bq2515x-battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.get_property		= bq2515x_battery_get_property,
	.properties		= bq2515x_battery_properties,
	.num_properties		= ARRAY_SIZE(bq2515x_battery_properties),
	.property_is_writeable	= bq2515x_power_supply_property_is_writeable,
};

static int bq2515x_power_supply_register(struct bq2515x_device *bq2515x,
		struct device *dev, struct power_supply_config psy_cfg)
{
	bq2515x->mains = devm_power_supply_register(bq2515x->dev,
						    &bq2515x_mains_desc,
						    &psy_cfg);
	if (IS_ERR(bq2515x->mains))
		return -EINVAL;

	bq2515x->battery = devm_power_supply_register(bq2515x->dev,
						      &bq2515x_battery_desc,
						      &psy_cfg);
	if (IS_ERR(bq2515x->battery))
		return -EINVAL;

	return 0;
}

static int bq2515x_hw_init(struct bq2515x_device *bq2515x)
{
	int ret;
	struct power_supply_battery_info *bat_info;

	ret = bq2515x_disable_watchdog_timers(bq2515x);
	if (ret)
		return ret;

	if (bq2515x->init_data.ilim) {
		ret = bq2515x_set_ilim_lvl(bq2515x, bq2515x->init_data.ilim);
		if (ret)
			return ret;
	}

	ret = power_supply_get_battery_info(bq2515x->mains, &bat_info);
	if (ret) {
		dev_warn(bq2515x->dev, "battery info missing, default values will be applied\n");

		bq2515x->init_data.ichg = BQ2515X_DEFAULT_ICHG_UA;

		bq2515x->init_data.vbatreg = BQ2515X_DEFAULT_VBAT_REG_UV;

		bq2515x->init_data.iprechg = BQ2515X_DEFAULT_IPRECHARGE_UA;

	} else {
		bq2515x->init_data.ichg =
				bat_info->constant_charge_current_max_ua;

		bq2515x->init_data.vbatreg =
				bat_info->constant_charge_voltage_max_uv;

		bq2515x->init_data.iprechg =
				bat_info->precharge_current_ua;
	}

	ret = bq2515x_set_const_charge_current(bq2515x,
						bq2515x->init_data.ichg);
	if (ret)
		return ret;

	ret = bq2515x_set_batt_reg(bq2515x, bq2515x->init_data.vbatreg);
	if (ret)
		return ret;

	return bq2515x_set_precharge_current(bq2515x,
						bq2515x->init_data.iprechg);
}

static int bq2515x_read_properties(struct bq2515x_device *bq2515x)
{
	int ret;

	ret = device_property_read_u32(bq2515x->dev,
				      "input-current-limit-microamp",
				      &bq2515x->init_data.ilim);
	if (ret) {
		switch (bq2515x->device_id) {
		case BQ25150:
			bq2515x->init_data.ilim = BQ25150_DEFAULT_ILIM_UA;
			break;
		case BQ25155:
			bq2515x->init_data.ilim = BQ25155_DEFAULT_ILIM_UA;
			break;
		}
	}

	bq2515x->ac_detect_gpio = devm_gpiod_get_optional(bq2515x->dev,
						   "ac-detect", GPIOD_IN);
	if (IS_ERR(bq2515x->ac_detect_gpio)) {
		ret = PTR_ERR(bq2515x->ac_detect_gpio);
		dev_err(bq2515x->dev, "Failed to get ac detect");
		return ret;
	}

	bq2515x->reset_gpio = devm_gpiod_get_optional(bq2515x->dev,
						   "reset", GPIOD_OUT_LOW);
	if (IS_ERR(bq2515x->reset_gpio)) {
		ret = PTR_ERR(bq2515x->reset_gpio);
		dev_err(bq2515x->dev, "Failed to get reset");
		return ret;
	}

	bq2515x->powerdown_gpio = devm_gpiod_get_optional(bq2515x->dev,
						"powerdown", GPIOD_OUT_LOW);
	if (IS_ERR(bq2515x->powerdown_gpio)) {
		ret = PTR_ERR(bq2515x->powerdown_gpio);
		dev_err(bq2515x->dev, "Failed to get powerdown");
		return ret;
	}

	bq2515x->ce_gpio = devm_gpiod_get_optional(bq2515x->dev,
						   "charge-enable",
						   GPIOD_OUT_LOW);
	if (IS_ERR(bq2515x->ce_gpio)) {
		ret = PTR_ERR(bq2515x->ce_gpio);
		dev_err(bq2515x->dev, "Failed to get ce");
		return ret;
	}

	return 0;
}

static bool bq2515x_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BQ2515X_STAT0 ... BQ2515X_FLAG3:
	case BQ2515X_ADC_VBAT_M ... BQ2515X_ADC_IIN_L:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config bq25150_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register		= BQ2515X_DEVICE_ID,
	.reg_defaults		= bq25150_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(bq25150_reg_defaults),
	.cache_type		= REGCACHE_RBTREE,
	.volatile_reg		= bq2515x_volatile_register,
};

static const struct regmap_config bq25155_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register		= BQ2515X_DEVICE_ID,
	.reg_defaults		= bq25155_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(bq25155_reg_defaults),
	.cache_type		= REGCACHE_RBTREE,
	.volatile_reg		= bq2515x_volatile_register,
};

static int bq2515x_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct device *dev = &client->dev;
	struct bq2515x_device *bq2515x;
	struct power_supply_config charger_cfg = {};
	int ret;

	bq2515x = devm_kzalloc(dev, sizeof(*bq2515x), GFP_KERNEL);
	if (!bq2515x)
		return -ENOMEM;

	bq2515x->dev = dev;

	strncpy(bq2515x->model_name, id->name, I2C_NAME_SIZE);

	bq2515x->device_id = id->driver_data;

	switch (bq2515x->device_id) {
	case BQ25150:
		bq2515x->regmap = devm_regmap_init_i2c(client,
						&bq25150_regmap_config);
		break;
	case BQ25155:
		bq2515x->regmap = devm_regmap_init_i2c(client,
						&bq25155_regmap_config);
		break;
	}

	if (IS_ERR(bq2515x->regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(bq2515x->regmap);
	}

	i2c_set_clientdata(client, bq2515x);

	charger_cfg.drv_data = bq2515x;
	charger_cfg.of_node = dev->of_node;

	ret = bq2515x_read_properties(bq2515x);
	if (ret) {
		dev_err(dev, "Failed to read device tree properties %d\n",
									ret);
		return ret;
	}

	ret = bq2515x_power_supply_register(bq2515x, dev, charger_cfg);
	if (ret) {
		dev_err(dev, "failed to register power supply\n");
		return ret;
	}

	ret = bq2515x_hw_init(bq2515x);
	if (ret) {
		dev_err(dev, "Cannot initialize the chip\n");
		return ret;
	}

	return 0;
}

static const struct i2c_device_id bq2515x_i2c_ids[] = {
	{ "bq25150", BQ25150, },
	{ "bq25155", BQ25155, },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq2515x_i2c_ids);

static const struct of_device_id bq2515x_of_match[] = {
	{ .compatible = "ti,bq25150", },
	{ .compatible = "ti,bq25155", },
	{ },
};
MODULE_DEVICE_TABLE(of, bq2515x_of_match);

static struct i2c_driver bq2515x_driver = {
	.driver = {
		.name = "bq2515x-charger",
		.of_match_table = bq2515x_of_match,
	},
	.probe = bq2515x_probe,
	.id_table = bq2515x_i2c_ids,
};
module_i2c_driver(bq2515x_driver);

MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_AUTHOR("Ricardo Rivera-Matos <r-rivera-matos@ti.com>");
MODULE_DESCRIPTION("BQ2515X charger driver");
MODULE_LICENSE("GPL v2");
