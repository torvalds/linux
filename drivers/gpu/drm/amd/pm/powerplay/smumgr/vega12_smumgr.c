/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#include "smumgr.h"
#include "vega12_inc.h"
#include "soc15_common.h"
#include "smu9_smumgr.h"
#include "vega12_smumgr.h"
#include "vega12_ppsmc.h"
#include "vega12/smu9_driver_if.h"
#include "ppatomctrl.h"
#include "pp_debug.h"


/*
 * Copy table from SMC into driver FB
 * @param   hwmgr    the address of the HW manager
 * @param   table_id    the driver's table ID to copy from
 */
static int vega12_copy_table_from_smc(struct pp_hwmgr *hwmgr,
				      uint8_t *table, int16_t table_id)
{
	struct vega12_smumgr *priv =
			(struct vega12_smumgr *)(hwmgr->smu_backend);
	struct amdgpu_device *adev = hwmgr->adev;

	PP_ASSERT_WITH_CODE(table_id < TABLE_COUNT,
			"Invalid SMU Table ID!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].version != 0,
			"Invalid SMU Table version!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].size != 0,
			"Invalid SMU Table Length!", return -EINVAL);
	PP_ASSERT_WITH_CODE(smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			upper_32_bits(priv->smu_tables.entry[table_id].mc_addr),
			NULL) == 0,
			"[CopyTableFromSMC] Attempt to Set Dram Addr High Failed!", return -EINVAL);
	PP_ASSERT_WITH_CODE(smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			lower_32_bits(priv->smu_tables.entry[table_id].mc_addr),
			NULL) == 0,
			"[CopyTableFromSMC] Attempt to Set Dram Addr Low Failed!",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableSmu2Dram,
			table_id,
			NULL) == 0,
			"[CopyTableFromSMC] Attempt to Transfer Table From SMU Failed!",
			return -EINVAL);

	amdgpu_asic_invalidate_hdp(adev, NULL);

	memcpy(table, priv->smu_tables.entry[table_id].table,
			priv->smu_tables.entry[table_id].size);

	return 0;
}

/*
 * Copy table from Driver FB into SMC
 * @param   hwmgr    the address of the HW manager
 * @param   table_id    the table to copy from
 */
static int vega12_copy_table_to_smc(struct pp_hwmgr *hwmgr,
				    uint8_t *table, int16_t table_id)
{
	struct vega12_smumgr *priv =
			(struct vega12_smumgr *)(hwmgr->smu_backend);
	struct amdgpu_device *adev = hwmgr->adev;

	PP_ASSERT_WITH_CODE(table_id < TABLE_COUNT,
			"Invalid SMU Table ID!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].version != 0,
			"Invalid SMU Table version!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].size != 0,
			"Invalid SMU Table Length!", return -EINVAL);

	memcpy(priv->smu_tables.entry[table_id].table, table,
			priv->smu_tables.entry[table_id].size);

	amdgpu_asic_flush_hdp(adev, NULL);

	PP_ASSERT_WITH_CODE(smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			upper_32_bits(priv->smu_tables.entry[table_id].mc_addr),
			NULL) == 0,
			"[CopyTableToSMC] Attempt to Set Dram Addr High Failed!",
			return -EINVAL;);
	PP_ASSERT_WITH_CODE(smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			lower_32_bits(priv->smu_tables.entry[table_id].mc_addr),
			NULL) == 0,
			"[CopyTableToSMC] Attempt to Set Dram Addr Low Failed!",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableDram2Smu,
			table_id,
			NULL) == 0,
			"[CopyTableToSMC] Attempt to Transfer Table To SMU Failed!",
			return -EINVAL);

	return 0;
}

