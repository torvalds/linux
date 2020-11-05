// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Power Interface (SCMI) based CPUFreq Interface driver
 *
 * Copyright (C) 2018 ARM Ltd.
 * Sudeep Holla <sudeep.holla@arm.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk-provider.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/energy_model.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>
#include <linux/scmi_protocol.h>
#include <linux/types.h>

struct scmi_data {
	int domain_id;
	struct device *cpu_dev;
};

static const struct scmi_handle *handle;

static unsigned int scmi_cpufreq_get_rate(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get_raw(cpu);
	const struct scmi_perf_ops *perf_ops = handle->perf_ops;
	struct scmi_data *priv = policy->driver_data;
	unsigned long rate;
	int ret;

	ret = perf_ops->freq_get(handle, priv->domain_id, &rate, false);
	if (ret)
		return 0;
	return rate / 1000;
}

/*
 * perf_ops->freq_set is not a synchronous, the actual OPP change will
 * happen asynchronously and can get notified if the events are
 * subscribed for by the SCMI firmware
 */
static int
scmi_cpufreq_set_target(struct cpufreq_policy *policy, unsigned int index)
{
	struct scmi_data *priv = policy->driver_data;
	const struct scmi_perf_ops *perf_ops = handle->perf_ops;
	u64 freq = policy->freq_table[index].frequency;

	return perf_ops->freq_set(handle, priv->domain_id, freq * 1000, false);
}

static unsigned int scmi_cpufreq_fast_switch(struct cpufreq_policy *policy,
					     unsigned int target_freq)
{
	struct scmi_data *priv = policy->driver_data;
	const struct scmi_perf_ops *perf_ops = handle->perf_ops;

	if (!perf_ops->freq_set(handle, priv->domain_id,
				target_freq * 1000, true))
		return target_freq;

	return 0;
}

static int
scmi_get_sharing_cpus(struct device *cpu_dev, struct cpumask *cpumask)
{
	int cpu, domain, tdomain;
	struct device *tcpu_dev;

	domain = handle->perf_ops->device_domain_id(cpu_dev);
	if (domain < 0)
		return domain;

	for_each_possible_cpu(cpu) {
		if (cpu == cpu_dev->id)
			continue;

		tcpu_dev = get_cpu_device(cpu);
		if (!tcpu_dev)
			continue;

		tdomain = handle->perf_ops->device_domain_id(tcpu_dev);
		if (tdomain == domain)
			cpumask_set_cpu(cpu, cpumask);
	}

	return 0;
}

static int __maybe_unused
scmi_get_cpu_power(unsigned long *power, unsigned long *KHz,
		   struct device *cpu_dev)
{
	unsigned long Hz;
	int ret, domain;

	domain = handle->perf_ops->device_domain_id(cpu_dev);
	if (domain < 0)
		return domain;

	/* Get the power cost of the performance domain. */
	Hz = *KHz * 1000;
	ret = handle->perf_ops->est_power_get(handle, domain, &Hz, power);
	if (ret)
		return ret;

	/* The EM framework specifies the frequency in KHz. */
	*KHz = Hz / 1000;

	return 0;
}

