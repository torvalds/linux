/*
 * __get_user functions.
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
 * __get_user_X
 *
 * Inputs:	%rcx contains the address.
 *		The register is modified, but all changes are undone
 *		before returning because the C code doesn't know about it.
 *
 * Outputs:	%rax is error code (0 or -EFAULT)
 *		%rdx contains zero-extended value
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
ENTRY(__get_user_1)
	CFI_STARTPROC
	GET_THREAD_INFO(%r8)
	cmpq threadinfo_addr_limit(%r8),%rcx
	jae bad_get_user
1:	movzb (%rcx),%edx
	xorl %eax,%eax
	ret
	CFI_ENDPROC
ENDPROC(__get_user_1)

ENTRY(__get_user_2)
	CFI_STARTPROC
	GET_THREAD_INFO(%r8)
	addq $1,%rcx
	jc 20f
	cmpq threadinfo_addr_limit(%r8),%rcx
	jae 20f
	decq   %rcx
2:	movzwl (%rcx),%edx
	xorl %eax,%eax
	ret
20:	decq    %rcx
	jmp	bad_get_user
	CFI_ENDPROC
ENDPROC(__get_user_2)

ENTRY(__get_user_4)
	CFI_STARTPROC
	GET_THREAD_INFO(%r8)
	addq $3,%rcx
	jc 30f
	cmpq threadinfo_addr_limit(%r8),%rcx
	jae 30f
	subq $3,%rcx
3:	movl (%rcx),%edx
	xorl %eax,%eax
	ret
30:	subq $3,%rcx
	jmp bad_get_user
	CFI_ENDPROC
ENDPROC(__get_user_4)

ENTRY(__get_user_8)
	CFI_STARTPROC
	GET_THREAD_INFO(%r8)
	addq $7,%rcx
	jc 40f
	cmpq threadinfo_addr_limit(%r8),%rcx
	jae	40f
	subq	$7,%rcx
4:	movq (%rcx),%rdx
	xorl %eax,%eax
	ret
40:	subq $7,%rcx
	jmp bad_get_user
	CFI_ENDPROC
ENDPROC(__get_user_8)

bad_get_user:
	CFI_STARTPROC
	xorl %edx,%edx
	movq $(-EFAULT),%rax
	ret
	CFI_ENDPROC
END(bad_get_user)

.section __ex_table,"a"
	.quad 1b,bad_get_user
	.quad 2b,bad_get_user
	.quad 3b,bad_get_user
	.quad 4b,bad_get_user
.previous
