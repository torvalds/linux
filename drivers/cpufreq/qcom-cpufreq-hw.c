// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>

#define LUT_MAX_ENTRIES			40U
#define LUT_SRC				GENMASK(31, 30)
#define LUT_L_VAL			GENMASK(7, 0)
#define LUT_CORE_COUNT			GENMASK(18, 16)
#define LUT_VOLT			GENMASK(11, 0)
#define CLK_HW_DIV			2
#define LUT_TURBO_IND			1

struct qcom_cpufreq_soc_data {
	u32 reg_enable;
	u32 reg_freq_lut;
	u32 reg_volt_lut;
	u32 reg_perf_state;
	u8 lut_row_size;
};

struct qcom_cpufreq_data {
	void __iomem *base;
	const struct qcom_cpufreq_soc_data *soc_data;
};

static unsigned long cpu_hw_rate, xo_rate;
static bool icc_scaling_enabled;

static int qcom_cpufreq_set_bw(struct cpufreq_policy *policy,
			       unsigned long freq_khz)
{
	unsigned long freq_hz = freq_khz * 1000;
	struct dev_pm_opp *opp;
	struct device *dev;
	int ret;

	dev = get_cpu_device(policy->cpu);
	if (!dev)
		return -ENODEV;

	opp = dev_pm_opp_find_freq_exact(dev, freq_hz, true);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	ret = dev_pm_opp_set_bw(dev, opp);
	dev_pm_opp_put(opp);
	return ret;
}

static int qcom_cpufreq_update_opp(struct device *cpu_dev,
				   unsigned long freq_khz,
				   unsigned long volt)
{
	unsigned long freq_hz = freq_khz * 1000;
	int ret;

	/* Skip voltage update if the opp table is not available */
	if (!icc_scaling_enabled)
		return dev_pm_opp_add(cpu_dev, freq_hz, volt);

	ret = dev_pm_opp_adjust_voltage(cpu_dev, freq_hz, volt, volt, volt);
	if (ret) {
		dev_err(cpu_dev, "Voltage update failed freq=%ld\n", freq_khz);
		return ret;
	}

	return dev_pm_opp_enable(cpu_dev, freq_hz);
}

static int qcom_cpufreq_hw_target_index(struct cpufreq_policy *policy,
					unsigned int index)
{
	struct qcom_cpufreq_data *data = policy->driver_data;
	const struct qcom_cpufreq_soc_data *soc_data = data->soc_data;
	unsigned long freq = policy->freq_table[index].frequency;

	writel_relaxed(index, data->base + soc_data->reg_perf_state);

	if (icc_scaling_enabled)
		qcom_cpufreq_set_bw(policy, freq);

	return 0;
}

static unsigned int qcom_cpufreq_hw_get(unsigned int cpu)
{
	struct qcom_cpufreq_data *data;
	const struct qcom_cpufreq_soc_data *soc_data;
	struct cpufreq_policy *policy;
	unsigned int index;

	policy = cpufreq_cpu_get_raw(cpu);
	if (!policy)
		return 0;

	data = policy->driver_data;
	soc_data = data->soc_data;

	index = readl_relaxed(data->base + soc_data->reg_perf_state);
	index = min(index, LUT_MAX_ENTRIES - 1);

	return policy->freq_table[index].frequency;
}

static unsigned int qcom_cpufreq_hw_fast_switch(struct cpufreq_policy *policy,
						unsigned int target_freq)
{
	struct qcom_cpufreq_data *data = policy->driver_data;
	const struct qcom_cpufreq_soc_data *soc_data = data->soc_data;
	unsigned int index;

	index = policy->cached_resolved_idx;
	writel_relaxed(index, data->base + soc_data->reg_perf_state);

	return policy->freq_table[index].frequency;
}

