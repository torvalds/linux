/* linux/arch/arm/mach-s5p64x0/init.c
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P64X0 - Init support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/serial_core.h>

#include <mach/map.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/s5p6440.h>
#include <plat/s5p6450.h>
#include <plat/regs-serial.h>

/* uart registration process */
void __init s5p6440_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	int uart;

	for (uart = 0; uart < no; uart++) {
		s5p_uart_resources[uart].resources->start = S5P6440_PA_UART(uart);
		s5p_uart_resources[uart].resources->end = S5P6440_PA_UART(uart) + S5P_SZ_UART;
	}

	s3c24xx_init_uartdevs("s3c6400-uart", s5p_uart_resources, cfg, no);
}

void __init s5p6450_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c24xx_init_uartdevs("s3c6400-uart", s5p_uart_resources, cfg, no);
}
