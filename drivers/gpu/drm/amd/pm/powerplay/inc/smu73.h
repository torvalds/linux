/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#ifndef _SMU73_H_
#define _SMU73_H_

#pragma pack(push, 1)
enum SID_OPTION {
  SID_OPTION_HI,
  SID_OPTION_LO,
  SID_OPTION_COUNT
};

enum Poly3rdOrderCoeff {
    LEAKAGE_TEMPERATURE_SCALAR,
    LEAKAGE_VOLTAGE_SCALAR,
    DYNAMIC_VOLTAGE_SCALAR,
    POLY_3RD_ORDER_COUNT
};

struct SMU7_Poly3rdOrder_Data
{
    int32_t a;
    int32_t b;
    int32_t c;
    int32_t d;
    uint8_t a_shift;
    uint8_t b_shift;
    uint8_t c_shift;
    uint8_t x_shift;
};

typedef struct SMU7_Poly3rdOrder_Data SMU7_Poly3rdOrder_Data;

struct Power_Calculator_Data
{
  uint16_t NoLoadVoltage;
  uint16_t LoadVoltage;
  uint16_t Resistance;
  uint16_t Temperature;
  uint16_t BaseLeakage;
  uint16_t LkgTempScalar;
  uint16_t LkgVoltScalar;
  uint16_t LkgAreaScalar;
  uint16_t LkgPower;
  uint16_t DynVoltScalar;
  uint32_t Cac;
  uint32_t DynPower;
  uint32_t TotalCurrent;
  uint32_t TotalPower;
};

typedef struct Power_Calculator_Data PowerCalculatorData_t;

struct Gc_Cac_Weight_Data
{
  uint8_t index;
  uint32_t value;
};

typedef struct Gc_Cac_Weight_Data GcCacWeight_Data;


typedef struct {
  uint32_t high;
  uint32_t low;
} data_64_t;

typedef struct {
  data_64_t high;
  data_64_t low;
} data_128_t;

#define SMU__NUM_SCLK_DPM_STATE  8
#define SMU__NUM_MCLK_DPM_LEVELS 4
#define SMU__NUM_LCLK_DPM_LEVELS 8
#define SMU__NUM_PCIE_DPM_LEVELS 8

#define SMU7_CONTEXT_ID_SMC        1
#define SMU7_CONTEXT_ID_VBIOS      2

#define SMU73_MAX_LEVELS_VDDC            16
#define SMU73_MAX_LEVELS_VDDGFX          16
#define SMU73_MAX_LEVELS_VDDCI           8
#define SMU73_MAX_LEVELS_MVDD            4

#define SMU_MAX_SMIO_LEVELS              4

#define SMU73_MAX_LEVELS_GRAPHICS        SMU__NUM_SCLK_DPM_STATE   // SCLK + SQ DPM + ULV
#define SMU73_MAX_LEVELS_MEMORY          SMU__NUM_MCLK_DPM_LEVELS   // MCLK Levels DPM
#define SMU73_MAX_LEVELS_GIO             SMU__NUM_LCLK_DPM_LEVELS  // LCLK Levels
#define SMU73_MAX_LEVELS_LINK            SMU__NUM_PCIE_DPM_LEVELS  // PCIe speed and number of lanes.
#define SMU73_MAX_LEVELS_UVD             8   // VCLK/DCLK levels for UVD.
#define SMU73_MAX_LEVELS_VCE             8   // ECLK levels for VCE.
#define SMU73_MAX_LEVELS_ACP             8   // ACLK levels for ACP.
#define SMU73_MAX_LEVELS_SAMU            8   // SAMCLK levels for SAMU.
#define SMU73_MAX_ENTRIES_SMIO           32  // Number of entries in SMIO table.

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

// Virtualization Defines
#define CG_XDMA_MASK  0x1
#define CG_XDMA_SHIFT 0
#define CG_UVD_MASK   0x2
#define CG_UVD_SHIFT  1
#define CG_VCE_MASK   0x4
#define CG_VCE_SHIFT  2
#define CG_SAMU_MASK  0x8
#define CG_SAMU_SHIFT 3
#define CG_GFX_MASK   0x10
#define CG_GFX_SHIFT  4
#define CG_SDMA_MASK  0x20
#define CG_SDMA_SHIFT 5
#define CG_HDP_MASK   0x40
#define CG_HDP_SHIFT  6
#define CG_MC_MASK    0x80
#define CG_MC_SHIFT   7
#define CG_DRM_MASK   0x100
#define CG_DRM_SHIFT  8
#define CG_ROM_MASK   0x200
#define CG_ROM_SHIFT  9
#define CG_BIF_MASK   0x400
#define CG_BIF_SHIFT  10

#define SMU73_DTE_ITERATIONS 5
#define SMU73_DTE_SOURCES 3
#define SMU73_DTE_SINKS 1
#define SMU73_NUM_CPU_TES 0
#define SMU73_NUM_GPU_TES 1
#define SMU73_NUM_NON_TES 2
#define SMU73_DTE_FAN_SCALAR_MIN 0x100
#define SMU73_DTE_FAN_SCALAR_MAX 0x166
#define SMU73_DTE_FAN_TEMP_MAX 93
#define SMU73_DTE_FAN_TEMP_MIN 83

#define SMU73_THERMAL_INPUT_LOOP_COUNT 6
#define SMU73_THERMAL_CLAMP_MODE_COUNT 8


struct SMU7_HystController_Data
{
    uint16_t waterfall_up;
    uint16_t waterfall_down;
    uint16_t waterfall_limit;
    uint16_t release_cnt;
    uint16_t release_limit;
    uint16_t spare;
};

typedef struct SMU7_HystController_Data SMU7_HystController_Data;

struct SMU73_PIDController
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

typedef struct SMU73_PIDController SMU73_PIDController;

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

    uint8_t  spare2;
    uint8_t  EnabledLevelsChange;
    uint8_t  DteClampMode;
    uint8_t  FpsClampMode;

    uint16_t LevelResidencyCounters [SMU73_MAX_LEVELS_GRAPHICS];
    uint16_t LevelSwitchCounters [SMU73_MAX_LEVELS_GRAPHICS];

    void     (*TargetStateCalculator)(uint8_t);
    void     (*SavedTargetStateCalculator)(uint8_t);

    uint16_t AutoDpmInterval;
    uint16_t AutoDpmRange;

    uint8_t  FpsEnabled;
    uint8_t  MaxPerfLevel;
    uint8_t  AllowLowClkInterruptToHost;
    uint8_t  FpsRunning;

    uint32_t MaxAllowedFrequency;

    uint32_t FilteredSclkFrequency;
    uint32_t LastSclkFrequency;
    uint32_t FilteredSclkFrequencyCnt;

    uint8_t  LedEnable;
    uint8_t  LedPin0;
    uint8_t  LedPin1;
    uint8_t  LedPin2;
    uint32_t LedAndMask;

    uint16_t FpsAlpha;
    uint16_t DeltaTime;
    uint32_t CurrentFps;
    uint32_t FilteredFps;
    uint32_t FrameCount;
    uint32_t FrameCountLast;
    uint16_t FpsTargetScalar;
    uint16_t FpsWaterfallLimitScalar;
    uint16_t FpsAlphaScalar;
    uint16_t spare8;
    SMU7_HystController_Data HystControllerData;
};

typedef struct SMU7_LocalDpmScoreboard SMU7_LocalDpmScoreboard;

#define SMU7_MAX_VOLTAGE_CLIENTS 12

typedef uint8_t (*VoltageChangeHandler_t)(uint16_t, uint8_t);

#define VDDC_MASK    0x00007FFF
#define VDDC_SHIFT   0
#define VDDCI_MASK   0x3FFF8000
#define VDDCI_SHIFT  15
#define PHASES_MASK  0xC0000000
#define PHASES_SHIFT 30

typedef uint32_t SMU_VoltageLevel;

struct SMU7_VoltageScoreboard
{
    SMU_VoltageLevel TargetVoltage;
    uint16_t MaxVid;
    uint8_t  HighestVidOffset;
    uint8_t  CurrentVidOffset;

    uint16_t CurrentVddc;
    uint16_t CurrentVddci;


    uint8_t  ControllerBusy;
    uint8_t  CurrentVid;
    uint8_t  CurrentVddciVid;
    uint8_t  padding;

    SMU_VoltageLevel RequestedVoltage[SMU7_MAX_VOLTAGE_CLIENTS];
    SMU_VoltageLevel TargetVoltageState;
    uint8_t  EnabledRequest[SMU7_MAX_VOLTAGE_CLIENTS];

    uint8_t  padding2;
    uint8_t  padding3;
    uint8_t  ControllerEnable;
    uint8_t  ControllerRunning;
    uint16_t CurrentStdVoltageHiSidd;
    uint16_t CurrentStdVoltageLoSidd;
    uint8_t  OverrideVoltage;
    uint8_t  padding4;
    uint8_t  padding5;
    uint8_t  CurrentPhases;

    VoltageChangeHandler_t ChangeVddc;

