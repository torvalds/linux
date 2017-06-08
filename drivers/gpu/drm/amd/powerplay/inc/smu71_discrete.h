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
#ifndef SMU71_DISCRETE_H
#define SMU71_DISCRETE_H

#include "smu71.h"

#if !defined(SMC_MICROCODE)
#pragma pack(push, 1)
#endif

#define VDDC_ON_SVI2  0x1
#define VDDCI_ON_SVI2 0x2
#define MVDD_ON_SVI2  0x4

struct SMU71_Discrete_VoltageLevel
{
    uint16_t    Voltage;
    uint16_t    StdVoltageHiSidd;
    uint16_t    StdVoltageLoSidd;
    uint8_t     Smio;
    uint8_t     padding;
};

typedef struct SMU71_Discrete_VoltageLevel SMU71_Discrete_VoltageLevel;

struct SMU71_Discrete_GraphicsLevel
{
    uint32_t    MinVddc;
    uint32_t    MinVddcPhases;

    uint32_t    SclkFrequency;

    uint8_t     pcieDpmLevel;
    uint8_t     DeepSleepDivId;
    uint16_t    ActivityLevel;

    uint32_t    CgSpllFuncCntl3;
    uint32_t    CgSpllFuncCntl4;
    uint32_t    SpllSpreadSpectrum;
    uint32_t    SpllSpreadSpectrum2;
    uint32_t    CcPwrDynRm;
    uint32_t    CcPwrDynRm1;
    uint8_t     SclkDid;
    uint8_t     DisplayWatermark;
    uint8_t     EnabledForActivity;
    uint8_t     EnabledForThrottle;
    uint8_t     UpHyst;
    uint8_t     DownHyst;
    uint8_t     VoltageDownHyst;
    uint8_t     PowerThrottle;
};

typedef struct SMU71_Discrete_GraphicsLevel SMU71_Discrete_GraphicsLevel;

struct SMU71_Discrete_ACPILevel
{
    uint32_t    Flags;
    uint32_t    MinVddc;
    uint32_t    MinVddcPhases;
    uint32_t    SclkFrequency;
    uint8_t     SclkDid;
    uint8_t     DisplayWatermark;
    uint8_t     DeepSleepDivId;
    uint8_t     padding;
    uint32_t    CgSpllFuncCntl;
    uint32_t    CgSpllFuncCntl2;
    uint32_t    CgSpllFuncCntl3;
    uint32_t    CgSpllFuncCntl4;
    uint32_t    SpllSpreadSpectrum;
    uint32_t    SpllSpreadSpectrum2;
    uint32_t    CcPwrDynRm;
    uint32_t    CcPwrDynRm1;
};

typedef struct SMU71_Discrete_ACPILevel SMU71_Discrete_ACPILevel;

struct SMU71_Discrete_Ulv
{
    uint32_t    CcPwrDynRm;
    uint32_t    CcPwrDynRm1;
    uint16_t    VddcOffset;
    uint8_t     VddcOffsetVid;
    uint8_t     VddcPhase;
    uint32_t    Reserved;
};

typedef struct SMU71_Discrete_Ulv SMU71_Discrete_Ulv;

struct SMU71_Discrete_MemoryLevel
{
    uint32_t    MinVddc;
    uint32_t    MinVddcPhases;
    uint32_t    MinVddci;
    uint32_t    MinMvdd;

    uint32_t    MclkFrequency;

    uint8_t     EdcReadEnable;
    uint8_t     EdcWriteEnable;
    uint8_t     RttEnable;
    uint8_t     StutterEnable;

    uint8_t     StrobeEnable;
    uint8_t     StrobeRatio;
    uint8_t     EnabledForThrottle;
    uint8_t     EnabledForActivity;

    uint8_t     UpHyst;
    uint8_t     DownHyst;
    uint8_t     VoltageDownHyst;
    uint8_t     padding;

    uint16_t    ActivityLevel;
    uint8_t     DisplayWatermark;
    uint8_t     padding1;

    uint32_t    MpllFuncCntl;
    uint32_t    MpllFuncCntl_1;
    uint32_t    MpllFuncCntl_2;
    uint32_t    MpllAdFuncCntl;
    uint32_t    MpllDqFuncCntl;
    uint32_t    MclkPwrmgtCntl;
    uint32_t    DllCntl;
    uint32_t    MpllSs1;
    uint32_t    MpllSs2;
};

