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
#include <asm/desc_defs.h>
#include <asm/trapnr.h>
#include "pgtable.h"

/*
 * Locally defined symbols should be marked hidden:
 */
	.hidden _bss
	.hidden _ebss
	.hidden _end

	__HEAD

/*
 * This macro gives the relative virtual address of X, i.e. the offset of X
 * from startup_32. This is the same as the link-time virtual address of X,
 * since startup_32 is at 0, but defining it this way tells the
 * assembler/linker that we do not want the actual run-time address of X. This
 * prevents the linker from trying to create unwanted run-time relocation
 * entries for the reference when the compressed kernel is linked as PIE.
 *
 * A reference X(%reg) will result in the link-time VA of X being stored with
 * the instruction, and a run-time R_X86_64_RELATIVE relocation entry that
 * adds the 64-bit base address where the kernel is loaded.
 *
 * Replacing it with (X-startup_32)(%reg) results in the offset being stored,
 * and no run-time relocation.
 *
 * The macro should be used as a displacement with a base register containing
 * the run-time address of startup_32 [i.e. rva(X)(%reg)], or as an immediate
 * [$ rva(X)].
 *
 * This macro can only be used from within the .head.text section, since the
 * expression requires startup_32 to be in the same section as the code being
 * assembled.
 */
#define rva(X) ((X) - startup_32)

	.code32
SYM_FUNC_START(startup_32)
	/*
	 * 32bit entry is 0 and it is ABI so immutable!
	 * If we come here directly from a bootloader,
	 * kernel(text+data+bss+brk) ramdisk, zero_page, command line
	 * all need to be under the 4G limit.
	 */
	cld
	cli

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
	subl	$ rva(1b), %ebp

	/* Load new GDT with the 64bit segments using 32bit descriptor */
	leal	rva(gdt)(%ebp), %eax
	movl	%eax, 2(%eax)
	lgdt	(%eax)

	/* Load segment registers with our descriptors */
	movl	$__BOOT_DS, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %fs
	movl	%eax, %gs
	movl	%eax, %ss

	/* Setup a stack and load CS from current GDT */
	leal	rva(boot_stack_end)(%ebp), %esp

	pushl	$__KERNEL32_CS
	leal	rva(1f)(%ebp), %eax
	pushl	%eax
	lretl
1:

	/* Setup Exception handling for SEV-ES */
	call	startup32_load_idt

	/* Make sure cpu supports long mode. */
	call	verify_cpu
	testl	%eax, %eax
	jnz	.Lno_longmode

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

#ifdef CONFIG_EFI_STUB
/*
 * If we were loaded via the EFI LoadImage service, startup_32 will be at an
 * offset to the start of the space allocated for the image. efi_pe_entry will
 * set up image_offset to tell us where the image actually starts, so that we
 * can use the full available buffer.
 *	image_offset = startup_32 - image_base
 * Otherwise image_offset will be zero and has no effect on the calculations.
 */
	subl    rva(image_offset)(%ebp), %ebx
#endif

	movl	BP_kernel_alignment(%esi), %eax
	decl	%eax
	addl	%eax, %ebx
	notl	%eax
	andl	%eax, %ebx
	cmpl	$LOAD_PHYSICAL_ADDR, %ebx
	jae	1f
#endif
	movl	$LOAD_PHYSICAL_ADDR, %ebx
1:

	/* Target address to relocate to for decompression */
	addl	BP_init_size(%esi), %ebx
	subl	$ rva(_end), %ebx

/*
 * Prepare for entering 64 bit mode
 */

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
#ifdef	CONFIG_AMD_MEM_ENCRYPT
	testl	%eax, %eax
	jz	1f
	subl	$32, %eax	/* Encryption bit is always above bit 31 */
	bts	%eax, %edx	/* Set encryption mask for page tables */
	/*
	 * Set MSR_AMD64_SEV_ENABLED_BIT in sev_status so that
	 * startup32_check_sev_cbit() will do a check. sev_enable() will
	 * initialize sev_status with all the bits reported by
	 * MSR_AMD_SEV_STATUS later, but only MSR_AMD64_SEV_ENABLED_BIT
	 * needs to be set for now.
	 */
	movl	$1, rva(sev_status)(%ebp)
