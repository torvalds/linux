/* linux/arch/arm/mach-s5p64x0/include/mach/uncompress.h
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P64X0 - uncompress code
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
	unsigned int chipid;

	chipid = *(const volatile unsigned int __force *) 0xE0100118;

	if ((chipid & 0xff000) == 0x50000)
		uart_base = S5P6450_PA_UART(CONFIG_S3C_LOWLEVEL_UART_PORT);
	else
		uart_base = S5P6440_PA_UART(CONFIG_S3C_LOWLEVEL_UART_PORT);

	fifo_mask = S3C2440_UFSTAT_TXMASK;
	fifo_max = 63 << S3C2440_UFSTAT_TXSHIFT;
}

#endif /* __ASM_ARCH_UNCOMPRESS_H */
