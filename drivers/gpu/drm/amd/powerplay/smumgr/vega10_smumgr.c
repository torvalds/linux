/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#include "vega10_inc.h"
#include "soc15_common.h"
#include "vega10_smumgr.h"
#include "vega10_hwmgr.h"
#include "vega10_ppsmc.h"
#include "smu9_driver_if.h"
#include "smu9_smumgr.h"
#include "ppatomctrl.h"
#include "pp_debug.h"


static int vega10_copy_table_from_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id)
{
	struct vega10_smumgr *priv = hwmgr->smu_backend;

	PP_ASSERT_WITH_CODE(table_id < MAX_SMU_TABLE,
			"Invalid SMU Table ID!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].version != 0,
			"Invalid SMU Table version!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].size != 0,
			"Invalid SMU Table Length!", return -EINVAL);
	smu9_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			upper_32_bits(priv->smu_tables.entry[table_id].mc_addr));
	smu9_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			lower_32_bits(priv->smu_tables.entry[table_id].mc_addr));
	smu9_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableSmu2Dram,
			priv->smu_tables.entry[table_id].table_id);

	memcpy(table, priv->smu_tables.entry[table_id].table,
			priv->smu_tables.entry[table_id].size);

	return 0;
}

static int vega10_copy_table_to_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id)
{
	struct vega10_smumgr *priv = hwmgr->smu_backend;

	PP_ASSERT_WITH_CODE(table_id < MAX_SMU_TABLE,
			"Invalid SMU Table ID!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].version != 0,
			"Invalid SMU Table version!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].size != 0,
			"Invalid SMU Table Length!", return -EINVAL);

	memcpy(priv->smu_tables.entry[table_id].table, table,
			priv->smu_tables.entry[table_id].size);

	smu9_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			upper_32_bits(priv->smu_tables.entry[table_id].mc_addr));
	smu9_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			lower_32_bits(priv->smu_tables.entry[table_id].mc_addr));
	smu9_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableDram2Smu,
			priv->smu_tables.entry[table_id].table_id);

	return 0;
}

int vega10_enable_smc_features(struct pp_hwmgr *hwmgr,
			       bool enable, uint32_t feature_mask)
{
	int msg = enable ? PPSMC_MSG_EnableSmuFeatures :
			PPSMC_MSG_DisableSmuFeatures;

	return smum_send_msg_to_smc_with_parameter(hwmgr,
			msg, feature_mask);
}

int vega10_get_enabled_smc_features(struct pp_hwmgr *hwmgr,
			    uint64_t *features_enabled)
{
	if (features_enabled == NULL)
		return -EINVAL;

	smu9_send_msg_to_smc(hwmgr, PPSMC_MSG_GetEnabledSmuFeatures);
	*features_enabled = smu9_get_argument(hwmgr);

	return 0;
}

static bool vega10_is_dpm_running(struct pp_hwmgr *hwmgr)
{
	uint64_t features_enabled = 0;

	vega10_get_enabled_smc_features(hwmgr, &features_enabled);

	if (features_enabled & SMC_DPM_FEATURES)
		return true;
	else
		return false;
}

static int vega10_set_tools_address(struct pp_hwmgr *hwmgr)
{
	struct vega10_smumgr *priv = hwmgr->smu_backend;

	if (priv->smu_tables.entry[TOOLSTABLE].mc_addr) {
		smu9_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetToolsDramAddrHigh,
				upper_32_bits(priv->smu_tables.entry[TOOLSTABLE].mc_addr));
		smu9_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetToolsDramAddrLow,
				lower_32_bits(priv->smu_tables.entry[TOOLSTABLE].mc_addr));
	}
	return 0;
}

