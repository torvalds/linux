/*
 * System Control and Power Interface (SCPI) based CPUFreq Interface driver
 *
 * It provides necessary ops to arm_big_little cpufreq driver.
 *
 * Copyright (C) 2015 ARM Ltd.
 * Sudeep Holla <sudeep.holla@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/scpi_protocol.h>
#include <linux/types.h>

#include "arm_big_little.h"

static struct scpi_ops *scpi_ops;

static struct scpi_dvfs_info *scpi_get_dvfs_info(struct device *cpu_dev)
{
	u8 domain = topology_physical_package_id(cpu_dev->id);

	if (domain < 0)
		return ERR_PTR(-EINVAL);
	return scpi_ops->dvfs_get_info(domain);
}

static int scpi_opp_table_ops(struct device *cpu_dev, bool remove)
{
	int idx, ret = 0;
	struct scpi_opp *opp;
	struct scpi_dvfs_info *info = scpi_get_dvfs_info(cpu_dev);

	if (IS_ERR(info))
		return PTR_ERR(info);

	if (!info->opps)
		return -EIO;

	for (opp = info->opps, idx = 0; idx < info->count; idx++, opp++) {
		if (remove)
			dev_pm_opp_remove(cpu_dev, opp->freq);
		else
			ret = dev_pm_opp_add(cpu_dev, opp->freq,
					     opp->m_volt * 1000);
		if (ret) {
			dev_warn(cpu_dev, "failed to add opp %uHz %umV\n",
				 opp->freq, opp->m_volt);
			while (idx-- > 0)
				dev_pm_opp_remove(cpu_dev, (--opp)->freq);
			return ret;
		}
	}
	return ret;
}

static int scpi_get_transition_latency(struct device *cpu_dev)
{
	struct scpi_dvfs_info *info = scpi_get_dvfs_info(cpu_dev);

	if (IS_ERR(info))
		return PTR_ERR(info);
	return info->latency;
}

static int scpi_init_opp_table(struct device *cpu_dev)
{
	return scpi_opp_table_ops(cpu_dev, false);
}

static void scpi_free_opp_table(struct device *cpu_dev)
{
	scpi_opp_table_ops(cpu_dev, true);
}

static struct cpufreq_arm_bL_ops scpi_cpufreq_ops = {
	.name	= "scpi",
	.get_transition_latency = scpi_get_transition_latency,
	.init_opp_table = scpi_init_opp_table,
	.free_opp_table = scpi_free_opp_table,
};

static int scpi_cpufreq_probe(struct platform_device *pdev)
{
	scpi_ops = get_scpi_ops();
	if (!scpi_ops)
		return -EIO;

	return bL_cpufreq_register(&scpi_cpufreq_ops);
}

static int scpi_cpufreq_remove(struct platform_device *pdev)
{
	bL_cpufreq_unregister(&scpi_cpufreq_ops);
	scpi_ops = NULL;
	return 0;
}

static struct platform_driver scpi_cpufreq_platdrv = {
	.driver = {
		.name	= "scpi-cpufreq",
		.owner	= THIS_MODULE,
	},
	.probe		= scpi_cpufreq_probe,
	.remove		= scpi_cpufreq_remove,
};
module_platform_driver(scpi_cpufreq_platdrv);

MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("ARM SCPI CPUFreq interface driver");
MODULE_LICENSE("GPL v2");
