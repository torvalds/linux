#ifndef _ASM_X86_UNWIND_HINTS_H
#define _ASM_X86_UNWIND_HINTS_H

#include "orc_types.h"

#ifdef __ASSEMBLY__

/*
 * In asm, there are two kinds of code: normal C-type callable functions and
 * the rest.  The normal callable functions can be called by other code, and
 * don't do anything unusual with the stack.  Such normal callable functions
 * are annotated with the ENTRY/ENDPROC macros.  Most asm code falls in this
 * category.  In this case, no special debugging annotations are needed because
 * objtool can automatically generate the ORC data for the ORC unwinder to read
 * at runtime.
 *
 * Anything which doesn't fall into the above category, such as syscall and
 * interrupt handlers, tends to not be called directly by other functions, and
 * often does unusual non-C-function-type things with the stack pointer.  Such
 * code needs to be annotated such that objtool can understand it.  The
 * following CFI hint macros are for this type of code.
 *
 * These macros provide hints to objtool about the state of the stack at each
 * instruction.  Objtool starts from the hints and follows the code flow,
 * making automatic CFI adjustments when it sees pushes and pops, filling out
 * the debuginfo as necessary.  It will also warn if it sees any
 * inconsistencies.
 */
.macro UNWIND_HINT sp_reg=ORC_REG_SP sp_offset=0 type=ORC_TYPE_CALL end=0
#ifdef CONFIG_STACK_VALIDATION
.Lunwind_hint_ip_\@:
	.pushsection .discard.unwind_hints
		/* struct unwind_hint */
		.long .Lunwind_hint_ip_\@ - .
		.short \sp_offset
		.byte \sp_reg
		.byte \type
		.byte \end
		.balign 4
	.popsection
#endif
.endm

.macro UNWIND_HINT_EMPTY
	UNWIND_HINT sp_reg=ORC_REG_UNDEFINED end=1
.endm

.macro UNWIND_HINT_REGS base=%rsp offset=0 indirect=0 extra=1 iret=0
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

	.if \iret
		.set type, ORC_TYPE_REGS_IRET
	.elseif \extra == 0
		.set type, ORC_TYPE_REGS_IRET
		.set sp_offset, \offset + (16*8)
	.else
		.set type, ORC_TYPE_REGS
	.endif

	UNWIND_HINT sp_reg=sp_reg sp_offset=sp_offset type=type
.endm

.macro UNWIND_HINT_IRET_REGS base=%rsp offset=0
	UNWIND_HINT_REGS base=\base offset=\offset iret=1
.endm

.macro UNWIND_HINT_FUNC sp_offset=8
	UNWIND_HINT sp_offset=\sp_offset
.endm

.macro UNWIND_HINT_SAVE
	UNWIND_HINT type=UNWIND_HINT_TYPE_SAVE
.endm

.macro UNWIND_HINT_RESTORE
	UNWIND_HINT type=UNWIND_HINT_TYPE_RESTORE
.endm

#else /* !__ASSEMBLY__ */

#define UNWIND_HINT(sp_reg, sp_offset, type, end)		\
	"987: \n\t"						\
	".pushsection .discard.unwind_hints\n\t"		\
	/* struct unwind_hint */				\
	".long 987b - .\n\t"					\
	".short " __stringify(sp_offset) "\n\t"			\
	".byte " __stringify(sp_reg) "\n\t"			\
	".byte " __stringify(type) "\n\t"			\
	".byte " __stringify(end) "\n\t"			\
	".balign 4 \n\t"					\
	".popsection\n\t"

#define UNWIND_HINT_SAVE UNWIND_HINT(0, 0, UNWIND_HINT_TYPE_SAVE, 0)

#define UNWIND_HINT_RESTORE UNWIND_HINT(0, 0, UNWIND_HINT_TYPE_RESTORE, 0)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_UNWIND_HINTS_H */
