/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: ppopov@mvista.com
 *
 * Interrupt dispatcher for Au1000 boards.
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.text
	.set	macro
	.set	noat
	.align	5

NESTED(au1000_IRQ, PT_SIZE, sp)
	SAVE_ALL
	CLI				# Important: mark KERNEL mode !

	mfc0	t0,CP0_CAUSE		# get pending interrupts
	mfc0	t1,CP0_STATUS		# get enabled interrupts
	and	t0,t1			# isolate allowed ones

	andi	t0,0xff00		# isolate pending bits
	beqz	t0, 3f			# spurious interrupt

	andi	a0, t0, CAUSEF_IP7
	beq	a0, zero, 1f
	move	a0, sp
	jal	mips_timer_interrupt
	j	ret_from_irq

1:
	andi	a0, t0, CAUSEF_IP2	# Interrupt Controller 0, Request 0
	beq	a0, zero, 2f
	move	a0,sp
	jal	intc0_req0_irqdispatch
	j	ret_from_irq
2:
	andi	a0, t0, CAUSEF_IP3	# Interrupt Controller 0, Request 1
	beq	a0, zero, 3f
	move	a0,sp
	jal	intc0_req1_irqdispatch
	j	ret_from_irq
3:
	andi	a0, t0, CAUSEF_IP4	# Interrupt Controller 1, Request 0
	beq	a0, zero, 4f
	move	a0,sp
	jal	intc1_req0_irqdispatch
	j	ret_from_irq
4:
	andi	a0, t0, CAUSEF_IP5	# Interrupt Controller 1, Request 1
	beq	a0, zero, 5f
	move	a0, sp
	jal	intc1_req1_irqdispatch
	j	ret_from_irq

5:
	move	a0, sp
	jal	spurious_interrupt
	j	ret_from_irq
END(au1000_IRQ)
