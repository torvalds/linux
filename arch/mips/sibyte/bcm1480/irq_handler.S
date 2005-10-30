/*
 * Copyright (C) 2000,2001,2002,2003,2004 Broadcom Corporation
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
 * bcm1480_irq_handler() is the routine that is actually called when an
 * interrupt occurs.  It is installed as the exception vector handler in
 * init_IRQ() in arch/mips/sibyte/bcm1480/irq.c
 *
 * In the handle we figure out which interrupts need handling, and use that
 * to call the dispatcher, which will take care of actually calling
 * registered handlers
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
#include <asm/sibyte/bcm1480_regs.h>
#include <asm/sibyte/bcm1480_int.h>

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
	#.set	mips4
	.align	5
	NESTED(bcm1480_irq_handler, PT_SIZE, sp)
	SAVE_ALL
	CLI

#ifdef CONFIG_SIBYTE_BCM1480_PROF
	/* Set compare to count to silence count/compare timer interrupts */
	mfc0	t1, CP0_COUNT
	mtc0	t1, CP0_COMPARE /* pause to clear IP[7] bit of cause ? */
#endif
	/* Read cause */
	mfc0	s0, CP0_CAUSE

#ifdef CONFIG_SIBYTE_BCM1480_PROF
	/* Cpu performance counter interrupt is routed to IP[7] */
	andi	t1, s0, CAUSEF_IP7
	beqz	t1, 0f
	 srl	t1, s0, (CAUSEB_BD-2)	/* Shift BD bit to bit 2 */
	and	t1, t1, 0x4		/* mask to get just BD bit */
#ifdef CONFIG_MIPS64
	dmfc0	a0, CP0_EPC
	daddu	a0, a0, t1		/* a0 = EPC + (BD ? 4 :	0) */
#else
	mfc0	a0, CP0_EPC
	addu	a0, a0, t1		/* a0 = EPC + (BD ? 4 :	0) */
#endif
	jal	sbprof_cpu_intr
	 nop
	j	ret_from_irq
	 nop
0:
#endif

	/* Timer interrupt is routed to IP[4] */
	andi	t1, s0, CAUSEF_IP4
	beqz	t1, 1f
	 nop
	jal	bcm1480_timer_interrupt
	 move	a0, sp			/* Pass the registers along */
	j	ret_from_irq
	 nop				/* delay slot  */
1:

#ifdef CONFIG_SMP
	/* Mailbox interrupt is routed to IP[3] */
	andi	 t1, s0, CAUSEF_IP3
	beqz	 t1, 2f
	 nop
	jal	 bcm1480_mailbox_interrupt
	 move	 a0, sp
	j	 ret_from_irq
	 nop				/* delay slot  */
2:
#endif

#ifdef CONFIG_KGDB
	/* KGDB (uart 1) interrupt is routed to IP[6] */
	andi	 t1, s0, CAUSEF_IP6
	beqz	 t1, 3f
	 nop				/* delay slot  */
	jal	 bcm1480_kgdb_interrupt
	 move	 a0, sp
	j	 ret_from_irq
	 nop				/* delay slot  */
3:
#endif

	and	 t1, s0, CAUSEF_IP2
	beqz	 t1, 9f
	 nop

	/*
	 * Default...we've hit an IP[2] interrupt, which means we've got
	 * to check the 1480 interrupt registers to figure out what to do
	 * Need to detect which CPU we're on, now that smp_affinity is
	 * supported.
	 */
	PTR_LA	 v0, CKSEG1 + A_BCM1480_IMR_CPU0_BASE
#ifdef CONFIG_SMP
	lw	 t1, TI_CPU($28)
	sll	 t1, t1, BCM1480_IMR_REGISTER_SPACING_SHIFT
	addu	 v0, v0, t1
#endif

	/* Read IP[2] status (get both high and low halves of status) */
	ld	 s0, R_BCM1480_IMR_INTERRUPT_STATUS_BASE_H(v0)
	ld	 s1, R_BCM1480_IMR_INTERRUPT_STATUS_BASE_L(v0)

	move	 s2, zero	/* intr number  */
	li	 s3, 64

	beqz	 s0, 9f		/* No interrupts.  Return.  */
	 move	 a1, sp

	xori	 s4, s0, 1	/* if s0 (_H) == 1, it's a low intr, so...  */
	movz	 s2, s3, s4	/* start the intr number at 64, and  */
	movz	 s0, s1, s4	/* look at the low status value.  */

	dclz	 s1, s0		/* Find the next interrupt.  */
	dsubu	 a0, zero, s1
	daddiu	 a0, a0, 63
	jal	 do_IRQ
	 daddu	 a0, a0, s2

9:	j	 ret_from_irq
	 nop

	.set pop
	END(bcm1480_irq_handler)
