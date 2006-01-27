/* Copyright 2002 Andi Kleen, SuSE Labs.
 * Subject to the GNU Public License v2.
 * 
 * Functions to copy from and to user space.		
 */		 

	#include <asm/current.h>
	#include <asm/asm-offsets.h>
	#include <asm/thread_info.h>

/* Standard copy_to_user with segment limit checking */		
	.globl copy_to_user
	.p2align 4	
copy_to_user:
	GET_THREAD_INFO(%rax)
	movq %rdi,%rcx
	addq %rdx,%rcx
	jc  bad_to_user
	cmpq threadinfo_addr_limit(%rax),%rcx
	jae bad_to_user
	jmp copy_user_generic

/* Standard copy_from_user with segment limit checking */	
	.globl copy_from_user
	.p2align 4	
copy_from_user:
	GET_THREAD_INFO(%rax)
	movq %rsi,%rcx
	addq %rdx,%rcx
	jc  bad_from_user
	cmpq threadinfo_addr_limit(%rax),%rcx
	jae  bad_from_user
	/* FALL THROUGH to copy_user_generic */
	
	.section .fixup,"ax"
	/* must zero dest */
bad_from_user:
	movl %edx,%ecx
	xorl %eax,%eax
	rep
	stosb
bad_to_user:
	movl	%edx,%eax
	ret
	.previous
	
		
/*
 * copy_user_generic - memory copy with exception handling.
 * 	
 * Input:	
 * rdi destination
 * rsi source
 * rdx count
 *
 * Only 4GB of copy is supported. This shouldn't be a problem
 * because the kernel normally only writes from/to page sized chunks
 * even if user space passed a longer buffer.
 * And more would be dangerous because both Intel and AMD have
 * errata with rep movsq > 4GB. If someone feels the need to fix
 * this please consider this.
 *
 * Output:		
 * eax uncopied bytes or 0 if successful.
 */

	.globl copy_user_generic
copy_user_generic:
	movl %edx,%ecx
	shrl $3,%ecx
	andl $7,%edx	
	jz   5f
1:	rep 
	movsq 
	movl %edx,%ecx
	xor %eax,%eax
2:	rep
	movsb
	ret
	/* align here? */
5: 	xorl %eax,%eax
6:	rep movsq
	ret

	.section .fixup,"ax"
3:	lea (%rdx,%rcx,8),%rax
	ret
4:	movl %ecx,%eax
	ret
	.previous

	.section __ex_table,"a"
	.quad 1b,3b
	.quad 2b,4b
	.quad 6b,4b
	.previous