1:
#endif

	/* Initialize Page tables to 0 */
	leal	rva(pgtable)(%ebx), %edi
	xorl	%eax, %eax
	movl	$(BOOT_INIT_PGT_SIZE/4), %ecx
	rep	stosl

	/* Build Level 4 */
	leal	rva(pgtable + 0)(%ebx), %edi
	leal	0x1007 (%edi), %eax
	movl	%eax, 0(%edi)
	addl	%edx, 4(%edi)

	/* Build Level 3 */
	leal	rva(pgtable + 0x1000)(%ebx), %edi
	leal	0x1007(%edi), %eax
	movl	$4, %ecx
1:	movl	%eax, 0x00(%edi)
	addl	%edx, 0x04(%edi)
	addl	$0x00001000, %eax
	addl	$8, %edi
	decl	%ecx
	jnz	1b

	/* Build Level 2 */
	leal	rva(pgtable + 0x2000)(%ebx), %edi
	movl	$0x00000183, %eax
	movl	$2048, %ecx
1:	movl	%eax, 0(%edi)
	addl	%edx, 4(%edi)
	addl	$0x00200000, %eax
	addl	$8, %edi
	decl	%ecx
	jnz	1b

	/* Enable the boot page tables */
	leal	rva(pgtable)(%ebx), %eax
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
	 * When the jump is performed we will be in long mode but
	 * in 32bit compatibility mode with EFER.LME = 1, CS.L = 0, CS.D = 1
	 * (and in turn EFER.LMA = 1).	To jump into 64bit mode we use
	 * the new gdt/idt that has __KERNEL_CS with CS.L = 1.
	 * We place all of the values on our mini stack so lret can
	 * used to perform that far jump.
	 */
	leal	rva(startup_64)(%ebp), %eax
#ifdef CONFIG_EFI_MIXED
	movl	rva(efi32_boot_args)(%ebp), %edi
	testl	%edi, %edi
	jz	1f
	leal	rva(efi64_stub_entry)(%ebp), %eax
	movl	rva(efi32_boot_args+4)(%ebp), %esi
	movl	rva(efi32_boot_args+8)(%ebp), %edx	// saved bootparams pointer
	testl	%edx, %edx
	jnz	1f
	/*
	 * efi_pe_entry uses MS calling convention, which requires 32 bytes of
	 * shadow space on the stack even if all arguments are passed in
	 * registers. We also need an additional 8 bytes for the space that
	 * would be occupied by the return address, and this also results in
	 * the correct stack alignment for entry.
	 */
	subl	$40, %esp
	leal	rva(efi_pe_entry)(%ebp), %eax
	movl	%edi, %ecx			// MS calling convention
	movl	%esi, %edx
1:
#endif
	/* Check if the C-bit position is correct when SEV is active */
	call	startup32_check_sev_cbit

	pushl	$__KERNEL_CS
	pushl	%eax

	/* Enter paged protected Mode, activating Long Mode */
	movl	$CR0_STATE, %eax
	movl	%eax, %cr0

	/* Jump from 32bit compatibility mode into 64bit mode. */
	lret
SYM_FUNC_END(startup_32)

#ifdef CONFIG_EFI_MIXED
	.org 0x190
SYM_FUNC_START(efi32_stub_entry)
	add	$0x4, %esp		/* Discard return address */
	popl	%ecx
	popl	%edx
	popl	%esi

	call	1f
1:	pop	%ebp
	subl	$ rva(1b), %ebp

	movl	%esi, rva(efi32_boot_args+8)(%ebp)
SYM_INNER_LABEL(efi32_pe_stub_entry, SYM_L_LOCAL)
	movl	%ecx, rva(efi32_boot_args)(%ebp)
	movl	%edx, rva(efi32_boot_args+4)(%ebp)
	movb	$0, rva(efi_is64)(%ebp)

	/* Save firmware GDTR and code/data selectors */
	sgdtl	rva(efi32_boot_gdt)(%ebp)
	movw	%cs, rva(efi32_boot_cs)(%ebp)
	movw	%ds, rva(efi32_boot_ds)(%ebp)

	/* Store firmware IDT descriptor */
	sidtl	rva(efi32_boot_idt)(%ebp)

	/* Disable paging */
	movl	%cr0, %eax
	btrl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0

	jmp	startup_32
