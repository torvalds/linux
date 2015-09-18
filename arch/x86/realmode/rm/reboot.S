#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page_types.h>
#include <asm/processor-flags.h>
#include <asm/msr-index.h>
#include "realmode.h"

/*
 * The following code and data reboots the machine by switching to real
 * mode and jumping to the BIOS reset entry point, as if the CPU has
 * really been reset.  The previous version asked the keyboard
 * controller to pulse the CPU reset line, which is more thorough, but
 * doesn't work with at least one type of 486 motherboard.  It is easy
 * to stop this code working; hence the copious comments.
 *
 * This code is called with the restart type (0 = BIOS, 1 = APM) in
 * the primary argument register (%eax for 32 bit, %edi for 64 bit).
 */
	.section ".text32", "ax"
	.code32
ENTRY(machine_real_restart_asm)

#ifdef CONFIG_X86_64
	/* Switch to trampoline GDT as it is guaranteed < 4 GiB */
	movl	$__KERNEL_DS, %eax
	movl	%eax, %ds
	lgdtl	pa_tr_gdt

	/* Disable paging to drop us out of long mode */
	movl	%cr0, %eax
	andl	$~X86_CR0_PG, %eax
	movl	%eax, %cr0
	ljmpl	$__KERNEL32_CS, $pa_machine_real_restart_paging_off

GLOBAL(machine_real_restart_paging_off)
	xorl	%eax, %eax
	xorl	%edx, %edx
	movl	$MSR_EFER, %ecx
	wrmsr

	movl	%edi, %eax
	
#endif /* CONFIG_X86_64 */
	
	/* Set up the IDT for real mode. */
	lidtl	pa_machine_real_restart_idt

	/*
	 * Set up a GDT from which we can load segment descriptors for real
	 * mode.  The GDT is not used in real mode; it is just needed here to
	 * prepare the descriptors.
	 */
	lgdtl	pa_machine_real_restart_gdt

	/*
	 * Load the data segment registers with 16-bit compatible values
	 */
	movl	$16, %ecx
	movl	%ecx, %ds
	movl	%ecx, %es
	movl	%ecx, %fs
	movl	%ecx, %gs
	movl	%ecx, %ss
	ljmpw	$8, $1f

/*
 * This is 16-bit protected mode code to disable paging and the cache,
 * switch to real mode and jump to the BIOS reset code.
 *
 * The instruction that switches to real mode by writing to CR0 must be
 * followed immediately by a far jump instruction, which set CS to a
 * valid value for real mode, and flushes the prefetch queue to avoid
 * running instructions that have already been decoded in protected
 * mode.
 *
 * Clears all the flags except ET, especially PG (paging), PE
 * (protected-mode enable) and TS (task switch for coprocessor state
 * save).  Flushes the TLB after paging has been disabled.  Sets CD and
 * NW, to disable the cache on a 486, and invalidates the cache.  This
 * is more like the state of a 486 after reset.  I don't know if
 * something else should be done for other chips.
 *
 * More could be done here to set up the registers as if a CPU reset had
 * occurred; hopefully real BIOSs don't assume much.  This is not the
 * actual BIOS entry point, anyway (that is at 0xfffffff0).
 *
 * Most of this work is probably excessive, but it is what is tested.
 */
	.text
	.code16

	.balign	16
machine_real_restart_asm16:
1:
	xorl	%ecx, %ecx
	movl	%cr0, %edx
	andl	$0x00000011, %edx
	orl	$0x60000000, %edx
	movl	%edx, %cr0
	movl	%ecx, %cr3
	movl	%cr0, %edx
	testl	$0x60000000, %edx	/* If no cache bits -> no wbinvd */
	jz	2f
	wbinvd
2:
	andb	$0x10, %dl
	movl	%edx, %cr0
	LJMPW_RM(3f)
3:
	andw	%ax, %ax
	jz	bios

apm:
	movw	$0x1000, %ax
	movw	%ax, %ss
	movw	$0xf000, %sp
	movw	$0x5307, %ax
	movw	$0x0001, %bx
	movw	$0x0003, %cx
	int	$0x15
	/* This should never return... */

bios:
	ljmpw	$0xf000, $0xfff0

	.section ".rodata", "a"

	.balign	16
GLOBAL(machine_real_restart_idt)
	.word	0xffff		/* Length - real mode default value */
	.long	0		/* Base - real mode default value */
END(machine_real_restart_idt)

	.balign	16
GLOBAL(machine_real_restart_gdt)
	/* Self-pointer */
	.word	0xffff		/* Length - real mode default value */
	.long	pa_machine_real_restart_gdt
	.word	0

	/*
	 * 16-bit code segment pointing to real_mode_seg
	 * Selector value 8
	 */
	.word	0xffff		/* Limit */
	.long	0x9b000000 + pa_real_mode_base
	.word	0

	/*
	 * 16-bit data segment with the selector value 16 = 0x10 and
	 * base value 0x100; since this is consistent with real mode
	 * semantics we don't have to reload the segments once CR0.PE = 0.
	 */
	.quad	GDT_ENTRY(0x0093, 0x100, 0xffff)
END(machine_real_restart_gdt)
