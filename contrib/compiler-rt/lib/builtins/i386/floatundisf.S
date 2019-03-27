// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.

#include "../assembly.h"

// float __floatundisf(du_int a);

// Note that there is a hardware instruction, fildll, that does most of what
// this function needs to do.  However, because of our ia32 ABI, it will take
// a write-small read-large stall, so the software implementation here is
// actually several cycles faster.

// This is a branch-free implementation.  A branchy implementation might be
// faster for the common case if you know something a priori about the input
// distribution.

/* branch-free x87 implementation - one cycle slower than without x87.

#ifdef __i386__

CONST_SECTION
.balign 3

		.quad	0x43f0000000000000
twop64:	.quad	0x0000000000000000

#define			TWOp64			twop64-0b(%ecx,%eax,8)

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(__floatundisf)
	movl		8(%esp),		%eax
	movd		8(%esp),		%xmm1
	movd		4(%esp),		%xmm0
	punpckldq	%xmm1,			%xmm0
	calll		0f
0:	popl		%ecx
	sarl		$31,			%eax
	movq		%xmm0,			4(%esp)
	fildll		4(%esp)
	faddl		TWOp64
	fstps		4(%esp)
	flds		4(%esp)
	ret
END_COMPILERRT_FUNCTION(__floatundisf)

#endif // __i386__

*/

/* branch-free, x87-free implementation - faster at the expense of code size */

#ifdef __i386__

CONST_SECTION

	.balign 16
twop52:
	.quad 0x4330000000000000
	.quad 0x0000000000000fff

	.balign 16
sticky:
	.quad 0x0000000000000000
	.long 0x00000012

	.balign 16
twelve:
	.long 0x00000000

#define			TWOp52			twop52-0b(%ecx)
#define			STICKY			sticky-0b(%ecx,%eax,8)

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(__floatundisf)
	movl		8(%esp),		%eax
	movd		8(%esp),		%xmm1
	movd		4(%esp),		%xmm0
	punpckldq	%xmm1,			%xmm0
	
	calll		0f
0:	popl		%ecx
	shrl		%eax					// high 31 bits of input as sint32
	addl		$0x7ff80000,	%eax
	sarl		$31,			%eax	// (big input) ? -1 : 0
	movsd		STICKY,			%xmm1	// (big input) ? 0xfff : 0
	movl		$12,			%edx
	andl		%eax,			%edx	// (big input) ? 12 : 0
	movd		%edx,			%xmm3
	andpd		%xmm0,			%xmm1	// (big input) ? input & 0xfff : 0
	movsd		TWOp52,			%xmm2	// 0x1.0p52
	psrlq		%xmm3,			%xmm0	// (big input) ? input >> 12 : input
	orpd		%xmm2,			%xmm1	// 0x1.0p52 + ((big input) ? input & 0xfff : input)
	orpd		%xmm1,			%xmm0	// 0x1.0p52 + ((big input) ? (input >> 12 | input & 0xfff) : input)
	subsd		%xmm2,			%xmm0	// (double)((big input) ? (input >> 12 | input & 0xfff) : input)
	cvtsd2ss	%xmm0,			%xmm0	// (float)((big input) ? (input >> 12 | input & 0xfff) : input)
	pslld		$23,			%xmm3
	paddd		%xmm3,			%xmm0	// (float)input
	movd		%xmm0,			4(%esp)
	flds		4(%esp)
	ret
END_COMPILERRT_FUNCTION(__floatundisf)

#endif // __i386__

NO_EXEC_STACK_DIRECTIVE