static int vega10_verify_smc_interface(struct pp_hwmgr *hwmgr)
{
	uint32_t smc_driver_if_version;
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t dev_id;
	uint32_t rev_id;

	PP_ASSERT_WITH_CODE(!smu9_send_msg_to_smc(hwmgr,
			PPSMC_MSG_GetDriverIfVersion),
			"Attempt to get SMC IF Version Number Failed!",
			return -EINVAL);
	smc_driver_if_version = smu9_get_argument(hwmgr);

	dev_id = adev->pdev->device;
	rev_id = adev->pdev->revision;

	if (!((dev_id == 0x687f) &&
		((rev_id == 0xc0) ||
		(rev_id == 0xc1) ||
		(rev_id == 0xc3)))) {
		if (smc_driver_if_version != SMU9_DRIVER_IF_VERSION) {
			pr_err("Your firmware(0x%x) doesn't match SMU9_DRIVER_IF_VERSION(0x%x). Please update your firmware!\n",
			       smc_driver_if_version, SMU9_DRIVER_IF_VERSION);
			return -EINVAL;
		}
	}

	return 0;
}

static int vega10_smu_init(struct pp_hwmgr *hwmgr)
{
	struct vega10_smumgr *priv;
	unsigned long tools_size;
	int ret;
	struct cgs_firmware_info info = {0};

	ret = cgs_get_firmware_info(hwmgr->device,
				    CGS_UCODE_ID_SMU,
				    &info);
	if (ret || !info.kptr)
		return -EINVAL;

	priv = kzalloc(sizeof(struct vega10_smumgr), GFP_KERNEL);

	if (!priv)
		return -ENOMEM;

	hwmgr->smu_backend = priv;

	/* allocate space for pptable */
	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
			sizeof(PPTable_t),
			PAGE_SIZE,
			AMDGPU_GEM_DOMAIN_VRAM,
			&priv->smu_tables.entry[PPTABLE].handle,
			&priv->smu_tables.entry[PPTABLE].mc_addr,
			&priv->smu_tables.entry[PPTABLE].table);
	if (ret)
		goto free_backend;

	priv->smu_tables.entry[PPTABLE].version = 0x01;
	priv->smu_tables.entry[PPTABLE].size = sizeof(PPTable_t);
	priv->smu_tables.entry[PPTABLE].table_id = TABLE_PPTABLE;

	/* allocate space for watermarks table */
	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
			sizeof(Watermarks_t),
			PAGE_SIZE,
			AMDGPU_GEM_DOMAIN_VRAM,
			&priv->smu_tables.entry[WMTABLE].handle,
			&priv->smu_tables.entry[WMTABLE].mc_addr,
			&priv->smu_tables.entry[WMTABLE].table);

	if (ret)
		goto err0;

	priv->smu_tables.entry[WMTABLE].version = 0x01;
	priv->smu_tables.entry[WMTABLE].size = sizeof(Watermarks_t);
	priv->smu_tables.entry[WMTABLE].table_id = TABLE_WATERMARKS;

	/* allocate space for AVFS table */
	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
			sizeof(AvfsTable_t),
			PAGE_SIZE,
			AMDGPU_GEM_DOMAIN_VRAM,
			&priv->smu_tables.entry[AVFSTABLE].handle,
			&priv->smu_tables.entry[AVFSTABLE].mc_addr,
			&priv->smu_tables.entry[AVFSTABLE].table);

	if (ret)
		goto err1;

	priv->smu_tables.entry[AVFSTABLE].version = 0x01;
	priv->smu_tables.entry[AVFSTABLE].size = sizeof(AvfsTable_t);
	priv->smu_tables.entry[AVFSTABLE].table_id = TABLE_AVFS;

	tools_size = 0x19000;
	if (tools_size) {
		ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
				tools_size,
				PAGE_SIZE,
				AMDGPU_GEM_DOMAIN_VRAM,
				&priv->smu_tables.entry[TOOLSTABLE].handle,
				&priv->smu_tables.entry[TOOLSTABLE].mc_addr,
				&priv->smu_tables.entry[TOOLSTABLE].table);
		if (ret)
			goto err2;
		priv->smu_tables.entry[TOOLSTABLE].version = 0x01;
		priv->smu_tables.entry[TOOLSTABLE].size = tools_size;
		priv->smu_tables.entry[TOOLSTABLE].table_id = TABLE_PMSTATUSLOG;
	}

	/* allocate space for AVFS Fuse table */
	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
			sizeof(AvfsFuseOverride_t),
			PAGE_SIZE,
			AMDGPU_GEM_DOMAIN_VRAM,
			&priv->smu_tables.entry[AVFSFUSETABLE].handle,
			&priv->smu_tables.entry[AVFSFUSETABLE].mc_addr,
			&priv->smu_tables.entry[AVFSFUSETABLE].table);
	if (ret)
		goto err3;

	priv->smu_tables.entry[AVFSFUSETABLE].version = 0x01;
	priv->smu_tables.entry[AVFSFUSETABLE].size = sizeof(AvfsFuseOverride_t);
	priv->smu_tables.entry[AVFSFUSETABLE].table_id = TABLE_AVFS_FUSE_OVERRIDE;


	return 0;

