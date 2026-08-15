/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#ifndef __SMU_15_0_8_PPT_H__
#define __SMU_15_0_8_PPT_H__

#define SMU_15_0_8_NUM_XGMI_LINKS 8
#define SMU_15_0_8_MAX_GFX_CLKS 8
#define SMU_15_0_8_MAX_CLKS 4
#define SMU_15_0_8_MAX_XCC 8
#define SMU_15_0_8_MAX_VCN 4
#define SMU_15_0_8_MAX_JPEG 40
#define SMU_15_0_8_MAX_AID 2
#define SMU_15_0_8_MAX_MID 2
#define SMU_15_0_8_MAX_HBM_STACKS 12
extern void smu_v15_0_8_set_ppt_funcs(struct smu_context *smu);

typedef struct {
	uint32_t MaxSocketPowerLimit;
	uint32_t MaxGfxclkFrequency;
	uint32_t MinGfxclkFrequency;
	uint32_t MaxFclkFrequency;
	uint32_t MinFclkFrequency;
	uint32_t MaxGl2clkFrequency;
	uint32_t MinGl2clkFrequency;
	uint32_t UclkFrequencyTable[4];
	uint32_t SocclkFrequency;
	uint32_t LclkFrequency;
	uint32_t VclkFrequency;
	uint32_t DclkFrequency;
	uint32_t CTFLimitMID;
	uint32_t CTFLimitAID;
	uint32_t CTFLimitXCD;
	uint32_t CTFLimitHBM;
	uint32_t ThermalLimitMID;
	uint32_t ThermalLimitAID;
	uint32_t ThermalLimitXCD;
	uint32_t ThermalLimitHBM;
	uint64_t PublicSerialNumberMID;
	uint64_t PublicSerialNumberAID;
	uint64_t PublicSerialNumberXCD;
	uint32_t PPT1Max;
	uint32_t PPT1Min;
	uint32_t PPT1Default;
	bool init;
} PPTable_t;

#if defined(SWSMU_CODE_LAYER_L2)
#include "smu_cmn.h"

