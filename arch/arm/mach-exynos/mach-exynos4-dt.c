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

#include <asm/mach/arch.h>

#include "common.h"

static void __init exynos4_dt_machine_init(void)
{
	exynos_cpuidle_init();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
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
	.map_io		= exynos_init_io,
	.init_early	= exynos_firmware_init,
	.init_machine	= exynos4_dt_machine_init,
	.init_late	= exynos_init_late,
	.dt_compat	= exynos4_dt_compat,
	.restart        = exynos4_restart,
MACHINE_END
