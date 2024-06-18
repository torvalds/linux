/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2017 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#include <linux/linkage.h>

#include <asm/processor-flags.h>
#include <asm/msr.h>
#include <asm/asm-offsets.h>
#include <asm/segment.h>
#include <asm/trapnr.h>

	.text
	.code32
SYM_FUNC_START(get_sev_encryption_bit)
	push	%ebx

	movl	$0x80000000, %eax	/* CPUID to check the highest leaf */
	cpuid
	cmpl	$0x8000001f, %eax	/* See if 0x8000001f is available */
	jb	.Lno_sev

	/*
	 * Check for the SEV feature:
	 *   CPUID Fn8000_001F[EAX] - Bit 1
	 *   CPUID Fn8000_001F[EBX] - Bits 5:0
	 *     Pagetable bit position used to indicate encryption
	 */
	movl	$0x8000001f, %eax
	cpuid
	bt	$1, %eax		/* Check if SEV is available */
	jnc	.Lno_sev

	movl	$MSR_AMD64_SEV, %ecx	/* Read the SEV MSR */
	rdmsr
	bt	$MSR_AMD64_SEV_ENABLED_BIT, %eax	/* Check if SEV is active */
	jnc	.Lno_sev

	movl	%ebx, %eax
	andl	$0x3f, %eax		/* Return the encryption bit location */
	jmp	.Lsev_exit

.Lno_sev:
	xor	%eax, %eax

.Lsev_exit:
	pop	%ebx
	RET
SYM_FUNC_END(get_sev_encryption_bit)

/**
 * sev_es_req_cpuid - Request a CPUID value from the Hypervisor using
 *		      the GHCB MSR protocol
 *
 * @%eax:	Register to request (0=EAX, 1=EBX, 2=ECX, 3=EDX)
 * @%edx:	CPUID Function
 *
 * Returns 0 in %eax on success, non-zero on failure
 * %edx returns CPUID value on success
 */
SYM_CODE_START_LOCAL(sev_es_req_cpuid)
	shll	$30, %eax
	orl     $0x00000004, %eax
	movl    $MSR_AMD64_SEV_ES_GHCB, %ecx
	wrmsr
	rep; vmmcall		# VMGEXIT
	rdmsr

	/* Check response */
	movl	%eax, %ecx
	andl	$0x3ffff000, %ecx	# Bits [12-29] MBZ
	jnz	2f

	/* Check return code */
	andl    $0xfff, %eax
	cmpl    $5, %eax
	jne	2f

	/* All good - return success */
	xorl	%eax, %eax
1:
	RET
2:
	movl	$-1, %eax
	jmp	1b
SYM_CODE_END(sev_es_req_cpuid)

SYM_CODE_START_LOCAL(startup32_vc_handler)
	pushl	%eax
	pushl	%ebx
	pushl	%ecx
	pushl	%edx

	/* Keep CPUID function in %ebx */
	movl	%eax, %ebx

	/* Check if error-code == SVM_EXIT_CPUID */
	cmpl	$0x72, 16(%esp)
	jne	.Lfail

	movl	$0, %eax		# Request CPUID[fn].EAX
	movl	%ebx, %edx		# CPUID fn
	call	sev_es_req_cpuid	# Call helper
	testl	%eax, %eax		# Check return code
	jnz	.Lfail
	movl	%edx, 12(%esp)		# Store result

	movl	$1, %eax		# Request CPUID[fn].EBX
	movl	%ebx, %edx		# CPUID fn
	call	sev_es_req_cpuid	# Call helper
	testl	%eax, %eax		# Check return code
	jnz	.Lfail
	movl	%edx, 8(%esp)		# Store result

	movl	$2, %eax		# Request CPUID[fn].ECX
	movl	%ebx, %edx		# CPUID fn
	call	sev_es_req_cpuid	# Call helper
	testl	%eax, %eax		# Check return code
	jnz	.Lfail
	movl	%edx, 4(%esp)		# Store result

	movl	$3, %eax		# Request CPUID[fn].EDX
	movl	%ebx, %edx		# CPUID fn
	call	sev_es_req_cpuid	# Call helper
	testl	%eax, %eax		# Check return code
	jnz	.Lfail
	movl	%edx, 0(%esp)		# Store result

	/*
	 * Sanity check CPUID results from the Hypervisor. See comment in
	 * do_vc_no_ghcb() for more details on why this is necessary.
	 */

	/* Fail if SEV leaf not available in CPUID[0x80000000].EAX */
	cmpl    $0x80000000, %ebx
	jne     .Lcheck_sev
	cmpl    $0x8000001f, 12(%esp)
	jb      .Lfail
	jmp     .Ldone

.Lcheck_sev:
	/* Fail if SEV bit not set in CPUID[0x8000001f].EAX[1] */
	cmpl    $0x8000001f, %ebx
	jne     .Ldone
	btl     $1, 12(%esp)
	jnc     .Lfail

.Ldone:
	popl	%edx
	popl	%ecx
	popl	%ebx
	popl	%eax

	/* Remove error code */
	addl	$4, %esp

	/* Jump over CPUID instruction */
	addl	$2, (%esp)

	iret
