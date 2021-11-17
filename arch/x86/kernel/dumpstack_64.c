// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 */
#include <linux/sched/debug.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <linux/kdebug.h>
#include <linux/export.h>
#include <linux/ptrace.h>
#include <linux/kexec.h>
#include <linux/sysfs.h>
#include <linux/bug.h>
#include <linux/nmi.h>

#include <asm/cpu_entry_area.h>
#include <asm/stacktrace.h>

static const char * const exception_stack_names[] = {
		[ ESTACK_DF	]	= "#DF",
		[ ESTACK_NMI	]	= "NMI",
		[ ESTACK_DB	]	= "#DB",
		[ ESTACK_MCE	]	= "#MC",
		[ ESTACK_VC	]	= "#VC",
		[ ESTACK_VC2	]	= "#VC2",
};

const char *stack_type_name(enum stack_type type)
{
	BUILD_BUG_ON(N_EXCEPTION_STACKS != 6);

	if (type == STACK_TYPE_IRQ)
		return "IRQ";

	if (type == STACK_TYPE_ENTRY) {
		/*
		 * On 64-bit, we have a generic entry stack that we
		 * use for all the kernel entry points, including
		 * SYSENTER.
		 */
		return "ENTRY_TRAMPOLINE";
	}

	if (type >= STACK_TYPE_EXCEPTION && type <= STACK_TYPE_EXCEPTION_LAST)
		return exception_stack_names[type - STACK_TYPE_EXCEPTION];

	return NULL;
}

/**
 * struct estack_pages - Page descriptor for exception stacks
 * @offs:	Offset from the start of the exception stack area
 * @size:	Size of the exception stack
 * @type:	Type to store in the stack_info struct
 */
struct estack_pages {
	u32	offs;
	u16	size;
	u16	type;
};

#define EPAGERANGE(st)							\
	[PFN_DOWN(CEA_ESTACK_OFFS(st)) ...				\
	 PFN_DOWN(CEA_ESTACK_OFFS(st) + CEA_ESTACK_SIZE(st) - 1)] = {	\
		.offs	= CEA_ESTACK_OFFS(st),				\
		.size	= CEA_ESTACK_SIZE(st),				\
		.type	= STACK_TYPE_EXCEPTION + ESTACK_ ##st, }

/*
 * Array of exception stack page descriptors. If the stack is larger than
 * PAGE_SIZE, all pages covering a particular stack will have the same
 * info. The guard pages including the not mapped DB2 stack are zeroed
 * out.
 */
static const
struct estack_pages estack_pages[CEA_ESTACK_PAGES] ____cacheline_aligned = {
	EPAGERANGE(DF),
	EPAGERANGE(NMI),
	EPAGERANGE(DB),
	EPAGERANGE(MCE),
	EPAGERANGE(VC),
	EPAGERANGE(VC2),
};

static __always_inline bool in_exception_stack(unsigned long *stack, struct stack_info *info)
{
	unsigned long begin, end, stk = (unsigned long)stack;
	const struct estack_pages *ep;
	struct pt_regs *regs;
	unsigned int k;

	BUILD_BUG_ON(N_EXCEPTION_STACKS != 6);

	begin = (unsigned long)__this_cpu_read(cea_exception_stacks);
	/*
	 * Handle the case where stack trace is collected _before_
	 * cea_exception_stacks had been initialized.
	 */
	if (!begin)
		return false;

	end = begin + sizeof(struct cea_exception_stacks);
	/* Bail if @stack is outside the exception stack area. */
	if (stk < begin || stk >= end)
		return false;

	/* Calc page offset from start of exception stacks */
	k = (stk - begin) >> PAGE_SHIFT;
	/* Lookup the page descriptor */
	ep = &estack_pages[k];
	/* Guard page? */
	if (!ep->size)
		return false;

	begin += (unsigned long)ep->offs;
	end = begin + (unsigned long)ep->size;
	regs = (struct pt_regs *)end - 1;

	info->type	= ep->type;
	info->begin	= (unsigned long *)begin;
	info->end	= (unsigned long *)end;
	info->next_sp	= (unsigned long *)regs->sp;
	return true;
}

static __always_inline bool in_irq_stack(unsigned long *stack, struct stack_info *info)
{
	unsigned long *end = (unsigned long *)this_cpu_read(hardirq_stack_ptr);
	unsigned long *begin;

	/*
	 * @end points directly to the top most stack entry to avoid a -8
	 * adjustment in the stack switch hotpath. Adjust it back before
	 * calculating @begin.
	 */
	end++;
	begin = end - (IRQ_STACK_SIZE / sizeof(long));

	/*
	 * Due to the switching logic RSP can never be == @end because the
	 * final operation is 'popq %rsp' which means after that RSP points
	 * to the original stack and not to @end.
	 */
	if (stack < begin || stack >= end)
		return false;

	info->type	= STACK_TYPE_IRQ;
	info->begin	= begin;
	info->end	= end;

	/*
	 * The next stack pointer is stored at the top of the irq stack
	 * before switching to the irq stack. Actual stack entries are all
	 * below that.
	 */
	info->next_sp = (unsigned long *)*(end - 1);

	return true;
}

bool noinstr get_stack_info_noinstr(unsigned long *stack, struct task_struct *task,
				    struct stack_info *info)
{
	if (in_task_stack(stack, task, info))
		return true;

	if (task != current)
		return false;

	if (in_exception_stack(stack, info))
		return true;

	if (in_irq_stack(stack, info))
		return true;

	if (in_entry_stack(stack, info))
		return true;

	return false;
}

int get_stack_info(unsigned long *stack, struct task_struct *task,
		   struct stack_info *info, unsigned long *visit_mask)
{
	task = task ? : current;

	if (!stack)
		goto unknown;

	if (!get_stack_info_noinstr(stack, task, info))
		goto unknown;

	/*
	 * Make sure we don't iterate through any given stack more than once.
	 * If it comes up a second time then there's something wrong going on:
	 * just break out and report an unknown stack type.
	 */
	if (visit_mask) {
		if (*visit_mask & (1UL << info->type)) {
			if (task == current)
				printk_deferred_once(KERN_WARNING "WARNING: stack recursion on stack type %d\n", info->type);
			goto unknown;
		}
		*visit_mask |= 1UL << info->type;
	}

	return 0;

unknown:
	info->type = STACK_TYPE_UNKNOWN;
	return -EINVAL;
}
