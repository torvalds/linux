/*
 * Samsung's Exynos4210 flattened device tree enabled machine
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

#include <linux/of_platform.h>
#include <linux/serial_core.h>

#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <mach/map.h>

#include <plat/cpu.h>
#include <plat/regs-serial.h>

#include "common.h"

/*
 * The following lookup table is used to override device names when devices
 * are registered from device tree. This is temporarily added to enable
 * device tree support addition for the Exynos4 architecture.
 *
 * For drivers that require platform data to be provided from the machine
 * file, a platform data pointer can also be supplied along with the
 * devices names. Usually, the platform data elements that cannot be parsed
 * from the device tree by the drivers (example: function pointers) are
 * supplied. But it should be noted that this is a temporary mechanism and
 * at some point, the drivers should be capable of parsing all the platform
 * data from the device tree.
 */
static const struct of_dev_auxdata exynos4210_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("samsung,exynos4210-uart", S5P_PA_UART0,
				"exynos4210-uart.0", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-uart", S5P_PA_UART1,
				"exynos4210-uart.1", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-uart", S5P_PA_UART2,
				"exynos4210-uart.2", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-uart", S5P_PA_UART3,
				"exynos4210-uart.3", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-sdhci", EXYNOS4_PA_HSMMC(0),
				"exynos4-sdhci.0", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-sdhci", EXYNOS4_PA_HSMMC(1),
				"exynos4-sdhci.1", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-sdhci", EXYNOS4_PA_HSMMC(2),
				"exynos4-sdhci.2", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-sdhci", EXYNOS4_PA_HSMMC(3),
				"exynos4-sdhci.3", NULL),
	OF_DEV_AUXDATA("samsung,s3c2440-i2c", EXYNOS4_PA_IIC(0),
				"s3c2440-i2c.0", NULL),
	OF_DEV_AUXDATA("arm,pl330", EXYNOS4_PA_PDMA0, "dma-pl330.0", NULL),
	OF_DEV_AUXDATA("arm,pl330", EXYNOS4_PA_PDMA1, "dma-pl330.1", NULL),
	{},
};

static void __init exynos4210_dt_map_io(void)
{
	exynos_init_io(NULL, 0);
	s3c24xx_init_clocks(24000000);
}

static void __init exynos4210_dt_machine_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
				exynos4210_auxdata_lookup, NULL);
}

static char const *exynos4210_dt_compat[] __initdata = {
	"samsung,exynos4210",
	NULL
};

DT_MACHINE_START(EXYNOS4210_DT, "Samsung Exynos4 (Flattened Device Tree)")
	/* Maintainer: Thomas Abraham <thomas.abraham@linaro.org> */
	.init_irq	= exynos4_init_irq,
	.map_io		= exynos4210_dt_map_io,
	.handle_irq	= gic_handle_irq,
	.init_machine	= exynos4210_dt_machine_init,
	.timer		= &exynos4_timer,
	.dt_compat	= exynos4210_dt_compat,
	.restart        = exynos4_restart,
MACHINE_END
