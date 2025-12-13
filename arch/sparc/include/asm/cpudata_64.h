/* SPDX-License-Identifier: GPL-2.0 */
/* cpudata.h: Per-cpu parameters.
 *
 * Copyright (C) 2003, 2005, 2006 David S. Miller (davem@davemloft.net)
 */

#ifndef _SPARC64_CPUDATA_H
#define _SPARC64_CPUDATA_H

#ifndef __ASSEMBLER__

typedef struct {
	/* Dcache line 1 */
	unsigned int	__softirq_pending; /* must be 1st, see rtrap.S */
	unsigned int	__nmi_count;
	unsigned long	clock_tick;	/* %tick's per second */
	unsigned long	__pad;
	unsigned int	irq0_irqs;
	unsigned int	__pad2;

	/* Dcache line 2, rarely used */
	unsigned int	dcache_size;
	unsigned int	dcache_line_size;
	unsigned int	icache_size;
	unsigned int	icache_line_size;
	unsigned int	ecache_size;
	unsigned int	ecache_line_size;
	unsigned short	sock_id;	/* physical package */
	unsigned short	core_id;
	unsigned short  max_cache_id;	/* groupings of highest shared cache */
	signed short	proc_id;	/* strand (aka HW thread) id */
} cpuinfo_sparc;

DECLARE_PER_CPU(cpuinfo_sparc, __cpu_data);
#define cpu_data(__cpu)		per_cpu(__cpu_data, (__cpu))
#define local_cpu_data()	(*this_cpu_ptr(&__cpu_data))

#endif /* !(__ASSEMBLER__) */

#include <asm/trap_block.h>

#endif /* _SPARC64_CPUDATA_H */
