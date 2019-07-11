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

#include "pp_debug.h"
#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "smu_v11_0.h"
#include "smu11_driver_if_arcturus.h"
#include "soc15_common.h"
#include "atom.h"
#include "power_state.h"
#include "arcturus_ppt.h"
#include "arcturus_ppsmc.h"
#include "nbio/nbio_7_4_sh_mask.h"

#define MSG_MAP(msg, index) \
	[SMU_MSG_##msg] = {1, (index)}

static struct smu_11_0_cmn2aisc_mapping arcturus_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,			     PPSMC_MSG_TestMessage),
	MSG_MAP(GetSmuVersion,			     PPSMC_MSG_GetSmuVersion),
	MSG_MAP(GetDriverIfVersion,		     PPSMC_MSG_GetDriverIfVersion),
	MSG_MAP(SetAllowedFeaturesMaskLow,	     PPSMC_MSG_SetAllowedFeaturesMaskLow),
	MSG_MAP(SetAllowedFeaturesMaskHigh,	     PPSMC_MSG_SetAllowedFeaturesMaskHigh),
	MSG_MAP(EnableAllSmuFeatures,		     PPSMC_MSG_EnableAllSmuFeatures),
	MSG_MAP(DisableAllSmuFeatures,		     PPSMC_MSG_DisableAllSmuFeatures),
	MSG_MAP(EnableSmuFeaturesLow,		     PPSMC_MSG_EnableSmuFeaturesLow),
	MSG_MAP(EnableSmuFeaturesHigh,		     PPSMC_MSG_EnableSmuFeaturesHigh),
	MSG_MAP(DisableSmuFeaturesLow,		     PPSMC_MSG_DisableSmuFeaturesLow),
	MSG_MAP(DisableSmuFeaturesHigh,		     PPSMC_MSG_DisableSmuFeaturesHigh),
	MSG_MAP(GetEnabledSmuFeaturesLow,	     PPSMC_MSG_GetEnabledSmuFeaturesLow),
	MSG_MAP(GetEnabledSmuFeaturesHigh,	     PPSMC_MSG_GetEnabledSmuFeaturesHigh),
	MSG_MAP(SetDriverDramAddrHigh,		     PPSMC_MSG_SetDriverDramAddrHigh),
	MSG_MAP(SetDriverDramAddrLow,		     PPSMC_MSG_SetDriverDramAddrLow),
	MSG_MAP(SetToolsDramAddrHigh,		     PPSMC_MSG_SetToolsDramAddrHigh),
	MSG_MAP(SetToolsDramAddrLow,		     PPSMC_MSG_SetToolsDramAddrLow),
	MSG_MAP(TransferTableSmu2Dram,		     PPSMC_MSG_TransferTableSmu2Dram),
	MSG_MAP(TransferTableDram2Smu,		     PPSMC_MSG_TransferTableDram2Smu),
	MSG_MAP(UseDefaultPPTable,		     PPSMC_MSG_UseDefaultPPTable),
	MSG_MAP(UseBackupPPTable,		     PPSMC_MSG_UseBackupPPTable),
	MSG_MAP(SetSystemVirtualDramAddrHigh,	     PPSMC_MSG_SetSystemVirtualDramAddrHigh),
	MSG_MAP(SetSystemVirtualDramAddrLow,	     PPSMC_MSG_SetSystemVirtualDramAddrLow),
	MSG_MAP(EnterBaco,			     PPSMC_MSG_EnterBaco),
	MSG_MAP(ExitBaco,			     PPSMC_MSG_ExitBaco),
	MSG_MAP(ArmD3,				     PPSMC_MSG_ArmD3),
	MSG_MAP(SetSoftMinByFreq,		     PPSMC_MSG_SetSoftMinByFreq),
	MSG_MAP(SetSoftMaxByFreq,		     PPSMC_MSG_SetSoftMaxByFreq),
	MSG_MAP(SetHardMinByFreq,		     PPSMC_MSG_SetHardMinByFreq),
	MSG_MAP(SetHardMaxByFreq,		     PPSMC_MSG_SetHardMaxByFreq),
	MSG_MAP(GetMinDpmFreq,			     PPSMC_MSG_GetMinDpmFreq),
	MSG_MAP(GetMaxDpmFreq,			     PPSMC_MSG_GetMaxDpmFreq),
	MSG_MAP(GetDpmFreqByIndex,		     PPSMC_MSG_GetDpmFreqByIndex),
	MSG_MAP(SetWorkloadMask,		     PPSMC_MSG_SetWorkloadMask),
	MSG_MAP(SetDfSwitchType,		     PPSMC_MSG_SetDfSwitchType),
	MSG_MAP(GetVoltageByDpm,		     PPSMC_MSG_GetVoltageByDpm),
	MSG_MAP(GetVoltageByDpmOverdrive,	     PPSMC_MSG_GetVoltageByDpmOverdrive),
	MSG_MAP(SetPptLimit,			     PPSMC_MSG_SetPptLimit),
	MSG_MAP(GetPptLimit,			     PPSMC_MSG_GetPptLimit),
	MSG_MAP(PowerUpVcn0,			     PPSMC_MSG_PowerUpVcn0),
	MSG_MAP(PowerDownVcn01,			     PPSMC_MSG_PowerDownVcn01),
	MSG_MAP(PowerUpVcn1,			     PPSMC_MSG_PowerUpVcn1),
	MSG_MAP(PowerDownVcn1,			     PPSMC_MSG_PowerDownVcn1),
	MSG_MAP(PrepareMp1ForUnload,		     PPSMC_MSG_PrepareMp1ForUnload),
	MSG_MAP(PrepareMp1ForReset,		     PPSMC_MSG_PrepareMp1ForReset),
	MSG_MAP(PrepareMp1ForShutdown,		     PPSMC_MSG_PrepareMp1ForShutdown),
	MSG_MAP(SoftReset,			     PPSMC_MSG_SoftReset),
	MSG_MAP(RunAfllBtc,			     PPSMC_MSG_RunAfllBtc),
	MSG_MAP(RunGfxDcBtc,			     PPSMC_MSG_RunGfxDcBtc),
	MSG_MAP(RunSocDcBtc,			     PPSMC_MSG_RunSocDcBtc),
	MSG_MAP(DramLogSetDramAddrHigh,		     PPSMC_MSG_DramLogSetDramAddrHigh),
	MSG_MAP(DramLogSetDramAddrLow,		     PPSMC_MSG_DramLogSetDramAddrLow),
	MSG_MAP(DramLogSetDramSize,		     PPSMC_MSG_DramLogSetDramSize),
	MSG_MAP(GetDebugData,			     PPSMC_MSG_GetDebugData),
	MSG_MAP(WaflTest,			     PPSMC_MSG_WaflTest),
	MSG_MAP(SetXgmiMode,			     PPSMC_MSG_SetXgmiMode),
	MSG_MAP(SetMemoryChannelEnable,		     PPSMC_MSG_SetMemoryChannelEnable),
};

static int arcturus_get_smu_msg_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_MSG_MAX_COUNT)
		return -EINVAL;

	mapping = arcturus_message_map[index];
	if (!(mapping.valid_mapping)) {
		pr_warn("Unsupported SMU message: %d\n", index);
		return -EINVAL;
	}

	return mapping.map_to;
}

static const struct pptable_funcs arcturus_ppt_funcs = {
	.get_smu_msg_index = arcturus_get_smu_msg_index,
};

void arcturus_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &arcturus_ppt_funcs;
	smu->smc_if_version = SMU11_DRIVER_IF_VERSION;
}
