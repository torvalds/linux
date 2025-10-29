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
	uint32_t PPT1Max;
	uint32_t PPT1Min;
	uint32_t PPT1Default;
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
	SMU_CAP(RAS_EEPROM),
	SMU_CAP(FAST_PPT),
	SMU_CAP(SYSTEM_POWER_METRICS),
	SMU_CAP(ALL),
};

#define SMU_13_0_6_NUM_XGMI_LINKS 8
#define SMU_13_0_6_MAX_GFX_CLKS 8
#define SMU_13_0_6_MAX_CLKS 4
#define SMU_13_0_6_MAX_XCC 8
#define SMU_13_0_6_MAX_VCN 4
#define SMU_13_0_6_MAX_JPEG 40

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
ssize_t smu_v13_0_12_get_xcp_metrics(struct smu_context *smu,
				     struct amdgpu_xcp *xcp, void *table,
				     void *smu_metrics);
int smu_v13_0_12_tables_init(struct smu_context *smu);
void smu_v13_0_12_tables_fini(struct smu_context *smu);
int smu_v13_0_12_get_npm_data(struct smu_context *smu,
			      enum amd_pp_sensors sensor,
			      uint32_t *value);
int smu_v13_0_12_get_system_power(struct smu_context *smu,
				  enum amd_pp_sensors sensor,
				  uint32_t *value);
extern const struct cmn2asic_mapping smu_v13_0_12_feature_mask_map[];
extern const struct cmn2asic_msg_mapping smu_v13_0_12_message_map[];
extern const struct smu_temp_funcs smu_v13_0_12_temp_funcs;
extern const struct ras_smu_drv smu_v13_0_12_ras_smu_drv;

#if defined(SWSMU_CODE_LAYER_L2)
#include "smu_cmn.h"

/* SMUv 13.0.6 GPU metrics*/
#define SMU_13_0_6_METRICS_FIELDS(SMU_SCALAR, SMU_ARRAY)                       \
	SMU_SCALAR(SMU_MATTR(TEMPERATURE_HOTSPOT), SMU_MUNIT(TEMP_1),          \
		   SMU_MTYPE(U16), temperature_hotspot);                       \
	SMU_SCALAR(SMU_MATTR(TEMPERATURE_MEM), SMU_MUNIT(TEMP_1),              \
		   SMU_MTYPE(U16), temperature_mem);                           \
	SMU_SCALAR(SMU_MATTR(TEMPERATURE_VRSOC), SMU_MUNIT(TEMP_1),            \
		   SMU_MTYPE(U16), temperature_vrsoc);                         \
	SMU_SCALAR(SMU_MATTR(CURR_SOCKET_POWER), SMU_MUNIT(POWER_1),           \
		   SMU_MTYPE(U16), curr_socket_power);                         \
	SMU_SCALAR(SMU_MATTR(AVERAGE_GFX_ACTIVITY), SMU_MUNIT(PERCENT),        \
		   SMU_MTYPE(U16), average_gfx_activity);                      \
	SMU_SCALAR(SMU_MATTR(AVERAGE_UMC_ACTIVITY), SMU_MUNIT(PERCENT),        \
		   SMU_MTYPE(U16), average_umc_activity);                      \
	SMU_SCALAR(SMU_MATTR(MEM_MAX_BANDWIDTH), SMU_MUNIT(BW_1),              \
		   SMU_MTYPE(U64), mem_max_bandwidth);                         \
	SMU_SCALAR(SMU_MATTR(ENERGY_ACCUMULATOR), SMU_MUNIT(NONE),             \
		   SMU_MTYPE(U64), energy_accumulator);                        \
	SMU_SCALAR(SMU_MATTR(SYSTEM_CLOCK_COUNTER), SMU_MUNIT(TIME_1),         \
		   SMU_MTYPE(U64), system_clock_counter);                      \
	SMU_SCALAR(SMU_MATTR(ACCUMULATION_COUNTER), SMU_MUNIT(NONE),           \
		   SMU_MTYPE(U32), accumulation_counter);                      \
	SMU_SCALAR(SMU_MATTR(PROCHOT_RESIDENCY_ACC), SMU_MUNIT(NONE),          \
		   SMU_MTYPE(U32), prochot_residency_acc);                     \
	SMU_SCALAR(SMU_MATTR(PPT_RESIDENCY_ACC), SMU_MUNIT(NONE),              \
		   SMU_MTYPE(U32), ppt_residency_acc);                         \
	SMU_SCALAR(SMU_MATTR(SOCKET_THM_RESIDENCY_ACC), SMU_MUNIT(NONE),       \
		   SMU_MTYPE(U32), socket_thm_residency_acc);                  \
	SMU_SCALAR(SMU_MATTR(VR_THM_RESIDENCY_ACC), SMU_MUNIT(NONE),           \
		   SMU_MTYPE(U32), vr_thm_residency_acc);                      \
	SMU_SCALAR(SMU_MATTR(HBM_THM_RESIDENCY_ACC), SMU_MUNIT(NONE),          \
		   SMU_MTYPE(U32), hbm_thm_residency_acc);                     \
	SMU_SCALAR(SMU_MATTR(GFXCLK_LOCK_STATUS), SMU_MUNIT(NONE),             \
		   SMU_MTYPE(U32), gfxclk_lock_status);                        \
	SMU_SCALAR(SMU_MATTR(PCIE_LINK_WIDTH), SMU_MUNIT(NONE),                \
		   SMU_MTYPE(U16), pcie_link_width);                           \
	SMU_SCALAR(SMU_MATTR(PCIE_LINK_SPEED), SMU_MUNIT(SPEED_2),             \
		   SMU_MTYPE(U16), pcie_link_speed);                           \
	SMU_SCALAR(SMU_MATTR(XGMI_LINK_WIDTH), SMU_MUNIT(NONE),                \
		   SMU_MTYPE(U16), xgmi_link_width);                           \
	SMU_SCALAR(SMU_MATTR(XGMI_LINK_SPEED), SMU_MUNIT(SPEED_1),             \
		   SMU_MTYPE(U16), xgmi_link_speed);                           \
	SMU_SCALAR(SMU_MATTR(GFX_ACTIVITY_ACC), SMU_MUNIT(PERCENT),            \
		   SMU_MTYPE(U32), gfx_activity_acc);                          \
	SMU_SCALAR(SMU_MATTR(MEM_ACTIVITY_ACC), SMU_MUNIT(PERCENT),            \
		   SMU_MTYPE(U32), mem_activity_acc);                          \
	SMU_SCALAR(SMU_MATTR(PCIE_BANDWIDTH_ACC), SMU_MUNIT(PERCENT),          \
		   SMU_MTYPE(U64), pcie_bandwidth_acc);                        \
	SMU_SCALAR(SMU_MATTR(PCIE_BANDWIDTH_INST), SMU_MUNIT(BW_1),            \
		   SMU_MTYPE(U64), pcie_bandwidth_inst);                       \
	SMU_SCALAR(SMU_MATTR(PCIE_L0_TO_RECOV_COUNT_ACC), SMU_MUNIT(NONE),     \
		   SMU_MTYPE(U64), pcie_l0_to_recov_count_acc);                \
	SMU_SCALAR(SMU_MATTR(PCIE_REPLAY_COUNT_ACC), SMU_MUNIT(NONE),          \
		   SMU_MTYPE(U64), pcie_replay_count_acc);                     \
	SMU_SCALAR(SMU_MATTR(PCIE_REPLAY_ROVER_COUNT_ACC), SMU_MUNIT(NONE),    \
		   SMU_MTYPE(U64), pcie_replay_rover_count_acc);               \
	SMU_SCALAR(SMU_MATTR(PCIE_NAK_SENT_COUNT_ACC), SMU_MUNIT(NONE),        \
		   SMU_MTYPE(U32), pcie_nak_sent_count_acc);                   \
	SMU_SCALAR(SMU_MATTR(PCIE_NAK_RCVD_COUNT_ACC), SMU_MUNIT(NONE),        \
		   SMU_MTYPE(U32), pcie_nak_rcvd_count_acc);                   \
	SMU_ARRAY(SMU_MATTR(XGMI_READ_DATA_ACC), SMU_MUNIT(DATA_1),            \
		  SMU_MTYPE(U64), xgmi_read_data_acc,                          \
		  SMU_13_0_6_NUM_XGMI_LINKS);                                  \
	SMU_ARRAY(SMU_MATTR(XGMI_WRITE_DATA_ACC), SMU_MUNIT(DATA_1),           \
		  SMU_MTYPE(U64), xgmi_write_data_acc,                         \
		  SMU_13_0_6_NUM_XGMI_LINKS);                                  \
	SMU_ARRAY(SMU_MATTR(XGMI_LINK_STATUS), SMU_MUNIT(NONE),                \
		  SMU_MTYPE(U16), xgmi_link_status,                            \
		  SMU_13_0_6_NUM_XGMI_LINKS);                                  \
	SMU_SCALAR(SMU_MATTR(FIRMWARE_TIMESTAMP), SMU_MUNIT(TIME_2),           \
		   SMU_MTYPE(U64), firmware_timestamp);                        \
	SMU_ARRAY(SMU_MATTR(CURRENT_GFXCLK), SMU_MUNIT(CLOCK_1),               \
		  SMU_MTYPE(U16), current_gfxclk, SMU_13_0_6_MAX_GFX_CLKS);    \
	SMU_ARRAY(SMU_MATTR(CURRENT_SOCCLK), SMU_MUNIT(CLOCK_1),               \
		  SMU_MTYPE(U16), current_socclk, SMU_13_0_6_MAX_CLKS);        \
	SMU_ARRAY(SMU_MATTR(CURRENT_VCLK0), SMU_MUNIT(CLOCK_1),                \
		  SMU_MTYPE(U16), current_vclk0, SMU_13_0_6_MAX_CLKS);         \
	SMU_ARRAY(SMU_MATTR(CURRENT_DCLK0), SMU_MUNIT(CLOCK_1),                \
		  SMU_MTYPE(U16), current_dclk0, SMU_13_0_6_MAX_CLKS);         \
	SMU_SCALAR(SMU_MATTR(CURRENT_UCLK), SMU_MUNIT(CLOCK_1),                \
		   SMU_MTYPE(U16), current_uclk);                              \
	SMU_SCALAR(SMU_MATTR(PCIE_LC_PERF_OTHER_END_RECOVERY),                 \
		   SMU_MUNIT(NONE), SMU_MTYPE(U32),                            \
		   pcie_lc_perf_other_end_recovery);                           \
	SMU_ARRAY(SMU_MATTR(GFX_BUSY_INST), SMU_MUNIT(PERCENT),                \
		  SMU_MTYPE(U32), gfx_busy_inst, SMU_13_0_6_MAX_XCC);          \
	SMU_ARRAY(SMU_MATTR(JPEG_BUSY), SMU_MUNIT(PERCENT), SMU_MTYPE(U16),    \
		  jpeg_busy, SMU_13_0_6_MAX_JPEG);                             \
	SMU_ARRAY(SMU_MATTR(VCN_BUSY), SMU_MUNIT(PERCENT), SMU_MTYPE(U16),     \
		  vcn_busy, SMU_13_0_6_MAX_VCN);                               \
	SMU_ARRAY(SMU_MATTR(GFX_BUSY_ACC), SMU_MUNIT(PERCENT), SMU_MTYPE(U64), \
		  gfx_busy_acc, SMU_13_0_6_MAX_XCC);                           \
	SMU_ARRAY(SMU_MATTR(GFX_BELOW_HOST_LIMIT_PPT_ACC), SMU_MUNIT(NONE),    \
		  SMU_MTYPE(U64), gfx_below_host_limit_ppt_acc,                \
		  SMU_13_0_6_MAX_XCC);                                         \
	SMU_ARRAY(SMU_MATTR(GFX_BELOW_HOST_LIMIT_THM_ACC), SMU_MUNIT(NONE),    \
		  SMU_MTYPE(U64), gfx_below_host_limit_thm_acc,                \
		  SMU_13_0_6_MAX_XCC);                                         \
	SMU_ARRAY(SMU_MATTR(GFX_LOW_UTILIZATION_ACC), SMU_MUNIT(NONE),         \
		  SMU_MTYPE(U64), gfx_low_utilization_acc,                     \
		  SMU_13_0_6_MAX_XCC);                                         \
	SMU_ARRAY(SMU_MATTR(GFX_BELOW_HOST_LIMIT_TOTAL_ACC), SMU_MUNIT(NONE),  \
		  SMU_MTYPE(U64), gfx_below_host_limit_total_acc,              \
		  SMU_13_0_6_MAX_XCC);

