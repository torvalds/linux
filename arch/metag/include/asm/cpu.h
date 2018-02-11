/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_METAG_CPU_H
#define _ASM_METAG_CPU_H

#include <linux/percpu.h>

struct cpuinfo_metag {
	struct cpu cpu;
#ifdef CONFIG_SMP
	unsigned long loops_per_jiffy;
#endif
};

DECLARE_PER_CPU(struct cpuinfo_metag, cpu_data);
#endif /* _ASM_METAG_CPU_H */
