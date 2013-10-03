/*
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * CPU frequency scaling for S5PC110/S5PV210
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/cpufreq.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>
#include <linux/suspend.h>

#include <mach/map.h>
#include <mach/regs-clock.h>

static struct clk *cpu_clk;
static struct clk *dmc0_clk;
static struct clk *dmc1_clk;
static struct cpufreq_freqs freqs;
static DEFINE_MUTEX(set_freq_lock);

/* APLL M,P,S values for 1G/800Mhz */
#define APLL_VAL_1000	((1 << 31) | (125 << 16) | (3 << 8) | 1)
#define APLL_VAL_800	((1 << 31) | (100 << 16) | (3 << 8) | 1)

/* Use 800MHz when entering sleep mode */
#define SLEEP_FREQ	(800 * 1000)

/*
 * relation has an additional symantics other than the standard of cpufreq
 * DISALBE_FURTHER_CPUFREQ: disable further access to target
 * ENABLE_FURTUER_CPUFREQ: enable access to target
 */
enum cpufreq_access {
	DISABLE_FURTHER_CPUFREQ = 0x10,
	ENABLE_FURTHER_CPUFREQ = 0x20,
};

static bool no_cpufreq_access;

/*
 * DRAM configurations to calculate refresh counter for changing
 * frequency of memory.
 */
struct dram_conf {
	unsigned long freq;	/* HZ */
	unsigned long refresh;	/* DRAM refresh counter * 1000 */
};

/* DRAM configuration (DMC0 and DMC1) */
static struct dram_conf s5pv210_dram_conf[2];

enum perf_level {
	L0, L1, L2, L3, L4,
};

enum s5pv210_mem_type {
	LPDDR	= 0x1,
	LPDDR2	= 0x2,
	DDR2	= 0x4,
};

enum s5pv210_dmc_port {
	DMC0 = 0,
	DMC1,
};

static struct cpufreq_frequency_table s5pv210_freq_table[] = {
	{L0, 1000*1000},
	{L1, 800*1000},
	{L2, 400*1000},
	{L3, 200*1000},
	{L4, 100*1000},
	{0, CPUFREQ_TABLE_END},
};

static struct regulator *arm_regulator;
static struct regulator *int_regulator;

struct s5pv210_dvs_conf {
	int arm_volt;	/* uV */
	int int_volt;	/* uV */
};

static const int arm_volt_max = 1350000;
static const int int_volt_max = 1250000;

static struct s5pv210_dvs_conf dvs_conf[] = {
	[L0] = {
		.arm_volt	= 1250000,
		.int_volt	= 1100000,
	},
	[L1] = {
		.arm_volt	= 1200000,
		.int_volt	= 1100000,
	},
	[L2] = {
		.arm_volt	= 1050000,
		.int_volt	= 1100000,
	},
	[L3] = {
		.arm_volt	= 950000,
		.int_volt	= 1100000,
	},
	[L4] = {
		.arm_volt	= 950000,
		.int_volt	= 1000000,
	},
};

static u32 clkdiv_val[5][11] = {
	/*
	 * Clock divider value for following
	 * { APLL, A2M, HCLK_MSYS, PCLK_MSYS,
	 *   HCLK_DSYS, PCLK_DSYS, HCLK_PSYS, PCLK_PSYS,
	 *   ONEDRAM, MFC, G3D }
	 */

	/* L0 : [1000/200/100][166/83][133/66][200/200] */
	{0, 4, 4, 1, 3, 1, 4, 1, 3, 0, 0},

	/* L1 : [800/200/100][166/83][133/66][200/200] */
	{0, 3, 3, 1, 3, 1, 4, 1, 3, 0, 0},

	/* L2 : [400/200/100][166/83][133/66][200/200] */
	{1, 3, 1, 1, 3, 1, 4, 1, 3, 0, 0},

	/* L3 : [200/200/100][166/83][133/66][200/200] */
	{3, 3, 1, 1, 3, 1, 4, 1, 3, 0, 0},

	/* L4 : [100/100/100][83/83][66/66][100/100] */
	{7, 7, 0, 0, 7, 0, 9, 0, 7, 0, 0},
};

/*
 * This function set DRAM refresh counter
 * accoriding to operating frequency of DRAM
 * ch: DMC port number 0 or 1
 * freq: Operating frequency of DRAM(KHz)
 */
