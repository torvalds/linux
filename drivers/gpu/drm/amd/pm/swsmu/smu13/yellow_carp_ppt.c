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

#define SWSMU_CODE_LAYER_L2

#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_v13_0_1.h"
#include "smu13_driver_if_yellow_carp.h"
#include "yellow_carp_ppt.h"
#include "smu_v13_0_1_ppsmc.h"
#include "smu_v13_0_1_pmfw.h"
#include "smu_cmn.h"

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

static struct cmn2asic_msg_mapping yellow_carp_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,                    PPSMC_MSG_TestMessage,			1),
	MSG_MAP(GetSmuVersion,                  PPSMC_MSG_GetSmuVersion,		1),
	MSG_MAP(GetDriverIfVersion,             PPSMC_MSG_GetDriverIfVersion,		1),
	MSG_MAP(EnableGfxOff,                   PPSMC_MSG_EnableGfxOff,			1),
	MSG_MAP(AllowGfxOff,                    PPSMC_MSG_AllowGfxOff,			1),
	MSG_MAP(DisallowGfxOff,                 PPSMC_MSG_DisallowGfxOff,		1),
	MSG_MAP(PowerDownVcn,                   PPSMC_MSG_PowerDownVcn,			1),
	MSG_MAP(PowerUpVcn,                     PPSMC_MSG_PowerUpVcn,			1),
	MSG_MAP(SetHardMinVcn,                  PPSMC_MSG_SetHardMinVcn,		1),
	MSG_MAP(ActiveProcessNotify,            PPSMC_MSG_ActiveProcessNotify,		1),
	MSG_MAP(SetDriverDramAddrHigh,          PPSMC_MSG_SetDriverDramAddrHigh,	1),
	MSG_MAP(SetDriverDramAddrLow,           PPSMC_MSG_SetDriverDramAddrLow,		1),
	MSG_MAP(TransferTableSmu2Dram,          PPSMC_MSG_TransferTableSmu2Dram,	1),
	MSG_MAP(TransferTableDram2Smu,          PPSMC_MSG_TransferTableDram2Smu,	1),
	MSG_MAP(GfxDeviceDriverReset,           PPSMC_MSG_GfxDeviceDriverReset,		1),
	MSG_MAP(GetEnabledSmuFeatures,          PPSMC_MSG_GetEnabledSmuFeatures,	1),
	MSG_MAP(SetHardMinSocclkByFreq,         PPSMC_MSG_SetHardMinSocclkByFreq,	1),
	MSG_MAP(SetSoftMinVcn,                  PPSMC_MSG_SetSoftMinVcn,		1),
	MSG_MAP(GetGfxclkFrequency,             PPSMC_MSG_GetGfxclkFrequency,		1),
	MSG_MAP(GetFclkFrequency,               PPSMC_MSG_GetFclkFrequency,		1),
	MSG_MAP(SetSoftMaxGfxClk,               PPSMC_MSG_SetSoftMaxGfxClk,		1),
	MSG_MAP(SetHardMinGfxClk,               PPSMC_MSG_SetHardMinGfxClk,		1),
	MSG_MAP(SetSoftMaxSocclkByFreq,         PPSMC_MSG_SetSoftMaxSocclkByFreq,	1),
	MSG_MAP(SetSoftMaxFclkByFreq,           PPSMC_MSG_SetSoftMaxFclkByFreq,		1),
	MSG_MAP(SetSoftMaxVcn,                  PPSMC_MSG_SetSoftMaxVcn,		1),
	MSG_MAP(SetPowerLimitPercentage,        PPSMC_MSG_SetPowerLimitPercentage,	1),
	MSG_MAP(PowerDownJpeg,                  PPSMC_MSG_PowerDownJpeg,		1),
	MSG_MAP(PowerUpJpeg,                    PPSMC_MSG_PowerUpJpeg,			1),
	MSG_MAP(SetHardMinFclkByFreq,           PPSMC_MSG_SetHardMinFclkByFreq,		1),
	MSG_MAP(SetSoftMinSocclkByFreq,         PPSMC_MSG_SetSoftMinSocclkByFreq,	1),
};

static struct cmn2asic_mapping yellow_carp_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP_VALID(WATERMARKS),
	TAB_MAP_VALID(SMU_METRICS),
	TAB_MAP_VALID(CUSTOM_DPM),
	TAB_MAP_VALID(DPMCLOCKS),
};
	
static int yellow_carp_init_smc_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;

	SMU_TABLE_INIT(tables, SMU_TABLE_WATERMARKS, sizeof(Watermarks_t),
		PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_DPMCLOCKS, sizeof(DpmClocks_t),
		PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS, sizeof(SmuMetrics_t),
		PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);

	smu_table->clocks_table = kzalloc(sizeof(DpmClocks_t), GFP_KERNEL);
	if (!smu_table->clocks_table)
		goto err0_out;

	smu_table->metrics_table = kzalloc(sizeof(SmuMetrics_t), GFP_KERNEL);
	if (!smu_table->metrics_table)
		goto err1_out;
	smu_table->metrics_time = 0;

	smu_table->watermarks_table = kzalloc(sizeof(Watermarks_t), GFP_KERNEL);
	if (!smu_table->watermarks_table)
		goto err2_out;

	smu_table->gpu_metrics_table_size = sizeof(struct gpu_metrics_v2_0);
	smu_table->gpu_metrics_table = kzalloc(smu_table->gpu_metrics_table_size, GFP_KERNEL);
	if (!smu_table->gpu_metrics_table)
		goto err3_out;

	return 0;

err3_out:
	kfree(smu_table->watermarks_table);
err2_out:
	kfree(smu_table->metrics_table);
err1_out:
	kfree(smu_table->clocks_table);
err0_out:
	return -ENOMEM;
}

static bool yellow_carp_is_dpm_running(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	/*
	 * Until now, the pmfw hasn't exported the interface of SMU
	 * feature mask to APU SKU so just force on all the feature
	 * at early initial stage.
	 */
	if (adev->in_suspend)
		return false;
	else
		return true;

}

static const struct pptable_funcs yellow_carp_ppt_funcs = {
	.check_fw_status = smu_v13_0_1_check_fw_status,
	.check_fw_version = smu_v13_0_1_check_fw_version,
	.init_smc_tables = yellow_carp_init_smc_tables,
	.fini_smc_tables = smu_v13_0_1_fini_smc_tables,
	.send_smc_msg_with_param = smu_cmn_send_smc_msg_with_param,
	.send_smc_msg = smu_cmn_send_smc_msg,
	.set_default_dpm_table = smu_v13_0_1_set_default_dpm_tables,
	.is_dpm_running = yellow_carp_is_dpm_running,
	.get_enabled_mask = smu_cmn_get_enabled_32_bits_mask,
	.get_pp_feature_mask = smu_cmn_get_pp_feature_mask,
	.set_driver_table_location = smu_v13_0_1_set_driver_table_location,
	.gfx_off_control = smu_v13_0_1_gfx_off_control,
};

void yellow_carp_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &yellow_carp_ppt_funcs;
	smu->message_map = yellow_carp_message_map;
	smu->table_map = yellow_carp_table_map;
	smu->is_apu = true;
}
