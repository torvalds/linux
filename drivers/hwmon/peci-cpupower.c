// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2020 Intel Corporation

#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/mfd/intel-peci-client.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "peci-hwmon.h"

enum PECI_CPUPOWER_POWER_SENSOR_TYPES {
	PECI_CPUPOWER_SENSOR_TYPE_POWER = 0,
	PECI_CPUPOWER_SENSOR_TYPE_ENERGY,
	PECI_CPUPOWER_SENSOR_TYPES_COUNT,
};

#define PECI_CPUPOWER_POWER_CHANNEL_COUNT	1 /* Supported channels number */
#define PECI_CPUPOWER_ENERGY_CHANNEL_COUNT	1 /* Supported channels number */

#define PECI_CPUPOWER_POWER_SENSOR_COUNT	4 /* Supported sensors number */
#define PECI_CPUPOWER_ENERGY_SENSOR_COUNT	1 /* Supported sensors number */

struct peci_cpupower {
	struct device *dev;
	struct peci_client_manager *mgr;
	char name[PECI_NAME_SIZE];
	u32 power_config[PECI_CPUPOWER_POWER_CHANNEL_COUNT + 1];
	u32 energy_config[PECI_CPUPOWER_ENERGY_CHANNEL_COUNT + 1];

	struct hwmon_channel_info power_info;
	struct hwmon_channel_info energy_info;
	const struct hwmon_channel_info *info[PECI_CPUPOWER_SENSOR_TYPES_COUNT + 1];
	struct hwmon_chip_info chip;

	struct peci_sensor_data
		power_sensor_data_list[PECI_CPUPOWER_POWER_CHANNEL_COUNT]
				      [PECI_CPUPOWER_POWER_SENSOR_COUNT];
	struct peci_sensor_data
		energy_sensor_data_list[PECI_CPUPOWER_ENERGY_CHANNEL_COUNT]
				       [PECI_CPUPOWER_ENERGY_SENSOR_COUNT];

	/* Below structs are not exposed to any sensor directly */
	struct peci_sensor_data energy_cache; /* used to limit PECI communication */
	struct peci_sensor_data power_sensor_prev_energy;
	struct peci_sensor_data energy_sensor_prev_energy;

	union peci_pkg_power_sku_unit units;
	bool units_valid;

	u32 ppl1_time_window;
	u32 ppl2_time_window;
	bool ppl_time_windows_valid;
};

static const char *peci_cpupower_labels[PECI_CPUPOWER_SENSOR_TYPES_COUNT] = {
	"cpu power",
	"cpu energy",
};

/**
 * peci_cpupower_read_cpu_pkg_pwr_info_low - read PCS Platform Power SKU low.
 * @peci_mgr: PECI client manager handle
 * @reg: Pointer to the variable read value is going to be put
 *
 * Return: 0 if succeeded, other values in case an error.
 */
static inline int
peci_cpupower_read_cpu_pkg_pwr_info_low(struct peci_client_manager *peci_mgr,
					union peci_package_power_info_low *reg)
{
	return peci_pcs_read(peci_mgr, PECI_MBX_INDEX_TDP,
			     PECI_PKG_ID_CPU_PACKAGE, &reg->value);
}

/**
 * peci_cpupower_read_cpu_pkg_pwr_lim_low - read PCS Package Power Limit Low
 * @peci_mgr: PECI client manager handle
 * @reg: Pointer to the variable read value is going to be put
 *
 * Return: 0 if succeeded, other values in case an error.
 */
static inline int
peci_cpupower_read_cpu_pkg_pwr_lim_low(struct peci_client_manager *peci_mgr,
				       union peci_package_power_limit_low *reg)
{
	return peci_pcs_read(peci_mgr, PECI_MBX_INDEX_PKG_POWER_LIMIT1,
			     PECI_PCS_PARAM_ZERO, &reg->value);
}