static void s5pv210_set_refresh(enum s5pv210_dmc_port ch, unsigned long freq)
{
	unsigned long tmp, tmp1;
	void __iomem *reg = NULL;

	if (ch == DMC0) {
		reg = (S5P_VA_DMC0 + 0x30);
	} else if (ch == DMC1) {
		reg = (S5P_VA_DMC1 + 0x30);
	} else {
		printk(KERN_ERR "Cannot find DMC port\n");
		return;
	}

	/* Find current DRAM frequency */
	tmp = s5pv210_dram_conf[ch].freq;

	do_div(tmp, freq);

	tmp1 = s5pv210_dram_conf[ch].refresh;

	do_div(tmp1, tmp);

	__raw_writel(tmp1, reg);
}

static int s5pv210_verify_speed(struct cpufreq_policy *policy)
{
	if (policy->cpu)
		return -EINVAL;

	return cpufreq_frequency_table_verify(policy, s5pv210_freq_table);
}

static unsigned int s5pv210_getspeed(unsigned int cpu)
{
	if (cpu)
		return 0;

	return clk_get_rate(cpu_clk) / 1000;
}

static int s5pv210_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	unsigned long reg;
	unsigned int index, priv_index;
	unsigned int pll_changing = 0;
	unsigned int bus_speed_changing = 0;
	int arm_volt, int_volt;
	int ret = 0;

	mutex_lock(&set_freq_lock);

	if (relation & ENABLE_FURTHER_CPUFREQ)
		no_cpufreq_access = false;

	if (no_cpufreq_access) {
#ifdef CONFIG_PM_VERBOSE
		pr_err("%s:%d denied access to %s as it is disabled"
				"temporarily\n", __FILE__, __LINE__, __func__);
#endif
		ret = -EINVAL;
		goto exit;
	}

	if (relation & DISABLE_FURTHER_CPUFREQ)
		no_cpufreq_access = true;

	relation &= ~(ENABLE_FURTHER_CPUFREQ | DISABLE_FURTHER_CPUFREQ);

	freqs.old = s5pv210_getspeed(0);

	if (cpufreq_frequency_table_target(policy, s5pv210_freq_table,
					   target_freq, relation, &index)) {
		ret = -EINVAL;
		goto exit;
	}

	freqs.new = s5pv210_freq_table[index].frequency;

	if (freqs.new == freqs.old)
		goto exit;

	/* Finding current running level index */
	if (cpufreq_frequency_table_target(policy, s5pv210_freq_table,
					   freqs.old, relation, &priv_index)) {
		ret = -EINVAL;
		goto exit;
	}

	arm_volt = dvs_conf[index].arm_volt;
	int_volt = dvs_conf[index].int_volt;

	if (freqs.new > freqs.old) {
		ret = regulator_set_voltage(arm_regulator,
				arm_volt, arm_volt_max);
		if (ret)
			goto exit;

		ret = regulator_set_voltage(int_regulator,
				int_volt, int_volt_max);
		if (ret)
			goto exit;
	}

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	/* Check if there need to change PLL */
	if ((index == L0) || (priv_index == L0))
		pll_changing = 1;

	/* Check if there need to change System bus clock */
	if ((index == L4) || (priv_index == L4))
		bus_speed_changing = 1;

	if (bus_speed_changing) {
		/*
		 * Reconfigure DRAM refresh counter value for minimum
		 * temporary clock while changing divider.
		 * expected clock is 83Mhz : 7.8usec/(1/83Mhz) = 0x287
		 */
		if (pll_changing)
			s5pv210_set_refresh(DMC1, 83000);
		else
			s5pv210_set_refresh(DMC1, 100000);

		s5pv210_set_refresh(DMC0, 83000);
	}

	/*
	 * APLL should be changed in this level
	 * APLL -> MPLL(for stable transition) -> APLL
	 * Some clock source's clock API are not prepared.
	 * Do not use clock API in below code.
	 */
	if (pll_changing) {
		/*
		 * 1. Temporary Change divider for MFC and G3D
		 * SCLKA2M(200/1=200)->(200/4=50)Mhz
		 */
		reg = __raw_readl(S5P_CLK_DIV2);
		reg &= ~(S5P_CLKDIV2_G3D_MASK | S5P_CLKDIV2_MFC_MASK);
		reg |= (3 << S5P_CLKDIV2_G3D_SHIFT) |
			(3 << S5P_CLKDIV2_MFC_SHIFT);
		__raw_writel(reg, S5P_CLK_DIV2);

		/* For MFC, G3D dividing */
		do {
			reg = __raw_readl(S5P_CLKDIV_STAT0);
		} while (reg & ((1 << 16) | (1 << 17)));

		/*
		 * 2. Change SCLKA2M(200Mhz)to SCLKMPLL in MFC_MUX, G3D MUX
		 * (200/4=50)->(667/4=166)Mhz
		 */
		reg = __raw_readl(S5P_CLK_SRC2);
		reg &= ~(S5P_CLKSRC2_G3D_MASK | S5P_CLKSRC2_MFC_MASK);
		reg |= (1 << S5P_CLKSRC2_G3D_SHIFT) |
			(1 << S5P_CLKSRC2_MFC_SHIFT);
		__raw_writel(reg, S5P_CLK_SRC2);

		do {
			reg = __raw_readl(S5P_CLKMUX_STAT1);
		} while (reg & ((1 << 7) | (1 << 3)));

		/*
		 * 3. DMC1 refresh count for 133Mhz if (index == L4) is
		 * true refresh counter is already programed in upper
		 * code. 0x287@83Mhz
		 */
		if (!bus_speed_changing)
			s5pv210_set_refresh(DMC1, 133000);

		/* 4. SCLKAPLL -> SCLKMPLL */
		reg = __raw_readl(S5P_CLK_SRC0);
		reg &= ~(S5P_CLKSRC0_MUX200_MASK);
		reg |= (0x1 << S5P_CLKSRC0_MUX200_SHIFT);
		__raw_writel(reg, S5P_CLK_SRC0);

		do {
			reg = __raw_readl(S5P_CLKMUX_STAT0);
		} while (reg & (0x1 << 18));

	}

	/* Change divider */
	reg = __raw_readl(S5P_CLK_DIV0);

	reg &= ~(S5P_CLKDIV0_APLL_MASK | S5P_CLKDIV0_A2M_MASK |
		S5P_CLKDIV0_HCLK200_MASK | S5P_CLKDIV0_PCLK100_MASK |
		S5P_CLKDIV0_HCLK166_MASK | S5P_CLKDIV0_PCLK83_MASK |
		S5P_CLKDIV0_HCLK133_MASK | S5P_CLKDIV0_PCLK66_MASK);

	reg |= ((clkdiv_val[index][0] << S5P_CLKDIV0_APLL_SHIFT) |
		(clkdiv_val[index][1] << S5P_CLKDIV0_A2M_SHIFT) |
		(clkdiv_val[index][2] << S5P_CLKDIV0_HCLK200_SHIFT) |
		(clkdiv_val[index][3] << S5P_CLKDIV0_PCLK100_SHIFT) |
		(clkdiv_val[index][4] << S5P_CLKDIV0_HCLK166_SHIFT) |
		(clkdiv_val[index][5] << S5P_CLKDIV0_PCLK83_SHIFT) |
		(clkdiv_val[index][6] << S5P_CLKDIV0_HCLK133_SHIFT) |
		(clkdiv_val[index][7] << S5P_CLKDIV0_PCLK66_SHIFT));

	__raw_writel(reg, S5P_CLK_DIV0);

	do {
		reg = __raw_readl(S5P_CLKDIV_STAT0);
	} while (reg & 0xff);

	/* ARM MCS value changed */
	reg = __raw_readl(S5P_ARM_MCS_CON);
	reg &= ~0x3;
	if (index >= L3)
		reg |= 0x3;
	else
		reg |= 0x1;

	__raw_writel(reg, S5P_ARM_MCS_CON);

	if (pll_changing) {
		/* 5. Set Lock time = 30us*24Mhz = 0x2cf */
		__raw_writel(0x2cf, S5P_APLL_LOCK);

		/*
		 * 6. Turn on APLL
		 * 6-1. Set PMS values
		 * 6-2. Wait untile the PLL is locked
		 */
		if (index == L0)
			__raw_writel(APLL_VAL_1000, S5P_APLL_CON);
		else
			__raw_writel(APLL_VAL_800, S5P_APLL_CON);

		do {
			reg = __raw_readl(S5P_APLL_CON);
		} while (!(reg & (0x1 << 29)));

		/*
		 * 7. Change souce clock from SCLKMPLL(667Mhz)
		 * to SCLKA2M(200Mhz) in MFC_MUX and G3D MUX
		 * (667/4=166)->(200/4=50)Mhz
		 */
		reg = __raw_readl(S5P_CLK_SRC2);
		reg &= ~(S5P_CLKSRC2_G3D_MASK | S5P_CLKSRC2_MFC_MASK);
		reg |= (0 << S5P_CLKSRC2_G3D_SHIFT) |
			(0 << S5P_CLKSRC2_MFC_SHIFT);
		__raw_writel(reg, S5P_CLK_SRC2);

		do {
			reg = __raw_readl(S5P_CLKMUX_STAT1);
		} while (reg & ((1 << 7) | (1 << 3)));

		/*
		 * 8. Change divider for MFC and G3D
		 * (200/4=50)->(200/1=200)Mhz
		 */
		reg = __raw_readl(S5P_CLK_DIV2);
		reg &= ~(S5P_CLKDIV2_G3D_MASK | S5P_CLKDIV2_MFC_MASK);
		reg |= (clkdiv_val[index][10] << S5P_CLKDIV2_G3D_SHIFT) |
			(clkdiv_val[index][9] << S5P_CLKDIV2_MFC_SHIFT);
		__raw_writel(reg, S5P_CLK_DIV2);

		/* For MFC, G3D dividing */
		do {
			reg = __raw_readl(S5P_CLKDIV_STAT0);
		} while (reg & ((1 << 16) | (1 << 17)));

		/* 9. Change MPLL to APLL in MSYS_MUX */
		reg = __raw_readl(S5P_CLK_SRC0);
		reg &= ~(S5P_CLKSRC0_MUX200_MASK);
		reg |= (0x0 << S5P_CLKSRC0_MUX200_SHIFT);
		__raw_writel(reg, S5P_CLK_SRC0);

		do {
			reg = __raw_readl(S5P_CLKMUX_STAT0);
		} while (reg & (0x1 << 18));

		/*
		 * 10. DMC1 refresh counter
		 * L4 : DMC1 = 100Mhz 7.8us/(1/100) = 0x30c
		 * Others : DMC1 = 200Mhz 7.8us/(1/200) = 0x618
		 */
		if (!bus_speed_changing)
			s5pv210_set_refresh(DMC1, 200000);
	}

	/*
	 * L4 level need to change memory bus speed, hence onedram clock divier
	 * and memory refresh parameter should be changed
	 */
	if (bus_speed_changing) {
		reg = __raw_readl(S5P_CLK_DIV6);
		reg &= ~S5P_CLKDIV6_ONEDRAM_MASK;
		reg |= (clkdiv_val[index][8] << S5P_CLKDIV6_ONEDRAM_SHIFT);
		__raw_writel(reg, S5P_CLK_DIV6);

		do {
			reg = __raw_readl(S5P_CLKDIV_STAT1);
		} while (reg & (1 << 15));

		/* Reconfigure DRAM refresh counter value */
		if (index != L4) {
			/*
			 * DMC0 : 166Mhz
			 * DMC1 : 200Mhz
			 */
			s5pv210_set_refresh(DMC0, 166000);
			s5pv210_set_refresh(DMC1, 200000);
		} else {
			/*
			 * DMC0 : 83Mhz
			 * DMC1 : 100Mhz
			 */
			s5pv210_set_refresh(DMC0, 83000);
			s5pv210_set_refresh(DMC1, 100000);
		}
	}

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	if (freqs.new < freqs.old) {
		regulator_set_voltage(int_regulator,
				int_volt, int_volt_max);

		regulator_set_voltage(arm_regulator,
				arm_volt, arm_volt_max);
	}

	printk(KERN_DEBUG "Perf changed[L%d]\n", index);

