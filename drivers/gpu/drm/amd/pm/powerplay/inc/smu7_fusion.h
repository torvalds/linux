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

#ifndef SMU7_FUSION_H
#define SMU7_FUSION_H

#include "smu7.h"

#pragma pack(push, 1)

#define SMU7_DTE_ITERATIONS 5
#define SMU7_DTE_SOURCES 5
#define SMU7_DTE_SINKS 3
#define SMU7_NUM_CPU_TES 2
#define SMU7_NUM_GPU_TES 1
#define SMU7_NUM_NON_TES 2

// All 'soft registers' should be uint32_t.
struct SMU7_SoftRegisters {
    uint32_t        RefClockFrequency;
    uint32_t        PmTimerP;
    uint32_t        FeatureEnables;
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

struct SMU7_Fusion_GraphicsLevel {
    uint32_t    MinVddNb;

    uint32_t    SclkFrequency;

    uint8_t     Vid;
    uint8_t     VidOffset;
    uint16_t    AT;

    uint8_t     PowerThrottle;
    uint8_t     GnbSlow;
    uint8_t     ForceNbPs1;
    uint8_t     SclkDid;

    uint8_t     DisplayWatermark;
    uint8_t     EnabledForActivity;
    uint8_t     EnabledForThrottle;
    uint8_t     UpH;

    uint8_t     DownH;
    uint8_t     VoltageDownH;
    uint8_t     DeepSleepDivId;

    uint8_t     ClkBypassCntl;

    uint32_t    reserved;
};

typedef struct SMU7_Fusion_GraphicsLevel SMU7_Fusion_GraphicsLevel;

struct SMU7_Fusion_GIOLevel {
    uint8_t     EnabledForActivity;
    uint8_t     LclkDid;
    uint8_t     Vid;
    uint8_t     VoltageDownH;

    uint32_t    MinVddNb;

    uint16_t    ResidencyCounter;
    uint8_t     UpH;
    uint8_t     DownH;

    uint32_t    LclkFrequency;

    uint8_t     ActivityLevel;
    uint8_t     EnabledForThrottle;

    uint8_t     ClkBypassCntl;

    uint8_t     padding;
};

typedef struct SMU7_Fusion_GIOLevel SMU7_Fusion_GIOLevel;

// UVD VCLK/DCLK state (level) definition.
struct SMU7_Fusion_UvdLevel {
    uint32_t VclkFrequency;
    uint32_t DclkFrequency;
    uint16_t MinVddNb;
    uint8_t  VclkDivider;
    uint8_t  DclkDivider;

    uint8_t     VClkBypassCntl;
    uint8_t     DClkBypassCntl;

    uint8_t     padding[2];

};

typedef struct SMU7_Fusion_UvdLevel SMU7_Fusion_UvdLevel;

// Clocks for other external blocks (VCE, ACP, SAMU).
struct SMU7_Fusion_ExtClkLevel {
    uint32_t Frequency;
    uint16_t MinVoltage;
    uint8_t  Divider;
    uint8_t  ClkBypassCntl;

    uint32_t Reserved;
};
typedef struct SMU7_Fusion_ExtClkLevel SMU7_Fusion_ExtClkLevel;

struct SMU7_Fusion_ACPILevel {
    uint32_t    Flags;
    uint32_t    MinVddNb;
    uint32_t    SclkFrequency;
    uint8_t     SclkDid;
    uint8_t     GnbSlow;
    uint8_t     ForceNbPs1;
    uint8_t     DisplayWatermark;
    uint8_t     DeepSleepDivId;
    uint8_t     padding[3];
};

typedef struct SMU7_Fusion_ACPILevel SMU7_Fusion_ACPILevel;

struct SMU7_Fusion_NbDpm {
    uint8_t DpmXNbPsHi;
    uint8_t DpmXNbPsLo;
    uint8_t Dpm0PgNbPsHi;
    uint8_t Dpm0PgNbPsLo;
    uint8_t EnablePsi1;
    uint8_t SkipDPM0;
    uint8_t SkipPG;
    uint8_t Hysteresis;
    uint8_t EnableDpmPstatePoll;
    uint8_t padding[3];
};

typedef struct SMU7_Fusion_NbDpm SMU7_Fusion_NbDpm;

struct SMU7_Fusion_StateInfo {
    uint32_t SclkFrequency;
    uint32_t LclkFrequency;
    uint32_t VclkFrequency;
    uint32_t DclkFrequency;
    uint32_t SamclkFrequency;
    uint32_t AclkFrequency;
    uint32_t EclkFrequency;
    uint8_t  DisplayWatermark;
    uint8_t  McArbIndex;
    int8_t   SclkIndex;
    int8_t   MclkIndex;
};

typedef struct SMU7_Fusion_StateInfo SMU7_Fusion_StateInfo;

struct SMU7_Fusion_DpmTable {
    uint32_t                            SystemFlags;

