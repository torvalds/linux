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
#include "vega12_smumgr.h"
#include "vega12_ppsmc.h"
#include "vega12/smu9_driver_if.h"

#include "ppatomctrl.h"
#include "pp_debug.h"


/* MP Apertures */
#define MP0_Public                  0x03800000
#define MP0_SRAM                    0x03900000
#define MP1_Public                  0x03b00000
#define MP1_SRAM                    0x03c00004

#define smnMP1_FIRMWARE_FLAGS                                                                           0x3010028
#define smnMP0_FW_INTF                                                                                  0x3010104
#define smnMP1_PUB_CTRL                                                                                 0x3010b14

static bool vega12_is_smc_ram_running(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t mp1_fw_flags;

	WREG32_SOC15(NBIF, 0, mmPCIE_INDEX2,
			(MP1_Public | (smnMP1_FIRMWARE_FLAGS & 0xffffffff)));

	mp1_fw_flags = RREG32_SOC15(NBIF, 0, mmPCIE_DATA2);

	if ((mp1_fw_flags & MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED_MASK) >>
				MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED__SHIFT)
		return true;

	return false;
}

/*
 * Check if SMC has responded to previous message.
 *
 * @param    smumgr  the address of the powerplay hardware manager.
 * @return   TRUE    SMC has responded, FALSE otherwise.
 */
static uint32_t vega12_wait_for_response(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t reg;

	reg = SOC15_REG_OFFSET(MP1, 0, mmMP1_SMN_C2PMSG_90);

	phm_wait_for_register_unequal(hwmgr, reg,
			0, MP1_C2PMSG_90__CONTENT_MASK);

	return RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90);
}

/*
 * Send a message to the SMC, and do not wait for its response.
 * @param    smumgr  the address of the powerplay hardware manager.
 * @param    msg the message to send.
 * @return   Always return 0.
 */
int vega12_send_msg_to_smc_without_waiting(struct pp_hwmgr *hwmgr,
		uint16_t msg)
{
	struct amdgpu_device *adev = hwmgr->adev;

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_66, msg);

	return 0;
}

/*
 * Send a message to the SMC, and wait for its response.
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @param    msg the message to send.
 * @return   Always return 0.
 */
int vega12_send_msg_to_smc(struct pp_hwmgr *hwmgr, uint16_t msg)
{
	struct amdgpu_device *adev = hwmgr->adev;

	vega12_wait_for_response(hwmgr);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	vega12_send_msg_to_smc_without_waiting(hwmgr, msg);

	if (vega12_wait_for_response(hwmgr) != 1)
		pr_err("Failed to send message: 0x%x\n", msg);

	return 0;
}

/*
 * Send a message to the SMC with parameter
 * @param    hwmgr:  the address of the powerplay hardware manager.
 * @param    msg: the message to send.
 * @param    parameter: the parameter to send
 * @return   Always return 0.
 */
int vega12_send_msg_to_smc_with_parameter(struct pp_hwmgr *hwmgr,
		uint16_t msg, uint32_t parameter)
{
	struct amdgpu_device *adev = hwmgr->adev;

	vega12_wait_for_response(hwmgr);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82, parameter);

	vega12_send_msg_to_smc_without_waiting(hwmgr, msg);

	if (vega12_wait_for_response(hwmgr) != 1)
		pr_err("Failed to send message: 0x%x\n", msg);

	return 0;
}


/*
 * Send a message to the SMC with parameter, do not wait for response
 * @param    hwmgr:  the address of the powerplay hardware manager.
 * @param    msg: the message to send.
 * @param    parameter: the parameter to send
 * @return   The response that came from the SMC.
 */
int vega12_send_msg_to_smc_with_parameter_without_waiting(
		struct pp_hwmgr *hwmgr, uint16_t msg, uint32_t parameter)
{
	struct amdgpu_device *adev = hwmgr->adev;

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_66, parameter);

	return vega12_send_msg_to_smc_without_waiting(hwmgr, msg);
}

/*
 * Retrieve an argument from SMC.
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @param    arg     pointer to store the argument from SMC.
 * @return   Always return 0.
 */
int vega12_read_arg_from_smc(struct pp_hwmgr *hwmgr, uint32_t *arg)
{
	struct amdgpu_device *adev = hwmgr->adev;

	*arg = RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82);

	return 0;
}

/*
 * Copy table from SMC into driver FB
 * @param   hwmgr    the address of the HW manager
 * @param   table_id    the driver's table ID to copy from
 */
int vega12_copy_table_from_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id)
{
	struct vega12_smumgr *priv =
			(struct vega12_smumgr *)(hwmgr->smu_backend);

	PP_ASSERT_WITH_CODE(table_id < TABLE_COUNT,
			"Invalid SMU Table ID!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].version != 0,
			"Invalid SMU Table version!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].size != 0,
			"Invalid SMU Table Length!", return -EINVAL);
	PP_ASSERT_WITH_CODE(vega12_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			upper_32_bits(priv->smu_tables.entry[table_id].mc_addr)) == 0,
			"[CopyTableFromSMC] Attempt to Set Dram Addr High Failed!", return -EINVAL);
	PP_ASSERT_WITH_CODE(vega12_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			lower_32_bits(priv->smu_tables.entry[table_id].mc_addr)) == 0,
			"[CopyTableFromSMC] Attempt to Set Dram Addr Low Failed!",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(vega12_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableSmu2Dram,
			table_id) == 0,
			"[CopyTableFromSMC] Attempt to Transfer Table From SMU Failed!",
			return -EINVAL);

	memcpy(table, priv->smu_tables.entry[table_id].table,
			priv->smu_tables.entry[table_id].size);

	return 0;
}

