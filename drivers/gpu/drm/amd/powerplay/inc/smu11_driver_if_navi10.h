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
#ifndef __SMU11_DRIVER_IF_NAVI10_H__
#define __SMU11_DRIVER_IF_NAVI10_H__

// *** IMPORTANT ***
// SMU TEAM: Always increment the interface version if 
// any structure is changed in this file
// Be aware of that the version should be updated in
// smu_v11_0.h, maybe rename is also needed.
// #define SMU11_DRIVER_IF_VERSION 0x33

#define PPTABLE_NV10_SMU_VERSION 8

#define NUM_GFXCLK_DPM_LEVELS  16
#define NUM_SMNCLK_DPM_LEVELS  2
#define NUM_SOCCLK_DPM_LEVELS  8
#define NUM_MP0CLK_DPM_LEVELS  2
#define NUM_DCLK_DPM_LEVELS    8
#define NUM_VCLK_DPM_LEVELS    8
#define NUM_DCEFCLK_DPM_LEVELS 8
#define NUM_PHYCLK_DPM_LEVELS  8
#define NUM_DISPCLK_DPM_LEVELS 8
#define NUM_PIXCLK_DPM_LEVELS  8
#define NUM_UCLK_DPM_LEVELS    4 
#define NUM_MP1CLK_DPM_LEVELS  2
#define NUM_LINK_LEVELS        2


#define MAX_GFXCLK_DPM_LEVEL  (NUM_GFXCLK_DPM_LEVELS  - 1)
#define MAX_SMNCLK_DPM_LEVEL  (NUM_SMNCLK_DPM_LEVELS  - 1)
#define MAX_SOCCLK_DPM_LEVEL  (NUM_SOCCLK_DPM_LEVELS  - 1)
#define MAX_MP0CLK_DPM_LEVEL  (NUM_MP0CLK_DPM_LEVELS  - 1)
#define MAX_DCLK_DPM_LEVEL    (NUM_DCLK_DPM_LEVELS    - 1)
#define MAX_VCLK_DPM_LEVEL    (NUM_VCLK_DPM_LEVELS    - 1)
#define MAX_DCEFCLK_DPM_LEVEL (NUM_DCEFCLK_DPM_LEVELS - 1)
#define MAX_DISPCLK_DPM_LEVEL (NUM_DISPCLK_DPM_LEVELS - 1)
#define MAX_PIXCLK_DPM_LEVEL  (NUM_PIXCLK_DPM_LEVELS  - 1)
#define MAX_PHYCLK_DPM_LEVEL  (NUM_PHYCLK_DPM_LEVELS  - 1)
#define MAX_UCLK_DPM_LEVEL    (NUM_UCLK_DPM_LEVELS    - 1)
#define MAX_MP1CLK_DPM_LEVEL  (NUM_MP1CLK_DPM_LEVELS  - 1)
#define MAX_LINK_LEVEL        (NUM_LINK_LEVELS        - 1)

//Gemini Modes
#define PPSMC_GeminiModeNone   0  //Single GPU board
#define PPSMC_GeminiModeMaster 1  //Master GPU on a Gemini board
#define PPSMC_GeminiModeSlave  2  //Slave GPU on a Gemini board

// Feature Control Defines
// DPM
#define FEATURE_DPM_PREFETCHER_BIT      0
#define FEATURE_DPM_GFXCLK_BIT          1
#define FEATURE_DPM_GFX_PACE_BIT        2
#define FEATURE_DPM_UCLK_BIT            3
#define FEATURE_DPM_SOCCLK_BIT          4
#define FEATURE_DPM_MP0CLK_BIT          5
#define FEATURE_DPM_LINK_BIT            6
#define FEATURE_DPM_DCEFCLK_BIT         7
#define FEATURE_MEM_VDDCI_SCALING_BIT   8 
#define FEATURE_MEM_MVDD_SCALING_BIT    9
                                        
