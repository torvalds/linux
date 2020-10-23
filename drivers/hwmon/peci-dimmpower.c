// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Intel Corporation

#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/mfd/intel-peci-client.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "peci-hwmon.h"

#define PECI_DIMMPOWER_CHANNEL_COUNT   1 /* Supported channels number */

#define PECI_DIMMPOWER_SENSOR_COUNT 4 /* Supported sensors/readings number */

struct peci_dimmpower {
	struct device *dev;
	struct peci_client_manager *mgr;
	char name[PECI_NAME_SIZE];
	u32 power_config[PECI_DIMMPOWER_CHANNEL_COUNT + 1];
	u32 config_idx;
	struct hwmon_channel_info power_info;
	const struct hwmon_channel_info *info[2];
	struct hwmon_chip_info chip;

	struct peci_sensor_data
		sensor_data_list[PECI_DIMMPOWER_CHANNEL_COUNT]
				[PECI_DIMMPOWER_SENSOR_COUNT];

	/*
	 * Not exposed to any sensor directly - just used to store previous raw
	 * energy counter value that is required to calculate average power
	 */
	struct peci_sensor_data power_sensor_prev_energy;

	union peci_pkg_power_sku_unit units;
	bool units_valid;

	u32 dpl_time_window;
	bool dpl_time_window_valid;
};

static const char *peci_dimmpower_labels[PECI_DIMMPOWER_CHANNEL_COUNT] = {
	"dimm power",
};

/**
 * peci_dimmpower_read_dram_power_limit - read PCS DRAM Power Limit
 * @peci_mgr: PECI client manager handle
 * @reg: Pointer to the variable read value is going to be put
 *
 * Return: 0 if succeeded, other values in case an error.
 */
static inline int
peci_dimmpower_read_dram_power_limit(struct peci_client_manager *peci_mgr,
				     union peci_dram_power_limit *reg)
{
	return peci_pcs_read(peci_mgr, PECI_MBX_INDEX_DDR_RAPL_PL1,
			     PECI_PCS_PARAM_ZERO, &reg->value);
}

static int
peci_dimmpower_get_avg_power(void *ctx, struct peci_sensor_conf *sensor_conf,
			     struct peci_sensor_data *sensor_data)
{
	struct peci_dimmpower *priv = (struct peci_dimmpower *)ctx;
	struct peci_sensor_data energy;
	int ret;

	if (!peci_sensor_need_update_with_time(sensor_data,
					       sensor_conf->update_interval)) {
		dev_dbg(priv->dev, "skip reading peci, average power %dmW\n",
			sensor_data->value);
		return 0;
	}

	ret = peci_pcs_get_units(priv->mgr, &priv->units, &priv->units_valid);
	if (ret) {
		dev_dbg(priv->dev, "not able to read units\n");
		return ret;
	}

	ret = peci_pcs_read(priv->mgr, PECI_MBX_INDEX_ENERGY_STATUS,
			    PECI_PKG_ID_DIMM, &energy.value);
	if (ret) {
		dev_dbg(priv->dev, "not able to read energy\n");
		return ret;
	}

	energy.last_updated = jiffies;

	ret = peci_pcs_calc_pwr_from_eng(priv->dev,
					 &priv->power_sensor_prev_energy,
					 &energy, priv->units.bits.eng_unit,
					 &sensor_data->value);
	peci_sensor_mark_updated_with_time(sensor_data, energy.last_updated);

	dev_dbg(priv->dev, "average power %dmW, jif %lu, HZ is %d jiffies\n",
		sensor_data->value, energy.last_updated, HZ);

	return ret;
}

static int
peci_dimmpower_get_power_limit(void *ctx, struct peci_sensor_conf *sensor_conf,
			       struct peci_sensor_data *sensor_data)
{
	struct peci_dimmpower *priv = (struct peci_dimmpower *)ctx;
	union peci_dram_power_limit power_limit;
	ulong jif;
	int ret;

	if (!peci_sensor_need_update_with_time(sensor_data,
					       sensor_conf->update_interval)) {
		dev_dbg(priv->dev, "skip reading peci, power limit %dmW\n",
			sensor_data->value);
		return 0;
	}

