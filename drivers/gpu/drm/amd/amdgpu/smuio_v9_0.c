/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#include "smuio_v9_0.h"
#include "smuio/smuio_9_0_offset.h"
#include "smuio/smuio_9_0_sh_mask.h"

static u32 smuio_v9_0_get_rom_index_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(SMUIO, 0, mmROM_INDEX);
}

static u32 smuio_v9_0_get_rom_data_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(SMUIO, 0, mmROM_DATA);
}

static void smuio_v9_0_update_rom_clock_gating(struct amdgpu_device *adev, bool enable)
{
	u32 def, data;

	/* enable/disable ROM CG is not supported on APU */
	if (adev->flags & AMD_IS_APU)
		return;

	def = data = RREG32_SOC15(SMUIO, 0, mmCGTT_ROM_CLK_CTRL0);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_ROM_MGCG))
		data &= ~(CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0_MASK |
			CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE1_MASK);
	else
		data |= CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0_MASK |
			CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE1_MASK;

	if (def != data)
		WREG32_SOC15(SMUIO, 0, mmCGTT_ROM_CLK_CTRL0, data);
}

static void smuio_v9_0_get_clock_gating_state(struct amdgpu_device *adev, u64 *flags)
{
	u32 data;

	/* CGTT_ROM_CLK_CTRL0 is not available for APUs */
	if (adev->flags & AMD_IS_APU)
		return;

	data = RREG32_SOC15(SMUIO, 0, mmCGTT_ROM_CLK_CTRL0);
	if (!(data & CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0_MASK))
		*flags |= AMD_CG_SUPPORT_ROM_MGCG;
}

const struct amdgpu_smuio_funcs smuio_v9_0_funcs = {
	.get_rom_index_offset = smuio_v9_0_get_rom_index_offset,
	.get_rom_data_offset = smuio_v9_0_get_rom_data_offset,
	.update_rom_clock_gating = smuio_v9_0_update_rom_clock_gating,
	.get_clock_gating_state = smuio_v9_0_get_clock_gating_state,
};