    SMU7_PIDController                  GraphicsPIDController;
    SMU7_PIDController                  GioPIDController;

    uint8_t                            GraphicsDpmLevelCount;
    uint8_t                            GIOLevelCount;
    uint8_t                            UvdLevelCount;
    uint8_t                            VceLevelCount;

    uint8_t                            AcpLevelCount;
    uint8_t                            SamuLevelCount;
    uint16_t                           FpsHighT;

    SMU7_Fusion_GraphicsLevel         GraphicsLevel[SMU__NUM_SCLK_DPM_STATE];
    SMU7_Fusion_ACPILevel             ACPILevel;
    SMU7_Fusion_UvdLevel              UvdLevel[SMU7_MAX_LEVELS_UVD];
    SMU7_Fusion_ExtClkLevel           VceLevel[SMU7_MAX_LEVELS_VCE];
    SMU7_Fusion_ExtClkLevel           AcpLevel[SMU7_MAX_LEVELS_ACP];
    SMU7_Fusion_ExtClkLevel           SamuLevel[SMU7_MAX_LEVELS_SAMU];

    uint8_t                           UvdBootLevel;
    uint8_t                           VceBootLevel;
    uint8_t                           AcpBootLevel;
    uint8_t                           SamuBootLevel;
    uint8_t                           UVDInterval;
    uint8_t                           VCEInterval;
    uint8_t                           ACPInterval;
    uint8_t                           SAMUInterval;

    uint8_t                           GraphicsBootLevel;
    uint8_t                           GraphicsInterval;
    uint8_t                           GraphicsThermThrottleEnable;
    uint8_t                           GraphicsVoltageChangeEnable;

    uint8_t                           GraphicsClkSlowEnable;
    uint8_t                           GraphicsClkSlowDivider;
    uint16_t                          FpsLowT;

    uint32_t                          DisplayCac;
    uint32_t                          LowSclkInterruptT;

    uint32_t                          DRAM_LOG_ADDR_H;
    uint32_t                          DRAM_LOG_ADDR_L;
    uint32_t                          DRAM_LOG_PHY_ADDR_H;
    uint32_t                          DRAM_LOG_PHY_ADDR_L;
    uint32_t                          DRAM_LOG_BUFF_SIZE;

};

struct SMU7_Fusion_GIODpmTable {

    SMU7_Fusion_GIOLevel              GIOLevel[SMU7_MAX_LEVELS_GIO];

    SMU7_PIDController                GioPIDController;

    uint32_t                          GIOLevelCount;

    uint8_t                           Enable;
    uint8_t                           GIOVoltageChangeEnable;
    uint8_t                           GIOBootLevel;
    uint8_t                           padding;
    uint8_t                           padding1[2];
    uint8_t                           TargetState;
    uint8_t                           CurrenttState;
    uint8_t                           ThrottleOnHtc;
    uint8_t                           ThermThrottleStatus;
    uint8_t                           ThermThrottleTempSelect;
    uint8_t                           ThermThrottleEnable;
    uint16_t                          TemperatureLimitHigh;
    uint16_t                          TemperatureLimitLow;

};

typedef struct SMU7_Fusion_DpmTable SMU7_Fusion_DpmTable;
typedef struct SMU7_Fusion_GIODpmTable SMU7_Fusion_GIODpmTable;

#pragma pack(pop)

#endif

