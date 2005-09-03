#include "uml-config.h"

	.globl syscall_stub
.section .__syscall_stub, "x"
syscall_stub:
	syscall
	/* We don't have 64-bit constants, so this constructs the address
	 * we need.
	 */
	movq	$(UML_CONFIG_STUB_DATA >> 32), %rbx
	salq	$32, %rbx
	movq	$(UML_CONFIG_STUB_DATA & 0xffffffff), %rcx
	or	%rcx, %rbx
	movq	%rax, (%rbx)
	int3

	.globl batch_syscall_stub
batch_syscall_stub:
	movq	$(UML_CONFIG_STUB_DATA >> 32), %rbx
	salq	$32, %rbx
	movq	$(UML_CONFIG_STUB_DATA & 0xffffffff), %rcx
	or	%rcx, %rbx
	movq	%rbx, %rsp
again:	pop	%rax
	cmpq	$0, %rax
jz	done
	pop	%rdi
	pop	%rsi
	pop	%rdx
	pop	%r10
 	pop	%r8
	pop	%r9
	syscall
	mov	%rax, (%rbx)
	jmp	again
done:	int3