int vega12_enable_smc_features(struct pp_hwmgr *hwmgr,
		bool enable, uint64_t feature_mask)
{
	uint32_t smu_features_low, smu_features_high;

	smu_features_low = (uint32_t)((feature_mask & SMU_FEATURES_LOW_MASK) >> SMU_FEATURES_LOW_SHIFT);
	smu_features_high = (uint32_t)((feature_mask & SMU_FEATURES_HIGH_MASK) >> SMU_FEATURES_HIGH_SHIFT);

	if (enable) {
		PP_ASSERT_WITH_CODE(smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_EnableSmuFeaturesLow, smu_features_low, NULL) == 0,
				"[EnableDisableSMCFeatures] Attempt to enable SMU features Low failed!",
				return -EINVAL);
		PP_ASSERT_WITH_CODE(smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_EnableSmuFeaturesHigh, smu_features_high, NULL) == 0,
				"[EnableDisableSMCFeatures] Attempt to enable SMU features High failed!",
				return -EINVAL);
	} else {
		PP_ASSERT_WITH_CODE(smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_DisableSmuFeaturesLow, smu_features_low, NULL) == 0,
				"[EnableDisableSMCFeatures] Attempt to disable SMU features Low failed!",
				return -EINVAL);
		PP_ASSERT_WITH_CODE(smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_DisableSmuFeaturesHigh, smu_features_high, NULL) == 0,
				"[EnableDisableSMCFeatures] Attempt to disable SMU features High failed!",
				return -EINVAL);
	}

	return 0;
}

int vega12_get_enabled_smc_features(struct pp_hwmgr *hwmgr,
		uint64_t *features_enabled)
{
	uint32_t smc_features_low, smc_features_high;

	if (features_enabled == NULL)
		return -EINVAL;

	PP_ASSERT_WITH_CODE(smum_send_msg_to_smc(hwmgr,
			PPSMC_MSG_GetEnabledSmuFeaturesLow,
			&smc_features_low) == 0,
			"[GetEnabledSMCFeatures] Attempt to get SMU features Low failed!",
			return -EINVAL);

	PP_ASSERT_WITH_CODE(smum_send_msg_to_smc(hwmgr,
			PPSMC_MSG_GetEnabledSmuFeaturesHigh,
			&smc_features_high) == 0,
			"[GetEnabledSMCFeatures] Attempt to get SMU features High failed!",
			return -EINVAL);

	*features_enabled = ((((uint64_t)smc_features_low << SMU_FEATURES_LOW_SHIFT) & SMU_FEATURES_LOW_MASK) |
			(((uint64_t)smc_features_high << SMU_FEATURES_HIGH_SHIFT) & SMU_FEATURES_HIGH_MASK));

	return 0;
}

static bool vega12_is_dpm_running(struct pp_hwmgr *hwmgr)
{
	uint64_t features_enabled = 0;

	vega12_get_enabled_smc_features(hwmgr, &features_enabled);

	if (features_enabled & SMC_DPM_FEATURES)
		return true;
	else
		return false;
}

static int vega12_set_tools_address(struct pp_hwmgr *hwmgr)
{
	struct vega12_smumgr *priv =
			(struct vega12_smumgr *)(hwmgr->smu_backend);

	if (priv->smu_tables.entry[TABLE_PMSTATUSLOG].mc_addr) {
		if (!smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetToolsDramAddrHigh,
				upper_32_bits(priv->smu_tables.entry[TABLE_PMSTATUSLOG].mc_addr),
				NULL))
			smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetToolsDramAddrLow,
					lower_32_bits(priv->smu_tables.entry[TABLE_PMSTATUSLOG].mc_addr),
					NULL);
	}
	return 0;
}

