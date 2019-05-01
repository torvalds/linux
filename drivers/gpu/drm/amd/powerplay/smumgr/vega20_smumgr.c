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

#include "smumgr.h"
#include "vega20_inc.h"
#include "soc15_common.h"
#include "vega20_smumgr.h"
#include "vega20_ppsmc.h"
#include "smu11_driver_if.h"
#include "ppatomctrl.h"
#include "pp_debug.h"
#include "smu_ucode_xfer_vi.h"
#include "smu7_smumgr.h"
#include "vega20_hwmgr.h"

/* MP Apertures */
#define MP0_Public			0x03800000
#define MP0_SRAM			0x03900000
#define MP1_Public			0x03b00000
#define MP1_SRAM			0x03c00004

/* address block */
#define smnMP1_FIRMWARE_FLAGS		0x3010024
#define smnMP0_FW_INTF			0x30101c0
#define smnMP1_PUB_CTRL			0x3010b14

bool vega20_is_smc_ram_running(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t mp1_fw_flags;

	mp1_fw_flags = RREG32_PCIE(MP1_Public |
				   (smnMP1_FIRMWARE_FLAGS & 0xffffffff));

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
static uint32_t vega20_wait_for_response(struct pp_hwmgr *hwmgr)
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
static int vega20_send_msg_to_smc_without_waiting(struct pp_hwmgr *hwmgr,
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
static int vega20_send_msg_to_smc(struct pp_hwmgr *hwmgr, uint16_t msg)
{
	struct amdgpu_device *adev = hwmgr->adev;
	int ret = 0;

	vega20_wait_for_response(hwmgr);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	vega20_send_msg_to_smc_without_waiting(hwmgr, msg);

	ret = vega20_wait_for_response(hwmgr);
	if (ret != PPSMC_Result_OK)
		pr_err("Failed to send message 0x%x, response 0x%x\n", msg, ret);

	return (ret == PPSMC_Result_OK) ? 0 : -EIO;
}

/*
 * Send a message to the SMC with parameter
 * @param    hwmgr:  the address of the powerplay hardware manager.
 * @param    msg: the message to send.
 * @param    parameter: the parameter to send
 * @return   Always return 0.
 */
static int vega20_send_msg_to_smc_with_parameter(struct pp_hwmgr *hwmgr,
		uint16_t msg, uint32_t parameter)
{
	struct amdgpu_device *adev = hwmgr->adev;
	int ret = 0;

	vega20_wait_for_response(hwmgr);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82, parameter);

	vega20_send_msg_to_smc_without_waiting(hwmgr, msg);

	ret = vega20_wait_for_response(hwmgr);
	if (ret != PPSMC_Result_OK)
		pr_err("Failed to send message 0x%x, response 0x%x\n", msg, ret);

	return (ret == PPSMC_Result_OK) ? 0 : -EIO;
}

static uint32_t vega20_get_argument(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	return RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82);
}

/*
 * Copy table from SMC into driver FB
 * @param   hwmgr    the address of the HW manager
 * @param   table_id    the driver's table ID to copy from
 */
static int vega20_copy_table_from_smc(struct pp_hwmgr *hwmgr,
				      uint8_t *table, int16_t table_id)
{
	struct vega20_smumgr *priv =
			(struct vega20_smumgr *)(hwmgr->smu_backend);
	struct amdgpu_device *adev = hwmgr->adev;
	int ret = 0;

	PP_ASSERT_WITH_CODE(table_id < TABLE_COUNT,
			"Invalid SMU Table ID!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].version != 0,
			"Invalid SMU Table version!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].size != 0,
			"Invalid SMU Table Length!", return -EINVAL);

	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			upper_32_bits(priv->smu_tables.entry[table_id].mc_addr))) == 0,
			"[CopyTableFromSMC] Attempt to Set Dram Addr High Failed!",
			return ret);
	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			lower_32_bits(priv->smu_tables.entry[table_id].mc_addr))) == 0,
			"[CopyTableFromSMC] Attempt to Set Dram Addr Low Failed!",
			return ret);
	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableSmu2Dram, table_id)) == 0,
			"[CopyTableFromSMC] Attempt to Transfer Table From SMU Failed!",
			return ret);

	/* flush hdp cache */
	adev->nbio_funcs->hdp_flush(adev, NULL);

	memcpy(table, priv->smu_tables.entry[table_id].table,
			priv->smu_tables.entry[table_id].size);

	return 0;
}

