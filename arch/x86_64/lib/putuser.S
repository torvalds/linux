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
#include <asm/dwarf2.h>
#include <asm/page.h>
#include <asm/errno.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>

	.text
ENTRY(__put_user_1)
	CFI_STARTPROC
	GET_THREAD_INFO(%r8)
	cmpq threadinfo_addr_limit(%r8),%rcx
	jae bad_put_user
1:	movb %dl,(%rcx)
	xorl %eax,%eax
	ret
	CFI_ENDPROC
ENDPROC(__put_user_1)

ENTRY(__put_user_2)
	CFI_STARTPROC
	GET_THREAD_INFO(%r8)
	addq $1,%rcx
	jc 20f
	cmpq threadinfo_addr_limit(%r8),%rcx
	jae 20f
	decq %rcx
2:	movw %dx,(%rcx)
	xorl %eax,%eax
	ret
20:	decq %rcx
	jmp bad_put_user
	CFI_ENDPROC
ENDPROC(__put_user_2)

ENTRY(__put_user_4)
	CFI_STARTPROC
	GET_THREAD_INFO(%r8)
	addq $3,%rcx
	jc 30f
	cmpq threadinfo_addr_limit(%r8),%rcx
	jae 30f
	subq $3,%rcx
3:	movl %edx,(%rcx)
	xorl %eax,%eax
	ret
30:	subq $3,%rcx
	jmp bad_put_user
	CFI_ENDPROC
ENDPROC(__put_user_4)

ENTRY(__put_user_8)
	CFI_STARTPROC
	GET_THREAD_INFO(%r8)
	addq $7,%rcx
	jc 40f
	cmpq threadinfo_addr_limit(%r8),%rcx
	jae 40f
	subq $7,%rcx
4:	movq %rdx,(%rcx)
	xorl %eax,%eax
	ret
40:	subq $7,%rcx
	jmp bad_put_user
	CFI_ENDPROC
ENDPROC(__put_user_8)

bad_put_user:
	CFI_STARTPROC
	movq $(-EFAULT),%rax
	ret
	CFI_ENDPROC
END(bad_put_user)

.section __ex_table,"a"
	.quad 1b,bad_put_user
	.quad 2b,bad_put_user
	.quad 3b,bad_put_user
	.quad 4b,bad_put_user
.previous