static int vega12_smu_init(struct pp_hwmgr *hwmgr)
{
	struct vega12_smumgr *priv;
	unsigned long tools_size;
	struct cgs_firmware_info info = {0};
	int ret;

	ret = cgs_get_firmware_info(hwmgr->device, CGS_UCODE_ID_SMU,
				&info);
	if (ret || !info.kptr)
		return -EINVAL;

	priv = kzalloc(sizeof(struct vega12_smumgr), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	hwmgr->smu_backend = priv;

	/* allocate space for pptable */
	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
			sizeof(PPTable_t),
			PAGE_SIZE,
			AMDGPU_GEM_DOMAIN_VRAM,
			&priv->smu_tables.entry[TABLE_PPTABLE].handle,
			&priv->smu_tables.entry[TABLE_PPTABLE].mc_addr,
			&priv->smu_tables.entry[TABLE_PPTABLE].table);
	if (ret)
		goto free_backend;

	priv->smu_tables.entry[TABLE_PPTABLE].version = 0x01;
	priv->smu_tables.entry[TABLE_PPTABLE].size = sizeof(PPTable_t);

	/* allocate space for watermarks table */
	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
				      sizeof(Watermarks_t),
				      PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &priv->smu_tables.entry[TABLE_WATERMARKS].handle,
				      &priv->smu_tables.entry[TABLE_WATERMARKS].mc_addr,
				      &priv->smu_tables.entry[TABLE_WATERMARKS].table);

	if (ret)
		goto err0;

	priv->smu_tables.entry[TABLE_WATERMARKS].version = 0x01;
	priv->smu_tables.entry[TABLE_WATERMARKS].size = sizeof(Watermarks_t);

	tools_size = 0x19000;
	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
				      tools_size,
				      PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &priv->smu_tables.entry[TABLE_PMSTATUSLOG].handle,
				      &priv->smu_tables.entry[TABLE_PMSTATUSLOG].mc_addr,
				      &priv->smu_tables.entry[TABLE_PMSTATUSLOG].table);
	if (ret)
		goto err1;

	priv->smu_tables.entry[TABLE_PMSTATUSLOG].version = 0x01;
	priv->smu_tables.entry[TABLE_PMSTATUSLOG].size = tools_size;

	/* allocate space for AVFS Fuse table */
	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
				      sizeof(AvfsFuseOverride_t),
				      PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &priv->smu_tables.entry[TABLE_AVFS_FUSE_OVERRIDE].handle,
				      &priv->smu_tables.entry[TABLE_AVFS_FUSE_OVERRIDE].mc_addr,
				      &priv->smu_tables.entry[TABLE_AVFS_FUSE_OVERRIDE].table);

	if (ret)
		goto err2;

	priv->smu_tables.entry[TABLE_AVFS_FUSE_OVERRIDE].version = 0x01;
	priv->smu_tables.entry[TABLE_AVFS_FUSE_OVERRIDE].size = sizeof(AvfsFuseOverride_t);

	/* allocate space for OverDrive table */
	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
				      sizeof(OverDriveTable_t),
				      PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &priv->smu_tables.entry[TABLE_OVERDRIVE].handle,
				      &priv->smu_tables.entry[TABLE_OVERDRIVE].mc_addr,
				      &priv->smu_tables.entry[TABLE_OVERDRIVE].table);
	if (ret)
		goto err3;

	priv->smu_tables.entry[TABLE_OVERDRIVE].version = 0x01;
	priv->smu_tables.entry[TABLE_OVERDRIVE].size = sizeof(OverDriveTable_t);

	/* allocate space for SMU_METRICS table */
	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
				      sizeof(SmuMetrics_t),
				      PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &priv->smu_tables.entry[TABLE_SMU_METRICS].handle,
				      &priv->smu_tables.entry[TABLE_SMU_METRICS].mc_addr,
				      &priv->smu_tables.entry[TABLE_SMU_METRICS].table);
	if (ret)
		goto err4;

	priv->smu_tables.entry[TABLE_SMU_METRICS].version = 0x01;
	priv->smu_tables.entry[TABLE_SMU_METRICS].size = sizeof(SmuMetrics_t);

	return 0;

err4:
	amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_OVERDRIVE].handle,
				&priv->smu_tables.entry[TABLE_OVERDRIVE].mc_addr,
				&priv->smu_tables.entry[TABLE_OVERDRIVE].table);
