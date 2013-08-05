/*
 * SAMSUNG EXYNOS5250 Flattened Device Tree enabled machine
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <mach/regs-pmu.h>

#include "common.h"

static void __init exynos5_dt_machine_init(void)
{
	struct device_node *i2c_np;
	const char *i2c_compat = "samsung,s3c2440-i2c";
	unsigned int tmp;

	/*
	 * Exynos5's legacy i2c controller and new high speed i2c
	 * controller have muxed interrupt sources. By default the
	 * interrupts for 4-channel HS-I2C controller are enabled.
	 * If node for first four channels of legacy i2c controller
	 * are available then re-configure the interrupts via the
	 * system register.
	 */
	for_each_compatible_node(i2c_np, NULL, i2c_compat) {
		if (of_device_is_available(i2c_np)) {
			if (of_alias_get_id(i2c_np, "i2c") < 4) {
				tmp = readl(EXYNOS5_SYS_I2C_CFG);
				writel(tmp & ~(0x1 << of_alias_get_id(i2c_np, "i2c")),
						EXYNOS5_SYS_I2C_CFG);
			}
		}
	}

	exynos_cpuidle_init();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static char const *exynos5_dt_compat[] __initdata = {
	"samsung,exynos5250",
	"samsung,exynos5420",
	"samsung,exynos5440",
	NULL
};

DT_MACHINE_START(EXYNOS5_DT, "SAMSUNG EXYNOS5 (Flattened Device Tree)")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	.smp		= smp_ops(exynos_smp_ops),
	.map_io		= exynos_init_io,
	.init_machine	= exynos5_dt_machine_init,
	.init_late	= exynos_init_late,
	.dt_compat	= exynos5_dt_compat,
	.restart        = exynos5_restart,
MACHINE_END