exit:
	mutex_unlock(&set_freq_lock);
	return ret;
}

#ifdef CONFIG_PM
static int s5pv210_cpufreq_suspend(struct cpufreq_policy *policy)
{
	return 0;
}

static int s5pv210_cpufreq_resume(struct cpufreq_policy *policy)
{
	return 0;
}
#endif

static int check_mem_type(void __iomem *dmc_reg)
{
	unsigned long val;

	val = __raw_readl(dmc_reg + 0x4);
	val = (val & (0xf << 8));

	return val >> 8;
}

static int __init s5pv210_cpu_init(struct cpufreq_policy *policy)
{
	unsigned long mem_type;
	int ret;

	cpu_clk = clk_get(NULL, "armclk");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

	dmc0_clk = clk_get(NULL, "sclk_dmc0");
	if (IS_ERR(dmc0_clk)) {
		ret = PTR_ERR(dmc0_clk);
		goto out_dmc0;
	}

	dmc1_clk = clk_get(NULL, "hclk_msys");
	if (IS_ERR(dmc1_clk)) {
		ret = PTR_ERR(dmc1_clk);
		goto out_dmc1;
	}

	if (policy->cpu != 0) {
		ret = -EINVAL;
		goto out_dmc1;
	}

	/*
	 * check_mem_type : This driver only support LPDDR & LPDDR2.
	 * other memory type is not supported.
	 */
	mem_type = check_mem_type(S5P_VA_DMC0);

	if ((mem_type != LPDDR) && (mem_type != LPDDR2)) {
		printk(KERN_ERR "CPUFreq doesn't support this memory type\n");
		ret = -EINVAL;
		goto out_dmc1;
	}

	/* Find current refresh counter and frequency each DMC */
	s5pv210_dram_conf[0].refresh = (__raw_readl(S5P_VA_DMC0 + 0x30) * 1000);
	s5pv210_dram_conf[0].freq = clk_get_rate(dmc0_clk);

	s5pv210_dram_conf[1].refresh = (__raw_readl(S5P_VA_DMC1 + 0x30) * 1000);
	s5pv210_dram_conf[1].freq = clk_get_rate(dmc1_clk);

	policy->cur = policy->min = policy->max = s5pv210_getspeed(0);

	cpufreq_frequency_table_get_attr(s5pv210_freq_table, policy->cpu);

	policy->cpuinfo.transition_latency = 40000;

	return cpufreq_frequency_table_cpuinfo(policy, s5pv210_freq_table);

out_dmc1:
	clk_put(dmc0_clk);
out_dmc0:
	clk_put(cpu_clk);
	return ret;
}

