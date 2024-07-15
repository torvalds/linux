/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *	sev_verify_cbit.S - Code for verification of the C-bit position reported
 *			    by the Hypervisor when running with SEV enabled.
 *
 *	Copyright (c) 2020  Joerg Roedel (jroedel@suse.de)
 *
 * sev_verify_cbit() is called before switching to a new long-mode page-table
 * at boot.
 *
 * Verify that the C-bit position is correct by writing a random value to
 * an encrypted memory location while on the current page-table. Then it
 * switches to the new page-table to verify the memory content is still the
 * same. After that it switches back to the current page-table and when the
 * check succeeded it returns. If the check failed the code invalidates the
 * stack pointer and goes into a hlt loop. The stack-pointer is invalidated to
 * make sure no interrupt or exception can get the CPU out of the hlt loop.
 *
 * New page-table pointer is expected in %rdi (first parameter)
 *
 */
SYM_FUNC_START(sev_verify_cbit)
#ifdef CONFIG_AMD_MEM_ENCRYPT
	/* First check if a C-bit was detected */
	movq	sme_me_mask(%rip), %rsi
	testq	%rsi, %rsi
	jz	3f

	/* sme_me_mask != 0 could mean SME or SEV - Check also for SEV */
	movq	sev_status(%rip), %rsi
	testq	%rsi, %rsi
	jz	3f

	/* Save CR4 in %rsi */
	movq	%cr4, %rsi

	/* Disable Global Pages */
	movq	%rsi, %rdx
	andq	$(~X86_CR4_PGE), %rdx
	movq	%rdx, %cr4

	/*
	 * Verified that running under SEV - now get a random value using
	 * RDRAND. This instruction is mandatory when running as an SEV guest.
	 *
	 * Don't bail out of the loop if RDRAND returns errors. It is better to
	 * prevent forward progress than to work with a non-random value here.
	 */
1:	rdrand	%rdx
	jnc	1b

	/* Store value to memory and keep it in %rdx */
	movq	%rdx, sev_check_data(%rip)

	/* Backup current %cr3 value to restore it later */
	movq	%cr3, %rcx

	/* Switch to new %cr3 - This might unmap the stack */
	movq	%rdi, %cr3

	/*
	 * Compare value in %rdx with memory location. If C-bit is incorrect
	 * this would read the encrypted data and make the check fail.
	 */
	cmpq	%rdx, sev_check_data(%rip)

	/* Restore old %cr3 */
	movq	%rcx, %cr3

	/* Restore previous CR4 */
	movq	%rsi, %cr4

	/* Check CMPQ result */
	je	3f

	/*
	 * The check failed, prevent any forward progress to prevent ROP
	 * attacks, invalidate the stack and go into a hlt loop.
	 */
	xorl	%esp, %esp
	subq	$0x1000, %rsp
2:	hlt
	jmp 2b
3:
#endif
	/* Return page-table pointer */
	movq	%rdi, %rax
	RET
SYM_FUNC_END(sev_verify_cbit)
