/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */

#include <asm/export.h>
#include <asm/ftrace.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.text
/*
 * Due to -fpatchable-function-entry=2: the compiler inserted 2 NOPs before the
 * regular C function prologue. When PC arrived here, the last 2 instructions
 * are as follows:
 * 	move		t0, ra
 * 	bl		callsite (for modules, callsite is a tramplione)
 *
 * modules trampoline is as follows:
 * 	lu12i.w		t1, callsite[31:12]
 * 	lu32i.d		t1, callsite[51:32]
 * 	lu52i.d		t1, t1, callsite[63:52]
 * 	jirl		zero, t1, callsite[11:0] >> 2
 *
 * See arch/loongarch/kernel/ftrace_dyn.c for details. Here, pay attention to
 * that the T series regs are available and safe because each C functions
 * follows the LoongArch's psABI as well.
 */

	.macro  ftrace_regs_entry
	PTR_ADDI	sp, sp, -PT_SIZE
	PTR_S		t0, sp, PT_R1  /* Save parent ra at PT_R1(RA) */
	PTR_S		a0, sp, PT_R4
	PTR_S		a1, sp, PT_R5
	PTR_S		a2, sp, PT_R6
	PTR_S		a3, sp, PT_R7
	PTR_S		a4, sp, PT_R8
	PTR_S		a5, sp, PT_R9
	PTR_S		a6, sp, PT_R10
	PTR_S		a7, sp, PT_R11
	PTR_S		fp, sp, PT_R22
	PTR_S		ra, sp, PT_ERA /* Save trace function ra at PT_ERA */
	PTR_ADDI	t8, sp, PT_SIZE
	PTR_S		t8, sp, PT_R3
	.endm

SYM_FUNC_START(ftrace_stub)
	jr		ra
SYM_FUNC_END(ftrace_stub)

SYM_CODE_START(ftrace_common)
	PTR_ADDI	a0, ra, -8	/* arg0: ip */
	move		a1, t0		/* arg1: parent_ip */
	la.pcrel	t1, function_trace_op
	PTR_L		a2, t1, 0	/* arg2: op */
	move		a3, sp		/* arg3: regs */

SYM_INNER_LABEL(ftrace_call, SYM_L_GLOBAL)
	bl		ftrace_stub
/*
 * As we didn't use S series regs in this assmembly code and all calls
 * are C function which will save S series regs by themselves, there is
 * no need to restore S series regs. The T series is available and safe
 * at the callsite, so there is no need to restore the T series regs.
 */
ftrace_common_return:
	PTR_L		ra, sp, PT_R1
	PTR_L		a0, sp, PT_R4
	PTR_L		a1, sp, PT_R5
	PTR_L		a2, sp, PT_R6
	PTR_L		a3, sp, PT_R7
	PTR_L		a4, sp, PT_R8
	PTR_L		a5, sp, PT_R9
	PTR_L		a6, sp, PT_R10
	PTR_L		a7, sp, PT_R11
	PTR_L		fp, sp, PT_R22
	PTR_L		t0, sp, PT_ERA
	PTR_ADDI	sp, sp, PT_SIZE
	jr		t0
SYM_CODE_END(ftrace_common)

SYM_CODE_START(ftrace_caller)
	ftrace_regs_entry
	b		ftrace_common
SYM_CODE_END(ftrace_caller)
