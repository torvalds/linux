//===-- floatundidf.S - Implement __floatundidf for x86_64 ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements __floatundidf for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

// double __floatundidf(du_int a);

#ifdef __x86_64__

CONST_SECTION

	.balign 16
twop52:
	.quad 0x4330000000000000

	.balign 16
twop84_plus_twop52:
	.quad 0x4530000000100000

	.balign 16
twop84:
	.quad 0x4530000000000000

#define REL_ADDR(_a)	(_a)(%rip)

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(__floatundidf)
	movd	%edi,							%xmm0 // low 32 bits of a
	shrq	$32,							%rdi  // high 32 bits of a
	orq		REL_ADDR(twop84),				%rdi  // 0x1p84 + a_hi (no rounding occurs)
	orpd	REL_ADDR(twop52),				%xmm0 // 0x1p52 + a_lo (no rounding occurs)
	movd	%rdi,							%xmm1
	subsd	REL_ADDR(twop84_plus_twop52),	%xmm1 // a_hi - 0x1p52 (no rounding occurs)
	addsd	%xmm1,							%xmm0 // a_hi + a_lo   (round happens here)
	ret
END_COMPILERRT_FUNCTION(__floatundidf)

#endif // __x86_64__

NO_EXEC_STACK_DIRECTIVE

