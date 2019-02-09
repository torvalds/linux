/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2007 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2412 Power Manager low-level sleep support
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <mach/hardware.h>
#include <mach/map.h>

#include <mach/regs-irq.h>

	.text

	.global	s3c2412_sleep_enter

s3c2412_sleep_enter:
	mov	r0, #0			/* argument for coprocessors */
	ldr	r1, =S3C2410_INTPND
	ldr	r2, =S3C2410_SRCPND
	ldr	r3, =S3C2410_EINTPEND

	teq	r0, r0
	bl	s3c2412_sleep_enter1
	teq	pc, r0
	bl	s3c2412_sleep_enter1

	.align	5

	/* this is called twice, first with the Z flag to ensure that the
	 * instructions have been loaded into the cache, and the second
	 * time to try and suspend the system.
	*/
s3c2412_sleep_enter1:
	mcr	p15, 0, r0, c7, c10, 4
	mcrne	p15, 0, r0, c7, c0, 4

	/* if we return from here, it is because an interrupt was
	 * active when we tried to shutdown. Try and ack the IRQ and
	 * retry, as simply returning causes the system to lock.
	*/

	ldrne	r9, [r1]
	strne	r9, [r1]
	ldrne	r9, [r2]
	strne	r9, [r2]
	ldrne	r9, [r3]
	strne	r9, [r3]
	bne	s3c2412_sleep_enter1

	ret	lr
