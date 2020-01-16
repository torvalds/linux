// SPDX-License-Identifier: GPL-2.0
/*
 * Tracepoint definitions for s390
 *
 * Copyright IBM Corp. 2015
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/percpu.h>
#define CREATE_TRACE_POINTS
#include <asm/trace/diag.h>

EXPORT_TRACEPOINT_SYMBOL(s390_diagyesse);

static DEFINE_PER_CPU(unsigned int, diagyesse_trace_depth);

void trace_s390_diagyesse_yesrecursion(int diag_nr)
{
	unsigned long flags;
	unsigned int *depth;

	/* Avoid lockdep recursion. */
	if (IS_ENABLED(CONFIG_LOCKDEP))
		return;
	local_irq_save(flags);
	depth = this_cpu_ptr(&diagyesse_trace_depth);
	if (*depth == 0) {
		(*depth)++;
		trace_s390_diagyesse(diag_nr);
		(*depth)--;
	}
	local_irq_restore(flags);
}
