/* strncpy_from_user.S: Sparc strncpy from userspace.
 *
 *  Copyright(C) 1996 David S. Miller
 */

#include <asm/ptrace.h>
#include <asm/errno.h>

	.text
	.align	4

	/* Must return:
	 *
	 * -EFAULT		for an exception
	 * count		if we hit the buffer limit
	 * bytes copied		if we hit a null byte
	 */

	.globl	__strncpy_from_user
__strncpy_from_user:
	/* %o0=dest, %o1=src, %o2=count */
	mov	%o2, %o3
1:
	subcc	%o2, 1, %o2
	bneg	2f
	 nop
10:
	ldub	[%o1], %o4
	add	%o0, 1, %o0
	cmp	%o4, 0
	add	%o1, 1, %o1
	bne	1b
	 stb	%o4, [%o0 - 1]
2:
	add	%o2, 1, %o0
	retl
	 sub	%o3, %o0, %o0

	.section .fixup,#alloc,#execinstr
	.align	4
4:
	retl
	 mov	-EFAULT, %o0

	.section __ex_table,#alloc
	.align	4
	.word	10b, 4b
