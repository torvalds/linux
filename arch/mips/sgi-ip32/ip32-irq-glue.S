/*
 * Low level interrupt handler for the SGI O2 aka IP32 aka Moosehead
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Harald Koerfgen
 * Copyright (C) 2001 Keith M Wesolowski
 */
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/addrspace.h>

		.text
		.set    noreorder
		.set    noat
		.align  5
		NESTED(ip32_handle_int, PT_SIZE, ra)
		.set    noat
		SAVE_ALL
		CLI			# TEST: interrupts should be off
		.set    at
		.set    noreorder

		mfc0	s0,CP0_CAUSE

		andi	t1, s0, IE_IRQ0
		bnez	t1, handle_irq0
		 andi	t1, s0, IE_IRQ1
		bnez	t1, handle_irq1
		 andi	t1, s0, IE_IRQ2
		bnez	t1, handle_irq2
		 andi	t1, s0, IE_IRQ3
		bnez	t1, handle_irq3
		 andi	t1, s0, IE_IRQ4
		bnez	t1, handle_irq4
		 andi	t1, s0, IE_IRQ5
		bnez	t1, handle_irq5
		 nop

		/* Either someone has triggered the "software interrupts"
		 * or we lost an interrupt somehow.  Ignore it.
		 */
		j	ret_from_irq
		 nop

handle_irq0:
		jal	ip32_irq0
		 move	a0, sp
		j	ret_from_irq
		 nop

handle_irq1:
		jal	ip32_irq1
		 move	a0, sp
		j	ret_from_irq
		 nop

handle_irq2:
		jal	ip32_irq2
		 move	a0, sp
		j	ret_from_irq
		 nop

handle_irq3:
		jal	ip32_irq3
		 move	a0, sp
		j	ret_from_irq
		 nop

handle_irq4:
		jal	ip32_irq4
		 move	a0, sp
		j	ret_from_irq
		 nop

handle_irq5:
		jal	ip32_irq5
		move	a0, sp
		j	ret_from_irq
		 nop

		END(ip32_handle_int)
