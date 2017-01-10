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

/* PhyID -> Status Mapping in DDI_PHY_GEN_STATUS
    0    GFX0L (3:0),                  (27:24),
    1    GFX0H (7:4),                  (31:28),
    2    GFX1L (3:0),                  (19:16),
    3    GFX1H (7:4),                  (23:20),
    4    DDIL   (3:0),                   (11: 8),
    5    DDIH  (7:4),                   (15:12),
    6    DDI2L (3:0),                   ( 3: 0),
    7    DDI2H (7:4),                   ( 7: 4),
*/
#define DDI_PHY_GEN_STATUS_VAL(phyID)   (1 << ((3 - ((phyID & 0x07)/2))*8 + (phyID & 0x01)*4))
#define IS_PHY_ID_USED_BY_PLL(PhyID)    (((0xF3 & (1 << PhyID)) & 0xFF) ? true : false)


int cz_phm_set_asic_block_gating(struct pp_hwmgr *hwmgr, enum PHM_AsicBlock block, enum PHM_ClockGateSetting gating)
{
	int ret = 0;

	switch (block) {
	case PHM_AsicBlock_UVD_MVC:
	case PHM_AsicBlock_UVD:
	case PHM_AsicBlock_UVD_HD:
	case PHM_AsicBlock_UVD_SD:
		if (gating == PHM_ClockGateSetting_StaticOff)
			ret = cz_dpm_powerdown_uvd(hwmgr);
		else
			ret = cz_dpm_powerup_uvd(hwmgr);
		break;
	case PHM_AsicBlock_GFX:
	default:
		break;
	}

	return ret;
}


bool cz_phm_is_safe_for_asic_block(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *state, enum PHM_AsicBlock block)
{
	return true;
}


int cz_phm_enable_disable_gfx_power_gating(struct pp_hwmgr *hwmgr, bool enable)
{
	return 0;
}

int cz_phm_smu_power_up_down_pcie(struct pp_hwmgr *hwmgr, uint32_t target, bool up, uint32_t args)
{
	/* TODO */
	return 0;
}

int cz_phm_initialize_display_phy_access(struct pp_hwmgr *hwmgr, bool initialize, bool accesshw)
{
	/* TODO */
	return 0;
}

int cz_phm_get_display_phy_access_info(struct pp_hwmgr *hwmgr)
{
	/* TODO */
	return 0;
}

int cz_phm_gate_unused_display_phys(struct pp_hwmgr *hwmgr)
{
	/* TODO */
	return 0;
}

int cz_phm_ungate_all_display_phys(struct pp_hwmgr *hwmgr)
{
	/* TODO */
	return 0;
}

static int cz_tf_uvd_power_gating_initialize(struct pp_hwmgr *hwmgr, void *pInput, void *pOutput, void *pStorage, int Result)
{
	return 0;
}

static int cz_tf_vce_power_gating_initialize(struct pp_hwmgr *hwmgr, void *pInput, void *pOutput, void *pStorage, int Result)
{
	return 0;
}

int cz_enable_disable_uvd_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	uint32_t dpm_features = 0;

	if (enable &&
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				  PHM_PlatformCaps_UVDDPM)) {
		cz_hwmgr->dpm_flags |= DPMFlags_UVD_Enabled;
		dpm_features |= UVD_DPM_MASK;
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
			    PPSMC_MSG_EnableAllSmuFeatures, dpm_features);
	} else {
		dpm_features |= UVD_DPM_MASK;
		cz_hwmgr->dpm_flags &= ~DPMFlags_UVD_Enabled;
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
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
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
			    PPSMC_MSG_EnableAllSmuFeatures, dpm_features);
	} else {
		dpm_features |= VCE_DPM_MASK;
		cz_hwmgr->dpm_flags &= ~DPMFlags_VCE_Enabled;
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
			   PPSMC_MSG_DisableAllSmuFeatures, dpm_features);
	}

	return 0;
}


int cz_dpm_powergate_uvd(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	if (cz_hwmgr->uvd_power_gated == bgate)
		return 0;

	cz_hwmgr->uvd_power_gated = bgate;

	if (bgate) {
		cgs_set_clockgating_state(hwmgr->device,
						AMD_IP_BLOCK_TYPE_UVD,
						AMD_CG_STATE_GATE);
		cgs_set_powergating_state(hwmgr->device,
						AMD_IP_BLOCK_TYPE_UVD,
						AMD_PG_STATE_GATE);
		cz_dpm_update_uvd_dpm(hwmgr, true);
		cz_dpm_powerdown_uvd(hwmgr);
	} else {
		cz_dpm_powerup_uvd(hwmgr);
		cgs_set_powergating_state(hwmgr->device,
						AMD_IP_BLOCK_TYPE_UVD,
						AMD_CG_STATE_UNGATE);
		cgs_set_clockgating_state(hwmgr->device,
						AMD_IP_BLOCK_TYPE_UVD,
						AMD_PG_STATE_UNGATE);
		cz_dpm_update_uvd_dpm(hwmgr, false);
	}

	return 0;
}

int cz_dpm_powergate_vce(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_VCEPowerGating)) {
		if (cz_hwmgr->vce_power_gated != bgate) {
			if (bgate) {
				cgs_set_clockgating_state(
							hwmgr->device,
							AMD_IP_BLOCK_TYPE_VCE,
							AMD_CG_STATE_GATE);
				cgs_set_powergating_state(
							hwmgr->device,
							AMD_IP_BLOCK_TYPE_VCE,
							AMD_PG_STATE_GATE);
				cz_enable_disable_vce_dpm(hwmgr, false);
				cz_dpm_powerdown_vce(hwmgr);
				cz_hwmgr->vce_power_gated = true;
			} else {
				cz_dpm_powerup_vce(hwmgr);
				cz_hwmgr->vce_power_gated = false;
				cgs_set_powergating_state(
							hwmgr->device,
							AMD_IP_BLOCK_TYPE_VCE,
							AMD_CG_STATE_UNGATE);
				cgs_set_clockgating_state(
							hwmgr->device,
							AMD_IP_BLOCK_TYPE_VCE,
							AMD_PG_STATE_UNGATE);
				cz_dpm_update_vce_dpm(hwmgr);
				cz_enable_disable_vce_dpm(hwmgr, true);
				return 0;
			}
		}
	} else {
		cz_hwmgr->vce_power_gated = bgate;
		cz_dpm_update_vce_dpm(hwmgr);
		cz_enable_disable_vce_dpm(hwmgr, !bgate);
		return 0;
	}

	if (!cz_hwmgr->vce_power_gated)
		cz_dpm_update_vce_dpm(hwmgr);

	return 0;
}


static const struct phm_master_table_item cz_enable_clock_power_gatings_list[] = {
	/*we don't need an exit table here, because there is only D3 cold on Kv*/
	{ phm_cf_want_uvd_power_gating, cz_tf_uvd_power_gating_initialize },
	{ phm_cf_want_vce_power_gating, cz_tf_vce_power_gating_initialize },
	/* to do { NULL, cz_tf_xdma_power_gating_enable }, */
	{ NULL, NULL }
};

const struct phm_master_table_header cz_phm_enable_clock_power_gatings_master = {
	0,
	PHM_MasterTableFlag_None,
	cz_enable_clock_power_gatings_list
};
