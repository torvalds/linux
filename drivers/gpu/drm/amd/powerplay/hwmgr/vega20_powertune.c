/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "hwmgr.h"
#include "vega20_hwmgr.h"
#include "vega20_powertune.h"
#include "vega20_smumgr.h"
#include "vega20_ppsmc.h"
#include "vega20_inc.h"
#include "pp_debug.h"

int vega20_set_power_limit(struct pp_hwmgr *hwmgr, uint32_t n)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);

	if (data->smu_features[GNLD_PPT].enabled)
		return smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetPptLimit, n);

	return 0;
}

int vega20_validate_power_level_request(struct pp_hwmgr *hwmgr,
		uint32_t tdp_percentage_adjustment, uint32_t tdp_absolute_value_adjustment)
{
	return (tdp_percentage_adjustment > hwmgr->platform_descriptor.TDPLimit) ? -1 : 0;
}

static int vega20_set_overdrive_target_percentage(struct pp_hwmgr *hwmgr,
		uint32_t adjust_percent)
{
	return smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_OverDriveSetPercentage, adjust_percent);
}

int vega20_power_control_set_level(struct pp_hwmgr *hwmgr)
{
	int adjust_percent, result = 0;

	if (PP_CAP(PHM_PlatformCaps_PowerContainment)) {
		adjust_percent =
				hwmgr->platform_descriptor.TDPAdjustmentPolarity ?
				hwmgr->platform_descriptor.TDPAdjustment :
				(-1 * hwmgr->platform_descriptor.TDPAdjustment);
		result = vega20_set_overdrive_target_percentage(hwmgr,
				(uint32_t)adjust_percent);
	}
	return result;
}
