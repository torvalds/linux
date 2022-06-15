// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2008 Marvell International Ltd.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/soc/pxa/cpu.h>
#include <linux/clk/pxa.h>
#include <linux/slab.h>
#include <linux/io.h>

#define HSS_104M	(0)
#define HSS_156M	(1)
#define HSS_208M	(2)
#define HSS_312M	(3)

#define SMCFS_78M	(0)
#define SMCFS_104M	(2)
#define SMCFS_208M	(5)

#define SFLFS_104M	(0)
#define SFLFS_156M	(1)
#define SFLFS_208M	(2)
#define SFLFS_312M	(3)

#define XSPCLK_156M	(0)
#define XSPCLK_NONE	(3)

#define DMCFS_26M	(0)
#define DMCFS_260M	(3)

#define ACCR_XPDIS		(1 << 31)	/* Core PLL Output Disable */
#define ACCR_SPDIS		(1 << 30)	/* System PLL Output Disable */
#define ACCR_D0CS		(1 << 26)	/* D0 Mode Clock Select */
#define ACCR_PCCE		(1 << 11)	/* Power Mode Change Clock Enable */
#define ACCR_DDR_D0CS		(1 << 7)	/* DDR SDRAM clock frequency in D0CS (PXA31x only) */

#define ACCR_SMCFS_MASK		(0x7 << 23)	/* Static Memory Controller Frequency Select */
#define ACCR_SFLFS_MASK		(0x3 << 18)	/* Frequency Select for Internal Memory Controller */
#define ACCR_XSPCLK_MASK	(0x3 << 16)	/* Core Frequency during Frequency Change */
#define ACCR_HSS_MASK		(0x3 << 14)	/* System Bus-Clock Frequency Select */
#define ACCR_DMCFS_MASK		(0x3 << 12)	/* Dynamic Memory Controller Clock Frequency Select */
#define ACCR_XN_MASK		(0x7 << 8)	/* Core PLL Turbo-Mode-to-Run-Mode Ratio */
#define ACCR_XL_MASK		(0x1f)		/* Core PLL Run-Mode-to-Oscillator Ratio */

#define ACCR_SMCFS(x)		(((x) & 0x7) << 23)
#define ACCR_SFLFS(x)		(((x) & 0x3) << 18)
#define ACCR_XSPCLK(x)		(((x) & 0x3) << 16)
#define ACCR_HSS(x)		(((x) & 0x3) << 14)
#define ACCR_DMCFS(x)		(((x) & 0x3) << 12)
#define ACCR_XN(x)		(((x) & 0x7) << 8)
#define ACCR_XL(x)		((x) & 0x1f)

struct pxa3xx_freq_info {
	unsigned int cpufreq_mhz;
	unsigned int core_xl : 5;
	unsigned int core_xn : 3;
	unsigned int hss : 2;
	unsigned int dmcfs : 2;
	unsigned int smcfs : 3;
	unsigned int sflfs : 2;
	unsigned int df_clkdiv : 3;

	int	vcc_core;	/* in mV */
	int	vcc_sram;	/* in mV */
};

#define OP(cpufreq, _xl, _xn, _hss, _dmc, _smc, _sfl, _dfi, vcore, vsram) \
{									\
	.cpufreq_mhz	= cpufreq,					\
	.core_xl	= _xl,						\
	.core_xn	= _xn,						\
	.hss		= HSS_##_hss##M,				\
	.dmcfs		= DMCFS_##_dmc##M,				\
	.smcfs		= SMCFS_##_smc##M,				\
	.sflfs		= SFLFS_##_sfl##M,				\
	.df_clkdiv	= _dfi,						\
	.vcc_core	= vcore,					\
	.vcc_sram	= vsram,					\
}

static struct pxa3xx_freq_info pxa300_freqs[] = {
	/*  CPU XL XN  HSS DMEM SMEM SRAM DFI VCC_CORE VCC_SRAM */
	OP(104,  8, 1, 104, 260,  78, 104, 3, 1000, 1100), /* 104MHz */
	OP(208, 16, 1, 104, 260, 104, 156, 2, 1000, 1100), /* 208MHz */
	OP(416, 16, 2, 156, 260, 104, 208, 2, 1100, 1200), /* 416MHz */
	OP(624, 24, 2, 208, 260, 208, 312, 3, 1375, 1400), /* 624MHz */
};

static struct pxa3xx_freq_info pxa320_freqs[] = {
	/*  CPU XL XN  HSS DMEM SMEM SRAM DFI VCC_CORE VCC_SRAM */
	OP(104,  8, 1, 104, 260,  78, 104, 3, 1000, 1100), /* 104MHz */
	OP(208, 16, 1, 104, 260, 104, 156, 2, 1000, 1100), /* 208MHz */
	OP(416, 16, 2, 156, 260, 104, 208, 2, 1100, 1200), /* 416MHz */
	OP(624, 24, 2, 208, 260, 208, 312, 3, 1375, 1400), /* 624MHz */
	OP(806, 31, 2, 208, 260, 208, 312, 3, 1400, 1400), /* 806MHz */
};

