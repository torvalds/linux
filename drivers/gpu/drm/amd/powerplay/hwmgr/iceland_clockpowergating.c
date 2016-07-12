/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * Author: Huang Rui <ray.huang@amd.com>
 *
 */

#include "hwmgr.h"
#include "iceland_clockpowergating.h"
#include "ppsmc.h"
#include "iceland_hwmgr.h"

int iceland_phm_powerdown_uvd(struct pp_hwmgr *hwmgr)
{
	/* iceland does not have MM hardware block */
	return 0;
}

static int iceland_phm_powerup_uvd(struct pp_hwmgr *hwmgr)
{
	/* iceland does not have MM hardware block */
	return 0;
}

static int iceland_phm_powerdown_vce(struct pp_hwmgr *hwmgr)
{
	/* iceland does not have MM hardware block */
	return 0;
}

static int iceland_phm_powerup_vce(struct pp_hwmgr *hwmgr)
{
	/* iceland does not have MM hardware block */
	return 0;
}

int iceland_phm_set_asic_block_gating(struct pp_hwmgr *hwmgr, enum
		PHM_AsicBlock block, enum PHM_ClockGateSetting gating)
{
	int ret = 0;

	switch (block) {
	case PHM_AsicBlock_UVD_MVC:
	case PHM_AsicBlock_UVD:
	case PHM_AsicBlock_UVD_HD:
	case PHM_AsicBlock_UVD_SD:
		if (gating == PHM_ClockGateSetting_StaticOff)
			ret = iceland_phm_powerdown_uvd(hwmgr);
		else
			ret = iceland_phm_powerup_uvd(hwmgr);
		break;
	case PHM_AsicBlock_GFX:
	default:
		break;
	}

	return ret;
}

int iceland_phm_disable_clock_power_gating(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	data->uvd_power_gated = false;
	data->vce_power_gated = false;

	iceland_phm_powerup_uvd(hwmgr);
	iceland_phm_powerup_vce(hwmgr);

	return 0;
}

int iceland_phm_powergate_uvd(struct pp_hwmgr *hwmgr, bool bgate)
{
	if (bgate) {
		iceland_update_uvd_dpm(hwmgr, true);
		iceland_phm_powerdown_uvd(hwmgr);
	} else {
		iceland_phm_powerup_uvd(hwmgr);
		iceland_update_uvd_dpm(hwmgr, false);
	}

	return 0;
}

int iceland_phm_powergate_vce(struct pp_hwmgr *hwmgr, bool bgate)
{
	if (bgate)
		return iceland_phm_powerdown_vce(hwmgr);
	else
		return iceland_phm_powerup_vce(hwmgr);

	return 0;
}

int iceland_phm_update_clock_gatings(struct pp_hwmgr *hwmgr,
					const uint32_t *msg_id)
{
	/* iceland does not have MM hardware block */
	return 0;
}
