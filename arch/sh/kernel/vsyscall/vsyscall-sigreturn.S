#include <asm/unistd.h>

	.text
	.balign 32
	.globl __kernel_sigreturn
	.type __kernel_sigreturn,@function
__kernel_sigreturn:
.LSTART_sigreturn:
	mov.w	1f, r3
	trapa	#0x10
	or	r0, r0
	or	r0, r0
	or	r0, r0
	or	r0, r0
	or	r0, r0

1:	.short	__NR_sigreturn
.LEND_sigreturn:
	.size __kernel_sigreturn,.-.LSTART_sigreturn

	.balign 32
	.globl __kernel_rt_sigreturn
	.type __kernel_rt_sigreturn,@function
__kernel_rt_sigreturn:
.LSTART_rt_sigreturn:
	mov.w	1f, r3
	trapa	#0x10
	or	r0, r0
	or	r0, r0
	or	r0, r0
	or	r0, r0
	or	r0, r0

1:	.short	__NR_rt_sigreturn
.LEND_rt_sigreturn:
	.size __kernel_rt_sigreturn,.-.LSTART_rt_sigreturn

	.section .eh_frame,"a",@progbits
	.previous
