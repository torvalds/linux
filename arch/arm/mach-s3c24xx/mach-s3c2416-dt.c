/*
 * Samsung's S3C2416 flattened device tree enabled machine
 *
 * Copyright (c) 2012 Heiko Stuebner <heiko@sntech.de>
 *
 * based on mach-exynos/mach-exynos4-dt.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2010-2011 Linaro Ltd.
 *		www.linaro.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/clocksource.h>
#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <linux/serial_core.h>

#include <asm/mach/arch.h>
#include <mach/map.h>

#include <plat/cpu.h>
#include <plat/pm.h>
#include <plat/regs-serial.h>

#include "common.h"

/*
 * The following lookup table is used to override device names when devices
 * are registered from device tree. This is temporarily added to enable
 * device tree support addition for the S3C2416 architecture.
 *
 * For drivers that require platform data to be provided from the machine
 * file, a platform data pointer can also be supplied along with the
 * devices names. Usually, the platform data elements that cannot be parsed
 * from the device tree by the drivers (example: function pointers) are
 * supplied. But it should be noted that this is a temporary mechanism and
 * at some point, the drivers should be capable of parsing all the platform
 * data from the device tree.
 */
static const struct of_dev_auxdata s3c2416_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("samsung,s3c2440-uart", S3C2410_PA_UART0,
				"s3c2440-uart.0", NULL),
	OF_DEV_AUXDATA("samsung,s3c2440-uart", S3C2410_PA_UART1,
				"s3c2440-uart.1", NULL),
	OF_DEV_AUXDATA("samsung,s3c2440-uart", S3C2410_PA_UART2,
				"s3c2440-uart.2", NULL),
	OF_DEV_AUXDATA("samsung,s3c2440-uart", S3C2443_PA_UART3,
				"s3c2440-uart.3", NULL),
	OF_DEV_AUXDATA("samsung,s3c6410-sdhci", S3C_PA_HSMMC0,
				"s3c-sdhci.0", NULL),
	OF_DEV_AUXDATA("samsung,s3c6410-sdhci", S3C_PA_HSMMC1,
				"s3c-sdhci.1", NULL),
	OF_DEV_AUXDATA("samsung,s3c2440-i2c", S3C_PA_IIC,
				"s3c2440-i2c.0", NULL),
	{},
};

static void __init s3c2416_dt_map_io(void)
{
	s3c24xx_init_io(NULL, 0);
	s3c24xx_init_clocks(12000000);
}

static void __init s3c2416_dt_machine_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
				s3c2416_auxdata_lookup, NULL);

	s3c_pm_init();
}

static char const *s3c2416_dt_compat[] __initdata = {
	"samsung,s3c2416",
	"samsung,s3c2450",
	NULL
};

DT_MACHINE_START(S3C2416_DT, "Samsung S3C2416 (Flattened Device Tree)")
	/* Maintainer: Heiko Stuebner <heiko@sntech.de> */
	.dt_compat	= s3c2416_dt_compat,
	.map_io		= s3c2416_dt_map_io,
	.init_irq	= irqchip_init,
	.init_machine	= s3c2416_dt_machine_init,
	 .init_time	= clocksource_of_init,
	.restart	= s3c2416_restart,
MACHINE_END
