/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/boot/head.S
 *
 *  Copyright (C) 1991, 1992, 1993  Linus Torvalds
 */

/*
 *  head.S contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00001000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory. [According to comments etc elsewhere on a compressed
 * kernel it will end up at 0x1000 + 1Mb I hope so as I assume this. - AC]
 *
 * Page 0 is deliberately kept safe, since System Management Mode code in 
 * laptops may need to access the BIOS data stored there.  This is also
 * useful for future device drivers that either access the BIOS via VM86 
 * mode.
 */

/*
 * High loaded stuff by Hans Lermen & Werner Almesberger, Feb. 1996
 */
	.code32
	.text

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/boot.h>
#include <asm/msr.h>
#include <asm/processor-flags.h>
#include <asm/asm-offsets.h>
#include <asm/bootparam.h>

/*
 * Locally defined symbols should be marked hidden:
 */
	.hidden _bss
	.hidden _ebss
	.hidden _got
	.hidden _egot

	__HEAD
	.code32
ENTRY(startup_32)
	/*
	 * 32bit entry is 0 and it is ABI so immutable!
	 * If we come here directly from a bootloader,
	 * kernel(text+data+bss+brk) ramdisk, zero_page, command line
	 * all need to be under the 4G limit.
	 */
	cld
	/*
	 * Test KEEP_SEGMENTS flag to see if the bootloader is asking
	 * us to not reload segments
	 */
	testb $KEEP_SEGMENTS, BP_loadflags(%esi)
	jnz 1f

	cli
	movl	$(__BOOT_DS), %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %ss
1:

/*
 * Calculate the delta between where we were compiled to run
 * at and where we were actually loaded at.  This can only be done
 * with a short local call on x86.  Nothing  else will tell us what
 * address we are running at.  The reserved chunk of the real-mode
 * data at 0x1e4 (defined as a scratch field) are used as the stack
 * for this calculation. Only 4 bytes are needed.
 */
	leal	(BP_scratch+4)(%esi), %esp
	call	1f
1:	popl	%ebp
	subl	$1b, %ebp

/* setup a stack and make sure cpu supports long mode. */
	movl	$boot_stack_end, %eax
	addl	%ebp, %eax
	movl	%eax, %esp

	call	verify_cpu
	testl	%eax, %eax
	jnz	no_longmode

/*
 * Compute the delta between where we were compiled to run at
 * and where the code will actually run at.
 *
 * %ebp contains the address we are loaded at by the boot loader and %ebx
 * contains the address where we should move the kernel image temporarily
 * for safe in-place decompression.
 */

#ifdef CONFIG_RELOCATABLE
	movl	%ebp, %ebx
	movl	BP_kernel_alignment(%esi), %eax
	decl	%eax
	addl	%eax, %ebx
	notl	%eax
	andl	%eax, %ebx
	cmpl	$LOAD_PHYSICAL_ADDR, %ebx
	jge	1f
#endif
	movl	$LOAD_PHYSICAL_ADDR, %ebx
1:

	/* Target address to relocate to for decompression */
	movl	BP_init_size(%esi), %eax
	subl	$_end, %eax
	addl	%eax, %ebx

/*
 * Prepare for entering 64 bit mode
 */

	/* Load new GDT with the 64bit segments using 32bit descriptor */
	addl	%ebp, gdt+2(%ebp)
	lgdt	gdt(%ebp)

	/* Enable PAE mode */
	movl	%cr4, %eax
	orl	$X86_CR4_PAE, %eax
	movl	%eax, %cr4

 /*
  * Build early 4G boot pagetable
  */
	/*
	 * If SEV is active then set the encryption mask in the page tables.
	 * This will insure that when the kernel is copied and decompressed
	 * it will be done so encrypted.
	 */
	call	get_sev_encryption_bit
	xorl	%edx, %edx
	testl	%eax, %eax
	jz	1f
	subl	$32, %eax	/* Encryption bit is always above bit 31 */
	bts	%eax, %edx	/* Set encryption mask for page tables */
1:

	/* Initialize Page tables to 0 */
	leal	pgtable(%ebx), %edi
	xorl	%eax, %eax
	movl	$(BOOT_INIT_PGT_SIZE/4), %ecx
	rep	stosl

	/* Build Level 4 */
	leal	pgtable + 0(%ebx), %edi
	leal	0x1007 (%edi), %eax
	movl	%eax, 0(%edi)
	addl	%edx, 4(%edi)

	/* Build Level 3 */
	leal	pgtable + 0x1000(%ebx), %edi
	leal	0x1007(%edi), %eax
	movl	$4, %ecx
