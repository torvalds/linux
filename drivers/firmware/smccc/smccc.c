// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Arm Limited
 */

#define pr_fmt(fmt) "smccc: " fmt

#include <linux/cache.h>
#include <linux/init.h>
#include <linux/arm-smccc.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <asm/archrandom.h>

static u32 smccc_version = ARM_SMCCC_VERSION_1_0;
static enum arm_smccc_conduit smccc_conduit = SMCCC_CONDUIT_NONE;

bool __ro_after_init smccc_trng_available = false;
s32 __ro_after_init smccc_soc_id_version = SMCCC_RET_NOT_SUPPORTED;
s32 __ro_after_init smccc_soc_id_revision = SMCCC_RET_NOT_SUPPORTED;

void __init arm_smccc_version_init(u32 version, enum arm_smccc_conduit conduit)
{
	struct arm_smccc_res res;

	smccc_version = version;
	smccc_conduit = conduit;

	smccc_trng_available = smccc_probe_trng();

	if ((smccc_version >= ARM_SMCCC_VERSION_1_2) &&
	    (smccc_conduit != SMCCC_CONDUIT_NONE)) {
		arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_FEATURES_FUNC_ID,
				     ARM_SMCCC_ARCH_SOC_ID, &res);
		if ((s32)res.a0 >= 0) {
			arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_SOC_ID, 0, &res);
			smccc_soc_id_version = (s32)res.a0;
			arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_SOC_ID, 1, &res);
			smccc_soc_id_revision = (s32)res.a0;
		}
	}
}

enum arm_smccc_conduit arm_smccc_1_1_get_conduit(void)
{
	if (smccc_version < ARM_SMCCC_VERSION_1_1)
		return SMCCC_CONDUIT_NONE;

	return smccc_conduit;
}
EXPORT_SYMBOL_GPL(arm_smccc_1_1_get_conduit);

u32 arm_smccc_get_version(void)
{
	return smccc_version;
}
EXPORT_SYMBOL_GPL(arm_smccc_get_version);

s32 arm_smccc_get_soc_id_version(void)
{
	return smccc_soc_id_version;
}

s32 arm_smccc_get_soc_id_revision(void)
{
	return smccc_soc_id_revision;
}
EXPORT_SYMBOL_GPL(arm_smccc_get_soc_id_revision);

static int __init smccc_devices_init(void)
{
	struct platform_device *pdev;

	if (smccc_trng_available) {
		pdev = platform_device_register_simple("smccc_trng", -1,
						       NULL, 0);
		if (IS_ERR(pdev))
			pr_err("smccc_trng: could not register device: %ld\n",
			       PTR_ERR(pdev));
	}

	return 0;
}
device_initcall(smccc_devices_init);
