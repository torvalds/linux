// SPDX-License-Identifier: GPL-2.0+
/*
 * HWMON driver for MCP998X/33 and MCP998XD/33D Multichannel Automotive
 * Temperature Monitor Family
 *
 * Copyright (C) 2026 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Victor Duicu <victor.duicu@microchip.com>
 *
 * Datasheet can be found here:
 * https://ww1.microchip.com/downloads/aemDocuments/documents/MSLD/ProductDocuments/DataSheets/MCP998X-Family-Data-Sheet-DS20006827.pdf
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/byteorder/generic.h>
#include <linux/delay.h>
#include <linux/device/devres.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/time64.h>
#include <linux/util_macros.h>

/* MCP9982 Registers */
#define MCP9982_HIGH_BYTE_ADDR(index)		(2 * (index))
#define MCP9982_ONE_SHOT_ADDR			0x0A
#define MCP9982_INTERNAL_HIGH_LIMIT_ADDR	0x0B
#define MCP9982_INTERNAL_LOW_LIMIT_ADDR		0x0C
#define MCP9982_EXT_HIGH_LIMIT_ADDR(index)	(4 * ((index) - 1) + 0x0D)
#define MCP9982_EXT_LOW_LIMIT_ADDR(index)	(4 * ((index) - 1) + 0x0F)
#define MCP9982_THERM_LIMIT_ADDR(index)		((index) + 0x1D)
#define MCP9982_CFG_ADDR			0x22
#define MCP9982_CONV_ADDR			0x24
#define MCP9982_HYS_ADDR			0x25
#define MCP9982_CONSEC_ALRT_ADDR		0x26
#define MCP9982_ALRT_CFG_ADDR			0x27
#define MCP9982_RUNNING_AVG_ADDR		0x28
#define MCP9982_HOTTEST_CFG_ADDR		0x29
#define MCP9982_STATUS_ADDR			0x2A
#define MCP9982_EXT_FAULT_STATUS_ADDR		0x2B
#define MCP9982_HIGH_LIMIT_STATUS_ADDR		0x2C
#define MCP9982_LOW_LIMIT_STATUS_ADDR		0x2D
#define MCP9982_THERM_LIMIT_STATUS_ADDR		0x2E
#define MCP9982_HOTTEST_HIGH_BYTE_ADDR		0x2F
#define MCP9982_HOTTEST_LOW_BYTE_ADDR		0x30
#define MCP9982_HOTTEST_STATUS_ADDR		0x31
#define MCP9982_THERM_SHTDWN_CFG_ADDR		0x32
#define MCP9982_HRDW_THERM_SHTDWN_LIMIT_ADDR	0x33
#define MCP9982_EXT_BETA_CFG_ADDR(index)	((index) + 0x33)
#define MCP9982_EXT_IDEAL_ADDR(index)		((index) + 0x35)

/* MCP9982 Bits */
#define MCP9982_CFG_MSKAL			BIT(7)
#define MCP9982_CFG_RS				BIT(6)
#define MCP9982_CFG_ATTHM			BIT(5)
#define MCP9982_CFG_RECD12			BIT(4)
#define MCP9982_CFG_RECD34			BIT(3)
#define MCP9982_CFG_RANGE			BIT(2)
#define MCP9982_CFG_DA_ENA			BIT(1)
#define MCP9982_CFG_APDD			BIT(0)

#define MCP9982_STATUS_BUSY			BIT(5)

/* Constants and default values */
#define MCP9982_MAX_NUM_CHANNELS		5
#define MCP9982_BETA_AUTODETECT			16
#define MCP9982_IDEALITY_DEFAULT		18
#define MCP9982_OFFSET				64
#define MCP9982_DEFAULT_CONSEC_ALRT_VAL		112
#define MCP9982_DEFAULT_HYS_VAL			10
#define MCP9982_DEFAULT_CONV_VAL		6
#define MCP9982_WAKE_UP_TIME_US			125000
#define MCP9982_WAKE_UP_TIME_MAX_US		130000
#define MCP9982_HIGH_LIMIT_DEFAULT		85000
#define MCP9982_LOW_LIMIT_DEFAULT		0