SYM_FUNC_END(efi32_stub_entry)
#endif

	.code64
	.org 0x200
SYM_CODE_START(startup_64)
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

	cld
	cli

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

#ifdef CONFIG_EFI_STUB
/*
 * If we were loaded via the EFI LoadImage service, startup_32 will be at an
 * offset to the start of the space allocated for the image. efi_pe_entry will
 * set up image_offset to tell us where the image actually starts, so that we
 * can use the full available buffer.
 *	image_offset = startup_32 - image_base
 * Otherwise image_offset will be zero and has no effect on the calculations.
 */
	movl    image_offset(%rip), %eax
	subq	%rax, %rbp
#endif

	movl	BP_kernel_alignment(%rsi), %eax
	decl	%eax
	addq	%rax, %rbp
	notq	%rax
	andq	%rax, %rbp
	cmpq	$LOAD_PHYSICAL_ADDR, %rbp
	jae	1f
#endif
	movq	$LOAD_PHYSICAL_ADDR, %rbp
1:

	/* Target address to relocate to for decompression */
	movl	BP_init_size(%rsi), %ebx
	subl	$ rva(_end), %ebx
	addq	%rbp, %rbx

	/* Set up the stack */
	leaq	rva(boot_stack_end)(%rbx), %rsp

	/*
	 * At this point we are in long mode with 4-level paging enabled,
	 * but we might want to enable 5-level paging or vice versa.
	 *
	 * The problem is that we cannot do it directly. Setting or clearing
	 * CR4.LA57 in long mode would trigger #GP. So we need to switch off
	 * long mode and paging first.
	 *
	 * We also need a trampoline in lower memory to switch over from
	 * 4- to 5-level paging for cases when the bootloader puts the kernel
	 * above 4G, but didn't enable 5-level paging for us.
	 *
	 * The same trampoline can be used to switch from 5- to 4-level paging
	 * mode, like when starting 4-level paging kernel via kexec() when
	 * original kernel worked in 5-level paging mode.
	 *
	 * For the trampoline, we need the top page table to reside in lower
	 * memory as we don't have a way to load 64-bit values into CR3 in
	 * 32-bit mode.
	 *
	 * We go though the trampoline even if we don't have to: if we're
	 * already in a desired paging mode. This way the trampoline code gets
	 * tested on every boot.
	 */

	/* Make sure we have GDT with 32-bit code segment */
	leaq	gdt64(%rip), %rax
	addq	%rax, 2(%rax)
	lgdt	(%rax)

	/* Reload CS so IRET returns to a CS actually in the GDT */
	pushq	$__KERNEL_CS
	leaq	.Lon_kernel_cs(%rip), %rax
	pushq	%rax
	lretq

.Lon_kernel_cs:

	pushq	%rsi
	call	load_stage1_idt
	popq	%rsi

#ifdef CONFIG_AMD_MEM_ENCRYPT
	/*
	 * Now that the stage1 interrupt handlers are set up, #VC exceptions from
	 * CPUID instructions can be properly handled for SEV-ES guests.
	 *
	 * For SEV-SNP, the CPUID table also needs to be set up in advance of any
	 * CPUID instructions being issued, so go ahead and do that now via
	 * sev_enable(), which will also handle the rest of the SEV-related
	 * detection/setup to ensure that has been done in advance of any dependent
	 * code.
	 */
	pushq	%rsi
	movq	%rsi, %rdi		/* real mode address */
	call	sev_enable
	popq	%rsi
#endif

	/*
	 * paging_prepare() sets up the trampoline and checks if we need to
	 * enable 5-level paging.
	 *
	 * paging_prepare() returns a two-quadword structure which lands
	 * into RDX:RAX:
	 *   - Address of the trampoline is returned in RAX.
	 *   - Non zero RDX means trampoline needs to enable 5-level
	 *     paging.
	 *
	 * RSI holds real mode data and needs to be preserved across
	 * this function call.
	 */
	pushq	%rsi
	movq	%rsi, %rdi		/* real mode address */
	call	paging_prepare
	popq	%rsi

	/* Save the trampoline address in RCX */
	movq	%rax, %rcx

	/*
	 * Load the address of trampoline_return() into RDI.
	 * It will be used by the trampoline to return to the main code.
	 */
	leaq	trampoline_return(%rip), %rdi

	/* Switch to compatibility mode (CS.L = 0 CS.D = 1) via far return */
	pushq	$__KERNEL32_CS
	leaq	TRAMPOLINE_32BIT_CODE_OFFSET(%rax), %rax
	pushq	%rax
	lretq
