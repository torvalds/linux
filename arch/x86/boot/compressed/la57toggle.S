/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/boot.h>
#include <asm/msr.h>
#include <asm/processor-flags.h>
#include "pgtable.h"

/*
 * This is the 32-bit trampoline that will be copied over to low memory. It
 * will be called using the ordinary 64-bit calling convention from code
 * running in 64-bit mode.
 *
 * Return address is at the top of the stack (might be above 4G).
 * The first argument (EDI) contains the address of the temporary PGD level
 * page table in 32-bit addressable memory which will be programmed into
 * register CR3.
 */

	.section ".rodata", "a", @progbits
SYM_CODE_START(trampoline_32bit_src)
	/*
	 * Preserve callee save 64-bit registers on the stack: this is
	 * necessary because the architecture does not guarantee that GPRs will
	 * retain their full 64-bit values across a 32-bit mode switch.
	 */
	pushq	%r15
	pushq	%r14
	pushq	%r13
	pushq	%r12
	pushq	%rbp
	pushq	%rbx

	/* Preserve top half of RSP in a legacy mode GPR to avoid truncation */
	movq	%rsp, %rbx
	shrq	$32, %rbx

	/* Switch to compatibility mode (CS.L = 0 CS.D = 1) via far return */
	pushq	$__KERNEL32_CS
	leaq	0f(%rip), %rax
	pushq	%rax
	lretq

	/*
	 * The 32-bit code below will do a far jump back to long mode and end
	 * up here after reconfiguring the number of paging levels. First, the
	 * stack pointer needs to be restored to its full 64-bit value before
	 * the callee save register contents can be popped from the stack.
	 */
.Lret:
	shlq	$32, %rbx
	orq	%rbx, %rsp

	/* Restore the preserved 64-bit registers */
	popq	%rbx
	popq	%rbp
	popq	%r12
	popq	%r13
	popq	%r14
	popq	%r15
	retq

	.code32
0:
	/* Disable paging */
	movl	%cr0, %eax
	btrl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0

	/* Point CR3 to the trampoline's new top level page table */
	movl	%edi, %cr3

	/* Set EFER.LME=1 as a precaution in case hypervsior pulls the rug */
	movl	$MSR_EFER, %ecx
	rdmsr
	btsl	$_EFER_LME, %eax
	/* Avoid writing EFER if no change was made (for TDX guest) */
	jc	1f
	wrmsr
1:
	/* Toggle CR4.LA57 */
	movl	%cr4, %eax
	btcl	$X86_CR4_LA57_BIT, %eax
	movl	%eax, %cr4

	/* Enable paging again. */
	movl	%cr0, %eax
	btsl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0

	/*
	 * Return to the 64-bit calling code using LJMP rather than LRET, to
	 * avoid the need for a 32-bit addressable stack. The destination
	 * address will be adjusted after the template code is copied into a
	 * 32-bit addressable buffer.
	 */
.Ljmp:	ljmpl	$__KERNEL_CS, $(.Lret - trampoline_32bit_src)
SYM_CODE_END(trampoline_32bit_src)

/*
 * This symbol is placed right after trampoline_32bit_src() so its address can
 * be used to infer the size of the trampoline code.
 */
SYM_DATA(trampoline_ljmp_imm_offset, .word  .Ljmp + 1 - trampoline_32bit_src)

	/*
         * The trampoline code has a size limit.
         * Make sure we fail to compile if the trampoline code grows
         * beyond TRAMPOLINE_32BIT_CODE_SIZE bytes.
	 */
	.org	trampoline_32bit_src + TRAMPOLINE_32BIT_CODE_SIZE
