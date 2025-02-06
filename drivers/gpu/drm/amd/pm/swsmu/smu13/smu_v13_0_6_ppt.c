/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_atombios.h"
#include "smu_v13_0_6_pmfw.h"
#include "smu13_driver_if_v13_0_6.h"
#include "smu_v13_0_6_ppsmc.h"
#include "soc15_common.h"
#include "atom.h"
#include "power_state.h"
#include "smu_v13_0.h"
#include "smu_v13_0_6_ppt.h"
#include "nbio/nbio_7_4_offset.h"
#include "nbio/nbio_7_4_sh_mask.h"
#include "thm/thm_11_0_2_offset.h"
#include "thm/thm_11_0_2_sh_mask.h"
#include "amdgpu_xgmi.h"
#include <linux/pci.h>
#include "amdgpu_ras.h"
#include "amdgpu_mca.h"
#include "amdgpu_aca.h"
#include "smu_cmn.h"
#include "mp/mp_13_0_6_offset.h"
#include "mp/mp_13_0_6_sh_mask.h"
#include "umc_v12_0.h"

#undef MP1_Public
#undef smnMP1_FIRMWARE_FLAGS

/* TODO: Check final register offsets */
#define MP1_Public 0x03b00000
#define smnMP1_FIRMWARE_FLAGS 0x3010028
/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

MODULE_FIRMWARE("amdgpu/smu_13_0_6.bin");
MODULE_FIRMWARE("amdgpu/smu_13_0_14.bin");

#define to_amdgpu_device(x) (container_of(x, struct amdgpu_device, pm.smu_i2c))

#define SMU_13_0_6_FEA_MAP(smu_feature, smu_13_0_6_feature)                    \
	[smu_feature] = { 1, (smu_13_0_6_feature) }

#define FEATURE_MASK(feature) (1ULL << feature)
#define SMC_DPM_FEATURE                                                        \
	(FEATURE_MASK(FEATURE_DATA_CALCULATION) |                              \
	 FEATURE_MASK(FEATURE_DPM_GFXCLK) | FEATURE_MASK(FEATURE_DPM_UCLK) |   \
	 FEATURE_MASK(FEATURE_DPM_SOCCLK) | FEATURE_MASK(FEATURE_DPM_FCLK) |   \
	 FEATURE_MASK(FEATURE_DPM_LCLK) | FEATURE_MASK(FEATURE_DPM_XGMI) |     \
	 FEATURE_MASK(FEATURE_DPM_VCN))

/* possible frequency drift (1Mhz) */
#define EPSILON 1

#define smnPCIE_ESM_CTRL 0x93D0
#define smnPCIE_LC_LINK_WIDTH_CNTL 0x1a340288
#define PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD_MASK 0x00000070L
#define PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD__SHIFT 0x4
#define MAX_LINK_WIDTH 6

#define smnPCIE_LC_SPEED_CNTL                   0x1a340290
#define PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE_MASK 0xE0
#define PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE__SHIFT 0x5
#define LINK_SPEED_MAX				4
#define SMU_13_0_6_DSCLK_THRESHOLD 140

#define MCA_BANK_IPID(_ip, _hwid, _type) \
	[AMDGPU_MCA_IP_##_ip] = { .hwid = _hwid, .mcatype = _type, }

#define SMU_CAP(x) SMU_13_0_6_CAPS_##x

enum smu_v13_0_6_caps {
	SMU_CAP(DPM),
	SMU_CAP(UNI_METRICS),
	SMU_CAP(DPM_POLICY),
	SMU_CAP(OTHER_END_METRICS),
	SMU_CAP(SET_UCLK_MAX),
	SMU_CAP(PCIE_METRICS),
	SMU_CAP(HST_LIMIT_METRICS),
	SMU_CAP(MCA_DEBUG_MODE),
	SMU_CAP(PER_INST_METRICS),
	SMU_CAP(CTF_LIMIT),
	SMU_CAP(RMA_MSG),
	SMU_CAP(ACA_SYND),
	SMU_CAP(SDMA_RESET),
	SMU_CAP(ALL),
};

struct mca_bank_ipid {
	enum amdgpu_mca_ip ip;
	uint16_t hwid;
	uint16_t mcatype;
};

struct mca_ras_info {
	enum amdgpu_ras_block blkid;
	enum amdgpu_mca_ip ip;
	int *err_code_array;
	int err_code_count;
	int (*get_err_count)(const struct mca_ras_info *mca_ras, struct amdgpu_device *adev,
			     enum amdgpu_mca_error_type type, struct mca_bank_entry *entry, uint32_t *count);
	bool (*bank_is_valid)(const struct mca_ras_info *mca_ras, struct amdgpu_device *adev,
			      enum amdgpu_mca_error_type type, struct mca_bank_entry *entry);
};

#define P2S_TABLE_ID_A 0x50325341
#define P2S_TABLE_ID_X 0x50325358
#define P2S_TABLE_ID_3 0x50325303

// clang-format off
static const struct cmn2asic_msg_mapping smu_v13_0_6_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,			     PPSMC_MSG_TestMessage,			0),
	MSG_MAP(GetSmuVersion,			     PPSMC_MSG_GetSmuVersion,			1),
	MSG_MAP(GetDriverIfVersion,		     PPSMC_MSG_GetDriverIfVersion,		1),
	MSG_MAP(EnableAllSmuFeatures,		     PPSMC_MSG_EnableAllSmuFeatures,		0),
	MSG_MAP(DisableAllSmuFeatures,		     PPSMC_MSG_DisableAllSmuFeatures,		0),
	MSG_MAP(RequestI2cTransaction,		     PPSMC_MSG_RequestI2cTransaction,		0),
	MSG_MAP(GetMetricsTable,		     PPSMC_MSG_GetMetricsTable,			1),
	MSG_MAP(GetMetricsVersion,		     PPSMC_MSG_GetMetricsVersion,		1),
	MSG_MAP(GetEnabledSmuFeaturesHigh,	     PPSMC_MSG_GetEnabledSmuFeaturesHigh,	1),
	MSG_MAP(GetEnabledSmuFeaturesLow,	     PPSMC_MSG_GetEnabledSmuFeaturesLow,	1),
	MSG_MAP(SetDriverDramAddrHigh,		     PPSMC_MSG_SetDriverDramAddrHigh,		1),
	MSG_MAP(SetDriverDramAddrLow,		     PPSMC_MSG_SetDriverDramAddrLow,		1),
	MSG_MAP(SetToolsDramAddrHigh,		     PPSMC_MSG_SetToolsDramAddrHigh,		0),
	MSG_MAP(SetToolsDramAddrLow,		     PPSMC_MSG_SetToolsDramAddrLow,		0),
	MSG_MAP(SetSoftMinByFreq,		     PPSMC_MSG_SetSoftMinByFreq,		0),
	MSG_MAP(SetSoftMaxByFreq,		     PPSMC_MSG_SetSoftMaxByFreq,		1),
	MSG_MAP(GetMinDpmFreq,			     PPSMC_MSG_GetMinDpmFreq,			1),
	MSG_MAP(GetMaxDpmFreq,			     PPSMC_MSG_GetMaxDpmFreq,			1),
	MSG_MAP(GetDpmFreqByIndex,		     PPSMC_MSG_GetDpmFreqByIndex,		1),
	MSG_MAP(SetPptLimit,			     PPSMC_MSG_SetPptLimit,			0),
	MSG_MAP(GetPptLimit,			     PPSMC_MSG_GetPptLimit,			1),
	MSG_MAP(GfxDeviceDriverReset,		     PPSMC_MSG_GfxDriverReset,			SMU_MSG_RAS_PRI),
	MSG_MAP(DramLogSetDramAddrHigh,		     PPSMC_MSG_DramLogSetDramAddrHigh,		0),
	MSG_MAP(DramLogSetDramAddrLow,		     PPSMC_MSG_DramLogSetDramAddrLow,		0),
	MSG_MAP(DramLogSetDramSize,		     PPSMC_MSG_DramLogSetDramSize,		0),
	MSG_MAP(GetDebugData,			     PPSMC_MSG_GetDebugData,			0),
	MSG_MAP(SetNumBadHbmPagesRetired,	     PPSMC_MSG_SetNumBadHbmPagesRetired,	0),
	MSG_MAP(DFCstateControl,		     PPSMC_MSG_DFCstateControl,			0),
	MSG_MAP(GetGmiPwrDnHyst,		     PPSMC_MSG_GetGmiPwrDnHyst,			0),
	MSG_MAP(SetGmiPwrDnHyst,		     PPSMC_MSG_SetGmiPwrDnHyst,			0),
	MSG_MAP(GmiPwrDnControl,		     PPSMC_MSG_GmiPwrDnControl,			0),
	MSG_MAP(EnterGfxoff,			     PPSMC_MSG_EnterGfxoff,			0),
	MSG_MAP(ExitGfxoff,			     PPSMC_MSG_ExitGfxoff,			0),
	MSG_MAP(EnableDeterminism,		     PPSMC_MSG_EnableDeterminism,		0),
	MSG_MAP(DisableDeterminism,		     PPSMC_MSG_DisableDeterminism,		0),
	MSG_MAP(GfxDriverResetRecovery,		     PPSMC_MSG_GfxDriverResetRecovery,		0),
	MSG_MAP(GetMinGfxclkFrequency,               PPSMC_MSG_GetMinGfxDpmFreq,                1),
	MSG_MAP(GetMaxGfxclkFrequency,               PPSMC_MSG_GetMaxGfxDpmFreq,                1),
	MSG_MAP(SetSoftMinGfxclk,                    PPSMC_MSG_SetSoftMinGfxClk,                1),
	MSG_MAP(SetSoftMaxGfxClk,                    PPSMC_MSG_SetSoftMaxGfxClk,                1),
	MSG_MAP(PrepareMp1ForUnload,                 PPSMC_MSG_PrepareForDriverUnload,          0),
	MSG_MAP(GetCTFLimit,                         PPSMC_MSG_GetCTFLimit,                     0),
	MSG_MAP(GetThermalLimit,                     PPSMC_MSG_ReadThrottlerLimit,              0),
	MSG_MAP(ClearMcaOnRead,	                     PPSMC_MSG_ClearMcaOnRead,                  0),
	MSG_MAP(QueryValidMcaCount,                  PPSMC_MSG_QueryValidMcaCount,              SMU_MSG_RAS_PRI),
	MSG_MAP(QueryValidMcaCeCount,                PPSMC_MSG_QueryValidMcaCeCount,            SMU_MSG_RAS_PRI),
	MSG_MAP(McaBankDumpDW,                       PPSMC_MSG_McaBankDumpDW,                   SMU_MSG_RAS_PRI),
	MSG_MAP(McaBankCeDumpDW,                     PPSMC_MSG_McaBankCeDumpDW,                 SMU_MSG_RAS_PRI),
	MSG_MAP(SelectPLPDMode,                      PPSMC_MSG_SelectPLPDMode,                  0),
	MSG_MAP(RmaDueToBadPageThreshold,            PPSMC_MSG_RmaDueToBadPageThreshold,        0),
	MSG_MAP(SelectPstatePolicy,                  PPSMC_MSG_SelectPstatePolicy,              0),
	MSG_MAP(ResetSDMA,                           PPSMC_MSG_ResetSDMA,                       0),
};

// clang-format on
static const struct cmn2asic_mapping smu_v13_0_6_clk_map[SMU_CLK_COUNT] = {
	CLK_MAP(SOCCLK, PPCLK_SOCCLK),
	CLK_MAP(FCLK, PPCLK_FCLK),
	CLK_MAP(UCLK, PPCLK_UCLK),
	CLK_MAP(MCLK, PPCLK_UCLK),
	CLK_MAP(DCLK, PPCLK_DCLK),
	CLK_MAP(VCLK, PPCLK_VCLK),
	CLK_MAP(LCLK, PPCLK_LCLK),
};

static const struct cmn2asic_mapping smu_v13_0_6_feature_mask_map[SMU_FEATURE_COUNT] = {
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DATA_CALCULATIONS_BIT, 		FEATURE_DATA_CALCULATION),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DPM_GFXCLK_BIT, 			FEATURE_DPM_GFXCLK),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DPM_UCLK_BIT, 			FEATURE_DPM_UCLK),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DPM_SOCCLK_BIT, 			FEATURE_DPM_SOCCLK),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DPM_FCLK_BIT, 			FEATURE_DPM_FCLK),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DPM_LCLK_BIT, 			FEATURE_DPM_LCLK),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DPM_VCLK_BIT,			FEATURE_DPM_VCN),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DPM_DCLK_BIT,			FEATURE_DPM_VCN),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DPM_XGMI_BIT, 			FEATURE_DPM_XGMI),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DS_GFXCLK_BIT, 			FEATURE_DS_GFXCLK),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DS_SOCCLK_BIT, 			FEATURE_DS_SOCCLK),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DS_LCLK_BIT, 			FEATURE_DS_LCLK),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DS_FCLK_BIT, 			FEATURE_DS_FCLK),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_VCN_DPM_BIT, 			FEATURE_DPM_VCN),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_PPT_BIT, 			FEATURE_PPT),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_TDC_BIT, 			FEATURE_TDC),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_APCC_DFLL_BIT, 			FEATURE_APCC_DFLL),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_MP1_CG_BIT, 			FEATURE_SMU_CG),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_GFXOFF_BIT, 			FEATURE_GFXOFF),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_FW_CTF_BIT, 			FEATURE_FW_CTF),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_THERMAL_BIT, 			FEATURE_THERMAL),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_XGMI_PER_LINK_PWR_DWN_BIT,	FEATURE_XGMI_PER_LINK_PWR_DOWN),
	SMU_13_0_6_FEA_MAP(SMU_FEATURE_DF_CSTATE_BIT, 			FEATURE_DF_CSTATE),
};

#define TABLE_PMSTATUSLOG             0
#define TABLE_SMU_METRICS             1
#define TABLE_I2C_COMMANDS            2
#define TABLE_COUNT                   3

static const struct cmn2asic_mapping smu_v13_0_6_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP(PMSTATUSLOG),
	TAB_MAP(SMU_METRICS),
	TAB_MAP(I2C_COMMANDS),
};

static const uint8_t smu_v13_0_6_throttler_map[] = {
	[THROTTLER_PPT_BIT]		= (SMU_THROTTLER_PPT0_BIT),
	[THROTTLER_THERMAL_SOCKET_BIT]	= (SMU_THROTTLER_TEMP_GPU_BIT),
	[THROTTLER_THERMAL_HBM_BIT]	= (SMU_THROTTLER_TEMP_MEM_BIT),
	[THROTTLER_THERMAL_VR_BIT]	= (SMU_THROTTLER_TEMP_VR_GFX_BIT),
	[THROTTLER_PROCHOT_BIT]		= (SMU_THROTTLER_PROCHOT_GFX_BIT),
};

struct PPTable_t {
	uint32_t MaxSocketPowerLimit;
	uint32_t MaxGfxclkFrequency;
	uint32_t MinGfxclkFrequency;
	uint32_t FclkFrequencyTable[4];
	uint32_t UclkFrequencyTable[4];
	uint32_t SocclkFrequencyTable[4];
	uint32_t VclkFrequencyTable[4];
	uint32_t DclkFrequencyTable[4];
	uint32_t LclkFrequencyTable[4];
	uint32_t MaxLclkDpmRange;
	uint32_t MinLclkDpmRange;
	uint64_t PublicSerialNumber_AID;
	bool Init;
};

#define SMUQ10_TO_UINT(x) ((x) >> 10)
#define SMUQ10_FRAC(x) ((x) & 0x3ff)
#define SMUQ10_ROUND(x) ((SMUQ10_TO_UINT(x)) + ((SMUQ10_FRAC(x)) >= 0x200))
#define GET_METRIC_FIELD(field, flag) ((flag) ?\
		(metrics_a->field) : (metrics_x->field))

struct smu_v13_0_6_dpm_map {
	enum smu_clk_type clk_type;
	uint32_t feature_num;
	struct smu_13_0_dpm_table *dpm_table;
	uint32_t *freq_table;
};

static inline void smu_v13_0_6_cap_set(struct smu_context *smu,
				       enum smu_v13_0_6_caps cap)
{
	struct smu_13_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;

	dpm_context->caps |= BIT_ULL(cap);
}

static inline void smu_v13_0_6_cap_clear(struct smu_context *smu,
					 enum smu_v13_0_6_caps cap)
{
	struct smu_13_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;

	dpm_context->caps &= ~BIT_ULL(cap);
}

static inline bool smu_v13_0_6_cap_supported(struct smu_context *smu,
					     enum smu_v13_0_6_caps cap)
{
	struct smu_13_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;

	return !!(dpm_context->caps & BIT_ULL(cap));
}

