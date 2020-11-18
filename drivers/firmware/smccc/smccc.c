// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Arm Limited
 */

#define pr_fmt(fmt) "smccc: " fmt

#include <linux/init.h>
#include <linux/arm-smccc.h>

static u32 smccc_version = ARM_SMCCC_VERSION_1_0;
static enum arm_smccc_conduit smccc_conduit = SMCCC_CONDUIT_NONE;

void __init arm_smccc_version_init(u32 version, enum arm_smccc_conduit conduit)
{
	smccc_version = version;
	smccc_conduit = conduit;
}

enum arm_smccc_conduit arm_smccc_1_1_get_conduit(void)
{
	if (smccc_version < ARM_SMCCC_VERSION_1_1)
		return SMCCC_CONDUIT_NONE;

	return smccc_conduit;
}

u32 arm_smccc_get_version(void)
{
	return smccc_version;
}
