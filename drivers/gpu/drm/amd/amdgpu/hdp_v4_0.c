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
#include "amdgpu_atombios.h"
#include "hdp_v4_0.h"
#include "amdgpu_ras.h"

#include "hdp/hdp_4_0_offset.h"
#include "hdp/hdp_4_0_sh_mask.h"
#include <uapi/linux/kfd_ioctl.h>

/* for Vega20 register name change */
#define mmHDP_MEM_POWER_CTRL    0x00d4
#define HDP_MEM_POWER_CTRL__IPH_MEM_POWER_CTRL_EN_MASK  0x00000001L
#define HDP_MEM_POWER_CTRL__IPH_MEM_POWER_LS_EN_MASK    0x00000002L
#define HDP_MEM_POWER_CTRL__RC_MEM_POWER_CTRL_EN_MASK   0x00010000L
#define HDP_MEM_POWER_CTRL__RC_MEM_POWER_LS_EN_MASK     0x00020000L
#define mmHDP_MEM_POWER_CTRL_BASE_IDX   0

static void hdp_v4_0_flush_hdp(struct amdgpu_device *adev,
				struct amdgpu_ring *ring)
{
	if (!ring || !ring->funcs->emit_wreg)
		WREG32_NO_KIQ((adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL) >> 2, 0);
	else
		amdgpu_ring_emit_wreg(ring, (adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL) >> 2, 0);
}

static void hdp_v4_0_invalidate_hdp(struct amdgpu_device *adev,
				    struct amdgpu_ring *ring)
{
	if (adev->ip_versions[HDP_HWIP][0] == IP_VERSION(4, 4, 0))
		return;

	if (!ring || !ring->funcs->emit_wreg)
		WREG32_SOC15_NO_KIQ(HDP, 0, mmHDP_READ_CACHE_INVALIDATE, 1);
	else
		amdgpu_ring_emit_wreg(ring, SOC15_REG_OFFSET(
			HDP, 0, mmHDP_READ_CACHE_INVALIDATE), 1);
}

static void hdp_v4_0_query_ras_error_count(struct amdgpu_device *adev,
					   void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;

	err_data->ue_count = 0;
	err_data->ce_count = 0;

	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__HDP))
		return;

	/* HDP SRAM errors are uncorrectable ones (i.e. fatal errors) */
	err_data->ue_count += RREG32_SOC15(HDP, 0, mmHDP_EDC_CNT);
};

static void hdp_v4_0_reset_ras_error_count(struct amdgpu_device *adev)
{
	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__HDP))
		return;

	if (adev->ip_versions[HDP_HWIP][0] >= IP_VERSION(4, 4, 0))
		WREG32_SOC15(HDP, 0, mmHDP_EDC_CNT, 0);
	else
		/*read back hdp ras counter to reset it to 0 */
		RREG32_SOC15(HDP, 0, mmHDP_EDC_CNT);
}

static void hdp_v4_0_update_clock_gating(struct amdgpu_device *adev,
					 bool enable)
{
	uint32_t def, data;

	if (adev->ip_versions[HDP_HWIP][0] == IP_VERSION(4, 0, 0) ||
	    adev->ip_versions[HDP_HWIP][0] == IP_VERSION(4, 0, 1) ||
	    adev->ip_versions[HDP_HWIP][0] == IP_VERSION(4, 1, 1) ||
	    adev->ip_versions[HDP_HWIP][0] == IP_VERSION(4, 1, 0)) {
		def = data = RREG32(SOC15_REG_OFFSET(HDP, 0, mmHDP_MEM_POWER_LS));

		if (enable && (adev->cg_flags & AMD_CG_SUPPORT_HDP_LS))
			data |= HDP_MEM_POWER_LS__LS_ENABLE_MASK;
		else
			data &= ~HDP_MEM_POWER_LS__LS_ENABLE_MASK;

		if (def != data)
			WREG32(SOC15_REG_OFFSET(HDP, 0, mmHDP_MEM_POWER_LS), data);
	} else {
		def = data = RREG32(SOC15_REG_OFFSET(HDP, 0, mmHDP_MEM_POWER_CTRL));

		if (enable && (adev->cg_flags & AMD_CG_SUPPORT_HDP_LS))
			data |= HDP_MEM_POWER_CTRL__IPH_MEM_POWER_CTRL_EN_MASK |
				HDP_MEM_POWER_CTRL__IPH_MEM_POWER_LS_EN_MASK |
				HDP_MEM_POWER_CTRL__RC_MEM_POWER_CTRL_EN_MASK |
				HDP_MEM_POWER_CTRL__RC_MEM_POWER_LS_EN_MASK;
		else
			data &= ~(HDP_MEM_POWER_CTRL__IPH_MEM_POWER_CTRL_EN_MASK |
				  HDP_MEM_POWER_CTRL__IPH_MEM_POWER_LS_EN_MASK |
				  HDP_MEM_POWER_CTRL__RC_MEM_POWER_CTRL_EN_MASK |
				  HDP_MEM_POWER_CTRL__RC_MEM_POWER_LS_EN_MASK);

		if (def != data)
			WREG32(SOC15_REG_OFFSET(HDP, 0, mmHDP_MEM_POWER_CTRL), data);
	}
}

static void hdp_v4_0_get_clockgating_state(struct amdgpu_device *adev,
					    u32 *flags)
{
	int data;

	/* AMD_CG_SUPPORT_HDP_LS */
	data = RREG32(SOC15_REG_OFFSET(HDP, 0, mmHDP_MEM_POWER_LS));
	if (data & HDP_MEM_POWER_LS__LS_ENABLE_MASK)
		*flags |= AMD_CG_SUPPORT_HDP_LS;
}

static void hdp_v4_0_init_registers(struct amdgpu_device *adev)
{
	switch (adev->ip_versions[HDP_HWIP][0]) {
	case IP_VERSION(4, 2, 1):
		WREG32_FIELD15(HDP, 0, HDP_MMHUB_CNTL, HDP_MMHUB_GCC, 1);
		break;
	default:
		break;
	}

	WREG32_FIELD15(HDP, 0, HDP_MISC_CNTL, FLUSH_INVALIDATE_CACHE, 1);

	WREG32_SOC15(HDP, 0, mmHDP_NONSURFACE_BASE, (adev->gmc.vram_start >> 8));
	WREG32_SOC15(HDP, 0, mmHDP_NONSURFACE_BASE_HI, (adev->gmc.vram_start >> 40));
}

struct amdgpu_ras_block_hw_ops hdp_v4_0_ras_hw_ops = {
	.query_ras_error_count = hdp_v4_0_query_ras_error_count,
	.reset_ras_error_count = hdp_v4_0_reset_ras_error_count,
};

struct amdgpu_hdp_ras hdp_v4_0_ras = {
	.ras_block = {
		.ras_comm = {
			.name = "hdp",
			.block = AMDGPU_RAS_BLOCK__HDP,
			.type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE,
		},
		.hw_ops = &hdp_v4_0_ras_hw_ops,
		.ras_fini = amdgpu_hdp_ras_fini,
	},
};

const struct amdgpu_hdp_funcs hdp_v4_0_funcs = {
	.flush_hdp = hdp_v4_0_flush_hdp,
	.invalidate_hdp = hdp_v4_0_invalidate_hdp,
	.update_clock_gating = hdp_v4_0_update_clock_gating,
	.get_clock_gating_state = hdp_v4_0_get_clockgating_state,
	.init_registers = hdp_v4_0_init_registers,
};