1:	movl	%eax, 0x00(%edi)
	addl	%edx, 0x04(%edi)
	addl	$0x00001000, %eax
	addl	$8, %edi
	decl	%ecx
	jnz	1b

	/* Build Level 2 */
	leal	pgtable + 0x2000(%ebx), %edi
	movl	$0x00000183, %eax
	movl	$2048, %ecx
1:	movl	%eax, 0(%edi)
	addl	%edx, 4(%edi)
	addl	$0x00200000, %eax
	addl	$8, %edi
	decl	%ecx
	jnz	1b

	/* Enable the boot page tables */
	leal	pgtable(%ebx), %eax
	movl	%eax, %cr3

	/* Enable Long mode in EFER (Extended Feature Enable Register) */
	movl	$MSR_EFER, %ecx
	rdmsr
	btsl	$_EFER_LME, %eax
	wrmsr

	/* After gdt is loaded */
	xorl	%eax, %eax
	lldt	%ax
	movl    $__BOOT_TSS, %eax
	ltr	%ax

	/*
	 * Setup for the jump to 64bit mode
	 *
	 * When the jump is performend we will be in long mode but
	 * in 32bit compatibility mode with EFER.LME = 1, CS.L = 0, CS.D = 1
	 * (and in turn EFER.LMA = 1).	To jump into 64bit mode we use
	 * the new gdt/idt that has __KERNEL_CS with CS.L = 1.
	 * We place all of the values on our mini stack so lret can
	 * used to perform that far jump.
	 */
	pushl	$__KERNEL_CS
	leal	startup_64(%ebp), %eax
#ifdef CONFIG_EFI_MIXED
	movl	efi32_config(%ebp), %ebx
	cmp	$0, %ebx
	jz	1f
	leal	handover_entry(%ebp), %eax
1:
#endif
	pushl	%eax

	/* Enter paged protected Mode, activating Long Mode */
	movl	$(X86_CR0_PG | X86_CR0_PE), %eax /* Enable Paging and Protected mode */
	movl	%eax, %cr0

	/* Jump from 32bit compatibility mode into 64bit mode. */
	lret
ENDPROC(startup_32)

#ifdef CONFIG_EFI_MIXED
	.org 0x190
ENTRY(efi32_stub_entry)
	add	$0x4, %esp		/* Discard return address */
	popl	%ecx
	popl	%edx
	popl	%esi

	leal	(BP_scratch+4)(%esi), %esp
	call	1f
1:	pop	%ebp
	subl	$1b, %ebp

	movl	%ecx, efi32_config(%ebp)
	movl	%edx, efi32_config+8(%ebp)
	sgdtl	efi32_boot_gdt(%ebp)

	leal	efi32_config(%ebp), %eax
	movl	%eax, efi_config(%ebp)

	jmp	startup_32
ENDPROC(efi32_stub_entry)
#endif

	.code64
	.org 0x200
ENTRY(startup_64)
	/*
	 * 64bit entry is 0x200 and it is ABI so immutable!
	 * We come here either from startup_32 or directly from a
	 * 64bit bootloader.
	 * If we come here from a bootloader, kernel(text+data+bss+brk),
	 * ramdisk, zero_page, command line could be above 4G.
	 * We depend on an identity mapped page table being provided
	 * that maps our entire kernel(text+data+bss+brk), zero page
	 * and command line.
	 */

	/* Setup data segments. */
	xorl	%eax, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %ss
	movl	%eax, %fs
	movl	%eax, %gs

	/*
	 * Compute the decompressed kernel start address.  It is where
	 * we were loaded at aligned to a 2M boundary. %rbp contains the
	 * decompressed kernel start address.
	 *
	 * If it is a relocatable kernel then decompress and run the kernel
	 * from load address aligned to 2MB addr, otherwise decompress and
	 * run the kernel from LOAD_PHYSICAL_ADDR
	 *
	 * We cannot rely on the calculation done in 32-bit mode, since we
	 * may have been invoked via the 64-bit entry point.
	 */

	/* Start with the delta to where the kernel will run at. */