//Idle                                  
#define FEATURE_DS_GFXCLK_BIT           10
#define FEATURE_DS_SOCCLK_BIT           11
#define FEATURE_DS_LCLK_BIT             12
#define FEATURE_DS_DCEFCLK_BIT          13
#define FEATURE_DS_UCLK_BIT             14
#define FEATURE_GFX_ULV_BIT             15  
#define FEATURE_FW_DSTATE_BIT           16 
#define FEATURE_GFXOFF_BIT              17
#define FEATURE_BACO_BIT                18
#define FEATURE_VCN_PG_BIT              19  
#define FEATURE_JPEG_PG_BIT             20
#define FEATURE_USB_PG_BIT              21
#define FEATURE_RSMU_SMN_CG_BIT         22
//Throttler/Response                    
#define FEATURE_PPT_BIT                 23
#define FEATURE_TDC_BIT                 24
#define FEATURE_GFX_EDC_BIT             25
#define FEATURE_APCC_PLUS_BIT           26
#define FEATURE_GTHR_BIT                27
#define FEATURE_ACDC_BIT                28
#define FEATURE_VR0HOT_BIT              29
#define FEATURE_VR1HOT_BIT              30  
#define FEATURE_FW_CTF_BIT              31
#define FEATURE_FAN_CONTROL_BIT         32
#define FEATURE_THERMAL_BIT             33
#define FEATURE_GFX_DCS_BIT             34
//VF                                    
#define FEATURE_RM_BIT                  35
#define FEATURE_LED_DISPLAY_BIT         36
//Other                                 
#define FEATURE_GFX_SS_BIT              37
#define FEATURE_OUT_OF_BAND_MONITOR_BIT 38
#define FEATURE_TEMP_DEPENDENT_VMIN_BIT 39

#define FEATURE_MMHUB_PG_BIT            40
#define FEATURE_ATHUB_PG_BIT            41
#define FEATURE_APCC_DFLL_BIT           42
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

// Debug Overrides Bitmask
#define DPM_OVERRIDE_DISABLE_SOCCLK_PID             0x00000001
#define DPM_OVERRIDE_DISABLE_UCLK_PID               0x00000002
#define DPM_OVERRIDE_DISABLE_VOLT_LINK_VCN_SOCCLK   0x00000004
#define DPM_OVERRIDE_ENABLE_FREQ_LINK_VCLK_SOCCLK   0x00000008
#define DPM_OVERRIDE_ENABLE_FREQ_LINK_DCLK_SOCCLK   0x00000010
#define DPM_OVERRIDE_ENABLE_FREQ_LINK_GFXCLK_SOCCLK 0x00000020
#define DPM_OVERRIDE_ENABLE_FREQ_LINK_GFXCLK_UCLK   0x00000040
#define DPM_OVERRIDE_DISABLE_VOLT_LINK_DCE_SOCCLK   0x00000080
#define DPM_OVERRIDE_DISABLE_VOLT_LINK_MP0_SOCCLK   0x00000100
#define DPM_OVERRIDE_DISABLE_DFLL_PLL_SHUTDOWN      0x00000200
#define DPM_OVERRIDE_DISABLE_MEMORY_TEMPERATURE_READ 0x00000400

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
#define THROTTLER_TEMP_VR_MEM0_BIT 5
#define THROTTLER_TEMP_VR_MEM1_BIT 6
#define THROTTLER_TEMP_VR_SOC_BIT  7
#define THROTTLER_TEMP_LIQUID0_BIT 8
#define THROTTLER_TEMP_LIQUID1_BIT 9
#define THROTTLER_TEMP_PLX_BIT     10
#define THROTTLER_TEMP_SKIN_BIT    11
#define THROTTLER_TDC_GFX_BIT      12
#define THROTTLER_TDC_SOC_BIT      13
#define THROTTLER_PPT0_BIT         14
#define THROTTLER_PPT1_BIT         15
#define THROTTLER_PPT2_BIT         16
#define THROTTLER_PPT3_BIT         17
#define THROTTLER_FIT_BIT          18
#define THROTTLER_PPM_BIT          19
#define THROTTLER_APCC_BIT         20

// FW DState Features Control Bits
#define FW_DSTATE_SOC_ULV_BIT              0
#define FW_DSTATE_G6_HSR_BIT               1
#define FW_DSTATE_G6_PHY_VDDCI_OFF_BIT     2
#define FW_DSTATE_MP0_DS_BIT               3
#define FW_DSTATE_SMN_DS_BIT               4
#define FW_DSTATE_MP1_DS_BIT               5
#define FW_DSTATE_MP1_WHISPER_MODE_BIT     6
#define FW_DSTATE_LIV_MIN_BIT              7
#define FW_DSTATE_SOC_PLL_PWRDN_BIT        8   

#define FW_DSTATE_SOC_ULV_MASK             (1 << FW_DSTATE_SOC_ULV_BIT          )
#define FW_DSTATE_G6_HSR_MASK              (1 << FW_DSTATE_G6_HSR_BIT           )
#define FW_DSTATE_G6_PHY_VDDCI_OFF_MASK    (1 << FW_DSTATE_G6_PHY_VDDCI_OFF_BIT )
#define FW_DSTATE_MP1_DS_MASK              (1 << FW_DSTATE_MP1_DS_BIT           )  
#define FW_DSTATE_MP0_DS_MASK              (1 << FW_DSTATE_MP0_DS_BIT           )   
#define FW_DSTATE_SMN_DS_MASK              (1 << FW_DSTATE_SMN_DS_BIT           )
#define FW_DSTATE_MP1_WHISPER_MODE_MASK    (1 << FW_DSTATE_MP1_WHISPER_MODE_BIT )
#define FW_DSTATE_LIV_MIN_MASK             (1 << FW_DSTATE_LIV_MIN_BIT          )
#define FW_DSTATE_SOC_PLL_PWRDN_MASK       (1 << FW_DSTATE_SOC_PLL_PWRDN_BIT    )

