/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef SMU11_DRIVER_IF_ARCTURUS_H
#define SMU11_DRIVER_IF_ARCTURUS_H

// *** IMPORTANT ***
// SMU TEAM: Always increment the interface version if
// any structure is changed in this file
//#define SMU11_DRIVER_IF_VERSION 0x09

#define PPTABLE_ARCTURUS_SMU_VERSION 4

#define NUM_GFXCLK_DPM_LEVELS  16
#define NUM_VCLK_DPM_LEVELS    8
#define NUM_DCLK_DPM_LEVELS    8
#define NUM_MP0CLK_DPM_LEVELS  2
#define NUM_SOCCLK_DPM_LEVELS  8
#define NUM_UCLK_DPM_LEVELS    4
#define NUM_FCLK_DPM_LEVELS    8
#define NUM_XGMI_LEVELS        2
#define NUM_XGMI_PSTATE_LEVELS 4

#define MAX_GFXCLK_DPM_LEVEL  (NUM_GFXCLK_DPM_LEVELS  - 1)
#define MAX_VCLK_DPM_LEVEL    (NUM_VCLK_DPM_LEVELS    - 1)
#define MAX_DCLK_DPM_LEVEL    (NUM_DCLK_DPM_LEVELS    - 1)
#define MAX_MP0CLK_DPM_LEVEL  (NUM_MP0CLK_DPM_LEVELS  - 1)
#define MAX_SOCCLK_DPM_LEVEL  (NUM_SOCCLK_DPM_LEVELS  - 1)
#define MAX_UCLK_DPM_LEVEL    (NUM_UCLK_DPM_LEVELS    - 1)
#define MAX_FCLK_DPM_LEVEL    (NUM_FCLK_DPM_LEVELS    - 1)
#define MAX_XGMI_LEVEL        (NUM_XGMI_LEVELS        - 1)
#define MAX_XGMI_PSTATE_LEVEL (NUM_XGMI_PSTATE_LEVELS - 1)

// Feature Control Defines
// DPM
#define FEATURE_DPM_PREFETCHER_BIT      0
#define FEATURE_DPM_GFXCLK_BIT          1
#define FEATURE_DPM_UCLK_BIT            2
#define FEATURE_DPM_SOCCLK_BIT          3
#define FEATURE_DPM_FCLK_BIT            4
#define FEATURE_DPM_MP0CLK_BIT          5
#define FEATURE_DPM_XGMI_BIT            6
// Idle
#define FEATURE_DS_GFXCLK_BIT           7
#define FEATURE_DS_SOCCLK_BIT           8
#define FEATURE_DS_LCLK_BIT             9
#define FEATURE_DS_FCLK_BIT             10
#define FEATURE_DS_UCLK_BIT             11
#define FEATURE_GFX_ULV_BIT             12
#define FEATURE_DPM_VCN_BIT             13
#define FEATURE_RSMU_SMN_CG_BIT         14
#define FEATURE_WAFL_CG_BIT             15
// Throttler/Response
#define FEATURE_PPT_BIT                 16
#define FEATURE_TDC_BIT                 17
#define FEATURE_APCC_PLUS_BIT           18
#define FEATURE_VR0HOT_BIT              19
#define FEATURE_VR1HOT_BIT              20
#define FEATURE_FW_CTF_BIT              21
#define FEATURE_FAN_CONTROL_BIT         22
#define FEATURE_THERMAL_BIT             23
// Other
#define FEATURE_OUT_OF_BAND_MONITOR_BIT 24
#define FEATURE_TEMP_DEPENDENT_VMIN_BIT 25
#define FEATURE_PER_PART_VMIN_BIT       26

#define FEATURE_SPARE_27_BIT            27
#define FEATURE_SPARE_28_BIT            28
#define FEATURE_SPARE_29_BIT            29
#define FEATURE_SPARE_30_BIT            30
#define FEATURE_SPARE_31_BIT            31
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


#define FEATURE_DPM_PREFETCHER_MASK       (1 << FEATURE_DPM_PREFETCHER_BIT       )
#define FEATURE_DPM_GFXCLK_MASK           (1 << FEATURE_DPM_GFXCLK_BIT           )
#define FEATURE_DPM_UCLK_MASK             (1 << FEATURE_DPM_UCLK_BIT             )
#define FEATURE_DPM_SOCCLK_MASK           (1 << FEATURE_DPM_SOCCLK_BIT           )
#define FEATURE_DPM_FCLK_MASK             (1 << FEATURE_DPM_FCLK_BIT             )
#define FEATURE_DPM_MP0CLK_MASK           (1 << FEATURE_DPM_MP0CLK_BIT           )
#define FEATURE_DPM_XGMI_MASK             (1 << FEATURE_DPM_XGMI_BIT             )

#define FEATURE_DS_GFXCLK_MASK            (1 << FEATURE_DS_GFXCLK_BIT            )
#define FEATURE_DS_SOCCLK_MASK            (1 << FEATURE_DS_SOCCLK_BIT            )
#define FEATURE_DS_LCLK_MASK              (1 << FEATURE_DS_LCLK_BIT              )
#define FEATURE_DS_FCLK_MASK              (1 << FEATURE_DS_FCLK_BIT              )
#define FEATURE_DS_UCLK_MASK              (1 << FEATURE_DS_UCLK_BIT              )
#define FEATURE_GFX_ULV_MASK              (1 << FEATURE_GFX_ULV_BIT              )
#define FEATURE_DPM_VCN_MASK              (1 << FEATURE_DPM_VCN_BIT              )
#define FEATURE_RSMU_SMN_CG_MASK          (1 << FEATURE_RSMU_SMN_CG_BIT          )
#define FEATURE_WAFL_CG_MASK              (1 << FEATURE_WAFL_CG_BIT              )

#define FEATURE_PPT_MASK                  (1 << FEATURE_PPT_BIT                  )
#define FEATURE_TDC_MASK                  (1 << FEATURE_TDC_BIT                  )
#define FEATURE_APCC_PLUS_MASK            (1 << FEATURE_APCC_PLUS_BIT            )
#define FEATURE_VR0HOT_MASK               (1 << FEATURE_VR0HOT_BIT               )
#define FEATURE_VR1HOT_MASK               (1 << FEATURE_VR1HOT_BIT               )
#define FEATURE_FW_CTF_MASK               (1 << FEATURE_FW_CTF_BIT               )
#define FEATURE_FAN_CONTROL_MASK          (1 << FEATURE_FAN_CONTROL_BIT          )
#define FEATURE_THERMAL_MASK              (1 << FEATURE_THERMAL_BIT              )

#define FEATURE_OUT_OF_BAND_MONITOR_MASK  (1 << FEATURE_OUT_OF_BAND_MONITOR_BIT   )
#define FEATURE_TEMP_DEPENDENT_VMIN_MASK  (1 << FEATURE_TEMP_DEPENDENT_VMIN_BIT )
#define FEATURE_PER_PART_VMIN_MASK        (1 << FEATURE_PER_PART_VMIN_BIT        )


//FIXME need updating
// Debug Overrides Bitmask
#define DPM_OVERRIDE_DISABLE_UCLK_PID               0x00000001
#define DPM_OVERRIDE_DISABLE_VOLT_LINK_VCN_FCLK     0x00000002

// I2C Config Bit Defines
#define I2C_CONTROLLER_ENABLED           1
#define I2C_CONTROLLER_DISABLED          0

// VR Mapping Bit Defines
#define VR_MAPPING_VR_SELECT_MASK  0x01
#define VR_MAPPING_VR_SELECT_SHIFT 0x00

#define VR_MAPPING_PLANE_SELECT_MASK  0x02
#define VR_MAPPING_PLANE_SELECT_SHIFT 0x01

// PSI Bit Defines
#define PSI_SEL_VR0_PLANE0_PSI0  0x01
#define PSI_SEL_VR0_PLANE0_PSI1  0x02
#define PSI_SEL_VR0_PLANE1_PSI0  0x04
#define PSI_SEL_VR0_PLANE1_PSI1  0x08
#define PSI_SEL_VR1_PLANE0_PSI0  0x10
#define PSI_SEL_VR1_PLANE0_PSI1  0x20
#define PSI_SEL_VR1_PLANE1_PSI0  0x40
#define PSI_SEL_VR1_PLANE1_PSI1  0x80

// Throttler Control/Status Bits
#define THROTTLER_PADDING_BIT      0
#define THROTTLER_TEMP_EDGE_BIT    1
#define THROTTLER_TEMP_HOTSPOT_BIT 2
#define THROTTLER_TEMP_MEM_BIT     3
#define THROTTLER_TEMP_VR_GFX_BIT  4
#define THROTTLER_TEMP_VR_MEM_BIT  5
#define THROTTLER_TEMP_VR_SOC_BIT  6
#define THROTTLER_TDC_GFX_BIT      7
#define THROTTLER_TDC_SOC_BIT      8
#define THROTTLER_PPT0_BIT         9
#define THROTTLER_PPT1_BIT         10
#define THROTTLER_PPT2_BIT         11
#define THROTTLER_PPT3_BIT         12
#define THROTTLER_PPM_BIT          13
#define THROTTLER_FIT_BIT          14
#define THROTTLER_APCC_BIT         15

// Table transfer status
#define TABLE_TRANSFER_OK         0x0
#define TABLE_TRANSFER_FAILED     0xFF
#define TABLE_TRANSFER_PENDING    0xAB

// Workload bits
#define WORKLOAD_PPLIB_DEFAULT_BIT        0
#define WORKLOAD_PPLIB_POWER_SAVING_BIT   1
#define WORKLOAD_PPLIB_VIDEO_BIT          2
#define WORKLOAD_PPLIB_COMPUTE_BIT        3
#define WORKLOAD_PPLIB_CUSTOM_BIT         4
#define WORKLOAD_PPLIB_COUNT              5

//XGMI performance states
#define XGMI_STATE_D0 1
#define XGMI_STATE_D3 0

#define NUM_I2C_CONTROLLERS                8

#define I2C_CONTROLLER_ENABLED             1
#define I2C_CONTROLLER_DISABLED            0

#define MAX_SW_I2C_COMMANDS                8

typedef enum {
  I2C_CONTROLLER_PORT_0 = 0,  //CKSVII2C0
  I2C_CONTROLLER_PORT_1 = 1,  //CKSVII2C1
  I2C_CONTROLLER_PORT_COUNT,
} I2cControllerPort_e;

typedef enum {
  I2C_CONTROLLER_NAME_VR_GFX = 0,
  I2C_CONTROLLER_NAME_VR_SOC,
  I2C_CONTROLLER_NAME_VR_MEM,
  I2C_CONTROLLER_NAME_SPARE,
  I2C_CONTROLLER_NAME_COUNT,
} I2cControllerName_e;

typedef enum {
  I2C_CONTROLLER_THROTTLER_TYPE_NONE = 0,
  I2C_CONTROLLER_THROTTLER_VR_GFX,
  I2C_CONTROLLER_THROTTLER_VR_SOC,
  I2C_CONTROLLER_THROTTLER_VR_MEM,
  I2C_CONTROLLER_THROTTLER_COUNT,
} I2cControllerThrottler_e;

typedef enum {
  I2C_CONTROLLER_PROTOCOL_VR_0,
  I2C_CONTROLLER_PROTOCOL_VR_1,
  I2C_CONTROLLER_PROTOCOL_TMP_0,
  I2C_CONTROLLER_PROTOCOL_TMP_1,
  I2C_CONTROLLER_PROTOCOL_SPARE_0,
  I2C_CONTROLLER_PROTOCOL_SPARE_1,
  I2C_CONTROLLER_PROTOCOL_COUNT,
} I2cControllerProtocol_e;

typedef struct {
  uint8_t   Enabled;
  uint8_t   Speed;
  uint8_t   Padding[2];
  uint32_t  SlaveAddress;
  uint8_t   ControllerPort;
  uint8_t   ControllerName;
  uint8_t   ThermalThrotter;
  uint8_t   I2cProtocol;
} I2cControllerConfig_t;

typedef enum {
  I2C_PORT_SVD_SCL = 0,
  I2C_PORT_GPIO,
} I2cPort_e;

typedef enum {
  I2C_SPEED_FAST_50K = 0,      //50  Kbits/s
  I2C_SPEED_FAST_100K,         //100 Kbits/s
  I2C_SPEED_FAST_400K,         //400 Kbits/s
  I2C_SPEED_FAST_PLUS_1M,      //1   Mbits/s (in fast mode)
  I2C_SPEED_HIGH_1M,           //1   Mbits/s (in high speed mode)
  I2C_SPEED_HIGH_2M,           //2.3 Mbits/s
  I2C_SPEED_COUNT,
} I2cSpeed_e;

typedef enum {
  I2C_CMD_READ = 0,
  I2C_CMD_WRITE,
  I2C_CMD_COUNT,
} I2cCmdType_e;

#define CMDCONFIG_STOP_BIT      0
#define CMDCONFIG_RESTART_BIT   1

#define CMDCONFIG_STOP_MASK     (1 << CMDCONFIG_STOP_BIT)
#define CMDCONFIG_RESTART_MASK  (1 << CMDCONFIG_RESTART_BIT)

typedef struct {
  uint8_t RegisterAddr; ////only valid for write, ignored for read
  uint8_t Cmd;  //Read(0) or Write(1)
  uint8_t Data;  //Return data for read. Data to send for write
  uint8_t CmdConfig; //Includes whether associated command should have a stop or restart command
} SwI2cCmd_t; //SW I2C Command Table

typedef struct {
  uint8_t     I2CcontrollerPort; //CKSVII2C0(0) or //CKSVII2C1(1)
  uint8_t     I2CSpeed;          //Slow(0) or Fast(1)
  uint16_t    SlaveAddress;
  uint8_t     NumCmds;           //Number of commands
  uint8_t     Padding[3];

  SwI2cCmd_t  SwI2cCmds[MAX_SW_I2C_COMMANDS];

  uint32_t     MmHubPadding[8]; // SMU internal use

} SwI2cRequest_t; // SW I2C Request Table

//D3HOT sequences
typedef enum {
  BACO_SEQUENCE,
  MSR_SEQUENCE,
  BAMACO_SEQUENCE,
  ULPS_SEQUENCE,
  D3HOT_SEQUENCE_COUNT,
}D3HOTSequence_e;

//THis is aligned with RSMU PGFSM Register Mapping
typedef enum {
  PG_DYNAMIC_MODE = 0,
  PG_STATIC_MODE,
} PowerGatingMode_e;

//This is aligned with RSMU PGFSM Register Mapping
typedef enum {
  PG_POWER_DOWN = 0,
  PG_POWER_UP,
} PowerGatingSettings_e;

typedef struct {
  uint32_t a;  // store in IEEE float format in this variable
  uint32_t b;  // store in IEEE float format in this variable
  uint32_t c;  // store in IEEE float format in this variable
} QuadraticInt_t;

typedef struct {
  uint32_t m;  // store in IEEE float format in this variable
  uint32_t b;  // store in IEEE float format in this variable
} LinearInt_t;

typedef struct {
  uint32_t a;  // store in IEEE float format in this variable
  uint32_t b;  // store in IEEE float format in this variable
  uint32_t c;  // store in IEEE float format in this variable
} DroopInt_t;

typedef enum {
  GFXCLK_SOURCE_PLL = 0,
  GFXCLK_SOURCE_AFLL,
  GFXCLK_SOURCE_COUNT,
} GfxclkSrc_e;

typedef enum {
  PPCLK_GFXCLK,
  PPCLK_VCLK,
  PPCLK_DCLK,
  PPCLK_SOCCLK,
  PPCLK_UCLK,
  PPCLK_FCLK,
  PPCLK_COUNT,
} PPCLK_e;

typedef enum {
  POWER_SOURCE_AC,
  POWER_SOURCE_DC,
  POWER_SOURCE_COUNT,
} POWER_SOURCE_e;

typedef enum {
  TEMP_EDGE,
  TEMP_HOTSPOT,
  TEMP_MEM,
  TEMP_VR_GFX,
  TEMP_VR_SOC,
  TEMP_VR_MEM,
  TEMP_COUNT
} TEMP_TYPE_e;

typedef enum  {
  PPT_THROTTLER_PPT0,
  PPT_THROTTLER_PPT1,
  PPT_THROTTLER_PPT2,
  PPT_THROTTLER_PPT3,
  PPT_THROTTLER_COUNT
} PPT_THROTTLER_e;

typedef enum {
  VOLTAGE_MODE_AVFS = 0,
  VOLTAGE_MODE_AVFS_SS,
  VOLTAGE_MODE_SS,
  VOLTAGE_MODE_COUNT,
} VOLTAGE_MODE_e;

typedef enum {
  AVFS_VOLTAGE_GFX = 0,
  AVFS_VOLTAGE_SOC,
  AVFS_VOLTAGE_COUNT,
} AVFS_VOLTAGE_TYPE_e;

typedef enum {
  GPIO_INT_POLARITY_ACTIVE_LOW = 0,
  GPIO_INT_POLARITY_ACTIVE_HIGH,
} GpioIntPolarity_e;

typedef enum {
  MEMORY_TYPE_GDDR6 = 0,
  MEMORY_TYPE_HBM,
} MemoryType_e;

typedef enum {
  PWR_CONFIG_TDP = 0,
  PWR_CONFIG_TGP,
  PWR_CONFIG_TCP_ESTIMATED,
  PWR_CONFIG_TCP_MEASURED,
} PwrConfig_e;

typedef enum {
  XGMI_LINK_RATE_2 = 2,    // 2Gbps
  XGMI_LINK_RATE_4 = 4,    // 4Gbps
  XGMI_LINK_RATE_8 = 8,    // 8Gbps
  XGMI_LINK_RATE_12 = 12,  // 12Gbps
  XGMI_LINK_RATE_16 = 16,  // 16Gbps
  XGMI_LINK_RATE_17 = 17,  // 17Gbps
  XGMI_LINK_RATE_18 = 18,  // 18Gbps
  XGMI_LINK_RATE_19 = 19,  // 19Gbps
  XGMI_LINK_RATE_20 = 20,  // 20Gbps
  XGMI_LINK_RATE_21 = 21,  // 21Gbps
  XGMI_LINK_RATE_22 = 22,  // 22Gbps
  XGMI_LINK_RATE_23 = 23,  // 23Gbps
  XGMI_LINK_RATE_24 = 24,  // 24Gbps
  XGMI_LINK_RATE_25 = 25,  // 25Gbps
  XGMI_LINK_RATE_COUNT
} XGMI_LINK_RATE_e;

typedef enum {
  XGMI_LINK_WIDTH_1 = 1,   // x1
  XGMI_LINK_WIDTH_2 = 2,   // x2
  XGMI_LINK_WIDTH_4 = 4,   // x4
  XGMI_LINK_WIDTH_8 = 8,   // x8
  XGMI_LINK_WIDTH_9 = 9,   // x9
  XGMI_LINK_WIDTH_16 = 16, // x16
  XGMI_LINK_WIDTH_COUNT
} XGMI_LINK_WIDTH_e;

typedef struct {
  uint8_t        VoltageMode;         // 0 - AVFS only, 1- min(AVFS,SS), 2-SS only
  uint8_t        SnapToDiscrete;      // 0 - Fine grained DPM, 1 - Discrete DPM
  uint8_t        NumDiscreteLevels;   // Set to 2 (Fmin, Fmax) when using fine grained DPM, otherwise set to # discrete levels used
  uint8_t        padding;
  LinearInt_t    ConversionToAvfsClk; // Transfer function to AVFS Clock (GHz->GHz)
  QuadraticInt_t SsCurve;             // Slow-slow curve (GHz->V)
  uint16_t       SsFmin;              // Fmin for SS curve. If SS curve is selected, will use V@SSFmin for F <= Fmin
  uint16_t       Padding16;
} DpmDescriptor_t;

typedef struct {
  uint32_t Version;

  // SECTION: Feature Enablement
  uint32_t FeaturesToRun[2];

  // SECTION: Infrastructure Limits
  uint16_t SocketPowerLimitAc[PPT_THROTTLER_COUNT];
  uint16_t SocketPowerLimitAcTau[PPT_THROTTLER_COUNT];
  uint16_t TdcLimitSoc;             // Amps
  uint16_t TdcLimitSocTau;          // Time constant of LPF in ms
  uint16_t TdcLimitGfx;             // Amps
  uint16_t TdcLimitGfxTau;          // Time constant of LPF in ms

  uint16_t TedgeLimit;              // Celcius
  uint16_t ThotspotLimit;           // Celcius
  uint16_t TmemLimit;               // Celcius
  uint16_t Tvr_gfxLimit;            // Celcius
  uint16_t Tvr_memLimit;            // Celcius
  uint16_t Tvr_socLimit;            // Celcius
  uint32_t FitLimit;                // Failures in time (failures per million parts over the defined lifetime)

  uint16_t PpmPowerLimit;           // Switch this this power limit when temperature is above PpmTempThreshold
  uint16_t PpmTemperatureThreshold;

  // SECTION: Throttler settings
  uint32_t ThrottlerControlMask;   // See Throtter masks defines

  // SECTION: ULV Settings
  uint16_t  UlvVoltageOffsetGfx; // In mV(Q2)
  uint16_t  UlvPadding;          // Padding

  uint8_t  UlvGfxclkBypass;  // 1 to turn off/bypass Gfxclk during ULV, 0 to leave Gfxclk on during ULV
  uint8_t  Padding234[3];

  // SECTION: Voltage Control Parameters
  uint16_t     MinVoltageGfx;     // In mV(Q2) Minimum Voltage ("Vmin") of VDD_GFX
  uint16_t     MinVoltageSoc;     // In mV(Q2) Minimum Voltage ("Vmin") of VDD_SOC
  uint16_t     MaxVoltageGfx;     // In mV(Q2) Maximum Voltage allowable of VDD_GFX
  uint16_t     MaxVoltageSoc;     // In mV(Q2) Maximum Voltage allowable of VDD_SOC

  uint16_t     LoadLineResistanceGfx;   // In mOhms with 8 fractional bits
  uint16_t     LoadLineResistanceSoc;   // In mOhms with 8 fractional bits

  //SECTION: DPM Config 1
  DpmDescriptor_t DpmDescriptor[PPCLK_COUNT];

  uint16_t       FreqTableGfx      [NUM_GFXCLK_DPM_LEVELS  ];     // In MHz
  uint16_t       FreqTableVclk     [NUM_VCLK_DPM_LEVELS    ];     // In MHz
  uint16_t       FreqTableDclk     [NUM_DCLK_DPM_LEVELS    ];     // In MHz
  uint16_t       FreqTableSocclk   [NUM_SOCCLK_DPM_LEVELS  ];     // In MHz
  uint16_t       FreqTableUclk     [NUM_UCLK_DPM_LEVELS    ];     // In MHz
  uint16_t       FreqTableFclk     [NUM_FCLK_DPM_LEVELS    ];     // In MHz

  uint32_t       Paddingclks[16];

  // SECTION: DPM Config 2
  uint16_t       Mp0clkFreq        [NUM_MP0CLK_DPM_LEVELS];       // in MHz
  uint16_t       Mp0DpmVoltage     [NUM_MP0CLK_DPM_LEVELS];       // mV(Q2)

  // GFXCLK DPM
  uint16_t        GfxclkFidle;          // In MHz
  uint16_t        GfxclkSlewRate;       // for PLL babystepping???
  uint8_t         Padding567[4];
  uint16_t        GfxclkDsMaxFreq;      // In MHz
  uint8_t         GfxclkSource;         // 0 = PLL, 1 = AFLL
  uint8_t         Padding456;

  // GFXCLK Thermal DPM (formerly 'Boost' Settings)
  uint16_t     EnableTdpm;
  uint16_t     TdpmHighHystTemperature;
  uint16_t     TdpmLowHystTemperature;
  uint16_t     GfxclkFreqHighTempLimit; // High limit on GFXCLK when temperature is high, for reliability.

  // SECTION: Fan Control
  uint16_t     FanStopTemp;          //Celcius
  uint16_t     FanStartTemp;         //Celcius

  uint16_t     FanGainEdge;
  uint16_t     FanGainHotspot;
  uint16_t     FanGainVrGfx;
  uint16_t     FanGainVrSoc;
  uint16_t     FanGainVrMem;
  uint16_t     FanGainHbm;
  uint16_t     FanPwmMin;
  uint16_t     FanAcousticLimitRpm;
  uint16_t     FanThrottlingRpm;
  uint16_t     FanMaximumRpm;
  uint16_t     FanTargetTemperature;
  uint16_t     FanTargetGfxclk;
  uint8_t      FanZeroRpmEnable;
  uint8_t      FanTachEdgePerRev;
  uint8_t      FanTempInputSelect;
  uint8_t      padding8_Fan;

  // The following are AFC override parameters. Leave at 0 to use FW defaults.
  int16_t      FuzzyFan_ErrorSetDelta;
  int16_t      FuzzyFan_ErrorRateSetDelta;
  int16_t      FuzzyFan_PwmSetDelta;
  uint16_t     FuzzyFan_Reserved;


  // SECTION: AVFS
  // Overrides
  uint8_t           OverrideAvfsGb[AVFS_VOLTAGE_COUNT];
  uint8_t           Padding8_Avfs[2];

  QuadraticInt_t    qAvfsGb[AVFS_VOLTAGE_COUNT];              // GHz->V Override of fused curve
  DroopInt_t        dBtcGbGfxPll;       // GHz->V BtcGb
  DroopInt_t        dBtcGbGfxAfll;        // GHz->V BtcGb
  DroopInt_t        dBtcGbSoc;            // GHz->V BtcGb
  LinearInt_t       qAgingGb[AVFS_VOLTAGE_COUNT];          // GHz->V

  QuadraticInt_t    qStaticVoltageOffset[AVFS_VOLTAGE_COUNT]; // GHz->V

  uint16_t          DcTol[AVFS_VOLTAGE_COUNT];            // mV Q2

  uint8_t           DcBtcEnabled[AVFS_VOLTAGE_COUNT];
  uint8_t           Padding8_GfxBtc[2];

  uint16_t          DcBtcMin[AVFS_VOLTAGE_COUNT];       // mV Q2
  uint16_t          DcBtcMax[AVFS_VOLTAGE_COUNT];       // mV Q2

  uint16_t          DcBtcGb[AVFS_VOLTAGE_COUNT];        // mV Q2

  // SECTION: XGMI
  uint8_t           XgmiDpmPstates[NUM_XGMI_LEVELS]; // 2 DPM states, high and low.  0-P0, 1-P1, 2-P2, 3-P3.
  uint8_t           XgmiDpmSpare[2];

  // Temperature Dependent Vmin
  uint16_t     VDDGFX_TVmin;       //Celcius
  uint16_t     VDDSOC_TVmin;       //Celcius
  uint16_t     VDDGFX_Vmin_HiTemp; // mV Q2
  uint16_t     VDDGFX_Vmin_LoTemp; // mV Q2
  uint16_t     VDDSOC_Vmin_HiTemp; // mV Q2
  uint16_t     VDDSOC_Vmin_LoTemp; // mV Q2

  uint16_t     VDDGFX_TVminHystersis; // Celcius
  uint16_t     VDDSOC_TVminHystersis; // Celcius


  // SECTION: Advanced Options
  uint32_t          DebugOverrides;
  QuadraticInt_t    ReservedEquation0;
  QuadraticInt_t    ReservedEquation1;
  QuadraticInt_t    ReservedEquation2;
  QuadraticInt_t    ReservedEquation3;

  uint16_t     MinVoltageUlvGfx; // In mV(Q2)  Minimum Voltage ("Vmin") of VDD_GFX in ULV mode
  uint16_t     PaddingUlv;       // Padding

  // Total Power configuration, use defines from PwrConfig_e
  uint8_t      TotalPowerConfig;    //0-TDP, 1-TGP, 2-TCP Estimated, 3-TCP Measured
  uint8_t      TotalPowerSpare1;
  uint16_t     TotalPowerSpare2;

  // APCC Settings
  uint16_t     PccThresholdLow;
  uint16_t     PccThresholdHigh;
  uint32_t     PaddingAPCC[6];  //FIXME pending SPEC

  // OOB Settings
  uint16_t BasePerformanceCardPower;
  uint16_t MaxPerformanceCardPower;
  uint16_t BasePerformanceFrequencyCap;   //In Mhz
  uint16_t MaxPerformanceFrequencyCap;    //In Mhz

  // Per-Part Vmin
  uint16_t VDDGFX_VminLow;        // mv Q2
  uint16_t VDDGFX_TVminLow;       //Celcius
  uint16_t VDDGFX_VminLow_HiTemp; // mv Q2
  uint16_t VDDGFX_VminLow_LoTemp; // mv Q2

  // SECTION: Reserved
  uint32_t     Reserved[7];

  // SECTION: BOARD PARAMETERS

  // SVI2 Board Parameters
  uint16_t     MaxVoltageStepGfx; // In mV(Q2) Max voltage step that SMU will request. Multiple steps are taken if voltage change exceeds this value.
  uint16_t     MaxVoltageStepSoc; // In mV(Q2) Max voltage step that SMU will request. Multiple steps are taken if voltage change exceeds this value.

  uint8_t      VddGfxVrMapping;     // Use VR_MAPPING* bitfields
  uint8_t      VddSocVrMapping;     // Use VR_MAPPING* bitfields
  uint8_t      VddMemVrMapping;     // Use VR_MAPPING* bitfields
  uint8_t      BoardVrMapping;      // Use VR_MAPPING* bitfields

  uint8_t      GfxUlvPhaseSheddingMask; // set this to 1 to set PSI0/1 to 1 in ULV mode
  uint8_t      ExternalSensorPresent; // External RDI connected to TMON (aka TEMP IN)
  uint8_t      Padding8_V[2];

  // Telemetry Settings
  uint16_t     GfxMaxCurrent;   // in Amps
  int8_t       GfxOffset;       // in Amps
  uint8_t      Padding_TelemetryGfx;

  uint16_t     SocMaxCurrent;   // in Amps
  int8_t       SocOffset;       // in Amps
  uint8_t      Padding_TelemetrySoc;

  uint16_t     MemMaxCurrent;   // in Amps
  int8_t       MemOffset;       // in Amps
  uint8_t      Padding_TelemetryMem;

  uint16_t     BoardMaxCurrent;   // in Amps
  int8_t       BoardOffset;       // in Amps
  uint8_t      Padding_TelemetryBoardInput;

  // GPIO Settings
  uint8_t      VR0HotGpio;      // GPIO pin configured for VR0 HOT event
  uint8_t      VR0HotPolarity;  // GPIO polarity for VR0 HOT event
  uint8_t      VR1HotGpio;      // GPIO pin configured for VR1 HOT event
  uint8_t      VR1HotPolarity;  // GPIO polarity for VR1 HOT event

  // GFXCLK PLL Spread Spectrum
  uint8_t      PllGfxclkSpreadEnabled;   // on or off
  uint8_t      PllGfxclkSpreadPercent;   // Q4.4
  uint16_t     PllGfxclkSpreadFreq;      // kHz

  // UCLK Spread Spectrum
  uint8_t      UclkSpreadEnabled;   // on or off
  uint8_t      UclkSpreadPercent;   // Q4.4
  uint16_t     UclkSpreadFreq;      // kHz

  // FCLK Spread Spectrum
  uint8_t      FclkSpreadEnabled;   // on or off
  uint8_t      FclkSpreadPercent;   // Q4.4
  uint16_t     FclkSpreadFreq;      // kHz

  // GFXCLK Fll Spread Spectrum
  uint8_t      FllGfxclkSpreadEnabled;   // on or off
  uint8_t      FllGfxclkSpreadPercent;   // Q4.4
  uint16_t     FllGfxclkSpreadFreq;      // kHz

  // I2C Controller Structure
  I2cControllerConfig_t  I2cControllers[NUM_I2C_CONTROLLERS];

  // Memory section
  uint32_t     MemoryChannelEnabled; // For DRAM use only, Max 32 channels enabled bit mask.

  uint8_t      DramBitWidth; // For DRAM use only.  See Dram Bit width type defines
  uint8_t      PaddingMem[3];

  // Total board power
  uint16_t     TotalBoardPower;     //Only needed for TCP Estimated case, where TCP = TGP+Total Board Power
  uint16_t     BoardPadding;

  // SECTION: XGMI Training
  uint8_t           XgmiLinkSpeed   [NUM_XGMI_PSTATE_LEVELS];
  uint8_t           XgmiLinkWidth   [NUM_XGMI_PSTATE_LEVELS];

  uint16_t          XgmiFclkFreq    [NUM_XGMI_PSTATE_LEVELS];
  uint16_t          XgmiSocVoltage  [NUM_XGMI_PSTATE_LEVELS];

  // GPIO pins for I2C communications with 2nd controller for Input Telemetry Sequence
  uint8_t      GpioI2cScl;          // Serial Clock
  uint8_t      GpioI2cSda;          // Serial Data
  uint16_t     GpioPadding;

  // Platform input telemetry voltage coefficient
  uint32_t     BoardVoltageCoeffA;    // decode by /1000
  uint32_t     BoardVoltageCoeffB;    // decode by /1000

  uint32_t     BoardReserved[7];

  // Padding for MMHUB - do not modify this
  uint32_t     MmHubPadding[8]; // SMU internal use

} PPTable_t;

typedef struct {
  // Time constant parameters for clock averages in ms
  uint16_t     GfxclkAverageLpfTau;
  uint16_t     SocclkAverageLpfTau;
  uint16_t     UclkAverageLpfTau;
  uint16_t     GfxActivityLpfTau;
  uint16_t     UclkActivityLpfTau;

  uint16_t     SocketPowerLpfTau;

  // Padding - ignore
  uint32_t     MmHubPadding[8]; // SMU internal use
} DriverSmuConfig_t;

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
  uint16_t AverageSocketPower    ;
  uint16_t TemperatureEdge       ;
  uint16_t TemperatureHotspot    ;
  uint16_t TemperatureHBM        ;
  uint16_t TemperatureVrGfx      ;
  uint16_t TemperatureVrSoc      ;
  uint16_t TemperatureVrMem      ;
  uint32_t ThrottlerStatus       ;

  uint16_t CurrFanSpeed          ;
  uint16_t Padding16;

  uint32_t Padding[4];

  // Padding - ignore
  uint32_t     MmHubPadding[8]; // SMU internal use
} SmuMetrics_t;


typedef struct {
  uint16_t avgPsmCount[75];
  uint16_t minPsmCount[75];
  float    avgPsmVoltage[75];
  float    minPsmVoltage[75];

  uint32_t MmHubPadding[8]; // SMU internal use
} AvfsDebugTable_t;