.Lfail:
	/* Send terminate request to Hypervisor */
	movl    $0x100, %eax
	xorl    %edx, %edx
	movl    $MSR_AMD64_SEV_ES_GHCB, %ecx
	wrmsr
	rep; vmmcall

	/* If request fails, go to hlt loop */
	hlt
	jmp .Lfail
SYM_CODE_END(startup32_vc_handler)

/*
 * Write an IDT entry into boot32_idt
 *
 * Parameters:
 *
 * %eax:	Handler address
 * %edx:	Vector number
 * %ecx:	IDT address
 */
SYM_FUNC_START_LOCAL(startup32_set_idt_entry)
	/* IDT entry address to %ecx */
	leal	(%ecx, %edx, 8), %ecx

	/* Build IDT entry, lower 4 bytes */
	movl    %eax, %edx
	andl    $0x0000ffff, %edx		# Target code segment offset [15:0]
	orl	$(__KERNEL32_CS << 16), %edx	# Target code segment selector

	/* Store lower 4 bytes to IDT */
	movl    %edx, (%ecx)

	/* Build IDT entry, upper 4 bytes */
	movl    %eax, %edx
	andl    $0xffff0000, %edx	# Target code segment offset [31:16]
	orl     $0x00008e00, %edx	# Present, Type 32-bit Interrupt Gate

	/* Store upper 4 bytes to IDT */
	movl    %edx, 4(%ecx)

	RET
SYM_FUNC_END(startup32_set_idt_entry)

SYM_FUNC_START(startup32_load_idt)
	push	%ebp
	push	%ebx

	call	1f
1:	pop	%ebp

	leal    (boot32_idt - 1b)(%ebp), %ebx

	/* #VC handler */
	leal    (startup32_vc_handler - 1b)(%ebp), %eax
	movl    $X86_TRAP_VC, %edx
	movl	%ebx, %ecx
	call    startup32_set_idt_entry

	/* Load IDT */
	leal	(boot32_idt_desc - 1b)(%ebp), %ecx
	movl	%ebx, 2(%ecx)
	lidt    (%ecx)

	pop	%ebx
	pop	%ebp
	RET
SYM_FUNC_END(startup32_load_idt)

/*
 * Check for the correct C-bit position when the startup_32 boot-path is used.
 *
 * The check makes use of the fact that all memory is encrypted when paging is
 * disabled. The function creates 64 bits of random data using the RDRAND
 * instruction. RDRAND is mandatory for SEV guests, so always available. If the
 * hypervisor violates that the kernel will crash right here.
 *
 * The 64 bits of random data are stored to a memory location and at the same
 * time kept in the %eax and %ebx registers. Since encryption is always active
 * when paging is off the random data will be stored encrypted in main memory.
 *
 * Then paging is enabled. When the C-bit position is correct all memory is
 * still mapped encrypted and comparing the register values with memory will
 * succeed. An incorrect C-bit position will map all memory unencrypted, so that
 * the compare will use the encrypted random data and fail.
 */
SYM_FUNC_START(startup32_check_sev_cbit)
	pushl	%ebx
	pushl	%ebp

	call	0f
0:	popl	%ebp

	/* Check for non-zero sev_status */
	movl	(sev_status - 0b)(%ebp), %eax
	testl	%eax, %eax
	jz	4f

	/*
	 * Get two 32-bit random values - Don't bail out if RDRAND fails
	 * because it is better to prevent forward progress if no random value
	 * can be gathered.
	 */
1:	rdrand	%eax
	jnc	1b
2:	rdrand	%ebx
	jnc	2b

	/* Store to memory and keep it in the registers */
	leal	(sev_check_data - 0b)(%ebp), %ebp
	movl	%eax, 0(%ebp)
	movl	%ebx, 4(%ebp)

	/* Enable paging to see if encryption is active */
	movl	%cr0, %edx			 /* Backup %cr0 in %edx */
	movl	$(X86_CR0_PG | X86_CR0_PE), %ecx /* Enable Paging and Protected mode */
	movl	%ecx, %cr0

	cmpl	%eax, 0(%ebp)
	jne	3f
	cmpl	%ebx, 4(%ebp)
	jne	3f

	movl	%edx, %cr0	/* Restore previous %cr0 */

	jmp	4f

3:	/* Check failed - hlt the machine */
	hlt
	jmp	3b

4:
	popl	%ebp
	popl	%ebx
	RET
SYM_FUNC_END(startup32_check_sev_cbit)

	.code64

#include "../../kernel/sev_verify_cbit.S"

	.data

	.balign	8
SYM_DATA(sme_me_mask,		.quad 0)
SYM_DATA(sev_status,		.quad 0)
SYM_DATA(sev_check_data,	.quad 0)

SYM_DATA_START_LOCAL(boot32_idt)
	.rept	32
	.quad	0
	.endr
SYM_DATA_END(boot32_idt)

SYM_DATA_START_LOCAL(boot32_idt_desc)
	.word	. - boot32_idt - 1
	.long	0
SYM_DATA_END(boot32_idt_desc)
