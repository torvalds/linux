/*
 * arch/score/kernel/ptrace.c
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/kernel.h>
#include <linux/ptrace.h>

#include <asm/uaccess.h>

static int is_16bitinsn(unsigned long insn)
{
	if ((insn & INSN32_MASK) == INSN32_MASK)
		return 0;
	else
		return 1;
}

int
read_tsk_long(struct task_struct *child,
		unsigned long addr, unsigned long *res)
{
	int copied;

	copied = access_process_vm(child, addr, res, sizeof(*res), 0);

	return copied != sizeof(*res) ? -EIO : 0;
}

int
read_tsk_short(struct task_struct *child,
		unsigned long addr, unsigned short *res)
{
	int copied;

	copied = access_process_vm(child, addr, res, sizeof(*res), 0);

	return copied != sizeof(*res) ? -EIO : 0;
}

static int
write_tsk_short(struct task_struct *child,
		unsigned long addr, unsigned short val)
{
	int copied;

	copied = access_process_vm(child, addr, &val, sizeof(val), 1);

	return copied != sizeof(val) ? -EIO : 0;
}

static int
write_tsk_long(struct task_struct *child,
		unsigned long addr, unsigned long val)
{
	int copied;

	copied = access_process_vm(child, addr, &val, sizeof(val), 1);

	return copied != sizeof(val) ? -EIO : 0;
}

/*
 * Get all user integer registers.
 */
static int ptrace_getregs(struct task_struct *tsk, void __user *uregs)
{
	struct pt_regs *regs = task_pt_regs(tsk);

	return copy_to_user(uregs, regs, sizeof(struct pt_regs)) ? -EFAULT : 0;
}

/*
 * Set all user integer registers.
 */
static int ptrace_setregs(struct task_struct *tsk, void __user *uregs)
{
	struct pt_regs newregs;
	int ret;

	ret = -EFAULT;
	if (copy_from_user(&newregs, uregs, sizeof(struct pt_regs)) == 0) {
		struct pt_regs *regs = task_pt_regs(tsk);
		*regs = newregs;
		ret = 0;
	}

	return ret;
}

void user_enable_single_step(struct task_struct *child)
{
	/* far_epc is the target of branch */
	unsigned int epc, far_epc = 0;
	unsigned long epc_insn, far_epc_insn;
	int ninsn_type;			/* next insn type 0=16b, 1=32b */
	unsigned int tmp, tmp2;
	struct pt_regs *regs = task_pt_regs(child);
	child->thread.single_step = 1;
	child->thread.ss_nextcnt = 1;
	epc = regs->cp0_epc;

	read_tsk_long(child, epc, &epc_insn);

	if (is_16bitinsn(epc_insn)) {
		if ((epc_insn & J16M) == J16) {
			tmp = epc_insn & 0xFFE;
			epc = (epc & 0xFFFFF000) | tmp;
		} else if ((epc_insn & B16M) == B16) {
			child->thread.ss_nextcnt = 2;
			tmp = (epc_insn & 0xFF) << 1;
			tmp = tmp << 23;
			tmp = (unsigned int)((int) tmp >> 23);
			far_epc = epc + tmp;
			epc += 2;
		} else if ((epc_insn & BR16M) == BR16) {
			child->thread.ss_nextcnt = 2;
			tmp = (epc_insn >> 4) & 0xF;
			far_epc = regs->regs[tmp];
			epc += 2;
		} else
			epc += 2;
	} else {
		if ((epc_insn & J32M) == J32) {
			tmp = epc_insn & 0x03FFFFFE;
			tmp2 = tmp & 0x7FFF;
			tmp = (((tmp >> 16) & 0x3FF) << 15) | tmp2;
			epc = (epc & 0xFFC00000) | tmp;
		} else if ((epc_insn & B32M) == B32) {
			child->thread.ss_nextcnt = 2;
			tmp = epc_insn & 0x03FFFFFE;	/* discard LK bit */
			tmp2 = tmp & 0x3FF;
			tmp = (((tmp >> 16) & 0x3FF) << 10) | tmp2; /* 20bit */
			tmp = tmp << 12;
			tmp = (unsigned int)((int) tmp >> 12);
			far_epc = epc + tmp;
			epc += 4;
		} else if ((epc_insn & BR32M) == BR32) {
			child->thread.ss_nextcnt = 2;
			tmp = (epc_insn >> 16) & 0x1F;
			far_epc = regs->regs[tmp];
			epc += 4;
		} else
			epc += 4;
	}

	if (child->thread.ss_nextcnt == 1) {
		read_tsk_long(child, epc, &epc_insn);

		if (is_16bitinsn(epc_insn)) {
			write_tsk_short(child, epc, SINGLESTEP16_INSN);
			ninsn_type = 0;
		} else {
			write_tsk_long(child, epc, SINGLESTEP32_INSN);
			ninsn_type = 1;
		}

		if (ninsn_type == 0) {  /* 16bits */
			child->thread.insn1_type = 0;
			child->thread.addr1 = epc;
			 /* the insn may have 32bit data */
			child->thread.insn1 = (short)epc_insn;
		} else {
			child->thread.insn1_type = 1;
			child->thread.addr1 = epc;
			child->thread.insn1 = epc_insn;
		}
	} else {
		/* branch! have two target child->thread.ss_nextcnt=2 */
		read_tsk_long(child, epc, &epc_insn);
		read_tsk_long(child, far_epc, &far_epc_insn);
		if (is_16bitinsn(epc_insn)) {
			write_tsk_short(child, epc, SINGLESTEP16_INSN);
			ninsn_type = 0;
		} else {
			write_tsk_long(child, epc, SINGLESTEP32_INSN);
			ninsn_type = 1;
		}

		if (ninsn_type == 0) {  /* 16bits */
			child->thread.insn1_type = 0;
			child->thread.addr1 = epc;
			 /* the insn may have 32bit data */
			child->thread.insn1 = (short)epc_insn;
		} else {
			child->thread.insn1_type = 1;
			child->thread.addr1 = epc;
			child->thread.insn1 = epc_insn;
		}

		if (is_16bitinsn(far_epc_insn)) {
			write_tsk_short(child, far_epc, SINGLESTEP16_INSN);
			ninsn_type = 0;
		} else {
			write_tsk_long(child, far_epc, SINGLESTEP32_INSN);
			ninsn_type = 1;
		}

		if (ninsn_type == 0) {  /* 16bits */
			child->thread.insn2_type = 0;
			child->thread.addr2 = far_epc;
			 /* the insn may have 32bit data */
			child->thread.insn2 = (short)far_epc_insn;
		} else {
			child->thread.insn2_type = 1;
			child->thread.addr2 = far_epc;
			child->thread.insn2 = far_epc_insn;
		}
	}
}

