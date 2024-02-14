// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#include <linux/bitfield.h>
#include <linux/cpufreq.h>
#include <linux/energy_model.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

#define LUT_MAX_ENTRIES			32U
#define LUT_FREQ			GENMASK(11, 0)
#define LUT_ROW_SIZE			0x4
#define CPUFREQ_HW_STATUS		BIT(0)
#define SVS_HW_STATUS			BIT(1)
#define POLL_USEC			1000
#define TIMEOUT_USEC			300000

enum {
	REG_FREQ_LUT_TABLE,
	REG_FREQ_ENABLE,
	REG_FREQ_PERF_STATE,
	REG_FREQ_HW_STATE,
	REG_EM_POWER_TBL,
	REG_FREQ_LATENCY,

	REG_ARRAY_SIZE,
};

struct mtk_cpufreq_data {
	struct cpufreq_frequency_table *table;
	void __iomem *reg_bases[REG_ARRAY_SIZE];
	struct resource *res;
	void __iomem *base;
	int nr_opp;
};

static const u16 cpufreq_mtk_offsets[REG_ARRAY_SIZE] = {
	[REG_FREQ_LUT_TABLE]	= 0x0,
	[REG_FREQ_ENABLE]	= 0x84,
	[REG_FREQ_PERF_STATE]	= 0x88,
	[REG_FREQ_HW_STATE]	= 0x8c,
	[REG_EM_POWER_TBL]	= 0x90,
	[REG_FREQ_LATENCY]	= 0x110,
};

static int __maybe_unused
mtk_cpufreq_get_cpu_power(struct device *cpu_dev, unsigned long *uW,
			  unsigned long *KHz)
{
	struct mtk_cpufreq_data *data;
	struct cpufreq_policy *policy;
	int i;

	policy = cpufreq_cpu_get_raw(cpu_dev->id);
	if (!policy)
		return 0;

	data = policy->driver_data;

	for (i = 0; i < data->nr_opp; i++) {
		if (data->table[i].frequency < *KHz)
			break;
	}
	i--;

	*KHz = data->table[i].frequency;
	/* Provide micro-Watts value to the Energy Model */
	*uW = readl_relaxed(data->reg_bases[REG_EM_POWER_TBL] +
			    i * LUT_ROW_SIZE);

	return 0;
}

static int mtk_cpufreq_hw_target_index(struct cpufreq_policy *policy,
				       unsigned int index)
{
	struct mtk_cpufreq_data *data = policy->driver_data;

	writel_relaxed(index, data->reg_bases[REG_FREQ_PERF_STATE]);

	return 0;
}

static unsigned int mtk_cpufreq_hw_get(unsigned int cpu)
{
	struct mtk_cpufreq_data *data;
	struct cpufreq_policy *policy;
	unsigned int index;

	policy = cpufreq_cpu_get_raw(cpu);
	if (!policy)
		return 0;

	data = policy->driver_data;

	index = readl_relaxed(data->reg_bases[REG_FREQ_PERF_STATE]);
	index = min(index, LUT_MAX_ENTRIES - 1);

	return data->table[index].frequency;
}

static unsigned int mtk_cpufreq_hw_fast_switch(struct cpufreq_policy *policy,
					       unsigned int target_freq)
{
	struct mtk_cpufreq_data *data = policy->driver_data;
	unsigned int index;

	index = cpufreq_table_find_index_dl(policy, target_freq, false);

	writel_relaxed(index, data->reg_bases[REG_FREQ_PERF_STATE]);

	return policy->freq_table[index].frequency;
}

static int mtk_cpu_create_freq_table(struct platform_device *pdev,
				     struct mtk_cpufreq_data *data)
{
	struct device *dev = &pdev->dev;
	u32 temp, i, freq, prev_freq = 0;
	void __iomem *base_table;

	data->table = devm_kcalloc(dev, LUT_MAX_ENTRIES + 1,
				   sizeof(*data->table), GFP_KERNEL);
	if (!data->table)
		return -ENOMEM;

	base_table = data->reg_bases[REG_FREQ_LUT_TABLE];

	for (i = 0; i < LUT_MAX_ENTRIES; i++) {
		temp = readl_relaxed(base_table + (i * LUT_ROW_SIZE));
		freq = FIELD_GET(LUT_FREQ, temp) * 1000;

		if (freq == prev_freq)
			break;

		data->table[i].frequency = freq;

		dev_dbg(dev, "index=%d freq=%d\n", i, data->table[i].frequency);

		prev_freq = freq;
	}

	data->table[i].frequency = CPUFREQ_TABLE_END;
	data->nr_opp = i;

	return 0;
}

static int mtk_cpu_resources_init(struct platform_device *pdev,
				  struct cpufreq_policy *policy,
				  const u16 *offsets)
{
	struct mtk_cpufreq_data *data;
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem *base;
	int ret, i;
	int index;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	index = of_perf_domain_get_sharing_cpumask(policy->cpu, "performance-domains",
						   "#performance-domain-cells",
						   policy->cpus);
	if (index < 0)
		return index;

	res = platform_get_resource(pdev, IORESOURCE_MEM, index);
	if (!res) {
		dev_err(dev, "failed to get mem resource %d\n", index);
		return -ENODEV;
	}

	if (!request_mem_region(res->start, resource_size(res), res->name)) {
		dev_err(dev, "failed to request resource %pR\n", res);
		return -EBUSY;
	}

	base = ioremap(res->start, resource_size(res));
	if (!base) {
		dev_err(dev, "failed to map resource %pR\n", res);
		ret = -ENOMEM;
		goto release_region;
	}

	data->base = base;
	data->res = res;

	for (i = REG_FREQ_LUT_TABLE; i < REG_ARRAY_SIZE; i++)
		data->reg_bases[i] = base + offsets[i];

	ret = mtk_cpu_create_freq_table(pdev, data);
	if (ret) {
		dev_info(dev, "Domain-%d failed to create freq table\n", index);
		return ret;
	}

	policy->freq_table = data->table;
	policy->driver_data = data;

	return 0;
release_region:
	release_mem_region(res->start, resource_size(res));
	return ret;
}

