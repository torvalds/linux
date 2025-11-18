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
#ifndef __SMU_13_0_6_PPT_H__
#define __SMU_13_0_6_PPT_H__

#define SMU_13_0_6_UMD_PSTATE_GFXCLK_LEVEL 0x2
#define SMU_13_0_6_UMD_PSTATE_SOCCLK_LEVEL 0x4
#define SMU_13_0_6_UMD_PSTATE_MCLK_LEVEL 0x2
#define SMU_CAP(x) SMU_13_0_6_CAPS_##x

typedef enum {
/*0*/   METRICS_VERSION_V0                  = 0,
/*1*/   METRICS_VERSION_V1                  = 1,
/*2*/   METRICS_VERSION_V2                  = 2,

/*3*/   NUM_METRICS                         = 3
} METRICS_LIST_e;

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
	uint32_t MaxNodePowerLimit;
	bool Init;
};

enum smu_v13_0_6_caps {
	SMU_CAP(DPM),
	SMU_CAP(DPM_POLICY),
	SMU_CAP(OTHER_END_METRICS),
	SMU_CAP(SET_UCLK_MAX),
	SMU_CAP(PCIE_METRICS),
	SMU_CAP(MCA_DEBUG_MODE),
	SMU_CAP(PER_INST_METRICS),
	SMU_CAP(CTF_LIMIT),
	SMU_CAP(RMA_MSG),
	SMU_CAP(ACA_SYND),
	SMU_CAP(SDMA_RESET),
	SMU_CAP(VCN_RESET),
	SMU_CAP(STATIC_METRICS),
	SMU_CAP(HST_LIMIT_METRICS),
	SMU_CAP(BOARD_VOLTAGE),
	SMU_CAP(PLDM_VERSION),
	SMU_CAP(TEMP_METRICS),
	SMU_CAP(NPM_METRICS),
	SMU_CAP(ALL),
};

extern void smu_v13_0_6_set_ppt_funcs(struct smu_context *smu);
bool smu_v13_0_6_cap_supported(struct smu_context *smu, enum smu_v13_0_6_caps cap);
int smu_v13_0_6_get_static_metrics_table(struct smu_context *smu);
int smu_v13_0_6_get_metrics_table(struct smu_context *smu, void *metrics_table,
				  bool bypass_cache);

bool smu_v13_0_12_is_dpm_running(struct smu_context *smu);
int smu_v13_0_12_get_max_metrics_size(void);
size_t smu_v13_0_12_get_system_metrics_size(void);
int smu_v13_0_12_setup_driver_pptable(struct smu_context *smu);
int smu_v13_0_12_get_smu_metrics_data(struct smu_context *smu,
				      MetricsMember_t member, uint32_t *value);
ssize_t smu_v13_0_12_get_gpu_metrics(struct smu_context *smu, void **table, void *smu_metrics);
ssize_t smu_v13_0_12_get_xcp_metrics(struct smu_context *smu,
				     struct amdgpu_xcp *xcp, void *table,
				     void *smu_metrics);
int smu_v13_0_12_tables_init(struct smu_context *smu);
void smu_v13_0_12_tables_fini(struct smu_context *smu);
int smu_v13_0_12_get_npm_data(struct smu_context *smu,
			      enum amd_pp_sensors sensor,
			      uint32_t *value);
extern const struct cmn2asic_mapping smu_v13_0_12_feature_mask_map[];
extern const struct cmn2asic_msg_mapping smu_v13_0_12_message_map[];
extern const struct smu_temp_funcs smu_v13_0_12_temp_funcs;
#endif
