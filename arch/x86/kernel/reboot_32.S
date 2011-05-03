#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/segment.h>
#include <asm/page_types.h>

/*
 * The following code and data reboots the machine by switching to real
 * mode and jumping to the BIOS reset entry point, as if the CPU has
 * really been reset.  The previous version asked the keyboard
 * controller to pulse the CPU reset line, which is more thorough, but
 * doesn't work with at least one type of 486 motherboard.  It is easy
 * to stop this code working; hence the copious comments.
 *
 * This code is called with the restart type (0 = BIOS, 1 = APM) in %eax.
 */
	.section ".x86_trampoline","a"
	.balign 16
	.code32
ENTRY(machine_real_restart_asm)
r_base = .
	/* Get our own relocated address */
	call	1f
1:	popl	%ebx
	subl	$(1b - r_base), %ebx

	/* Compute the equivalent real-mode segment */
	movl	%ebx, %ecx
	shrl	$4, %ecx
	
	/* Patch post-real-mode segment jump */
	movw	(dispatch_table - r_base)(%ebx,%eax,2),%ax
	movw	%ax, (101f - r_base)(%ebx)
	movw	%cx, (102f - r_base)(%ebx)

	/* Set up the IDT for real mode. */
	lidtl	(machine_real_restart_idt - r_base)(%ebx)

	/*
	 * Set up a GDT from which we can load segment descriptors for real
	 * mode.  The GDT is not used in real mode; it is just needed here to
	 * prepare the descriptors.
	 */
	lgdtl	(machine_real_restart_gdt - r_base)(%ebx)

	/*
	 * Load the data segment registers with 16-bit compatible values
	 */
	movl	$16, %ecx
	movl	%ecx, %ds
	movl	%ecx, %es
	movl	%ecx, %fs
	movl	%ecx, %gs
	movl	%ecx, %ss
	ljmpl	$8, $1f - r_base

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
	.code16
1:
	xorl	%ecx, %ecx
	movl	%cr0, %eax
	andl	$0x00000011, %eax
	orl	$0x60000000, %eax
	movl	%eax, %cr0
	movl	%ecx, %cr3
	movl	%cr0, %edx
	andl	$0x60000000, %edx	/* If no cache bits -> no wbinvd */
	jz	2f
	wbinvd
2:
	andb	$0x10, %al
	movl	%eax, %cr0
	.byte	0xea			/* ljmpw */
101:	.word	0			/* Offset */
102:	.word	0			/* Segment */

bios:
	ljmpw	$0xf000, $0xfff0

apm:
	movw	$0x1000, %ax
	movw	%ax, %ss
	movw	$0xf000, %sp
	movw	$0x5307, %ax
	movw	$0x0001, %bx
	movw	$0x0003, %cx
	int	$0x15

END(machine_real_restart_asm)

	.balign 16
	/* These must match <asm/reboot.h */
dispatch_table:
	.word	bios - r_base
	.word	apm - r_base
END(dispatch_table)

	.balign 16
machine_real_restart_idt:
	.word	0xffff		/* Length - real mode default value */
	.long	0		/* Base - real mode default value */
END(machine_real_restart_idt)

	.balign 16
ENTRY(machine_real_restart_gdt)
	.quad	0		/* Self-pointer, filled in by PM code */
	.quad	0		/* 16-bit code segment, filled in by PM code */
	/*
	 * 16-bit data segment with the selector value 16 = 0x10 and
	 * base value 0x100; since this is consistent with real mode
	 * semantics we don't have to reload the segments once CR0.PE = 0.
	 */
	.quad	GDT_ENTRY(0x0093, 0x100, 0xffff)
END(machine_real_restart_gdt)
