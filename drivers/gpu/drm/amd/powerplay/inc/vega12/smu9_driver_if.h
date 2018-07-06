/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#ifndef VEGA12_SMU9_DRIVER_IF_H
#define VEGA12_SMU9_DRIVER_IF_H

/**** IMPORTANT ***
 * SMU TEAM: Always increment the interface version if
 * any structure is changed in this file
 */
#define SMU9_DRIVER_IF_VERSION 0x10

#define PPTABLE_V12_SMU_VERSION 1

#define NUM_GFXCLK_DPM_LEVELS  16
#define NUM_VCLK_DPM_LEVELS    8
#define NUM_DCLK_DPM_LEVELS    8
#define NUM_ECLK_DPM_LEVELS    8
#define NUM_MP0CLK_DPM_LEVELS  2
#define NUM_UCLK_DPM_LEVELS    4
#define NUM_SOCCLK_DPM_LEVELS  8
#define NUM_DCEFCLK_DPM_LEVELS 8
#define NUM_DISPCLK_DPM_LEVELS 8
#define NUM_PIXCLK_DPM_LEVELS  8
#define NUM_PHYCLK_DPM_LEVELS  8
#define NUM_LINK_LEVELS        2

#define MAX_GFXCLK_DPM_LEVEL  (NUM_GFXCLK_DPM_LEVELS  - 1)
#define MAX_VCLK_DPM_LEVEL    (NUM_VCLK_DPM_LEVELS    - 1)
#define MAX_DCLK_DPM_LEVEL    (NUM_DCLK_DPM_LEVELS    - 1)
#define MAX_ECLK_DPM_LEVEL    (NUM_ECLK_DPM_LEVELS    - 1)
#define MAX_MP0CLK_DPM_LEVEL  (NUM_MP0CLK_DPM_LEVELS  - 1)
#define MAX_UCLK_DPM_LEVEL    (NUM_UCLK_DPM_LEVELS    - 1)
#define MAX_SOCCLK_DPM_LEVEL  (NUM_SOCCLK_DPM_LEVELS  - 1)
#define MAX_DCEFCLK_DPM_LEVEL (NUM_DCEFCLK_DPM_LEVELS - 1)
#define MAX_DISPCLK_DPM_LEVEL (NUM_DISPCLK_DPM_LEVELS - 1)
#define MAX_PIXCLK_DPM_LEVEL  (NUM_PIXCLK_DPM_LEVELS  - 1)
#define MAX_PHYCLK_DPM_LEVEL  (NUM_PHYCLK_DPM_LEVELS  - 1)
#define MAX_LINK_LEVEL        (NUM_LINK_LEVELS        - 1)


#define PPSMC_GeminiModeNone   0
#define PPSMC_GeminiModeMaster 1
#define PPSMC_GeminiModeSlave  2


#define FEATURE_DPM_PREFETCHER_BIT      0
#define FEATURE_DPM_GFXCLK_BIT          1
#define FEATURE_DPM_UCLK_BIT            2
#define FEATURE_DPM_SOCCLK_BIT          3
#define FEATURE_DPM_UVD_BIT             4
#define FEATURE_DPM_VCE_BIT             5
#define FEATURE_ULV_BIT                 6
#define FEATURE_DPM_MP0CLK_BIT          7
#define FEATURE_DPM_LINK_BIT            8
#define FEATURE_DPM_DCEFCLK_BIT         9
#define FEATURE_DS_GFXCLK_BIT           10
#define FEATURE_DS_SOCCLK_BIT           11
#define FEATURE_DS_LCLK_BIT             12
#define FEATURE_PPT_BIT                 13
#define FEATURE_TDC_BIT                 14
#define FEATURE_THERMAL_BIT             15
#define FEATURE_GFX_PER_CU_CG_BIT       16
#define FEATURE_RM_BIT                  17
#define FEATURE_DS_DCEFCLK_BIT          18
#define FEATURE_ACDC_BIT                19
#define FEATURE_VR0HOT_BIT              20
#define FEATURE_VR1HOT_BIT              21
#define FEATURE_FW_CTF_BIT              22
#define FEATURE_LED_DISPLAY_BIT         23
#define FEATURE_FAN_CONTROL_BIT         24
#define FEATURE_GFX_EDC_BIT             25
#define FEATURE_GFXOFF_BIT              26
#define FEATURE_CG_BIT                  27
#define FEATURE_ACG_BIT                 28
#define FEATURE_SPARE_29_BIT            29
#define FEATURE_SPARE_30_BIT            30
#define FEATURE_SPARE_31_BIT            31