#ifdef CONFIG_RELOCATABLE
	leaq	startup_32(%rip) /* - $startup_32 */, %rbp
	movl	BP_kernel_alignment(%rsi), %eax
	decl	%eax
	addq	%rax, %rbp
	notq	%rax
	andq	%rax, %rbp
	cmpq	$LOAD_PHYSICAL_ADDR, %rbp
	jge	1f
#endif
	movq	$LOAD_PHYSICAL_ADDR, %rbp
1:

	/* Target address to relocate to for decompression */
	movl	BP_init_size(%rsi), %ebx
	subl	$_end, %ebx
	addq	%rbp, %rbx

	/* Set up the stack */
	leaq	boot_stack_end(%rbx), %rsp

#ifdef CONFIG_X86_5LEVEL
	/*
	 * Check if we need to enable 5-level paging.
	 * RSI holds real mode data and need to be preserved across
	 * a function call.
	 */
	pushq	%rsi
	call	l5_paging_required
	popq	%rsi

	/* If l5_paging_required() returned zero, we're done here. */
	cmpq	$0, %rax
	je	lvl5

	/*
	 * At this point we are in long mode with 4-level paging enabled,
	 * but we want to enable 5-level paging.
	 *
	 * The problem is that we cannot do it directly. Setting LA57 in
	 * long mode would trigger #GP. So we need to switch off long mode
	 * first.
	 *
	 * NOTE: This is not going to work if bootloader put us above 4G
	 * limit.
	 *
	 * The first step is go into compatibility mode.
	 */

	/* Clear additional page table */
	leaq	lvl5_pgtable(%rbx), %rdi
	xorq	%rax, %rax
	movq	$(PAGE_SIZE/8), %rcx
	rep	stosq

	/*
	 * Setup current CR3 as the first and only entry in a new top level
	 * page table.
	 */
	movq	%cr3, %rdi
	leaq	0x7 (%rdi), %rax
	movq	%rax, lvl5_pgtable(%rbx)

	/* Switch to compatibility mode (CS.L = 0 CS.D = 1) via far return */
	pushq	$__KERNEL32_CS
	leaq	compatible_mode(%rip), %rax
	pushq	%rax
	lretq
lvl5:
#endif

	/* Zero EFLAGS */
	pushq	$0
	popfq

/*
 * Copy the compressed kernel to the end of our buffer
 * where decompression in place becomes safe.
 */
	pushq	%rsi
	leaq	(_bss-8)(%rip), %rsi
	leaq	(_bss-8)(%rbx), %rdi
	movq	$_bss /* - $startup_32 */, %rcx
	shrq	$3, %rcx
	std
	rep	movsq
	cld
	popq	%rsi

/*
 * Jump to the relocated address.
 */
	leaq	relocated(%rbx), %rax
	jmp	*%rax

#ifdef CONFIG_EFI_STUB

/* The entry point for the PE/COFF executable is efi_pe_entry. */
ENTRY(efi_pe_entry)
	movq	%rcx, efi64_config(%rip)	/* Handle */
	movq	%rdx, efi64_config+8(%rip) /* EFI System table pointer */

	leaq	efi64_config(%rip), %rax
	movq	%rax, efi_config(%rip)

	call	1f
1:	popq	%rbp
	subq	$1b, %rbp

	/*
	 * Relocate efi_config->call().
	 */
	addq	%rbp, efi64_config+40(%rip)

	movq	%rax, %rdi
	call	make_boot_params
	cmpq	$0,%rax
	je	fail
	mov	%rax, %rsi
	leaq	startup_32(%rip), %rax
	movl	%eax, BP_code32_start(%rsi)
	jmp	2f		/* Skip the relocation */

handover_entry:
	call	1f
1:	popq	%rbp
	subq	$1b, %rbp

	/*
	 * Relocate efi_config->call().
	 */
	movq	efi_config(%rip), %rax
	addq	%rbp, 40(%rax)
2:
	movq	efi_config(%rip), %rdi
	call	efi_main
	movq	%rax,%rsi
	cmpq	$0,%rax
	jne	2f
fail:
	/* EFI init failed, so hang. */
	hlt
	jmp	fail
2:
	movl	BP_code32_start(%esi), %eax
	leaq	startup_64(%rax), %rax
	jmp	*%rax
ENDPROC(efi_pe_entry)

	.org 0x390