    VoltageChangeHandler_t ChangeVddci;
    VoltageChangeHandler_t ChangePhase;
    VoltageChangeHandler_t ChangeMvdd;

    VoltageChangeHandler_t functionLinks[6];

    uint16_t * VddcFollower1;

    int16_t  Driver_OD_RequestedVidOffset1;
    int16_t  Driver_OD_RequestedVidOffset2;

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
    uint32_t GpuPower;

    uint32_t VddcPower;
    uint32_t VddcVoltage;
    uint32_t VddcCurrent;

    uint32_t MvddPower;
    uint32_t MvddVoltage;
    uint32_t MvddCurrent;

    uint32_t RocPower;

    uint16_t Telemetry_1_slope;
    uint16_t Telemetry_2_slope;
    int32_t  Telemetry_1_offset;
    int32_t  Telemetry_2_offset;
};
typedef struct SMU7_PowerScoreboard SMU7_PowerScoreboard;

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
struct SMU73_SoftRegisters
{
    uint32_t        RefClockFrequency;
    uint32_t        PmTimerPeriod;
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

    uint32_t        AverageGraphicsActivity;
    uint32_t        AverageMemoryActivity;
    uint32_t        AverageGioActivity;

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
    uint32_t        UlvEnterCount;
    uint32_t        UlvTime;
    uint32_t        UcodeLoadStatus;
    uint32_t        Reserved[2];

};

typedef struct SMU73_SoftRegisters SMU73_SoftRegisters;

struct SMU73_Firmware_Header
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
    uint32_t ClockStretcherTable;
    uint32_t Reserved[41];
    uint32_t Signature;
};

typedef struct SMU73_Firmware_Header SMU73_Firmware_Header;

#define SMU7_FIRMWARE_HEADER_LOCATION 0x20000

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


#define MC_BLOCK_COUNT 1
#define CPL_BLOCK_COUNT 5
#define SE_BLOCK_COUNT 15
#define GC_BLOCK_COUNT 24

struct SMU7_Local_Cac {
  uint8_t BlockId;
  uint8_t SignalId;
  uint8_t Threshold;
  uint8_t Padding;
};

typedef struct SMU7_Local_Cac SMU7_Local_Cac;

struct SMU7_Local_Cac_Table {

  SMU7_Local_Cac CplLocalCac[CPL_BLOCK_COUNT];
  SMU7_Local_Cac McLocalCac[MC_BLOCK_COUNT];
  SMU7_Local_Cac SeLocalCac[SE_BLOCK_COUNT];
  SMU7_Local_Cac GcLocalCac[GC_BLOCK_COUNT];
};

typedef struct SMU7_Local_Cac_Table SMU7_Local_Cac_Table;

#if !defined(SMC_MICROCODE)
#pragma pack(pop)
#endif

// Description of Clock Gating bitmask for Tonga:
// System Clock Gating
#define CG_SYS_BITMASK_FIRST_BIT      0  // First bit of Sys CG bitmask
#define CG_SYS_BITMASK_LAST_BIT       9  // Last bit of Sys CG bitmask
#define CG_SYS_BIF_MGLS_SHIFT         0
#define CG_SYS_ROM_SHIFT              1
#define CG_SYS_MC_MGCG_SHIFT          2
#define CG_SYS_MC_MGLS_SHIFT          3
#define CG_SYS_SDMA_MGCG_SHIFT        4
#define CG_SYS_SDMA_MGLS_SHIFT        5
#define CG_SYS_DRM_MGCG_SHIFT         6
#define CG_SYS_HDP_MGCG_SHIFT         7
#define CG_SYS_HDP_MGLS_SHIFT         8
#define CG_SYS_DRM_MGLS_SHIFT         9

#define CG_SYS_BIF_MGLS_MASK          0x1
#define CG_SYS_ROM_MASK               0x2
#define CG_SYS_MC_MGCG_MASK           0x4
#define CG_SYS_MC_MGLS_MASK           0x8
#define CG_SYS_SDMA_MGCG_MASK         0x10
#define CG_SYS_SDMA_MGLS_MASK         0x20
#define CG_SYS_DRM_MGCG_MASK          0x40
#define CG_SYS_HDP_MGCG_MASK          0x80
#define CG_SYS_HDP_MGLS_MASK          0x100
#define CG_SYS_DRM_MGLS_MASK          0x200

// Graphics Clock Gating
#define CG_GFX_BITMASK_FIRST_BIT      16 // First bit of Gfx CG bitmask
#define CG_GFX_BITMASK_LAST_BIT       20 // Last bit of Gfx CG bitmask
#define CG_GFX_CGCG_SHIFT             16
#define CG_GFX_CGLS_SHIFT             17
#define CG_CPF_MGCG_SHIFT             18
#define CG_RLC_MGCG_SHIFT             19
#define CG_GFX_OTHERS_MGCG_SHIFT      20