typedef struct SMU71_Discrete_MemoryLevel SMU71_Discrete_MemoryLevel;

struct SMU71_Discrete_LinkLevel
{
    uint8_t     PcieGenSpeed;           ///< 0:PciE-gen1 1:PciE-gen2 2:PciE-gen3
    uint8_t     PcieLaneCount;          ///< 1=x1, 2=x2, 3=x4, 4=x8, 5=x12, 6=x16
    uint8_t     EnabledForActivity;
    uint8_t     SPC;
    uint32_t    DownThreshold;
    uint32_t    UpThreshold;
    uint32_t    Reserved;
};

typedef struct SMU71_Discrete_LinkLevel SMU71_Discrete_LinkLevel;


#ifdef SMU__DYNAMIC_MCARB_SETTINGS
// MC ARB DRAM Timing registers.
struct SMU71_Discrete_MCArbDramTimingTableEntry
{
    uint32_t McArbDramTiming;
    uint32_t McArbDramTiming2;
    uint8_t  McArbBurstTime;
    uint8_t  padding[3];
};

typedef struct SMU71_Discrete_MCArbDramTimingTableEntry SMU71_Discrete_MCArbDramTimingTableEntry;

struct SMU71_Discrete_MCArbDramTimingTable
{
    SMU71_Discrete_MCArbDramTimingTableEntry entries[SMU__NUM_SCLK_DPM_STATE][SMU__NUM_MCLK_DPM_LEVELS];
};

typedef struct SMU71_Discrete_MCArbDramTimingTable SMU71_Discrete_MCArbDramTimingTable;
#endif

// UVD VCLK/DCLK state (level) definition.
struct SMU71_Discrete_UvdLevel
{
    uint32_t VclkFrequency;
    uint32_t DclkFrequency;
    uint16_t MinVddc;
    uint8_t  MinVddcPhases;
    uint8_t  VclkDivider;
    uint8_t  DclkDivider;
    uint8_t  padding[3];
};

typedef struct SMU71_Discrete_UvdLevel SMU71_Discrete_UvdLevel;

// Clocks for other external blocks (VCE, ACP, SAMU).
struct SMU71_Discrete_ExtClkLevel
{
    uint32_t Frequency;
    uint16_t MinVoltage;
    uint8_t  MinPhases;
    uint8_t  Divider;
};

typedef struct SMU71_Discrete_ExtClkLevel SMU71_Discrete_ExtClkLevel;

// Everything that we need to keep track of about the current state.
// Use this instead of copies of the GraphicsLevel and MemoryLevel structures to keep track of state parameters
// that need to be checked later.
// We don't need to cache everything about a state, just a few parameters.
struct SMU71_Discrete_StateInfo
{
    uint32_t SclkFrequency;
    uint32_t MclkFrequency;
    uint32_t VclkFrequency;
    uint32_t DclkFrequency;
    uint32_t SamclkFrequency;
    uint32_t AclkFrequency;
    uint32_t EclkFrequency;
    uint16_t MvddVoltage;
    uint16_t padding16;
    uint8_t  DisplayWatermark;
    uint8_t  McArbIndex;
    uint8_t  McRegIndex;
    uint8_t  SeqIndex;
    uint8_t  SclkDid;
    int8_t   SclkIndex;
    int8_t   MclkIndex;
    uint8_t  PCIeGen;

};

typedef struct SMU71_Discrete_StateInfo SMU71_Discrete_StateInfo;


struct SMU71_Discrete_DpmTable
{
    // Multi-DPM controller settings
    SMU71_PIDController                  GraphicsPIDController;
    SMU71_PIDController                  MemoryPIDController;
    SMU71_PIDController                  LinkPIDController;

    uint32_t                            SystemFlags;

    // SMIO masks for voltage and phase controls
    uint32_t                            SmioMaskVddcVid;
    uint32_t                            SmioMaskVddcPhase;
    uint32_t                            SmioMaskVddciVid;
    uint32_t                            SmioMaskMvddVid;