#define NUM_FEATURES                    32

#define FEATURE_DPM_PREFETCHER_MASK     (1 << FEATURE_DPM_PREFETCHER_BIT     )
#define FEATURE_DPM_GFXCLK_MASK         (1 << FEATURE_DPM_GFXCLK_BIT         )
#define FEATURE_DPM_UCLK_MASK           (1 << FEATURE_DPM_UCLK_BIT           )
#define FEATURE_DPM_SOCCLK_MASK         (1 << FEATURE_DPM_SOCCLK_BIT         )
#define FEATURE_DPM_UVD_MASK            (1 << FEATURE_DPM_UVD_BIT            )
#define FEATURE_DPM_VCE_MASK            (1 << FEATURE_DPM_VCE_BIT            )
#define FEATURE_ULV_MASK                (1 << FEATURE_ULV_BIT                )
#define FEATURE_DPM_MP0CLK_MASK         (1 << FEATURE_DPM_MP0CLK_BIT         )
#define FEATURE_DPM_LINK_MASK           (1 << FEATURE_DPM_LINK_BIT           )
#define FEATURE_DPM_DCEFCLK_MASK        (1 << FEATURE_DPM_DCEFCLK_BIT        )
#define FEATURE_DS_GFXCLK_MASK          (1 << FEATURE_DS_GFXCLK_BIT          )
#define FEATURE_DS_SOCCLK_MASK          (1 << FEATURE_DS_SOCCLK_BIT          )
#define FEATURE_DS_LCLK_MASK            (1 << FEATURE_DS_LCLK_BIT            )
#define FEATURE_PPT_MASK                (1 << FEATURE_PPT_BIT                )
#define FEATURE_TDC_MASK                (1 << FEATURE_TDC_BIT                )
#define FEATURE_THERMAL_MASK            (1 << FEATURE_THERMAL_BIT            )
#define FEATURE_GFX_PER_CU_CG_MASK      (1 << FEATURE_GFX_PER_CU_CG_BIT      )
#define FEATURE_RM_MASK                 (1 << FEATURE_RM_BIT                 )
#define FEATURE_DS_DCEFCLK_MASK         (1 << FEATURE_DS_DCEFCLK_BIT         )
#define FEATURE_ACDC_MASK               (1 << FEATURE_ACDC_BIT               )
#define FEATURE_VR0HOT_MASK             (1 << FEATURE_VR0HOT_BIT             )
#define FEATURE_VR1HOT_MASK             (1 << FEATURE_VR1HOT_BIT             )
#define FEATURE_FW_CTF_MASK             (1 << FEATURE_FW_CTF_BIT             )
#define FEATURE_LED_DISPLAY_MASK        (1 << FEATURE_LED_DISPLAY_BIT        )
#define FEATURE_FAN_CONTROL_MASK        (1 << FEATURE_FAN_CONTROL_BIT        )
#define FEATURE_GFX_EDC_MASK            (1 << FEATURE_GFX_EDC_BIT            )
#define FEATURE_GFXOFF_MASK             (1 << FEATURE_GFXOFF_BIT             )
#define FEATURE_CG_MASK                 (1 << FEATURE_CG_BIT                 )
#define FEATURE_ACG_MASK          (1 << FEATURE_ACG_BIT)
#define FEATURE_SPARE_29_MASK           (1 << FEATURE_SPARE_29_BIT           )
#define FEATURE_SPARE_30_MASK           (1 << FEATURE_SPARE_30_BIT           )
#define FEATURE_SPARE_31_MASK           (1 << FEATURE_SPARE_31_BIT           )