//I2C Interface

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
  I2C_CONTROLLER_NAME_VR_VDDCI,
  I2C_CONTROLLER_NAME_VR_MVDD,
  I2C_CONTROLLER_NAME_LIQUID0,
  I2C_CONTROLLER_NAME_LIQUID1,  
  I2C_CONTROLLER_NAME_PLX,
  I2C_CONTROLLER_NAME_SPARE,
  I2C_CONTROLLER_NAME_COUNT,  
} I2cControllerName_e;

typedef enum {
  I2C_CONTROLLER_THROTTLER_TYPE_NONE = 0,
  I2C_CONTROLLER_THROTTLER_VR_GFX,
  I2C_CONTROLLER_THROTTLER_VR_SOC,
  I2C_CONTROLLER_THROTTLER_VR_VDDCI,
  I2C_CONTROLLER_THROTTLER_VR_MVDD,
  I2C_CONTROLLER_THROTTLER_LIQUID0,
  I2C_CONTROLLER_THROTTLER_LIQUID1,  
  I2C_CONTROLLER_THROTTLER_PLX,
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
  GFXCLK_SOURCE_DFLL, 
  GFXCLK_SOURCE_COUNT, 
} GfxclkSrc_e; 

//Only Clks that have DPM descriptors are listed here 
typedef enum {
  PPCLK_GFXCLK = 0,
  PPCLK_SOCCLK,
  PPCLK_UCLK,
  PPCLK_DCLK,
  PPCLK_VCLK,
  PPCLK_DCEFCLK,
  PPCLK_DISPCLK,
  PPCLK_PIXCLK,
  PPCLK_PHYCLK,
  PPCLK_COUNT,
} PPCLK_e;

typedef enum {
  POWER_SOURCE_AC,
  POWER_SOURCE_DC,
  POWER_SOURCE_COUNT,
} POWER_SOURCE_e;

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
  UCLK_DIV_BY_1 = 0,
  UCLK_DIV_BY_2,
  UCLK_DIV_BY_4,
  UCLK_DIV_BY_8,
} UCLK_DIV_e;

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

typedef struct {
  uint8_t        VoltageMode;         // 0 - AVFS only, 1- min(AVFS,SS), 2-SS only
  uint8_t        SnapToDiscrete;      // 0 - Fine grained DPM, 1 - Discrete DPM
  uint8_t        NumDiscreteLevels;   // Set to 2 (Fmin, Fmax) when using fine grained DPM, otherwise set to # discrete levels used
  uint8_t        Padding;         
  LinearInt_t    ConversionToAvfsClk; // Transfer function to AVFS Clock (GHz->GHz)
  QuadraticInt_t SsCurve;             // Slow-slow curve (GHz->V)
} DpmDescriptor_t;

typedef enum  {
  TEMP_EDGE,
  TEMP_HOTSPOT,
  TEMP_MEM,
  TEMP_VR_GFX,
  TEMP_VR_MEM0,
  TEMP_VR_MEM1,
  TEMP_VR_SOC,  
  TEMP_LIQUID0,
  TEMP_LIQUID1,  
  TEMP_PLX,
  TEMP_COUNT
} TEMP_e;

//Out of band monitor status defines
//see SPEC //gpu/doc/soc_arch/spec/feature/SMBUS/SMBUS.xlsx
#define POWER_MANAGER_CONTROLLER_NOT_RUNNING 0
#define POWER_MANAGER_CONTROLLER_RUNNING     1

#define POWER_MANAGER_CONTROLLER_BIT                             0
#define MAXIMUM_DPM_STATE_GFX_ENGINE_RESTRICTED_BIT              8
#define GPU_DIE_TEMPERATURE_THROTTLING_BIT                       9
#define HBM_DIE_TEMPERATURE_THROTTLING_BIT                       10
#define TGP_THROTTLING_BIT                                       11
#define PCC_THROTTLING_BIT                                       12
#define HBM_TEMPERATURE_EXCEEDING_TEMPERATURE_LIMIT_BIT          13
#define HBM_TEMPERATURE_EXCEEDING_MAX_MEMORY_TEMPERATURE_BIT     14

