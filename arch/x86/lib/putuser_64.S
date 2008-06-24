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
 * %rbx is destroyed.
 *
 * These functions should not modify any other registers,
 * as they get called from within inline assembly.
 */

#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/page.h>
#include <asm/errno.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>

#define ENTER   CFI_STARTPROC ; \
                GET_THREAD_INFO(%rbx)
#define EXIT    ret ; \
                CFI_ENDPROC

	.text
ENTRY(__put_user_1)
	ENTER
	cmp TI_addr_limit(%rbx),%rcx
	jae bad_put_user
1:	movb %al,(%rcx)
	xor %eax,%eax
	EXIT
ENDPROC(__put_user_1)

ENTRY(__put_user_2)
	ENTER
	mov TI_addr_limit(%rbx),%rbx
	sub  $1, %rbx
	cmp %rbx ,%rcx
	jae  bad_put_user
2:	movw %ax,(%rcx)
	xor %eax,%eax
	EXIT
ENDPROC(__put_user_2)

ENTRY(__put_user_4)
	ENTER
	mov TI_addr_limit(%rbx),%rbx
	sub  $3, %rbx
	cmp  %rbx, %rcx
	jae bad_put_user
3:	movl %eax,(%rcx)
	xor %eax,%eax
	EXIT
ENDPROC(__put_user_4)

ENTRY(__put_user_8)
	ENTER
	mov TI_addr_limit(%rbx),%rbx
	sub  $7, %rbx
	cmp  %rbx, %rcx
	jae bad_put_user
4:	movq %rax,(%rcx)
	xor %eax,%eax
	EXIT
ENDPROC(__put_user_8)

bad_put_user:
	CFI_STARTPROC
	mov $(-EFAULT),%rax
	EXIT
END(bad_put_user)

.section __ex_table,"a"
	.quad 1b,bad_put_user
	.quad 2b,bad_put_user
	.quad 3b,bad_put_user
	.quad 4b,bad_put_user
.previous
