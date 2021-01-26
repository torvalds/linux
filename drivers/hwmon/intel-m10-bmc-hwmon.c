// SPDX-License-Identifier: GPL-2.0
/*
 * Intel MAX 10 BMC HWMON Driver
 *
 * Copyright (C) 2018-2020 Intel Corporation. All rights reserved.
 *
 */
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/mfd/intel-m10-bmc.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

struct m10bmc_sdata {
	unsigned int reg_input;
	unsigned int reg_max;
	unsigned int reg_crit;
	unsigned int reg_hyst;
	unsigned int reg_min;
	unsigned int multiplier;
	const char *label;
};

struct m10bmc_hwmon_board_data {
	const struct m10bmc_sdata *tables[hwmon_max];
	const struct hwmon_channel_info **hinfo;
};

struct m10bmc_hwmon {
	struct device *dev;
	struct hwmon_chip_info chip;
	char *hw_name;
	struct intel_m10bmc *m10bmc;
	const struct m10bmc_hwmon_board_data *bdata;
};

static const struct m10bmc_sdata n3000bmc_temp_tbl[] = {
	{ 0x100, 0x104, 0x108, 0x10c, 0x0, 500, "Board Temperature" },
	{ 0x110, 0x114, 0x118, 0x0, 0x0, 500, "FPGA Die Temperature" },
	{ 0x11c, 0x124, 0x120, 0x0, 0x0, 500, "QSFP0 Temperature" },
	{ 0x12c, 0x134, 0x130, 0x0, 0x0, 500, "QSFP1 Temperature" },
	{ 0x168, 0x0, 0x0, 0x0, 0x0, 500, "Retimer A Temperature" },
	{ 0x16c, 0x0, 0x0, 0x0, 0x0, 500, "Retimer A SerDes Temperature" },
	{ 0x170, 0x0, 0x0, 0x0, 0x0, 500, "Retimer B Temperature" },
	{ 0x174, 0x0, 0x0, 0x0, 0x0, 500, "Retimer B SerDes Temperature" },
};

static const struct m10bmc_sdata n3000bmc_in_tbl[] = {
	{ 0x128, 0x0, 0x0, 0x0, 0x0, 1, "QSFP0 Supply Voltage" },
	{ 0x138, 0x0, 0x0, 0x0, 0x0, 1, "QSFP1 Supply Voltage" },
	{ 0x13c, 0x0, 0x0, 0x0, 0x0, 1, "FPGA Core Voltage" },
	{ 0x144, 0x0, 0x0, 0x0, 0x0, 1, "12V Backplane Voltage" },
	{ 0x14c, 0x0, 0x0, 0x0, 0x0, 1, "1.2V Voltage" },
	{ 0x150, 0x0, 0x0, 0x0, 0x0, 1, "12V AUX Voltage" },
	{ 0x158, 0x0, 0x0, 0x0, 0x0, 1, "1.8V Voltage" },
	{ 0x15c, 0x0, 0x0, 0x0, 0x0, 1, "3.3V Voltage" },
};

static const struct m10bmc_sdata n3000bmc_curr_tbl[] = {
	{ 0x140, 0x0, 0x0, 0x0, 0x0, 1, "FPGA Core Current" },
	{ 0x148, 0x0, 0x0, 0x0, 0x0, 1, "12V Backplane Current" },
	{ 0x154, 0x0, 0x0, 0x0, 0x0, 1, "12V AUX Current" },
};

static const struct m10bmc_sdata n3000bmc_power_tbl[] = {
	{ 0x160, 0x0, 0x0, 0x0, 0x0, 1000, "Board Power" },
};

static const struct hwmon_channel_info *n3000bmc_hinfo[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_LABEL),
	NULL
};

static const struct m10bmc_hwmon_board_data n3000bmc_hwmon_bdata = {
	.tables = {
		[hwmon_temp] = n3000bmc_temp_tbl,
		[hwmon_in] = n3000bmc_in_tbl,
		[hwmon_curr] = n3000bmc_curr_tbl,
		[hwmon_power] = n3000bmc_power_tbl,
	},

	.hinfo = n3000bmc_hinfo,
};

static umode_t
m10bmc_hwmon_is_visible(const void *data, enum hwmon_sensor_types type,
			u32 attr, int channel)
{
	return 0444;
}

static const struct m10bmc_sdata *
find_sensor_data(struct m10bmc_hwmon *hw, enum hwmon_sensor_types type,
		 int channel)
{
	const struct m10bmc_sdata *tbl;

	tbl = hw->bdata->tables[type];
	if (!tbl)
		return ERR_PTR(-EOPNOTSUPP);

	return &tbl[channel];
}

