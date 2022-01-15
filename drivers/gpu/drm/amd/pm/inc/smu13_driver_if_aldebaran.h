/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef SMU13_DRIVER_IF_ALDEBARAN_H
#define SMU13_DRIVER_IF_ALDEBARAN_H

#define NUM_VCLK_DPM_LEVELS   8
#define NUM_DCLK_DPM_LEVELS   8
#define NUM_SOCCLK_DPM_LEVELS 8
#define NUM_LCLK_DPM_LEVELS   8
#define NUM_UCLK_DPM_LEVELS   4
#define NUM_FCLK_DPM_LEVELS   8
#define NUM_XGMI_DPM_LEVELS   4

// Feature Control Defines
#define FEATURE_DATA_CALCULATIONS       0
#define FEATURE_DPM_GFXCLK_BIT          1
#define FEATURE_DPM_UCLK_BIT            2
#define FEATURE_DPM_SOCCLK_BIT          3
#define FEATURE_DPM_FCLK_BIT            4
#define FEATURE_DPM_LCLK_BIT            5
#define FEATURE_DPM_XGMI_BIT            6
#define FEATURE_DS_GFXCLK_BIT           7
#define FEATURE_DS_SOCCLK_BIT           8
#define FEATURE_DS_LCLK_BIT             9
#define FEATURE_DS_FCLK_BIT             10
#define FEATURE_DS_UCLK_BIT             11
#define FEATURE_GFX_SS_BIT              12
#define FEATURE_DPM_VCN_BIT             13
#define FEATURE_RSMU_SMN_CG_BIT         14
#define FEATURE_WAFL_CG_BIT             15
#define FEATURE_PPT_BIT                 16
#define FEATURE_TDC_BIT                 17
#define FEATURE_APCC_PLUS_BIT           18
#define FEATURE_APCC_DFLL_BIT           19
#define FEATURE_FW_CTF_BIT              20
#define FEATURE_THERMAL_BIT             21
#define FEATURE_OUT_OF_BAND_MONITOR_BIT 22
#define FEATURE_SPARE_23_BIT            23
#define FEATURE_XGMI_PER_LINK_PWR_DWN   24
#define FEATURE_DF_CSTATE               25
#define FEATURE_FUSE_CG_BIT             26
#define FEATURE_MP1_CG_BIT              27
#define FEATURE_SMUIO_CG_BIT            28
#define FEATURE_THM_CG_BIT              29
#define FEATURE_CLK_CG_BIT              30
#define FEATURE_EDC_BIT                 31
#define FEATURE_SPARE_32_BIT            32
#define FEATURE_SPARE_33_BIT            33
#define FEATURE_SPARE_34_BIT            34
#define FEATURE_SPARE_35_BIT            35
#define FEATURE_SPARE_36_BIT            36
#define FEATURE_SPARE_37_BIT            37
#define FEATURE_SPARE_38_BIT            38
#define FEATURE_SPARE_39_BIT            39
#define FEATURE_SPARE_40_BIT            40
#define FEATURE_SPARE_41_BIT            41
#define FEATURE_SPARE_42_BIT            42
#define FEATURE_SPARE_43_BIT            43
#define FEATURE_SPARE_44_BIT            44
#define FEATURE_SPARE_45_BIT            45
#define FEATURE_SPARE_46_BIT            46
#define FEATURE_SPARE_47_BIT            47
#define FEATURE_SPARE_48_BIT            48
#define FEATURE_SPARE_49_BIT            49
#define FEATURE_SPARE_50_BIT            50
#define FEATURE_SPARE_51_BIT            51
#define FEATURE_SPARE_52_BIT            52
#define FEATURE_SPARE_53_BIT            53
#define FEATURE_SPARE_54_BIT            54
#define FEATURE_SPARE_55_BIT            55
#define FEATURE_SPARE_56_BIT            56
#define FEATURE_SPARE_57_BIT            57
#define FEATURE_SPARE_58_BIT            58
#define FEATURE_SPARE_59_BIT            59
#define FEATURE_SPARE_60_BIT            60
#define FEATURE_SPARE_61_BIT            61
#define FEATURE_SPARE_62_BIT            62
#define FEATURE_SPARE_63_BIT            63

