/*
 * Samsung's EXYNOS4 flattened device tree enabled machine
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
#include <linux/clocksource.h>

#include <asm/mach/arch.h>
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
static const struct of_dev_auxdata exynos4_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("samsung,exynos4210-uart", EXYNOS4_PA_UART0,
				"exynos4210-uart.0", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-uart", EXYNOS4_PA_UART1,
				"exynos4210-uart.1", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-uart", EXYNOS4_PA_UART2,
				"exynos4210-uart.2", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-uart", EXYNOS4_PA_UART3,
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
	OF_DEV_AUXDATA("samsung,s3c2440-i2c", EXYNOS4_PA_IIC(1),
				"s3c2440-i2c.1", NULL),
	OF_DEV_AUXDATA("samsung,s3c2440-i2c", EXYNOS4_PA_IIC(2),
				"s3c2440-i2c.2", NULL),
	OF_DEV_AUXDATA("samsung,s3c2440-i2c", EXYNOS4_PA_IIC(3),
				"s3c2440-i2c.3", NULL),
	OF_DEV_AUXDATA("samsung,s3c2440-i2c", EXYNOS4_PA_IIC(4),
				"s3c2440-i2c.4", NULL),
	OF_DEV_AUXDATA("samsung,s3c2440-i2c", EXYNOS4_PA_IIC(5),
				"s3c2440-i2c.5", NULL),
	OF_DEV_AUXDATA("samsung,s3c2440-i2c", EXYNOS4_PA_IIC(6),
				"s3c2440-i2c.6", NULL),
	OF_DEV_AUXDATA("samsung,s3c2440-i2c", EXYNOS4_PA_IIC(7),
				"s3c2440-i2c.7", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-spi", EXYNOS4_PA_SPI0,
				"exynos4210-spi.0", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-spi", EXYNOS4_PA_SPI1,
				"exynos4210-spi.1", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-spi", EXYNOS4_PA_SPI2,
				"exynos4210-spi.2", NULL),
	OF_DEV_AUXDATA("arm,pl330", EXYNOS4_PA_PDMA0, "dma-pl330.0", NULL),
	OF_DEV_AUXDATA("arm,pl330", EXYNOS4_PA_PDMA1, "dma-pl330.1", NULL),
	OF_DEV_AUXDATA("arm,pl330", EXYNOS4_PA_MDMA1, "dma-pl330.2", NULL),
	OF_DEV_AUXDATA("samsung,exynos4210-tmu", EXYNOS4_PA_TMU,
				"exynos-tmu", NULL),
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x13620000,
			"exynos-sysmmu.0", NULL), /* MFC_L */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x13630000,
			"exynos-sysmmu.1", NULL), /* MFC_R */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x13E20000,
			"exynos-sysmmu.2", NULL), /* TV */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x11A60000,
			"exynos-sysmmu.3", NULL), /* JPEG */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x12A30000,
			"exynos-sysmmu.4", NULL), /* ROTATOR */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x11A20000,
			"exynos-sysmmu.5", NULL), /* FIMC0 */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x11A30000,
			"exynos-sysmmu.6", NULL), /* FIMC1 */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x11A40000,
			"exynos-sysmmu.7", NULL), /* FIMC2 */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x11A50000,
			"exynos-sysmmu.8", NULL), /* FIMC3 */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x12A20000,
			"exynos-sysmmu.9", NULL), /* G2D(4210) */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x10A40000,
			"exynos-sysmmu.9", NULL), /* G2D(4x12) */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x11E20000,
			"exynos-sysmmu.10", NULL), /* FIMD0 */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x12220000,
			"exynos-sysmmu.11", NULL), /* FIMD1(4210) */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x12260000,
			"exynos-sysmmu.12", NULL), /* IS0(4x12) */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x122B0000,
			"exynos-sysmmu.13", NULL), /* IS1(4x12) */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x123B0000,
			"exynos-sysmmu.14", NULL), /* FIMC-LITE0(4x12) */
	OF_DEV_AUXDATA("samsung,exynos-sysmmu", 0x123C0000,
			"exynos-sysmmu.15", NULL), /* FIMC-LITE1(4x12) */
	{},
};

static void __init exynos4_dt_map_io(void)
{
	exynos_init_io(NULL, 0);
	s3c24xx_init_clocks(24000000);
}

static void __init exynos4_dt_machine_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
				exynos4_auxdata_lookup, NULL);
}

static char const *exynos4_dt_compat[] __initdata = {
	"samsung,exynos4210",
	"samsung,exynos4212",
	"samsung,exynos4412",
	NULL
};

DT_MACHINE_START(EXYNOS4210_DT, "Samsung Exynos4 (Flattened Device Tree)")
	/* Maintainer: Thomas Abraham <thomas.abraham@linaro.org> */
	.smp		= smp_ops(exynos_smp_ops),
	.init_irq	= exynos4_init_irq,
	.map_io		= exynos4_dt_map_io,
	.init_machine	= exynos4_dt_machine_init,
	.init_late	= exynos_init_late,
	.init_time	= clocksource_of_init,
	.dt_compat	= exynos4_dt_compat,
	.restart        = exynos4_restart,
MACHINE_END
