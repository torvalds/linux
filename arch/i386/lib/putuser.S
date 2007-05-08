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
#include <linux/linkage.h>
#include <asm/dwarf2.h>
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

#define ENTER	CFI_STARTPROC ; \
		pushl %ebx ; \
		CFI_ADJUST_CFA_OFFSET 4 ; \
		CFI_REL_OFFSET ebx, 0 ; \
		GET_THREAD_INFO(%ebx)
#define EXIT	popl %ebx ; \
		CFI_ADJUST_CFA_OFFSET -4 ; \
		CFI_RESTORE ebx ; \
		ret ; \
		CFI_ENDPROC

.text
ENTRY(__put_user_1)
	ENTER
	cmpl TI_addr_limit(%ebx),%ecx
	jae bad_put_user
1:	movb %al,(%ecx)
	xorl %eax,%eax
	EXIT
ENDPROC(__put_user_1)

ENTRY(__put_user_2)
	ENTER
	movl TI_addr_limit(%ebx),%ebx
	subl $1,%ebx
	cmpl %ebx,%ecx
	jae bad_put_user
2:	movw %ax,(%ecx)
	xorl %eax,%eax
	EXIT
ENDPROC(__put_user_2)

ENTRY(__put_user_4)
	ENTER
	movl TI_addr_limit(%ebx),%ebx
	subl $3,%ebx
	cmpl %ebx,%ecx
	jae bad_put_user
3:	movl %eax,(%ecx)
	xorl %eax,%eax
	EXIT
ENDPROC(__put_user_4)

ENTRY(__put_user_8)
	ENTER
	movl TI_addr_limit(%ebx),%ebx
	subl $7,%ebx
	cmpl %ebx,%ecx
	jae bad_put_user
4:	movl %eax,(%ecx)
5:	movl %edx,4(%ecx)
	xorl %eax,%eax
	EXIT
ENDPROC(__put_user_8)

bad_put_user:
	CFI_STARTPROC simple
	CFI_DEF_CFA esp, 2*4
	CFI_OFFSET eip, -1*4
	CFI_OFFSET ebx, -2*4
	movl $-14,%eax
	EXIT
END(bad_put_user)

.section __ex_table,"a"
	.long 1b,bad_put_user
	.long 2b,bad_put_user
	.long 3b,bad_put_user
	.long 4b,bad_put_user
	.long 5b,bad_put_user
.previous