trampoline_return:
	/* Restore the stack, the 32-bit trampoline uses its own stack */
	leaq	rva(boot_stack_end)(%rbx), %rsp

	/*
	 * cleanup_trampoline() would restore trampoline memory.
	 *
	 * RDI is address of the page table to use instead of page table
	 * in trampoline memory (if required).
	 *
	 * RSI holds real mode data and needs to be preserved across
	 * this function call.
	 */
	pushq	%rsi
	leaq	rva(top_pgtable)(%rbx), %rdi
	call	cleanup_trampoline
	popq	%rsi

	/* Zero EFLAGS */
	pushq	$0
	popfq

/*
 * Copy the compressed kernel to the end of our buffer
 * where decompression in place becomes safe.
 */
	pushq	%rsi
	leaq	(_bss-8)(%rip), %rsi
	leaq	rva(_bss-8)(%rbx), %rdi
	movl	$(_bss - startup_32), %ecx
	shrl	$3, %ecx
	std
	rep	movsq
	cld
	popq	%rsi

	/*
	 * The GDT may get overwritten either during the copy we just did or
	 * during extract_kernel below. To avoid any issues, repoint the GDTR
	 * to the new copy of the GDT.
	 */
	leaq	rva(gdt64)(%rbx), %rax
	leaq	rva(gdt)(%rbx), %rdx
	movq	%rdx, 2(%rax)
	lgdt	(%rax)

/*
 * Jump to the relocated address.
 */
	leaq	rva(.Lrelocated)(%rbx), %rax
	jmp	*%rax
SYM_CODE_END(startup_64)

#ifdef CONFIG_EFI_STUB
	.org 0x390
SYM_FUNC_START(efi64_stub_entry)
	and	$~0xf, %rsp			/* realign the stack */
	movq	%rdx, %rbx			/* save boot_params pointer */
	call	efi_main
	movq	%rbx,%rsi
	leaq	rva(startup_64)(%rax), %rax
	jmp	*%rax
SYM_FUNC_END(efi64_stub_entry)
SYM_FUNC_ALIAS(efi_stub_entry, efi64_stub_entry)
#endif

	.text
SYM_FUNC_START_LOCAL_NOALIGN(.Lrelocated)

/*
 * Clear BSS (stack is currently empty)
 */
	xorl	%eax, %eax
	leaq    _bss(%rip), %rdi
	leaq    _ebss(%rip), %rcx
	subq	%rdi, %rcx
	shrq	$3, %rcx
	rep	stosq

	pushq	%rsi
	call	load_stage2_idt

	/* Pass boot_params to initialize_identity_maps() */
	movq	(%rsp), %rdi
	call	initialize_identity_maps
	popq	%rsi

/*
 * Do the extraction, and jump to the new kernel..
 */
	pushq	%rsi			/* Save the real mode argument */
	movq	%rsi, %rdi		/* real mode address */
	leaq	boot_heap(%rip), %rsi	/* malloc area for uncompression */
	leaq	input_data(%rip), %rdx  /* input_data */
	movl	input_len(%rip), %ecx	/* input_len */
	movq	%rbp, %r8		/* output target address */
	movl	output_len(%rip), %r9d	/* decompressed length, end of relocs */
	call	extract_kernel		/* returns kernel location in %rax */
	popq	%rsi

/*
 * Jump to the decompressed kernel.
 */
	jmp	*%rax
SYM_FUNC_END(.Lrelocated)

	.code32
/*
 * This is the 32-bit trampoline that will be copied over to low memory.
 *
 * RDI contains the return address (might be above 4G).
 * ECX contains the base address of the trampoline memory.
 * Non zero RDX means trampoline needs to enable 5-level paging.
 */