#define DPM_OVERRIDE_DISABLE_SOCCLK_PID             0x00000001
#define DPM_OVERRIDE_DISABLE_UCLK_PID               0x00000002
#define DPM_OVERRIDE_ENABLE_VOLT_LINK_UVD_SOCCLK    0x00000004
#define DPM_OVERRIDE_ENABLE_VOLT_LINK_UVD_UCLK      0x00000008
#define DPM_OVERRIDE_ENABLE_FREQ_LINK_VCLK_SOCCLK   0x00000010
#define DPM_OVERRIDE_ENABLE_FREQ_LINK_VCLK_UCLK     0x00000020
#define DPM_OVERRIDE_ENABLE_FREQ_LINK_DCLK_SOCCLK   0x00000040
#define DPM_OVERRIDE_ENABLE_FREQ_LINK_DCLK_UCLK     0x00000080
#define DPM_OVERRIDE_ENABLE_VOLT_LINK_VCE_SOCCLK    0x00000100
#define DPM_OVERRIDE_ENABLE_VOLT_LINK_VCE_UCLK      0x00000200
#define DPM_OVERRIDE_ENABLE_FREQ_LINK_ECLK_SOCCLK   0x00000400
#define DPM_OVERRIDE_ENABLE_FREQ_LINK_ECLK_UCLK     0x00000800
#define DPM_OVERRIDE_ENABLE_FREQ_LINK_GFXCLK_SOCCLK 0x00001000
#define DPM_OVERRIDE_ENABLE_FREQ_LINK_GFXCLK_UCLK   0x00002000
#define DPM_OVERRIDE_ENABLE_GFXOFF_GFXCLK_SWITCH    0x00004000
#define DPM_OVERRIDE_ENABLE_GFXOFF_SOCCLK_SWITCH    0x00008000
#define DPM_OVERRIDE_ENABLE_GFXOFF_UCLK_SWITCH      0x00010000


#define VR_MAPPING_VR_SELECT_MASK  0x01
#define VR_MAPPING_VR_SELECT_SHIFT 0x00

#define VR_MAPPING_PLANE_SELECT_MASK  0x02
#define VR_MAPPING_PLANE_SELECT_SHIFT 0x01


#define PSI_SEL_VR0_PLANE0_PSI0  0x01
#define PSI_SEL_VR0_PLANE0_PSI1  0x02
#define PSI_SEL_VR0_PLANE1_PSI0  0x04
#define PSI_SEL_VR0_PLANE1_PSI1  0x08
#define PSI_SEL_VR1_PLANE0_PSI0  0x10
#define PSI_SEL_VR1_PLANE0_PSI1  0x20
#define PSI_SEL_VR1_PLANE1_PSI0  0x40
#define PSI_SEL_VR1_PLANE1_PSI1  0x80


#define THROTTLER_STATUS_PADDING_BIT      0
#define THROTTLER_STATUS_TEMP_EDGE_BIT    1
#define THROTTLER_STATUS_TEMP_HOTSPOT_BIT 2
#define THROTTLER_STATUS_TEMP_HBM_BIT     3
#define THROTTLER_STATUS_TEMP_VR_GFX_BIT  4
#define THROTTLER_STATUS_TEMP_VR_MEM_BIT  5
#define THROTTLER_STATUS_TEMP_LIQUID_BIT  6
#define THROTTLER_STATUS_TEMP_PLX_BIT     7
#define THROTTLER_STATUS_TEMP_SKIN_BIT    8
#define THROTTLER_STATUS_TDC_GFX_BIT      9
#define THROTTLER_STATUS_TDC_SOC_BIT      10
#define THROTTLER_STATUS_PPT_BIT          11
#define THROTTLER_STATUS_FIT_BIT          12
#define THROTTLER_STATUS_PPM_BIT          13


#define TABLE_TRANSFER_OK         0x0
#define TABLE_TRANSFER_FAILED     0xFF


