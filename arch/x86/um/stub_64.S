#include <as-layout.h>

	.globl syscall_stub
.section .__syscall_stub, "ax"
syscall_stub:
	syscall
	/* We don't have 64-bit constants, so this constructs the address
	 * we need.
	 */
	movq	$(STUB_DATA >> 32), %rbx
	salq	$32, %rbx
	movq	$(STUB_DATA & 0xffffffff), %rcx
	or	%rcx, %rbx
	movq	%rax, (%rbx)
	int3

	.globl batch_syscall_stub
batch_syscall_stub:
	mov	$(STUB_DATA >> 32), %rbx
	sal	$32, %rbx
	mov	$(STUB_DATA & 0xffffffff), %rax
	or	%rax, %rbx
	/* load pointer to first operation */
	mov	%rbx, %rsp
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