static int mtk_cpufreq_hw_cpu_init(struct cpufreq_policy *policy)
{
	struct platform_device *pdev = cpufreq_get_driver_data();
	int sig, pwr_hw = CPUFREQ_HW_STATUS | SVS_HW_STATUS;
	struct mtk_cpufreq_data *data;
	unsigned int latency;
	int ret;

	/* Get the bases of cpufreq for domains */
	ret = mtk_cpu_resources_init(pdev, policy, platform_get_drvdata(pdev));
	if (ret) {
		dev_info(&pdev->dev, "CPUFreq resource init failed\n");
		return ret;
	}

	data = policy->driver_data;

	latency = readl_relaxed(data->reg_bases[REG_FREQ_LATENCY]) * 1000;
	if (!latency)
		latency = CPUFREQ_ETERNAL;

	policy->cpuinfo.transition_latency = latency;
	policy->fast_switch_possible = true;

	/* HW should be in enabled state to proceed now */
	writel_relaxed(0x1, data->reg_bases[REG_FREQ_ENABLE]);
	if (readl_poll_timeout(data->reg_bases[REG_FREQ_HW_STATE], sig,
			       (sig & pwr_hw) == pwr_hw, POLL_USEC,
			       TIMEOUT_USEC)) {
		if (!(sig & CPUFREQ_HW_STATUS)) {
			pr_info("cpufreq hardware of CPU%d is not enabled\n",
				policy->cpu);
			return -ENODEV;
		}

		pr_info("SVS of CPU%d is not enabled\n", policy->cpu);
	}

	return 0;
}

static int mtk_cpufreq_hw_cpu_exit(struct cpufreq_policy *policy)
{
	struct mtk_cpufreq_data *data = policy->driver_data;
	struct resource *res = data->res;
	void __iomem *base = data->base;

	/* HW should be in paused state now */
	writel_relaxed(0x0, data->reg_bases[REG_FREQ_ENABLE]);
	iounmap(base);
	release_mem_region(res->start, resource_size(res));

	return 0;
}

static void mtk_cpufreq_register_em(struct cpufreq_policy *policy)
{
	struct em_data_callback em_cb = EM_DATA_CB(mtk_cpufreq_get_cpu_power);
	struct mtk_cpufreq_data *data = policy->driver_data;

	em_dev_register_perf_domain(get_cpu_device(policy->cpu), data->nr_opp,
				    &em_cb, policy->cpus, true);
}

static struct cpufreq_driver cpufreq_mtk_hw_driver = {
	.flags		= CPUFREQ_NEED_INITIAL_FREQ_CHECK |
			  CPUFREQ_HAVE_GOVERNOR_PER_POLICY |
			  CPUFREQ_IS_COOLING_DEV,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= mtk_cpufreq_hw_target_index,
	.get		= mtk_cpufreq_hw_get,
	.init		= mtk_cpufreq_hw_cpu_init,
	.exit		= mtk_cpufreq_hw_cpu_exit,
	.register_em	= mtk_cpufreq_register_em,
	.fast_switch	= mtk_cpufreq_hw_fast_switch,
	.name		= "mtk-cpufreq-hw",
	.attr		= cpufreq_generic_attr,
};

static int mtk_cpufreq_hw_driver_probe(struct platform_device *pdev)
{
	const void *data;
	int ret;

	data = of_device_get_match_data(&pdev->dev);
	if (!data)
		return -EINVAL;

	platform_set_drvdata(pdev, (void *) data);
	cpufreq_mtk_hw_driver.driver_data = pdev;

	ret = cpufreq_register_driver(&cpufreq_mtk_hw_driver);
	if (ret)
		dev_err(&pdev->dev, "CPUFreq HW driver failed to register\n");

	return ret;
}

static int mtk_cpufreq_hw_driver_remove(struct platform_device *pdev)
{
	return cpufreq_unregister_driver(&cpufreq_mtk_hw_driver);
}

static const struct of_device_id mtk_cpufreq_hw_match[] = {
	{ .compatible = "mediatek,cpufreq-hw", .data = &cpufreq_mtk_offsets },
	{}
};

static struct platform_driver mtk_cpufreq_hw_driver = {
	.probe = mtk_cpufreq_hw_driver_probe,
	.remove = mtk_cpufreq_hw_driver_remove,
	.driver = {
		.name = "mtk-cpufreq-hw",
		.of_match_table = mtk_cpufreq_hw_match,
	},
};
module_platform_driver(mtk_cpufreq_hw_driver);

MODULE_AUTHOR("Hector Yuan <hector.yuan@mediatek.com>");
MODULE_DESCRIPTION("Mediatek cpufreq-hw driver");
MODULE_LICENSE("GPL v2");
