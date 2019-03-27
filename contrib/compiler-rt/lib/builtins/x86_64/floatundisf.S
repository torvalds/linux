// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.

#include "../assembly.h"

// float __floatundisf(du_int a);

#ifdef __x86_64__

CONST_SECTION

	.balign 16
two:
	.single 2.0

#define REL_ADDR(_a)	(_a)(%rip)

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(__floatundisf)
	movq		$1,			%rsi
	testq		%rdi,		%rdi
	js			1f
	cvtsi2ssq	%rdi,		%xmm0
	ret
	
1:	andq		%rdi,		%rsi
	shrq		%rdi
	orq			%rsi,		%rdi
	cvtsi2ssq	%rdi,		%xmm0
	mulss	REL_ADDR(two),	%xmm0
	ret
END_COMPILERRT_FUNCTION(__floatundisf)

#endif // __x86_64__

NO_EXEC_STACK_DIRECTIVE

