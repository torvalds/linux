/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#include <linux/linkage.h>
#include <linux/pgtable.h>
#include <asm/page.h>
#include <asm/processor-flags.h>
#include <asm/msr-index.h>
#include <asm/nospec-branch.h>

	.text
	.code64
SYM_FUNC_START(sme_encrypt_execute)

	/*
	 * Entry parameters:
	 *   RDI - virtual address for the encrypted mapping
	 *   RSI - virtual address for the decrypted mapping
	 *   RDX - length to encrypt
	 *   RCX - virtual address of the encryption workarea, including:
	 *     - stack page (PAGE_SIZE)
	 *     - encryption routine page (PAGE_SIZE)
	 *     - intermediate copy buffer (PMD_PAGE_SIZE)
	 *    R8 - physical address of the pagetables to use for encryption
	 */

	push	%rbp
	movq	%rsp, %rbp		/* RBP now has original stack pointer */

	/* Set up a one page stack in the non-encrypted memory area */
	movq	%rcx, %rax		/* Workarea stack page */
	leaq	PAGE_SIZE(%rax), %rsp	/* Set new stack pointer */
	addq	$PAGE_SIZE, %rax	/* Workarea encryption routine */

	push	%r12
	movq	%rdi, %r10		/* Encrypted area */
	movq	%rsi, %r11		/* Decrypted area */
	movq	%rdx, %r12		/* Area length */

	/* Copy encryption routine into the workarea */
	movq	%rax, %rdi				/* Workarea encryption routine */
	leaq	__enc_copy(%rip), %rsi			/* Encryption routine */
	movq	$(.L__enc_copy_end - __enc_copy), %rcx	/* Encryption routine length */
	rep	movsb

	/* Setup registers for call */
	movq	%r10, %rdi		/* Encrypted area */
	movq	%r11, %rsi		/* Decrypted area */
	movq	%r8, %rdx		/* Pagetables used for encryption */
	movq	%r12, %rcx		/* Area length */
	movq	%rax, %r8		/* Workarea encryption routine */
	addq	$PAGE_SIZE, %r8		/* Workarea intermediate copy buffer */

	ANNOTATE_RETPOLINE_SAFE
	call	*%rax			/* Call the encryption routine */

	pop	%r12

	movq	%rbp, %rsp		/* Restore original stack pointer */
	pop	%rbp

	ret
SYM_FUNC_END(sme_encrypt_execute)

SYM_FUNC_START(__enc_copy)
/*
 * Routine used to encrypt memory in place.
 *   This routine must be run outside of the kernel proper since
 *   the kernel will be encrypted during the process. So this
 *   routine is defined here and then copied to an area outside
 *   of the kernel where it will remain and run decrypted
 *   during execution.
 *
 *   On entry the registers must be:
 *     RDI - virtual address for the encrypted mapping
 *     RSI - virtual address for the decrypted mapping
 *     RDX - address of the pagetables to use for encryption
 *     RCX - length of area
 *      R8 - intermediate copy buffer
 *
 *     RAX - points to this routine
 *
 * The area will be encrypted by copying from the non-encrypted
 * memory space to an intermediate buffer and then copying from the
 * intermediate buffer back to the encrypted memory space. The physical
 * addresses of the two mappings are the same which results in the area
 * being encrypted "in place".
 */
	/* Enable the new page tables */
	mov	%rdx, %cr3

	/* Flush any global TLBs */
	mov	%cr4, %rdx
	andq	$~X86_CR4_PGE, %rdx
	mov	%rdx, %cr4
	orq	$X86_CR4_PGE, %rdx
	mov	%rdx, %cr4

	push	%r15
	push	%r12

	movq	%rcx, %r9		/* Save area length */
	movq	%rdi, %r10		/* Save encrypted area address */
	movq	%rsi, %r11		/* Save decrypted area address */

	/* Set the PAT register PA5 entry to write-protect */
	movl	$MSR_IA32_CR_PAT, %ecx
	rdmsr
	mov	%rdx, %r15		/* Save original PAT value */
	andl	$0xffff00ff, %edx	/* Clear PA5 */
	orl	$0x00000500, %edx	/* Set PA5 to WP */
	wrmsr

	wbinvd				/* Invalidate any cache entries */

	/* Copy/encrypt up to 2MB at a time */
	movq	$PMD_PAGE_SIZE, %r12
1:
	cmpq	%r12, %r9
	jnb	2f
	movq	%r9, %r12

2:
	movq	%r11, %rsi		/* Source - decrypted area */
	movq	%r8, %rdi		/* Dest   - intermediate copy buffer */
	movq	%r12, %rcx
	rep	movsb

	movq	%r8, %rsi		/* Source - intermediate copy buffer */
	movq	%r10, %rdi		/* Dest   - encrypted area */
	movq	%r12, %rcx
	rep	movsb

	addq	%r12, %r11
	addq	%r12, %r10
	subq	%r12, %r9		/* Kernel length decrement */
	jnz	1b			/* Kernel length not zero? */

	/* Restore PAT register */
	movl	$MSR_IA32_CR_PAT, %ecx
	rdmsr
	mov	%r15, %rdx		/* Restore original PAT value */
	wrmsr

	pop	%r12
	pop	%r15

	ret
.L__enc_copy_end:
SYM_FUNC_END(__enc_copy)
