// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices LTC4283 I2C Negative Voltage Hot Swap Controller (HWMON)
 *
 * Copyright 2025 Analog Devices Inc.
 */
#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bits.h>

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/device/devres.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/math.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/module.h>

#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/unaligned.h>
#include <linux/units.h>

#define LTC4283_SYSTEM_STATUS		0x00
#define LTC4283_FAULT_STATUS		0x03
#define   LTC4283_OV_MASK		BIT(0)
#define   LTC4283_UV_MASK		BIT(1)
#define   LTC4283_OC_MASK		BIT(2)
#define   LTC4283_FET_BAD_MASK		BIT(3)
#define   LTC4283_FET_SHORT_MASK	BIT(6)
#define LTC4283_FAULT_LOG		0x04
#define   LTC4283_OV_FAULT_MASK		BIT(0)
#define   LTC4283_UV_FAULT_MASK		BIT(1)
#define   LTC4283_OC_FAULT_MASK		BIT(2)
#define   LTC4283_FET_BAD_FAULT_MASK	BIT(3)
#define   LTC4283_PGI_FAULT_MASK	BIT(4)
#define   LTC4283_PWR_FAIL_FAULT_MASK	BIT(5)
#define   LTC4283_FET_SHORT_FAULT_MASK	BIT(6)
#define LTC4283_ADC_ALM_LOG_1		0x05
#define   LTC4283_POWER_LOW_ALM		BIT(0)
#define   LTC4283_POWER_HIGH_ALM	BIT(1)
#define   LTC4283_SENSE_LOW_ALM		BIT(4)
#define   LTC4283_SENSE_HIGH_ALM	BIT(5)
#define LTC4283_ADC_ALM_LOG_2		0x06
#define LTC4283_ADC_ALM_LOG_3		0x07
#define LTC4283_ADC_ALM_LOG_4		0x08
#define LTC4283_ADC_ALM_LOG_5		0x09
#define LTC4283_CONTROL_1		0x0a
#define   LTC4283_RW_PAGE_MASK		BIT(0)
#define   LTC4283_PIGIO2_ACLB_MASK	BIT(2)
#define   LTC4283_PWRGD_RST_CTRL_MASK	BIT(3)
#define   LTC4283_FET_BAD_OFF_MASK	BIT(4)
#define   LTC4283_THERM_TMR_MASK	BIT(5)
#define   LTC4283_DVDT_MASK		BIT(6)
#define LTC4283_CONTROL_2		0x0b
#define   LTC4283_OV_RETRY_MASK		BIT(0)
#define   LTC4283_UV_RETRY_MASK		BIT(1)
#define   LTC4283_OC_RETRY_MASK		GENMASK(3, 2)
#define   LTC4283_FET_BAD_RETRY_MASK	GENMASK(5, 4)
#define   LTC4283_EXT_FAULT_RETRY_MASK	BIT(7)
#define LTC4283_RESERVED_OC		0x0c
#define LTC4283_CONFIG_1		0x0d
#define   LTC4283_FB_MASK		GENMASK(3, 2)
#define   LTC4283_ILIM_MASK		GENMASK(7, 4)
#define LTC4283_CONFIG_2		0x0e
#define   LTC4283_COOLING_DL_MASK	GENMASK(3, 1)
#define   LTC4283_FTBD_DL_MASK		GENMASK(5, 4)
#define LTC4283_CONFIG_3		0x0f
#define   LTC4283_VPWR_DRNS_MASK	BIT(6)
#define   LTC4283_EXTFLT_TURN_OFF_MASK	BIT(7)
#define LTC4283_PGIO_CONFIG		0x10
#define   LTC4283_PGIO1_CFG_MASK	GENMASK(1, 0)
#define   LTC4283_PGIO2_CFG_MASK	GENMASK(3, 2)
#define   LTC4283_PGIO3_CFG_MASK	GENMASK(5, 4)
#define   LTC4283_PGIO4_CFG_MASK	GENMASK(7, 6)
#define LTC4283_PGIO_CONFIG_2		0x11
#define   LTC4283_ADC_MASK		GENMASK(2, 0)
#define LTC4283_ADC_SELECT(c)		(0x13 + (c) / 8)
#define   LTC4283_ADC_SELECT_MASK(c)	BIT((c) % 8)
#define LTC4283_SENSE_MIN_TH		0x1b
#define LTC4283_SENSE_MAX_TH		0x1c
#define LTC4283_VPWR_MIN_TH		0x1d
#define LTC4283_VPWR_MAX_TH		0x1e
#define LTC4283_POWER_MIN_TH		0x1f
#define LTC4283_POWER_MAX_TH		0x20
#define LTC4283_ADC_2_MIN_TH(c)		(0x21 + (c) * 2)
#define LTC4283_ADC_2_MAX_TH(c)		(0x22 + (c) * 2)
#define LTC4283_ADC_2_MIN_TH_DIFF(c)	(0x39 + (c) * 2)
#define LTC4283_ADC_2_MAX_TH_DIFF(c)	(0x3a + (c) * 2)
#define LTC4283_SENSE			0x41
#define LTC4283_SENSE_MIN		0x42
#define LTC4283_SENSE_MAX		0x43
#define LTC4283_VPWR			0x44
#define LTC4283_VPWR_MIN		0x45
#define LTC4283_VPWR_MAX		0x46
#define LTC4283_POWER			0x47
#define LTC4283_POWER_MIN		0x48
#define LTC4283_POWER_MAX		0x49
#define LTC4283_RESERVED_68		0x68
#define LTC4283_RESERVED_6D		0x6D
/* get channels from ADC 2 */
#define LTC4283_ADC_2(c)		(0x4a + (c) * 3)
#define LTC4283_ADC_2_MIN(c)		(0x4b + (c) * 3)
#define LTC4283_ADC_2_MAX(c)		(0x4c + (c) * 3)
#define LTC4283_ADC_2_DIFF(c)		(0x6e + (c) * 3)
#define LTC4283_ADC_2_MIN_DIFF(c)	(0x6f + (c) * 3)
#define LTC4283_ADC_2_MAX_DIFF(c)	(0x70 + (c) * 3)
#define LTC4283_ENERGY			0x7a
#define LTC4283_METER_CONTROL		0x84
#define   LTC4283_INTEGRATE_I_MASK	BIT(0)
#define   LTC4283_METER_HALT_MASK	BIT(6)
#define LTC4283_RESERVED_86		0x86
#define LTC4283_RESERVED_8F		0x8F
#define LTC4283_FAULT_LOG_CTRL		0x90
#define   LTC4283_FAULT_LOG_EN_MASK	BIT(7)
#define LTC4283_RESERVED_91		0x91
#define LTC4283_RESERVED_A1		0xA1
#define LTC4283_RESERVED_A3		0xA3
#define LTC4283_RESERVED_AC		0xAC
#define LTC4283_POWER_PLAY_MSB		0xE7
#define LTC4283_POWER_PLAY_LSB		0xE8
#define LTC4283_RESERVED_F1		0xF1
#define LTC4283_RESERVED_FF		0xFF

/* also applies for differential channels */
#define LTC4283_ADC1_FS_uV		32768
#define LTC4283_ADC2_FS_mV		2048
#define LTC4283_TCONV_uS		64103
#define LTC4283_VILIM_MIN_uV		15000
#define LTC4283_VILIM_MAX_uV		30000
#define LTC4283_VILIM_RANGE	\
	(LTC4283_VILIM_MAX_uV - LTC4283_VILIM_MIN_uV + 1)

#define LTC4283_PGIO_FUNC_GPIO		2
#define LTC4283_PGIO2_FUNC_ACLB		3

/*
 * Maximum value for rsense in nano ohms. The reasoning for this value is that
 * it's the max value for which multiplying by 256 does not overflow long on
 * 32bits. For the minimum value, is a sane minimum rsense for which power_max
 * does not overflow 32bits.
 */
#define LTC4283_MAX_RSENSE	1677721599
#define LTC4283_MIN_RSENSE	50000

