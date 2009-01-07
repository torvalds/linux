/*
 * File:	 arch/blackfin/mach-common/cpufreq.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:	 Blackfin core clock scaling
 *
 * Modified:
 *		 Copyright 2004-2008 Analog Devices Inc.
 *
 * Bugs:	 Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA	02110-1301	USA
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/fs.h>
#include <asm/blackfin.h>
#include <asm/time.h>


/* this is the table of CCLK frequencies, in Hz */
/* .index is the entry in the auxillary dpm_state_table[] */
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

/*
   normalized to maximum frequncy offset for CYCLES,
   used in time-ts cycles clock source, but could be used
   somewhere also.
 */
unsigned long long __bfin_cycles_off;
unsigned int __bfin_cycles_mod;

/**************************************************************************/

static unsigned int bfin_getfreq_khz(unsigned int cpu)
{
	/* The driver only support single cpu */
	if (cpu != 0)
		return -1;

	return get_cclk() / 1000;
}


static int bfin_target(struct cpufreq_policy *policy,
			unsigned int target_freq, unsigned int relation)
{
	unsigned int index, plldiv, tscale;
	unsigned long flags, cclk_hz;
	struct cpufreq_freqs freqs;
	cycles_t cycles;

	if (cpufreq_frequency_table_target(policy, bfin_freq_table,
		 target_freq, relation, &index))
		return -EINVAL;

	cclk_hz = bfin_freq_table[index].frequency;

	freqs.old = bfin_getfreq_khz(0);
	freqs.new = cclk_hz;
	freqs.cpu = 0;

	pr_debug("cpufreq: changing cclk to %lu; target = %u, oldfreq = %u\n",
		 cclk_hz, target_freq, freqs.old);

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	local_irq_save_hw(flags);
		plldiv = (bfin_read_PLL_DIV() & SSEL) | dpm_state_table[index].csel;
		tscale = dpm_state_table[index].tscale;
		bfin_write_PLL_DIV(plldiv);
		/* we have to adjust the core timer, because it is using cclk */
		bfin_write_TSCALE(tscale);
		cycles = get_cycles();
		SSYNC();
	cycles += 10; /* ~10 cycles we lose after get_cycles() */
	__bfin_cycles_off += (cycles << __bfin_cycles_mod) - (cycles << index);
	__bfin_cycles_mod = index;
	local_irq_restore_hw(flags);
	/* TODO: just test case for cycles clock source, remove later */
	pr_debug("cpufreq: done\n");
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static int bfin_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, bfin_freq_table);
}

static int __init __bfin_cpu_init(struct cpufreq_policy *policy)
{

	unsigned long cclk, sclk, csel, min_cclk;
	int index;

	if (policy->cpu != 0)
		return -EINVAL;

	cclk = get_cclk() / 1000;
	sclk = get_sclk() / 1000;

#if ANOMALY_05000273 || (!defined(CONFIG_BF54x) && defined(CONFIG_BFIN_DCACHE))
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

	policy->cpuinfo.transition_latency = (bfin_read_PLL_LOCKCNT() / (sclk / 1000000)) * 1000;
	/*Now ,only support one cpu */
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
