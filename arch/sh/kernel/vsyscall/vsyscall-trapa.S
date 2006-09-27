	.text
	.globl __kernel_vsyscall
	.type __kernel_vsyscall,@function
__kernel_vsyscall:
.LSTART_vsyscall:
	/* XXX: We'll have to do something here once we opt to use the vDSO
	 * page for something other than the signal trampoline.. as well as
	 * fill out .eh_frame -- PFM. */
.LEND_vsyscall:
	.size __kernel_vsyscall,.-.LSTART_vsyscall
	.previous

	.section .eh_frame,"a",@progbits
.LCIE:
	.ualong	.LCIE_end - .LCIE_start
.LCIE_start:
	.ualong	0		/* CIE ID */
	.byte	0x1		/* Version number */
	.string	"zRS"		/* NUL-terminated augmentation string */
	.uleb128 0x1		/* Code alignment factor */
	.sleb128 -4		/* Data alignment factor */
	.byte	0x11		/* Return address register column */
				/* Augmentation length and data (none) */
	.byte	0xc		/* DW_CFA_def_cfa */
	.uleb128 0xf		/* r15 */
	.uleb128 0x0		/* offset 0 */

	.align 2
.LCIE_end:

	.ualong	.LFDE_end-.LFDE_start	/* Length FDE */
.LFDE_start:
	.ualong	.LCIE			/* CIE pointer */
	.ualong	.LSTART_vsyscall-.	/* start address */
	.ualong	.LEND_vsyscall-.LSTART_vsyscall
	.uleb128 0
	.align 2
.LFDE_end:
	.previous

/* Get the common code for the sigreturn entry points */
#include "vsyscall-sigreturn.S"
