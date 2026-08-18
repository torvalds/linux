// SPDX-License-Identifier: GPL-2.0+
/*
 * HWMON driver for Microchip EMC1812/13/14/15/33 Multichannel high-accuracy
 * 2-wire low-voltage remote diode temperature monitor family.
 *
 * Copyright (C) 2026 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Marius Cristea <marius.cristea@microchip.com>
 *
 * Datasheet can be found here:
 * https://ww1.microchip.com/downloads/aemDocuments/documents/MSLD/ProductDocuments/DataSheets/EMC1812-3-4-5-33-Data-Sheet-DS20005751.pdf
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/string.h>
#include <linux/units.h>
#include <linux/util_macros.h>

/* EMC1812 Registers Addresses */
#define EMC1812_STATUS_ADDR				0x02
#define EMC1812_CONFIG_LO_ADDR				0x03

#define EMC1812_CFG_ADDR				0x09
#define EMC1812_CONV_ADDR				0x0A
#define EMC1812_INT_DIODE_HIGH_LIMIT_ADDR		0x0B
#define EMC1812_INT_DIODE_LOW_LIMIT_ADDR		0x0C
#define EMC1812_EXT1_HIGH_LIMIT_HIGH_BYTE_ADDR		0x0D
#define EMC1812_EXT1_LOW_LIMIT_HIGH_BYTE_ADDR		0x0E
#define EMC1812_ONE_SHOT_ADDR				0x0F

#define EMC1812_EXT1_HIGH_LIMIT_LOW_BYTE_ADDR		0x13
#define EMC1812_EXT1_LOW_LIMIT_LOW_BYTE_ADDR		0x14
#define EMC1812_EXT2_HIGH_LIMIT_HIGH_BYTE_ADDR		0x15
#define EMC1812_EXT2_LOW_LIMIT_HIGH_BYTE_ADDR		0x16
#define EMC1812_EXT2_HIGH_LIMIT_LOW_BYTE_ADDR		0x17
#define EMC1812_EXT2_LOW_LIMIT_LOW_BYTE_ADDR		0x18
#define EMC1812_EXT1_THERM_LIMIT_ADDR			0x19
#define EMC1812_EXT2_THERM_LIMIT_ADDR			0x1A
#define EMC1812_EXT_DIODE_FAULT_STATUS_ADDR		0x1B

#define EMC1812_DIODE_FAULT_MASK_ADDR			0x1F
#define EMC1812_INT_DIODE_THERM_LIMIT_ADDR		0x20
#define EMC1812_THRM_HYS_ADDR				0x21
#define EMC1812_CONSEC_ALERT_ADDR			0x22

#define EMC1812_EXT1_BETA_CONFIG_ADDR			0x25
#define EMC1812_EXT2_BETA_CONFIG_ADDR			0x26
#define EMC1812_EXT1_IDEALITY_FACTOR_ADDR		0x27
#define EMC1812_EXT2_IDEALITY_FACTOR_ADDR		0x28

#define EMC1812_EXT3_HIGH_LIMIT_HIGH_BYTE_ADDR		0x2C
#define EMC1812_EXT3_LOW_LIMIT_HIGH_BYTE_ADDR		0x2D
#define EMC1812_EXT3_HIGH_LIMIT_LOW_BYTE_ADDR		0x2E
#define EMC1812_EXT3_LOW_LIMIT_LOW_BYTE_ADDR		0x2F
#define EMC1812_EXT3_THERM_LIMIT_ADDR			0x30
#define EMC1812_EXT3_IDEALITY_FACTOR_ADDR		0x31

#define EMC1812_EXT4_HIGH_LIMIT_HIGH_BYTE_ADDR		0x34
#define EMC1812_EXT4_LOW_LIMIT_HIGH_BYTE_ADDR		0x35
#define EMC1812_EXT4_HIGH_LIMIT_LOW_BYTE_ADDR		0x36
#define EMC1812_EXT4_LOW_LIMIT_LOW_BYTE_ADDR		0x37
#define EMC1812_EXT4_THERM_LIMIT_ADDR			0x38
#define EMC1812_EXT4_IDEALITY_FACTOR_ADDR		0x39
#define EMC1812_HIGH_LIMIT_STATUS_ADDR			0x3A
#define EMC1812_LOW_LIMIT_STATUS_ADDR			0x3B
#define EMC1812_THERM_LIMIT_STATUS_ADDR			0x3C
#define EMC1812_ROC_GAIN_ADDR				0x3D
#define EMC1812_ROC_CONFIG_ADDR				0x3E
#define EMC1812_ROC_STATUS_ADDR				0x3F
#define EMC1812_R1_RESH_ADDR				0x40
#define EMC1812_R1_LIMH_ADDR				0x41
#define EMC1812_R1_LIML_ADDR				0x42
#define EMC1812_R1_SMPL_ADDR				0x43
#define EMC1812_R2_RESH_ADDR				0x44
#define EMC1812_R2_3_RESL_ADDR				0x45
#define EMC1812_R2_LIMH_ADDR				0x46
#define EMC1812_R2_LIML_ADDR				0x47
#define EMC1812_R2_SMPL_ADDR				0x48
#define EMC1812_PER_MAXTH_1_ADDR			0x49
#define EMC1812_PER_MAXT1L_ADDR				0x4A
#define EMC1812_PER_MAXTH_2_ADDR			0x4B
#define EMC1812_PER_MAXT2_3L_ADDR			0x4C
#define EMC1812_GBL_MAXT1H_ADDR				0x4D
#define EMC1812_GBL_MAXT1L_ADDR				0x4E
#define EMC1812_GBL_MAXT2H_ADDR				0x4F
#define EMC1812_GBL_MAXT2L_ADDR				0x50
#define EMC1812_FILTER_SEL_ADDR				0x51

