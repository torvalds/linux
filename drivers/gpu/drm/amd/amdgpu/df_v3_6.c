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

#define DF_3_6_SMN_REG_INST_DIST        0x8
#define DF_3_6_INST_CNT                 8

/* Defined in global_features.h as FTI_PERFMON_VISIBLE */
#define DF_V3_6_MAX_COUNTERS		4

/* get flags from df perfmon config */
#define DF_V3_6_GET_EVENT(x)		(x & 0xFFUL)
#define DF_V3_6_GET_INSTANCE(x)		((x >> 8) & 0xFFUL)
#define DF_V3_6_GET_UNITMASK(x)		((x >> 16) & 0xFFUL)
#define DF_V3_6_PERFMON_OVERFLOW	0xFFFFFFFFFFFFULL

static u32 df_v3_6_channel_number[] = {1, 2, 0, 4, 0, 8, 0,
				       16, 32, 0, 0, 0, 2, 4, 8};

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

/* same as perfmon_wreg but return status on write value check */
static int df_v3_6_perfmon_arm_with_status(struct amdgpu_device *adev,
					  uint32_t lo_addr, uint32_t lo_val,
					  uint32_t hi_addr, uint32_t  hi_val)
{
	unsigned long flags, address, data;
	uint32_t lo_val_rb, hi_val_rb;

	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(address, lo_addr);
	WREG32(data, lo_val);
	WREG32(address, hi_addr);
	WREG32(data, hi_val);

	WREG32(address, lo_addr);
	lo_val_rb = RREG32(data);
	WREG32(address, hi_addr);
	hi_val_rb = RREG32(data);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);

	if (!(lo_val == lo_val_rb && hi_val == hi_val_rb))
		return -EBUSY;

	return 0;
}


/*
 * retry arming counters every 100 usecs within 1 millisecond interval.
 * if retry fails after time out, return error.
 */
#define ARM_RETRY_USEC_TIMEOUT	1000
#define ARM_RETRY_USEC_INTERVAL	100
static int df_v3_6_perfmon_arm_with_retry(struct amdgpu_device *adev,
					  uint32_t lo_addr, uint32_t lo_val,
					  uint32_t hi_addr, uint32_t  hi_val)
{
	int countdown = ARM_RETRY_USEC_TIMEOUT;

	while (countdown) {

		if (!df_v3_6_perfmon_arm_with_status(adev, lo_addr, lo_val,
						     hi_addr, hi_val))
			break;

		countdown -= ARM_RETRY_USEC_INTERVAL;
		udelay(ARM_RETRY_USEC_INTERVAL);
	}

	return countdown > 0 ? 0 : -ETIME;
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
	adev = drm_to_adev(ddev);
	count = 0;

	for (i = 0; i < DF_V3_6_MAX_COUNTERS; i++) {
		if (adev->df_perfmon_config_assign_mask[i] == 0)
			count++;
	}

	return snprintf(buf, PAGE_SIZE,	"%i\n", count);
}

/* device attr for available perfmon counters */
static DEVICE_ATTR(df_cntr_avail, S_IRUGO, df_v3_6_get_df_cntr_avail, NULL);

static void df_v3_6_query_hashes(struct amdgpu_device *adev)
{
	u32 tmp;

	adev->df.hash_status.hash_64k = false;
	adev->df.hash_status.hash_2m = false;
	adev->df.hash_status.hash_1g = false;

	if (adev->asic_type != CHIP_ARCTURUS)
		return;

	/* encoding for hash-enabled on Arcturus */
	if (adev->df.funcs->get_fb_channel_number(adev) == 0xe) {
		tmp = RREG32_SOC15(DF, 0, mmDF_CS_UMC_AON0_DfGlobalCtrl);
		adev->df.hash_status.hash_64k = REG_GET_FIELD(tmp,
						DF_CS_UMC_AON0_DfGlobalCtrl,
						GlbHashIntlvCtl64K);
		adev->df.hash_status.hash_2m = REG_GET_FIELD(tmp,
						DF_CS_UMC_AON0_DfGlobalCtrl,
						GlbHashIntlvCtl2M);
		adev->df.hash_status.hash_1g = REG_GET_FIELD(tmp,
						DF_CS_UMC_AON0_DfGlobalCtrl,
						GlbHashIntlvCtl1G);
	}
}

/* init perfmons */
static void df_v3_6_sw_init(struct amdgpu_device *adev)
{
	int i, ret;

	ret = device_create_file(adev->dev, &dev_attr_df_cntr_avail);
	if (ret)
		DRM_ERROR("failed to create file for available df counters\n");

	for (i = 0; i < AMDGPU_MAX_DF_PERFMONS; i++)
		adev->df_perfmon_config_assign_mask[i] = 0;

	df_v3_6_query_hashes(adev);
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

	fb_channel_number = adev->df.funcs->get_fb_channel_number(adev);
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
		adev->df.funcs->enable_broadcast_mode(adev, true);

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
		adev->df.funcs->enable_broadcast_mode(adev, false);
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
static bool df_v3_6_pmc_has_counter(struct amdgpu_device *adev,
				      uint64_t config,
				      int counter_idx)
{

	return ((config & 0x0FFFFFFUL) ==
			adev->df_perfmon_config_assign_mask[counter_idx]);

}

/* get address based on counter assignment */
static void df_v3_6_pmc_get_addr(struct amdgpu_device *adev,
				 uint64_t config,
				 int counter_idx,
				 int is_ctrl,
				 uint32_t *lo_base_addr,
				 uint32_t *hi_base_addr)
{
	if (!df_v3_6_pmc_has_counter(adev, config, counter_idx))
		return;

	switch (counter_idx) {

	case 0:
		*lo_base_addr = is_ctrl ? smnPerfMonCtlLo4 : smnPerfMonCtrLo4;
		*hi_base_addr = is_ctrl ? smnPerfMonCtlHi4 : smnPerfMonCtrHi4;
		break;
	case 1:
		*lo_base_addr = is_ctrl ? smnPerfMonCtlLo5 : smnPerfMonCtrLo5;
		*hi_base_addr = is_ctrl ? smnPerfMonCtlHi5 : smnPerfMonCtrHi5;
		break;
	case 2:
		*lo_base_addr = is_ctrl ? smnPerfMonCtlLo6 : smnPerfMonCtrLo6;
		*hi_base_addr = is_ctrl ? smnPerfMonCtlHi6 : smnPerfMonCtrHi6;
		break;
	case 3:
		*lo_base_addr = is_ctrl ? smnPerfMonCtlLo7 : smnPerfMonCtrLo7;
		*hi_base_addr = is_ctrl ? smnPerfMonCtlHi7 : smnPerfMonCtrHi7;
		break;

	}

}

/* get read counter address */
static void df_v3_6_pmc_get_read_settings(struct amdgpu_device *adev,
					  uint64_t config,
					  int counter_idx,
					  uint32_t *lo_base_addr,
					  uint32_t *hi_base_addr)
{
	df_v3_6_pmc_get_addr(adev, config, counter_idx, 0, lo_base_addr,
								hi_base_addr);
}

/* get control counter settings i.e. address and values to set */
static int df_v3_6_pmc_get_ctrl_settings(struct amdgpu_device *adev,
					  uint64_t config,
					  int counter_idx,
					  uint32_t *lo_base_addr,
					  uint32_t *hi_base_addr,
					  uint32_t *lo_val,
					  uint32_t *hi_val,
					  bool is_enable)
{

	uint32_t eventsel, instance, unitmask;
	uint32_t instance_10, instance_5432, instance_76;

	df_v3_6_pmc_get_addr(adev, config, counter_idx, 1, lo_base_addr,
				hi_base_addr);

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

	*lo_val = (unitmask << 8) | (instance_10 << 6) | eventsel;
	*lo_val = is_enable ? *lo_val | (1 << 22) : *lo_val & ~(1 << 22);
	*hi_val = (instance_76 << 29) | instance_5432;

	DRM_DEBUG_DRIVER("config=%llx addr=%08x:%08x val=%08x:%08x",
		config, *lo_base_addr, *hi_base_addr, *lo_val, *hi_val);

	return 0;
}

/* add df performance counters for read */
static int df_v3_6_pmc_add_cntr(struct amdgpu_device *adev,
				   uint64_t config)
{
	int i;

	for (i = 0; i < DF_V3_6_MAX_COUNTERS; i++) {
		if (adev->df_perfmon_config_assign_mask[i] == 0U) {
			adev->df_perfmon_config_assign_mask[i] =
							config & 0x0FFFFFFUL;
			return i;
		}
	}

	return -ENOSPC;
}

#define DEFERRED_ARM_MASK	(1 << 31)
static int df_v3_6_pmc_set_deferred(struct amdgpu_device *adev,
				    int counter_idx, uint64_t config,
				    bool is_deferred)
{

	if (!df_v3_6_pmc_has_counter(adev, config, counter_idx))
		return -EINVAL;

	if (is_deferred)
		adev->df_perfmon_config_assign_mask[counter_idx] |=
							DEFERRED_ARM_MASK;
	else
		adev->df_perfmon_config_assign_mask[counter_idx] &=
							~DEFERRED_ARM_MASK;

	return 0;
}

static bool df_v3_6_pmc_is_deferred(struct amdgpu_device *adev,
				    int counter_idx,
				    uint64_t config)
{
	return	(df_v3_6_pmc_has_counter(adev, config, counter_idx) &&
			(adev->df_perfmon_config_assign_mask[counter_idx]
				& DEFERRED_ARM_MASK));

}

/* release performance counter */
static void df_v3_6_pmc_release_cntr(struct amdgpu_device *adev,
				     uint64_t config,
				     int counter_idx)
{
	if (df_v3_6_pmc_has_counter(adev, config, counter_idx))
		adev->df_perfmon_config_assign_mask[counter_idx] = 0ULL;
}


static void df_v3_6_reset_perfmon_cntr(struct amdgpu_device *adev,
					 uint64_t config,
					 int counter_idx)
{
	uint32_t lo_base_addr = 0, hi_base_addr = 0;

	df_v3_6_pmc_get_read_settings(adev, config, counter_idx, &lo_base_addr,
				      &hi_base_addr);

	if ((lo_base_addr == 0) || (hi_base_addr == 0))
		return;

	df_v3_6_perfmon_wreg(adev, lo_base_addr, 0, hi_base_addr, 0);
}

/* return available counter if is_add == 1 otherwise return error status. */
static int df_v3_6_pmc_start(struct amdgpu_device *adev, uint64_t config,
			     int counter_idx, int is_add)
{
	uint32_t lo_base_addr, hi_base_addr, lo_val, hi_val;
	int err = 0, ret = 0;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
	case CHIP_ARCTURUS:
		if (is_add)
			return df_v3_6_pmc_add_cntr(adev, config);

		ret = df_v3_6_pmc_get_ctrl_settings(adev,
					config,
					counter_idx,
					&lo_base_addr,
					&hi_base_addr,
					&lo_val,
					&hi_val,
					true);

		if (ret)
			return ret;

		err = df_v3_6_perfmon_arm_with_retry(adev,
						     lo_base_addr,
						     lo_val,
						     hi_base_addr,
						     hi_val);

		if (err)
			ret = df_v3_6_pmc_set_deferred(adev, config,
							counter_idx, true);

		break;
	default:
		break;
	}

	return ret;
}