#define NUM_FEATURES                    64

// I2C Config Bit Defines
#define I2C_CONTROLLER_ENABLED  1
#define I2C_CONTROLLER_DISABLED 0

// Throttler Status Bits.
// These are aligned with the out of band monitor alarm bits for common throttlers
#define THROTTLER_PPT0_BIT         0
#define THROTTLER_PPT1_BIT         1
#define THROTTLER_TDC_GFX_BIT      2
#define THROTTLER_TDC_SOC_BIT      3
#define THROTTLER_TDC_HBM_BIT      4
#define THROTTLER_SPARE_5          5
#define THROTTLER_TEMP_GPU_BIT     6
#define THROTTLER_TEMP_MEM_BIT     7
#define THORTTLER_SPARE_8          8
#define THORTTLER_SPARE_9          9
#define THORTTLER_SPARE_10         10
#define THROTTLER_TEMP_VR_GFX_BIT  11
#define THROTTLER_TEMP_VR_SOC_BIT  12
#define THROTTLER_TEMP_VR_MEM_BIT  13
#define THORTTLER_SPARE_14         14
#define THORTTLER_SPARE_15         15
#define THORTTLER_SPARE_16         16
#define THORTTLER_SPARE_17         17
#define THORTTLER_SPARE_18         18
#define THROTTLER_APCC_BIT         19

// Table transfer status
#define TABLE_TRANSFER_OK         0x0
#define TABLE_TRANSFER_FAILED     0xFF
#define TABLE_TRANSFER_PENDING    0xAB

//I2C Interface
#define NUM_I2C_CONTROLLERS                8

#define I2C_CONTROLLER_ENABLED             1
#define I2C_CONTROLLER_DISABLED            0

#define MAX_SW_I2C_COMMANDS                24

typedef enum {
  I2C_CONTROLLER_PORT_0, //CKSVII2C0
  I2C_CONTROLLER_PORT_1, //CKSVII2C1
  I2C_CONTROLLER_PORT_COUNT,
} I2cControllerPort_e;

typedef enum {
  I2C_CONTROLLER_THROTTLER_TYPE_NONE,
  I2C_CONTROLLER_THROTTLER_VR_GFX0,
  I2C_CONTROLLER_THROTTLER_VR_GFX1,
  I2C_CONTROLLER_THROTTLER_VR_SOC,
  I2C_CONTROLLER_THROTTLER_VR_MEM,
  I2C_CONTROLLER_THROTTLER_COUNT,
} I2cControllerThrottler_e;

typedef enum {
  I2C_CONTROLLER_PROTOCOL_VR_MP2855,
  I2C_CONTROLLER_PROTOCOL_COUNT,
} I2cControllerProtocol_e;

typedef struct {
  uint8_t   Enabled;
  uint8_t   Speed;
  uint8_t   SlaveAddress;
  uint8_t   ControllerPort;
  uint8_t   ThermalThrotter;
  uint8_t   I2cProtocol;
  uint8_t   PaddingConfig[2];
} I2cControllerConfig_t;

typedef enum {
  I2C_PORT_SVD_SCL,
  I2C_PORT_GPIO,
} I2cPort_e;

typedef enum {
  I2C_SPEED_FAST_50K,     //50  Kbits/s
  I2C_SPEED_FAST_100K,    //100 Kbits/s
  I2C_SPEED_FAST_400K,    //400 Kbits/s
  I2C_SPEED_FAST_PLUS_1M, //1   Mbits/s (in fast mode)
  I2C_SPEED_HIGH_1M,      //1   Mbits/s (in high speed mode)
  I2C_SPEED_HIGH_2M,      //2.3 Mbits/s
  I2C_SPEED_COUNT,
} I2cSpeed_e;

