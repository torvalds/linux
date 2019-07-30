/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
#include "amdgpu_smu.h"
#include "soc15_common.h"
#include "smu_v12_0_ppsmc.h"
#include "smu12_driver_if.h"
#include "smu_v12_0.h"
#include "renoir_ppt.h"


#define MSG_MAP(msg, index) \
	[SMU_MSG_##msg] = {1, (index)}

#define TAB_MAP_VALID(tab) \
	[SMU_TABLE_##tab] = {1, TABLE_##tab}

#define TAB_MAP_INVALID(tab) \
	[SMU_TABLE_##tab] = {0, TABLE_##tab}

static struct smu_12_0_cmn2aisc_mapping renoir_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,                    PPSMC_MSG_TestMessage),
	MSG_MAP(GetSmuVersion,                  PPSMC_MSG_GetSmuVersion),
	MSG_MAP(GetDriverIfVersion,             PPSMC_MSG_GetDriverIfVersion),
	MSG_MAP(PowerUpGfx,                     PPSMC_MSG_PowerUpGfx),
	MSG_MAP(AllowGfxOff,                    PPSMC_MSG_EnableGfxOff),
	MSG_MAP(DisallowGfxOff,                 PPSMC_MSG_DisableGfxOff),
	MSG_MAP(PowerDownIspByTile,             PPSMC_MSG_PowerDownIspByTile),
	MSG_MAP(PowerUpIspByTile,               PPSMC_MSG_PowerUpIspByTile),
	MSG_MAP(PowerDownVcn,                   PPSMC_MSG_PowerDownVcn),
	MSG_MAP(PowerUpVcn,                     PPSMC_MSG_PowerUpVcn),
	MSG_MAP(PowerDownSdma,                  PPSMC_MSG_PowerDownSdma),
	MSG_MAP(PowerUpSdma,                    PPSMC_MSG_PowerUpSdma),
	MSG_MAP(SetHardMinIspclkByFreq,         PPSMC_MSG_SetHardMinIspclkByFreq),
	MSG_MAP(SetHardMinVcn,                  PPSMC_MSG_SetHardMinVcn),
	MSG_MAP(Spare1,                         PPSMC_MSG_spare1),
	MSG_MAP(Spare2,                         PPSMC_MSG_spare2),
	MSG_MAP(SetAllowFclkSwitch,             PPSMC_MSG_SetAllowFclkSwitch),
	MSG_MAP(SetMinVideoGfxclkFreq,          PPSMC_MSG_SetMinVideoGfxclkFreq),
	MSG_MAP(ActiveProcessNotify,            PPSMC_MSG_ActiveProcessNotify),
	MSG_MAP(SetCustomPolicy,                PPSMC_MSG_SetCustomPolicy),
	MSG_MAP(SetVideoFps,                    PPSMC_MSG_SetVideoFps),
	MSG_MAP(NumOfDisplays,                  PPSMC_MSG_SetDisplayCount),
	MSG_MAP(QueryPowerLimit,                PPSMC_MSG_QueryPowerLimit),
	MSG_MAP(SetDriverDramAddrHigh,          PPSMC_MSG_SetDriverDramAddrHigh),
	MSG_MAP(SetDriverDramAddrLow,           PPSMC_MSG_SetDriverDramAddrLow),
	MSG_MAP(TransferTableSmu2Dram,          PPSMC_MSG_TransferTableSmu2Dram),
	MSG_MAP(TransferTableDram2Smu,          PPSMC_MSG_TransferTableDram2Smu),
	MSG_MAP(GfxDeviceDriverReset,           PPSMC_MSG_GfxDeviceDriverReset),
	MSG_MAP(SetGfxclkOverdriveByFreqVid,    PPSMC_MSG_SetGfxclkOverdriveByFreqVid),
	MSG_MAP(SetHardMinDcfclkByFreq,         PPSMC_MSG_SetHardMinDcfclkByFreq),
	MSG_MAP(SetHardMinSocclkByFreq,         PPSMC_MSG_SetHardMinSocclkByFreq),
	MSG_MAP(ControlIgpuATS,                 PPSMC_MSG_ControlIgpuATS),
	MSG_MAP(SetMinVideoFclkFreq,            PPSMC_MSG_SetMinVideoFclkFreq),
	MSG_MAP(SetMinDeepSleepDcfclk,          PPSMC_MSG_SetMinDeepSleepDcfclk),
	MSG_MAP(ForcePowerDownGfx,              PPSMC_MSG_ForcePowerDownGfx),
	MSG_MAP(SetPhyclkVoltageByFreq,         PPSMC_MSG_SetPhyclkVoltageByFreq),
	MSG_MAP(SetDppclkVoltageByFreq,         PPSMC_MSG_SetDppclkVoltageByFreq),
	MSG_MAP(SetSoftMinVcn,                  PPSMC_MSG_SetSoftMinVcn),
	MSG_MAP(EnablePostCode,                 PPSMC_MSG_EnablePostCode),
	MSG_MAP(GetGfxclkFrequency,             PPSMC_MSG_GetGfxclkFrequency),
	MSG_MAP(GetFclkFrequency,               PPSMC_MSG_GetFclkFrequency),
	MSG_MAP(GetMinGfxclkFrequency,          PPSMC_MSG_GetMinGfxclkFrequency),
	MSG_MAP(GetMaxGfxclkFrequency,          PPSMC_MSG_GetMaxGfxclkFrequency),
	MSG_MAP(SoftReset,                      PPSMC_MSG_SoftReset),
	MSG_MAP(SetGfxCGPG,                     PPSMC_MSG_SetGfxCGPG),
	MSG_MAP(SetSoftMaxGfxClk,               PPSMC_MSG_SetSoftMaxGfxClk),
	MSG_MAP(SetHardMinGfxClk,               PPSMC_MSG_SetHardMinGfxClk),
	MSG_MAP(SetSoftMaxSocclkByFreq,         PPSMC_MSG_SetSoftMaxSocclkByFreq),
	MSG_MAP(SetSoftMaxFclkByFreq,           PPSMC_MSG_SetSoftMaxFclkByFreq),
	MSG_MAP(SetSoftMaxVcn,                  PPSMC_MSG_SetSoftMaxVcn),
	MSG_MAP(PowerGateMmHub,                 PPSMC_MSG_PowerGateMmHub),
	MSG_MAP(UpdatePmeRestore,               PPSMC_MSG_UpdatePmeRestore),
	MSG_MAP(GpuChangeState,                 PPSMC_MSG_GpuChangeState),
	MSG_MAP(SetPowerLimitPercentage,        PPSMC_MSG_SetPowerLimitPercentage),
	MSG_MAP(ForceGfxContentSave,            PPSMC_MSG_ForceGfxContentSave),
	MSG_MAP(EnableTmdp48MHzRefclkPwrDown,   PPSMC_MSG_EnableTmdp48MHzRefclkPwrDown),
	MSG_MAP(PowerDownJpeg,                  PPSMC_MSG_PowerDownJpeg),
	MSG_MAP(PowerUpJpeg,                    PPSMC_MSG_PowerUpJpeg),
	MSG_MAP(PowerGateAtHub,                 PPSMC_MSG_PowerGateAtHub),
	MSG_MAP(SetSoftMinJpeg,                 PPSMC_MSG_SetSoftMinJpeg),
	MSG_MAP(SetHardMinFclkByFreq,           PPSMC_MSG_SetHardMinFclkByFreq),
};

static struct smu_12_0_cmn2aisc_mapping renoir_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP_VALID(WATERMARKS),
	TAB_MAP_INVALID(CUSTOM_DPM),
	TAB_MAP_VALID(DPMCLOCKS),
	TAB_MAP_VALID(SMU_METRICS),
};

