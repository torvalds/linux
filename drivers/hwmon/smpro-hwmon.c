// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ampere Computing SoC's SMPro Hardware Monitoring Driver
 *
 * Copyright (c) 2022, Ampere Computing LLC
 */
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

/* Logical Power Sensor Registers */
#define SOC_TEMP		0x10
#define SOC_VRD_TEMP		0x11
#define DIMM_VRD_TEMP		0x12
#define CORE_VRD_TEMP		0x13
#define CH0_DIMM_TEMP		0x14
#define CH1_DIMM_TEMP		0x15
#define CH2_DIMM_TEMP		0x16
#define CH3_DIMM_TEMP		0x17
#define CH4_DIMM_TEMP		0x18
#define CH5_DIMM_TEMP		0x19
#define CH6_DIMM_TEMP		0x1A
#define CH7_DIMM_TEMP		0x1B
#define RCA_VRD_TEMP		0x1C

#define CORE_VRD_PWR		0x20
#define SOC_PWR			0x21
#define DIMM_VRD1_PWR		0x22
#define DIMM_VRD2_PWR		0x23
#define CORE_VRD_PWR_MW		0x26
#define SOC_PWR_MW		0x27
#define DIMM_VRD1_PWR_MW	0x28
#define DIMM_VRD2_PWR_MW	0x29
#define RCA_VRD_PWR		0x2A
#define RCA_VRD_PWR_MW		0x2B

#define MEM_HOT_THRESHOLD	0x32
#define SOC_VR_HOT_THRESHOLD	0x33
#define CORE_VRD_VOLT		0x34
#define SOC_VRD_VOLT		0x35
#define DIMM_VRD1_VOLT		0x36
#define DIMM_VRD2_VOLT		0x37
#define RCA_VRD_VOLT		0x38

#define CORE_VRD_CURR		0x39
#define SOC_VRD_CURR		0x3A
#define DIMM_VRD1_CURR		0x3B
#define DIMM_VRD2_CURR		0x3C
#define RCA_VRD_CURR		0x3D

struct smpro_hwmon {
	struct regmap *regmap;
};

struct smpro_sensor {
	const u8 reg;
	const u8 reg_ext;
	const char *label;
};

static const struct smpro_sensor temperature[] = {
	{
		.reg = SOC_TEMP,
		.label = "temp1 SoC"
	},
	{
		.reg = SOC_VRD_TEMP,
		.reg_ext = SOC_VR_HOT_THRESHOLD,
		.label = "temp2 SoC VRD"
	},
	{
		.reg = DIMM_VRD_TEMP,
		.label = "temp3 DIMM VRD"
	},
	{
		.reg = CORE_VRD_TEMP,
		.label = "temp4 CORE VRD"
	},
	{
		.reg = CH0_DIMM_TEMP,
		.reg_ext = MEM_HOT_THRESHOLD,
		.label = "temp5 CH0 DIMM"
	},
	{
		.reg = CH1_DIMM_TEMP,
		.reg_ext = MEM_HOT_THRESHOLD,
		.label = "temp6 CH1 DIMM"
	},
	{
		.reg = CH2_DIMM_TEMP,
		.reg_ext = MEM_HOT_THRESHOLD,
		.label = "temp7 CH2 DIMM"
	},
	{
		.reg = CH3_DIMM_TEMP,
		.reg_ext = MEM_HOT_THRESHOLD,
		.label = "temp8 CH3 DIMM"
	},
	{
		.reg = CH4_DIMM_TEMP,
		.reg_ext = MEM_HOT_THRESHOLD,
		.label = "temp9 CH4 DIMM"
	},
	{
		.reg = CH5_DIMM_TEMP,
		.reg_ext = MEM_HOT_THRESHOLD,
		.label = "temp10 CH5 DIMM"
	},
	{
		.reg = CH6_DIMM_TEMP,
		.reg_ext = MEM_HOT_THRESHOLD,
		.label = "temp11 CH6 DIMM"
	},
	{
		.reg = CH7_DIMM_TEMP,
		.reg_ext = MEM_HOT_THRESHOLD,
		.label = "temp12 CH7 DIMM"
	},
	{
		.reg = RCA_VRD_TEMP,
		.label = "temp13 RCA VRD"
	},
};

static const struct smpro_sensor voltage[] = {
	{
		.reg = CORE_VRD_VOLT,
		.label = "vout0 CORE VRD"
	},
	{
		.reg = SOC_VRD_VOLT,
		.label = "vout1 SoC VRD"
	},
	{
		.reg = DIMM_VRD1_VOLT,
		.label = "vout2 DIMM VRD1"
	},
	{
		.reg = DIMM_VRD2_VOLT,
		.label = "vout3 DIMM VRD2"
	},
	{
		.reg = RCA_VRD_VOLT,
		.label = "vout4 RCA VRD"
	},
};

static const struct smpro_sensor curr_sensor[] = {
	{
		.reg = CORE_VRD_CURR,
		.label = "iout1 CORE VRD"
	},
	{
		.reg = SOC_VRD_CURR,
		.label = "iout2 SoC VRD"
	},
	{
		.reg = DIMM_VRD1_CURR,
		.label = "iout3 DIMM VRD1"
	},
	{
		.reg = DIMM_VRD2_CURR,
		.label = "iout4 DIMM VRD2"
	},
	{
		.reg = RCA_VRD_CURR,
		.label = "iout5 RCA VRD"
	},
};

