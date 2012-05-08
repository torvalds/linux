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
#include "realmode.h"

	.text
	.code16

	.balign	PAGE_SIZE
ENTRY(trampoline_data)
	wbinvd			# Needed for NUMA-Q should be harmless for others

	LJMPW_RM(1f)
1:
	mov	%cs, %ax	# Code and data in the same place
	mov	%ax, %ds

	cli			# We should be safe anyway

	movl	startup_32_smp, %eax	# where we need to go

	movl	$0xA5A5A5A5, trampoline_status
				# write marker for master knows we're running

	/*
	 * GDT tables in non default location kernel can be beyond 16MB and
	 * lgdt will not be able to load the address as in real mode default
	 * operand size is 16bit. Use lgdtl instead to force operand size
	 * to 32 bit.
	 */
	lidtl	boot_idt_descr		# load idt with 0, 0
	lgdtl	boot_gdt_descr		# load gdt with whatever is appropriate

	movw	$1, %dx			# protected mode (PE) bit
	lmsw	%dx			# into protected mode

	ljmpl	$__BOOT_CS, $pa_startup_32

	.section ".text32","ax"
	.code32
ENTRY(startup_32)			# note: also used from wakeup_asm.S
	jmp	*%eax

	.section ".rodata","a"

	.balign	4
boot_idt_descr:
	.word	0				# idt limit = 0
	.long	0				# idt base = 0L

	.data

boot_gdt_descr:
	.word	__BOOT_DS + 7			# gdt limit
GLOBAL(boot_gdt)
	.long	0				# gdt base

	.bss

	.balign	4
GLOBAL(trampoline_status)	.space	4
GLOBAL(startup_32_smp)		.space	4
