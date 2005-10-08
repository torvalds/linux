/*
 * Copyright (C) 2001 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/config.h"
#include "linux/kernel.h"
#include "linux/smp.h"
#include "linux/sched.h"
#include "linux/kallsyms.h"
#include "asm/ptrace.h"
#include "sysrq.h"

/* This is declared by <linux/sched.h> */
void show_regs(struct pt_regs *regs)
{
        printk("\n");
        printk("EIP: %04lx:[<%08lx>] CPU: %d %s", 
	       0xffff & PT_REGS_CS(regs), PT_REGS_IP(regs),
	       smp_processor_id(), print_tainted());
        if (PT_REGS_CS(regs) & 3)
                printk(" ESP: %04lx:%08lx", 0xffff & PT_REGS_SS(regs),
		       PT_REGS_SP(regs));
        printk(" EFLAGS: %08lx\n    %s\n", PT_REGS_EFLAGS(regs),
	       print_tainted());
        printk("EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
                PT_REGS_EAX(regs), PT_REGS_EBX(regs), 
	       PT_REGS_ECX(regs), 
	       PT_REGS_EDX(regs));
        printk("ESI: %08lx EDI: %08lx EBP: %08lx",
	       PT_REGS_ESI(regs), PT_REGS_EDI(regs), 
	       PT_REGS_EBP(regs));
        printk(" DS: %04lx ES: %04lx\n",
	       0xffff & PT_REGS_DS(regs), 
	       0xffff & PT_REGS_ES(regs));

        show_trace(NULL, (unsigned long *) &regs);
}

/* Copied from i386. */
static inline int valid_stack_ptr(struct thread_info *tinfo, void *p)
{
	return	p > (void *)tinfo &&
		p < (void *)tinfo + THREAD_SIZE - 3;
}

/* Adapted from i386 (we also print the address we read from). */
static inline unsigned long print_context_stack(struct thread_info *tinfo,
				unsigned long *stack, unsigned long ebp)
{
	unsigned long addr;

#ifdef CONFIG_FRAME_POINTER
	while (valid_stack_ptr(tinfo, (void *)ebp)) {
		addr = *(unsigned long *)(ebp + 4);
		printk("%08lx:  [<%08lx>]", ebp + 4, addr);
		print_symbol(" %s", addr);
		printk("\n");
		ebp = *(unsigned long *)ebp;
	}
#else
	while (valid_stack_ptr(tinfo, stack)) {
		addr = *stack;
		if (__kernel_text_address(addr)) {
			printk("%08lx:  [<%08lx>]", (unsigned long) stack, addr);
			print_symbol(" %s", addr);
			printk("\n");
		}
		stack++;
	}
#endif
	return ebp;
}

void show_trace(struct task_struct* task, unsigned long * stack)
{
	unsigned long ebp;
	struct thread_info *context;

	/* Turn this into BUG_ON if possible. */
	if (!stack) {
		stack = (unsigned long*) &stack;
		printk("show_trace: got NULL stack, implicit assumption task == current");
		WARN_ON(1);
	}

	if (!task)
		task = current;

	if (task != current) {
		ebp = (unsigned long) KSTK_EBP(task);
	} else {
		asm ("movl %%ebp, %0" : "=r" (ebp) : );
	}

	context = (struct thread_info *)
		((unsigned long)stack & (~(THREAD_SIZE - 1)));
	print_context_stack(context, stack, ebp);

	printk("\n");
}