#define POWER_MANAGER_CONTROLLER_MASK                            (1 << POWER_MANAGER_CONTROLLER_BIT                        ) 
#define MAXIMUM_DPM_STATE_GFX_ENGINE_RESTRICTED_MASK             (1 << MAXIMUM_DPM_STATE_GFX_ENGINE_RESTRICTED_BIT         )
#define GPU_DIE_TEMPERATURE_THROTTLING_MASK                      (1 << GPU_DIE_TEMPERATURE_THROTTLING_BIT                  ) 
#define HBM_DIE_TEMPERATURE_THROTTLING_MASK                      (1 << HBM_DIE_TEMPERATURE_THROTTLING_BIT                  )
#define TGP_THROTTLING_MASK                                      (1 << TGP_THROTTLING_BIT                                  )
#define PCC_THROTTLING_MASK                                      (1 << PCC_THROTTLING_BIT                                  )
#define HBM_TEMPERATURE_EXCEEDING_TEMPERATURE_LIMIT_MASK         (1 << HBM_TEMPERATURE_EXCEEDING_TEMPERATURE_LIMIT_BIT     )
#define HBM_TEMPERATURE_EXCEEDING_MAX_MEMORY_TEMPERATURE_MASK    (1 << HBM_TEMPERATURE_EXCEEDING_MAX_MEMORY_TEMPERATURE_BIT) 

//This structure to be DMA to SMBUS Config register space
typedef struct {
  uint8_t  MinorInfoVersion;
  uint8_t  MajorInfoVersion;
  uint8_t  TableSize;
  uint8_t  Reserved;

  uint8_t  Reserved1;
  uint8_t  RevID;
  uint16_t DeviceID;

  uint16_t DieTemperatureLimit;
  uint16_t FanTargetTemperature;

  uint16_t MemoryTemperatureLimit;
  uint16_t MemoryTemperatureLimit1;

  uint16_t TGP;
  uint16_t CardPower;

  uint32_t DieTemperatureRegisterOffset;

  uint32_t Reserved2;
  
  uint32_t Reserved3;

  uint32_t Status;

  uint16_t DieTemperature;
  uint16_t CurrentMemoryTemperature;

  uint16_t MemoryTemperature;
  uint8_t MemoryHotspotPosition;
  uint8_t Reserved4;

  uint32_t BoardLevelEnergyAccumulator;  
} OutOfBandMonitor_t;

typedef struct {
  uint32_t Version;

  // SECTION: Feature Enablement
  uint32_t FeaturesToRun[2];

  // SECTION: Infrastructure Limits
  uint16_t SocketPowerLimitAc[PPT_THROTTLER_COUNT];
  uint16_t SocketPowerLimitAcTau[PPT_THROTTLER_COUNT];
  uint16_t SocketPowerLimitDc[PPT_THROTTLER_COUNT];
  uint16_t SocketPowerLimitDcTau[PPT_THROTTLER_COUNT];  

  uint16_t TdcLimitSoc;             // Amps
  uint16_t TdcLimitSocTau;          // Time constant of LPF in ms
  uint16_t TdcLimitGfx;             // Amps
  uint16_t TdcLimitGfxTau;          // Time constant of LPF in ms
  
  uint16_t TedgeLimit;              // Celcius
  uint16_t ThotspotLimit;           // Celcius
  uint16_t TmemLimit;               // Celcius
  uint16_t Tvr_gfxLimit;            // Celcius
  uint16_t Tvr_mem0Limit;           // Celcius
  uint16_t Tvr_mem1Limit;           // Celcius  
  uint16_t Tvr_socLimit;            // Celcius
  uint16_t Tliquid0Limit;           // Celcius
  uint16_t Tliquid1Limit;           // Celcius
  uint16_t TplxLimit;               // Celcius
  uint32_t FitLimit;                // Failures in time (failures per million parts over the defined lifetime)

  uint16_t PpmPowerLimit;           // Switch this this power limit when temperature is above PpmTempThreshold
  uint16_t PpmTemperatureThreshold;
  
  // SECTION: Throttler settings
  uint32_t ThrottlerControlMask;   // See Throtter masks defines

  // SECTION: FW DSTATE Settings  
  uint32_t FwDStateMask;           // See FW DState masks defines

  // SECTION: ULV Settings
  uint16_t  UlvVoltageOffsetSoc; // In mV(Q2)
  uint16_t  UlvVoltageOffsetGfx; // In mV(Q2)

  uint8_t   GceaLinkMgrIdleThreshold;        //Set by SMU FW during enablment of SOC_ULV. Controls delay for GFX SDP port disconnection during idle events
  uint8_t   paddingRlcUlvParams[3];
  
  uint8_t  UlvSmnclkDid;     //DID for ULV mode. 0 means CLK will not be modified in ULV.
  uint8_t  UlvMp1clkDid;     //DID for ULV mode. 0 means CLK will not be modified in ULV.
  uint8_t  UlvGfxclkBypass;  // 1 to turn off/bypass Gfxclk during ULV, 0 to leave Gfxclk on during ULV
  uint8_t  Padding234;

  uint16_t     MinVoltageUlvGfx; // In mV(Q2)  Minimum Voltage ("Vmin") of VDD_GFX in ULV mode 
  uint16_t     MinVoltageUlvSoc; // In mV(Q2)  Minimum Voltage ("Vmin") of VDD_SOC in ULV mode


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
  uint16_t       FreqTableDcefclk  [NUM_DCEFCLK_DPM_LEVELS ];     // In MHz
  uint16_t       FreqTableDispclk  [NUM_DISPCLK_DPM_LEVELS ];     // In MHz
  uint16_t       FreqTablePixclk   [NUM_PIXCLK_DPM_LEVELS  ];     // In MHz
  uint16_t       FreqTablePhyclk   [NUM_PHYCLK_DPM_LEVELS  ];     // In MHz
  uint32_t       Paddingclks[16];

  uint16_t       DcModeMaxFreq     [PPCLK_COUNT            ];     // In MHz
  uint16_t       Padding8_Clks;
  
  uint8_t        FreqTableUclkDiv  [NUM_UCLK_DPM_LEVELS    ];     // 0:Div-1, 1:Div-1/2, 2:Div-1/4, 3:Div-1/8

  // SECTION: DPM Config 2
  uint16_t       Mp0clkFreq        [NUM_MP0CLK_DPM_LEVELS];       // in MHz
  uint16_t       Mp0DpmVoltage     [NUM_MP0CLK_DPM_LEVELS];       // mV(Q2)
  uint16_t       MemVddciVoltage   [NUM_UCLK_DPM_LEVELS];         // mV(Q2)
  uint16_t       MemMvddVoltage    [NUM_UCLK_DPM_LEVELS];         // mV(Q2)
  // GFXCLK DPM
  uint16_t        GfxclkFgfxoffEntry;   // in Mhz
  uint16_t        GfxclkFinit;          // in Mhz 
  uint16_t        GfxclkFidle;          // in MHz
  uint16_t        GfxclkSlewRate;       // for PLL babystepping???
  uint16_t        GfxclkFopt;           // in Mhz
  uint8_t         Padding567[2]; 
  uint16_t        GfxclkDsMaxFreq;      // in MHz
  uint8_t         GfxclkSource;         // 0 = PLL, 1 = DFLL
  uint8_t         Padding456;

  // UCLK section
  uint8_t      LowestUclkReservedForUlv; // Set this to 1 if UCLK DPM0 is reserved for ULV-mode only
  uint8_t      paddingUclk[3];
  
  uint8_t      MemoryType;          // 0-GDDR6, 1-HBM
  uint8_t      MemoryChannels;
  uint8_t      PaddingMem[2];

  // Link DPM Settings
  uint8_t      PcieGenSpeed[NUM_LINK_LEVELS];           ///< 0:PciE-gen1 1:PciE-gen2 2:PciE-gen3 3:PciE-gen4
  uint8_t      PcieLaneCount[NUM_LINK_LEVELS];          ///< 1=x1, 2=x2, 3=x4, 4=x8, 5=x12, 6=x16
  uint16_t     LclkFreq[NUM_LINK_LEVELS];              

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
  uint16_t     FanGainLiquid0;
  uint16_t     FanGainLiquid1;  
  uint16_t     FanGainVrGfx;
  uint16_t     FanGainVrSoc;
  uint16_t     FanGainVrMem0;
  uint16_t     FanGainVrMem1;  
  uint16_t     FanGainPlx;
  uint16_t     FanGainMem;
  uint16_t     FanPwmMin;
  uint16_t     FanAcousticLimitRpm;
  uint16_t     FanThrottlingRpm;
  uint16_t     FanMaximumRpm;
  uint16_t     FanTargetTemperature;
  uint16_t     FanTargetGfxclk;
  uint8_t      FanTempInputSelect;
  uint8_t      FanPadding;
  uint8_t      FanZeroRpmEnable; 
  uint8_t      FanTachEdgePerRev;
  //uint8_t      padding8_Fan[2];
    
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
  DroopInt_t        dBtcGbGfxPll;         // GHz->V BtcGb
  DroopInt_t        dBtcGbGfxDfll;        // GHz->V BtcGb
  DroopInt_t        dBtcGbSoc;            // GHz->V BtcGb
  LinearInt_t       qAgingGb[AVFS_VOLTAGE_COUNT];          // GHz->V 

  QuadraticInt_t    qStaticVoltageOffset[AVFS_VOLTAGE_COUNT]; // GHz->V 

  uint16_t          DcTol[AVFS_VOLTAGE_COUNT];            // mV Q2

  uint8_t           DcBtcEnabled[AVFS_VOLTAGE_COUNT];
  uint8_t           Padding8_GfxBtc[2];

  uint16_t          DcBtcMin[AVFS_VOLTAGE_COUNT];       // mV Q2
  uint16_t          DcBtcMax[AVFS_VOLTAGE_COUNT];       // mV Q2

  // SECTION: Advanced Options
  uint32_t          DebugOverrides;
  QuadraticInt_t    ReservedEquation0; 
  QuadraticInt_t    ReservedEquation1; 
  QuadraticInt_t    ReservedEquation2; 
  QuadraticInt_t    ReservedEquation3; 
  
  // Total Power configuration, use defines from PwrConfig_e
  uint8_t      TotalPowerConfig;    //0-TDP, 1-TGP, 2-TCP Estimated, 3-TCP Measured
  uint8_t      TotalPowerSpare1;  
  uint16_t     TotalPowerSpare2;

  // APCC Settings
  uint16_t     PccThresholdLow;
  uint16_t     PccThresholdHigh;
  uint32_t     MGpuFanBoostLimitRpm;
  uint32_t     PaddingAPCC[5];

  // Temperature Dependent Vmin
  uint16_t     VDDGFX_TVmin;       //Celcius
  uint16_t     VDDSOC_TVmin;       //Celcius
  uint16_t     VDDGFX_Vmin_HiTemp; // mV Q2
  uint16_t     VDDGFX_Vmin_LoTemp; // mV Q2
  uint16_t     VDDSOC_Vmin_HiTemp; // mV Q2
  uint16_t     VDDSOC_Vmin_LoTemp; // mV Q2
  
  uint16_t     VDDGFX_TVminHystersis; // Celcius
  uint16_t     VDDSOC_TVminHystersis; // Celcius

  // BTC Setting
  uint32_t     BtcConfig;
  
  uint16_t     SsFmin[10]; // PPtable value to function similar to VFTFmin for SS Curve; Size is PPCLK_COUNT rounded to nearest multiple of 2
  uint16_t     DcBtcGb[AVFS_VOLTAGE_COUNT];

  // SECTION: Board Reserved
  uint32_t     Reserved[8];

  // SECTION: BOARD PARAMETERS
  // I2C Control
  I2cControllerConfig_t  I2cControllers[NUM_I2C_CONTROLLERS];     

  // SVI2 Board Parameters
  uint16_t     MaxVoltageStepGfx; // In mV(Q2) Max voltage step that SMU will request. Multiple steps are taken if voltage change exceeds this value.
  uint16_t     MaxVoltageStepSoc; // In mV(Q2) Max voltage step that SMU will request. Multiple steps are taken if voltage change exceeds this value.
  
  uint8_t      VddGfxVrMapping;   // Use VR_MAPPING* bitfields
  uint8_t      VddSocVrMapping;   // Use VR_MAPPING* bitfields
  uint8_t      VddMem0VrMapping;  // Use VR_MAPPING* bitfields
  uint8_t      VddMem1VrMapping;  // Use VR_MAPPING* bitfields

  uint8_t      GfxUlvPhaseSheddingMask; // set this to 1 to set PSI0/1 to 1 in ULV mode
  uint8_t      SocUlvPhaseSheddingMask; // set this to 1 to set PSI0/1 to 1 in ULV mode
  uint8_t      ExternalSensorPresent; // External RDI connected to TMON (aka TEMP IN)
  uint8_t      Padding8_V; 

  // Telemetry Settings
  uint16_t     GfxMaxCurrent;   // in Amps
  int8_t       GfxOffset;       // in Amps
  uint8_t      Padding_TelemetryGfx;

  uint16_t     SocMaxCurrent;   // in Amps
  int8_t       SocOffset;       // in Amps
  uint8_t      Padding_TelemetrySoc;

  uint16_t     Mem0MaxCurrent;   // in Amps
  int8_t       Mem0Offset;       // in Amps
  uint8_t      Padding_TelemetryMem0;
  
  uint16_t     Mem1MaxCurrent;   // in Amps
  int8_t       Mem1Offset;       // in Amps
  uint8_t      Padding_TelemetryMem1;
  
  // GPIO Settings
  uint8_t      AcDcGpio;        // GPIO pin configured for AC/DC switching
  uint8_t      AcDcPolarity;    // GPIO polarity for AC/DC switching
  uint8_t      VR0HotGpio;      // GPIO pin configured for VR0 HOT event
  uint8_t      VR0HotPolarity;  // GPIO polarity for VR0 HOT event

  uint8_t      VR1HotGpio;      // GPIO pin configured for VR1 HOT event 
  uint8_t      VR1HotPolarity;  // GPIO polarity for VR1 HOT event 
  uint8_t      GthrGpio;        // GPIO pin configured for GTHR Event
  uint8_t      GthrPolarity;    // replace GPIO polarity for GTHR

  // LED Display Settings
  uint8_t      LedPin0;         // GPIO number for LedPin[0]
  uint8_t      LedPin1;         // GPIO number for LedPin[1]
  uint8_t      LedPin2;         // GPIO number for LedPin[2]
  uint8_t      padding8_4;
 
  // GFXCLK PLL Spread Spectrum
  uint8_t      PllGfxclkSpreadEnabled;   // on or off
  uint8_t      PllGfxclkSpreadPercent;   // Q4.4
  uint16_t     PllGfxclkSpreadFreq;      // kHz

  // GFXCLK DFLL Spread Spectrum
  uint8_t      DfllGfxclkSpreadEnabled;   // on or off
  uint8_t      DfllGfxclkSpreadPercent;   // Q4.4
  uint16_t     DfllGfxclkSpreadFreq;      // kHz
  
  // UCLK Spread Spectrum
  uint8_t      UclkSpreadEnabled;   // on or off
  uint8_t      UclkSpreadPercent;   // Q4.4
  uint16_t     UclkSpreadFreq;      // kHz

  // SOCCLK Spread Spectrum
  uint8_t      SoclkSpreadEnabled;   // on or off
  uint8_t      SocclkSpreadPercent;   // Q4.4
  uint16_t     SocclkSpreadFreq;      // kHz

  // Total board power
  uint16_t     TotalBoardPower;     //Only needed for TCP Estimated case, where TCP = TGP+Total Board Power
  uint16_t     BoardPadding; 

  // Mvdd Svi2 Div Ratio Setting
  uint32_t     MvddRatio; // This is used for MVDD Vid workaround. It has 16 fractional bits (Q16.16)

  uint8_t      RenesesLoadLineEnabled;
  uint8_t      GfxLoadlineResistance;
  uint8_t      SocLoadlineResistance;
  uint8_t      Padding8_Loadline;

  uint32_t     BoardReserved[8];

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
  
  uint16_t      GfxclkFmin;           // MHz
  uint16_t      GfxclkFmax;           // MHz
  uint16_t      GfxclkFreq1;          // MHz
  uint16_t      GfxclkVolt1;          // mV (Q2)
  uint16_t      GfxclkFreq2;          // MHz
  uint16_t      GfxclkVolt2;          // mV (Q2)
  uint16_t      GfxclkFreq3;          // MHz
  uint16_t      GfxclkVolt3;          // mV (Q2)
  uint16_t      UclkFmax;             // MHz
  int16_t       OverDrivePct;         // %
  uint16_t      FanMaximumRpm;
  uint16_t      FanMinimumPwm;
  uint16_t      FanTargetTemperature; // Degree Celcius 
  uint16_t      MaxOpTemp;            // Degree Celcius
  uint16_t      FanZeroRpmEnable;
  uint16_t      Padding;

  uint32_t     MmHubPadding[8]; // SMU internal use  

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
  uint16_t AverageSocketPower    ;
  uint16_t TemperatureEdge       ;
  uint16_t TemperatureHotspot    ;
  uint16_t TemperatureMem        ;
  uint16_t TemperatureVrGfx      ;
  uint16_t TemperatureVrMem0     ;
  uint16_t TemperatureVrMem1     ;  
  uint16_t TemperatureVrSoc      ;  
  uint16_t TemperatureLiquid0    ;
  uint16_t TemperatureLiquid1    ;  
  uint16_t TemperaturePlx        ;
  uint16_t Padding16             ;
  uint32_t ThrottlerStatus       ; 
 
  uint8_t  LinkDpmLevel;
  uint8_t  Padding8_2;
  uint16_t CurrFanSpeed;

  // Padding - ignore
  uint32_t     MmHubPadding[8]; // SMU internal use
} SmuMetrics_t;

