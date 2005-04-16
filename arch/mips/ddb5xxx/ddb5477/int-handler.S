/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * First-level interrupt dispatcher for ddb5477
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
#include <asm/ddb5xxx/ddb5477.h>

/*
 * first level interrupt dispatcher for ocelot board -
 * We check for the timer first, then check PCI ints A and D.
 * Then check for serial IRQ and fall through.
 */
	.align	5
	NESTED(ddb5477_handle_int, PT_SIZE, sp)
	SAVE_ALL
	CLI
	.set	at
	.set	noreorder
	mfc0	t0, CP0_CAUSE
	mfc0	t2, CP0_STATUS

	and	t0, t2

	andi	t1, t0, STATUSF_IP7	/* cpu timer */
	bnez	t1, ll_cputimer_irq
	andi	t1, t0, (STATUSF_IP2 | STATUSF_IP3 | STATUSF_IP4 | STATUSF_IP5 | STATUSF_IP6 )
	bnez	t1, ll_vrc5477_irq
	andi	t1, t0, STATUSF_IP0	/* software int 0 */
	bnez	t1, ll_cpu_ip0
	andi	t1, t0, STATUSF_IP1	/* software int 1 */
	bnez	t1, ll_cpu_ip1
	nop
	.set	reorder

	/* wrong alarm or masked ... */
	j	spurious_interrupt
	nop
	END(ddb5477_handle_int)

	.align	5

ll_vrc5477_irq:
	move	a0, sp
	jal	vrc5477_irq_dispatch
	j	ret_from_irq

ll_cputimer_irq:
	li	a0, CPU_IRQ_BASE + 7
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq


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
