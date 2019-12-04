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

/* init df format attrs */
AMDGPU_PMU_ATTR(event,		"config:0-7");
AMDGPU_PMU_ATTR(instance,	"config:8-15");
AMDGPU_PMU_ATTR(umask,		"config:16-23");

/* df format attributes  */
static struct attribute *df_v3_6_format_attrs[] = {
	&pmu_attr_event.attr,
	&pmu_attr_instance.attr,
	&pmu_attr_umask.attr,
	NULL
};

/* df format attribute group */
static struct attribute_group df_v3_6_format_attr_group = {
	.name = "format",
	.attrs = df_v3_6_format_attrs,
};

/* df event attrs */
AMDGPU_PMU_ATTR(cake0_pcsout_txdata,
		      "event=0x7,instance=0x46,umask=0x2");
AMDGPU_PMU_ATTR(cake1_pcsout_txdata,
		      "event=0x7,instance=0x47,umask=0x2");
AMDGPU_PMU_ATTR(cake0_pcsout_txmeta,
		      "event=0x7,instance=0x46,umask=0x4");
AMDGPU_PMU_ATTR(cake1_pcsout_txmeta,
		      "event=0x7,instance=0x47,umask=0x4");
AMDGPU_PMU_ATTR(cake0_ftiinstat_reqalloc,
		      "event=0xb,instance=0x46,umask=0x4");
AMDGPU_PMU_ATTR(cake1_ftiinstat_reqalloc,
		      "event=0xb,instance=0x47,umask=0x4");
AMDGPU_PMU_ATTR(cake0_ftiinstat_rspalloc,
		      "event=0xb,instance=0x46,umask=0x8");
AMDGPU_PMU_ATTR(cake1_ftiinstat_rspalloc,
		      "event=0xb,instance=0x47,umask=0x8");

/* df event attributes  */
static struct attribute *df_v3_6_event_attrs[] = {
	&pmu_attr_cake0_pcsout_txdata.attr,
	&pmu_attr_cake1_pcsout_txdata.attr,
	&pmu_attr_cake0_pcsout_txmeta.attr,
	&pmu_attr_cake1_pcsout_txmeta.attr,
	&pmu_attr_cake0_ftiinstat_reqalloc.attr,
	&pmu_attr_cake1_ftiinstat_reqalloc.attr,
	&pmu_attr_cake0_ftiinstat_rspalloc.attr,
	&pmu_attr_cake1_ftiinstat_rspalloc.attr,
	NULL
};

/* df event attribute group */
static struct attribute_group df_v3_6_event_attr_group = {
	.name = "events",
	.attrs = df_v3_6_event_attrs
};

/* df event attr groups  */
const struct attribute_group *df_v3_6_attr_groups[] = {
		&df_v3_6_format_attr_group,
		&df_v3_6_event_attr_group,
		NULL
};

static uint64_t df_v3_6_get_fica(struct amdgpu_device *adev,
				 uint32_t ficaa_val)
{
	unsigned long flags, address, data;
	uint32_t ficadl_val, ficadh_val;

	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(address, smnDF_PIE_AON_FabricIndirectConfigAccessAddress3);
	WREG32(data, ficaa_val);

	WREG32(address, smnDF_PIE_AON_FabricIndirectConfigAccessDataLo3);
	ficadl_val = RREG32(data);

	WREG32(address, smnDF_PIE_AON_FabricIndirectConfigAccessDataHi3);
	ficadh_val = RREG32(data);

	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);

	return (((ficadh_val & 0xFFFFFFFFFFFFFFFF) << 32) | ficadl_val);
}