typedef struct {
  uint16_t MinClock; // This is either DCEFCLK or SOCCLK (in MHz)
  uint16_t MaxClock; // This is either DCEFCLK or SOCCLK (in MHz)
  uint16_t MinUclk;
  uint16_t MaxUclk;
  
  uint8_t  WmSetting;
  uint8_t  Padding[3];

  uint32_t     MmHubPadding[8]; // SMU internal use  
} WatermarkRowGeneric_t;

#define NUM_WM_RANGES 4

typedef enum {
  WM_SOCCLK = 0,
  WM_DCEFCLK,
  WM_COUNT,
} WM_CLOCK_e;

typedef struct {
  // Watermarks
  WatermarkRowGeneric_t WatermarkRow[WM_COUNT][NUM_WM_RANGES];

  uint32_t     MmHubPadding[8]; // SMU internal use
} Watermarks_t;

typedef struct {
  uint16_t avgPsmCount[28];
  uint16_t minPsmCount[28];
  float    avgPsmVoltage[28];
  float    minPsmVoltage[28];

  uint32_t     MmHubPadding[32]; // SMU internal use
} AvfsDebugTable_t_NV14;

typedef struct {
  uint16_t avgPsmCount[36];
  uint16_t minPsmCount[36];
  float    avgPsmVoltage[36]; 
  float    minPsmVoltage[36];

  uint32_t     MmHubPadding[8]; // SMU internal use
} AvfsDebugTable_t_NV10;

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

  uint32_t EnabledAvfsModules[2]; //NV10 - 36 AVFS modules

  uint32_t     MmHubPadding[8]; // SMU internal use
} AvfsFuseOverride_t;

