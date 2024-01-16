/* SPDX-License-Identifier: GPL-2.0 */
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
	.previous

	.section .eh_frame,"a",@progbits
.LCIE1:
	.ualong	.LCIE1_end - .LCIE1_start
.LCIE1_start:
	.ualong	0		/* CIE ID */
	.byte	0x1		/* Version number */
	.string	"zRS"		/* NUL-terminated augmentation string */
	.uleb128 0x1		/* Code alignment factor */
	.sleb128 -4		/* Data alignment factor */
	.byte	0x11		/* Return address register column */
	.uleb128 0x1		/* Augmentation length and data */
	.byte 0x1b              /* DW_EH_PE_pcrel | DW_EH_PE_sdata4. */
	.byte	0xc, 0xf, 0x0	/* DW_CFA_def_cfa: r15 ofs 0 */

	.align 2
.LCIE1_end:

	.ualong	.LFDE0_end-.LFDE0_start	/* Length FDE0 */
.LFDE0_start:
	.ualong	.LFDE0_start-.LCIE1	/* CIE pointer */
	.ualong	.LSTART_sigreturn-.	/* PC-relative start address */
	.ualong	.LEND_sigreturn-.LSTART_sigreturn
	.uleb128 0			/* Augmentation */
	.align 2
.LFDE0_end:

	.ualong	.LFDE1_end-.LFDE1_start	/* Length FDE1 */
.LFDE1_start:
	.ualong	.LFDE1_start-.LCIE1	/* CIE pointer */
	.ualong	.LSTART_rt_sigreturn-.	/* PC-relative start address */
	.ualong	.LEND_rt_sigreturn-.LSTART_rt_sigreturn
	.uleb128 0			/* Augmentation */
	.align 2
.LFDE1_end:

	.previous