static const struct hwmon_channel_info * const mcp9985_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MIN |
			   HWMON_T_MIN_ALARM | HWMON_T_MAX | HWMON_T_MAX_ALARM |
			   HWMON_T_MAX_HYST | HWMON_T_CRIT | HWMON_T_CRIT_ALARM |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MIN |
			   HWMON_T_MIN_ALARM | HWMON_T_MAX | HWMON_T_MAX_ALARM |
			   HWMON_T_MAX_HYST | HWMON_T_CRIT | HWMON_T_CRIT_ALARM |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MIN |
			   HWMON_T_MIN_ALARM | HWMON_T_MAX | HWMON_T_MAX_ALARM |
			   HWMON_T_MAX_HYST | HWMON_T_CRIT | HWMON_T_CRIT_ALARM |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MIN |
			   HWMON_T_MIN_ALARM | HWMON_T_MAX | HWMON_T_MAX_ALARM |
			   HWMON_T_MAX_HYST | HWMON_T_CRIT | HWMON_T_CRIT_ALARM |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MIN |
			   HWMON_T_MIN_ALARM | HWMON_T_MAX | HWMON_T_MAX_ALARM |
			   HWMON_T_MAX_HYST | HWMON_T_CRIT | HWMON_T_CRIT_ALARM |
			   HWMON_T_CRIT_HYST),
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_UPDATE_INTERVAL),
	NULL
};

/**
 * struct mcp9982_features - features of a mcp9982 instance
 * @name:			chip's name
 * @phys_channels:		number of physical channels supported by the chip
 * @hw_thermal_shutdown:	presence of hardware thermal shutdown circuitry
 * @allow_apdd:			whether the chip supports enabling APDD
 * @has_recd34:			whether the chip has the channels that are affected by recd34
 */
struct mcp9982_features {
	const char	*name;
	u8		phys_channels;
	bool		hw_thermal_shutdown;
	bool		allow_apdd;
	bool		has_recd34;
};

static const struct mcp9982_features mcp9933_chip_config = {
	.name = "mcp9933",
	.phys_channels = 3,
	.hw_thermal_shutdown = false,
	.allow_apdd = true,
	.has_recd34 = false,
};

static const struct mcp9982_features mcp9933d_chip_config = {
	.name = "mcp9933d",
	.phys_channels = 3,
	.hw_thermal_shutdown = true,
	.allow_apdd = true,
	.has_recd34 = false,
};

static const struct mcp9982_features mcp9982_chip_config = {
	.name = "mcp9982",
	.phys_channels = 2,
	.hw_thermal_shutdown = false,
	.allow_apdd = false,
	.has_recd34 = false,
};

static const struct mcp9982_features mcp9982d_chip_config = {
	.name = "mcp9982d",
	.phys_channels = 2,
	.hw_thermal_shutdown = true,
	.allow_apdd = false,
	.has_recd34 = false,
};

static const struct mcp9982_features mcp9983_chip_config = {
	.name = "mcp9983",
	.phys_channels = 3,
	.hw_thermal_shutdown = false,
	.allow_apdd = false,
	.has_recd34 = true,
};

static const struct mcp9982_features mcp9983d_chip_config = {
	.name = "mcp9983d",
	.phys_channels = 3,
	.hw_thermal_shutdown = true,
	.allow_apdd = false,
	.has_recd34 = true,
};

static const struct mcp9982_features mcp9984_chip_config = {
	.name = "mcp9984",
	.phys_channels = 4,
	.hw_thermal_shutdown = false,
	.allow_apdd = true,
	.has_recd34 = true,
};

static const struct mcp9982_features mcp9984d_chip_config = {
	.name = "mcp9984d",
	.phys_channels = 4,
	.hw_thermal_shutdown = true,
	.allow_apdd = true,
	.has_recd34 = true,
};

static const struct mcp9982_features mcp9985_chip_config = {
	.name = "mcp9985",
	.phys_channels = 5,
	.hw_thermal_shutdown = false,
	.allow_apdd = true,
	.has_recd34 = true,
};

static const struct mcp9982_features mcp9985d_chip_config = {
	.name = "mcp9985d",
	.phys_channels = 5,
	.hw_thermal_shutdown = true,
	.allow_apdd = true,
	.has_recd34 = true,
};

static const unsigned int mcp9982_update_interval[11] = {
	16000, 8000, 4000, 2000, 1000, 500, 250, 125, 64, 32, 16
};

/* MCP9982 regmap configuration */
static const struct regmap_range mcp9982_regmap_wr_ranges[] = {
	regmap_reg_range(MCP9982_ONE_SHOT_ADDR, MCP9982_CFG_ADDR),
	regmap_reg_range(MCP9982_CONV_ADDR, MCP9982_HOTTEST_CFG_ADDR),
	regmap_reg_range(MCP9982_THERM_SHTDWN_CFG_ADDR, MCP9982_THERM_SHTDWN_CFG_ADDR),
	regmap_reg_range(MCP9982_EXT_BETA_CFG_ADDR(1), MCP9982_EXT_IDEAL_ADDR(4)),
};

static const struct regmap_access_table mcp9982_regmap_wr_table = {
	.yes_ranges = mcp9982_regmap_wr_ranges,
	.n_yes_ranges = ARRAY_SIZE(mcp9982_regmap_wr_ranges),
};

