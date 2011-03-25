/*
 * Blackfin core clock scaling
 *
 * Copyright 2008-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <asm/blackfin.h>
#include <asm/time.h>
#include <asm/dpmc.h>

/* this is the table of CCLK frequencies, in Hz */
/* .index is the entry in the auxiliary dpm_state_table[] */
static struct cpufreq_frequency_table bfin_freq_table[] = {
	{
		.frequency = CPUFREQ_TABLE_END,
		.index = 0,
	},
	{
		.frequency = CPUFREQ_TABLE_END,
		.index = 1,
	},
	{
		.frequency = CPUFREQ_TABLE_END,
		.index = 2,
	},
	{
		.frequency = CPUFREQ_TABLE_END,
		.index = 0,
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
	(!defined(CONFIG_BF54x) && defined(CONFIG_BFIN_EXTMEM_DCACHEABLE))
	min_cclk = sclk * 2;
#else
	min_cclk = sclk;
#endif
	csel = ((bfin_read_PLL_DIV() & CSEL) >> 4);

	for (index = 0;  (cclk >> index) >= min_cclk && csel <= 3; index++, csel++) {
		bfin_freq_table[index].frequency = cclk >> index;
		dpm_state_table[index].csel = csel << 4; /* Shift now into PLL_DIV bitpos */
		dpm_state_table[index].tscale =  (TIME_SCALE / (1 << csel)) - 1;

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

static int bfin_target(struct cpufreq_policy *poli,
			unsigned int target_freq, unsigned int relation)
{
	unsigned int index, plldiv, cpu;
	unsigned long flags, cclk_hz;
	struct cpufreq_freqs freqs;
	static unsigned long lpj_ref;
	static unsigned int  lpj_ref_freq;

#if defined(CONFIG_CYCLES_CLOCKSOURCE)
	cycles_t cycles;
#endif

	for_each_online_cpu(cpu) {
		struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

		if (!policy)
			continue;

		if (cpufreq_frequency_table_target(policy, bfin_freq_table,
				 target_freq, relation, &index))
			return -EINVAL;

		cclk_hz = bfin_freq_table[index].frequency;

		freqs.old = bfin_getfreq_khz(0);
		freqs.new = cclk_hz;
		freqs.cpu = cpu;

		pr_debug("cpufreq: changing cclk to %lu; target = %u, oldfreq = %u\n",
			 cclk_hz, target_freq, freqs.old);

		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
		if (cpu == CPUFREQ_CPU) {
			flags = hard_local_irq_save();
			plldiv = (bfin_read_PLL_DIV() & SSEL) |
						dpm_state_table[index].csel;
			bfin_write_PLL_DIV(plldiv);
			on_each_cpu(bfin_adjust_core_timer, &index, 1);
#if defined(CONFIG_CYCLES_CLOCKSOURCE)
			cycles = get_cycles();
			SSYNC();
			cycles += 10; /* ~10 cycles we lose after get_cycles() */
			__bfin_cycles_off +=
			    (cycles << __bfin_cycles_mod) - (cycles << index);
			__bfin_cycles_mod = index;
#endif
			if (!lpj_ref_freq) {
				lpj_ref = loops_per_jiffy;
				lpj_ref_freq = freqs.old;
			}
			if (freqs.new != freqs.old) {
				loops_per_jiffy = cpufreq_scale(lpj_ref,
						lpj_ref_freq, freqs.new);
			}
			hard_local_irq_restore(flags);
		}
		/* TODO: just test case for cycles clock source, remove later */
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	}

	pr_debug("cpufreq: done\n");
	return 0;
}

static int bfin_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, bfin_freq_table);
}

static int __init __bfin_cpu_init(struct cpufreq_policy *policy)
{

	unsigned long cclk, sclk;

	cclk = get_cclk() / 1000;
	sclk = get_sclk() / 1000;

	if (policy->cpu == CPUFREQ_CPU)
		bfin_init_tables(cclk, sclk);

	policy->cpuinfo.transition_latency = 50000; /* 50us assumed */

	policy->cur = cclk;
	cpufreq_frequency_table_get_attr(bfin_freq_table, policy->cpu);
	return cpufreq_frequency_table_cpuinfo(policy, bfin_freq_table);
}

static struct freq_attr *bfin_freq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver bfin_driver = {
	.verify = bfin_verify_speed,
	.target = bfin_target,
	.get = bfin_getfreq_khz,
	.init = __bfin_cpu_init,
	.name = "bfin cpufreq",
	.owner = THIS_MODULE,
	.attr = bfin_freq_attr,
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
