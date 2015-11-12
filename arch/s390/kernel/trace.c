/*
 * Tracepoint definitions for s390
 *
 * Copyright IBM Corp. 2015
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/percpu.h>
#define CREATE_TRACE_POINTS
#include <asm/trace/diag.h>

EXPORT_TRACEPOINT_SYMBOL(diagnose);

static DEFINE_PER_CPU(unsigned int, diagnose_trace_depth);

void trace_diagnose_norecursion(int diag_nr)
{
	unsigned long flags;
	unsigned int *depth;

	local_irq_save(flags);
	depth = this_cpu_ptr(&diagnose_trace_depth);
	if (*depth == 0) {
		(*depth)++;
		trace_diagnose(diag_nr);
		(*depth)--;
	}
	local_irq_restore(flags);
}
