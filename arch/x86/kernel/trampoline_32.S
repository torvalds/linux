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
 *	and IP is zero.  Thus, data addresses need to be absolute
 *	(no relocation) and are taken with regard to r_base.
 *
 *	If you work on this file, check the object module with
 *	objdump --reloc to make sure there are no relocation
 *	entries except for:
 *
 *	TYPE              VALUE
 *	R_386_32          startup_32_smp
 *	R_386_32          boot_gdt
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/segment.h>
#include <asm/page_types.h>

/* We can free up trampoline after bootup if cpu hotplug is not supported. */
__CPUINITRODATA
.code16

ENTRY(trampoline_data)
r_base = .
	wbinvd			# Needed for NUMA-Q should be harmless for others
	mov	%cs, %ax	# Code and data in the same place
	mov	%ax, %ds

	cli			# We should be safe anyway

	movl	$0xA5A5A5A5, trampoline_data - r_base
				# write marker for master knows we're running

	/* GDT tables in non default location kernel can be beyond 16MB and
	 * lgdt will not be able to load the address as in real mode default
	 * operand size is 16bit. Use lgdtl instead to force operand size
	 * to 32 bit.
	 */

	lidtl	boot_idt_descr - r_base	# load idt with 0, 0
	lgdtl	boot_gdt_descr - r_base	# load gdt with whatever is appropriate

	xor	%ax, %ax
	inc	%ax		# protected mode (PE) bit
	lmsw	%ax		# into protected mode
	# flush prefetch and jump to startup_32_smp in arch/i386/kernel/head.S
	ljmpl	$__BOOT_CS, $(startup_32_smp-__PAGE_OFFSET)

	# These need to be in the same 64K segment as the above;
	# hence we don't use the boot_gdt_descr defined in head.S
boot_gdt_descr:
	.word	__BOOT_DS + 7			# gdt limit
	.long	boot_gdt - __PAGE_OFFSET	# gdt base

boot_idt_descr:
	.word	0				# idt limit = 0
	.long	0				# idt base = 0L

.globl trampoline_end
trampoline_end:
