// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/module.h>
#include <linux/personality.h>
#include <linux/kallsyms.h>
#include <linux/hardirq.h>
#include <linux/kdebug.h>
#include <linux/sched/task_stack.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>

#include <asm/proc-fns.h>
#include <asm/unistd.h>
#include <asm/fpu.h>

#include <linux/ptrace.h>
#include <nds32_intrinsic.h>

extern void show_pte(struct mm_struct *mm, unsigned long addr);

/*
 * Dump out the contents of some memory nicely...
 */
void dump_mem(const char *lvl, unsigned long bottom, unsigned long top)
{
	unsigned long first;
	int i;

	pr_emerg("%s(0x%08lx to 0x%08lx)\n", lvl, bottom, top);

	for (first = bottom & ~31; first < top; first += 32) {
		unsigned long p;
		char str[sizeof(" 12345678") * 8 + 1];

		memset(str, ' ', sizeof(str));
		str[sizeof(str) - 1] = '\0';

		for (p = first, i = 0; i < 8 && p < top; i++, p += 4) {
			if (p >= bottom && p < top) {
				unsigned long val;

				if (get_kernel_nofault(val,
						(unsigned long *)p) == 0)
					sprintf(str + i * 9, " %08lx", val);
				else
					sprintf(str + i * 9, " ????????");
			}
		}
		pr_emerg("%s%04lx:%s\n", lvl, first & 0xffff, str);
	}
}

EXPORT_SYMBOL(dump_mem);

#define LOOP_TIMES (100)
static void __dump(struct task_struct *tsk, unsigned long *base_reg,
		   const char *loglvl)
{
	unsigned long ret_addr;
	int cnt = LOOP_TIMES, graph = 0;
	printk("%sCall Trace:\n", loglvl);
	if (!IS_ENABLED(CONFIG_FRAME_POINTER)) {
		while (!kstack_end(base_reg)) {
			ret_addr = *base_reg++;
			if (__kernel_text_address(ret_addr)) {
				ret_addr = ftrace_graph_ret_addr(
						tsk, &graph, ret_addr, NULL);
				print_ip_sym(loglvl, ret_addr);
			}
			if (--cnt < 0)
				break;
		}
	} else {
		while (!kstack_end((void *)base_reg) &&
		       !((unsigned long)base_reg & 0x3) &&
		       ((unsigned long)base_reg >= TASK_SIZE)) {
			unsigned long next_fp;
			ret_addr = base_reg[LP_OFFSET];
			next_fp = base_reg[FP_OFFSET];
			if (__kernel_text_address(ret_addr)) {

				ret_addr = ftrace_graph_ret_addr(
						tsk, &graph, ret_addr, NULL);
				print_ip_sym(loglvl, ret_addr);
			}
			if (--cnt < 0)
				break;
			base_reg = (unsigned long *)next_fp;
		}
	}
	printk("%s\n", loglvl);
}

void show_stack(struct task_struct *tsk, unsigned long *sp, const char *loglvl)
{
	unsigned long *base_reg;

	if (!tsk)
		tsk = current;
	if (!IS_ENABLED(CONFIG_FRAME_POINTER)) {
		if (tsk != current)
			base_reg = (unsigned long *)(tsk->thread.cpu_context.sp);
		else
			__asm__ __volatile__("\tori\t%0, $sp, #0\n":"=r"(base_reg));
	} else {
		if (tsk != current)
			base_reg = (unsigned long *)(tsk->thread.cpu_context.fp);
		else
			__asm__ __volatile__("\tori\t%0, $fp, #0\n":"=r"(base_reg));
	}
	__dump(tsk, base_reg, loglvl);
	barrier();
}

DEFINE_SPINLOCK(die_lock);

/*
 * This function is protected against re-entrancy.
 */