static int do_sensor_read(struct m10bmc_hwmon *hw,
			  const struct m10bmc_sdata *data,
			  unsigned int regoff, long *val)
{
	unsigned int regval;
	int ret;

	ret = m10bmc_sys_read(hw->m10bmc, regoff, &regval);
	if (ret)
		return ret;

	/*
	 * BMC Firmware will return 0xdeadbeef if the sensor value is invalid
	 * at that time. This usually happens on sensor channels which connect
	 * to external pluggable modules, e.g. QSFP temperature and voltage.
	 * When the QSFP is unplugged from cage, driver will get 0xdeadbeef
	 * from their registers.
	 */
	if (regval == 0xdeadbeef)
		return -ENODATA;

	*val = regval * data->multiplier;

	return 0;
}

static int m10bmc_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *val)
{
	struct m10bmc_hwmon *hw = dev_get_drvdata(dev);
	unsigned int reg = 0, reg_hyst = 0;
	const struct m10bmc_sdata *data;
	long hyst, value;
	int ret;

	data = find_sensor_data(hw, type, channel);
	if (IS_ERR(data))
		return PTR_ERR(data);

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			reg = data->reg_input;
			break;
		case hwmon_temp_max_hyst:
			reg_hyst = data->reg_hyst;
			fallthrough;
		case hwmon_temp_max:
			reg = data->reg_max;
			break;
		case hwmon_temp_crit_hyst:
			reg_hyst = data->reg_hyst;
			fallthrough;
		case hwmon_temp_crit:
			reg = data->reg_crit;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			reg = data->reg_input;
			break;
		case hwmon_in_max:
			reg = data->reg_max;
			break;
		case hwmon_in_crit:
			reg = data->reg_crit;
			break;
		case hwmon_in_min:
			reg = data->reg_min;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
			reg = data->reg_input;
			break;
		case hwmon_curr_max:
			reg = data->reg_max;
			break;
		case hwmon_curr_crit:
			reg = data->reg_crit;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_power:
		switch (attr) {
		case hwmon_power_input:
			reg = data->reg_input;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (!reg)
		return -EOPNOTSUPP;

	ret = do_sensor_read(hw, data, reg, &value);
	if (ret)
		return ret;

	if (reg_hyst) {
		ret = do_sensor_read(hw, data, reg_hyst, &hyst);
		if (ret)
			return ret;

		value -= hyst;
	}

	*val = value;

	return 0;
}

static int m10bmc_hwmon_read_string(struct device *dev,
				    enum hwmon_sensor_types type,
				    u32 attr, int channel, const char **str)
{
	struct m10bmc_hwmon *hw = dev_get_drvdata(dev);
	const struct m10bmc_sdata *data;

	data = find_sensor_data(hw, type, channel);
	if (IS_ERR(data))
		return PTR_ERR(data);

	*str = data->label;

	return 0;
}

static const struct hwmon_ops m10bmc_hwmon_ops = {
	.is_visible = m10bmc_hwmon_is_visible,
	.read = m10bmc_hwmon_read,
	.read_string = m10bmc_hwmon_read_string,
};

static int m10bmc_hwmon_probe(struct platform_device *pdev)
{
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct intel_m10bmc *m10bmc = dev_get_drvdata(pdev->dev.parent);
	struct device *hwmon_dev, *dev = &pdev->dev;
	struct m10bmc_hwmon *hw;
	int i;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	hw->dev = dev;
	hw->m10bmc = m10bmc;
	hw->bdata = (const struct m10bmc_hwmon_board_data *)id->driver_data;

	hw->chip.info = hw->bdata->hinfo;
	hw->chip.ops = &m10bmc_hwmon_ops;

	hw->hw_name = devm_kstrdup(dev, id->name, GFP_KERNEL);
	if (!hw->hw_name)
		return -ENOMEM;

	for (i = 0; hw->hw_name[i]; i++)
		if (hwmon_is_bad_char(hw->hw_name[i]))
			hw->hw_name[i] = '_';

	hwmon_dev = devm_hwmon_device_register_with_info(dev, hw->hw_name,
							 hw, &hw->chip, NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct platform_device_id intel_m10bmc_hwmon_ids[] = {
	{
		.name = "n3000bmc-hwmon",
		.driver_data = (unsigned long)&n3000bmc_hwmon_bdata,
	},
	{ }
};

static struct platform_driver intel_m10bmc_hwmon_driver = {
	.probe = m10bmc_hwmon_probe,
	.driver = {
		.name = "intel-m10-bmc-hwmon",
	},
	.id_table = intel_m10bmc_hwmon_ids,
};
module_platform_driver(intel_m10bmc_hwmon_driver);

MODULE_DEVICE_TABLE(platform, intel_m10bmc_hwmon_ids);
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel MAX 10 BMC hardware monitor");
MODULE_LICENSE("GPL");
