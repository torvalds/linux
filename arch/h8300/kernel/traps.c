/*
 * linux/arch/h8300/boot/traps.c -- general exception handling code
 * H8/300 support Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * Cloned from Linux/m68k.
 *
 * No original Copyright holder listed,
 * Probable original (C) Roman Zippel (assigned DJD, 1999)
 *
 * Copyright 1999-2000 D. Jeff Dionne, <jeff@rt-control.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/mm_types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/bug.h>

#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/page.h>

static DEFINE_SPINLOCK(die_lock);

/*
 * this must be called very early as the kernel might
 * use some instruction that are emulated on the 060
 */

void __init base_trap_init(void)
{
}

asmlinkage void set_esp0(unsigned long ssp)
{
	current->thread.esp0 = ssp;
}

/*
 *	Generic dumping code. Used for panic and debug.
 */

static void dump(struct pt_regs *fp)
{
	unsigned long	*sp;
	unsigned char	*tp;
	int		i;

	pr_info("\nCURRENT PROCESS:\n\n");
	pr_info("COMM=%s PID=%d\n", current->comm, current->pid);
	if (current->mm) {
		pr_info("TEXT=%08x-%08x DATA=%08x-%08x BSS=%08x-%08x\n",
			(int) current->mm->start_code,
			(int) current->mm->end_code,
			(int) current->mm->start_data,
			(int) current->mm->end_data,
			(int) current->mm->end_data,
			(int) current->mm->brk);
		pr_info("USER-STACK=%08x  KERNEL-STACK=%08lx\n\n",
			(int) current->mm->start_stack,
			(int) PAGE_SIZE+(unsigned long)current);
	}

	show_regs(fp);
	pr_info("\nCODE:");
	tp = ((unsigned char *) fp->pc) - 0x20;
	for (sp = (unsigned long *) tp, i = 0; (i < 0x40);  i += 4) {
		if ((i % 0x10) == 0)
			pr_info("\n%08x: ", (int) (tp + i));
		pr_info("%08x ", (int) *sp++);
	}
	pr_info("\n");

	pr_info("\nKERNEL STACK:");
	tp = ((unsigned char *) fp) - 0x40;
	for (sp = (unsigned long *) tp, i = 0; (i < 0xc0); i += 4) {
		if ((i % 0x10) == 0)
			pr_info("\n%08x: ", (int) (tp + i));
		pr_info("%08x ", (int) *sp++);
	}
	pr_info("\n");
	if (STACK_MAGIC != *(unsigned long *)((unsigned long)current+PAGE_SIZE))
		pr_info("(Possibly corrupted stack page??)\n");

	pr_info("\n\n");
}

void die(const char *str, struct pt_regs *fp, unsigned long err)
{
	static int diecount;

	oops_enter();

	console_verbose();
	spin_lock_irq(&die_lock);
	report_bug(fp->pc, fp);
	pr_crit("%s: %04lx [#%d] ", str, err & 0xffff, ++diecount);
	dump(fp);

	spin_unlock_irq(&die_lock);
	make_dead_task(SIGSEGV);
}

static int kstack_depth_to_print = 24;

void show_stack(struct task_struct *task, unsigned long *esp, const char *loglvl)
{
	unsigned long *stack,  addr;
	int i;

	if (esp == NULL)
		esp = (unsigned long *) &esp;

	stack = esp;

	printk("%sStack from %08lx:", loglvl, (unsigned long)stack);
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (((unsigned long)stack & (THREAD_SIZE - 1)) >=
		    THREAD_SIZE-4)
			break;
		if (i % 8 == 0)
			printk("%s ", loglvl);
		pr_cont(" %08lx", *stack++);
	}

	printk("%s\nCall Trace:\n", loglvl);
	i = 0;
	stack = esp;
	while (((unsigned long)stack & (THREAD_SIZE - 1)) < THREAD_SIZE-4) {
		addr = *stack++;
		/*
		 * If the address is either in the text segment of the
		 * kernel, or in the region which contains vmalloc'ed
		 * memory, it *may* be the address of a calling
		 * routine; if so, print it so that someone tracing
		 * down the cause of the crash will be able to figure
		 * out the call path that was taken.
		 */
		if (check_kernel_text(addr)) {
			if (i % 4 == 0)
				printk("%s       ", loglvl);
			pr_cont(" [<%08lx>]", addr);
			i++;
		}
	}
	printk("%s\n", loglvl);
}
