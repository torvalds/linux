/*
 * arch/arm/include/asm/tc2_pm_setup.S
 *
 * Created by: Nicolas Pitre, October 2012
 (             (based on dcscb_setup.S by Dave Martin)
 * Copyright:  (C) 2012  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/linkage.h>
#include <asm/mcpm.h>


#define SPC_PHYS_BASE		0x7FFF0000
#define SPC_WAKE_INT_STAT	0xb2c

#define SNOOP_CTL_A15		0x404
#define SNOOP_CTL_A7		0x504

#define A15_SNOOP_MASK		(0x3 << 7)
#define A7_SNOOP_MASK		(0x1 << 13)

#define A15_BX_ADDR0		0xB68


ENTRY(tc2_resume)
	mrc	p15, 0, r0, c0, c0, 5
	ubfx	r1, r0, #0, #4		@ r1 = cpu
	ubfx	r2, r0, #8, #4		@ r2 = cluster
	add	r1, r1, r2, lsl #2	@ r1 = index of CPU in WAKE_INT_STAT
	ldr	r3, =SPC_PHYS_BASE + SPC_WAKE_INT_STAT
	ldr	r3, [r3]
	lsr	r3, r1
	tst	r3, #1
	wfieq				@ if no pending IRQ reenters wfi
	b	mcpm_entry_point
ENDPROC(tc2_resume)

/*
 * Enable cluster-level coherency, in preparation for turning on the MMU.
 * The ACTLR SMP bit does not need to be set here, because cpu_resume()
 * already restores that.
 */

ENTRY(tc2_pm_power_up_setup)

	cmp	r0, #0
	beq	2f

	b cci_enable_port_for_self

2:	@ Clear the BX addr register
	ldr	r3, =SPC_PHYS_BASE + A15_BX_ADDR0
	mrc	p15, 0, r0, c0, c0, 5	@ MPIDR
	ubfx	r1, r0, #8, #4		@ cluster
	ubfx	r0, r0, #0, #4		@ cpu
	add	r3, r3, r1, lsl #4
	mov	r1, #0
	str	r1, [r3, r0, lsl #2]
	dsb

	bx	lr

ENDPROC(tc2_pm_power_up_setup)
