/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014 Renesas Electronics Corporation
 *
 * Initialization of CNTVOFF register from secure mode
 *
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

ENTRY(secure_cntvoff_init)
	.arch	armv7-a
	/*
	 * CNTVOFF has to be initialized either from non-secure Hypervisor
	 * mode or secure Monitor mode with SCR.NS==1. If TrustZone is enabled
	 * then it should be handled by the secure code. The CPU must implement
	 * the virtualization extensions.
	 */
	cps	#MON_MODE
	mrc	p15, 0, r1, c1, c1, 0		/* Get Secure Config */
	orr	r0, r1, #1
	mcr	p15, 0, r0, c1, c1, 0		/* Set Non Secure bit */
	isb
	mov	r0, #0
	mcrr	p15, 4, r0, r0, c14		/* CNTVOFF = 0 */
	isb
	mcr	p15, 0, r1, c1, c1, 0		/* Set Secure bit */
	isb
	cps	#SVC_MODE
	ret	lr
ENDPROC(secure_cntvoff_init)
