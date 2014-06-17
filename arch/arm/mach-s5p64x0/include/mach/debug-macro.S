/* linux/arch/arm/mach-s5p64x0/include/mach/debug-macro.S
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* pull in the relevant register and map files. */

#include <linux/serial_s3c.h>
#include <plat/map-base.h>
#include <plat/map-s5p.h>

	.macro addruart, rp, rv, tmp
		mov	\rp, #0xE0000000
		orr	\rp, \rp, #0x00100000
		ldr	\rp, [\rp, #0x118 ]
		and	\rp, \rp, #0xff000
		teq	\rp, #0x50000		@@ S5P6450
		ldreq	\rp, =0xEC800000
		movne	\rp, #0xEC000000	@@ S5P6440
		ldrne	\rv, = S3C_VA_UART
#if CONFIG_DEBUG_S3C_UART != 0
		add	\rp, \rp, #(0x400 * CONFIG_DEBUG_S3C_UART)
		add	\rv, \rv, #(0x400 * CONFIG_DEBUG_S3C_UART)
#endif
	.endm

#include <debug/samsung.S>
