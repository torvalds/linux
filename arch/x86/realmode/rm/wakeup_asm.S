/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ACPI wakeup real mode startup stub
 */
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/msr-index.h>
#include <asm/page_types.h>
#include <asm/pgtable_types.h>
#include <asm/processor-flags.h>
#include "realmode.h"
#include "wakeup.h"

	.code16

/* This should match the structure in wakeup.h */
	.section ".data", "aw"

	.balign	16
GLOBAL(wakeup_header)
	video_mode:	.short	0	/* Video mode number */
	pmode_entry:	.long	0
	pmode_cs:	.short	__KERNEL_CS
	pmode_cr0:	.long	0	/* Saved %cr0 */
	pmode_cr3:	.long	0	/* Saved %cr3 */
	pmode_cr4:	.long	0	/* Saved %cr4 */
	pmode_efer:	.quad	0	/* Saved EFER */
	pmode_gdt:	.quad	0
	pmode_misc_en:	.quad	0	/* Saved MISC_ENABLE MSR */
	pmode_behavior:	.long	0	/* Wakeup behavior flags */
	realmode_flags:	.long	0
	real_magic:	.long	0
	signature:	.long	WAKEUP_HEADER_SIGNATURE
END(wakeup_header)

	.text
	.code16

	.balign	16
ENTRY(wakeup_start)
	cli
	cld

	LJMPW_RM(3f)
3:
	/* Apparently some dimwit BIOS programmers don't know how to
	   program a PM to RM transition, and we might end up here with
	   junk in the data segment descriptor registers.  The only way
	   to repair that is to go into PM and fix it ourselves... */
	movw	$16, %cx
	lgdtl	%cs:wakeup_gdt
	movl	%cr0, %eax
	orb	$X86_CR0_PE, %al
	movl	%eax, %cr0
	ljmpw	$8, $2f
2:
	movw	%cx, %ds
	movw	%cx, %es
	movw	%cx, %ss
	movw	%cx, %fs
	movw	%cx, %gs

	andb	$~X86_CR0_PE, %al
	movl	%eax, %cr0
	LJMPW_RM(3f)
3:
	/* Set up segments */
	movw	%cs, %ax
	movw	%ax, %ss
	movl	$rm_stack_end, %esp
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs

	lidtl	wakeup_idt

	/* Clear the EFLAGS */
	pushl $0
	popfl

	/* Check header signature... */
	movl	signature, %eax
	cmpl	$WAKEUP_HEADER_SIGNATURE, %eax
	jne	bogus_real_magic

	/* Check we really have everything... */
	movl	end_signature, %eax
	cmpl	$REALMODE_END_SIGNATURE, %eax
	jne	bogus_real_magic

	/* Call the C code */
	calll	main

	/* Restore MISC_ENABLE before entering protected mode, in case
	   BIOS decided to clear XD_DISABLE during S3. */
	movl	pmode_behavior, %edi
	btl	$WAKEUP_BEHAVIOR_RESTORE_MISC_ENABLE, %edi
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

	btl	$WAKEUP_BEHAVIOR_RESTORE_CR4, %edi
	jnc	1f
	movl	pmode_cr4, %eax
	movl	%eax, %cr4
1:
	btl	$WAKEUP_BEHAVIOR_RESTORE_EFER, %edi
	jnc	1f
	movl	pmode_efer, %eax
	movl	pmode_efer + 4, %edx
	movl	$MSR_EFER, %ecx
	wrmsr
1:

	lgdtl	pmode_gdt

	/* This really couldn't... */
	movl	pmode_entry, %eax
	movl	pmode_cr0, %ecx
	movl	%ecx, %cr0
	ljmpl	$__KERNEL_CS, $pa_startup_32
	/* -> jmp *%eax in trampoline_32.S */
#else
	jmp	trampoline_start
#endif

bogus_real_magic:
1:
	hlt
	jmp	1b

	.section ".rodata","a"

	/*
	 * Set up the wakeup GDT.  We set these up as Big Real Mode,
	 * that is, with limits set to 4 GB.  At least the Lenovo
	 * Thinkpad X61 is known to need this for the video BIOS
	 * initialization quirk to work; this is likely to also
	 * be the case for other laptops or integrated video devices.
	 */

	.balign	16
GLOBAL(wakeup_gdt)
	.word	3*8-1		/* Self-descriptor */
	.long	pa_wakeup_gdt
	.word	0

	.word	0xffff		/* 16-bit code segment @ real_mode_base */
	.long	0x9b000000 + pa_real_mode_base
	.word	0x008f		/* big real mode */

	.word	0xffff		/* 16-bit data segment @ real_mode_base */
	.long	0x93000000 + pa_real_mode_base
	.word	0x008f		/* big real mode */
END(wakeup_gdt)

	.section ".rodata","a"
	.balign	8

	/* This is the standard real-mode IDT */
	.balign	16
GLOBAL(wakeup_idt)
	.word	0xffff		/* limit */
	.long	0		/* address */
	.word	0
END(wakeup_idt)
