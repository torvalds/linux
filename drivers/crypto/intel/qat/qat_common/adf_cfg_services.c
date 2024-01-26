// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */

#include <linux/export.h>
#include <linux/pci.h>
#include <linux/string.h>
#include "adf_cfg.h"
#include "adf_cfg_services.h"
#include "adf_cfg_strings.h"

const char *const adf_cfg_services[] = {
	[SVC_CY] = ADF_CFG_CY,
	[SVC_CY2] = ADF_CFG_ASYM_SYM,
	[SVC_DC] = ADF_CFG_DC,
	[SVC_DCC] = ADF_CFG_DCC,
	[SVC_SYM] = ADF_CFG_SYM,
	[SVC_ASYM] = ADF_CFG_ASYM,
	[SVC_DC_ASYM] = ADF_CFG_DC_ASYM,
	[SVC_ASYM_DC] = ADF_CFG_ASYM_DC,
	[SVC_DC_SYM] = ADF_CFG_DC_SYM,
	[SVC_SYM_DC] = ADF_CFG_SYM_DC,
};
EXPORT_SYMBOL_GPL(adf_cfg_services);

int adf_get_service_enabled(struct adf_accel_dev *accel_dev)
{
	char services[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = {0};
	int ret;

	ret = adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC,
				      ADF_SERVICES_ENABLED, services);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			ADF_SERVICES_ENABLED " param not found\n");
		return ret;
	}

	ret = match_string(adf_cfg_services, ARRAY_SIZE(adf_cfg_services),
			   services);
	if (ret < 0)
		dev_err(&GET_DEV(accel_dev),
			"Invalid value of " ADF_SERVICES_ENABLED " param: %s\n",
			services);

	return ret;
}
EXPORT_SYMBOL_GPL(adf_get_service_enabled);
