/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_STACK_H
#define _ASM_TILE_STACK_H

#include <linux/types.h>
#include <linux/sched.h>
#include <asm/backtrace.h>
#include <hv/hypervisor.h>

/* Everything we need to keep track of a backtrace iteration */
struct KBacktraceIterator {
	BacktraceIterator it;
	struct task_struct *task;     /* task we are backtracing */
	HV_PTE *pgtable;	      /* page table for user space access */
	int end;		      /* iteration complete. */
	int new_context;              /* new context is starting */
	int profile;                  /* profiling, so stop on async intrpt */
	int verbose;		      /* printk extra info (don't want to
				       * do this for profiling) */
	int is_current;               /* backtracing current task */
};

/* Iteration methods for kernel backtraces */

/*
 * Initialize a KBacktraceIterator from a task_struct, and optionally from
 * a set of registers.  If the registers are omitted, the process is
 * assumed to be descheduled, and registers are read from the process's
 * thread_struct and stack.  "verbose" means to printk some additional
 * information about fault handlers as we pass them on the stack.
 */
extern void KBacktraceIterator_init(struct KBacktraceIterator *kbt,
				    struct task_struct *, struct pt_regs *);

/* Initialize iterator based on current stack. */
extern void KBacktraceIterator_init_current(struct KBacktraceIterator *kbt);

/* Helper method for above. */
extern void _KBacktraceIterator_init_current(struct KBacktraceIterator *kbt,
				ulong pc, ulong lr, ulong sp, ulong r52);

/* No more frames? */
extern int KBacktraceIterator_end(struct KBacktraceIterator *kbt);

/* Advance to the next frame. */
extern void KBacktraceIterator_next(struct KBacktraceIterator *kbt);

/*
 * Dump stack given complete register info. Use only from the
 * architecture-specific code; show_stack()
 * and dump_stack() (in entry.S) are architecture-independent entry points.
 */
extern void tile_show_stack(struct KBacktraceIterator *, int headers);

/* Dump stack of current process, with registers to seed the backtrace. */
extern void dump_stack_regs(struct pt_regs *);

/* Helper method for assembly dump_stack(). */
extern void _dump_stack(int dummy, ulong pc, ulong lr, ulong sp, ulong r52);

#endif /* _ASM_TILE_STACK_H */
