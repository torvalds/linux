/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#include "df_v4_3.h"

#include "df/df_4_3_offset.h"
#include "df/df_4_3_sh_mask.h"

static bool df_v4_3_query_ras_poison_mode(struct amdgpu_device *adev)
{
	uint32_t hw_assert_msklo, hw_assert_mskhi;
	uint32_t v0, v1, v28, v31;

	hw_assert_msklo = RREG32_SOC15(DF, 0,
				regDF_CS_UMC_AON0_HardwareAssertMaskLow);
	hw_assert_mskhi = RREG32_SOC15(DF, 0,
				regDF_NCS_PG0_HardwareAssertMaskHigh);

	v0 = REG_GET_FIELD(hw_assert_msklo,
		DF_CS_UMC_AON0_HardwareAssertMaskLow, HWAssertMsk0);
	v1 = REG_GET_FIELD(hw_assert_msklo,
		DF_CS_UMC_AON0_HardwareAssertMaskLow, HWAssertMsk1);
	v28 = REG_GET_FIELD(hw_assert_mskhi,
		DF_NCS_PG0_HardwareAssertMaskHigh, HWAssertMsk28);
	v31 = REG_GET_FIELD(hw_assert_mskhi,
		DF_NCS_PG0_HardwareAssertMaskHigh, HWAssertMsk31);

	if (v0 && v1 && v28 && v31)
		return true;
	else if (!v0 && !v1 && !v28 && !v31)
		return false;
	else {
		dev_warn(adev->dev, "DF poison setting is inconsistent(%d:%d:%d:%d)!\n",
				v0, v1, v28, v31);
		return false;
	}
}

const struct amdgpu_df_funcs df_v4_3_funcs = {
	.query_ras_poison_mode = df_v4_3_query_ras_poison_mode,
};