static int
peci_cpupower_get_energy_counter(struct peci_cpupower *priv,
				 struct peci_sensor_data *sensor_data,
				 ulong update_interval)
{
	int ret;

	if (!peci_sensor_need_update_with_time(sensor_data,
					       update_interval)) {
		dev_dbg(priv->dev, "skip reading package energy over peci\n");
		return 0;
	}

	ret = peci_pcs_read(priv->mgr, PECI_MBX_INDEX_ENERGY_COUNTER,
			    PECI_PKG_ID_CPU_PACKAGE, &sensor_data->uvalue);
	if (ret) {
		dev_dbg(priv->dev, "not able to read package energy\n");
		return ret;
	}

	peci_sensor_mark_updated(sensor_data);

	dev_dbg(priv->dev,
		"energy counter updated %duJ, jif %lu, HZ is %d jiffies\n",
		sensor_data->uvalue, sensor_data->last_updated, HZ);

	return ret;
}

static int
peci_cpupower_get_average_power(void *ctx,
				struct peci_sensor_conf *sensor_conf,
				struct peci_sensor_data *sensor_data)
{
	struct peci_cpupower *priv = (struct peci_cpupower *)ctx;
	int ret;

	if (!peci_sensor_need_update_with_time(sensor_data,
					       sensor_conf->update_interval)) {
		dev_dbg(priv->dev,
			"skip generating new power value %dmW jif %lu\n",
			sensor_data->value, jiffies);
		return 0;
	}

	ret = peci_cpupower_get_energy_counter(priv, &priv->energy_cache,
					       sensor_conf->update_interval);
	if (ret) {
		dev_dbg(priv->dev, "cannot update energy counter\n");
		return ret;
	}

	ret = peci_pcs_get_units(priv->mgr, &priv->units, &priv->units_valid);
	if (ret) {
		dev_dbg(priv->dev, "not able to read units\n");
		return ret;
	}

	ret = peci_pcs_calc_pwr_from_eng(priv->dev,
					 &priv->power_sensor_prev_energy,
					 &priv->energy_cache,
					 priv->units.bits.eng_unit,
					 &sensor_data->value);
	if (ret) {
		dev_dbg(priv->dev, "power calculation failed\n");
		return ret;
	}

	peci_sensor_mark_updated_with_time(sensor_data,
					   priv->energy_cache.last_updated);

	dev_dbg(priv->dev, "average power %dmW, jif %lu, HZ is %d jiffies\n",
		sensor_data->value, sensor_data->last_updated, HZ);

	return ret;
}

static int
peci_cpupower_get_power_limit(void *ctx, struct peci_sensor_conf *sensor_conf,
			      struct peci_sensor_data *sensor_data)
{
	struct peci_cpupower *priv = (struct peci_cpupower *)ctx;
	union peci_package_power_limit_low power_limit;
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

	ret = peci_cpupower_read_cpu_pkg_pwr_lim_low(priv->mgr, &power_limit);
	if (ret) {
		dev_dbg(priv->dev, "not able to read power limit\n");
		return ret;
	}

	peci_sensor_mark_updated(sensor_data);
	sensor_data->value = peci_pcs_xn_to_munits(power_limit.bits.pwr_lim_1,
						   priv->units.bits.pwr_unit);

	dev_dbg(priv->dev, "raw power limit %u, unit %u, power limit %d\n",
		power_limit.bits.pwr_lim_1, priv->units.bits.pwr_unit,
		sensor_data->value);

	return ret;
}

static int
peci_cpupower_set_power_limit(void *ctx, struct peci_sensor_conf *sensor_conf,
			      struct peci_sensor_data *sensor_data,
			      s32 val)
{
	struct peci_cpupower *priv = (struct peci_cpupower *)ctx;
	union peci_package_power_limit_high power_limit_high;
	union peci_package_power_limit_low power_limit_low;
	int ret;

	ret = peci_pcs_get_units(priv->mgr, &priv->units, &priv->units_valid);
	if (ret) {
		dev_dbg(priv->dev, "not able to read units\n");
		return ret;
	}

	ret = peci_cpupower_read_cpu_pkg_pwr_lim_low(priv->mgr,
						     &power_limit_low);
	if (ret) {
		dev_dbg(priv->dev, "not able to read package power limit 1\n");
		return ret;
	}

	ret = peci_pcs_read(priv->mgr, PECI_MBX_INDEX_PKG_POWER_LIMIT2,
			    PECI_PCS_PARAM_ZERO, &power_limit_high.value);
	if (ret) {
		dev_dbg(priv->dev, "not able to read package power limit 2\n");
		return ret;
	}

	/* Calculate PPL time windows if needed */
	if (!priv->ppl_time_windows_valid) {
		priv->ppl1_time_window =
			peci_pcs_calc_plxy_time_window(peci_pcs_munits_to_xn(
				PECI_PCS_PPL1_TIME_WINDOW,
				priv->units.bits.tim_unit));
		priv->ppl2_time_window =
			peci_pcs_calc_plxy_time_window(peci_pcs_munits_to_xn(
				PECI_PCS_PPL2_TIME_WINDOW,
				priv->units.bits.tim_unit));
		priv->ppl_time_windows_valid = true;
	}

	/* Enable or disable power limitation */
	if (val > 0) {
		/* Set PPL1 */
		power_limit_low.bits.pwr_lim_1 =
			peci_pcs_munits_to_xn(val, priv->units.bits.pwr_unit);
		power_limit_low.bits.pwr_lim_1_en = 1u;
		power_limit_low.bits.pwr_clmp_lim_1 = 1u;
		power_limit_low.bits.pwr_lim_1_time = priv->ppl1_time_window;

		/* Set PPL2 */
		power_limit_high.bits.pwr_lim_2 =
			peci_pcs_munits_to_xn(PECI_PCS_PPL1_TO_PPL2(val),
					      priv->units.bits.pwr_unit);
		power_limit_high.bits.pwr_lim_2_en = 1u;
		power_limit_high.bits.pwr_clmp_lim_2 = 1u;
		power_limit_high.bits.pwr_lim_2_time = priv->ppl2_time_window;
	} else {
		power_limit_low.bits.pwr_lim_1 = 0u;
		power_limit_low.bits.pwr_lim_1_en = 0u;
		power_limit_low.bits.pwr_clmp_lim_1 = 0u;
		power_limit_low.bits.pwr_lim_1_time = 0u;
		power_limit_high.bits.pwr_lim_2 = 0u;
		power_limit_high.bits.pwr_lim_2_en = 0u;
		power_limit_high.bits.pwr_clmp_lim_2 = 0u;
		power_limit_high.bits.pwr_lim_2_time = 0u;
	}

	ret = peci_pcs_write(priv->mgr, PECI_MBX_INDEX_PKG_POWER_LIMIT1,
			     PECI_PCS_PARAM_ZERO, power_limit_low.value);
	if (ret) {
		dev_dbg(priv->dev, "not able to write package power limit 1\n");
		return ret;
	}

	ret = peci_pcs_write(priv->mgr, PECI_MBX_INDEX_PKG_POWER_LIMIT2,
			     PECI_PCS_PARAM_ZERO, power_limit_high.value);
	if (ret) {
		dev_dbg(priv->dev, "not able to write package power limit 2\n");
		return ret;
	}

	dev_dbg(priv->dev,
		"power limit %d, unit %u, raw package power limit 1 %u,\n",
		val, priv->units.bits.pwr_unit, power_limit_low.bits.pwr_lim_1);

	return ret;
}

static int
peci_cpupower_read_max_power(void *ctx, struct peci_sensor_conf *sensor_conf,
			     struct peci_sensor_data *sensor_data)
{
	struct peci_cpupower *priv = (struct peci_cpupower *)ctx;
	union peci_package_power_info_low power_info;
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