SYM_CODE_START(trampoline_32bit_src)
	/* Set up data and stack segments */
	movl	$__KERNEL_DS, %eax
	movl	%eax, %ds
	movl	%eax, %ss

	/* Set up new stack */
	leal	TRAMPOLINE_32BIT_STACK_END(%ecx), %esp

	/* Disable paging */
	movl	%cr0, %eax
	btrl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0

	/* Check what paging mode we want to be in after the trampoline */
	testl	%edx, %edx
	jz	1f

	/* We want 5-level paging: don't touch CR3 if it already points to 5-level page tables */
	movl	%cr4, %eax
	testl	$X86_CR4_LA57, %eax
	jnz	3f
	jmp	2f
1:
	/* We want 4-level paging: don't touch CR3 if it already points to 4-level page tables */
	movl	%cr4, %eax
	testl	$X86_CR4_LA57, %eax
	jz	3f
2:
	/* Point CR3 to the trampoline's new top level page table */
	leal	TRAMPOLINE_32BIT_PGTABLE_OFFSET(%ecx), %eax
	movl	%eax, %cr3
3:
	/* Set EFER.LME=1 as a precaution in case hypervsior pulls the rug */
	pushl	%ecx
	pushl	%edx
	movl	$MSR_EFER, %ecx
	rdmsr
	btsl	$_EFER_LME, %eax
	/* Avoid writing EFER if no change was made (for TDX guest) */
	jc	1f
	wrmsr
1:	popl	%edx
	popl	%ecx

#ifdef CONFIG_X86_MCE
	/*
	 * Preserve CR4.MCE if the kernel will enable #MC support.
	 * Clearing MCE may fault in some environments (that also force #MC
	 * support). Any machine check that occurs before #MC support is fully
	 * configured will crash the system regardless of the CR4.MCE value set
	 * here.
	 */
	movl	%cr4, %eax
	andl	$X86_CR4_MCE, %eax
#else
	movl	$0, %eax
#endif

	/* Enable PAE and LA57 (if required) paging modes */
	orl	$X86_CR4_PAE, %eax
	testl	%edx, %edx
	jz	1f
	orl	$X86_CR4_LA57, %eax
1:
	movl	%eax, %cr4

	/* Calculate address of paging_enabled() once we are executing in the trampoline */
	leal	.Lpaging_enabled - trampoline_32bit_src + TRAMPOLINE_32BIT_CODE_OFFSET(%ecx), %eax

	/* Prepare the stack for far return to Long Mode */
	pushl	$__KERNEL_CS
	pushl	%eax

	/* Enable paging again. */
	movl	%cr0, %eax
	btsl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0

	lret
SYM_CODE_END(trampoline_32bit_src)

	.code64
SYM_FUNC_START_LOCAL_NOALIGN(.Lpaging_enabled)
	/* Return from the trampoline */
	jmp	*%rdi
SYM_FUNC_END(.Lpaging_enabled)

	/*
         * The trampoline code has a size limit.
         * Make sure we fail to compile if the trampoline code grows
         * beyond TRAMPOLINE_32BIT_CODE_SIZE bytes.
	 */
	.org	trampoline_32bit_src + TRAMPOLINE_32BIT_CODE_SIZE

	.code32
SYM_FUNC_START_LOCAL_NOALIGN(.Lno_longmode)
	/* This isn't an x86-64 CPU, so hang intentionally, we cannot continue */
1:
	hlt
	jmp     1b
SYM_FUNC_END(.Lno_longmode)

#include "../../kernel/verify_cpu.S"

	.data
SYM_DATA_START_LOCAL(gdt64)
	.word	gdt_end - gdt - 1
	.quad   gdt - gdt64
SYM_DATA_END(gdt64)
	.balign	8
SYM_DATA_START_LOCAL(gdt)
	.word	gdt_end - gdt - 1
	.long	0
	.word	0
	.quad	0x00cf9a000000ffff	/* __KERNEL32_CS */
	.quad	0x00af9a000000ffff	/* __KERNEL_CS */
	.quad	0x00cf92000000ffff	/* __KERNEL_DS */
	.quad	0x0080890000000000	/* TS descriptor */
	.quad   0x0000000000000000	/* TS continued */
SYM_DATA_END_LABEL(gdt, SYM_L_LOCAL, gdt_end)

