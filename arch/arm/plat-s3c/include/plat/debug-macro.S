/* linux/include/asm-arm/plat-s3c/debug-macro.S
 *
 * Copyright 2005, 2007 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <plat/regs-serial.h>

/* The S3C2440 implementations are used by default as they are the
 * most widely re-used */

	.macro fifo_level_s3c2440 rd, rx
		ldr	\rd, [ \rx, # S3C2410_UFSTAT ]
		and	\rd, \rd, #S3C2440_UFSTAT_TXMASK
	.endm

#ifndef fifo_level
#define fifo_level fifo_level_s3c2410
#endif

	.macro  fifo_full_s3c2440 rd, rx
		ldr	\rd, [ \rx, # S3C2410_UFSTAT ]
		tst	\rd, #S3C2440_UFSTAT_TXFULL
	.endm

#ifndef fifo_full
#define fifo_full fifo_full_s3c2440
#endif

	.macro	senduart,rd,rx
		strb 	\rd, [\rx, # S3C2410_UTXH ]
	.endm

	.macro	busyuart, rd, rx
		ldr	\rd, [ \rx, # S3C2410_UFCON ]
		tst	\rd, #S3C2410_UFCON_FIFOMODE	@ fifo enabled?
		beq	1001f				@
		@ FIFO enabled...
1003:
		fifo_full \rd, \rx
		bne	1003b
		b	1002f

1001:
		@ busy waiting for non fifo
		ldr	\rd, [ \rx, # S3C2410_UTRSTAT ]
		tst	\rd, #S3C2410_UTRSTAT_TXFE
		beq	1001b

1002:		@ exit busyuart
	.endm

	.macro	waituart,rd,rx
		ldr	\rd, [ \rx, # S3C2410_UFCON ]
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
		ldr	\rd, [ \rx, # S3C2410_UTRSTAT ]
		tst	\rd, #S3C2410_UTRSTAT_TXFE
		beq	1001b

1002:		@ exit busyuart
	.endm