static unsigned int pxa3xx_freqs_num;
static struct pxa3xx_freq_info *pxa3xx_freqs;
static struct cpufreq_frequency_table *pxa3xx_freqs_table;

static int setup_freqs_table(struct cpufreq_policy *policy,
			     struct pxa3xx_freq_info *freqs, int num)
{
	struct cpufreq_frequency_table *table;
	int i;

	table = kcalloc(num + 1, sizeof(*table), GFP_KERNEL);
	if (table == NULL)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		table[i].driver_data = i;
		table[i].frequency = freqs[i].cpufreq_mhz * 1000;
	}
	table[num].driver_data = i;
	table[num].frequency = CPUFREQ_TABLE_END;

	pxa3xx_freqs = freqs;
	pxa3xx_freqs_num = num;
	pxa3xx_freqs_table = table;

	policy->freq_table = table;

	return 0;
}

static void __update_core_freq(struct pxa3xx_freq_info *info)
{
	u32 mask, disable, enable, xclkcfg;

	mask	= ACCR_XN_MASK | ACCR_XL_MASK;
	disable = mask | ACCR_XSPCLK_MASK;
	enable  = ACCR_XN(info->core_xn) | ACCR_XL(info->core_xl);
	/* No clock until core PLL is re-locked */
	enable |= ACCR_XSPCLK(XSPCLK_NONE);
	xclkcfg = (info->core_xn == 2) ? 0x3 : 0x2;	/* turbo bit */

	pxa3xx_clk_update_accr(disable, enable, xclkcfg, mask);
}

static void __update_bus_freq(struct pxa3xx_freq_info *info)
{
	u32 mask, disable, enable;

	mask	= ACCR_SMCFS_MASK | ACCR_SFLFS_MASK | ACCR_HSS_MASK |
		  ACCR_DMCFS_MASK;
	disable = mask;
	enable	= ACCR_SMCFS(info->smcfs) | ACCR_SFLFS(info->sflfs) |
		  ACCR_HSS(info->hss) | ACCR_DMCFS(info->dmcfs);

	pxa3xx_clk_update_accr(disable, enable, 0, mask);
}

static unsigned int pxa3xx_cpufreq_get(unsigned int cpu)
{
	return pxa3xx_get_clk_frequency_khz(0);
}

static int pxa3xx_cpufreq_set(struct cpufreq_policy *policy, unsigned int index)
{
	struct pxa3xx_freq_info *next;
	unsigned long flags;

	if (policy->cpu != 0)
		return -EINVAL;

	next = &pxa3xx_freqs[index];

	local_irq_save(flags);
	__update_core_freq(next);
	__update_bus_freq(next);
	local_irq_restore(flags);

	return 0;
}

static int pxa3xx_cpufreq_init(struct cpufreq_policy *policy)
{
	int ret = -EINVAL;

	/* set default policy and cpuinfo */
	policy->min = policy->cpuinfo.min_freq = 104000;
	policy->max = policy->cpuinfo.max_freq =
		(cpu_is_pxa320()) ? 806000 : 624000;
	policy->cpuinfo.transition_latency = 1000; /* FIXME: 1 ms, assumed */

	if (cpu_is_pxa300() || cpu_is_pxa310())
		ret = setup_freqs_table(policy, pxa300_freqs,
					ARRAY_SIZE(pxa300_freqs));

	if (cpu_is_pxa320())
		ret = setup_freqs_table(policy, pxa320_freqs,
					ARRAY_SIZE(pxa320_freqs));

	if (ret) {
		pr_err("failed to setup frequency table\n");
		return ret;
	}

	pr_info("CPUFREQ support for PXA3xx initialized\n");
	return 0;
}

static struct cpufreq_driver pxa3xx_cpufreq_driver = {
	.flags		= CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= pxa3xx_cpufreq_set,
	.init		= pxa3xx_cpufreq_init,
	.get		= pxa3xx_cpufreq_get,
	.name		= "pxa3xx-cpufreq",
};

static int __init cpufreq_init(void)
{
	if (cpu_is_pxa3xx())
		return cpufreq_register_driver(&pxa3xx_cpufreq_driver);

	return 0;
}
module_init(cpufreq_init);

static void __exit cpufreq_exit(void)
{
	cpufreq_unregister_driver(&pxa3xx_cpufreq_driver);
}
module_exit(cpufreq_exit);

MODULE_DESCRIPTION("CPU frequency scaling driver for PXA3xx");
MODULE_LICENSE("GPL");
