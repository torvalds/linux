/*
 *	kirkwood_freq.c: cpufreq driver for the Marvell kirkwood
 *
 *	Copyright (C) 2013 Andrew Lunn <andrew@lunn.ch>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/cpufreq.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <asm/proc-fns.h>

#define CPU_SW_INT_BLK BIT(28)

static struct priv
{
	struct clk *cpu_clk;
	struct clk *ddr_clk;
	struct clk *powersave_clk;
	struct device *dev;
	void __iomem *base;
} priv;

#define STATE_CPU_FREQ 0x01
#define STATE_DDR_FREQ 0x02

/*
 * Kirkwood can swap the clock to the CPU between two clocks:
 *
 * - cpu clk
 * - ddr clk
 *
 * The frequencies are set at runtime before registering this *
 * table.
 */
static struct cpufreq_frequency_table kirkwood_freq_table[] = {
	{STATE_CPU_FREQ,	0}, /* CPU uses cpuclk */
	{STATE_DDR_FREQ,	0}, /* CPU uses ddrclk */
	{0,			CPUFREQ_TABLE_END},
};

static unsigned int kirkwood_cpufreq_get_cpu_frequency(unsigned int cpu)
{
	if (__clk_is_enabled(priv.powersave_clk))
		return kirkwood_freq_table[1].frequency;
	return kirkwood_freq_table[0].frequency;
}

static void kirkwood_cpufreq_set_cpu_state(struct cpufreq_policy *policy,
		unsigned int index)
{
	struct cpufreq_freqs freqs;
	unsigned int state = kirkwood_freq_table[index].driver_data;
	unsigned long reg;

	freqs.old = kirkwood_cpufreq_get_cpu_frequency(0);
	freqs.new = kirkwood_freq_table[index].frequency;

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	dev_dbg(priv.dev, "Attempting to set frequency to %i KHz\n",
		kirkwood_freq_table[index].frequency);
	dev_dbg(priv.dev, "old frequency was %i KHz\n",
		kirkwood_cpufreq_get_cpu_frequency(0));

	if (freqs.old != freqs.new) {
		local_irq_disable();

		/* Disable interrupts to the CPU */
		reg = readl_relaxed(priv.base);
		reg |= CPU_SW_INT_BLK;
		writel_relaxed(reg, priv.base);

		switch (state) {
		case STATE_CPU_FREQ:
			clk_disable(priv.powersave_clk);
			break;
		case STATE_DDR_FREQ:
			clk_enable(priv.powersave_clk);
			break;
		}

		/* Wait-for-Interrupt, while the hardware changes frequency */
		cpu_do_idle();

		/* Enable interrupts to the CPU */
		reg = readl_relaxed(priv.base);
		reg &= ~CPU_SW_INT_BLK;
		writel_relaxed(reg, priv.base);

		local_irq_enable();
	}
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);
};

static int kirkwood_cpufreq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, kirkwood_freq_table);
}

static int kirkwood_cpufreq_target(struct cpufreq_policy *policy,
			    unsigned int target_freq,
			    unsigned int relation)
{
	unsigned int index = 0;

	if (cpufreq_frequency_table_target(policy, kirkwood_freq_table,
				target_freq, relation, &index))
		return -EINVAL;

	kirkwood_cpufreq_set_cpu_state(policy, index);

	return 0;
}

/* Module init and exit code */
static int kirkwood_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	int result;

	/* cpuinfo and default policy values */
	policy->cpuinfo.transition_latency = 5000; /* 5uS */
	policy->cur = kirkwood_cpufreq_get_cpu_frequency(0);

	result = cpufreq_frequency_table_cpuinfo(policy, kirkwood_freq_table);
	if (result)
		return result;

	cpufreq_frequency_table_get_attr(kirkwood_freq_table, policy->cpu);

	return 0;
}

static int kirkwood_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static struct freq_attr *kirkwood_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver kirkwood_cpufreq_driver = {
	.get	= kirkwood_cpufreq_get_cpu_frequency,
	.verify	= kirkwood_cpufreq_verify,
	.target	= kirkwood_cpufreq_target,
	.init	= kirkwood_cpufreq_cpu_init,
	.exit	= kirkwood_cpufreq_cpu_exit,
	.name	= "kirkwood-cpufreq",
	.attr	= kirkwood_cpufreq_attr,
};

static int kirkwood_cpufreq_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct resource *res;
	int err;

	priv.dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv.base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv.base))
		return PTR_ERR(priv.base);

	np = of_find_node_by_path("/cpus/cpu@0");
	if (!np)
		return -ENODEV;

	priv.cpu_clk = of_clk_get_by_name(np, "cpu_clk");
	if (IS_ERR(priv.cpu_clk)) {
		dev_err(priv.dev, "Unable to get cpuclk");
		return PTR_ERR(priv.cpu_clk);
	}

	clk_prepare_enable(priv.cpu_clk);
	kirkwood_freq_table[0].frequency = clk_get_rate(priv.cpu_clk) / 1000;

	priv.ddr_clk = of_clk_get_by_name(np, "ddrclk");
	if (IS_ERR(priv.ddr_clk)) {
		dev_err(priv.dev, "Unable to get ddrclk");
		err = PTR_ERR(priv.ddr_clk);
		goto out_cpu;
	}

	clk_prepare_enable(priv.ddr_clk);
	kirkwood_freq_table[1].frequency = clk_get_rate(priv.ddr_clk) / 1000;

	priv.powersave_clk = of_clk_get_by_name(np, "powersave");
	if (IS_ERR(priv.powersave_clk)) {
		dev_err(priv.dev, "Unable to get powersave");
		err = PTR_ERR(priv.powersave_clk);
		goto out_ddr;
	}
	clk_prepare(priv.powersave_clk);

	of_node_put(np);
	np = NULL;

	err = cpufreq_register_driver(&kirkwood_cpufreq_driver);
	if (!err)
		return 0;

	dev_err(priv.dev, "Failed to register cpufreq driver");

	clk_disable_unprepare(priv.powersave_clk);
out_ddr:
	clk_disable_unprepare(priv.ddr_clk);
out_cpu:
	clk_disable_unprepare(priv.cpu_clk);
	of_node_put(np);

	return err;
}

static int kirkwood_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&kirkwood_cpufreq_driver);

	clk_disable_unprepare(priv.powersave_clk);
	clk_disable_unprepare(priv.ddr_clk);
	clk_disable_unprepare(priv.cpu_clk);

	return 0;
}

static struct platform_driver kirkwood_cpufreq_platform_driver = {
	.probe = kirkwood_cpufreq_probe,
	.remove = kirkwood_cpufreq_remove,
	.driver = {
		.name = "kirkwood-cpufreq",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(kirkwood_cpufreq_platform_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Andrew Lunn <andrew@lunn.ch");
MODULE_DESCRIPTION("cpufreq driver for Marvell's kirkwood CPU");
MODULE_ALIAS("platform:kirkwood-cpufreq");