#define WORKLOAD_DEFAULT_BIT              0
#define WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT 1
#define WORKLOAD_PPLIB_POWER_SAVING_BIT   2
#define WORKLOAD_PPLIB_VIDEO_BIT          3
#define WORKLOAD_PPLIB_VR_BIT             4
#define WORKLOAD_PPLIB_COMPUTE_BIT        5
#define WORKLOAD_PPLIB_CUSTOM_BIT         6
#define WORKLOAD_PPLIB_COUNT              7

typedef struct {
  uint32_t a;
  uint32_t b;
  uint32_t c;
} QuadraticInt_t;

typedef struct {
  uint32_t m;
  uint32_t b;
} LinearInt_t;

typedef struct {
  uint32_t a;
  uint32_t b;
  uint32_t c;
} DroopInt_t;

typedef enum {
  PPCLK_GFXCLK,
  PPCLK_VCLK,
  PPCLK_DCLK,
  PPCLK_ECLK,
  PPCLK_SOCCLK,
  PPCLK_UCLK,
  PPCLK_DCEFCLK,
  PPCLK_DISPCLK,
  PPCLK_PIXCLK,
  PPCLK_PHYCLK,
  PPCLK_COUNT,
} PPCLK_e;

enum {
  VOLTAGE_MODE_AVFS,
  VOLTAGE_MODE_AVFS_SS,
  VOLTAGE_MODE_SS,
  VOLTAGE_MODE_COUNT,
};

typedef struct {
  uint8_t        VoltageMode;
  uint8_t        SnapToDiscrete;
  uint8_t        NumDiscreteLevels;
  uint8_t        padding;
  LinearInt_t    ConversionToAvfsClk;
  QuadraticInt_t SsCurve;
} DpmDescriptor_t;

