// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include <net/devlink.h>

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_nsp.h"
#include "nfp_main.h"

/**
 * struct nfp_devlink_param_u8_arg - Devlink u8 parameter get/set arguments
 * @hwinfo_name:	HWinfo key name
 * @default_hi_val:	Default HWinfo value if HWinfo doesn't exist
 * @invalid_dl_val:	Devlink value to use if HWinfo is unknown/invalid.
 *			-errno if there is no unknown/invalid value available
 * @hi_to_dl:	HWinfo to devlink value mapping
 * @dl_to_hi:	Devlink to hwinfo value mapping
 * @max_dl_val:	Maximum devlink value supported, for validation only
 * @max_hi_val:	Maximum HWinfo value supported, for validation only
 */
struct nfp_devlink_param_u8_arg {
	const char *hwinfo_name;
	const char *default_hi_val;
	int invalid_dl_val;
	u8 hi_to_dl[4];
	u8 dl_to_hi[4];
	u8 max_dl_val;
	u8 max_hi_val;
};

static const struct nfp_devlink_param_u8_arg nfp_devlink_u8_args[] = {
	[DEVLINK_PARAM_GENERIC_ID_FW_LOAD_POLICY] = {
		.hwinfo_name = "app_fw_from_flash",
		.default_hi_val = NFP_NSP_APP_FW_LOAD_DEFAULT,
		.invalid_dl_val =
			DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_UNKNOWN,
		.hi_to_dl = {
			[NFP_NSP_APP_FW_LOAD_DISK] =
				DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_DISK,
			[NFP_NSP_APP_FW_LOAD_FLASH] =
				DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_FLASH,
			[NFP_NSP_APP_FW_LOAD_PREF] =
				DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_DRIVER,
		},
		.dl_to_hi = {
			[DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_DRIVER] =
				NFP_NSP_APP_FW_LOAD_PREF,
			[DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_FLASH] =
				NFP_NSP_APP_FW_LOAD_FLASH,
			[DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_DISK] =
				NFP_NSP_APP_FW_LOAD_DISK,
		},
		.max_dl_val = DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_DISK,
		.max_hi_val = NFP_NSP_APP_FW_LOAD_PREF,
	},
	[DEVLINK_PARAM_GENERIC_ID_RESET_DEV_ON_DRV_PROBE] = {
		.hwinfo_name = "abi_drv_reset",
		.default_hi_val = NFP_NSP_DRV_RESET_DEFAULT,
		.invalid_dl_val =
			DEVLINK_PARAM_RESET_DEV_ON_DRV_PROBE_VALUE_UNKNOWN,
		.hi_to_dl = {
			[NFP_NSP_DRV_RESET_ALWAYS] =
				DEVLINK_PARAM_RESET_DEV_ON_DRV_PROBE_VALUE_ALWAYS,
			[NFP_NSP_DRV_RESET_NEVER] =
				DEVLINK_PARAM_RESET_DEV_ON_DRV_PROBE_VALUE_NEVER,
			[NFP_NSP_DRV_RESET_DISK] =
				DEVLINK_PARAM_RESET_DEV_ON_DRV_PROBE_VALUE_DISK,
		},
		.dl_to_hi = {
			[DEVLINK_PARAM_RESET_DEV_ON_DRV_PROBE_VALUE_ALWAYS] =
				NFP_NSP_DRV_RESET_ALWAYS,
			[DEVLINK_PARAM_RESET_DEV_ON_DRV_PROBE_VALUE_NEVER] =
				NFP_NSP_DRV_RESET_NEVER,
			[DEVLINK_PARAM_RESET_DEV_ON_DRV_PROBE_VALUE_DISK] =
				NFP_NSP_DRV_RESET_DISK,
		},
		.max_dl_val = DEVLINK_PARAM_RESET_DEV_ON_DRV_PROBE_VALUE_DISK,
		.max_hi_val = NFP_NSP_DRV_RESET_NEVER,
	}
};

static int
nfp_devlink_param_u8_get(struct devlink *devlink, u32 id,
			 struct devlink_param_gset_ctx *ctx)
{
	const struct nfp_devlink_param_u8_arg *arg;
	struct nfp_pf *pf = devlink_priv(devlink);
	struct nfp_nsp *nsp;
	char hwinfo[32];
	long value;
	int err;

	if (id >= ARRAY_SIZE(nfp_devlink_u8_args))
		return -EOPNOTSUPP;

	arg = &nfp_devlink_u8_args[id];

	nsp = nfp_nsp_open(pf->cpp);
	if (IS_ERR(nsp)) {
		err = PTR_ERR(nsp);
		nfp_warn(pf->cpp, "can't access NSP: %d\n", err);
		return err;
	}

