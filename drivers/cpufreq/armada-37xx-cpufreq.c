// SPDX-License-Identifier: GPL-2.0+
/*
 * CPU frequency scaling support for Armada 37xx platform.
 *
 * Copyright (C) 2017 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 */

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "cpufreq-dt.h"

/* Power management in North Bridge register set */
#define ARMADA_37XX_NB_L0L1	0x18
#define ARMADA_37XX_NB_L2L3	0x1C
#define  ARMADA_37XX_NB_TBG_DIV_OFF	13
#define  ARMADA_37XX_NB_TBG_DIV_MASK	0x7
#define  ARMADA_37XX_NB_CLK_SEL_OFF	11
#define  ARMADA_37XX_NB_CLK_SEL_MASK	0x1
#define  ARMADA_37XX_NB_CLK_SEL_TBG	0x1
#define  ARMADA_37XX_NB_TBG_SEL_OFF	9
#define  ARMADA_37XX_NB_TBG_SEL_MASK	0x3
#define  ARMADA_37XX_NB_VDD_SEL_OFF	6
#define  ARMADA_37XX_NB_VDD_SEL_MASK	0x3
#define  ARMADA_37XX_NB_CONFIG_SHIFT	16
#define ARMADA_37XX_NB_DYN_MOD	0x24
#define  ARMADA_37XX_NB_CLK_SEL_EN	BIT(26)
#define  ARMADA_37XX_NB_TBG_EN		BIT(28)
#define  ARMADA_37XX_NB_DIV_EN		BIT(29)
#define  ARMADA_37XX_NB_VDD_EN		BIT(30)
#define  ARMADA_37XX_NB_DFS_EN		BIT(31)
#define ARMADA_37XX_NB_CPU_LOAD 0x30
#define  ARMADA_37XX_NB_CPU_LOAD_MASK	0x3
#define  ARMADA_37XX_DVFS_LOAD_0	0
#define  ARMADA_37XX_DVFS_LOAD_1	1
#define  ARMADA_37XX_DVFS_LOAD_2	2
#define  ARMADA_37XX_DVFS_LOAD_3	3

/*
 * On Armada 37xx the Power management manages 4 level of CPU load,
 * each level can be associated with a CPU clock source, a CPU
 * divider, a VDD level, etc...
 */
#define LOAD_LEVEL_NR	4

struct armada37xx_cpufreq_state {
	struct regmap *regmap;
	u32 nb_l0l1;
	u32 nb_l2l3;
	u32 nb_dyn_mod;
	u32 nb_cpu_load;
};

static struct armada37xx_cpufreq_state *armada37xx_cpufreq_state;

struct armada_37xx_dvfs {
	u32 cpu_freq_max;
	u8 divider[LOAD_LEVEL_NR];
};

static struct armada_37xx_dvfs armada_37xx_dvfs[] = {
	{.cpu_freq_max = 1200*1000*1000, .divider = {1, 2, 4, 6} },
	{.cpu_freq_max = 1000*1000*1000, .divider = {1, 2, 4, 5} },
	{.cpu_freq_max = 800*1000*1000,  .divider = {1, 2, 3, 4} },
	{.cpu_freq_max = 600*1000*1000,  .divider = {2, 4, 5, 6} },
};

static struct armada_37xx_dvfs *armada_37xx_cpu_freq_info_get(u32 freq)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(armada_37xx_dvfs); i++) {
		if (freq == armada_37xx_dvfs[i].cpu_freq_max)
			return &armada_37xx_dvfs[i];
	}

	pr_err("Unsupported CPU frequency %d MHz\n", freq/1000000);
	return NULL;
}

/*
 * Setup the four level managed by the hardware. Once the four level
 * will be configured then the DVFS will be enabled.
 */
static void __init armada37xx_cpufreq_dvfs_setup(struct regmap *base,
						 struct clk *clk, u8 *divider)
{
	int load_lvl;
	struct clk *parent;

