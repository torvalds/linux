/* linux/arch/arm/mach-s5pv210/include/mach/uncompress.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PV210 - uncompress code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H

#include <mach/map.h>
#include <plat/uncompress.h>

static void arch_detect_cpu(void)
{
	/* we do not need to do any cpu detection here at the moment. */
	fifo_mask = S5PV210_UFSTAT_TXMASK;
	fifo_max = 63 << S5PV210_UFSTAT_TXSHIFT;

	uart_base = (volatile u8 *)S5P_PA_UART(CONFIG_S3C_LOWLEVEL_UART_PORT);
}

#endif /* __ASM_ARCH_UNCOMPRESS_H */
