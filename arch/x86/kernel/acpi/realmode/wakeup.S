/*
 * ACPI wakeup real mode startup stub
 */
#include <asm/segment.h>
#include <asm/msr-index.h>
#include <asm/page.h>
#include <asm/pgtable.h>

	.code16
	.section ".header", "a"

/* This should match the structure in wakeup.h */
		.globl	wakeup_header
wakeup_header:
video_mode:	.short	0	/* Video mode number */
pmode_return:	.byte	0x66, 0xea	/* ljmpl */
		.long	0	/* offset goes here */
		.short	__KERNEL_CS
pmode_cr0:	.long	0	/* Saved %cr0 */
pmode_cr3:	.long	0	/* Saved %cr3 */
pmode_cr4:	.long	0	/* Saved %cr4 */
pmode_efer:	.quad	0	/* Saved EFER */
pmode_gdt:	.quad	0
realmode_flags:	.long	0
real_magic:	.long	0
trampoline_segment:	.word 0
signature:	.long	0x51ee1111

	.text
	.globl	_start
	.code16
wakeup_code:
_start:
	cli
	cld

	/* Set up segments */
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss

	movl	$wakeup_stack_end, %esp

	/* Clear the EFLAGS */
	pushl	$0
	popfl

	/* Check header signature... */
	movl	signature, %eax
	cmpl	$0x51ee1111, %eax
	jne	bogus_real_magic

	/* Check we really have everything... */
	movl	end_signature, %eax
	cmpl	$0x65a22c82, %eax
	jne	bogus_real_magic

	/* Call the C code */
	calll	main

	/* Do any other stuff... */

#ifndef CONFIG_64BIT
	/* This could also be done in C code... */
	movl	pmode_cr3, %eax
	movl	%eax, %cr3

	movl	pmode_cr4, %ecx
	jecxz	1f
	movl	%ecx, %cr4
1:
	movl	pmode_efer, %eax
	movl	pmode_efer + 4, %edx
	movl	%eax, %ecx
	orl	%edx, %ecx
	jz	1f
	movl	$0xc0000080, %ecx
	wrmsr
1:

	lgdtl	pmode_gdt

	/* This really couldn't... */
	movl	pmode_cr0, %eax
	movl	%eax, %cr0
	jmp	pmode_return
#else
	pushw	$0
	pushw	trampoline_segment
	pushw	$0
	lret
#endif

bogus_real_magic:
1:
	hlt
	jmp	1b

	.data
	.balign	4
	.globl	HEAP, heap_end
HEAP:
	.long	wakeup_heap
heap_end:
	.long	wakeup_stack

	.bss
wakeup_heap:
	.space	2048
wakeup_stack:
	.space	2048
wakeup_stack_end:
