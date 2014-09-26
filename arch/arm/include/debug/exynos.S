/*
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* pull in the relevant register and map files. */

#define S3C_ADDR_BASE   0xF6000000
#define S3C_VA_UART	S3C_ADDR_BASE + 0x01000000
#define EXYNOS4_PA_UART	0x13800000
#define EXYNOS5_PA_UART	0x12C00000

	/* note, for the boot process to work we have to keep the UART
	 * virtual address aligned to an 1MiB boundary for the L1
	 * mapping the head code makes. We keep the UART virtual address
	 * aligned and add in the offset when we load the value here.
	 */

	.macro addruart, rp, rv, tmp
		mrc	p15, 0, \tmp, c0, c0, 0
		and	\tmp, \tmp, #0xf0
		teq	\tmp, #0xf0		@@ A15
		beq	100f
		mrc	p15, 0, \tmp, c0, c0, 5
		and	\tmp, \tmp, #0xf00
		teq	\tmp, #0x100		@@ A15 + A7 but boot to A7
100:		ldreq	\rp, =EXYNOS5_PA_UART
		movne	\rp, #EXYNOS4_PA_UART	@@ EXYNOS4
		ldr	\rv, =S3C_VA_UART
#if CONFIG_DEBUG_S3C_UART != 0
		add	\rp, \rp, #(0x10000 * CONFIG_DEBUG_S3C_UART)
		add	\rv, \rv, #(0x10000 * CONFIG_DEBUG_S3C_UART)
#endif
	.endm

#define fifo_full fifo_full_s5pv210
#define fifo_level fifo_level_s5pv210

#include <debug/samsung.S>
