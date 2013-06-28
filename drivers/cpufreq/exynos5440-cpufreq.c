/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Amit Daniel Kachhap <amit.daniel@samsung.com>
 *
 * EXYNOS5440 - CPU frequency scaling support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/opp.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Register definitions */
#define XMU_DVFS_CTRL		0x0060
#define XMU_PMU_P0_7		0x0064
#define XMU_C0_3_PSTATE		0x0090
#define XMU_P_LIMIT		0x00a0
#define XMU_P_STATUS		0x00a4
#define XMU_PMUEVTEN		0x00d0
#define XMU_PMUIRQEN		0x00d4
#define XMU_PMUIRQ		0x00d8

/* PMU mask and shift definations */
#define P_VALUE_MASK		0x7

#define XMU_DVFS_CTRL_EN_SHIFT	0

#define P0_7_CPUCLKDEV_SHIFT	21
#define P0_7_CPUCLKDEV_MASK	0x7
#define P0_7_ATBCLKDEV_SHIFT	18
#define P0_7_ATBCLKDEV_MASK	0x7
#define P0_7_CSCLKDEV_SHIFT	15
#define P0_7_CSCLKDEV_MASK	0x7
#define P0_7_CPUEMA_SHIFT	28
#define P0_7_CPUEMA_MASK	0xf
#define P0_7_L2EMA_SHIFT	24
#define P0_7_L2EMA_MASK		0xf
#define P0_7_VDD_SHIFT		8
#define P0_7_VDD_MASK		0x7f
#define P0_7_FREQ_SHIFT		0
#define P0_7_FREQ_MASK		0xff

#define C0_3_PSTATE_VALID_SHIFT	8
#define C0_3_PSTATE_CURR_SHIFT	4
#define C0_3_PSTATE_NEW_SHIFT	0

#define PSTATE_CHANGED_EVTEN_SHIFT	0

#define PSTATE_CHANGED_IRQEN_SHIFT	0

#define PSTATE_CHANGED_SHIFT		0

/* some constant values for clock divider calculation */
#define CPU_DIV_FREQ_MAX	500
#define CPU_DBG_FREQ_MAX	375
#define CPU_ATB_FREQ_MAX	500

#define PMIC_LOW_VOLT		0x30
#define PMIC_HIGH_VOLT		0x28

#define CPUEMA_HIGH		0x2
#define CPUEMA_MID		0x4
#define CPUEMA_LOW		0x7

#define L2EMA_HIGH		0x1
#define L2EMA_MID		0x3
#define L2EMA_LOW		0x4

#define DIV_TAB_MAX	2
/* frequency unit is 20MHZ */
#define FREQ_UNIT	20
#define MAX_VOLTAGE	1550000 /* In microvolt */
#define VOLTAGE_STEP	12500	/* In microvolt */

#define CPUFREQ_NAME		"exynos5440_dvfs"
#define DEF_TRANS_LATENCY	100000

enum cpufreq_level_index {
	L0, L1, L2, L3, L4,
	L5, L6, L7, L8, L9,
};
#define CPUFREQ_LEVEL_END	(L7 + 1)

struct exynos_dvfs_data {
	void __iomem *base;
	struct resource *mem;
	int irq;
	struct clk *cpu_clk;
	unsigned int cur_frequency;
	unsigned int latency;
	struct cpufreq_frequency_table *freq_table;
	unsigned int freq_count;
	struct device *dev;
	bool dvfs_enabled;
	struct work_struct irq_work;
};

static struct exynos_dvfs_data *dvfs_info;
static DEFINE_MUTEX(cpufreq_lock);
static struct cpufreq_freqs freqs;

