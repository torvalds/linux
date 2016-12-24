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
#ifndef SMU71_H
#define SMU71_H

#if !defined(SMC_MICROCODE)
#pragma pack(push, 1)
#endif

#define SMU__NUM_PCIE_DPM_LEVELS 8
#define SMU__NUM_SCLK_DPM_STATE 8
#define SMU__NUM_MCLK_DPM_LEVELS 4
#define SMU__VARIANT__ICELAND 1
#define SMU__DGPU_ONLY 1
#define SMU__DYNAMIC_MCARB_SETTINGS 1

enum SID_OPTION {
  SID_OPTION_HI,
  SID_OPTION_LO,
  SID_OPTION_COUNT
};

typedef struct {
  uint32_t high;
  uint32_t low;
} data_64_t;

typedef struct {
  data_64_t high;
  data_64_t low;
} data_128_t;

#define SMU7_CONTEXT_ID_SMC        1
#define SMU7_CONTEXT_ID_VBIOS      2

#define SMU71_MAX_LEVELS_VDDC            8
#define SMU71_MAX_LEVELS_VDDCI           4
#define SMU71_MAX_LEVELS_MVDD            4
#define SMU71_MAX_LEVELS_VDDNB           8

#define SMU71_MAX_LEVELS_GRAPHICS        SMU__NUM_SCLK_DPM_STATE
#define SMU71_MAX_LEVELS_MEMORY          SMU__NUM_MCLK_DPM_LEVELS
#define SMU71_MAX_LEVELS_GIO             SMU__NUM_LCLK_DPM_LEVELS
#define SMU71_MAX_LEVELS_LINK            SMU__NUM_PCIE_DPM_LEVELS
#define SMU71_MAX_ENTRIES_SMIO           32

#define DPM_NO_LIMIT 0
#define DPM_NO_UP 1
#define DPM_GO_DOWN 2
#define DPM_GO_UP 3

#define SMU7_FIRST_DPM_GRAPHICS_LEVEL    0
#define SMU7_FIRST_DPM_MEMORY_LEVEL      0

#define GPIO_CLAMP_MODE_VRHOT      1
#define GPIO_CLAMP_MODE_THERM      2
#define GPIO_CLAMP_MODE_DC         4

#define SCRATCH_B_TARG_PCIE_INDEX_SHIFT 0
#define SCRATCH_B_TARG_PCIE_INDEX_MASK  (0x7<<SCRATCH_B_TARG_PCIE_INDEX_SHIFT)
#define SCRATCH_B_CURR_PCIE_INDEX_SHIFT 3
#define SCRATCH_B_CURR_PCIE_INDEX_MASK  (0x7<<SCRATCH_B_CURR_PCIE_INDEX_SHIFT)
#define SCRATCH_B_TARG_UVD_INDEX_SHIFT  6
#define SCRATCH_B_TARG_UVD_INDEX_MASK   (0x7<<SCRATCH_B_TARG_UVD_INDEX_SHIFT)
#define SCRATCH_B_CURR_UVD_INDEX_SHIFT  9
#define SCRATCH_B_CURR_UVD_INDEX_MASK   (0x7<<SCRATCH_B_CURR_UVD_INDEX_SHIFT)
#define SCRATCH_B_TARG_VCE_INDEX_SHIFT  12
#define SCRATCH_B_TARG_VCE_INDEX_MASK   (0x7<<SCRATCH_B_TARG_VCE_INDEX_SHIFT)
#define SCRATCH_B_CURR_VCE_INDEX_SHIFT  15
#define SCRATCH_B_CURR_VCE_INDEX_MASK   (0x7<<SCRATCH_B_CURR_VCE_INDEX_SHIFT)
#define SCRATCH_B_TARG_ACP_INDEX_SHIFT  18
#define SCRATCH_B_TARG_ACP_INDEX_MASK   (0x7<<SCRATCH_B_TARG_ACP_INDEX_SHIFT)
#define SCRATCH_B_CURR_ACP_INDEX_SHIFT  21
#define SCRATCH_B_CURR_ACP_INDEX_MASK   (0x7<<SCRATCH_B_CURR_ACP_INDEX_SHIFT)
#define SCRATCH_B_TARG_SAMU_INDEX_SHIFT 24
#define SCRATCH_B_TARG_SAMU_INDEX_MASK  (0x7<<SCRATCH_B_TARG_SAMU_INDEX_SHIFT)
#define SCRATCH_B_CURR_SAMU_INDEX_SHIFT 27
#define SCRATCH_B_CURR_SAMU_INDEX_MASK  (0x7<<SCRATCH_B_CURR_SAMU_INDEX_SHIFT)