/* SMUv 15.0.8 GPU metrics*/
#define SMU_15_0_8_METRICS_FIELDS(SMU_SCALAR, SMU_ARRAY)                       \
	SMU_SCALAR(SMU_MATTR(TEMPERATURE_HOTSPOT), SMU_MUNIT(TEMP_1),          \
		   SMU_MTYPE(U16), temperature_hotspot);                       \
	SMU_SCALAR(SMU_MATTR(TEMPERATURE_MEM), SMU_MUNIT(TEMP_1),              \
		   SMU_MTYPE(U16), temperature_mem);                           \
	SMU_SCALAR(SMU_MATTR(TEMPERATURE_VRSOC), SMU_MUNIT(TEMP_1),            \
		   SMU_MTYPE(U16), temperature_vrsoc);                         \
	SMU_ARRAY(SMU_MATTR(TEMPERATURE_HBM), SMU_MUNIT(TEMP_1),               \
		  SMU_MTYPE(U16), temperature_hbm,                             \
		  SMU_15_0_8_MAX_HBM_STACKS);                                  \
	SMU_ARRAY(SMU_MATTR(TEMPERATURE_MID), SMU_MUNIT(TEMP_1),               \
		  SMU_MTYPE(U16), temperature_mid, SMU_15_0_8_MAX_MID);        \
	SMU_ARRAY(SMU_MATTR(TEMPERATURE_AID), SMU_MUNIT(TEMP_1),               \
		  SMU_MTYPE(U16), temperature_aid, SMU_15_0_8_MAX_AID);        \
	SMU_ARRAY(SMU_MATTR(TEMPERATURE_XCD), SMU_MUNIT(TEMP_1),               \
		  SMU_MTYPE(U16), temperature_xcd, SMU_15_0_8_MAX_XCC);        \
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
		   SMU_MTYPE(U64), accumulation_counter);                      \
	SMU_SCALAR(SMU_MATTR(PROCHOT_RESIDENCY_ACC), SMU_MUNIT(NONE),          \
		   SMU_MTYPE(U64), prochot_residency_acc);                     \
	SMU_SCALAR(SMU_MATTR(PPT_RESIDENCY_ACC), SMU_MUNIT(NONE),              \
		   SMU_MTYPE(U64), ppt_residency_acc);                         \
	SMU_SCALAR(SMU_MATTR(SOCKET_THM_RESIDENCY_ACC), SMU_MUNIT(NONE),       \
		   SMU_MTYPE(U64), socket_thm_residency_acc);                  \
	SMU_SCALAR(SMU_MATTR(VR_THM_RESIDENCY_ACC), SMU_MUNIT(NONE),           \
		   SMU_MTYPE(U64), vr_thm_residency_acc);                      \
	SMU_SCALAR(SMU_MATTR(HBM_THM_RESIDENCY_ACC), SMU_MUNIT(NONE),          \
		   SMU_MTYPE(U64), hbm_thm_residency_acc);                     \
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
	SMU_SCALAR(SMU_MATTR(GFX_ACTIVITY_ACC), SMU_MUNIT(NONE),               \
		   SMU_MTYPE(U64), gfx_activity_acc);                          \
	SMU_SCALAR(SMU_MATTR(MEM_ACTIVITY_ACC), SMU_MUNIT(NONE),               \
		   SMU_MTYPE(U64), mem_activity_acc);                          \
	SMU_ARRAY(SMU_MATTR(PCIE_BANDWIDTH_ACC), SMU_MUNIT(NONE),              \
		  SMU_MTYPE(U64), pcie_bandwidth_acc, SMU_15_0_8_MAX_MID);     \
	SMU_ARRAY(SMU_MATTR(PCIE_BANDWIDTH_INST), SMU_MUNIT(BW_1),             \
		  SMU_MTYPE(U32), pcie_bandwidth_inst, SMU_15_0_8_MAX_MID);    \
	SMU_SCALAR(SMU_MATTR(PCIE_L0_TO_RECOV_COUNT_ACC), SMU_MUNIT(NONE),     \
		   SMU_MTYPE(U64), pcie_l0_to_recov_count_acc);                \
	SMU_SCALAR(SMU_MATTR(PCIE_REPLAY_COUNT_ACC), SMU_MUNIT(NONE),          \
		   SMU_MTYPE(U64), pcie_replay_count_acc);                     \
	SMU_SCALAR(SMU_MATTR(PCIE_REPLAY_ROVER_COUNT_ACC), SMU_MUNIT(NONE),    \
		   SMU_MTYPE(U64), pcie_replay_rover_count_acc);               \
	SMU_SCALAR(SMU_MATTR(PCIE_NAK_SENT_COUNT_ACC), SMU_MUNIT(NONE),        \
		   SMU_MTYPE(U64), pcie_nak_sent_count_acc);                   \
	SMU_SCALAR(SMU_MATTR(PCIE_NAK_RCVD_COUNT_ACC), SMU_MUNIT(NONE),        \
		   SMU_MTYPE(U64), pcie_nak_rcvd_count_acc);                   \
	SMU_ARRAY(SMU_MATTR(XGMI_LINK_STATUS), SMU_MUNIT(NONE),                \
		  SMU_MTYPE(U16), xgmi_link_status,                            \
		  SMU_15_0_8_NUM_XGMI_LINKS);                                  \
	SMU_SCALAR(SMU_MATTR(XGMI_READ_DATA_ACC), SMU_MUNIT(DATA_1),           \
		   SMU_MTYPE(U64), xgmi_read_data_acc);                        \
	SMU_SCALAR(SMU_MATTR(XGMI_WRITE_DATA_ACC), SMU_MUNIT(DATA_1),          \
		   SMU_MTYPE(U64), xgmi_write_data_acc);                                  \
	SMU_SCALAR(SMU_MATTR(FIRMWARE_TIMESTAMP), SMU_MUNIT(TIME_2),           \
		   SMU_MTYPE(U64), firmware_timestamp);                        \
	SMU_ARRAY(SMU_MATTR(CURRENT_GFXCLK), SMU_MUNIT(CLOCK_1),               \
		  SMU_MTYPE(U16), current_gfxclk, SMU_15_0_8_MAX_GFX_CLKS);    \
	SMU_ARRAY(SMU_MATTR(CURRENT_SOCCLK), SMU_MUNIT(CLOCK_1),               \
		  SMU_MTYPE(U16), current_socclk, SMU_15_0_8_MAX_MID);         \
	SMU_ARRAY(SMU_MATTR(CURRENT_VCLK0), SMU_MUNIT(CLOCK_1),                \
		  SMU_MTYPE(U16), current_vclk0, SMU_15_0_8_MAX_VCN);          \
	SMU_ARRAY(SMU_MATTR(CURRENT_DCLK0), SMU_MUNIT(CLOCK_1),                \
		  SMU_MTYPE(U16), current_dclk0, SMU_15_0_8_MAX_VCN);          \
	SMU_ARRAY(SMU_MATTR(CURRENT_UCLK), SMU_MUNIT(CLOCK_1),                 \
		  SMU_MTYPE(U16), current_uclk, SMU_15_0_8_MAX_AID);           \
	SMU_SCALAR(SMU_MATTR(PCIE_LC_PERF_OTHER_END_RECOVERY),                 \
		   SMU_MUNIT(NONE), SMU_MTYPE(U64),                            \
		   pcie_lc_perf_other_end_recovery);                           \
	SMU_ARRAY(SMU_MATTR(GFX_BUSY_INST), SMU_MUNIT(PERCENT),                \
		  SMU_MTYPE(U32), gfx_busy_inst, SMU_15_0_8_MAX_XCC);          \
	SMU_ARRAY(SMU_MATTR(JPEG_BUSY), SMU_MUNIT(PERCENT), SMU_MTYPE(U16),    \
		  jpeg_busy, SMU_15_0_8_MAX_JPEG);                             \
	SMU_ARRAY(SMU_MATTR(VCN_BUSY), SMU_MUNIT(PERCENT), SMU_MTYPE(U16),     \
		  vcn_busy, SMU_15_0_8_MAX_VCN);                               \
	SMU_ARRAY(SMU_MATTR(GFX_BUSY_ACC), SMU_MUNIT(NONE), SMU_MTYPE(U64),    \
		  gfx_busy_acc, SMU_15_0_8_MAX_XCC);                           \
	SMU_ARRAY(SMU_MATTR(GFX_BELOW_HOST_LIMIT_PPT_ACC), SMU_MUNIT(NONE),    \
		  SMU_MTYPE(U64), gfx_below_host_limit_ppt_acc,                \
		  SMU_15_0_8_MAX_XCC);                                         \
	SMU_ARRAY(SMU_MATTR(GFX_BELOW_HOST_LIMIT_THM_ACC), SMU_MUNIT(NONE),    \
		  SMU_MTYPE(U64), gfx_below_host_limit_thm_acc,                \
		  SMU_15_0_8_MAX_XCC);                                         \
	SMU_ARRAY(SMU_MATTR(GFX_LOW_UTILIZATION_ACC), SMU_MUNIT(NONE),         \
		  SMU_MTYPE(U64), gfx_low_utilization_acc,                     \
		  SMU_15_0_8_MAX_XCC);                                         \
	SMU_ARRAY(SMU_MATTR(GFX_BELOW_HOST_LIMIT_TOTAL_ACC), SMU_MUNIT(NONE),  \
		  SMU_MTYPE(U64), gfx_below_host_limit_total_acc,              \
		  SMU_15_0_8_MAX_XCC);