static int init_div_table(void)
{
	struct cpufreq_frequency_table *freq_tbl = dvfs_info->freq_table;
	unsigned int tmp, clk_div, ema_div, freq, volt_id;
	int i = 0;
	struct opp *opp;

	rcu_read_lock();
	for (i = 0; freq_tbl[i].frequency != CPUFREQ_TABLE_END; i++) {

		opp = opp_find_freq_exact(dvfs_info->dev,
					freq_tbl[i].frequency * 1000, true);
		if (IS_ERR(opp)) {
			rcu_read_unlock();
			dev_err(dvfs_info->dev,
				"failed to find valid OPP for %u KHZ\n",
				freq_tbl[i].frequency);
			return PTR_ERR(opp);
		}

		freq = freq_tbl[i].frequency / 1000; /* In MHZ */
		clk_div = ((freq / CPU_DIV_FREQ_MAX) & P0_7_CPUCLKDEV_MASK)
					<< P0_7_CPUCLKDEV_SHIFT;
		clk_div |= ((freq / CPU_ATB_FREQ_MAX) & P0_7_ATBCLKDEV_MASK)
					<< P0_7_ATBCLKDEV_SHIFT;
		clk_div |= ((freq / CPU_DBG_FREQ_MAX) & P0_7_CSCLKDEV_MASK)
					<< P0_7_CSCLKDEV_SHIFT;

		/* Calculate EMA */
		volt_id = opp_get_voltage(opp);
		volt_id = (MAX_VOLTAGE - volt_id) / VOLTAGE_STEP;
		if (volt_id < PMIC_HIGH_VOLT) {
			ema_div = (CPUEMA_HIGH << P0_7_CPUEMA_SHIFT) |
				(L2EMA_HIGH << P0_7_L2EMA_SHIFT);
		} else if (volt_id > PMIC_LOW_VOLT) {
			ema_div = (CPUEMA_LOW << P0_7_CPUEMA_SHIFT) |
				(L2EMA_LOW << P0_7_L2EMA_SHIFT);
		} else {
			ema_div = (CPUEMA_MID << P0_7_CPUEMA_SHIFT) |
				(L2EMA_MID << P0_7_L2EMA_SHIFT);
		}

		tmp = (clk_div | ema_div | (volt_id << P0_7_VDD_SHIFT)
			| ((freq / FREQ_UNIT) << P0_7_FREQ_SHIFT));

		__raw_writel(tmp, dvfs_info->base + XMU_PMU_P0_7 + 4 * i);
	}

	rcu_read_unlock();
	return 0;
}

static void exynos_enable_dvfs(void)
{
	unsigned int tmp, i, cpu;
	struct cpufreq_frequency_table *freq_table = dvfs_info->freq_table;
	/* Disable DVFS */
	__raw_writel(0,	dvfs_info->base + XMU_DVFS_CTRL);

	/* Enable PSTATE Change Event */
	tmp = __raw_readl(dvfs_info->base + XMU_PMUEVTEN);
	tmp |= (1 << PSTATE_CHANGED_EVTEN_SHIFT);
	 __raw_writel(tmp, dvfs_info->base + XMU_PMUEVTEN);

	/* Enable PSTATE Change IRQ */
	tmp = __raw_readl(dvfs_info->base + XMU_PMUIRQEN);
	tmp |= (1 << PSTATE_CHANGED_IRQEN_SHIFT);
	 __raw_writel(tmp, dvfs_info->base + XMU_PMUIRQEN);

	/* Set initial performance index */
	for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++)
		if (freq_table[i].frequency == dvfs_info->cur_frequency)
			break;

	if (freq_table[i].frequency == CPUFREQ_TABLE_END) {
		dev_crit(dvfs_info->dev, "Boot up frequency not supported\n");
		/* Assign the highest frequency */
		i = 0;
		dvfs_info->cur_frequency = freq_table[i].frequency;
	}

	dev_info(dvfs_info->dev, "Setting dvfs initial frequency = %uKHZ",
						dvfs_info->cur_frequency);

	for (cpu = 0; cpu < CONFIG_NR_CPUS; cpu++) {
		tmp = __raw_readl(dvfs_info->base + XMU_C0_3_PSTATE + cpu * 4);
		tmp &= ~(P_VALUE_MASK << C0_3_PSTATE_NEW_SHIFT);
		tmp |= (i << C0_3_PSTATE_NEW_SHIFT);
		__raw_writel(tmp, dvfs_info->base + XMU_C0_3_PSTATE + cpu * 4);
	}

	/* Enable DVFS */
	__raw_writel(1 << XMU_DVFS_CTRL_EN_SHIFT,
				dvfs_info->base + XMU_DVFS_CTRL);
}

static int exynos_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy,
					      dvfs_info->freq_table);
}

static unsigned int exynos_getspeed(unsigned int cpu)
{
	return dvfs_info->cur_frequency;
}

