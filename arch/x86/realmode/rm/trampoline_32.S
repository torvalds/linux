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
 *	On entry to trampoline_start, the processor is in real mode
 *	with 16-bit addressing and 16-bit data.  CS has some value
 *	and IP is zero.  Thus, we load CS to the physical segment
 *	of the real mode code before doing anything further.
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/segment.h>
#include <asm/page_types.h>
#include "realmode.h"

	.text
	.code16

	.balign	PAGE_SIZE
ENTRY(trampoline_start)
	wbinvd			# Needed for NUMA-Q should be harmless for others

	LJMPW_RM(1f)
1:
	mov	%cs, %ax	# Code and data in the same place
	mov	%ax, %ds

	cli			# We should be safe anyway

	movl	tr_start, %eax	# where we need to go

	movl	$0xA5A5A5A5, trampoline_status
				# write marker for master knows we're running

	/*
	 * GDT tables in non default location kernel can be beyond 16MB and
	 * lgdt will not be able to load the address as in real mode default
	 * operand size is 16bit. Use lgdtl instead to force operand size
	 * to 32 bit.
	 */
	lidtl	tr_idt			# load idt with 0, 0
	lgdtl	tr_gdt			# load gdt with whatever is appropriate

	movw	$1, %dx			# protected mode (PE) bit
	lmsw	%dx			# into protected mode

	ljmpl	$__BOOT_CS, $pa_startup_32

	.section ".text32","ax"
	.code32
ENTRY(startup_32)			# note: also used from wakeup_asm.S
	jmp	*%eax

#include "trampoline_common.S"
