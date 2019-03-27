// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.

#include "../assembly.h"

// double __floatundidf(du_int a);

#ifdef __i386__

CONST_SECTION

	.balign 16
twop52:
	.quad 0x4330000000000000

	.balign 16
twop32:
	.quad 0x41f0000000000000

#define REL_ADDR(_a)	(_a)-0b(%eax)

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(__floatdidf)
	cvtsi2sd	8(%esp),			%xmm1
	movss		4(%esp),			%xmm0 // low 32 bits of a
	calll		0f
0:	popl		%eax
	mulsd		REL_ADDR(twop32),	%xmm1 // a_hi as a double (without rounding)
	movsd		REL_ADDR(twop52),	%xmm2 // 0x1.0p52
	subsd		%xmm2,				%xmm1 // a_hi - 0x1p52 (no rounding occurs)
	orpd		%xmm2,				%xmm0 // 0x1p52 + a_lo (no rounding occurs)
	addsd		%xmm1,				%xmm0 // a_hi + a_lo   (round happens here)
	movsd		%xmm0,			   4(%esp)
	fldl	   4(%esp)
	ret
END_COMPILERRT_FUNCTION(__floatdidf)

#endif // __i386__

NO_EXEC_STACK_DIRECTIVE