static int renoir_get_smu_msg_index(struct smu_context *smc, uint32_t index)
{
	struct smu_12_0_cmn2aisc_mapping mapping;

	if (index >= SMU_MSG_MAX_COUNT)
		return -EINVAL;

	mapping = renoir_message_map[index];
	if (!(mapping.valid_mapping))
		return -EINVAL;

	return mapping.map_to;
}

static int renoir_get_smu_table_index(struct smu_context *smc, uint32_t index)
{
	struct smu_12_0_cmn2aisc_mapping mapping;

	if (index >= SMU_TABLE_COUNT)
		return -EINVAL;

	mapping = renoir_table_map[index];
	if (!(mapping.valid_mapping))
		return -EINVAL;

	return mapping.map_to;
}

static int renoir_tables_init(struct smu_context *smu, struct smu_table *tables)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	SMU_TABLE_INIT(tables, SMU_TABLE_WATERMARKS, sizeof(Watermarks_t),
		PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_DPMCLOCKS, sizeof(DpmClocks_t),
		PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS, sizeof(SmuMetrics_t),
		PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);

	smu_table->clocks_table = kzalloc(sizeof(DpmClocks_t), GFP_KERNEL);
	if (!smu_table->clocks_table)
		return -ENOMEM;

	return 0;
}

static const struct pptable_funcs renoir_ppt_funcs = {
	.get_smu_msg_index = renoir_get_smu_msg_index,
	.get_smu_table_index = renoir_get_smu_table_index,
	.tables_init = renoir_tables_init,
	.set_power_state = NULL,
};

void renoir_set_ppt_funcs(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	smu->ppt_funcs = &renoir_ppt_funcs;
	smu->smc_if_version = SMU12_DRIVER_IF_VERSION;
	smu_table->table_count = TABLE_COUNT;
}
