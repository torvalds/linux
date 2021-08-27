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

/* Clk register set */
#define ARMADA_37XX_CLK_TBG_SEL		0
#define ARMADA_37XX_CLK_TBG_SEL_CPU_OFF	22

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

/* AVS register set */
#define ARMADA_37XX_AVS_CTL0		0x0
#define	 ARMADA_37XX_AVS_ENABLE		BIT(30)
#define	 ARMADA_37XX_AVS_HIGH_VDD_LIMIT	16
#define	 ARMADA_37XX_AVS_LOW_VDD_LIMIT	22
#define	 ARMADA_37XX_AVS_VDD_MASK	0x3F
#define ARMADA_37XX_AVS_CTL2		0x8
#define	 ARMADA_37XX_AVS_LOW_VDD_EN	BIT(6)
#define ARMADA_37XX_AVS_VSET(x)	    (0x1C + 4 * (x))

/*
 * On Armada 37xx the Power management manages 4 level of CPU load,
 * each level can be associated with a CPU clock source, a CPU
 * divider, a VDD level, etc...
 */
#define LOAD_LEVEL_NR	4

#define MIN_VOLT_MV 1000
#define MIN_VOLT_MV_FOR_L1_1000MHZ 1108
#define MIN_VOLT_MV_FOR_L1_1200MHZ 1155

/*  AVS value for the corresponding voltage (in mV) */
static int avs_map[] = {
	747, 758, 770, 782, 793, 805, 817, 828, 840, 852, 863, 875, 887, 898,
	910, 922, 933, 945, 957, 968, 980, 992, 1003, 1015, 1027, 1038, 1050,
	1062, 1073, 1085, 1097, 1108, 1120, 1132, 1143, 1155, 1167, 1178, 1190,
	1202, 1213, 1225, 1237, 1248, 1260, 1272, 1283, 1295, 1307, 1318, 1330,
	1342
};

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
	u32 avs[LOAD_LEVEL_NR];
};

static struct armada_37xx_dvfs armada_37xx_dvfs[] = {
	/*
	 * The cpufreq scaling for 1.2 GHz variant of the SOC is currently
	 * unstable because we do not know how to configure it properly.
	 */
	/* {.cpu_freq_max = 1200*1000*1000, .divider = {1, 2, 4, 6} }, */
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
						 struct regmap *clk_base, u8 *divider)
{
	u32 cpu_tbg_sel;
	int load_lvl;

	/* Determine to which TBG clock is CPU connected */
	regmap_read(clk_base, ARMADA_37XX_CLK_TBG_SEL, &cpu_tbg_sel);
	cpu_tbg_sel >>= ARMADA_37XX_CLK_TBG_SEL_CPU_OFF;
	cpu_tbg_sel &= ARMADA_37XX_NB_TBG_SEL_MASK;

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

		/* Set TBG index, for all levels we use the same TBG */
		val = cpu_tbg_sel << ARMADA_37XX_NB_TBG_SEL_OFF;
		mask = (ARMADA_37XX_NB_TBG_SEL_MASK
			<< ARMADA_37XX_NB_TBG_SEL_OFF);

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
}

/*
 * Find out the armada 37x supported AVS value whose voltage value is
 * the round-up closest to the target voltage value.
 */
static u32 armada_37xx_avs_val_match(int target_vm)
{
	u32 avs;

	/* Find out the round-up closest supported voltage value */
	for (avs = 0; avs < ARRAY_SIZE(avs_map); avs++)
		if (avs_map[avs] >= target_vm)
			break;

	/*
	 * If all supported voltages are smaller than target one,
	 * choose the largest supported voltage
	 */
	if (avs == ARRAY_SIZE(avs_map))
		avs = ARRAY_SIZE(avs_map) - 1;

	return avs;
}

/*
 * For Armada 37xx soc, L0(VSET0) VDD AVS value is set to SVC revision
 * value or a default value when SVC is not supported.
 * - L0 can be read out from the register of AVS_CTRL_0 and L0 voltage
 *   can be got from the mapping table of avs_map.
 * - L1 voltage should be about 100mv smaller than L0 voltage
 * - L2 & L3 voltage should be about 150mv smaller than L0 voltage.
 * This function calculates L1 & L2 & L3 AVS values dynamically based
 * on L0 voltage and fill all AVS values to the AVS value table.
 * When base CPU frequency is 1000 or 1200 MHz then there is additional
 * minimal avs value for load L1.
 */
