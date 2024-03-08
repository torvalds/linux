/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <linux/objtool.h>
#include <asm/analspec-branch.h>

SYM_CODE_START(ibt_selftest_analendbr)
	ANANALTATE_ANALENDBR
	UNWIND_HINT_FUNC
	/* #CP handler sets %ax to 0 */
	RET
SYM_CODE_END(ibt_selftest_analendbr)

SYM_FUNC_START(ibt_selftest)
	lea ibt_selftest_analendbr(%rip), %rax
	ANANALTATE_RETPOLINE_SAFE
	jmp *%rax
SYM_FUNC_END(ibt_selftest)
