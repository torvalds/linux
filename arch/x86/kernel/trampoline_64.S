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

#ifdef CONFIG_ACPI_SLEEP
.section .rodata, "a", @progbits
#else
/* We can free up the trampoline after bootup if cpu hotplug is not supported. */
__CPUINITRODATA
#endif
.code16

ENTRY(trampoline_data)
r_base = .
	cli			# We should be safe anyway
	wbinvd
	mov	%cs, %ax	# Code and data in the same place
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %ss


	movl	$0xA5A5A5A5, trampoline_data - r_base
				# write marker for master knows we're running

					# Setup stack
	movw	$(trampoline_stack_end - r_base), %sp

	call	verify_cpu		# Verify the cpu supports long mode
	testl   %eax, %eax		# Check for return code
	jnz	no_longmode

	mov	%cs, %ax
	movzx	%ax, %esi		# Find the 32bit trampoline location
	shll	$4, %esi

					# Fixup the vectors
	addl	%esi, startup_32_vector - r_base
	addl	%esi, startup_64_vector - r_base
	addl	%esi, tgdt + 2 - r_base	# Fixup the gdt pointer

	/*
	 * GDT tables in non default location kernel can be beyond 16MB and
	 * lgdt will not be able to load the address as in real mode default
	 * operand size is 16bit. Use lgdtl instead to force operand size
	 * to 32 bit.
	 */

	lidtl	tidt - r_base	# load idt with 0, 0
	lgdtl	tgdt - r_base	# load gdt with whatever is appropriate

	mov	$X86_CR0_PE, %ax	# protected mode (PE) bit
	lmsw	%ax			# into protected mode

	# flush prefetch and jump to startup_32
	ljmpl	*(startup_32_vector - r_base)

	.code32
	.balign 4
startup_32:
	movl	$__KERNEL_DS, %eax	# Initialize the %ds segment register
	movl	%eax, %ds

	movl	$X86_CR4_PAE, %eax
	movl	%eax, %cr4		# Enable PAE mode

					# Setup trampoline 4 level pagetables
	leal	(trampoline_level4_pgt - r_base)(%esi), %eax
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
	ljmp	*(startup_64_vector - r_base)(%esi)

	.code64
	.balign 4
startup_64:
	# Now jump into the kernel using virtual addresses
	movq	$secondary_startup_64, %rax
	jmp	*%rax

	.code16
no_longmode:
	hlt
	jmp no_longmode
#include "verify_cpu.S"

	# Careful these need to be in the same 64K segment as the above;
tidt:
	.word	0			# idt limit = 0
	.word	0, 0			# idt base = 0L

	# Duplicate the global descriptor table
	# so the kernel can live anywhere
	.balign 4
tgdt:
	.short	tgdt_end - tgdt		# gdt limit
	.long	tgdt - r_base
	.short 0
	.quad	0x00cf9b000000ffff	# __KERNEL32_CS
	.quad	0x00af9b000000ffff	# __KERNEL_CS
	.quad	0x00cf93000000ffff	# __KERNEL_DS
tgdt_end:

	.balign 4
startup_32_vector:
	.long	startup_32 - r_base
	.word	__KERNEL32_CS, 0

	.balign 4
startup_64_vector:
	.long	startup_64 - r_base
	.word	__KERNEL_CS, 0

trampoline_stack:
	.org 0x1000
trampoline_stack_end:
ENTRY(trampoline_level4_pgt)
	.quad	level3_ident_pgt - __START_KERNEL_map + _KERNPG_TABLE
	.fill	510,8,0
	.quad	level3_kernel_pgt - __START_KERNEL_map + _KERNPG_TABLE

ENTRY(trampoline_end)
