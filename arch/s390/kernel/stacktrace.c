/*
 * arch/s390/kernel/stacktrace.c
 *
 * Stack trace management functions
 *
 *  Copyright (C) IBM Corp. 2006
 *  Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/kallsyms.h>

static unsigned long save_context_stack(struct stack_trace *trace,
					unsigned int *skip,
					unsigned long sp,
					unsigned long low,
					unsigned long high)
{
	struct stack_frame *sf;
	struct pt_regs *regs;
	unsigned long addr;

	while(1) {
		sp &= PSW_ADDR_INSN;
		if (sp < low || sp > high)
			return sp;
		sf = (struct stack_frame *)sp;
		while(1) {
			addr = sf->gprs[8] & PSW_ADDR_INSN;
			if (!(*skip))
				trace->entries[trace->nr_entries++] = addr;
			else
				(*skip)--;
			if (trace->nr_entries >= trace->max_entries)
				return sp;
			low = sp;
			sp = sf->back_chain & PSW_ADDR_INSN;
			if (!sp)
				break;
			if (sp <= low || sp > high - sizeof(*sf))
				return sp;
			sf = (struct stack_frame *)sp;
		}
		/* Zero backchain detected, check for interrupt frame. */
		sp = (unsigned long)(sf + 1);
		if (sp <= low || sp > high - sizeof(*regs))
			return sp;
		regs = (struct pt_regs *)sp;
		addr = regs->psw.addr & PSW_ADDR_INSN;
		if (!(*skip))
			trace->entries[trace->nr_entries++] = addr;
		else
			(*skip)--;
		if (trace->nr_entries >= trace->max_entries)
			return sp;
		low = sp;
		sp = regs->gprs[15];
	}
}

void save_stack_trace(struct stack_trace *trace)
{
	register unsigned long sp asm ("15");
	unsigned long orig_sp, new_sp;

	orig_sp = sp & PSW_ADDR_INSN;

	new_sp = save_context_stack(trace, &trace->skip, orig_sp,
				S390_lowcore.panic_stack - PAGE_SIZE,
				S390_lowcore.panic_stack);
	if (new_sp != orig_sp)
		return;
	new_sp = save_context_stack(trace, &trace->skip, new_sp,
				S390_lowcore.async_stack - ASYNC_SIZE,
				S390_lowcore.async_stack);
	if (new_sp != orig_sp)
		return;

	save_context_stack(trace, &trace->skip, new_sp,
			   S390_lowcore.thread_info,
			   S390_lowcore.thread_info + THREAD_SIZE);
	return;
}