typedef struct {
  uint32_t Version;


  uint32_t FeaturesToRun[2];


  uint16_t SocketPowerLimitAc0;
  uint16_t SocketPowerLimitAc0Tau;
  uint16_t SocketPowerLimitAc1;
  uint16_t SocketPowerLimitAc1Tau;
  uint16_t SocketPowerLimitAc2;
  uint16_t SocketPowerLimitAc2Tau;
  uint16_t SocketPowerLimitAc3;
  uint16_t SocketPowerLimitAc3Tau;
  uint16_t SocketPowerLimitDc;
  uint16_t SocketPowerLimitDcTau;
  uint16_t TdcLimitSoc;
  uint16_t TdcLimitSocTau;
  uint16_t TdcLimitGfx;
  uint16_t TdcLimitGfxTau;

  uint16_t TedgeLimit;
  uint16_t ThotspotLimit;
  uint16_t ThbmLimit;
  uint16_t Tvr_gfxLimit;
  uint16_t Tvr_memLimit;
  uint16_t Tliquid1Limit;
  uint16_t Tliquid2Limit;
  uint16_t TplxLimit;
  uint32_t FitLimit;

  uint16_t PpmPowerLimit;
  uint16_t PpmTemperatureThreshold;

  uint8_t  MemoryOnPackage;
  uint8_t  padding8_limits[3];


  uint16_t  UlvVoltageOffsetSoc;
  uint16_t  UlvVoltageOffsetGfx;

  uint8_t  UlvSmnclkDid;
  uint8_t  UlvMp1clkDid;
  uint8_t  UlvGfxclkBypass;
  uint8_t  Padding234;


  uint16_t     MinVoltageGfx;
  uint16_t     MinVoltageSoc;
  uint16_t     MaxVoltageGfx;
  uint16_t     MaxVoltageSoc;

  uint16_t     LoadLineResistance;
  uint16_t     LoadLine_padding;


  DpmDescriptor_t DpmDescriptor[PPCLK_COUNT];

  uint16_t       FreqTableGfx      [NUM_GFXCLK_DPM_LEVELS  ];
  uint16_t       FreqTableVclk     [NUM_VCLK_DPM_LEVELS    ];
  uint16_t       FreqTableDclk     [NUM_DCLK_DPM_LEVELS    ];
  uint16_t       FreqTableEclk     [NUM_ECLK_DPM_LEVELS    ];
  uint16_t       FreqTableSocclk   [NUM_SOCCLK_DPM_LEVELS  ];
  uint16_t       FreqTableUclk     [NUM_UCLK_DPM_LEVELS    ];
  uint16_t       FreqTableDcefclk  [NUM_DCEFCLK_DPM_LEVELS ];
  uint16_t       FreqTableDispclk  [NUM_DISPCLK_DPM_LEVELS ];
  uint16_t       FreqTablePixclk   [NUM_PIXCLK_DPM_LEVELS  ];
  uint16_t       FreqTablePhyclk   [NUM_PHYCLK_DPM_LEVELS  ];

  uint16_t       DcModeMaxFreq     [PPCLK_COUNT            ];


  uint16_t       Mp0clkFreq        [NUM_MP0CLK_DPM_LEVELS];
  uint16_t       Mp0DpmVoltage     [NUM_MP0CLK_DPM_LEVELS];


  uint16_t        GfxclkFidle;
  uint16_t        GfxclkSlewRate;
  uint16_t        CksEnableFreq;
  uint16_t        Padding789;
  QuadraticInt_t  CksVoltageOffset;
  uint16_t        AcgThresholdFreqHigh;
  uint16_t        AcgThresholdFreqLow;
  uint16_t        GfxclkDsMaxFreq;
  uint8_t         Padding456[2];


  uint8_t      LowestUclkReservedForUlv;
  uint8_t      Padding8_Uclk[3];


  uint8_t      PcieGenSpeed[NUM_LINK_LEVELS];
  uint8_t      PcieLaneCount[NUM_LINK_LEVELS];
  uint16_t     LclkFreq[NUM_LINK_LEVELS];


  uint16_t     EnableTdpm;
  uint16_t     TdpmHighHystTemperature;
  uint16_t     TdpmLowHystTemperature;
  uint16_t     GfxclkFreqHighTempLimit;


  uint16_t     FanStopTemp;
  uint16_t     FanStartTemp;

  uint16_t     FanGainEdge;
  uint16_t     FanGainHotspot;
  uint16_t     FanGainLiquid;
  uint16_t     FanGainVrVddc;
  uint16_t     FanGainVrMvdd;
  uint16_t     FanGainPlx;
  uint16_t     FanGainHbm;
  uint16_t     FanPwmMin;
  uint16_t     FanAcousticLimitRpm;
  uint16_t     FanThrottlingRpm;
  uint16_t     FanMaximumRpm;
  uint16_t     FanTargetTemperature;
  uint16_t     FanTargetGfxclk;
  uint8_t      FanZeroRpmEnable; 
  uint8_t      FanTachEdgePerRev;



  int16_t      FuzzyFan_ErrorSetDelta;
  int16_t      FuzzyFan_ErrorRateSetDelta;
  int16_t      FuzzyFan_PwmSetDelta;
  uint16_t     FuzzyFan_Reserved;




  uint8_t           OverrideAvfsGb;
  uint8_t           Padding8_Avfs[3];

  QuadraticInt_t    qAvfsGb;
  DroopInt_t        dBtcGbGfxCksOn;
  DroopInt_t        dBtcGbGfxCksOff;
  DroopInt_t        dBtcGbGfxAcg;
  DroopInt_t        dBtcGbSoc;
  LinearInt_t       qAgingGbGfx;
  LinearInt_t       qAgingGbSoc;

  QuadraticInt_t    qStaticVoltageOffsetGfx;
  QuadraticInt_t    qStaticVoltageOffsetSoc;

  uint16_t          DcTolGfx;
  uint16_t          DcTolSoc;

  uint8_t           DcBtcGfxEnabled;
  uint8_t           DcBtcSocEnabled;
  uint8_t           Padding8_GfxBtc[2];

  uint16_t          DcBtcGfxMin;
  uint16_t          DcBtcGfxMax;

  uint16_t          DcBtcSocMin;
  uint16_t          DcBtcSocMax;



  uint32_t          DebugOverrides;
  QuadraticInt_t    ReservedEquation0;
  QuadraticInt_t    ReservedEquation1;
  QuadraticInt_t    ReservedEquation2;
  QuadraticInt_t    ReservedEquation3;

	uint16_t     MinVoltageUlvGfx;
	uint16_t     MinVoltageUlvSoc;

	uint32_t     Reserved[14];



  uint8_t      Liquid1_I2C_address;
  uint8_t      Liquid2_I2C_address;
  uint8_t      Vr_I2C_address;
  uint8_t      Plx_I2C_address;

  uint8_t      Liquid_I2C_LineSCL;
  uint8_t      Liquid_I2C_LineSDA;
  uint8_t      Vr_I2C_LineSCL;
  uint8_t      Vr_I2C_LineSDA;

  uint8_t      Plx_I2C_LineSCL;
  uint8_t      Plx_I2C_LineSDA;
  uint8_t      VrSensorPresent;
  uint8_t      LiquidSensorPresent;

  uint16_t     MaxVoltageStepGfx;
  uint16_t     MaxVoltageStepSoc;

  uint8_t      VddGfxVrMapping;
  uint8_t      VddSocVrMapping;
  uint8_t      VddMem0VrMapping;
  uint8_t      VddMem1VrMapping;

  uint8_t      GfxUlvPhaseSheddingMask;
  uint8_t      SocUlvPhaseSheddingMask;
  uint8_t      ExternalSensorPresent;
  uint8_t      Padding8_V;


  uint16_t     GfxMaxCurrent;
  int8_t       GfxOffset;
  uint8_t      Padding_TelemetryGfx;

  uint16_t     SocMaxCurrent;
  int8_t       SocOffset;
  uint8_t      Padding_TelemetrySoc;

  uint16_t     Mem0MaxCurrent;
  int8_t       Mem0Offset;
  uint8_t      Padding_TelemetryMem0;

  uint16_t     Mem1MaxCurrent;
  int8_t       Mem1Offset;
  uint8_t      Padding_TelemetryMem1;


  uint8_t      AcDcGpio;
  uint8_t      AcDcPolarity;
  uint8_t      VR0HotGpio;
  uint8_t      VR0HotPolarity;

  uint8_t      VR1HotGpio;
  uint8_t      VR1HotPolarity;
  uint8_t      Padding1;
  uint8_t      Padding2;



  uint8_t      LedPin0;
  uint8_t      LedPin1;
  uint8_t      LedPin2;
  uint8_t      padding8_4;


	uint8_t      PllGfxclkSpreadEnabled;
	uint8_t      PllGfxclkSpreadPercent;
	uint16_t     PllGfxclkSpreadFreq;

  uint8_t      UclkSpreadEnabled;
  uint8_t      UclkSpreadPercent;
  uint16_t     UclkSpreadFreq;

  uint8_t      SocclkSpreadEnabled;
  uint8_t      SocclkSpreadPercent;
  uint16_t     SocclkSpreadFreq;

	uint8_t      AcgGfxclkSpreadEnabled;
	uint8_t      AcgGfxclkSpreadPercent;
	uint16_t     AcgGfxclkSpreadFreq;

  uint8_t      Vr2_I2C_address;
  uint8_t      padding_vr2[3];

  uint32_t     BoardReserved[9];


  uint32_t     MmHubPadding[7];

} PPTable_t;

