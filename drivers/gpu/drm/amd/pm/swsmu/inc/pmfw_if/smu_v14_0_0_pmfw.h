/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __SMU_V14_0_0_PMFW_H__
#define __SMU_V14_0_0_PMFW_H__

#include "smu14_driver_if_v14_0_0.h"

#pragma pack(push, 1)

#define ENABLE_DEBUG_FEATURES

// Firmware features
// Feature Control Defines
#define FEATURE_CCLK_DPM_BIT                 0
#define FEATURE_FAN_CONTROLLER_BIT           1
#define FEATURE_DATA_CALCULATION_BIT         2
#define FEATURE_PPT_BIT                      3
#define FEATURE_TDC_BIT                      4
#define FEATURE_THERMAL_BIT                  5
#define FEATURE_FIT_BIT                      6
#define FEATURE_EDC_BIT                      7
#define FEATURE_PLL_POWER_DOWN_BIT           8
#define FEATURE_VDDOFF_BIT                   9
#define FEATURE_VCN_DPM_BIT                 10   /* this is for both VCN0 and VCN1 */
#define FEATURE_DS_MPM_BIT                  11
#define FEATURE_FCLK_DPM_BIT                12
#define FEATURE_SOCCLK_DPM_BIT              13
#define FEATURE_DS_MPIO_BIT                 14
#define FEATURE_LCLK_DPM_BIT                15
#define FEATURE_SHUBCLK_DPM_BIT             16
#define FEATURE_DCFCLK_DPM_BIT              17
#define FEATURE_ISP_DPM_BIT                 18
#define FEATURE_IPU_DPM_BIT                 19
#define FEATURE_GFX_DPM_BIT                 20
#define FEATURE_DS_GFXCLK_BIT               21
#define FEATURE_DS_SOCCLK_BIT               22
#define FEATURE_DS_LCLK_BIT                 23
#define FEATURE_LOW_POWER_DCNCLKS_BIT       24
#define FEATURE_DS_SHUBCLK_BIT              25
#define FEATURE_RESERVED0_BIT               26
#define FEATURE_ZSTATES_BIT                 27
#define FEATURE_IOMMUL2_PG_BIT              28
#define FEATURE_DS_FCLK_BIT                 29
#define FEATURE_DS_SMNCLK_BIT               30
#define FEATURE_DS_MP1CLK_BIT               31
#define FEATURE_WHISPER_MODE_BIT            32
#define FEATURE_SMU_LOW_POWER_BIT           33
#define FEATURE_RESERVED1_BIT               34  /* v14_0_0 SMART_L3_RINSER; v14_0_1 RESERVED1 */
#define FEATURE_GFX_DEM_BIT                 35  /* v14_0_0 SPARE; v14_0_1 GFX_DEM */
#define FEATURE_PSI_BIT                     36
#define FEATURE_PROCHOT_BIT                 37
#define FEATURE_CPUOFF_BIT                  38
#define FEATURE_STAPM_BIT                   39
#define FEATURE_S0I3_BIT                    40
#define FEATURE_DF_LIGHT_CSTATE             41
#define FEATURE_PERF_LIMIT_BIT              42
#define FEATURE_CORE_DLDO_BIT               43
#define FEATURE_DVO_BIT                     44
#define FEATURE_DS_VCN_BIT                  45  /* v14_0_1 this is for both VCN0 and VCN1 */
#define FEATURE_CPPC_BIT                    46
#define FEATURE_CPPC_PREFERRED_CORES        47
#define FEATURE_DF_CSTATES_BIT              48
#define FEATURE_FAST_PSTATE_CLDO_BIT        49  /* v14_0_0 SPARE */
#define FEATURE_ATHUB_PG_BIT                50
#define FEATURE_VDDOFF_ECO_BIT              51
#define FEATURE_ZSTATES_ECO_BIT             52
#define FEATURE_CC6_BIT                     53
#define FEATURE_DS_UMCCLK_BIT               54
#define FEATURE_DS_ISPCLK_BIT               55
#define FEATURE_DS_HSPCLK_BIT               56
#define FEATURE_P3T_BIT                     57
#define FEATURE_DS_IPUCLK_BIT               58
#define FEATURE_DS_VPECLK_BIT               59
#define FEATURE_VPE_DPM_BIT                 60
#define FEATURE_SMART_L3_RINSER_BIT         61  /* v14_0_0 SPARE*/
#define FEATURE_PCC_BIT                     62  /* v14_0_0 FP_DIDT v14_0_1 PCC_BIT */
#define NUM_FEATURES                        63

