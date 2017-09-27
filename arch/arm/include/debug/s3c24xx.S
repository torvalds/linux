/* arch/arm/mach-s3c2410/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Copyright (C) 2005 Simtec Electronics
 *
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/serial_s3c.h>

#define S3C2410_UART1_OFF (0x4000)

	.macro addruart, rp, rv, tmp
		ldr	\rp, = CONFIG_DEBUG_UART_PHYS
		ldr	\rv, = CONFIG_DEBUG_UART_VIRT
	.endm

	.macro  fifo_full_s3c2410 rd, rx
		ldr	\rd, [\rx, # S3C2410_UFSTAT]
		tst	\rd, #S3C2410_UFSTAT_TXFULL
	.endm

	.macro fifo_level_s3c2410 rd, rx
		ldr	\rd, [\rx, # S3C2410_UFSTAT]
		and	\rd, \rd, #S3C2410_UFSTAT_TXMASK
	.endm

/* Select the correct implementation depending on the configuration. The
 * S3C2440 will get selected by default, as these are the most widely
 * used variants of these
*/

#if defined(CONFIG_DEBUG_S3C2410_UART)
#define fifo_full  fifo_full_s3c2410
#define fifo_level fifo_level_s3c2410
#endif

/* include the reset of the code which will do the work */

#include <debug/samsung.S>
