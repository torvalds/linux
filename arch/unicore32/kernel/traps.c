// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/unicore32/kernel/traps.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 *  'traps.c' handles hardware exceptions after we have saved some state.
 *  Mostly a debugging aid, but will probably kill the offending process.
 */
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/spinlock.h>
#include <linux/personality.h>
#include <linux/kallsyms.h>
#include <linux/kdebug.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/atomic.h>
#include <linux/unistd.h>

#include <asm/cacheflush.h>
#include <asm/traps.h>

#include "setup.h"

static void dump_mem(const char *, const char *, unsigned long, unsigned long);

void dump_backtrace_entry(unsigned long where,
		unsigned long from, unsigned long frame)
{
#ifdef CONFIG_KALLSYMS
	printk(KERN_DEFAULT "[<%08lx>] (%pS) from [<%08lx>] (%pS)\n",
			where, (void *)where, from, (void *)from);
#else
	printk(KERN_DEFAULT "Function entered at [<%08lx>] from [<%08lx>]\n",
			where, from);
#endif
}

/*
 * Stack pointers should always be within the kernels view of
 * physical memory.  If it is not there, then we can't dump
 * out any information relating to the stack.
 */
static int verify_stack(unsigned long sp)
{
	if (sp < PAGE_OFFSET ||
	    (sp > (unsigned long)high_memory && high_memory != NULL))
		return -EFAULT;

	return 0;
}

/*
 * Dump out the contents of some memory nicely...
 */
static void dump_mem(const char *lvl, const char *str, unsigned long bottom,
		     unsigned long top)
{
	unsigned long first;
	mm_segment_t fs;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	printk(KERN_DEFAULT "%s%s(0x%08lx to 0x%08lx)\n",
			lvl, str, bottom, top);

	for (first = bottom & ~31; first < top; first += 32) {
		unsigned long p;
		char str[sizeof(" 12345678") * 8 + 1];

		memset(str, ' ', sizeof(str));
		str[sizeof(str) - 1] = '\0';

		for (p = first, i = 0; i < 8 && p < top; i++, p += 4) {
			if (p >= bottom && p < top) {
				unsigned long val;
				if (__get_user(val, (unsigned long *)p) == 0)
					sprintf(str + i * 9, " %08lx", val);
				else
					sprintf(str + i * 9, " ????????");
			}
		}
		printk(KERN_DEFAULT "%s%04lx:%s\n", lvl, first & 0xffff, str);
	}

	set_fs(fs);
}

static void dump_instr(const char *lvl, struct pt_regs *regs)
{
	unsigned long addr = instruction_pointer(regs);
	const int width = 8;
	mm_segment_t fs;
	char str[sizeof("00000000 ") * 5 + 2 + 1], *p = str;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	for (i = -4; i < 1; i++) {
		unsigned int val, bad;

		bad = __get_user(val, &((u32 *)addr)[i]);

		if (!bad)
			p += sprintf(p, i == 0 ? "(%0*x) " : "%0*x ",
					width, val);
		else {
			p += sprintf(p, "bad PC value");
			break;
		}
	}
	printk(KERN_DEFAULT "%sCode: %s\n", lvl, str);

	set_fs(fs);
}

static void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk)
{
	unsigned int fp, mode;
	int ok = 1;

	printk(KERN_DEFAULT "Backtrace: ");

	if (!tsk)
		tsk = current;

	if (regs) {
		fp = regs->UCreg_fp;
		mode = processor_mode(regs);
	} else if (tsk != current) {
		fp = thread_saved_fp(tsk);
		mode = 0x10;
	} else {
		asm("mov %0, fp" : "=r" (fp) : : "cc");
		mode = 0x10;
	}

	if (!fp) {
		printk("no frame pointer");
		ok = 0;
	} else if (verify_stack(fp)) {
		printk("invalid frame pointer 0x%08x", fp);
		ok = 0;
	} else if (fp < (unsigned long)end_of_stack(tsk))
		printk("frame pointer underflow");
	printk("\n");

	if (ok)
		c_backtrace(fp, mode);
}

void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	dump_backtrace(NULL, tsk);
	barrier();
}

