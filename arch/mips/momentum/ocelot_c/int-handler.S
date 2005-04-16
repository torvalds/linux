/*
 * Copyright 2002 Momentum Computer Inc.
 * Author: Matthew Dharm <mdharm@momenco.com>
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * First-level interrupt dispatcher for Ocelot-CS board.
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
#include "ocelot_c_fpga.h"

/*
 * First level interrupt dispatcher for Ocelot-CS board
 */
		.align	5
		NESTED(ocelot_handle_int, PT_SIZE, sp)
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
		bnez	t1, ll_scsi_irq
		andi	t1, t0, STATUSF_IP3	/* int1 hardware line */
		bnez	t1, ll_uart_decode_irq
		andi	t1, t0, STATUSF_IP4	/* int2 hardware line */
		bnez	t1, ll_pmc_irq
		andi	t1, t0, STATUSF_IP5	/* int3 hardware line */
		bnez	t1, ll_cpci_decode_irq
		andi	t1, t0, STATUSF_IP6	/* int4 hardware line */
		bnez	t1, ll_mv64340_decode_irq
		andi	t1, t0, STATUSF_IP7	/* cpu timer */
		bnez	t1, ll_cputimer_irq

		.set	reorder

		/* wrong alarm or masked ... */
		j	spurious_interrupt
		nop
		END(ocelot_handle_int)

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
ll_scsi_irq:
		li	a0, 2
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_uart_decode_irq:
		move	a0, sp
		jal	ll_uart_irq
		j	ret_from_irq

ll_pmc_irq:
		li	a0, 4
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq
	
ll_cpci_decode_irq:
		move	a0, sp
		jal	ll_cpci_irq
		j	ret_from_irq

ll_mv64340_decode_irq:
		move	a0, sp
		jal	ll_mv64340_irq
		j	ret_from_irq

ll_cputimer_irq:
		li	a0, 7
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq
	