static void __init armada37xx_cpufreq_avs_configure(struct regmap *base,
						struct armada_37xx_dvfs *dvfs)
{
	unsigned int target_vm;
	int load_level = 0;
	u32 l0_vdd_min;

	if (base == NULL)
		return;

	/* Get L0 VDD min value */
	regmap_read(base, ARMADA_37XX_AVS_CTL0, &l0_vdd_min);
	l0_vdd_min = (l0_vdd_min >> ARMADA_37XX_AVS_LOW_VDD_LIMIT) &
		ARMADA_37XX_AVS_VDD_MASK;
	if (l0_vdd_min >= ARRAY_SIZE(avs_map))  {
		pr_err("L0 VDD MIN %d is not correct.\n", l0_vdd_min);
		return;
	}
	dvfs->avs[0] = l0_vdd_min;

	if (avs_map[l0_vdd_min] <= MIN_VOLT_MV) {
		/*
		 * If L0 voltage is smaller than 1000mv, then all VDD sets
		 * use L0 voltage;
		 */
		u32 avs_min = armada_37xx_avs_val_match(MIN_VOLT_MV);

		for (load_level = 1; load_level < LOAD_LEVEL_NR; load_level++)
			dvfs->avs[load_level] = avs_min;

		/*
		 * Set the avs values for load L0 and L1 when base CPU frequency
		 * is 1000/1200 MHz to its typical initial values according to
		 * the Armada 3700 Hardware Specifications.
		 */
		if (dvfs->cpu_freq_max >= 1000*1000*1000) {
			if (dvfs->cpu_freq_max >= 1200*1000*1000)
				avs_min = armada_37xx_avs_val_match(MIN_VOLT_MV_FOR_L1_1200MHZ);
			else
				avs_min = armada_37xx_avs_val_match(MIN_VOLT_MV_FOR_L1_1000MHZ);
			dvfs->avs[0] = dvfs->avs[1] = avs_min;
		}

		return;
	}

	/*
	 * L1 voltage is equal to L0 voltage - 100mv and it must be
	 * larger than 1000mv
	 */

	target_vm = avs_map[l0_vdd_min] - 100;
	target_vm = target_vm > MIN_VOLT_MV ? target_vm : MIN_VOLT_MV;
	dvfs->avs[1] = armada_37xx_avs_val_match(target_vm);

	/*
	 * L2 & L3 voltage is equal to L0 voltage - 150mv and it must
	 * be larger than 1000mv
	 */
	target_vm = avs_map[l0_vdd_min] - 150;
	target_vm = target_vm > MIN_VOLT_MV ? target_vm : MIN_VOLT_MV;
	dvfs->avs[2] = dvfs->avs[3] = armada_37xx_avs_val_match(target_vm);

	/*
	 * Fix the avs value for load L1 when base CPU frequency is 1000/1200 MHz,
	 * otherwise the CPU gets stuck when switching from load L1 to load L0.
	 * Also ensure that avs value for load L1 is not higher than for L0.
	 */
	if (dvfs->cpu_freq_max >= 1000*1000*1000) {
		u32 avs_min_l1;

		if (dvfs->cpu_freq_max >= 1200*1000*1000)
			avs_min_l1 = armada_37xx_avs_val_match(MIN_VOLT_MV_FOR_L1_1200MHZ);
		else
			avs_min_l1 = armada_37xx_avs_val_match(MIN_VOLT_MV_FOR_L1_1000MHZ);

		if (avs_min_l1 > dvfs->avs[0])
			avs_min_l1 = dvfs->avs[0];

		if (dvfs->avs[1] < avs_min_l1)
			dvfs->avs[1] = avs_min_l1;
	}
}

static void __init armada37xx_cpufreq_avs_setup(struct regmap *base,
						struct armada_37xx_dvfs *dvfs)
{
	unsigned int avs_val = 0;
	int load_level = 0;

	if (base == NULL)
		return;

	/* Disable AVS before the configuration */
	regmap_update_bits(base, ARMADA_37XX_AVS_CTL0,
			   ARMADA_37XX_AVS_ENABLE, 0);


	/* Enable low voltage mode */
	regmap_update_bits(base, ARMADA_37XX_AVS_CTL2,
			   ARMADA_37XX_AVS_LOW_VDD_EN,
			   ARMADA_37XX_AVS_LOW_VDD_EN);