typedef struct {

  uint16_t     GfxclkAverageLpfTau;
  uint16_t     SocclkAverageLpfTau;
  uint16_t     UclkAverageLpfTau;
  uint16_t     GfxActivityLpfTau;
  uint16_t     UclkActivityLpfTau;


  uint32_t     MmHubPadding[7];
} DriverSmuConfig_t;

typedef struct {

  uint16_t      GfxclkFmin;
  uint16_t      GfxclkFmax;
  uint16_t      GfxclkFreq1;
  uint16_t      GfxclkOffsetVolt1;
  uint16_t      GfxclkFreq2;
  uint16_t      GfxclkOffsetVolt2;
  uint16_t      GfxclkFreq3;
  uint16_t      GfxclkOffsetVolt3;
  uint16_t      UclkFmax;
  int16_t       OverDrivePct;
  uint16_t      FanMaximumRpm;
  uint16_t      FanMinimumPwm;
  uint16_t      FanTargetTemperature;
  uint16_t      MaxOpTemp;

} OverDriveTable_t;

typedef struct {
  uint16_t CurrClock[PPCLK_COUNT];
  uint16_t AverageGfxclkFrequency;
  uint16_t AverageSocclkFrequency;
  uint16_t AverageUclkFrequency  ;
  uint16_t AverageGfxActivity    ;
  uint16_t AverageUclkActivity   ;
  uint8_t  CurrSocVoltageOffset  ;
  uint8_t  CurrGfxVoltageOffset  ;
  uint8_t  CurrMemVidOffset      ;
  uint8_t  Padding8              ;
  uint16_t CurrSocketPower       ;
  uint16_t TemperatureEdge       ;
  uint16_t TemperatureHotspot    ;
  uint16_t TemperatureHBM        ;
  uint16_t TemperatureVrGfx      ;
  uint16_t TemperatureVrMem      ;
  uint16_t TemperatureLiquid     ;
  uint16_t TemperaturePlx        ;
  uint32_t ThrottlerStatus       ;

  uint8_t  LinkDpmLevel;
  uint8_t  Padding[3];


  uint32_t     MmHubPadding[7];
} SmuMetrics_t;