/*
 * Copy table from Driver FB into SMC
 * @param   hwmgr    the address of the HW manager
 * @param   table_id    the table to copy from
 */
int vega12_copy_table_to_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id)
{
	struct vega12_smumgr *priv =
			(struct vega12_smumgr *)(hwmgr->smu_backend);

	PP_ASSERT_WITH_CODE(table_id < TABLE_COUNT,
			"Invalid SMU Table ID!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].version != 0,
			"Invalid SMU Table version!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].size != 0,
			"Invalid SMU Table Length!", return -EINVAL);

	memcpy(priv->smu_tables.entry[table_id].table, table,
			priv->smu_tables.entry[table_id].size);

	PP_ASSERT_WITH_CODE(vega12_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			upper_32_bits(priv->smu_tables.entry[table_id].mc_addr)) == 0,
			"[CopyTableToSMC] Attempt to Set Dram Addr High Failed!",
			return -EINVAL;);
	PP_ASSERT_WITH_CODE(vega12_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			lower_32_bits(priv->smu_tables.entry[table_id].mc_addr)) == 0,
			"[CopyTableToSMC] Attempt to Set Dram Addr Low Failed!",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(vega12_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableDram2Smu,
			table_id) == 0,
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
		PP_ASSERT_WITH_CODE(vega12_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_EnableSmuFeaturesLow, smu_features_low) == 0,
				"[EnableDisableSMCFeatures] Attemp to enable SMU features Low failed!",
				return -EINVAL);
		PP_ASSERT_WITH_CODE(vega12_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_EnableSmuFeaturesHigh, smu_features_high) == 0,
				"[EnableDisableSMCFeatures] Attemp to enable SMU features High failed!",
				return -EINVAL);
	} else {
		PP_ASSERT_WITH_CODE(vega12_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_DisableSmuFeaturesLow, smu_features_low) == 0,
				"[EnableDisableSMCFeatures] Attemp to disable SMU features Low failed!",
				return -EINVAL);
		PP_ASSERT_WITH_CODE(vega12_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_DisableSmuFeaturesHigh, smu_features_high) == 0,
				"[EnableDisableSMCFeatures] Attemp to disable SMU features High failed!",
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

	PP_ASSERT_WITH_CODE(vega12_send_msg_to_smc(hwmgr,
			PPSMC_MSG_GetEnabledSmuFeaturesLow) == 0,
			"[GetEnabledSMCFeatures] Attemp to get SMU features Low failed!",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(vega12_read_arg_from_smc(hwmgr,
			&smc_features_low) == 0,
			"[GetEnabledSMCFeatures] Attemp to read SMU features Low argument failed!",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(vega12_send_msg_to_smc(hwmgr,
			PPSMC_MSG_GetEnabledSmuFeaturesHigh) == 0,
			"[GetEnabledSMCFeatures] Attemp to get SMU features High failed!",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(vega12_read_arg_from_smc(hwmgr,
			&smc_features_high) == 0,
			"[GetEnabledSMCFeatures] Attemp to read SMU features High argument failed!",
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
		if (!vega12_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetToolsDramAddrHigh,
				upper_32_bits(priv->smu_tables.entry[TABLE_PMSTATUSLOG].mc_addr)))
			vega12_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetToolsDramAddrLow,
					lower_32_bits(priv->smu_tables.entry[TABLE_PMSTATUSLOG].mc_addr));
	}
	return 0;
}

#if 0 /* tentatively remove */
static int vega12_verify_smc_interface(struct pp_hwmgr *hwmgr)
{
	uint32_t smc_driver_if_version;

	PP_ASSERT_WITH_CODE(!vega12_send_msg_to_smc(hwmgr,
			PPSMC_MSG_GetDriverIfVersion),
			"Attempt to get SMC IF Version Number Failed!",
			return -EINVAL);
	vega12_read_arg_from_smc(hwmgr, &smc_driver_if_version);

	if (smc_driver_if_version != SMU9_DRIVER_IF_VERSION) {
		pr_err("Your firmware(0x%x) doesn't match \
			SMU9_DRIVER_IF_VERSION(0x%x). \
			Please update your firmware!\n",
			smc_driver_if_version, SMU9_DRIVER_IF_VERSION);
		return -EINVAL;
	}

	return 0;
}
#endif

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
	if (tools_size) {
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
	}

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

	return 0;

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
		kfree(hwmgr->smu_backend);
		hwmgr->smu_backend = NULL;
	}
	return 0;
}

static int vega12_start_smu(struct pp_hwmgr *hwmgr)
{
	PP_ASSERT_WITH_CODE(vega12_is_smc_ram_running(hwmgr),
			"SMC is not running!",
			return -EINVAL);

#if 0 /* tentatively remove */
	PP_ASSERT_WITH_CODE(!vega12_verify_smc_interface(hwmgr),
			"Failed to verify SMC interface!",
			return -EINVAL);
#endif

	vega12_set_tools_address(hwmgr);

	return 0;
}

const struct pp_smumgr_func vega12_smu_funcs = {
	.smu_init = &vega12_smu_init,
	.smu_fini = &vega12_smu_fini,
	.start_smu = &vega12_start_smu,
	.request_smu_load_specific_fw = NULL,
	.send_msg_to_smc = &vega12_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = &vega12_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
	.is_dpm_running = vega12_is_dpm_running,
};
