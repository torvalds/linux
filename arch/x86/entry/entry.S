/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common place for both 32- and 64-bit entry routines.
 */

#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/msr-index.h>
#include <asm/unwind_hints.h>
#include <asm/segment.h>
#include <asm/cache.h>
#include <asm/cpufeatures.h>
#include <asm/nospec-branch.h>

#include "calling.h"

.pushsection .noinstr.text, "ax"

SYM_FUNC_START(entry_ibpb)
	movl	$MSR_IA32_PRED_CMD, %ecx
	movl	$PRED_CMD_IBPB, %eax
	xorl	%edx, %edx
	wrmsr

	/* Make sure IBPB clears return stack preductions too. */
	FILL_RETURN_BUFFER %rax, RSB_CLEAR_LOOPS, X86_BUG_IBPB_NO_RET
	RET
SYM_FUNC_END(entry_ibpb)
/* For KVM */
EXPORT_SYMBOL_GPL(entry_ibpb);

.popsection

/*
 * Define the VERW operand that is disguised as entry code so that
 * it can be referenced with KPTI enabled. This ensure VERW can be
 * used late in exit-to-user path after page tables are switched.
 */
.pushsection .entry.text, "ax"

.align L1_CACHE_BYTES, 0xcc
SYM_CODE_START_NOALIGN(mds_verw_sel)
	UNWIND_HINT_UNDEFINED
	ANNOTATE_NOENDBR
	.word __KERNEL_DS
.align L1_CACHE_BYTES, 0xcc
SYM_CODE_END(mds_verw_sel);
/* For KVM */
EXPORT_SYMBOL_GPL(mds_verw_sel);

.popsection

THUNK warn_thunk_thunk, __warn_thunk
