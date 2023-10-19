/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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

#include "radeon.h"
#include "trinityd.h"
#include "trinity_dpm.h"
#include "ppsmc.h"

static int trinity_notify_message_to_smu(struct radeon_device *rdev, u32 id)
{
	int i;
	u32 v = 0;

	WREG32(SMC_MESSAGE_0, id);
	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(SMC_RESP_0) != 0)
			break;
		udelay(1);
	}
	v = RREG32(SMC_RESP_0);

	if (v != 1) {
		if (v == 0xFF) {
			DRM_ERROR("SMC failed to handle the message!\n");
			return -EINVAL;
		} else if (v == 0xFE) {
			DRM_ERROR("Unknown SMC message!\n");
			return -EINVAL;
		}
	}

	return 0;
}

int trinity_dpm_bapm_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		return trinity_notify_message_to_smu(rdev, PPSMC_MSG_EnableBAPM);
	else
		return trinity_notify_message_to_smu(rdev, PPSMC_MSG_DisableBAPM);
}

int trinity_dpm_config(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32_SMC(SMU_SCRATCH0, 1);
	else
		WREG32_SMC(SMU_SCRATCH0, 0);

	return trinity_notify_message_to_smu(rdev, PPSMC_MSG_DPM_Config);
}

int trinity_dpm_force_state(struct radeon_device *rdev, u32 n)
{
	WREG32_SMC(SMU_SCRATCH0, n);

	return trinity_notify_message_to_smu(rdev, PPSMC_MSG_DPM_ForceState);
}

int trinity_dpm_n_levels_disabled(struct radeon_device *rdev, u32 n)
{
	WREG32_SMC(SMU_SCRATCH0, n);

	return trinity_notify_message_to_smu(rdev, PPSMC_MSG_DPM_N_LevelsDisabled);
}

int trinity_uvd_dpm_config(struct radeon_device *rdev)
{
	return trinity_notify_message_to_smu(rdev, PPSMC_MSG_UVD_DPM_Config);
}

int trinity_dpm_no_forced_level(struct radeon_device *rdev)
{
	return trinity_notify_message_to_smu(rdev, PPSMC_MSG_NoForcedLevel);
}

int trinity_dce_enable_voltage_adjustment(struct radeon_device *rdev,
					  bool enable)
{
	if (enable)
		return trinity_notify_message_to_smu(rdev, PPSMC_MSG_DCE_AllowVoltageAdjustment);
	else
		return trinity_notify_message_to_smu(rdev, PPSMC_MSG_DCE_RemoveVoltageAdjustment);
}

int trinity_gfx_dynamic_mgpg_config(struct radeon_device *rdev)
{
	return trinity_notify_message_to_smu(rdev, PPSMC_MSG_PG_SIMD_Config);
}

void trinity_acquire_mutex(struct radeon_device *rdev)
{
	int i;

	WREG32(SMC_INT_REQ, 1);
	for (i = 0; i < rdev->usec_timeout; i++) {
		if ((RREG32(SMC_INT_REQ) & 0xffff) == 1)
			break;
		udelay(1);
	}
}

void trinity_release_mutex(struct radeon_device *rdev)
{
	WREG32(SMC_INT_REQ, 0);
}
