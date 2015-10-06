/*
 * Code for the vDSO.  This version uses the old int $0x80 method.
*/

#include <asm/dwarf2.h>

/*
 * First get the common code for the sigreturn entry points.
 * This must come first.
 */
#include "sigreturn.S"

	.text
	.globl __kernel_vsyscall
	.type __kernel_vsyscall,@function
	ALIGN
__kernel_vsyscall:
	CFI_STARTPROC
	int $0x80
	ret
	CFI_ENDPROC

	.size __kernel_vsyscall,.-__kernel_vsyscall
	.previous