#define EMC1812_INT_HIGH_BYTE_ADDR		0x60
#define EMC1812_INT_LOW_BYTE_ADDR		0x61
#define EMC1812_EXT1_HIGH_BYTE_ADDR		0x62
#define EMC1812_EXT1_LOW_BYTE_ADDR		0x63
#define EMC1812_EXT2_HIGH_BYTE_ADDR		0x64
#define EMC1812_EXT2_LOW_BYTE_ADDR		0x65
#define EMC1812_EXT3_HIGH_BYTE_ADDR		0x66
#define EMC1812_EXT3_LOW_BYTE_ADDR		0x67
#define EMC1812_EXT4_HIGH_BYTE_ADDR		0x68
#define EMC1812_EXT4_LOW_BYTE_ADDR		0x69
#define EMC1812_HOTTEST_DIODE_HIGH_BYTE_ADDR	0x6A
#define EMC1812_HOTTEST_DIODE_LOW_BYTE_ADDR	0x6B
#define EMC1812_HOTTEST_STATUS_ADDR		0x6C
#define EMC1812_HOTTEST_CFG_ADDR		0x6D

#define EMC1812_PRODUCT_ID_ADDR		0xFD
#define EMC1812_MANUFACTURER_ID_ADDR	0xFE
#define EMC1812_REVISION_ADDR		0xFF

/* EMC1812 Config Bits */
#define EMC1812_CFG_MSKAL		BIT(7)
#define EMC1812_CFG_RS			BIT(6)
#define EMC1812_CFG_ATTHM		BIT(5)
#define EMC1812_CFG_RECD12		BIT(4)
#define EMC1812_CFG_RECD34		BIT(3)
#define EMC1812_CFG_RANGE		BIT(2)
#define EMC1812_CFG_DA_ENA		BIT(1)
#define EMC1812_CFG_APDD		BIT(0)

/* EMC1812 Status Bits */
#define EMC1812_STATUS_ROCF		BIT(7)
#define EMC1812_STATUS_HOTCHG		BIT(6)
#define EMC1812_STATUS_BUSY		BIT(5)
#define EMC1812_STATUS_HIGH		BIT(4)
#define EMC1812_STATUS_LOW		BIT(3)
#define EMC1812_STATUS_FAULT		BIT(2)
#define EMC1812_STATUS_ETHRM		BIT(1)
#define EMC1812_STATUS_ITHRM		BIT(0)

#define EMC1812_BETA_LOCK_VAL		0x0F

#define EMC1812_TEMP_CH_ADDR(index)	(EMC1812_INT_HIGH_BYTE_ADDR + 2 * (index))

#define EMC1812_FILTER_MASK_LEN		2

#define EMC1812_PID			0x81
#define EMC1813_PID			0x87
#define EMC1814_PID			0x84
#define EMC1815_PID			0x85
#define EMC1833_PID			0x83

/* The maximum number of channels a member of the family can have */
#define EMC1812_MAX_NUM_CHANNELS		5
#define EMC1812_TEMP_OFFSET			64

#define EMC1812_DEFAULT_IDEALITY_FACTOR		0x12

/* Constants and default values */
#define EMC1812_HIGH_LIMIT_DEFAULT		(85 + EMC1812_TEMP_OFFSET)

#define EMC1812_TEMP_MASK (HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX | \
			   HWMON_T_CRIT | HWMON_T_MAX_HYST | HWMON_T_CRIT_HYST | \
			   HWMON_T_MIN_ALARM | HWMON_T_MAX_ALARM | \
			   HWMON_T_CRIT_ALARM | HWMON_T_LABEL)

