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
#include "smu10_inc.h"
#include "soc15_common.h"
#include "smu10_smumgr.h"
#include "ppatomctrl.h"
#include "rv_ppsmc.h"
#include "smu10_driver_if.h"
#include "smu10.h"
#include "ppatomctrl.h"
#include "pp_debug.h"


#define BUFFER_SIZE                 80000
#define MAX_STRING_SIZE             15
#define BUFFER_SIZETWO              131072

#define MP0_Public                  0x03800000
#define MP0_SRAM                    0x03900000
#define MP1_Public                  0x03b00000
#define MP1_SRAM                    0x03c00004

#define smnMP1_FIRMWARE_FLAGS       0x3010028


static uint32_t smu10_wait_for_response(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t reg;

	reg = SOC15_REG_OFFSET(MP1, 0, mmMP1_SMN_C2PMSG_90);

	phm_wait_for_register_unequal(hwmgr, reg,
			0, MP1_C2PMSG_90__CONTENT_MASK);

	return RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90);
}

static int smu10_send_msg_to_smc_without_waiting(struct pp_hwmgr *hwmgr,
		uint16_t msg)
{
	struct amdgpu_device *adev = hwmgr->adev;

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_66, msg);

	return 0;
}

static uint32_t smu10_read_arg_from_smc(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	return RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82);
}

static int smu10_send_msg_to_smc(struct pp_hwmgr *hwmgr, uint16_t msg)
{
	struct amdgpu_device *adev = hwmgr->adev;

	smu10_wait_for_response(hwmgr);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	smu10_send_msg_to_smc_without_waiting(hwmgr, msg);

	if (smu10_wait_for_response(hwmgr) == 0)
		printk("Failed to send Message %x.\n", msg);

	return 0;
}


static int smu10_send_msg_to_smc_with_parameter(struct pp_hwmgr *hwmgr,
		uint16_t msg, uint32_t parameter)
{
	struct amdgpu_device *adev = hwmgr->adev;

	smu10_wait_for_response(hwmgr);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82, parameter);

	smu10_send_msg_to_smc_without_waiting(hwmgr, msg);


	if (smu10_wait_for_response(hwmgr) == 0)
		printk("Failed to send Message %x.\n", msg);

	return 0;
}

static int smu10_copy_table_from_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id)
{
	struct smu10_smumgr *priv =
			(struct smu10_smumgr *)(hwmgr->smu_backend);

	PP_ASSERT_WITH_CODE(table_id < MAX_SMU_TABLE,
			"Invalid SMU Table ID!", return -EINVAL;);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].version != 0,
			"Invalid SMU Table version!", return -EINVAL;);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].size != 0,
			"Invalid SMU Table Length!", return -EINVAL;);
	smu10_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			upper_32_bits(priv->smu_tables.entry[table_id].mc_addr));
	smu10_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			lower_32_bits(priv->smu_tables.entry[table_id].mc_addr));
	smu10_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableSmu2Dram,
			priv->smu_tables.entry[table_id].table_id);

	memcpy(table, (uint8_t *)priv->smu_tables.entry[table_id].table,
			priv->smu_tables.entry[table_id].size);

	return 0;
}

static int smu10_copy_table_to_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id)
{
	struct smu10_smumgr *priv =
			(struct smu10_smumgr *)(hwmgr->smu_backend);

	PP_ASSERT_WITH_CODE(table_id < MAX_SMU_TABLE,
			"Invalid SMU Table ID!", return -EINVAL;);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].version != 0,
			"Invalid SMU Table version!", return -EINVAL;);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].size != 0,
			"Invalid SMU Table Length!", return -EINVAL;);

	memcpy(priv->smu_tables.entry[table_id].table, table,
			priv->smu_tables.entry[table_id].size);

	smu10_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			upper_32_bits(priv->smu_tables.entry[table_id].mc_addr));
	smu10_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			lower_32_bits(priv->smu_tables.entry[table_id].mc_addr));
	smu10_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableDram2Smu,
			priv->smu_tables.entry[table_id].table_id);

	return 0;
}

static int smu10_verify_smc_interface(struct pp_hwmgr *hwmgr)
{
	uint32_t smc_driver_if_version;

	smu10_send_msg_to_smc(hwmgr,
			PPSMC_MSG_GetDriverIfVersion);
	smc_driver_if_version = smu10_read_arg_from_smc(hwmgr);

	if ((smc_driver_if_version != SMU10_DRIVER_IF_VERSION) &&
	    (smc_driver_if_version != SMU10_DRIVER_IF_VERSION + 1)) {
		pr_err("Attempt to read SMC IF Version Number Failed!\n");
		return -EINVAL;
	}

	return 0;
}

