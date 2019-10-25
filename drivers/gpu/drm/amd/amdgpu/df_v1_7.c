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
#include "amdgpu.h"
#include "df_v1_7.h"

#include "df/df_1_7_default.h"
#include "df/df_1_7_offset.h"
#include "df/df_1_7_sh_mask.h"

static u32 df_v1_7_channel_number[] = {1, 2, 0, 4, 0, 8, 0, 16, 2};

static void df_v1_7_sw_init(struct amdgpu_device *adev)
{
}

static void df_v1_7_enable_broadcast_mode(struct amdgpu_device *adev,
                                          bool enable)
{
	u32 tmp;

	if (enable) {
		tmp = RREG32_SOC15(DF, 0, mmFabricConfigAccessControl);
		tmp &= ~FabricConfigAccessControl__CfgRegInstAccEn_MASK;
		WREG32_SOC15(DF, 0, mmFabricConfigAccessControl, tmp);
	} else
		WREG32_SOC15(DF, 0, mmFabricConfigAccessControl,
			     mmFabricConfigAccessControl_DEFAULT);
}

static u32 df_v1_7_get_fb_channel_number(struct amdgpu_device *adev)
{
	u32 tmp;

	tmp = RREG32_SOC15(DF, 0, mmDF_CS_AON0_DramBaseAddress0);
	tmp &= DF_CS_AON0_DramBaseAddress0__IntLvNumChan_MASK;
	tmp >>= DF_CS_AON0_DramBaseAddress0__IntLvNumChan__SHIFT;

	return tmp;
}

static u32 df_v1_7_get_hbm_channel_number(struct amdgpu_device *adev)
{
	int fb_channel_number;

	fb_channel_number = adev->df_funcs->get_fb_channel_number(adev);

	return df_v1_7_channel_number[fb_channel_number];
}

static void df_v1_7_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						     bool enable)
{
	u32 tmp;

	/* Put DF on broadcast mode */
	adev->df_funcs->enable_broadcast_mode(adev, true);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_DF_MGCG)) {
		tmp = RREG32_SOC15(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater);
		tmp &= ~DF_PIE_AON0_DfGlobalClkGater__MGCGMode_MASK;
		tmp |= DF_V1_7_MGCG_ENABLE_15_CYCLE_DELAY;
		WREG32_SOC15(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater, tmp);
	} else {
		tmp = RREG32_SOC15(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater);
		tmp &= ~DF_PIE_AON0_DfGlobalClkGater__MGCGMode_MASK;
		tmp |= DF_V1_7_MGCG_DISABLE;
		WREG32_SOC15(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater, tmp);
	}

	/* Exit boradcast mode */
	adev->df_funcs->enable_broadcast_mode(adev, false);
}

static void df_v1_7_get_clockgating_state(struct amdgpu_device *adev,
					  u32 *flags)
{
	u32 tmp;

	/* AMD_CG_SUPPORT_DF_MGCG */
	tmp = RREG32_SOC15(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater);
	if (tmp & DF_V1_7_MGCG_ENABLE_15_CYCLE_DELAY)
		*flags |= AMD_CG_SUPPORT_DF_MGCG;
}

static void df_v1_7_enable_ecc_force_par_wr_rmw(struct amdgpu_device *adev,
						bool enable)
{
	WREG32_FIELD15(DF, 0, DF_CS_AON0_CoherentSlaveModeCtrlA0,
		       ForceParWrRMW, enable);
}

const struct amdgpu_df_funcs df_v1_7_funcs = {
	.sw_init = df_v1_7_sw_init,
	.enable_broadcast_mode = df_v1_7_enable_broadcast_mode,
	.get_fb_channel_number = df_v1_7_get_fb_channel_number,
	.get_hbm_channel_number = df_v1_7_get_hbm_channel_number,
	.update_medium_grain_clock_gating = df_v1_7_update_medium_grain_clock_gating,
	.get_clockgating_state = df_v1_7_get_clockgating_state,
	.enable_ecc_force_par_wr_rmw = df_v1_7_enable_ecc_force_par_wr_rmw,
};
