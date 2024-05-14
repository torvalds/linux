/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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

#ifndef SMU7_DISCRETE_H
#define SMU7_DISCRETE_H

#include "smu7.h"

#pragma pack(push, 1)

#define SMU7_DTE_ITERATIONS 5
#define SMU7_DTE_SOURCES 3
#define SMU7_DTE_SINKS 1
#define SMU7_NUM_CPU_TES 0
#define SMU7_NUM_GPU_TES 1
#define SMU7_NUM_NON_TES 2

struct SMU7_SoftRegisters
{
    uint32_t        RefClockFrequency;
    uint32_t        PmTimerP;
    uint32_t        FeatureEnables;
    uint32_t        PreVBlankGap;
    uint32_t        VBlankTimeout;
    uint32_t        TrainTimeGap;

    uint32_t        MvddSwitchTime;
    uint32_t        LongestAcpiTrainTime;
    uint32_t        AcpiDelay;
    uint32_t        G5TrainTime;
    uint32_t        DelayMpllPwron;
    uint32_t        VoltageChangeTimeout;
    uint32_t        HandshakeDisables;

    uint8_t         DisplayPhy1Config;
    uint8_t         DisplayPhy2Config;
    uint8_t         DisplayPhy3Config;
    uint8_t         DisplayPhy4Config;

    uint8_t         DisplayPhy5Config;
    uint8_t         DisplayPhy6Config;
    uint8_t         DisplayPhy7Config;
    uint8_t         DisplayPhy8Config;

    uint32_t        AverageGraphicsA;
    uint32_t        AverageMemoryA;
    uint32_t        AverageGioA;

    uint8_t         SClkDpmEnabledLevels;
    uint8_t         MClkDpmEnabledLevels;
    uint8_t         LClkDpmEnabledLevels;
    uint8_t         PCIeDpmEnabledLevels;

    uint8_t         UVDDpmEnabledLevels;
    uint8_t         SAMUDpmEnabledLevels;
    uint8_t         ACPDpmEnabledLevels;
    uint8_t         VCEDpmEnabledLevels;

    uint32_t        DRAM_LOG_ADDR_H;
    uint32_t        DRAM_LOG_ADDR_L;
    uint32_t        DRAM_LOG_PHY_ADDR_H;
    uint32_t        DRAM_LOG_PHY_ADDR_L;
    uint32_t        DRAM_LOG_BUFF_SIZE;
    uint32_t        UlvEnterC;
    uint32_t        UlvTime;
    uint32_t        Reserved[3];

};

typedef struct SMU7_SoftRegisters SMU7_SoftRegisters;

struct SMU7_Discrete_VoltageLevel
{
    uint16_t    Voltage;
    uint16_t    StdVoltageHiSidd;
    uint16_t    StdVoltageLoSidd;
    uint8_t     Smio;
    uint8_t     padding;
};

typedef struct SMU7_Discrete_VoltageLevel SMU7_Discrete_VoltageLevel;

struct SMU7_Discrete_GraphicsLevel
{
    uint32_t    Flags;
    uint32_t    MinVddc;
    uint32_t    MinVddcPhases;

    uint32_t    SclkFrequency;

    uint8_t     padding1[2];
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
    uint8_t     UpH;
    uint8_t     DownH;
    uint8_t     VoltageDownH;
    uint8_t     PowerThrottle;
    uint8_t     DeepSleepDivId;
    uint8_t     padding[3];
};

typedef struct SMU7_Discrete_GraphicsLevel SMU7_Discrete_GraphicsLevel;

struct SMU7_Discrete_ACPILevel
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

typedef struct SMU7_Discrete_ACPILevel SMU7_Discrete_ACPILevel;

struct SMU7_Discrete_Ulv
{
    uint32_t    CcPwrDynRm;
    uint32_t    CcPwrDynRm1;
    uint16_t    VddcOffset;
    uint8_t     VddcOffsetVid;
    uint8_t     VddcPhase;
    uint32_t    Reserved;
};

typedef struct SMU7_Discrete_Ulv SMU7_Discrete_Ulv;

struct SMU7_Discrete_MemoryLevel
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

    uint8_t     UpH;
    uint8_t     DownH;
    uint8_t     VoltageDownH;
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