static int df_v3_6_pmc_stop(struct amdgpu_device *adev, uint64_t config,
			    int counter_idx, int is_remove)
{
	uint32_t lo_base_addr, hi_base_addr, lo_val, hi_val;
	int ret = 0;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
	case CHIP_ARCTURUS:
		ret = df_v3_6_pmc_get_ctrl_settings(adev,
			config,
			counter_idx,
			&lo_base_addr,
			&hi_base_addr,
			&lo_val,
			&hi_val,
			false);

		if (ret)
			return ret;


		if (is_remove) {
			df_v3_6_reset_perfmon_cntr(adev, config, counter_idx);
			df_v3_6_pmc_release_cntr(adev, config, counter_idx);
		}

		break;
	default:
		break;
	}

	return ret;
}

static void df_v3_6_pmc_get_count(struct amdgpu_device *adev,
				  uint64_t config,
				  int counter_idx,
				  uint64_t *count)
{
	uint32_t lo_base_addr = 0, hi_base_addr = 0, lo_val = 0, hi_val = 0;
	*count = 0;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
	case CHIP_ARCTURUS:
		df_v3_6_pmc_get_read_settings(adev, config, counter_idx,
						&lo_base_addr, &hi_base_addr);

		if ((lo_base_addr == 0) || (hi_base_addr == 0))
			return;

		/* rearm the counter or throw away count value on failure */
		if (df_v3_6_pmc_is_deferred(adev, config, counter_idx)) {
			int rearm_err = df_v3_6_perfmon_arm_with_status(adev,
							lo_base_addr, lo_val,
							hi_base_addr, hi_val);

			if (rearm_err)
				return;

			df_v3_6_pmc_set_deferred(adev, config, counter_idx,
									false);
		}

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
	.set_fica = df_v3_6_set_fica,
};
