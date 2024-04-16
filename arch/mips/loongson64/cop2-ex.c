/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Lemote Corporation.
 *   written by Huacai Chen <chenhc@lemote.com>
 *
 * based on arch/mips/cavium-octeon/cpu.c
 * Copyright (C) 2009 Wind River Systems,
 *   written by Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/notifier.h>
#include <linux/ptrace.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>

#include <asm/fpu.h>
#include <asm/cop2.h>
#include <asm/inst.h>
#include <asm/branch.h>
#include <asm/current.h>
#include <asm/mipsregs.h>
#include <asm/unaligned-emul.h>

static int loongson_cu2_call(struct notifier_block *nfb, unsigned long action,
	void *data)
{
	unsigned int res, fpu_owned;
	unsigned long ra, value, value_next;
	union mips_instruction insn;
	int fr = !test_thread_flag(TIF_32BIT_FPREGS);
	struct pt_regs *regs = (struct pt_regs *)data;
	void __user *addr = (void __user *)regs->cp0_badvaddr;
	unsigned int __user *pc = (unsigned int __user *)exception_epc(regs);

	ra = regs->regs[31];
	__get_user(insn.word, pc);

	switch (action) {
	case CU2_EXCEPTION:
		preempt_disable();
		fpu_owned = __is_fpu_owner();
		if (!fr)
			set_c0_status(ST0_CU1 | ST0_CU2);
		else
			set_c0_status(ST0_CU1 | ST0_CU2 | ST0_FR);
		enable_fpu_hazard();
		KSTK_STATUS(current) |= (ST0_CU1 | ST0_CU2);
		if (fr)
			KSTK_STATUS(current) |= ST0_FR;
		else
			KSTK_STATUS(current) &= ~ST0_FR;
		/* If FPU is owned, we needn't init or restore fp */
		if (!fpu_owned) {
			set_thread_flag(TIF_USEDFPU);
			init_fp_ctx(current);
			_restore_fp(current);
		}
		preempt_enable();

		return NOTIFY_STOP;	/* Don't call default notifier */

	case CU2_LWC2_OP:
		if (insn.loongson3_lswc2_format.ls == 0)
			goto sigbus;

		if (insn.loongson3_lswc2_format.fr == 0) {	/* gslq */
			if (!access_ok(addr, 16))
				goto sigbus;

			LoadDW(addr, value, res);
			if (res)
				goto fault;

			LoadDW(addr + 8, value_next, res);
			if (res)
				goto fault;

			regs->regs[insn.loongson3_lswc2_format.rt] = value;
			regs->regs[insn.loongson3_lswc2_format.rq] = value_next;
			compute_return_epc(regs);
		} else {					/* gslqc1 */
			if (!access_ok(addr, 16))
				goto sigbus;

			lose_fpu(1);
			LoadDW(addr, value, res);
			if (res)
				goto fault;

			LoadDW(addr + 8, value_next, res);
			if (res)
				goto fault;

			set_fpr64(&current->thread.fpu.fpr[insn.loongson3_lswc2_format.rt], 0, value);
			set_fpr64(&current->thread.fpu.fpr[insn.loongson3_lswc2_format.rq], 0, value_next);
			compute_return_epc(regs);
			own_fpu(1);
		}
		return NOTIFY_STOP;	/* Don't call default notifier */

	case CU2_SWC2_OP:
		if (insn.loongson3_lswc2_format.ls == 0)
			goto sigbus;

		if (insn.loongson3_lswc2_format.fr == 0) {	/* gssq */
			if (!access_ok(addr, 16))
				goto sigbus;

			/* write upper 8 bytes first */
			value_next = regs->regs[insn.loongson3_lswc2_format.rq];

			StoreDW(addr + 8, value_next, res);
			if (res)
				goto fault;
			value = regs->regs[insn.loongson3_lswc2_format.rt];

			StoreDW(addr, value, res);
			if (res)
				goto fault;

			compute_return_epc(regs);
		} else {					/* gssqc1 */
			if (!access_ok(addr, 16))
				goto sigbus;

			lose_fpu(1);
			value_next = get_fpr64(&current->thread.fpu.fpr[insn.loongson3_lswc2_format.rq], 0);

			StoreDW(addr + 8, value_next, res);
			if (res)
				goto fault;

			value = get_fpr64(&current->thread.fpu.fpr[insn.loongson3_lswc2_format.rt], 0);

			StoreDW(addr, value, res);
			if (res)
				goto fault;

			compute_return_epc(regs);
			own_fpu(1);
		}
		return NOTIFY_STOP;	/* Don't call default notifier */

	case CU2_LDC2_OP:
		switch (insn.loongson3_lsdc2_format.opcode1) {
		/*
		 * Loongson-3 overridden ldc2 instructions.
		 * opcode1              instruction
		 *   0x1          gslhx: load 2 bytes to GPR
		 *   0x2          gslwx: load 4 bytes to GPR
		 *   0x3          gsldx: load 8 bytes to GPR
		 *   0x6	  gslwxc1: load 4 bytes to FPR
		 *   0x7	  gsldxc1: load 8 bytes to FPR
		 */
		case 0x1:
			if (!access_ok(addr, 2))
				goto sigbus;

			LoadHW(addr, value, res);
			if (res)
				goto fault;

			compute_return_epc(regs);
			regs->regs[insn.loongson3_lsdc2_format.rt] = value;
			break;
		case 0x2:
			if (!access_ok(addr, 4))
				goto sigbus;

			LoadW(addr, value, res);
			if (res)
				goto fault;

			compute_return_epc(regs);
			regs->regs[insn.loongson3_lsdc2_format.rt] = value;
			break;
		case 0x3:
			if (!access_ok(addr, 8))
				goto sigbus;

			LoadDW(addr, value, res);
			if (res)
				goto fault;

			compute_return_epc(regs);
			regs->regs[insn.loongson3_lsdc2_format.rt] = value;
			break;
		case 0x6:
			die_if_kernel("Unaligned FP access in kernel code", regs);
			BUG_ON(!used_math());
			if (!access_ok(addr, 4))
				goto sigbus;

			lose_fpu(1);
			LoadW(addr, value, res);
			if (res)
				goto fault;

			set_fpr64(&current->thread.fpu.fpr[insn.loongson3_lsdc2_format.rt], 0, value);
			compute_return_epc(regs);
			own_fpu(1);

			break;
		case 0x7:
			die_if_kernel("Unaligned FP access in kernel code", regs);
			BUG_ON(!used_math());
			if (!access_ok(addr, 8))
				goto sigbus;

			lose_fpu(1);
			LoadDW(addr, value, res);
			if (res)
				goto fault;

			set_fpr64(&current->thread.fpu.fpr[insn.loongson3_lsdc2_format.rt], 0, value);
			compute_return_epc(regs);
			own_fpu(1);
			break;

		}
		return NOTIFY_STOP;	/* Don't call default notifier */

	case CU2_SDC2_OP:
		switch (insn.loongson3_lsdc2_format.opcode1) {
		/*
		 * Loongson-3 overridden sdc2 instructions.
		 * opcode1              instruction
		 *   0x1          gsshx: store 2 bytes from GPR
		 *   0x2          gsswx: store 4 bytes from GPR
		 *   0x3          gssdx: store 8 bytes from GPR
		 *   0x6          gsswxc1: store 4 bytes from FPR
		 *   0x7          gssdxc1: store 8 bytes from FPR
		 */
		case 0x1:
			if (!access_ok(addr, 2))
				goto sigbus;

			compute_return_epc(regs);
			value = regs->regs[insn.loongson3_lsdc2_format.rt];

			StoreHW(addr, value, res);
			if (res)
				goto fault;

			break;
		case 0x2:
			if (!access_ok(addr, 4))
				goto sigbus;

			compute_return_epc(regs);
			value = regs->regs[insn.loongson3_lsdc2_format.rt];

			StoreW(addr, value, res);
			if (res)
				goto fault;

			break;
		case 0x3:
			if (!access_ok(addr, 8))
				goto sigbus;

			compute_return_epc(regs);
			value = regs->regs[insn.loongson3_lsdc2_format.rt];

			StoreDW(addr, value, res);
			if (res)
				goto fault;

			break;

		case 0x6:
			die_if_kernel("Unaligned FP access in kernel code", regs);
			BUG_ON(!used_math());

			if (!access_ok(addr, 4))
				goto sigbus;

			lose_fpu(1);
			value = get_fpr64(&current->thread.fpu.fpr[insn.loongson3_lsdc2_format.rt], 0);

			StoreW(addr, value, res);
			if (res)
				goto fault;

			compute_return_epc(regs);
			own_fpu(1);

			break;
		case 0x7:
			die_if_kernel("Unaligned FP access in kernel code", regs);
			BUG_ON(!used_math());

			if (!access_ok(addr, 8))
				goto sigbus;

			lose_fpu(1);
			value = get_fpr64(&current->thread.fpu.fpr[insn.loongson3_lsdc2_format.rt], 0);

			StoreDW(addr, value, res);
			if (res)
				goto fault;

			compute_return_epc(regs);
			own_fpu(1);

			break;
		}
		return NOTIFY_STOP;	/* Don't call default notifier */
	}

	return NOTIFY_OK;		/* Let default notifier send signals */

fault:
	/* roll back jump/branch */
	regs->regs[31] = ra;
	regs->cp0_epc = (unsigned long)pc;
	/* Did we have an exception handler installed? */
	if (fixup_exception(regs))
		return NOTIFY_STOP;	/* Don't call default notifier */

	die_if_kernel("Unhandled kernel unaligned access", regs);
	force_sig(SIGSEGV);

	return NOTIFY_STOP;	/* Don't call default notifier */

sigbus:
	die_if_kernel("Unhandled kernel unaligned access", regs);
	force_sig(SIGBUS);

	return NOTIFY_STOP;	/* Don't call default notifier */
}

static int __init loongson_cu2_setup(void)
{
	return cu2_notifier(loongson_cu2_call, 0);
}
early_initcall(loongson_cu2_setup);
