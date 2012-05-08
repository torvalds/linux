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
 *	On entry to trampoline_data, the processor is in real mode
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
#include <linux/init.h>
#include <asm/pgtable_types.h>
#include <asm/page_types.h>
#include <asm/msr.h>
#include <asm/segment.h>
#include <asm/processor-flags.h>

	.text
	.balign PAGE_SIZE
	.code16

ENTRY(trampoline_data)
	cli			# We should be safe anyway
	wbinvd

	.byte	0xea		# ljmpw
	.word	1f		# Offset
	.word	real_mode_seg	# Segment
1:
	mov	%cs, %ax	# Code and data in the same place
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %ss

	movl	$0xA5A5A5A5, trampoline_status
	# write marker for master knows we're running

	# Setup stack
	movw	$trampoline_stack_end, %sp

	call	verify_cpu		# Verify the cpu supports long mode
	testl   %eax, %eax		# Check for return code
	jnz	no_longmode

	/*
	 * GDT tables in non default location kernel can be beyond 16MB and
	 * lgdt will not be able to load the address as in real mode default
	 * operand size is 16bit. Use lgdtl instead to force operand size
	 * to 32 bit.
	 */

	lidtl	tidt	# load idt with 0, 0
	lgdtl	tgdt	# load gdt with whatever is appropriate

	mov	$X86_CR0_PE, %ax	# protected mode (PE) bit
	lmsw	%ax			# into protected mode

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
	movl	$__KERNEL_DS, %eax	# Initialize the %ds segment register
	movl	%eax, %ds

	movl	$X86_CR4_PAE, %eax
	movl	%eax, %cr4		# Enable PAE mode

	movl	pa_startup_64_smp, %esi
	movl	pa_startup_64_smp_high, %edi

					# Setup trampoline 4 level pagetables
	leal	pa_trampoline_level4_pgt, %eax
	movl	%eax, %cr3

	movl	$MSR_EFER, %ecx
	movl	$(1 << _EFER_LME), %eax	# Enable Long Mode
	xorl	%edx, %edx
	wrmsr

	# Enable paging and in turn activate Long Mode
	# Enable protected mode
	movl	$(X86_CR0_PG | X86_CR0_PE), %eax
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
	movl	%edi, %eax
	shlq	$32, %rax
	addl	%esi, %eax
	jmp	*%rax

	.section ".rodata","a"
	.balign	16
tidt:
	.word	0			# idt limit = 0
	.word	0, 0			# idt base = 0L

	# Duplicate the global descriptor table
	# so the kernel can live anywhere
	.balign 4
	.globl tgdt
tgdt:
	.short	tgdt_end - tgdt		# gdt limit
	.long	pa_tgdt
	.short	0
	.quad	0x00cf9b000000ffff	# __KERNEL32_CS
	.quad	0x00af9b000000ffff	# __KERNEL_CS
	.quad	0x00cf93000000ffff	# __KERNEL_DS
tgdt_end:

	.data
	.balign 4
GLOBAL(trampoline_status)
	.long	0

trampoline_stack:
	.org 0x1000
trampoline_stack_end:

	.globl	level3_ident_pgt
	.globl	level3_kernel_pgt
GLOBAL(trampoline_level4_pgt)
	level3_ident_pgt:	.quad	0
	.fill 510,8,0
	level3_kernel_pgt:	.quad	0

	.globl	startup_64_smp
	.globl	startup_64_smp_high
startup_64_smp:		.long 0
startup_64_smp_high:	.long 0