static const struct regmap_range mcp9982_regmap_rd_ranges[] = {
	regmap_reg_range(MCP9982_HIGH_BYTE_ADDR(0), MCP9982_CFG_ADDR),
	regmap_reg_range(MCP9982_CONV_ADDR, MCP9982_EXT_IDEAL_ADDR(4)),
};

static const struct regmap_access_table mcp9982_regmap_rd_table = {
	.yes_ranges = mcp9982_regmap_rd_ranges,
	.n_yes_ranges = ARRAY_SIZE(mcp9982_regmap_rd_ranges),
};

static bool mcp9982_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MCP9982_ONE_SHOT_ADDR:
	case MCP9982_INTERNAL_HIGH_LIMIT_ADDR:
	case MCP9982_INTERNAL_LOW_LIMIT_ADDR:
	case MCP9982_EXT_LOW_LIMIT_ADDR(1):
	case MCP9982_EXT_LOW_LIMIT_ADDR(1) + 1:
	case MCP9982_EXT_LOW_LIMIT_ADDR(2):
	case MCP9982_EXT_LOW_LIMIT_ADDR(2) + 1:
	case MCP9982_EXT_LOW_LIMIT_ADDR(3):
	case MCP9982_EXT_LOW_LIMIT_ADDR(3) + 1:
	case MCP9982_EXT_LOW_LIMIT_ADDR(4):
	case MCP9982_EXT_LOW_LIMIT_ADDR(4) + 1:
	case MCP9982_EXT_HIGH_LIMIT_ADDR(1):
	case MCP9982_EXT_HIGH_LIMIT_ADDR(1) + 1:
	case MCP9982_EXT_HIGH_LIMIT_ADDR(2):
	case MCP9982_EXT_HIGH_LIMIT_ADDR(2) + 1:
	case MCP9982_EXT_HIGH_LIMIT_ADDR(3):
	case MCP9982_EXT_HIGH_LIMIT_ADDR(3) + 1:
	case MCP9982_EXT_HIGH_LIMIT_ADDR(4):
	case MCP9982_EXT_HIGH_LIMIT_ADDR(4) + 1:
	case MCP9982_THERM_LIMIT_ADDR(0):
	case MCP9982_THERM_LIMIT_ADDR(1):
	case MCP9982_THERM_LIMIT_ADDR(2):
	case MCP9982_THERM_LIMIT_ADDR(3):
	case MCP9982_THERM_LIMIT_ADDR(4):
	case MCP9982_CFG_ADDR:
	case MCP9982_CONV_ADDR:
	case MCP9982_HYS_ADDR:
	case MCP9982_CONSEC_ALRT_ADDR:
	case MCP9982_ALRT_CFG_ADDR:
	case MCP9982_RUNNING_AVG_ADDR:
	case MCP9982_HOTTEST_CFG_ADDR:
	case MCP9982_THERM_SHTDWN_CFG_ADDR:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config mcp9982_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.rd_table = &mcp9982_regmap_rd_table,
	.wr_table = &mcp9982_regmap_wr_table,
	.volatile_reg = mcp9982_is_volatile_reg,
	.max_register = MCP9982_EXT_IDEAL_ADDR(4),
	.cache_type = REGCACHE_MAPLE,
};

/**
 * struct mcp9982_priv - information about chip parameters
 * @regmap:			device register map
 * @chip:			pointer to structure holding chip features
 * @labels:			labels of the channels
 * @interval_idx:		index representing the current update interval
 * @enabled_channel_mask:	mask containing which channels should be enabled
 * @num_channels:		number of active physical channels
 * @recd34_enable:		state of Resistance Error Correction(REC) on channels 3 and 4
 * @recd12_enable:		state of Resistance Error Correction(REC) on channels 1 and 2
 * @apdd_enable:		state of anti-parallel diode mode
 * @run_state:			chip is in Run state, otherwise is in Standby state
 */
struct mcp9982_priv {
	struct regmap *regmap;
	const struct mcp9982_features *chip;
	const char *labels[MCP9982_MAX_NUM_CHANNELS];
	unsigned int interval_idx;
	unsigned long enabled_channel_mask;
	u8 num_channels;
	bool recd34_enable;
	bool recd12_enable;
	bool apdd_enable;
	bool run_state;
};

static int mcp9982_read_limit(struct mcp9982_priv *priv, u8 address, long *val)
{
	unsigned int limit, reg_high, reg_low;
	int ret;

	switch (address) {
	case MCP9982_INTERNAL_HIGH_LIMIT_ADDR:
	case MCP9982_INTERNAL_LOW_LIMIT_ADDR:
	case MCP9982_THERM_LIMIT_ADDR(0):
	case MCP9982_THERM_LIMIT_ADDR(1):
	case MCP9982_THERM_LIMIT_ADDR(2):
	case MCP9982_THERM_LIMIT_ADDR(3):
	case MCP9982_THERM_LIMIT_ADDR(4):
		ret = regmap_read(priv->regmap, address, &limit);
		if (ret)
			return ret;

		*val = ((int)limit - MCP9982_OFFSET) * 1000;

		return 0;
	case MCP9982_EXT_HIGH_LIMIT_ADDR(1):
	case MCP9982_EXT_HIGH_LIMIT_ADDR(2):
	case MCP9982_EXT_HIGH_LIMIT_ADDR(3):
	case MCP9982_EXT_HIGH_LIMIT_ADDR(4):
	case MCP9982_EXT_LOW_LIMIT_ADDR(1):
	case MCP9982_EXT_LOW_LIMIT_ADDR(2):
	case MCP9982_EXT_LOW_LIMIT_ADDR(3):
	case MCP9982_EXT_LOW_LIMIT_ADDR(4):
		/*
		 * In order to keep consistency with reading temperature memory region we will use
		 * single byte I2C read.
		 */
		ret = regmap_read(priv->regmap, address, &reg_high);
		if (ret)
			return ret;

		ret = regmap_read(priv->regmap, address + 1, &reg_low);
		if (ret)
			return ret;

		*val = ((reg_high << 8) + reg_low) >> 5;
		*val = (*val - (MCP9982_OFFSET << 3)) * 125;

		return 0;
	default:
		return -EINVAL;
	}
}

static int mcp9982_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			long *val)
{
	struct mcp9982_priv *priv = dev_get_drvdata(dev);
	unsigned int reg_high, reg_low, hyst, reg_status;
	int ret;
	u8 addr;

	/*
	 * In Standby State the conversion cycle must be initated manually in
	 * order to read fresh temperature values and the status of the alarms.
	 */
	if (!priv->run_state) {
		switch (type) {
		case hwmon_temp:
			switch (attr) {
			case hwmon_temp_input:
			case hwmon_temp_max_alarm:
			case hwmon_temp_min_alarm:
			case hwmon_temp_crit_alarm:
				ret = regmap_write(priv->regmap, MCP9982_ONE_SHOT_ADDR, 1);
				if (ret)
					return ret;
				/*
				 * When the device is in Standby mode, 125 ms need
				 * to pass from writing in One Shot register before
				 * the conversion cycle begins.
				 */
				usleep_range(MCP9982_WAKE_UP_TIME_US, MCP9982_WAKE_UP_TIME_MAX_US);
				ret = regmap_read_poll_timeout
					       (priv->regmap, MCP9982_STATUS_ADDR,
					       reg_status, !(reg_status & MCP9982_STATUS_BUSY),
					       MCP9982_WAKE_UP_TIME_US,
					       MCP9982_WAKE_UP_TIME_US * 10);
				break;
			}
			break;
		default:
			break;
		}
	}

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			/*
			 * The only areas of memory that support SMBus block read are 80h->89h
			 * (temperature memory block) and 90h->97h(status memory block).
			 * In this context the read operation uses SMBus protocol and the first
			 * value returned will be the number of addresses that can be read.
			 * Temperature memory block is 10 bytes long and status memory block is 8
			 * bytes long.
			 *
			 * Depending on the read instruction used, the chip behaves differently:
			 * - regmap_bulk_read() when applied to the temperature memory block
			 * (80h->89h), the chip replies with SMBus block read, including count,
			 * additionally to the high and the low bytes. This function cannot be
			 * applied on the memory region 00h->09h(memory area which does not support
			 * block reads, returns wrong data) unless use_single_read is set in
			 * regmap_config.
			 *
			 * - regmap_multi_reg_read() when applied to the 00h->09h area uses I2C
			 * and returns only the high and low temperature bytes. When applied to
			 * the temperature memory block (80h->89h) returns the count till the end of
			 * the temperature memory block(aka SMBus count).
			 *
			 * - i2c_smbus_read_block_data() is not supported by all drivers.
			 *
			 * In order to keep consistency with reading limit memory region we will
			 * use single byte I2C read.
			 *
			 * Low register is latched when high temperature register is read.
			 */
			ret = regmap_read(priv->regmap, MCP9982_HIGH_BYTE_ADDR(channel), &reg_high);
			if (ret)
				return ret;

			ret = regmap_read(priv->regmap, MCP9982_HIGH_BYTE_ADDR(channel) + 1,
					  &reg_low);
			if (ret)
				return ret;

			*val = ((reg_high << 8) + reg_low) >> 5;
			*val = (*val - (MCP9982_OFFSET << 3)) * 125;

			return 0;
		case hwmon_temp_max:
			if (channel)
				addr = MCP9982_EXT_HIGH_LIMIT_ADDR(channel);
			else
				addr = MCP9982_INTERNAL_HIGH_LIMIT_ADDR;

			return mcp9982_read_limit(priv, addr, val);
		case hwmon_temp_max_alarm:
			*val = regmap_test_bits(priv->regmap, MCP9982_HIGH_LIMIT_STATUS_ADDR,
						BIT(channel));
			if (*val < 0)
				return *val;

			return 0;
		case hwmon_temp_max_hyst:
			if (channel)
				addr = MCP9982_EXT_HIGH_LIMIT_ADDR(channel);
			else
				addr = MCP9982_INTERNAL_HIGH_LIMIT_ADDR;
			ret = mcp9982_read_limit(priv, addr, val);
			if (ret)
				return ret;

			ret = regmap_read(priv->regmap, MCP9982_HYS_ADDR, &hyst);
			if (ret)
				return ret;

			*val -= hyst * 1000;

			return 0;
		case hwmon_temp_min:
			if (channel)
				addr = MCP9982_EXT_LOW_LIMIT_ADDR(channel);
			else
				addr = MCP9982_INTERNAL_LOW_LIMIT_ADDR;

			return mcp9982_read_limit(priv, addr, val);
		case hwmon_temp_min_alarm:
			*val = regmap_test_bits(priv->regmap, MCP9982_LOW_LIMIT_STATUS_ADDR,
						BIT(channel));
			if (*val < 0)
				return *val;

			return 0;
		case hwmon_temp_crit:
			return mcp9982_read_limit(priv, MCP9982_THERM_LIMIT_ADDR(channel), val);
		case hwmon_temp_crit_alarm:
			*val = regmap_test_bits(priv->regmap, MCP9982_THERM_LIMIT_STATUS_ADDR,
						BIT(channel));
			if (*val < 0)
				return *val;

			return 0;
		case hwmon_temp_crit_hyst:
			ret = mcp9982_read_limit(priv, MCP9982_THERM_LIMIT_ADDR(channel), val);
			if (ret)
				return ret;

			ret = regmap_read(priv->regmap, MCP9982_HYS_ADDR, &hyst);
			if (ret)
				return ret;

			*val -= hyst * 1000;

			return 0;
		default:
			return -EINVAL;
		}
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			*val = mcp9982_update_interval[priv->interval_idx];
			return 0;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int mcp9982_read_label(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			      int channel, const char **str)
{
	struct mcp9982_priv *priv = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_label:
			*str = priv->labels[channel];
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static int mcp9982_write_limit(struct mcp9982_priv *priv, u8 address, long val)
{
	int ret;
	unsigned int regh, regl;

	switch (address) {
	case MCP9982_INTERNAL_HIGH_LIMIT_ADDR:
	case MCP9982_INTERNAL_LOW_LIMIT_ADDR:
	case MCP9982_THERM_LIMIT_ADDR(0):
	case MCP9982_THERM_LIMIT_ADDR(1):
	case MCP9982_THERM_LIMIT_ADDR(2):
	case MCP9982_THERM_LIMIT_ADDR(3):
	case MCP9982_THERM_LIMIT_ADDR(4):
		regh = DIV_ROUND_CLOSEST(val, 1000);
		regh = clamp_val(regh, 0, 255);

		return regmap_write(priv->regmap, address, regh);
	case MCP9982_EXT_HIGH_LIMIT_ADDR(1):
	case MCP9982_EXT_HIGH_LIMIT_ADDR(2):
	case MCP9982_EXT_HIGH_LIMIT_ADDR(3):
	case MCP9982_EXT_HIGH_LIMIT_ADDR(4):
	case MCP9982_EXT_LOW_LIMIT_ADDR(1):
	case MCP9982_EXT_LOW_LIMIT_ADDR(2):
	case MCP9982_EXT_LOW_LIMIT_ADDR(3):
	case MCP9982_EXT_LOW_LIMIT_ADDR(4):
		val = DIV_ROUND_CLOSEST(val, 125);
		regh = (val >> 3) & 0xff;
		regl = (val & 0x07) << 5;
		/* Block writing is not supported by the chip. */
		ret = regmap_write(priv->regmap, address, regh);
		if (ret)
			return ret;

		return regmap_write(priv->regmap, address + 1, regl);
	default:
		return -EINVAL;
	}
}

static int mcp9982_write_hyst(struct mcp9982_priv *priv, int channel, long val)
{
	int hyst, ret;
	int limit;

	val = DIV_ROUND_CLOSEST(val, 1000);
	val = clamp_val(val, 0, 255);

	/* Therm register is 8 bits and so it keeps only the integer part of the temperature. */
	ret = regmap_read(priv->regmap, MCP9982_THERM_LIMIT_ADDR(channel), &limit);
	if (ret)
		return ret;

	hyst = clamp_val(limit - val, 0, 255);

	return regmap_write(priv->regmap, MCP9982_HYS_ADDR, hyst);
}

static int mcp9982_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			 long val)
{
	struct mcp9982_priv *priv = dev_get_drvdata(dev);
	unsigned int idx;
	u8 addr;

	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:

			/*
			 * For MCP998XD and MCP9933D update interval
			 * can't be longer than 1 second.
			 */
			if (priv->chip->hw_thermal_shutdown)
				val = clamp_val(val, 0, 1000);

			idx = find_closest_descending(val, mcp9982_update_interval,
						      ARRAY_SIZE(mcp9982_update_interval));
			priv->interval_idx = idx;

			return regmap_write(priv->regmap, MCP9982_CONV_ADDR, idx);
		default:
			return -EINVAL;
		}
	case hwmon_temp:
		val = clamp_val(val, -64000, 191875);
		val = val + (MCP9982_OFFSET * 1000);
		switch (attr) {
		case hwmon_temp_max:
			if (channel)
				addr = MCP9982_EXT_HIGH_LIMIT_ADDR(channel);
			else
				addr = MCP9982_INTERNAL_HIGH_LIMIT_ADDR;

			return mcp9982_write_limit(priv, addr, val);
		case hwmon_temp_min:
			if (channel)
				addr = MCP9982_EXT_LOW_LIMIT_ADDR(channel);
			else
				addr = MCP9982_INTERNAL_LOW_LIMIT_ADDR;

			return mcp9982_write_limit(priv, addr, val);
		case hwmon_temp_crit:
			return mcp9982_write_limit(priv, MCP9982_THERM_LIMIT_ADDR(channel), val);
		case hwmon_temp_crit_hyst:
			return mcp9982_write_hyst(priv, channel, val);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static umode_t mcp9982_is_visible(const void *_data, enum hwmon_sensor_types type, u32 attr,
				  int channel)
{
	const struct mcp9982_priv *priv = _data;

	if (!test_bit(channel, &priv->enabled_channel_mask))
		return 0;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_label:
			if (priv->labels[channel])
				return 0444;
			else
				return 0;
		case hwmon_temp_input:
		case hwmon_temp_min_alarm:
		case hwmon_temp_max_alarm:
		case hwmon_temp_max_hyst:
		case hwmon_temp_crit_alarm:
			return 0444;
		case hwmon_temp_min:
		case hwmon_temp_max:
		case hwmon_temp_crit:
		case hwmon_temp_crit_hyst:
			return 0644;
		default:
			return 0;
		}
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			return 0644;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

static const struct hwmon_ops mcp9982_hwmon_ops = {
	.is_visible = mcp9982_is_visible,
	.read = mcp9982_read,
	.read_string = mcp9982_read_label,
	.write = mcp9982_write,
};

static int mcp9982_init(struct device *dev, struct mcp9982_priv *priv)
{
	long high_limit, low_limit;
	unsigned int i;
	int ret;
	u8 val;

	/* Chips 82/83 and 82D/83D do not support anti-parallel diode mode. */
	if (!priv->chip->allow_apdd && priv->apdd_enable == 1)
		return dev_err_probe(dev, -EINVAL, "Incorrect setting of APDD.\n");

	/* Chips with "D" work only in Run state. */
	if (priv->chip->hw_thermal_shutdown && !priv->run_state)
		return dev_err_probe(dev, -EINVAL, "Incorrect setting of Power State.\n");

	/* All chips with "D" in the name must have RECD12 enabled. */
	if (priv->chip->hw_thermal_shutdown && !priv->recd12_enable)
		return dev_err_probe(dev, -EINVAL, "Incorrect setting of RECD12.\n");
	/* Chips 83D/84D/85D must have RECD34 enabled. */
	if (priv->chip->hw_thermal_shutdown)
		if ((priv->chip->has_recd34 && !priv->recd34_enable))
			return dev_err_probe(dev, -EINVAL, "Incorrect setting of RECD34.\n");

	/*
	 * Set default values in registers.
	 * APDD, RECD12 and RECD34 are active on 0.
	 */
	val = FIELD_PREP(MCP9982_CFG_MSKAL, 1) |
	      FIELD_PREP(MCP9982_CFG_RS, !priv->run_state) |
	      FIELD_PREP(MCP9982_CFG_ATTHM, 1) |
	      FIELD_PREP(MCP9982_CFG_RECD12, !priv->recd12_enable) |
	      FIELD_PREP(MCP9982_CFG_RECD34, !priv->recd34_enable) |
	      FIELD_PREP(MCP9982_CFG_RANGE, 1) | FIELD_PREP(MCP9982_CFG_DA_ENA, 0) |
	      FIELD_PREP(MCP9982_CFG_APDD, !priv->apdd_enable);

	ret = regmap_write(priv->regmap, MCP9982_CFG_ADDR, val);
	if (ret)
		return ret;

	/*
	 * Read initial value from register.
	 * The convert register utilises only 4 out of 8 bits.
	 * Numerical values 0->10 set their respective update intervals,
	 * while numerical values 11->15 default to 1 second.
	 */
	ret = regmap_read(priv->regmap, MCP9982_CONV_ADDR, &priv->interval_idx);
	if (ret)
		return ret;
	if (priv->interval_idx >= 11)
		priv->interval_idx = 4;

	ret = regmap_write(priv->regmap, MCP9982_HYS_ADDR, MCP9982_DEFAULT_HYS_VAL);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, MCP9982_CONSEC_ALRT_ADDR, MCP9982_DEFAULT_CONSEC_ALRT_VAL);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, MCP9982_ALRT_CFG_ADDR, 0);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, MCP9982_RUNNING_AVG_ADDR, 0);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, MCP9982_HOTTEST_CFG_ADDR, 0);
	if (ret)
		return ret;

	/*
	 * Only external channels 1 and 2 support beta compensation.
	 * Set beta auto-detection.
	 */
	for (i = 1; i < 3; i++)
		if (test_bit(i, &priv->enabled_channel_mask)) {
			ret = regmap_write(priv->regmap, MCP9982_EXT_BETA_CFG_ADDR(i),
					   MCP9982_BETA_AUTODETECT);
			if (ret)
				return ret;
		}

	high_limit = MCP9982_HIGH_LIMIT_DEFAULT + (MCP9982_OFFSET * 1000);
	low_limit = MCP9982_LOW_LIMIT_DEFAULT + (MCP9982_OFFSET * 1000);

	/* Set default values for internal channel limits. */
	if (test_bit(0, &priv->enabled_channel_mask)) {
		ret = mcp9982_write_limit(priv, MCP9982_INTERNAL_HIGH_LIMIT_ADDR, high_limit);
		if (ret)
			return ret;

		ret = mcp9982_write_limit(priv, MCP9982_INTERNAL_LOW_LIMIT_ADDR, low_limit);
		if (ret)
			return ret;

		ret = mcp9982_write_limit(priv, MCP9982_THERM_LIMIT_ADDR(0), high_limit);
		if (ret)
			return ret;
	}

	/* Set ideality factor and limits to default for external channels. */
	for (i = 1; i < MCP9982_MAX_NUM_CHANNELS; i++)
		if (test_bit(i, &priv->enabled_channel_mask)) {
			ret = regmap_write(priv->regmap, MCP9982_EXT_IDEAL_ADDR(i),
					   MCP9982_IDEALITY_DEFAULT);
			if (ret)
				return ret;

			ret = mcp9982_write_limit(priv, MCP9982_EXT_HIGH_LIMIT_ADDR(i), high_limit);
			if (ret)
				return ret;

			ret = mcp9982_write_limit(priv, MCP9982_EXT_LOW_LIMIT_ADDR(i), low_limit);
			if (ret)
				return ret;

			ret = mcp9982_write_limit(priv, MCP9982_THERM_LIMIT_ADDR(i), high_limit);
			if (ret)
				return ret;
		}

	return 0;
}

