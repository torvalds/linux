/*
 * Common code for the sigreturn entry points on the vsyscall page.
 * So far this code is the same for both int80 and sysenter versions.
 * This file is #include'd by vsyscall-*.S to define them after the
 * vsyscall entry point.  The kernel assumes that the addresses of these
 * routines are constant for all vsyscall implementations.
 */

#include <asm/unistd.h>
#include <asm/asm-offsets.h>


/* XXX
   Should these be named "_sigtramp" or something?
*/

	.text
	.org __kernel_vsyscall+32,0x90
	.globl __kernel_sigreturn
	.type __kernel_sigreturn,@function
__kernel_sigreturn:
.LSTART_sigreturn:
	popl %eax		/* XXX does this mean it needs unwind info? */
	movl $__NR_sigreturn, %eax
	int $0x80
.LEND_sigreturn:
	.size __kernel_sigreturn,.-.LSTART_sigreturn

	.balign 32
	.globl __kernel_rt_sigreturn
	.type __kernel_rt_sigreturn,@function
__kernel_rt_sigreturn:
.LSTART_rt_sigreturn:
	movl $__NR_rt_sigreturn, %eax
	int $0x80
.LEND_rt_sigreturn:
	.size __kernel_rt_sigreturn,.-.LSTART_rt_sigreturn
	.balign 32
	.previous

	.section .eh_frame,"a",@progbits
.LSTARTFRAMEDLSI1:
	.long .LENDCIEDLSI1-.LSTARTCIEDLSI1
.LSTARTCIEDLSI1:
	.long 0			/* CIE ID */
	.byte 1			/* Version number */
	.string "zRS"		/* NUL-terminated augmentation string */
	.uleb128 1		/* Code alignment factor */
	.sleb128 -4		/* Data alignment factor */
	.byte 8			/* Return address register column */
	.uleb128 1		/* Augmentation value length */
	.byte 0x1b		/* DW_EH_PE_pcrel|DW_EH_PE_sdata4. */
	.byte 0			/* DW_CFA_nop */
	.align 4
.LENDCIEDLSI1:
	.long .LENDFDEDLSI1-.LSTARTFDEDLSI1 /* Length FDE */
.LSTARTFDEDLSI1:
	.long .LSTARTFDEDLSI1-.LSTARTFRAMEDLSI1 /* CIE pointer */
	/* HACK: The dwarf2 unwind routines will subtract 1 from the
	   return address to get an address in the middle of the
	   presumed call instruction.  Since we didn't get here via
	   a call, we need to include the nop before the real start
	   to make up for it.  */
	.long .LSTART_sigreturn-1-.	/* PC-relative start address */
	.long .LEND_sigreturn-.LSTART_sigreturn+1
	.uleb128 0			/* Augmentation */
	/* What follows are the instructions for the table generation.
	   We record the locations of each register saved.  This is
	   complicated by the fact that the "CFA" is always assumed to
	   be the value of the stack pointer in the caller.  This means
	   that we must define the CFA of this body of code to be the
	   saved value of the stack pointer in the sigcontext.  Which
	   also means that there is no fixed relation to the other 
	   saved registers, which means that we must use DW_CFA_expression
	   to compute their addresses.  It also means that when we 
	   adjust the stack with the popl, we have to do it all over again.  */

#define do_cfa_expr(offset)						\
	.byte 0x0f;			/* DW_CFA_def_cfa_expression */	\
	.uleb128 1f-0f;			/*   length */			\
0:	.byte 0x74;			/*     DW_OP_breg4 */		\
	.sleb128 offset;		/*      offset */		\
	.byte 0x06;			/*     DW_OP_deref */		\
1:

#define do_expr(regno, offset)						\
	.byte 0x10;			/* DW_CFA_expression */		\
	.uleb128 regno;			/*   regno */			\
	.uleb128 1f-0f;			/*   length */			\
0:	.byte 0x74;			/*     DW_OP_breg4 */		\
	.sleb128 offset;		/*       offset */		\
1:

	do_cfa_expr(SIGCONTEXT_esp+4)
	do_expr(0, SIGCONTEXT_eax+4)
	do_expr(1, SIGCONTEXT_ecx+4)
	do_expr(2, SIGCONTEXT_edx+4)
	do_expr(3, SIGCONTEXT_ebx+4)
	do_expr(5, SIGCONTEXT_ebp+4)
	do_expr(6, SIGCONTEXT_esi+4)
	do_expr(7, SIGCONTEXT_edi+4)
	do_expr(8, SIGCONTEXT_eip+4)

	.byte 0x42	/* DW_CFA_advance_loc 2 -- nop; popl eax. */

	do_cfa_expr(SIGCONTEXT_esp)
	do_expr(0, SIGCONTEXT_eax)
	do_expr(1, SIGCONTEXT_ecx)
	do_expr(2, SIGCONTEXT_edx)
	do_expr(3, SIGCONTEXT_ebx)
	do_expr(5, SIGCONTEXT_ebp)
	do_expr(6, SIGCONTEXT_esi)
	do_expr(7, SIGCONTEXT_edi)
	do_expr(8, SIGCONTEXT_eip)

	.align 4
.LENDFDEDLSI1:

	.long .LENDFDEDLSI2-.LSTARTFDEDLSI2 /* Length FDE */
.LSTARTFDEDLSI2:
	.long .LSTARTFDEDLSI2-.LSTARTFRAMEDLSI1 /* CIE pointer */
	/* HACK: See above wrt unwind library assumptions.  */
	.long .LSTART_rt_sigreturn-1-.	/* PC-relative start address */
	.long .LEND_rt_sigreturn-.LSTART_rt_sigreturn+1
	.uleb128 0			/* Augmentation */
	/* What follows are the instructions for the table generation.
	   We record the locations of each register saved.  This is
	   slightly less complicated than the above, since we don't
	   modify the stack pointer in the process.  */

	do_cfa_expr(RT_SIGFRAME_sigcontext-4 + SIGCONTEXT_esp)
	do_expr(0, RT_SIGFRAME_sigcontext-4 + SIGCONTEXT_eax)
	do_expr(1, RT_SIGFRAME_sigcontext-4 + SIGCONTEXT_ecx)
	do_expr(2, RT_SIGFRAME_sigcontext-4 + SIGCONTEXT_edx)
	do_expr(3, RT_SIGFRAME_sigcontext-4 + SIGCONTEXT_ebx)
	do_expr(5, RT_SIGFRAME_sigcontext-4 + SIGCONTEXT_ebp)
	do_expr(6, RT_SIGFRAME_sigcontext-4 + SIGCONTEXT_esi)
	do_expr(7, RT_SIGFRAME_sigcontext-4 + SIGCONTEXT_edi)
	do_expr(8, RT_SIGFRAME_sigcontext-4 + SIGCONTEXT_eip)

	.align 4
.LENDFDEDLSI2:
	.previous