void die(const char *str, struct pt_regs *regs, int err)
{
	struct task_struct *tsk = current;
	static int die_counter;

	console_verbose();
	spin_lock_irq(&die_lock);
	bust_spinlocks(1);

	pr_emerg("Internal error: %s: %x [#%d]\n", str, err, ++die_counter);
	print_modules();
	pr_emerg("CPU: %i\n", smp_processor_id());
	show_regs(regs);
	pr_emerg("Process %s (pid: %d, stack limit = 0x%p)\n",
		 tsk->comm, tsk->pid, end_of_stack(tsk));

	if (!user_mode(regs) || in_interrupt()) {
		dump_mem("Stack: ", regs->sp, (regs->sp + PAGE_SIZE) & PAGE_MASK);
		dump_stack();
	}

	bust_spinlocks(0);
	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

EXPORT_SYMBOL(die);

void die_if_kernel(const char *str, struct pt_regs *regs, int err)
{
	if (user_mode(regs))
		return;

	die(str, regs, err);
}

int bad_syscall(int n, struct pt_regs *regs)
{
	if (current->personality != PER_LINUX) {
		send_sig(SIGSEGV, current, 1);
		return regs->uregs[0];
	}

	force_sig_fault(SIGILL, ILL_ILLTRP,
			(void __user *)instruction_pointer(regs) - 4);
	die_if_kernel("Oops - bad syscall", regs, n);
	return regs->uregs[0];
}

void __pte_error(const char *file, int line, unsigned long val)
{
	pr_emerg("%s:%d: bad pte %08lx.\n", file, line, val);
}

void __pmd_error(const char *file, int line, unsigned long val)
{
	pr_emerg("%s:%d: bad pmd %08lx.\n", file, line, val);
}

void __pgd_error(const char *file, int line, unsigned long val)
{
	pr_emerg("%s:%d: bad pgd %08lx.\n", file, line, val);
}

extern char *exception_vector, *exception_vector_end;
void __init early_trap_init(void)
{
	unsigned long ivb = 0;
	unsigned long base = PAGE_OFFSET;

	memcpy((unsigned long *)base, (unsigned long *)&exception_vector,
	       ((unsigned long)&exception_vector_end -
		(unsigned long)&exception_vector));
	ivb = __nds32__mfsr(NDS32_SR_IVB);
	/* Check platform support. */
	if (((ivb & IVB_mskNIVIC) >> IVB_offNIVIC) < 2)
		panic
		    ("IVIC mode is not allowed on the platform with interrupt controller\n");
	__nds32__mtsr((ivb & ~IVB_mskESZ) | (IVB_valESZ16 << IVB_offESZ) |
		      IVB_BASE, NDS32_SR_IVB);
	__nds32__mtsr(INT_MASK_INITAIAL_VAL, NDS32_SR_INT_MASK);

	/*
	 * 0x800 = 128 vectors * 16byte.
	 * It should be enough to flush a page.
	 */
	cpu_cache_wbinval_page(base, true);
}

static void send_sigtrap(struct pt_regs *regs, int error_code, int si_code)
{
	struct task_struct *tsk = current;

	tsk->thread.trap_no = ENTRY_DEBUG_RELATED;
	tsk->thread.error_code = error_code;

	force_sig_fault(SIGTRAP, si_code,
			(void __user *)instruction_pointer(regs));
}

void do_debug_trap(unsigned long entry, unsigned long addr,
		   unsigned long type, struct pt_regs *regs)
{
	if (notify_die(DIE_OOPS, "Oops", regs, addr, type, SIGTRAP)
	    == NOTIFY_STOP)
		return;

	if (user_mode(regs)) {
		/* trap_signal */
		send_sigtrap(regs, 0, TRAP_BRKPT);
	} else {
		/* kernel_trap */
		if (!fixup_exception(regs))
			die("unexpected kernel_trap", regs, 0);
	}
}

void unhandled_interruption(struct pt_regs *regs)
{
	pr_emerg("unhandled_interruption\n");
	show_regs(regs);
	if (!user_mode(regs))
		do_exit(SIGKILL);
	force_sig(SIGKILL);
}

void unhandled_exceptions(unsigned long entry, unsigned long addr,
			  unsigned long type, struct pt_regs *regs)
{
	pr_emerg("Unhandled Exception: entry: %lx addr:%lx itype:%lx\n", entry,
		 addr, type);
	show_regs(regs);
	if (!user_mode(regs))
		do_exit(SIGKILL);
	force_sig(SIGKILL);
}

extern int do_page_fault(unsigned long entry, unsigned long addr,
			 unsigned int error_code, struct pt_regs *regs);

/*
 * 2:DEF dispatch for TLB MISC exception handler
*/

void do_dispatch_tlb_misc(unsigned long entry, unsigned long addr,
			  unsigned long type, struct pt_regs *regs)
{
	type = type & (ITYPE_mskINST | ITYPE_mskETYPE);
	if ((type & ITYPE_mskETYPE) < 5) {
		/* Permission exceptions */
		do_page_fault(entry, addr, type, regs);
	} else
		unhandled_exceptions(entry, addr, type, regs);
}

void do_revinsn(struct pt_regs *regs)
{
	pr_emerg("Reserved Instruction\n");
	show_regs(regs);
	if (!user_mode(regs))
		do_exit(SIGILL);
	force_sig(SIGILL);
}

#ifdef CONFIG_ALIGNMENT_TRAP
extern int unalign_access_mode;
extern int do_unaligned_access(unsigned long addr, struct pt_regs *regs);
#endif
void do_dispatch_general(unsigned long entry, unsigned long addr,
			 unsigned long itype, struct pt_regs *regs,
			 unsigned long oipc)
{
	unsigned int swid = itype >> ITYPE_offSWID;
	unsigned long type = itype & (ITYPE_mskINST | ITYPE_mskETYPE);
	if (type == ETYPE_ALIGNMENT_CHECK) {
#ifdef CONFIG_ALIGNMENT_TRAP
		/* Alignment check */
		if (user_mode(regs) && unalign_access_mode) {
			int ret;
			ret = do_unaligned_access(addr, regs);

			if (ret == 0)
				return;

			if (ret == -EFAULT)
				pr_emerg
				    ("Unhandled unaligned access exception\n");
		}
#endif
		do_page_fault(entry, addr, type, regs);
	} else if (type == ETYPE_RESERVED_INSTRUCTION) {
		/* Reserved instruction */
		do_revinsn(regs);
	} else if (type == ETYPE_COPROCESSOR) {
		/* Coprocessor */
#if IS_ENABLED(CONFIG_FPU)
		unsigned int fucop_exist = __nds32__mfsr(NDS32_SR_FUCOP_EXIST);
		unsigned int cpid = ((itype & ITYPE_mskCPID) >> ITYPE_offCPID);

		if ((cpid == FPU_CPID) &&
		    (fucop_exist & FUCOP_EXIST_mskCP0ISFPU)) {
			unsigned int subtype = (itype & ITYPE_mskSTYPE);

			if (true == do_fpu_exception(subtype, regs))
				return;
		}
#endif
		unhandled_exceptions(entry, addr, type, regs);
	} else if (type == ETYPE_TRAP && swid == SWID_RAISE_INTERRUPT_LEVEL) {
		/* trap, used on v3 EDM target debugging workaround */
		/*
		 * DIPC(OIPC) is passed as parameter before
		 * interrupt is enabled, so the DIPC will not be corrupted
		 * even though interrupts are coming in
		 */
		/*
		 * 1. update ipc
		 * 2. update pt_regs ipc with oipc
		 * 3. update pt_regs ipsw (clear DEX)
		 */
		__asm__ volatile ("mtsr %0, $IPC\n\t"::"r" (oipc));
		regs->ipc = oipc;
		if (regs->pipsw & PSW_mskDEX) {
			pr_emerg
			    ("Nested Debug exception is possibly happened\n");
			pr_emerg("ipc:%08x pipc:%08x\n",
				 (unsigned int)regs->ipc,
				 (unsigned int)regs->pipc);
		}
		do_debug_trap(entry, addr, itype, regs);
		regs->ipsw &= ~PSW_mskDEX;
	} else
		unhandled_exceptions(entry, addr, type, regs);
}