static void smu_v13_0_14_init_caps(struct smu_context *smu)
{
	enum smu_v13_0_6_caps default_cap_list[] = { SMU_CAP(DPM),
						     SMU_CAP(UNI_METRICS),
						     SMU_CAP(SET_UCLK_MAX),
						     SMU_CAP(DPM_POLICY),
						     SMU_CAP(PCIE_METRICS),
						     SMU_CAP(CTF_LIMIT),
						     SMU_CAP(MCA_DEBUG_MODE),
						     SMU_CAP(RMA_MSG),
						     SMU_CAP(ACA_SYND) };
	uint32_t fw_ver = smu->smc_fw_version;

	for (int i = 0; i < ARRAY_SIZE(default_cap_list); i++)
		smu_v13_0_6_cap_set(smu, default_cap_list[i]);

	if (fw_ver >= 0x05550E00)
		smu_v13_0_6_cap_set(smu, SMU_CAP(OTHER_END_METRICS));
	if (fw_ver >= 0x05551000)
		smu_v13_0_6_cap_set(smu, SMU_CAP(HST_LIMIT_METRICS));
	if (fw_ver >= 0x05550B00)
		smu_v13_0_6_cap_set(smu, SMU_CAP(PER_INST_METRICS));
	if (fw_ver >= 0x5551200)
		smu_v13_0_6_cap_set(smu, SMU_CAP(SDMA_RESET));
}

static void smu_v13_0_12_init_caps(struct smu_context *smu)
{
	enum smu_v13_0_6_caps default_cap_list[] = { SMU_CAP(DPM),
						     SMU_CAP(UNI_METRICS),
						     SMU_CAP(PCIE_METRICS),
						     SMU_CAP(CTF_LIMIT),
						     SMU_CAP(MCA_DEBUG_MODE),
						     SMU_CAP(RMA_MSG),
						     SMU_CAP(ACA_SYND) };
	uint32_t fw_ver = smu->smc_fw_version;

	for (int i = 0; i < ARRAY_SIZE(default_cap_list); i++)
		smu_v13_0_6_cap_set(smu, default_cap_list[i]);

	if (fw_ver < 0x00561900)
		smu_v13_0_6_cap_clear(smu, SMU_CAP(DPM));

	if (fw_ver >= 0x00561700)
		smu_v13_0_6_cap_set(smu, SMU_CAP(SDMA_RESET));
}

static void smu_v13_0_6_init_caps(struct smu_context *smu)
{
	enum smu_v13_0_6_caps default_cap_list[] = { SMU_CAP(DPM),
						     SMU_CAP(UNI_METRICS),
						     SMU_CAP(SET_UCLK_MAX),
						     SMU_CAP(DPM_POLICY),
						     SMU_CAP(PCIE_METRICS),
						     SMU_CAP(CTF_LIMIT),
						     SMU_CAP(MCA_DEBUG_MODE),
						     SMU_CAP(RMA_MSG),
						     SMU_CAP(ACA_SYND) };
	struct amdgpu_device *adev = smu->adev;
	uint32_t fw_ver = smu->smc_fw_version;
	uint32_t pgm = (fw_ver >> 24) & 0xFF;

	for (int i = 0; i < ARRAY_SIZE(default_cap_list); i++)
		smu_v13_0_6_cap_set(smu, default_cap_list[i]);

	if (fw_ver < 0x552F00)
		smu_v13_0_6_cap_clear(smu, SMU_CAP(DPM));
	if (fw_ver < 0x554500)
		smu_v13_0_6_cap_clear(smu, SMU_CAP(CTF_LIMIT));

	if (adev->flags & AMD_IS_APU) {
		smu_v13_0_6_cap_clear(smu, SMU_CAP(PCIE_METRICS));
		smu_v13_0_6_cap_clear(smu, SMU_CAP(DPM_POLICY));
		smu_v13_0_6_cap_clear(smu, SMU_CAP(RMA_MSG));
		smu_v13_0_6_cap_clear(smu, SMU_CAP(ACA_SYND));

		if (fw_ver <= 0x4556900)
			smu_v13_0_6_cap_clear(smu, SMU_CAP(UNI_METRICS));
		if (fw_ver >= 0x04556F00)
			smu_v13_0_6_cap_set(smu, SMU_CAP(HST_LIMIT_METRICS));
		if (fw_ver >= 0x04556A00)
			smu_v13_0_6_cap_set(smu, SMU_CAP(PER_INST_METRICS));
	} else {
		if (fw_ver >= 0x557600)
			smu_v13_0_6_cap_set(smu, SMU_CAP(OTHER_END_METRICS));
		if (fw_ver < 0x00556000)
			smu_v13_0_6_cap_clear(smu, SMU_CAP(DPM_POLICY));
		if (amdgpu_sriov_vf(adev) && (fw_ver < 0x556600))
			smu_v13_0_6_cap_clear(smu, SMU_CAP(SET_UCLK_MAX));
		if (fw_ver < 0x556300)
			smu_v13_0_6_cap_clear(smu, SMU_CAP(PCIE_METRICS));
		if (fw_ver < 0x554800)
			smu_v13_0_6_cap_clear(smu, SMU_CAP(MCA_DEBUG_MODE));
		if (fw_ver >= 0x556F00)
			smu_v13_0_6_cap_set(smu, SMU_CAP(PER_INST_METRICS));
		if (fw_ver < 0x00555a00)
			smu_v13_0_6_cap_clear(smu, SMU_CAP(RMA_MSG));
		if (fw_ver < 0x00555600)
			smu_v13_0_6_cap_clear(smu, SMU_CAP(ACA_SYND));
		if (pgm == 0 && fw_ver >= 0x557900)
			smu_v13_0_6_cap_set(smu, SMU_CAP(HST_LIMIT_METRICS));
	}
	if (((pgm == 7) && (fw_ver >= 0x7550700)) ||
	    ((pgm == 0) && (fw_ver >= 0x00557900)) ||
	    ((pgm == 4) && (fw_ver >= 0x4557000)))
		smu_v13_0_6_cap_set(smu, SMU_CAP(SDMA_RESET));
}

static void smu_v13_0_x_init_caps(struct smu_context *smu)
{
	switch (amdgpu_ip_version(smu->adev, MP1_HWIP, 0)) {
	case IP_VERSION(13, 0, 12):
		return smu_v13_0_12_init_caps(smu);
	case IP_VERSION(13, 0, 14):
		return smu_v13_0_14_init_caps(smu);
	default:
		return smu_v13_0_6_init_caps(smu);
	}
}

static int smu_v13_0_6_check_fw_version(struct smu_context *smu)
{
	int r;

	r = smu_v13_0_check_fw_version(smu);
	/* Initialize caps flags once fw version is fetched */
	if (!r)
		smu_v13_0_x_init_caps(smu);

	return r;
}

static int smu_v13_0_6_init_microcode(struct smu_context *smu)
{
	const struct smc_firmware_header_v2_1 *v2_1;
	const struct common_firmware_header *hdr;
	struct amdgpu_firmware_info *ucode = NULL;
	struct smc_soft_pptable_entry *entries;
	struct amdgpu_device *adev = smu->adev;
	uint32_t p2s_table_id = P2S_TABLE_ID_A;
	int ret = 0, i, p2stable_count;
	int var = (adev->pdev->device & 0xF);
	char ucode_prefix[15];

	/* No need to load P2S tables in IOV mode */
	if (amdgpu_sriov_vf(adev))
		return 0;

	if (!(adev->flags & AMD_IS_APU)) {
		p2s_table_id = P2S_TABLE_ID_X;
		if (var == 0x5)
			p2s_table_id = P2S_TABLE_ID_3;
	}

	amdgpu_ucode_ip_version_decode(adev, MP1_HWIP, ucode_prefix,
				       sizeof(ucode_prefix));
	ret  = amdgpu_ucode_request(adev, &adev->pm.fw, AMDGPU_UCODE_REQUIRED,
				    "amdgpu/%s.bin", ucode_prefix);
	if (ret)
		goto out;

	hdr = (const struct common_firmware_header *)adev->pm.fw->data;
	amdgpu_ucode_print_smc_hdr(hdr);

	/* SMU v13.0.6 binary file doesn't carry pptables, instead the entries
	 * are used to carry p2s tables.
	 */
	v2_1 = (const struct smc_firmware_header_v2_1 *)adev->pm.fw->data;
	entries = (struct smc_soft_pptable_entry
			   *)((uint8_t *)v2_1 +
			      le32_to_cpu(v2_1->pptable_entry_offset));
	p2stable_count = le32_to_cpu(v2_1->pptable_count);
	for (i = 0; i < p2stable_count; i++) {
		if (le32_to_cpu(entries[i].id) == p2s_table_id) {
			smu->pptable_firmware.data =
				((uint8_t *)v2_1 +
				 le32_to_cpu(entries[i].ppt_offset_bytes));
			smu->pptable_firmware.size =
				le32_to_cpu(entries[i].ppt_size_bytes);
			break;
		}
	}

	if (smu->pptable_firmware.data && smu->pptable_firmware.size) {
		ucode = &adev->firmware.ucode[AMDGPU_UCODE_ID_P2S_TABLE];
		ucode->ucode_id = AMDGPU_UCODE_ID_P2S_TABLE;
		ucode->fw = &smu->pptable_firmware;
		adev->firmware.fw_size += ALIGN(ucode->fw->size, PAGE_SIZE);
	}

	return 0;
out:
	amdgpu_ucode_release(&adev->pm.fw);

	return ret;
}

static int smu_v13_0_6_tables_init(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	struct amdgpu_device *adev = smu->adev;

	if (!(adev->flags & AMD_IS_APU))
		SMU_TABLE_INIT(tables, SMU_TABLE_PMSTATUSLOG, SMU13_TOOL_SIZE,
			       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);

	SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS,
		       max(sizeof(MetricsTableX_t), sizeof(MetricsTableA_t)),
		       PAGE_SIZE,
		       AMDGPU_GEM_DOMAIN_VRAM | AMDGPU_GEM_DOMAIN_GTT);

	SMU_TABLE_INIT(tables, SMU_TABLE_I2C_COMMANDS, sizeof(SwI2cRequest_t),
		       PAGE_SIZE,
		       AMDGPU_GEM_DOMAIN_VRAM | AMDGPU_GEM_DOMAIN_GTT);

	smu_table->metrics_table = kzalloc(max(sizeof(MetricsTableX_t),
		       sizeof(MetricsTableA_t)), GFP_KERNEL);
	if (!smu_table->metrics_table)
		return -ENOMEM;
	smu_table->metrics_time = 0;

	smu_table->gpu_metrics_table_size = sizeof(struct gpu_metrics_v1_7);
	smu_table->gpu_metrics_table =
		kzalloc(smu_table->gpu_metrics_table_size, GFP_KERNEL);
	if (!smu_table->gpu_metrics_table) {
		kfree(smu_table->metrics_table);
		return -ENOMEM;
	}

	smu_table->driver_pptable =
		kzalloc(sizeof(struct PPTable_t), GFP_KERNEL);
	if (!smu_table->driver_pptable) {
		kfree(smu_table->metrics_table);
		kfree(smu_table->gpu_metrics_table);
		return -ENOMEM;
	}

	return 0;
}

static int smu_v13_0_6_select_policy_soc_pstate(struct smu_context *smu,
						int policy)
{
	struct amdgpu_device *adev = smu->adev;
	int ret, param;

	switch (policy) {
	case SOC_PSTATE_DEFAULT:
		param = 0;
		break;
	case SOC_PSTATE_0:
		param = 1;
		break;
	case SOC_PSTATE_1:
		param = 2;
		break;
	case SOC_PSTATE_2:
		param = 3;
		break;
	default:
		return -EINVAL;
	}

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SelectPstatePolicy,
					      param, NULL);

	if (ret)
		dev_err(adev->dev, "select soc pstate policy %d failed",
			policy);

	return ret;
}

static int smu_v13_0_6_select_plpd_policy(struct smu_context *smu, int level)
{
	struct amdgpu_device *adev = smu->adev;
	int ret, param;

	switch (level) {
	case XGMI_PLPD_DEFAULT:
		param = PPSMC_PLPD_MODE_DEFAULT;
		break;
	case XGMI_PLPD_OPTIMIZED:
		param = PPSMC_PLPD_MODE_OPTIMIZED;
		break;
	case XGMI_PLPD_DISALLOW:
		param = 0;
		break;
	default:
		return -EINVAL;
	}

	if (level == XGMI_PLPD_DISALLOW)
		ret = smu_cmn_send_smc_msg_with_param(
			smu, SMU_MSG_GmiPwrDnControl, param, NULL);
	else
		/* change xgmi per-link power down policy */
		ret = smu_cmn_send_smc_msg_with_param(
			smu, SMU_MSG_SelectPLPDMode, param, NULL);

	if (ret)
		dev_err(adev->dev,
			"select xgmi per-link power down policy %d failed\n",
			level);

	return ret;
}

static int smu_v13_0_6_allocate_dpm_context(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_dpm_policy *policy;

	smu_dpm->dpm_context =
		kzalloc(sizeof(struct smu_13_0_dpm_context), GFP_KERNEL);
	if (!smu_dpm->dpm_context)
		return -ENOMEM;
	smu_dpm->dpm_context_size = sizeof(struct smu_13_0_dpm_context);

	smu_dpm->dpm_policies =
		kzalloc(sizeof(struct smu_dpm_policy_ctxt), GFP_KERNEL);
	if (!smu_dpm->dpm_policies) {
		kfree(smu_dpm->dpm_context);
		return -ENOMEM;
	}

	if (!(smu->adev->flags & AMD_IS_APU)) {
		policy = &(smu_dpm->dpm_policies->policies[0]);

		policy->policy_type = PP_PM_POLICY_SOC_PSTATE;
		policy->level_mask = BIT(SOC_PSTATE_DEFAULT) |
				     BIT(SOC_PSTATE_0) | BIT(SOC_PSTATE_1) |
				     BIT(SOC_PSTATE_2);
		policy->current_level = SOC_PSTATE_DEFAULT;
		policy->set_policy = smu_v13_0_6_select_policy_soc_pstate;
		smu_cmn_generic_soc_policy_desc(policy);
		smu_dpm->dpm_policies->policy_mask |=
			BIT(PP_PM_POLICY_SOC_PSTATE);
	}
	policy = &(smu_dpm->dpm_policies->policies[1]);

	policy->policy_type = PP_PM_POLICY_XGMI_PLPD;
	policy->level_mask = BIT(XGMI_PLPD_DISALLOW) | BIT(XGMI_PLPD_DEFAULT) |
			     BIT(XGMI_PLPD_OPTIMIZED);
	policy->current_level = XGMI_PLPD_DEFAULT;
	policy->set_policy = smu_v13_0_6_select_plpd_policy;
	smu_cmn_generic_plpd_policy_desc(policy);
	smu_dpm->dpm_policies->policy_mask |= BIT(PP_PM_POLICY_XGMI_PLPD);

	return 0;
}

static int smu_v13_0_6_init_smc_tables(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_v13_0_6_tables_init(smu);
	if (ret)
		return ret;

	ret = smu_v13_0_6_allocate_dpm_context(smu);

	return ret;
}

static int smu_v13_0_6_get_allowed_feature_mask(struct smu_context *smu,
						uint32_t *feature_mask,
						uint32_t num)
{
	if (num > 2)
		return -EINVAL;

	/* pptable will handle the features to enable */
	memset(feature_mask, 0xFF, sizeof(uint32_t) * num);

	return 0;
}

static int smu_v13_0_6_get_metrics_table(struct smu_context *smu,
					 void *metrics_table, bool bypass_cache)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	uint32_t table_size = smu_table->tables[SMU_TABLE_SMU_METRICS].size;
	struct smu_table *table = &smu_table->driver_table;
	int ret;

	if (bypass_cache || !smu_table->metrics_time ||
	    time_after(jiffies,
		       smu_table->metrics_time + msecs_to_jiffies(1))) {
		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetMetricsTable, NULL);
		if (ret) {
			dev_info(smu->adev->dev,
				 "Failed to export SMU metrics table!\n");
			return ret;
		}

		amdgpu_asic_invalidate_hdp(smu->adev, NULL);
		memcpy(smu_table->metrics_table, table->cpu_addr, table_size);

		smu_table->metrics_time = jiffies;
	}

	if (metrics_table)
		memcpy(metrics_table, smu_table->metrics_table, table_size);

	return 0;
}

