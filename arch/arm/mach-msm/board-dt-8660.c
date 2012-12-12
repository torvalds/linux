/* Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
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
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>

#include <mach/board.h>
#include "common.h"

static const struct of_device_id msm_dt_gic_match[] __initconst = {
	{ .compatible = "qcom,msm-8660-qgic", .data = gic_of_init },
	{}
};

static void __init msm8x60_init_irq(void)
{
	of_irq_init(msm_dt_gic_match);
}

static void __init msm8x60_init_late(void)
{
	smd_debugfs_init();
}

static struct of_dev_auxdata msm_auxdata_lookup[] __initdata = {
	{}
};

static void __init msm8x60_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			msm_auxdata_lookup, NULL);
}

static const char *msm8x60_fluid_match[] __initdata = {
	"qcom,msm8660-fluid",
	"qcom,msm8660-surf",
	NULL
};

DT_MACHINE_START(MSM_DT, "Qualcomm MSM (Flattened Device Tree)")
	.smp = smp_ops(msm_smp_ops),
	.map_io = msm_map_msm8x60_io,
	.init_irq = msm8x60_init_irq,
	.handle_irq = gic_handle_irq,
	.init_machine = msm8x60_dt_init,
	.init_late = msm8x60_init_late,
	.timer = &msm_dt_timer,
	.dt_compat = msm8x60_fluid_match,
MACHINE_END