err3:
	amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_AVFS_FUSE_OVERRIDE].handle,
				&priv->smu_tables.entry[TABLE_AVFS_FUSE_OVERRIDE].mc_addr,
				&priv->smu_tables.entry[TABLE_AVFS_FUSE_OVERRIDE].table);
err2:
	if (priv->smu_tables.entry[TABLE_PMSTATUSLOG].table)
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_PMSTATUSLOG].handle,
				&priv->smu_tables.entry[TABLE_PMSTATUSLOG].mc_addr,
				&priv->smu_tables.entry[TABLE_PMSTATUSLOG].table);
err1:
	amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_WATERMARKS].handle,
				&priv->smu_tables.entry[TABLE_WATERMARKS].mc_addr,
				&priv->smu_tables.entry[TABLE_WATERMARKS].table);
err0:
	amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_PPTABLE].handle,
			&priv->smu_tables.entry[TABLE_PPTABLE].mc_addr,
			&priv->smu_tables.entry[TABLE_PPTABLE].table);
free_backend:
	kfree(hwmgr->smu_backend);

	return -EINVAL;
}

static int vega12_smu_fini(struct pp_hwmgr *hwmgr)
{
	struct vega12_smumgr *priv =
			(struct vega12_smumgr *)(hwmgr->smu_backend);

	if (priv) {
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_PPTABLE].handle,
				      &priv->smu_tables.entry[TABLE_PPTABLE].mc_addr,
				      &priv->smu_tables.entry[TABLE_PPTABLE].table);
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_WATERMARKS].handle,
				      &priv->smu_tables.entry[TABLE_WATERMARKS].mc_addr,
				      &priv->smu_tables.entry[TABLE_WATERMARKS].table);
		if (priv->smu_tables.entry[TABLE_PMSTATUSLOG].table)
			amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_PMSTATUSLOG].handle,
					      &priv->smu_tables.entry[TABLE_PMSTATUSLOG].mc_addr,
					      &priv->smu_tables.entry[TABLE_PMSTATUSLOG].table);
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_AVFS_FUSE_OVERRIDE].handle,
				      &priv->smu_tables.entry[TABLE_AVFS_FUSE_OVERRIDE].mc_addr,
				      &priv->smu_tables.entry[TABLE_AVFS_FUSE_OVERRIDE].table);
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_OVERDRIVE].handle,
				      &priv->smu_tables.entry[TABLE_OVERDRIVE].mc_addr,
				      &priv->smu_tables.entry[TABLE_OVERDRIVE].table);
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_SMU_METRICS].handle,
				      &priv->smu_tables.entry[TABLE_SMU_METRICS].mc_addr,
				      &priv->smu_tables.entry[TABLE_SMU_METRICS].table);
		kfree(hwmgr->smu_backend);
		hwmgr->smu_backend = NULL;
	}
	return 0;
}

static int vega12_start_smu(struct pp_hwmgr *hwmgr)
{
	PP_ASSERT_WITH_CODE(smu9_is_smc_ram_running(hwmgr),
			"SMC is not running!",
			return -EINVAL);

	vega12_set_tools_address(hwmgr);

	return 0;
}

static int vega12_smc_table_manager(struct pp_hwmgr *hwmgr, uint8_t *table,
				    uint16_t table_id, bool rw)
{
	int ret;

	if (rw)
		ret = vega12_copy_table_from_smc(hwmgr, table, table_id);
	else
		ret = vega12_copy_table_to_smc(hwmgr, table, table_id);

	return ret;
}

const struct pp_smumgr_func vega12_smu_funcs = {
	.name = "vega12_smu",
	.smu_init = &vega12_smu_init,
	.smu_fini = &vega12_smu_fini,
	.start_smu = &vega12_start_smu,
	.request_smu_load_specific_fw = NULL,
	.send_msg_to_smc = &smu9_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = &smu9_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
	.is_dpm_running = vega12_is_dpm_running,
	.get_argument = smu9_get_argument,
	.smc_table_manager = vega12_smc_table_manager,
};
