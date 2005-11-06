/*
 * Code for the vsyscall page.  This version uses the syscall instruction.
 */

#include <asm/ia32_unistd.h>
#include <asm/asm-offsets.h>
#include <asm/segment.h>

	.text
	.section .text.vsyscall,"ax"
	.globl __kernel_vsyscall
	.type __kernel_vsyscall,@function
__kernel_vsyscall:
.LSTART_vsyscall:
	push	%ebp
.Lpush_ebp:
	movl	%ecx, %ebp
	syscall
	movl	$__USER32_DS, %ecx
	movl	%ecx, %ss
	movl	%ebp, %ecx
	popl	%ebp
.Lpop_ebp:
	ret
.LEND_vsyscall:
	.size __kernel_vsyscall,.-.LSTART_vsyscall

	.section .eh_frame,"a",@progbits
.LSTARTFRAME:
	.long .LENDCIE-.LSTARTCIE
.LSTARTCIE:
	.long 0			/* CIE ID */
	.byte 1			/* Version number */
	.string "zR"		/* NUL-terminated augmentation string */
	.uleb128 1		/* Code alignment factor */
	.sleb128 -4		/* Data alignment factor */
	.byte 8			/* Return address register column */
	.uleb128 1		/* Augmentation value length */
	.byte 0x1b		/* DW_EH_PE_pcrel|DW_EH_PE_sdata4. */
	.byte 0x0c		/* DW_CFA_def_cfa */
	.uleb128 4
	.uleb128 4
	.byte 0x88		/* DW_CFA_offset, column 0x8 */
	.uleb128 1
	.align 4
.LENDCIE:

	.long .LENDFDE1-.LSTARTFDE1	/* Length FDE */
.LSTARTFDE1:
	.long .LSTARTFDE1-.LSTARTFRAME	/* CIE pointer */
	.long .LSTART_vsyscall-.	/* PC-relative start address */
	.long .LEND_vsyscall-.LSTART_vsyscall
	.uleb128 0			/* Augmentation length */
	/* What follows are the instructions for the table generation.
	   We have to record all changes of the stack pointer.  */
	.byte 0x40 + .Lpush_ebp-.LSTART_vsyscall /* DW_CFA_advance_loc */
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.uleb128 8
	.byte 0x85, 0x02	/* DW_CFA_offset %ebp -8 */
	.byte 0x40 + .Lpop_ebp-.Lpush_ebp /* DW_CFA_advance_loc */
	.byte 0xc5		/* DW_CFA_restore %ebp */
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.uleb128 4
	.align 4
.LENDFDE1:

#define SYSCALL_ENTER_KERNEL	syscall
#include "vsyscall-sigreturn.S"