static const struct hwmon_channel_info * const emc1812_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp,
			   EMC1812_TEMP_MASK,
			   EMC1812_TEMP_MASK | HWMON_T_FAULT,
			   EMC1812_TEMP_MASK | HWMON_T_FAULT,
			   EMC1812_TEMP_MASK | HWMON_T_FAULT,
			   EMC1812_TEMP_MASK | HWMON_T_FAULT),
	NULL
};

/**
 * struct emc1812_features - features of a emc1812 instance
 * @name:		chip's name
 * @phys_channels:	number of physical channels supported by the chip
 * @has_ext2_beta_reg:	the EXT2_BETA register is available on the chip
 */
struct emc1812_features {
	const char	*name;
	u8		phys_channels;
	bool		has_ext2_beta_reg;
};

static const struct emc1812_features emc1833_chip_config = {
	.name = "emc1833",
	.phys_channels = 3,
	.has_ext2_beta_reg = true,
};

static const struct emc1812_features emc1812_chip_config = {
	.name = "emc1812",
	.phys_channels = 2,
	.has_ext2_beta_reg = false,
};

static const struct emc1812_features emc1813_chip_config = {
	.name = "emc1813",
	.phys_channels = 3,
	.has_ext2_beta_reg = true,
};

static const struct emc1812_features emc1814_chip_config = {
	.name = "emc1814",
	.phys_channels = 4,
	.has_ext2_beta_reg = false,
};

static const struct emc1812_features emc1815_chip_config = {
	.name = "emc1815",
	.phys_channels = 5,
	.has_ext2_beta_reg = false,
};

enum emc1812_limit_type {temp_min, temp_max};

static const u8 emc1812_temp_map[] = {
	[hwmon_temp_min] = temp_min,
	[hwmon_temp_max] = temp_max,
};

static const u8 emc1812_ideality_regs[] = {
	[0] = 0xff,
	[1] = EMC1812_EXT1_IDEALITY_FACTOR_ADDR,
	[2] = EMC1812_EXT2_IDEALITY_FACTOR_ADDR,
	[3] = EMC1812_EXT3_IDEALITY_FACTOR_ADDR,
	[4] = EMC1812_EXT4_IDEALITY_FACTOR_ADDR,
};

static const u8 emc1812_temp_crit_regs[] = {
	[0] = EMC1812_INT_DIODE_THERM_LIMIT_ADDR,
	[1] = EMC1812_EXT1_THERM_LIMIT_ADDR,
	[2] = EMC1812_EXT2_THERM_LIMIT_ADDR,
	[3] = EMC1812_EXT3_THERM_LIMIT_ADDR,
	[4] = EMC1812_EXT4_THERM_LIMIT_ADDR,
};

static const u8 emc1812_limit_regs[][2] = {
	[0] = {
		[temp_min] = EMC1812_INT_DIODE_LOW_LIMIT_ADDR,
		[temp_max] = EMC1812_INT_DIODE_HIGH_LIMIT_ADDR,
	},
	[1] = {
		[temp_min] = EMC1812_EXT1_LOW_LIMIT_HIGH_BYTE_ADDR,
		[temp_max] = EMC1812_EXT1_HIGH_LIMIT_HIGH_BYTE_ADDR,
	},
	[2] = {
		[temp_min] = EMC1812_EXT2_LOW_LIMIT_HIGH_BYTE_ADDR,
		[temp_max] = EMC1812_EXT2_HIGH_LIMIT_HIGH_BYTE_ADDR,
	},
	[3] = {
		[temp_min] = EMC1812_EXT3_LOW_LIMIT_HIGH_BYTE_ADDR,
		[temp_max] = EMC1812_EXT3_HIGH_LIMIT_HIGH_BYTE_ADDR,
	},
	[4] = {
		[temp_min] = EMC1812_EXT4_LOW_LIMIT_HIGH_BYTE_ADDR,
		[temp_max] = EMC1812_EXT4_HIGH_LIMIT_HIGH_BYTE_ADDR,
	},
};

static const u8 emc1812_limit_regs_low[][2] = {
	[0] = {
		[temp_min] = 0xff,
		[temp_max] = 0xff,
	},
	[1] = {
		[temp_min] = EMC1812_EXT1_LOW_LIMIT_LOW_BYTE_ADDR,
		[temp_max] = EMC1812_EXT1_HIGH_LIMIT_LOW_BYTE_ADDR,
	},
	[2] = {
		[temp_min] = EMC1812_EXT2_LOW_LIMIT_LOW_BYTE_ADDR,
		[temp_max] = EMC1812_EXT2_HIGH_LIMIT_LOW_BYTE_ADDR,
	},
	[3] = {
		[temp_min] = EMC1812_EXT3_LOW_LIMIT_LOW_BYTE_ADDR,
		[temp_max] = EMC1812_EXT3_HIGH_LIMIT_LOW_BYTE_ADDR,
	},
	[4] = {
		[temp_min] = EMC1812_EXT4_LOW_LIMIT_LOW_BYTE_ADDR,
		[temp_max] = EMC1812_EXT4_HIGH_LIMIT_LOW_BYTE_ADDR,
	},
};

