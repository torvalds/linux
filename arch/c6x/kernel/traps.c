/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2006, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/sched/debug.h>
#include <linux/kallsyms.h>
#include <linux/bug.h>

#include <asm/soc.h>
#include <asm/special_insns.h>
#include <asm/traps.h>

int (*c6x_nmi_handler)(struct pt_regs *regs);

void __init trap_init(void)
{
	ack_exception(EXCEPT_TYPE_NXF);
	ack_exception(EXCEPT_TYPE_EXC);
	ack_exception(EXCEPT_TYPE_IXF);
	ack_exception(EXCEPT_TYPE_SXF);
	enable_exception();
}

void show_regs(struct pt_regs *regs)
{
	pr_err("\n");
	show_regs_print_info(KERN_ERR);
	pr_err("PC: %08lx SP: %08lx\n", regs->pc, regs->sp);
	pr_err("Status: %08lx ORIG_A4: %08lx\n", regs->csr, regs->orig_a4);
	pr_err("A0: %08lx  B0: %08lx\n", regs->a0, regs->b0);
	pr_err("A1: %08lx  B1: %08lx\n", regs->a1, regs->b1);
	pr_err("A2: %08lx  B2: %08lx\n", regs->a2, regs->b2);
	pr_err("A3: %08lx  B3: %08lx\n", regs->a3, regs->b3);
	pr_err("A4: %08lx  B4: %08lx\n", regs->a4, regs->b4);
	pr_err("A5: %08lx  B5: %08lx\n", regs->a5, regs->b5);
	pr_err("A6: %08lx  B6: %08lx\n", regs->a6, regs->b6);
	pr_err("A7: %08lx  B7: %08lx\n", regs->a7, regs->b7);
	pr_err("A8: %08lx  B8: %08lx\n", regs->a8, regs->b8);
	pr_err("A9: %08lx  B9: %08lx\n", regs->a9, regs->b9);
	pr_err("A10: %08lx  B10: %08lx\n", regs->a10, regs->b10);
	pr_err("A11: %08lx  B11: %08lx\n", regs->a11, regs->b11);
	pr_err("A12: %08lx  B12: %08lx\n", regs->a12, regs->b12);
	pr_err("A13: %08lx  B13: %08lx\n", regs->a13, regs->b13);
	pr_err("A14: %08lx  B14: %08lx\n", regs->a14, regs->dp);
	pr_err("A15: %08lx  B15: %08lx\n", regs->a15, regs->sp);
	pr_err("A16: %08lx  B16: %08lx\n", regs->a16, regs->b16);
	pr_err("A17: %08lx  B17: %08lx\n", regs->a17, regs->b17);
	pr_err("A18: %08lx  B18: %08lx\n", regs->a18, regs->b18);
	pr_err("A19: %08lx  B19: %08lx\n", regs->a19, regs->b19);
	pr_err("A20: %08lx  B20: %08lx\n", regs->a20, regs->b20);
	pr_err("A21: %08lx  B21: %08lx\n", regs->a21, regs->b21);
	pr_err("A22: %08lx  B22: %08lx\n", regs->a22, regs->b22);
	pr_err("A23: %08lx  B23: %08lx\n", regs->a23, regs->b23);
	pr_err("A24: %08lx  B24: %08lx\n", regs->a24, regs->b24);
	pr_err("A25: %08lx  B25: %08lx\n", regs->a25, regs->b25);
	pr_err("A26: %08lx  B26: %08lx\n", regs->a26, regs->b26);
	pr_err("A27: %08lx  B27: %08lx\n", regs->a27, regs->b27);
	pr_err("A28: %08lx  B28: %08lx\n", regs->a28, regs->b28);
	pr_err("A29: %08lx  B29: %08lx\n", regs->a29, regs->b29);
	pr_err("A30: %08lx  B30: %08lx\n", regs->a30, regs->b30);
	pr_err("A31: %08lx  B31: %08lx\n", regs->a31, regs->b31);
}

void die(char *str, struct pt_regs *fp, int nr)
{
	console_verbose();
	pr_err("%s: %08x\n", str, nr);
	show_regs(fp);

	pr_err("Process %s (pid: %d, stackpage=%08lx)\n",
	       current->comm, current->pid, (PAGE_SIZE +
					     (unsigned long) current));

	dump_stack();
	while (1)
		;
}

static void die_if_kernel(char *str, struct pt_regs *fp, int nr)
{
	if (user_mode(fp))
		return;

	die(str, fp, nr);
}


/* Internal exceptions */
static struct exception_info iexcept_table[10] = {
	{ "Oops - instruction fetch", SIGBUS, BUS_ADRERR },
	{ "Oops - fetch packet", SIGBUS, BUS_ADRERR },
	{ "Oops - execute packet", SIGILL, ILL_ILLOPC },
	{ "Oops - undefined instruction", SIGILL, ILL_ILLOPC },
	{ "Oops - resource conflict", SIGILL, ILL_ILLOPC },
	{ "Oops - resource access", SIGILL, ILL_PRVREG },
	{ "Oops - privilege", SIGILL, ILL_PRVOPC },
	{ "Oops - loops buffer", SIGILL, ILL_ILLOPC },
	{ "Oops - software exception", SIGILL, ILL_ILLTRP },
	{ "Oops - unknown exception", SIGILL, ILL_ILLOPC }
};