static int s5pv210_cpufreq_notifier_event(struct notifier_block *this,
					  unsigned long event, void *ptr)
{
	int ret;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		ret = cpufreq_driver_target(cpufreq_cpu_get(0), SLEEP_FREQ,
					    DISABLE_FURTHER_CPUFREQ);
		if (ret < 0)
			return NOTIFY_BAD;

		return NOTIFY_OK;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		cpufreq_driver_target(cpufreq_cpu_get(0), SLEEP_FREQ,
				      ENABLE_FURTHER_CPUFREQ);

		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static int s5pv210_cpufreq_reboot_notifier_event(struct notifier_block *this,
						 unsigned long event, void *ptr)
{
	int ret;

	ret = cpufreq_driver_target(cpufreq_cpu_get(0), SLEEP_FREQ,
				    DISABLE_FURTHER_CPUFREQ);
	if (ret < 0)
		return NOTIFY_BAD;

	return NOTIFY_DONE;
}

static struct cpufreq_driver s5pv210_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= s5pv210_verify_speed,
	.target		= s5pv210_target,
	.get		= s5pv210_getspeed,
	.init		= s5pv210_cpu_init,
	.name		= "s5pv210",
#ifdef CONFIG_PM
	.suspend	= s5pv210_cpufreq_suspend,
	.resume		= s5pv210_cpufreq_resume,
#endif
};

static struct notifier_block s5pv210_cpufreq_notifier = {
	.notifier_call = s5pv210_cpufreq_notifier_event,
};

static struct notifier_block s5pv210_cpufreq_reboot_notifier = {
	.notifier_call = s5pv210_cpufreq_reboot_notifier_event,
};

static int __init s5pv210_cpufreq_init(void)
{
	arm_regulator = regulator_get(NULL, "vddarm");
	if (IS_ERR(arm_regulator)) {
		pr_err("failed to get regulator vddarm");
		return PTR_ERR(arm_regulator);
	}

	int_regulator = regulator_get(NULL, "vddint");
	if (IS_ERR(int_regulator)) {
		pr_err("failed to get regulator vddint");
		regulator_put(arm_regulator);
		return PTR_ERR(int_regulator);
	}

	register_pm_notifier(&s5pv210_cpufreq_notifier);
	register_reboot_notifier(&s5pv210_cpufreq_reboot_notifier);

	return cpufreq_register_driver(&s5pv210_driver);
}

late_initcall(s5pv210_cpufreq_init);