	ret = peci_cpupower_read_cpu_pkg_pwr_info_low(priv->mgr, &power_info);
	if (ret) {
		dev_dbg(priv->dev, "not able to read package power info\n");
		return ret;
	}

	peci_sensor_mark_updated(sensor_data);
	sensor_data->value = peci_pcs_xn_to_munits(power_info.bits.pkg_tdp,
						   priv->units.bits.pwr_unit);


	dev_dbg(priv->dev, "raw max power %u, unit %u, max power %dmW\n",
		power_info.bits.pkg_tdp, priv->units.bits.pwr_unit,
		sensor_data->value);

	return ret;
}

static int
peci_cpupower_read_min_power(void *ctx, struct peci_sensor_conf *sensor_conf,
			     struct peci_sensor_data *sensor_data)
{
	struct peci_cpupower *priv = (struct peci_cpupower *)ctx;
	union peci_package_power_info_low power_info;
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

	ret = peci_cpupower_read_cpu_pkg_pwr_info_low(priv->mgr, &power_info);
	if (ret) {
		dev_dbg(priv->dev, "not able to read package power info\n");
		return ret;
	}

	peci_sensor_mark_updated(sensor_data);
	sensor_data->value = peci_pcs_xn_to_munits(power_info.bits.pkg_min_pwr,
						   priv->units.bits.pwr_unit);

	dev_dbg(priv->dev, "raw min power %u, unit %u, min power %dmW\n",
		power_info.bits.pkg_min_pwr, priv->units.bits.pwr_unit,
		sensor_data->value);

	return ret;
}

static int
peci_cpupower_read_energy(void *ctx, struct peci_sensor_conf *sensor_conf,
			  struct peci_sensor_data *sensor_data)
{
	struct peci_cpupower *priv = (struct peci_cpupower *)ctx;
	int ret;

	if (!peci_sensor_need_update_with_time(sensor_data,
					       sensor_conf->update_interval)) {
		dev_dbg(priv->dev,
			"skip generating new energy value %duJ jif %lu\n",
			sensor_data->value, jiffies);
		return 0;
	}

	ret = peci_cpupower_get_energy_counter(priv, &priv->energy_cache,
					       sensor_conf->update_interval);
	if (ret) {
		dev_dbg(priv->dev, "cannot update energy counter\n");
		return ret;
	}

	ret = peci_pcs_get_units(priv->mgr, &priv->units, &priv->units_valid);
	if (ret) {
		dev_dbg(priv->dev, "not able to read units\n");
		return ret;
	}

	ret = peci_pcs_calc_acc_eng(priv->dev,
				    &priv->energy_sensor_prev_energy,
				    &priv->energy_cache,
				    priv->units.bits.eng_unit,
				    &sensor_data->uvalue);

	if (ret) {
		dev_dbg(priv->dev, "cumulative energy calculation failed\n");
		return ret;
	}

	peci_sensor_mark_updated_with_time(sensor_data,
					   priv->energy_cache.last_updated);

	dev_dbg(priv->dev, "energy %duJ, jif %lu, HZ is %d jiffies\n",
		sensor_data->uvalue, sensor_data->last_updated, HZ);

	return 0;
}

static struct peci_sensor_conf
peci_cpupower_power_cfg[PECI_CPUPOWER_POWER_CHANNEL_COUNT]
		       [PECI_CPUPOWER_POWER_SENSOR_COUNT] = {
	/* Channel 0  - Power */
	{
		{
			.attribute = hwmon_power_average,
			.config = HWMON_P_AVERAGE,
			.update_interval = UPDATE_INTERVAL_100MS,
			.read = peci_cpupower_get_average_power,
			.write = NULL,
		},
		{
			.attribute = hwmon_power_cap,
			.config = HWMON_P_CAP,
			.update_interval = UPDATE_INTERVAL_100MS,
			.read = peci_cpupower_get_power_limit,
			.write = peci_cpupower_set_power_limit,
		},
		{
			.attribute = hwmon_power_cap_max,
			.config = HWMON_P_CAP_MAX,
			.update_interval = UPDATE_INTERVAL_10S,
			.read = peci_cpupower_read_max_power,
			.write = NULL,
		},
		{
			.attribute = hwmon_power_cap_min,
			.config = HWMON_P_CAP_MIN,
			.update_interval = UPDATE_INTERVAL_10S,
			.read = peci_cpupower_read_min_power,
			.write = NULL,
		},
	},
};