/* External exceptions */
static struct exception_info eexcept_table[128] = {
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },

	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },

	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },

	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - external exception", SIGBUS, BUS_ADRERR },
	{ "Oops - CPU memory protection fault", SIGSEGV, SEGV_ACCERR },
	{ "Oops - CPU memory protection fault in L1P", SIGSEGV, SEGV_ACCERR },
	{ "Oops - DMA memory protection fault in L1P", SIGSEGV, SEGV_ACCERR },
	{ "Oops - CPU memory protection fault in L1D", SIGSEGV, SEGV_ACCERR },
	{ "Oops - DMA memory protection fault in L1D", SIGSEGV, SEGV_ACCERR },
	{ "Oops - CPU memory protection fault in L2", SIGSEGV, SEGV_ACCERR },
	{ "Oops - DMA memory protection fault in L2", SIGSEGV, SEGV_ACCERR },
	{ "Oops - EMC CPU memory protection fault", SIGSEGV, SEGV_ACCERR },
	{ "Oops - EMC bus error", SIGBUS, BUS_ADRERR }
};

static void do_trap(struct exception_info *except_info, struct pt_regs *regs)
{
	unsigned long addr = instruction_pointer(regs);
	siginfo_t info;

	if (except_info->code != TRAP_BRKPT)
		pr_err("TRAP: %s PC[0x%lx] signo[%d] code[%d]\n",
		       except_info->kernel_str, regs->pc,
		       except_info->signo, except_info->code);

	die_if_kernel(except_info->kernel_str, regs, addr);

	info.si_signo = except_info->signo;
	info.si_errno = 0;
	info.si_code  = except_info->code;
	info.si_addr  = (void __user *)addr;

	force_sig_info(except_info->signo, &info, current);
}

/*
 * Process an internal exception (non maskable)
 */
static int process_iexcept(struct pt_regs *regs)
{
	unsigned int iexcept_report = get_iexcept();
	unsigned int iexcept_num;

	ack_exception(EXCEPT_TYPE_IXF);

	pr_err("IEXCEPT: PC[0x%lx]\n", regs->pc);

	while (iexcept_report) {
		iexcept_num = __ffs(iexcept_report);
		iexcept_report &= ~(1 << iexcept_num);
		set_iexcept(iexcept_report);
		if (*(unsigned int *)regs->pc == BKPT_OPCODE) {
			/* This is a breakpoint */
			struct exception_info bkpt_exception = {
				"Oops - undefined instruction",
				  SIGTRAP, TRAP_BRKPT
			};
			do_trap(&bkpt_exception, regs);
			iexcept_report &= ~(0xFF);
			set_iexcept(iexcept_report);
			continue;
		}

		do_trap(&iexcept_table[iexcept_num], regs);
	}
	return 0;
}

/*
 * Process an external exception (maskable)
 */
static void process_eexcept(struct pt_regs *regs)
{
	int evt;

	pr_err("EEXCEPT: PC[0x%lx]\n", regs->pc);

	while ((evt = soc_get_exception()) >= 0)
		do_trap(&eexcept_table[evt], regs);

	ack_exception(EXCEPT_TYPE_EXC);
}

/*
 * Main exception processing
 */
asmlinkage int process_exception(struct pt_regs *regs)
{
	unsigned int type;
	unsigned int type_num;
	unsigned int ie_num = 9; /* default is unknown exception */

	while ((type = get_except_type()) != 0) {
		type_num = fls(type) - 1;

		switch (type_num) {
		case EXCEPT_TYPE_NXF:
			ack_exception(EXCEPT_TYPE_NXF);
			if (c6x_nmi_handler)
				(c6x_nmi_handler)(regs);
			else
				pr_alert("NMI interrupt!\n");
			break;

		case EXCEPT_TYPE_IXF:
			if (process_iexcept(regs))
				return 1;
			break;

		case EXCEPT_TYPE_EXC:
			process_eexcept(regs);
			break;

		case EXCEPT_TYPE_SXF:
			ie_num = 8;
		default:
			ack_exception(type_num);
			do_trap(&iexcept_table[ie_num], regs);
			break;
		}
	}
	return 0;
}

static int kstack_depth_to_print = 48;

static void show_trace(unsigned long *stack, unsigned long *endstack)
{
	unsigned long addr;
	int i;

	pr_debug("Call trace:");
	i = 0;
	while (stack + 1 <= endstack) {
		addr = *stack++;
		/*
		 * If the address is either in the text segment of the
		 * kernel, or in the region which contains vmalloc'ed
		 * memory, it *may* be the address of a calling
		 * routine; if so, print it so that someone tracing
		 * down the cause of the crash will be able to figure
		 * out the call path that was taken.
		 */
		if (__kernel_text_address(addr)) {
#ifndef CONFIG_KALLSYMS
			if (i % 5 == 0)
				pr_debug("\n	    ");
#endif
			pr_debug(" [<%08lx>]", addr);
			print_symbol(" %s\n", addr);
			i++;
		}
	}
	pr_debug("\n");
}

void show_stack(struct task_struct *task, unsigned long *stack)
{
	unsigned long *p, *endstack;
	int i;

	if (!stack) {
		if (task && task != current)
			/* We know this is a kernel stack,
			   so this is the start/end */
			stack = (unsigned long *)thread_saved_ksp(task);
		else
			stack = (unsigned long *)&stack;
	}
	endstack = (unsigned long *)(((unsigned long)stack + THREAD_SIZE - 1)
				     & -THREAD_SIZE);

	pr_debug("Stack from %08lx:", (unsigned long)stack);
	for (i = 0, p = stack; i < kstack_depth_to_print; i++) {
		if (p + 1 > endstack)
			break;
		if (i % 8 == 0)
			pr_cont("\n	    ");
		pr_cont(" %08lx", *p++);
	}
	pr_cont("\n");
	show_trace(stack, endstack);
}

int is_valid_bugaddr(unsigned long addr)
{
	return __kernel_text_address(addr);
}
