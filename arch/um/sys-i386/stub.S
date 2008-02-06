#include "uml-config.h"
#include "as-layout.h"

	.globl syscall_stub
.section .__syscall_stub, "x"

	.globl batch_syscall_stub
batch_syscall_stub:
	/* load pointer to first operation */
	mov	$(STUB_DATA+8), %esp

again:
	/* load length of additional data */
	mov	0x0(%esp), %eax

	/* if(length == 0) : end of list */
	/* write possible 0 to header */
	mov	%eax, STUB_DATA+4
	cmpl	$0, %eax
	jz	done

	/* save current pointer */
	mov	%esp, STUB_DATA+4

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

	/* check return value */
	pop	%ebx
	cmp	%ebx, %eax
	je	again

done:
	/* save return value */
	mov	%eax, STUB_DATA

	/* stop */
	int3
