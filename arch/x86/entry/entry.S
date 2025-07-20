/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common place for both 32- and 64-bit entry routines.
 */

#include <linux/export.h>
#include <linux/linkage.h>
#include <linux/objtool.h>
#include <asm/msr-index.h>
#include <asm/unwind_hints.h>
#include <asm/segment.h>
#include <asm/cache.h>
#include <asm/cpufeatures.h>
#include <asm/nospec-branch.h>

#include "calling.h"

.pushsection .noinstr.text, "ax"

/* Clobbers AX, CX, DX */
SYM_FUNC_START(write_ibpb)
	ANNOTATE_NOENDBR
	movl	$MSR_IA32_PRED_CMD, %ecx
	movl	_ASM_RIP(x86_pred_cmd), %eax
	xorl	%edx, %edx
	wrmsr

	/* Make sure IBPB clears return stack preductions too. */
	FILL_RETURN_BUFFER %rax, RSB_CLEAR_LOOPS, X86_BUG_IBPB_NO_RET
	RET
SYM_FUNC_END(write_ibpb)
/* For KVM */
EXPORT_SYMBOL_GPL(write_ibpb);

.popsection

/*
 * Define the VERW operand that is disguised as entry code so that
 * it can be referenced with KPTI enabled. This ensures VERW can be
 * used late in exit-to-user path after page tables are switched.
 */
.pushsection .entry.text, "ax"

.align L1_CACHE_BYTES, 0xcc
SYM_CODE_START_NOALIGN(x86_verw_sel)
	UNWIND_HINT_UNDEFINED
	ANNOTATE_NOENDBR
	.word __KERNEL_DS
.align L1_CACHE_BYTES, 0xcc
SYM_CODE_END(x86_verw_sel);
/* For KVM */
EXPORT_SYMBOL_GPL(x86_verw_sel);

.popsection

THUNK warn_thunk_thunk, __warn_thunk

/*
 * Clang's implementation of TLS stack cookies requires the variable in
 * question to be a TLS variable. If the variable happens to be defined as an
 * ordinary variable with external linkage in the same compilation unit (which
 * amounts to the whole of vmlinux with LTO enabled), Clang will drop the
 * segment register prefix from the references, resulting in broken code. Work
 * around this by avoiding the symbol used in -mstack-protector-guard-symbol=
 * entirely in the C code, and use an alias emitted by the linker script
 * instead.
 */
#if defined(CONFIG_STACKPROTECTOR) && defined(CONFIG_SMP)
EXPORT_SYMBOL(__ref_stack_chk_guard);
#endif