	snprintf(hwinfo, sizeof(hwinfo), arg->hwinfo_name);
	err = nfp_nsp_hwinfo_lookup_optional(nsp, hwinfo, sizeof(hwinfo),
					     arg->default_hi_val);
	if (err) {
		nfp_warn(pf->cpp, "HWinfo lookup failed: %d\n", err);
		goto exit_close_nsp;
	}

	err = kstrtol(hwinfo, 0, &value);
	if (err || value < 0 || value > arg->max_hi_val) {
		nfp_warn(pf->cpp, "HWinfo '%s' value %li invalid\n",
			 arg->hwinfo_name, value);

		if (arg->invalid_dl_val >= 0)
			ctx->val.vu8 = arg->invalid_dl_val;
		else
			err = arg->invalid_dl_val;

		goto exit_close_nsp;
	}

	ctx->val.vu8 = arg->hi_to_dl[value];

exit_close_nsp:
	nfp_nsp_close(nsp);
	return err;
}

static int
nfp_devlink_param_u8_set(struct devlink *devlink, u32 id,
			 struct devlink_param_gset_ctx *ctx)
{
	const struct nfp_devlink_param_u8_arg *arg;
	struct nfp_pf *pf = devlink_priv(devlink);
	struct nfp_nsp *nsp;
	char hwinfo[32];
	int err;

	if (id >= ARRAY_SIZE(nfp_devlink_u8_args))
		return -EOPNOTSUPP;

	arg = &nfp_devlink_u8_args[id];

	nsp = nfp_nsp_open(pf->cpp);
	if (IS_ERR(nsp)) {
		err = PTR_ERR(nsp);
		nfp_warn(pf->cpp, "can't access NSP: %d\n", err);
		return err;
	}

	/* Note the value has already been validated. */
	snprintf(hwinfo, sizeof(hwinfo), "%s=%u",
		 arg->hwinfo_name, arg->dl_to_hi[ctx->val.vu8]);
	err = nfp_nsp_hwinfo_set(nsp, hwinfo, sizeof(hwinfo));
	if (err) {
		nfp_warn(pf->cpp, "HWinfo set failed: %d\n", err);
		goto exit_close_nsp;
	}

exit_close_nsp:
	nfp_nsp_close(nsp);
	return err;
}

static int
nfp_devlink_param_u8_validate(struct devlink *devlink, u32 id,
			      union devlink_param_value val,
			      struct netlink_ext_ack *extack)
{
	const struct nfp_devlink_param_u8_arg *arg;

	if (id >= ARRAY_SIZE(nfp_devlink_u8_args))
		return -EOPNOTSUPP;

	arg = &nfp_devlink_u8_args[id];

	if (val.vu8 > arg->max_dl_val) {
		NL_SET_ERR_MSG_MOD(extack, "parameter out of range");
		return -EINVAL;
	}

	if (val.vu8 == arg->invalid_dl_val) {
		NL_SET_ERR_MSG_MOD(extack, "unknown/invalid value specified");
		return -EINVAL;
	}

	return 0;
}

static const struct devlink_param nfp_devlink_params[] = {
	DEVLINK_PARAM_GENERIC(FW_LOAD_POLICY,
			      BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			      nfp_devlink_param_u8_get,
			      nfp_devlink_param_u8_set,
			      nfp_devlink_param_u8_validate),
	DEVLINK_PARAM_GENERIC(RESET_DEV_ON_DRV_PROBE,
			      BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			      nfp_devlink_param_u8_get,
			      nfp_devlink_param_u8_set,
			      nfp_devlink_param_u8_validate),
};

static int nfp_devlink_supports_params(struct nfp_pf *pf)
{
	struct nfp_nsp *nsp;
	bool supported;
	int err;

	nsp = nfp_nsp_open(pf->cpp);
	if (IS_ERR(nsp)) {
		err = PTR_ERR(nsp);
		dev_err(&pf->pdev->dev, "Failed to access the NSP: %d\n", err);
		return err;
	}

	supported = nfp_nsp_has_hwinfo_lookup(nsp) &&
		    nfp_nsp_has_hwinfo_set(nsp);

	nfp_nsp_close(nsp);
	return supported;
}

int nfp_devlink_params_register(struct nfp_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	int err;

	err = nfp_devlink_supports_params(pf);
	if (err <= 0)
		return err;

	return devlink_params_register(devlink, nfp_devlink_params,
				       ARRAY_SIZE(nfp_devlink_params));
}

void nfp_devlink_params_unregister(struct nfp_pf *pf)
{
	int err;

	err = nfp_devlink_supports_params(pf);
	if (err <= 0)
		return;

	devlink_params_unregister(priv_to_devlink(pf), nfp_devlink_params,
				  ARRAY_SIZE(nfp_devlink_params));
}