#if defined SMU__DGPU_ONLY
#define SMU71_DTE_ITERATIONS 5
#define SMU71_DTE_SOURCES 3
#define SMU71_DTE_SINKS 1
#define SMU71_NUM_CPU_TES 0
#define SMU71_NUM_GPU_TES 1
#define SMU71_NUM_NON_TES 2

#endif

#if defined SMU__FUSION_ONLY
#define SMU7_DTE_ITERATIONS 5
#define SMU7_DTE_SOURCES 5
#define SMU7_DTE_SINKS 3
#define SMU7_NUM_CPU_TES 2
#define SMU7_NUM_GPU_TES 1
#define SMU7_NUM_NON_TES 2

#endif

struct SMU71_PIDController
{
    uint32_t Ki;
    int32_t LFWindupUpperLim;
    int32_t LFWindupLowerLim;
    uint32_t StatePrecision;
    uint32_t LfPrecision;
    uint32_t LfOffset;
    uint32_t MaxState;
    uint32_t MaxLfFraction;
    uint32_t StateShift;
};

typedef struct SMU71_PIDController SMU71_PIDController;

struct SMU7_LocalDpmScoreboard
{
    uint32_t PercentageBusy;

    int32_t  PIDError;
    int32_t  PIDIntegral;
    int32_t  PIDOutput;

    uint32_t SigmaDeltaAccum;
    uint32_t SigmaDeltaOutput;
    uint32_t SigmaDeltaLevel;

    uint32_t UtilizationSetpoint;

    uint8_t  TdpClampMode;
    uint8_t  TdcClampMode;
    uint8_t  ThermClampMode;
    uint8_t  VoltageBusy;

    int8_t   CurrLevel;
    int8_t   TargLevel;
    uint8_t  LevelChangeInProgress;
    uint8_t  UpHyst;

    uint8_t  DownHyst;
    uint8_t  VoltageDownHyst;
    uint8_t  DpmEnable;
    uint8_t  DpmRunning;

    uint8_t  DpmForce;
    uint8_t  DpmForceLevel;
    uint8_t  DisplayWatermark;
    uint8_t  McArbIndex;

    uint32_t MinimumPerfSclk;

    uint8_t  AcpiReq;
    uint8_t  AcpiAck;
    uint8_t  GfxClkSlow;
    uint8_t  GpioClampMode;

    uint8_t  FpsFilterWeight;
    uint8_t  EnabledLevelsChange;
    uint8_t  DteClampMode;
    uint8_t  FpsClampMode;

    uint16_t LevelResidencyCounters [SMU71_MAX_LEVELS_GRAPHICS];
    uint16_t LevelSwitchCounters [SMU71_MAX_LEVELS_GRAPHICS];

    void     (*TargetStateCalculator)(uint8_t);
    void     (*SavedTargetStateCalculator)(uint8_t);

    uint16_t AutoDpmInterval;
    uint16_t AutoDpmRange;

    uint8_t  FpsEnabled;
    uint8_t  MaxPerfLevel;
    uint8_t  AllowLowClkInterruptToHost;
    uint8_t  FpsRunning;

    uint32_t MaxAllowedFrequency;
};

