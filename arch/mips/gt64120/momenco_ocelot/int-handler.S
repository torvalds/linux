/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * First-level interrupt dispatcher for ocelot board.
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
 * first level interrupt dispatcher for ocelot board -
 * We check for the timer first, then check PCI ints A and D.
 * Then check for serial IRQ and fall through.
 */
		.align	5
		NESTED(ocelot_handle_int, PT_SIZE, sp)
		SAVE_ALL
		CLI
		.set	at
		mfc0	t0, CP0_CAUSE
		mfc0	t2, CP0_STATUS

		and	t0, t2

		 andi	t1, t0, STATUSF_IP2	/* int0 hardware line */
		bnez	t1, ll_pri_enet_irq
		 andi	t1, t0, STATUSF_IP3	/* int1 hardware line */
		bnez	t1, ll_sec_enet_irq
		 andi	t1, t0, STATUSF_IP4	/* int2 hardware line */
		bnez	t1, ll_uart1_irq
		 andi	t1, t0, STATUSF_IP5	/* int3 hardware line */
		bnez	t1, ll_cpci_irq
		 andi	t1, t0, STATUSF_IP6	/* int4 hardware line */
		bnez	t1, ll_galileo_irq
		 andi	t1, t0, STATUSF_IP7	/* cpu timer */
		bnez	t1, ll_cputimer_irq

                /* now look at the extended interrupts */
		mfc0	t0, CP0_CAUSE
		cfc0	t1, CP0_S1_INTCONTROL

		/* shift the mask 8 bits left to line up the bits */
		 sll	t2, t1, 8

		 and	t0, t2
		 srl	t0, t0, 16

		 andi	t1, t0, STATUSF_IP8	/* int6 hardware line */
		bnez	t1, ll_pmc1_irq
		 andi	t1, t0, STATUSF_IP9	/* int7 hardware line */
		bnez	t1, ll_pmc2_irq
		 andi	t1, t0, STATUSF_IP10	/* int8 hardware line */
		bnez	t1, ll_cpci_abcd_irq
		 andi	t1, t0, STATUSF_IP11	/* int9 hardware line */
		bnez	t1, ll_uart2_irq

		.set	reorder

		/* wrong alarm or masked ... */
		j	spurious_interrupt
		nop
		END(ocelot_handle_int)

		.align	5
ll_pri_enet_irq:
		li	a0, 2
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_sec_enet_irq:
		li	a0, 3
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_uart1_irq:
		li	a0, 4
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_cpci_irq:
		li	a0, 5
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_galileo_irq:
		li	a0, 6
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_cputimer_irq:
		li	a0, 7
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_pmc1_irq:
		li	a0, 8
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_pmc2_irq:
		li	a0, 9
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_cpci_abcd_irq:
		li	a0, 10
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_uart2_irq:
		li	a0, 11
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq
