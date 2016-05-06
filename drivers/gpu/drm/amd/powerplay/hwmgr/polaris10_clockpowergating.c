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
 */

#include "polaris10_clockpowergating.h"

int polaris10_phm_powerdown_uvd(struct pp_hwmgr *hwmgr)
{
	if (phm_cf_want_uvd_power_gating(hwmgr))
		return smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_UVDPowerOFF);
	return 0;
}

int polaris10_phm_powerup_uvd(struct pp_hwmgr *hwmgr)
{
	if (phm_cf_want_uvd_power_gating(hwmgr)) {
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				  PHM_PlatformCaps_UVDDynamicPowerGating)) {
			return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_UVDPowerON, 1);
		} else {
			return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_UVDPowerON, 0);
		}
	}

	return 0;
}

int polaris10_phm_powerdown_vce(struct pp_hwmgr *hwmgr)
{
	if (phm_cf_want_vce_power_gating(hwmgr))
		return smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_VCEPowerOFF);
	return 0;
}

int polaris10_phm_powerup_vce(struct pp_hwmgr *hwmgr)
{
	if (phm_cf_want_vce_power_gating(hwmgr))
		return smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_VCEPowerON);
	return 0;
}

int polaris10_phm_powerdown_samu(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SamuPowerGating))
		return smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_SAMPowerOFF);
	return 0;
}

int polaris10_phm_powerup_samu(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SamuPowerGating))
		return smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_SAMPowerON);
	return 0;
}

int polaris10_phm_disable_clock_power_gating(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	data->uvd_power_gated = false;
	data->vce_power_gated = false;
	data->samu_power_gated = false;

	polaris10_phm_powerup_uvd(hwmgr);
	polaris10_phm_powerup_vce(hwmgr);
	polaris10_phm_powerup_samu(hwmgr);

	return 0;
}

int polaris10_phm_powergate_uvd(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	if (data->uvd_power_gated == bgate)
		return 0;

	data->uvd_power_gated = bgate;

	if (bgate) {
		polaris10_update_uvd_dpm(hwmgr, true);
		polaris10_phm_powerdown_uvd(hwmgr);
	} else {
		polaris10_phm_powerup_uvd(hwmgr);
		polaris10_update_uvd_dpm(hwmgr, false);
	}

	return 0;
}

int polaris10_phm_powergate_vce(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	if (data->vce_power_gated == bgate)
		return 0;

	data->vce_power_gated = bgate;

	if (bgate)
		polaris10_phm_powerdown_vce(hwmgr);
	else
		polaris10_phm_powerup_vce(hwmgr);

	return 0;
}

int polaris10_phm_powergate_samu(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	if (data->samu_power_gated == bgate)
		return 0;

	data->samu_power_gated = bgate;

	if (bgate) {
		polaris10_update_samu_dpm(hwmgr, true);
		polaris10_phm_powerdown_samu(hwmgr);
	} else {
		polaris10_phm_powerup_samu(hwmgr);
		polaris10_update_samu_dpm(hwmgr, false);
	}

	return 0;
}

