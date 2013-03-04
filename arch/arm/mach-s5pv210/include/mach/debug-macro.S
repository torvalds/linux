/* linux/arch/arm/mach-s5pv210/include/mach/debug-macro.S
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Based on arch/arm/mach-s3c6400/include/mach/debug-macro.S
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* pull in the relevant register and map files. */

#include <mach/map.h>
#include <plat/regs-serial.h>

	/* note, for the boot process to work we have to keep the UART
	 * virtual address aligned to an 1MiB boundary for the L1
	 * mapping the head code makes. We keep the UART virtual address
	 * aligned and add in the offset when we load the value here.
	 */

	.macro addruart, rp, rv, tmp
		ldr	\rp, = S3C_PA_UART
		ldr	\rv, = S3C_VA_UART
#if CONFIG_DEBUG_S3C_UART != 0
		add	\rp, \rp, #(0x400 * CONFIG_DEBUG_S3C_UART)
		add	\rv, \rv, #(0x400 * CONFIG_DEBUG_S3C_UART)
#endif
	.endm

#define fifo_full fifo_full_s5pv210
#define fifo_level fifo_level_s5pv210

/* include the reset of the code which will do the work, we're only
 * compiling for a single cpu processor type so the default of s3c2440
 * will be fine with us.
 */

#include <debug/samsung.S>
