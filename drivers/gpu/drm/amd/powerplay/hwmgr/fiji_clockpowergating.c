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
#include "fiji_clockpowergating.h"
#include "fiji_ppsmc.h"
#include "fiji_hwmgr.h"

int fiji_phm_disable_clock_power_gating(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	data->uvd_power_gated = false;
	data->vce_power_gated = false;
	data->samu_power_gated = false;
	data->acp_power_gated = false;

	return 0;
}

int fiji_phm_powergate_uvd(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if (data->uvd_power_gated == bgate)
		return 0;

	data->uvd_power_gated = bgate;

	if (bgate)
		fiji_update_uvd_dpm(hwmgr, true);
	else
		fiji_update_uvd_dpm(hwmgr, false);

	return 0;
}

int fiji_phm_powergate_vce(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_set_power_state_input states;
	const struct pp_power_state  *pcurrent;
	struct pp_power_state  *requested;

	if (data->vce_power_gated == bgate)
		return 0;

	data->vce_power_gated = bgate;

	pcurrent = hwmgr->current_ps;
	requested = hwmgr->request_ps;

	states.pcurrent_state = &(pcurrent->hardware);
	states.pnew_state = &(requested->hardware);

	fiji_update_vce_dpm(hwmgr, &states);
	fiji_enable_disable_vce_dpm(hwmgr, !bgate);

	return 0;
}

int fiji_phm_powergate_samu(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if (data->samu_power_gated == bgate)
		return 0;

	data->samu_power_gated = bgate;

	if (bgate)
		fiji_update_samu_dpm(hwmgr, true);
	else
		fiji_update_samu_dpm(hwmgr, false);

	return 0;
}

int fiji_phm_powergate_acp(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if (data->acp_power_gated == bgate)
		return 0;

	data->acp_power_gated = bgate;

	if (bgate)
		fiji_update_acp_dpm(hwmgr, true);
	else
		fiji_update_acp_dpm(hwmgr, false);

	return 0;
}