    uint32_t                            VddcLevelCount;
    uint32_t                            VddciLevelCount;
    uint32_t                            MvddLevelCount;

    SMU71_Discrete_VoltageLevel          VddcLevel               [SMU71_MAX_LEVELS_VDDC];
    SMU71_Discrete_VoltageLevel          VddciLevel              [SMU71_MAX_LEVELS_VDDCI];
    SMU71_Discrete_VoltageLevel          MvddLevel               [SMU71_MAX_LEVELS_MVDD];

    uint8_t                             GraphicsDpmLevelCount;
    uint8_t                             MemoryDpmLevelCount;
    uint8_t                             LinkLevelCount;
    uint8_t                             MasterDeepSleepControl;

    uint32_t                            Reserved[5];

    // State table entries for each DPM state
    SMU71_Discrete_GraphicsLevel         GraphicsLevel           [SMU71_MAX_LEVELS_GRAPHICS];
    SMU71_Discrete_MemoryLevel           MemoryACPILevel;
    SMU71_Discrete_MemoryLevel           MemoryLevel             [SMU71_MAX_LEVELS_MEMORY];
    SMU71_Discrete_LinkLevel             LinkLevel               [SMU71_MAX_LEVELS_LINK];
    SMU71_Discrete_ACPILevel             ACPILevel;

    uint32_t                            SclkStepSize;
    uint32_t                            Smio                    [SMU71_MAX_ENTRIES_SMIO];

    uint8_t                             GraphicsBootLevel;
    uint8_t                             GraphicsVoltageChangeEnable;
    uint8_t                             GraphicsThermThrottleEnable;
    uint8_t                             GraphicsInterval;

    uint8_t                             VoltageInterval;
    uint8_t                             ThermalInterval;
    uint16_t                            TemperatureLimitHigh;

    uint16_t                            TemperatureLimitLow;
    uint8_t                             MemoryBootLevel;
    uint8_t                             MemoryVoltageChangeEnable;

    uint8_t                             MemoryInterval;
    uint8_t                             MemoryThermThrottleEnable;
    uint8_t                             MergedVddci;
    uint8_t                             padding2;

    uint16_t                            VoltageResponseTime;
    uint16_t                            PhaseResponseTime;

    uint8_t                             PCIeBootLinkLevel;
    uint8_t                             PCIeGenInterval;
    uint8_t                             DTEInterval;
    uint8_t                             DTEMode;

    uint8_t                             SVI2Enable;
    uint8_t                             VRHotGpio;
    uint8_t                             AcDcGpio;
    uint8_t                             ThermGpio;

    uint32_t                            DisplayCac;

    uint16_t                            MaxPwr;
    uint16_t                            NomPwr;

    uint16_t                            FpsHighThreshold;
    uint16_t                            FpsLowThreshold;

    uint16_t                            BAPMTI_R  [SMU71_DTE_ITERATIONS][SMU71_DTE_SOURCES][SMU71_DTE_SINKS];
    uint16_t                            BAPMTI_RC [SMU71_DTE_ITERATIONS][SMU71_DTE_SOURCES][SMU71_DTE_SINKS];

    uint8_t                             DTEAmbientTempBase;
    uint8_t                             DTETjOffset;
    uint8_t                             GpuTjMax;
    uint8_t                             GpuTjHyst;

    uint16_t                            BootVddc;
    uint16_t                            BootVddci;

    uint16_t                            BootMVdd;
    uint16_t                            padding;

    uint32_t                            BAPM_TEMP_GRADIENT;

    uint32_t                            LowSclkInterruptThreshold;
    uint32_t                            VddGfxReChkWait;

    uint16_t                            PPM_PkgPwrLimit;
    uint16_t                            PPM_TemperatureLimit;

    uint16_t                            DefaultTdp;
    uint16_t                            TargetTdp;
};

typedef struct SMU71_Discrete_DpmTable SMU71_Discrete_DpmTable;

// --------------------------------------------------- AC Timing Parameters ------------------------------------------------
#define SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE 16
#define SMU71_DISCRETE_MC_REGISTER_ARRAY_SET_COUNT SMU71_MAX_LEVELS_MEMORY

struct SMU71_Discrete_MCRegisterAddress
{
    uint16_t s0;
    uint16_t s1;
};