ENTRY(efi64_stub_entry)
	movq	%rdi, efi64_config(%rip)	/* Handle */
	movq	%rsi, efi64_config+8(%rip) /* EFI System table pointer */

	leaq	efi64_config(%rip), %rax
	movq	%rax, efi_config(%rip)

	movq	%rdx, %rsi
	jmp	handover_entry
ENDPROC(efi64_stub_entry)
#endif

	.text
relocated:

/*
 * Clear BSS (stack is currently empty)
 */
	xorl	%eax, %eax
	leaq    _bss(%rip), %rdi
	leaq    _ebss(%rip), %rcx
	subq	%rdi, %rcx
	shrq	$3, %rcx
	rep	stosq

/*
 * Adjust our own GOT
 */
	leaq	_got(%rip), %rdx
	leaq	_egot(%rip), %rcx
1:
	cmpq	%rcx, %rdx
	jae	2f
	addq	%rbx, (%rdx)
	addq	$8, %rdx
	jmp	1b
2:
	
/*
 * Do the extraction, and jump to the new kernel..
 */
	pushq	%rsi			/* Save the real mode argument */
	movq	%rsi, %rdi		/* real mode address */
	leaq	boot_heap(%rip), %rsi	/* malloc area for uncompression */
	leaq	input_data(%rip), %rdx  /* input_data */
	movl	$z_input_len, %ecx	/* input_len */
	movq	%rbp, %r8		/* output target address */
	movq	$z_output_len, %r9	/* decompressed length, end of relocs */
	call	extract_kernel		/* returns kernel location in %rax */
	popq	%rsi

/*
 * Jump to the decompressed kernel.
 */
	jmp	*%rax

	.code32
#ifdef CONFIG_X86_5LEVEL
compatible_mode:
	/* Setup data and stack segments */
	movl	$__KERNEL_DS, %eax
	movl	%eax, %ds
	movl	%eax, %ss

	/* Disable paging */
	movl	%cr0, %eax
	btrl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0

	/* Point CR3 to 5-level paging */
	leal	lvl5_pgtable(%ebx), %eax
	movl	%eax, %cr3

	/* Enable PAE and LA57 mode */
	movl	%cr4, %eax
	orl	$(X86_CR4_PAE | X86_CR4_LA57), %eax
	movl	%eax, %cr4

	/* Calculate address we are running at */
	call	1f
1:	popl	%edi
	subl	$1b, %edi

	/* Prepare stack for far return to Long Mode */
	pushl	$__KERNEL_CS
	leal	lvl5(%edi), %eax
	push	%eax

	/* Enable paging back */
	movl	$(X86_CR0_PG | X86_CR0_PE), %eax
	movl	%eax, %cr0

	lret
#endif

no_longmode:
	/* This isn't an x86-64 CPU so hang */
1:
	hlt
	jmp     1b

#include "../../kernel/verify_cpu.S"

	.data
gdt:
	.word	gdt_end - gdt
	.long	gdt
	.word	0
	.quad	0x00cf9a000000ffff	/* __KERNEL32_CS */
	.quad	0x00af9a000000ffff	/* __KERNEL_CS */
	.quad	0x00cf92000000ffff	/* __KERNEL_DS */
	.quad	0x0080890000000000	/* TS descriptor */
	.quad   0x0000000000000000	/* TS continued */
gdt_end:

#ifdef CONFIG_EFI_STUB
efi_config:
	.quad	0

#ifdef CONFIG_EFI_MIXED
	.global efi32_config
efi32_config:
	.fill	5,8,0
	.quad	efi64_thunk
	.byte	0
#endif

	.global efi64_config
efi64_config:
	.fill	5,8,0
	.quad	efi_call
	.byte	1
#endif /* CONFIG_EFI_STUB */

/*
 * Stack and heap for uncompression
 */
	.bss
	.balign 4
boot_heap:
	.fill BOOT_HEAP_SIZE, 1, 0
boot_stack:
	.fill BOOT_STACK_SIZE, 1, 0
boot_stack_end:

/*
 * Space for page tables (not in .bss so not zeroed)
 */
	.section ".pgtable","a",@nobits
	.balign 4096
pgtable:
	.fill BOOT_PGT_SIZE, 1, 0
#ifdef CONFIG_X86_5LEVEL
lvl5_pgtable:
	.fill PAGE_SIZE, 1, 0
#endif
