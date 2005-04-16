/*
 * Copyright (C) 2000, 2001, 2002, 2003 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
 * sb1250_handle_int() is the routine that is actually called when an interrupt
 * occurs.  It is installed as the exception vector handler in arch_init_irq()
 * in arch/mips/sibyte/sb1250/irq.c
 *
 * In the handle we figure out which interrupts need handling, and use that to
 * call the dispatcher, which will take care of actually calling registered
 * handlers
 *
 * Note that we take care of all raised interrupts in one go at the handler.
 * This is more BSDish than the Indy code, and also, IMHO, more sane.
 */
#include <linux/config.h>

#include <asm/addrspace.h>
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/sibyte/sb1250_defs.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_int.h>

/*
 * What a pain. We have to be really careful saving the upper 32 bits of any
 * register across function calls if we don't want them trashed--since were
 * running in -o32, the calling routing never saves the full 64 bits of a
 * register across a function call.  Being the interrupt handler, we're
 * guaranteed that interrupts are disabled during this code so we don't have
 * to worry about random interrupts blasting the high 32 bits.
 */

	.text
	.set	push
	.set	noreorder
	.set	noat
	.set	mips64
	.align	5
	NESTED(sb1250_irq_handler, PT_SIZE, sp)
	SAVE_ALL
	CLI

#ifdef CONFIG_SIBYTE_SB1250_PROF
	/* Set compare to count to silence count/compare timer interrupts */
	mfc0	t1, CP0_COUNT
	mtc0	t1, CP0_COMPARE /* pause to clear IP[7] bit of cause ? */
#endif
	/* Read cause */
	mfc0	s0, CP0_CAUSE

#ifdef CONFIG_SIBYTE_SB1250_PROF
	/* Cpu performance counter interrupt is routed to IP[7] */
	andi	t1, s0, CAUSEF_IP7
	beqz	t1, 0f
	 srl	t1, s0, (CAUSEB_BD-2)  /* Shift BD bit to bit 2 */
	and	t1, t1, 0x4		/* mask to get just BD bit */
	mfc0	a0, CP0_EPC
	jal	sbprof_cpu_intr
	 addu	a0, a0, t1		/* a0 = EPC + (BD ? 4 :	0) */
	j	ret_from_irq
	 nop
0:
#endif

	/* Timer interrupt is routed to IP[4] */
	andi	t1, s0, CAUSEF_IP4
	beqz	t1, 1f
	 nop
	jal	sb1250_timer_interrupt
	 move	a0, sp			/* Pass the registers along */
	j	ret_from_irq
	 nop				# delay slot
1:

#ifdef CONFIG_SMP
	/* Mailbox interrupt is routed to IP[3] */
	andi	 t1, s0, CAUSEF_IP3
	beqz	 t1, 2f
	 nop
	jal	 sb1250_mailbox_interrupt
	 move    a0, sp
	j	ret_from_irq
	 nop				# delay slot
2:
#endif

#ifdef CONFIG_KGDB
	/* KGDB (uart 1) interrupt is routed to IP[6] */
	andi	t1, s0, CAUSEF_IP6
	beqz	t1, 1f
	nop                            # delay slot
	jal	sb1250_kgdb_interrupt
         move	a0, sp
	j	ret_from_irq
	nop                            # delay slot
1:
#endif

	and      t1, s0, CAUSEF_IP2
	beqz     t1, 4f
	 nop

	/*
	 * Default...we've hit an IP[2] interrupt, which means we've got to
	 * check the 1250 interrupt registers to figure out what to do
	 * Need to detect which CPU we're on, now that smp_affinity is supported.
	 */
	PTR_LA	v0, CKSEG1 + A_IMR_CPU0_BASE
#ifdef CONFIG_SMP
	lw	t1, TI_CPU($28)
	sll	t1, IMR_REGISTER_SPACING_SHIFT
	addu	v0, t1
#endif
	ld	s0, R_IMR_INTERRUPT_STATUS_BASE(v0)	/* read IP[2] status */

	beqz	s0, 4f		/* No interrupts.  Return */
	 move	a1, sp

3:	dclz	s1, s0		/* Find the next interrupt */
	dsubu	a0, zero, s1
	daddiu	a0, a0, 63
	jal	 do_IRQ
	 nop

4:	j        ret_from_irq
	 nop

	.set pop
	END(sb1250_irq_handler)