typedef struct {
  uint8_t  AvfsVersion;
  uint8_t  Padding;
  uint8_t  AvfsEn[AVFS_VOLTAGE_COUNT];

  uint8_t  OverrideVFT[AVFS_VOLTAGE_COUNT];
  uint8_t  OverrideAvfsGb[AVFS_VOLTAGE_COUNT];

  uint8_t  OverrideTemperatures[AVFS_VOLTAGE_COUNT];
  uint8_t  OverrideVInversion[AVFS_VOLTAGE_COUNT];
  uint8_t  OverrideP2V[AVFS_VOLTAGE_COUNT];
  uint8_t  OverrideP2VCharzFreq[AVFS_VOLTAGE_COUNT];

  int32_t VFT0_m1[AVFS_VOLTAGE_COUNT]; // Q8.24
  int32_t VFT0_m2[AVFS_VOLTAGE_COUNT]; // Q12.12
  int32_t VFT0_b[AVFS_VOLTAGE_COUNT];  // Q32

  int32_t VFT1_m1[AVFS_VOLTAGE_COUNT]; // Q8.16
  int32_t VFT1_m2[AVFS_VOLTAGE_COUNT]; // Q12.12
  int32_t VFT1_b[AVFS_VOLTAGE_COUNT];  // Q32

  int32_t VFT2_m1[AVFS_VOLTAGE_COUNT]; // Q8.16
  int32_t VFT2_m2[AVFS_VOLTAGE_COUNT]; // Q12.12
  int32_t VFT2_b[AVFS_VOLTAGE_COUNT];  // Q32

  int32_t AvfsGb0_m1[AVFS_VOLTAGE_COUNT]; // Q8.24
  int32_t AvfsGb0_m2[AVFS_VOLTAGE_COUNT]; // Q12.12
  int32_t AvfsGb0_b[AVFS_VOLTAGE_COUNT];  // Q32

  int32_t AcBtcGb_m1[AVFS_VOLTAGE_COUNT]; // Q8.24
  int32_t AcBtcGb_m2[AVFS_VOLTAGE_COUNT]; // Q12.12
  int32_t AcBtcGb_b[AVFS_VOLTAGE_COUNT];  // Q32

  uint32_t AvfsTempCold[AVFS_VOLTAGE_COUNT];
  uint32_t AvfsTempMid[AVFS_VOLTAGE_COUNT];
  uint32_t AvfsTempHot[AVFS_VOLTAGE_COUNT];

  uint32_t VInversion[AVFS_VOLTAGE_COUNT]; // in mV with 2 fractional bits


  int32_t P2V_m1[AVFS_VOLTAGE_COUNT]; // Q8.24
  int32_t P2V_m2[AVFS_VOLTAGE_COUNT]; // Q12.12
  int32_t P2V_b[AVFS_VOLTAGE_COUNT];  // Q32

  uint32_t P2VCharzFreq[AVFS_VOLTAGE_COUNT]; // in 10KHz units

  uint32_t EnabledAvfsModules[3];

  uint32_t MmHubPadding[8]; // SMU internal use
} AvfsFuseOverride_t;