typedef struct {
  uint16_t MinClock;
  uint16_t MaxClock;
  uint16_t MinUclk;
  uint16_t MaxUclk;

  uint8_t  WmSetting;
  uint8_t  Padding[3];
} WatermarkRowGeneric_t;

#define NUM_WM_RANGES 4

typedef enum {
  WM_SOCCLK = 0,
  WM_DCEFCLK,
  WM_COUNT_PP,
} WM_CLOCK_e;

typedef struct {

  WatermarkRowGeneric_t WatermarkRow[WM_COUNT_PP][NUM_WM_RANGES];

  uint32_t     MmHubPadding[7];
} Watermarks_t;

typedef struct {
  uint16_t avgPsmCount[30];
  uint16_t minPsmCount[30];
  float    avgPsmVoltage[30];
  float    minPsmVoltage[30];

  uint32_t MmHubPadding[7];
} AvfsDebugTable_t;

typedef struct {
  uint8_t  AvfsEn;
  uint8_t  AvfsVersion;
  uint8_t  OverrideVFT;
  uint8_t  OverrideAvfsGb;

  uint8_t  OverrideTemperatures;
  uint8_t  OverrideVInversion;
  uint8_t  OverrideP2V;
  uint8_t  OverrideP2VCharzFreq;

  int32_t VFT0_m1;
  int32_t VFT0_m2;
  int32_t VFT0_b;

  int32_t VFT1_m1;
  int32_t VFT1_m2;
  int32_t VFT1_b;

  int32_t VFT2_m1;
  int32_t VFT2_m2;
  int32_t VFT2_b;

  int32_t AvfsGb0_m1;
  int32_t AvfsGb0_m2;
  int32_t AvfsGb0_b;

  int32_t AcBtcGb_m1;
  int32_t AcBtcGb_m2;
  int32_t AcBtcGb_b;

  uint32_t AvfsTempCold;
  uint32_t AvfsTempMid;
  uint32_t AvfsTempHot;

  uint32_t GfxVInversion;
  uint32_t SocVInversion;

  int32_t P2V_m1;
  int32_t P2V_m2;
  int32_t P2V_b;

  uint32_t P2VCharzFreq;

  uint32_t EnabledAvfsModules;

  uint32_t MmHubPadding[7];
} AvfsFuseOverride_t;