static int __die(const char *str, int err, struct thread_info *thread,
		struct pt_regs *regs)
{
	struct task_struct *tsk = thread->task;
	static int die_counter;
	int ret;

	printk(KERN_EMERG "Internal error: %s: %x [#%d]\n",
	       str, err, ++die_counter);

	/* trap and error numbers are mostly meaningless on UniCore */
	ret = notify_die(DIE_OOPS, str, regs, err, tsk->thread.trap_no, \
			SIGSEGV);
	if (ret == NOTIFY_STOP)
		return ret;

	print_modules();
	__show_regs(regs);
	printk(KERN_EMERG "Process %.*s (pid: %d, stack limit = 0x%p)\n",
		TASK_COMM_LEN, tsk->comm, task_pid_nr(tsk), thread + 1);

	if (!user_mode(regs) || in_interrupt()) {
		dump_mem(KERN_EMERG, "Stack: ", regs->UCreg_sp,
			 THREAD_SIZE + (unsigned long)task_stack_page(tsk));
		dump_backtrace(regs, tsk);
		dump_instr(KERN_EMERG, regs);
	}

	return ret;
}

DEFINE_SPINLOCK(die_lock);

/*
 * This function is protected against re-entrancy.
 */
void die(const char *str, struct pt_regs *regs, int err)
{
	struct thread_info *thread = current_thread_info();
	int ret;

	oops_enter();

	spin_lock_irq(&die_lock);
	console_verbose();
	bust_spinlocks(1);
	ret = __die(str, err, thread, regs);

	bust_spinlocks(0);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	spin_unlock_irq(&die_lock);
	oops_exit();

	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception");
	if (ret != NOTIFY_STOP)
		do_exit(SIGSEGV);
}

void uc32_notify_die(const char *str, struct pt_regs *regs,
		int sig, int code, void __user *addr,
		unsigned long err, unsigned long trap)
{
	if (user_mode(regs)) {
		current->thread.error_code = err;
		current->thread.trap_no = trap;

		force_sig_fault(sig, code, addr);
	} else
		die(str, regs, err);
}

/*
 * bad_mode handles the impossible case in the vectors.  If you see one of
 * these, then it's extremely serious, and could mean you have buggy hardware.
 * It never returns, and never tries to sync.  We hope that we can at least
 * dump out some state information...
 */
asmlinkage void bad_mode(struct pt_regs *regs, unsigned int reason)
{
	console_verbose();

	printk(KERN_CRIT "Bad mode detected with reason 0x%x\n", reason);

	die("Oops - bad mode", regs, 0);
	local_irq_disable();
	panic("bad mode");
}

void __pte_error(const char *file, int line, unsigned long val)
{
	printk(KERN_DEFAULT "%s:%d: bad pte %08lx.\n", file, line, val);
}

void __pmd_error(const char *file, int line, unsigned long val)
{
	printk(KERN_DEFAULT "%s:%d: bad pmd %08lx.\n", file, line, val);
}

void __pgd_error(const char *file, int line, unsigned long val)
{
	printk(KERN_DEFAULT "%s:%d: bad pgd %08lx.\n", file, line, val);
}

asmlinkage void __div0(void)
{
	printk(KERN_DEFAULT "Division by zero in kernel.\n");
	dump_stack();
}
EXPORT_SYMBOL(__div0);

void abort(void)
{
	BUG();

	/* if that doesn't kill us, halt */
	panic("Oops failed to kill thread");
}

void __init trap_init(void)
{
	return;
}

void __init early_trap_init(void)
{
	unsigned long vectors = VECTORS_BASE;

	/*
	 * Copy the vectors, stubs (in entry-unicore.S)
	 * into the vector page, mapped at 0xffff0000, and ensure these
	 * are visible to the instruction stream.
	 */
	memcpy((void *)vectors,
			__vectors_start,
			__vectors_end - __vectors_start);
	memcpy((void *)vectors + 0x200,
			__stubs_start,
			__stubs_end - __stubs_start);

	early_signal_init();

	flush_icache_range(vectors, vectors + PAGE_SIZE);
}