static struct peci_sensor_conf
peci_cpupower_energy_cfg[PECI_CPUPOWER_ENERGY_CHANNEL_COUNT]
		[PECI_CPUPOWER_ENERGY_SENSOR_COUNT] = {
	/* Channel 0  - Energy */
	{
		{
			.attribute = hwmon_energy_input,
			.config = HWMON_E_INPUT,
			.update_interval = UPDATE_INTERVAL_100MS,
			.read = peci_cpupower_read_energy,
			.write = NULL,
		},
	}
};

static bool
peci_cpupower_is_channel_valid(enum hwmon_sensor_types type,
			       int channel)
{
	if ((type == hwmon_power && channel < PECI_CPUPOWER_POWER_CHANNEL_COUNT) ||
	    (type == hwmon_energy && channel < PECI_CPUPOWER_ENERGY_CHANNEL_COUNT))
		return true;

	return false;
}

static int
peci_cpupower_read_string(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, const char **str)
{
	if (!peci_cpupower_is_channel_valid(type, channel))
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_power_label:
		*str = peci_cpupower_labels[PECI_CPUPOWER_SENSOR_TYPE_POWER];
		break;
	case hwmon_energy_label:
		*str = peci_cpupower_labels[PECI_CPUPOWER_SENSOR_TYPE_ENERGY];
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
peci_cpupower_read(struct device *dev, enum hwmon_sensor_types type,
		   u32 attr, int channel, long *val)
{
	struct peci_cpupower *priv = dev_get_drvdata(dev);
	struct peci_sensor_conf *sensor_conf;
	struct peci_sensor_data *sensor_data;
	int ret;

	if (!priv || !val)
		return -EINVAL;

	if (!peci_cpupower_is_channel_valid(type, channel))
		return -EOPNOTSUPP;

	switch (type) {
	case hwmon_power:
		ret = peci_sensor_get_ctx(attr, peci_cpupower_power_cfg[channel],
					  &sensor_conf,
					  priv->power_sensor_data_list[channel],
					  &sensor_data,
					  ARRAY_SIZE(peci_cpupower_power_cfg[channel]));
		break;
	case hwmon_energy:
		ret = peci_sensor_get_ctx(attr, peci_cpupower_energy_cfg[channel],
					  &sensor_conf,
					  priv->energy_sensor_data_list[channel],
					  &sensor_data,
					  ARRAY_SIZE(peci_cpupower_energy_cfg[channel]));
		break;
	default:
		ret = -EOPNOTSUPP;
	}

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
peci_cpupower_write(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int channel, long val)
{
	struct peci_cpupower *priv = dev_get_drvdata(dev);
	struct peci_sensor_conf *sensor_conf;
	struct peci_sensor_data *sensor_data;
	int ret;

	if (!priv)
		return -EINVAL;

	if (!peci_cpupower_is_channel_valid(type, channel))
		return -EOPNOTSUPP;

	switch (type) {
	case hwmon_power:
		ret = peci_sensor_get_ctx(attr, peci_cpupower_power_cfg[channel],
					  &sensor_conf,
					  priv->power_sensor_data_list[channel],
					  &sensor_data,
					  ARRAY_SIZE(peci_cpupower_power_cfg[channel]));
		break;
	case hwmon_energy:
		ret = peci_sensor_get_ctx(attr, peci_cpupower_energy_cfg[channel],
					  &sensor_conf,
					  priv->energy_sensor_data_list[channel],
					  &sensor_data,
					  ARRAY_SIZE(peci_cpupower_energy_cfg[channel]));
		break;
	default:
		ret = -EOPNOTSUPP;
	}

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
peci_cpupower_is_visible(const void *data, enum hwmon_sensor_types type,
			 u32 attr, int channel)
{
	struct peci_sensor_conf *sensor_conf;
	umode_t mode = 0;
	int ret;

	if (!peci_cpupower_is_channel_valid(type, channel))
		return mode;

	if (attr == hwmon_power_label || attr == hwmon_energy_label)
		return 0444;

	switch (type) {
	case hwmon_power:
		ret = peci_sensor_get_ctx(attr, peci_cpupower_power_cfg[channel],
					  &sensor_conf, NULL, NULL,
					  ARRAY_SIZE(peci_cpupower_power_cfg[channel]));
		break;
	case hwmon_energy:
		ret = peci_sensor_get_ctx(attr, peci_cpupower_energy_cfg[channel],
					  &sensor_conf, NULL, NULL,
					  ARRAY_SIZE(peci_cpupower_energy_cfg[channel]));
		break;
	default:
		return mode;
	}

	if (!ret) {
		if (sensor_conf->read)
			mode |= 0444;
		if (sensor_conf->write)
			mode |= 0200;
	}

	return mode;
}

static const struct hwmon_ops peci_cpupower_ops = {
	.is_visible = peci_cpupower_is_visible,
	.read_string = peci_cpupower_read_string,
	.read = peci_cpupower_read,
	.write = peci_cpupower_write,
};

static int peci_cpupower_probe(struct platform_device *pdev)
{
	struct peci_client_manager *mgr = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct peci_cpupower *priv;
	struct device *hwmon_dev;
	u32 power_cfg_idx = 0;
	u32 energy_cfg_idx = 0;
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

	snprintf(priv->name, PECI_NAME_SIZE, "peci_cpupower.cpu%d",
		 mgr->client->addr - PECI_BASE_ADDR);

	priv->power_config[power_cfg_idx] = HWMON_P_LABEL |
		peci_sensor_get_config(peci_cpupower_power_cfg[power_cfg_idx],
				       ARRAY_SIZE(peci_cpupower_power_cfg[power_cfg_idx]));

	priv->energy_config[energy_cfg_idx] = HWMON_E_LABEL |
		peci_sensor_get_config(peci_cpupower_energy_cfg[energy_cfg_idx],
				       ARRAY_SIZE(peci_cpupower_energy_cfg[energy_cfg_idx]));

	priv->info[PECI_CPUPOWER_SENSOR_TYPE_POWER] = &priv->power_info;
	priv->power_info.type = hwmon_power;
	priv->power_info.config = priv->power_config;

	priv->info[PECI_CPUPOWER_SENSOR_TYPE_ENERGY] = &priv->energy_info;
	priv->energy_info.type = hwmon_energy;
	priv->energy_info.config = priv->energy_config;

	priv->chip.ops = &peci_cpupower_ops;
	priv->chip.info = priv->info;

	hwmon_dev = devm_hwmon_device_register_with_info(priv->dev, priv->name,
							 priv, &priv->chip,
							 NULL);

	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_dbg(dev, "%s: sensor '%s'\n", dev_name(hwmon_dev), priv->name);

	return 0;
}

static const struct platform_device_id peci_cpupower_ids[] = {
	{ .name = "peci-cpupower", .driver_data = 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, peci_cpupower_ids);

static struct platform_driver peci_cpupower_driver = {
	.probe    = peci_cpupower_probe,
	.id_table = peci_cpupower_ids,
	.driver   = { .name = KBUILD_MODNAME, },
};
module_platform_driver(peci_cpupower_driver);

MODULE_AUTHOR("Zhikui Ren <zhikui.ren@intel.com>");
MODULE_DESCRIPTION("PECI cpupower driver");
MODULE_LICENSE("GPL v2");
