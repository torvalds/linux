/* Copyright (c) 2010-2012,2013 The Linux Foundation. All rights reserved.
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
#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"

static const char * const msm_dt_match[] __initconst = {
	"qcom,msm8660-fluid",
	"qcom,msm8660-surf",
	"qcom,msm8960-cdp",
	NULL
};

DT_MACHINE_START(MSM_DT, "Qualcomm MSM (Flattened Device Tree)")
	.smp = smp_ops(msm_smp_ops),
	.dt_compat = msm_dt_match,
MACHINE_END
