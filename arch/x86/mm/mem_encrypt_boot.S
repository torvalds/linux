/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/processor-flags.h>
#include <asm/msr-index.h>

	.text
	.code64
ENTRY(sme_encrypt_execute)

	/*
	 * Entry parameters:
	 *   RDI - virtual address for the encrypted kernel mapping
	 *   RSI - virtual address for the decrypted kernel mapping
	 *   RDX - length of kernel
	 *   RCX - virtual address of the encryption workarea, including:
	 *     - stack page (PAGE_SIZE)
	 *     - encryption routine page (PAGE_SIZE)
	 *     - intermediate copy buffer (PMD_PAGE_SIZE)
	 *    R8 - physcial address of the pagetables to use for encryption
	 */

	push	%rbp
	movq	%rsp, %rbp		/* RBP now has original stack pointer */

	/* Set up a one page stack in the non-encrypted memory area */
	movq	%rcx, %rax		/* Workarea stack page */
	leaq	PAGE_SIZE(%rax), %rsp	/* Set new stack pointer */
	addq	$PAGE_SIZE, %rax	/* Workarea encryption routine */

	push	%r12
	movq	%rdi, %r10		/* Encrypted kernel */
	movq	%rsi, %r11		/* Decrypted kernel */
	movq	%rdx, %r12		/* Kernel length */

	/* Copy encryption routine into the workarea */
	movq	%rax, %rdi				/* Workarea encryption routine */
	leaq	__enc_copy(%rip), %rsi			/* Encryption routine */
	movq	$(.L__enc_copy_end - __enc_copy), %rcx	/* Encryption routine length */
	rep	movsb

	/* Setup registers for call */
	movq	%r10, %rdi		/* Encrypted kernel */
	movq	%r11, %rsi		/* Decrypted kernel */
	movq	%r8, %rdx		/* Pagetables used for encryption */
	movq	%r12, %rcx		/* Kernel length */
	movq	%rax, %r8		/* Workarea encryption routine */
	addq	$PAGE_SIZE, %r8		/* Workarea intermediate copy buffer */

	call	*%rax			/* Call the encryption routine */

	pop	%r12

	movq	%rbp, %rsp		/* Restore original stack pointer */
	pop	%rbp

	ret
ENDPROC(sme_encrypt_execute)

ENTRY(__enc_copy)
/*
 * Routine used to encrypt kernel.
 *   This routine must be run outside of the kernel proper since
 *   the kernel will be encrypted during the process. So this
 *   routine is defined here and then copied to an area outside
 *   of the kernel where it will remain and run decrypted
 *   during execution.
 *
 *   On entry the registers must be:
 *     RDI - virtual address for the encrypted kernel mapping
 *     RSI - virtual address for the decrypted kernel mapping
 *     RDX - address of the pagetables to use for encryption
 *     RCX - length of kernel
 *      R8 - intermediate copy buffer
 *
 *     RAX - points to this routine
 *
 * The kernel will be encrypted by copying from the non-encrypted
 * kernel space to an intermediate buffer and then copying from the
 * intermediate buffer back to the encrypted kernel space. The physical
 * addresses of the two kernel space mappings are the same which
 * results in the kernel being encrypted "in place".
 */
	/* Enable the new page tables */
	mov	%rdx, %cr3

	/* Flush any global TLBs */
	mov	%cr4, %rdx
	andq	$~X86_CR4_PGE, %rdx
	mov	%rdx, %cr4
	orq	$X86_CR4_PGE, %rdx
	mov	%rdx, %cr4

	/* Set the PAT register PA5 entry to write-protect */
	push	%rcx
	movl	$MSR_IA32_CR_PAT, %ecx
	rdmsr
	push	%rdx			/* Save original PAT value */
	andl	$0xffff00ff, %edx	/* Clear PA5 */
	orl	$0x00000500, %edx	/* Set PA5 to WP */
	wrmsr
	pop	%rdx			/* RDX contains original PAT value */
	pop	%rcx

	movq	%rcx, %r9		/* Save kernel length */
	movq	%rdi, %r10		/* Save encrypted kernel address */
	movq	%rsi, %r11		/* Save decrypted kernel address */

	wbinvd				/* Invalidate any cache entries */

	/* Copy/encrypt 2MB at a time */
1:
	movq	%r11, %rsi		/* Source - decrypted kernel */
	movq	%r8, %rdi		/* Dest   - intermediate copy buffer */
	movq	$PMD_PAGE_SIZE, %rcx	/* 2MB length */
	rep	movsb

	movq	%r8, %rsi		/* Source - intermediate copy buffer */
	movq	%r10, %rdi		/* Dest   - encrypted kernel */
	movq	$PMD_PAGE_SIZE, %rcx	/* 2MB length */
	rep	movsb

	addq	$PMD_PAGE_SIZE, %r11
	addq	$PMD_PAGE_SIZE, %r10
	subq	$PMD_PAGE_SIZE, %r9	/* Kernel length decrement */
	jnz	1b			/* Kernel length not zero? */

	/* Restore PAT register */
	push	%rdx			/* Save original PAT value */
	movl	$MSR_IA32_CR_PAT, %ecx
	rdmsr
	pop	%rdx			/* Restore original PAT value */
	wrmsr

	ret
.L__enc_copy_end:
ENDPROC(__enc_copy)