/* Lookup table for temperature conversion times in msec */
static const u16 emc1812_conv_time[] = {
	16000, 8000, 4000, 2000, 1000, 500, 250, 125, 62, 31, 16
};

/**
 * struct emc1812_data - information about chip parameters
 * @labels:		labels of the channels
 * @active_ch_mask:	active channels
 * @chip:		pointer to structure holding chip features
 * @regmap:		device register map
 * @recd34_en:		state of Resistance Error Correction (REC) on channels 3 and 4
 * @recd12_en:		state of Resistance Error Correction (REC) on channels 1 and 2
 * @apdd_en:		state of anti-parallel diode mode
 */
struct emc1812_data {
	const char *labels[EMC1812_MAX_NUM_CHANNELS];
	unsigned long active_ch_mask;
	const struct emc1812_features *chip;
	struct regmap *regmap;
	bool recd34_en;
	bool recd12_en;
	bool apdd_en;
};

/* emc1812 regmap configuration */
static const struct regmap_range emc1812_regmap_writable_ranges[] = {
	regmap_reg_range(EMC1812_CFG_ADDR, EMC1812_ONE_SHOT_ADDR),
	regmap_reg_range(EMC1812_EXT1_HIGH_LIMIT_LOW_BYTE_ADDR, EMC1812_EXT2_THERM_LIMIT_ADDR),
	regmap_reg_range(EMC1812_DIODE_FAULT_MASK_ADDR, EMC1812_CONSEC_ALERT_ADDR),
	regmap_reg_range(EMC1812_EXT1_BETA_CONFIG_ADDR, EMC1812_EXT4_IDEALITY_FACTOR_ADDR),
	regmap_reg_range(EMC1812_ROC_GAIN_ADDR, EMC1812_ROC_CONFIG_ADDR),
	regmap_reg_range(EMC1812_R1_LIMH_ADDR, EMC1812_R1_SMPL_ADDR),
	regmap_reg_range(EMC1812_R2_LIMH_ADDR, EMC1812_R2_SMPL_ADDR),
	regmap_reg_range(EMC1812_FILTER_SEL_ADDR, EMC1812_FILTER_SEL_ADDR),
	regmap_reg_range(EMC1812_HOTTEST_CFG_ADDR, EMC1812_HOTTEST_CFG_ADDR),
};

static const struct regmap_access_table emc1812_regmap_wr_table = {
	.yes_ranges = emc1812_regmap_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(emc1812_regmap_writable_ranges),
};

static const struct regmap_range emc1812_regmap_rd_ranges[] = {
	regmap_reg_range(EMC1812_STATUS_ADDR, EMC1812_CONFIG_LO_ADDR),
	regmap_reg_range(EMC1812_CFG_ADDR, EMC1812_ONE_SHOT_ADDR),
	regmap_reg_range(EMC1812_EXT1_HIGH_LIMIT_LOW_BYTE_ADDR,
			 EMC1812_EXT_DIODE_FAULT_STATUS_ADDR),
	regmap_reg_range(EMC1812_DIODE_FAULT_MASK_ADDR, EMC1812_CONSEC_ALERT_ADDR),
	regmap_reg_range(EMC1812_EXT1_BETA_CONFIG_ADDR, EMC1812_FILTER_SEL_ADDR),
	regmap_reg_range(EMC1812_INT_HIGH_BYTE_ADDR, EMC1812_HOTTEST_CFG_ADDR),
	regmap_reg_range(EMC1812_PRODUCT_ID_ADDR, EMC1812_REVISION_ADDR),
};

static const struct regmap_access_table emc1812_regmap_rd_table = {
	.yes_ranges = emc1812_regmap_rd_ranges,
	.n_yes_ranges = ARRAY_SIZE(emc1812_regmap_rd_ranges),
};

