/*
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/debug_locks.h>

#include <asm/exceptions.h>
#include <asm/system.h>

void trap_init(void)
{
	__enable_hw_exceptions();
}

static int kstack_depth_to_print = 24;

static int __init kstack_setup(char *s)
{
	kstack_depth_to_print = strict_strtoul(s, 0, NULL);

	return 1;
}
__setup("kstack=", kstack_setup);

void show_trace(struct task_struct *task, unsigned long *stack)
{
	unsigned long addr;

	if (!stack)
		stack = (unsigned long *)&stack;

	printk(KERN_NOTICE "Call Trace: ");
#ifdef CONFIG_KALLSYMS
	printk(KERN_NOTICE "\n");
#endif
	while (!kstack_end(stack)) {
		addr = *stack++;
		/*
		 * If the address is either in the text segment of the
		 * kernel, or in the region which contains vmalloc'ed
		 * memory, it *may* be the address of a calling
		 * routine; if so, print it so that someone tracing
		 * down the cause of the crash will be able to figure
		 * out the call path that was taken.
		 */
		if (kernel_text_address(addr))
			print_ip_sym(addr);
	}
	printk(KERN_NOTICE "\n");

	if (!task)
		task = current;

	debug_show_held_locks(task);
}

void show_stack(struct task_struct *task, unsigned long *sp)
{
	unsigned long *stack;
	int i;

	if (sp == NULL) {
		if (task)
			sp = (unsigned long *) ((struct thread_info *)
						(task->stack))->cpu_context.r1;
		else
			sp = (unsigned long *)&sp;
	}

	stack = sp;

	printk(KERN_INFO "\nStack:\n  ");

	for (i = 0; i < kstack_depth_to_print; i++) {
		if (kstack_end(sp))
			break;
		if (i && ((i % 8) == 0))
			printk("\n  ");
		printk("%08lx ", *sp++);
	}
	printk("\n");
	show_trace(task, stack);
}

void dump_stack(void)
{
	show_stack(NULL, NULL);
}
EXPORT_SYMBOL(dump_stack);

#ifdef CONFIG_MMU
void __bug(const char *file, int line, void *data)
{
	if (data)
		printk(KERN_CRIT "kernel BUG at %s:%d (data = %p)!\n",
			file, line, data);
	else
		printk(KERN_CRIT "kernel BUG at %s:%d!\n", file, line);

	machine_halt();
}

int bad_trap(int trap_num, struct pt_regs *regs)
{
	printk(KERN_CRIT
		"unimplemented trap %d called at 0x%08lx, pid %d!\n",
		trap_num, regs->pc, current->pid);
	return -ENOSYS;
}

int debug_trap(struct pt_regs *regs)
{
	int i;
	printk(KERN_CRIT "debug trap\n");
	for (i = 0; i < 32; i++) {
		/* printk("r%i:%08X\t",i,regs->gpr[i]); */
		if ((i % 4) == 3)
			printk(KERN_CRIT "\n");
	}
	printk(KERN_CRIT "pc:%08lX\tmsr:%08lX\n", regs->pc, regs->msr);
	return -ENOSYS;
}
#endif
