/*
 *
 *	Trampoline.S	Derived from Setup.S by Linus Torvalds
 *
 *	4 Jan 1997 Michael Chastain: changed to gnu as.
 *
 *	Entry: CS:IP point to the start of our code, we are 
 *	in real mode with no stack, but the rest of the 
 *	trampoline page to make our stack and everything else
 *	is a mystery.
 *
 *	In fact we don't actually need a stack so we don't
 *	set one up.
 *
 *	On entry to trampoline_data, the processor is in real mode
 *	with 16-bit addressing and 16-bit data.  CS has some value
 *	and IP is zero.  Thus, data addresses need to be absolute
 *	(no relocation) and are taken with regard to r_base.
 *
 *	If you work on this file, check the object module with objdump
 *	--full-contents --reloc to make sure there are no relocation
 *	entries. For the GDT entry we do hand relocation in smpboot.c
 *	because of 64bit linker limitations.
 */

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>

.data

.code16

ENTRY(trampoline_data)
r_base = .
	wbinvd	
	mov	%cs, %ax	# Code and data in the same place
	mov	%ax, %ds

	cli			# We should be safe anyway

	movl	$0xA5A5A5A5, trampoline_data - r_base
				# write marker for master knows we're running

	/*
	 * GDT tables in non default location kernel can be beyond 16MB and
	 * lgdt will not be able to load the address as in real mode default
	 * operand size is 16bit. Use lgdtl instead to force operand size
	 * to 32 bit.
	 */

	lidtl	idt_48 - r_base	# load idt with 0, 0
	lgdtl	gdt_48 - r_base	# load gdt with whatever is appropriate

	xor	%ax, %ax
	inc	%ax		# protected mode (PE) bit
	lmsw	%ax		# into protected mode
	# flaush prefetch and jump to startup_32 in arch/x86_64/kernel/head.S
	ljmpl	$__KERNEL32_CS, $(startup_32-__START_KERNEL_map)

	# Careful these need to be in the same 64K segment as the above;
idt_48:
	.word	0			# idt limit = 0
	.word	0, 0			# idt base = 0L

gdt_48:
	.short	__KERNEL32_CS + 7	# gdt limit
	.long	cpu_gdt_table-__START_KERNEL_map

.globl trampoline_end
trampoline_end:	