typedef enum {
  I2C_CMD_READ,
  I2C_CMD_WRITE,
  I2C_CMD_COUNT,
} I2cCmdType_e;

#define CMDCONFIG_STOP_BIT             0
#define CMDCONFIG_RESTART_BIT          1
#define CMDCONFIG_READWRITE_BIT        2 //bit should be 0 for read, 1 for write

#define CMDCONFIG_STOP_MASK           (1 << CMDCONFIG_STOP_BIT)
#define CMDCONFIG_RESTART_MASK        (1 << CMDCONFIG_RESTART_BIT)
#define CMDCONFIG_READWRITE_MASK      (1 << CMDCONFIG_READWRITE_BIT)

typedef struct {
  uint8_t ReadWriteData;  //Return data for read. Data to send for write
  uint8_t CmdConfig; //Includes whether associated command should have a stop or restart command, and is a read or write
} SwI2cCmd_t; //SW I2C Command Table

typedef struct {
  uint8_t    I2CcontrollerPort; //CKSVII2C0(0) or //CKSVII2C1(1)
  uint8_t    I2CSpeed;          //Use I2cSpeed_e to indicate speed to select
  uint8_t    SlaveAddress;      //Slave address of device
  uint8_t    NumCmds;           //Number of commands
  SwI2cCmd_t SwI2cCmds[MAX_SW_I2C_COMMANDS];
} SwI2cRequest_t; // SW I2C Request Table

typedef struct {
  SwI2cRequest_t SwI2cRequest;
  uint32_t       Spare[8];
  uint32_t       MmHubPadding[8]; // SMU internal use
} SwI2cRequestExternal_t;

typedef struct {
  uint32_t a;  // store in IEEE float format in this variable
  uint32_t b;  // store in IEEE float format in this variable
  uint32_t c;  // store in IEEE float format in this variable
} QuadraticInt_t;

typedef struct {
  uint32_t m;  // store in IEEE float format in this variable
  uint32_t b;  // store in IEEE float format in this variable
} LinearInt_t;

typedef enum {
  GFXCLK_SOURCE_PLL,
  GFXCLK_SOURCE_DFLL,
  GFXCLK_SOURCE_COUNT,
} GfxclkSrc_e;

typedef enum {
  PPCLK_GFXCLK,
  PPCLK_VCLK,
  PPCLK_DCLK,
  PPCLK_SOCCLK,
  PPCLK_UCLK,
  PPCLK_FCLK,
  PPCLK_LCLK,
  PPCLK_COUNT,
} PPCLK_e;

typedef enum {
  GPIO_INT_POLARITY_ACTIVE_LOW,
  GPIO_INT_POLARITY_ACTIVE_HIGH,
} GpioIntPolarity_e;

//PPSMC_MSG_SetUclkDpmMode
typedef enum {
  UCLK_DPM_MODE_BANDWIDTH,
  UCLK_DPM_MODE_LATENCY,
} UCLK_DPM_MODE_e;

typedef struct {
  uint8_t        StartupLevel;
  uint8_t        NumDiscreteLevels;   // Set to 2 (Fmin, Fmax) when using fine grained DPM, otherwise set to # discrete levels used
  uint16_t       SsFmin;              // Fmin for SS curve. If SS curve is selected, will use V@SSFmin for F <= Fmin
  LinearInt_t    ConversionToAvfsClk; // Transfer function to AVFS Clock (GHz->GHz)
  QuadraticInt_t SsCurve;             // Slow-slow curve (GHz->V)
} DpmDescriptor_t;