typedef struct {

  uint8_t   Gfx_ActiveHystLimit;
  uint8_t   Gfx_IdleHystLimit;
  uint8_t   Gfx_FPS;
  uint8_t   Gfx_MinActiveFreqType;
  uint8_t   Gfx_BoosterFreqType; 
  uint8_t   Gfx_MinFreqStep;                // Minimum delta between current and target frequeny in order for FW to change clock.
  uint16_t  Gfx_MinActiveFreq;              // MHz
  uint16_t  Gfx_BoosterFreq;                // MHz
  uint16_t  Gfx_PD_Data_time_constant;      // Time constant of PD controller in ms
  uint32_t  Gfx_PD_Data_limit_a;            // Q16
  uint32_t  Gfx_PD_Data_limit_b;            // Q16
  uint32_t  Gfx_PD_Data_limit_c;            // Q16
  uint32_t  Gfx_PD_Data_error_coeff;        // Q16
  uint32_t  Gfx_PD_Data_error_rate_coeff;   // Q16
  
  uint8_t   Soc_ActiveHystLimit;
  uint8_t   Soc_IdleHystLimit;
  uint8_t   Soc_FPS;
  uint8_t   Soc_MinActiveFreqType;
  uint8_t   Soc_BoosterFreqType; 
  uint8_t   Soc_MinFreqStep;                // Minimum delta between current and target frequeny in order for FW to change clock.
  uint16_t  Soc_MinActiveFreq;              // MHz
  uint16_t  Soc_BoosterFreq;                // MHz
  uint16_t  Soc_PD_Data_time_constant;      // Time constant of PD controller in ms
  uint32_t  Soc_PD_Data_limit_a;            // Q16
  uint32_t  Soc_PD_Data_limit_b;            // Q16
  uint32_t  Soc_PD_Data_limit_c;            // Q16
  uint32_t  Soc_PD_Data_error_coeff;        // Q16
  uint32_t  Soc_PD_Data_error_rate_coeff;   // Q16
  
  uint8_t   Mem_ActiveHystLimit;
  uint8_t   Mem_IdleHystLimit;
  uint8_t   Mem_FPS;
  uint8_t   Mem_MinActiveFreqType;
  uint8_t   Mem_BoosterFreqType;
  uint8_t   Mem_MinFreqStep;                // Minimum delta between current and target frequeny in order for FW to change clock.
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

  uint32_t     MmHubPadding[8]; // SMU internal use  

} DpmActivityMonitorCoeffInt_t;


