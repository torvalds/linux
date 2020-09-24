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
 * The 32-bit x86 assembler in binutils 2.26 will generate R_386_GOT32X
 * relocation to get the symbol address in PIC.  When the compressed x86
 * kernel isn't built as PIC, the linker optimizes R_386_GOT32X
 * relocations to their fixed symbol addresses.  However, when the
 * compressed x86 kernel is loaded at a different address, it leads
 * to the following load failure:
 *
 *   Failed to allocate space for phdrs
 *
 * during the decompression stage.
 *
 * If the compressed x86 kernel is relocatable at run-time, it should be
 * compiled with -fPIE, instead of -fPIC, if possible and should be built as
 * Position Independent Executable (PIE) so that linker won't optimize
 * R_386_GOT32X relocation to its fixed symbol address.  Older
 * linkers generate R_386_32 relocations against locally defined symbols,
 * _bss, _ebss, _got, _egot and _end, in PIE.  It isn't wrong, just less
 * optimal than R_386_RELATIVE.  But the x86 kernel fails to properly handle
 * R_386_32 relocations when relocating the kernel.  To generate
 * R_386_RELATIVE relocations, we mark _bss, _ebss, _got, _egot and _end as
 * hidden:
 */
	.hidden _bss
	.hidden _ebss
	.hidden _got
	.hidden _egot
	.hidden _end

	__HEAD
ENTRY(startup_32)
	cld
	/*
	 * Test KEEP_SEGMENTS flag to see if the bootloader is asking
	 * us to not reload segments
	 */
	testb	$KEEP_SEGMENTS, BP_loadflags(%esi)
	jnz	1f

	cli
	movl	$__BOOT_DS, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %fs
	movl	%eax, %gs
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

/*
 * %ebp contains the address we are loaded at by the boot loader and %ebx
 * contains the address where we should move the kernel image temporarily
 * for safe in-place decompression.
 */

#ifdef CONFIG_RELOCATABLE
	movl	%ebp, %ebx
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

	/* Target address to relocate to for decompression */
	movl    BP_init_size(%esi), %eax
	subl    $_end, %eax
	addl    %eax, %ebx

	/* Set up the stack */
	leal	boot_stack_end(%ebx), %esp

	/* Zero EFLAGS */
	pushl	$0
	popfl

/*
 * Copy the compressed kernel to the end of our buffer
 * where decompression in place becomes safe.
 */
	pushl	%esi
	leal	(_bss-4)(%ebp), %esi
	leal	(_bss-4)(%ebx), %edi
	movl	$(_bss - startup_32), %ecx
	shrl	$2, %ecx
	std
	rep	movsl
	cld
	popl	%esi

/*
 * Jump to the relocated address.
 */
	leal	relocated(%ebx), %eax
	jmp	*%eax
ENDPROC(startup_32)

#ifdef CONFIG_EFI_STUB
/*
 * We don't need the return address, so set up the stack so efi_main() can find
 * its arguments.
 */
ENTRY(efi_pe_entry)
	add	$0x4, %esp

	call	1f
1:	popl	%esi
	subl	$1b, %esi

	popl	%ecx
	movl	%ecx, efi32_config(%esi)	/* Handle */
	popl	%ecx
	movl	%ecx, efi32_config+8(%esi)	/* EFI System table pointer */

	/* Relocate efi_config->call() */
	leal	efi32_config(%esi), %eax
	add	%esi, 40(%eax)
	pushl	%eax

	call	make_boot_params
	cmpl	$0, %eax
	je	fail
	movl	%esi, BP_code32_start(%eax)
	popl	%ecx
	pushl	%eax
	pushl	%ecx
	jmp	2f		/* Skip efi_config initialization */
ENDPROC(efi_pe_entry)

ENTRY(efi32_stub_entry)
	add	$0x4, %esp
	popl	%ecx
	popl	%edx

	call	1f
1:	popl	%esi
	subl	$1b, %esi

	movl	%ecx, efi32_config(%esi)	/* Handle */
	movl	%edx, efi32_config+8(%esi)	/* EFI System table pointer */

	/* Relocate efi_config->call() */
	leal	efi32_config(%esi), %eax
	add	%esi, 40(%eax)
	pushl	%eax
2:
	call	efi_main
	cmpl	$0, %eax
	movl	%eax, %esi
	jne	2f
fail:
	/* EFI init failed, so hang. */
	hlt
	jmp	fail
2:
	movl	BP_code32_start(%esi), %eax
	leal	startup_32(%eax), %eax
	jmp	*%eax
ENDPROC(efi32_stub_entry)
#endif

	.text
relocated:

/*
 * Clear BSS (stack is currently empty)
 */
	xorl	%eax, %eax
	leal	_bss(%ebx), %edi
	leal	_ebss(%ebx), %ecx
	subl	%edi, %ecx
	shrl	$2, %ecx
	rep	stosl

/*
 * Adjust our own GOT
 */
	leal	_got(%ebx), %edx
	leal	_egot(%ebx), %ecx
1:
	cmpl	%ecx, %edx
	jae	2f
	addl	%ebx, (%edx)
	addl	$4, %edx
	jmp	1b
2:

/*
 * Do the extraction, and jump to the new kernel..
 */
				/* push arguments for extract_kernel: */
	pushl	$z_output_len	/* decompressed length, end of relocs */

	movl    BP_init_size(%esi), %eax
	subl    $_end, %eax
	movl    %ebx, %ebp
	subl    %eax, %ebp
	pushl	%ebp		/* output address */

	pushl	$z_input_len	/* input_len */
	leal	input_data(%ebx), %eax
	pushl	%eax		/* input_data */
	leal	boot_heap(%ebx), %eax
	pushl	%eax		/* heap area */
	pushl	%esi		/* real mode pointer */
	call	extract_kernel	/* returns kernel location in %eax */
	addl	$24, %esp

/*
 * Jump to the extracted kernel.
 */
	xorl	%ebx, %ebx
	jmp	*%eax

#ifdef CONFIG_EFI_STUB
	.data
efi32_config:
	.fill 5,8,0
	.long efi_call_phys
	.long 0
	.byte 0
#endif

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