typedef struct SMU7_LocalDpmScoreboard SMU7_LocalDpmScoreboard;

#define SMU7_MAX_VOLTAGE_CLIENTS 12

struct SMU7_VoltageScoreboard
{
    uint16_t CurrentVoltage;
    uint16_t HighestVoltage;
    uint16_t MaxVid;
    uint8_t  HighestVidOffset;
    uint8_t  CurrentVidOffset;
#if defined (SMU__DGPU_ONLY)
    uint8_t  CurrentPhases;
    uint8_t  HighestPhases;
#else
    uint8_t  AvsOffset;
    uint8_t  AvsOffsetApplied;
#endif
    uint8_t  ControllerBusy;
    uint8_t  CurrentVid;
    uint16_t RequestedVoltage[SMU7_MAX_VOLTAGE_CLIENTS];
#if defined (SMU__DGPU_ONLY)
    uint8_t  RequestedPhases[SMU7_MAX_VOLTAGE_CLIENTS];
#endif
    uint8_t  EnabledRequest[SMU7_MAX_VOLTAGE_CLIENTS];
    uint8_t  TargetIndex;
    uint8_t  Delay;
    uint8_t  ControllerEnable;
    uint8_t  ControllerRunning;
    uint16_t CurrentStdVoltageHiSidd;
    uint16_t CurrentStdVoltageLoSidd;
#if defined (SMU__DGPU_ONLY)
    uint16_t RequestedVddci;
    uint16_t CurrentVddci;
    uint16_t HighestVddci;
    uint8_t  CurrentVddciVid;
    uint8_t  TargetVddciIndex;
#endif
};

typedef struct SMU7_VoltageScoreboard SMU7_VoltageScoreboard;

// -------------------------------------------------------------------------------------------------------------------------
#define SMU7_MAX_PCIE_LINK_SPEEDS 3 /* 0:Gen1 1:Gen2 2:Gen3 */

struct SMU7_PCIeLinkSpeedScoreboard
{
    uint8_t     DpmEnable;
    uint8_t     DpmRunning;
    uint8_t     DpmForce;
    uint8_t     DpmForceLevel;

    uint8_t     CurrentLinkSpeed;
    uint8_t     EnabledLevelsChange;
    uint16_t    AutoDpmInterval;

    uint16_t    AutoDpmRange;
    uint16_t    AutoDpmCount;

    uint8_t     DpmMode;
    uint8_t     AcpiReq;
    uint8_t     AcpiAck;
    uint8_t     CurrentLinkLevel;

};

typedef struct SMU7_PCIeLinkSpeedScoreboard SMU7_PCIeLinkSpeedScoreboard;

// -------------------------------------------------------- CAC table ------------------------------------------------------
#define SMU7_LKGE_LUT_NUM_OF_TEMP_ENTRIES 16
#define SMU7_LKGE_LUT_NUM_OF_VOLT_ENTRIES 16

#define SMU7_SCALE_I  7
#define SMU7_SCALE_R 12

struct SMU7_PowerScoreboard
{
    uint16_t   MinVoltage;
    uint16_t   MaxVoltage;

    uint32_t   AvgGpuPower;

    uint16_t   VddcLeakagePower[SID_OPTION_COUNT];
    uint16_t   VddcSclkConstantPower[SID_OPTION_COUNT];
    uint16_t   VddcSclkDynamicPower[SID_OPTION_COUNT];
    uint16_t   VddcNonSclkDynamicPower[SID_OPTION_COUNT];
    uint16_t   VddcTotalPower[SID_OPTION_COUNT];
    uint16_t   VddcTotalCurrent[SID_OPTION_COUNT];
    uint16_t   VddcLoadVoltage[SID_OPTION_COUNT];
    uint16_t   VddcNoLoadVoltage[SID_OPTION_COUNT];

    uint16_t   DisplayPhyPower;
    uint16_t   PciePhyPower;

