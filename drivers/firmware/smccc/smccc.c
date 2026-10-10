// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Arm Limited
 */

#define pr_fmt(fmt) "smccc: " fmt

#include <linux/cache.h>
#include <linux/init.h>
#include <linux/arm-smccc.h>
#include <linux/kernel.h>
#include <linux/arm-smccc-bus.h>
#include <linux/arm-smccc-rsi.h>

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

bool arm_smccc_hypervisor_has_uuid(const uuid_t *hyp_uuid)
{
	struct arm_smccc_res res = {};
	uuid_t uuid;

	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_CALL_UID_FUNC_ID, &res);
	if (res.a0 == SMCCC_RET_NOT_SUPPORTED)
		return false;

	uuid = smccc_res_to_uuid(res.a0, res.a1, res.a2, res.a3);
	return uuid_equal(&uuid, hyp_uuid);
}
EXPORT_SYMBOL_GPL(arm_smccc_hypervisor_has_uuid);

struct smccc_device_info {
	u32 func_id;
	bool requires_smc;
	const char *device_name;
};

static const struct smccc_device_info smccc_devices[] __initconst = {
	{
		.func_id        = ARM_SMCCC_TRNG_VERSION,
		.requires_smc   = false,
		.device_name    = "arm-smccc-trng",
	},
	{
		.func_id        = SMC_RSI_ABI_VERSION,
		.requires_smc   = true,
		.device_name    = "arm-rsi",
	},
};

static bool __init smccc_probe_smccc_device(const struct smccc_device_info *smccc_dev)
{
	int ret;
	struct arm_smccc_res res = {};

	if (smccc_conduit == SMCCC_CONDUIT_NONE)
		return false;

	if (smccc_dev->requires_smc && smccc_conduit != SMCCC_CONDUIT_SMC)
		return false;

	if (IS_ENABLED(CONFIG_ARM) && ARM_SMCCC_IS_64(smccc_dev->func_id))
		return false;

	arm_smccc_1_1_invoke(smccc_dev->func_id, 0, &res);
	ret = res.a0;

	if (ret == SMCCC_RET_NOT_SUPPORTED)
		return false;

	return true;
}

static int __init smccc_devices_init(void)
{
	struct arm_smccc_device *sdev;
	const struct smccc_device_info *smccc_dev;

	for (int i = 0; i < ARRAY_SIZE(smccc_devices); i++) {
		smccc_dev = &smccc_devices[i];

		if (!smccc_probe_smccc_device(smccc_dev))
			continue;

		sdev = arm_smccc_device_register(smccc_dev->device_name, smccc_dev->func_id);
		if (IS_ERR(sdev))
			pr_err("%s: could not register device: %pe\n",
			       smccc_dev->device_name, sdev);
	}

	return 0;
}
device_initcall(smccc_devices_init);
