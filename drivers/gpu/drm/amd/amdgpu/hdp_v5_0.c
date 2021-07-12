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
#include "hdp_v5_0.h"

#include "hdp/hdp_5_0_0_offset.h"
#include "hdp/hdp_5_0_0_sh_mask.h"
#include <uapi/linux/kfd_ioctl.h>

static void hdp_v5_0_flush_hdp(struct amdgpu_device *adev,
				struct amdgpu_ring *ring)
{
	if (!ring || !ring->funcs->emit_wreg)
		WREG32_NO_KIQ((adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL) >> 2, 0);
	else
		amdgpu_ring_emit_wreg(ring, (adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL) >> 2, 0);
}

static void hdp_v5_0_invalidate_hdp(struct amdgpu_device *adev,
				    struct amdgpu_ring *ring)
{
	if (!ring || !ring->funcs->emit_wreg) {
		WREG32_SOC15_NO_KIQ(HDP, 0, mmHDP_READ_CACHE_INVALIDATE, 1);
	} else {
		amdgpu_ring_emit_wreg(ring, SOC15_REG_OFFSET(
					HDP, 0, mmHDP_READ_CACHE_INVALIDATE), 1);
	}
}

static void hdp_v5_0_update_mem_power_gating(struct amdgpu_device *adev,
					  bool enable)
{
	uint32_t hdp_clk_cntl, hdp_clk_cntl1;
	uint32_t hdp_mem_pwr_cntl;

	if (!(adev->cg_flags & (AMD_CG_SUPPORT_HDP_LS |
				AMD_CG_SUPPORT_HDP_DS |
				AMD_CG_SUPPORT_HDP_SD)))
		return;

	hdp_clk_cntl = hdp_clk_cntl1 = RREG32_SOC15(HDP, 0, mmHDP_CLK_CNTL);
	hdp_mem_pwr_cntl = RREG32_SOC15(HDP, 0, mmHDP_MEM_POWER_CTRL);

	/* Before doing clock/power mode switch,
	 * forced on IPH & RC clock */
	hdp_clk_cntl = REG_SET_FIELD(hdp_clk_cntl, HDP_CLK_CNTL,
				     IPH_MEM_CLK_SOFT_OVERRIDE, 1);
	hdp_clk_cntl = REG_SET_FIELD(hdp_clk_cntl, HDP_CLK_CNTL,
				     RC_MEM_CLK_SOFT_OVERRIDE, 1);
	WREG32_SOC15(HDP, 0, mmHDP_CLK_CNTL, hdp_clk_cntl);

	/* HDP 5.0 doesn't support dynamic power mode switch,
	 * disable clock and power gating before any changing */
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 IPH_MEM_POWER_CTRL_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 IPH_MEM_POWER_LS_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 IPH_MEM_POWER_DS_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 IPH_MEM_POWER_SD_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 RC_MEM_POWER_CTRL_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 RC_MEM_POWER_LS_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 RC_MEM_POWER_DS_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 RC_MEM_POWER_SD_EN, 0);
	WREG32_SOC15(HDP, 0, mmHDP_MEM_POWER_CTRL, hdp_mem_pwr_cntl);

	/* Already disabled above. The actions below are for "enabled" only */
	if (enable) {
		/* only one clock gating mode (LS/DS/SD) can be enabled */
		if (adev->cg_flags & AMD_CG_SUPPORT_HDP_LS) {
			hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl,
							 HDP_MEM_POWER_CTRL,
							 IPH_MEM_POWER_LS_EN, 1);
			hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl,
							 HDP_MEM_POWER_CTRL,
							 RC_MEM_POWER_LS_EN, 1);
		} else if (adev->cg_flags & AMD_CG_SUPPORT_HDP_DS) {
			hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl,
							 HDP_MEM_POWER_CTRL,
							 IPH_MEM_POWER_DS_EN, 1);
			hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl,
							 HDP_MEM_POWER_CTRL,
							 RC_MEM_POWER_DS_EN, 1);
		} else if (adev->cg_flags & AMD_CG_SUPPORT_HDP_SD) {
			hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl,
							 HDP_MEM_POWER_CTRL,
							 IPH_MEM_POWER_SD_EN, 1);
			/* RC should not use shut down mode, fallback to ds  or ls if allowed */
			if (adev->cg_flags & AMD_CG_SUPPORT_HDP_DS)
				hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl,
								 HDP_MEM_POWER_CTRL,
								 RC_MEM_POWER_DS_EN, 1);
			else if (adev->cg_flags & AMD_CG_SUPPORT_HDP_LS)
				hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl,
								 HDP_MEM_POWER_CTRL,
								 RC_MEM_POWER_LS_EN, 1);
		}

		/* confirmed that IPH_MEM_POWER_CTRL_EN and RC_MEM_POWER_CTRL_EN have to
		 * be set for SRAM LS/DS/SD */
		if (adev->cg_flags & (AMD_CG_SUPPORT_HDP_LS | AMD_CG_SUPPORT_HDP_DS |
				      AMD_CG_SUPPORT_HDP_SD)) {
			hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
							 IPH_MEM_POWER_CTRL_EN, 1);
			hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
							 RC_MEM_POWER_CTRL_EN, 1);
			WREG32_SOC15(HDP, 0, mmHDP_MEM_POWER_CTRL, hdp_mem_pwr_cntl);
		}
	}

	/* disable IPH & RC clock override after clock/power mode changing */
	hdp_clk_cntl = REG_SET_FIELD(hdp_clk_cntl, HDP_CLK_CNTL,
				     IPH_MEM_CLK_SOFT_OVERRIDE, 0);
	hdp_clk_cntl = REG_SET_FIELD(hdp_clk_cntl, HDP_CLK_CNTL,
				     RC_MEM_CLK_SOFT_OVERRIDE, 0);
	WREG32_SOC15(HDP, 0, mmHDP_CLK_CNTL, hdp_clk_cntl);
}

