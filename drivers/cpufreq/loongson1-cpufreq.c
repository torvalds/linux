/*
 * CPU Frequency Scaling for Loongson 1 SoC
 *
 * Copyright (C) 2014-2016 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <cpufreq.h>
#include <loongson1.h>

struct ls1x_cpufreq {
	struct device *dev;
	struct clk *clk;	/* CPU clk */
	struct clk *mux_clk;	/* MUX of CPU clk */
	struct clk *pll_clk;	/* PLL clk */
	struct clk *osc_clk;	/* OSC clk */
	unsigned int max_freq;
	unsigned int min_freq;
};

static struct ls1x_cpufreq *cpufreq;

static int ls1x_cpufreq_notifier(struct notifier_block *nb,
				 unsigned long val, void *data)
{
	if (val == CPUFREQ_POSTCHANGE)
		current_cpu_data.udelay_val = loops_per_jiffy;

	return NOTIFY_OK;
}

static struct notifier_block ls1x_cpufreq_notifier_block = {
	.notifier_call = ls1x_cpufreq_notifier
};

static int ls1x_cpufreq_target(struct cpufreq_policy *policy,
			       unsigned int index)
{
	struct device *cpu_dev = get_cpu_device(policy->cpu);
	unsigned int old_freq, new_freq;

	old_freq = policy->cur;
	new_freq = policy->freq_table[index].frequency;

	/*
	 * The procedure of reconfiguring CPU clk is as below.
	 *
	 *  - Reparent CPU clk to OSC clk
	 *  - Reset CPU clock (very important)
	 *  - Reconfigure CPU DIV
	 *  - Reparent CPU clk back to CPU DIV clk
	 */

	clk_set_parent(policy->clk, cpufreq->osc_clk);
	__raw_writel(__raw_readl(LS1X_CLK_PLL_DIV) | RST_CPU_EN | RST_CPU,
		     LS1X_CLK_PLL_DIV);
	__raw_writel(__raw_readl(LS1X_CLK_PLL_DIV) & ~(RST_CPU_EN | RST_CPU),
		     LS1X_CLK_PLL_DIV);
	clk_set_rate(cpufreq->mux_clk, new_freq * 1000);
	clk_set_parent(policy->clk, cpufreq->mux_clk);
	dev_dbg(cpu_dev, "%u KHz --> %u KHz\n", old_freq, new_freq);

	return 0;
}

static int ls1x_cpufreq_init(struct cpufreq_policy *policy)
{
	struct device *cpu_dev = get_cpu_device(policy->cpu);
	struct cpufreq_frequency_table *freq_tbl;
	unsigned int pll_freq, freq;
	int steps, i, ret;

	pll_freq = clk_get_rate(cpufreq->pll_clk) / 1000;

	steps = 1 << DIV_CPU_WIDTH;
	freq_tbl = kcalloc(steps, sizeof(*freq_tbl), GFP_KERNEL);
	if (!freq_tbl)
		return -ENOMEM;

	for (i = 0; i < (steps - 1); i++) {
		freq = pll_freq / (i + 1);
		if ((freq < cpufreq->min_freq) || (freq > cpufreq->max_freq))
			freq_tbl[i].frequency = CPUFREQ_ENTRY_INVALID;
		else
			freq_tbl[i].frequency = freq;
		dev_dbg(cpu_dev,
			"cpufreq table: index %d: frequency %d\n", i,
			freq_tbl[i].frequency);
	}
	freq_tbl[i].frequency = CPUFREQ_TABLE_END;

	policy->clk = cpufreq->clk;
	ret = cpufreq_generic_init(policy, freq_tbl, 0);
	if (ret)
		kfree(freq_tbl);

	return ret;
}

static int ls1x_cpufreq_exit(struct cpufreq_policy *policy)
{
	kfree(policy->freq_table);
	return 0;
}

static struct cpufreq_driver ls1x_cpufreq_driver = {
	.name		= "cpufreq-ls1x",
	.flags		= CPUFREQ_STICKY | CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= ls1x_cpufreq_target,
	.get		= cpufreq_generic_get,
	.init		= ls1x_cpufreq_init,
	.exit		= ls1x_cpufreq_exit,
	.attr		= cpufreq_generic_attr,
};

static int ls1x_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_notifier(&ls1x_cpufreq_notifier_block,
				    CPUFREQ_TRANSITION_NOTIFIER);
	cpufreq_unregister_driver(&ls1x_cpufreq_driver);

	return 0;
}

static int ls1x_cpufreq_probe(struct platform_device *pdev)
{
	struct plat_ls1x_cpufreq *pdata = dev_get_platdata(&pdev->dev);
	struct clk *clk;
	int ret;

	if (!pdata || !pdata->clk_name || !pdata->osc_clk_name) {
		dev_err(&pdev->dev, "platform data missing\n");
		return -EINVAL;
	}

	cpufreq =
	    devm_kzalloc(&pdev->dev, sizeof(struct ls1x_cpufreq), GFP_KERNEL);
	if (!cpufreq)
		return -ENOMEM;

	cpufreq->dev = &pdev->dev;

	clk = devm_clk_get(&pdev->dev, pdata->clk_name);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "unable to get %s clock\n",
			pdata->clk_name);
		return PTR_ERR(clk);
	}
	cpufreq->clk = clk;

	clk = clk_get_parent(clk);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "unable to get parent of %s clock\n",
			__clk_get_name(cpufreq->clk));
		return PTR_ERR(clk);
	}
	cpufreq->mux_clk = clk;

	clk = clk_get_parent(clk);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "unable to get parent of %s clock\n",
			__clk_get_name(cpufreq->mux_clk));
		return PTR_ERR(clk);
	}
	cpufreq->pll_clk = clk;

	clk = devm_clk_get(&pdev->dev, pdata->osc_clk_name);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "unable to get %s clock\n",
			pdata->osc_clk_name);
		return PTR_ERR(clk);
	}
	cpufreq->osc_clk = clk;

	cpufreq->max_freq = pdata->max_freq;
	cpufreq->min_freq = pdata->min_freq;

	ret = cpufreq_register_driver(&ls1x_cpufreq_driver);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to register CPUFreq driver: %d\n", ret);
		return ret;
	}

	ret = cpufreq_register_notifier(&ls1x_cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);

	if (ret) {
		dev_err(&pdev->dev,
			"failed to register CPUFreq notifier: %d\n",ret);
		cpufreq_unregister_driver(&ls1x_cpufreq_driver);
	}

	return ret;
}

static struct platform_driver ls1x_cpufreq_platdrv = {
	.probe	= ls1x_cpufreq_probe,
	.remove	= ls1x_cpufreq_remove,
	.driver	= {
		.name	= "ls1x-cpufreq",
	},
};

module_platform_driver(ls1x_cpufreq_platdrv);

MODULE_AUTHOR("Kelvin Cheung <keguang.zhang@gmail.com>");
MODULE_DESCRIPTION("Loongson1 CPUFreq driver");
MODULE_LICENSE("GPL");