/*
 * Copy table from Driver FB into SMC
 * @param   hwmgr    the address of the HW manager
 * @param   table_id    the table to copy from
 */
static int vega20_copy_table_to_smc(struct pp_hwmgr *hwmgr,
				    uint8_t *table, int16_t table_id)
{
	struct vega20_smumgr *priv =
			(struct vega20_smumgr *)(hwmgr->smu_backend);
	int ret = 0;

	PP_ASSERT_WITH_CODE(table_id < TABLE_COUNT,
			"Invalid SMU Table ID!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].version != 0,
			"Invalid SMU Table version!", return -EINVAL);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].size != 0,
			"Invalid SMU Table Length!", return -EINVAL);

	memcpy(priv->smu_tables.entry[table_id].table, table,
			priv->smu_tables.entry[table_id].size);

	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			upper_32_bits(priv->smu_tables.entry[table_id].mc_addr))) == 0,
			"[CopyTableToSMC] Attempt to Set Dram Addr High Failed!",
			return ret);
	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			lower_32_bits(priv->smu_tables.entry[table_id].mc_addr))) == 0,
			"[CopyTableToSMC] Attempt to Set Dram Addr Low Failed!",
			return ret);
	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableDram2Smu, table_id)) == 0,
			"[CopyTableToSMC] Attempt to Transfer Table To SMU Failed!",
			return ret);

	return 0;
}

int vega20_set_activity_monitor_coeff(struct pp_hwmgr *hwmgr,
		uint8_t *table, uint16_t workload_type)
{
	struct vega20_smumgr *priv =
			(struct vega20_smumgr *)(hwmgr->smu_backend);
	int ret = 0;

	memcpy(priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].table, table,
			priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].size);

	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			upper_32_bits(priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].mc_addr))) == 0,
			"[SetActivityMonitor] Attempt to Set Dram Addr High Failed!",
			return ret);
	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			lower_32_bits(priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].mc_addr))) == 0,
			"[SetActivityMonitor] Attempt to Set Dram Addr Low Failed!",
			return ret);
	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableDram2Smu, TABLE_ACTIVITY_MONITOR_COEFF | (workload_type << 16))) == 0,
			"[SetActivityMonitor] Attempt to Transfer Table To SMU Failed!",
			return ret);

	return 0;
}

int vega20_get_activity_monitor_coeff(struct pp_hwmgr *hwmgr,
		uint8_t *table, uint16_t workload_type)
{
	struct vega20_smumgr *priv =
			(struct vega20_smumgr *)(hwmgr->smu_backend);
	struct amdgpu_device *adev = hwmgr->adev;
	int ret = 0;

	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			upper_32_bits(priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].mc_addr))) == 0,
			"[GetActivityMonitor] Attempt to Set Dram Addr High Failed!",
			return ret);
	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			lower_32_bits(priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].mc_addr))) == 0,
			"[GetActivityMonitor] Attempt to Set Dram Addr Low Failed!",
			return ret);
	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableSmu2Dram,
			TABLE_ACTIVITY_MONITOR_COEFF | (workload_type << 16))) == 0,
			"[GetActivityMonitor] Attempt to Transfer Table From SMU Failed!",
			return ret);

	/* flush hdp cache */
	adev->nbio_funcs->hdp_flush(adev, NULL);

	memcpy(table, priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].table,
			priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].size);

	return 0;
}

int vega20_enable_smc_features(struct pp_hwmgr *hwmgr,
		bool enable, uint64_t feature_mask)
{
	uint32_t smu_features_low, smu_features_high;
	int ret = 0;

	smu_features_low = (uint32_t)((feature_mask & SMU_FEATURES_LOW_MASK) >> SMU_FEATURES_LOW_SHIFT);
	smu_features_high = (uint32_t)((feature_mask & SMU_FEATURES_HIGH_MASK) >> SMU_FEATURES_HIGH_SHIFT);

	if (enable) {
		PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_EnableSmuFeaturesLow, smu_features_low)) == 0,
				"[EnableDisableSMCFeatures] Attemp to enable SMU features Low failed!",
				return ret);
		PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_EnableSmuFeaturesHigh, smu_features_high)) == 0,
				"[EnableDisableSMCFeatures] Attemp to enable SMU features High failed!",
				return ret);
	} else {
		PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_DisableSmuFeaturesLow, smu_features_low)) == 0,
				"[EnableDisableSMCFeatures] Attemp to disable SMU features Low failed!",
				return ret);
		PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_DisableSmuFeaturesHigh, smu_features_high)) == 0,
				"[EnableDisableSMCFeatures] Attemp to disable SMU features High failed!",
				return ret);
	}

	return 0;
}

