/* 
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/kernel.h"
#include "linux/module.h"
#include "linux/kallsyms.h"
#include "asm/page.h"
#include "asm/processor.h"
#include "sysrq.h"
#include "user_util.h"

void show_trace(unsigned long * stack)
{
	/* XXX: Copy the CONFIG_FRAME_POINTER stack-walking backtrace from
	 * arch/i386/kernel/traps.c, and then move this to sys-i386/sysrq.c.*/
        unsigned long addr;

        if (!stack) {
                stack = (unsigned long*) &stack;
		WARN_ON(1);
	}

        printk("Call Trace: \n");
        while (((long) stack & (THREAD_SIZE-1)) != 0) {
                addr = *stack;
		if (__kernel_text_address(addr)) {
			printk("%08lx:  [<%08lx>]", (unsigned long) stack, addr);
			print_symbol(" %s", addr);
			printk("\n");
                }
                stack++;
        }
        printk("\n");
}

/*
 * stack dumps generator - this is used by arch-independent code.
 * And this is identical to i386 currently.
 */
void dump_stack(void)
{
	unsigned long stack;

	show_trace(&stack);
}
EXPORT_SYMBOL(dump_stack);

/*Stolen from arch/i386/kernel/traps.c */
static int kstack_depth_to_print = 24;

/* This recently started being used in arch-independent code too, as in
 * kernel/sched.c.*/
void show_stack(struct task_struct *task, unsigned long *esp)
{
	unsigned long *stack;
	int i;

	if (esp == NULL) {
		if (task != current) {
			esp = (unsigned long *) KSTK_ESP(task);
			/* Which one? No actual difference - just coding style.*/
			//esp = (unsigned long *) PT_REGS_IP(&task->thread.regs);
		} else {
			esp = (unsigned long *) &esp;
		}
	}

	stack = esp;
	for(i = 0; i < kstack_depth_to_print; i++) {
		if (kstack_end(stack))
			break;
		if (i && ((i % 8) == 0))
			printk("\n       ");
		printk("%08lx ", *stack++);
	}

	show_trace(esp);
}
