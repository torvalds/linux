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
#include "rv_inc.h"
#include "pp_soc15.h"
#include "rv_smumgr.h"
#include "ppatomctrl.h"
#include "rv_ppsmc.h"
#include "smu10_driver_if.h"
#include "smu10.h"
#include "ppatomctrl.h"
#include "pp_debug.h"
#include "smu_ucode_xfer_vi.h"
#include "smu7_smumgr.h"

#define VOLTAGE_SCALE 4

#define BUFFER_SIZE                 80000
#define MAX_STRING_SIZE             15
#define BUFFER_SIZETWO              131072

#define MP0_Public                  0x03800000
#define MP0_SRAM                    0x03900000
#define MP1_Public                  0x03b00000
#define MP1_SRAM                    0x03c00004

#define smnMP1_FIRMWARE_FLAGS       0x3010028


bool rv_is_smc_ram_running(struct pp_hwmgr *hwmgr)
{
	uint32_t mp1_fw_flags, reg;

	reg = soc15_get_register_offset(NBIF_HWID, 0,
			mmPCIE_INDEX2_BASE_IDX, mmPCIE_INDEX2);

	cgs_write_register(hwmgr->device, reg,
			(MP1_Public | (smnMP1_FIRMWARE_FLAGS & 0xffffffff)));

	reg = soc15_get_register_offset(NBIF_HWID, 0,
			mmPCIE_DATA2_BASE_IDX, mmPCIE_DATA2);

	mp1_fw_flags = cgs_read_register(hwmgr->device, reg);

	if (mp1_fw_flags & MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED_MASK)
		return true;

	return false;
}

static uint32_t rv_wait_for_response(struct pp_hwmgr *hwmgr)
{
	uint32_t reg;

	if (!rv_is_smc_ram_running(hwmgr))
		return -EINVAL;

	reg = soc15_get_register_offset(MP1_HWID, 0,
			mmMP1_SMN_C2PMSG_90_BASE_IDX, mmMP1_SMN_C2PMSG_90);

	phm_wait_for_register_unequal(hwmgr, reg,
			0, MP1_C2PMSG_90__CONTENT_MASK);

	return cgs_read_register(hwmgr->device, reg);
}

int rv_send_msg_to_smc_without_waiting(struct pp_hwmgr *hwmgr,
		uint16_t msg)
{
	uint32_t reg;

	if (!rv_is_smc_ram_running(hwmgr))
		return -EINVAL;

	reg = soc15_get_register_offset(MP1_HWID, 0,
			mmMP1_SMN_C2PMSG_66_BASE_IDX, mmMP1_SMN_C2PMSG_66);
	cgs_write_register(hwmgr->device, reg, msg);

	return 0;
}

int rv_read_arg_from_smc(struct pp_hwmgr *hwmgr, uint32_t *arg)
{
	uint32_t reg;

	reg = soc15_get_register_offset(MP1_HWID, 0,
			mmMP1_SMN_C2PMSG_82_BASE_IDX, mmMP1_SMN_C2PMSG_82);

	*arg = cgs_read_register(hwmgr->device, reg);

	return 0;
}

int rv_send_msg_to_smc(struct pp_hwmgr *hwmgr, uint16_t msg)
{
	uint32_t reg;

	rv_wait_for_response(hwmgr);

	reg = soc15_get_register_offset(MP1_HWID, 0,
			mmMP1_SMN_C2PMSG_90_BASE_IDX, mmMP1_SMN_C2PMSG_90);
	cgs_write_register(hwmgr->device, reg, 0);

	rv_send_msg_to_smc_without_waiting(hwmgr, msg);

	if (rv_wait_for_response(hwmgr) == 0)
		printk("Failed to send Message %x.\n", msg);

	return 0;
}


int rv_send_msg_to_smc_with_parameter(struct pp_hwmgr *hwmgr,
		uint16_t msg, uint32_t parameter)
{
	uint32_t reg;

	rv_wait_for_response(hwmgr);

	reg = soc15_get_register_offset(MP1_HWID, 0,
			mmMP1_SMN_C2PMSG_90_BASE_IDX, mmMP1_SMN_C2PMSG_90);
	cgs_write_register(hwmgr->device, reg, 0);

	reg = soc15_get_register_offset(MP1_HWID, 0,
			mmMP1_SMN_C2PMSG_82_BASE_IDX, mmMP1_SMN_C2PMSG_82);
	cgs_write_register(hwmgr->device, reg, parameter);

	rv_send_msg_to_smc_without_waiting(hwmgr, msg);


	if (rv_wait_for_response(hwmgr) == 0)
		printk("Failed to send Message %x.\n", msg);

	return 0;
}