static int scmi_cpufreq_init(struct cpufreq_policy *policy)
{
	int ret, nr_opp;
	unsigned int latency;
	struct device *cpu_dev;
	struct scmi_data *priv;
	struct cpufreq_frequency_table *freq_table;
	struct em_data_callback em_cb = EM_DATA_CB(scmi_get_cpu_power);

	cpu_dev = get_cpu_device(policy->cpu);
	if (!cpu_dev) {
		pr_err("failed to get cpu%d device\n", policy->cpu);
		return -ENODEV;
	}

	ret = handle->perf_ops->device_opps_add(handle, cpu_dev);
	if (ret) {
		dev_warn(cpu_dev, "failed to add opps to the device\n");
		return ret;
	}

	ret = scmi_get_sharing_cpus(cpu_dev, policy->cpus);
	if (ret) {
		dev_warn(cpu_dev, "failed to get sharing cpumask\n");
		return ret;
	}

	ret = dev_pm_opp_set_sharing_cpus(cpu_dev, policy->cpus);
	if (ret) {
		dev_err(cpu_dev, "%s: failed to mark OPPs as shared: %d\n",
			__func__, ret);
		return ret;
	}

	nr_opp = dev_pm_opp_get_opp_count(cpu_dev);
	if (nr_opp <= 0) {
		dev_dbg(cpu_dev, "OPP table is not ready, deferring probe\n");
		ret = -EPROBE_DEFER;
		goto out_free_opp;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto out_free_opp;
	}

	ret = dev_pm_opp_init_cpufreq_table(cpu_dev, &freq_table);
	if (ret) {
		dev_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		goto out_free_priv;
	}

	priv->cpu_dev = cpu_dev;
	priv->domain_id = handle->perf_ops->device_domain_id(cpu_dev);

	policy->driver_data = priv;
	policy->freq_table = freq_table;

	/* SCMI allows DVFS request for any domain from any CPU */
	policy->dvfs_possible_from_any_cpu = true;

	latency = handle->perf_ops->transition_latency_get(handle, cpu_dev);
	if (!latency)
		latency = CPUFREQ_ETERNAL;

	policy->cpuinfo.transition_latency = latency;

	policy->fast_switch_possible =
		handle->perf_ops->fast_switch_possible(handle, cpu_dev);

	em_dev_register_perf_domain(cpu_dev, nr_opp, &em_cb, policy->cpus,
				    false);

	return 0;

out_free_priv:
	kfree(priv);
out_free_opp:
	dev_pm_opp_remove_all_dynamic(cpu_dev);

	return ret;
}

static int scmi_cpufreq_exit(struct cpufreq_policy *policy)
{
	struct scmi_data *priv = policy->driver_data;

	dev_pm_opp_free_cpufreq_table(priv->cpu_dev, &policy->freq_table);
	dev_pm_opp_remove_all_dynamic(priv->cpu_dev);
	kfree(priv);

	return 0;
}

static struct cpufreq_driver scmi_cpufreq_driver = {
	.name	= "scmi",
	.flags	= CPUFREQ_STICKY | CPUFREQ_HAVE_GOVERNOR_PER_POLICY |
		  CPUFREQ_NEED_INITIAL_FREQ_CHECK |
		  CPUFREQ_IS_COOLING_DEV,
	.verify	= cpufreq_generic_frequency_table_verify,
	.attr	= cpufreq_generic_attr,
	.target_index	= scmi_cpufreq_set_target,
	.fast_switch	= scmi_cpufreq_fast_switch,
	.get	= scmi_cpufreq_get_rate,
	.init	= scmi_cpufreq_init,
	.exit	= scmi_cpufreq_exit,
};

static int scmi_cpufreq_probe(struct scmi_device *sdev)
{
	int ret;
	struct device *dev = &sdev->dev;

	handle = sdev->handle;

	if (!handle || !handle->perf_ops)
		return -ENODEV;

#ifdef CONFIG_COMMON_CLK
	/* dummy clock provider as needed by OPP if clocks property is used */
	if (of_find_property(dev->of_node, "#clock-cells", NULL))
		devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, NULL);
#endif

	ret = cpufreq_register_driver(&scmi_cpufreq_driver);
	if (ret) {
		dev_err(dev, "%s: registering cpufreq failed, err: %d\n",
			__func__, ret);
	}

	return ret;
}

static void scmi_cpufreq_remove(struct scmi_device *sdev)
{
	cpufreq_unregister_driver(&scmi_cpufreq_driver);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_PERF, "cpufreq" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_cpufreq_drv = {
	.name		= "scmi-cpufreq",
	.probe		= scmi_cpufreq_probe,
	.remove		= scmi_cpufreq_remove,
	.id_table	= scmi_id_table,
};
module_scmi_driver(scmi_cpufreq_drv);

MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("ARM SCMI CPUFreq interface driver");
MODULE_LICENSE("GPL v2");
