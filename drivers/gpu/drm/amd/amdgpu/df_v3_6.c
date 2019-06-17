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

	/* Put DF on broadcast mode */
	adev->df_funcs->enable_broadcast_mode(adev, true);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_DF_MGCG)) {
		tmp = RREG32_SOC15(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater);
		tmp &= ~DF_PIE_AON0_DfGlobalClkGater__MGCGMode_MASK;
		tmp |= DF_V3_6_MGCG_ENABLE_15_CYCLE_DELAY;
		WREG32_SOC15(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater, tmp);
	} else {
		tmp = RREG32_SOC15(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater);
		tmp &= ~DF_PIE_AON0_DfGlobalClkGater__MGCGMode_MASK;
		tmp |= DF_V3_6_MGCG_DISABLE;
		WREG32_SOC15(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater, tmp);
	}

	/* Exit broadcast mode */
	adev->df_funcs->enable_broadcast_mode(adev, false);
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

/* hold counter assignment per gpu struct */
struct df_v3_6_event_mask {
		struct amdgpu_device gpu;
		uint64_t config_assign_mask[AMDGPU_DF_MAX_COUNTERS];
};

/* get assigned df perfmon ctr as int */
static void df_v3_6_pmc_config_2_cntr(struct amdgpu_device *adev,
				      uint64_t config,
				      int *counter)
{
	struct df_v3_6_event_mask *mask;
	int i;

	mask = container_of(adev, struct df_v3_6_event_mask, gpu);

	for (i = 0; i < AMDGPU_DF_MAX_COUNTERS; i++) {
		if ((config & 0x0FFFFFFUL) == mask->config_assign_mask[i]) {
			*counter = i;
			return;
		}
	}
}

/* get address based on counter assignment */
static void df_v3_6_pmc_get_addr(struct amdgpu_device *adev,
				 uint64_t config,
				 int is_ctrl,
				 uint32_t *lo_base_addr,
				 uint32_t *hi_base_addr)
{

	int target_cntr = -1;

	df_v3_6_pmc_config_2_cntr(adev, config, &target_cntr);

	if (target_cntr < 0)
		return;

	switch (target_cntr) {

	case 0:
		*lo_base_addr = is_ctrl ? smnPerfMonCtlLo0 : smnPerfMonCtrLo0;
		*hi_base_addr = is_ctrl ? smnPerfMonCtlHi0 : smnPerfMonCtrHi0;
		break;
	case 1:
		*lo_base_addr = is_ctrl ? smnPerfMonCtlLo1 : smnPerfMonCtrLo1;
		*hi_base_addr = is_ctrl ? smnPerfMonCtlHi1 : smnPerfMonCtrHi1;
		break;
	case 2:
		*lo_base_addr = is_ctrl ? smnPerfMonCtlLo2 : smnPerfMonCtrLo2;
		*hi_base_addr = is_ctrl ? smnPerfMonCtlHi2 : smnPerfMonCtrHi2;
		break;
	case 3:
		*lo_base_addr = is_ctrl ? smnPerfMonCtlLo3 : smnPerfMonCtrLo3;
		*hi_base_addr = is_ctrl ? smnPerfMonCtlHi3 : smnPerfMonCtrHi3;
		break;

	}

}

/* get read counter address */
static void df_v3_6_pmc_get_read_settings(struct amdgpu_device *adev,
					  uint64_t config,
					  uint32_t *lo_base_addr,
					  uint32_t *hi_base_addr)
{
	df_v3_6_pmc_get_addr(adev, config, 0, lo_base_addr, hi_base_addr);
}

/* get control counter settings i.e. address and values to set */
static int df_v3_6_pmc_get_ctrl_settings(struct amdgpu_device *adev,
					  uint64_t config,
					  uint32_t *lo_base_addr,
					  uint32_t *hi_base_addr,
					  uint32_t *lo_val,
					  uint32_t *hi_val)
{

	uint32_t eventsel, instance, unitmask;
	uint32_t es_5_0, es_13_0, es_13_6, es_13_12, es_11_8, es_7_0;

	df_v3_6_pmc_get_addr(adev, config, 1, lo_base_addr, hi_base_addr);

	if (lo_val == NULL || hi_val == NULL)
		return -EINVAL;

	if ((*lo_base_addr == 0) || (*hi_base_addr == 0)) {
		DRM_ERROR("DF PMC addressing not retrieved! Lo: %x, Hi: %x",
				*lo_base_addr, *hi_base_addr);
		return -ENXIO;
	}

	eventsel = GET_EVENT(config);
	instance = GET_INSTANCE(config);
	unitmask = GET_UNITMASK(config);

	es_5_0 = eventsel & 0x3FUL;
	es_13_6 = instance;
	es_13_0 = (es_13_6 << 6) + es_5_0;
	es_13_12 = (es_13_0 & 0x03000UL) >> 12;
	es_11_8 = (es_13_0 & 0x0F00UL) >> 8;
	es_7_0 = es_13_0 & 0x0FFUL;
	*lo_val = (es_7_0 & 0xFFUL) | ((unitmask & 0x0FUL) << 8);
	*hi_val = (es_11_8 | ((es_13_12)<<(29)));

	return 0;
}

/* assign df performance counters for read */
static int df_v3_6_pmc_assign_cntr(struct amdgpu_device *adev,
				   uint64_t config,
				   int *is_assigned)
{

	struct df_v3_6_event_mask *mask;
	int i, target_cntr;

	target_cntr = -1;

	*is_assigned = 0;

	df_v3_6_pmc_config_2_cntr(adev, config, &target_cntr);

	if (target_cntr >= 0) {
		*is_assigned = 1;
		return 0;
	}

	mask = container_of(adev, struct df_v3_6_event_mask, gpu);

	for (i = 0; i < AMDGPU_DF_MAX_COUNTERS; i++) {
		if (mask->config_assign_mask[i] == 0ULL) {
			mask->config_assign_mask[i] = config & 0x0FFFFFFUL;
			return 0;
		}
	}

	return -ENOSPC;
}

/* release performance counter */
static void df_v3_6_pmc_release_cntr(struct amdgpu_device *adev,
				     uint64_t config)
{

	struct df_v3_6_event_mask *mask;
	int target_cntr;

	target_cntr = -1;

	df_v3_6_pmc_config_2_cntr(adev, config, &target_cntr);

	mask = container_of(adev, struct df_v3_6_event_mask, gpu);

	if (target_cntr >= 0)
		mask->config_assign_mask[target_cntr] = 0ULL;

}

/*
 * get xgmi link counters via programmable data fabric (df) counters (max 4)
 * using cake tx event.
 *
 * @adev -> amdgpu device
 * @instance-> currently cake has 2 links to poll on vega20
 * @count -> counters to pass
 *
 */

static void df_v3_6_get_xgmi_link_cntr(struct amdgpu_device *adev,
				       int instance,
				       uint64_t *count)
{
	uint32_t lo_base_addr, hi_base_addr, lo_val, hi_val;
	uint64_t config;

	config = GET_INSTANCE_CONFIG(instance);

	df_v3_6_pmc_get_read_settings(adev, config, &lo_base_addr,
				      &hi_base_addr);

	if ((lo_base_addr == 0) || (hi_base_addr == 0))
		return;

	lo_val = RREG32_PCIE(lo_base_addr);
	hi_val = RREG32_PCIE(hi_base_addr);

	*count  = ((hi_val | 0ULL) << 32) | (lo_val | 0ULL);
}

/*
 * reset xgmi link counters
 *
 * @adev -> amdgpu device
 * @instance-> currently cake has 2 links to poll on vega20
 *
 */
static void df_v3_6_reset_xgmi_link_cntr(struct amdgpu_device *adev,
					 int instance)
{
	uint32_t lo_base_addr, hi_base_addr;
	uint64_t config;

	config = 0ULL | (0x7ULL) | ((0x46ULL + instance) << 8) | (0x2 << 16);

	df_v3_6_pmc_get_read_settings(adev, config, &lo_base_addr,
				      &hi_base_addr);

	if ((lo_base_addr == 0) || (hi_base_addr == 0))
		return;

	WREG32_PCIE(lo_base_addr, 0UL);
	WREG32_PCIE(hi_base_addr, 0UL);
}

/*
 * add xgmi link counters
 *
 * @adev -> amdgpu device
 * @instance-> currently cake has 2 links to poll on vega20
 *
 */

static int df_v3_6_add_xgmi_link_cntr(struct amdgpu_device *adev,
				      int instance)
{
	uint32_t lo_base_addr, hi_base_addr, lo_val, hi_val;
	uint64_t config;
	int ret, is_assigned;

	if (instance < 0 || instance > 1)
		return -EINVAL;

	config = GET_INSTANCE_CONFIG(instance);

	ret = df_v3_6_pmc_assign_cntr(adev, config, &is_assigned);

	if (ret || is_assigned)
		return ret;

	ret = df_v3_6_pmc_get_ctrl_settings(adev,
			config,
			&lo_base_addr,
			&hi_base_addr,
			&lo_val,
			&hi_val);

	if (ret)
		return ret;

	WREG32_PCIE(lo_base_addr, lo_val);
	WREG32_PCIE(hi_base_addr, hi_val);

	return ret;
}


