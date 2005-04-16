/*
 * __put_user functions.
 *
 * (C) Copyright 2005 Linus Torvalds
 *
 * These functions have a non-standard call interface
 * to make them more efficient, especially as they
 * return an error value in addition to the "real"
 * return value.
 */
#include <asm/thread_info.h>


/*
 * __put_user_X
 *
 * Inputs:	%eax[:%edx] contains the data
 *		%ecx contains the address
 *
 * Outputs:	%eax is error code (0 or -EFAULT)
 *
 * These functions should not modify any other registers,
 * as they get called from within inline assembly.
 */

#define ENTER	pushl %ebx ; GET_THREAD_INFO(%ebx)
#define EXIT	popl %ebx ; ret

.text
.align 4
.globl __put_user_1
__put_user_1:
	ENTER
	cmpl TI_addr_limit(%ebx),%ecx
	jae bad_put_user
1:	movb %al,(%ecx)
	xorl %eax,%eax
	EXIT

.align 4
.globl __put_user_2
__put_user_2:
	ENTER
	movl TI_addr_limit(%ebx),%ebx
	subl $1,%ebx
	cmpl %ebx,%ecx
	jae bad_put_user
2:	movw %ax,(%ecx)
	xorl %eax,%eax
	EXIT

.align 4
.globl __put_user_4
__put_user_4:
	ENTER
	movl TI_addr_limit(%ebx),%ebx
	subl $3,%ebx
	cmpl %ebx,%ecx
	jae bad_put_user
3:	movl %eax,(%ecx)
	xorl %eax,%eax
	EXIT

.align 4
.globl __put_user_8
__put_user_8:
	ENTER
	movl TI_addr_limit(%ebx),%ebx
	subl $7,%ebx
	cmpl %ebx,%ecx
	jae bad_put_user
4:	movl %eax,(%ecx)
5:	movl %edx,4(%ecx)
	xorl %eax,%eax
	EXIT

bad_put_user:
	movl $-14,%eax
	EXIT

.section __ex_table,"a"
	.long 1b,bad_put_user
	.long 2b,bad_put_user
	.long 3b,bad_put_user
	.long 4b,bad_put_user
	.long 5b,bad_put_user
.previous
