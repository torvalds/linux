// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pervasive backend for the cbe_cpufreq driver
 *
 * This driver makes use of the pervasive unit to
 * engage the desired frequency.
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005-2007
 *
 * Author: Christian Krafft <krafft@de.ibm.com>
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <asm/machdep.h>
#include <asm/hw_irq.h>
#include <asm/cell-regs.h>

#include "ppc_cbe_cpufreq.h"

/* to write to MIC register */
static u64 MIC_Slow_Fast_Timer_table[] = {
	[0 ... 7] = 0x007fc00000000000ull,
};

/* more values for the MIC */
static u64 MIC_Slow_Next_Timer_table[] = {
	0x0000240000000000ull,
	0x0000268000000000ull,
	0x000029C000000000ull,
	0x00002D0000000000ull,
	0x0000300000000000ull,
	0x0000334000000000ull,
	0x000039C000000000ull,
	0x00003FC000000000ull,
};


int cbe_cpufreq_set_pmode(int cpu, unsigned int pmode)
{
	struct cbe_pmd_regs __iomem *pmd_regs;
	struct cbe_mic_tm_regs __iomem *mic_tm_regs;
	unsigned long flags;
	u64 value;
#ifdef DEBUG
	long time;
#endif

	local_irq_save(flags);

	mic_tm_regs = cbe_get_cpu_mic_tm_regs(cpu);
	pmd_regs = cbe_get_cpu_pmd_regs(cpu);

#ifdef DEBUG
	time = jiffies;
#endif

	out_be64(&mic_tm_regs->slow_fast_timer_0, MIC_Slow_Fast_Timer_table[pmode]);
	out_be64(&mic_tm_regs->slow_fast_timer_1, MIC_Slow_Fast_Timer_table[pmode]);

	out_be64(&mic_tm_regs->slow_next_timer_0, MIC_Slow_Next_Timer_table[pmode]);
	out_be64(&mic_tm_regs->slow_next_timer_1, MIC_Slow_Next_Timer_table[pmode]);

	value = in_be64(&pmd_regs->pmcr);
	/* set bits to zero */
	value &= 0xFFFFFFFFFFFFFFF8ull;
	/* set bits to next pmode */
	value |= pmode;

	out_be64(&pmd_regs->pmcr, value);

#ifdef DEBUG
	/* wait until new pmode appears in status register */
	value = in_be64(&pmd_regs->pmsr) & 0x07;
	while (value != pmode) {
		cpu_relax();
		value = in_be64(&pmd_regs->pmsr) & 0x07;
	}

	time = jiffies  - time;
	time = jiffies_to_msecs(time);
	pr_debug("had to wait %lu ms for a transition using " \
		 "pervasive unit\n", time);
#endif
	local_irq_restore(flags);

	return 0;
}


int cbe_cpufreq_get_pmode(int cpu)
{
	int ret;
	struct cbe_pmd_regs __iomem *pmd_regs;

	pmd_regs = cbe_get_cpu_pmd_regs(cpu);
	ret = in_be64(&pmd_regs->pmsr) & 0x07;

	return ret;
}

