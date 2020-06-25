/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/stringify.h>
#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/cpufeatures.h>
#include <asm/alternative-asm.h>
#include <asm/export.h>
#include <asm/nospec-branch.h>
#include <asm/unwind_hints.h>
#include <asm/frame.h>

.macro THUNK reg
	.section .text.__x86.indirect_thunk

	.align 32
SYM_FUNC_START(__x86_indirect_thunk_\reg)
	JMP_NOSPEC \reg
SYM_FUNC_END(__x86_indirect_thunk_\reg)

SYM_FUNC_START_NOALIGN(__x86_retpoline_\reg)
	ANNOTATE_INTRA_FUNCTION_CALL
	call	.Ldo_rop_\@
.Lspec_trap_\@:
	UNWIND_HINT_EMPTY
	pause
	lfence
	jmp	.Lspec_trap_\@
.Ldo_rop_\@:
	mov	%\reg, (%_ASM_SP)
	UNWIND_HINT_RET_OFFSET
	ret
SYM_FUNC_END(__x86_retpoline_\reg)

.endm

/*
 * Despite being an assembler file we can't just use .irp here
 * because __KSYM_DEPS__ only uses the C preprocessor and would
 * only see one instance of "__x86_indirect_thunk_\reg" rather
 * than one per register with the correct names. So we do it
 * the simple and nasty way...
 *
 * Worse, you can only have a single EXPORT_SYMBOL per line,
 * and CPP can't insert newlines, so we have to repeat everything
 * at least twice.
 */

#define __EXPORT_THUNK(sym)	_ASM_NOKPROBE(sym); EXPORT_SYMBOL(sym)
#define EXPORT_THUNK(reg)	__EXPORT_THUNK(__x86_indirect_thunk_ ## reg)
#define EXPORT_RETPOLINE(reg)  __EXPORT_THUNK(__x86_retpoline_ ## reg)

#undef GEN
#define GEN(reg) THUNK reg
#include <asm/GEN-for-each-reg.h>

#undef GEN
#define GEN(reg) EXPORT_THUNK(reg)
#include <asm/GEN-for-each-reg.h>

#undef GEN
#define GEN(reg) EXPORT_RETPOLINE(reg)
#include <asm/GEN-for-each-reg.h>
