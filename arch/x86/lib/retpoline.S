/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/stringify.h>
#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>
#include <asm/export.h>
#include <asm/nospec-branch.h>
#include <asm/unwind_hints.h>
#include <asm/frame.h>

	.section .text.__x86.indirect_thunk

.macro RETPOLINE reg
	ANNOTATE_INTRA_FUNCTION_CALL
	call    .Ldo_rop_\@
.Lspec_trap_\@:
	UNWIND_HINT_EMPTY
	pause
	lfence
	jmp .Lspec_trap_\@
.Ldo_rop_\@:
	mov     %\reg, (%_ASM_SP)
	UNWIND_HINT_FUNC
	ret
.endm

.macro THUNK reg

	.align 32

SYM_FUNC_START(__x86_indirect_thunk_\reg)

	ALTERNATIVE_2 __stringify(ANNOTATE_RETPOLINE_SAFE; jmp *%\reg), \
		      __stringify(RETPOLINE \reg), X86_FEATURE_RETPOLINE, \
		      __stringify(lfence; ANNOTATE_RETPOLINE_SAFE; jmp *%\reg), X86_FEATURE_RETPOLINE_LFENCE

SYM_FUNC_END(__x86_indirect_thunk_\reg)

.endm

/*
 * This generates .altinstr_replacement symbols for use by objtool. They,
 * however, must not actually live in .altinstr_replacement since that will be
 * discarded after init, but module alternatives will also reference these
 * symbols.
 *
 * Their names matches the "__x86_indirect_" prefix to mark them as retpolines.
 */
.macro ALT_THUNK reg

	.align 1

SYM_FUNC_START_NOALIGN(__x86_indirect_alt_call_\reg)
	ANNOTATE_RETPOLINE_SAFE
1:	call	*%\reg
2:	.skip	5-(2b-1b), 0x90
SYM_FUNC_END(__x86_indirect_alt_call_\reg)

STACK_FRAME_NON_STANDARD(__x86_indirect_alt_call_\reg)

SYM_FUNC_START_NOALIGN(__x86_indirect_alt_jmp_\reg)
	ANNOTATE_RETPOLINE_SAFE
1:	jmp	*%\reg
2:	.skip	5-(2b-1b), 0x90
SYM_FUNC_END(__x86_indirect_alt_jmp_\reg)

STACK_FRAME_NON_STANDARD(__x86_indirect_alt_jmp_\reg)

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

#undef GEN
#define GEN(reg) THUNK reg
#include <asm/GEN-for-each-reg.h>

#undef GEN
#define GEN(reg) EXPORT_THUNK(reg)
#include <asm/GEN-for-each-reg.h>

#undef GEN
#define GEN(reg) ALT_THUNK reg
#include <asm/GEN-for-each-reg.h>

#undef GEN
#define GEN(reg) __EXPORT_THUNK(__x86_indirect_alt_call_ ## reg)
#include <asm/GEN-for-each-reg.h>

#undef GEN
#define GEN(reg) __EXPORT_THUNK(__x86_indirect_alt_jmp_ ## reg)
#include <asm/GEN-for-each-reg.h>
