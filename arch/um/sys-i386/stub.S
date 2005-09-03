#include "uml-config.h"

	.globl syscall_stub
.section .__syscall_stub, "x"
syscall_stub:
	int 	$0x80
	mov	%eax, UML_CONFIG_STUB_DATA
	int3

	.globl batch_syscall_stub
batch_syscall_stub:
	mov	$UML_CONFIG_STUB_DATA, %esp
again:	pop	%eax
	cmpl	$0, %eax
	jz	done
	pop	%ebx
	pop	%ecx
	pop	%edx
	pop	%esi
 	pop	%edi
	pop	%ebp
	int	$0x80
	mov	%eax, UML_CONFIG_STUB_DATA
	jmp	again
done:	int3