int vega20_get_enabled_smc_features(struct pp_hwmgr *hwmgr,
		uint64_t *features_enabled)
{
	uint32_t smc_features_low, smc_features_high;
	int ret = 0;

	if (features_enabled == NULL)
		return -EINVAL;

	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc(hwmgr,
			PPSMC_MSG_GetEnabledSmuFeaturesLow)) == 0,
			"[GetEnabledSMCFeatures] Attemp to get SMU features Low failed!",
			return ret);
	smc_features_low = vega20_get_argument(hwmgr);
	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc(hwmgr,
			PPSMC_MSG_GetEnabledSmuFeaturesHigh)) == 0,
			"[GetEnabledSMCFeatures] Attemp to get SMU features High failed!",
			return ret);
	smc_features_high = vega20_get_argument(hwmgr);

	*features_enabled = ((((uint64_t)smc_features_low << SMU_FEATURES_LOW_SHIFT) & SMU_FEATURES_LOW_MASK) |
			(((uint64_t)smc_features_high << SMU_FEATURES_HIGH_SHIFT) & SMU_FEATURES_HIGH_MASK));

	return 0;
}

static int vega20_set_tools_address(struct pp_hwmgr *hwmgr)
{
	struct vega20_smumgr *priv =
			(struct vega20_smumgr *)(hwmgr->smu_backend);
	int ret = 0;

	if (priv->smu_tables.entry[TABLE_PMSTATUSLOG].mc_addr) {
		ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetToolsDramAddrHigh,
				upper_32_bits(priv->smu_tables.entry[TABLE_PMSTATUSLOG].mc_addr));
		if (!ret)
			ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetToolsDramAddrLow,
					lower_32_bits(priv->smu_tables.entry[TABLE_PMSTATUSLOG].mc_addr));
	}

	return ret;
}

int vega20_set_pptable_driver_address(struct pp_hwmgr *hwmgr)
{
	struct vega20_smumgr *priv =
			(struct vega20_smumgr *)(hwmgr->smu_backend);
	int ret = 0;

	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			upper_32_bits(priv->smu_tables.entry[TABLE_PPTABLE].mc_addr))) == 0,
			"[SetPPtabeDriverAddress] Attempt to Set Dram Addr High Failed!",
			return ret);
	PP_ASSERT_WITH_CODE((ret = vega20_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			lower_32_bits(priv->smu_tables.entry[TABLE_PPTABLE].mc_addr))) == 0,
			"[SetPPtabeDriverAddress] Attempt to Set Dram Addr Low Failed!",
			return ret);

	return ret;
}

static int vega20_smu_init(struct pp_hwmgr *hwmgr)
{
	struct vega20_smumgr *priv;
	unsigned long tools_size = 0x19000;
	int ret = 0;

	struct cgs_firmware_info info = {0};

	ret = cgs_get_firmware_info(hwmgr->device,
				smu7_convert_fw_type_to_cgs(UCODE_ID_SMU),
				&info);
	if (ret || !info.kptr)
		return -EINVAL;

	priv = kzalloc(sizeof(struct vega20_smumgr), GFP_KERNEL);
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

	/* allocate space for pmstatuslog table */
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

	/* allocate space for OverDrive table */
	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
			sizeof(OverDriveTable_t),
			PAGE_SIZE,
			AMDGPU_GEM_DOMAIN_VRAM,
			&priv->smu_tables.entry[TABLE_OVERDRIVE].handle,
			&priv->smu_tables.entry[TABLE_OVERDRIVE].mc_addr,
			&priv->smu_tables.entry[TABLE_OVERDRIVE].table);
	if (ret)
		goto err2;

	priv->smu_tables.entry[TABLE_OVERDRIVE].version = 0x01;
	priv->smu_tables.entry[TABLE_OVERDRIVE].size = sizeof(OverDriveTable_t);

	/* allocate space for SmuMetrics table */
	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
			sizeof(SmuMetrics_t),
			PAGE_SIZE,
			AMDGPU_GEM_DOMAIN_VRAM,
			&priv->smu_tables.entry[TABLE_SMU_METRICS].handle,
			&priv->smu_tables.entry[TABLE_SMU_METRICS].mc_addr,
			&priv->smu_tables.entry[TABLE_SMU_METRICS].table);
	if (ret)
		goto err3;

	priv->smu_tables.entry[TABLE_SMU_METRICS].version = 0x01;
	priv->smu_tables.entry[TABLE_SMU_METRICS].size = sizeof(SmuMetrics_t);

	/* allocate space for ActivityMonitor table */
	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
			sizeof(DpmActivityMonitorCoeffInt_t),
			PAGE_SIZE,
			AMDGPU_GEM_DOMAIN_VRAM,
			&priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].handle,
			&priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].mc_addr,
			&priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].table);
	if (ret)
		goto err4;

	priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].version = 0x01;
	priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].size = sizeof(DpmActivityMonitorCoeffInt_t);

	return 0;

