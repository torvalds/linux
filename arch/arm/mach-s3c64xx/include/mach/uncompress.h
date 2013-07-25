/* arch/arm/mach-s3c6400/include/mach/uncompress.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C6400 - uncompress code
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
	fifo_mask = S3C2440_UFSTAT_TXMASK;
	fifo_max = 63 << S3C2440_UFSTAT_TXSHIFT;

	uart_base = (volatile u8 *)S3C_PA_UART +
		(S3C_UART_OFFSET * CONFIG_S3C_LOWLEVEL_UART_PORT);
}

#endif /* __ASM_ARCH_UNCOMPRESS_H */
