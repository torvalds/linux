/* linux/arch/arm/mach-s5p6442/include/mach/debug-macro.S
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

	.macro addruart, rx, rtmp
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1
		ldreq	\rx, = S3C_PA_UART
		ldrne	\rx, = S3C_VA_UART
#if CONFIG_DEBUG_S3C_UART != 0
		add	\rx, \rx, #(0x400 * CONFIG_DEBUG_S3C_UART)
#endif
	.endm

#define fifo_full fifo_full_s5pv210
#define fifo_level fifo_level_s5pv210

/* include the reset of the code which will do the work, we're only
 * compiling for a single cpu processor type so the default of s3c2440
 * will be fine with us.
 */

#include <plat/debug-macro.S>