static int smu10_smu_fini(struct pp_hwmgr *hwmgr)
{
	struct smu10_smumgr *priv =
			(struct smu10_smumgr *)(hwmgr->smu_backend);

	if (priv) {
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[SMU10_WMTABLE].handle,
					&priv->smu_tables.entry[SMU10_WMTABLE].mc_addr,
					&priv->smu_tables.entry[SMU10_WMTABLE].table);
		amdgpu_bo_free_kernel(&priv->smu_tables.entry[SMU10_CLOCKTABLE].handle,
					&priv->smu_tables.entry[SMU10_CLOCKTABLE].mc_addr,
					&priv->smu_tables.entry[SMU10_CLOCKTABLE].table);
		kfree(hwmgr->smu_backend);
		hwmgr->smu_backend = NULL;
	}

	return 0;
}

static int smu10_start_smu(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetSmuVersion);
	hwmgr->smu_version = smu10_read_arg_from_smc(hwmgr);
	adev->pm.fw_version = hwmgr->smu_version >> 8;

	if (smu10_verify_smc_interface(hwmgr))
		return -EINVAL;

	return 0;
}

static int smu10_smu_init(struct pp_hwmgr *hwmgr)
{
	struct smu10_smumgr *priv;
	int r;

	priv = kzalloc(sizeof(struct smu10_smumgr), GFP_KERNEL);

	if (!priv)
		return -ENOMEM;

	hwmgr->smu_backend = priv;

	/* allocate space for watermarks table */
	r = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
			sizeof(Watermarks_t),
			PAGE_SIZE,
			AMDGPU_GEM_DOMAIN_VRAM,
			&priv->smu_tables.entry[SMU10_WMTABLE].handle,
			&priv->smu_tables.entry[SMU10_WMTABLE].mc_addr,
			&priv->smu_tables.entry[SMU10_WMTABLE].table);

	if (r)
		goto err0;

	priv->smu_tables.entry[SMU10_WMTABLE].version = 0x01;
	priv->smu_tables.entry[SMU10_WMTABLE].size = sizeof(Watermarks_t);
	priv->smu_tables.entry[SMU10_WMTABLE].table_id = TABLE_WATERMARKS;

	/* allocate space for watermarks table */
	r = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
			sizeof(DpmClocks_t),
			PAGE_SIZE,
			AMDGPU_GEM_DOMAIN_VRAM,
			&priv->smu_tables.entry[SMU10_CLOCKTABLE].handle,
			&priv->smu_tables.entry[SMU10_CLOCKTABLE].mc_addr,
			&priv->smu_tables.entry[SMU10_CLOCKTABLE].table);

	if (r)
		goto err1;

	priv->smu_tables.entry[SMU10_CLOCKTABLE].version = 0x01;
	priv->smu_tables.entry[SMU10_CLOCKTABLE].size = sizeof(DpmClocks_t);
	priv->smu_tables.entry[SMU10_CLOCKTABLE].table_id = TABLE_DPMCLOCKS;

	return 0;

err1:
	amdgpu_bo_free_kernel(&priv->smu_tables.entry[SMU10_WMTABLE].handle,
				&priv->smu_tables.entry[SMU10_WMTABLE].mc_addr,
				&priv->smu_tables.entry[SMU10_WMTABLE].table);
err0:
	kfree(priv);
	return -EINVAL;
}

static int smu10_smc_table_manager(struct pp_hwmgr *hwmgr, uint8_t *table, uint16_t table_id, bool rw)
{
	int ret;

	if (rw)
		ret = smu10_copy_table_from_smc(hwmgr, table, table_id);
	else
		ret = smu10_copy_table_to_smc(hwmgr, table, table_id);

	return ret;
}


const struct pp_smumgr_func smu10_smu_funcs = {
	.smu_init = &smu10_smu_init,
	.smu_fini = &smu10_smu_fini,
	.start_smu = &smu10_start_smu,
	.request_smu_load_specific_fw = NULL,
	.send_msg_to_smc = &smu10_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = &smu10_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
	.get_argument = smu10_read_arg_from_smc,
	.smc_table_manager = smu10_smc_table_manager,
};