SYM_DATA_START(boot_idt_desc)
	.word	boot_idt_end - boot_idt - 1
	.quad	0
SYM_DATA_END(boot_idt_desc)
	.balign 8
SYM_DATA_START(boot_idt)
	.rept	BOOT_IDT_ENTRIES
	.quad	0
	.quad	0
	.endr
SYM_DATA_END_LABEL(boot_idt, SYM_L_GLOBAL, boot_idt_end)

#ifdef CONFIG_AMD_MEM_ENCRYPT
SYM_DATA_START(boot32_idt_desc)
	.word   boot32_idt_end - boot32_idt - 1
	.long   0
SYM_DATA_END(boot32_idt_desc)
	.balign 8
SYM_DATA_START(boot32_idt)
	.rept 32
	.quad 0
	.endr
SYM_DATA_END_LABEL(boot32_idt, SYM_L_GLOBAL, boot32_idt_end)
#endif

#ifdef CONFIG_EFI_STUB
SYM_DATA(image_offset, .long 0)
#endif
#ifdef CONFIG_EFI_MIXED
SYM_DATA_LOCAL(efi32_boot_args, .long 0, 0, 0)
SYM_DATA(efi_is64, .byte 1)

#define ST32_boottime		60 // offsetof(efi_system_table_32_t, boottime)
#define BS32_handle_protocol	88 // offsetof(efi_boot_services_32_t, handle_protocol)
#define LI32_image_base		32 // offsetof(efi_loaded_image_32_t, image_base)

	__HEAD
	.code32
SYM_FUNC_START(efi32_pe_entry)
/*
 * efi_status_t efi32_pe_entry(efi_handle_t image_handle,
 *			       efi_system_table_32_t *sys_table)
 */

	pushl	%ebp
	movl	%esp, %ebp
	pushl	%eax				// dummy push to allocate loaded_image

	pushl	%ebx				// save callee-save registers
	pushl	%edi

	call	verify_cpu			// check for long mode support
	testl	%eax, %eax
	movl	$0x80000003, %eax		// EFI_UNSUPPORTED
	jnz	2f

	call	1f
1:	pop	%ebx
	subl	$ rva(1b), %ebx

	/* Get the loaded image protocol pointer from the image handle */
	leal	-4(%ebp), %eax
	pushl	%eax				// &loaded_image
	leal	rva(loaded_image_proto)(%ebx), %eax
	pushl	%eax				// pass the GUID address
	pushl	8(%ebp)				// pass the image handle

	/*
	 * Note the alignment of the stack frame.
	 *   sys_table
	 *   handle             <-- 16-byte aligned on entry by ABI
	 *   return address
	 *   frame pointer
	 *   loaded_image       <-- local variable
	 *   saved %ebx		<-- 16-byte aligned here
	 *   saved %edi
	 *   &loaded_image
	 *   &loaded_image_proto
	 *   handle             <-- 16-byte aligned for call to handle_protocol
	 */

	movl	12(%ebp), %eax			// sys_table
	movl	ST32_boottime(%eax), %eax	// sys_table->boottime
	call	*BS32_handle_protocol(%eax)	// sys_table->boottime->handle_protocol
	addl	$12, %esp			// restore argument space
	testl	%eax, %eax
	jnz	2f

	movl	8(%ebp), %ecx			// image_handle
	movl	12(%ebp), %edx			// sys_table
	movl	-4(%ebp), %esi			// loaded_image
	movl	LI32_image_base(%esi), %esi	// loaded_image->image_base
	movl	%ebx, %ebp			// startup_32 for efi32_pe_stub_entry
	/*
	 * We need to set the image_offset variable here since startup_32() will
	 * use it before we get to the 64-bit efi_pe_entry() in C code.
	 */
	subl	%esi, %ebx
	movl	%ebx, rva(image_offset)(%ebp)	// save image_offset
	jmp	efi32_pe_stub_entry

2:	popl	%edi				// restore callee-save registers
	popl	%ebx
	leave
	RET
SYM_FUNC_END(efi32_pe_entry)

	.section ".rodata"
	/* EFI loaded image protocol GUID */
	.balign 4
