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
#include "df_v3_6.h"

#include "df/df_3_6_default.h"
#include "df/df_3_6_offset.h"
#include "df/df_3_6_sh_mask.h"

static u32 df_v3_6_channel_number[] = {1, 2, 0, 4, 0, 8, 0,
				       16, 32, 0, 0, 0, 2, 4, 8};

static void df_v3_6_init(struct amdgpu_device *adev)
{
}

static void df_v3_6_enable_broadcast_mode(struct amdgpu_device *adev,
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

static u32 df_v3_6_get_fb_channel_number(struct amdgpu_device *adev)
{
	u32 tmp;

	tmp = RREG32_SOC15(DF, 0, mmDF_CS_UMC_AON0_DramBaseAddress0);
	tmp &= DF_CS_UMC_AON0_DramBaseAddress0__IntLvNumChan_MASK;
	tmp >>= DF_CS_UMC_AON0_DramBaseAddress0__IntLvNumChan__SHIFT;

	return tmp;
}

static u32 df_v3_6_get_hbm_channel_number(struct amdgpu_device *adev)
{
	int fb_channel_number;

	fb_channel_number = adev->df_funcs->get_fb_channel_number(adev);
	if (fb_channel_number >= ARRAY_SIZE(df_v3_6_channel_number))
		fb_channel_number = 0;

	return df_v3_6_channel_number[fb_channel_number];
}

static void df_v3_6_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						     bool enable)
{
	u32 tmp;

	if (adev->cg_flags & AMD_CG_SUPPORT_DF_MGCG) {
		/* Put DF on broadcast mode */
		adev->df_funcs->enable_broadcast_mode(adev, true);

		if (enable) {
			tmp = RREG32_SOC15(DF, 0,
					mmDF_PIE_AON0_DfGlobalClkGater);
			tmp &= ~DF_PIE_AON0_DfGlobalClkGater__MGCGMode_MASK;
			tmp |= DF_V3_6_MGCG_ENABLE_15_CYCLE_DELAY;
			WREG32_SOC15(DF, 0,
					mmDF_PIE_AON0_DfGlobalClkGater, tmp);
		} else {
			tmp = RREG32_SOC15(DF, 0,
					mmDF_PIE_AON0_DfGlobalClkGater);
			tmp &= ~DF_PIE_AON0_DfGlobalClkGater__MGCGMode_MASK;
			tmp |= DF_V3_6_MGCG_DISABLE;
			WREG32_SOC15(DF, 0,
					mmDF_PIE_AON0_DfGlobalClkGater, tmp);
		}

		/* Exit broadcast mode */
		adev->df_funcs->enable_broadcast_mode(adev, false);
	}
}

static void df_v3_6_get_clockgating_state(struct amdgpu_device *adev,
					  u32 *flags)
{
	u32 tmp;

	/* AMD_CG_SUPPORT_DF_MGCG */
	tmp = RREG32_SOC15(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater);
	if (tmp & DF_V3_6_MGCG_ENABLE_15_CYCLE_DELAY)
		*flags |= AMD_CG_SUPPORT_DF_MGCG;
}

const struct amdgpu_df_funcs df_v3_6_funcs = {
	.init = df_v3_6_init,
	.enable_broadcast_mode = df_v3_6_enable_broadcast_mode,
	.get_fb_channel_number = df_v3_6_get_fb_channel_number,
	.get_hbm_channel_number = df_v3_6_get_hbm_channel_number,
	.update_medium_grain_clock_gating =
			df_v3_6_update_medium_grain_clock_gating,
	.get_clockgating_state = df_v3_6_get_clockgating_state,
};
