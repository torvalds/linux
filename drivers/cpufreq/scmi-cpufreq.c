// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Power Interface (SCMI) based CPUFreq Interface driver
 *
 * Copyright (C) 2018-2021 ARM Ltd.
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
#include <linux/units.h>

struct scmi_data {
	int domain_id;
	int nr_opp;
	struct device *cpu_dev;
	cpumask_var_t opp_shared_cpus;
};

static struct scmi_protocol_handle *ph;
static const struct scmi_perf_proto_ops *perf_ops;
static struct cpufreq_driver scmi_cpufreq_driver;

static unsigned int scmi_cpufreq_get_rate(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get_raw(cpu);
	struct scmi_data *priv = policy->driver_data;
	unsigned long rate;
	int ret;

	ret = perf_ops->freq_get(ph, priv->domain_id, &rate, false);
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
	u64 freq = policy->freq_table[index].frequency;

	return perf_ops->freq_set(ph, priv->domain_id, freq * 1000, false);
}

static unsigned int scmi_cpufreq_fast_switch(struct cpufreq_policy *policy,
					     unsigned int target_freq)
{
	struct scmi_data *priv = policy->driver_data;
	unsigned long freq = target_freq;

	if (!perf_ops->freq_set(ph, priv->domain_id, freq * 1000, true))
		return target_freq;

	return 0;
}

static int scmi_cpu_domain_id(struct device *cpu_dev)
{
	struct device_node *np = cpu_dev->of_node;
	struct of_phandle_args domain_id;
	int index;

	if (of_parse_phandle_with_args(np, "clocks", "#clock-cells", 0,
				       &domain_id)) {
		/* Find the corresponding index for power-domain "perf". */
		index = of_property_match_string(np, "power-domain-names",
						 "perf");
		if (index < 0)
			return -EINVAL;

		if (of_parse_phandle_with_args(np, "power-domains",
					       "#power-domain-cells", index,
					       &domain_id))
			return -EINVAL;
	}

	return domain_id.args[0];
}

static int
scmi_get_sharing_cpus(struct device *cpu_dev, int domain,
		      struct cpumask *cpumask)
{
	int cpu, tdomain;
	struct device *tcpu_dev;

	for_each_possible_cpu(cpu) {
		if (cpu == cpu_dev->id)
			continue;

		tcpu_dev = get_cpu_device(cpu);
		if (!tcpu_dev)
			continue;

		tdomain = scmi_cpu_domain_id(tcpu_dev);
		if (tdomain == domain)
			cpumask_set_cpu(cpu, cpumask);
	}

	return 0;
}

static int __maybe_unused
scmi_get_cpu_power(struct device *cpu_dev, unsigned long *power,
		   unsigned long *KHz)
{
	enum scmi_power_scale power_scale = perf_ops->power_scale_get(ph);
	unsigned long Hz;
	int ret, domain;

	domain = scmi_cpu_domain_id(cpu_dev);
	if (domain < 0)
		return domain;

	/* Get the power cost of the performance domain. */
	Hz = *KHz * 1000;
	ret = perf_ops->est_power_get(ph, domain, &Hz, power);
	if (ret)
		return ret;

	/* Convert the power to uW if it is mW (ignore bogoW) */
	if (power_scale == SCMI_POWER_MILLIWATTS)
		*power *= MICROWATT_PER_MILLIWATT;

	/* The EM framework specifies the frequency in KHz. */
	*KHz = Hz / 1000;

	return 0;
}

static int
scmi_get_rate_limit(u32 domain, bool has_fast_switch)
{
	int ret, rate_limit;

	if (has_fast_switch) {
		/*
		 * Fast channels are used whenever available,
		 * so use their rate_limit value if populated.
		 */
		ret = perf_ops->fast_switch_rate_limit(ph, domain,
						       &rate_limit);
		if (!ret && rate_limit)
			return rate_limit;
	}

	ret = perf_ops->rate_limit_get(ph, domain, &rate_limit);
	if (ret)
		return 0;

	return rate_limit;
}

static struct freq_attr *scmi_cpufreq_hw_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
	NULL,
};