static bool emc1812_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case EMC1812_STATUS_ADDR:
	case EMC1812_EXT_DIODE_FAULT_STATUS_ADDR:
	case EMC1812_DIODE_FAULT_MASK_ADDR:
	case EMC1812_EXT1_BETA_CONFIG_ADDR:
	case EMC1812_EXT2_BETA_CONFIG_ADDR:
	case EMC1812_HIGH_LIMIT_STATUS_ADDR:
	case EMC1812_LOW_LIMIT_STATUS_ADDR:
	case EMC1812_THERM_LIMIT_STATUS_ADDR:
	case EMC1812_ROC_STATUS_ADDR:
	case EMC1812_PER_MAXTH_1_ADDR:
	case EMC1812_PER_MAXT1L_ADDR:
	case EMC1812_PER_MAXTH_2_ADDR:
	case EMC1812_PER_MAXT2_3L_ADDR:
	case EMC1812_GBL_MAXT1H_ADDR:
	case EMC1812_GBL_MAXT1L_ADDR:
	case EMC1812_GBL_MAXT2H_ADDR:
	case EMC1812_GBL_MAXT2L_ADDR:
	case EMC1812_INT_HIGH_BYTE_ADDR:
	case EMC1812_INT_LOW_BYTE_ADDR:
	case EMC1812_EXT1_HIGH_BYTE_ADDR:
	case EMC1812_EXT1_LOW_BYTE_ADDR:
	case EMC1812_EXT2_HIGH_BYTE_ADDR:
	case EMC1812_EXT2_LOW_BYTE_ADDR:
	case EMC1812_EXT3_HIGH_BYTE_ADDR:
	case EMC1812_EXT3_LOW_BYTE_ADDR:
	case EMC1812_EXT4_HIGH_BYTE_ADDR:
	case EMC1812_EXT4_LOW_BYTE_ADDR:
	case EMC1812_HOTTEST_DIODE_HIGH_BYTE_ADDR:
	case EMC1812_HOTTEST_DIODE_LOW_BYTE_ADDR:
	case EMC1812_HOTTEST_STATUS_ADDR:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config emc1812_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.rd_table = &emc1812_regmap_rd_table,
	.wr_table = &emc1812_regmap_wr_table,
	.volatile_reg = emc1812_is_volatile_reg,
	.max_register = EMC1812_REVISION_ADDR,
	.cache_type = REGCACHE_MAPLE,
};