typedef struct SMU71_Discrete_MCRegisterAddress SMU71_Discrete_MCRegisterAddress;

struct SMU71_Discrete_MCRegisterSet
{
    uint32_t value[SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE];
};

typedef struct SMU71_Discrete_MCRegisterSet SMU71_Discrete_MCRegisterSet;

struct SMU71_Discrete_MCRegisters
{
    uint8_t                             last;
    uint8_t                             reserved[3];
    SMU71_Discrete_MCRegisterAddress     address[SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE];
    SMU71_Discrete_MCRegisterSet         data[SMU71_DISCRETE_MC_REGISTER_ARRAY_SET_COUNT];
};

typedef struct SMU71_Discrete_MCRegisters SMU71_Discrete_MCRegisters;


// --------------------------------------------------- Fan Table -----------------------------------------------------------
struct SMU71_Discrete_FanTable
{
    uint16_t FdoMode;
    int16_t  TempMin;
    int16_t  TempMed;
    int16_t  TempMax;
    int16_t  Slope1;
    int16_t  Slope2;
    int16_t  FdoMin;
    int16_t  HystUp;
    int16_t  HystDown;
    int16_t  HystSlope;
    int16_t  TempRespLim;
    int16_t  TempCurr;
    int16_t  SlopeCurr;
    int16_t  PwmCurr;
    uint32_t RefreshPeriod;
    int16_t  FdoMax;
    uint8_t  TempSrc;
    int8_t   Padding;
};

typedef struct SMU71_Discrete_FanTable SMU71_Discrete_FanTable;

#define SMU7_DISCRETE_GPIO_SCLK_DEBUG             4
#define SMU7_DISCRETE_GPIO_SCLK_DEBUG_BIT         (0x1 << SMU7_DISCRETE_GPIO_SCLK_DEBUG)

struct SMU71_MclkDpmScoreboard
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

    uint32_t MinimumPerfMclk;

    uint8_t  AcpiReq;
    uint8_t  AcpiAck;
    uint8_t  MclkSwitchInProgress;
    uint8_t  MclkSwitchCritical;

    uint8_t  TargetMclkIndex;
    uint8_t  TargetMvddIndex;
    uint8_t  MclkSwitchResult;

    uint8_t  EnabledLevelsChange;

    uint16_t LevelResidencyCounters [SMU71_MAX_LEVELS_MEMORY];
    uint16_t LevelSwitchCounters [SMU71_MAX_LEVELS_MEMORY];

    void     (*TargetStateCalculator)(uint8_t);
    void     (*SavedTargetStateCalculator)(uint8_t);

    uint16_t AutoDpmInterval;
    uint16_t AutoDpmRange;

    uint16_t  MclkSwitchingTime;
    uint8_t padding[2];
};

typedef struct SMU71_MclkDpmScoreboard SMU71_MclkDpmScoreboard;

struct SMU71_UlvScoreboard
{
    uint8_t     EnterUlv;
    uint8_t     ExitUlv;
    uint8_t     UlvActive;
    uint8_t     WaitingForUlv;
    uint8_t     UlvEnable;
    uint8_t     UlvRunning;
    uint8_t     UlvMasterEnable;
    uint8_t     padding;
    uint32_t    UlvAbortedCount;
    uint32_t    UlvTimeStamp;
};

typedef struct SMU71_UlvScoreboard SMU71_UlvScoreboard;

struct SMU71_VddGfxScoreboard
{
    uint8_t     VddGfxEnable;
    uint8_t     VddGfxActive;
    uint8_t     padding[2];

    uint32_t    VddGfxEnteredCount;
    uint32_t    VddGfxAbortedCount;
};

typedef struct SMU71_VddGfxScoreboard SMU71_VddGfxScoreboard;

struct SMU71_AcpiScoreboard {
  uint32_t SavedInterruptMask[2];
  uint8_t LastACPIRequest;
  uint8_t CgBifResp;
  uint8_t RequestType;
  uint8_t Padding;
  SMU71_Discrete_ACPILevel D0Level;
};

typedef struct SMU71_AcpiScoreboard SMU71_AcpiScoreboard;


