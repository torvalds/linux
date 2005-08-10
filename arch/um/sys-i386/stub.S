#include "uml-config.h"

	.globl syscall_stub
.section .__syscall_stub, "x"
syscall_stub:
	int 	$0x80
	mov	%eax, UML_CONFIG_STUB_DATA
	int3