int polaris10_phm_update_clock_gatings(struct pp_hwmgr *hwmgr,
					const uint32_t *msg_id)
{
	PPSMC_Msg msg;
	uint32_t value;

	switch ((*msg_id & PP_GROUP_MASK) >> PP_GROUP_SHIFT) {
	case PP_GROUP_GFX:
		switch ((*msg_id & PP_BLOCK_MASK) >> PP_BLOCK_SHIFT) {
		case PP_BLOCK_GFX_CG:
			if (PP_STATE_SUPPORT_CG & *msg_id) {
				msg = ((*msg_id & PP_STATE_MASK) & PP_STATE_CG) ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_GFX_CGCG_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			if (PP_STATE_SUPPORT_LS & *msg_id) {
				msg = (*msg_id & PP_STATE_MASK) & PP_STATE_LS
					? PPSMC_MSG_EnableClockGatingFeature
					: PPSMC_MSG_DisableClockGatingFeature;
				value = CG_GFX_CGLS_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			break;

		case PP_BLOCK_GFX_3D:
			if (PP_STATE_SUPPORT_CG & *msg_id) {
				msg = ((*msg_id & PP_STATE_MASK) & PP_STATE_CG) ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_GFX_3DCG_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}

			if  (PP_STATE_SUPPORT_LS & *msg_id) {
				msg = (*msg_id & PP_STATE_MASK) & PP_STATE_LS ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_GFX_3DLS_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			break;

		case PP_BLOCK_GFX_RLC:
			if (PP_STATE_SUPPORT_LS & *msg_id) {
				msg = (*msg_id & PP_STATE_MASK) & PP_STATE_LS ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_GFX_RLC_LS_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			break;

		case PP_BLOCK_GFX_CP:
			if (PP_STATE_SUPPORT_LS & *msg_id) {
				msg = (*msg_id & PP_STATE_MASK) & PP_STATE_LS ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_GFX_CP_LS_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			break;

		case PP_BLOCK_GFX_MG:
			if (PP_STATE_SUPPORT_CG & *msg_id) {
				msg = ((*msg_id & PP_STATE_MASK) & PP_STATE_CG)	?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = (CG_CPF_MGCG_MASK | CG_RLC_MGCG_MASK |
						CG_GFX_OTHERS_MGCG_MASK);

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			break;

		default:
			return -1;
		}
		break;

	case PP_GROUP_SYS:
		switch ((*msg_id & PP_BLOCK_MASK) >> PP_BLOCK_SHIFT) {
		case PP_BLOCK_SYS_BIF:
			if (PP_STATE_SUPPORT_CG & *msg_id) {
				msg = (*msg_id & PP_STATE_MASK) & PP_STATE_CG ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_SYS_BIF_MGCG_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			if  (PP_STATE_SUPPORT_LS & *msg_id) {
				msg = (*msg_id & PP_STATE_MASK) & PP_STATE_LS ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_SYS_BIF_MGLS_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			break;

		case PP_BLOCK_SYS_MC:
			if (PP_STATE_SUPPORT_CG & *msg_id) {
				msg = ((*msg_id & PP_STATE_MASK) & PP_STATE_CG)	?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_SYS_MC_MGCG_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}

			if (PP_STATE_SUPPORT_LS & *msg_id) {
				msg = (*msg_id & PP_STATE_MASK) & PP_STATE_LS ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_SYS_MC_MGLS_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			break;

		case PP_BLOCK_SYS_DRM:
			if (PP_STATE_SUPPORT_CG & *msg_id) {
				msg = (*msg_id & PP_STATE_MASK) & PP_STATE_CG ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_SYS_DRM_MGCG_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			if (PP_STATE_SUPPORT_LS & *msg_id) {
				msg = (*msg_id & PP_STATE_MASK) & PP_STATE_LS ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_SYS_DRM_MGLS_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			break;

		case PP_BLOCK_SYS_HDP:
			if (PP_STATE_SUPPORT_CG & *msg_id) {
				msg = ((*msg_id & PP_STATE_MASK) & PP_STATE_CG) ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_SYS_HDP_MGCG_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}

			if (PP_STATE_SUPPORT_LS & *msg_id) {
				msg = (*msg_id & PP_STATE_MASK) & PP_STATE_LS ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_SYS_HDP_MGLS_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			break;

		case PP_BLOCK_SYS_SDMA:
			if (PP_STATE_SUPPORT_CG & *msg_id) {
				msg = ((*msg_id & PP_STATE_MASK) & PP_STATE_CG)	?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_SYS_SDMA_MGCG_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}

			if (PP_STATE_SUPPORT_LS & *msg_id) {
				msg = (*msg_id & PP_STATE_MASK) & PP_STATE_LS ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_SYS_SDMA_MGLS_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			break;

		case PP_BLOCK_SYS_ROM:
			if (PP_STATE_SUPPORT_CG & *msg_id) {
				msg = ((*msg_id & PP_STATE_MASK) & PP_STATE_CG) ?
						PPSMC_MSG_EnableClockGatingFeature :
						PPSMC_MSG_DisableClockGatingFeature;
				value = CG_SYS_ROM_MASK;

				if (smum_send_msg_to_smc_with_parameter(
						hwmgr->smumgr, msg, value))
					return -1;
			}
			break;

		default:
			return -1;

		}
		break;

	default:
		return -1;

	}

	return 0;
}

/* This function is for Polaris11 only for now,
 * Powerplay will only control the static per CU Power Gating.
 * Dynamic per CU Power Gating will be done in gfx.
 */
int polaris10_phm_enable_per_cu_power_gating(struct pp_hwmgr *hwmgr, bool enable)
{
	struct cgs_system_info sys_info = {0};
	uint32_t active_cus;
	int result;

	sys_info.size = sizeof(struct cgs_system_info);
	sys_info.info_id = CGS_SYSTEM_INFO_GFX_CU_INFO;

	result = cgs_query_system_info(hwmgr->device, &sys_info);

	if (result)
		return -EINVAL;
	else
		active_cus = sys_info.value;

	if (enable)
		return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_GFX_CU_PG_ENABLE, active_cus);
	else
		return smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_GFX_CU_PG_DISABLE);
}
