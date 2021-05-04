/* SPDX-License-Identifier: GPL-2.0 */
#include <as-layout.h>

.section .__syscall_stub, "ax"
	.globl batch_syscall_stub
batch_syscall_stub:
	/* %rsp has the pointer to first operation */
	mov	%rsp, %rbx
	add	$0x10, %rsp
again:
	/* load length of additional data */
	mov	0x0(%rsp), %rax

	/* if(length == 0) : end of list */
	/* write possible 0 to header */
	mov	%rax, 8(%rbx)
	cmp	$0, %rax
	jz	done

	/* save current pointer */
	mov	%rsp, 8(%rbx)

	/* skip additional data */
	add	%rax, %rsp

	/* load syscall-# */
	pop	%rax

	/* load syscall params */
	pop	%rdi
	pop	%rsi
	pop	%rdx
	pop	%r10
 	pop	%r8
	pop	%r9

	/* execute syscall */
	syscall

	/* check return value */
	pop	%rcx
	cmp	%rcx, %rax
	je	again

done:
	/* save return value */
	mov	%rax, (%rbx)

	/* stop */
	int3
