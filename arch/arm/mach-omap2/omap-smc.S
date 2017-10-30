/*
 * OMAP34xx and OMAP44xx secure APIs file.
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Written by Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * Copyright (C) 2012 Ivaylo Dimitrov <freemangordon@abv.bg>
 * Copyright (C) 2013 Pali Rohár <pali.rohar@gmail.com>
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>

/*
 * This is common routine to manage secure monitor API
 * used to modify the PL310 secure registers.
 * 'r0' contains the value to be modified and 'r12' contains
 * the monitor API number. It uses few CPU registers
 * internally and hence they need be backed up including
 * link register "lr".
 * Function signature : void omap_smc1(u32 fn, u32 arg)
 */

ENTRY(omap_smc1)
	stmfd   sp!, {r2-r12, lr}
	mov	r12, r0
	mov 	r0, r1
	dsb
	smc	#0
	ldmfd   sp!, {r2-r12, pc}
ENDPROC(omap_smc1)

/**
 * u32 omap_smc2(u32 id, u32 falg, u32 pargs)
 * Low level common routine for secure HAL and PPA APIs.
 * @id: Application ID of HAL APIs
 * @flag: Flag to indicate the criticality of operation
 * @pargs: Physical address of parameter list starting
 *	    with number of parametrs
 */
ENTRY(omap_smc2)
	stmfd   sp!, {r4-r12, lr}
	mov	r3, r2
	mov	r2, r1
	mov	r1, #0x0	@ Process ID
	mov	r6, #0xff
	mov	r12, #0x00	@ Secure Service ID
	mov	r7, #0
	mcr	p15, 0, r7, c7, c5, 6
	dsb
	dmb
	smc	#0
	ldmfd   sp!, {r4-r12, pc}
ENDPROC(omap_smc2)

/**
 * u32 omap_smc3(u32 service_id, u32 process_id, u32 flag, u32 pargs)
 * Low level common routine for secure HAL and PPA APIs via smc #1
 * r0 - @service_id: Secure Service ID
 * r1 - @process_id: Process ID
 * r2 - @flag: Flag to indicate the criticality of operation
 * r3 - @pargs: Physical address of parameter list
 */
ENTRY(omap_smc3)
	stmfd	sp!, {r4-r11, lr}
	mov	r12, r0		@ Copy the secure service ID
	mov	r6, #0xff	@ Indicate new Task call
	dsb			@ Memory Barrier (not sure if needed, copied from omap_smc2)
	smc	#1		@ Call PPA service
	ldmfd	sp!, {r4-r11, pc}
ENDPROC(omap_smc3)

ENTRY(omap_modify_auxcoreboot0)
	stmfd   sp!, {r1-r12, lr}
	ldr	r12, =0x104
	dsb
	smc	#0
	ldmfd   sp!, {r1-r12, pc}
ENDPROC(omap_modify_auxcoreboot0)

ENTRY(omap_auxcoreboot_addr)
	stmfd   sp!, {r2-r12, lr}
	ldr	r12, =0x105
	dsb
	smc	#0
	ldmfd   sp!, {r2-r12, pc}
ENDPROC(omap_auxcoreboot_addr)

ENTRY(omap_read_auxcoreboot0)
	stmfd   sp!, {r2-r12, lr}
	ldr	r12, =0x103
	dsb
	smc	#0
	ldmfd   sp!, {r2-r12, pc}
ENDPROC(omap_read_auxcoreboot0)
