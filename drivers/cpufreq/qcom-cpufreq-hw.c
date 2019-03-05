// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

#define LUT_MAX_ENTRIES			40U
#define LUT_SRC				GENMASK(31, 30)
#define LUT_L_VAL			GENMASK(7, 0)
#define LUT_CORE_COUNT			GENMASK(18, 16)
#define LUT_ROW_SIZE			32
#define CLK_HW_DIV			2

/* Register offsets */
#define REG_ENABLE			0x0
#define REG_LUT_TABLE			0x110
#define REG_PERF_STATE			0x920

static unsigned long cpu_hw_rate, xo_rate;
static struct platform_device *global_pdev;

static int qcom_cpufreq_hw_target_index(struct cpufreq_policy *policy,
					unsigned int index)
{
	void __iomem *perf_state_reg = policy->driver_data;

	writel_relaxed(index, perf_state_reg);

	return 0;
}

static unsigned int qcom_cpufreq_hw_get(unsigned int cpu)
{
	void __iomem *perf_state_reg;
	struct cpufreq_policy *policy;
	unsigned int index;

	policy = cpufreq_cpu_get_raw(cpu);
	if (!policy)
		return 0;

	perf_state_reg = policy->driver_data;

	index = readl_relaxed(perf_state_reg);
	index = min(index, LUT_MAX_ENTRIES - 1);

	return policy->freq_table[index].frequency;
}

static unsigned int qcom_cpufreq_hw_fast_switch(struct cpufreq_policy *policy,
						unsigned int target_freq)
{
	void __iomem *perf_state_reg = policy->driver_data;
	int index;

	index = policy->cached_resolved_idx;
	if (index < 0)
		return 0;

	writel_relaxed(index, perf_state_reg);

	return policy->freq_table[index].frequency;
}

static int qcom_cpufreq_hw_read_lut(struct device *dev,
				    struct cpufreq_policy *policy,
				    void __iomem *base)
{
	u32 data, src, lval, i, core_count, prev_cc = 0, prev_freq = 0, freq;
	unsigned int max_cores = cpumask_weight(policy->cpus);
	struct cpufreq_frequency_table	*table;

	table = kcalloc(LUT_MAX_ENTRIES + 1, sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	for (i = 0; i < LUT_MAX_ENTRIES; i++) {
		data = readl_relaxed(base + REG_LUT_TABLE + i * LUT_ROW_SIZE);
		src = FIELD_GET(LUT_SRC, data);
		lval = FIELD_GET(LUT_L_VAL, data);
		core_count = FIELD_GET(LUT_CORE_COUNT, data);

		if (src)
			freq = xo_rate * lval / 1000;
		else
			freq = cpu_hw_rate / 1000;

		/* Ignore boosts in the middle of the table */
		if (core_count != max_cores) {
			table[i].frequency = CPUFREQ_ENTRY_INVALID;
		} else {
			table[i].frequency = freq;
			dev_dbg(dev, "index=%d freq=%d, core_count %d\n", i,
				freq, core_count);
		}

		/*
		 * Two of the same frequencies with the same core counts means
		 * end of table
		 */
		if (i > 0 && prev_freq == freq && prev_cc == core_count) {
			struct cpufreq_frequency_table *prev = &table[i - 1];

			/*
			 * Only treat the last frequency that might be a boost
			 * as the boost frequency
			 */
			if (prev_cc != max_cores) {
				prev->frequency = prev_freq;
				prev->flags = CPUFREQ_BOOST_FREQ;
			}

			break;
		}

		prev_cc = core_count;
		prev_freq = freq;
	}

	table[i].frequency = CPUFREQ_TABLE_END;
	policy->freq_table = table;

	return 0;
}

static void qcom_get_related_cpus(int index, struct cpumask *m)
{
	struct device_node *cpu_np;
	struct of_phandle_args args;
	int cpu, ret;

	for_each_possible_cpu(cpu) {
		cpu_np = of_cpu_device_node_get(cpu);
		if (!cpu_np)
			continue;

		ret = of_parse_phandle_with_args(cpu_np, "qcom,freq-domain",
						 "#freq-domain-cells", 0,
						 &args);
		of_node_put(cpu_np);
		if (ret < 0)
			continue;

		if (index == args.args[0])
			cpumask_set_cpu(cpu, m);
	}
}

static int qcom_cpufreq_hw_cpu_init(struct cpufreq_policy *policy)
{
	struct device *dev = &global_pdev->dev;
	struct of_phandle_args args;
	struct device_node *cpu_np;
	struct resource *res;
	void __iomem *base;
	int ret, index;

	cpu_np = of_cpu_device_node_get(policy->cpu);
	if (!cpu_np)
		return -EINVAL;

	ret = of_parse_phandle_with_args(cpu_np, "qcom,freq-domain",
					 "#freq-domain-cells", 0, &args);
	of_node_put(cpu_np);
	if (ret)
		return ret;

	index = args.args[0];

	res = platform_get_resource(global_pdev, IORESOURCE_MEM, index);
	if (!res)
		return -ENODEV;

	base = devm_ioremap(dev, res->start, resource_size(res));
	if (!base)
		return -ENOMEM;

	/* HW should be in enabled state to proceed */
	if (!(readl_relaxed(base + REG_ENABLE) & 0x1)) {
		dev_err(dev, "Domain-%d cpufreq hardware not enabled\n", index);
		ret = -ENODEV;
		goto error;
	}

	qcom_get_related_cpus(index, policy->cpus);
	if (!cpumask_weight(policy->cpus)) {
		dev_err(dev, "Domain-%d failed to get related CPUs\n", index);
		ret = -ENOENT;
		goto error;
	}

	policy->driver_data = base + REG_PERF_STATE;

	ret = qcom_cpufreq_hw_read_lut(dev, policy, base);
	if (ret) {
		dev_err(dev, "Domain-%d failed to read LUT\n", index);
		goto error;
	}

	policy->fast_switch_possible = true;

	return 0;
error:
	devm_iounmap(dev, base);
	return ret;
}

static int qcom_cpufreq_hw_cpu_exit(struct cpufreq_policy *policy)
{
	void __iomem *base = policy->driver_data - REG_PERF_STATE;

	kfree(policy->freq_table);
	devm_iounmap(&global_pdev->dev, base);

	return 0;
}

static struct freq_attr *qcom_cpufreq_hw_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	&cpufreq_freq_attr_scaling_boost_freqs,
	NULL
};