static ssize_t smu_v13_0_6_get_pm_metrics(struct smu_context *smu,
					  void *metrics, size_t max_size)
{
	struct smu_table_context *smu_tbl_ctxt = &smu->smu_table;
	uint32_t table_version = smu_tbl_ctxt->tables[SMU_TABLE_SMU_METRICS].version;
	uint32_t table_size = smu_tbl_ctxt->tables[SMU_TABLE_SMU_METRICS].size;
	struct amdgpu_pm_metrics *pm_metrics = metrics;
	uint32_t pmfw_version;
	int ret;

	if (!pm_metrics || !max_size)
		return -EINVAL;

	if (max_size < (table_size + sizeof(pm_metrics->common_header)))
		return -EOVERFLOW;

	/* Don't use cached metrics data */
	ret = smu_v13_0_6_get_metrics_table(smu, pm_metrics->data, true);
	if (ret)
		return ret;

	smu_cmn_get_smc_version(smu, NULL, &pmfw_version);

	memset(&pm_metrics->common_header, 0,
	       sizeof(pm_metrics->common_header));
	if (amdgpu_ip_version(smu->adev, MP1_HWIP, 0) == IP_VERSION(13, 0, 6))
		pm_metrics->common_header.mp1_ip_discovery_version = IP_VERSION(13, 0, 6);
	if (amdgpu_ip_version(smu->adev, MP1_HWIP, 0) == IP_VERSION(13, 0, 14))
		pm_metrics->common_header.mp1_ip_discovery_version = IP_VERSION(13, 0, 14);
	pm_metrics->common_header.pmfw_version = pmfw_version;
	pm_metrics->common_header.pmmetrics_version = table_version;
	pm_metrics->common_header.structure_size =
		sizeof(pm_metrics->common_header) + table_size;

	return pm_metrics->common_header.structure_size;
}

static int smu_v13_0_6_setup_driver_pptable(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	MetricsTableX_t *metrics_x = (MetricsTableX_t *)smu_table->metrics_table;
	MetricsTableA_t *metrics_a = (MetricsTableA_t *)smu_table->metrics_table;
	struct PPTable_t *pptable =
		(struct PPTable_t *)smu_table->driver_pptable;
	bool flag = !smu_v13_0_6_cap_supported(smu, SMU_CAP(UNI_METRICS));
	int ret, i, retry = 100;
	uint32_t table_version;

	/* Store one-time values in driver PPTable */
	if (!pptable->Init) {
		while (--retry) {
			ret = smu_v13_0_6_get_metrics_table(smu, NULL, true);
			if (ret)
				return ret;

			/* Ensure that metrics have been updated */
			if (GET_METRIC_FIELD(AccumulationCounter, flag))
				break;

			usleep_range(1000, 1100);
		}

		if (!retry)
			return -ETIME;

		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetMetricsVersion,
					   &table_version);
		if (ret)
			return ret;
		smu_table->tables[SMU_TABLE_SMU_METRICS].version =
			table_version;

		pptable->MaxSocketPowerLimit =
			SMUQ10_ROUND(GET_METRIC_FIELD(MaxSocketPowerLimit, flag));
		pptable->MaxGfxclkFrequency =
			SMUQ10_ROUND(GET_METRIC_FIELD(MaxGfxclkFrequency, flag));
		pptable->MinGfxclkFrequency =
			SMUQ10_ROUND(GET_METRIC_FIELD(MinGfxclkFrequency, flag));

		for (i = 0; i < 4; ++i) {
			pptable->FclkFrequencyTable[i] =
				SMUQ10_ROUND(GET_METRIC_FIELD(FclkFrequencyTable, flag)[i]);
			pptable->UclkFrequencyTable[i] =
				SMUQ10_ROUND(GET_METRIC_FIELD(UclkFrequencyTable, flag)[i]);
			pptable->SocclkFrequencyTable[i] = SMUQ10_ROUND(
				GET_METRIC_FIELD(SocclkFrequencyTable, flag)[i]);
			pptable->VclkFrequencyTable[i] =
				SMUQ10_ROUND(GET_METRIC_FIELD(VclkFrequencyTable, flag)[i]);
			pptable->DclkFrequencyTable[i] =
				SMUQ10_ROUND(GET_METRIC_FIELD(DclkFrequencyTable, flag)[i]);
			pptable->LclkFrequencyTable[i] =
				SMUQ10_ROUND(GET_METRIC_FIELD(LclkFrequencyTable, flag)[i]);
		}

		/* use AID0 serial number by default */
		pptable->PublicSerialNumber_AID = GET_METRIC_FIELD(PublicSerialNumber_AID, flag)[0];

		pptable->Init = true;
	}

	return 0;
}

static int smu_v13_0_6_get_dpm_ultimate_freq(struct smu_context *smu,
					     enum smu_clk_type clk_type,
					     uint32_t *min, uint32_t *max)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct PPTable_t *pptable =
		(struct PPTable_t *)smu_table->driver_pptable;
	uint32_t clock_limit = 0, param;
	int ret = 0, clk_id = 0;

	if (!smu_cmn_clk_dpm_is_enabled(smu, clk_type)) {
		switch (clk_type) {
		case SMU_MCLK:
		case SMU_UCLK:
			if (pptable->Init)
				clock_limit = pptable->UclkFrequencyTable[0];
			break;
		case SMU_GFXCLK:
		case SMU_SCLK:
			if (pptable->Init)
				clock_limit = pptable->MinGfxclkFrequency;
			break;
		case SMU_SOCCLK:
			if (pptable->Init)
				clock_limit = pptable->SocclkFrequencyTable[0];
			break;
		case SMU_FCLK:
			if (pptable->Init)
				clock_limit = pptable->FclkFrequencyTable[0];
			break;
		case SMU_VCLK:
			if (pptable->Init)
				clock_limit = pptable->VclkFrequencyTable[0];
			break;
		case SMU_DCLK:
			if (pptable->Init)
				clock_limit = pptable->DclkFrequencyTable[0];
			break;
		default:
			break;
		}

		if (min)
			*min = clock_limit;

		if (max)
			*max = clock_limit;

		return 0;
	}

	if (!(clk_type == SMU_GFXCLK || clk_type == SMU_SCLK)) {
		clk_id = smu_cmn_to_asic_specific_index(
			smu, CMN2ASIC_MAPPING_CLK, clk_type);
		if (clk_id < 0) {
			ret = -EINVAL;
			goto failed;
		}
		param = (clk_id & 0xffff) << 16;
	}

	if (max) {
		if (clk_type == SMU_GFXCLK || clk_type == SMU_SCLK)
			ret = smu_cmn_send_smc_msg(
				smu, SMU_MSG_GetMaxGfxclkFrequency, max);
		else
			ret = smu_cmn_send_smc_msg_with_param(
				smu, SMU_MSG_GetMaxDpmFreq, param, max);
		if (ret)
			goto failed;
	}

	if (min) {
		if (clk_type == SMU_GFXCLK || clk_type == SMU_SCLK)
			ret = smu_cmn_send_smc_msg(
				smu, SMU_MSG_GetMinGfxclkFrequency, min);
		else
			ret = smu_cmn_send_smc_msg_with_param(
				smu, SMU_MSG_GetMinDpmFreq, param, min);
	}

failed:
	return ret;
}

static int smu_v13_0_6_get_dpm_level_count(struct smu_context *smu,
					  enum smu_clk_type clk_type,
					  uint32_t *levels)
{
	int ret;

	ret = smu_v13_0_get_dpm_freq_by_index(smu, clk_type, 0xff, levels);
	if (!ret)
		++(*levels);

	return ret;
}

static void smu_v13_0_6_pm_policy_init(struct smu_context *smu)
{
	struct smu_dpm_policy *policy;

	policy = smu_get_pm_policy(smu, PP_PM_POLICY_SOC_PSTATE);
	if (policy)
		policy->current_level = SOC_PSTATE_DEFAULT;
}

static int smu_v13_0_6_set_default_dpm_table(struct smu_context *smu)
{
	struct smu_13_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_13_0_dpm_table *dpm_table = NULL;
	struct PPTable_t *pptable =
		(struct PPTable_t *)smu_table->driver_pptable;
	uint32_t gfxclkmin, gfxclkmax, levels;
	int ret = 0, i, j;
	struct smu_v13_0_6_dpm_map dpm_map[] = {
		{ SMU_SOCCLK, SMU_FEATURE_DPM_SOCCLK_BIT,
		  &dpm_context->dpm_tables.soc_table,
		  pptable->SocclkFrequencyTable },
		{ SMU_UCLK, SMU_FEATURE_DPM_UCLK_BIT,
		  &dpm_context->dpm_tables.uclk_table,
		  pptable->UclkFrequencyTable },
		{ SMU_FCLK, SMU_FEATURE_DPM_FCLK_BIT,
		  &dpm_context->dpm_tables.fclk_table,
		  pptable->FclkFrequencyTable },
		{ SMU_VCLK, SMU_FEATURE_DPM_VCLK_BIT,
		  &dpm_context->dpm_tables.vclk_table,
		  pptable->VclkFrequencyTable },
		{ SMU_DCLK, SMU_FEATURE_DPM_DCLK_BIT,
		  &dpm_context->dpm_tables.dclk_table,
		  pptable->DclkFrequencyTable },
	};

	smu_v13_0_6_setup_driver_pptable(smu);

	/* DPM policy not supported in older firmwares */
	if (!smu_v13_0_6_cap_supported(smu, SMU_CAP(DPM_POLICY))) {
		struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

		smu_dpm->dpm_policies->policy_mask &=
			~BIT(PP_PM_POLICY_SOC_PSTATE);
	}

	smu_v13_0_6_pm_policy_init(smu);
	/* gfxclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.gfx_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_GFXCLK_BIT)) {
		/* In the case of gfxclk, only fine-grained dpm is honored.
		 * Get min/max values from FW.
		 */
		ret = smu_v13_0_6_get_dpm_ultimate_freq(smu, SMU_GFXCLK,
							&gfxclkmin, &gfxclkmax);
		if (ret)
			return ret;

		dpm_table->count = 2;
		dpm_table->dpm_levels[0].value = gfxclkmin;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->dpm_levels[1].value = gfxclkmax;
		dpm_table->dpm_levels[1].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[1].value;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = pptable->MinGfxclkFrequency;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	for (j = 0; j < ARRAY_SIZE(dpm_map); j++) {
		dpm_table = dpm_map[j].dpm_table;
		levels = 1;
		if (smu_cmn_feature_is_enabled(smu, dpm_map[j].feature_num)) {
			ret = smu_v13_0_6_get_dpm_level_count(
				smu, dpm_map[j].clk_type, &levels);
			if (ret)
				return ret;
		}
		dpm_table->count = levels;
		for (i = 0; i < dpm_table->count; ++i) {
			dpm_table->dpm_levels[i].value =
				dpm_map[j].freq_table[i];
			dpm_table->dpm_levels[i].enabled = true;

		}
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[levels - 1].value;

	}

	return 0;
}

static int smu_v13_0_6_setup_pptable(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;

	/* TODO: PPTable is not available.
	 * 1) Find an alternate way to get 'PPTable values' here.
	 * 2) Check if there is SW CTF
	 */
	table_context->thermal_controller_type = 0;

	return 0;
}

static int smu_v13_0_6_check_fw_status(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t mp1_fw_flags;

	mp1_fw_flags =
		RREG32_PCIE(MP1_Public | (smnMP1_FIRMWARE_FLAGS & 0xffffffff));

	if ((mp1_fw_flags & MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED_MASK) >>
	    MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED__SHIFT)
		return 0;

	return -EIO;
}

static int smu_v13_0_6_populate_umd_state_clk(struct smu_context *smu)
{
	struct smu_13_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	struct smu_13_0_dpm_table *gfx_table =
		&dpm_context->dpm_tables.gfx_table;
	struct smu_13_0_dpm_table *mem_table =
		&dpm_context->dpm_tables.uclk_table;
	struct smu_13_0_dpm_table *soc_table =
		&dpm_context->dpm_tables.soc_table;
	struct smu_umd_pstate_table *pstate_table = &smu->pstate_table;

	pstate_table->gfxclk_pstate.min = gfx_table->min;
	pstate_table->gfxclk_pstate.peak = gfx_table->max;
	pstate_table->gfxclk_pstate.curr.min = gfx_table->min;
	pstate_table->gfxclk_pstate.curr.max = gfx_table->max;

	pstate_table->uclk_pstate.min = mem_table->min;
	pstate_table->uclk_pstate.peak = mem_table->max;
	pstate_table->uclk_pstate.curr.min = mem_table->min;
	pstate_table->uclk_pstate.curr.max = mem_table->max;

	pstate_table->socclk_pstate.min = soc_table->min;
	pstate_table->socclk_pstate.peak = soc_table->max;
	pstate_table->socclk_pstate.curr.min = soc_table->min;
	pstate_table->socclk_pstate.curr.max = soc_table->max;

	if (gfx_table->count > SMU_13_0_6_UMD_PSTATE_GFXCLK_LEVEL &&
	    mem_table->count > SMU_13_0_6_UMD_PSTATE_MCLK_LEVEL &&
	    soc_table->count > SMU_13_0_6_UMD_PSTATE_SOCCLK_LEVEL) {
		pstate_table->gfxclk_pstate.standard =
			gfx_table->dpm_levels[SMU_13_0_6_UMD_PSTATE_GFXCLK_LEVEL].value;
		pstate_table->uclk_pstate.standard =
			mem_table->dpm_levels[SMU_13_0_6_UMD_PSTATE_MCLK_LEVEL].value;
		pstate_table->socclk_pstate.standard =
			soc_table->dpm_levels[SMU_13_0_6_UMD_PSTATE_SOCCLK_LEVEL].value;
	} else {
		pstate_table->gfxclk_pstate.standard =
			pstate_table->gfxclk_pstate.min;
		pstate_table->uclk_pstate.standard =
			pstate_table->uclk_pstate.min;
		pstate_table->socclk_pstate.standard =
			pstate_table->socclk_pstate.min;
	}

	return 0;
}

static int smu_v13_0_6_get_clk_table(struct smu_context *smu,
				     struct pp_clock_levels_with_latency *clocks,
				     struct smu_13_0_dpm_table *dpm_table)
{
	int i, count;

	count = (dpm_table->count > MAX_NUM_CLOCKS) ? MAX_NUM_CLOCKS :
						      dpm_table->count;
	clocks->num_levels = count;

	for (i = 0; i < count; i++) {
		clocks->data[i].clocks_in_khz =
			dpm_table->dpm_levels[i].value * 1000;
		clocks->data[i].latency_in_us = 0;
	}

	return 0;
}

static int smu_v13_0_6_freqs_in_same_level(int32_t frequency1,
					   int32_t frequency2)
{
	return (abs(frequency1 - frequency2) <= EPSILON);
}

static uint32_t smu_v13_0_6_get_throttler_status(struct smu_context *smu)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_13_0_power_context *power_context = smu_power->power_context;
	uint32_t  throttler_status = 0;

	throttler_status = atomic_read(&power_context->throttle_status);
	dev_dbg(smu->adev->dev, "SMU Throttler status: %u", throttler_status);

	return throttler_status;
}

static int smu_v13_0_6_get_smu_metrics_data(struct smu_context *smu,
					    MetricsMember_t member,
					    uint32_t *value)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	MetricsTableX_t *metrics_x = (MetricsTableX_t *)smu_table->metrics_table;
	MetricsTableA_t *metrics_a = (MetricsTableA_t *)smu_table->metrics_table;
	bool flag = !smu_v13_0_6_cap_supported(smu, SMU_CAP(UNI_METRICS));
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;
	int xcc_id;

	ret = smu_v13_0_6_get_metrics_table(smu, NULL, false);
	if (ret)
		return ret;

	/* For clocks with multiple instances, only report the first one */
	switch (member) {
	case METRICS_CURR_GFXCLK:
	case METRICS_AVERAGE_GFXCLK:
		if (smu_v13_0_6_cap_supported(smu, SMU_CAP(DPM))) {
			xcc_id = GET_INST(GC, 0);
			*value = SMUQ10_ROUND(GET_METRIC_FIELD(GfxclkFrequency, flag)[xcc_id]);
		} else {
			*value = 0;
		}
		break;
	case METRICS_CURR_SOCCLK:
	case METRICS_AVERAGE_SOCCLK:
		*value = SMUQ10_ROUND(GET_METRIC_FIELD(SocclkFrequency, flag)[0]);
		break;
	case METRICS_CURR_UCLK:
	case METRICS_AVERAGE_UCLK:
		*value = SMUQ10_ROUND(GET_METRIC_FIELD(UclkFrequency, flag));
		break;
	case METRICS_CURR_VCLK:
		*value = SMUQ10_ROUND(GET_METRIC_FIELD(VclkFrequency, flag)[0]);
		break;
	case METRICS_CURR_DCLK:
		*value = SMUQ10_ROUND(GET_METRIC_FIELD(DclkFrequency, flag)[0]);
		break;
	case METRICS_CURR_FCLK:
		*value = SMUQ10_ROUND(GET_METRIC_FIELD(FclkFrequency, flag));
		break;
	case METRICS_AVERAGE_GFXACTIVITY:
		*value = SMUQ10_ROUND(GET_METRIC_FIELD(SocketGfxBusy, flag));
		break;
	case METRICS_AVERAGE_MEMACTIVITY:
		*value = SMUQ10_ROUND(GET_METRIC_FIELD(DramBandwidthUtilization, flag));
		break;
	case METRICS_CURR_SOCKETPOWER:
		*value = SMUQ10_ROUND(GET_METRIC_FIELD(SocketPower, flag)) << 8;
		break;
	case METRICS_TEMPERATURE_HOTSPOT:
		*value = SMUQ10_ROUND(GET_METRIC_FIELD(MaxSocketTemperature, flag)) *
			 SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_MEM:
		*value = SMUQ10_ROUND(GET_METRIC_FIELD(MaxHbmTemperature, flag)) *
			 SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	/* This is the max of all VRs and not just SOC VR.
	 * No need to define another data type for the same.
	 */
	case METRICS_TEMPERATURE_VRSOC:
		*value = SMUQ10_ROUND(GET_METRIC_FIELD(MaxVrTemperature, flag)) *
			 SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	default:
		*value = UINT_MAX;
		break;
	}

	return ret;
}