static int exynos_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	unsigned int index, tmp;
	int ret = 0, i;
	struct cpufreq_frequency_table *freq_table = dvfs_info->freq_table;

	mutex_lock(&cpufreq_lock);

	ret = cpufreq_frequency_table_target(policy, freq_table,
					   target_freq, relation, &index);
	if (ret)
		goto out;

	freqs.old = dvfs_info->cur_frequency;
	freqs.new = freq_table[index].frequency;

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	/* Set the target frequency in all C0_3_PSTATE register */
	for_each_cpu(i, policy->cpus) {
		tmp = __raw_readl(dvfs_info->base + XMU_C0_3_PSTATE + i * 4);
		tmp &= ~(P_VALUE_MASK << C0_3_PSTATE_NEW_SHIFT);
		tmp |= (index << C0_3_PSTATE_NEW_SHIFT);

		__raw_writel(tmp, dvfs_info->base + XMU_C0_3_PSTATE + i * 4);
	}
out:
	mutex_unlock(&cpufreq_lock);
	return ret;
}

static void exynos_cpufreq_work(struct work_struct *work)
{
	unsigned int cur_pstate, index;
	struct cpufreq_policy *policy = cpufreq_cpu_get(0); /* boot CPU */
	struct cpufreq_frequency_table *freq_table = dvfs_info->freq_table;

	/* Ensure we can access cpufreq structures */
	if (unlikely(dvfs_info->dvfs_enabled == false))
		goto skip_work;

	mutex_lock(&cpufreq_lock);
	freqs.old = dvfs_info->cur_frequency;

	cur_pstate = __raw_readl(dvfs_info->base + XMU_P_STATUS);
	if (cur_pstate >> C0_3_PSTATE_VALID_SHIFT & 0x1)
		index = (cur_pstate >> C0_3_PSTATE_CURR_SHIFT) & P_VALUE_MASK;
	else
		index = (cur_pstate >> C0_3_PSTATE_NEW_SHIFT) & P_VALUE_MASK;

	if (likely(index < dvfs_info->freq_count)) {
		freqs.new = freq_table[index].frequency;
		dvfs_info->cur_frequency = freqs.new;
	} else {
		dev_crit(dvfs_info->dev, "New frequency out of range\n");
		freqs.new = dvfs_info->cur_frequency;
	}
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	cpufreq_cpu_put(policy);
	mutex_unlock(&cpufreq_lock);
skip_work:
	enable_irq(dvfs_info->irq);
}

static irqreturn_t exynos_cpufreq_irq(int irq, void *id)
{
	unsigned int tmp;

	tmp = __raw_readl(dvfs_info->base + XMU_PMUIRQ);
	if (tmp >> PSTATE_CHANGED_SHIFT & 0x1) {
		__raw_writel(tmp, dvfs_info->base + XMU_PMUIRQ);
		disable_irq_nosync(irq);
		schedule_work(&dvfs_info->irq_work);
	}
	return IRQ_HANDLED;
}

static void exynos_sort_descend_freq_table(void)
{
	struct cpufreq_frequency_table *freq_tbl = dvfs_info->freq_table;
	int i = 0, index;
	unsigned int tmp_freq;
	/*
	 * Exynos5440 clock controller state logic expects the cpufreq table to
	 * be in descending order. But the OPP library constructs the table in
	 * ascending order. So to make the table descending we just need to
	 * swap the i element with the N - i element.
	 */
	for (i = 0; i < dvfs_info->freq_count / 2; i++) {
		index = dvfs_info->freq_count - i - 1;
		tmp_freq = freq_tbl[i].frequency;
		freq_tbl[i].frequency = freq_tbl[index].frequency;
		freq_tbl[index].frequency = tmp_freq;
	}
}

static int exynos_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	int ret;

	ret = cpufreq_frequency_table_cpuinfo(policy, dvfs_info->freq_table);
	if (ret) {
		dev_err(dvfs_info->dev, "Invalid frequency table: %d\n", ret);
		return ret;
	}

	policy->cur = dvfs_info->cur_frequency;
	policy->cpuinfo.transition_latency = dvfs_info->latency;
	cpumask_setall(policy->cpus);

	cpufreq_frequency_table_get_attr(dvfs_info->freq_table, policy->cpu);

	return 0;
}

static struct cpufreq_driver exynos_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= exynos_verify_speed,
	.target		= exynos_target,
	.get		= exynos_getspeed,
	.init		= exynos_cpufreq_cpu_init,
	.name		= CPUFREQ_NAME,
};