int rv_copy_table_from_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id)
{
	struct rv_smumgr *priv =
			(struct rv_smumgr *)(hwmgr->smu_backend);

	PP_ASSERT_WITH_CODE(table_id < MAX_SMU_TABLE,
			"Invalid SMU Table ID!", return -EINVAL;);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].version != 0,
			"Invalid SMU Table version!", return -EINVAL;);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].size != 0,
			"Invalid SMU Table Length!", return -EINVAL;);
	PP_ASSERT_WITH_CODE(rv_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			priv->smu_tables.entry[table_id].table_addr_high) == 0,
			"[CopyTableFromSMC] Attempt to Set Dram Addr High Failed!", return -EINVAL;);
	PP_ASSERT_WITH_CODE(rv_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			priv->smu_tables.entry[table_id].table_addr_low) == 0,
			"[CopyTableFromSMC] Attempt to Set Dram Addr Low Failed!",
			return -EINVAL;);
	PP_ASSERT_WITH_CODE(rv_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableSmu2Dram,
			priv->smu_tables.entry[table_id].table_id) == 0,
			"[CopyTableFromSMC] Attempt to Transfer Table From SMU Failed!",
			return -EINVAL;);

	memcpy(table, priv->smu_tables.entry[table_id].table,
			priv->smu_tables.entry[table_id].size);

	return 0;
}

int rv_copy_table_to_smc(struct pp_hwmgr *hwmgr,
		uint8_t *table, int16_t table_id)
{
	struct rv_smumgr *priv =
			(struct rv_smumgr *)(hwmgr->smu_backend);

	PP_ASSERT_WITH_CODE(table_id < MAX_SMU_TABLE,
			"Invalid SMU Table ID!", return -EINVAL;);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].version != 0,
			"Invalid SMU Table version!", return -EINVAL;);
	PP_ASSERT_WITH_CODE(priv->smu_tables.entry[table_id].size != 0,
			"Invalid SMU Table Length!", return -EINVAL;);

	memcpy(priv->smu_tables.entry[table_id].table, table,
			priv->smu_tables.entry[table_id].size);

	PP_ASSERT_WITH_CODE(rv_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrHigh,
			priv->smu_tables.entry[table_id].table_addr_high) == 0,
			"[CopyTableToSMC] Attempt to Set Dram Addr High Failed!",
			return -EINVAL;);
	PP_ASSERT_WITH_CODE(rv_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetDriverDramAddrLow,
			priv->smu_tables.entry[table_id].table_addr_low) == 0,
			"[CopyTableToSMC] Attempt to Set Dram Addr Low Failed!",
			return -EINVAL;);
	PP_ASSERT_WITH_CODE(rv_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_TransferTableDram2Smu,
			priv->smu_tables.entry[table_id].table_id) == 0,
			"[CopyTableToSMC] Attempt to Transfer Table To SMU Failed!",
			return -EINVAL;);

	return 0;
}

static int rv_verify_smc_interface(struct pp_hwmgr *hwmgr)
{
	uint32_t smc_driver_if_version;

	PP_ASSERT_WITH_CODE(!rv_send_msg_to_smc(hwmgr,
			PPSMC_MSG_GetDriverIfVersion),
			"Attempt to get SMC IF Version Number Failed!",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(!rv_read_arg_from_smc(hwmgr,
			&smc_driver_if_version),
			"Attempt to read SMC IF Version Number Failed!",
			return -EINVAL);

	if (smc_driver_if_version != SMU10_DRIVER_IF_VERSION)
		return -EINVAL;

	return 0;
}

/* sdma is disabled by default in vbios, need to re-enable in driver */
static int rv_smc_enable_sdma(struct pp_hwmgr *hwmgr)
{
	PP_ASSERT_WITH_CODE(!rv_send_msg_to_smc(hwmgr,
			PPSMC_MSG_PowerUpSdma),
			"Attempt to power up sdma Failed!",
			return -EINVAL);

	return 0;
}

static int rv_smc_disable_sdma(struct pp_hwmgr *hwmgr)
{
	PP_ASSERT_WITH_CODE(!rv_send_msg_to_smc(hwmgr,
			PPSMC_MSG_PowerDownSdma),
			"Attempt to power down sdma Failed!",
			return -EINVAL);

	return 0;
}

/* vcn is disabled by default in vbios, need to re-enable in driver */
static int rv_smc_enable_vcn(struct pp_hwmgr *hwmgr)
{
	PP_ASSERT_WITH_CODE(!rv_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_PowerUpVcn, 0),
			"Attempt to power up vcn Failed!",
			return -EINVAL);

	return 0;
}

