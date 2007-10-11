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

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/msr.h>

.section ".text.head"
	.code32
	.globl startup_32

startup_32:
	cld
	cli
	movl	$(__KERNEL_DS), %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %ss

/* Calculate the delta between where we were compiled to run
 * at and where we were actually loaded at.  This can only be done
 * with a short local call on x86.  Nothing  else will tell us what
 * address we are running at.  The reserved chunk of the real-mode
 * data at 0x1e4 (defined as a scratch field) are used as the stack
 * for this calculation. Only 4 bytes are needed.
 */
	leal	(0x1e4+4)(%esi), %esp
	call	1f
1:	popl	%ebp
	subl	$1b, %ebp

/* setup a stack and make sure cpu supports long mode. */
	movl	$user_stack_end, %eax
	addl	%ebp, %eax
	movl	%eax, %esp

	call	verify_cpu
	testl	%eax, %eax
	jnz	no_longmode

/* Compute the delta between where we were compiled to run at
 * and where the code will actually run at.
 */
/* %ebp contains the address we are loaded at by the boot loader and %ebx
 * contains the address where we should move the kernel image temporarily
 * for safe in-place decompression.
 */

#ifdef CONFIG_RELOCATABLE
	movl	%ebp, %ebx
	addl	$(LARGE_PAGE_SIZE -1), %ebx
	andl	$LARGE_PAGE_MASK, %ebx
#else
	movl	$CONFIG_PHYSICAL_START, %ebx
#endif

	/* Replace the compressed data size with the uncompressed size */
	subl	input_len(%ebp), %ebx
	movl	output_len(%ebp), %eax
	addl	%eax, %ebx
	/* Add 8 bytes for every 32K input block */
	shrl	$12, %eax
	addl	%eax, %ebx
	/* Add 32K + 18 bytes of extra slack and align on a 4K boundary */
	addl	$(32768 + 18 + 4095), %ebx
	andl	$~4095, %ebx

/*
 * Prepare for entering 64 bit mode
 */

	/* Load new GDT with the 64bit segments using 32bit descriptor */
	leal	gdt(%ebp), %eax
	movl	%eax, gdt+2(%ebp)
	lgdt	gdt(%ebp)

	/* Enable PAE mode */
	xorl	%eax, %eax
	orl	$(1 << 5), %eax
	movl	%eax, %cr4

 /*
  * Build early 4G boot pagetable
  */
	/* Initialize Page tables to 0*/
	leal	pgtable(%ebx), %edi
	xorl	%eax, %eax
	movl	$((4096*6)/4), %ecx
	rep	stosl

	/* Build Level 4 */
	leal	pgtable + 0(%ebx), %edi
	leal	0x1007 (%edi), %eax
	movl	%eax, 0(%edi)

	/* Build Level 3 */
	leal	pgtable + 0x1000(%ebx), %edi
	leal	0x1007(%edi), %eax
	movl	$4, %ecx
1:	movl	%eax, 0x00(%edi)
	addl	$0x00001000, %eax
	addl	$8, %edi
	decl	%ecx
	jnz	1b

	/* Build Level 2 */
	leal	pgtable + 0x2000(%ebx), %edi
	movl	$0x00000183, %eax
	movl	$2048, %ecx
1:	movl	%eax, 0(%edi)
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

	/* Setup for the jump to 64bit mode
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
	pushl	%eax

	/* Enter paged protected Mode, activating Long Mode */
	movl	$0x80000001, %eax /* Enable Paging and Protected mode */
	movl	%eax, %cr0

	/* Jump from 32bit compatibility mode into 64bit mode. */
	lret

no_longmode:
	/* This isn't an x86-64 CPU so hang */
1:
	hlt
	jmp     1b

#include "../../kernel/verify_cpu_64.S"

	/* Be careful here startup_64 needs to be at a predictable
	 * address so I can export it in an ELF header.  Bootloaders
	 * should look at the ELF header to find this address, as
	 * it may change in the future.
	 */
	.code64
	.org 0x200