void user_disable_single_step(struct task_struct *child)
{
	if (child->thread.insn1_type == 0)
		write_tsk_short(child, child->thread.addr1,
				child->thread.insn1);

	if (child->thread.insn1_type == 1)
		write_tsk_long(child, child->thread.addr1,
				child->thread.insn1);

	if (child->thread.ss_nextcnt == 2) {	/* branch */
		if (child->thread.insn1_type == 0)
			write_tsk_short(child, child->thread.addr1,
					child->thread.insn1);
		if (child->thread.insn1_type == 1)
			write_tsk_long(child, child->thread.addr1,
					child->thread.insn1);
		if (child->thread.insn2_type == 0)
			write_tsk_short(child, child->thread.addr2,
					child->thread.insn2);
		if (child->thread.insn2_type == 1)
			write_tsk_long(child, child->thread.addr2,
					child->thread.insn2);
	}

	child->thread.single_step = 0;
	child->thread.ss_nextcnt = 0;
}

void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
}

long
arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret;
	unsigned long __user *datap = (void __user *)data;

	switch (request) {
	/* Read the word at location addr in the USER area.  */
	case PTRACE_PEEKUSR: {
		struct pt_regs *regs;
		unsigned long tmp;

		regs = task_pt_regs(child);

		tmp = 0;  /* Default return value. */
		switch (addr) {
		case 0 ... 31:
			tmp = regs->regs[addr];
			break;
		case PC:
			tmp = regs->cp0_epc;
			break;
		case ECR:
			tmp = regs->cp0_ecr;
			break;
		case EMA:
			tmp = regs->cp0_ema;
			break;
		case CEH:
			tmp = regs->ceh;
			break;
		case CEL:
			tmp = regs->cel;
			break;
		case CONDITION:
			tmp = regs->cp0_condition;
			break;
		case PSR:
			tmp = regs->cp0_psr;
			break;
		case COUNTER:
			tmp = regs->sr0;
			break;
		case LDCR:
			tmp = regs->sr1;
			break;
		case STCR:
			tmp = regs->sr2;
			break;
		default:
			tmp = 0;
			return -EIO;
		}

		ret = put_user(tmp, (unsigned int __user *) datap);
		return ret;
	}

	case PTRACE_POKEUSR: {
		struct pt_regs *regs;
		ret = 0;
		regs = task_pt_regs(child);

		switch (addr) {
		case 0 ... 31:
			regs->regs[addr] = data;
			break;
		case PC:
			regs->cp0_epc = data;
			break;
		case CEH:
			regs->ceh = data;
			break;
		case CEL:
			regs->cel = data;
			break;
		case CONDITION:
			regs->cp0_condition = data;
			break;
		case PSR:
		case COUNTER:
		case STCR:
		case LDCR:
			break; /* user can't write the reg */
		default:
			/* The rest are not allowed. */
			ret = -EIO;
			break;
		}
		break;
	}

	case PTRACE_GETREGS:
		ret = ptrace_getregs(child, (void __user *)datap);
		break;

	case PTRACE_SETREGS:
		ret = ptrace_setregs(child, (void __user *)datap);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

/*
 * Notification of system call entry/exit
 * - triggered by current->work.syscall_trace
 */
asmlinkage void do_syscall_trace(struct pt_regs *regs, int entryexit)
{
	if (!(current->ptrace & PT_PTRACED))
		return;

	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;

	/* The 0x80 provides a way for the tracing parent to distinguish
	   between a syscall stop and SIGTRAP delivery. */
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD) ?
			0x80 : 0));

	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
