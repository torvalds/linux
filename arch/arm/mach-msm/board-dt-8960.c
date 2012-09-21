/* Copyright (c) 2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <asm/hardware/gic.h>
#include <asm/mach/arch.h>

#include "common.h"

static const struct of_device_id msm_dt_gic_match[] __initconst = {
	{ .compatible = "qcom,msm-qgic2", .data = gic_of_init },
	{ }
};

static void __init msm_dt_init_irq(void)
{
	of_irq_init(msm_dt_gic_match);
}

static void __init msm_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const msm8960_dt_match[] __initconst = {
	"qcom,msm8960-cdp",
	NULL
};

DT_MACHINE_START(MSM8960_DT, "Qualcomm MSM (Flattened Device Tree)")
	.map_io = msm_map_msm8960_io,
	.init_irq = msm_dt_init_irq,
	.timer = &msm_dt_timer,
	.init_machine = msm_dt_init,
	.dt_compat = msm8960_dt_match,
	.handle_irq = gic_handle_irq,
MACHINE_END
