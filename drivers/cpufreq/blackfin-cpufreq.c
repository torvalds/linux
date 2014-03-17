/*
 * Blackfin core clock scaling
 *
 * Copyright 2008-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <asm/blackfin.h>
#include <asm/time.h>
#include <asm/dpmc.h>


/* this is the table of CCLK frequencies, in Hz */
/* .driver_data is the entry in the auxiliary dpm_state_table[] */
static struct cpufreq_frequency_table bfin_freq_table[] = {
	{
		.frequency = CPUFREQ_TABLE_END,
		.driver_data = 0,
	},
	{
		.frequency = CPUFREQ_TABLE_END,
		.driver_data = 1,
	},
	{
		.frequency = CPUFREQ_TABLE_END,
		.driver_data = 2,
	},
	{
		.frequency = CPUFREQ_TABLE_END,
		.driver_data = 0,
	},
};

static struct bfin_dpm_state {
	unsigned int csel; /* system clock divider */
	unsigned int tscale; /* change the divider on the core timer interrupt */
} dpm_state_table[3];

#if defined(CONFIG_CYCLES_CLOCKSOURCE)
/*
 * normalized to maximum frequency offset for CYCLES,
 * used in time-ts cycles clock source, but could be used
 * somewhere also.
 */
unsigned long long __bfin_cycles_off;
unsigned int __bfin_cycles_mod;
#endif

/**************************************************************************/
static void __init bfin_init_tables(unsigned long cclk, unsigned long sclk)
{

	unsigned long csel, min_cclk;
	int index;

	/* Anomaly 273 seems to still exist on non-BF54x w/dcache turned on */
#if ANOMALY_05000273 || ANOMALY_05000274 || \
	(!(defined(CONFIG_BF54x) || defined(CONFIG_BF60x)) \
	&& defined(CONFIG_BFIN_EXTMEM_DCACHEABLE))
	min_cclk = sclk * 2;
#else
	min_cclk = sclk;
#endif

#ifndef CONFIG_BF60x
	csel = ((bfin_read_PLL_DIV() & CSEL) >> 4);
#else
	csel = bfin_read32(CGU0_DIV) & 0x1F;
#endif

	for (index = 0;  (cclk >> index) >= min_cclk && csel <= 3 && index < 3; index++, csel++) {
		bfin_freq_table[index].frequency = cclk >> index;
#ifndef CONFIG_BF60x
		dpm_state_table[index].csel = csel << 4; /* Shift now into PLL_DIV bitpos */
#else
		dpm_state_table[index].csel = csel;
#endif
		dpm_state_table[index].tscale =  (TIME_SCALE >> index) - 1;

		pr_debug("cpufreq: freq:%d csel:0x%x tscale:%d\n",
						 bfin_freq_table[index].frequency,
						 dpm_state_table[index].csel,
						 dpm_state_table[index].tscale);
	}
	return;
}

static void bfin_adjust_core_timer(void *info)
{
	unsigned int tscale;
	unsigned int index = *(unsigned int *)info;

	/* we have to adjust the core timer, because it is using cclk */
	tscale = dpm_state_table[index].tscale;
	bfin_write_TSCALE(tscale);
	return;
}

static unsigned int bfin_getfreq_khz(unsigned int cpu)
{
	/* Both CoreA/B have the same core clock */
	return get_cclk() / 1000;
}

#ifdef CONFIG_BF60x
unsigned long cpu_set_cclk(int cpu, unsigned long new)
{
	struct clk *clk;
	int ret;

	clk = clk_get(NULL, "CCLK");
	if (IS_ERR(clk))
		return -ENODEV;

	ret = clk_set_rate(clk, new);
	clk_put(clk);
	return ret;
}
#endif

static int bfin_target(struct cpufreq_policy *policy, unsigned int index)
{
#ifndef CONFIG_BF60x
	unsigned int plldiv;
#endif
	static unsigned long lpj_ref;
	static unsigned int  lpj_ref_freq;
	unsigned int old_freq, new_freq;
	int ret = 0;

#if defined(CONFIG_CYCLES_CLOCKSOURCE)
	cycles_t cycles;
#endif

	old_freq = bfin_getfreq_khz(0);
	new_freq = bfin_freq_table[index].frequency;

#ifndef CONFIG_BF60x
	plldiv = (bfin_read_PLL_DIV() & SSEL) | dpm_state_table[index].csel;
	bfin_write_PLL_DIV(plldiv);
#else
	ret = cpu_set_cclk(policy->cpu, new_freq * 1000);
	if (ret != 0) {
		WARN_ONCE(ret, "cpufreq set freq failed %d\n", ret);
		return ret;
	}
#endif
	on_each_cpu(bfin_adjust_core_timer, &index, 1);
#if defined(CONFIG_CYCLES_CLOCKSOURCE)
	cycles = get_cycles();
	SSYNC();
	cycles += 10; /* ~10 cycles we lose after get_cycles() */
	__bfin_cycles_off += (cycles << __bfin_cycles_mod) - (cycles << index);
	__bfin_cycles_mod = index;
#endif
	if (!lpj_ref_freq) {
		lpj_ref = loops_per_jiffy;
		lpj_ref_freq = old_freq;
	}
	if (new_freq != old_freq) {
		loops_per_jiffy = cpufreq_scale(lpj_ref,
				lpj_ref_freq, new_freq);
	}

	return ret;
}

static int __bfin_cpu_init(struct cpufreq_policy *policy)
{

	unsigned long cclk, sclk;

	cclk = get_cclk() / 1000;
	sclk = get_sclk() / 1000;

	if (policy->cpu == CPUFREQ_CPU)
		bfin_init_tables(cclk, sclk);

	policy->cpuinfo.transition_latency = 50000; /* 50us assumed */

	return cpufreq_table_validate_and_show(policy, bfin_freq_table);
}

static struct cpufreq_driver bfin_driver = {
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = bfin_target,
	.get = bfin_getfreq_khz,
	.init = __bfin_cpu_init,
	.name = "bfin cpufreq",
	.attr = cpufreq_generic_attr,
};

static int __init bfin_cpu_init(void)
{
	return cpufreq_register_driver(&bfin_driver);
}

static void __exit bfin_cpu_exit(void)
{
	cpufreq_unregister_driver(&bfin_driver);
}

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("cpufreq driver for Blackfin");
MODULE_LICENSE("GPL");

module_init(bfin_cpu_init);
module_exit(bfin_cpu_exit);
