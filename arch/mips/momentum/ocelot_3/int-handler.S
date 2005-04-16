/*
 * Copyright 2002 Momentum Computer Inc.
 * Author: Matthew Dharm <mdharm@momenco.com>
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * Copyright 2004 PMC-Sierra
 * Author: Manish Lachwani (lachwani@pmc-sierra.com)
 *
 * Copyright (C) 2004 MontaVista Software Inc.
 * Author: Manish Lachwani, mlachwani@mvista.com
 *
 * First-level interrupt dispatcher for Ocelot-3 board.
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
 * First level interrupt dispatcher for Ocelot-3 board
 */
		.align	5
		NESTED(ocelot3_handle_int, PT_SIZE, sp)
		SAVE_ALL
		CLI
		.set	at

		mfc0	t0, CP0_CAUSE
		mfc0	t2, CP0_STATUS

		and	t0, t2

		andi	t1, t0, STATUSF_IP0	/* sw0 software interrupt (IRQ0) */
		bnez	t1, ll_sw0_irq

		andi	t1, t0, STATUSF_IP1	/* sw1 software interrupt (IRQ1) */
		bnez	t1, ll_sw1_irq

		andi	t1, t0, STATUSF_IP2	/* int0 hardware line (IRQ2) */
		bnez	t1, ll_pci0slot1_irq

		andi	t1, t0, STATUSF_IP3	/* int1 hardware line (IRQ3) */
		bnez	t1, ll_pci0slot2_irq

		andi	t1, t0, STATUSF_IP4	/* int2 hardware line (IRQ4) */
		bnez	t1, ll_pci1slot1_irq

		andi	t1, t0, STATUSF_IP5	/* int3 hardware line (IRQ5) */
		bnez	t1, ll_pci1slot2_irq

		andi	t1, t0, STATUSF_IP6	/* int4 hardware line (IRQ6) */
		bnez	t1, ll_uart_irq

		andi	t1, t0, STATUSF_IP7	/* cpu timer (IRQ7) */
		bnez	t1, ll_cputimer_irq

                /* now look at extended interrupts */
                mfc0    t0, CP0_CAUSE
                cfc0    t1, CP0_S1_INTCONTROL

                /* shift the mask 8 bits left to line up the bits */
                sll     t2, t1, 8

                and     t0, t2
                srl     t0, t0, 16

                andi    t1, t0, STATUSF_IP8     /* int6 hardware line (IRQ9) */
                bnez    t1, ll_mv64340_decode_irq

		.set	reorder

		/* wrong alarm or masked ... */
		j	spurious_interrupt
		nop
		END(ocelot3_handle_int)

		.align	5
ll_sw0_irq:
		li	a0, 0		/* IRQ 1 */
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq
ll_sw1_irq:
		li	a0, 1		/* IRQ 2 */
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_pci0slot1_irq:
		li	a0, 2		/* IRQ 3 */
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_pci0slot2_irq:
		li	a0, 3		/* IRQ 4 */
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_pci1slot1_irq:
		li	a0, 4		/* IRQ 5 */
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_pci1slot2_irq:
		li	a0, 5		/* IRQ 6 */
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_uart_irq:
		li	a0, 6		/* IRQ 7 */
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_cputimer_irq:
		li	a0, 7		/* IRQ 8 */
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_mv64340_decode_irq:
		move	a0, sp
		jal	ll_mv64340_irq
		j	ret_from_irq

