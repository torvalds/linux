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

#include "ellesmere_clockpowergating.h"

int ellesmere_phm_powerdown_uvd(struct pp_hwmgr *hwmgr)
{
	if (phm_cf_want_uvd_power_gating(hwmgr))
		return smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_UVDPowerOFF);
	return 0;
}

int ellesmere_phm_powerup_uvd(struct pp_hwmgr *hwmgr)
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

int ellesmere_phm_powerdown_vce(struct pp_hwmgr *hwmgr)
{
	if (phm_cf_want_vce_power_gating(hwmgr))
		return smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_VCEPowerOFF);
	return 0;
}

int ellesmere_phm_powerup_vce(struct pp_hwmgr *hwmgr)
{
	if (phm_cf_want_vce_power_gating(hwmgr))
		return smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_VCEPowerON);
	return 0;
}

int ellesmere_phm_powerdown_samu(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SamuPowerGating))
		return smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_SAMPowerOFF);
	return 0;
}

int ellesmere_phm_powerup_samu(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SamuPowerGating))
		return smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_SAMPowerON);
	return 0;
}

int ellesmere_phm_disable_clock_power_gating(struct pp_hwmgr *hwmgr)
{
	struct ellesmere_hwmgr *data = (struct ellesmere_hwmgr *)(hwmgr->backend);

	data->uvd_power_gated = false;
	data->vce_power_gated = false;
	data->samu_power_gated = false;

	ellesmere_phm_powerup_uvd(hwmgr);
	ellesmere_phm_powerup_vce(hwmgr);
	ellesmere_phm_powerup_samu(hwmgr);

	return 0;
}

int ellesmere_phm_powergate_uvd(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct ellesmere_hwmgr *data = (struct ellesmere_hwmgr *)(hwmgr->backend);

	if (data->uvd_power_gated == bgate)
		return 0;

	data->uvd_power_gated = bgate;

	if (bgate) {
		ellesmere_update_uvd_dpm(hwmgr, true);
		ellesmere_phm_powerdown_uvd(hwmgr);
	} else {
		ellesmere_phm_powerup_uvd(hwmgr);
		ellesmere_update_uvd_dpm(hwmgr, false);
	}

	return 0;
}

int ellesmere_phm_powergate_vce(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct ellesmere_hwmgr *data = (struct ellesmere_hwmgr *)(hwmgr->backend);

	if (data->vce_power_gated == bgate)
		return 0;

	if (bgate)
		ellesmere_phm_powerdown_vce(hwmgr);
	else
		ellesmere_phm_powerup_vce(hwmgr);

	return 0;
}

int ellesmere_phm_powergate_samu(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct ellesmere_hwmgr *data = (struct ellesmere_hwmgr *)(hwmgr->backend);

	if (data->samu_power_gated == bgate)
		return 0;

	data->samu_power_gated = bgate;

	if (bgate) {
		ellesmere_update_samu_dpm(hwmgr, true);
		ellesmere_phm_powerdown_samu(hwmgr);
	} else {
		ellesmere_phm_powerup_samu(hwmgr);
		ellesmere_update_samu_dpm(hwmgr, false);
	}

	return 0;
}