	ret = peci_pcs_get_units(priv->mgr, &priv->units, &priv->units_valid);
	if (ret) {
		dev_dbg(priv->dev, "not able to read units\n");
		return ret;
	}

	jif = jiffies;
	ret = peci_dimmpower_read_dram_power_limit(priv->mgr, &power_limit);
	if (ret) {
		dev_dbg(priv->dev, "not able to read power limit\n");
		return ret;
	}

	sensor_data->value = peci_pcs_xn_to_munits(power_limit.bits.pp_pwr_lim,
						   priv->units.bits.pwr_unit);
	peci_sensor_mark_updated_with_time(sensor_data, jif);

	dev_dbg(priv->dev, "raw power limit %u, unit %u, power limit %d\n",
		power_limit.bits.pp_pwr_lim, priv->units.bits.pwr_unit,
		sensor_data->value);

	return ret;
}

static int
peci_dimmpower_set_power_limit(void *ctx, struct peci_sensor_conf *sensor_conf,
			       struct peci_sensor_data *sensor_data,
			       s32 val)
{
	struct peci_dimmpower *priv = (struct peci_dimmpower *)ctx;
	union peci_dram_power_limit power_limit;
	int ret;

	ret = peci_pcs_get_units(priv->mgr, &priv->units, &priv->units_valid);
	if (ret) {
		dev_dbg(priv->dev, "not able to read units\n");
		return ret;
	}

	ret = peci_dimmpower_read_dram_power_limit(priv->mgr, &power_limit);
	if (ret) {
		dev_dbg(priv->dev, "not able to read power limit\n");
		return ret;
	}

	/* Calculate DPL time window if needed */
	if (!priv->dpl_time_window_valid) {
		priv->dpl_time_window =
			peci_pcs_calc_plxy_time_window(peci_pcs_munits_to_xn(
				PECI_PCS_PPL1_TIME_WINDOW,
				priv->units.bits.tim_unit));
		priv->dpl_time_window_valid = true;
	}

	/* Enable or disable power limitation */
	if (val > 0) {
		power_limit.bits.pp_pwr_lim =
			peci_pcs_munits_to_xn(val, priv->units.bits.pwr_unit);
		power_limit.bits.pwr_lim_ctrl_en = 1u;
		power_limit.bits.ctrl_time_win = priv->dpl_time_window;
	} else {
		power_limit.bits.pp_pwr_lim = 0u;
		power_limit.bits.pwr_lim_ctrl_en = 0u;
	}

	ret = peci_pcs_write(priv->mgr, PECI_MBX_INDEX_DDR_RAPL_PL1,
			     PECI_PCS_PARAM_ZERO, power_limit.value);
	if (ret) {
		dev_dbg(priv->dev, "not able to write power limit\n");
		return ret;
	}

	dev_dbg(priv->dev, "power limit %d, unit %u, raw power limit %u,\n",
		val, priv->units.bits.pwr_unit, power_limit.bits.pp_pwr_lim);

	return ret;
}

static int
peci_dimmpower_read_max_power(void *ctx, struct peci_sensor_conf *sensor_conf,
			      struct peci_sensor_data *sensor_data)
{
	struct peci_dimmpower *priv = (struct peci_dimmpower *)ctx;
	union peci_dram_power_info_high power_info;
	ulong jif;
	int ret;

	if (!peci_sensor_need_update_with_time(sensor_data,
					       sensor_conf->update_interval)) {
		dev_dbg(priv->dev, "skip reading peci, max power %dmW\n",
			sensor_data->value);
		return 0;
	}

	ret = peci_pcs_get_units(priv->mgr, &priv->units, &priv->units_valid);
	if (ret) {
		dev_dbg(priv->dev, "not able to read units\n");
		return ret;
	}

	jif = jiffies;
	ret = peci_pcs_read(priv->mgr, PECI_MBX_INDEX_DDR_PWR_INFO_HIGH,
			    PECI_PCS_PARAM_ZERO, &power_info.value);
	if (ret) {
		dev_dbg(priv->dev, "not able to read power info\n");
		return ret;
	}

	sensor_data->value = peci_pcs_xn_to_munits(power_info.bits.max_pwr,
						   priv->units.bits.pwr_unit);
	peci_sensor_mark_updated_with_time(sensor_data, jif);