static int smu_v13_0_6_get_current_clk_freq_by_table(struct smu_context *smu,
						     enum smu_clk_type clk_type,
						     uint32_t *value)
{
	MetricsMember_t member_type;

	if (!value)
		return -EINVAL;

	switch (clk_type) {
	case SMU_GFXCLK:
		member_type = METRICS_CURR_GFXCLK;
		break;
	case SMU_UCLK:
		member_type = METRICS_CURR_UCLK;
		break;
	case SMU_SOCCLK:
		member_type = METRICS_CURR_SOCCLK;
		break;
	case SMU_VCLK:
		member_type = METRICS_CURR_VCLK;
		break;
	case SMU_DCLK:
		member_type = METRICS_CURR_DCLK;
		break;
	case SMU_FCLK:
		member_type = METRICS_CURR_FCLK;
		break;
	default:
		return -EINVAL;
	}

	return smu_v13_0_6_get_smu_metrics_data(smu, member_type, value);
}

static int smu_v13_0_6_print_clks(struct smu_context *smu, char *buf, int size,
				  struct smu_13_0_dpm_table *single_dpm_table,
				  uint32_t curr_clk, const char *clk_name)
{
	struct pp_clock_levels_with_latency clocks;
	int i, ret, level = -1;
	uint32_t clk1, clk2;

	ret = smu_v13_0_6_get_clk_table(smu, &clocks, single_dpm_table);
	if (ret) {
		dev_err(smu->adev->dev, "Attempt to get %s clk levels failed!",
			clk_name);
		return ret;
	}

	if (!clocks.num_levels)
		return -EINVAL;

	if (curr_clk < SMU_13_0_6_DSCLK_THRESHOLD) {
		size = sysfs_emit_at(buf, size, "S: %uMhz *\n", curr_clk);
		for (i = 0; i < clocks.num_levels; i++)
			size += sysfs_emit_at(buf, size, "%d: %uMhz\n", i,
					      clocks.data[i].clocks_in_khz /
						      1000);

	} else {
		if ((clocks.num_levels == 1) ||
		    (curr_clk < (clocks.data[0].clocks_in_khz / 1000)))
			level = 0;
		for (i = 0; i < clocks.num_levels; i++) {
			clk1 = clocks.data[i].clocks_in_khz / 1000;

			if (i < (clocks.num_levels - 1))
				clk2 = clocks.data[i + 1].clocks_in_khz / 1000;

			if (curr_clk == clk1) {
				level = i;
			} else if (curr_clk >= clk1 && curr_clk < clk2) {
				level = (curr_clk - clk1) <= (clk2 - curr_clk) ?
						i :
						i + 1;
			}

			size += sysfs_emit_at(buf, size, "%d: %uMhz %s\n", i,
					      clk1, (level == i) ? "*" : "");
		}
	}

	return size;
}

static int smu_v13_0_6_print_clk_levels(struct smu_context *smu,
					enum smu_clk_type type, char *buf)
{
	int now, size = 0;
	int ret = 0;
	struct smu_umd_pstate_table *pstate_table = &smu->pstate_table;
	struct smu_13_0_dpm_table *single_dpm_table;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_13_0_dpm_context *dpm_context = NULL;
	uint32_t min_clk, max_clk;

	smu_cmn_get_sysfs_buf(&buf, &size);

	if (amdgpu_ras_intr_triggered()) {
		size += sysfs_emit_at(buf, size, "unavailable\n");
		return size;
	}

	dpm_context = smu_dpm->dpm_context;

	switch (type) {
	case SMU_OD_SCLK:
		size += sysfs_emit_at(buf, size, "%s:\n", "OD_SCLK");
		size += sysfs_emit_at(buf, size, "0: %uMhz\n1: %uMhz\n",
				      pstate_table->gfxclk_pstate.curr.min,
				      pstate_table->gfxclk_pstate.curr.max);
		break;
	case SMU_SCLK:
		ret = smu_v13_0_6_get_current_clk_freq_by_table(smu, SMU_GFXCLK,
								&now);
		if (ret) {
			dev_err(smu->adev->dev,
				"Attempt to get current gfx clk Failed!");
			return ret;
		}

		min_clk = pstate_table->gfxclk_pstate.curr.min;
		max_clk = pstate_table->gfxclk_pstate.curr.max;

		if (now < SMU_13_0_6_DSCLK_THRESHOLD) {
			size += sysfs_emit_at(buf, size, "S: %uMhz *\n",
					      now);
			size += sysfs_emit_at(buf, size, "0: %uMhz\n",
					      min_clk);
			size += sysfs_emit_at(buf, size, "1: %uMhz\n",
					      max_clk);

		} else if (!smu_v13_0_6_freqs_in_same_level(now, min_clk) &&
		    !smu_v13_0_6_freqs_in_same_level(now, max_clk)) {
			size += sysfs_emit_at(buf, size, "0: %uMhz\n",
					      min_clk);
			size += sysfs_emit_at(buf, size, "1: %uMhz *\n",
					      now);
			size += sysfs_emit_at(buf, size, "2: %uMhz\n",
					      max_clk);
		} else {
			size += sysfs_emit_at(buf, size, "0: %uMhz %s\n",
					      min_clk,
					      smu_v13_0_6_freqs_in_same_level(now, min_clk) ? "*" : "");
			size += sysfs_emit_at(buf, size, "1: %uMhz %s\n",
					      max_clk,
					      smu_v13_0_6_freqs_in_same_level(now, max_clk) ? "*" : "");
		}

		break;

	case SMU_OD_MCLK:
		size += sysfs_emit_at(buf, size, "%s:\n", "OD_MCLK");
		size += sysfs_emit_at(buf, size, "0: %uMhz\n1: %uMhz\n",
				      pstate_table->uclk_pstate.curr.min,
				      pstate_table->uclk_pstate.curr.max);
		break;
	case SMU_MCLK:
		ret = smu_v13_0_6_get_current_clk_freq_by_table(smu, SMU_UCLK,
								&now);
		if (ret) {
			dev_err(smu->adev->dev,
				"Attempt to get current mclk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_context->dpm_tables.uclk_table);

		return smu_v13_0_6_print_clks(smu, buf, size, single_dpm_table,
					      now, "mclk");

	case SMU_SOCCLK:
		ret = smu_v13_0_6_get_current_clk_freq_by_table(smu, SMU_SOCCLK,
								&now);
		if (ret) {
			dev_err(smu->adev->dev,
				"Attempt to get current socclk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_context->dpm_tables.soc_table);

		return smu_v13_0_6_print_clks(smu, buf, size, single_dpm_table,
					      now, "socclk");

	case SMU_FCLK:
		ret = smu_v13_0_6_get_current_clk_freq_by_table(smu, SMU_FCLK,
								&now);
		if (ret) {
			dev_err(smu->adev->dev,
				"Attempt to get current fclk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_context->dpm_tables.fclk_table);

		return smu_v13_0_6_print_clks(smu, buf, size, single_dpm_table,
					      now, "fclk");

	case SMU_VCLK:
		ret = smu_v13_0_6_get_current_clk_freq_by_table(smu, SMU_VCLK,
								&now);
		if (ret) {
			dev_err(smu->adev->dev,
				"Attempt to get current vclk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_context->dpm_tables.vclk_table);

		return smu_v13_0_6_print_clks(smu, buf, size, single_dpm_table,
					      now, "vclk");

	case SMU_DCLK:
		ret = smu_v13_0_6_get_current_clk_freq_by_table(smu, SMU_DCLK,
							       &now);
		if (ret) {
			dev_err(smu->adev->dev,
				"Attempt to get current dclk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_context->dpm_tables.dclk_table);

		return smu_v13_0_6_print_clks(smu, buf, size, single_dpm_table,
					      now, "dclk");

	default:
		break;
	}

	return size;
}

static int smu_v13_0_6_upload_dpm_level(struct smu_context *smu, bool max,
					uint32_t feature_mask, uint32_t level)
{
	struct smu_13_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	uint32_t freq;
	int ret = 0;

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_GFXCLK_BIT) &&
	    (feature_mask & FEATURE_MASK(FEATURE_DPM_GFXCLK))) {
		freq = dpm_context->dpm_tables.gfx_table.dpm_levels[level].value;
		ret = smu_cmn_send_smc_msg_with_param(
			smu,
			(max ? SMU_MSG_SetSoftMaxGfxClk :
			       SMU_MSG_SetSoftMinGfxclk),
			freq & 0xffff, NULL);
		if (ret) {
			dev_err(smu->adev->dev,
				"Failed to set soft %s gfxclk !\n",
				max ? "max" : "min");
			return ret;
		}
	}

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT) &&
	    (feature_mask & FEATURE_MASK(FEATURE_DPM_UCLK))) {
		freq = dpm_context->dpm_tables.uclk_table.dpm_levels[level]
			       .value;
		ret = smu_cmn_send_smc_msg_with_param(
			smu,
			(max ? SMU_MSG_SetSoftMaxByFreq :
			       SMU_MSG_SetSoftMinByFreq),
			(PPCLK_UCLK << 16) | (freq & 0xffff), NULL);
		if (ret) {
			dev_err(smu->adev->dev,
				"Failed to set soft %s memclk !\n",
				max ? "max" : "min");
			return ret;
		}
	}

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT) &&
	    (feature_mask & FEATURE_MASK(FEATURE_DPM_SOCCLK))) {
		freq = dpm_context->dpm_tables.soc_table.dpm_levels[level].value;
		ret = smu_cmn_send_smc_msg_with_param(
			smu,
			(max ? SMU_MSG_SetSoftMaxByFreq :
			       SMU_MSG_SetSoftMinByFreq),
			(PPCLK_SOCCLK << 16) | (freq & 0xffff), NULL);
		if (ret) {
			dev_err(smu->adev->dev,
				"Failed to set soft %s socclk !\n",
				max ? "max" : "min");
			return ret;
		}
	}

	return ret;
}

static int smu_v13_0_6_force_clk_levels(struct smu_context *smu,
					enum smu_clk_type type, uint32_t mask)
{
	struct smu_13_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	struct smu_13_0_dpm_table *single_dpm_table = NULL;
	uint32_t soft_min_level, soft_max_level;
	int ret = 0;

	soft_min_level = mask ? (ffs(mask) - 1) : 0;
	soft_max_level = mask ? (fls(mask) - 1) : 0;

	switch (type) {
	case SMU_SCLK:
		single_dpm_table = &(dpm_context->dpm_tables.gfx_table);
		if (soft_max_level >= single_dpm_table->count) {
			dev_err(smu->adev->dev,
				"Clock level specified %d is over max allowed %d\n",
				soft_max_level, single_dpm_table->count - 1);
			ret = -EINVAL;
			break;
		}

		ret = smu_v13_0_6_upload_dpm_level(
			smu, false, FEATURE_MASK(FEATURE_DPM_GFXCLK),
			soft_min_level);
		if (ret) {
			dev_err(smu->adev->dev,
				"Failed to upload boot level to lowest!\n");
			break;
		}

		ret = smu_v13_0_6_upload_dpm_level(
			smu, true, FEATURE_MASK(FEATURE_DPM_GFXCLK),
			soft_max_level);
		if (ret)
			dev_err(smu->adev->dev,
				"Failed to upload dpm max level to highest!\n");

		break;

	case SMU_MCLK:
	case SMU_SOCCLK:
	case SMU_FCLK:
		/*
		 * Should not arrive here since smu_13_0_6 does not
		 * support mclk/socclk/fclk softmin/softmax settings
		 */
		ret = -EINVAL;
		break;

	default:
		break;
	}

	return ret;
}

static int smu_v13_0_6_get_current_activity_percent(struct smu_context *smu,
						    enum amd_pp_sensors sensor,
						    uint32_t *value)
{
	int ret = 0;

	if (!value)
		return -EINVAL;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = smu_v13_0_6_get_smu_metrics_data(
			smu, METRICS_AVERAGE_GFXACTIVITY, value);
		break;
	case AMDGPU_PP_SENSOR_MEM_LOAD:
		ret = smu_v13_0_6_get_smu_metrics_data(
			smu, METRICS_AVERAGE_MEMACTIVITY, value);
		break;
	default:
		dev_err(smu->adev->dev,
			"Invalid sensor for retrieving clock activity\n");
		return -EINVAL;
	}

	return ret;
}

static int smu_v13_0_6_thermal_get_temperature(struct smu_context *smu,
					       enum amd_pp_sensors sensor,
					       uint32_t *value)
{
	int ret = 0;

	if (!value)
		return -EINVAL;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		ret = smu_v13_0_6_get_smu_metrics_data(
			smu, METRICS_TEMPERATURE_HOTSPOT, value);
		break;
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		ret = smu_v13_0_6_get_smu_metrics_data(
			smu, METRICS_TEMPERATURE_MEM, value);
		break;
	default:
		dev_err(smu->adev->dev, "Invalid sensor for retrieving temp\n");
		return -EINVAL;
	}

	return ret;
}

static int smu_v13_0_6_read_sensor(struct smu_context *smu,
				   enum amd_pp_sensors sensor, void *data,
				   uint32_t *size)
{
	int ret = 0;

	if (amdgpu_ras_intr_triggered())
		return 0;

	if (!data || !size)
		return -EINVAL;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_MEM_LOAD:
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = smu_v13_0_6_get_current_activity_percent(smu, sensor,
							       (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_INPUT_POWER:
		ret = smu_v13_0_6_get_smu_metrics_data(smu,
						       METRICS_CURR_SOCKETPOWER,
						       (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		ret = smu_v13_0_6_thermal_get_temperature(smu, sensor,
							  (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = smu_v13_0_6_get_current_clk_freq_by_table(
			smu, SMU_UCLK, (uint32_t *)data);
		/* the output clock frequency in 10K unit */
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = smu_v13_0_6_get_current_clk_freq_by_table(
			smu, SMU_GFXCLK, (uint32_t *)data);
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VDDGFX:
		ret = smu_v13_0_get_gfx_vdd(smu, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_AVG_POWER:
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int smu_v13_0_6_get_power_limit(struct smu_context *smu,
						uint32_t *current_power_limit,
						uint32_t *default_power_limit,
						uint32_t *max_power_limit,
						uint32_t *min_power_limit)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct PPTable_t *pptable =
		(struct PPTable_t *)smu_table->driver_pptable;
	uint32_t power_limit = 0;
	int ret;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetPptLimit, &power_limit);

	if (ret) {
		dev_err(smu->adev->dev, "Couldn't get PPT limit");
		return -EINVAL;
	}

	if (current_power_limit)
		*current_power_limit = power_limit;
	if (default_power_limit)
		*default_power_limit = power_limit;

	if (max_power_limit) {
		*max_power_limit = pptable->MaxSocketPowerLimit;
	}

	if (min_power_limit)
		*min_power_limit = 0;
	return 0;
}

static int smu_v13_0_6_set_power_limit(struct smu_context *smu,
				       enum smu_ppt_limit_type limit_type,
				       uint32_t limit)
{
	return smu_v13_0_set_power_limit(smu, limit_type, limit);
}

static int smu_v13_0_6_irq_process(struct amdgpu_device *adev,
				   struct amdgpu_irq_src *source,
				   struct amdgpu_iv_entry *entry)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_13_0_power_context *power_context = smu_power->power_context;
	uint32_t client_id = entry->client_id;
	uint32_t ctxid = entry->src_data[0];
	uint32_t src_id = entry->src_id;
	uint32_t data;

	if (client_id == SOC15_IH_CLIENTID_MP1) {
		if (src_id == IH_INTERRUPT_ID_TO_DRIVER) {
			/* ACK SMUToHost interrupt */
			data = RREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL);
			data = REG_SET_FIELD(data, MP1_SMN_IH_SW_INT_CTRL, INT_ACK, 1);
			WREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL, data);
			/*
			 * ctxid is used to distinguish different events for SMCToHost
			 * interrupt.
			 */
			switch (ctxid) {
			case IH_INTERRUPT_CONTEXT_ID_THERMAL_THROTTLING:
				/*
				 * Increment the throttle interrupt counter
				 */
				atomic64_inc(&smu->throttle_int_counter);

				if (!atomic_read(&adev->throttling_logging_enabled))
					return 0;

				/* This uses the new method which fixes the
				 * incorrect throttling status reporting
				 * through metrics table. For older FWs,
				 * it will be ignored.
				 */
				if (__ratelimit(&adev->throttling_logging_rs)) {
					atomic_set(
						&power_context->throttle_status,
							entry->src_data[1]);
					schedule_work(&smu->throttling_logging_work);
				}
				break;
			default:
				dev_dbg(adev->dev, "Unhandled context id %d from client:%d!\n",
									ctxid, client_id);
				break;
			}
		}
	}

	return 0;
}

static int smu_v13_0_6_set_irq_state(struct amdgpu_device *adev,
			      struct amdgpu_irq_src *source,
			      unsigned tyep,
			      enum amdgpu_interrupt_state state)
{
	uint32_t val = 0;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		/* For MP1 SW irqs */
		val = RREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT_CTRL, INT_MASK, 1);
		WREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL, val);

		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		/* For MP1 SW irqs */
		val = RREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT, ID, 0xFE);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT, VALID, 0);
		WREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT, val);

