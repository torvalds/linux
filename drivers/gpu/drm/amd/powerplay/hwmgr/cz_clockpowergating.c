/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#include "cz_clockpowergating.h"
#include "cz_ppsmc.h"

int cz_enable_disable_uvd_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	uint32_t dpm_features = 0;

	if (enable &&
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				  PHM_PlatformCaps_UVDDPM)) {
		cz_hwmgr->dpm_flags |= DPMFlags_UVD_Enabled;
		dpm_features |= UVD_DPM_MASK;
		smum_send_msg_to_smc_with_parameter(hwmgr,
			    PPSMC_MSG_EnableAllSmuFeatures, dpm_features);
	} else {
		dpm_features |= UVD_DPM_MASK;
		cz_hwmgr->dpm_flags &= ~DPMFlags_UVD_Enabled;
		smum_send_msg_to_smc_with_parameter(hwmgr,
			   PPSMC_MSG_DisableAllSmuFeatures, dpm_features);
	}
	return 0;
}

int cz_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	uint32_t dpm_features = 0;

	if (enable && phm_cap_enabled(
				hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_VCEDPM)) {
		cz_hwmgr->dpm_flags |= DPMFlags_VCE_Enabled;
		dpm_features |= VCE_DPM_MASK;
		smum_send_msg_to_smc_with_parameter(hwmgr,
			    PPSMC_MSG_EnableAllSmuFeatures, dpm_features);
	} else {
		dpm_features |= VCE_DPM_MASK;
		cz_hwmgr->dpm_flags &= ~DPMFlags_VCE_Enabled;
		smum_send_msg_to_smc_with_parameter(hwmgr,
			   PPSMC_MSG_DisableAllSmuFeatures, dpm_features);
	}

	return 0;
}


void cz_dpm_powergate_uvd(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	cz_hwmgr->uvd_power_gated = bgate;

	if (bgate) {
		cgs_set_powergating_state(hwmgr->device,
						AMD_IP_BLOCK_TYPE_UVD,
						AMD_PG_STATE_GATE);
		cgs_set_clockgating_state(hwmgr->device,
						AMD_IP_BLOCK_TYPE_UVD,
						AMD_CG_STATE_GATE);
		cz_dpm_update_uvd_dpm(hwmgr, true);
		cz_dpm_powerdown_uvd(hwmgr);
	} else {
		cz_dpm_powerup_uvd(hwmgr);
		cgs_set_clockgating_state(hwmgr->device,
						AMD_IP_BLOCK_TYPE_UVD,
						AMD_CG_STATE_UNGATE);
		cgs_set_powergating_state(hwmgr->device,
						AMD_IP_BLOCK_TYPE_UVD,
						AMD_PG_STATE_UNGATE);
		cz_dpm_update_uvd_dpm(hwmgr, false);
	}

}

void cz_dpm_powergate_vce(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	if (bgate) {
		cgs_set_powergating_state(
					hwmgr->device,
					AMD_IP_BLOCK_TYPE_VCE,
					AMD_PG_STATE_GATE);
		cgs_set_clockgating_state(
					hwmgr->device,
					AMD_IP_BLOCK_TYPE_VCE,
					AMD_CG_STATE_GATE);
		cz_enable_disable_vce_dpm(hwmgr, false);
		cz_dpm_powerdown_vce(hwmgr);
		cz_hwmgr->vce_power_gated = true;
	} else {
		cz_dpm_powerup_vce(hwmgr);
		cz_hwmgr->vce_power_gated = false;
		cgs_set_clockgating_state(
					hwmgr->device,
					AMD_IP_BLOCK_TYPE_VCE,
					AMD_CG_STATE_UNGATE);
		cgs_set_powergating_state(
					hwmgr->device,
					AMD_IP_BLOCK_TYPE_VCE,
					AMD_PG_STATE_UNGATE);
		cz_dpm_update_vce_dpm(hwmgr);
		cz_enable_disable_vce_dpm(hwmgr, true);
	}
}