static int qcom_cpufreq_hw_read_lut(struct device *cpu_dev,
				    struct cpufreq_policy *policy)
{
	u32 data, src, lval, i, core_count, prev_freq = 0, freq;
	u32 volt;
	struct cpufreq_frequency_table	*table;
	struct dev_pm_opp *opp;
	unsigned long rate;
	int ret;
	struct qcom_cpufreq_data *drv_data = policy->driver_data;
	const struct qcom_cpufreq_soc_data *soc_data = drv_data->soc_data;

	table = kcalloc(LUT_MAX_ENTRIES + 1, sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	ret = dev_pm_opp_of_add_table(cpu_dev);
	if (!ret) {
		/* Disable all opps and cross-validate against LUT later */
		icc_scaling_enabled = true;
		for (rate = 0; ; rate++) {
			opp = dev_pm_opp_find_freq_ceil(cpu_dev, &rate);
			if (IS_ERR(opp))
				break;

			dev_pm_opp_put(opp);
			dev_pm_opp_disable(cpu_dev, rate);
		}
	} else if (ret != -ENODEV) {
		dev_err(cpu_dev, "Invalid opp table in device tree\n");
		return ret;
	} else {
		policy->fast_switch_possible = true;
		icc_scaling_enabled = false;
	}

	for (i = 0; i < LUT_MAX_ENTRIES; i++) {
		data = readl_relaxed(drv_data->base + soc_data->reg_freq_lut +
				      i * soc_data->lut_row_size);
		src = FIELD_GET(LUT_SRC, data);
		lval = FIELD_GET(LUT_L_VAL, data);
		core_count = FIELD_GET(LUT_CORE_COUNT, data);

		data = readl_relaxed(drv_data->base + soc_data->reg_volt_lut +
				      i * soc_data->lut_row_size);
		volt = FIELD_GET(LUT_VOLT, data) * 1000;

		if (src)
			freq = xo_rate * lval / 1000;
		else
			freq = cpu_hw_rate / 1000;

		if (freq != prev_freq && core_count != LUT_TURBO_IND) {
			if (!qcom_cpufreq_update_opp(cpu_dev, freq, volt)) {
				table[i].frequency = freq;
				dev_dbg(cpu_dev, "index=%d freq=%d, core_count %d\n", i,
				freq, core_count);
			} else {
				dev_warn(cpu_dev, "failed to update OPP for freq=%d\n", freq);
				table[i].frequency = CPUFREQ_ENTRY_INVALID;
			}

		} else if (core_count == LUT_TURBO_IND) {
			table[i].frequency = CPUFREQ_ENTRY_INVALID;
		}

		/*
		 * Two of the same frequencies with the same core counts means
		 * end of table
		 */
		if (i > 0 && prev_freq == freq) {
			struct cpufreq_frequency_table *prev = &table[i - 1];

			/*
			 * Only treat the last frequency that might be a boost
			 * as the boost frequency
			 */
			if (prev->frequency == CPUFREQ_ENTRY_INVALID) {
				if (!qcom_cpufreq_update_opp(cpu_dev, prev_freq, volt)) {
					prev->frequency = prev_freq;
					prev->flags = CPUFREQ_BOOST_FREQ;
				} else {
					dev_warn(cpu_dev, "failed to update OPP for freq=%d\n",
						 freq);
				}
			}

			break;
		}

		prev_freq = freq;
	}

	table[i].frequency = CPUFREQ_TABLE_END;
	policy->freq_table = table;
	dev_pm_opp_set_sharing_cpus(cpu_dev, policy->cpus);

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

static const struct qcom_cpufreq_soc_data qcom_soc_data = {
	.reg_enable = 0x0,
	.reg_freq_lut = 0x110,
	.reg_volt_lut = 0x114,
	.reg_perf_state = 0x920,
	.lut_row_size = 32,
};

static const struct qcom_cpufreq_soc_data epss_soc_data = {
	.reg_enable = 0x0,
	.reg_freq_lut = 0x100,
	.reg_volt_lut = 0x200,
	.reg_perf_state = 0x320,
	.lut_row_size = 4,
};

static const struct of_device_id qcom_cpufreq_hw_match[] = {
	{ .compatible = "qcom,cpufreq-hw", .data = &qcom_soc_data },
	{ .compatible = "qcom,cpufreq-epss", .data = &epss_soc_data },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_cpufreq_hw_match);

static int qcom_cpufreq_hw_cpu_init(struct cpufreq_policy *policy)
{
	struct platform_device *pdev = cpufreq_get_driver_data();
	struct device *dev = &pdev->dev;
	struct of_phandle_args args;
	struct device_node *cpu_np;
	struct device *cpu_dev;
	void __iomem *base;
	struct qcom_cpufreq_data *data;
	int ret, index;

	cpu_dev = get_cpu_device(policy->cpu);
	if (!cpu_dev) {
		pr_err("%s: failed to get cpu%d device\n", __func__,
		       policy->cpu);
		return -ENODEV;
	}

	cpu_np = of_cpu_device_node_get(policy->cpu);
	if (!cpu_np)
		return -EINVAL;

	ret = of_parse_phandle_with_args(cpu_np, "qcom,freq-domain",
					 "#freq-domain-cells", 0, &args);
	of_node_put(cpu_np);
	if (ret)
		return ret;

	index = args.args[0];

	base = devm_platform_ioremap_resource(pdev, index);
	if (IS_ERR(base))
		return PTR_ERR(base);

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto error;
	}

	data->soc_data = of_device_get_match_data(&pdev->dev);
	data->base = base;

	/* HW should be in enabled state to proceed */
	if (!(readl_relaxed(base + data->soc_data->reg_enable) & 0x1)) {
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

	policy->driver_data = data;

	ret = qcom_cpufreq_hw_read_lut(cpu_dev, policy);
	if (ret) {
		dev_err(dev, "Domain-%d failed to read LUT\n", index);
		goto error;
	}

	ret = dev_pm_opp_get_opp_count(cpu_dev);
	if (ret <= 0) {
		dev_err(cpu_dev, "Failed to add OPPs\n");
		ret = -ENODEV;
		goto error;
	}

	dev_pm_opp_of_register_em(cpu_dev, policy->cpus);

	return 0;
error:
	devm_iounmap(dev, base);
	return ret;
}

static int qcom_cpufreq_hw_cpu_exit(struct cpufreq_policy *policy)
{
	struct device *cpu_dev = get_cpu_device(policy->cpu);
	struct qcom_cpufreq_data *data = policy->driver_data;
	struct platform_device *pdev = cpufreq_get_driver_data();

	dev_pm_opp_remove_all_dynamic(cpu_dev);
	dev_pm_opp_of_cpumask_remove_table(policy->related_cpus);
	kfree(policy->freq_table);
	devm_iounmap(&pdev->dev, data->base);

	return 0;
}

static struct freq_attr *qcom_cpufreq_hw_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	&cpufreq_freq_attr_scaling_boost_freqs,
	NULL
};

static struct cpufreq_driver cpufreq_qcom_hw_driver = {
	.flags		= CPUFREQ_STICKY | CPUFREQ_NEED_INITIAL_FREQ_CHECK |
			  CPUFREQ_HAVE_GOVERNOR_PER_POLICY |
			  CPUFREQ_IS_COOLING_DEV,
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
	struct device *cpu_dev;
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

	cpufreq_qcom_hw_driver.driver_data = pdev;

	/* Check for optional interconnect paths on CPU0 */
	cpu_dev = get_cpu_device(0);
	if (!cpu_dev)
		return -EPROBE_DEFER;

	ret = dev_pm_opp_of_find_icc_paths(cpu_dev, NULL);
	if (ret)
		return ret;

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
postcore_initcall(qcom_cpufreq_hw_init);

static void __exit qcom_cpufreq_hw_exit(void)
{
	platform_driver_unregister(&qcom_cpufreq_hw_driver);
}
module_exit(qcom_cpufreq_hw_exit);

MODULE_DESCRIPTION("QCOM CPUFREQ HW Driver");
MODULE_LICENSE("GPL v2");
