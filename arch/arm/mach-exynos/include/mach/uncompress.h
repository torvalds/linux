/*
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - uncompress code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H __FILE__

#include <asm/mach-types.h>

#include <mach/map.h>

volatile u8 *uart_base;

#include <plat/uncompress.h>

static void arch_detect_cpu(void)
{
	if (machine_is_smdk5250())
		uart_base = (volatile u8 *)EXYNOS5_PA_UART + (S3C_UART_OFFSET * CONFIG_S3C_LOWLEVEL_UART_PORT);
	else
		uart_base = (volatile u8 *)EXYNOS4_PA_UART + (S3C_UART_OFFSET * CONFIG_S3C_LOWLEVEL_UART_PORT);

	/*
	 * For preventing FIFO overrun or infinite loop of UART console,
	 * fifo_max should be the minimum fifo size of all of the UART channels
	 */
	fifo_mask = S5PV210_UFSTAT_TXMASK;
	fifo_max = 15 << S5PV210_UFSTAT_TXSHIFT;
}
#endif /* __ASM_ARCH_UNCOMPRESS_H */