ENTRY(startup_64)
	/* We come here either from startup_32 or directly from a
	 * 64bit bootloader.  If we come here from a bootloader we depend on
	 * an identity mapped page table being provied that maps our
	 * entire text+data+bss and hopefully all of memory.
	 */

	/* Setup data segments. */
	xorl	%eax, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %ss
	movl	%eax, %fs
	movl	%eax, %gs
	lldt	%ax
	movl    $0x20, %eax
	ltr	%ax

	/* Compute the decompressed kernel start address.  It is where
	 * we were loaded at aligned to a 2M boundary. %rbp contains the
	 * decompressed kernel start address.
	 *
	 * If it is a relocatable kernel then decompress and run the kernel
	 * from load address aligned to 2MB addr, otherwise decompress and
	 * run the kernel from CONFIG_PHYSICAL_START
	 */

	/* Start with the delta to where the kernel will run at. */
#ifdef CONFIG_RELOCATABLE
	leaq	startup_32(%rip) /* - $startup_32 */, %rbp
	addq	$(LARGE_PAGE_SIZE - 1), %rbp
	andq	$LARGE_PAGE_MASK, %rbp
	movq	%rbp, %rbx
#else
	movq	$CONFIG_PHYSICAL_START, %rbp
	movq	%rbp, %rbx
#endif

	/* Replace the compressed data size with the uncompressed size */
	movl	input_len(%rip), %eax
	subq	%rax, %rbx
	movl	output_len(%rip), %eax
	addq	%rax, %rbx
	/* Add 8 bytes for every 32K input block */
	shrq	$12, %rax
	addq	%rax, %rbx
	/* Add 32K + 18 bytes of extra slack and align on a 4K boundary */
	addq	$(32768 + 18 + 4095), %rbx
	andq	$~4095, %rbx

/* Copy the compressed kernel to the end of our buffer
 * where decompression in place becomes safe.
 */
	leaq	_end(%rip), %r8
	leaq	_end(%rbx), %r9
	movq	$_end /* - $startup_32 */, %rcx
1:	subq	$8, %r8
	subq	$8, %r9
	movq	0(%r8), %rax
	movq	%rax, 0(%r9)
	subq	$8, %rcx
	jnz	1b

/*
 * Jump to the relocated address.
 */
	leaq	relocated(%rbx), %rax
	jmp	*%rax

.section ".text"
relocated:

/*
 * Clear BSS
 */
	xorq	%rax, %rax
	leaq    _edata(%rbx), %rdi
	leaq    _end(%rbx), %rcx
	subq	%rdi, %rcx
	cld
	rep
	stosb

	/* Setup the stack */
	leaq	user_stack_end(%rip), %rsp

	/* zero EFLAGS after setting rsp */
	pushq	$0
	popfq

/*
 * Do the decompression, and jump to the new kernel..
 */
	pushq	%rsi			# Save the real mode argument
	movq	%rsi, %rdi		# real mode address
	leaq	_heap(%rip), %rsi	# _heap
	leaq	input_data(%rip), %rdx  # input_data
	movl	input_len(%rip), %eax
	movq	%rax, %rcx		# input_len
	movq	%rbp, %r8		# output
	call	decompress_kernel
	popq	%rsi


/*
 * Jump to the decompressed kernel.
 */
	jmp	*%rbp

	.data
gdt:
	.word	gdt_end - gdt
	.long	gdt
	.word	0
	.quad	0x0000000000000000	/* NULL descriptor */
	.quad	0x00af9a000000ffff	/* __KERNEL_CS */
	.quad	0x00cf92000000ffff	/* __KERNEL_DS */
	.quad	0x0080890000000000	/* TS descriptor */
	.quad   0x0000000000000000	/* TS continued */
gdt_end:
	.bss
/* Stack for uncompression */
	.balign 4
user_stack:
	.fill 4096,4,0
user_stack_end:
