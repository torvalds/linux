/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/linkage.h>
#include <asm/unwind_hints.h>
#include <asm/nospec-branch.h>

/*
 * Notably, the FineIBT preamble calling these will have ZF set and r10 zero.
 *
 * The very last element is in fact larger than 32 bytes, but since its the
 * last element, this does not matter,
 *
 * There are 2 #UD sites, located between 0,1-2,3 and 4,5-6,7 such that they
 * can be reached using Jcc.d8, these elements (1 and 5) have sufficiently
 * big alignment holes for this to not stagger the array.
 */

.pushsection .noinstr.text, "ax"

	.align 32
SYM_CODE_START(__bhi_args)

#ifdef CONFIG_FINEIBT_BHI

	.align 32
SYM_INNER_LABEL(__bhi_args_0, SYM_L_LOCAL)
	ANNOTATE_NOENDBR
	UNWIND_HINT_FUNC
	jne .Lud_1
	ANNOTATE_UNRET_SAFE
	ret
	int3

	.align 32
SYM_INNER_LABEL(__bhi_args_1, SYM_L_LOCAL)
	ANNOTATE_NOENDBR
	UNWIND_HINT_FUNC
	jne .Lud_1
	cmovne %r10, %rdi
	ANNOTATE_UNRET_SAFE
	ret
	int3

	.align 8
	ANNOTATE_REACHABLE
.Lud_1:	ud2
	ANNOTATE_UNRET_SAFE
	ret
	int3

	.align 32
SYM_INNER_LABEL(__bhi_args_2, SYM_L_LOCAL)
	ANNOTATE_NOENDBR
	UNWIND_HINT_FUNC
	jne .Lud_1
	cmovne %r10, %rdi
	cmovne %r10, %rsi
	ANNOTATE_UNRET_SAFE
	ret
	int3

	.align 32
SYM_INNER_LABEL(__bhi_args_3, SYM_L_LOCAL)
	ANNOTATE_NOENDBR
	UNWIND_HINT_FUNC
	jne .Lud_1
	cmovne %r10, %rdi
	cmovne %r10, %rsi
	cmovne %r10, %rdx
	ANNOTATE_UNRET_SAFE
	ret
	int3

	.align 32
SYM_INNER_LABEL(__bhi_args_4, SYM_L_LOCAL)
	ANNOTATE_NOENDBR
	UNWIND_HINT_FUNC
	jne .Lud_2
	cmovne %r10, %rdi
	cmovne %r10, %rsi
	cmovne %r10, %rdx
	cmovne %r10, %rcx
	ANNOTATE_UNRET_SAFE
	ret
	int3

	.align 32
SYM_INNER_LABEL(__bhi_args_5, SYM_L_LOCAL)
	ANNOTATE_NOENDBR
	UNWIND_HINT_FUNC
	jne .Lud_2
	cmovne %r10, %rdi
	cmovne %r10, %rsi
	cmovne %r10, %rdx
	cmovne %r10, %rcx
	cmovne %r10, %r8
	ANNOTATE_UNRET_SAFE
	ret
	int3

	.align 8
	ANNOTATE_REACHABLE
.Lud_2:	ud2
	ANNOTATE_UNRET_SAFE
	ret
	int3

	.align 32
SYM_INNER_LABEL(__bhi_args_6, SYM_L_LOCAL)
	ANNOTATE_NOENDBR
	UNWIND_HINT_FUNC
	jne .Lud_2
	cmovne %r10, %rdi
	cmovne %r10, %rsi
	cmovne %r10, %rdx
	cmovne %r10, %rcx
	cmovne %r10, %r8
	cmovne %r10, %r9
	ANNOTATE_UNRET_SAFE
	ret
	int3

	.align 32
SYM_INNER_LABEL(__bhi_args_7, SYM_L_LOCAL)
	ANNOTATE_NOENDBR
	UNWIND_HINT_FUNC
	jne .Lud_2
	cmovne %r10, %rdi
	cmovne %r10, %rsi
	cmovne %r10, %rdx
	cmovne %r10, %rcx
	cmovne %r10, %r8
	cmovne %r10, %r9
	cmovne %r10, %rsp
	ANNOTATE_UNRET_SAFE
	ret
	int3

#endif /* CONFIG_FINEIBT_BHI */

	.align 32
SYM_INNER_LABEL(__bhi_args_end, SYM_L_GLOBAL)
	ANNOTATE_NOENDBR
	nop /* Work around toolchain+objtool quirk */
SYM_CODE_END(__bhi_args)

.popsection