	dev_dbg(priv->dev, "raw max power %u, unit %u, max power %dmW\n",
		power_info.bits.max_pwr, priv->units.bits.pwr_unit,
		sensor_data->value);

	return ret;
}

static int
peci_dimmpower_read_min_power(void *ctx, struct peci_sensor_conf *sensor_conf,
			      struct peci_sensor_data *sensor_data)
{
	struct peci_dimmpower *priv = (struct peci_dimmpower *)ctx;
	union peci_dram_power_info_low power_info;
	ulong jif;
	int ret;

	if (!peci_sensor_need_update_with_time(sensor_data,
					       sensor_conf->update_interval)) {
		dev_dbg(priv->dev, "skip reading peci, min power %dmW\n",
			sensor_data->value);
		return 0;
	}

	ret = peci_pcs_get_units(priv->mgr, &priv->units, &priv->units_valid);
	if (ret) {
		dev_dbg(priv->dev, "not able to read units\n");
		return ret;
	}

	jif = jiffies;
	ret = peci_pcs_read(priv->mgr, PECI_MBX_INDEX_DDR_PWR_INFO_LOW,
			    PECI_PCS_PARAM_ZERO, &power_info.value);
	if (ret) {
		dev_dbg(priv->dev, "not able to read power info\n");
		return ret;
	}

	sensor_data->value = peci_pcs_xn_to_munits(power_info.bits.min_pwr,
						   priv->units.bits.pwr_unit);
	peci_sensor_mark_updated_with_time(sensor_data, jif);

	dev_dbg(priv->dev, "raw min power %u, unit %u, min power %dmW\n",
		power_info.bits.min_pwr, priv->units.bits.pwr_unit,
		sensor_data->value);

	return ret;
}

static struct peci_sensor_conf
peci_dimmpower_cfg[PECI_DIMMPOWER_CHANNEL_COUNT]
		  [PECI_DIMMPOWER_SENSOR_COUNT] = {
	/* Channel 0  - Power */
	{
		{
			.attribute = hwmon_power_average,
			.config = HWMON_P_AVERAGE,
			.update_interval = UPDATE_INTERVAL_100MS,
			.read = peci_dimmpower_get_avg_power,
			.write = NULL,
		},
		{
			.attribute = hwmon_power_cap,
			.config = HWMON_P_CAP,
			.update_interval = UPDATE_INTERVAL_100MS,
			.read = peci_dimmpower_get_power_limit,
			.write = peci_dimmpower_set_power_limit,
		},
		{
			.attribute = hwmon_power_cap_max,
			.config = HWMON_P_CAP_MAX,
			.update_interval = UPDATE_INTERVAL_10S,
			.read = peci_dimmpower_read_max_power,
			.write = NULL,
		},
		{
			.attribute = hwmon_power_cap_min,
			.config = HWMON_P_CAP_MIN,
			.update_interval = UPDATE_INTERVAL_10S,
			.read = peci_dimmpower_read_min_power,
			.write = NULL,
		},
	},
};

static int
peci_dimmpower_read_string(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, const char **str)
{
	if (attr != hwmon_power_label ||
	    channel >= PECI_DIMMPOWER_CHANNEL_COUNT)
		return -EOPNOTSUPP;

	if (str)
		*str = peci_dimmpower_labels[channel];
	else
		return -EINVAL;

	return 0;
}

