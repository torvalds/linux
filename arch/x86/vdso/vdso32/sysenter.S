/*
 * Code for the vDSO.  This version uses the sysenter instruction.
 *
 * First get the common code for the sigreturn entry points.
 * This must come first.
 */
#include "sigreturn.S"

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
 *
 * You can not use this vsyscall for the clone() syscall because the
 * three words on the parent stack do not get copied to the child.
 */
	.text
	.globl __kernel_vsyscall
	.type __kernel_vsyscall,@function
	ALIGN
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

	/* 14: System call restart point is here! (SYSENTER_RETURN-2) */
	int $0x80
	/* 16: System call normal return point is here! */
VDSO32_SYSENTER_RETURN:	/* Symbol used by sysenter.c via vdso32-syms.h */
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
	.byte 0x40 + (.Lpush_ecx-.LSTART_vsyscall) /* DW_CFA_advance_loc */
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.byte 0x08		/* RA at offset 8 now */
	.byte 0x40 + (.Lpush_edx-.Lpush_ecx) /* DW_CFA_advance_loc */
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.byte 0x0c		/* RA at offset 12 now */
	.byte 0x40 + (.Lenter_kernel-.Lpush_edx) /* DW_CFA_advance_loc */
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.byte 0x10		/* RA at offset 16 now */
	.byte 0x85, 0x04	/* DW_CFA_offset %ebp -16 */
	/* Finally the epilogue.  */
	.byte 0x40 + (.Lpop_ebp-.Lenter_kernel)	/* DW_CFA_advance_loc */
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.byte 0x0c		/* RA at offset 12 now */
	.byte 0xc5		/* DW_CFA_restore %ebp */
	.byte 0x40 + (.Lpop_edx-.Lpop_ebp) /* DW_CFA_advance_loc */
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.byte 0x08		/* RA at offset 8 now */
	.byte 0x40 + (.Lpop_ecx-.Lpop_edx) /* DW_CFA_advance_loc */
	.byte 0x0e		/* DW_CFA_def_cfa_offset */
	.byte 0x04		/* RA at offset 4 now */
	.align 4
.LENDFDEDLSI:
	.previous

	/*
	 * Emit a symbol with the size of this .eh_frame data,
	 * to verify it matches the other versions.
	 */
VDSO32_vsyscall_eh_frame_size = (.LENDFDEDLSI-.LSTARTFRAMEDLSI)
