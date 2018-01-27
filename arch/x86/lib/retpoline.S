/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/stringify.h>
#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/cpufeatures.h>
#include <asm/alternative-asm.h>
#include <asm/export.h>
#include <asm/nospec-branch.h>
#include <asm/bitsperlong.h>

.macro THUNK reg
	.section .text.__x86.indirect_thunk

ENTRY(__x86_indirect_thunk_\reg)
	CFI_STARTPROC
	JMP_NOSPEC %\reg
	CFI_ENDPROC
ENDPROC(__x86_indirect_thunk_\reg)
.endm

/*
 * Despite being an assembler file we can't just use .irp here
 * because __KSYM_DEPS__ only uses the C preprocessor and would
 * only see one instance of "__x86_indirect_thunk_\reg" rather
 * than one per register with the correct names. So we do it
 * the simple and nasty way...
 */
#define __EXPORT_THUNK(sym) _ASM_NOKPROBE(sym); EXPORT_SYMBOL(sym)
#define EXPORT_THUNK(reg) __EXPORT_THUNK(__x86_indirect_thunk_ ## reg)
#define GENERATE_THUNK(reg) THUNK reg ; EXPORT_THUNK(reg)

GENERATE_THUNK(_ASM_AX)
GENERATE_THUNK(_ASM_BX)
GENERATE_THUNK(_ASM_CX)
GENERATE_THUNK(_ASM_DX)
GENERATE_THUNK(_ASM_SI)
GENERATE_THUNK(_ASM_DI)
GENERATE_THUNK(_ASM_BP)
#ifdef CONFIG_64BIT
GENERATE_THUNK(r8)
GENERATE_THUNK(r9)
GENERATE_THUNK(r10)
GENERATE_THUNK(r11)
GENERATE_THUNK(r12)
GENERATE_THUNK(r13)
GENERATE_THUNK(r14)
GENERATE_THUNK(r15)
#endif

/*
 * Fill the CPU return stack buffer.
 *
 * Each entry in the RSB, if used for a speculative 'ret', contains an
 * infinite 'pause; lfence; jmp' loop to capture speculative execution.
 *
 * This is required in various cases for retpoline and IBRS-based
 * mitigations for the Spectre variant 2 vulnerability. Sometimes to
 * eliminate potentially bogus entries from the RSB, and sometimes
 * purely to ensure that it doesn't get empty, which on some CPUs would
 * allow predictions from other (unwanted!) sources to be used.
 *
 * Google experimented with loop-unrolling and this turned out to be
 * the optimal version - two calls, each with their own speculation
 * trap should their return address end up getting used, in a loop.
 */
.macro STUFF_RSB nr:req sp:req
	mov	$(\nr / 2), %_ASM_BX
	.align 16
771:
	call	772f
773:						/* speculation trap */
	pause
	lfence
	jmp	773b
	.align 16
772:
	call	774f
775:						/* speculation trap */
	pause
	lfence
	jmp	775b
	.align 16
774:
	dec	%_ASM_BX
	jnz	771b
	add	$((BITS_PER_LONG/8) * \nr), \sp
.endm

#define RSB_FILL_LOOPS		16	/* To avoid underflow */

ENTRY(__fill_rsb)
	STUFF_RSB RSB_FILL_LOOPS, %_ASM_SP
	ret
END(__fill_rsb)
EXPORT_SYMBOL_GPL(__fill_rsb)

#define RSB_CLEAR_LOOPS		32	/* To forcibly overwrite all entries */

ENTRY(__clear_rsb)
	STUFF_RSB RSB_CLEAR_LOOPS, %_ASM_SP
	ret
END(__clear_rsb)
EXPORT_SYMBOL_GPL(__clear_rsb)