typedef struct SMU7_Discrete_MemoryLevel SMU7_Discrete_MemoryLevel;

struct SMU7_Discrete_LinkLevel
{
    uint8_t     PcieGenSpeed;
    uint8_t     PcieLaneCount;
    uint8_t     EnabledForActivity;
    uint8_t     Padding;
    uint32_t    DownT;
    uint32_t    UpT;
    uint32_t    Reserved;
};

typedef struct SMU7_Discrete_LinkLevel SMU7_Discrete_LinkLevel;


struct SMU7_Discrete_MCArbDramTimingTableEntry
{
    uint32_t McArbDramTiming;
    uint32_t McArbDramTiming2;
    uint8_t  McArbBurstTime;
    uint8_t  padding[3];
};

typedef struct SMU7_Discrete_MCArbDramTimingTableEntry SMU7_Discrete_MCArbDramTimingTableEntry;

struct SMU7_Discrete_MCArbDramTimingTable
{
    SMU7_Discrete_MCArbDramTimingTableEntry entries[SMU__NUM_SCLK_DPM_STATE][SMU__NUM_MCLK_DPM_LEVELS];
};

typedef struct SMU7_Discrete_MCArbDramTimingTable SMU7_Discrete_MCArbDramTimingTable;

struct SMU7_Discrete_UvdLevel
{
    uint32_t VclkFrequency;
    uint32_t DclkFrequency;
    uint16_t MinVddc;
    uint8_t  MinVddcPhases;
    uint8_t  VclkDivider;
    uint8_t  DclkDivider;
    uint8_t  padding[3];
};

typedef struct SMU7_Discrete_UvdLevel SMU7_Discrete_UvdLevel;

struct SMU7_Discrete_ExtClkLevel
{
    uint32_t Frequency;
    uint16_t MinVoltage;
    uint8_t  MinPhases;
    uint8_t  Divider;
};

typedef struct SMU7_Discrete_ExtClkLevel SMU7_Discrete_ExtClkLevel;

struct SMU7_Discrete_StateInfo
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

typedef struct SMU7_Discrete_StateInfo SMU7_Discrete_StateInfo;


struct SMU7_Discrete_DpmTable
{
    SMU7_PIDController                  GraphicsPIDController;
    SMU7_PIDController                  MemoryPIDController;
    SMU7_PIDController                  LinkPIDController;

    uint32_t                            SystemFlags;


    uint32_t                            SmioMaskVddcVid;
    uint32_t                            SmioMaskVddcPhase;
    uint32_t                            SmioMaskVddciVid;
    uint32_t                            SmioMaskMvddVid;

    uint32_t                            VddcLevelCount;
    uint32_t                            VddciLevelCount;
    uint32_t                            MvddLevelCount;

    SMU7_Discrete_VoltageLevel          VddcLevel               [SMU7_MAX_LEVELS_VDDC];
//    SMU7_Discrete_VoltageLevel          VddcStandardReference   [SMU7_MAX_LEVELS_VDDC];
    SMU7_Discrete_VoltageLevel          VddciLevel              [SMU7_MAX_LEVELS_VDDCI];
    SMU7_Discrete_VoltageLevel          MvddLevel               [SMU7_MAX_LEVELS_MVDD];

    uint8_t                             GraphicsDpmLevelCount;
    uint8_t                             MemoryDpmLevelCount;
    uint8_t                             LinkLevelCount;
    uint8_t                             UvdLevelCount;
    uint8_t                             VceLevelCount;
    uint8_t                             AcpLevelCount;
    uint8_t                             SamuLevelCount;
    uint8_t                             MasterDeepSleepControl;
    uint32_t                            Reserved[5];
//    uint32_t                            SamuDefaultLevel;

    SMU7_Discrete_GraphicsLevel         GraphicsLevel           [SMU7_MAX_LEVELS_GRAPHICS];
    SMU7_Discrete_MemoryLevel           MemoryACPILevel;
    SMU7_Discrete_MemoryLevel           MemoryLevel             [SMU7_MAX_LEVELS_MEMORY];
    SMU7_Discrete_LinkLevel             LinkLevel               [SMU7_MAX_LEVELS_LINK];
    SMU7_Discrete_ACPILevel             ACPILevel;
    SMU7_Discrete_UvdLevel              UvdLevel                [SMU7_MAX_LEVELS_UVD];
    SMU7_Discrete_ExtClkLevel           VceLevel                [SMU7_MAX_LEVELS_VCE];
    SMU7_Discrete_ExtClkLevel           AcpLevel                [SMU7_MAX_LEVELS_ACP];
    SMU7_Discrete_ExtClkLevel           SamuLevel               [SMU7_MAX_LEVELS_SAMU];
    SMU7_Discrete_Ulv                   Ulv;

