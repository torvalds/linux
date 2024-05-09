// SPDX-License-Identifier: GPL-2.0
/*
 * Clang Control Flow Integrity (CFI) support.
 *
 * Copyright (C) 2023 Google LLC
 */
#include <asm/cfi.h>
#include <asm/insn.h>

/*
 * Returns the target address and the expected type when regs->epc points
 * to a compiler-generated CFI trap.
 */
static bool decode_cfi_insn(struct pt_regs *regs, unsigned long *target,
			    u32 *type)
{
	unsigned long *regs_ptr = (unsigned long *)regs;
	int rs1_num;
	u32 insn;

	*target = *type = 0;

	/*
	 * The compiler generates the following instruction sequence
	 * for indirect call checks:
	 *
	 *   lw      t1, -4(<reg>)
	 *   lui     t2, <hi20>
	 *   addiw   t2, t2, <lo12>
	 *   beq     t1, t2, .Ltmp1
	 *   ebreak  ; <- regs->epc
	 *   .Ltmp1:
	 *   jalr    <reg>
	 *
	 * We can read the expected type and the target address from the
	 * registers passed to the beq/jalr instructions.
	 */
	if (get_kernel_nofault(insn, (void *)regs->epc - 4))
		return false;
	if (!riscv_insn_is_beq(insn))
		return false;

	*type = (u32)regs_ptr[RV_EXTRACT_RS1_REG(insn)];

	if (get_kernel_nofault(insn, (void *)regs->epc) ||
	    get_kernel_nofault(insn, (void *)regs->epc + GET_INSN_LENGTH(insn)))
		return false;

	if (riscv_insn_is_jalr(insn))
		rs1_num = RV_EXTRACT_RS1_REG(insn);
	else if (riscv_insn_is_c_jalr(insn))
		rs1_num = RVC_EXTRACT_C2_RS1_REG(insn);
	else
		return false;

	*target = regs_ptr[rs1_num];

	return true;
}

/*
 * Checks if the ebreak trap is because of a CFI failure, and handles the trap
 * if needed. Returns a bug_trap_type value similarly to report_bug.
 */
enum bug_trap_type handle_cfi_failure(struct pt_regs *regs)
{
	unsigned long target;
	u32 type;

	if (!is_cfi_trap(regs->epc))
		return BUG_TRAP_TYPE_NONE;

	if (!decode_cfi_insn(regs, &target, &type))
		return report_cfi_failure_noaddr(regs, regs->epc);

	return report_cfi_failure(regs, regs->epc, &target, type);
}