	for (load_lvl = 0; load_lvl < LOAD_LEVEL_NR; load_lvl++) {
		unsigned int reg, mask, val, offset = 0;

		if (load_lvl <= ARMADA_37XX_DVFS_LOAD_1)
			reg = ARMADA_37XX_NB_L0L1;
		else
			reg = ARMADA_37XX_NB_L2L3;

		if (load_lvl == ARMADA_37XX_DVFS_LOAD_0 ||
		    load_lvl == ARMADA_37XX_DVFS_LOAD_2)
			offset += ARMADA_37XX_NB_CONFIG_SHIFT;

		/* Set cpu clock source, for all the level we use TBG */
		val = ARMADA_37XX_NB_CLK_SEL_TBG << ARMADA_37XX_NB_CLK_SEL_OFF;
		mask = (ARMADA_37XX_NB_CLK_SEL_MASK
			<< ARMADA_37XX_NB_CLK_SEL_OFF);

		/*
		 * Set cpu divider based on the pre-computed array in
		 * order to have balanced step.
		 */
		val |= divider[load_lvl] << ARMADA_37XX_NB_TBG_DIV_OFF;
		mask |= (ARMADA_37XX_NB_TBG_DIV_MASK
			<< ARMADA_37XX_NB_TBG_DIV_OFF);

		/* Set VDD divider which is actually the load level. */
		val |= load_lvl << ARMADA_37XX_NB_VDD_SEL_OFF;
		mask |= (ARMADA_37XX_NB_VDD_SEL_MASK
			<< ARMADA_37XX_NB_VDD_SEL_OFF);

		val <<= offset;
		mask <<= offset;

		regmap_update_bits(base, reg, mask, val);
	}

	/*
	 * Set cpu clock source, for all the level we keep the same
	 * clock source that the one already configured. For this one
	 * we need to use the clock framework
	 */
	parent = clk_get_parent(clk);
	clk_set_parent(clk, parent);
}

static void armada37xx_cpufreq_disable_dvfs(struct regmap *base)
{
	unsigned int reg = ARMADA_37XX_NB_DYN_MOD,
		mask = ARMADA_37XX_NB_DFS_EN;

	regmap_update_bits(base, reg, mask, 0);
}

static void __init armada37xx_cpufreq_enable_dvfs(struct regmap *base)
{
	unsigned int val, reg = ARMADA_37XX_NB_CPU_LOAD,
		mask = ARMADA_37XX_NB_CPU_LOAD_MASK;

	/* Start with the highest load (0) */
	val = ARMADA_37XX_DVFS_LOAD_0;
	regmap_update_bits(base, reg, mask, val);

	/* Now enable DVFS for the CPUs */
	reg = ARMADA_37XX_NB_DYN_MOD;
	mask =	ARMADA_37XX_NB_CLK_SEL_EN | ARMADA_37XX_NB_TBG_EN |
		ARMADA_37XX_NB_DIV_EN | ARMADA_37XX_NB_VDD_EN |
		ARMADA_37XX_NB_DFS_EN;

	regmap_update_bits(base, reg, mask, mask);
}

static int armada37xx_cpufreq_suspend(struct cpufreq_policy *policy)
{
	struct armada37xx_cpufreq_state *state = armada37xx_cpufreq_state;

	regmap_read(state->regmap, ARMADA_37XX_NB_L0L1, &state->nb_l0l1);
	regmap_read(state->regmap, ARMADA_37XX_NB_L2L3, &state->nb_l2l3);
	regmap_read(state->regmap, ARMADA_37XX_NB_CPU_LOAD,
		    &state->nb_cpu_load);
	regmap_read(state->regmap, ARMADA_37XX_NB_DYN_MOD, &state->nb_dyn_mod);

	return 0;
}

static int armada37xx_cpufreq_resume(struct cpufreq_policy *policy)
{
	struct armada37xx_cpufreq_state *state = armada37xx_cpufreq_state;

	/* Ensure DVFS is disabled otherwise the following registers are RO */
	armada37xx_cpufreq_disable_dvfs(state->regmap);

	regmap_write(state->regmap, ARMADA_37XX_NB_L0L1, state->nb_l0l1);
	regmap_write(state->regmap, ARMADA_37XX_NB_L2L3, state->nb_l2l3);
	regmap_write(state->regmap, ARMADA_37XX_NB_CPU_LOAD,
		     state->nb_cpu_load);

	/*
	 * NB_DYN_MOD register is the one that actually enable back DVFS if it
	 * was enabled before the suspend operation. This must be done last
	 * otherwise other registers are not writable.
	 */
	regmap_write(state->regmap, ARMADA_37XX_NB_DYN_MOD, state->nb_dyn_mod);

	return 0;
}

