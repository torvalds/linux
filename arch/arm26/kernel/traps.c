/*
 *  linux/arch/arm26/kernel/traps.c
 *
 *  Copyright (C) 1995-2002 Russell King
 *  Fragments that appear the same as linux/arch/i386/kernel/traps.c (C) Linus Torvalds
 *  Copyright (C) 2003 Ian Molton (ARM26)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  'traps.c' handles hardware exceptions after we have saved some state in
 *  'linux/arch/arm26/lib/traps.S'.  Mostly a debugging aid, but will probably
 *  kill the offending process.
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/elf.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <linux/mutex.h>

#include "ptrace.h"

extern void c_backtrace (unsigned long fp, int pmode);
extern void show_pte(struct mm_struct *mm, unsigned long addr);

const char *processor_modes[] = { "USER_26", "FIQ_26" , "IRQ_26" , "SVC_26" };

static const char *handler[]= { "prefetch abort", "data abort", "address exception", "interrupt" "*bad reason*"};

/*
 * Stack pointers should always be within the kernels view of
 * physical memory.  If it is not there, then we can't dump
 * out any information relating to the stack.
 */
static int verify_stack(unsigned long sp)
{
	if (sp < PAGE_OFFSET || (sp > (unsigned long)high_memory && high_memory != 0))
		return -EFAULT;

	return 0;
}

/*
 * Dump out the contents of some memory nicely...
 */
static void dump_mem(const char *str, unsigned long bottom, unsigned long top)
{
	unsigned long p = bottom & ~31;
	mm_segment_t fs;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	printk("%s", str);
	printk("(0x%08lx to 0x%08lx)\n", bottom, top);

	for (p = bottom & ~31; p < top;) {
		printk("%04lx: ", p & 0xffff);

		for (i = 0; i < 8; i++, p += 4) {
			unsigned int val;

			if (p < bottom || p >= top)
				printk("         ");
			else {
				__get_user(val, (unsigned long *)p);
				printk("%08x ", val);
			}
		}
		printk ("\n");
	}

	set_fs(fs);
}

static void dump_instr(struct pt_regs *regs)
{
	unsigned long addr = instruction_pointer(regs);
	const int width = 8;
	mm_segment_t fs;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	printk("Code: ");
	for (i = -4; i < 1; i++) {
		unsigned int val, bad;

		bad = __get_user(val, &((u32 *)addr)[i]);

		if (!bad)
			printk(i == 0 ? "(%0*x) " : "%0*x ", width, val);
		else {
			printk("bad PC value.");
			break;
		}
	}
	printk("\n");

	set_fs(fs);
}

/*static*/ void __dump_stack(struct task_struct *tsk, unsigned long sp)
{
	dump_mem("Stack: ", sp, 8192+(unsigned long)task_stack_page(tsk));
}

void dump_stack(void)
{
#ifdef CONFIG_DEBUG_ERRORS
        __backtrace();
#endif
}

EXPORT_SYMBOL(dump_stack);

//FIXME - was a static fn
void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk)
{
	unsigned int fp;
	int ok = 1;

	printk("Backtrace: ");
	fp = regs->ARM_fp;
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
		c_backtrace(fp, processor_mode(regs));
}

/* FIXME - this is probably wrong.. */
void show_stack(struct task_struct *task, unsigned long *sp) {
	dump_mem("Stack: ", (unsigned long)sp, 8192+(unsigned long)task_stack_page(task));
}

DEFINE_SPINLOCK(die_lock);

/*
 * This function is protected against re-entrancy.
 */
