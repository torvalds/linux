// SPDX-License-Identifier: GPL-2.0

/*
 * Stack trace utility functions etc.
 *
 * Copyright 2008 Christoph Hellwig, IBM Corp.
 * Copyright 2018 SUSE Linux GmbH
 * Copyright 2018 Nick Piggin, Michael Ellerman, IBM Corp.
 */

#include <linux/export.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/nmi.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <linux/ftrace.h>
#include <asm/kprobes.h>

#include <asm/paca.h>

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
static void save_context_stack(struct stack_trace *trace, unsigned long sp,
			struct task_struct *tsk, int savesched)
{
	for (;;) {
		unsigned long *stack = (unsigned long *) sp;
		unsigned long newsp, ip;

		if (!validate_sp(sp, tsk, STACK_FRAME_OVERHEAD))
			return;

		newsp = stack[0];
		ip = stack[STACK_FRAME_LR_SAVE];

		if (savesched || !in_sched_functions(ip)) {
			if (!trace->skip)
				trace->entries[trace->nr_entries++] = ip;
			else
				trace->skip--;
		}

		if (trace->nr_entries >= trace->max_entries)
			return;

		sp = newsp;
	}
}

void save_stack_trace(struct stack_trace *trace)
{
	unsigned long sp;

	sp = current_stack_pointer();

	save_context_stack(trace, sp, current, 1);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	unsigned long sp;

	if (tsk == current)
		sp = current_stack_pointer();
	else
		sp = tsk->thread.ksp;

	save_context_stack(trace, sp, tsk, 0);
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);

void
save_stack_trace_regs(struct pt_regs *regs, struct stack_trace *trace)
{
	save_context_stack(trace, regs->gpr[1], current, 0);
}
EXPORT_SYMBOL_GPL(save_stack_trace_regs);

#ifdef CONFIG_HAVE_RELIABLE_STACKTRACE
int
save_stack_trace_tsk_reliable(struct task_struct *tsk,
				struct stack_trace *trace)
{
	unsigned long sp;
	unsigned long stack_page = (unsigned long)task_stack_page(tsk);
	unsigned long stack_end;
	int graph_idx = 0;

	/*
	 * The last frame (unwinding first) may not yet have saved
	 * its LR onto the stack.
	 */
	int firstframe = 1;

	if (tsk == current)
		sp = current_stack_pointer();
	else
		sp = tsk->thread.ksp;

	stack_end = stack_page + THREAD_SIZE;
	if (!is_idle_task(tsk)) {
		/*
		 * For user tasks, this is the SP value loaded on
		 * kernel entry, see "PACAKSAVE(r13)" in _switch() and
		 * system_call_common()/EXCEPTION_PROLOG_COMMON().
		 *
		 * Likewise for non-swapper kernel threads,
		 * this also happens to be the top of the stack
		 * as setup by copy_thread().
		 *
		 * Note that stack backlinks are not properly setup by
		 * copy_thread() and thus, a forked task() will have
		 * an unreliable stack trace until it's been
		 * _switch()'ed to for the first time.
		 */
		stack_end -= STACK_FRAME_OVERHEAD + sizeof(struct pt_regs);
	} else {
		/*
		 * idle tasks have a custom stack layout,
		 * c.f. cpu_idle_thread_init().
		 */
		stack_end -= STACK_FRAME_OVERHEAD;
	}

	if (sp < stack_page + sizeof(struct thread_struct) ||
	    sp > stack_end - STACK_FRAME_MIN_SIZE) {
		return 1;
	}

	for (;;) {
		unsigned long *stack = (unsigned long *) sp;
		unsigned long newsp, ip;

		/* sanity check: ABI requires SP to be aligned 16 bytes. */
		if (sp & 0xF)
			return 1;

		/* Mark stacktraces with exception frames as unreliable. */
		if (sp <= stack_end - STACK_INT_FRAME_SIZE &&
		    stack[STACK_FRAME_MARKER] == STACK_FRAME_REGS_MARKER) {
			return 1;
		}

		newsp = stack[0];
		/* Stack grows downwards; unwinder may only go up. */
		if (newsp <= sp)
			return 1;

		if (newsp != stack_end &&
		    newsp > stack_end - STACK_FRAME_MIN_SIZE) {
			return 1; /* invalid backlink, too far up. */
		}

		/* Examine the saved LR: it must point into kernel code. */
		ip = stack[STACK_FRAME_LR_SAVE];
		if (!firstframe && !__kernel_text_address(ip))
			return 1;
		firstframe = 0;

		/*
		 * FIXME: IMHO these tests do not belong in
		 * arch-dependent code, they are generic.
		 */
		ip = ftrace_graph_ret_addr(tsk, &graph_idx, ip, NULL);
#ifdef CONFIG_KPROBES
		/*
		 * Mark stacktraces with kretprobed functions on them
		 * as unreliable.
		 */
		if (ip == (unsigned long)kretprobe_trampoline)
			return 1;
#endif

		if (!trace->skip)
			trace->entries[trace->nr_entries++] = ip;
		else
			trace->skip--;

		if (newsp == stack_end)
			break;

		if (trace->nr_entries >= trace->max_entries)
			return -E2BIG;

		sp = newsp;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk_reliable);
#endif /* CONFIG_HAVE_RELIABLE_STACKTRACE */

#if defined(CONFIG_PPC_BOOK3S_64) && defined(CONFIG_NMI_IPI)
static void handle_backtrace_ipi(struct pt_regs *regs)
{
	nmi_cpu_backtrace(regs);
}

static void raise_backtrace_ipi(cpumask_t *mask)
{
	unsigned int cpu;

	for_each_cpu(cpu, mask) {
		if (cpu == smp_processor_id())
			handle_backtrace_ipi(NULL);
		else
			smp_send_safe_nmi_ipi(cpu, handle_backtrace_ipi, 5 * USEC_PER_SEC);
	}

	for_each_cpu(cpu, mask) {
		struct paca_struct *p = paca_ptrs[cpu];

		cpumask_clear_cpu(cpu, mask);

		pr_warn("CPU %d didn't respond to backtrace IPI, inspecting paca.\n", cpu);
		if (!virt_addr_valid(p)) {
			pr_warn("paca pointer appears corrupt? (%px)\n", p);
			continue;
		}

		pr_warn("irq_soft_mask: 0x%02x in_mce: %d in_nmi: %d",
			p->irq_soft_mask, p->in_mce, p->in_nmi);

		if (virt_addr_valid(p->__current))
			pr_cont(" current: %d (%s)\n", p->__current->pid,
				p->__current->comm);
		else
			pr_cont(" current pointer corrupt? (%px)\n", p->__current);

		pr_warn("Back trace of paca->saved_r1 (0x%016llx) (possibly stale):\n", p->saved_r1);
		show_stack(p->__current, (unsigned long *)p->saved_r1);
	}
}

void arch_trigger_cpumask_backtrace(const cpumask_t *mask, bool exclude_self)
{
	nmi_trigger_cpumask_backtrace(mask, exclude_self, raise_backtrace_ipi);
}
#endif /* defined(CONFIG_PPC_BOOK3S_64) && defined(CONFIG_NMI_IPI) */
