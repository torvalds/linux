/*
 * SMP support for APMU based systems with Cortex A7/A15
 *
 * Copyright (C) 2014  Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

ENTRY(shmobile_init_cntvoff)
	/*
	 * CNTVOFF has to be initialized either from non-secure Hypervisor
	 * mode or secure Monitor mode with SCR.NS==1. If TrustZone is enabled
	 * then it should be handled by the secure code
	 */
	cps	#MON_MODE
	mrc	p15, 0, r1, c1, c1, 0		/* Get Secure Config */
	orr	r0, r1, #1
	mcr	p15, 0, r0, c1, c1, 0		/* Set Non Secure bit */
	instr_sync
	mov	r0, #0
	mcrr	p15, 4, r0, r0, c14		/* CNTVOFF = 0 */
	instr_sync
	mcr	p15, 0, r1, c1, c1, 0		/* Set Secure bit */
	instr_sync
	cps	#SVC_MODE
	ret	lr
ENDPROC(shmobile_init_cntvoff)

#ifdef CONFIG_SMP
ENTRY(shmobile_boot_apmu)
	bl	shmobile_init_cntvoff
	b	secondary_startup
ENDPROC(shmobile_boot_apmu)
#endif