		val = RREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT_CTRL, INT_MASK, 0);
		WREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL, val);

		break;
	default:
		break;
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs smu_v13_0_6_irq_funcs = {
	.set = smu_v13_0_6_set_irq_state,
	.process = smu_v13_0_6_irq_process,
};

static int smu_v13_0_6_register_irq_handler(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct amdgpu_irq_src *irq_src = &smu->irq_source;
	int ret = 0;

	if (amdgpu_sriov_vf(adev))
		return 0;

	irq_src->num_types = 1;
	irq_src->funcs = &smu_v13_0_6_irq_funcs;

	ret = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_MP1,
				IH_INTERRUPT_ID_TO_DRIVER,
				irq_src);
	if (ret)
		return ret;

	return ret;
}

static int smu_v13_0_6_notify_unload(struct smu_context *smu)
{
	if (amdgpu_in_reset(smu->adev))
		return 0;

	dev_dbg(smu->adev->dev, "Notify PMFW about driver unload");
	/* Ignore return, just intimate FW that driver is not going to be there */
	smu_cmn_send_smc_msg(smu, SMU_MSG_PrepareMp1ForUnload, NULL);

	return 0;
}

static int smu_v13_0_6_mca_set_debug_mode(struct smu_context *smu, bool enable)
{
	/* NOTE: this ClearMcaOnRead message is only supported for smu version 85.72.0 or higher */
	if (!smu_v13_0_6_cap_supported(smu, SMU_CAP(MCA_DEBUG_MODE)))
		return 0;

	return smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_ClearMcaOnRead,
					       enable ? 0 : ClearMcaOnRead_UE_FLAG_MASK | ClearMcaOnRead_CE_POLL_MASK,
					       NULL);
}

static int smu_v13_0_6_system_features_control(struct smu_context *smu,
					       bool enable)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	if (amdgpu_sriov_vf(adev))
		return 0;

	if (enable) {
		if (!(adev->flags & AMD_IS_APU))
			ret = smu_v13_0_system_features_control(smu, enable);
	} else {
		/* Notify FW that the device is no longer driver managed */
		smu_v13_0_6_notify_unload(smu);
	}

	return ret;
}

static int smu_v13_0_6_set_gfx_soft_freq_limited_range(struct smu_context *smu,
						       uint32_t min,
						       uint32_t max)
{
	int ret;

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxGfxClk,
					      max & 0xffff, NULL);
	if (ret)
		return ret;

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMinGfxclk,
					      min & 0xffff, NULL);

	return ret;
}

static int smu_v13_0_6_set_performance_level(struct smu_context *smu,
					     enum amd_dpm_forced_level level)
{
	struct smu_dpm_context *smu_dpm = &(smu->smu_dpm);
	struct smu_13_0_dpm_context *dpm_context = smu_dpm->dpm_context;
	struct smu_13_0_dpm_table *gfx_table =
		&dpm_context->dpm_tables.gfx_table;
	struct smu_13_0_dpm_table *uclk_table =
		&dpm_context->dpm_tables.uclk_table;
	struct smu_umd_pstate_table *pstate_table = &smu->pstate_table;
	int ret;

	/* Disable determinism if switching to another mode */
	if ((smu_dpm->dpm_level == AMD_DPM_FORCED_LEVEL_PERF_DETERMINISM) &&
	    (level != AMD_DPM_FORCED_LEVEL_PERF_DETERMINISM)) {
		smu_cmn_send_smc_msg(smu, SMU_MSG_DisableDeterminism, NULL);
		pstate_table->gfxclk_pstate.curr.max = gfx_table->max;
	}

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_PERF_DETERMINISM:
		return 0;

	case AMD_DPM_FORCED_LEVEL_AUTO:
		if ((gfx_table->min != pstate_table->gfxclk_pstate.curr.min) ||
		    (gfx_table->max != pstate_table->gfxclk_pstate.curr.max)) {
			ret = smu_v13_0_6_set_gfx_soft_freq_limited_range(
				smu, gfx_table->min, gfx_table->max);
			if (ret)
				return ret;

			pstate_table->gfxclk_pstate.curr.min = gfx_table->min;
			pstate_table->gfxclk_pstate.curr.max = gfx_table->max;
		}

		if (uclk_table->max != pstate_table->uclk_pstate.curr.max) {
			/* Min UCLK is not expected to be changed */
			ret = smu_v13_0_set_soft_freq_limited_range(
				smu, SMU_UCLK, 0, uclk_table->max, false);
			if (ret)
				return ret;
			pstate_table->uclk_pstate.curr.max = uclk_table->max;
		}
		pstate_table->uclk_pstate.custom.max = 0;

		return 0;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
		return 0;
	default:
		break;
	}

	return -EINVAL;
}

static int smu_v13_0_6_set_soft_freq_limited_range(struct smu_context *smu,
						   enum smu_clk_type clk_type,
						   uint32_t min, uint32_t max,
						   bool automatic)
{
	struct smu_dpm_context *smu_dpm = &(smu->smu_dpm);
	struct smu_13_0_dpm_context *dpm_context = smu_dpm->dpm_context;
	struct smu_umd_pstate_table *pstate_table = &smu->pstate_table;
	struct amdgpu_device *adev = smu->adev;
	uint32_t min_clk;
	uint32_t max_clk;
	int ret = 0;

	if (clk_type != SMU_GFXCLK && clk_type != SMU_SCLK &&
	    clk_type != SMU_UCLK)
		return -EINVAL;

	if ((smu_dpm->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL) &&
	    (smu_dpm->dpm_level != AMD_DPM_FORCED_LEVEL_PERF_DETERMINISM))
		return -EINVAL;

	if (smu_dpm->dpm_level == AMD_DPM_FORCED_LEVEL_MANUAL) {
		if (min >= max) {
			dev_err(smu->adev->dev,
				"Minimum clk should be less than the maximum allowed clock\n");
			return -EINVAL;
		}

		if (clk_type == SMU_GFXCLK) {
			if ((min == pstate_table->gfxclk_pstate.curr.min) &&
			    (max == pstate_table->gfxclk_pstate.curr.max))
				return 0;

			ret = smu_v13_0_6_set_gfx_soft_freq_limited_range(
				smu, min, max);
			if (!ret) {
				pstate_table->gfxclk_pstate.curr.min = min;
				pstate_table->gfxclk_pstate.curr.max = max;
			}
		}

		if (clk_type == SMU_UCLK) {
			if (max == pstate_table->uclk_pstate.curr.max)
				return 0;
			/* For VF, only allowed in FW versions 85.102 or greater */
			if (!smu_v13_0_6_cap_supported(smu,
						       SMU_CAP(SET_UCLK_MAX)))
				return -EOPNOTSUPP;
			/* Only max clock limiting is allowed for UCLK */
			ret = smu_v13_0_set_soft_freq_limited_range(
				smu, SMU_UCLK, 0, max, false);
			if (!ret)
				pstate_table->uclk_pstate.curr.max = max;
		}

		return ret;
	}

	if (smu_dpm->dpm_level == AMD_DPM_FORCED_LEVEL_PERF_DETERMINISM) {
		if (!max || (max < dpm_context->dpm_tables.gfx_table.min) ||
		    (max > dpm_context->dpm_tables.gfx_table.max)) {
			dev_warn(
				adev->dev,
				"Invalid max frequency %d MHz specified for determinism\n",
				max);
			return -EINVAL;
		}

		/* Restore default min/max clocks and enable determinism */
		min_clk = dpm_context->dpm_tables.gfx_table.min;
		max_clk = dpm_context->dpm_tables.gfx_table.max;
		ret = smu_v13_0_6_set_gfx_soft_freq_limited_range(smu, min_clk,
								 max_clk);
		if (!ret) {
			usleep_range(500, 1000);
			ret = smu_cmn_send_smc_msg_with_param(
				smu, SMU_MSG_EnableDeterminism, max, NULL);
			if (ret) {
				dev_err(adev->dev,
					"Failed to enable determinism at GFX clock %d MHz\n",
					max);
			} else {
				pstate_table->gfxclk_pstate.curr.min = min_clk;
				pstate_table->gfxclk_pstate.curr.max = max;
			}
		}
	}

	return ret;
}

static int smu_v13_0_6_usr_edit_dpm_table(struct smu_context *smu,
					  enum PP_OD_DPM_TABLE_COMMAND type,
					  long input[], uint32_t size)
{
	struct smu_dpm_context *smu_dpm = &(smu->smu_dpm);
	struct smu_13_0_dpm_context *dpm_context = smu_dpm->dpm_context;
	struct smu_umd_pstate_table *pstate_table = &smu->pstate_table;
	uint32_t min_clk;
	uint32_t max_clk;
	int ret = 0;

	/* Only allowed in manual or determinism mode */
	if ((smu_dpm->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL) &&
	    (smu_dpm->dpm_level != AMD_DPM_FORCED_LEVEL_PERF_DETERMINISM))
		return -EINVAL;

	switch (type) {
	case PP_OD_EDIT_SCLK_VDDC_TABLE:
		if (size != 2) {
			dev_err(smu->adev->dev,
				"Input parameter number not correct\n");
			return -EINVAL;
		}

		if (input[0] == 0) {
			if (input[1] < dpm_context->dpm_tables.gfx_table.min) {
				dev_warn(
					smu->adev->dev,
					"Minimum GFX clk (%ld) MHz specified is less than the minimum allowed (%d) MHz\n",
					input[1],
					dpm_context->dpm_tables.gfx_table.min);
				pstate_table->gfxclk_pstate.custom.min =
					pstate_table->gfxclk_pstate.curr.min;
				return -EINVAL;
			}

			pstate_table->gfxclk_pstate.custom.min = input[1];
		} else if (input[0] == 1) {
			if (input[1] > dpm_context->dpm_tables.gfx_table.max) {
				dev_warn(
					smu->adev->dev,
					"Maximum GFX clk (%ld) MHz specified is greater than the maximum allowed (%d) MHz\n",
					input[1],
					dpm_context->dpm_tables.gfx_table.max);
				pstate_table->gfxclk_pstate.custom.max =
					pstate_table->gfxclk_pstate.curr.max;
				return -EINVAL;
			}

			pstate_table->gfxclk_pstate.custom.max = input[1];
		} else {
			return -EINVAL;
		}
		break;
	case PP_OD_EDIT_MCLK_VDDC_TABLE:
		if (size != 2) {
			dev_err(smu->adev->dev,
				"Input parameter number not correct\n");
			return -EINVAL;
		}

		if (!smu_cmn_feature_is_enabled(smu,
						SMU_FEATURE_DPM_UCLK_BIT)) {
			dev_warn(smu->adev->dev,
				 "UCLK_LIMITS setting not supported!\n");
			return -EOPNOTSUPP;
		}

		if (input[0] == 0) {
			dev_info(smu->adev->dev,
				 "Setting min UCLK level is not supported");
			return -EINVAL;
		} else if (input[0] == 1) {
			if (input[1] > dpm_context->dpm_tables.uclk_table.max) {
				dev_warn(
					smu->adev->dev,
					"Maximum UCLK (%ld) MHz specified is greater than the maximum allowed (%d) MHz\n",
					input[1],
					dpm_context->dpm_tables.uclk_table.max);
				pstate_table->uclk_pstate.custom.max =
					pstate_table->uclk_pstate.curr.max;
				return -EINVAL;
			}

			pstate_table->uclk_pstate.custom.max = input[1];
		}
		break;

	case PP_OD_RESTORE_DEFAULT_TABLE:
		if (size != 0) {
			dev_err(smu->adev->dev,
				"Input parameter number not correct\n");
			return -EINVAL;
		} else {
			/* Use the default frequencies for manual and determinism mode */
			min_clk = dpm_context->dpm_tables.gfx_table.min;
			max_clk = dpm_context->dpm_tables.gfx_table.max;

			ret = smu_v13_0_6_set_soft_freq_limited_range(
				smu, SMU_GFXCLK, min_clk, max_clk, false);

			if (ret)
				return ret;

			min_clk = dpm_context->dpm_tables.uclk_table.min;
			max_clk = dpm_context->dpm_tables.uclk_table.max;
			ret = smu_v13_0_6_set_soft_freq_limited_range(
				smu, SMU_UCLK, min_clk, max_clk, false);
			if (ret)
				return ret;
			pstate_table->uclk_pstate.custom.max = 0;
		}
		break;
	case PP_OD_COMMIT_DPM_TABLE:
		if (size != 0) {
			dev_err(smu->adev->dev,
				"Input parameter number not correct\n");
			return -EINVAL;
		} else {
			if (!pstate_table->gfxclk_pstate.custom.min)
				pstate_table->gfxclk_pstate.custom.min =
					pstate_table->gfxclk_pstate.curr.min;

			if (!pstate_table->gfxclk_pstate.custom.max)
				pstate_table->gfxclk_pstate.custom.max =
					pstate_table->gfxclk_pstate.curr.max;

			min_clk = pstate_table->gfxclk_pstate.custom.min;
			max_clk = pstate_table->gfxclk_pstate.custom.max;

			ret = smu_v13_0_6_set_soft_freq_limited_range(
				smu, SMU_GFXCLK, min_clk, max_clk, false);

			if (ret)
				return ret;

			if (!pstate_table->uclk_pstate.custom.max)
				return 0;

			min_clk = pstate_table->uclk_pstate.curr.min;
			max_clk = pstate_table->uclk_pstate.custom.max;
			return smu_v13_0_6_set_soft_freq_limited_range(
				smu, SMU_UCLK, min_clk, max_clk, false);
		}
		break;
	default:
		return -ENOSYS;
	}

	return ret;
}

static int smu_v13_0_6_get_enabled_mask(struct smu_context *smu,
					uint64_t *feature_mask)
{
	int ret;

	ret = smu_cmn_get_enabled_mask(smu, feature_mask);

	if (ret == -EIO && !smu_v13_0_6_cap_supported(smu, SMU_CAP(DPM))) {
		*feature_mask = 0;
		ret = 0;
	}

	return ret;
}

static bool smu_v13_0_6_is_dpm_running(struct smu_context *smu)
{
	int ret;
	uint64_t feature_enabled;

	ret = smu_v13_0_6_get_enabled_mask(smu, &feature_enabled);

	if (ret)
		return false;

	return !!(feature_enabled & SMC_DPM_FEATURE);
}

static int smu_v13_0_6_request_i2c_xfer(struct smu_context *smu,
					void *table_data)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *table = &smu_table->driver_table;
	struct amdgpu_device *adev = smu->adev;
	uint32_t table_size;
	int ret = 0;

	if (!table_data)
		return -EINVAL;

	table_size = smu_table->tables[SMU_TABLE_I2C_COMMANDS].size;

	memcpy(table->cpu_addr, table_data, table_size);
	/* Flush hdp cache */
	amdgpu_asic_flush_hdp(adev, NULL);
	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_RequestI2cTransaction,
					  NULL);

	return ret;
}

static int smu_v13_0_6_i2c_xfer(struct i2c_adapter *i2c_adap,
				struct i2c_msg *msg, int num_msgs)
{
	struct amdgpu_smu_i2c_bus *smu_i2c = i2c_get_adapdata(i2c_adap);
	struct amdgpu_device *adev = smu_i2c->adev;
	struct smu_context *smu = adev->powerplay.pp_handle;
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *table = &smu_table->driver_table;
	SwI2cRequest_t *req, *res = (SwI2cRequest_t *)table->cpu_addr;
	int i, j, r, c;
	u16 dir;

	if (!adev->pm.dpm_enabled)
		return -EBUSY;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->I2CcontrollerPort = smu_i2c->port;
	req->I2CSpeed = I2C_SPEED_FAST_400K;
	req->SlaveAddress = msg[0].addr << 1; /* wants an 8-bit address */
	dir = msg[0].flags & I2C_M_RD;

