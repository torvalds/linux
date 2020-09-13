/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 Power Manager (Suspend-To-RAM) support
 *
 * Based on PXA/SA1100 sleep code by:
 *	Nicolas Pitre, (c) 2002 Monta Vista Software Inc
 *	Cliff Brake, (c) 2001
 */

#include <linux/linkage.h>
#include <linux/serial_s3c.h>
#include <asm/assembler.h>
#include "map.h"

#include "regs-gpio.h"
#include "regs-clock.h"

#include "regs-mem-s3c24xx.h"

	/* s3c2410_cpu_suspend
	 *
	 * put the cpu into sleep mode
	*/

ENTRY(s3c2410_cpu_suspend)
	@@ prepare cpu to sleep

	ldr	r4, =S3C2410_REFRESH
	ldr	r5, =S3C24XX_MISCCR
	ldr	r6, =S3C2410_CLKCON
	ldr	r7, [r4]		@ get REFRESH (and ensure in TLB)
	ldr	r8, [r5]		@ get MISCCR (and ensure in TLB)
	ldr	r9, [r6]		@ get CLKCON (and ensure in TLB)

	orr	r7, r7, #S3C2410_REFRESH_SELF	@ SDRAM sleep command
	orr	r8, r8, #S3C2410_MISCCR_SDSLEEP @ SDRAM power-down signals
	orr	r9, r9, #S3C2410_CLKCON_POWER	@ power down command

	teq	pc, #0			@ first as a trial-run to load cache
	bl	s3c2410_do_sleep
	teq	r0, r0			@ now do it for real
	b	s3c2410_do_sleep	@

	@@ align next bit of code to cache line
	.align	5
s3c2410_do_sleep:
	streq	r7, [r4]			@ SDRAM sleep command
	streq	r8, [r5]			@ SDRAM power-down config
	streq	r9, [r6]			@ CPU sleep
1:	beq	1b
	ret	lr