static int __init armada37xx_cpufreq_driver_init(void)
{
	struct cpufreq_dt_platform_data pdata;
	struct armada_37xx_dvfs *dvfs;
	struct platform_device *pdev;
	unsigned long freq;
	unsigned int cur_frequency;
	struct regmap *nb_pm_base;
	struct device *cpu_dev;
	int load_lvl, ret;
	struct clk *clk;

	nb_pm_base =
		syscon_regmap_lookup_by_compatible("marvell,armada-3700-nb-pm");

	if (IS_ERR(nb_pm_base))
		return -ENODEV;

	/* Before doing any configuration on the DVFS first, disable it */
	armada37xx_cpufreq_disable_dvfs(nb_pm_base);

	/*
	 * On CPU 0 register the operating points supported (which are
	 * the nominal CPU frequency and full integer divisions of
	 * it).
	 */
	cpu_dev = get_cpu_device(0);
	if (!cpu_dev) {
		dev_err(cpu_dev, "Cannot get CPU\n");
		return -ENODEV;
	}

	clk = clk_get(cpu_dev, 0);
	if (IS_ERR(clk)) {
		dev_err(cpu_dev, "Cannot get clock for CPU0\n");
		return PTR_ERR(clk);
	}

	/* Get nominal (current) CPU frequency */
	cur_frequency = clk_get_rate(clk);
	if (!cur_frequency) {
		dev_err(cpu_dev, "Failed to get clock rate for CPU\n");
		clk_put(clk);
		return -EINVAL;
	}

	dvfs = armada_37xx_cpu_freq_info_get(cur_frequency);
	if (!dvfs) {
		clk_put(clk);
		return -EINVAL;
	}

	armada37xx_cpufreq_state = kmalloc(sizeof(*armada37xx_cpufreq_state),
					   GFP_KERNEL);
	if (!armada37xx_cpufreq_state) {
		clk_put(clk);
		return -ENOMEM;
	}

	armada37xx_cpufreq_state->regmap = nb_pm_base;

	armada37xx_cpufreq_dvfs_setup(nb_pm_base, clk, dvfs->divider);
	clk_put(clk);

	for (load_lvl = ARMADA_37XX_DVFS_LOAD_0; load_lvl < LOAD_LEVEL_NR;
	     load_lvl++) {
		freq = cur_frequency / dvfs->divider[load_lvl];

		ret = dev_pm_opp_add(cpu_dev, freq, 0);
		if (ret)
			goto remove_opp;
	}

	/* Now that everything is setup, enable the DVFS at hardware level */
	armada37xx_cpufreq_enable_dvfs(nb_pm_base);

	pdata.suspend = armada37xx_cpufreq_suspend;
	pdata.resume = armada37xx_cpufreq_resume;

	pdev = platform_device_register_data(NULL, "cpufreq-dt", -1, &pdata,
					     sizeof(pdata));
	ret = PTR_ERR_OR_ZERO(pdev);
	if (ret)
		goto disable_dvfs;

	return 0;

disable_dvfs:
	armada37xx_cpufreq_disable_dvfs(nb_pm_base);
remove_opp:
	/* clean-up the already added opp before leaving */
	while (load_lvl-- > ARMADA_37XX_DVFS_LOAD_0) {
		freq = cur_frequency / dvfs->divider[load_lvl];
		dev_pm_opp_remove(cpu_dev, freq);
	}

	kfree(armada37xx_cpufreq_state);

	return ret;
}
/* late_initcall, to guarantee the driver is loaded after A37xx clock driver */
late_initcall(armada37xx_cpufreq_driver_init);

MODULE_AUTHOR("Gregory CLEMENT <gregory.clement@free-electrons.com>");
MODULE_DESCRIPTION("Armada 37xx cpufreq driver");
MODULE_LICENSE("GPL");