	for (c = i = 0; i < num_msgs; i++) {
		for (j = 0; j < msg[i].len; j++, c++) {
			SwI2cCmd_t *cmd = &req->SwI2cCmds[c];

			if (!(msg[i].flags & I2C_M_RD)) {
				/* write */
				cmd->CmdConfig |= CMDCONFIG_READWRITE_MASK;
				cmd->ReadWriteData = msg[i].buf[j];
			}

			if ((dir ^ msg[i].flags) & I2C_M_RD) {
				/* The direction changes.
				 */
				dir = msg[i].flags & I2C_M_RD;
				cmd->CmdConfig |= CMDCONFIG_RESTART_MASK;
			}

			req->NumCmds++;

			/*
			 * Insert STOP if we are at the last byte of either last
			 * message for the transaction or the client explicitly
			 * requires a STOP at this particular message.
			 */
			if ((j == msg[i].len - 1) &&
			    ((i == num_msgs - 1) || (msg[i].flags & I2C_M_STOP))) {
				cmd->CmdConfig &= ~CMDCONFIG_RESTART_MASK;
				cmd->CmdConfig |= CMDCONFIG_STOP_MASK;
			}
		}
	}
	mutex_lock(&adev->pm.mutex);
	r = smu_v13_0_6_request_i2c_xfer(smu, req);
	if (r) {
		/* Retry once, in case of an i2c collision */
		r = smu_v13_0_6_request_i2c_xfer(smu, req);
		if (r)
			goto fail;
	}

	for (c = i = 0; i < num_msgs; i++) {
		if (!(msg[i].flags & I2C_M_RD)) {
			c += msg[i].len;
			continue;
		}
		for (j = 0; j < msg[i].len; j++, c++) {
			SwI2cCmd_t *cmd = &res->SwI2cCmds[c];

			msg[i].buf[j] = cmd->ReadWriteData;
		}
	}
	r = num_msgs;
fail:
	mutex_unlock(&adev->pm.mutex);
	kfree(req);
	return r;
}

static u32 smu_v13_0_6_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm smu_v13_0_6_i2c_algo = {
	.master_xfer = smu_v13_0_6_i2c_xfer,
	.functionality = smu_v13_0_6_i2c_func,
};

static const struct i2c_adapter_quirks smu_v13_0_6_i2c_control_quirks = {
	.flags = I2C_AQ_COMB | I2C_AQ_COMB_SAME_ADDR | I2C_AQ_NO_ZERO_LEN,
	.max_read_len = MAX_SW_I2C_COMMANDS,
	.max_write_len = MAX_SW_I2C_COMMANDS,
	.max_comb_1st_msg_len = 2,
	.max_comb_2nd_msg_len = MAX_SW_I2C_COMMANDS - 2,
};

static int smu_v13_0_6_i2c_control_init(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int res, i;

	for (i = 0; i < MAX_SMU_I2C_BUSES; i++) {
		struct amdgpu_smu_i2c_bus *smu_i2c = &adev->pm.smu_i2c[i];
		struct i2c_adapter *control = &smu_i2c->adapter;

		smu_i2c->adev = adev;
		smu_i2c->port = i;
		mutex_init(&smu_i2c->mutex);
		control->owner = THIS_MODULE;
		control->dev.parent = &adev->pdev->dev;
		control->algo = &smu_v13_0_6_i2c_algo;
		snprintf(control->name, sizeof(control->name), "AMDGPU SMU %d", i);
		control->quirks = &smu_v13_0_6_i2c_control_quirks;
		i2c_set_adapdata(control, smu_i2c);

		res = i2c_add_adapter(control);
		if (res) {
			DRM_ERROR("Failed to register hw i2c, err: %d\n", res);
			goto Out_err;
		}
	}

	adev->pm.ras_eeprom_i2c_bus = &adev->pm.smu_i2c[0].adapter;
	adev->pm.fru_eeprom_i2c_bus = &adev->pm.smu_i2c[0].adapter;

	return 0;
Out_err:
	for ( ; i >= 0; i--) {
		struct amdgpu_smu_i2c_bus *smu_i2c = &adev->pm.smu_i2c[i];
		struct i2c_adapter *control = &smu_i2c->adapter;

		i2c_del_adapter(control);
	}
	return res;
}

static void smu_v13_0_6_i2c_control_fini(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int i;

	for (i = 0; i < MAX_SMU_I2C_BUSES; i++) {
		struct amdgpu_smu_i2c_bus *smu_i2c = &adev->pm.smu_i2c[i];
		struct i2c_adapter *control = &smu_i2c->adapter;

		i2c_del_adapter(control);
	}
	adev->pm.ras_eeprom_i2c_bus = NULL;
	adev->pm.fru_eeprom_i2c_bus = NULL;
}

static void smu_v13_0_6_get_unique_id(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_table_context *smu_table = &smu->smu_table;
	struct PPTable_t *pptable =
		(struct PPTable_t *)smu_table->driver_pptable;

	adev->unique_id = pptable->PublicSerialNumber_AID;
}

static int smu_v13_0_6_get_bamaco_support(struct smu_context *smu)
{
	/* smu_13_0_6 does not support baco */

	return 0;
}

static const char *const throttling_logging_label[] = {
	[THROTTLER_PROCHOT_BIT] = "Prochot",
	[THROTTLER_PPT_BIT] = "PPT",
	[THROTTLER_THERMAL_SOCKET_BIT] = "SOC",
	[THROTTLER_THERMAL_VR_BIT] = "VR",
	[THROTTLER_THERMAL_HBM_BIT] = "HBM"
};

static void smu_v13_0_6_log_thermal_throttling_event(struct smu_context *smu)
{
	int throttler_idx, throttling_events = 0, buf_idx = 0;
	struct amdgpu_device *adev = smu->adev;
	uint32_t throttler_status;
	char log_buf[256];

	throttler_status = smu_v13_0_6_get_throttler_status(smu);
	if (!throttler_status)
		return;

	memset(log_buf, 0, sizeof(log_buf));
	for (throttler_idx = 0;
	     throttler_idx < ARRAY_SIZE(throttling_logging_label);
	     throttler_idx++) {
		if (throttler_status & (1U << throttler_idx)) {
			throttling_events++;
			buf_idx += snprintf(
				log_buf + buf_idx, sizeof(log_buf) - buf_idx,
				"%s%s", throttling_events > 1 ? " and " : "",
				throttling_logging_label[throttler_idx]);
			if (buf_idx >= sizeof(log_buf)) {
				dev_err(adev->dev, "buffer overflow!\n");
				log_buf[sizeof(log_buf) - 1] = '\0';
				break;
			}
		}
	}

	dev_warn(adev->dev,
		 "WARN: GPU is throttled, expect performance decrease. %s.\n",
		 log_buf);
	kgd2kfd_smi_event_throttle(
		smu->adev->kfd.dev,
		smu_cmn_get_indep_throttler_status(throttler_status,
						   smu_v13_0_6_throttler_map));
}

static int
smu_v13_0_6_get_current_pcie_link_width_level(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	return REG_GET_FIELD(RREG32_PCIE(smnPCIE_LC_LINK_WIDTH_CNTL),
			     PCIE_LC_LINK_WIDTH_CNTL, LC_LINK_WIDTH_RD);
}

static int smu_v13_0_6_get_current_pcie_link_speed(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t speed_level;
	uint32_t esm_ctrl;

	/* TODO: confirm this on real target */
	esm_ctrl = RREG32_PCIE(smnPCIE_ESM_CTRL);
	if ((esm_ctrl >> 15) & 0x1)
		return (((esm_ctrl >> 8) & 0x7F) + 128);

	speed_level = (RREG32_PCIE(smnPCIE_LC_SPEED_CNTL) &
		PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE_MASK)
		>> PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE__SHIFT;
	if (speed_level > LINK_SPEED_MAX)
		speed_level = 0;

	return pcie_gen_to_speed(speed_level + 1);
}

static ssize_t smu_v13_0_6_get_gpu_metrics(struct smu_context *smu, void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct gpu_metrics_v1_7 *gpu_metrics =
		(struct gpu_metrics_v1_7 *)smu_table->gpu_metrics_table;
	bool flag = !smu_v13_0_6_cap_supported(smu, SMU_CAP(UNI_METRICS));
	int ret = 0, xcc_id, inst, i, j, k, idx;
	struct amdgpu_device *adev = smu->adev;
	MetricsTableX_t *metrics_x;
	MetricsTableA_t *metrics_a;
	struct amdgpu_xcp *xcp;
	u16 link_width_level;
	u32 inst_mask;
	bool per_inst;

	metrics_x = kzalloc(max(sizeof(MetricsTableX_t), sizeof(MetricsTableA_t)), GFP_KERNEL);
	ret = smu_v13_0_6_get_metrics_table(smu, metrics_x, true);
	if (ret) {
		kfree(metrics_x);
		return ret;
	}

	metrics_a = (MetricsTableA_t *)metrics_x;

	smu_cmn_init_soft_gpu_metrics(gpu_metrics, 1, 7);

	gpu_metrics->temperature_hotspot =
		SMUQ10_ROUND(GET_METRIC_FIELD(MaxSocketTemperature, flag));
	/* Individual HBM stack temperature is not reported */
	gpu_metrics->temperature_mem =
		SMUQ10_ROUND(GET_METRIC_FIELD(MaxHbmTemperature, flag));
	/* Reports max temperature of all voltage rails */
	gpu_metrics->temperature_vrsoc =
		SMUQ10_ROUND(GET_METRIC_FIELD(MaxVrTemperature, flag));

	gpu_metrics->average_gfx_activity =
		SMUQ10_ROUND(GET_METRIC_FIELD(SocketGfxBusy, flag));
	gpu_metrics->average_umc_activity =
		SMUQ10_ROUND(GET_METRIC_FIELD(DramBandwidthUtilization, flag));

	gpu_metrics->mem_max_bandwidth =
		SMUQ10_ROUND(GET_METRIC_FIELD(MaxDramBandwidth, flag));

	gpu_metrics->curr_socket_power =
		SMUQ10_ROUND(GET_METRIC_FIELD(SocketPower, flag));
	/* Energy counter reported in 15.259uJ (2^-16) units */
	gpu_metrics->energy_accumulator = GET_METRIC_FIELD(SocketEnergyAcc, flag);

	for (i = 0; i < MAX_GFX_CLKS; i++) {
		xcc_id = GET_INST(GC, i);
		if (xcc_id >= 0)
			gpu_metrics->current_gfxclk[i] =
				SMUQ10_ROUND(GET_METRIC_FIELD(GfxclkFrequency, flag)[xcc_id]);

		if (i < MAX_CLKS) {
			gpu_metrics->current_socclk[i] =
				SMUQ10_ROUND(GET_METRIC_FIELD(SocclkFrequency, flag)[i]);
			inst = GET_INST(VCN, i);
			if (inst >= 0) {
				gpu_metrics->current_vclk0[i] =
					SMUQ10_ROUND(GET_METRIC_FIELD(VclkFrequency, flag)[inst]);
				gpu_metrics->current_dclk0[i] =
					SMUQ10_ROUND(GET_METRIC_FIELD(DclkFrequency, flag)[inst]);
			}
		}
	}

	gpu_metrics->current_uclk = SMUQ10_ROUND(GET_METRIC_FIELD(UclkFrequency, flag));

	/* Total accumulated cycle counter */
	gpu_metrics->accumulation_counter = GET_METRIC_FIELD(AccumulationCounter, flag);

	/* Accumulated throttler residencies */
	gpu_metrics->prochot_residency_acc = GET_METRIC_FIELD(ProchotResidencyAcc, flag);
	gpu_metrics->ppt_residency_acc = GET_METRIC_FIELD(PptResidencyAcc, flag);
	gpu_metrics->socket_thm_residency_acc = GET_METRIC_FIELD(SocketThmResidencyAcc, flag);
	gpu_metrics->vr_thm_residency_acc = GET_METRIC_FIELD(VrThmResidencyAcc, flag);
	gpu_metrics->hbm_thm_residency_acc = GET_METRIC_FIELD(HbmThmResidencyAcc, flag);

	/* Clock Lock Status. Each bit corresponds to each GFXCLK instance */
	gpu_metrics->gfxclk_lock_status = GET_METRIC_FIELD(GfxLockXCDMak, flag) >> GET_INST(GC, 0);

	if (!(adev->flags & AMD_IS_APU)) {
		/*Check smu version, PCIE link speed and width will be reported from pmfw metric
		 * table for both pf & one vf for smu version 85.99.0 or higher else report only
		 * for pf from registers
		 */
		if (smu_v13_0_6_cap_supported(smu, SMU_CAP(PCIE_METRICS))) {
			gpu_metrics->pcie_link_width = metrics_x->PCIeLinkWidth;
			gpu_metrics->pcie_link_speed =
				pcie_gen_to_speed(metrics_x->PCIeLinkSpeed);
		} else if (!amdgpu_sriov_vf(adev)) {
			link_width_level = smu_v13_0_6_get_current_pcie_link_width_level(smu);
			if (link_width_level > MAX_LINK_WIDTH)
				link_width_level = 0;

			gpu_metrics->pcie_link_width =
				DECODE_LANE_WIDTH(link_width_level);
			gpu_metrics->pcie_link_speed =
				smu_v13_0_6_get_current_pcie_link_speed(smu);
		}

		gpu_metrics->pcie_bandwidth_acc =
				SMUQ10_ROUND(metrics_x->PcieBandwidthAcc[0]);
		gpu_metrics->pcie_bandwidth_inst =
				SMUQ10_ROUND(metrics_x->PcieBandwidth[0]);
		gpu_metrics->pcie_l0_to_recov_count_acc =
				metrics_x->PCIeL0ToRecoveryCountAcc;
		gpu_metrics->pcie_replay_count_acc =
				metrics_x->PCIenReplayAAcc;
		gpu_metrics->pcie_replay_rover_count_acc =
				metrics_x->PCIenReplayARolloverCountAcc;
		gpu_metrics->pcie_nak_sent_count_acc =
				metrics_x->PCIeNAKSentCountAcc;
		gpu_metrics->pcie_nak_rcvd_count_acc =
				metrics_x->PCIeNAKReceivedCountAcc;
		if (smu_v13_0_6_cap_supported(smu, SMU_CAP(OTHER_END_METRICS)))
			gpu_metrics->pcie_lc_perf_other_end_recovery =
				metrics_x->PCIeOtherEndRecoveryAcc;

	}

	gpu_metrics->system_clock_counter = ktime_get_boottime_ns();

	gpu_metrics->gfx_activity_acc =
		SMUQ10_ROUND(GET_METRIC_FIELD(SocketGfxBusyAcc, flag));
	gpu_metrics->mem_activity_acc =
		SMUQ10_ROUND(GET_METRIC_FIELD(DramBandwidthUtilizationAcc, flag));

	for (i = 0; i < NUM_XGMI_LINKS; i++) {
		gpu_metrics->xgmi_read_data_acc[i] =
			SMUQ10_ROUND(GET_METRIC_FIELD(XgmiReadDataSizeAcc, flag)[i]);
		gpu_metrics->xgmi_write_data_acc[i] =
			SMUQ10_ROUND(GET_METRIC_FIELD(XgmiWriteDataSizeAcc, flag)[i]);
		ret = amdgpu_get_xgmi_link_status(adev, i);
		if (ret >= 0)
			gpu_metrics->xgmi_link_status[i] = ret;
	}

	gpu_metrics->num_partition = adev->xcp_mgr->num_xcps;

	per_inst = smu_v13_0_6_cap_supported(smu, SMU_CAP(PER_INST_METRICS));

	for_each_xcp(adev->xcp_mgr, xcp, i) {
		amdgpu_xcp_get_inst_details(xcp, AMDGPU_XCP_VCN, &inst_mask);
		idx = 0;
		for_each_inst(k, inst_mask) {
			/* Both JPEG and VCN has same instances */
			inst = GET_INST(VCN, k);

			for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
				gpu_metrics->xcp_stats[i].jpeg_busy
					[(idx * adev->jpeg.num_jpeg_rings) + j] =
					SMUQ10_ROUND(GET_METRIC_FIELD(JpegBusy, flag)
							[(inst * adev->jpeg.num_jpeg_rings) + j]);
			}
			gpu_metrics->xcp_stats[i].vcn_busy[idx] =
			       SMUQ10_ROUND(GET_METRIC_FIELD(VcnBusy, flag)[inst]);
			idx++;

		}

		if (per_inst) {
			amdgpu_xcp_get_inst_details(xcp, AMDGPU_XCP_GFX, &inst_mask);
			idx = 0;
			for_each_inst(k, inst_mask) {
				inst = GET_INST(GC, k);
				gpu_metrics->xcp_stats[i].gfx_busy_inst[idx] =
					SMUQ10_ROUND(metrics_x->GfxBusy[inst]);
				gpu_metrics->xcp_stats[i].gfx_busy_acc[idx] =
					SMUQ10_ROUND(metrics_x->GfxBusyAcc[inst]);

				if (smu_v13_0_6_cap_supported(
					    smu, SMU_CAP(HST_LIMIT_METRICS)))
					gpu_metrics->xcp_stats[i].gfx_below_host_limit_acc[idx] =
						SMUQ10_ROUND(metrics_x->GfxclkBelowHostLimitAcc
								[inst]);
				idx++;
			}
		}
	}

	gpu_metrics->xgmi_link_width = SMUQ10_ROUND(GET_METRIC_FIELD(XgmiWidth, flag));
	gpu_metrics->xgmi_link_speed = SMUQ10_ROUND(GET_METRIC_FIELD(XgmiBitrate, flag));

	gpu_metrics->firmware_timestamp = GET_METRIC_FIELD(Timestamp, flag);

	*table = (void *)gpu_metrics;
	kfree(metrics_x);

	return sizeof(*gpu_metrics);
}