/*
 * start xgmi link counters
 *
 * @adev -> amdgpu device
 * @instance-> currently cake has 2 links to poll on vega20
 * @is_enable -> either resume or assign event via df perfmon
 *
 */

static int df_v3_6_start_xgmi_link_cntr(struct amdgpu_device *adev,
					int instance,
					int is_enable)
{
	uint32_t lo_base_addr, hi_base_addr, lo_val;
	uint64_t config;
	int ret;

	if (instance < 0 || instance > 1)
		return -EINVAL;

	if (is_enable) {

		ret = df_v3_6_add_xgmi_link_cntr(adev, instance);

		if (ret)
			return ret;

	} else {

		config = GET_INSTANCE_CONFIG(instance);

		df_v3_6_pmc_get_ctrl_settings(adev,
				config,
				&lo_base_addr,
				&hi_base_addr,
				NULL,
				NULL);

		if (lo_base_addr == 0)
			return -EINVAL;

		lo_val = RREG32_PCIE(lo_base_addr);

		WREG32_PCIE(lo_base_addr, lo_val | (1ULL << 22));

		ret = 0;
	}

	return ret;

}

/*
 * start xgmi link counters
 *
 * @adev -> amdgpu device
 * @instance-> currently cake has 2 links to poll on vega20
 * @is_enable -> either pause or unassign event via df perfmon
 *
 */

static int df_v3_6_stop_xgmi_link_cntr(struct amdgpu_device *adev,
				       int instance,
				       int is_disable)
{

	uint32_t lo_base_addr, hi_base_addr, lo_val;
	uint64_t config;

	config = GET_INSTANCE_CONFIG(instance);

	if (is_disable) {
		df_v3_6_reset_xgmi_link_cntr(adev, instance);
		df_v3_6_pmc_release_cntr(adev, config);
	} else {

		df_v3_6_pmc_get_ctrl_settings(adev,
				config,
				&lo_base_addr,
				&hi_base_addr,
				NULL,
				NULL);

		if ((lo_base_addr == 0) || (hi_base_addr == 0))
			return -EINVAL;

		lo_val = RREG32_PCIE(lo_base_addr);

		WREG32_PCIE(lo_base_addr, lo_val & ~(1ULL << 22));
	}

	return 0;
}

static int df_v3_6_pmc_start(struct amdgpu_device *adev, uint64_t config,
			     int is_enable)
{
	int xgmi_tx_link, ret = 0;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
		xgmi_tx_link = IS_DF_XGMI_0_TX(config) ? 0
					: (IS_DF_XGMI_1_TX(config) ? 1 : -1);

		if (xgmi_tx_link >= 0)
			ret = df_v3_6_start_xgmi_link_cntr(adev, xgmi_tx_link,
						      is_enable);

		if (ret)
			return ret;

		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

static int df_v3_6_pmc_stop(struct amdgpu_device *adev, uint64_t config,
			    int is_disable)
{
	int xgmi_tx_link, ret = 0;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
			xgmi_tx_link = IS_DF_XGMI_0_TX(config) ? 0
				: (IS_DF_XGMI_1_TX(config) ? 1 : -1);

			if (xgmi_tx_link >= 0) {
				ret = df_v3_6_stop_xgmi_link_cntr(adev,
								  xgmi_tx_link,
								  is_disable);
				if (ret)
					return ret;
			}

			ret = 0;
			break;
	default:
		break;
	}

	return ret;
}

static void df_v3_6_pmc_get_count(struct amdgpu_device *adev,
				  uint64_t config,
				  uint64_t *count)
{

	int xgmi_tx_link;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
		xgmi_tx_link = IS_DF_XGMI_0_TX(config) ? 0
					: (IS_DF_XGMI_1_TX(config) ? 1 : -1);

		if (xgmi_tx_link >= 0) {
			df_v3_6_reset_xgmi_link_cntr(adev, xgmi_tx_link);
			df_v3_6_get_xgmi_link_cntr(adev, xgmi_tx_link, count);
		}

		break;
	default:
		break;
	}

}

const struct amdgpu_df_funcs df_v3_6_funcs = {
	.init = df_v3_6_init,
	.enable_broadcast_mode = df_v3_6_enable_broadcast_mode,
	.get_fb_channel_number = df_v3_6_get_fb_channel_number,
	.get_hbm_channel_number = df_v3_6_get_hbm_channel_number,
	.update_medium_grain_clock_gating =
			df_v3_6_update_medium_grain_clock_gating,
	.get_clockgating_state = df_v3_6_get_clockgating_state,
	.pmc_start = df_v3_6_pmc_start,
	.pmc_stop = df_v3_6_pmc_stop,
	.pmc_get_count = df_v3_6_pmc_get_count
};