DECLARE_SMU_METRICS_CLASS(smu_v15_0_8_gpu_metrics, SMU_15_0_8_METRICS_FIELDS);

/* Maximum temperature sensor counts for system metrics */
#define SMU_15_0_8_MAX_SYSTEM_TEMP_ENTRIES	32
#define SMU_15_0_8_MAX_NODE_TEMP_ENTRIES	12
#define SMU_15_0_8_MAX_VR_TEMP_ENTRIES		22

/* SMUv 15.0.8 GPU board temperature metrics */
#define SMU_15_0_8_GPUBOARD_TEMP_METRICS_FIELDS(SMU_SCALAR, SMU_ARRAY)         \
	SMU_SCALAR(SMU_MATTR(ACCUMULATION_COUNTER), SMU_MUNIT(NONE),           \
		   SMU_MTYPE(U64), accumulation_counter);                      \
	SMU_SCALAR(SMU_MATTR(LABEL_VERSION), SMU_MUNIT(NONE),                  \
		   SMU_MTYPE(U16), label_version);                             \
	SMU_SCALAR(SMU_MATTR(NODE_ID), SMU_MUNIT(NONE),                        \
		   SMU_MTYPE(U16), node_id);                                   \
	SMU_SCALAR(SMU_MATTR(NODE_TEMP_RETIMER), SMU_MUNIT(TEMP_1),            \
		   SMU_MTYPE(S16), node_temp_retimer);                         \
	SMU_SCALAR(SMU_MATTR(NODE_TEMP_IBC), SMU_MUNIT(TEMP_1),                \
		   SMU_MTYPE(S16), node_temp_ibc);                             \
	SMU_SCALAR(SMU_MATTR(NODE_TEMP_IBC_2), SMU_MUNIT(TEMP_1),              \
		   SMU_MTYPE(S16), node_temp_ibc_2);                           \
	SMU_SCALAR(SMU_MATTR(NODE_TEMP_VDD18_VR), SMU_MUNIT(TEMP_1),           \
		   SMU_MTYPE(S16), node_temp_vdd18_vr);                        \
	SMU_SCALAR(SMU_MATTR(NODE_TEMP_04_HBM_B_VR), SMU_MUNIT(TEMP_1),        \
		   SMU_MTYPE(S16), node_temp_04_hbm_b_vr);                     \
	SMU_SCALAR(SMU_MATTR(NODE_TEMP_04_HBM_D_VR), SMU_MUNIT(TEMP_1),        \
		   SMU_MTYPE(S16), node_temp_04_hbm_d_vr);                     \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDCR_SOCIO_A), SMU_MUNIT(TEMP_1),        \
		   SMU_MTYPE(S16), vr_temp_vddcr_socio_a);                     \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDCR_SOCIO_C), SMU_MUNIT(TEMP_1),        \
		   SMU_MTYPE(S16), vr_temp_vddcr_socio_c);                     \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDCR_X0), SMU_MUNIT(TEMP_1),             \
		   SMU_MTYPE(S16), vr_temp_vddcr_x0);                          \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDCR_X1), SMU_MUNIT(TEMP_1),             \
		   SMU_MTYPE(S16), vr_temp_vddcr_x1);                          \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDIO_HBM_B), SMU_MUNIT(TEMP_1),          \
		   SMU_MTYPE(S16), vr_temp_vddio_hbm_b);                       \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDIO_HBM_D), SMU_MUNIT(TEMP_1),          \
		   SMU_MTYPE(S16), vr_temp_vddio_hbm_d);                       \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDIO_04_HBM_B), SMU_MUNIT(TEMP_1),       \
		   SMU_MTYPE(S16), vr_temp_vddio_04_hbm_b);                    \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDIO_04_HBM_D), SMU_MUNIT(TEMP_1),       \
		   SMU_MTYPE(S16), vr_temp_vddio_04_hbm_d);                    \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDCR_HBM_B), SMU_MUNIT(TEMP_1),          \
		   SMU_MTYPE(S16), vr_temp_vddcr_hbm_b);                       \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDCR_HBM_D), SMU_MUNIT(TEMP_1),          \
		   SMU_MTYPE(S16), vr_temp_vddcr_hbm_d);                       \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDCR_075_HBM_B), SMU_MUNIT(TEMP_1),      \
		   SMU_MTYPE(S16), vr_temp_vddcr_075_hbm_b);                   \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDCR_075_HBM_D), SMU_MUNIT(TEMP_1),      \
		   SMU_MTYPE(S16), vr_temp_vddcr_075_hbm_d);                   \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDIO_11_GTA_A), SMU_MUNIT(TEMP_1),       \
		   SMU_MTYPE(S16), vr_temp_vddio_11_gta_a);                    \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDIO_11_GTA_C), SMU_MUNIT(TEMP_1),       \
		   SMU_MTYPE(S16), vr_temp_vddio_11_gta_c);                    \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDAN_075_GTA_A), SMU_MUNIT(TEMP_1),      \
		   SMU_MTYPE(S16), vr_temp_vddan_075_gta_a);                   \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDAN_075_GTA_C), SMU_MUNIT(TEMP_1),      \
		   SMU_MTYPE(S16), vr_temp_vddan_075_gta_c);                   \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDCR_075_UCIE), SMU_MUNIT(TEMP_1),       \
		   SMU_MTYPE(S16), vr_temp_vddcr_075_ucie);                    \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDIO_065_UCIEAA), SMU_MUNIT(TEMP_1),     \
		   SMU_MTYPE(S16), vr_temp_vddio_065_ucieaa);                  \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDIO_065_UCIEAM_A), SMU_MUNIT(TEMP_1),   \
		   SMU_MTYPE(S16), vr_temp_vddio_065_ucieam_a);                \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDIO_065_UCIEAM_C), SMU_MUNIT(TEMP_1),   \
		   SMU_MTYPE(S16), vr_temp_vddio_065_ucieam_c);                \
	SMU_SCALAR(SMU_MATTR(VR_TEMP_VDDAN_075), SMU_MUNIT(TEMP_1),            \
		   SMU_MTYPE(S16), vr_temp_vddan_075);