DECLARE_SMU_METRICS_CLASS(smu_v13_0_6_gpu_metrics, SMU_13_0_6_METRICS_FIELDS);
void smu_v13_0_12_get_gpu_metrics(struct smu_context *smu, void **table,
				  void *smu_metrics,
				  struct smu_v13_0_6_gpu_metrics *gpu_metrics);

#define SMU_13_0_6_PARTITION_METRICS_FIELDS(SMU_SCALAR, SMU_ARRAY)             \
	SMU_ARRAY(SMU_MATTR(CURRENT_GFXCLK), SMU_MUNIT(CLOCK_1),               \
		  SMU_MTYPE(U16), current_gfxclk, SMU_13_0_6_MAX_XCC);         \
	SMU_ARRAY(SMU_MATTR(CURRENT_SOCCLK), SMU_MUNIT(CLOCK_1),               \
		  SMU_MTYPE(U16), current_socclk, SMU_13_0_6_MAX_CLKS);        \
	SMU_ARRAY(SMU_MATTR(CURRENT_VCLK0), SMU_MUNIT(CLOCK_1),                \
		  SMU_MTYPE(U16), current_vclk0, SMU_13_0_6_MAX_CLKS);         \
	SMU_ARRAY(SMU_MATTR(CURRENT_DCLK0), SMU_MUNIT(CLOCK_1),                \
		  SMU_MTYPE(U16), current_dclk0, SMU_13_0_6_MAX_CLKS);         \
	SMU_SCALAR(SMU_MATTR(CURRENT_UCLK), SMU_MUNIT(CLOCK_1),                \
		   SMU_MTYPE(U16), current_uclk);                              \
	SMU_ARRAY(SMU_MATTR(GFX_BUSY_INST), SMU_MUNIT(PERCENT),                \
		  SMU_MTYPE(U32), gfx_busy_inst, SMU_13_0_6_MAX_XCC);          \
	SMU_ARRAY(SMU_MATTR(JPEG_BUSY), SMU_MUNIT(PERCENT), SMU_MTYPE(U16),    \
		  jpeg_busy, SMU_13_0_6_MAX_JPEG);                             \
	SMU_ARRAY(SMU_MATTR(VCN_BUSY), SMU_MUNIT(PERCENT), SMU_MTYPE(U16),     \
		  vcn_busy, SMU_13_0_6_MAX_VCN);                               \
	SMU_ARRAY(SMU_MATTR(GFX_BUSY_ACC), SMU_MUNIT(PERCENT), SMU_MTYPE(U64), \
		  gfx_busy_acc, SMU_13_0_6_MAX_XCC);                           \
	SMU_ARRAY(SMU_MATTR(GFX_BELOW_HOST_LIMIT_PPT_ACC), SMU_MUNIT(NONE),    \
		  SMU_MTYPE(U64), gfx_below_host_limit_ppt_acc,                \
		  SMU_13_0_6_MAX_XCC);                                         \
	SMU_ARRAY(SMU_MATTR(GFX_BELOW_HOST_LIMIT_THM_ACC), SMU_MUNIT(NONE),    \
		  SMU_MTYPE(U64), gfx_below_host_limit_thm_acc,                \
		  SMU_13_0_6_MAX_XCC);                                         \
	SMU_ARRAY(SMU_MATTR(GFX_LOW_UTILIZATION_ACC), SMU_MUNIT(NONE),         \
		  SMU_MTYPE(U64), gfx_low_utilization_acc,                     \
		  SMU_13_0_6_MAX_XCC);                                         \
	SMU_ARRAY(SMU_MATTR(GFX_BELOW_HOST_LIMIT_TOTAL_ACC), SMU_MUNIT(NONE),  \
		  SMU_MTYPE(U64), gfx_below_host_limit_total_acc,              \
		  SMU_13_0_6_MAX_XCC);

DECLARE_SMU_METRICS_CLASS(smu_v13_0_6_partition_metrics,
			  SMU_13_0_6_PARTITION_METRICS_FIELDS);

#endif /* SWSMU_CODE_LAYER_L2 */

#endif
