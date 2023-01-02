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
	.text

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page_types.h>
#include <asm/boot.h>
#include <asm/asm-offsets.h>
#include <asm/bootparam.h>

/*
 * These symbols needed to be marked as .hidden to prevent the BFD linker from
 * generating R_386_32 (rather than R_386_RELATIVE) relocations for them when
 * the 32-bit compressed kernel is linked as PIE. This is no longer necessary,
 * but it doesn't hurt to keep them .hidden.
 */
	.hidden _bss
	.hidden _ebss
	.hidden _end

	__HEAD
SYM_FUNC_START(startup_32)
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
1:	popl	%edx
	addl	$_GLOBAL_OFFSET_TABLE_+(.-1b), %edx

	/* Load new GDT */
	leal	gdt@GOTOFF(%edx), %eax
	movl	%eax, 2(%eax)
	lgdt	(%eax)

	/* Load segment registers with our descriptors */
	movl	$__BOOT_DS, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %fs
	movl	%eax, %gs
	movl	%eax, %ss

/*
 * %edx contains the address we are loaded at by the boot loader (plus the
 * offset to the GOT).  The below code calculates %ebx to be the address where
 * we should move the kernel image temporarily for safe in-place decompression
 * (again, plus the offset to the GOT).
 *
 * %ebp is calculated to be the address that the kernel will be decompressed to.
 */

#ifdef CONFIG_RELOCATABLE
	leal	startup_32@GOTOFF(%edx), %ebx

#ifdef CONFIG_EFI_STUB
/*
 * If we were loaded via the EFI LoadImage service, startup_32() will be at an
 * offset to the start of the space allocated for the image. efi_pe_entry() will
 * set up image_offset to tell us where the image actually starts, so that we
 * can use the full available buffer.
 *	image_offset = startup_32 - image_base
 * Otherwise image_offset will be zero and has no effect on the calculations.
 */
	subl    image_offset@GOTOFF(%edx), %ebx
#endif

	movl	BP_kernel_alignment(%esi), %eax
	decl	%eax
	addl    %eax, %ebx
	notl	%eax
	andl    %eax, %ebx
	cmpl	$LOAD_PHYSICAL_ADDR, %ebx
	jae	1f
#endif
	movl	$LOAD_PHYSICAL_ADDR, %ebx
1:

	movl	%ebx, %ebp	// Save the output address for later
	/* Target address to relocate to for decompression */
	addl    BP_init_size(%esi), %ebx
	subl    $_end@GOTOFF, %ebx

	/* Set up the stack */
	leal	boot_stack_end@GOTOFF(%ebx), %esp

	/* Zero EFLAGS */
	pushl	$0
	popfl

/*
 * Copy the compressed kernel to the end of our buffer
 * where decompression in place becomes safe.
 */
	pushl	%esi
	leal	(_bss@GOTOFF-4)(%edx), %esi
	leal	(_bss@GOTOFF-4)(%ebx), %edi
	movl	$(_bss - startup_32), %ecx
	shrl	$2, %ecx
	std
	rep	movsl
	cld
	popl	%esi

	/*
	 * The GDT may get overwritten either during the copy we just did or
	 * during extract_kernel below. To avoid any issues, repoint the GDTR
	 * to the new copy of the GDT.
	 */
	leal	gdt@GOTOFF(%ebx), %eax
	movl	%eax, 2(%eax)
	lgdt	(%eax)

/*
 * Jump to the relocated address.
 */
	leal	.Lrelocated@GOTOFF(%ebx), %eax
	jmp	*%eax
SYM_FUNC_END(startup_32)

#ifdef CONFIG_EFI_STUB
SYM_FUNC_START(efi32_stub_entry)
	add	$0x4, %esp
	movl	8(%esp), %esi	/* save boot_params pointer */
	call	efi_main
	/* efi_main returns the possibly relocated address of startup_32 */
	jmp	*%eax
SYM_FUNC_END(efi32_stub_entry)
SYM_FUNC_ALIAS(efi_stub_entry, efi32_stub_entry)
#endif

	.text
SYM_FUNC_START_LOCAL_NOALIGN(.Lrelocated)

/*
 * Clear BSS (stack is currently empty)
 */
	xorl	%eax, %eax
	leal	_bss@GOTOFF(%ebx), %edi
	leal	_ebss@GOTOFF(%ebx), %ecx
	subl	%edi, %ecx
	shrl	$2, %ecx
	rep	stosl

/*
 * Do the extraction, and jump to the new kernel..
 */
	/* push arguments for extract_kernel: */

	pushl	output_len@GOTOFF(%ebx)	/* decompressed length, end of relocs */
	pushl	%ebp			/* output address */
	pushl	input_len@GOTOFF(%ebx)	/* input_len */
	leal	input_data@GOTOFF(%ebx), %eax
	pushl	%eax			/* input_data */
	leal	boot_heap@GOTOFF(%ebx), %eax
	pushl	%eax			/* heap area */
	pushl	%esi			/* real mode pointer */
	call	extract_kernel		/* returns kernel location in %eax */
	addl	$24, %esp

/*
 * Jump to the extracted kernel.
 */
	xorl	%ebx, %ebx
	jmp	*%eax
SYM_FUNC_END(.Lrelocated)

	.data
	.balign	8
SYM_DATA_START_LOCAL(gdt)
	.word	gdt_end - gdt - 1
	.long	0
	.word	0
	.quad	0x0000000000000000	/* Reserved */
	.quad	0x00cf9a000000ffff	/* __KERNEL_CS */
	.quad	0x00cf92000000ffff	/* __KERNEL_DS */
SYM_DATA_END_LABEL(gdt, SYM_L_LOCAL, gdt_end)

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
