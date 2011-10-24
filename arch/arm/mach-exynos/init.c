/* linux/arch/arm/mach-exynos4/init.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/serial_core.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/regs-serial.h>

/* uart registration process */
void __init exynos4_common_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	struct s3c2410_uartcfg *tcfg = cfg;
	u32 ucnt;

	for (ucnt = 0; ucnt < no; ucnt++, tcfg++) {
		tcfg->has_fracval = 1;
		tcfg->flags |= NO_NEED_CHECK_CLKSRC;
	}

	s3c24xx_init_uartdevs("s5pv210-uart", s5p_uart_resources, cfg, no);
}
