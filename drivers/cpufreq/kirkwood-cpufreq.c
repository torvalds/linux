// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	kirkwood_freq.c: cpufreq driver for the Marvell kirkwood
 *
 *	Copyright (C) 2013 Andrew Lunn <andrew@lunn.ch>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
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
 * The frequencies are set at runtime before registering this table.
 */
static struct cpufreq_frequency_table kirkwood_freq_table[] = {
	{0, STATE_CPU_FREQ,	0}, /* CPU uses cpuclk */
	{0, STATE_DDR_FREQ,	0}, /* CPU uses ddrclk */
	{0, 0,			CPUFREQ_TABLE_END},
};

static unsigned int kirkwood_cpufreq_get_cpu_frequency(unsigned int cpu)
{
	return clk_get_rate(priv.powersave_clk) / 1000;
}

static int kirkwood_cpufreq_target(struct cpufreq_policy *policy,
			    unsigned int index)
{
	unsigned int state = kirkwood_freq_table[index].driver_data;
	unsigned long reg;

	local_irq_disable();

	/* Disable interrupts to the CPU */
	reg = readl_relaxed(priv.base);
	reg |= CPU_SW_INT_BLK;
	writel_relaxed(reg, priv.base);

	switch (state) {
	case STATE_CPU_FREQ:
		clk_set_parent(priv.powersave_clk, priv.cpu_clk);
		break;
	case STATE_DDR_FREQ:
		clk_set_parent(priv.powersave_clk, priv.ddr_clk);
		break;
	}

	/* Wait-for-Interrupt, while the hardware changes frequency */
	cpu_do_idle();

	/* Enable interrupts to the CPU */
	reg = readl_relaxed(priv.base);
	reg &= ~CPU_SW_INT_BLK;
	writel_relaxed(reg, priv.base);

	local_irq_enable();

	return 0;
}

/* Module init and exit code */
static int kirkwood_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	cpufreq_generic_init(policy, kirkwood_freq_table, 5000);
	return 0;
}

static struct cpufreq_driver kirkwood_cpufreq_driver = {
	.flags	= CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.get	= kirkwood_cpufreq_get_cpu_frequency,
	.verify	= cpufreq_generic_frequency_table_verify,
	.target_index = kirkwood_cpufreq_target,
	.init	= kirkwood_cpufreq_cpu_init,
	.name	= "kirkwood-cpufreq",
};

static int kirkwood_cpufreq_probe(struct platform_device *pdev)
{
	struct device_node *np;
	int err;

	priv.dev = &pdev->dev;

	priv.base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv.base))
		return PTR_ERR(priv.base);

	np = of_cpu_device_node_get(0);
	if (!np) {
		dev_err(&pdev->dev, "failed to get cpu device node\n");
		return -ENODEV;
	}

	priv.cpu_clk = of_clk_get_by_name(np, "cpu_clk");
	if (IS_ERR(priv.cpu_clk)) {
		dev_err(priv.dev, "Unable to get cpuclk\n");
		err = PTR_ERR(priv.cpu_clk);
		goto out_node;
	}

	err = clk_prepare_enable(priv.cpu_clk);
	if (err) {
		dev_err(priv.dev, "Unable to prepare cpuclk\n");
		goto out_node;
	}

	kirkwood_freq_table[0].frequency = clk_get_rate(priv.cpu_clk) / 1000;

	priv.ddr_clk = of_clk_get_by_name(np, "ddrclk");
	if (IS_ERR(priv.ddr_clk)) {
		dev_err(priv.dev, "Unable to get ddrclk\n");
		err = PTR_ERR(priv.ddr_clk);
		goto out_cpu;
	}

	err = clk_prepare_enable(priv.ddr_clk);
	if (err) {
		dev_err(priv.dev, "Unable to prepare ddrclk\n");
		goto out_cpu;
	}
	kirkwood_freq_table[1].frequency = clk_get_rate(priv.ddr_clk) / 1000;

	priv.powersave_clk = of_clk_get_by_name(np, "powersave");
	if (IS_ERR(priv.powersave_clk)) {
		dev_err(priv.dev, "Unable to get powersave\n");
		err = PTR_ERR(priv.powersave_clk);
		goto out_ddr;
	}
	err = clk_prepare_enable(priv.powersave_clk);
	if (err) {
		dev_err(priv.dev, "Unable to prepare powersave clk\n");
		goto out_ddr;
	}

	err = cpufreq_register_driver(&kirkwood_cpufreq_driver);
	if (err) {
		dev_err(priv.dev, "Failed to register cpufreq driver\n");
		goto out_powersave;
	}

	of_node_put(np);
	return 0;

out_powersave:
	clk_disable_unprepare(priv.powersave_clk);
out_ddr:
	clk_disable_unprepare(priv.ddr_clk);
out_cpu:
	clk_disable_unprepare(priv.cpu_clk);
out_node:
	of_node_put(np);

	return err;
}

static void kirkwood_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&kirkwood_cpufreq_driver);

	clk_disable_unprepare(priv.powersave_clk);
	clk_disable_unprepare(priv.ddr_clk);
	clk_disable_unprepare(priv.cpu_clk);
}

static struct platform_driver kirkwood_cpufreq_platform_driver = {
	.probe = kirkwood_cpufreq_probe,
	.remove = kirkwood_cpufreq_remove,
	.driver = {
		.name = "kirkwood-cpufreq",
	},
};

module_platform_driver(kirkwood_cpufreq_platform_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Andrew Lunn <andrew@lunn.ch");
MODULE_DESCRIPTION("cpufreq driver for Marvell's kirkwood CPU");
MODULE_ALIAS("platform:kirkwood-cpufreq");