typedef struct {
  uint32_t Version;

  // SECTION: Feature Enablement
  uint32_t FeaturesToRun[2];

  // SECTION: Infrastructure Limits
  uint16_t PptLimit;      // Watts
  uint16_t TdcLimitGfx;   // Amps
  uint16_t TdcLimitSoc;   // Amps
  uint16_t TdcLimitHbm;   // Amps
  uint16_t ThotspotLimit; // Celcius
  uint16_t TmemLimit;     // Celcius
  uint16_t Tvr_gfxLimit;  // Celcius
  uint16_t Tvr_memLimit;  // Celcius
  uint16_t Tvr_socLimit;  // Celcius
  uint16_t PaddingLimit;

  // SECTION: Voltage Control Parameters
  uint16_t MaxVoltageGfx; // In mV(Q2) Maximum Voltage allowable of VDD_GFX
  uint16_t MaxVoltageSoc; // In mV(Q2) Maximum Voltage allowable of VDD_SOC

  //SECTION: DPM Config 1
  DpmDescriptor_t DpmDescriptor[PPCLK_COUNT];

  uint8_t  DidTableVclk[NUM_VCLK_DPM_LEVELS];     //PPCLK_VCLK
  uint8_t  DidTableDclk[NUM_DCLK_DPM_LEVELS];     //PPCLK_DCLK
  uint8_t  DidTableSocclk[NUM_SOCCLK_DPM_LEVELS]; //PPCLK_SOCCLK
  uint8_t  DidTableLclk[NUM_LCLK_DPM_LEVELS];     //PPCLK_LCLK
  uint32_t FidTableFclk[NUM_FCLK_DPM_LEVELS];     //PPCLK_FCLK
  uint8_t  DidTableFclk[NUM_FCLK_DPM_LEVELS];     //PPCLK_FCLK
  uint32_t FidTableUclk[NUM_UCLK_DPM_LEVELS];     //PPCLK_UCLK
  uint8_t  DidTableUclk[NUM_UCLK_DPM_LEVELS];     //PPCLK_UCLK

  uint32_t StartupFidPll0; //GFXAVFSCLK, SOCCLK, MP0CLK, MPIOCLK, DXIOCLK
  uint32_t StartupFidPll4; //VCLK, DCLK, WAFLCLK
  uint32_t StartupFidPll5; //SMNCLK, MP1CLK, LCLK

  uint8_t  StartupSmnclkDid;
  uint8_t  StartupMp0clkDid;
  uint8_t  StartupMp1clkDid;
  uint8_t  StartupWaflclkDid;
  uint8_t  StartupGfxavfsclkDid;
  uint8_t  StartupMpioclkDid;
  uint8_t  StartupDxioclkDid;
  uint8_t  spare123;

  uint8_t  StartupVidGpu0Svi0Plane0; //VDDCR_GFX0
  uint8_t  StartupVidGpu0Svi0Plane1; //VDDCR_SOC
  uint8_t  StartupVidGpu0Svi1Plane0; //VDDCR_HBM
  uint8_t  StartupVidGpu0Svi1Plane1; //UNUSED [0 = plane is not used and should not be programmed]

  uint8_t  StartupVidGpu1Svi0Plane0; //VDDCR_GFX1
  uint8_t  StartupVidGpu1Svi0Plane1; //UNUSED [0 = plane is not used and should not be programmed]
  uint8_t  StartupVidGpu1Svi1Plane0; //UNUSED [0 = plane is not used and should not be programmed]
  uint8_t  StartupVidGpu1Svi1Plane1; //UNUSED [0 = plane is not used and should not be programmed]

  // GFXCLK DPM
  uint16_t GfxclkFmax;   // In MHz
  uint16_t GfxclkFmin;   // In MHz
  uint16_t GfxclkFidle;  // In MHz
  uint16_t GfxclkFinit;  // In MHz
  uint8_t  GfxclkSource; // GfxclkSrc_e [0 = PLL, 1 = DFLL]
  uint8_t  spare1[2];
  uint8_t  StartupGfxclkDid;
  uint32_t StartupGfxclkFid;

  // SECTION: AVFS
  uint16_t GFX_Guardband_Freq[8];         // MHz [unsigned]
  int16_t  GFX_Guardband_Voltage_Cold[8]; // mV [signed]
  int16_t  GFX_Guardband_Voltage_Mid[8];  // mV [signed]
  int16_t  GFX_Guardband_Voltage_Hot[8];  // mV [signed]

  uint16_t SOC_Guardband_Freq[8];         // MHz [unsigned]
  int16_t  SOC_Guardband_Voltage_Cold[8]; // mV [signed]
  int16_t  SOC_Guardband_Voltage_Mid[8];  // mV [signed]
  int16_t  SOC_Guardband_Voltage_Hot[8];  // mV [signed]

  // VDDCR_GFX BTC
  uint16_t DcBtcEnabled;
  int16_t  DcBtcMin;       // mV [signed]
  int16_t  DcBtcMax;       // mV [signed]
  int16_t  DcBtcGb;        // mV [signed]

  // SECTION: XGMI
  uint8_t  XgmiLinkSpeed[NUM_XGMI_DPM_LEVELS]; //Gbps [EX: 32 = 32Gbps]
  uint8_t  XgmiLinkWidth[NUM_XGMI_DPM_LEVELS]; //Width [EX: 16 = x16]
  uint8_t  XgmiStartupLevel;
  uint8_t  spare12[3];

  // GFX Vmin
  uint16_t GFX_PPVmin_Enabled;
  uint16_t GFX_Vmin_Plat_Offset_Hot;  // mV
  uint16_t GFX_Vmin_Plat_Offset_Cold; // mV
  uint16_t GFX_Vmin_Hot_T0;           // mV
  uint16_t GFX_Vmin_Cold_T0;          // mV
  uint16_t GFX_Vmin_Hot_Eol;          // mV
  uint16_t GFX_Vmin_Cold_Eol;         // mV
  uint16_t GFX_Vmin_Aging_Offset;     // mV
  uint16_t GFX_Vmin_Temperature_Hot;  // 'C
  uint16_t GFX_Vmin_Temperature_Cold; // 'C

  // SOC Vmin
  uint16_t SOC_PPVmin_Enabled;
  uint16_t SOC_Vmin_Plat_Offset_Hot;  // mV
  uint16_t SOC_Vmin_Plat_Offset_Cold; // mV
  uint16_t SOC_Vmin_Hot_T0;           // mV
  uint16_t SOC_Vmin_Cold_T0;          // mV
  uint16_t SOC_Vmin_Hot_Eol;          // mV
  uint16_t SOC_Vmin_Cold_Eol;         // mV
  uint16_t SOC_Vmin_Aging_Offset;     // mV
  uint16_t SOC_Vmin_Temperature_Hot;  // 'C
  uint16_t SOC_Vmin_Temperature_Cold; // 'C

  // APCC Settings
  uint32_t ApccPlusResidencyLimit; //PCC residency % (0-100)

  // Determinism
  uint16_t DeterminismVoltageOffset; //mV
  uint16_t spare22;

  // reserved
  uint32_t spare3[14];

  // SECTION: BOARD PARAMETERS
  // Telemetry Settings
  uint16_t GfxMaxCurrent; // in Amps
  int8_t   GfxOffset;     // in Amps
  uint8_t  Padding_TelemetryGfx;

  uint16_t SocMaxCurrent; // in Amps
  int8_t   SocOffset;     // in Amps
  uint8_t  Padding_TelemetrySoc;

  uint16_t MemMaxCurrent; // in Amps
  int8_t   MemOffset;     // in Amps
  uint8_t  Padding_TelemetryMem;

  uint16_t BoardMaxCurrent; // in Amps
  int8_t   BoardOffset;     // in Amps
  uint8_t  Padding_TelemetryBoardInput;

  // Platform input telemetry voltage coefficient
  uint32_t BoardVoltageCoeffA; // decode by /1000
  uint32_t BoardVoltageCoeffB; // decode by /1000

  // GPIO Settings
  uint8_t  VR0HotGpio;     // GPIO pin configured for VR0 HOT event
  uint8_t  VR0HotPolarity; // GPIO polarity for VR0 HOT event
  uint8_t  VR1HotGpio;     // GPIO pin configured for VR1 HOT event
  uint8_t  VR1HotPolarity; // GPIO polarity for VR1 HOT event

  // UCLK Spread Spectrum
  uint8_t  UclkSpreadEnabled; // on or off
  uint8_t  UclkSpreadPercent; // Q4.4
  uint16_t UclkSpreadFreq;    // kHz

  // FCLK Spread Spectrum
  uint8_t  FclkSpreadEnabled; // on or off
  uint8_t  FclkSpreadPercent; // Q4.4
  uint16_t FclkSpreadFreq;    // kHz

  // I2C Controller Structure
  I2cControllerConfig_t  I2cControllers[NUM_I2C_CONTROLLERS];

  // GPIO pins for I2C communications with 2nd controller for Input Telemetry Sequence
  uint8_t  GpioI2cScl; // Serial Clock
  uint8_t  GpioI2cSda; // Serial Data
  uint16_t spare5;

  uint16_t XgmiMaxCurrent; // in Amps
  int8_t   XgmiOffset;     // in Amps
  uint8_t  Padding_TelemetryXgmi;

  uint16_t  EdcPowerLimit;
  uint16_t  spare6;

  //reserved
  uint32_t reserved[14];

} PPTable_t;