NORET_TYPE void die(const char *str, struct pt_regs *regs, int err)
{
	struct task_struct *tsk = current;

	console_verbose();
	spin_lock_irq(&die_lock);

	printk("Internal error: %s: %x\n", str, err);
	printk("CPU: %d\n", smp_processor_id());
	show_regs(regs);
	printk("Process %s (pid: %d, stack limit = 0x%p)\n",
		current->comm, current->pid, end_of_stack(tsk));

	if (!user_mode(regs) || in_interrupt()) {
		__dump_stack(tsk, (unsigned long)(regs + 1));
		dump_backtrace(regs, tsk);
		dump_instr(regs);
	}
while(1);
	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

void die_if_kernel(const char *str, struct pt_regs *regs, int err)
{
	if (user_mode(regs))
    		return;

    	die(str, regs, err);
}

static DEFINE_MUTEX(undef_mutex);
static int (*undef_hook)(struct pt_regs *);

int request_undef_hook(int (*fn)(struct pt_regs *))
{
	int ret = -EBUSY;

	mutex_lock(&undef_mutex);
	if (undef_hook == NULL) {
		undef_hook = fn;
		ret = 0;
	}
	mutex_unlock(&undef_mutex);

	return ret;
}

int release_undef_hook(int (*fn)(struct pt_regs *))
{
	int ret = -EINVAL;

	mutex_lock(&undef_mutex);
	if (undef_hook == fn) {
		undef_hook = NULL;
		ret = 0;
	}
	mutex_unlock(&undef_mutex);

	return ret;
}

static int undefined_extension(struct pt_regs *regs, unsigned int op)
{
	switch (op) {
	case 1:	/* 0xde01 / 0x?7f001f0 */
		ptrace_break(current, regs);
		return 0;
	}
	return 1;
}

asmlinkage void do_undefinstr(struct pt_regs *regs)
{
	siginfo_t info;
	void *pc;

	regs->ARM_pc -= 4;

	pc = (unsigned long *)instruction_pointer(regs); /* strip PSR */

	if (user_mode(regs)) {
		u32 instr;

		get_user(instr, (u32 *)pc);

		if ((instr & 0x0fff00ff) == 0x07f000f0 &&
		    undefined_extension(regs, (instr >> 8) & 255) == 0) {
			regs->ARM_pc += 4;
			return;
		}
	} else {
		if (undef_hook && undef_hook(regs) == 0) {
			regs->ARM_pc += 4;
			return;
		}
	}

#ifdef CONFIG_DEBUG_USER
	printk(KERN_INFO "%s (%d): undefined instruction: pc=%p\n",
		current->comm, current->pid, pc);
	dump_instr(regs);
#endif

	current->thread.error_code = 0;
	current->thread.trap_no = 6;

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLOPC;
	info.si_addr  = pc;

	force_sig_info(SIGILL, &info, current);

	die_if_kernel("Oops - undefined instruction", regs, 0);
}

asmlinkage void do_excpt(unsigned long address, struct pt_regs *regs, int mode)
{
	siginfo_t info;

#ifdef CONFIG_DEBUG_USER
	printk(KERN_INFO "%s (%d): address exception: pc=%08lx\n",
		current->comm, current->pid, instruction_pointer(regs));
	dump_instr(regs);
#endif

	current->thread.error_code = 0;
	current->thread.trap_no = 11;

	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code  = BUS_ADRERR;
	info.si_addr  = (void *)address;

	force_sig_info(SIGBUS, &info, current);

	die_if_kernel("Oops - address exception", regs, mode);
}

asmlinkage void do_unexp_fiq (struct pt_regs *regs)
{
#ifndef CONFIG_IGNORE_FIQ
	printk("Hmm.  Unexpected FIQ received, but trying to continue\n");
	printk("You may have a hardware problem...\n");
#endif
}

/*
 * bad_mode handles the impossible case in the vectors.  If you see one of
 * these, then it's extremely serious, and could mean you have buggy hardware.
 * It never returns, and never tries to sync.  We hope that we can at least
 * dump out some state information...
 */
asmlinkage void bad_mode(struct pt_regs *regs, int reason, int proc_mode)
{
	unsigned int vectors = vectors_base();

	console_verbose();

	printk(KERN_CRIT "Bad mode in %s handler detected: mode %s\n",
		handler[reason<5?reason:4], processor_modes[proc_mode]);

	/*
	 * Dump out the vectors and stub routines.  Maybe a better solution
	 * would be to dump them out only if we detect that they are corrupted.
	 */
	dump_mem(KERN_CRIT "Vectors: ", vectors, vectors + 0x40);
	dump_mem(KERN_CRIT "Stubs: ", vectors + 0x200, vectors + 0x4b8);

	die("Oops", regs, 0);
	local_irq_disable();
	panic("bad mode");
}

static int bad_syscall(int n, struct pt_regs *regs)
{
	struct thread_info *thread = current_thread_info();
	siginfo_t info;

	if (current->personality != PER_LINUX && thread->exec_domain->handler) {
		thread->exec_domain->handler(n, regs);
		return regs->ARM_r0;
	}

#ifdef CONFIG_DEBUG_USER
	printk(KERN_ERR "[%d] %s: obsolete system call %08x.\n",
		current->pid, current->comm, n);
	dump_instr(regs);
#endif

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLTRP;
	info.si_addr  = (void *)instruction_pointer(regs) - 4;

	force_sig_info(SIGILL, &info, current);
	die_if_kernel("Oops", regs, n);
	return regs->ARM_r0;
}

static inline void
do_cache_op(unsigned long start, unsigned long end, int flags)
{
	struct vm_area_struct *vma;

	if (end < start)
		return;

	vma = find_vma(current->active_mm, start);
	if (vma && vma->vm_start < end) {
		if (start < vma->vm_start)
			start = vma->vm_start;
		if (end > vma->vm_end)
			end = vma->vm_end;
	}
}

/*
 * Handle all unrecognised system calls.
 *  0x9f0000 - 0x9fffff are some more esoteric system calls
 */
#define NR(x) ((__ARM_NR_##x) - __ARM_NR_BASE)
asmlinkage int arm_syscall(int no, struct pt_regs *regs)
{
	siginfo_t info;

	if ((no >> 16) != 0x9f)
		return bad_syscall(no, regs);

	switch (no & 0xffff) {
	case 0: /* branch through 0 */
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		info.si_code  = SEGV_MAPERR;
		info.si_addr  = NULL;

		force_sig_info(SIGSEGV, &info, current);

		die_if_kernel("branch through zero", regs, 0);
		return 0;

	case NR(breakpoint): /* SWI BREAK_POINT */
		ptrace_break(current, regs);
		return regs->ARM_r0;

	case NR(cacheflush):
		return 0;

	case NR(usr26):
		break;

	default:
		/* Calls 9f00xx..9f07ff are defined to return -ENOSYS
		   if not implemented, rather than raising SIGILL.  This
		   way the calling program can gracefully determine whether
		   a feature is supported.  */
		if (no <= 0x7ff)
			return -ENOSYS;
		break;
	}
#ifdef CONFIG_DEBUG_USER
	/*
	 * experience shows that these seem to indicate that
	 * something catastrophic has happened
	 */
	printk("[%d] %s: arm syscall %d\n", current->pid, current->comm, no);
	dump_instr(regs);
	if (user_mode(regs)) {
		show_regs(regs);
		c_backtrace(regs->ARM_fp, processor_mode(regs));
	}
#endif
	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLTRP;
	info.si_addr  = (void *)instruction_pointer(regs) - 4;

	force_sig_info(SIGILL, &info, current);
	die_if_kernel("Oops", regs, no);
	return 0;
}

void __bad_xchg(volatile void *ptr, int size)
{
	printk("xchg: bad data size: pc 0x%p, ptr 0x%p, size %d\n",
		__builtin_return_address(0), ptr, size);
	BUG();
}

/*
 * A data abort trap was taken, but we did not handle the instruction.
 * Try to abort the user program, or panic if it was the kernel.
 */
asmlinkage void
baddataabort(int code, unsigned long instr, struct pt_regs *regs)
{
	unsigned long addr = instruction_pointer(regs);
	siginfo_t info;

#ifdef CONFIG_DEBUG_USER
	printk(KERN_ERR "[%d] %s: bad data abort: code %d instr 0x%08lx\n",
		current->pid, current->comm, code, instr);
	dump_instr(regs);
	show_pte(current->mm, addr);
#endif

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLOPC;
	info.si_addr  = (void *)addr;

	force_sig_info(SIGILL, &info, current);
	die_if_kernel("unknown data abort code", regs, instr);
}

volatile void __bug(const char *file, int line, void *data)
{
	printk(KERN_CRIT"kernel BUG at %s:%d!", file, line);
	if (data)
		printk(KERN_CRIT" - extra data = %p", data);
	printk("\n");
	*(int *)0 = 0;
}

void __readwrite_bug(const char *fn)
{
	printk("%s called, but not implemented", fn);
	BUG();
}

void __pte_error(const char *file, int line, unsigned long val)
{
	printk("%s:%d: bad pte %08lx.\n", file, line, val);
}

void __pmd_error(const char *file, int line, unsigned long val)
{
	printk("%s:%d: bad pmd %08lx.\n", file, line, val);
}

void __pgd_error(const char *file, int line, unsigned long val)
{
	printk("%s:%d: bad pgd %08lx.\n", file, line, val);
}

asmlinkage void __div0(void)
{
	printk("Division by zero in kernel.\n");
	dump_stack();
}

void abort(void)
{
	BUG();

	/* if that doesn't kill us, halt */
	panic("Oops failed to kill thread");
}

void __init trap_init(void)
{
	extern void __trap_init(unsigned long);
	unsigned long base = vectors_base();

	__trap_init(base);
	if (base != 0)
		printk(KERN_DEBUG "Relocating machine vectors to 0x%08lx\n",
			base);
}