static struct cpufreq_driver cpufreq_qcom_hw_driver = {
	.flags		= CPUFREQ_STICKY | CPUFREQ_NEED_INITIAL_FREQ_CHECK |
			  CPUFREQ_HAVE_GOVERNOR_PER_POLICY,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= qcom_cpufreq_hw_target_index,
	.get		= qcom_cpufreq_hw_get,
	.init		= qcom_cpufreq_hw_cpu_init,
	.exit		= qcom_cpufreq_hw_cpu_exit,
	.fast_switch    = qcom_cpufreq_hw_fast_switch,
	.name		= "qcom-cpufreq-hw",
	.attr		= qcom_cpufreq_hw_attr,
};

static int qcom_cpufreq_hw_driver_probe(struct platform_device *pdev)
{
	struct clk *clk;
	int ret;

	clk = clk_get(&pdev->dev, "xo");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	xo_rate = clk_get_rate(clk);
	clk_put(clk);

	clk = clk_get(&pdev->dev, "alternate");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	cpu_hw_rate = clk_get_rate(clk) / CLK_HW_DIV;
	clk_put(clk);

	global_pdev = pdev;

	ret = cpufreq_register_driver(&cpufreq_qcom_hw_driver);
	if (ret)
		dev_err(&pdev->dev, "CPUFreq HW driver failed to register\n");
	else
		dev_dbg(&pdev->dev, "QCOM CPUFreq HW driver initialized\n");

	return ret;
}

static int qcom_cpufreq_hw_driver_remove(struct platform_device *pdev)
{
	return cpufreq_unregister_driver(&cpufreq_qcom_hw_driver);
}

static const struct of_device_id qcom_cpufreq_hw_match[] = {
	{ .compatible = "qcom,cpufreq-hw" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_cpufreq_hw_match);

static struct platform_driver qcom_cpufreq_hw_driver = {
	.probe = qcom_cpufreq_hw_driver_probe,
	.remove = qcom_cpufreq_hw_driver_remove,
	.driver = {
		.name = "qcom-cpufreq-hw",
		.of_match_table = qcom_cpufreq_hw_match,
	},
};

static int __init qcom_cpufreq_hw_init(void)
{
	return platform_driver_register(&qcom_cpufreq_hw_driver);
}
subsys_initcall(qcom_cpufreq_hw_init);

static void __exit qcom_cpufreq_hw_exit(void)
{
	platform_driver_unregister(&qcom_cpufreq_hw_driver);
}
module_exit(qcom_cpufreq_hw_exit);

MODULE_DESCRIPTION("QCOM CPUFREQ HW Driver");
MODULE_LICENSE("GPL v2");