    uint32_t                            SclkStepSize;
    uint32_t                            Smio                    [SMU7_MAX_ENTRIES_SMIO];

    uint8_t                             UvdBootLevel;
    uint8_t                             VceBootLevel;
    uint8_t                             AcpBootLevel;
    uint8_t                             SamuBootLevel;

    uint8_t                             UVDInterval;
    uint8_t                             VCEInterval;
    uint8_t                             ACPInterval;
    uint8_t                             SAMUInterval;

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
    uint16_t                            VddcVddciDelta;

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

    uint16_t                            PPM_PkgPwrLimit;
    uint16_t                            PPM_TemperatureLimit;

    uint16_t                            DefaultTdp;
    uint16_t                            TargetTdp;

    uint16_t                            FpsHighT;
    uint16_t                            FpsLowT;

    uint16_t                            BAPMTI_R  [SMU7_DTE_ITERATIONS][SMU7_DTE_SOURCES][SMU7_DTE_SINKS];
    uint16_t                            BAPMTI_RC [SMU7_DTE_ITERATIONS][SMU7_DTE_SOURCES][SMU7_DTE_SINKS];

    uint8_t                             DTEAmbientTempBase;
    uint8_t                             DTETjOffset;
    uint8_t                             GpuTjMax;
    uint8_t                             GpuTjHyst;

    uint16_t                            BootVddc;
    uint16_t                            BootVddci;

    uint16_t                            BootMVdd;
    uint16_t                            padding;

    uint32_t                            BAPM_TEMP_GRADIENT;

    uint32_t                            LowSclkInterruptT;
};

typedef struct SMU7_Discrete_DpmTable SMU7_Discrete_DpmTable;

#define SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE 16
#define SMU7_DISCRETE_MC_REGISTER_ARRAY_SET_COUNT SMU7_MAX_LEVELS_MEMORY

struct SMU7_Discrete_MCRegisterAddress
{
    uint16_t s0;
    uint16_t s1;
};

typedef struct SMU7_Discrete_MCRegisterAddress SMU7_Discrete_MCRegisterAddress;

struct SMU7_Discrete_MCRegisterSet
{
    uint32_t value[SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE];
};

typedef struct SMU7_Discrete_MCRegisterSet SMU7_Discrete_MCRegisterSet;

struct SMU7_Discrete_MCRegisters
{
    uint8_t                             last;
    uint8_t                             reserved[3];
    SMU7_Discrete_MCRegisterAddress     address[SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE];
    SMU7_Discrete_MCRegisterSet         data[SMU7_DISCRETE_MC_REGISTER_ARRAY_SET_COUNT];
};

typedef struct SMU7_Discrete_MCRegisters SMU7_Discrete_MCRegisters;

struct SMU7_Discrete_FanTable
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

typedef struct SMU7_Discrete_FanTable SMU7_Discrete_FanTable;


struct SMU7_Discrete_PmFuses {
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

  // dw9-dw10
  uint8_t BapmVddCVidHiSidd2[8];

  // dw11-dw12
  int16_t FuzzyFan_ErrorSetDelta;
  int16_t FuzzyFan_ErrorRateSetDelta;
  int16_t FuzzyFan_PwmSetDelta;
  uint16_t CalcMeasPowerBlend;

  // dw13-dw16
  uint8_t GnbLPML[16];

  // dw17
  uint8_t GnbLPMLMaxVid;
  uint8_t GnbLPMLMinVid;
  uint8_t Reserved1[2];

  // dw18
  uint16_t BapmVddCBaseLeakageHiSidd;
  uint16_t BapmVddCBaseLeakageLoSidd;
};

typedef struct SMU7_Discrete_PmFuses SMU7_Discrete_PmFuses;


#pragma pack(pop)

#endif

