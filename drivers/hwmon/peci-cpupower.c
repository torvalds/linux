// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2020 Intel Corporation

#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/mfd/intel-peci-client.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "peci-hwmon.h"

#define POWER_DEFAULT_CHANNEL_NUMS   1

struct peci_cpupower {
	struct peci_client_manager *mgr;
	struct device *dev;
	char name[PECI_NAME_SIZE];
	const struct cpu_gen_info *gen_info;
	struct peci_sensor_data energy;
	long avg_power_val;
	u64 core_mask;
	u32 power_config[POWER_DEFAULT_CHANNEL_NUMS + 1];
	uint config_idx;
	struct hwmon_channel_info power_info;
	const struct hwmon_channel_info *info[2];
	struct hwmon_chip_info chip;
};

enum cpupower_channels {
	average_power,
};

static const u32 config_table[POWER_DEFAULT_CHANNEL_NUMS] = {
	/* average power */
	HWMON_P_LABEL | HWMON_P_AVERAGE,
};

static const char *cpupower_label[POWER_DEFAULT_CHANNEL_NUMS] = {
	"Average Power",
};

static int get_average_power(struct peci_cpupower *priv)
{
	u8  pkg_cfg[4];
	int ret;

	if (!peci_sensor_need_update(&priv->energy))
		return 0;

	ret = peci_client_read_package_config(priv->mgr,
					      PECI_MBX_INDEX_TDP_UNITS,
					      PECI_PKG_ID_PKG_ENERGY_STATUS,
					      pkg_cfg);

	u32 power_unit = ((le32_to_cpup((__le32 *)pkg_cfg)) & 0x1f00) >> 8;

	dev_dbg(priv->dev, "cpupower units %d  (1J/pow(2, unit))\n",
		power_unit);

	ret = peci_client_read_package_config(priv->mgr,
					      PECI_MBX_INDEX_ENERGY_COUNTER,
					      PECI_PKG_ID_PKG_ENERGY_STATUS,
					      pkg_cfg);
	if (!ret) {
		u32 energy_cnt = le32_to_cpup((__le32 *)pkg_cfg);
		ulong jif = jiffies;
		ulong elapsed = (jif - priv->energy.last_updated);
		long power_val = 0;
		/*
		 * Don't calculate average power for first counter read or
		 * counter wrapped around or last counter read was more than
		 * 60 minutes ago (jiffies did not wrap and power calculation
		 * does not overflow or underflow
		 */
		if (priv->energy.last_updated > 0 &&
		    energy_cnt > priv->energy.value &&
		    (elapsed < (HZ * 3600))) {
			power_val = (long)(energy_cnt - priv->energy.value)
				/ elapsed * HZ;
			dev_dbg(priv->dev, "countDiff %d, jiffes elapsed %d, raw powerValue %d scale to %d mW\n",
				(long)(energy_cnt - priv->energy.value),
				elapsed, power_val,
				power_val >> (power_unit - 10));
		} else {
			dev_dbg(priv->dev, "countDiff %d, jiffes elapsed %d, skipping calculate power, try agin\n",
				(long)(energy_cnt - priv->energy.value),
				elapsed);
			ret = -EAGAIN;
		}

		priv->energy.value = energy_cnt;
		priv->avg_power_val = power_val >> ((power_unit - 10));
		peci_sensor_mark_updated(&priv->energy);

		dev_dbg(priv->dev, "energy counter 0x%8x, average power %dmW, jif %u, HZ is %d jiffies\n",
			priv->energy.value, priv->avg_power_val,
			jif, HZ);
	}
	return ret;
}

static int cpupower_read_string(struct device *dev,
				enum hwmon_sensor_types type,
				u32 attr, int channel, const char **str)
{
	if (attr != hwmon_power_label)
		return -EOPNOTSUPP;
	if (channel >= POWER_DEFAULT_CHANNEL_NUMS)
		return -EOPNOTSUPP;
	*str = cpupower_label[channel];

	return 0;
}

static int cpupower_read(struct device *dev,
			 enum hwmon_sensor_types type,
			 u32 attr, int channel, long *val)
{
	struct peci_cpupower *priv = dev_get_drvdata(dev);
	int ret;

	if (channel >= POWER_DEFAULT_CHANNEL_NUMS ||
	    !(priv->power_config[channel] & BIT(attr)))
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_power_average:
		switch (channel) {
		case average_power:
			ret = get_average_power(priv);
			if (ret)
				break;

			*val = priv->avg_power_val;
			break;
		default:
			break;
		}
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static umode_t cpupower_is_visible(const void *data,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	const struct peci_cpupower *priv = data;

	if (channel < POWER_DEFAULT_CHANNEL_NUMS ||
	    (priv->power_config[channel] & BIT(attr)))
		return 0444;

	return 0;
}

static const struct hwmon_ops cpupower_ops = {
	.is_visible = cpupower_is_visible,
	.read_string = cpupower_read_string,
	.read = cpupower_read,
};

static int peci_cpupower_probe(struct platform_device *pdev)
{
	struct peci_client_manager *mgr = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct peci_cpupower *priv;
	struct device *hwmon_dev;

	if ((mgr->client->adapter->cmd_mask &
				(BIT(PECI_CMD_RD_PKG_CFG))) !=
			(BIT(PECI_CMD_RD_PKG_CFG))) {
		return -ENODEV;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->mgr = mgr;
	priv->dev = dev;
	priv->gen_info = mgr->gen_info;

	snprintf(priv->name, PECI_NAME_SIZE, "peci_cpupower.cpu%d",
		 mgr->client->addr - PECI_BASE_ADDR);

	priv->power_config[priv->config_idx++] = config_table[average_power];

	priv->chip.ops = &cpupower_ops;
	priv->chip.info = priv->info;

	priv->info[0] = &priv->power_info;

	priv->power_info.type = hwmon_power;
	priv->power_info.config = priv->power_config;

	hwmon_dev = devm_hwmon_device_register_with_info(priv->dev,
							 priv->name,
							 priv,
							 &priv->chip,
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