static int
peci_dimmpower_read(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int channel, long *val)
{
	struct peci_dimmpower *priv = dev_get_drvdata(dev);
	struct peci_sensor_conf *sensor_conf;
	struct peci_sensor_data *sensor_data;
	int ret;

	if (!priv || !val)
		return -EINVAL;

	if (channel >= PECI_DIMMPOWER_CHANNEL_COUNT)
		return -EOPNOTSUPP;

	ret = peci_sensor_get_ctx(attr, peci_dimmpower_cfg[channel],
				  &sensor_conf, priv->sensor_data_list[channel],
				  &sensor_data,
				  ARRAY_SIZE(peci_dimmpower_cfg[channel]));
	if (ret)
		return ret;

	if (sensor_conf->read) {
		ret = sensor_conf->read(priv, sensor_conf, sensor_data);
		if (!ret)
			*val = (long)sensor_data->value;
	} else {
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int
peci_dimmpower_write(struct device *dev, enum hwmon_sensor_types type,
		     u32 attr, int channel, long val)
{
	struct peci_dimmpower *priv = dev_get_drvdata(dev);
	struct peci_sensor_conf *sensor_conf;
	struct peci_sensor_data *sensor_data;
	int ret;

	if (!priv)
		return -EINVAL;

	if (channel >= PECI_DIMMPOWER_CHANNEL_COUNT)
		return -EOPNOTSUPP;

	ret = peci_sensor_get_ctx(attr, peci_dimmpower_cfg[channel],
				  &sensor_conf, priv->sensor_data_list[channel],
				  &sensor_data,
				  ARRAY_SIZE(peci_dimmpower_cfg[channel]));
	if (ret)
		return ret;

	if (sensor_conf->write) {
		ret = sensor_conf->write(priv, sensor_conf, sensor_data,
					 (s32)val);
	} else {
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static umode_t
peci_dimmpower_is_visible(const void *data, enum hwmon_sensor_types type,
			  u32 attr, int channel)
{
	struct peci_sensor_conf *sensor_conf;
	umode_t mode = 0;
	int ret;

	if (channel >= PECI_DIMMPOWER_CHANNEL_COUNT)
		return mode;

	if (attr == hwmon_power_label)
		return 0444;

	ret = peci_sensor_get_ctx(attr, peci_dimmpower_cfg[channel],
				  &sensor_conf, NULL, NULL,
				  ARRAY_SIZE(peci_dimmpower_cfg[channel]));
	if (!ret) {
		if (sensor_conf->read)
			mode |= 0444;
		if (sensor_conf->write)
			mode |= 0200;
	}

	return mode;
}

static const struct hwmon_ops peci_dimmpower_ops = {
	.is_visible = peci_dimmpower_is_visible,
	.read_string = peci_dimmpower_read_string,
	.read = peci_dimmpower_read,
	.write = peci_dimmpower_write,
};

static int peci_dimmpower_probe(struct platform_device *pdev)
{
	struct peci_client_manager *mgr = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct peci_dimmpower *priv;
	struct device *hwmon_dev;
	u32 cmd_mask;

	cmd_mask = BIT(PECI_CMD_RD_PKG_CFG) | BIT(PECI_CMD_WR_PKG_CFG);
	if ((mgr->client->adapter->cmd_mask & cmd_mask) != cmd_mask)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->mgr = mgr;
	priv->dev = dev;

	snprintf(priv->name, PECI_NAME_SIZE, "peci_dimmpower.cpu%d",
		 mgr->client->addr - PECI_BASE_ADDR);

	priv->power_config[priv->config_idx] = HWMON_P_LABEL |
		peci_sensor_get_config(peci_dimmpower_cfg[priv->config_idx],
				       ARRAY_SIZE(peci_dimmpower_cfg
						  [priv->config_idx]));
	priv->config_idx++;

	priv->chip.ops = &peci_dimmpower_ops;
	priv->chip.info = priv->info;
	priv->info[0] = &priv->power_info;

	priv->power_info.type = hwmon_power;
	priv->power_info.config = priv->power_config;

	hwmon_dev = devm_hwmon_device_register_with_info(priv->dev, priv->name,
							 priv, &priv->chip,
							 NULL);

	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_dbg(dev, "%s: sensor '%s'\n", dev_name(hwmon_dev), priv->name);

	return 0;
}

static const struct platform_device_id peci_dimmpower_ids[] = {
	{ .name = "peci-dimmpower", .driver_data = 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, peci_dimmpower_ids);

static struct platform_driver peci_dimmpower_driver = {
	.probe    = peci_dimmpower_probe,
	.id_table = peci_dimmpower_ids,
	.driver   = { .name = KBUILD_MODNAME, },
};
module_platform_driver(peci_dimmpower_driver);

MODULE_AUTHOR("Zbigniew Lukwinski <zbigniew.lukwinski@linux.intel.com>");
MODULE_DESCRIPTION("PECI dimmpower driver");
MODULE_LICENSE("GPL v2");
