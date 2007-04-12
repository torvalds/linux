/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/notifier.h>

#include <asm/traps.h>
#include <asm/sysreg.h>
#include <asm/addrspace.h>
#include <asm/ocd.h>
#include <asm/mmu_context.h>
#include <asm/uaccess.h>

static void dump_mem(const char *str, unsigned long bottom, unsigned long top)
{
	unsigned long p;
	int i;

	printk("%s(0x%08lx to 0x%08lx)\n", str, bottom, top);

	for (p = bottom & ~31; p < top; ) {
		printk("%04lx: ", p & 0xffff);

		for (i = 0; i < 8; i++, p += 4) {
			unsigned int val;

			if (p < bottom || p >= top)
				printk("         ");
			else {
				if (__get_user(val, (unsigned int __user *)p)) {
					printk("\n");
					goto out;
				}
				printk("%08x ", val);
			}
		}
		printk("\n");
	}

out:
	return;
}

static inline int valid_stack_ptr(struct thread_info *tinfo, unsigned long p)
{
	return (p > (unsigned long)tinfo)
		&& (p < (unsigned long)tinfo + THREAD_SIZE - 3);
}

#ifdef CONFIG_FRAME_POINTER
static inline void __show_trace(struct task_struct *tsk, unsigned long *sp,
				struct pt_regs *regs)
{
	unsigned long lr, fp;
	struct thread_info *tinfo;

	tinfo = (struct thread_info *)
		((unsigned long)sp & ~(THREAD_SIZE - 1));

	if (regs)
		fp = regs->r7;
	else if (tsk == current)
		asm("mov %0, r7" : "=r"(fp));
	else
		fp = tsk->thread.cpu_context.r7;

	/*
	 * Walk the stack as long as the frame pointer (a) is within
	 * the kernel stack of the task, and (b) it doesn't move
	 * downwards.
	 */
	while (valid_stack_ptr(tinfo, fp)) {
		unsigned long new_fp;

		lr = *(unsigned long *)fp;
		printk(" [<%08lx>] ", lr);
		print_symbol("%s\n", lr);

		new_fp = *(unsigned long *)(fp + 4);
		if (new_fp <= fp)
			break;
		fp = new_fp;
	}
	printk("\n");
}
#else
static inline void __show_trace(struct task_struct *tsk, unsigned long *sp,
				struct pt_regs *regs)
{
	unsigned long addr;

	while (!kstack_end(sp)) {
		addr = *sp++;
		if (kernel_text_address(addr)) {
			printk(" [<%08lx>] ", addr);
			print_symbol("%s\n", addr);
		}
	}
}
#endif

void show_trace(struct task_struct *tsk, unsigned long *sp,
		       struct pt_regs *regs)
{
	if (regs &&
	    (((regs->sr & MODE_MASK) == MODE_EXCEPTION) ||
	     ((regs->sr & MODE_MASK) == MODE_USER)))
		return;

	printk ("Call trace:");
#ifdef CONFIG_KALLSYMS
	printk("\n");
#endif

	__show_trace(tsk, sp, regs);
	printk("\n");
}

void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	unsigned long stack;

	if (!tsk)
		tsk = current;
	if (sp == 0) {
		if (tsk == current) {
			register unsigned long *real_sp __asm__("sp");
			sp = real_sp;
		} else {
			sp = (unsigned long *)tsk->thread.cpu_context.ksp;
		}
	}

	stack = (unsigned long)sp;
	dump_mem("Stack: ", stack,
		 THREAD_SIZE + (unsigned long)tsk->thread_info);
	show_trace(tsk, sp, NULL);
}

void dump_stack(void)
{
	show_stack(NULL, NULL);
}
EXPORT_SYMBOL(dump_stack);

ATOMIC_NOTIFIER_HEAD(avr32_die_chain);

int register_die_notifier(struct notifier_block *nb)
{
	pr_debug("register_die_notifier: %p\n", nb);

	return atomic_notifier_chain_register(&avr32_die_chain, nb);
}
EXPORT_SYMBOL(register_die_notifier);