static void smu_v13_0_6_restore_pci_config(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int i;

	for (i = 0; i < 16; i++)
		pci_write_config_dword(adev->pdev, i * 4,
				       adev->pdev->saved_config_space[i]);
	pci_restore_msi_state(adev->pdev);
}

static int smu_v13_0_6_mode2_reset(struct smu_context *smu)
{
	int ret = 0, index;
	struct amdgpu_device *adev = smu->adev;
	int timeout = 10;

	index = smu_cmn_to_asic_specific_index(smu, CMN2ASIC_MAPPING_MSG,
					       SMU_MSG_GfxDeviceDriverReset);
	if (index < 0)
		return index;

	mutex_lock(&smu->message_lock);

	ret = smu_cmn_send_msg_without_waiting(smu, (uint16_t)index,
					       SMU_RESET_MODE_2);

	/* Reset takes a bit longer, wait for 200ms. */
	msleep(200);

	dev_dbg(smu->adev->dev, "restore config space...\n");
	/* Restore the config space saved during init */
	amdgpu_device_load_pci_state(adev->pdev);

	/* Certain platforms have switches which assign virtual BAR values to
	 * devices. OS uses the virtual BAR values and device behind the switch
	 * is assgined another BAR value. When device's config space registers
	 * are queried, switch returns the virtual BAR values. When mode-2 reset
	 * is performed, switch is unaware of it, and will continue to return
	 * the same virtual values to the OS.This affects
	 * pci_restore_config_space() API as it doesn't write the value saved if
	 * the current value read from config space is the same as what is
	 * saved. As a workaround, make sure the config space is restored
	 * always.
	 */
	if (!(adev->flags & AMD_IS_APU))
		smu_v13_0_6_restore_pci_config(smu);

	dev_dbg(smu->adev->dev, "wait for reset ack\n");
	do {
		ret = smu_cmn_wait_for_response(smu);
		/* Wait a bit more time for getting ACK */
		if (ret == -ETIME) {
			--timeout;
			usleep_range(500, 1000);
			continue;
		}

		if (ret)
			goto out;

	} while (ret == -ETIME && timeout);

out:
	mutex_unlock(&smu->message_lock);

	if (ret)
		dev_err(adev->dev, "failed to send mode2 reset, error code %d",
			ret);

	return ret;
}

static int smu_v13_0_6_get_thermal_temperature_range(struct smu_context *smu,
						     struct smu_temperature_range *range)
{
	struct amdgpu_device *adev = smu->adev;
	u32 aid_temp, xcd_temp, max_temp;
	u32 ccd_temp = 0;
	int ret;

	if (amdgpu_sriov_vf(smu->adev))
		return 0;

	if (!range)
		return -EINVAL;

	/*Check smu version, GetCtfLimit message only supported for smu version 85.69 or higher */
	if (!smu_v13_0_6_cap_supported(smu, SMU_CAP(CTF_LIMIT)))
		return 0;

	/* Get SOC Max operating temperature */
	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetCTFLimit,
					      PPSMC_AID_THM_TYPE, &aid_temp);
	if (ret)
		goto failed;
	if (adev->flags & AMD_IS_APU) {
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetCTFLimit,
						      PPSMC_CCD_THM_TYPE, &ccd_temp);
		if (ret)
			goto failed;
	}
	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetCTFLimit,
					      PPSMC_XCD_THM_TYPE, &xcd_temp);
	if (ret)
		goto failed;
	range->hotspot_emergency_max = max3(aid_temp, xcd_temp, ccd_temp) *
				       SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;

	/* Get HBM Max operating temperature */
	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetCTFLimit,
					      PPSMC_HBM_THM_TYPE, &max_temp);
	if (ret)
		goto failed;
	range->mem_emergency_max =
		max_temp * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;

	/* Get SOC thermal throttle limit */
	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetThermalLimit,
					      PPSMC_THROTTLING_LIMIT_TYPE_SOCKET,
					      &max_temp);
	if (ret)
		goto failed;
	range->hotspot_crit_max =
		max_temp * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;

	/* Get HBM thermal throttle limit */
	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetThermalLimit,
					      PPSMC_THROTTLING_LIMIT_TYPE_HBM,
					      &max_temp);
	if (ret)
		goto failed;

	range->mem_crit_max = max_temp * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;

failed:
	return ret;
}

static int smu_v13_0_6_mode1_reset(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	u32 fatal_err, param;
	int ret = 0;

	fatal_err = 0;
	param = SMU_RESET_MODE_1;

	/* fatal error triggered by ras, PMFW supports the flag */
	if (amdgpu_ras_get_fed_status(adev))
		fatal_err = 1;

	param |= (fatal_err << 16);
	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GfxDeviceDriverReset,
					      param, NULL);

	if (!ret)
		msleep(SMU13_MODE1_RESET_WAIT_TIME_IN_MS);

	return ret;
}

static bool smu_v13_0_6_is_mode1_reset_supported(struct smu_context *smu)
{
	return true;
}

static bool smu_v13_0_6_is_mode2_reset_supported(struct smu_context *smu)
{
	return true;
}

static int smu_v13_0_6_smu_send_hbm_bad_page_num(struct smu_context *smu,
						 uint32_t size)
{
	int ret = 0;

	/* message SMU to update the bad page number on SMUBUS */
	ret = smu_cmn_send_smc_msg_with_param(
		smu, SMU_MSG_SetNumBadHbmPagesRetired, size, NULL);
	if (ret)
		dev_err(smu->adev->dev,
			"[%s] failed to message SMU to update HBM bad pages number\n",
			__func__);

	return ret;
}

static int smu_v13_0_6_send_rma_reason(struct smu_context *smu)
{
	int ret;

	/* NOTE: the message is only valid on dGPU with pmfw 85.90.0 and above */
	if (!smu_v13_0_6_cap_supported(smu, SMU_CAP(RMA_MSG)))
		return 0;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_RmaDueToBadPageThreshold, NULL);
	if (ret)
		dev_err(smu->adev->dev,
			"[%s] failed to send BadPageThreshold event to SMU\n",
			__func__);

	return ret;
}

static int smu_v13_0_6_reset_sdma(struct smu_context *smu, uint32_t inst_mask)
{
	int ret = 0;

	if (!smu_v13_0_6_cap_supported(smu, SMU_CAP(SDMA_RESET)))
		return -EOPNOTSUPP;

	ret = smu_cmn_send_smc_msg_with_param(smu,
						SMU_MSG_ResetSDMA, inst_mask, NULL);
	if (ret)
		dev_err(smu->adev->dev,
			"failed to send ResetSDMA event with mask 0x%x\n",
			inst_mask);

	return ret;
}

static int mca_smu_set_debug_mode(struct amdgpu_device *adev, bool enable)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	return smu_v13_0_6_mca_set_debug_mode(smu, enable);
}

static int smu_v13_0_6_get_valid_mca_count(struct smu_context *smu, enum amdgpu_mca_error_type type, uint32_t *count)
{
	uint32_t msg;
	int ret;

	if (!count)
		return -EINVAL;

	switch (type) {
	case AMDGPU_MCA_ERROR_TYPE_UE:
		msg = SMU_MSG_QueryValidMcaCount;
		break;
	case AMDGPU_MCA_ERROR_TYPE_CE:
		msg = SMU_MSG_QueryValidMcaCeCount;
		break;
	default:
		return -EINVAL;
	}

	ret = smu_cmn_send_smc_msg(smu, msg, count);
	if (ret) {
		*count = 0;
		return ret;
	}

	return 0;
}

static int __smu_v13_0_6_mca_dump_bank(struct smu_context *smu, enum amdgpu_mca_error_type type,
				       int idx, int offset, uint32_t *val)
{
	uint32_t msg, param;

	switch (type) {
	case AMDGPU_MCA_ERROR_TYPE_UE:
		msg = SMU_MSG_McaBankDumpDW;
		break;
	case AMDGPU_MCA_ERROR_TYPE_CE:
		msg = SMU_MSG_McaBankCeDumpDW;
		break;
	default:
		return -EINVAL;
	}

	param = ((idx & 0xffff) << 16) | (offset & 0xfffc);

	return smu_cmn_send_smc_msg_with_param(smu, msg, param, val);
}

static int smu_v13_0_6_mca_dump_bank(struct smu_context *smu, enum amdgpu_mca_error_type type,
				     int idx, int offset, uint32_t *val, int count)
{
	int ret, i;

	if (!val)
		return -EINVAL;

	for (i = 0; i < count; i++) {
		ret = __smu_v13_0_6_mca_dump_bank(smu, type, idx, offset + (i << 2), &val[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct mca_bank_ipid smu_v13_0_6_mca_ipid_table[AMDGPU_MCA_IP_COUNT] = {
	MCA_BANK_IPID(UMC, 0x96, 0x0),
	MCA_BANK_IPID(SMU, 0x01, 0x1),
	MCA_BANK_IPID(MP5, 0x01, 0x2),
	MCA_BANK_IPID(PCS_XGMI, 0x50, 0x0),
};

static void mca_bank_entry_info_decode(struct mca_bank_entry *entry, struct mca_bank_info *info)
{
	u64 ipid = entry->regs[MCA_REG_IDX_IPID];
	u32 instidhi, instid;

	/* NOTE: All MCA IPID register share the same format,
	 * so the driver can share the MCMP1 register header file.
	 * */

	info->hwid = REG_GET_FIELD(ipid, MCMP1_IPIDT0, HardwareID);
	info->mcatype = REG_GET_FIELD(ipid, MCMP1_IPIDT0, McaType);

	/*
	 * Unfied DieID Format: SAASS. A:AID, S:Socket.
	 * Unfied DieID[4] = InstanceId[0]
	 * Unfied DieID[0:3] = InstanceIdHi[0:3]
	 */
	instidhi = REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdHi);
	instid = REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdLo);
	info->aid = ((instidhi >> 2) & 0x03);
	info->socket_id = ((instid & 0x1) << 2) | (instidhi & 0x03);
}

static int mca_bank_read_reg(struct amdgpu_device *adev, enum amdgpu_mca_error_type type,
			     int idx, int reg_idx, uint64_t *val)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	uint32_t data[2] = {0, 0};
	int ret;

	if (!val || reg_idx >= MCA_REG_IDX_COUNT)
		return -EINVAL;

	ret = smu_v13_0_6_mca_dump_bank(smu, type, idx, reg_idx * 8, data, ARRAY_SIZE(data));
	if (ret)
		return ret;

	*val = (uint64_t)data[1] << 32 | data[0];

	dev_dbg(adev->dev, "mca read bank reg: type:%s, index: %d, reg_idx: %d, val: 0x%016llx\n",
		type == AMDGPU_MCA_ERROR_TYPE_UE ? "UE" : "CE", idx, reg_idx, *val);

	return 0;
}

static int mca_get_mca_entry(struct amdgpu_device *adev, enum amdgpu_mca_error_type type,
			     int idx, struct mca_bank_entry *entry)
{
	int i, ret;

	/* NOTE: populated all mca register by default */
	for (i = 0; i < ARRAY_SIZE(entry->regs); i++) {
		ret = mca_bank_read_reg(adev, type, idx, i, &entry->regs[i]);
		if (ret)
			return ret;
	}

	entry->idx = idx;
	entry->type = type;

	mca_bank_entry_info_decode(entry, &entry->info);

	return 0;
}

static int mca_decode_ipid_to_hwip(uint64_t val)
{
	const struct mca_bank_ipid *ipid;
	uint16_t hwid, mcatype;
	int i;

	hwid = REG_GET_FIELD(val, MCMP1_IPIDT0, HardwareID);
	mcatype = REG_GET_FIELD(val, MCMP1_IPIDT0, McaType);

	for (i = 0; i < ARRAY_SIZE(smu_v13_0_6_mca_ipid_table); i++) {
		ipid = &smu_v13_0_6_mca_ipid_table[i];

		if (!ipid->hwid)
			continue;

		if (ipid->hwid == hwid && ipid->mcatype == mcatype)
			return i;
	}

	return AMDGPU_MCA_IP_UNKNOW;
}

static int mca_umc_mca_get_err_count(const struct mca_ras_info *mca_ras, struct amdgpu_device *adev,
				     enum amdgpu_mca_error_type type, struct mca_bank_entry *entry, uint32_t *count)
{
	uint64_t status0;
	uint32_t ext_error_code;
	uint32_t odecc_err_cnt;

	status0 = entry->regs[MCA_REG_IDX_STATUS];
	ext_error_code = MCA_REG__STATUS__ERRORCODEEXT(status0);
	odecc_err_cnt = MCA_REG__MISC0__ERRCNT(entry->regs[MCA_REG_IDX_MISC0]);

	if (!REG_GET_FIELD(status0, MCMP1_STATUST0, Val)) {
		*count = 0;
		return 0;
	}

	if (umc_v12_0_is_deferred_error(adev, status0) ||
	    umc_v12_0_is_uncorrectable_error(adev, status0) ||
	    umc_v12_0_is_correctable_error(adev, status0))
		*count = (ext_error_code == 0) ? odecc_err_cnt : 1;

	amdgpu_umc_update_ecc_status(adev,
			entry->regs[MCA_REG_IDX_STATUS],
			entry->regs[MCA_REG_IDX_IPID],
			entry->regs[MCA_REG_IDX_ADDR]);

	return 0;
}

static int mca_pcs_xgmi_mca_get_err_count(const struct mca_ras_info *mca_ras, struct amdgpu_device *adev,
					  enum amdgpu_mca_error_type type, struct mca_bank_entry *entry,
					  uint32_t *count)
{
	u32 ext_error_code;
	u32 err_cnt;

	ext_error_code = MCA_REG__STATUS__ERRORCODEEXT(entry->regs[MCA_REG_IDX_STATUS]);
	err_cnt = MCA_REG__MISC0__ERRCNT(entry->regs[MCA_REG_IDX_MISC0]);

	if (type == AMDGPU_MCA_ERROR_TYPE_UE &&
	    (ext_error_code == 0 || ext_error_code == 9))
		*count = err_cnt;
	else if (type == AMDGPU_MCA_ERROR_TYPE_CE && ext_error_code == 6)
		*count = err_cnt;

	return 0;
}

static bool mca_smu_check_error_code(struct amdgpu_device *adev, const struct mca_ras_info *mca_ras,
				     uint32_t errcode)
{
	int i;

	if (!mca_ras->err_code_count || !mca_ras->err_code_array)
		return true;

	for (i = 0; i < mca_ras->err_code_count; i++) {
		if (errcode == mca_ras->err_code_array[i])
			return true;
	}

	return false;
}

static int mca_gfx_mca_get_err_count(const struct mca_ras_info *mca_ras, struct amdgpu_device *adev,
				     enum amdgpu_mca_error_type type, struct mca_bank_entry *entry, uint32_t *count)
{
	uint64_t status0, misc0;

	status0 = entry->regs[MCA_REG_IDX_STATUS];
	if (!REG_GET_FIELD(status0, MCMP1_STATUST0, Val)) {
		*count = 0;
		return 0;
	}

	if (type == AMDGPU_MCA_ERROR_TYPE_UE &&
	    REG_GET_FIELD(status0, MCMP1_STATUST0, UC) == 1 &&
	    REG_GET_FIELD(status0, MCMP1_STATUST0, PCC) == 1) {
		*count = 1;
		return 0;
	} else {
		misc0 = entry->regs[MCA_REG_IDX_MISC0];
		*count = REG_GET_FIELD(misc0, MCMP1_MISC0T0, ErrCnt);
	}

	return 0;
}

static int mca_smu_mca_get_err_count(const struct mca_ras_info *mca_ras, struct amdgpu_device *adev,
				     enum amdgpu_mca_error_type type, struct mca_bank_entry *entry, uint32_t *count)
{
	uint64_t status0, misc0;

	status0 = entry->regs[MCA_REG_IDX_STATUS];
	if (!REG_GET_FIELD(status0, MCMP1_STATUST0, Val)) {
		*count = 0;
		return 0;
	}

	if (type == AMDGPU_MCA_ERROR_TYPE_UE &&
	    REG_GET_FIELD(status0, MCMP1_STATUST0, UC) == 1 &&
	    REG_GET_FIELD(status0, MCMP1_STATUST0, PCC) == 1) {
		if (count)
			*count = 1;
		return 0;
	}

	misc0 = entry->regs[MCA_REG_IDX_MISC0];
	*count = REG_GET_FIELD(misc0, MCMP1_MISC0T0, ErrCnt);

	return 0;
}

static bool mca_gfx_smu_bank_is_valid(const struct mca_ras_info *mca_ras, struct amdgpu_device *adev,
				      enum amdgpu_mca_error_type type, struct mca_bank_entry *entry)
{
	uint32_t instlo;

	instlo = REG_GET_FIELD(entry->regs[MCA_REG_IDX_IPID], MCMP1_IPIDT0, InstanceIdLo);
	instlo &= GENMASK(31, 1);
	switch (instlo) {
	case 0x36430400: /* SMNAID XCD 0 */
	case 0x38430400: /* SMNAID XCD 1 */
	case 0x40430400: /* SMNXCD XCD 0, NOTE: FIXME: fix this error later */
		return true;
	default:
		return false;
	}

	return false;
};

static bool mca_smu_bank_is_valid(const struct mca_ras_info *mca_ras, struct amdgpu_device *adev,
				  enum amdgpu_mca_error_type type, struct mca_bank_entry *entry)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	uint32_t errcode, instlo;

	instlo = REG_GET_FIELD(entry->regs[MCA_REG_IDX_IPID], MCMP1_IPIDT0, InstanceIdLo);
	instlo &= GENMASK(31, 1);
	if (instlo != 0x03b30400)
		return false;

	if (smu_v13_0_6_cap_supported(smu, SMU_CAP(ACA_SYND))) {
		errcode = MCA_REG__SYND__ERRORINFORMATION(entry->regs[MCA_REG_IDX_SYND]);
		errcode &= 0xff;
	} else {
		errcode = REG_GET_FIELD(entry->regs[MCA_REG_IDX_STATUS], MCMP1_STATUST0, ErrorCode);
	}

	return mca_smu_check_error_code(adev, mca_ras, errcode);
}