typedef struct {
  // Time constant parameters for clock averages in ms
  uint16_t     GfxclkAverageLpfTau;
  uint16_t     SocclkAverageLpfTau;
  uint16_t     UclkAverageLpfTau;
  uint16_t     GfxActivityLpfTau;
  uint16_t     UclkActivityLpfTau;

  uint16_t     SocketPowerLpfTau;

  uint32_t     Spare[8];
  // Padding - ignore
  uint32_t     MmHubPadding[8]; // SMU internal use
} DriverSmuConfig_t;

typedef struct {
  uint16_t CurrClock[PPCLK_COUNT];
  uint16_t Padding1              ;
  uint16_t AverageGfxclkFrequency;
  uint16_t AverageSocclkFrequency;
  uint16_t AverageUclkFrequency  ;
  uint16_t AverageGfxActivity    ;
  uint16_t AverageUclkActivity   ;
  uint8_t  CurrSocVoltageOffset  ;
  uint8_t  CurrGfxVoltageOffset  ;
  uint8_t  CurrMemVidOffset      ;
  uint8_t  Padding8              ;
  uint16_t AverageSocketPower    ;
  uint16_t TemperatureEdge       ;
  uint16_t TemperatureHotspot    ;
  uint16_t TemperatureHBM        ;  // Max
  uint16_t TemperatureVrGfx      ;
  uint16_t TemperatureVrSoc      ;
  uint16_t TemperatureVrMem      ;
  uint32_t ThrottlerStatus       ;

  uint32_t PublicSerialNumLower32;
  uint32_t PublicSerialNumUpper32;
  uint16_t TemperatureAllHBM[4]  ;
  uint32_t GfxBusyAcc            ;
  uint32_t DramBusyAcc           ;
  uint32_t EnergyAcc64bitLow     ; //15.259uJ resolution
  uint32_t EnergyAcc64bitHigh    ;
  uint32_t TimeStampLow          ; //10ns resolution
  uint32_t TimeStampHigh         ;

  // Padding - ignore
  uint32_t     MmHubPadding[8]; // SMU internal use
} SmuMetrics_t;


typedef struct {
  uint16_t avgPsmCount[76];
  uint16_t minPsmCount[76];
  float    avgPsmVoltage[76];
  float    minPsmVoltage[76];

  uint32_t MmHubPadding[8]; // SMU internal use
} AvfsDebugTable_t;

// These defines are used with the following messages:
// SMC_MSG_TransferTableDram2Smu
// SMC_MSG_TransferTableSmu2Dram
#define TABLE_PPTABLE                 0
#define TABLE_AVFS_PSM_DEBUG          1
#define TABLE_AVFS_FUSE_OVERRIDE      2
#define TABLE_PMSTATUSLOG             3
#define TABLE_SMU_METRICS             4
#define TABLE_DRIVER_SMU_CONFIG       5
#define TABLE_I2C_COMMANDS            6
#define TABLE_COUNT                   7

#endif