// Workload bits
#define WORKLOAD_PPLIB_DEFAULT_BIT        0 
#define WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT 1 
#define WORKLOAD_PPLIB_POWER_SAVING_BIT   2 
#define WORKLOAD_PPLIB_VIDEO_BIT          3 
#define WORKLOAD_PPLIB_VR_BIT             4 
#define WORKLOAD_PPLIB_COMPUTE_BIT        5 
#define WORKLOAD_PPLIB_CUSTOM_BIT         6 
#define WORKLOAD_PPLIB_COUNT              7 


// These defines are used with the following messages:
// SMC_MSG_TransferTableDram2Smu
// SMC_MSG_TransferTableSmu2Dram

// Table transfer status
#define TABLE_TRANSFER_OK         0x0
#define TABLE_TRANSFER_FAILED     0xFF

// Table types
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
#define TABLE_I2C_COMMANDS           10
#define TABLE_PACE                   11
#define TABLE_COUNT                  12

//RLC Pace Table total number of levels
#define RLC_PACE_TABLE_NUM_LEVELS 16

typedef struct {
  float FlopsPerByteTable[RLC_PACE_TABLE_NUM_LEVELS];
  
  uint32_t     MmHubPadding[8]; // SMU internal use  
} RlcPaceFlopsPerByteOverride_t;

// These defines are used with the SMC_MSG_SetUclkFastSwitch message.
#define UCLK_SWITCH_SLOW 0
#define UCLK_SWITCH_FAST 1
#endif