struct SMU71_Discrete_PmFuses {
  // dw0-dw1
  uint8_t BapmVddCVidHiSidd[8];

  // dw2-dw3
  uint8_t BapmVddCVidLoSidd[8];

  // dw4-dw5
  uint8_t VddCVid[8];

  // dw6
  uint8_t SviLoadLineEn;
  uint8_t SviLoadLineVddC;
  uint8_t SviLoadLineTrimVddC;
  uint8_t SviLoadLineOffsetVddC;

  // dw7
  uint16_t TDC_VDDC_PkgLimit;
  uint8_t TDC_VDDC_ThrottleReleaseLimitPerc;
  uint8_t TDC_MAWt;

  // dw8
  uint8_t TdcWaterfallCtl;
  uint8_t LPMLTemperatureMin;
  uint8_t LPMLTemperatureMax;
  uint8_t Reserved;

  // dw9-dw12
  uint8_t LPMLTemperatureScaler[16];

  // dw13-dw14
  int16_t FuzzyFan_ErrorSetDelta;
  int16_t FuzzyFan_ErrorRateSetDelta;
  int16_t FuzzyFan_PwmSetDelta;
  uint16_t Reserved6;

  // dw15
  uint8_t GnbLPML[16];

  // dw15
  uint8_t GnbLPMLMaxVid;
  uint8_t GnbLPMLMinVid;
  uint8_t Reserved1[2];

  // dw16
  uint16_t BapmVddCBaseLeakageHiSidd;
  uint16_t BapmVddCBaseLeakageLoSidd;
};

typedef struct SMU71_Discrete_PmFuses SMU71_Discrete_PmFuses;

struct SMU71_Discrete_Log_Header_Table {
  uint32_t    version;
  uint32_t    asic_id;
  uint16_t    flags;
  uint16_t    entry_size;
  uint32_t    total_size;
  uint32_t    num_of_entries;
  uint8_t     type;
  uint8_t     mode;
  uint8_t     filler_0[2];
  uint32_t    filler_1[2];
};

typedef struct SMU71_Discrete_Log_Header_Table SMU71_Discrete_Log_Header_Table;

struct SMU71_Discrete_Log_Cntl {
    uint8_t             Enabled;
    uint8_t             Type;
    uint8_t             padding[2];
    uint32_t            BufferSize;
    uint32_t            SamplesLogged;
    uint32_t            SampleSize;
    uint32_t            AddrL;
    uint32_t            AddrH;
};

typedef struct SMU71_Discrete_Log_Cntl SMU71_Discrete_Log_Cntl;

#if defined SMU__DGPU_ONLY
  #define CAC_ACC_NW_NUM_OF_SIGNALS 83
#endif


struct SMU71_Discrete_Cac_Collection_Table {
  uint32_t temperature;
  uint32_t cac_acc_nw[CAC_ACC_NW_NUM_OF_SIGNALS];
  uint32_t filler[4];
};

typedef struct SMU71_Discrete_Cac_Collection_Table SMU71_Discrete_Cac_Collection_Table;

struct SMU71_Discrete_Cac_Verification_Table {
  uint32_t VddcTotalPower;
  uint32_t VddcLeakagePower;
  uint32_t VddcConstantPower;
  uint32_t VddcGfxDynamicPower;
  uint32_t VddcUvdDynamicPower;
  uint32_t VddcVceDynamicPower;
  uint32_t VddcAcpDynamicPower;
  uint32_t VddcPcieDynamicPower;
  uint32_t VddcDceDynamicPower;
  uint32_t VddcCurrent;
  uint32_t VddcVoltage;
  uint32_t VddciTotalPower;
  uint32_t VddciLeakagePower;
  uint32_t VddciConstantPower;
  uint32_t VddciDynamicPower;
  uint32_t Vddr1TotalPower;
  uint32_t Vddr1LeakagePower;
  uint32_t Vddr1ConstantPower;
  uint32_t Vddr1DynamicPower;
  uint32_t spare[8];
  uint32_t temperature;
};

typedef struct SMU71_Discrete_Cac_Verification_Table SMU71_Discrete_Cac_Verification_Table;

#if !defined(SMC_MICROCODE)
#pragma pack(pop)
#endif


#endif