static void df_v3_6_set_fica(struct amdgpu_device *adev, uint32_t ficaa_val,
			     uint32_t ficadl_val, uint32_t ficadh_val)
{
	unsigned long flags, address, data;

	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(address, smnDF_PIE_AON_FabricIndirectConfigAccessAddress3);
	WREG32(data, ficaa_val);

	WREG32(address, smnDF_PIE_AON_FabricIndirectConfigAccessDataLo3);
	WREG32(data, ficadl_val);

	WREG32(address, smnDF_PIE_AON_FabricIndirectConfigAccessDataHi3);
	WREG32(data, ficadh_val);

	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

/*
 * df_v3_6_perfmon_rreg - read perfmon lo and hi
 *
 * required to be atomic.  no mmio method provided so subsequent reads for lo
 * and hi require to preserve df finite state machine
 */
static void df_v3_6_perfmon_rreg(struct amdgpu_device *adev,
			    uint32_t lo_addr, uint32_t *lo_val,
			    uint32_t hi_addr, uint32_t *hi_val)
{
	unsigned long flags, address, data;

	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(address, lo_addr);
	*lo_val = RREG32(data);
	WREG32(address, hi_addr);
	*hi_val = RREG32(data);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

/*
 * df_v3_6_perfmon_wreg - write to perfmon lo and hi
 *
 * required to be atomic.  no mmio method provided so subsequent reads after
 * data writes cannot occur to preserve data fabrics finite state machine.
 */
static void df_v3_6_perfmon_wreg(struct amdgpu_device *adev, uint32_t lo_addr,
			    uint32_t lo_val, uint32_t hi_addr, uint32_t hi_val)
{
	unsigned long flags, address, data;

	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(address, lo_addr);
	WREG32(data, lo_val);
	WREG32(address, hi_addr);
	WREG32(data, hi_val);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

/* get the number of df counters available */
static ssize_t df_v3_6_get_df_cntr_avail(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct amdgpu_device *adev;
	struct drm_device *ddev;
	int i, count;

	ddev = dev_get_drvdata(dev);
	adev = ddev->dev_private;
	count = 0;

	for (i = 0; i < DF_V3_6_MAX_COUNTERS; i++) {
		if (adev->df_perfmon_config_assign_mask[i] == 0)
			count++;
	}

	return snprintf(buf, PAGE_SIZE,	"%i\n", count);
}

/* device attr for available perfmon counters */
static DEVICE_ATTR(df_cntr_avail, S_IRUGO, df_v3_6_get_df_cntr_avail, NULL);

/* init perfmons */
static void df_v3_6_sw_init(struct amdgpu_device *adev)
{
	int i, ret;

	ret = device_create_file(adev->dev, &dev_attr_df_cntr_avail);
	if (ret)
		DRM_ERROR("failed to create file for available df counters\n");

	for (i = 0; i < AMDGPU_MAX_DF_PERFMONS; i++)
		adev->df_perfmon_config_assign_mask[i] = 0;
}

static void df_v3_6_sw_fini(struct amdgpu_device *adev)
{

	device_remove_file(adev->dev, &dev_attr_df_cntr_avail);

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

/* get assigned df perfmon ctr as int */
static int df_v3_6_pmc_config_2_cntr(struct amdgpu_device *adev,
				      uint64_t config)
{
	int i;

	for (i = 0; i < DF_V3_6_MAX_COUNTERS; i++) {
		if ((config & 0x0FFFFFFUL) ==
					adev->df_perfmon_config_assign_mask[i])
			return i;
	}

	return -EINVAL;
}

/* get address based on counter assignment */
static void df_v3_6_pmc_get_addr(struct amdgpu_device *adev,
				 uint64_t config,
				 int is_ctrl,
				 uint32_t *lo_base_addr,
				 uint32_t *hi_base_addr)
{
	int target_cntr = df_v3_6_pmc_config_2_cntr(adev, config);

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
	uint32_t instance_10, instance_5432, instance_76;

	df_v3_6_pmc_get_addr(adev, config, 1, lo_base_addr, hi_base_addr);

	if ((*lo_base_addr == 0) || (*hi_base_addr == 0)) {
		DRM_ERROR("[DF PMC] addressing not retrieved! Lo: %x, Hi: %x",
				*lo_base_addr, *hi_base_addr);
		return -ENXIO;
	}

	eventsel = DF_V3_6_GET_EVENT(config) & 0x3f;
	unitmask = DF_V3_6_GET_UNITMASK(config) & 0xf;
	instance = DF_V3_6_GET_INSTANCE(config);

	instance_10 = instance & 0x3;
	instance_5432 = (instance >> 2) & 0xf;
	instance_76 = (instance >> 6) & 0x3;

	*lo_val = (unitmask << 8) | (instance_10 << 6) | eventsel | (1 << 22);
	*hi_val = (instance_76 << 29) | instance_5432;

	DRM_DEBUG_DRIVER("config=%llx addr=%08x:%08x val=%08x:%08x",
		config, *lo_base_addr, *hi_base_addr, *lo_val, *hi_val);

	return 0;
}

/* add df performance counters for read */
static int df_v3_6_pmc_add_cntr(struct amdgpu_device *adev,
				   uint64_t config)
{
	int i, target_cntr;

	target_cntr = df_v3_6_pmc_config_2_cntr(adev, config);

	if (target_cntr >= 0)
		return 0;

	for (i = 0; i < DF_V3_6_MAX_COUNTERS; i++) {
		if (adev->df_perfmon_config_assign_mask[i] == 0U) {
			adev->df_perfmon_config_assign_mask[i] =
							config & 0x0FFFFFFUL;
			return 0;
		}
	}

	return -ENOSPC;
}

/* release performance counter */
static void df_v3_6_pmc_release_cntr(struct amdgpu_device *adev,
				     uint64_t config)
{
	int target_cntr = df_v3_6_pmc_config_2_cntr(adev, config);

	if (target_cntr >= 0)
		adev->df_perfmon_config_assign_mask[target_cntr] = 0ULL;
}


static void df_v3_6_reset_perfmon_cntr(struct amdgpu_device *adev,
					 uint64_t config)
{
	uint32_t lo_base_addr, hi_base_addr;

	df_v3_6_pmc_get_read_settings(adev, config, &lo_base_addr,
				      &hi_base_addr);

	if ((lo_base_addr == 0) || (hi_base_addr == 0))
		return;

	df_v3_6_perfmon_wreg(adev, lo_base_addr, 0, hi_base_addr, 0);
}

static int df_v3_6_pmc_start(struct amdgpu_device *adev, uint64_t config,
			     int is_enable)
{
	uint32_t lo_base_addr, hi_base_addr, lo_val, hi_val;
	int ret = 0;

	switch (adev->asic_type) {
	case CHIP_VEGA20:

		df_v3_6_reset_perfmon_cntr(adev, config);

		if (is_enable) {
			ret = df_v3_6_pmc_add_cntr(adev, config);
		} else {
			ret = df_v3_6_pmc_get_ctrl_settings(adev,
					config,
					&lo_base_addr,
					&hi_base_addr,
					&lo_val,
					&hi_val);

			if (ret)
				return ret;

			df_v3_6_perfmon_wreg(adev, lo_base_addr, lo_val,
					hi_base_addr, hi_val);
		}

		break;
	default:
		break;
	}

	return ret;
}

static int df_v3_6_pmc_stop(struct amdgpu_device *adev, uint64_t config,
			    int is_disable)
{
	uint32_t lo_base_addr, hi_base_addr, lo_val, hi_val;
	int ret = 0;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
		ret = df_v3_6_pmc_get_ctrl_settings(adev,
			config,
			&lo_base_addr,
			&hi_base_addr,
			&lo_val,
			&hi_val);

		if (ret)
			return ret;

		df_v3_6_perfmon_wreg(adev, lo_base_addr, 0, hi_base_addr, 0);

		if (is_disable)
			df_v3_6_pmc_release_cntr(adev, config);

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
	uint32_t lo_base_addr, hi_base_addr, lo_val, hi_val;
	*count = 0;

	switch (adev->asic_type) {
	case CHIP_VEGA20:

		df_v3_6_pmc_get_read_settings(adev, config, &lo_base_addr,
				      &hi_base_addr);

		if ((lo_base_addr == 0) || (hi_base_addr == 0))
			return;

		df_v3_6_perfmon_rreg(adev, lo_base_addr, &lo_val,
				hi_base_addr, &hi_val);

		*count  = ((hi_val | 0ULL) << 32) | (lo_val | 0ULL);

		if (*count >= DF_V3_6_PERFMON_OVERFLOW)
			*count = 0;

		DRM_DEBUG_DRIVER("config=%llx addr=%08x:%08x val=%08x:%08x",
			 config, lo_base_addr, hi_base_addr, lo_val, hi_val);

		break;

	default:
		break;
	}
}

const struct amdgpu_df_funcs df_v3_6_funcs = {
	.sw_init = df_v3_6_sw_init,
	.sw_fini = df_v3_6_sw_fini,
	.enable_broadcast_mode = df_v3_6_enable_broadcast_mode,
	.get_fb_channel_number = df_v3_6_get_fb_channel_number,
	.get_hbm_channel_number = df_v3_6_get_hbm_channel_number,
	.update_medium_grain_clock_gating =
			df_v3_6_update_medium_grain_clock_gating,
	.get_clockgating_state = df_v3_6_get_clockgating_state,
	.pmc_start = df_v3_6_pmc_start,
	.pmc_stop = df_v3_6_pmc_stop,
	.pmc_get_count = df_v3_6_pmc_get_count,
	.get_fica = df_v3_6_get_fica,
	.set_fica = df_v3_6_set_fica
};