typedef struct {
  uint8_t   Gfx_ActiveHystLimit;
  uint8_t   Gfx_IdleHystLimit;
  uint8_t   Gfx_FPS;
  uint8_t   Gfx_MinActiveFreqType;
  uint8_t   Gfx_BoosterFreqType;
  uint8_t   Gfx_MinFreqStep;                // Minimum delta between current and target frequeny in order for FW to change clock.
  uint8_t   Gfx_UseRlcBusy;
  uint8_t   PaddingGfx[3];
  uint16_t  Gfx_MinActiveFreq;              // MHz
  uint16_t  Gfx_BoosterFreq;                // MHz
  uint16_t  Gfx_PD_Data_time_constant;      // Time constant of PD controller in ms
  uint32_t  Gfx_PD_Data_limit_a;            // Q16
  uint32_t  Gfx_PD_Data_limit_b;            // Q16
  uint32_t  Gfx_PD_Data_limit_c;            // Q16
  uint32_t  Gfx_PD_Data_error_coeff;        // Q16
  uint32_t  Gfx_PD_Data_error_rate_coeff;   // Q16

  uint8_t   Mem_ActiveHystLimit;
  uint8_t   Mem_IdleHystLimit;
  uint8_t   Mem_FPS;
  uint8_t   Mem_MinActiveFreqType;
  uint8_t   Mem_BoosterFreqType;
  uint8_t   Mem_MinFreqStep;                // Minimum delta between current and target frequeny in order for FW to change clock.
  uint8_t   Mem_UseRlcBusy;
  uint8_t   PaddingMem[3];
  uint16_t  Mem_MinActiveFreq;              // MHz
  uint16_t  Mem_BoosterFreq;                // MHz
  uint16_t  Mem_PD_Data_time_constant;      // Time constant of PD controller in ms
  uint32_t  Mem_PD_Data_limit_a;            // Q16
  uint32_t  Mem_PD_Data_limit_b;            // Q16
  uint32_t  Mem_PD_Data_limit_c;            // Q16
  uint32_t  Mem_PD_Data_error_coeff;        // Q16
  uint32_t  Mem_PD_Data_error_rate_coeff;   // Q16

  uint32_t  Mem_UpThreshold_Limit;          // Q16
  uint8_t   Mem_UpHystLimit;
  uint8_t   Mem_DownHystLimit;
  uint16_t  Mem_Fps;

  uint32_t  BusyThreshold;                  // Q16
  uint32_t  BusyHyst;
  uint32_t  IdleHyst;

  uint32_t  MmHubPadding[8]; // SMU internal use
} DpmActivityMonitorCoeffInt_t;

// These defines are used with the following messages:
// SMC_MSG_TransferTableDram2Smu
// SMC_MSG_TransferTableSmu2Dram
#define TABLE_PPTABLE                 0
#define TABLE_AVFS                    1
#define TABLE_AVFS_PSM_DEBUG          2
#define TABLE_AVFS_FUSE_OVERRIDE      3
#define TABLE_PMSTATUSLOG             4
#define TABLE_SMU_METRICS             5
#define TABLE_DRIVER_SMU_CONFIG       6
#define TABLE_OVERDRIVE               7
#define TABLE_WAFL_XGMI_TOPOLOGY      8
#define TABLE_I2C_COMMANDS            9
#define TABLE_ACTIVITY_MONITOR_COEFF  10
#define TABLE_COUNT                   11

// These defines are used with the SMC_MSG_SetUclkFastSwitch message.
typedef enum {
  DF_SWITCH_TYPE_FAST = 0,
  DF_SWITCH_TYPE_SLOW,
  DF_SWITCH_TYPE_COUNT,
} DF_SWITCH_TYPE_e;

typedef enum {
  DRAM_BIT_WIDTH_DISABLED = 0,
  DRAM_BIT_WIDTH_X_8,
  DRAM_BIT_WIDTH_X_16,
  DRAM_BIT_WIDTH_X_32,
  DRAM_BIT_WIDTH_X_64, // NOT USED.
  DRAM_BIT_WIDTH_X_128,
  DRAM_BIT_WIDTH_COUNT,
} DRAM_BIT_WIDTH_TYPE_e;

#define REMOVE_FMAX_MARGIN_BIT     0x0
#define REMOVE_DCTOL_MARGIN_BIT    0x1
#define REMOVE_PLATFORM_MARGIN_BIT 0x2

#endif