// Firmware Header/Footer
struct SMU14_Firmware_Footer {
  uint32_t Signature;
};
typedef struct SMU14_Firmware_Footer SMU14_Firmware_Footer;

// PSP3.0 Header Definition
typedef struct {
  uint32_t ImageVersion;
  uint32_t ImageVersion2; // This is repeated because DW0 cannot be written in SRAM due to HW bug.
  uint32_t Padding0[3];
  uint32_t SizeFWSigned;
  uint32_t Padding1[25];
  uint32_t FirmwareType;
  uint32_t Filler[32];
} SMU_Firmware_Header;

typedef struct {
  // MP1_EXT_SCRATCH0
  uint32_t DpmHandlerID         : 8;
  uint32_t ActivityMonitorID    : 8;
  uint32_t DpmTimerID           : 8;
  uint32_t DpmHubID             : 4;
  uint32_t DpmHubTask           : 4;
  // MP1_EXT_SCRATCH1
  uint32_t CclkSyncStatus       : 8;
  uint32_t Ccx0CpuOff           : 2;
  uint32_t Ccx1CpuOff           : 2;
  uint32_t GfxOffStatus         : 2;
  uint32_t VddOff               : 1;
  uint32_t InWhisperMode        : 1;
  uint32_t ZstateStatus         : 4;
  uint32_t spare0               : 4;
  uint32_t DstateFun            : 4;
  uint32_t DstateDev            : 4;
  // MP1_EXT_SCRATCH2
  uint32_t P2JobHandler         :24;
  uint32_t RsmuPmiP2PendingCnt  : 8;
  // MP1_EXT_SCRATCH3
  uint32_t PostCode             :32;
  // MP1_EXT_SCRATCH4
  uint32_t MsgPortBusy          :24;
  uint32_t RsmuPmiP1Pending     : 1;
  uint32_t DfCstateExitPending  : 1;
  uint32_t Ccx0Pc6ExitPending   : 1;
  uint32_t Ccx1Pc6ExitPending   : 1;
  uint32_t WarmResetPending     : 1;
  uint32_t spare1               : 3;
  // MP1_EXT_SCRATCH5
  uint32_t IdleMask             :32;
  // MP1_EXT_SCRATCH6 = RTOS threads' status
  // MP1_EXT_SCRATCH7 = RTOS Current Job
} FwStatus_t;

typedef struct {
  // MP1_EXT_SCRATCH0
  uint32_t DpmHandlerID         : 8;
  uint32_t ActivityMonitorID    : 8;
  uint32_t DpmTimerID           : 8;
  uint32_t DpmHubID             : 4;
  uint32_t DpmHubTask           : 4;
  // MP1_EXT_SCRATCH1
  uint32_t CclkSyncStatus       : 8;
  uint32_t ZstateStatus         : 4;
  uint32_t Cpu1VddOff           : 4;
  uint32_t DstateFun            : 4;
  uint32_t DstateDev            : 4;
  uint32_t GfxOffStatus         : 2;
  uint32_t Cpu0Off              : 2;
  uint32_t Cpu1Off              : 2;
  uint32_t Cpu0VddOff           : 2;
  // MP1_EXT_SCRATCH2
  uint32_t P2JobHandler         :32;
  // MP1_EXT_SCRATCH3
  uint32_t PostCode             :32;
  // MP1_EXT_SCRATCH4
  uint32_t MsgPortBusy          :15;
  uint32_t RsmuPmiP1Pending     : 1;
  uint32_t RsmuPmiP2PendingCnt  : 8;
  uint32_t DfCstateExitPending  : 1;
  uint32_t Pc6EntryPending      : 1;
  uint32_t Pc6ExitPending       : 1;
  uint32_t WarmResetPending     : 1;
  uint32_t Mp0ClkPending        : 1;
  uint32_t InWhisperMode        : 1;
  uint32_t spare2               : 2;
  // MP1_EXT_SCRATCH5
  uint32_t IdleMask             :32;
  // MP1_EXT_SCRATCH6 = RTOS threads' status
  // MP1_EXT_SCRATCH7 = RTOS Current Job
} FwStatus_t_v14_0_1;

#pragma pack(pop)

#endif