static int scmi_cpufreq_init(struct cpufreq_policy *policy)
{
	int ret, nr_opp, domain;
	unsigned int latency;
	struct device *cpu_dev;
	struct scmi_data *priv;
	struct cpufreq_frequency_table *freq_table;

	cpu_dev = get_cpu_device(policy->cpu);
	if (!cpu_dev) {
		pr_err("failed to get cpu%d device\n", policy->cpu);
		return -ENODEV;
	}

	domain = scmi_cpu_domain_id(cpu_dev);
	if (domain < 0)
		return domain;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (!zalloc_cpumask_var(&priv->opp_shared_cpus, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto out_free_priv;
	}

	/* Obtain CPUs that share SCMI performance controls */
	ret = scmi_get_sharing_cpus(cpu_dev, domain, policy->cpus);
	if (ret) {
		dev_warn(cpu_dev, "failed to get sharing cpumask\n");
		goto out_free_cpumask;
	}

	/*
	 * Obtain CPUs that share performance levels.
	 * The OPP 'sharing cpus' info may come from DT through an empty opp
	 * table and opp-shared.
	 */
	ret = dev_pm_opp_of_get_sharing_cpus(cpu_dev, priv->opp_shared_cpus);
	if (ret || cpumask_empty(priv->opp_shared_cpus)) {
		/*
		 * Either opp-table is not set or no opp-shared was found.
		 * Use the CPU mask from SCMI to designate CPUs sharing an OPP
		 * table.
		 */
		cpumask_copy(priv->opp_shared_cpus, policy->cpus);
	}

	 /*
	  * A previous CPU may have marked OPPs as shared for a few CPUs, based on
	  * what OPP core provided. If the current CPU is part of those few, then
	  * there is no need to add OPPs again.
	  */
	nr_opp = dev_pm_opp_get_opp_count(cpu_dev);
	if (nr_opp <= 0) {
		ret = perf_ops->device_opps_add(ph, cpu_dev, domain);
		if (ret) {
			dev_warn(cpu_dev, "failed to add opps to the device\n");
			goto out_free_cpumask;
		}

		nr_opp = dev_pm_opp_get_opp_count(cpu_dev);
		if (nr_opp <= 0) {
			dev_err(cpu_dev, "%s: No OPPs for this device: %d\n",
				__func__, nr_opp);

			ret = -ENODEV;
			goto out_free_opp;
		}

		ret = dev_pm_opp_set_sharing_cpus(cpu_dev, priv->opp_shared_cpus);
		if (ret) {
			dev_err(cpu_dev, "%s: failed to mark OPPs as shared: %d\n",
				__func__, ret);

			goto out_free_opp;
		}

		priv->nr_opp = nr_opp;
	}

	ret = dev_pm_opp_init_cpufreq_table(cpu_dev, &freq_table);
	if (ret) {
		dev_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		goto out_free_opp;
	}

	priv->cpu_dev = cpu_dev;
	priv->domain_id = domain;

	policy->driver_data = priv;
	policy->freq_table = freq_table;

	/* SCMI allows DVFS request for any domain from any CPU */
	policy->dvfs_possible_from_any_cpu = true;

	latency = perf_ops->transition_latency_get(ph, domain);
	if (!latency)
		latency = CPUFREQ_ETERNAL;

	policy->cpuinfo.transition_latency = latency;

	policy->fast_switch_possible =
		perf_ops->fast_switch_possible(ph, domain);

	policy->transition_delay_us =
		scmi_get_rate_limit(domain, policy->fast_switch_possible);

	if (policy_has_boost_freq(policy)) {
		ret = cpufreq_enable_boost_support();
		if (ret) {
			dev_warn(cpu_dev, "failed to enable boost: %d\n", ret);
			goto out_free_opp;
		} else {
			scmi_cpufreq_hw_attr[1] = &cpufreq_freq_attr_scaling_boost_freqs;
			scmi_cpufreq_driver.boost_enabled = true;
		}
	}

	return 0;

out_free_opp:
	dev_pm_opp_remove_all_dynamic(cpu_dev);

out_free_cpumask:
	free_cpumask_var(priv->opp_shared_cpus);

out_free_priv:
	kfree(priv);

	return ret;
}

static void scmi_cpufreq_exit(struct cpufreq_policy *policy)
{
	struct scmi_data *priv = policy->driver_data;

	dev_pm_opp_free_cpufreq_table(priv->cpu_dev, &policy->freq_table);
	dev_pm_opp_remove_all_dynamic(priv->cpu_dev);
	free_cpumask_var(priv->opp_shared_cpus);
	kfree(priv);
}

static void scmi_cpufreq_register_em(struct cpufreq_policy *policy)
{
	struct em_data_callback em_cb = EM_DATA_CB(scmi_get_cpu_power);
	enum scmi_power_scale power_scale = perf_ops->power_scale_get(ph);
	struct scmi_data *priv = policy->driver_data;
	bool em_power_scale = false;

	/*
	 * This callback will be called for each policy, but we don't need to
	 * register with EM every time. Despite not being part of the same
	 * policy, some CPUs may still share their perf-domains, and a CPU from
	 * another policy may already have registered with EM on behalf of CPUs
	 * of this policy.
	 */
	if (!priv->nr_opp)
		return;

	if (power_scale == SCMI_POWER_MILLIWATTS
	    || power_scale == SCMI_POWER_MICROWATTS)
		em_power_scale = true;

	em_dev_register_perf_domain(get_cpu_device(policy->cpu), priv->nr_opp,
				    &em_cb, priv->opp_shared_cpus,
				    em_power_scale);
}

static struct cpufreq_driver scmi_cpufreq_driver = {
	.name	= "scmi",
	.flags	= CPUFREQ_HAVE_GOVERNOR_PER_POLICY |
		  CPUFREQ_NEED_INITIAL_FREQ_CHECK |
		  CPUFREQ_IS_COOLING_DEV,
	.verify	= cpufreq_generic_frequency_table_verify,
	.attr	= scmi_cpufreq_hw_attr,
	.target_index	= scmi_cpufreq_set_target,
	.fast_switch	= scmi_cpufreq_fast_switch,
	.get	= scmi_cpufreq_get_rate,
	.init	= scmi_cpufreq_init,
	.exit	= scmi_cpufreq_exit,
	.register_em	= scmi_cpufreq_register_em,
};

static int scmi_cpufreq_probe(struct scmi_device *sdev)
{
	int ret;
	struct device *dev = &sdev->dev;
	const struct scmi_handle *handle;

	handle = sdev->handle;

	if (!handle)
		return -ENODEV;

	perf_ops = handle->devm_protocol_get(sdev, SCMI_PROTOCOL_PERF, &ph);
	if (IS_ERR(perf_ops))
		return PTR_ERR(perf_ops);

#ifdef CONFIG_COMMON_CLK
	/* dummy clock provider as needed by OPP if clocks property is used */
	if (of_property_present(dev->of_node, "#clock-cells")) {
		ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, NULL);
		if (ret)
			return dev_err_probe(dev, ret, "%s: registering clock provider failed\n", __func__);
	}
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
