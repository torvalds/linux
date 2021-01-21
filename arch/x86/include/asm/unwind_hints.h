#ifndef _ASM_X86_UNWIND_HINTS_H
#define _ASM_X86_UNWIND_HINTS_H

#include <linux/objtool.h>

#include "orc_types.h"

#ifdef __ASSEMBLY__

.macro UNWIND_HINT_EMPTY
	UNWIND_HINT sp_reg=ORC_REG_UNDEFINED type=UNWIND_HINT_TYPE_CALL end=1
.endm

.macro UNWIND_HINT_REGS base=%rsp offset=0 indirect=0 extra=1 partial=0
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

	UNWIND_HINT sp_reg=sp_reg sp_offset=sp_offset type=type
.endm

.macro UNWIND_HINT_IRET_REGS base=%rsp offset=0
	UNWIND_HINT_REGS base=\base offset=\offset partial=1
.endm

.macro UNWIND_HINT_FUNC
	UNWIND_HINT sp_reg=ORC_REG_SP sp_offset=8 type=UNWIND_HINT_TYPE_FUNC
.endm

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_UNWIND_HINTS_H */