static void hdp_v5_0_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t hdp_clk_cntl;

	if (!(adev->cg_flags & AMD_CG_SUPPORT_HDP_MGCG))
		return;

	hdp_clk_cntl = RREG32_SOC15(HDP, 0, mmHDP_CLK_CNTL);

	if (enable) {
		hdp_clk_cntl &=
			~(uint32_t)
			(HDP_CLK_CNTL__IPH_MEM_CLK_SOFT_OVERRIDE_MASK |
			 HDP_CLK_CNTL__RC_MEM_CLK_SOFT_OVERRIDE_MASK |
			 HDP_CLK_CNTL__DBUS_CLK_SOFT_OVERRIDE_MASK |
			 HDP_CLK_CNTL__DYN_CLK_SOFT_OVERRIDE_MASK |
			 HDP_CLK_CNTL__XDP_REG_CLK_SOFT_OVERRIDE_MASK |
			 HDP_CLK_CNTL__HDP_REG_CLK_SOFT_OVERRIDE_MASK);
	} else {
		hdp_clk_cntl |= HDP_CLK_CNTL__IPH_MEM_CLK_SOFT_OVERRIDE_MASK |
			HDP_CLK_CNTL__RC_MEM_CLK_SOFT_OVERRIDE_MASK |
			HDP_CLK_CNTL__DBUS_CLK_SOFT_OVERRIDE_MASK |
			HDP_CLK_CNTL__DYN_CLK_SOFT_OVERRIDE_MASK |
			HDP_CLK_CNTL__XDP_REG_CLK_SOFT_OVERRIDE_MASK |
			HDP_CLK_CNTL__HDP_REG_CLK_SOFT_OVERRIDE_MASK;
	}

	WREG32_SOC15(HDP, 0, mmHDP_CLK_CNTL, hdp_clk_cntl);
}

static void hdp_v5_0_update_clock_gating(struct amdgpu_device *adev,
					      bool enable)
{
	hdp_v5_0_update_mem_power_gating(adev, enable);
	hdp_v5_0_update_medium_grain_clock_gating(adev, enable);
}

static void hdp_v5_0_get_clockgating_state(struct amdgpu_device *adev,
					    u32 *flags)
{
	uint32_t tmp;

	/* AMD_CG_SUPPORT_HDP_MGCG */
	tmp = RREG32_SOC15(HDP, 0, mmHDP_CLK_CNTL);
	if (!(tmp & (HDP_CLK_CNTL__IPH_MEM_CLK_SOFT_OVERRIDE_MASK |
		     HDP_CLK_CNTL__RC_MEM_CLK_SOFT_OVERRIDE_MASK |
		     HDP_CLK_CNTL__DBUS_CLK_SOFT_OVERRIDE_MASK |
		     HDP_CLK_CNTL__DYN_CLK_SOFT_OVERRIDE_MASK |
		     HDP_CLK_CNTL__XDP_REG_CLK_SOFT_OVERRIDE_MASK |
		     HDP_CLK_CNTL__HDP_REG_CLK_SOFT_OVERRIDE_MASK)))
		*flags |= AMD_CG_SUPPORT_HDP_MGCG;

	/* AMD_CG_SUPPORT_HDP_LS/DS/SD */
	tmp = RREG32_SOC15(HDP, 0, mmHDP_MEM_POWER_CTRL);
	if (tmp & HDP_MEM_POWER_CTRL__IPH_MEM_POWER_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_HDP_LS;
	else if (tmp & HDP_MEM_POWER_CTRL__IPH_MEM_POWER_DS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_HDP_DS;
	else if (tmp & HDP_MEM_POWER_CTRL__IPH_MEM_POWER_SD_EN_MASK)
		*flags |= AMD_CG_SUPPORT_HDP_SD;
}

static void hdp_v5_0_init_registers(struct amdgpu_device *adev)
{
	u32 tmp;

	tmp = RREG32_SOC15(HDP, 0, mmHDP_MISC_CNTL);
	tmp |= HDP_MISC_CNTL__FLUSH_INVALIDATE_CACHE_MASK;
	WREG32_SOC15(HDP, 0, mmHDP_MISC_CNTL, tmp);
}

const struct amdgpu_hdp_funcs hdp_v5_0_funcs = {
	.flush_hdp = hdp_v5_0_flush_hdp,
	.invalidate_hdp = hdp_v5_0_invalidate_hdp,
	.update_clock_gating = hdp_v5_0_update_clock_gating,
	.get_clock_gating_state = hdp_v5_0_get_clockgating_state,
	.init_registers = hdp_v5_0_init_registers,
};
