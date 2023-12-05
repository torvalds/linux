/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The actual FRED entry points.
 */

#include <asm/asm.h>
#include <asm/fred.h>

#include "calling.h"

	.code64
	.section .noinstr.text, "ax"

.macro FRED_ENTER
	UNWIND_HINT_END_OF_STACK
	ENDBR
	PUSH_AND_CLEAR_REGS
	movq	%rsp, %rdi	/* %rdi -> pt_regs */
.endm

.macro FRED_EXIT
	UNWIND_HINT_REGS
	POP_REGS
.endm

/*
 * The new RIP value that FRED event delivery establishes is
 * IA32_FRED_CONFIG & ~FFFH for events that occur in ring 3.
 * Thus the FRED ring 3 entry point must be 4K page aligned.
 */
	.align 4096

SYM_CODE_START_NOALIGN(asm_fred_entrypoint_user)
	FRED_ENTER
	call	fred_entry_from_user
SYM_INNER_LABEL(asm_fred_exit_user, SYM_L_GLOBAL)
	FRED_EXIT
1:	ERETU

	_ASM_EXTABLE_TYPE(1b, asm_fred_entrypoint_user, EX_TYPE_ERETU)
SYM_CODE_END(asm_fred_entrypoint_user)

/*
 * The new RIP value that FRED event delivery establishes is
 * (IA32_FRED_CONFIG & ~FFFH) + 256 for events that occur in
 * ring 0, i.e., asm_fred_entrypoint_user + 256.
 */
	.org asm_fred_entrypoint_user + 256, 0xcc
SYM_CODE_START_NOALIGN(asm_fred_entrypoint_kernel)
	FRED_ENTER
	call	fred_entry_from_kernel
	FRED_EXIT
	ERETS
SYM_CODE_END(asm_fred_entrypoint_kernel)