    uint16_t   VddciTotalPower;
    uint16_t   Vddr1TotalPower;

    uint32_t   RocPower;

    uint32_t   last_power;
    uint32_t   enableWinAvg;

    uint32_t   lkg_acc;
    uint16_t   VoltLkgeScaler;
    uint16_t   TempLkgeScaler;

    uint32_t   uvd_cac_dclk;
    uint32_t   uvd_cac_vclk;
    uint32_t   vce_cac_eclk;
    uint32_t   samu_cac_samclk;
    uint32_t   display_cac_dispclk;
    uint32_t   acp_cac_aclk;
    uint32_t   unb_cac;

    uint32_t   WinTime;

    uint16_t  GpuPwr_MAWt;
    uint16_t  FilteredVddcTotalPower;

    uint8_t   CalculationRepeats;
    uint8_t   WaterfallUp;
    uint8_t   WaterfallDown;
    uint8_t   WaterfallLimit;
};

typedef struct SMU7_PowerScoreboard SMU7_PowerScoreboard;

// --------------------------------------------------------------------------------------------------

struct SMU7_ThermalScoreboard
{
   int16_t  GpuLimit;
   int16_t  GpuHyst;
   uint16_t CurrGnbTemp;
   uint16_t FilteredGnbTemp;
   uint8_t  ControllerEnable;
   uint8_t  ControllerRunning;
   uint8_t  WaterfallUp;
   uint8_t  WaterfallDown;
   uint8_t  WaterfallLimit;
   uint8_t  padding[3];
};

typedef struct SMU7_ThermalScoreboard SMU7_ThermalScoreboard;

// For FeatureEnables:
#define SMU7_SCLK_DPM_CONFIG_MASK                        0x01
#define SMU7_VOLTAGE_CONTROLLER_CONFIG_MASK              0x02
#define SMU7_THERMAL_CONTROLLER_CONFIG_MASK              0x04
#define SMU7_MCLK_DPM_CONFIG_MASK                        0x08
#define SMU7_UVD_DPM_CONFIG_MASK                         0x10
#define SMU7_VCE_DPM_CONFIG_MASK                         0x20
#define SMU7_ACP_DPM_CONFIG_MASK                         0x40
#define SMU7_SAMU_DPM_CONFIG_MASK                        0x80
#define SMU7_PCIEGEN_DPM_CONFIG_MASK                    0x100

#define SMU7_ACP_MCLK_HANDSHAKE_DISABLE                  0x00000001
#define SMU7_ACP_SCLK_HANDSHAKE_DISABLE                  0x00000002
#define SMU7_UVD_MCLK_HANDSHAKE_DISABLE                  0x00000100
#define SMU7_UVD_SCLK_HANDSHAKE_DISABLE                  0x00000200
#define SMU7_VCE_MCLK_HANDSHAKE_DISABLE                  0x00010000
#define SMU7_VCE_SCLK_HANDSHAKE_DISABLE                  0x00020000

// All 'soft registers' should be uint32_t.
struct SMU71_SoftRegisters
{
    uint32_t        RefClockFrequency;
    uint32_t        PmTimerPeriod;
    uint32_t        FeatureEnables;
#if defined (SMU__DGPU_ONLY)
    uint32_t        PreVBlankGap;
    uint32_t        VBlankTimeout;
    uint32_t        TrainTimeGap;
    uint32_t        MvddSwitchTime;
    uint32_t        LongestAcpiTrainTime;
    uint32_t        AcpiDelay;
    uint32_t        G5TrainTime;
    uint32_t        DelayMpllPwron;
    uint32_t        VoltageChangeTimeout;
#endif
    uint32_t        HandshakeDisables;

    uint8_t         DisplayPhy1Config;
    uint8_t         DisplayPhy2Config;
    uint8_t         DisplayPhy3Config;
    uint8_t         DisplayPhy4Config;