SYM_DATA_START_LOCAL(loaded_image_proto)
	.long	0x5b1b31a1
	.word	0x9562, 0x11d2
	.byte	0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b
SYM_DATA_END(loaded_image_proto)
#endif

#ifdef CONFIG_AMD_MEM_ENCRYPT
	__HEAD
	.code32
/*
 * Write an IDT entry into boot32_idt
 *
 * Parameters:
 *
 * %eax:	Handler address
 * %edx:	Vector number
 *
 * Physical offset is expected in %ebp
 */
SYM_FUNC_START(startup32_set_idt_entry)
	push    %ebx
	push    %ecx

	/* IDT entry address to %ebx */
	leal    rva(boot32_idt)(%ebp), %ebx
	shl	$3, %edx
	addl    %edx, %ebx

	/* Build IDT entry, lower 4 bytes */
	movl    %eax, %edx
	andl    $0x0000ffff, %edx	# Target code segment offset [15:0]
	movl    $__KERNEL32_CS, %ecx	# Target code segment selector
	shl     $16, %ecx
	orl     %ecx, %edx

	/* Store lower 4 bytes to IDT */
	movl    %edx, (%ebx)

	/* Build IDT entry, upper 4 bytes */
	movl    %eax, %edx
	andl    $0xffff0000, %edx	# Target code segment offset [31:16]
	orl     $0x00008e00, %edx	# Present, Type 32-bit Interrupt Gate

	/* Store upper 4 bytes to IDT */
	movl    %edx, 4(%ebx)

	pop     %ecx
	pop     %ebx
	RET
SYM_FUNC_END(startup32_set_idt_entry)
#endif

SYM_FUNC_START(startup32_load_idt)
#ifdef CONFIG_AMD_MEM_ENCRYPT
	/* #VC handler */
	leal    rva(startup32_vc_handler)(%ebp), %eax
	movl    $X86_TRAP_VC, %edx
	call    startup32_set_idt_entry

	/* Load IDT */
	leal	rva(boot32_idt)(%ebp), %eax
	movl	%eax, rva(boot32_idt_desc+2)(%ebp)
	lidt    rva(boot32_idt_desc)(%ebp)
#endif
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
#ifdef CONFIG_AMD_MEM_ENCRYPT
	pushl	%eax
	pushl	%ebx
	pushl	%ecx
	pushl	%edx

	/* Check for non-zero sev_status */
	movl	rva(sev_status)(%ebp), %eax
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
	movl	%eax, rva(sev_check_data)(%ebp)
	movl	%ebx, rva(sev_check_data+4)(%ebp)

	/* Enable paging to see if encryption is active */
	movl	%cr0, %edx			 /* Backup %cr0 in %edx */
	movl	$(X86_CR0_PG | X86_CR0_PE), %ecx /* Enable Paging and Protected mode */
	movl	%ecx, %cr0

	cmpl	%eax, rva(sev_check_data)(%ebp)
	jne	3f
	cmpl	%ebx, rva(sev_check_data+4)(%ebp)
	jne	3f

	movl	%edx, %cr0	/* Restore previous %cr0 */

	jmp	4f

3:	/* Check failed - hlt the machine */
	hlt
	jmp	3b

4:
	popl	%edx
	popl	%ecx
	popl	%ebx
	popl	%eax
#endif
	RET
SYM_FUNC_END(startup32_check_sev_cbit)

/*
 * Stack and heap for uncompression
 */
	.bss
	.balign 4
SYM_DATA_LOCAL(boot_heap,	.fill BOOT_HEAP_SIZE, 1, 0)

SYM_DATA_START_LOCAL(boot_stack)
	.fill BOOT_STACK_SIZE, 1, 0
	.balign 16
SYM_DATA_END_LABEL(boot_stack, SYM_L_LOCAL, boot_stack_end)

/*
 * Space for page tables (not in .bss so not zeroed)
 */
	.section ".pgtable","aw",@nobits
	.balign 4096
SYM_DATA_LOCAL(pgtable,		.fill BOOT_PGT_SIZE, 1, 0)

/*
 * The page table is going to be used instead of page table in the trampoline
 * memory.
 */
SYM_DATA_LOCAL(top_pgtable,	.fill PAGE_SIZE, 1, 0)