typedef struct {

  uint8_t   Gfx_ActiveHystLimit;
  uint8_t   Gfx_IdleHystLimit;
  uint8_t   Gfx_FPS;
  uint8_t   Gfx_MinActiveFreqType;
  uint8_t   Gfx_BoosterFreqType; 
  uint8_t   Gfx_UseRlcBusy; 
  uint16_t  Gfx_MinActiveFreq;
  uint16_t  Gfx_BoosterFreq;
  uint16_t  Gfx_PD_Data_time_constant;
  uint32_t  Gfx_PD_Data_limit_a;
  uint32_t  Gfx_PD_Data_limit_b;
  uint32_t  Gfx_PD_Data_limit_c;
  uint32_t  Gfx_PD_Data_error_coeff;
  uint32_t  Gfx_PD_Data_error_rate_coeff;

  uint8_t   Soc_ActiveHystLimit;
  uint8_t   Soc_IdleHystLimit;
  uint8_t   Soc_FPS;
  uint8_t   Soc_MinActiveFreqType;
  uint8_t   Soc_BoosterFreqType; 
  uint8_t   Soc_UseRlcBusy;
  uint16_t  Soc_MinActiveFreq;
  uint16_t  Soc_BoosterFreq;
  uint16_t  Soc_PD_Data_time_constant;
  uint32_t  Soc_PD_Data_limit_a;
  uint32_t  Soc_PD_Data_limit_b;
  uint32_t  Soc_PD_Data_limit_c;
  uint32_t  Soc_PD_Data_error_coeff;
  uint32_t  Soc_PD_Data_error_rate_coeff;

  uint8_t   Mem_ActiveHystLimit;
  uint8_t   Mem_IdleHystLimit;
  uint8_t   Mem_FPS;
  uint8_t   Mem_MinActiveFreqType;
  uint8_t   Mem_BoosterFreqType;
  uint8_t   Mem_UseRlcBusy; 
  uint16_t  Mem_MinActiveFreq;
  uint16_t  Mem_BoosterFreq;
  uint16_t  Mem_PD_Data_time_constant;
  uint32_t  Mem_PD_Data_limit_a;
  uint32_t  Mem_PD_Data_limit_b;
  uint32_t  Mem_PD_Data_limit_c;
  uint32_t  Mem_PD_Data_error_coeff;
  uint32_t  Mem_PD_Data_error_rate_coeff;

} DpmActivityMonitorCoeffInt_t;




#define TABLE_PPTABLE                 0
#define TABLE_WATERMARKS              1
#define TABLE_AVFS                    2
#define TABLE_AVFS_PSM_DEBUG          3
#define TABLE_AVFS_FUSE_OVERRIDE      4
#define TABLE_PMSTATUSLOG             5
#define TABLE_SMU_METRICS             6
#define TABLE_DRIVER_SMU_CONFIG       7
#define TABLE_ACTIVITY_MONITOR_COEFF  8
#define TABLE_OVERDRIVE               9
#define TABLE_COUNT                  10


#define UCLK_SWITCH_SLOW 0
#define UCLK_SWITCH_FAST 1


#define SQ_Enable_MASK 0x1
#define SQ_IR_MASK 0x2
#define SQ_PCC_MASK 0x4
#define SQ_EDC_MASK 0x8

#define TCP_Enable_MASK 0x100
#define TCP_IR_MASK 0x200
#define TCP_PCC_MASK 0x400
#define TCP_EDC_MASK 0x800

#define TD_Enable_MASK 0x10000
#define TD_IR_MASK 0x20000
#define TD_PCC_MASK 0x40000
#define TD_EDC_MASK 0x80000

#define DB_Enable_MASK 0x1000000
#define DB_IR_MASK 0x2000000
#define DB_PCC_MASK 0x4000000
#define DB_EDC_MASK 0x8000000

#define SQ_Enable_SHIFT 0
#define SQ_IR_SHIFT 1
#define SQ_PCC_SHIFT 2
#define SQ_EDC_SHIFT 3

#define TCP_Enable_SHIFT 8
#define TCP_IR_SHIFT 9
#define TCP_PCC_SHIFT 10
#define TCP_EDC_SHIFT 11

#define TD_Enable_SHIFT 16
#define TD_IR_SHIFT 17
#define TD_PCC_SHIFT 18
#define TD_EDC_SHIFT 19

#define DB_Enable_SHIFT 24
#define DB_IR_SHIFT 25
#define DB_PCC_SHIFT 26
#define DB_EDC_SHIFT 27

#define REMOVE_FMAX_MARGIN_BIT     0x0
#define REMOVE_DCTOL_MARGIN_BIT    0x1
#define REMOVE_PLATFORM_MARGIN_BIT 0x2

#endif