static int rv_smc_disable_vcn(struct pp_hwmgr *hwmgr)
{
	PP_ASSERT_WITH_CODE(!rv_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_PowerDownVcn, 0),
			"Attempt to power down vcn Failed!",
			return -EINVAL);

	return 0;
}

static int rv_smu_fini(struct pp_hwmgr *hwmgr)
{
	struct rv_smumgr *priv =
			(struct rv_smumgr *)(hwmgr->smu_backend);

	if (priv) {
		rv_smc_disable_sdma(hwmgr);
		rv_smc_disable_vcn(hwmgr);
		cgs_free_gpu_mem(hwmgr->device,
				priv->smu_tables.entry[WMTABLE].handle);
		cgs_free_gpu_mem(hwmgr->device,
				priv->smu_tables.entry[CLOCKTABLE].handle);
		kfree(hwmgr->smu_backend);
		hwmgr->smu_backend = NULL;
	}

	return 0;
}

static int rv_start_smu(struct pp_hwmgr *hwmgr)
{
	struct cgs_firmware_info info = {0};

	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetSmuVersion);
	rv_read_arg_from_smc(hwmgr, &hwmgr->smu_version);
	info.version = hwmgr->smu_version >> 8;

	cgs_get_firmware_info(hwmgr->device, CGS_UCODE_ID_SMU, &info);

	if (rv_verify_smc_interface(hwmgr))
		return -EINVAL;
	if (rv_smc_enable_sdma(hwmgr))
		return -EINVAL;
	if (rv_smc_enable_vcn(hwmgr))
		return -EINVAL;

	return 0;
}

static int rv_smu_init(struct pp_hwmgr *hwmgr)
{
	struct rv_smumgr *priv;
	uint64_t mc_addr;
	void *kaddr = NULL;
	unsigned long handle;

	priv = kzalloc(sizeof(struct rv_smumgr), GFP_KERNEL);

	if (!priv)
		return -ENOMEM;

	hwmgr->smu_backend = priv;

	/* allocate space for watermarks table */
	smu_allocate_memory(hwmgr->device,
			sizeof(Watermarks_t),
			CGS_GPU_MEM_TYPE__GART_CACHEABLE,
			PAGE_SIZE,
			&mc_addr,
			&kaddr,
			&handle);

	PP_ASSERT_WITH_CODE(kaddr,
			"[rv_smu_init] Out of memory for wmtable.",
			kfree(hwmgr->smu_backend);
			hwmgr->smu_backend = NULL;
			return -EINVAL);

	priv->smu_tables.entry[WMTABLE].version = 0x01;
	priv->smu_tables.entry[WMTABLE].size = sizeof(Watermarks_t);
	priv->smu_tables.entry[WMTABLE].table_id = TABLE_WATERMARKS;
	priv->smu_tables.entry[WMTABLE].table_addr_high =
			smu_upper_32_bits(mc_addr);
	priv->smu_tables.entry[WMTABLE].table_addr_low =
			smu_lower_32_bits(mc_addr);
	priv->smu_tables.entry[WMTABLE].table = kaddr;
	priv->smu_tables.entry[WMTABLE].handle = handle;

	/* allocate space for watermarks table */
	smu_allocate_memory(hwmgr->device,
			sizeof(DpmClocks_t),
			CGS_GPU_MEM_TYPE__GART_CACHEABLE,
			PAGE_SIZE,
			&mc_addr,
			&kaddr,
			&handle);

	PP_ASSERT_WITH_CODE(kaddr,
			"[rv_smu_init] Out of memory for CLOCKTABLE.",
			cgs_free_gpu_mem(hwmgr->device,
			(cgs_handle_t)priv->smu_tables.entry[WMTABLE].handle);
			kfree(hwmgr->smu_backend);
			hwmgr->smu_backend = NULL;
			return -EINVAL);

	priv->smu_tables.entry[CLOCKTABLE].version = 0x01;
	priv->smu_tables.entry[CLOCKTABLE].size = sizeof(DpmClocks_t);
	priv->smu_tables.entry[CLOCKTABLE].table_id = TABLE_DPMCLOCKS;
	priv->smu_tables.entry[CLOCKTABLE].table_addr_high =
			smu_upper_32_bits(mc_addr);
	priv->smu_tables.entry[CLOCKTABLE].table_addr_low =
			smu_lower_32_bits(mc_addr);
	priv->smu_tables.entry[CLOCKTABLE].table = kaddr;
	priv->smu_tables.entry[CLOCKTABLE].handle = handle;

	return 0;
}

const struct pp_smumgr_func rv_smu_funcs = {
	.smu_init = &rv_smu_init,
	.smu_fini = &rv_smu_fini,
	.start_smu = &rv_start_smu,
	.request_smu_load_specific_fw = NULL,
	.send_msg_to_smc = &rv_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = &rv_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
};


