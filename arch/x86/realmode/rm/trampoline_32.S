/*
 *
 *	Trampoline.S	Derived from Setup.S by Linus Torvalds
 *
 *	4 Jan 1997 Michael Chastain: changed to gnu as.
 *
 *	This is only used for booting secondary CPUs in SMP machine
 *
 *	Entry: CS:IP point to the start of our code, we are
 *	in real mode with no stack, but the rest of the
 *	trampoline page to make our stack and everything else
 *	is a mystery.
 *
 *	We jump into arch/x86/kernel/head_32.S.
 *
 *	On entry to trampoline_data, the processor is in real mode
 *	with 16-bit addressing and 16-bit data.  CS has some value
 *	and IP is zero.  Thus, we load CS to the physical segment
 *	of the real mode code before doing anything further.
 *
 *	The structure real_mode_header includes entries that need
 *	to be set up before executing this code:
 *
 *	startup_32_smp
 *	boot_gdt
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/segment.h>
#include <asm/page_types.h>

	.text
	.code16
	.globl trampoline_data

	.balign PAGE_SIZE
trampoline_data:
	wbinvd			# Needed for NUMA-Q should be harmless for others

	.byte	0xea		# ljmpw
	.word	1f		# Offset
	.word	real_mode_seg	# Segment
1:
	mov	%cs, %ax	# Code and data in the same place
	mov	%ax, %ds

	cli			# We should be safe anyway

	movl	$0xA5A5A5A5, trampoline_status
				# write marker for master knows we're running

	/* GDT tables in non default location kernel can be beyond 16MB and
	 * lgdt will not be able to load the address as in real mode default
	 * operand size is 16bit. Use lgdtl instead to force operand size
	 * to 32 bit.
	 */

	lidtl	boot_idt_descr		# load idt with 0, 0
	lgdtl	boot_gdt_descr		# load gdt with whatever is appropriate

	xor	%ax, %ax
	inc	%ax			# protected mode (PE) bit
	lmsw	%ax			# into protected mode

	# flush prefetch and jump to startup_32_smp in arch/i386/kernel/head.S
	ljmpl	*(startup_32_smp)

	.data
	.globl startup_32_smp, boot_gdt, trampoline_status

boot_gdt_descr:
	.word	__BOOT_DS + 7			# gdt limit
boot_gdt:
	.long	0				# gdt base

boot_idt_descr:
	.word	0				# idt limit = 0
	.long	0				# idt base = 0L

trampoline_status:
	.long	0

startup_32_smp:
	.long	0x00000000
	.word	__BOOT_CS, 0