err4:
	amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_SMU_METRICS].handle,
			&priv->smu_tables.entry[TABLE_SMU_METRICS].mc_addr,
			&priv->smu_tables.entry[TABLE_SMU_METRICS].table);
err3:
	amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_OVERDRIVE].handle,
			&priv->smu_tables.entry[TABLE_OVERDRIVE].mc_addr,
			&priv->smu_tables.entry[TABLE_OVERDRIVE].table);
err2:
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

static int vega20_smu_fini(struct pp_hwmgr *hwmgr)
{
	struct vega20_smumgr *priv =
			(struct vega20_smumgr *)(hwmgr->smu_backend);

	if (priv) {
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_PPTABLE].handle,
				&priv->smu_tables.entry[TABLE_PPTABLE].mc_addr,
				&priv->smu_tables.entry[TABLE_PPTABLE].table);
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_WATERMARKS].handle,
				&priv->smu_tables.entry[TABLE_WATERMARKS].mc_addr,
				&priv->smu_tables.entry[TABLE_WATERMARKS].table);
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_PMSTATUSLOG].handle,
				&priv->smu_tables.entry[TABLE_PMSTATUSLOG].mc_addr,
				&priv->smu_tables.entry[TABLE_PMSTATUSLOG].table);
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_OVERDRIVE].handle,
				&priv->smu_tables.entry[TABLE_OVERDRIVE].mc_addr,
				&priv->smu_tables.entry[TABLE_OVERDRIVE].table);
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_SMU_METRICS].handle,
				&priv->smu_tables.entry[TABLE_SMU_METRICS].mc_addr,
				&priv->smu_tables.entry[TABLE_SMU_METRICS].table);
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].handle,
				&priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].mc_addr,
				&priv->smu_tables.entry[TABLE_ACTIVITY_MONITOR_COEFF].table);
		kfree(hwmgr->smu_backend);
		hwmgr->smu_backend = NULL;
	}
	return 0;
}

static int vega20_start_smu(struct pp_hwmgr *hwmgr)
{
	int ret;

	ret = vega20_is_smc_ram_running(hwmgr);
	PP_ASSERT_WITH_CODE(ret,
			"[Vega20StartSmu] SMC is not running!",
			return -EINVAL);

	ret = vega20_set_tools_address(hwmgr);
	PP_ASSERT_WITH_CODE(!ret,
			"[Vega20StartSmu] Failed to set tools address!",
			return ret);

	return 0;
}

static bool vega20_is_dpm_running(struct pp_hwmgr *hwmgr)
{
	uint64_t features_enabled = 0;

	vega20_get_enabled_smc_features(hwmgr, &features_enabled);

	if (features_enabled & SMC_DPM_FEATURES)
		return true;
	else
		return false;
}

static int vega20_smc_table_manager(struct pp_hwmgr *hwmgr, uint8_t *table,
				    uint16_t table_id, bool rw)
{
	int ret;

	if (rw)
		ret = vega20_copy_table_from_smc(hwmgr, table, table_id);
	else
		ret = vega20_copy_table_to_smc(hwmgr, table, table_id);

	return ret;
}

const struct pp_smumgr_func vega20_smu_funcs = {
	.name = "vega20_smu",
	.smu_init = &vega20_smu_init,
	.smu_fini = &vega20_smu_fini,
	.start_smu = &vega20_start_smu,
	.request_smu_load_specific_fw = NULL,
	.send_msg_to_smc = &vega20_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = &vega20_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
	.is_dpm_running = vega20_is_dpm_running,
	.get_argument = vega20_get_argument,
	.smc_table_manager = vega20_smc_table_manager,
};
