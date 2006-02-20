/*
 * Code for the vsyscall page.  This version uses the sysenter instruction.
 *
 * NOTE:
 * 1) __kernel_vsyscall _must_ be first in this page.
 * 2) there are alignment constraints on this stub, see vsyscall-sigreturn.S
 *    for details.
 */

/*
 * The caller puts arg2 in %ecx, which gets pushed. The kernel will use
 * %ecx itself for arg2. The pushing is because the sysexit instruction
 * (found in entry.S) requires that we clobber %ecx with the desired %esp.
 * User code might expect that %ecx is unclobbered though, as it would be
 * for returning via the iret instruction, so we must push and pop.
 *
 * The caller puts arg3 in %edx, which the sysexit instruction requires
 * for %eip. Thus, exactly as for arg2, we must push and pop.
 *
 * Arg6 is different. The caller puts arg6 in %ebp. Since the sysenter
 * instruction clobbers %esp, the user's %esp won't even survive entry
 * into the kernel. We store %esp in %ebp. Code in entry.S must fetch
 * arg6 from the stack.
 */
	.text
	.globl __kernel_vsyscall
	.type __kernel_vsyscall,@function
__kernel_vsyscall:
.LSTART_vsyscall:
	push %ecx
.Lpush_ecx:
	push %edx
.Lpush_edx:
	push %ebp
.Lenter_kernel:
	movl %esp,%ebp
	sysenter

	/* 7: align return point with nop's to make disassembly easier */
	.space 7,0x90

	/* 14: System call restart point is here! (SYSENTER_RETURN - 2) */
	jmp .Lenter_kernel
	/* 16: System call normal return point is here! */
	.globl SYSENTER_RETURN	/* Symbol used by entry.S.  */
SYSENTER_RETURN:
	pop %ebp
.Lpop_ebp:
	pop %edx
.Lpop_edx:
	pop %ecx
.Lpop_ecx:
	ret
.LEND_vsyscall:
	.size __kernel_vsyscall,.-.LSTART_vsyscall
	.previous

	.section .eh_frame,"a",@progbits
.LSTARTFRAMEDLSI:
	.long .LENDCIEDLSI-.LSTARTCIEDLSI
.LSTARTCIEDLSI:
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
.LENDCIEDLSI:
	.long .LENDFDEDLSI-.LSTARTFDEDLSI /* Length FDE */
.LSTARTFDEDLSI:
	.long .LSTARTFDEDLSI-.LSTARTFRAMEDLSI /* CIE pointer */
	.long .LSTART_vsyscall-.	/* PC-relative start address */
	.long .LEND_vsyscall-.LSTART_vsyscall
	.uleb128 0
	/* What follows are the instructions for the table generation.
	   We have to record all changes of the stack pointer.  */
	.byte 0x04		/* DW_CFA_advance_loc4 */
	.long .Lpush_ecx-.LSTART_vsyscall
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.byte 0x08		/* RA at offset 8 now */
	.byte 0x04		/* DW_CFA_advance_loc4 */
	.long .Lpush_edx-.Lpush_ecx
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.byte 0x0c		/* RA at offset 12 now */
	.byte 0x04		/* DW_CFA_advance_loc4 */
	.long .Lenter_kernel-.Lpush_edx
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.byte 0x10		/* RA at offset 16 now */
	.byte 0x85, 0x04	/* DW_CFA_offset %ebp -16 */
	/* Finally the epilogue.  */
	.byte 0x04		/* DW_CFA_advance_loc4 */
	.long .Lpop_ebp-.Lenter_kernel
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.byte 0x0c		/* RA at offset 12 now */
	.byte 0xc5		/* DW_CFA_restore %ebp */
	.byte 0x04		/* DW_CFA_advance_loc4 */
	.long .Lpop_edx-.Lpop_ebp
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.byte 0x08		/* RA at offset 8 now */
	.byte 0x04		/* DW_CFA_advance_loc4 */
	.long .Lpop_ecx-.Lpop_edx
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.byte 0x04		/* RA at offset 4 now */
	.align 4
.LENDFDEDLSI:
	.previous

/*
 * Get the common code for the sigreturn entry points.
 */
#include "vsyscall-sigreturn.S"