static const struct of_device_id exynos_cpufreq_match[] = {
	{
		.compatible = "samsung,exynos5440-cpufreq",
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_cpufreq_match);

static int exynos_cpufreq_probe(struct platform_device *pdev)
{
	int ret = -EINVAL;
	struct device_node *np;
	struct resource res;

	np =  pdev->dev.of_node;
	if (!np)
		return -ENODEV;

	dvfs_info = devm_kzalloc(&pdev->dev, sizeof(*dvfs_info), GFP_KERNEL);
	if (!dvfs_info) {
		ret = -ENOMEM;
		goto err_put_node;
	}

	dvfs_info->dev = &pdev->dev;

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		goto err_put_node;

	dvfs_info->base = devm_ioremap_resource(dvfs_info->dev, &res);
	if (IS_ERR(dvfs_info->base)) {
		ret = PTR_ERR(dvfs_info->base);
		goto err_put_node;
	}

	dvfs_info->irq = irq_of_parse_and_map(np, 0);
	if (!dvfs_info->irq) {
		dev_err(dvfs_info->dev, "No cpufreq irq found\n");
		ret = -ENODEV;
		goto err_put_node;
	}

	ret = of_init_opp_table(dvfs_info->dev);
	if (ret) {
		dev_err(dvfs_info->dev, "failed to init OPP table: %d\n", ret);
		goto err_put_node;
	}

	ret = opp_init_cpufreq_table(dvfs_info->dev, &dvfs_info->freq_table);
	if (ret) {
		dev_err(dvfs_info->dev,
			"failed to init cpufreq table: %d\n", ret);
		goto err_put_node;
	}
	dvfs_info->freq_count = opp_get_opp_count(dvfs_info->dev);
	exynos_sort_descend_freq_table();

	if (of_property_read_u32(np, "clock-latency", &dvfs_info->latency))
		dvfs_info->latency = DEF_TRANS_LATENCY;

	dvfs_info->cpu_clk = devm_clk_get(dvfs_info->dev, "armclk");
	if (IS_ERR(dvfs_info->cpu_clk)) {
		dev_err(dvfs_info->dev, "Failed to get cpu clock\n");
		ret = PTR_ERR(dvfs_info->cpu_clk);
		goto err_free_table;
	}

	dvfs_info->cur_frequency = clk_get_rate(dvfs_info->cpu_clk);
	if (!dvfs_info->cur_frequency) {
		dev_err(dvfs_info->dev, "Failed to get clock rate\n");
		ret = -EINVAL;
		goto err_free_table;
	}
	dvfs_info->cur_frequency /= 1000;

	INIT_WORK(&dvfs_info->irq_work, exynos_cpufreq_work);
	ret = devm_request_irq(dvfs_info->dev, dvfs_info->irq,
				exynos_cpufreq_irq, IRQF_TRIGGER_NONE,
				CPUFREQ_NAME, dvfs_info);
	if (ret) {
		dev_err(dvfs_info->dev, "Failed to register IRQ\n");
		goto err_free_table;
	}

	ret = init_div_table();
	if (ret) {
		dev_err(dvfs_info->dev, "Failed to initialise div table\n");
		goto err_free_table;
	}

	exynos_enable_dvfs();
	ret = cpufreq_register_driver(&exynos_driver);
	if (ret) {
		dev_err(dvfs_info->dev,
			"%s: failed to register cpufreq driver\n", __func__);
		goto err_free_table;
	}

	of_node_put(np);
	dvfs_info->dvfs_enabled = true;
	return 0;

err_free_table:
	opp_free_cpufreq_table(dvfs_info->dev, &dvfs_info->freq_table);
err_put_node:
	of_node_put(np);
	dev_err(dvfs_info->dev, "%s: failed initialization\n", __func__);
	return ret;
}

static int exynos_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&exynos_driver);
	opp_free_cpufreq_table(dvfs_info->dev, &dvfs_info->freq_table);
	return 0;
}

static struct platform_driver exynos_cpufreq_platdrv = {
	.driver = {
		.name	= "exynos5440-cpufreq",
		.owner	= THIS_MODULE,
		.of_match_table = exynos_cpufreq_match,
	},
	.probe		= exynos_cpufreq_probe,
	.remove		= exynos_cpufreq_remove,
};
module_platform_driver(exynos_cpufreq_platdrv);

MODULE_AUTHOR("Amit Daniel Kachhap <amit.daniel@samsung.com>");
MODULE_DESCRIPTION("Exynos5440 cpufreq driver");
MODULE_LICENSE("GPL");