static int sdma_err_codes[] = { CODE_SDMA0, CODE_SDMA1, CODE_SDMA2, CODE_SDMA3 };
static int mmhub_err_codes[] = {
	CODE_DAGB0, CODE_DAGB0 + 1, CODE_DAGB0 + 2, CODE_DAGB0 + 3, CODE_DAGB0 + 4, /* DAGB0-4 */
	CODE_EA0, CODE_EA0 + 1, CODE_EA0 + 2, CODE_EA0 + 3, CODE_EA0 + 4,	/* MMEA0-4*/
	CODE_VML2, CODE_VML2_WALKER, CODE_MMCANE,
};

static int vcn_err_codes[] = {
	CODE_VIDD, CODE_VIDV,
};
static int jpeg_err_codes[] = {
	CODE_JPEG0S, CODE_JPEG0D, CODE_JPEG1S, CODE_JPEG1D,
	CODE_JPEG2S, CODE_JPEG2D, CODE_JPEG3S, CODE_JPEG3D,
	CODE_JPEG4S, CODE_JPEG4D, CODE_JPEG5S, CODE_JPEG5D,
	CODE_JPEG6S, CODE_JPEG6D, CODE_JPEG7S, CODE_JPEG7D,
};

static const struct mca_ras_info mca_ras_table[] = {
	{
		.blkid = AMDGPU_RAS_BLOCK__UMC,
		.ip = AMDGPU_MCA_IP_UMC,
		.get_err_count = mca_umc_mca_get_err_count,
	}, {
		.blkid = AMDGPU_RAS_BLOCK__GFX,
		.ip = AMDGPU_MCA_IP_SMU,
		.get_err_count = mca_gfx_mca_get_err_count,
		.bank_is_valid = mca_gfx_smu_bank_is_valid,
	}, {
		.blkid = AMDGPU_RAS_BLOCK__SDMA,
		.ip = AMDGPU_MCA_IP_SMU,
		.err_code_array = sdma_err_codes,
		.err_code_count = ARRAY_SIZE(sdma_err_codes),
		.get_err_count = mca_smu_mca_get_err_count,
		.bank_is_valid = mca_smu_bank_is_valid,
	}, {
		.blkid = AMDGPU_RAS_BLOCK__MMHUB,
		.ip = AMDGPU_MCA_IP_SMU,
		.err_code_array = mmhub_err_codes,
		.err_code_count = ARRAY_SIZE(mmhub_err_codes),
		.get_err_count = mca_smu_mca_get_err_count,
		.bank_is_valid = mca_smu_bank_is_valid,
	}, {
		.blkid = AMDGPU_RAS_BLOCK__XGMI_WAFL,
		.ip = AMDGPU_MCA_IP_PCS_XGMI,
		.get_err_count = mca_pcs_xgmi_mca_get_err_count,
	}, {
		.blkid = AMDGPU_RAS_BLOCK__VCN,
		.ip = AMDGPU_MCA_IP_SMU,
		.err_code_array = vcn_err_codes,
		.err_code_count = ARRAY_SIZE(vcn_err_codes),
		.get_err_count = mca_smu_mca_get_err_count,
		.bank_is_valid = mca_smu_bank_is_valid,
	}, {
		.blkid = AMDGPU_RAS_BLOCK__JPEG,
		.ip = AMDGPU_MCA_IP_SMU,
		.err_code_array = jpeg_err_codes,
		.err_code_count = ARRAY_SIZE(jpeg_err_codes),
		.get_err_count = mca_smu_mca_get_err_count,
		.bank_is_valid = mca_smu_bank_is_valid,
	},
};

static const struct mca_ras_info *mca_get_mca_ras_info(struct amdgpu_device *adev, enum amdgpu_ras_block blkid)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mca_ras_table); i++) {
		if (mca_ras_table[i].blkid == blkid)
			return &mca_ras_table[i];
	}

	return NULL;
}

static int mca_get_valid_mca_count(struct amdgpu_device *adev, enum amdgpu_mca_error_type type, uint32_t *count)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret;

	switch (type) {
	case AMDGPU_MCA_ERROR_TYPE_UE:
	case AMDGPU_MCA_ERROR_TYPE_CE:
		ret = smu_v13_0_6_get_valid_mca_count(smu, type, count);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static bool mca_bank_is_valid(struct amdgpu_device *adev, const struct mca_ras_info *mca_ras,
			      enum amdgpu_mca_error_type type, struct mca_bank_entry *entry)
{
	if (mca_decode_ipid_to_hwip(entry->regs[MCA_REG_IDX_IPID]) != mca_ras->ip)
		return false;

	if (mca_ras->bank_is_valid)
		return mca_ras->bank_is_valid(mca_ras, adev, type, entry);

	return true;
}

static int mca_smu_parse_mca_error_count(struct amdgpu_device *adev, enum amdgpu_ras_block blk, enum amdgpu_mca_error_type type,
					 struct mca_bank_entry *entry, uint32_t *count)
{
	const struct mca_ras_info *mca_ras;

	if (!entry || !count)
		return -EINVAL;

	mca_ras = mca_get_mca_ras_info(adev, blk);
	if (!mca_ras)
		return -EOPNOTSUPP;

	if (!mca_bank_is_valid(adev, mca_ras, type, entry)) {
		*count = 0;
		return 0;
	}

	return mca_ras->get_err_count(mca_ras, adev, type, entry, count);
}

static int mca_smu_get_mca_entry(struct amdgpu_device *adev,
				 enum amdgpu_mca_error_type type, int idx, struct mca_bank_entry *entry)
{
	return mca_get_mca_entry(adev, type, idx, entry);
}

static int mca_smu_get_valid_mca_count(struct amdgpu_device *adev,
				       enum amdgpu_mca_error_type type, uint32_t *count)
{
	return mca_get_valid_mca_count(adev, type, count);
}

static const struct amdgpu_mca_smu_funcs smu_v13_0_6_mca_smu_funcs = {
	.max_ue_count = 12,
	.max_ce_count = 12,
	.mca_set_debug_mode = mca_smu_set_debug_mode,
	.mca_parse_mca_error_count = mca_smu_parse_mca_error_count,
	.mca_get_mca_entry = mca_smu_get_mca_entry,
	.mca_get_valid_mca_count = mca_smu_get_valid_mca_count,
};

static int aca_smu_set_debug_mode(struct amdgpu_device *adev, bool enable)
{
	struct smu_context *smu = adev->powerplay.pp_handle;

	return smu_v13_0_6_mca_set_debug_mode(smu, enable);
}

static int smu_v13_0_6_get_valid_aca_count(struct smu_context *smu, enum aca_smu_type type, u32 *count)
{
	uint32_t msg;
	int ret;

	if (!count)
		return -EINVAL;

	switch (type) {
	case ACA_SMU_TYPE_UE:
		msg = SMU_MSG_QueryValidMcaCount;
		break;
	case ACA_SMU_TYPE_CE:
		msg = SMU_MSG_QueryValidMcaCeCount;
		break;
	default:
		return -EINVAL;
	}

	ret = smu_cmn_send_smc_msg(smu, msg, count);
	if (ret) {
		*count = 0;
		return ret;
	}

	return 0;
}

static int aca_smu_get_valid_aca_count(struct amdgpu_device *adev,
				       enum aca_smu_type type, u32 *count)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int ret;

	switch (type) {
	case ACA_SMU_TYPE_UE:
	case ACA_SMU_TYPE_CE:
		ret = smu_v13_0_6_get_valid_aca_count(smu, type, count);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int __smu_v13_0_6_aca_bank_dump(struct smu_context *smu, enum aca_smu_type type,
				       int idx, int offset, u32 *val)
{
	uint32_t msg, param;

	switch (type) {
	case ACA_SMU_TYPE_UE:
		msg = SMU_MSG_McaBankDumpDW;
		break;
	case ACA_SMU_TYPE_CE:
		msg = SMU_MSG_McaBankCeDumpDW;
		break;
	default:
		return -EINVAL;
	}

	param = ((idx & 0xffff) << 16) | (offset & 0xfffc);

	return smu_cmn_send_smc_msg_with_param(smu, msg, param, (uint32_t *)val);
}

static int smu_v13_0_6_aca_bank_dump(struct smu_context *smu, enum aca_smu_type type,
				     int idx, int offset, u32 *val, int count)
{
	int ret, i;

	if (!val)
		return -EINVAL;

	for (i = 0; i < count; i++) {
		ret = __smu_v13_0_6_aca_bank_dump(smu, type, idx, offset + (i << 2), &val[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int aca_bank_read_reg(struct amdgpu_device *adev, enum aca_smu_type type,
			     int idx, int reg_idx, u64 *val)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	u32 data[2] = {0, 0};
	int ret;

	if (!val || reg_idx >= ACA_REG_IDX_COUNT)
		return -EINVAL;

	ret = smu_v13_0_6_aca_bank_dump(smu, type, idx, reg_idx * 8, data, ARRAY_SIZE(data));
	if (ret)
		return ret;

	*val = (u64)data[1] << 32 | data[0];

	dev_dbg(adev->dev, "mca read bank reg: type:%s, index: %d, reg_idx: %d, val: 0x%016llx\n",
		type == ACA_SMU_TYPE_UE ? "UE" : "CE", idx, reg_idx, *val);

	return 0;
}

static int aca_smu_get_valid_aca_bank(struct amdgpu_device *adev,
				      enum aca_smu_type type, int idx, struct aca_bank *bank)
{
	int i, ret, count;

	count = min_t(int, 16, ARRAY_SIZE(bank->regs));
	for (i = 0; i < count; i++) {
		ret = aca_bank_read_reg(adev, type, idx, i, &bank->regs[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int aca_smu_parse_error_code(struct amdgpu_device *adev, struct aca_bank *bank)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	int error_code;

	if (smu_v13_0_6_cap_supported(smu, SMU_CAP(ACA_SYND)))
		error_code = ACA_REG__SYND__ERRORINFORMATION(bank->regs[ACA_REG_IDX_SYND]);
	else
		error_code = ACA_REG__STATUS__ERRORCODE(bank->regs[ACA_REG_IDX_STATUS]);

	return error_code & 0xff;
}

static const struct aca_smu_funcs smu_v13_0_6_aca_smu_funcs = {
	.max_ue_bank_count = 12,
	.max_ce_bank_count = 12,
	.set_debug_mode = aca_smu_set_debug_mode,
	.get_valid_aca_count = aca_smu_get_valid_aca_count,
	.get_valid_aca_bank = aca_smu_get_valid_aca_bank,
	.parse_error_code = aca_smu_parse_error_code,
};

static const struct pptable_funcs smu_v13_0_6_ppt_funcs = {
	/* init dpm */
	.get_allowed_feature_mask = smu_v13_0_6_get_allowed_feature_mask,
	/* dpm/clk tables */
	.set_default_dpm_table = smu_v13_0_6_set_default_dpm_table,
	.populate_umd_state_clk = smu_v13_0_6_populate_umd_state_clk,
	.print_clk_levels = smu_v13_0_6_print_clk_levels,
	.force_clk_levels = smu_v13_0_6_force_clk_levels,
	.read_sensor = smu_v13_0_6_read_sensor,
	.set_performance_level = smu_v13_0_6_set_performance_level,
	.get_power_limit = smu_v13_0_6_get_power_limit,
	.is_dpm_running = smu_v13_0_6_is_dpm_running,
	.get_unique_id = smu_v13_0_6_get_unique_id,
	.init_microcode = smu_v13_0_6_init_microcode,
	.fini_microcode = smu_v13_0_fini_microcode,
	.init_smc_tables = smu_v13_0_6_init_smc_tables,
	.fini_smc_tables = smu_v13_0_fini_smc_tables,
	.init_power = smu_v13_0_init_power,
	.fini_power = smu_v13_0_fini_power,
	.check_fw_status = smu_v13_0_6_check_fw_status,
	/* pptable related */
	.check_fw_version = smu_v13_0_6_check_fw_version,
	.set_driver_table_location = smu_v13_0_set_driver_table_location,
	.set_tool_table_location = smu_v13_0_set_tool_table_location,
	.notify_memory_pool_location = smu_v13_0_notify_memory_pool_location,
	.system_features_control = smu_v13_0_6_system_features_control,
	.send_smc_msg_with_param = smu_cmn_send_smc_msg_with_param,
	.send_smc_msg = smu_cmn_send_smc_msg,
	.get_enabled_mask = smu_v13_0_6_get_enabled_mask,
	.feature_is_enabled = smu_cmn_feature_is_enabled,
	.set_power_limit = smu_v13_0_6_set_power_limit,
	.set_xgmi_pstate = smu_v13_0_set_xgmi_pstate,
	.register_irq_handler = smu_v13_0_6_register_irq_handler,
	.enable_thermal_alert = smu_v13_0_enable_thermal_alert,
	.disable_thermal_alert = smu_v13_0_disable_thermal_alert,
	.setup_pptable = smu_v13_0_6_setup_pptable,
	.get_bamaco_support = smu_v13_0_6_get_bamaco_support,
	.get_dpm_ultimate_freq = smu_v13_0_6_get_dpm_ultimate_freq,
	.set_soft_freq_limited_range = smu_v13_0_6_set_soft_freq_limited_range,
	.od_edit_dpm_table = smu_v13_0_6_usr_edit_dpm_table,
	.log_thermal_throttling_event = smu_v13_0_6_log_thermal_throttling_event,
	.get_pp_feature_mask = smu_cmn_get_pp_feature_mask,
	.get_gpu_metrics = smu_v13_0_6_get_gpu_metrics,
	.get_pm_metrics = smu_v13_0_6_get_pm_metrics,
	.get_thermal_temperature_range = smu_v13_0_6_get_thermal_temperature_range,
	.mode1_reset_is_support = smu_v13_0_6_is_mode1_reset_supported,
	.mode2_reset_is_support = smu_v13_0_6_is_mode2_reset_supported,
	.mode1_reset = smu_v13_0_6_mode1_reset,
	.mode2_reset = smu_v13_0_6_mode2_reset,
	.wait_for_event = smu_v13_0_wait_for_event,
	.i2c_init = smu_v13_0_6_i2c_control_init,
	.i2c_fini = smu_v13_0_6_i2c_control_fini,
	.send_hbm_bad_pages_num = smu_v13_0_6_smu_send_hbm_bad_page_num,
	.send_rma_reason = smu_v13_0_6_send_rma_reason,
	.reset_sdma = smu_v13_0_6_reset_sdma,
};

void smu_v13_0_6_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &smu_v13_0_6_ppt_funcs;
	smu->message_map = smu_v13_0_6_message_map;
	smu->clock_map = smu_v13_0_6_clk_map;
	smu->feature_map = smu_v13_0_6_feature_mask_map;
	smu->table_map = smu_v13_0_6_table_map;
	smu->smc_driver_if_version = SMU13_0_6_DRIVER_IF_VERSION;
	smu->smc_fw_caps |= SMU_FW_CAP_RAS_PRI;
	smu_v13_0_set_smu_mailbox_registers(smu);
	amdgpu_mca_smu_init_funcs(smu->adev, &smu_v13_0_6_mca_smu_funcs);
	amdgpu_aca_set_smu_funcs(smu->adev, &smu_v13_0_6_aca_smu_funcs);
}