DECLARE_SMU_METRICS_CLASS(smu_v15_0_8_gpuboard_temp_metrics,
			  SMU_15_0_8_GPUBOARD_TEMP_METRICS_FIELDS);

/* SMUv 15.0.8 Baseboard temperature metrics - ID-based approach */
#define SMU_15_0_8_BASEBOARD_TEMP_METRICS_FIELDS(SMU_SCALAR, SMU_ARRAY)        \
	SMU_SCALAR(SMU_MATTR(ACCUMULATION_COUNTER), SMU_MUNIT(NONE),           \
		   SMU_MTYPE(U64), accumulation_counter);                      \
	SMU_SCALAR(SMU_MATTR(LABEL_VERSION), SMU_MUNIT(NONE),                  \
		   SMU_MTYPE(U16), label_version);                             \
	SMU_SCALAR(SMU_MATTR(NODE_ID), SMU_MUNIT(NONE),                        \
		   SMU_MTYPE(U16), node_id);                                   \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_UBB_FPGA), SMU_MUNIT(TEMP_1),         \
		   SMU_MTYPE(S16), system_temp_ubb_fpga);                      \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_UBB_FRONT), SMU_MUNIT(TEMP_1),        \
		   SMU_MTYPE(S16), system_temp_ubb_front);                     \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_UBB_BACK), SMU_MUNIT(TEMP_1),         \
		   SMU_MTYPE(S16), system_temp_ubb_back);                      \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_UBB_OAM7), SMU_MUNIT(TEMP_1),         \
		   SMU_MTYPE(S16), system_temp_ubb_oam7);                      \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_UBB_IBC), SMU_MUNIT(TEMP_1),          \
		   SMU_MTYPE(S16), system_temp_ubb_ibc);                       \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_UBB_UFPGA), SMU_MUNIT(TEMP_1),        \
		   SMU_MTYPE(S16), system_temp_ubb_ufpga);                     \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_UBB_OAM1), SMU_MUNIT(TEMP_1),         \
		   SMU_MTYPE(S16), system_temp_ubb_oam1);                      \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_OAM_0_1_HSC), SMU_MUNIT(TEMP_1),      \
		   SMU_MTYPE(S16), system_temp_oam_0_1_hsc);                   \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_OAM_2_3_HSC), SMU_MUNIT(TEMP_1),      \
		   SMU_MTYPE(S16), system_temp_oam_2_3_hsc);                   \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_OAM_4_5_HSC), SMU_MUNIT(TEMP_1),      \
		   SMU_MTYPE(S16), system_temp_oam_4_5_hsc);                   \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_OAM_6_7_HSC), SMU_MUNIT(TEMP_1),      \
		   SMU_MTYPE(S16), system_temp_oam_6_7_hsc);                   \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_UBB_FPGA_0V72_VR), SMU_MUNIT(TEMP_1), \
		   SMU_MTYPE(S16), system_temp_ubb_fpga_0v72_vr);              \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_UBB_FPGA_3V3_VR), SMU_MUNIT(TEMP_1),  \
		   SMU_MTYPE(S16), system_temp_ubb_fpga_3v3_vr);               \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_RETIMER_0_1_2_3_1V2_VR), SMU_MUNIT(TEMP_1), \
		   SMU_MTYPE(S16), system_temp_retimer_0_1_2_3_1v2_vr);        \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_RETIMER_4_5_6_7_1V2_VR), SMU_MUNIT(TEMP_1), \
		   SMU_MTYPE(S16), system_temp_retimer_4_5_6_7_1v2_vr);        \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_RETIMER_0_1_0V9_VR), SMU_MUNIT(TEMP_1), \
		   SMU_MTYPE(S16), system_temp_retimer_0_1_0v9_vr);            \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_RETIMER_4_5_0V9_VR), SMU_MUNIT(TEMP_1), \
		   SMU_MTYPE(S16), system_temp_retimer_4_5_0v9_vr);            \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_RETIMER_2_3_0V9_VR), SMU_MUNIT(TEMP_1), \
		   SMU_MTYPE(S16), system_temp_retimer_2_3_0v9_vr);            \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_RETIMER_6_7_0V9_VR), SMU_MUNIT(TEMP_1), \
		   SMU_MTYPE(S16), system_temp_retimer_6_7_0v9_vr);            \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_OAM_0_1_2_3_3V3_VR), SMU_MUNIT(TEMP_1), \
		   SMU_MTYPE(S16), system_temp_oam_0_1_2_3_3v3_vr);            \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_OAM_4_5_6_7_3V3_VR), SMU_MUNIT(TEMP_1), \
		   SMU_MTYPE(S16), system_temp_oam_4_5_6_7_3v3_vr);            \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_IBC_HSC), SMU_MUNIT(TEMP_1),          \
		   SMU_MTYPE(S16), system_temp_ibc_hsc);                       \
	SMU_SCALAR(SMU_MATTR(SYSTEM_TEMP_IBC), SMU_MUNIT(TEMP_1),              \
		   SMU_MTYPE(S16), system_temp_ibc);

