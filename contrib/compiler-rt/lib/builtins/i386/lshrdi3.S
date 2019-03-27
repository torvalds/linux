// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.

#include "../assembly.h"

// di_int __lshrdi3(di_int input, int count);

// This routine has some extra memory traffic, loading the 64-bit input via two
// 32-bit loads, then immediately storing it back to the stack via a single 64-bit
// store.  This is to avoid a write-small, read-large stall.
// However, if callers of this routine can be safely assumed to store the argument
// via a 64-bt store, this is unnecessary memory traffic, and should be avoided.
// It can be turned off by defining the TRUST_CALLERS_USE_64_BIT_STORES macro.

#ifdef __i386__
#ifdef __SSE2__

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(__lshrdi3)
	movd	  12(%esp),		%xmm2	// Load count
#ifndef TRUST_CALLERS_USE_64_BIT_STORES
	movd	   4(%esp),		%xmm0
	movd	   8(%esp),		%xmm1
	punpckldq	%xmm1,		%xmm0	// Load input
#else
	movq	   4(%esp),		%xmm0	// Load input
#endif
	psrlq		%xmm2,		%xmm0	// shift input by count
	movd		%xmm0,		%eax
	psrlq		$32,		%xmm0
	movd		%xmm0,		%edx
	ret
END_COMPILERRT_FUNCTION(__lshrdi3)

#else // Use GPRs instead of SSE2 instructions, if they aren't available.

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(__lshrdi3)
	movl	  12(%esp),		%ecx	// Load count
	movl	   8(%esp),		%edx	// Load high
	movl	   4(%esp),		%eax	// Load low
	
	testl		$0x20,		%ecx	// If count >= 32
	jnz			1f					//    goto 1

	shrdl		%cl, %edx,	%eax	// right shift low by count
	shrl		%cl,		%edx	// right shift high by count
	ret
	
1:	movl		%edx,		%eax	// Move high to low
	xorl		%edx,		%edx	// clear high
	shrl		%cl,		%eax	// shift low by count - 32
	ret
END_COMPILERRT_FUNCTION(__lshrdi3)

#endif // __SSE2__
#endif // __i386__

NO_EXEC_STACK_DIRECTIVE