	for (load_level = 1; load_level < LOAD_LEVEL_NR; load_level++) {
		avs_val = dvfs->avs[load_level];
		regmap_update_bits(base, ARMADA_37XX_AVS_VSET(load_level-1),
		    ARMADA_37XX_AVS_VDD_MASK << ARMADA_37XX_AVS_HIGH_VDD_LIMIT |
		    ARMADA_37XX_AVS_VDD_MASK << ARMADA_37XX_AVS_LOW_VDD_LIMIT,
		    avs_val << ARMADA_37XX_AVS_HIGH_VDD_LIMIT |
		    avs_val << ARMADA_37XX_AVS_LOW_VDD_LIMIT);
	}

	/* Enable AVS after the configuration */
	regmap_update_bits(base, ARMADA_37XX_AVS_CTL0,
			   ARMADA_37XX_AVS_ENABLE,
			   ARMADA_37XX_AVS_ENABLE);

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
	unsigned int cur_frequency, base_frequency;
	struct regmap *nb_clk_base, *nb_pm_base, *avs_base;
	struct device *cpu_dev;
	int load_lvl, ret;
	struct clk *clk, *parent;

	nb_clk_base =
		syscon_regmap_lookup_by_compatible("marvell,armada-3700-periph-clock-nb");
	if (IS_ERR(nb_clk_base))
		return -ENODEV;

	nb_pm_base =
		syscon_regmap_lookup_by_compatible("marvell,armada-3700-nb-pm");

	if (IS_ERR(nb_pm_base))
		return -ENODEV;

	avs_base =
		syscon_regmap_lookup_by_compatible("marvell,armada-3700-avs");

	/* if AVS is not present don't use it but still try to setup dvfs */
	if (IS_ERR(avs_base)) {
		pr_info("Syscon failed for Adapting Voltage Scaling: skip it\n");
		avs_base = NULL;
	}
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

	parent = clk_get_parent(clk);
	if (IS_ERR(parent)) {
		dev_err(cpu_dev, "Cannot get parent clock for CPU0\n");
		clk_put(clk);
		return PTR_ERR(parent);
	}

	/* Get parent CPU frequency */
	base_frequency =  clk_get_rate(parent);

	if (!base_frequency) {
		dev_err(cpu_dev, "Failed to get parent clock rate for CPU\n");
		clk_put(clk);
		return -EINVAL;
	}

	/* Get nominal (current) CPU frequency */
	cur_frequency = clk_get_rate(clk);
	if (!cur_frequency) {
		dev_err(cpu_dev, "Failed to get clock rate for CPU\n");
		clk_put(clk);
		return -EINVAL;
	}

	dvfs = armada_37xx_cpu_freq_info_get(base_frequency);
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

	armada37xx_cpufreq_avs_configure(avs_base, dvfs);
	armada37xx_cpufreq_avs_setup(avs_base, dvfs);

	armada37xx_cpufreq_dvfs_setup(nb_pm_base, nb_clk_base, dvfs->divider);
	clk_put(clk);

	for (load_lvl = ARMADA_37XX_DVFS_LOAD_0; load_lvl < LOAD_LEVEL_NR;
	     load_lvl++) {
		unsigned long u_volt = avs_map[dvfs->avs[load_lvl]] * 1000;
		freq = base_frequency / dvfs->divider[load_lvl];
		ret = dev_pm_opp_add(cpu_dev, freq, u_volt);
		if (ret)
			goto remove_opp;


	}

	/* Now that everything is setup, enable the DVFS at hardware level */
	armada37xx_cpufreq_enable_dvfs(nb_pm_base);

	memset(&pdata, 0, sizeof(pdata));
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
		freq = base_frequency / dvfs->divider[load_lvl];
		dev_pm_opp_remove(cpu_dev, freq);
	}

	kfree(armada37xx_cpufreq_state);

	return ret;
}
/* late_initcall, to guarantee the driver is loaded after A37xx clock driver */
late_initcall(armada37xx_cpufreq_driver_init);

static const struct of_device_id __maybe_unused armada37xx_cpufreq_of_match[] = {
	{ .compatible = "marvell,armada-3700-nb-pm" },
	{ },
};
MODULE_DEVICE_TABLE(of, armada37xx_cpufreq_of_match);

MODULE_AUTHOR("Gregory CLEMENT <gregory.clement@free-electrons.com>");
MODULE_DESCRIPTION("Armada 37xx cpufreq driver");
MODULE_LICENSE("GPL");
