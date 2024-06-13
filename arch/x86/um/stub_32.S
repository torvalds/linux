/* SPDX-License-Identifier: GPL-2.0 */
#include <as-layout.h>

.section .__syscall_stub, "ax"

	.globl batch_syscall_stub
batch_syscall_stub:
	/* %esp comes in as "top of page" */
	mov %esp, %ecx
	/* %esp has pointer to first operation */
	add $8, %esp
again:
	/* load length of additional data */
	mov	0x0(%esp), %eax

	/* if(length == 0) : end of list */
	/* write possible 0 to header */
	mov	%eax, 0x4(%ecx)
	cmpl	$0, %eax
	jz	done

	/* save current pointer */
	mov	%esp, 0x4(%ecx)

	/* skip additional data */
	add	%eax, %esp

	/* load syscall-# */
	pop	%eax

	/* load syscall params */
	pop	%ebx
	pop	%ecx
	pop	%edx
	pop	%esi
 	pop	%edi
	pop	%ebp

	/* execute syscall */
	int	$0x80

	/* restore top of page pointer in %ecx */
	mov	%esp, %ecx
	andl	$(~UM_KERN_PAGE_SIZE) + 1, %ecx

	/* check return value */
	pop	%ebx
	cmp	%ebx, %eax
	je	again

done:
	/* save return value */
	mov	%eax, (%ecx)

	/* stop */
	int3
