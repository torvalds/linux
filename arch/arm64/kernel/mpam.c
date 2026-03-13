// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Arm Ltd. */

#include <asm/mpam.h>

#include <linux/arm_mpam.h>
#include <linux/jump_label.h>
#include <linux/percpu.h>

DEFINE_STATIC_KEY_FALSE(mpam_enabled);
DEFINE_PER_CPU(u64, arm64_mpam_default);
DEFINE_PER_CPU(u64, arm64_mpam_current);

u64 arm64_mpam_global_default;

static int __init arm64_mpam_register_cpus(void)
{
	u64 mpamidr = read_sanitised_ftr_reg(SYS_MPAMIDR_EL1);
	u16 partid_max = FIELD_GET(MPAMIDR_EL1_PARTID_MAX, mpamidr);
	u8 pmg_max = FIELD_GET(MPAMIDR_EL1_PMG_MAX, mpamidr);

	return mpam_register_requestor(partid_max, pmg_max);
}
/* Must occur before mpam_msc_driver_init() from subsys_initcall() */
arch_initcall(arm64_mpam_register_cpus)