    uint8_t         DisplayPhy5Config;
    uint8_t         DisplayPhy6Config;
    uint8_t         DisplayPhy7Config;
    uint8_t         DisplayPhy8Config;

    uint32_t        AverageGraphicsActivity;
    uint32_t        AverageMemoryActivity;
    uint32_t        AverageGioActivity;

    uint8_t         SClkDpmEnabledLevels;
    uint8_t         MClkDpmEnabledLevels;
    uint8_t         LClkDpmEnabledLevels;
    uint8_t         PCIeDpmEnabledLevels;

    uint32_t        DRAM_LOG_ADDR_H;
    uint32_t        DRAM_LOG_ADDR_L;
    uint32_t        DRAM_LOG_PHY_ADDR_H;
    uint32_t        DRAM_LOG_PHY_ADDR_L;
    uint32_t        DRAM_LOG_BUFF_SIZE;
    uint32_t        UlvEnterCount;
    uint32_t        UlvTime;
    uint32_t        UcodeLoadStatus;
    uint8_t         DPMFreezeAndForced;
    uint8_t         Activity_Weight;
    uint8_t         Reserved8[2];
    uint32_t        Reserved;
};

typedef struct SMU71_SoftRegisters SMU71_SoftRegisters;

struct SMU71_Firmware_Header
{
    uint32_t Digest[5];
    uint32_t Version;
    uint32_t HeaderSize;
    uint32_t Flags;
    uint32_t EntryPoint;
    uint32_t CodeSize;
    uint32_t ImageSize;

    uint32_t Rtos;
    uint32_t SoftRegisters;
    uint32_t DpmTable;
    uint32_t FanTable;
    uint32_t CacConfigTable;
    uint32_t CacStatusTable;

    uint32_t mcRegisterTable;

    uint32_t mcArbDramTimingTable;

    uint32_t PmFuseTable;
    uint32_t Globals;
    uint32_t UvdDpmTable;
    uint32_t AcpDpmTable;
    uint32_t VceDpmTable;
    uint32_t SamuDpmTable;
    uint32_t UlvSettings;
    uint32_t Reserved[37];
    uint32_t Signature;
};

typedef struct SMU71_Firmware_Header SMU71_Firmware_Header;

struct SMU7_HystController_Data
{
    uint8_t waterfall_up;
    uint8_t waterfall_down;
    uint8_t pstate;
    uint8_t clamp_mode;
};

typedef struct SMU7_HystController_Data SMU7_HystController_Data;

#define SMU71_FIRMWARE_HEADER_LOCATION 0x20000

enum  DisplayConfig {
    PowerDown = 1,
    DP54x4,
    DP54x2,
    DP54x1,
    DP27x4,
    DP27x2,
    DP27x1,
    HDMI297,
    HDMI162,
    LVDS,
    DP324x4,
    DP324x2,
    DP324x1
};

//#define SX_BLOCK_COUNT 8
//#define MC_BLOCK_COUNT 1
//#define CPL_BLOCK_COUNT 27

#if defined SMU__VARIANT__ICELAND
  #define SX_BLOCK_COUNT 8
  #define MC_BLOCK_COUNT 1
  #define CPL_BLOCK_COUNT 29
#endif

struct SMU7_Local_Cac {
  uint8_t BlockId;
  uint8_t SignalId;
  uint8_t Threshold;
  uint8_t Padding;
};

typedef struct SMU7_Local_Cac SMU7_Local_Cac;

struct SMU7_Local_Cac_Table {
  SMU7_Local_Cac SxLocalCac[SX_BLOCK_COUNT];
  SMU7_Local_Cac CplLocalCac[CPL_BLOCK_COUNT];
  SMU7_Local_Cac McLocalCac[MC_BLOCK_COUNT];
};

typedef struct SMU7_Local_Cac_Table SMU7_Local_Cac_Table;

#if !defined(SMC_MICROCODE)
#pragma pack(pop)
#endif

#endif

