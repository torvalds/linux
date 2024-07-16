/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copied from omap44xx-smc.S Copyright (C) 2010 Texas Instruments, Inc.
 * Copyright 2012 Calxeda, Inc.
 */

#include <linux/linkage.h>

/*
 * This is common routine to manage secure monitor API
 * used to modify the PL310 secure registers.
 * 'r0' contains the value to be modified and 'r12' contains
 * the monitor API number.
 * Function signature : void highbank_smc1(u32 fn, u32 arg)
 */
	.arch armv7-a
	.arch_extension sec
ENTRY(highbank_smc1)
	stmfd   sp!, {r4-r11, lr}
	mov	r12, r0
	mov 	r0, r1
	dsb
	smc	#0
	ldmfd   sp!, {r4-r11, pc}
ENDPROC(highbank_smc1)