DECLARE_SMU_METRICS_CLASS(smu_v15_0_8_baseboard_temp_metrics,
			  SMU_15_0_8_BASEBOARD_TEMP_METRICS_FIELDS);
#define SMU_15_0_8_PARTITION_METRICS_FIELDS(SMU_SCALAR, SMU_ARRAY)             \
	SMU_ARRAY(SMU_MATTR(CURRENT_GFXCLK), SMU_MUNIT(CLOCK_1),               \
		  SMU_MTYPE(U16), current_gfxclk, SMU_15_0_8_MAX_XCC);         \
	SMU_ARRAY(SMU_MATTR(CURRENT_VCLK0), SMU_MUNIT(CLOCK_1),                \
		  SMU_MTYPE(U16), current_vclk, SMU_15_0_8_MAX_VCN);          \
	SMU_ARRAY(SMU_MATTR(CURRENT_DCLK0), SMU_MUNIT(CLOCK_1),                \
		  SMU_MTYPE(U16), current_dclk, SMU_15_0_8_MAX_VCN);          \
	SMU_ARRAY(SMU_MATTR(GFX_BUSY_INST), SMU_MUNIT(PERCENT),                \
		  SMU_MTYPE(U8), gfx_busy_inst, SMU_15_0_8_MAX_XCC);          \
	SMU_ARRAY(SMU_MATTR(JPEG_BUSY), SMU_MUNIT(PERCENT), SMU_MTYPE(U8),    \
		  jpeg_busy, SMU_15_0_8_MAX_JPEG);                             \
	SMU_ARRAY(SMU_MATTR(VCN_BUSY), SMU_MUNIT(PERCENT), SMU_MTYPE(U8),     \
		  vcn_busy, SMU_15_0_8_MAX_VCN);                               \
	SMU_ARRAY(SMU_MATTR(GFX_BUSY_ACC), SMU_MUNIT(NONE), SMU_MTYPE(U64),    \
		  gfx_busy_acc, SMU_15_0_8_MAX_XCC);                           \
	SMU_ARRAY(SMU_MATTR(GFX_BELOW_HOST_LIMIT_PPT_ACC), SMU_MUNIT(NONE),    \
		  SMU_MTYPE(U64), gfx_below_host_limit_ppt_acc,                \
		  SMU_15_0_8_MAX_XCC);                                         \
	SMU_ARRAY(SMU_MATTR(GFX_BELOW_HOST_LIMIT_THM_ACC), SMU_MUNIT(NONE),    \
		  SMU_MTYPE(U64), gfx_below_host_limit_thm_acc,                \
		  SMU_15_0_8_MAX_XCC);                                         \
	SMU_ARRAY(SMU_MATTR(GFX_LOW_UTILIZATION_ACC), SMU_MUNIT(NONE),         \
		  SMU_MTYPE(U64), gfx_low_utilization_acc,                     \
		  SMU_15_0_8_MAX_XCC);                                         \
	SMU_ARRAY(SMU_MATTR(GFX_BELOW_HOST_LIMIT_TOTAL_ACC), SMU_MUNIT(NONE),  \
		  SMU_MTYPE(U64), gfx_below_host_limit_total_acc,              \
		  SMU_15_0_8_MAX_XCC);					       \
	SMU_SCALAR(SMU_MATTR(ACCUMULATION_COUNTER), SMU_MUNIT(NONE),           \
		   SMU_MTYPE(U64), accumulation_counter);                      \
	SMU_SCALAR(SMU_MATTR(FIRMWARE_TIMESTAMP), SMU_MUNIT(TIME_2),           \
		   SMU_MTYPE(U64), firmware_timestamp);

DECLARE_SMU_METRICS_CLASS(smu_v15_0_8_partition_metrics,
			  SMU_15_0_8_PARTITION_METRICS_FIELDS);
#endif
#endif
