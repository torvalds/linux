/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * First-level interrupt dispatcher for ddb5476
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

#include <asm/ddb5xxx/ddb5476.h>

/*
 * first level interrupt dispatcher for ocelot board -
 * We check for the timer first, then check PCI ints A and D.
 * Then check for serial IRQ and fall through.
 */
	.align	5
	NESTED(ddb5476_handle_int, PT_SIZE, sp)
	SAVE_ALL
	CLI
	.set	at
	.set	noreorder
	mfc0	t0, CP0_CAUSE
	mfc0	t2, CP0_STATUS

	and	t0, t2

        andi    t1, t0, STATUSF_IP7     /* cpu timer */
        bnez    t1, ll_cpu_ip7
        andi    t1, t0, STATUSF_IP2	/* vrc5476 & i8259 */
        bnez    t1, ll_cpu_ip2
        andi    t1, t0, STATUSF_IP3
        bnez    t1, ll_cpu_ip3
        andi    t1, t0, STATUSF_IP4
        bnez    t1, ll_cpu_ip4
        andi    t1, t0, STATUSF_IP5
        bnez    t1, ll_cpu_ip5
        andi    t1, t0, STATUSF_IP6
        bnez    t1, ll_cpu_ip6
        andi    t1, t0, STATUSF_IP0     /* software int 0 */
        bnez    t1, ll_cpu_ip0
        andi    t1, t0, STATUSF_IP1     /* software int 1 */
        bnez    t1, ll_cpu_ip1
        nop

	.set	reorder

	/* wrong alarm or masked ... */
	// jal	spurious_interrupt
	// j	ret_from_irq
	move 	a0, sp
	jal	vrc5476_irq_dispatch
	j	ret_from_irq
	nop

	.align	5

ll_cpu_ip0:
	li	a0, CPU_IRQ_BASE + 0
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq

ll_cpu_ip1:
	li	a0, CPU_IRQ_BASE + 1
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq

ll_cpu_ip2:		/* jump to second-level dispatching */
	move	a0, sp
	jal	vrc5476_irq_dispatch
	j	ret_from_irq

ll_cpu_ip3:
	li	a0, CPU_IRQ_BASE + 3
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq

ll_cpu_ip4:
	li	a0, CPU_IRQ_BASE + 4
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq

ll_cpu_ip5:
	li	a0, CPU_IRQ_BASE + 5
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq

ll_cpu_ip6:
	li	a0, CPU_IRQ_BASE + 6
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq

ll_cpu_ip7:
	li	a0, CPU_IRQ_BASE + 7
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq

	END(ddb5476_handle_int)
