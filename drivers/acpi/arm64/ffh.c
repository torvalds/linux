// SPDX-License-Identifier: GPL-2.0-only
#include <linux/acpi.h>
#include <linux/arm-smccc.h>
#include <linux/slab.h>

/*
 * Implements ARM64 specific callbacks to support ACPI FFH Operation Region as
 * specified in https://developer.arm.com/docs/den0048/latest
 */
struct acpi_ffh_data {
	struct acpi_ffh_info info;
	void (*invoke_ffh_fn)(unsigned long a0, unsigned long a1,
			      unsigned long a2, unsigned long a3,
			      unsigned long a4, unsigned long a5,
			      unsigned long a6, unsigned long a7,
			      struct arm_smccc_res *args,
			      struct arm_smccc_quirk *res);
	void (*invoke_ffh64_fn)(const struct arm_smccc_1_2_regs *args,
				struct arm_smccc_1_2_regs *res);
};

int acpi_ffh_address_space_arch_setup(void *handler_ctxt, void **region_ctxt)
{
	enum arm_smccc_conduit conduit;
	struct acpi_ffh_data *ffh_ctxt;

	if (arm_smccc_get_version() < ARM_SMCCC_VERSION_1_2)
		return -EOPNOTSUPP;

	conduit = arm_smccc_1_1_get_conduit();
	if (conduit == SMCCC_CONDUIT_NONE) {
		pr_err("%s: invalid SMCCC conduit\n", __func__);
		return -EOPNOTSUPP;
	}

	ffh_ctxt = kzalloc(sizeof(*ffh_ctxt), GFP_KERNEL);
	if (!ffh_ctxt)
		return -ENOMEM;

	if (conduit == SMCCC_CONDUIT_SMC) {
		ffh_ctxt->invoke_ffh_fn = __arm_smccc_smc;
		ffh_ctxt->invoke_ffh64_fn = arm_smccc_1_2_smc;
	} else {
		ffh_ctxt->invoke_ffh_fn = __arm_smccc_hvc;
		ffh_ctxt->invoke_ffh64_fn = arm_smccc_1_2_hvc;
	}

	memcpy(ffh_ctxt, handler_ctxt, sizeof(ffh_ctxt->info));

	*region_ctxt = ffh_ctxt;
	return AE_OK;
}

static bool acpi_ffh_smccc_owner_allowed(u32 fid)
{
	int owner = ARM_SMCCC_OWNER_NUM(fid);

	if (owner == ARM_SMCCC_OWNER_STANDARD ||
	    owner == ARM_SMCCC_OWNER_SIP || owner == ARM_SMCCC_OWNER_OEM)
		return true;

	return false;
}

int acpi_ffh_address_space_arch_handler(acpi_integer *value, void *region_context)
{
	int ret = 0;
	struct acpi_ffh_data *ffh_ctxt = region_context;

	if (ffh_ctxt->info.offset == 0) {
		/* SMC/HVC 32bit call */
		struct arm_smccc_res res;
		u32 a[8] = { 0 }, *ptr = (u32 *)value;

		if (!ARM_SMCCC_IS_FAST_CALL(*ptr) || ARM_SMCCC_IS_64(*ptr) ||
		    !acpi_ffh_smccc_owner_allowed(*ptr) ||
		    ffh_ctxt->info.length > 32) {
			ret = AE_ERROR;
		} else {
			int idx, len = ffh_ctxt->info.length >> 2;

			for (idx = 0; idx < len; idx++)
				a[idx] = *(ptr + idx);

			ffh_ctxt->invoke_ffh_fn(a[0], a[1], a[2], a[3], a[4],
						a[5], a[6], a[7], &res, NULL);
			memcpy(value, &res, sizeof(res));
		}

	} else if (ffh_ctxt->info.offset == 1) {
		/* SMC/HVC 64bit call */
		struct arm_smccc_1_2_regs *r = (struct arm_smccc_1_2_regs *)value;

		if (!ARM_SMCCC_IS_FAST_CALL(r->a0) || !ARM_SMCCC_IS_64(r->a0) ||
		    !acpi_ffh_smccc_owner_allowed(r->a0) ||
		    ffh_ctxt->info.length > sizeof(*r)) {
			ret = AE_ERROR;
		} else {
			ffh_ctxt->invoke_ffh64_fn(r, r);
			memcpy(value, r, ffh_ctxt->info.length);
		}
	} else {
		ret = AE_ERROR;
	}

	return ret;
}
