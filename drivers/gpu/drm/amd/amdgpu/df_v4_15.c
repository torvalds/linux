/*
 * Copyright 2024 Advanced Micro Devices, Inc.
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
#include "df_v4_15.h"

#include "df/df_4_15_offset.h"
#include "df/df_4_15_sh_mask.h"

static void df_v4_15_hw_init(struct amdgpu_device *adev)
{
	if (adev->have_atomics_support) {
		uint32_t tmp;
		uint32_t dis_lcl_proc = (1 <<  1 |
					1 <<  2 |
					1 << 13);

		tmp = RREG32_SOC15(DF, 0, regNCSConfigurationRegister1);
		tmp |= (dis_lcl_proc << NCSConfigurationRegister1__DisIntAtomicsLclProcessing__SHIFT);
		WREG32_SOC15(DF, 0, regNCSConfigurationRegister1, tmp);
	}
}

const struct amdgpu_df_funcs df_v4_15_funcs = {
	.hw_init = df_v4_15_hw_init
};
