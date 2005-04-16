/*
 * Copyright 2002 Momentum Computer Inc.
 * Author: Matthew Dharm <mdharm@momenco.com>
 *
 * Based on work:
 *   Copyright 2001 MontaVista Software Inc.
 *   Author: jsun@mvista.com or jsun@junsun.net
 *
 * First-level interrupt dispatcher for Jaguar-ATX board.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

/*
 * First level interrupt dispatcher for Ocelot-CS board
 */
		.align	5
		NESTED(jaguar_handle_int, PT_SIZE, sp)
		SAVE_ALL
		CLI
		.set	at
		mfc0	t0, CP0_CAUSE  
		mfc0	t2, CP0_STATUS

		and	t0, t2
        
		andi	t1, t0, STATUSF_IP0	/* sw0 software interrupt */
		bnez	t1, ll_sw0_irq
		andi	t1, t0, STATUSF_IP1	/* sw1 software interrupt */
		bnez	t1, ll_sw1_irq
		andi	t1, t0, STATUSF_IP2	/* int0 hardware line */
		bnez	t1, ll_pcixa_irq
		andi	t1, t0, STATUSF_IP3	/* int1 hardware line */
		bnez	t1, ll_pcixb_irq
		andi	t1, t0, STATUSF_IP4	/* int2 hardware line */
		bnez	t1, ll_pcia_irq
		andi	t1, t0, STATUSF_IP5	/* int3 hardware line */
		bnez	t1, ll_pcib_irq
		andi	t1, t0, STATUSF_IP6	/* int4 hardware line */
		bnez	t1, ll_uart_irq
		andi	t1, t0, STATUSF_IP7	/* cpu timer */
		bnez	t1, ll_cputimer_irq

		nop
		nop

		/* now look at extended interrupts */
		mfc0	t0, CP0_CAUSE
		cfc0	t1, CP0_S1_INTCONTROL

		/* shift the mask 8 bits left to line up the bits */
		sll	t2, t1, 8

		and	t0, t2
		srl	t0, t0, 16

		andi	t1, t0, STATUSF_IP8	/* int6 hardware line */
		bnez	t1, ll_mv64340_decode_irq

		nop
		nop

		.set	reorder

		/* wrong alarm or masked ... */
		j	spurious_interrupt
		nop
		END(jaguar_handle_int)

		.align	5
ll_sw0_irq:
		li	a0, 0
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq
ll_sw1_irq:
		li	a0, 1
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq
ll_pcixa_irq:
		li	a0, 2
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_pcixb_irq:
		li	a0, 3
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_pcia_irq:
		li	a0, 4
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq
	
ll_pcib_irq:
		li	a0, 5
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq
	
ll_uart_irq:
		li	a0, 6
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq
	
ll_cputimer_irq:
		li	a0, 7
		move	a1, sp
		jal	ll_timer_interrupt
		j	ret_from_irq
	
ll_mv64340_decode_irq:
		move	a0, sp
		jal	ll_mv64340_irq
		j	ret_from_irq