int unregister_die_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&avr32_die_chain, nb);
}
EXPORT_SYMBOL(unregister_die_notifier);

static DEFINE_SPINLOCK(die_lock);

void __die(const char *str, struct pt_regs *regs, unsigned long err,
	   const char *file, const char *func, unsigned long line)
{
	struct task_struct *tsk = current;
	static int die_counter;

	console_verbose();
	spin_lock_irq(&die_lock);
	bust_spinlocks(1);

	printk(KERN_ALERT "%s", str);
	if (file && func)
		printk(" in %s:%s, line %ld", file, func, line);
	printk("[#%d]:\n", ++die_counter);
	print_modules();
	show_regs(regs);
	printk("Process %s (pid: %d, stack limit = 0x%p)\n",
	       tsk->comm, tsk->pid, tsk->thread_info + 1);

	if (!user_mode(regs) || in_interrupt()) {
		dump_mem("Stack: ", regs->sp,
			 THREAD_SIZE + (unsigned long)tsk->thread_info);
	}

	bust_spinlocks(0);
	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

void __die_if_kernel(const char *str, struct pt_regs *regs, unsigned long err,
		     const char *file, const char *func, unsigned long line)
{
	if (!user_mode(regs))
		__die(str, regs, err, file, func, line);
}

asmlinkage void do_nmi(unsigned long ecr, struct pt_regs *regs)
{
#ifdef CONFIG_SUBARCH_AVR32B
	/*
	 * The exception entry always saves RSR_EX. For NMI, this is
	 * wrong; it should be RSR_NMI
	 */
	regs->sr = sysreg_read(RSR_NMI);
#endif

	printk("NMI taken!!!!\n");
	die("NMI", regs, ecr);
	BUG();
}

asmlinkage void do_critical_exception(unsigned long ecr, struct pt_regs *regs)
{
	printk("Unable to handle critical exception %lu at pc = %08lx!\n",
	       ecr, regs->pc);
	die("Oops", regs, ecr);
	BUG();
}

asmlinkage void do_address_exception(unsigned long ecr, struct pt_regs *regs)
{
	siginfo_t info;

	die_if_kernel("Oops: Address exception in kernel mode", regs, ecr);

#ifdef DEBUG
	if (ecr == ECR_ADDR_ALIGN_X)
		pr_debug("Instruction Address Exception at pc = %08lx\n",
			 regs->pc);
	else if (ecr == ECR_ADDR_ALIGN_R)
		pr_debug("Data Address Exception (Read) at pc = %08lx\n",
			 regs->pc);
	else if (ecr == ECR_ADDR_ALIGN_W)
		pr_debug("Data Address Exception (Write) at pc = %08lx\n",
			 regs->pc);
	else
		BUG();

	show_regs(regs);
#endif

	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRALN;
	info.si_addr = (void __user *)regs->pc;

	force_sig_info(SIGBUS, &info, current);
}

/* This way of handling undefined instructions is stolen from ARM */
static LIST_HEAD(undef_hook);
static spinlock_t undef_lock = SPIN_LOCK_UNLOCKED;

void register_undef_hook(struct undef_hook *hook)
{
	spin_lock_irq(&undef_lock);
	list_add(&hook->node, &undef_hook);
	spin_unlock_irq(&undef_lock);
}

void unregister_undef_hook(struct undef_hook *hook)
{
	spin_lock_irq(&undef_lock);
	list_del(&hook->node);
	spin_unlock_irq(&undef_lock);
}

static int do_cop_absent(u32 insn)
{
	int cop_nr;
	u32 cpucr;
	if ( (insn & 0xfdf00000) == 0xf1900000 )
		/* LDC0 */
		cop_nr = 0;
	else
		cop_nr = (insn >> 13) & 0x7;

	/* Try enabling the coprocessor */
	cpucr = sysreg_read(CPUCR);
	cpucr |= (1 << (24 + cop_nr));
	sysreg_write(CPUCR, cpucr);

	cpucr = sysreg_read(CPUCR);
	if ( !(cpucr & (1 << (24 + cop_nr))) ){
		printk("Coprocessor #%i not found!\n", cop_nr);
		return -1;
	}

	return 0;
}

#ifdef CONFIG_BUG
#ifdef CONFIG_DEBUG_BUGVERBOSE
static inline void do_bug_verbose(struct pt_regs *regs, u32 insn)
{
	char *file;
	u16 line;
	char c;

	if (__get_user(line, (u16 __user *)(regs->pc + 2)))
		return;
	if (__get_user(file, (char * __user *)(regs->pc + 4))
	    || (unsigned long)file < PAGE_OFFSET
	    || __get_user(c, file))
		file = "<bad filename>";

	printk(KERN_ALERT "kernel BUG at %s:%d!\n", file, line);
}
#else
static inline void do_bug_verbose(struct pt_regs *regs, u32 insn)
{

}
#endif
#endif

asmlinkage void do_illegal_opcode(unsigned long ecr, struct pt_regs *regs)
{
	u32 insn;
	struct undef_hook *hook;
	siginfo_t info;
	void __user *pc;

	if (!user_mode(regs))
		goto kernel_trap;

	local_irq_enable();

	pc = (void __user *)instruction_pointer(regs);
	if (__get_user(insn, (u32 __user *)pc))
		goto invalid_area;

        if (ecr == ECR_COPROC_ABSENT) {
		if (do_cop_absent(insn) == 0)
			return;
        }

	spin_lock_irq(&undef_lock);
	list_for_each_entry(hook, &undef_hook, node) {
		if ((insn & hook->insn_mask) == hook->insn_val) {
			if (hook->fn(regs, insn) == 0) {
				spin_unlock_irq(&undef_lock);
				return;
			}
		}
	}
	spin_unlock_irq(&undef_lock);

invalid_area:

#ifdef DEBUG
	printk("Illegal instruction at pc = %08lx\n", regs->pc);
	if (regs->pc < TASK_SIZE) {
		unsigned long ptbr, pgd, pte, *p;

		ptbr = sysreg_read(PTBR);
		p = (unsigned long *)ptbr;
		pgd = p[regs->pc >> 22];
		p = (unsigned long *)((pgd & 0x1ffff000) | 0x80000000);
		pte = p[(regs->pc >> 12) & 0x3ff];
		printk("page table: 0x%08lx -> 0x%08lx -> 0x%08lx\n", ptbr, pgd, pte);
	}
#endif

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_addr = (void __user *)regs->pc;
	switch (ecr) {
	case ECR_ILLEGAL_OPCODE:
	case ECR_UNIMPL_INSTRUCTION:
		info.si_code = ILL_ILLOPC;
		break;
	case ECR_PRIVILEGE_VIOLATION:
		info.si_code = ILL_PRVOPC;
		break;
	case ECR_COPROC_ABSENT:
		info.si_code = ILL_COPROC;
		break;
	default:
		BUG();
	}

	force_sig_info(SIGILL, &info, current);
	return;

kernel_trap:
#ifdef CONFIG_BUG
	if (__kernel_text_address(instruction_pointer(regs))) {
		insn = *(u16 *)instruction_pointer(regs);
		if (insn == AVR32_BUG_OPCODE) {
			do_bug_verbose(regs, insn);
			die("Kernel BUG", regs, 0);
			return;
		}
	}
#endif

	die("Oops: Illegal instruction in kernel code", regs, ecr);
}

asmlinkage void do_fpe(unsigned long ecr, struct pt_regs *regs)
{
	siginfo_t info;

	printk("Floating-point exception at pc = %08lx\n", regs->pc);

	/* We have no FPU... */
	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_addr = (void __user *)regs->pc;
	info.si_code = ILL_COPROC;

	force_sig_info(SIGILL, &info, current);
}


void __init trap_init(void)
{

}
