/* SPDX-License-Identifier: GPL-2.0 */
/*
 * trampoline entry and return code for kretprobes.
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>

	.text

	.macro	save_all_base_regs
	stp x0, x1, [sp, #S_X0]
	stp x2, x3, [sp, #S_X2]
	stp x4, x5, [sp, #S_X4]
	stp x6, x7, [sp, #S_X6]
	stp x8, x9, [sp, #S_X8]
	stp x10, x11, [sp, #S_X10]
	stp x12, x13, [sp, #S_X12]
	stp x14, x15, [sp, #S_X14]
	stp x16, x17, [sp, #S_X16]
	stp x18, x19, [sp, #S_X18]
	stp x20, x21, [sp, #S_X20]
	stp x22, x23, [sp, #S_X22]
	stp x24, x25, [sp, #S_X24]
	stp x26, x27, [sp, #S_X26]
	stp x28, x29, [sp, #S_X28]
	add x0, sp, #PT_REGS_SIZE
	stp lr, x0, [sp, #S_LR]
	/*
	 * Construct a useful saved PSTATE
	 */
	mrs x0, nzcv
	mrs x1, daif
	orr x0, x0, x1
	mrs x1, CurrentEL
	orr x0, x0, x1
	mrs x1, SPSel
	orr x0, x0, x1
	stp xzr, x0, [sp, #S_PC]
	.endm

	.macro	restore_all_base_regs
	ldr x0, [sp, #S_PSTATE]
	and x0, x0, #(PSR_N_BIT | PSR_Z_BIT | PSR_C_BIT | PSR_V_BIT)
	msr nzcv, x0
	ldp x0, x1, [sp, #S_X0]
	ldp x2, x3, [sp, #S_X2]
	ldp x4, x5, [sp, #S_X4]
	ldp x6, x7, [sp, #S_X6]
	ldp x8, x9, [sp, #S_X8]
	ldp x10, x11, [sp, #S_X10]
	ldp x12, x13, [sp, #S_X12]
	ldp x14, x15, [sp, #S_X14]
	ldp x16, x17, [sp, #S_X16]
	ldp x18, x19, [sp, #S_X18]
	ldp x20, x21, [sp, #S_X20]
	ldp x22, x23, [sp, #S_X22]
	ldp x24, x25, [sp, #S_X24]
	ldp x26, x27, [sp, #S_X26]
	ldp x28, x29, [sp, #S_X28]
	.endm

SYM_CODE_START(__kretprobe_trampoline)
	sub sp, sp, #PT_REGS_SIZE

	save_all_base_regs

	mov x0, sp
	bl trampoline_probe_handler
	/*
	 * Replace trampoline address in lr with actual orig_ret_addr return
	 * address.
	 */
	mov lr, x0

	restore_all_base_regs

	add sp, sp, #PT_REGS_SIZE
	ret

SYM_CODE_END(__kretprobe_trampoline)
