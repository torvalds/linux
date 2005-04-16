/*
 * __put_user functions.
 *
 * (C) Copyright 1998 Linus Torvalds
 * (C) Copyright 2005 Andi Kleen
 *
 * These functions have a non-standard call interface
 * to make them more efficient, especially as they
 * return an error value in addition to the "real"
 * return value.
 */

/*
 * __put_user_X
 *
 * Inputs:	%rcx contains the address
 *		%rdx contains new value
 *
 * Outputs:	%rax is error code (0 or -EFAULT)
 *
 * %r8 is destroyed.
 *
 * These functions should not modify any other registers,
 * as they get called from within inline assembly.
 */

#include <linux/linkage.h>
#include <asm/page.h>
#include <asm/errno.h>
#include <asm/offset.h>
#include <asm/thread_info.h>

	.text
	.p2align 4
.globl __put_user_1
__put_user_1:
	GET_THREAD_INFO(%r8)
	cmpq threadinfo_addr_limit(%r8),%rcx
	jae bad_put_user
1:	movb %dl,(%rcx)
	xorl %eax,%eax
	ret

	.p2align 4
.globl __put_user_2
__put_user_2:
	GET_THREAD_INFO(%r8)
	addq $1,%rcx
	jc bad_put_user
	cmpq threadinfo_addr_limit(%r8),%rcx
	jae	 bad_put_user
2:	movw %dx,-1(%rcx)
	xorl %eax,%eax
	ret

	.p2align 4
.globl __put_user_4
__put_user_4:
	GET_THREAD_INFO(%r8)
	addq $3,%rcx
	jc bad_put_user
	cmpq threadinfo_addr_limit(%r8),%rcx
	jae bad_put_user
3:	movl %edx,-3(%rcx)
	xorl %eax,%eax
	ret

	.p2align 4
.globl __put_user_8
__put_user_8:
	GET_THREAD_INFO(%r8)
	addq $7,%rcx
	jc bad_put_user
	cmpq threadinfo_addr_limit(%r8),%rcx
	jae	bad_put_user
4:	movq %rdx,-7(%rcx)
	xorl %eax,%eax
	ret

bad_put_user:
	movq $(-EFAULT),%rax
	ret

.section __ex_table,"a"
	.quad 1b,bad_put_user
	.quad 2b,bad_put_user
	.quad 3b,bad_put_user
	.quad 4b,bad_put_user
.previous
