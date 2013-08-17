/*
 * ACPI wakeup real mode startup stub
 */
#include <asm/segment.h>
#include <asm/msr-index.h>
#include <asm/page_types.h>
#include <asm/pgtable_types.h>
#include <asm/processor-flags.h>
#include "wakeup.h"

	.code16
	.section ".jump", "ax"
	.globl	_start
_start:
	cli
	jmp	wakeup_code

/* This should match the structure in wakeup.h */
		.section ".header", "a"
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
pmode_misc_en:	.quad	0	/* Saved MISC_ENABLE MSR */
pmode_behavior:	.long	0	/* Wakeup behavior flags */
realmode_flags:	.long	0
real_magic:	.long	0
trampoline_segment:	.word 0
_pad1:		.byte	0
wakeup_jmp:	.byte	0xea	/* ljmpw */
wakeup_jmp_off:	.word	3f
wakeup_jmp_seg:	.word	0
wakeup_gdt:	.quad	0, 0, 0
signature:	.long	WAKEUP_HEADER_SIGNATURE

	.text
	.code16
wakeup_code:
	cld

	/* Apparently some dimwit BIOS programmers don't know how to
	   program a PM to RM transition, and we might end up here with
	   junk in the data segment descriptor registers.  The only way
	   to repair that is to go into PM and fix it ourselves... */
	movw	$16, %cx
	lgdtl	%cs:wakeup_gdt
	movl	%cr0, %eax
	orb	$X86_CR0_PE, %al
	movl	%eax, %cr0
	jmp	1f
1:	ljmpw	$8, $2f
2:
	movw	%cx, %ds
	movw	%cx, %es
	movw	%cx, %ss
	movw	%cx, %fs
	movw	%cx, %gs

	andb	$~X86_CR0_PE, %al
	movl	%eax, %cr0
	jmp	wakeup_jmp
3:
	/* Set up segments */
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	lidtl	wakeup_idt

	movl	$wakeup_stack_end, %esp

	/* Clear the EFLAGS */
	pushl	$0
	popfl

	/* Check header signature... */
	movl	signature, %eax
	cmpl	$WAKEUP_HEADER_SIGNATURE, %eax
	jne	bogus_real_magic

	/* Check we really have everything... */
	movl	end_signature, %eax
	cmpl	$WAKEUP_END_SIGNATURE, %eax
	jne	bogus_real_magic

	/* Call the C code */
	calll	main

	/* Restore MISC_ENABLE before entering protected mode, in case
	   BIOS decided to clear XD_DISABLE during S3. */
	movl	pmode_behavior, %eax
	btl	$WAKEUP_BEHAVIOR_RESTORE_MISC_ENABLE, %eax
	jnc	1f

	movl	pmode_misc_en, %eax
	movl	pmode_misc_en + 4, %edx
	movl	$MSR_IA32_MISC_ENABLE, %ecx
	wrmsr
1:

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
	movl	$MSR_EFER, %ecx
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
	.balign	8

	/* This is the standard real-mode IDT */
wakeup_idt:
	.word	0xffff		/* limit */
	.long	0		/* address */
	.word	0

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

	.section ".signature","a"
end_signature:
	.long	WAKEUP_END_SIGNATURE
