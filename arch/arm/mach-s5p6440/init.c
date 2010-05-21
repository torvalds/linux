/* linux/arch/arm/mach-s5p6440/init.c
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P6440 - Init support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/serial_core.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/s5p6440.h>
#include <plat/regs-serial.h>

static struct s3c24xx_uart_clksrc s5p6440_serial_clocks[] = {
	[0] = {
		.name		= "pclk_low",
		.divisor	= 1,
		.min_baud	= 0,
		.max_baud	= 0,
	},
	[1] = {
		.name		= "uclk1",
		.divisor	= 1,
		.min_baud	= 0,
		.max_baud	= 0,
	},
};

/* uart registration process */
void __init s5p6440_common_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	struct s3c2410_uartcfg *tcfg = cfg;
	u32 ucnt;

	for (ucnt = 0; ucnt < no; ucnt++, tcfg++) {
		if (!tcfg->clocks) {
			tcfg->clocks = s5p6440_serial_clocks;
			tcfg->clocks_size = ARRAY_SIZE(s5p6440_serial_clocks);
		}
	}

	s3c24xx_init_uartdevs("s3c6400-uart", s5p_uart_resources, cfg, no);
}
