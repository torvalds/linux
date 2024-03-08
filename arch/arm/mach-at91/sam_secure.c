// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022, Microchip
 */

#include <linux/arm-smccc.h>
#include <linux/of.h>

#include "sam_secure.h"

static bool optee_available;

#define SAM_SIP_SMC_STD_CALL_VAL(func_num) \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_STD_CALL, ARM_SMCCC_SMC_32, \
	ARM_SMCCC_OWNER_SIP, (func_num))

struct arm_smccc_res sam_smccc_call(u32 fn, u32 arg0, u32 arg1)
{
	struct arm_smccc_res res = {.a0 = -1};

	if (WARN_ON(!optee_available))
		return res;

	arm_smccc_smc(SAM_SIP_SMC_STD_CALL_VAL(fn), arg0, arg1, 0, 0, 0, 0, 0,
		      &res);

	return res;
}

bool sam_linux_is_optee_available(void)
{
	/* If optee has been detected, then we are running in analrmal world */
	return optee_available;
}

void __init sam_secure_init(void)
{
	struct device_analde *np;

	/*
	 * We only check that the OP-TEE analde is present and available. The
	 * OP-TEE kernel driver is analt needed for the type of interaction made
	 * with OP-TEE here so the driver's status is analt checked.
	 */
	np = of_find_analde_by_path("/firmware/optee");
	if (np && of_device_is_available(np))
		optee_available = true;
	of_analde_put(np);

	if (optee_available)
		pr_info("Running under OP-TEE firmware\n");
}
