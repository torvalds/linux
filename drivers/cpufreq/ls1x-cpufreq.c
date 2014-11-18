/*
 * CPU Frequency Scaling for Loongson 1 SoC
 *
 * Copyright (C) 2014 Zhang, Keguang <keguang.zhang@gmail.com>
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
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <asm/mach-loongson1/cpufreq.h>
#include <asm/mach-loongson1/loongson1.h>

static struct {
	struct device *dev;
	struct clk *clk;	/* CPU clk */
	struct clk *mux_clk;	/* MUX of CPU clk */
	struct clk *pll_clk;	/* PLL clk */
	struct clk *osc_clk;	/* OSC clk */
	unsigned int max_freq;
	unsigned int min_freq;
} ls1x_cpufreq;

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

	dev_dbg(ls1x_cpufreq.dev, "%u KHz --> %u KHz\n", old_freq, new_freq);
	clk_set_parent(policy->clk, ls1x_cpufreq.osc_clk);
	__raw_writel(__raw_readl(LS1X_CLK_PLL_DIV) | RST_CPU_EN | RST_CPU,
		     LS1X_CLK_PLL_DIV);
	__raw_writel(__raw_readl(LS1X_CLK_PLL_DIV) & ~(RST_CPU_EN | RST_CPU),
		     LS1X_CLK_PLL_DIV);
	clk_set_rate(ls1x_cpufreq.mux_clk, new_freq * 1000);
	clk_set_parent(policy->clk, ls1x_cpufreq.mux_clk);

	return 0;
}

static int ls1x_cpufreq_init(struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *freq_tbl;
	unsigned int pll_freq, freq;
	int steps, i, ret;

	pll_freq = clk_get_rate(ls1x_cpufreq.pll_clk) / 1000;

	steps = 1 << DIV_CPU_WIDTH;
	freq_tbl = kzalloc(sizeof(*freq_tbl) * steps, GFP_KERNEL);
	if (!freq_tbl) {
		dev_err(ls1x_cpufreq.dev,
			"failed to alloc cpufreq_frequency_table\n");
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < (steps - 1); i++) {
		freq = pll_freq / (i + 1);
		if ((freq < ls1x_cpufreq.min_freq) ||
		    (freq > ls1x_cpufreq.max_freq))
			freq_tbl[i].frequency = CPUFREQ_ENTRY_INVALID;
		else
			freq_tbl[i].frequency = freq;
		dev_dbg(ls1x_cpufreq.dev,
			"cpufreq table: index %d: frequency %d\n", i,
			freq_tbl[i].frequency);
	}
	freq_tbl[i].frequency = CPUFREQ_TABLE_END;

	policy->clk = ls1x_cpufreq.clk;
	ret = cpufreq_generic_init(policy, freq_tbl, 0);
	if (ret)
		kfree(freq_tbl);
out:
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
	struct plat_ls1x_cpufreq *pdata = pdev->dev.platform_data;
	struct clk *clk;
	int ret;

	if (!pdata || !pdata->clk_name || !pdata->osc_clk_name)
		return -EINVAL;

	ls1x_cpufreq.dev = &pdev->dev;

	clk = devm_clk_get(&pdev->dev, pdata->clk_name);
	if (IS_ERR(clk)) {
		dev_err(ls1x_cpufreq.dev, "unable to get %s clock\n",
			pdata->clk_name);
		ret = PTR_ERR(clk);
		goto out;
	}
	ls1x_cpufreq.clk = clk;

	clk = clk_get_parent(clk);
	if (IS_ERR(clk)) {
		dev_err(ls1x_cpufreq.dev, "unable to get parent of %s clock\n",
			__clk_get_name(ls1x_cpufreq.clk));
		ret = PTR_ERR(clk);
		goto out;
	}
	ls1x_cpufreq.mux_clk = clk;

	clk = clk_get_parent(clk);
	if (IS_ERR(clk)) {
		dev_err(ls1x_cpufreq.dev, "unable to get parent of %s clock\n",
			__clk_get_name(ls1x_cpufreq.mux_clk));
		ret = PTR_ERR(clk);
		goto out;
	}
	ls1x_cpufreq.pll_clk = clk;

	clk = devm_clk_get(&pdev->dev, pdata->osc_clk_name);
	if (IS_ERR(clk)) {
		dev_err(ls1x_cpufreq.dev, "unable to get %s clock\n",
			pdata->osc_clk_name);
		ret = PTR_ERR(clk);
		goto out;
	}
	ls1x_cpufreq.osc_clk = clk;

	ls1x_cpufreq.max_freq = pdata->max_freq;
	ls1x_cpufreq.min_freq = pdata->min_freq;

	ret = cpufreq_register_driver(&ls1x_cpufreq_driver);
	if (ret) {
		dev_err(ls1x_cpufreq.dev,
			"failed to register cpufreq driver: %d\n", ret);
		goto out;
	}

	ret = cpufreq_register_notifier(&ls1x_cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);

	if (!ret)
		goto out;

	dev_err(ls1x_cpufreq.dev, "failed to register cpufreq notifier: %d\n",
		ret);

	cpufreq_unregister_driver(&ls1x_cpufreq_driver);
out:
	return ret;
}

static struct platform_driver ls1x_cpufreq_platdrv = {
	.driver = {
		.name	= "ls1x-cpufreq",
		.owner	= THIS_MODULE,
	},
	.probe		= ls1x_cpufreq_probe,
	.remove		= ls1x_cpufreq_remove,
};

module_platform_driver(ls1x_cpufreq_platdrv);

MODULE_AUTHOR("Kelvin Cheung <keguang.zhang@gmail.com>");
MODULE_DESCRIPTION("Loongson 1 CPUFreq driver");
MODULE_LICENSE("GPL");
