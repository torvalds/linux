// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.

#include "../assembly.h"

// du_int __udivdi3(du_int a, du_int b);

// result = a / b.
// both inputs and the output are 64-bit unsigned integers.
// This will do whatever the underlying hardware is set to do on division by zero.
// No other exceptions are generated, as the divide cannot overflow.
//
// This is targeted at 32-bit x86 *only*, as this can be done directly in hardware
// on x86_64.  The performance goal is ~40 cycles per divide, which is faster than
// currently possible via simulation of integer divides on the x87 unit.
//
// Stephen Canon, December 2008

#ifdef __i386__

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(__udivdi3)

	pushl		%ebx
	movl	 20(%esp),			%ebx	// Find the index i of the leading bit in b.
	bsrl		%ebx,			%ecx	// If the high word of b is zero, jump to
	jz			9f						// the code to handle that special case [9].
	
	/* High word of b is known to be non-zero on this branch */
	
	movl	 16(%esp),			%eax	// Construct bhi, containing bits [1+i:32+i] of b
	
	shrl		%cl,			%eax	// Practically, this means that bhi is given by:
	shrl		%eax					//
	notl		%ecx					//		bhi = (high word of b) << (31 - i) |
	shll		%cl,			%ebx	//			  (low word of b) >> (1 + i)
	orl			%eax,			%ebx	//
	movl	 12(%esp),			%edx	// Load the high and low words of a, and jump
	movl	  8(%esp),			%eax	// to [1] if the high word is larger than bhi
	cmpl		%ebx,			%edx	// to avoid overflowing the upcoming divide.
	jae			1f						
		
	/* High word of a is greater than or equal to (b >> (1 + i)) on this branch */
	
	divl		%ebx					// eax <-- qs, edx <-- r such that ahi:alo = bs*qs + r

	pushl		%edi
	notl		%ecx
	shrl		%eax
	shrl		%cl,			%eax	// q = qs >> (1 + i)
	movl		%eax,			%edi
	mull	 20(%esp)					// q*blo
	movl	 12(%esp),			%ebx
	movl	 16(%esp),			%ecx	// ECX:EBX = a
	subl		%eax,			%ebx
	sbbl		%edx,			%ecx	// ECX:EBX = a - q*blo
	movl	 24(%esp),			%eax
	imull		%edi,			%eax	// q*bhi
	subl		%eax,			%ecx	// ECX:EBX = a - q*b
	sbbl		$0,				%edi	// decrement q if remainder is negative
	xorl		%edx,			%edx
	movl		%edi,			%eax
	popl		%edi
	popl		%ebx
	retl


1:	/* High word of a is greater than or equal to (b >> (1 + i)) on this branch */
	 
	subl		%ebx,			%edx	// subtract bhi from ahi so that divide will not
	divl		%ebx					// overflow, and find q and r such that
										//
										//		ahi:alo = (1:q)*bhi + r
										//
										// Note that q is a number in (31-i).(1+i)
										// fix point.

	pushl		%edi
	notl		%ecx
	shrl		%eax
	orl			$0x80000000,	%eax
	shrl		%cl,			%eax	// q = (1:qs) >> (1 + i)
	movl		%eax,			%edi
	mull	 20(%esp)					// q*blo
	movl	 12(%esp),			%ebx
	movl	 16(%esp),			%ecx	// ECX:EBX = a
	subl		%eax,			%ebx
	sbbl		%edx,			%ecx	// ECX:EBX = a - q*blo
	movl	 24(%esp),			%eax
	imull		%edi,			%eax	// q*bhi
	subl		%eax,			%ecx	// ECX:EBX = a - q*b
	sbbl		$0,				%edi	// decrement q if remainder is negative
	xorl		%edx,			%edx
	movl		%edi,			%eax
	popl		%edi
	popl		%ebx
	retl

	
9:	/* High word of b is zero on this branch */

	movl	 12(%esp),			%eax	// Find qhi and rhi such that
	movl	 16(%esp),			%ecx	//
	xorl		%edx,			%edx	//		ahi = qhi*b + rhi	with	0 ≤ rhi < b
	divl		%ecx					//
	movl		%eax,			%ebx	//
	movl	  8(%esp),			%eax	// Find qlo such that
	divl		%ecx					//
	movl		%ebx,			%edx	//		rhi:alo = qlo*b + rlo  with 0 ≤ rlo < b
	popl		%ebx					//
	retl								// and return qhi:qlo
END_COMPILERRT_FUNCTION(__udivdi3)

#endif // __i386__

NO_EXEC_STACK_DIRECTIVE

