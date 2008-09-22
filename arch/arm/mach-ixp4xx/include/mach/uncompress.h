/*
 * arch/arm/mach-ixp4xx/include/mach/uncompress.h 
 *
 * Copyright (C) 2002 Intel Corporation.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _ARCH_UNCOMPRESS_H_
#define _ARCH_UNCOMPRESS_H_

#include "ixp4xx-regs.h"
#include <asm/mach-types.h>
#include <linux/serial_reg.h>

#define TX_DONE (UART_LSR_TEMT|UART_LSR_THRE)

static volatile u32* uart_base;

static inline void putc(int c)
{
	/* Check THRE and TEMT bits before we transmit the character.
	 */
	while ((uart_base[UART_LSR] & TX_DONE) != TX_DONE)
		barrier();

	*uart_base = c;
}

static void flush(void)
{
}

static __inline__ void __arch_decomp_setup(unsigned long arch_id)
{
	/*
	 * Some boards are using UART2 as console
	 */
	if (machine_is_adi_coyote() || machine_is_gtwx5715() ||
			 machine_is_gateway7001() || machine_is_wg302v2())
		uart_base = (volatile u32*) IXP4XX_UART2_BASE_PHYS;
	else
		uart_base = (volatile u32*) IXP4XX_UART1_BASE_PHYS;
}

/*
 * arch_id is a variable in decompress_kernel()
 */
#define arch_decomp_setup()	__arch_decomp_setup(arch_id)

#define arch_decomp_wdog()

#endif