#define CG_GFX_CGCG_MASK              0x00010000
#define CG_GFX_CGLS_MASK              0x00020000
#define CG_CPF_MGCG_MASK              0x00040000
#define CG_RLC_MGCG_MASK              0x00080000
#define CG_GFX_OTHERS_MGCG_MASK       0x00100000



// Voltage Regulator Configuration
// VR Config info is contained in dpmTable.VRConfig

#define VRCONF_VDDC_MASK         0x000000FF
#define VRCONF_VDDC_SHIFT        0
#define VRCONF_VDDGFX_MASK       0x0000FF00
#define VRCONF_VDDGFX_SHIFT      8
#define VRCONF_VDDCI_MASK        0x00FF0000
#define VRCONF_VDDCI_SHIFT       16
#define VRCONF_MVDD_MASK         0xFF000000
#define VRCONF_MVDD_SHIFT        24

#define VR_MERGED_WITH_VDDC      0
#define VR_SVI2_PLANE_1          1
#define VR_SVI2_PLANE_2          2
#define VR_SMIO_PATTERN_1        3
#define VR_SMIO_PATTERN_2        4
#define VR_STATIC_VOLTAGE        5

// Clock Stretcher Configuration

#define CLOCK_STRETCHER_MAX_ENTRIES 0x4
#define CKS_LOOKUPTable_MAX_ENTRIES 0x4

// The 'settings' field is subdivided in the following way:
#define CLOCK_STRETCHER_SETTING_DDT_MASK             0x01
#define CLOCK_STRETCHER_SETTING_DDT_SHIFT            0x0
#define CLOCK_STRETCHER_SETTING_STRETCH_AMOUNT_MASK  0x1E
#define CLOCK_STRETCHER_SETTING_STRETCH_AMOUNT_SHIFT 0x1
#define CLOCK_STRETCHER_SETTING_ENABLE_MASK          0x80
#define CLOCK_STRETCHER_SETTING_ENABLE_SHIFT         0x7

struct SMU_ClockStretcherDataTableEntry {
  uint8_t minVID;
  uint8_t maxVID;


  uint16_t setting;
};
typedef struct SMU_ClockStretcherDataTableEntry SMU_ClockStretcherDataTableEntry;

struct SMU_ClockStretcherDataTable {
  SMU_ClockStretcherDataTableEntry ClockStretcherDataTableEntry[CLOCK_STRETCHER_MAX_ENTRIES];
};
typedef struct SMU_ClockStretcherDataTable SMU_ClockStretcherDataTable;

struct SMU_CKS_LOOKUPTableEntry {
  uint16_t minFreq;
  uint16_t maxFreq;

  uint8_t setting;
  uint8_t padding[3];
};
typedef struct SMU_CKS_LOOKUPTableEntry SMU_CKS_LOOKUPTableEntry;

struct SMU_CKS_LOOKUPTable {
  SMU_CKS_LOOKUPTableEntry CKS_LOOKUPTableEntry[CKS_LOOKUPTable_MAX_ENTRIES];
};
typedef struct SMU_CKS_LOOKUPTable SMU_CKS_LOOKUPTable;

struct AgmAvfsData_t {
  uint16_t avgPsmCount[28];
  uint16_t minPsmCount[28];
};
typedef struct AgmAvfsData_t AgmAvfsData_t;

// AVFS DEFINES

enum VFT_COLUMNS {
  SCLK0,
  SCLK1,
  SCLK2,
  SCLK3,
  SCLK4,
  SCLK5,
  SCLK6,
  SCLK7,

  NUM_VFT_COLUMNS
};

#define TEMP_RANGE_MAXSTEPS 12
struct VFT_CELL_t {
  uint16_t Voltage;
};

typedef struct VFT_CELL_t VFT_CELL_t;

struct VFT_TABLE_t {
  VFT_CELL_t    Cell[TEMP_RANGE_MAXSTEPS][NUM_VFT_COLUMNS];
  uint16_t      AvfsGbv [NUM_VFT_COLUMNS];
  uint16_t      BtcGbv  [NUM_VFT_COLUMNS];
  uint16_t      Temperature [TEMP_RANGE_MAXSTEPS];

  uint8_t       NumTemperatureSteps;
  uint8_t       padding[3];
};
typedef struct VFT_TABLE_t VFT_TABLE_t;

#endif
