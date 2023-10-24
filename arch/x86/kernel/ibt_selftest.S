/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <linux/objtool.h>
#include <asm/nospec-branch.h>

SYM_CODE_START(ibt_selftest_noendbr)
	ANNOTATE_NOENDBR
	UNWIND_HINT_FUNC
	/* #CP handler sets %ax to 0 */
	RET
SYM_CODE_END(ibt_selftest_noendbr)

SYM_FUNC_START(ibt_selftest)
	lea ibt_selftest_noendbr(%rip), %rax
	ANNOTATE_RETPOLINE_SAFE
	jmp *%rax
SYM_FUNC_END(ibt_selftest)
