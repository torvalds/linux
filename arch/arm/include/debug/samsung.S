/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2005, 2007 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 */

#include <linux/serial_s3c.h>

/* The S5PV210/S5PC110 implementations are as belows. */

	.macro fifo_level_s5pv210 rd, rx
		ldr	\rd, [\rx, # S3C2410_UFSTAT]
ARM_BE8(rev \rd, \rd)
		and	\rd, \rd, #S5PV210_UFSTAT_TXMASK
	.endm

	.macro  fifo_full_s5pv210 rd, rx
		ldr	\rd, [\rx, # S3C2410_UFSTAT]
ARM_BE8(rev \rd, \rd)
		tst	\rd, #S5PV210_UFSTAT_TXFULL
	.endm

/* The S3C2440 implementations are used by default as they are the
 * most widely re-used */

	.macro fifo_level_s3c2440 rd, rx
		ldr	\rd, [\rx, # S3C2410_UFSTAT]
ARM_BE8(rev \rd, \rd)
		and	\rd, \rd, #S3C2440_UFSTAT_TXMASK
	.endm

#ifndef fifo_level
#define fifo_level fifo_level_s3c2440
#endif

	.macro  fifo_full_s3c2440 rd, rx
		ldr	\rd, [\rx, # S3C2410_UFSTAT]
ARM_BE8(rev \rd, \rd)
		tst	\rd, #S3C2440_UFSTAT_TXFULL
	.endm

#ifndef fifo_full
#define fifo_full fifo_full_s3c2440
#endif

	.macro	senduart,rd,rx
		strb 	\rd, [\rx, # S3C2410_UTXH]
	.endm

	.macro	busyuart, rd, rx
		ldr	\rd, [\rx, # S3C2410_UFCON]
ARM_BE8(rev \rd, \rd)
		tst	\rd, #S3C2410_UFCON_FIFOMODE	@ fifo enabled?
		beq	1001f				@
		@ FIFO enabled...
1003:
		fifo_full \rd, \rx
		bne	1003b
		b	1002f

1001:
		@ busy waiting for non fifo
		ldr	\rd, [\rx, # S3C2410_UTRSTAT]
ARM_BE8(rev \rd, \rd)
		tst	\rd, #S3C2410_UTRSTAT_TXFE
		beq	1001b

1002:		@ exit busyuart
	.endm

	.macro	waituartcts,rd,rx
	.endm

	.macro	waituarttxrdy,rd,rx
		ldr	\rd, [\rx, # S3C2410_UFCON]
ARM_BE8(rev \rd, \rd)
		tst	\rd, #S3C2410_UFCON_FIFOMODE	@ fifo enabled?
		beq	1001f				@
		@ FIFO enabled...
1003:
		fifo_level \rd, \rx
		teq	\rd, #0
		bne	1003b
		b	1002f
1001:
		@ idle waiting for non fifo
		ldr	\rd, [\rx, # S3C2410_UTRSTAT]
ARM_BE8(rev \rd, \rd)
		tst	\rd, #S3C2410_UTRSTAT_TXFE
		beq	1001b

1002:		@ exit busyuart
	.endm