static const struct smpro_sensor power[] = {
	{
		.reg = CORE_VRD_PWR,
		.reg_ext = CORE_VRD_PWR_MW,
		.label = "power1 CORE VRD"
	},
	{
		.reg = SOC_PWR,
		.reg_ext = SOC_PWR_MW,
		.label = "power2 SoC"
	},
	{
		.reg = DIMM_VRD1_PWR,
		.reg_ext = DIMM_VRD1_PWR_MW,
		.label = "power3 DIMM VRD1"
	},
	{
		.reg = DIMM_VRD2_PWR,
		.reg_ext = DIMM_VRD2_PWR_MW,
		.label = "power4 DIMM VRD2"
	},
	{
		.reg = RCA_VRD_PWR,
		.reg_ext = RCA_VRD_PWR_MW,
		.label = "power5 RCA VRD"
	},
};

static int smpro_read_temp(struct device *dev, u32 attr, int channel, long *val)
{
	struct smpro_hwmon *hwmon = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	switch (attr) {
	case hwmon_temp_input:
		ret = regmap_read(hwmon->regmap, temperature[channel].reg, &value);
		if (ret)
			return ret;
		break;
	case hwmon_temp_crit:
		ret = regmap_read(hwmon->regmap, temperature[channel].reg_ext, &value);
		if (ret)
			return ret;
		break;
	default:
		return -EOPNOTSUPP;
	}

	*val = sign_extend32(value, 8) * 1000;
	return 0;
}

static int smpro_read_in(struct device *dev, u32 attr, int channel, long *val)
{
	struct smpro_hwmon *hwmon = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	switch (attr) {
	case hwmon_in_input:
		ret = regmap_read(hwmon->regmap, voltage[channel].reg, &value);
		if (ret < 0)
			return ret;
		/* 15-bit value in 1mV */
		*val = value & 0x7fff;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int smpro_read_curr(struct device *dev, u32 attr, int channel, long *val)
{
	struct smpro_hwmon *hwmon = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	switch (attr) {
	case hwmon_curr_input:
		ret = regmap_read(hwmon->regmap, curr_sensor[channel].reg, &value);
		if (ret < 0)
			return ret;
		/* Scale reported by the hardware is 1mA */
		*val = value & 0x7fff;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int smpro_read_power(struct device *dev, u32 attr, int channel, long *val_pwr)
{
	struct smpro_hwmon *hwmon = dev_get_drvdata(dev);
	unsigned int val = 0, val_mw = 0;
	int ret;

	switch (attr) {
	case hwmon_power_input:
		ret = regmap_read(hwmon->regmap, power[channel].reg, &val);
		if (ret)
			return ret;

		ret = regmap_read(hwmon->regmap, power[channel].reg_ext, &val_mw);
		if (ret)
			return ret;
		/* 10-bit value */
		*val_pwr = (val & 0x3ff) * 1000000 + (val_mw & 0x3ff) * 1000;
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static int smpro_read(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_temp:
		return smpro_read_temp(dev, attr, channel, val);
	case hwmon_in:
		return smpro_read_in(dev, attr, channel, val);
	case hwmon_power:
		return smpro_read_power(dev, attr, channel, val);
	case hwmon_curr:
		return smpro_read_curr(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int smpro_read_string(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_label:
			*str = temperature[channel].label;
			return 0;
		default:
			break;
		}
		break;

	case hwmon_in:
		switch (attr) {
		case hwmon_in_label:
			*str = voltage[channel].label;
			return 0;
		default:
			break;
		}
		break;

	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_label:
			*str = curr_sensor[channel].label;
			return 0;
		default:
			break;
		}
		break;

	case hwmon_power:
		switch (attr) {
		case hwmon_power_label:
			*str = power[channel].label;
			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static umode_t smpro_is_visible(const void *data, enum hwmon_sensor_types type,
				u32 attr, int channel)
{
	const struct smpro_hwmon *hwmon = data;
	unsigned int value;
	int ret;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
		case hwmon_temp_label:
		case hwmon_temp_crit:
			ret = regmap_read(hwmon->regmap, temperature[channel].reg, &value);
			if (ret || value == 0xFFFF)
				return 0;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0444;
}

static const struct hwmon_channel_info * const smpro_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL),
	NULL
};

static const struct hwmon_ops smpro_hwmon_ops = {
	.is_visible = smpro_is_visible,
	.read = smpro_read,
	.read_string = smpro_read_string,
};

static const struct hwmon_chip_info smpro_chip_info = {
	.ops = &smpro_hwmon_ops,
	.info = smpro_info,
};

static int smpro_hwmon_probe(struct platform_device *pdev)
{
	struct smpro_hwmon *hwmon;
	struct device *hwmon_dev;

	hwmon = devm_kzalloc(&pdev->dev, sizeof(struct smpro_hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	hwmon->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!hwmon->regmap)
		return -ENODEV;

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev, "smpro_hwmon",
							 hwmon, &smpro_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct platform_driver smpro_hwmon_driver = {
	.probe		= smpro_hwmon_probe,
	.driver = {
		.name	= "smpro-hwmon",
	},
};

module_platform_driver(smpro_hwmon_driver);

MODULE_AUTHOR("Thu Nguyen <thu@os.amperecomputing.com>");
MODULE_AUTHOR("Quan Nguyen <quan@os.amperecomputing.com>");
MODULE_DESCRIPTION("Ampere Altra SMPro hwmon driver");
MODULE_LICENSE("GPL");