static umode_t emc1812_is_visible(const void *_data, enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	const struct emc1812_data *data = _data;

	switch (type) {
	case hwmon_temp:
		/* Don't show channels which are not enabled */
		if (!(data->active_ch_mask & BIT(channel)))
			return 0;

		switch (attr) {
		case hwmon_temp_min:
		case hwmon_temp_max:
		case hwmon_temp_crit:
		case hwmon_temp_crit_hyst:
			return 0644;
		case hwmon_temp_crit_alarm:
		case hwmon_temp_input:
		case hwmon_temp_fault:
		case hwmon_temp_max_alarm:
		case hwmon_temp_max_hyst:
		case hwmon_temp_min_alarm:
			return 0444;
		case hwmon_temp_label:
			if (data->labels[channel])
				return 0444;
			return 0;
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
};

static int emc1812_get_temp(struct emc1812_data *data, int channel, long *val)
{
	__be16 tmp_be16;
	int ret;

	ret = regmap_bulk_read(data->regmap, EMC1812_TEMP_CH_ADDR(channel),
			       &tmp_be16, sizeof(tmp_be16));
	if (ret)
		return ret;

	/* Range is always -64 to 191.875°C */
	*val = ((be16_to_cpu(tmp_be16) >> 5) - (EMC1812_TEMP_OFFSET << 3)) * 125;

	return 0;
}

static int emc1812_get_crit_limit_temp(struct emc1812_data *data, int channel, long *val)
{
	unsigned int tmp;
	int ret;

	/* Critical register is 8bits long and keeps only integer part of temperature */
	ret = regmap_read(data->regmap, emc1812_temp_crit_regs[channel], &tmp);
	if (ret)
		return ret;

	*val = tmp;
	/* Range is always -64 to 191°C */
	*val = (*val - EMC1812_TEMP_OFFSET) * 1000;

	return 0;
}

static int emc1812_get_limit_temp(struct emc1812_data *data, int ch,
				  enum emc1812_limit_type type, long *val)
{
	unsigned int regvalh;
	unsigned int regvall = 0;
	int ret;

	ret = regmap_read(data->regmap, emc1812_limit_regs[ch][type], &regvalh);
	if (ret < 0)
		return ret;

	if (ch) {
		ret = regmap_read(data->regmap, emc1812_limit_regs_low[ch][type], &regvall);
		if (ret < 0)
			return ret;
	}

	/* Range is always -64 to 191.875°C */
	*val = ((regvalh << 3) | (regvall >> 5));
	*val = (*val - (EMC1812_TEMP_OFFSET << 3)) * 125;

	return 0;
}

static int emc1812_read_reg(struct device *dev, struct emc1812_data *data, u32 attr,
			    int channel, long *val)
{
	unsigned int hyst;
	int ret;

	switch (attr) {
	case hwmon_temp_min:
	case hwmon_temp_max:
		return emc1812_get_limit_temp(data, channel, emc1812_temp_map[attr], val);
	case hwmon_temp_crit:
		return emc1812_get_crit_limit_temp(data, channel, val);
	case hwmon_temp_input:
		return emc1812_get_temp(data, channel, val);
	case hwmon_temp_max_hyst:
		ret = emc1812_get_limit_temp(data, channel, temp_max, val);
		if (ret < 0)
			return ret;

		ret = regmap_read(data->regmap, EMC1812_THRM_HYS_ADDR, &hyst);
		if (ret < 0)
			return ret;

		*val -= (long)hyst * 1000;

		return 0;
	case hwmon_temp_crit_hyst:
		ret = emc1812_get_crit_limit_temp(data, channel, val);
		if (ret < 0)
			return ret;

		ret = regmap_read(data->regmap, EMC1812_THRM_HYS_ADDR, &hyst);
		if (ret < 0)
			return ret;

		*val -= (long)hyst * 1000;

		return 0;
	case hwmon_temp_min_alarm:
		*val = regmap_test_bits(data->regmap, EMC1812_LOW_LIMIT_STATUS_ADDR,
					BIT(channel));
		if (*val < 0)
			return *val;

		return 0;
	case hwmon_temp_max_alarm:
		*val = regmap_test_bits(data->regmap, EMC1812_HIGH_LIMIT_STATUS_ADDR,
					BIT(channel));
		if (*val < 0)
			return *val;

		return 0;
	case hwmon_temp_crit_alarm:
		*val = regmap_test_bits(data->regmap, EMC1812_THERM_LIMIT_STATUS_ADDR,
					BIT(channel));
		if (*val < 0)
			return *val;

		return 0;
	case hwmon_temp_fault:
		*val = regmap_test_bits(data->regmap, EMC1812_EXT_DIODE_FAULT_STATUS_ADDR,
					BIT(channel));
		if (*val < 0)
			return *val;

		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int emc1812_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			int channel, long *val)
{
	struct emc1812_data *data = dev_get_drvdata(dev);
	unsigned int convrate;
	int ret;

	switch (type) {
	case hwmon_temp:
		return emc1812_read_reg(dev, data, attr, channel, val);
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			ret = regmap_read(data->regmap, EMC1812_CONV_ADDR, &convrate);
			if (ret < 0)
				return ret;

			if (convrate > 10)
				convrate = 4;

			*val = DIV_ROUND_CLOSEST(16000, 1 << convrate);
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static int emc1812_read_string(struct device *dev, enum hwmon_sensor_types type,
			       u32 attr, int channel, const char **str)
{
	struct emc1812_data *data = dev_get_drvdata(dev);

	if (channel >= data->chip->phys_channels)
		return -EOPNOTSUPP;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_label:
			*str = data->labels[channel];
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static int emc1812_set_hyst(struct emc1812_data *data, int channel, int val)
{
	unsigned int limit;
	int hyst, ret;

	/* Critical register is 8bits long and keeps only integer part of temperature */
	ret = regmap_read(data->regmap, emc1812_temp_crit_regs[channel], &limit);
	if (ret)
		return ret;

	hyst = clamp_val((int)limit - val, 0, 255);

	ret = regmap_write(data->regmap, EMC1812_THRM_HYS_ADDR, hyst);

	return ret;
}

static int emc1812_set_temp(struct emc1812_data *data, int channel,
			    enum emc1812_limit_type map, int val)
{
	unsigned int valh, vall;
	u8 regh, regl;
	int ret;

	regh = emc1812_limit_regs[channel][map];
	regl = emc1812_limit_regs_low[channel][map];

	if (channel) {
		val = DIV_ROUND_CLOSEST(val, 125);
		valh = (val >> 3) & 0xff;
		vall = (val & 0x07) << 5;
	} else {
		/* Temperature limit for internal channel is stored on 8bits */
		valh = DIV_ROUND_CLOSEST(val, 1000);
		valh = clamp_val(valh, 0, 255);
	}

	ret = regmap_write(data->regmap, regh, valh);
	if (ret < 0)
		return ret;

	if (channel)
		ret = regmap_write(data->regmap, regl, vall);

	return ret;
}

static int emc1812_write(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			 int channel, long val)
{
	struct emc1812_data *data = dev_get_drvdata(dev);
	unsigned int interval, tmp;

	switch (type) {
	case hwmon_temp:
		/* Range should be -64000 to 191875°C + (EMC1812_TEMP_OFFSET * 1000) */
		val = clamp_val(val, -64000, 191875);
		val = val + (EMC1812_TEMP_OFFSET * 1000);

		switch (attr) {
		case hwmon_temp_min:
		case hwmon_temp_max:
			return emc1812_set_temp(data, channel, emc1812_temp_map[attr], val);
		case hwmon_temp_crit:
			/* Critical temperature limit is stored on 8bits */
			val = DIV_ROUND_CLOSEST(val, 1000);
			tmp = clamp_val(val, 0, 255);
			return regmap_write(data->regmap, emc1812_temp_crit_regs[channel], tmp);
		case hwmon_temp_crit_hyst:
			/* Critical temperature hysteresis is stored on 8bits */
			val = DIV_ROUND_CLOSEST(val, 1000);
			tmp = clamp_val(val, 0, 255);
			return emc1812_set_hyst(data, channel, tmp);
		default:
			return -EOPNOTSUPP;
		}
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			interval = clamp_val(val, 0, 16000);
			tmp = find_closest_descending(interval, emc1812_conv_time,
						      ARRAY_SIZE(emc1812_conv_time));
			return regmap_write(data->regmap, EMC1812_CONV_ADDR, tmp);
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static int emc1812_init(struct emc1812_data *priv)
{
	int i, ret;
	u8 val;

	ret = regmap_write(priv->regmap, EMC1812_THRM_HYS_ADDR, 0x0A);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, EMC1812_CONSEC_ALERT_ADDR, 0x70);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, EMC1812_FILTER_SEL_ADDR, 0);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, EMC1812_HOTTEST_CFG_ADDR, 0);
	if (ret)
		return ret;

	/* Enables the beta compensation factor auto-detection function for beta1 and beta2 */
	ret = regmap_write(priv->regmap, EMC1812_EXT1_BETA_CONFIG_ADDR,
			   EMC1812_BETA_LOCK_VAL);
	if (ret)
		return ret;

	if (priv->chip->has_ext2_beta_reg) {
		ret = regmap_write(priv->regmap, EMC1812_EXT2_BETA_CONFIG_ADDR,
				   EMC1812_BETA_LOCK_VAL);
		if (ret)
			return ret;
	}

	for (i = 0; i < priv->chip->phys_channels; i++) {
		if (!test_bit(i, &priv->active_ch_mask))
			continue;

		/* Update the max temperature limit for extended temperature range. */
		ret = emc1812_set_temp(priv, i, emc1812_temp_map[hwmon_temp_max],
				       EMC1812_HIGH_LIMIT_DEFAULT * 1000);
		if (ret)
			return ret;

		/* Update the critical temperature limit for extended temperature range. */
		ret = regmap_write(priv->regmap, emc1812_temp_crit_regs[i],
				   EMC1812_HIGH_LIMIT_DEFAULT);
		if (ret)
			return ret;

		/* Set the ideality factor */
		if (i > 0) {
			ret = regmap_write(priv->regmap, emc1812_ideality_regs[i],
					   EMC1812_DEFAULT_IDEALITY_FACTOR);
			if (ret)
				return ret;
		}
	}

	/*
	 * Set default values in registers. APDD, RECD12 and RECD34 are active on 0.
	 * Set the device to be in Run (Active) state and converting on all
	 * channels.
	 * Don't change conversion rate. After reset, default is 4 conversions/seconds.
	 * The temperature measurement range is -64°C to +191.875°C.
	 * Set ALERT/THERM2 pin to be in comparator mode (When the ALERT/THERM2 pin is
	 * asserted in comparator mode, the corresponding High Limit Status bits are set.
	 * Reading these bits does not clear them until the ALERT/THERM2 pin is deasserted.
	 * Once the ALERT/THERM2 pin is deasserted, the status bits are automatically
	 * cleared.).
	 */
	val = FIELD_PREP(EMC1812_CFG_MSKAL, 0) |
	      FIELD_PREP(EMC1812_CFG_RS, 0) |
	      FIELD_PREP(EMC1812_CFG_ATTHM, 1) |
	      FIELD_PREP(EMC1812_CFG_RECD12, !priv->recd12_en) |
	      FIELD_PREP(EMC1812_CFG_RECD34, !priv->recd34_en) |
	      FIELD_PREP(EMC1812_CFG_RANGE, 1) |
	      FIELD_PREP(EMC1812_CFG_DA_ENA, 0) |
	      FIELD_PREP(EMC1812_CFG_APDD, !priv->apdd_en);

	return regmap_write(priv->regmap, EMC1812_CFG_ADDR, val);
}

static int emc1812_parse_fw_config(struct emc1812_data *data, struct device *dev)
{
	unsigned int reg_nr = 0;
	int ret;

	/* To be able to load the driver in case we don't have device tree */
	if (!dev_fwnode(dev)) {
		data->active_ch_mask = BIT(data->chip->phys_channels) - 1;
		return 0;
	}

	data->apdd_en = device_property_read_bool(dev, "microchip,enable-anti-parallel");
	data->recd12_en = device_property_read_bool(dev, "microchip,parasitic-res-on-channel1-2");
	data->recd34_en = device_property_read_bool(dev, "microchip,parasitic-res-on-channel3-4");

	/* Internal temperature channel is always active */
	data->labels[reg_nr] = "internal_diode";
	set_bit(reg_nr, &data->active_ch_mask);

	device_for_each_child_node_scoped(dev, child) {
		ret = fwnode_property_read_u32(child, "reg", &reg_nr);
		if (ret || reg_nr >= data->chip->phys_channels)
			return dev_err_probe(dev, -EINVAL,
					     "The index is higher then the chip supports\n");
		/* Mark channel as active */
		set_bit(reg_nr, &data->active_ch_mask);

		fwnode_property_read_string(child, "label", &data->labels[reg_nr]);
	}

	return 0;
}

static int emc1812_chip_identify(struct emc1812_data *data, struct i2c_client *client)
{
	const struct emc1812_features *chip;
	struct device *dev = &client->dev;
	unsigned int tmp;
	int ret;

	ret = regmap_read(data->regmap, EMC1812_PRODUCT_ID_ADDR, &tmp);
	if (ret)
		return ret;

	switch (tmp) {
	case EMC1812_PID:
		data->chip = &emc1812_chip_config;
		break;
	case EMC1813_PID:
		data->chip = &emc1813_chip_config;
		break;
	case EMC1814_PID:
		data->chip = &emc1814_chip_config;
		break;
	case EMC1815_PID:
		data->chip = &emc1815_chip_config;
		break;
	case EMC1833_PID:
		data->chip = &emc1833_chip_config;
		break;
	default:
		/*
		 * If failed to identify the hardware based on internal registers,
		 * try using fallback compatible in device tree to deal with some
		 * newer part number.
		 */
		chip = i2c_get_match_data(client);
		if (!chip)
			return -ENODEV;

		dev_warn(dev, "Unrecognized hardware ID 0x%x, using %s from devicetree data\n",
			 tmp, chip->name);

		data->chip = chip;

		return 0;
	}

	return 0;
}

static const struct hwmon_ops emc1812_ops = {
	.is_visible = emc1812_is_visible,
	.read = emc1812_read,
	.read_string = emc1812_read_string,
	.write = emc1812_write,
};

static const struct hwmon_chip_info emc1812_chip_info = {
	.ops = &emc1812_ops,
	.info = emc1812_info,
};

static int emc1812_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct emc1812_data *data;
	struct device *hwmon_dev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = devm_regmap_init_i2c(client, &emc1812_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "Cannot initialize register map\n");

	ret = emc1812_chip_identify(data, client);
	if (ret)
		return dev_err_probe(dev, ret, "Chip identification fails\n");

	ret = emc1812_parse_fw_config(data, dev);
	if (ret)
		return ret;

	ret = emc1812_init(data);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot initialize device\n");

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, data,
							 &emc1812_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id emc1812_id[] = {
	{ .name = "emc1812", .driver_data = (kernel_ulong_t)&emc1812_chip_config },
	{ .name = "emc1813", .driver_data = (kernel_ulong_t)&emc1813_chip_config },
	{ .name = "emc1814", .driver_data = (kernel_ulong_t)&emc1814_chip_config },
	{ .name = "emc1815", .driver_data = (kernel_ulong_t)&emc1815_chip_config },
	{ .name = "emc1833", .driver_data = (kernel_ulong_t)&emc1833_chip_config },
	{ }
};
MODULE_DEVICE_TABLE(i2c, emc1812_id);

static const struct of_device_id emc1812_of_match[] = {
	{
		.compatible = "microchip,emc1812",
		.data = &emc1812_chip_config
	},
	{
		.compatible = "microchip,emc1813",
		.data = &emc1813_chip_config
	},
	{
		.compatible = "microchip,emc1814",
		.data = &emc1814_chip_config
	},
	{
		.compatible = "microchip,emc1815",
		.data = &emc1815_chip_config
	},
	{
		.compatible = "microchip,emc1833",
		.data = &emc1833_chip_config
	},
	{ }
};
MODULE_DEVICE_TABLE(of, emc1812_of_match);

static struct i2c_driver emc1812_driver = {
	.driver	 = {
		.name = "emc1812",
		.of_match_table = emc1812_of_match,
	},
	.probe = emc1812_probe,
	.id_table = emc1812_id,
};
module_i2c_driver(emc1812_driver);

MODULE_AUTHOR("Marius Cristea <marius.cristea@microchip.com>");
MODULE_DESCRIPTION("EMC1812/13/14/15/33 high-accuracy remote diode temperature monitor Driver");
MODULE_LICENSE("GPL");
