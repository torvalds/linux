// SPDX-License-Identifier: GPL-2.0
/*
 * Clang Control Flow Integrity (CFI) support.
 *
 * Copyright (C) 2022 Google LLC
 */
#include <asm/cfi.h>
#include <asm/insn.h>
#include <asm/insn-eval.h>
#include <linux/string.h>

/*
 * Returns the target address and the expected type when regs->ip points
 * to a compiler-generated CFI trap.
 */
static bool decode_cfi_insn(struct pt_regs *regs, unsigned long *target,
			    u32 *type)
{
	char buffer[MAX_INSN_SIZE];
	struct insn insn;
	int offset = 0;

	*target = *type = 0;

	/*
	 * The compiler generates the following instruction sequence
	 * for indirect call checks:
	 *
	 *   movl    -<id>, %r10d       ; 6 bytes
	 *   addl    -4(%reg), %r10d    ; 4 bytes
	 *   je      .Ltmp1             ; 2 bytes
	 *   ud2                        ; <- regs->ip
	 *   .Ltmp1:
	 *
	 * We can decode the expected type and the target address from the
	 * movl/addl instructions.
	 */
	if (copy_from_kernel_nofault(buffer, (void *)regs->ip - 12, MAX_INSN_SIZE))
		return false;
	if (insn_decode_kernel(&insn, &buffer[offset]))
		return false;
	if (insn.opcode.value != 0xBA)
		return false;

	*type = -(u32)insn.immediate.value;

	if (copy_from_kernel_nofault(buffer, (void *)regs->ip - 6, MAX_INSN_SIZE))
		return false;
	if (insn_decode_kernel(&insn, &buffer[offset]))
		return false;
	if (insn.opcode.value != 0x3)
		return false;

	/* Read the target address from the register. */
	offset = insn_get_modrm_rm_off(&insn, regs);
	if (offset < 0)
		return false;

	*target = *(unsigned long *)((void *)regs + offset);

	return true;
}

/*
 * Checks if a ud2 trap is because of a CFI failure, and handles the trap
 * if needed. Returns a bug_trap_type value similarly to report_bug.
 */
enum bug_trap_type handle_cfi_failure(struct pt_regs *regs)
{
	unsigned long target;
	u32 type;

	if (!is_cfi_trap(regs->ip))
		return BUG_TRAP_TYPE_NONE;

	if (!decode_cfi_insn(regs, &target, &type))
		return report_cfi_failure_noaddr(regs, regs->ip);

	return report_cfi_failure(regs, regs->ip, &target, type);
}

/*
 * Ensure that __kcfi_typeid_ symbols are emitted for functions that may
 * not be indirectly called with all configurations.
 */
__ADDRESSABLE(__memcpy)