/* voltage channels */
enum {
	LTC4283_CHAN_VIN,
	LTC4283_CHAN_VPWR,
	LTC4283_CHAN_ADI_1,
	LTC4283_CHAN_ADI_2,
	LTC4283_CHAN_ADI_3,
	LTC4283_CHAN_ADI_4,
	LTC4283_CHAN_ADIO_1,
	LTC4283_CHAN_ADIO_2,
	LTC4283_CHAN_ADIO_3,
	LTC4283_CHAN_ADIO_4,
	LTC4283_CHAN_DRNS,
	LTC4283_CHAN_DRAIN,
	/* differential channels */
	LTC4283_CHAN_ADIN12,
	LTC4283_CHAN_ADIN34,
	LTC4283_CHAN_ADIO12,
	LTC4283_CHAN_ADIO34,
	LTC4283_CHAN_MAX
};

/* Just for ease of use on the regmap  */
#define LTC4283_ADIO34_MAX \
	LTC4283_ADC_2_MAX_DIFF(LTC4283_CHAN_ADIO34 - LTC4283_CHAN_ADIN12)

struct ltc4283_hwmon {
	struct regmap *map;
	struct i2c_client *client;
	unsigned long gpio_mask;
	unsigned long ch_enable_mask;
	/* in microwatt */
	unsigned long power_max;
	/* in millivolt */
	u32 vsense_max;
	/* in tenths of microohm*/
	u32 rsense;
	bool energy_en;
	bool ext_fault;
};

static int ltc4283_read_voltage_word(const struct ltc4283_hwmon *st,
				     u32 reg, u32 fs, long *val)
{
	unsigned int __raw;
	int ret;

	ret = regmap_read(st->map, reg, &__raw);
	if (ret)
		return ret;

	*val = DIV_ROUND_CLOSEST(__raw * fs, BIT(16));
	return 0;
}

static int ltc4283_read_voltage_byte(const struct ltc4283_hwmon *st,
				     u32 reg, u32 fs, long *val)
{
	int ret;
	u32 in;

	ret = regmap_read(st->map, reg, &in);
	if (ret)
		return ret;

	*val = DIV_ROUND_CLOSEST(in * fs, BIT(8));
	return 0;
}

static u32 ltc4283_in_reg(u32 attr, u32 channel)
{
	switch (attr) {
	case hwmon_in_input:
		if (channel == LTC4283_CHAN_VPWR)
			return LTC4283_VPWR;
		if (channel >= LTC4283_CHAN_ADI_1 && channel <= LTC4283_CHAN_DRAIN)
			return LTC4283_ADC_2(channel - LTC4283_CHAN_ADI_1);
		return LTC4283_ADC_2_DIFF(channel - LTC4283_CHAN_ADIN12);
	case hwmon_in_highest:
		if (channel == LTC4283_CHAN_VPWR)
			return LTC4283_VPWR_MAX;
		if (channel >= LTC4283_CHAN_ADI_1 && channel <= LTC4283_CHAN_DRAIN)
			return LTC4283_ADC_2_MAX(channel - LTC4283_CHAN_ADI_1);
		return LTC4283_ADC_2_MAX_DIFF(channel - LTC4283_CHAN_ADIN12);
	case hwmon_in_lowest:
		if (channel == LTC4283_CHAN_VPWR)
			return LTC4283_VPWR_MIN;
		if (channel >= LTC4283_CHAN_ADI_1 && channel <= LTC4283_CHAN_DRAIN)
			return LTC4283_ADC_2_MIN(channel - LTC4283_CHAN_ADI_1);
		return LTC4283_ADC_2_MIN_DIFF(channel - LTC4283_CHAN_ADIN12);
	case hwmon_in_max:
		if (channel == LTC4283_CHAN_VPWR)
			return LTC4283_VPWR_MAX_TH;
		if (channel >= LTC4283_CHAN_ADI_1 && channel <= LTC4283_CHAN_DRAIN)
			return LTC4283_ADC_2_MAX_TH(channel - LTC4283_CHAN_ADI_1);
		return LTC4283_ADC_2_MAX_TH_DIFF(channel - LTC4283_CHAN_ADIN12);
	default:
		if (channel == LTC4283_CHAN_VPWR)
			return LTC4283_VPWR_MIN_TH;
		if (channel >= LTC4283_CHAN_ADI_1 && channel <= LTC4283_CHAN_DRAIN)
			return LTC4283_ADC_2_MIN_TH(channel - LTC4283_CHAN_ADI_1);
		return LTC4283_ADC_2_MIN_TH_DIFF(channel - LTC4283_CHAN_ADIN12);
	}
}

static int ltc4283_read_in_vals(const struct ltc4283_hwmon *st,
				u32 attr, u32 channel, long *val)
{
	u32 reg = ltc4283_in_reg(attr, channel);
	int ret;

	if (channel < LTC4283_CHAN_ADIN12) {
		if (attr != hwmon_in_max && attr != hwmon_in_min)
			return ltc4283_read_voltage_word(st, reg,
							 LTC4283_ADC2_FS_mV,
							 val);

		return ltc4283_read_voltage_byte(st, reg,
						 LTC4283_ADC2_FS_mV, val);
	}

	if (attr != hwmon_in_max && attr != hwmon_in_min)
		ret = ltc4283_read_voltage_word(st, reg,
						LTC4283_ADC1_FS_uV, val);
	else
		ret = ltc4283_read_voltage_byte(st, reg,
						LTC4283_ADC1_FS_uV, val);
	if (ret)
		return ret;

	*val = DIV_ROUND_CLOSEST(*val, MILLI);
	return 0;
}

static int ltc4283_read_alarm(struct ltc4283_hwmon *st, u32 reg,
			      u32 mask, long *val)
{
	u32 alarm;
	int ret;

	ret = regmap_read(st->map, reg, &alarm);
	if (ret)
		return ret;

	*val = !!(alarm & mask);

	/* If not status/fault logs, clear the alarm after reading it. */
	if (reg != LTC4283_FAULT_STATUS && reg != LTC4283_FAULT_LOG)
		return regmap_write(st->map, reg, alarm & ~mask);

	return 0;
}

static int ltc4283_read_in_alarm(struct ltc4283_hwmon *st, u32 channel,
				 bool max_alm, long *val)
{
	if (channel == LTC4283_CHAN_VPWR)
		return ltc4283_read_alarm(st, LTC4283_ADC_ALM_LOG_1,
					  BIT(2 + max_alm), val);

	if (channel >= LTC4283_CHAN_ADI_1 && channel <= LTC4283_CHAN_ADI_4) {
		u32 bit = (channel - LTC4283_CHAN_ADI_1) * 2;
		/*
		 * Lower channels go to higher bits. We also want to go +1 down
		 * in the min_alarm case.
		 */
		return ltc4283_read_alarm(st, LTC4283_ADC_ALM_LOG_2,
					  BIT(7 - bit - !max_alm), val);
	}

	if (channel >= LTC4283_CHAN_ADIO_1 && channel <= LTC4283_CHAN_ADIO_4) {
		u32 bit = (channel - LTC4283_CHAN_ADIO_1) * 2;

		return ltc4283_read_alarm(st, LTC4283_ADC_ALM_LOG_3,
					  BIT(7 - bit - !max_alm), val);
	}

	if (channel >= LTC4283_CHAN_ADIN12 && channel <= LTC4283_CHAN_ADIO34) {
		u32 bit = (channel - LTC4283_CHAN_ADIN12) * 2;

		return ltc4283_read_alarm(st, LTC4283_ADC_ALM_LOG_5,
					  BIT(7 - bit - !max_alm), val);
	}

	if (channel == LTC4283_CHAN_DRNS)
		return ltc4283_read_alarm(st, LTC4283_ADC_ALM_LOG_4,
					  BIT(6 + max_alm), val);

	return ltc4283_read_alarm(st, LTC4283_ADC_ALM_LOG_4, BIT(4 + max_alm),
				  val);
}

