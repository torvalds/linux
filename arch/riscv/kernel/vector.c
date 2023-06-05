// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 SiFive
 * Author: Andy Chiu <andy.chiu@sifive.com>
 */
#include <linux/export.h>
#include <linux/sched/signal.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#include <asm/thread_info.h>
#include <asm/processor.h>
#include <asm/insn.h>
#include <asm/vector.h>
#include <asm/csr.h>
#include <asm/elf.h>
#include <asm/ptrace.h>
#include <asm/bug.h>

unsigned long riscv_v_vsize __read_mostly;
EXPORT_SYMBOL_GPL(riscv_v_vsize);

int riscv_v_setup_vsize(void)
{
	unsigned long this_vsize;

	/* There are 32 vector registers with vlenb length. */
	riscv_v_enable();
	this_vsize = csr_read(CSR_VLENB) * 32;
	riscv_v_disable();

	if (!riscv_v_vsize) {
		riscv_v_vsize = this_vsize;
		return 0;
	}

	if (riscv_v_vsize != this_vsize) {
		WARN(1, "RISCV_ISA_V only supports one vlenb on SMP systems");
		return -EOPNOTSUPP;
	}

	return 0;
}

static bool insn_is_vector(u32 insn_buf)
{
	u32 opcode = insn_buf & __INSN_OPCODE_MASK;
	u32 width, csr;

	/*
	 * All V-related instructions, including CSR operations are 4-Byte. So,
	 * do not handle if the instruction length is not 4-Byte.
	 */
	if (unlikely(GET_INSN_LENGTH(insn_buf) != 4))
		return false;

	switch (opcode) {
	case RVV_OPCODE_VECTOR:
		return true;
	case RVV_OPCODE_VL:
	case RVV_OPCODE_VS:
		width = RVV_EXRACT_VL_VS_WIDTH(insn_buf);
		if (width == RVV_VL_VS_WIDTH_8 || width == RVV_VL_VS_WIDTH_16 ||
		    width == RVV_VL_VS_WIDTH_32 || width == RVV_VL_VS_WIDTH_64)
			return true;

		break;
	case RVG_OPCODE_SYSTEM:
		csr = RVG_EXTRACT_SYSTEM_CSR(insn_buf);
		if ((csr >= CSR_VSTART && csr <= CSR_VCSR) ||
		    (csr >= CSR_VL && csr <= CSR_VLENB))
			return true;
	}

	return false;
}

static int riscv_v_thread_zalloc(void)
{
	void *datap;

	datap = kzalloc(riscv_v_vsize, GFP_KERNEL);
	if (!datap)
		return -ENOMEM;

	current->thread.vstate.datap = datap;
	memset(&current->thread.vstate, 0, offsetof(struct __riscv_v_ext_state,
						    datap));
	return 0;
}

bool riscv_v_first_use_handler(struct pt_regs *regs)
{
	u32 __user *epc = (u32 __user *)regs->epc;
	u32 insn = (u32)regs->badaddr;

	/* Do not handle if V is not supported, or disabled */
	if (!(ELF_HWCAP & COMPAT_HWCAP_ISA_V))
		return false;

	/* If V has been enabled then it is not the first-use trap */
	if (riscv_v_vstate_query(regs))
		return false;

	/* Get the instruction */
	if (!insn) {
		if (__get_user(insn, epc))
			return false;
	}

	/* Filter out non-V instructions */
	if (!insn_is_vector(insn))
		return false;

	/* Sanity check. datap should be null by the time of the first-use trap */
	WARN_ON(current->thread.vstate.datap);

	/*
	 * Now we sure that this is a V instruction. And it executes in the
	 * context where VS has been off. So, try to allocate the user's V
	 * context and resume execution.
	 */
	if (riscv_v_thread_zalloc()) {
		force_sig(SIGBUS);
		return true;
	}
	riscv_v_vstate_on(regs);
	return true;
}