static int mcp9982_parse_fw_config(struct device *dev, int device_nr_channels)
{
	struct mcp9982_priv *priv = dev_get_drvdata(dev);
	unsigned int reg_nr;
	int ret;

	/* Initialise internal channel( which is always present ). */
	priv->labels[0] = "internal diode";
	priv->enabled_channel_mask = 1;

	/* Default values to work on systems without devicetree or firmware nodes. */
	if (!dev_fwnode(dev)) {
		priv->num_channels = device_nr_channels;
		priv->enabled_channel_mask = BIT(priv->num_channels) - 1;
		priv->apdd_enable = false;
		priv->recd12_enable = true;
		priv->recd34_enable = true;
		priv->run_state = true;
		return 0;
	}

	priv->apdd_enable =
		device_property_read_bool(dev, "microchip,enable-anti-parallel");

	priv->recd12_enable =
		device_property_read_bool(dev, "microchip,parasitic-res-on-channel1-2");

	priv->recd34_enable =
		device_property_read_bool(dev, "microchip,parasitic-res-on-channel3-4");

	priv->run_state =
		device_property_read_bool(dev, "microchip,power-state");

	priv->num_channels = device_get_child_node_count(dev) + 1;

	if (priv->num_channels > device_nr_channels)
		return dev_err_probe(dev, -EINVAL,
				     "More channels than the chip supports.\n");

	/* Read information about the external channels. */
	device_for_each_named_child_node_scoped(dev, child, "channel") {
		reg_nr = 0;
		ret = fwnode_property_read_u32(child, "reg", &reg_nr);
		if (ret || !reg_nr || reg_nr >= device_nr_channels)
			return dev_err_probe(dev, -EINVAL,
			  "Channel reg is incorrectly set.\n");

		fwnode_property_read_string(child, "label", &priv->labels[reg_nr]);
		set_bit(reg_nr, &priv->enabled_channel_mask);
	}

	return 0;
}

