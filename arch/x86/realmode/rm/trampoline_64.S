/*
 *
 *	Trampoline.S	Derived from Setup.S by Linus Torvalds
 *
 *	4 Jan 1997 Michael Chastain: changed to gnu as.
 *	15 Sept 2005 Eric Biederman: 64bit PIC support
 *
 *	Entry: CS:IP point to the start of our code, we are
 *	in real mode with no stack, but the rest of the
 *	trampoline page to make our stack and everything else
 *	is a mystery.
 *
 *	On entry to trampoline_start, the processor is in real mode
 *	with 16-bit addressing and 16-bit data.  CS has some value
 *	and IP is zero.  Thus, data addresses need to be absolute
 *	(no relocation) and are taken with regard to r_base.
 *
 *	With the addition of trampoline_level4_pgt this code can
 *	now enter a 64bit kernel that lives at arbitrary 64bit
 *	physical addresses.
 *
 *	If you work on this file, check the object module with objdump
 *	--full-contents --reloc to make sure there are no relocation
 *	entries.
 */

#include <linux/linkage.h>
#include <asm/pgtable_types.h>
#include <asm/page_types.h>
#include <asm/msr.h>
#include <asm/segment.h>
#include <asm/processor-flags.h>
#include "realmode.h"

	.text
	.code16

	.balign	PAGE_SIZE
ENTRY(trampoline_start)
	cli			# We should be safe anyway
	wbinvd

	LJMPW_RM(1f)
1:
	mov	%cs, %ax	# Code and data in the same place
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %ss

	movl	$0xA5A5A5A5, trampoline_status
	# write marker for master knows we're running

	# Setup stack
	movl	$rm_stack_end, %esp

	call	verify_cpu		# Verify the cpu supports long mode
	testl   %eax, %eax		# Check for return code
	jnz	no_longmode

	/*
	 * GDT tables in non default location kernel can be beyond 16MB and
	 * lgdt will not be able to load the address as in real mode default
	 * operand size is 16bit. Use lgdtl instead to force operand size
	 * to 32 bit.
	 */

	lidtl	tr_idt	# load idt with 0, 0
	lgdtl	tr_gdt	# load gdt with whatever is appropriate

	movw	$__KERNEL_DS, %dx	# Data segment descriptor

	# Enable protected mode
	movl	$X86_CR0_PE, %eax	# protected mode (PE) bit
	movl	%eax, %cr0		# into protected mode

	# flush prefetch and jump to startup_32
	ljmpl	$__KERNEL32_CS, $pa_startup_32

no_longmode:
	hlt
	jmp no_longmode
#include "../kernel/verify_cpu.S"

	.section ".text32","ax"
	.code32
	.balign 4
ENTRY(startup_32)
	movl	%edx, %ss
	addl	$pa_real_mode_base, %esp
	movl	%edx, %ds
	movl	%edx, %es
	movl	%edx, %fs
	movl	%edx, %gs

	movl	pa_tr_cr4, %eax
	movl	%eax, %cr4		# Enable PAE mode

	# Setup trampoline 4 level pagetables
	movl	$pa_trampoline_pgd, %eax
	movl	%eax, %cr3

	# Set up EFER
	movl	pa_tr_efer, %eax
	movl	pa_tr_efer + 4, %edx
	movl	$MSR_EFER, %ecx
	wrmsr

	# Enable paging and in turn activate Long Mode
	movl	$(X86_CR0_PG | X86_CR0_WP | X86_CR0_PE), %eax
	movl	%eax, %cr0

	/*
	 * At this point we're in long mode but in 32bit compatibility mode
	 * with EFER.LME = 1, CS.L = 0, CS.D = 1 (and in turn
	 * EFER.LMA = 1). Now we want to jump in 64bit mode, to do that we use
	 * the new gdt/idt that has __KERNEL_CS with CS.L = 1.
	 */
	ljmpl	$__KERNEL_CS, $pa_startup_64

	.section ".text64","ax"
	.code64
	.balign 4
ENTRY(startup_64)
	# Now jump into the kernel using virtual addresses
	jmpq	*tr_start(%rip)

	.section ".rodata","a"
	# Duplicate the global descriptor table
	# so the kernel can live anywhere
	.balign	16
	.globl tr_gdt
tr_gdt:
	.short	tr_gdt_end - tr_gdt - 1	# gdt limit
	.long	pa_tr_gdt
	.short	0
	.quad	0x00cf9b000000ffff	# __KERNEL32_CS
	.quad	0x00af9b000000ffff	# __KERNEL_CS
	.quad	0x00cf93000000ffff	# __KERNEL_DS
tr_gdt_end:

	.bss
	.balign	PAGE_SIZE
GLOBAL(trampoline_pgd)		.space	PAGE_SIZE

	.balign	8
GLOBAL(trampoline_header)
	tr_start:		.space	8
	GLOBAL(tr_efer)		.space	8
	GLOBAL(tr_cr4)		.space	4
END(trampoline_header)

#include "trampoline_common.S"
