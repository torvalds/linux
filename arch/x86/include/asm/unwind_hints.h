#ifndef _ASM_X86_UNWIND_HINTS_H
#define _ASM_X86_UNWIND_HINTS_H

#include <linux/objtool.h>

#include "orc_types.h"

#ifdef __ASSEMBLY__

.macro UNWIND_HINT_END_OF_STACK
	UNWIND_HINT type=UNWIND_HINT_TYPE_END_OF_STACK
.endm

.macro UNWIND_HINT_UNDEFINED
	UNWIND_HINT type=UNWIND_HINT_TYPE_UNDEFINED
.endm

.macro UNWIND_HINT_ENTRY
	VALIDATE_UNRET_BEGIN
	UNWIND_HINT_END_OF_STACK
.endm

.macro UNWIND_HINT_REGS base=%rsp offset=0 indirect=0 extra=1 partial=0 signal=1
	.if \base == %rsp
		.if \indirect
			.set sp_reg, ORC_REG_SP_INDIRECT
		.else
			.set sp_reg, ORC_REG_SP
		.endif
	.elseif \base == %rbp
		.set sp_reg, ORC_REG_BP
	.elseif \base == %rdi
		.set sp_reg, ORC_REG_DI
	.elseif \base == %rdx
		.set sp_reg, ORC_REG_DX
	.elseif \base == %r10
		.set sp_reg, ORC_REG_R10
	.else
		.error "UNWIND_HINT_REGS: bad base register"
	.endif

	.set sp_offset, \offset

	.if \partial
		.set type, UNWIND_HINT_TYPE_REGS_PARTIAL
	.elseif \extra == 0
		.set type, UNWIND_HINT_TYPE_REGS_PARTIAL
		.set sp_offset, \offset + (16*8)
	.else
		.set type, UNWIND_HINT_TYPE_REGS
	.endif

	UNWIND_HINT sp_reg=sp_reg sp_offset=sp_offset type=type signal=\signal
.endm

.macro UNWIND_HINT_IRET_REGS base=%rsp offset=0 signal=1
	UNWIND_HINT_REGS base=\base offset=\offset partial=1 signal=\signal
.endm

.macro UNWIND_HINT_IRET_ENTRY base=%rsp offset=0 signal=1
	VALIDATE_UNRET_BEGIN
	UNWIND_HINT_IRET_REGS base=\base offset=\offset signal=\signal
.endm

.macro UNWIND_HINT_FUNC
	UNWIND_HINT sp_reg=ORC_REG_SP sp_offset=8 type=UNWIND_HINT_TYPE_FUNC
.endm

.macro UNWIND_HINT_SAVE
	UNWIND_HINT type=UNWIND_HINT_TYPE_SAVE
.endm

.macro UNWIND_HINT_RESTORE
	UNWIND_HINT type=UNWIND_HINT_TYPE_RESTORE
.endm

#else

#define UNWIND_HINT_FUNC \
	UNWIND_HINT(UNWIND_HINT_TYPE_FUNC, ORC_REG_SP, 8, 0)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_UNWIND_HINTS_H */