static const struct hwmon_chip_info mcp998x_chip_info = {
	.ops = &mcp9982_hwmon_ops,
	.info = mcp9985_info,
};

static int mcp9982_probe(struct i2c_client *client)
{
	const struct mcp9982_features *chip;
	struct device *dev = &client->dev;
	struct mcp9982_priv *priv;
	struct device *hwmon_dev;
	int ret;

	priv = devm_kzalloc(dev, sizeof(struct mcp9982_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = devm_regmap_init_i2c(client, &mcp9982_regmap_config);

	if (IS_ERR(priv->regmap))
		return dev_err_probe(dev, PTR_ERR(priv->regmap),
				     "Cannot initialize register map.\n");

	dev_set_drvdata(dev, priv);

	chip = i2c_get_match_data(client);
	if (!chip)
		return -EINVAL;
	priv->chip = chip;

	ret = mcp9982_parse_fw_config(dev, chip->phys_channels);
	if (ret)
		return ret;

	ret = mcp9982_init(dev, priv);
	if (ret)
		return ret;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, chip->name, priv,
							 &mcp998x_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id mcp9982_id[] = {
	{ .name = "mcp9933", .driver_data = (kernel_ulong_t)&mcp9933_chip_config },
	{ .name = "mcp9933d", .driver_data = (kernel_ulong_t)&mcp9933d_chip_config },
	{ .name = "mcp9982", .driver_data = (kernel_ulong_t)&mcp9982_chip_config },
	{ .name = "mcp9982d", .driver_data = (kernel_ulong_t)&mcp9982d_chip_config },
	{ .name = "mcp9983", .driver_data = (kernel_ulong_t)&mcp9983_chip_config },
	{ .name = "mcp9983d", .driver_data = (kernel_ulong_t)&mcp9983d_chip_config },
	{ .name = "mcp9984", .driver_data = (kernel_ulong_t)&mcp9984_chip_config },
	{ .name = "mcp9984d", .driver_data = (kernel_ulong_t)&mcp9984d_chip_config },
	{ .name = "mcp9985", .driver_data = (kernel_ulong_t)&mcp9985_chip_config },
	{ .name = "mcp9985d", .driver_data = (kernel_ulong_t)&mcp9985d_chip_config },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcp9982_id);

static const struct of_device_id mcp9982_of_match[] = {
	{
		.compatible = "microchip,mcp9933",
		.data = &mcp9933_chip_config,
	}, {
		.compatible = "microchip,mcp9933d",
		.data = &mcp9933d_chip_config,
	}, {
		.compatible = "microchip,mcp9982",
		.data = &mcp9982_chip_config,
	}, {
		.compatible = "microchip,mcp9982d",
		.data = &mcp9982d_chip_config,
	}, {
		.compatible = "microchip,mcp9983",
		.data = &mcp9983_chip_config,
	}, {
		.compatible = "microchip,mcp9983d",
		.data = &mcp9983d_chip_config,
	}, {
		.compatible = "microchip,mcp9984",
		.data = &mcp9984_chip_config,
	}, {
		.compatible = "microchip,mcp9984d",
		.data = &mcp9984d_chip_config,
	}, {
		.compatible = "microchip,mcp9985",
		.data = &mcp9985_chip_config,
	}, {
		.compatible = "microchip,mcp9985d",
		.data = &mcp9985d_chip_config,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, mcp9982_of_match);

static struct i2c_driver mcp9982_driver = {
	.driver	 = {
		.name = "mcp9982",
		.of_match_table = mcp9982_of_match,
	},
	.probe = mcp9982_probe,
	.id_table = mcp9982_id,
};
module_i2c_driver(mcp9982_driver);

MODULE_AUTHOR("Victor Duicu <victor.duicu@microchip.com>");
MODULE_DESCRIPTION("MCP998X/33 and MCP998XD/33D Multichannel Automotive Temperature Monitor Driver");
MODULE_LICENSE("GPL");