static int ltc4283_read_in(struct ltc4283_hwmon *st, u32 attr, u32 channel,
			   long *val)
{
	switch (attr) {
	case hwmon_in_input:
		if (!test_bit(channel, &st->ch_enable_mask))
			return -ENODATA;

		return ltc4283_read_in_vals(st, attr, channel, val);
	case hwmon_in_highest:
	case hwmon_in_lowest:
	case hwmon_in_max:
	case hwmon_in_min:
		return ltc4283_read_in_vals(st, attr, channel, val);
	case hwmon_in_max_alarm:
		return ltc4283_read_in_alarm(st, channel, true, val);
	case hwmon_in_min_alarm:
		return ltc4283_read_in_alarm(st, channel, false, val);
	case hwmon_in_crit_alarm:
		return ltc4283_read_alarm(st, LTC4283_FAULT_STATUS,
					  LTC4283_OV_MASK, val);
	case hwmon_in_lcrit_alarm:
		return ltc4283_read_alarm(st, LTC4283_FAULT_STATUS,
					  LTC4283_UV_MASK, val);
	case hwmon_in_fault:
		/*
		 * We report failure if we detect either a fer_bad or a
		 * fet_short in the status register.
		 */
		return ltc4283_read_alarm(st, LTC4283_FAULT_STATUS,
					  LTC4283_FET_BAD_MASK | LTC4283_FET_SHORT_MASK, val);
	case hwmon_in_enable:
		*val = test_bit(channel, &st->ch_enable_mask);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int ltc4283_read_current_word(const struct ltc4283_hwmon *st, u32 reg,
				     long *val)
{
	u64 temp = (u64)LTC4283_ADC1_FS_uV * DECA * MILLI;
	unsigned int __raw;
	int ret;

	ret = regmap_read(st->map, reg, &__raw);
	if (ret)
		return ret;

	*val = DIV64_U64_ROUND_CLOSEST(__raw * temp,
				       BIT_ULL(16) * st->rsense);

	return 0;
}

static int ltc4283_read_current_byte(const struct ltc4283_hwmon *st, u32 reg,
				     long *val)
{
	u64 temp = (u64)LTC4283_ADC1_FS_uV * DECA * MILLI;
	u32 curr;
	int ret;

	ret = regmap_read(st->map, reg, &curr);
	if (ret)
		return ret;

	*val = DIV_ROUND_CLOSEST_ULL(curr * temp, BIT(8) * st->rsense);
	return 0;
}

static int ltc4283_read_curr(struct ltc4283_hwmon *st, u32 attr, long *val)
{
	switch (attr) {
	case hwmon_curr_input:
		return ltc4283_read_current_word(st, LTC4283_SENSE, val);
	case hwmon_curr_highest:
		return ltc4283_read_current_word(st, LTC4283_SENSE_MAX, val);
	case hwmon_curr_lowest:
		return ltc4283_read_current_word(st, LTC4283_SENSE_MIN, val);
	case hwmon_curr_max:
		return ltc4283_read_current_byte(st, LTC4283_SENSE_MAX_TH, val);
	case hwmon_curr_min:
		return ltc4283_read_current_byte(st, LTC4283_SENSE_MIN_TH, val);
	case hwmon_curr_max_alarm:
		return ltc4283_read_alarm(st, LTC4283_ADC_ALM_LOG_1,
					  LTC4283_SENSE_HIGH_ALM, val);
	case hwmon_curr_min_alarm:
		return ltc4283_read_alarm(st, LTC4283_ADC_ALM_LOG_1,
					  LTC4283_SENSE_LOW_ALM, val);
	case hwmon_curr_crit_alarm:
		return ltc4283_read_alarm(st, LTC4283_FAULT_STATUS,
					  LTC4283_OC_MASK, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int ltc4283_read_power_word(const struct ltc4283_hwmon *st,
				   u32 reg, long *val)
{
	u64 temp = (u64)LTC4283_ADC1_FS_uV * LTC4283_ADC2_FS_mV * DECA * MILLI;
	unsigned int __raw;
	int ret;

	ret = regmap_read(st->map, reg, &__raw);
	if (ret)
		return ret;

	/*
	 * Power is given by:
	 *     P = CODE(16b) * 32.768mV * 2.048V / (2^16 * Rsense)
	 */
	*val = DIV64_U64_ROUND_CLOSEST(temp * __raw, BIT_ULL(16) * st->rsense);

	return 0;
}

static int ltc4283_read_power_byte(const struct ltc4283_hwmon *st,
				   u32 reg, long *val)
{
	u64 temp = (u64)LTC4283_ADC1_FS_uV * LTC4283_ADC2_FS_mV * DECA * MILLI;
	u32 power;
	int ret;

	ret = regmap_read(st->map, reg, &power);
	if (ret)
		return ret;

	*val = DIV_ROUND_CLOSEST_ULL(power * temp, BIT(8) * st->rsense);

	return 0;
}

static int ltc4283_read_power(struct ltc4283_hwmon *st, u32 attr, long *val)
{
	switch (attr) {
	case hwmon_power_input:
		return ltc4283_read_power_word(st, LTC4283_POWER, val);
	case hwmon_power_input_highest:
		return ltc4283_read_power_word(st, LTC4283_POWER_MAX, val);
	case hwmon_power_input_lowest:
		return ltc4283_read_power_word(st, LTC4283_POWER_MIN, val);
	case hwmon_power_max_alarm:
		return ltc4283_read_alarm(st, LTC4283_ADC_ALM_LOG_1,
					  LTC4283_POWER_HIGH_ALM, val);
	case hwmon_power_min_alarm:
		return ltc4283_read_alarm(st, LTC4283_ADC_ALM_LOG_1,
					  LTC4283_POWER_LOW_ALM, val);
	case hwmon_power_max:
		return ltc4283_read_power_byte(st, LTC4283_POWER_MAX_TH, val);
	case hwmon_power_min:
		return ltc4283_read_power_byte(st, LTC4283_POWER_MIN_TH, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int ltc4283_read_energy(struct ltc4283_hwmon *st, u32 attr, s64 *val)
{
	u64 temp = LTC4283_ADC1_FS_uV * LTC4283_ADC2_FS_mV, energy;
	u8 raw[8] = {};
	int ret;

	if (!st->energy_en)
		return -ENODATA;

	ret = i2c_smbus_read_i2c_block_data(st->client, LTC4283_ENERGY, 6, raw);
	if (ret < 0)
		return ret;
	if (ret != 6)
		return -EIO;

	energy = get_unaligned_be64(raw) >> 16;

	/*
	 * The formula for energy is given by:
	 *	E = CODE(48b) * 32.768mV * 2.048V * Tconv / 2^24 * Rsense
	 *
	 * As Rsense can have tenths of micro-ohm resolution, we need to
	 * multiply by DECA to get microjoule.
	 */

	/*
	 * Use mul_u64_u64_div_u64() to handle the 128-bit intermediate
	 * product of energy (up to 48 bits) * temp * Tconv without overflow.
	 * Multiply rsense by CENTI to convert from tenths-of-microohm back
	 * to nanoohm so the result comes out in microjoule.
	 */
	energy = mul_u64_u64_div_u64(energy, temp * LTC4283_TCONV_uS,
				     BIT_ULL(24) * st->rsense * CENTI);

	*val = energy;
	return 0;
}

static int ltc4283_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct ltc4283_hwmon *st = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_in:
		return ltc4283_read_in(st, attr, channel, val);
	case hwmon_curr:
		return ltc4283_read_curr(st, attr, val);
	case hwmon_power:
		return ltc4283_read_power(st, attr, val);
	case hwmon_energy:
		*val = st->energy_en;
		return 0;
	case hwmon_energy64:
		return ltc4283_read_energy(st, attr, (s64 *)val);
	default:
		return -EOPNOTSUPP;
	}
}

static int ltc4283_write_power_byte(const struct ltc4283_hwmon *st, u32 reg,
				    long val)
{
	u64 temp = (u64)LTC4283_ADC1_FS_uV * LTC4283_ADC2_FS_mV * DECA * MILLI;
	u32 __raw;

	val = clamp_val(val, 0, st->power_max);
	__raw = DIV64_U64_ROUND_CLOSEST(val * BIT_ULL(8) * st->rsense, temp);

	return regmap_write(st->map, reg, __raw);
}

static int ltc4283_write_power_word(const struct ltc4283_hwmon *st,
				    u32 reg, unsigned long val)
{
	u64 divisor = (u64)LTC4283_ADC1_FS_uV * LTC4283_ADC2_FS_mV * DECA * MILLI;
	u16 __raw;

	__raw = mul_u64_u64_div_u64(val, st->rsense * BIT_ULL(16), divisor);

	return regmap_write(st->map, reg, __raw);
}

static int ltc4283_reset_power_hist(struct ltc4283_hwmon *st)
{
	int ret;

	ret = ltc4283_write_power_word(st, LTC4283_POWER_MIN, st->power_max);
	if (ret)
		return ret;

	ret = ltc4283_write_power_word(st, LTC4283_POWER_MAX, 0);
	if (ret)
		return ret;

	/* Clear possible power faults. */
	return regmap_clear_bits(st->map, LTC4283_FAULT_LOG,
				 LTC4283_PWR_FAIL_FAULT_MASK | LTC4283_PGI_FAULT_MASK);
}

static int ltc4283_write_power(struct ltc4283_hwmon *st, u32 attr, long val)
{
	switch (attr) {
	case hwmon_power_max:
		return ltc4283_write_power_byte(st, LTC4283_POWER_MAX_TH, val);
	case hwmon_power_min:
		return ltc4283_write_power_byte(st, LTC4283_POWER_MIN_TH, val);
	case hwmon_power_reset_history:
		return ltc4283_reset_power_hist(st);
	default:
		return -EOPNOTSUPP;
	}
}

static int ltc4283_write_in_history(struct ltc4283_hwmon *st, u32 reg,
				    long lowest, u32 fs)
{
	u32 __raw;
	int ret;

	__raw = DIV_ROUND_CLOSEST(BIT(16) * lowest, fs);
	if (__raw == BIT(16))
		__raw = U16_MAX;

	ret = regmap_write(st->map, reg, __raw);
	if (ret)
		return ret;

	return regmap_write(st->map, reg + 1, 0);
}

static int ltc4283_write_in_byte(const struct ltc4283_hwmon *st,
				 u32 reg, u32 fs, long val)
{
	u32 __raw;

	val = clamp_val(val, 0, fs);
	__raw = DIV_ROUND_CLOSEST(val * BIT(8), fs);
	if (__raw == BIT(8))
		__raw = U8_MAX;

	return regmap_write(st->map, reg, __raw);
}

static int ltc4283_reset_in_hist(struct ltc4283_hwmon *st, u32 channel)
{
	u32 reg, fs;
	int ret;

	/*
	 * Make sure to clear possible under/over voltage faults. Otherwise the
	 * chip won't latch on again.
	 */
	if (channel == LTC4283_CHAN_VIN)
		return regmap_clear_bits(st->map, LTC4283_FAULT_LOG,
					 LTC4283_OV_FAULT_MASK | LTC4283_UV_FAULT_MASK);

	if (channel == LTC4283_CHAN_VPWR)
		return ltc4283_write_in_history(st, LTC4283_VPWR_MIN,
						LTC4283_ADC2_FS_mV,
						LTC4283_ADC2_FS_mV);

	if (channel >= LTC4283_CHAN_ADI_1 && channel <= LTC4283_CHAN_DRAIN) {
		fs = LTC4283_ADC2_FS_mV;
		reg = LTC4283_ADC_2_MIN(channel - LTC4283_CHAN_ADI_1);
	} else {
		fs = LTC4283_ADC1_FS_uV;
		reg = LTC4283_ADC_2_MIN_DIFF(channel - LTC4283_CHAN_ADIN12);
	}

	ret = ltc4283_write_in_history(st, reg, fs, fs);
	if (ret)
		return ret;
	if (channel != LTC4283_CHAN_DRAIN)
		return 0;

	/* Then, let's also clear possible fet faults. Same as above. */
	return regmap_clear_bits(st->map, LTC4283_FAULT_LOG,
				 LTC4283_FET_BAD_FAULT_MASK | LTC4283_FET_SHORT_FAULT_MASK);
}

static int ltc4283_write_in_en(struct ltc4283_hwmon *st, u32 channel, bool en)
{
	unsigned int bit, adc_idx = channel - LTC4283_CHAN_ADI_1;
	unsigned int reg = LTC4283_ADC_SELECT(adc_idx);
	int ret;

	bit = LTC4283_ADC_SELECT_MASK(adc_idx);
	if (channel > LTC4283_CHAN_DRAIN)
		/* Account for two reserved fields after DRAIN. */
		bit <<= 2;

	if (en)
		ret = regmap_set_bits(st->map, reg, bit);
	else
		ret = regmap_clear_bits(st->map, reg, bit);
	if (ret)
		return ret;

	__assign_bit(channel, &st->ch_enable_mask, en);
	return 0;
}

static int ltc4283_write_minmax(struct ltc4283_hwmon *st, long val,
				u32 channel, bool is_max)
{
	u32 reg;

	if (channel == LTC4283_CHAN_VPWR) {
		if (is_max)
			return ltc4283_write_in_byte(st, LTC4283_VPWR_MAX_TH,
						     LTC4283_ADC2_FS_mV, val);

		return ltc4283_write_in_byte(st, LTC4283_VPWR_MIN_TH,
					     LTC4283_ADC2_FS_mV, val);
	}

	if (channel >= LTC4283_CHAN_ADI_1 && channel <= LTC4283_CHAN_DRAIN) {
		if (is_max) {
			reg = LTC4283_ADC_2_MAX_TH(channel - LTC4283_CHAN_ADI_1);
			return ltc4283_write_in_byte(st, reg,
						     LTC4283_ADC2_FS_mV, val);
		}

		reg = LTC4283_ADC_2_MIN_TH(channel - LTC4283_CHAN_ADI_1);
		return ltc4283_write_in_byte(st, reg, LTC4283_ADC2_FS_mV, val);
	}

	/* Clamp before multiplying to avoid overflow on any arch. */
	val = clamp_val(val, 0, LONG_MAX / MILLI);

	if (is_max) {
		reg = LTC4283_ADC_2_MAX_TH_DIFF(channel - LTC4283_CHAN_ADIN12);
		return ltc4283_write_in_byte(st, reg, LTC4283_ADC1_FS_uV,
					     val * MILLI);
	}

	reg = LTC4283_ADC_2_MIN_TH_DIFF(channel - LTC4283_CHAN_ADIN12);
	return ltc4283_write_in_byte(st, reg, LTC4283_ADC1_FS_uV, val * MILLI);
}

static int ltc4283_write_in(struct ltc4283_hwmon *st, u32 attr, long val,
			    int channel)
{
	switch (attr) {
	case hwmon_in_max:
		return ltc4283_write_minmax(st, val, channel, true);
	case hwmon_in_min:
		return ltc4283_write_minmax(st, val, channel, false);
	case hwmon_in_reset_history:
		return ltc4283_reset_in_hist(st, channel);
	case hwmon_in_enable:
		return ltc4283_write_in_en(st, channel, !!val);
	default:
		return -EOPNOTSUPP;
	}
}

static int ltc4283_write_curr_byte(const struct ltc4283_hwmon *st,
				   u32 reg, long val)
{
	u32 temp = LTC4283_ADC1_FS_uV * DECA * MILLI;
	u32 reg_val, isense_max;

	isense_max = DIV_ROUND_CLOSEST(st->vsense_max * MICRO * DECA, st->rsense);
	val = clamp_val(val, 0, isense_max);
	reg_val = DIV_ROUND_CLOSEST_ULL(val * BIT_ULL(8) * st->rsense, temp);

	return regmap_write(st->map, reg, reg_val);
}

static int ltc4283_write_curr_history(struct ltc4283_hwmon *st)
{
	int ret;

	ret = ltc4283_write_in_history(st, LTC4283_SENSE_MIN,
				       st->vsense_max * MILLI,
				       LTC4283_ADC1_FS_uV);
	if (ret)
		return ret;

	/* Now, let's also clear possible overcurrent logs. */
	return regmap_clear_bits(st->map, LTC4283_FAULT_LOG,
				 LTC4283_OC_FAULT_MASK);
}

static int ltc4283_write_curr(struct ltc4283_hwmon *st, u32 attr, long val)
{
	switch (attr) {
	case hwmon_curr_max:
		return ltc4283_write_curr_byte(st, LTC4283_SENSE_MAX_TH, val);
	case hwmon_curr_min:
		return ltc4283_write_curr_byte(st, LTC4283_SENSE_MIN_TH, val);
	case hwmon_curr_reset_history:
		return ltc4283_write_curr_history(st);
	default:
		return -EOPNOTSUPP;
	}
}

static int ltc4283_energy_enable_set(struct ltc4283_hwmon *st, long val)
{
	int ret;

	/* Setting the bit halts the meter. */
	val = !!val;
	ret = regmap_update_bits(st->map, LTC4283_METER_CONTROL,
				 LTC4283_METER_HALT_MASK,
				 FIELD_PREP(LTC4283_METER_HALT_MASK, !val));
	if (ret)
		return ret;

	st->energy_en = val;

	return 0;
}

static int ltc4283_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct ltc4283_hwmon *st = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_power:
		return ltc4283_write_power(st, attr, val);
	case hwmon_in:
		return ltc4283_write_in(st, attr, val, channel);
	case hwmon_curr:
		return ltc4283_write_curr(st, attr, val);
	case hwmon_energy:
		return ltc4283_energy_enable_set(st, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t ltc4283_in_is_visible(const struct ltc4283_hwmon *st,
				     u32 attr, int channel)
{
	/* If ADIO is set as a GPIO, don´t make it visible. */
	if (channel >= LTC4283_CHAN_ADIO_1 && channel <= LTC4283_CHAN_ADIO_4) {
		/* ADIOX pins come at index 0 in the gpio mask. */
		channel -= LTC4283_CHAN_ADIO_1;
		if (test_bit(channel, &st->gpio_mask))
			return 0;
	}

	/* Also take care of differential channels. */
	if (channel >= LTC4283_CHAN_ADIO12 && channel <= LTC4283_CHAN_ADIO34) {
		channel -= LTC4283_CHAN_ADIO12;
		/* If one channel in the pair is used, make it invisible. */
		if (test_bit(channel * 2, &st->gpio_mask) ||
		    test_bit(channel * 2 + 1, &st->gpio_mask))
			return 0;
	}

	switch (attr) {
	case hwmon_in_input:
	case hwmon_in_highest:
	case hwmon_in_lowest:
	case hwmon_in_max_alarm:
	case hwmon_in_min_alarm:
	case hwmon_in_label:
	case hwmon_in_lcrit_alarm:
	case hwmon_in_crit_alarm:
	case hwmon_in_fault:
		return 0444;
	case hwmon_in_max:
	case hwmon_in_min:
	case hwmon_in_enable:
		return 0644;
	case hwmon_in_reset_history:
		return 0200;
	default:
		return 0;
	}
}

static umode_t ltc4283_curr_is_visible(u32 attr)
{
	switch (attr) {
	case hwmon_curr_input:
	case hwmon_curr_highest:
	case hwmon_curr_lowest:
	case hwmon_curr_max_alarm:
	case hwmon_curr_min_alarm:
	case hwmon_curr_crit_alarm:
	case hwmon_curr_label:
		return 0444;
	case hwmon_curr_max:
	case hwmon_curr_min:
		return 0644;
	case hwmon_curr_reset_history:
		return 0200;
	default:
		return 0;
	}
}

static umode_t ltc4283_power_is_visible(u32 attr)
{
	switch (attr) {
	case hwmon_power_input:
	case hwmon_power_input_highest:
	case hwmon_power_input_lowest:
	case hwmon_power_label:
	case hwmon_power_max_alarm:
	case hwmon_power_min_alarm:
		return 0444;
	case hwmon_power_max:
	case hwmon_power_min:
		return 0644;
	case hwmon_power_reset_history:
		return 0200;
	default:
		return 0;
	}
}

static umode_t ltc4283_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_in:
		return ltc4283_in_is_visible(data, attr, channel);
	case hwmon_curr:
		return ltc4283_curr_is_visible(attr);
	case hwmon_power:
		return ltc4283_power_is_visible(attr);
	case hwmon_energy:
		/* hwmon_energy_enable */
		return 0644;
	case hwmon_energy64:
		/* hwmon_energy_input */
		return 0444;
	default:
		return 0;
	}
}

static const char * const ltc4283_in_strs[] = {
	"VIN", "VPWR", "VADI1", "VADI2", "VADI3", "VADI4", "VADIO1", "VADIO2",
	"VADIO3", "VADIO4", "DRNS", "DRAIN", "ADIN2-ADIN1", "ADIN4-ADIN3",
	"ADIO2-ADIO1", "ADIO4-ADIO3"
};

static int ltc4283_read_labels(struct device *dev,
			       enum hwmon_sensor_types type,
			       u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_in:
		*str = ltc4283_in_strs[channel];
		return 0;
	case hwmon_curr:
		*str = "ISENSE";
		return 0;
	case hwmon_power:
		*str = "Power";
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

/*
 * Set max limits for ISENSE and Power as that depends on the max voltage on
 * rsense that is defined in ILIM_ADJUST. This is specially important for power
 * because for some rsense and vfsout values, if we allow the default raw 255
 * value, that would overflow long in 32bit archs when reading back the max
 * power limit.
 */
static int ltc4283_set_max_limits(struct ltc4283_hwmon *st, struct device *dev)
{
	u32 temp = st->vsense_max * DECA * MICRO;
	int ret;

	ret = ltc4283_write_in_byte(st, LTC4283_SENSE_MAX_TH, LTC4283_ADC1_FS_uV,
				    st->vsense_max * MILLI);
	if (ret)
		return ret;

	/* Power is given by ISENSE * Vout. */
	st->power_max = DIV_ROUND_CLOSEST(temp, st->rsense) * LTC4283_ADC2_FS_mV;
	return ltc4283_write_power_byte(st, LTC4283_POWER_MAX_TH, st->power_max);
}

static int ltc4283_parse_array_prop(const struct ltc4283_hwmon *st,
				    struct device *dev, const char *prop,
				    const u32 *vals, u32 n_vals)
{
	u32 prop_val;
	int ret;
	u32 i;

	ret = device_property_read_u32(dev, prop, &prop_val);
	if (ret)
		return n_vals;

	for (i = 0; i < n_vals; i++) {
		if (prop_val != vals[i])
			continue;

		return i;
	}

	return dev_err_probe(dev, -EINVAL,
			     "Invalid %s property value %u\n", prop, prop_val);
}

static int ltc4283_get_defaults(struct ltc4283_hwmon *st)
{
	u32 reg_val, ilm_adjust, c;
	int ret;

	ret = regmap_read(st->map, LTC4283_METER_CONTROL, &reg_val);
	if (ret)
		return ret;

	st->energy_en = !FIELD_GET(LTC4283_METER_HALT_MASK, reg_val);

	ret = regmap_read(st->map, LTC4283_CONFIG_1, &reg_val);
	if (ret)
		return ret;

	ilm_adjust = FIELD_GET(LTC4283_ILIM_MASK, reg_val);
	st->vsense_max = LTC4283_VILIM_MIN_uV / MILLI + ilm_adjust;

	ret = regmap_read(st->map, LTC4283_PGIO_CONFIG, &reg_val);
	if (ret)
		return ret;

	/* Can be latter overwritten in ltc4283_pgio_config() */
	if (FIELD_GET(LTC4283_PGIO4_CFG_MASK, reg_val) < LTC4283_PGIO_FUNC_GPIO)
		st->ext_fault = true;

	/* VPWR and VIN are always enabled */
	__set_bit(LTC4283_CHAN_VIN, &st->ch_enable_mask);
	__set_bit(LTC4283_CHAN_VPWR, &st->ch_enable_mask);
	for (c = LTC4283_CHAN_ADI_1; c < LTC4283_CHAN_MAX; c++) {
		u32 chan = c - LTC4283_CHAN_ADI_1, bit;

		ret = regmap_read(st->map, LTC4283_ADC_SELECT(chan), &reg_val);
		if (ret)
			return ret;

		bit = LTC4283_ADC_SELECT_MASK(chan);
		if (c > LTC4283_CHAN_DRAIN)
			/* account for two reserved fields after DRAIN */
			bit <<= 2;

		if (!(bit & reg_val))
			continue;

		__set_bit(c, &st->ch_enable_mask);
	}

	return 0;
}

static const char * const ltc4283_pgio1_funcs[] = {
	"inverted_power_good", "power_good", "gpio"
};

static const char * const ltc4283_pgio2_funcs[] = {
	 "inverted_power_good", "power_good", "gpio", "active_current_limiting"
};

static const char * const ltc4283_pgio3_funcs[] = {
	"inverted_power_good_input", "power_good_input", "gpio"
};

static const char * const ltc4283_pgio4_funcs[] = {
	"inverted_external_fault", "external_fault", "gpio"
};

enum {
	LTC4283_PIN_ADIO1,
	LTC4283_PIN_ADIO2,
	LTC4283_PIN_ADIO3,
	LTC4283_PIN_ADIO4,
	LTC4283_PIN_PGIO1,
	LTC4283_PIN_PGIO2,
	LTC4283_PIN_PGIO3,
	LTC4283_PIN_PGIO4,
};

static int ltc4283_pgio_config(struct ltc4283_hwmon *st, struct device *dev)
{
	int ret, func;

	func = device_property_match_property_string(dev, "adi,pgio1-func",
						     ltc4283_pgio1_funcs,
						     ARRAY_SIZE(ltc4283_pgio1_funcs));
	if (func < 0 && func != -EINVAL)
		return dev_err_probe(dev, func,
				     "Invalid adi,pgio1-func property\n");
	if (func >= 0) {
		if (func == LTC4283_PGIO_FUNC_GPIO) {
			__set_bit(LTC4283_PIN_PGIO1, &st->gpio_mask);
			/* If GPIO, default to an input pin. */
			func++;
		}

		ret = regmap_update_bits(st->map, LTC4283_PGIO_CONFIG,
					 LTC4283_PGIO1_CFG_MASK,
					 FIELD_PREP(LTC4283_PGIO1_CFG_MASK, func));
		if (ret)
			return ret;
	}

	func = device_property_match_property_string(dev, "adi,pgio2-func",
						     ltc4283_pgio2_funcs,
						     ARRAY_SIZE(ltc4283_pgio2_funcs));

	if (func < 0 && func != -EINVAL)
		return dev_err_probe(dev, func,
				     "Invalid adi,pgio2-func property\n");
	if (func >= 0) {
		if (func != LTC4283_PGIO2_FUNC_ACLB) {
			if (func == LTC4283_PGIO_FUNC_GPIO)  {
				__set_bit(LTC4283_PIN_PGIO2, &st->gpio_mask);
				func++;
			}

			ret = regmap_update_bits(st->map, LTC4283_PGIO_CONFIG,
						 LTC4283_PGIO2_CFG_MASK,
						 FIELD_PREP(LTC4283_PGIO2_CFG_MASK, func));
		} else {
			ret = regmap_set_bits(st->map, LTC4283_CONTROL_1,
					      LTC4283_PIGIO2_ACLB_MASK);
		}

		if (ret)
			return ret;
	}

	func = device_property_match_property_string(dev, "adi,pgio3-func",
						     ltc4283_pgio3_funcs,
						     ARRAY_SIZE(ltc4283_pgio3_funcs));

	if (func < 0 && func != -EINVAL)
		return dev_err_probe(dev, func,
				     "Invalid adi,pgio3-func property\n");
	if (func >= 0) {
		if (func == LTC4283_PGIO_FUNC_GPIO) {
			__set_bit(LTC4283_PIN_PGIO3, &st->gpio_mask);
			func++;
		}

		ret = regmap_update_bits(st->map, LTC4283_PGIO_CONFIG,
					 LTC4283_PGIO3_CFG_MASK,
					 FIELD_PREP(LTC4283_PGIO3_CFG_MASK, func));
		if (ret)
			return ret;
	}

	func = device_property_match_property_string(dev, "adi,pgio4-func",
						     ltc4283_pgio4_funcs,
						     ARRAY_SIZE(ltc4283_pgio4_funcs));

	if (func < 0 && func != -EINVAL)
		return dev_err_probe(dev, func,
				     "Invalid adi,pgio4-func property\n");
	if (func >= 0) {
		if (func == LTC4283_PGIO_FUNC_GPIO) {
			__set_bit(LTC4283_PIN_PGIO4, &st->gpio_mask);
			func++;
			st->ext_fault = false;
		} else {
			st->ext_fault = true;
		}

		ret = regmap_update_bits(st->map, LTC4283_PGIO_CONFIG,
					 LTC4283_PGIO4_CFG_MASK,
					 FIELD_PREP(LTC4283_PGIO4_CFG_MASK, func));
		if (ret)
			return ret;
	}

	return 0;
}

static int ltc4283_adio_config(struct ltc4283_hwmon *st, struct device *dev,
			       const char *prop, u32 pin)
{
	u32 adc_idx;
	int ret;

	if (!device_property_read_bool(dev, prop))
		return 0;

	adc_idx = LTC4283_CHAN_ADIO_1 - LTC4283_CHAN_ADI_1 + pin;
	ret = regmap_clear_bits(st->map, LTC4283_ADC_SELECT(adc_idx),
				LTC4283_ADC_SELECT_MASK(adc_idx));
	if (ret)
		return ret;

	__set_bit(pin, &st->gpio_mask);
	return 0;
}

static int ltc4283_pin_config(struct ltc4283_hwmon *st, struct device *dev)
{
	int ret;

	ret = ltc4283_pgio_config(st, dev);
	if (ret)
		return ret;

	ret = ltc4283_adio_config(st, dev, "adi,gpio-on-adio1", LTC4283_PIN_ADIO1);
	if (ret)
		return ret;

	ret = ltc4283_adio_config(st, dev, "adi,gpio-on-adio2", LTC4283_PIN_ADIO2);
	if (ret)
		return ret;

	ret = ltc4283_adio_config(st, dev, "adi,gpio-on-adio3", LTC4283_PIN_ADIO3);
	if (ret)
		return ret;

	return ltc4283_adio_config(st, dev, "adi,gpio-on-adio4", LTC4283_PIN_ADIO4);
}

static const char * const ltc4283_oc_fet_retry[] = {
	"latch-off", "1", "7", "unlimited"
};

static const u32 ltc4283_fb_factor[] = {
	100, 50, 20, 10
};

static const u32 ltc4283_cooling_dl[] = {
	512, 1002, 2005, 4100, 8190, 16400, 32800, 65600
};

static const u32 ltc4283_fet_bad_delay[] = {
	256, 512, 1002, 2005
};

static int ltc4283_setup(struct ltc4283_hwmon *st, struct device *dev)
{
	u32 val;
	int ret;

	/* The part has an eeprom so let's get the needed defaults from it */
	ret = ltc4283_get_defaults(st);
	if (ret)
		return ret;

	/*
	 * Default to LTC4283_MIN_RSENSE so we can probe without FW properties.
	 */
	st->rsense = LTC4283_MIN_RSENSE;
	ret = device_property_read_u32(dev, "adi,rsense-nano-ohms",
				       &st->rsense);
	if (!ret) {
		if (st->rsense < LTC4283_MIN_RSENSE || st->rsense > LTC4283_MAX_RSENSE)
			return dev_err_probe(dev, -EINVAL,
					     "adi,rsense-nano-ohms(%u) too small or too large [%u %u]\n",
					     st->rsense, LTC4283_MIN_RSENSE, LTC4283_MAX_RSENSE);
	}

	/*
	 * The resolution for rsense is tenths of micro (eg: 62.5 uOhm) which
	 * means we need nano in the bindings. However, to make things easier to
	 * handle (with respect to overflows) we divide it by 100 as we don't
	 * really need the last two digits.
	 */
	st->rsense /= CENTI;

	ret = device_property_read_u32(dev, "adi,current-limit-sense-microvolt",
				       &st->vsense_max);
	if (!ret) {
		u32 reg_val;

		if (!in_range(st->vsense_max, LTC4283_VILIM_MIN_uV,
			      LTC4283_VILIM_RANGE)) {
			return dev_err_probe(dev, -EINVAL,
					     "adi,current-limit-sense-microvolt (%u) out of range [%u %u]\n",
					     st->vsense_max, LTC4283_VILIM_MIN_uV,
					     LTC4283_VILIM_MAX_uV);
		}

		st->vsense_max /= MILLI;
		reg_val = FIELD_PREP(LTC4283_ILIM_MASK,
				     st->vsense_max - LTC4283_VILIM_MIN_uV / MILLI);
		ret = regmap_update_bits(st->map, LTC4283_CONFIG_1,
					 LTC4283_ILIM_MASK, reg_val);
		if (ret)
			return ret;
	}

	ret = ltc4283_parse_array_prop(st, dev, "adi,current-limit-foldback-factor",
				       ltc4283_fb_factor, ARRAY_SIZE(ltc4283_fb_factor));
	if (ret < 0)
		return ret;
	if (ret < ARRAY_SIZE(ltc4283_fb_factor)) {
		ret = regmap_update_bits(st->map, LTC4283_CONFIG_1, LTC4283_FB_MASK,
					 FIELD_PREP(LTC4283_FB_MASK, ret));
		if (ret)
			return ret;
	}

	ret = ltc4283_parse_array_prop(st, dev, "adi,cooling-delay-ms",
				       ltc4283_cooling_dl, ARRAY_SIZE(ltc4283_cooling_dl));
	if (ret < 0)
		return ret;
	if (ret < ARRAY_SIZE(ltc4283_cooling_dl)) {
		ret = regmap_update_bits(st->map, LTC4283_CONFIG_2, LTC4283_COOLING_DL_MASK,
					 FIELD_PREP(LTC4283_COOLING_DL_MASK, ret));
		if (ret)
			return ret;
	}

	ret = ltc4283_parse_array_prop(st, dev, "adi,fet-bad-timer-delay-ms",
				       ltc4283_fet_bad_delay, ARRAY_SIZE(ltc4283_fet_bad_delay));
	if (ret < 0)
		return ret;
	if (ret < ARRAY_SIZE(ltc4283_fet_bad_delay)) {
		ret = regmap_update_bits(st->map, LTC4283_CONFIG_2, LTC4283_FTBD_DL_MASK,
					 FIELD_PREP(LTC4283_FTBD_DL_MASK, ret));
		if (ret)
			return ret;
	}

	ret = ltc4283_set_max_limits(st, dev);
	if (ret)
		return ret;

	ret = ltc4283_pin_config(st, dev);
	if (ret)
		return ret;

	if (device_property_read_bool(dev, "adi,power-good-reset-on-fet")) {
		ret = regmap_clear_bits(st->map, LTC4283_CONTROL_1,
					LTC4283_PWRGD_RST_CTRL_MASK);
		if (ret)
			return ret;
	}

	if (device_property_read_bool(dev, "adi,fet-turn-off-disable")) {
		ret = regmap_clear_bits(st->map, LTC4283_CONTROL_1,
					LTC4283_FET_BAD_OFF_MASK);
		if (ret)
			return ret;
	}

	if (device_property_read_bool(dev, "adi,tmr-pull-down-disable")) {
		ret = regmap_set_bits(st->map, LTC4283_CONTROL_1,
				      LTC4283_THERM_TMR_MASK);
		if (ret)
			return ret;
	}

	if (device_property_read_bool(dev, "adi,dvdt-inrush-control-disable")) {
		ret = regmap_clear_bits(st->map, LTC4283_CONTROL_1,
					LTC4283_DVDT_MASK);
		if (ret)
			return ret;
	}

	if (device_property_read_bool(dev, "adi,undervoltage-retry-disable")) {
		ret = regmap_clear_bits(st->map, LTC4283_CONTROL_2,
					LTC4283_UV_RETRY_MASK);
		if (ret)
			return ret;
	}

	if (device_property_read_bool(dev, "adi,overvoltage-retry-disable")) {
		ret = regmap_clear_bits(st->map, LTC4283_CONTROL_2,
					LTC4283_OV_RETRY_MASK);
		if (ret)
			return ret;
	}

	if (device_property_read_bool(dev, "adi,external-fault-retry-enable")) {
		if (!st->ext_fault)
			return dev_err_probe(dev, -EINVAL,
					     "adi,external-fault-retry-enable set but PGIO4 not configured\n");
		ret = regmap_set_bits(st->map, LTC4283_CONTROL_2,
				      LTC4283_EXT_FAULT_RETRY_MASK);
		if (ret)
			return ret;
	}

	if (device_property_read_bool(dev, "adi,fault-log-enable")) {
		ret = regmap_set_bits(st->map, LTC4283_FAULT_LOG_CTRL,
				      LTC4283_FAULT_LOG_EN_MASK);
		if (ret)
			return ret;
	}

	ret = device_property_match_property_string(dev, "adi,overcurrent-retries",
						    ltc4283_oc_fet_retry,
						    ARRAY_SIZE(ltc4283_oc_fet_retry));
	/* We still want to catch when an invalid string is given. */
	if (ret < 0 && ret != -EINVAL)
		return dev_err_probe(dev, ret,
				     "adi,overcurrent-retries invalid value\n");
	if (ret >= 0) {
		ret = regmap_update_bits(st->map, LTC4283_CONTROL_2,
					 LTC4283_OC_RETRY_MASK,
					 FIELD_PREP(LTC4283_OC_RETRY_MASK, ret));
		if (ret)
			return ret;
	}

	ret = device_property_match_property_string(dev, "adi,fet-bad-retries",
						    ltc4283_oc_fet_retry,
						    ARRAY_SIZE(ltc4283_oc_fet_retry));
	if (ret < 0 && ret != -EINVAL)
		return dev_err_probe(dev, ret,
				     "adi,fet-bad-retries invalid value\n");
	if (ret >= 0) {
		ret = regmap_update_bits(st->map, LTC4283_CONTROL_2,
					 LTC4283_FET_BAD_RETRY_MASK,
					 FIELD_PREP(LTC4283_FET_BAD_RETRY_MASK, ret));
		if (ret)
			return ret;
	}

	if (device_property_read_bool(dev, "adi,external-fault-fet-off-enable")) {
		if (!st->ext_fault)
			return dev_err_probe(dev, -EINVAL,
					     "adi,external-fault-fet-off-enable set but PGIO4 not configured\n");
		ret = regmap_set_bits(st->map, LTC4283_CONFIG_3,
				      LTC4283_EXTFLT_TURN_OFF_MASK);
		if (ret)
			return ret;
	}

	if (device_property_read_bool(dev, "adi,vpower-drns-enable")) {
		u32 chan = LTC4283_CHAN_DRNS - LTC4283_CHAN_ADI_1;

		__clear_bit(LTC4283_CHAN_DRNS, &st->ch_enable_mask);
		/*
		 * Then, let's by default disable DRNS from ADC2 given that it
		 * is already being monitored by the VPWR channel. One can still
		 * enable it later on if needed.
		 */
		ret = regmap_clear_bits(st->map, LTC4283_ADC_SELECT(chan),
					LTC4283_ADC_SELECT_MASK(chan));
		if (ret)
			return ret;

		val = 1;
	} else {
		val = 0;
	}

	ret = regmap_update_bits(st->map, LTC4283_CONFIG_3,
				 LTC4283_VPWR_DRNS_MASK,
				 FIELD_PREP(LTC4283_VPWR_DRNS_MASK, val));
	if (ret)
		return ret;

	/* Make sure the ADC has 12bit resolution since we're assuming that. */
	ret = regmap_update_bits(st->map, LTC4283_PGIO_CONFIG_2,
				 LTC4283_ADC_MASK,
				 FIELD_PREP(LTC4283_ADC_MASK, 3));
	if (ret)
		return ret;

	/* Energy reads (which are 6 byte block reads) rely on page access */
	ret = regmap_set_bits(st->map, LTC4283_CONTROL_1, LTC4283_RW_PAGE_MASK);
	if (ret)
		return ret;

	/*
	 * Make sure we are integrating power as we only support reporting
	 * consumed energy.
	 */
	return regmap_clear_bits(st->map, LTC4283_METER_CONTROL,
				 LTC4283_INTEGRATE_I_MASK);
}

static const struct hwmon_channel_info * const ltc4283_info[] = {
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_LCRIT_ALARM | HWMON_I_CRIT_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_MAX_ALARM | HWMON_I_RESET_HISTORY |
			   HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_FAULT | HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST |
			   HWMON_I_MAX | HWMON_I_MIN | HWMON_I_MIN_ALARM |
			   HWMON_I_RESET_HISTORY | HWMON_I_MAX_ALARM |
			   HWMON_I_ENABLE | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LOWEST | HWMON_C_HIGHEST |
			   HWMON_C_MAX | HWMON_C_MIN | HWMON_C_MIN_ALARM |
			   HWMON_C_MAX_ALARM | HWMON_C_CRIT_ALARM |
			   HWMON_C_RESET_HISTORY | HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_INPUT_LOWEST |
			   HWMON_P_INPUT_HIGHEST | HWMON_P_MAX | HWMON_P_MIN |
			   HWMON_P_MAX_ALARM | HWMON_P_MIN_ALARM |
			   HWMON_P_RESET_HISTORY | HWMON_P_LABEL),
	HWMON_CHANNEL_INFO(energy,
			   HWMON_E_ENABLE),
	HWMON_CHANNEL_INFO(energy64,
			   HWMON_E_INPUT),
	NULL
};

static const struct hwmon_ops ltc4283_ops = {
	.read = ltc4283_read,
	.write = ltc4283_write,
	.is_visible = ltc4283_is_visible,
	.read_string = ltc4283_read_labels,
};

static const struct hwmon_chip_info ltc4283_chip_info = {
	.ops = &ltc4283_ops,
	.info = ltc4283_info,
};

static int ltc4283_show_fault_log(void *arg, u64 *val, u32 mask)
{
	struct ltc4283_hwmon *st = arg;
	long alarm;
	int ret;

	ret = ltc4283_read_alarm(st, LTC4283_FAULT_LOG, mask, &alarm);
	if (ret)
		return ret;

	*val = alarm;

	return 0;
}

static int ltc4283_show_in0_lcrit_fault_log(void *arg, u64 *val)
{
	return ltc4283_show_fault_log(arg, val, LTC4283_UV_FAULT_MASK);
}
DEFINE_DEBUGFS_ATTRIBUTE(ltc4283_in0_lcrit_fault_log,
			 ltc4283_show_in0_lcrit_fault_log, NULL, "%llu\n");

static int ltc4283_show_in0_crit_fault_log(void *arg, u64 *val)
{
	return ltc4283_show_fault_log(arg, val, LTC4283_OV_FAULT_MASK);
}
DEFINE_DEBUGFS_ATTRIBUTE(ltc4283_in0_crit_fault_log,
			 ltc4283_show_in0_crit_fault_log, NULL, "%llu\n");

static int ltc4283_show_fet_bad_fault_log(void *arg, u64 *val)
{
	return ltc4283_show_fault_log(arg, val, LTC4283_FET_BAD_FAULT_MASK);
}
DEFINE_DEBUGFS_ATTRIBUTE(ltc4283_fet_bad_fault_log,
			 ltc4283_show_fet_bad_fault_log, NULL, "%llu\n");

static int ltc4283_show_fet_short_fault_log(void *arg, u64 *val)
{
	return ltc4283_show_fault_log(arg, val, LTC4283_FET_SHORT_FAULT_MASK);
}
DEFINE_DEBUGFS_ATTRIBUTE(ltc4283_fet_short_fault_log,
			 ltc4283_show_fet_short_fault_log, NULL, "%llu\n");

static int ltc4283_show_curr1_crit_fault_log(void *arg, u64 *val)
{
	return ltc4283_show_fault_log(arg, val, LTC4283_OC_FAULT_MASK);
}
DEFINE_DEBUGFS_ATTRIBUTE(ltc4283_curr1_crit_fault_log,
			 ltc4283_show_curr1_crit_fault_log, NULL, "%llu\n");

static int ltc4283_show_power1_failed_fault_log(void *arg, u64 *val)
{
	return ltc4283_show_fault_log(arg, val, LTC4283_PWR_FAIL_FAULT_MASK);
}
DEFINE_DEBUGFS_ATTRIBUTE(ltc4283_power1_failed_fault_log,
			 ltc4283_show_power1_failed_fault_log, NULL, "%llu\n");

static int ltc4283_show_power1_good_input_fault_log(void *arg, u64 *val)
{
	return ltc4283_show_fault_log(arg, val, LTC4283_PGI_FAULT_MASK);
}
DEFINE_DEBUGFS_ATTRIBUTE(ltc4283_power1_good_input_fault_log,
			 ltc4283_show_power1_good_input_fault_log, NULL, "%llu\n");

static void ltc4283_debugfs_init(struct ltc4283_hwmon *st, struct i2c_client *i2c)
{
	debugfs_create_file_unsafe("in0_crit_fault_log", 0400, i2c->debugfs, st,
				   &ltc4283_in0_crit_fault_log);
	debugfs_create_file_unsafe("in0_lcrit_fault_log", 0400, i2c->debugfs, st,
				   &ltc4283_in0_lcrit_fault_log);
	debugfs_create_file_unsafe("in11_fet_bad_fault_log", 0400, i2c->debugfs, st,
				   &ltc4283_fet_bad_fault_log);
	debugfs_create_file_unsafe("in11_fet_short_fault_log", 0400, i2c->debugfs, st,
				   &ltc4283_fet_short_fault_log);
	debugfs_create_file_unsafe("curr1_crit_fault_log", 0400, i2c->debugfs, st,
				   &ltc4283_curr1_crit_fault_log);
	debugfs_create_file_unsafe("power1_failed_fault_log", 0400, i2c->debugfs, st,
				   &ltc4283_power1_failed_fault_log);
	debugfs_create_file_unsafe("power1_good_input_fault_log", 0400, i2c->debugfs,
				   st, &ltc4283_power1_good_input_fault_log);
}

static bool ltc4283_is_word_reg(unsigned int reg)
{
	return reg >= LTC4283_SENSE && reg <= LTC4283_ADIO34_MAX;
}

static int ltc4283_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct i2c_client *client = context;
	int ret;

	if (ltc4283_is_word_reg(reg))
		ret = i2c_smbus_read_word_swapped(client, reg);
	else
		ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}

static int ltc4283_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct i2c_client *client = context;

	if (ltc4283_is_word_reg(reg))
		return i2c_smbus_write_word_swapped(client, reg, val);

	return i2c_smbus_write_byte_data(client, reg, val);
}

static const struct regmap_bus ltc4283_regmap_bus = {
	.reg_read = ltc4283_reg_read,
	.reg_write = ltc4283_reg_write,
};

static bool ltc4283_writable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LTC4283_SYSTEM_STATUS ... LTC4283_FAULT_STATUS:
		return false;
	case LTC4283_RESERVED_OC:
		return false;
	case LTC4283_RESERVED_86 ... LTC4283_RESERVED_8F:
		return false;
	case LTC4283_RESERVED_91 ... LTC4283_RESERVED_A1:
		return false;
	case LTC4283_RESERVED_A3:
		return false;
	case LTC4283_RESERVED_AC:
		return false;
	case LTC4283_POWER_PLAY_MSB ... LTC4283_POWER_PLAY_LSB:
		return false;
	case LTC4283_RESERVED_F1 ... LTC4283_RESERVED_FF:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config ltc4283_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 0xFF,
	.writeable_reg = ltc4283_writable_reg,
};

static int ltc4283_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev, *hwmon;
	struct auxiliary_device *adev;
	struct ltc4283_hwmon *st;
	int ret, id;

	st = devm_kzalloc(dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA |
				     I2C_FUNC_SMBUS_READ_I2C_BLOCK))
		return -EOPNOTSUPP;

	st->client = client;
	st->map = devm_regmap_init(dev, &ltc4283_regmap_bus, client,
				   &ltc4283_regmap_config);
	if (IS_ERR(st->map))
		return dev_err_probe(dev, PTR_ERR(st->map),
				     "Failed to create regmap\n");

	ret = ltc4283_setup(st, dev);
	if (ret)
		return ret;

	hwmon = devm_hwmon_device_register_with_info(dev, "ltc4283", st,
						     &ltc4283_chip_info, NULL);

	if (IS_ERR(hwmon))
		return PTR_ERR(hwmon);

	ltc4283_debugfs_init(st, client);

	if (!st->gpio_mask)
		return 0;

	id = (client->adapter->nr << 10) | client->addr;
	adev = __devm_auxiliary_device_create(dev, KBUILD_MODNAME, "gpio",
					      &st->gpio_mask, id);
	if (!adev)
		return dev_err_probe(dev, -ENODEV, "Failed to add GPIO device\n");

	return 0;
}

static const struct of_device_id ltc4283_of_match[] = {
	{ .compatible = "adi,ltc4283" },
	{ }
};

static const struct i2c_device_id ltc4283_i2c_id[] = {
	{ "ltc4283" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltc4283_i2c_id);

static struct i2c_driver ltc4283_driver = {
	.driver	= {
		.name = "ltc4283",
		.of_match_table = ltc4283_of_match,
	},
	.probe = ltc4283_probe,
	.id_table = ltc4283_i2c_id,
};
module_i2c_driver(ltc4283_driver);

MODULE_AUTHOR("Nuno Sá <nuno.sa@analog.com>");
MODULE_DESCRIPTION("LTC4283 Hot Swap Controller driver");
MODULE_LICENSE("GPL");