err3:
	if (priv->smu_tables.entry[TOOLSTABLE].table)
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[TOOLSTABLE].handle,
				&priv->smu_tables.entry[TOOLSTABLE].mc_addr,
				&priv->smu_tables.entry[TOOLSTABLE].table);
err2:
	amdgpu_bo_free_kernel(&priv->smu_tables.entry[AVFSTABLE].handle,
				&priv->smu_tables.entry[AVFSTABLE].mc_addr,
				&priv->smu_tables.entry[AVFSTABLE].table);
err1:
	amdgpu_bo_free_kernel(&priv->smu_tables.entry[WMTABLE].handle,
				&priv->smu_tables.entry[WMTABLE].mc_addr,
				&priv->smu_tables.entry[WMTABLE].table);
err0:
	amdgpu_bo_free_kernel(&priv->smu_tables.entry[PPTABLE].handle,
			&priv->smu_tables.entry[PPTABLE].mc_addr,
			&priv->smu_tables.entry[PPTABLE].table);
free_backend:
	kfree(hwmgr->smu_backend);

	return -EINVAL;
}

static int vega10_smu_fini(struct pp_hwmgr *hwmgr)
{
	struct vega10_smumgr *priv = hwmgr->smu_backend;

	if (priv) {
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[PPTABLE].handle,
				&priv->smu_tables.entry[PPTABLE].mc_addr,
				&priv->smu_tables.entry[PPTABLE].table);
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[WMTABLE].handle,
					&priv->smu_tables.entry[WMTABLE].mc_addr,
					&priv->smu_tables.entry[WMTABLE].table);
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[AVFSTABLE].handle,
					&priv->smu_tables.entry[AVFSTABLE].mc_addr,
					&priv->smu_tables.entry[AVFSTABLE].table);
		if (priv->smu_tables.entry[TOOLSTABLE].table)
			amdgpu_bo_free_kernel(&priv->smu_tables.entry[TOOLSTABLE].handle,
					&priv->smu_tables.entry[TOOLSTABLE].mc_addr,
					&priv->smu_tables.entry[TOOLSTABLE].table);
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[AVFSFUSETABLE].handle,
					&priv->smu_tables.entry[AVFSFUSETABLE].mc_addr,
					&priv->smu_tables.entry[AVFSFUSETABLE].table);
		kfree(hwmgr->smu_backend);
		hwmgr->smu_backend = NULL;
	}
	return 0;
}

static int vega10_start_smu(struct pp_hwmgr *hwmgr)
{
	if (!smu9_is_smc_ram_running(hwmgr))
		return -EINVAL;

	PP_ASSERT_WITH_CODE(!vega10_verify_smc_interface(hwmgr),
			"Failed to verify SMC interface!",
			return -EINVAL);

	vega10_set_tools_address(hwmgr);

	return 0;
}

static int vega10_smc_table_manager(struct pp_hwmgr *hwmgr, uint8_t *table,
				    uint16_t table_id, bool rw)
{
	int ret;

	if (rw)
		ret = vega10_copy_table_from_smc(hwmgr, table, table_id);
	else
		ret = vega10_copy_table_to_smc(hwmgr, table, table_id);

	return ret;
}

const struct pp_smumgr_func vega10_smu_funcs = {
	.smu_init = &vega10_smu_init,
	.smu_fini = &vega10_smu_fini,
	.start_smu = &vega10_start_smu,
	.request_smu_load_specific_fw = NULL,
	.send_msg_to_smc = &smu9_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = &smu9_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
	.is_dpm_running = vega10_is_dpm_running,
	.get_argument = smu9_get_argument,
	.smc_table_manager = vega10_smc_table_manager,
};
