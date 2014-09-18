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
 */

#ifndef _PPTABLE_H
#define _PPTABLE_H

#pragma pack(1)

typedef struct _ATOM_PPLIB_THERMALCONTROLLER

{
    UCHAR ucType;           // one of ATOM_PP_THERMALCONTROLLER_*
    UCHAR ucI2cLine;        // as interpreted by DAL I2C
    UCHAR ucI2cAddress;
    UCHAR ucFanParameters;  // Fan Control Parameters.
    UCHAR ucFanMinRPM;      // Fan Minimum RPM (hundreds) -- for display purposes only.
    UCHAR ucFanMaxRPM;      // Fan Maximum RPM (hundreds) -- for display purposes only.
    UCHAR ucReserved;       // ----
    UCHAR ucFlags;          // to be defined
} ATOM_PPLIB_THERMALCONTROLLER;

#define ATOM_PP_FANPARAMETERS_TACHOMETER_PULSES_PER_REVOLUTION_MASK 0x0f
#define ATOM_PP_FANPARAMETERS_NOFAN                                 0x80    // No fan is connected to this controller.

#define ATOM_PP_THERMALCONTROLLER_NONE      0
#define ATOM_PP_THERMALCONTROLLER_LM63      1  // Not used by PPLib
#define ATOM_PP_THERMALCONTROLLER_ADM1032   2  // Not used by PPLib
#define ATOM_PP_THERMALCONTROLLER_ADM1030   3  // Not used by PPLib
#define ATOM_PP_THERMALCONTROLLER_MUA6649   4  // Not used by PPLib
#define ATOM_PP_THERMALCONTROLLER_LM64      5
#define ATOM_PP_THERMALCONTROLLER_F75375    6  // Not used by PPLib
#define ATOM_PP_THERMALCONTROLLER_RV6xx     7
#define ATOM_PP_THERMALCONTROLLER_RV770     8
#define ATOM_PP_THERMALCONTROLLER_ADT7473   9
#define ATOM_PP_THERMALCONTROLLER_KONG      10
#define ATOM_PP_THERMALCONTROLLER_EXTERNAL_GPIO     11
#define ATOM_PP_THERMALCONTROLLER_EVERGREEN 12
#define ATOM_PP_THERMALCONTROLLER_EMC2103   13  /* 0x0D */ // Only fan control will be implemented, do NOT show this in PPGen.
#define ATOM_PP_THERMALCONTROLLER_SUMO      14  /* 0x0E */ // Sumo type, used internally
#define ATOM_PP_THERMALCONTROLLER_NISLANDS  15
#define ATOM_PP_THERMALCONTROLLER_SISLANDS  16
#define ATOM_PP_THERMALCONTROLLER_LM96163   17
#define ATOM_PP_THERMALCONTROLLER_CISLANDS  18
#define ATOM_PP_THERMALCONTROLLER_KAVERI    19


// Thermal controller 'combo type' to use an external controller for Fan control and an internal controller for thermal.
// We probably should reserve the bit 0x80 for this use.
// To keep the number of these types low we should also use the same code for all ASICs (i.e. do not distinguish RV6xx and RV7xx Internal here).
// The driver can pick the correct internal controller based on the ASIC.

#define ATOM_PP_THERMALCONTROLLER_ADT7473_WITH_INTERNAL   0x89    // ADT7473 Fan Control + Internal Thermal Controller
#define ATOM_PP_THERMALCONTROLLER_EMC2103_WITH_INTERNAL   0x8D    // EMC2103 Fan Control + Internal Thermal Controller

typedef struct _ATOM_PPLIB_STATE
{
    UCHAR ucNonClockStateIndex;
    UCHAR ucClockStateIndices[1]; // variable-sized
} ATOM_PPLIB_STATE;


typedef struct _ATOM_PPLIB_FANTABLE
{
    UCHAR   ucFanTableFormat;                // Change this if the table format changes or version changes so that the other fields are not the same.
    UCHAR   ucTHyst;                         // Temperature hysteresis. Integer.
    USHORT  usTMin;                          // The temperature, in 0.01 centigrades, below which we just run at a minimal PWM.
    USHORT  usTMed;                          // The middle temperature where we change slopes.
    USHORT  usTHigh;                         // The high point above TMed for adjusting the second slope.
    USHORT  usPWMMin;                        // The minimum PWM value in percent (0.01% increments).
    USHORT  usPWMMed;                        // The PWM value (in percent) at TMed.
    USHORT  usPWMHigh;                       // The PWM value at THigh.
} ATOM_PPLIB_FANTABLE;

typedef struct _ATOM_PPLIB_FANTABLE2
{
    ATOM_PPLIB_FANTABLE basicTable;
    USHORT  usTMax;                          // The max temperature
} ATOM_PPLIB_FANTABLE2;

typedef struct _ATOM_PPLIB_EXTENDEDHEADER
{
    USHORT  usSize;
    ULONG   ulMaxEngineClock;   // For Overdrive.
    ULONG   ulMaxMemoryClock;   // For Overdrive.
    // Add extra system parameters here, always adjust size to include all fields.
    USHORT  usVCETableOffset; //points to ATOM_PPLIB_VCE_Table
    USHORT  usUVDTableOffset;   //points to ATOM_PPLIB_UVD_Table
    USHORT  usSAMUTableOffset;  //points to ATOM_PPLIB_SAMU_Table
    USHORT  usPPMTableOffset;   //points to ATOM_PPLIB_PPM_Table
    USHORT  usACPTableOffset;  //points to ATOM_PPLIB_ACP_Table   
    USHORT  usPowerTuneTableOffset; //points to ATOM_PPLIB_POWERTUNE_Table   
} ATOM_PPLIB_EXTENDEDHEADER;

//// ATOM_PPLIB_POWERPLAYTABLE::ulPlatformCaps
#define ATOM_PP_PLATFORM_CAP_BACKBIAS 1
#define ATOM_PP_PLATFORM_CAP_POWERPLAY 2
#define ATOM_PP_PLATFORM_CAP_SBIOSPOWERSOURCE 4
#define ATOM_PP_PLATFORM_CAP_ASPM_L0s 8
#define ATOM_PP_PLATFORM_CAP_ASPM_L1 16
#define ATOM_PP_PLATFORM_CAP_HARDWAREDC 32
#define ATOM_PP_PLATFORM_CAP_GEMINIPRIMARY 64
#define ATOM_PP_PLATFORM_CAP_STEPVDDC 128
#define ATOM_PP_PLATFORM_CAP_VOLTAGECONTROL 256
#define ATOM_PP_PLATFORM_CAP_SIDEPORTCONTROL 512
#define ATOM_PP_PLATFORM_CAP_TURNOFFPLL_ASPML1 1024
#define ATOM_PP_PLATFORM_CAP_HTLINKCONTROL 2048
#define ATOM_PP_PLATFORM_CAP_MVDDCONTROL 4096
#define ATOM_PP_PLATFORM_CAP_GOTO_BOOT_ON_ALERT 0x2000              // Go to boot state on alerts, e.g. on an AC->DC transition.
#define ATOM_PP_PLATFORM_CAP_DONT_WAIT_FOR_VBLANK_ON_ALERT 0x4000   // Do NOT wait for VBLANK during an alert (e.g. AC->DC transition).
#define ATOM_PP_PLATFORM_CAP_VDDCI_CONTROL 0x8000                   // Does the driver control VDDCI independently from VDDC.
#define ATOM_PP_PLATFORM_CAP_REGULATOR_HOT 0x00010000               // Enable the 'regulator hot' feature.
#define ATOM_PP_PLATFORM_CAP_BACO          0x00020000               // Does the driver supports BACO state.
#define ATOM_PP_PLATFORM_CAP_NEW_CAC_VOLTAGE   0x00040000           // Does the driver supports new CAC voltage table.
#define ATOM_PP_PLATFORM_CAP_REVERT_GPIO5_POLARITY   0x00080000     // Does the driver supports revert GPIO5 polarity.
#define ATOM_PP_PLATFORM_CAP_OUTPUT_THERMAL2GPIO17   0x00100000     // Does the driver supports thermal2GPIO17.
#define ATOM_PP_PLATFORM_CAP_VRHOT_GPIO_CONFIGURABLE   0x00200000   // Does the driver supports VR HOT GPIO Configurable.
#define ATOM_PP_PLATFORM_CAP_TEMP_INVERSION   0x00400000            // Does the driver supports Temp Inversion feature.
#define ATOM_PP_PLATFORM_CAP_EVV    0x00800000

typedef struct _ATOM_PPLIB_POWERPLAYTABLE
{
      ATOM_COMMON_TABLE_HEADER sHeader;

      UCHAR ucDataRevision;

      UCHAR ucNumStates;
      UCHAR ucStateEntrySize;
      UCHAR ucClockInfoSize;
      UCHAR ucNonClockSize;

      // offset from start of this table to array of ucNumStates ATOM_PPLIB_STATE structures
      USHORT usStateArrayOffset;

      // offset from start of this table to array of ASIC-specific structures,
      // currently ATOM_PPLIB_CLOCK_INFO.
      USHORT usClockInfoArrayOffset;

      // offset from start of this table to array of ATOM_PPLIB_NONCLOCK_INFO
      USHORT usNonClockInfoArrayOffset;

      USHORT usBackbiasTime;    // in microseconds
      USHORT usVoltageTime;     // in microseconds
      USHORT usTableSize;       //the size of this structure, or the extended structure

      ULONG ulPlatformCaps;            // See ATOM_PPLIB_CAPS_*

      ATOM_PPLIB_THERMALCONTROLLER    sThermalController;

      USHORT usBootClockInfoOffset;
      USHORT usBootNonClockInfoOffset;

} ATOM_PPLIB_POWERPLAYTABLE;

typedef struct _ATOM_PPLIB_POWERPLAYTABLE2
{
    ATOM_PPLIB_POWERPLAYTABLE basicTable;
    UCHAR   ucNumCustomThermalPolicy;
    USHORT  usCustomThermalPolicyArrayOffset;
}ATOM_PPLIB_POWERPLAYTABLE2, *LPATOM_PPLIB_POWERPLAYTABLE2;

typedef struct _ATOM_PPLIB_POWERPLAYTABLE3
{
    ATOM_PPLIB_POWERPLAYTABLE2 basicTable2;
    USHORT                     usFormatID;                      // To be used ONLY by PPGen.
    USHORT                     usFanTableOffset;
    USHORT                     usExtendendedHeaderOffset;
} ATOM_PPLIB_POWERPLAYTABLE3, *LPATOM_PPLIB_POWERPLAYTABLE3;

typedef struct _ATOM_PPLIB_POWERPLAYTABLE4
{
    ATOM_PPLIB_POWERPLAYTABLE3 basicTable3;
    ULONG                      ulGoldenPPID;                    // PPGen use only     
    ULONG                      ulGoldenRevision;                // PPGen use only
    USHORT                     usVddcDependencyOnSCLKOffset;
    USHORT                     usVddciDependencyOnMCLKOffset;
    USHORT                     usVddcDependencyOnMCLKOffset;
    USHORT                     usMaxClockVoltageOnDCOffset;
    USHORT                     usVddcPhaseShedLimitsTableOffset;    // Points to ATOM_PPLIB_PhaseSheddingLimits_Table
    USHORT                     usMvddDependencyOnMCLKOffset;  
} ATOM_PPLIB_POWERPLAYTABLE4, *LPATOM_PPLIB_POWERPLAYTABLE4;

typedef struct _ATOM_PPLIB_POWERPLAYTABLE5
{
    ATOM_PPLIB_POWERPLAYTABLE4 basicTable4;
    ULONG                      ulTDPLimit;
    ULONG                      ulNearTDPLimit;
    ULONG                      ulSQRampingThreshold;
    USHORT                     usCACLeakageTableOffset;         // Points to ATOM_PPLIB_CAC_Leakage_Table
    ULONG                      ulCACLeakage;                    // The iLeakage for driver calculated CAC leakage table
    USHORT                     usTDPODLimit;
    USHORT                     usLoadLineSlope;                 // in milliOhms * 100
} ATOM_PPLIB_POWERPLAYTABLE5, *LPATOM_PPLIB_POWERPLAYTABLE5;

//// ATOM_PPLIB_NONCLOCK_INFO::usClassification
#define ATOM_PPLIB_CLASSIFICATION_UI_MASK          0x0007
#define ATOM_PPLIB_CLASSIFICATION_UI_SHIFT         0
#define ATOM_PPLIB_CLASSIFICATION_UI_NONE          0
#define ATOM_PPLIB_CLASSIFICATION_UI_BATTERY       1
#define ATOM_PPLIB_CLASSIFICATION_UI_BALANCED      3
#define ATOM_PPLIB_CLASSIFICATION_UI_PERFORMANCE   5
// 2, 4, 6, 7 are reserved

#define ATOM_PPLIB_CLASSIFICATION_BOOT                   0x0008
#define ATOM_PPLIB_CLASSIFICATION_THERMAL                0x0010
#define ATOM_PPLIB_CLASSIFICATION_LIMITEDPOWERSOURCE     0x0020
#define ATOM_PPLIB_CLASSIFICATION_REST                   0x0040
#define ATOM_PPLIB_CLASSIFICATION_FORCED                 0x0080
#define ATOM_PPLIB_CLASSIFICATION_3DPERFORMANCE          0x0100
#define ATOM_PPLIB_CLASSIFICATION_OVERDRIVETEMPLATE      0x0200
#define ATOM_PPLIB_CLASSIFICATION_UVDSTATE               0x0400
#define ATOM_PPLIB_CLASSIFICATION_3DLOW                  0x0800
#define ATOM_PPLIB_CLASSIFICATION_ACPI                   0x1000
#define ATOM_PPLIB_CLASSIFICATION_HD2STATE               0x2000
#define ATOM_PPLIB_CLASSIFICATION_HDSTATE                0x4000
#define ATOM_PPLIB_CLASSIFICATION_SDSTATE                0x8000

//// ATOM_PPLIB_NONCLOCK_INFO::usClassification2
#define ATOM_PPLIB_CLASSIFICATION2_LIMITEDPOWERSOURCE_2     0x0001
#define ATOM_PPLIB_CLASSIFICATION2_ULV                      0x0002
#define ATOM_PPLIB_CLASSIFICATION2_MVC                      0x0004   //Multi-View Codec (BD-3D)

//// ATOM_PPLIB_NONCLOCK_INFO::ulCapsAndSettings
#define ATOM_PPLIB_SINGLE_DISPLAY_ONLY           0x00000001
#define ATOM_PPLIB_SUPPORTS_VIDEO_PLAYBACK         0x00000002

// 0 is 2.5Gb/s, 1 is 5Gb/s
#define ATOM_PPLIB_PCIE_LINK_SPEED_MASK            0x00000004
#define ATOM_PPLIB_PCIE_LINK_SPEED_SHIFT           2

// lanes - 1: 1, 2, 4, 8, 12, 16 permitted by PCIE spec
#define ATOM_PPLIB_PCIE_LINK_WIDTH_MASK            0x000000F8
#define ATOM_PPLIB_PCIE_LINK_WIDTH_SHIFT           3

// lookup into reduced refresh-rate table
#define ATOM_PPLIB_LIMITED_REFRESHRATE_VALUE_MASK  0x00000F00
#define ATOM_PPLIB_LIMITED_REFRESHRATE_VALUE_SHIFT 8

#define ATOM_PPLIB_LIMITED_REFRESHRATE_UNLIMITED    0
#define ATOM_PPLIB_LIMITED_REFRESHRATE_50HZ         1
// 2-15 TBD as needed.

#define ATOM_PPLIB_SOFTWARE_DISABLE_LOADBALANCING        0x00001000
#define ATOM_PPLIB_SOFTWARE_ENABLE_SLEEP_FOR_TIMESTAMPS  0x00002000

#define ATOM_PPLIB_DISALLOW_ON_DC                       0x00004000

#define ATOM_PPLIB_ENABLE_VARIBRIGHT                     0x00008000

//memory related flags
#define ATOM_PPLIB_SWSTATE_MEMORY_DLL_OFF               0x000010000

//M3 Arb    //2bits, current 3 sets of parameters in total
#define ATOM_PPLIB_M3ARB_MASK                       0x00060000
#define ATOM_PPLIB_M3ARB_SHIFT                      17

#define ATOM_PPLIB_ENABLE_DRR                       0x00080000

// remaining 16 bits are reserved
typedef struct _ATOM_PPLIB_THERMAL_STATE
{
    UCHAR   ucMinTemperature;
    UCHAR   ucMaxTemperature;
    UCHAR   ucThermalAction;
}ATOM_PPLIB_THERMAL_STATE, *LPATOM_PPLIB_THERMAL_STATE;

// Contained in an array starting at the offset
// in ATOM_PPLIB_POWERPLAYTABLE::usNonClockInfoArrayOffset.
// referenced from ATOM_PPLIB_STATE_INFO::ucNonClockStateIndex
#define ATOM_PPLIB_NONCLOCKINFO_VER1      12
#define ATOM_PPLIB_NONCLOCKINFO_VER2      24
typedef struct _ATOM_PPLIB_NONCLOCK_INFO
{
      USHORT usClassification;
      UCHAR  ucMinTemperature;
      UCHAR  ucMaxTemperature;
      ULONG  ulCapsAndSettings;
      UCHAR  ucRequiredPower;
      USHORT usClassification2;
      ULONG  ulVCLK;
      ULONG  ulDCLK;
      UCHAR  ucUnused[5];
} ATOM_PPLIB_NONCLOCK_INFO;

// Contained in an array starting at the offset
// in ATOM_PPLIB_POWERPLAYTABLE::usClockInfoArrayOffset.
// referenced from ATOM_PPLIB_STATE::ucClockStateIndices
typedef struct _ATOM_PPLIB_R600_CLOCK_INFO
{
      USHORT usEngineClockLow;
      UCHAR ucEngineClockHigh;

      USHORT usMemoryClockLow;
      UCHAR ucMemoryClockHigh;

      USHORT usVDDC;
      USHORT usUnused1;
      USHORT usUnused2;

      ULONG ulFlags; // ATOM_PPLIB_R600_FLAGS_*

} ATOM_PPLIB_R600_CLOCK_INFO;

// ulFlags in ATOM_PPLIB_R600_CLOCK_INFO
#define ATOM_PPLIB_R600_FLAGS_PCIEGEN2          1
#define ATOM_PPLIB_R600_FLAGS_UVDSAFE           2
#define ATOM_PPLIB_R600_FLAGS_BACKBIASENABLE    4
#define ATOM_PPLIB_R600_FLAGS_MEMORY_ODT_OFF    8
#define ATOM_PPLIB_R600_FLAGS_MEMORY_DLL_OFF   16
#define ATOM_PPLIB_R600_FLAGS_LOWPOWER         32   // On the RV770 use 'low power' setting (sequencer S0).

typedef struct _ATOM_PPLIB_RS780_CLOCK_INFO

{
      USHORT usLowEngineClockLow;         // Low Engine clock in MHz (the same way as on the R600).
      UCHAR  ucLowEngineClockHigh;
      USHORT usHighEngineClockLow;        // High Engine clock in MHz.
      UCHAR  ucHighEngineClockHigh;
      USHORT usMemoryClockLow;            // For now one of the ATOM_PPLIB_RS780_SPMCLK_XXXX constants.
      UCHAR  ucMemoryClockHigh;           // Currentyl unused.
      UCHAR  ucPadding;                   // For proper alignment and size.
      USHORT usVDDC;                      // For the 780, use: None, Low, High, Variable
      UCHAR  ucMaxHTLinkWidth;            // From SBIOS - {2, 4, 8, 16}
      UCHAR  ucMinHTLinkWidth;            // From SBIOS - {2, 4, 8, 16}. Effective only if CDLW enabled. Minimum down stream width could 
      USHORT usHTLinkFreq;                // See definition ATOM_PPLIB_RS780_HTLINKFREQ_xxx or in MHz(>=200).
      ULONG  ulFlags; 
} ATOM_PPLIB_RS780_CLOCK_INFO;

#define ATOM_PPLIB_RS780_VOLTAGE_NONE       0 
#define ATOM_PPLIB_RS780_VOLTAGE_LOW        1 
#define ATOM_PPLIB_RS780_VOLTAGE_HIGH       2 
#define ATOM_PPLIB_RS780_VOLTAGE_VARIABLE   3 

#define ATOM_PPLIB_RS780_SPMCLK_NONE        0   // We cannot change the side port memory clock, leave it as it is.
#define ATOM_PPLIB_RS780_SPMCLK_LOW         1
#define ATOM_PPLIB_RS780_SPMCLK_HIGH        2

#define ATOM_PPLIB_RS780_HTLINKFREQ_NONE       0 
#define ATOM_PPLIB_RS780_HTLINKFREQ_LOW        1 
#define ATOM_PPLIB_RS780_HTLINKFREQ_HIGH       2 

typedef struct _ATOM_PPLIB_EVERGREEN_CLOCK_INFO
{
      USHORT usEngineClockLow;
      UCHAR  ucEngineClockHigh;

      USHORT usMemoryClockLow;
      UCHAR  ucMemoryClockHigh;

      USHORT usVDDC;
      USHORT usVDDCI;
      USHORT usUnused;

      ULONG ulFlags; // ATOM_PPLIB_R600_FLAGS_*

} ATOM_PPLIB_EVERGREEN_CLOCK_INFO;

typedef struct _ATOM_PPLIB_SI_CLOCK_INFO
{
      USHORT usEngineClockLow;
      UCHAR  ucEngineClockHigh;

      USHORT usMemoryClockLow;
      UCHAR  ucMemoryClockHigh;

      USHORT usVDDC;
      USHORT usVDDCI;
      UCHAR  ucPCIEGen;
      UCHAR  ucUnused1;

      ULONG ulFlags; // ATOM_PPLIB_SI_FLAGS_*, no flag is necessary for now

} ATOM_PPLIB_SI_CLOCK_INFO;

typedef struct _ATOM_PPLIB_CI_CLOCK_INFO
{
      USHORT usEngineClockLow;
      UCHAR  ucEngineClockHigh;

      USHORT usMemoryClockLow;
      UCHAR  ucMemoryClockHigh;
      
      UCHAR  ucPCIEGen;
      USHORT usPCIELane;
} ATOM_PPLIB_CI_CLOCK_INFO;

typedef struct _ATOM_PPLIB_SUMO_CLOCK_INFO{
      USHORT usEngineClockLow;  //clockfrequency & 0xFFFF. The unit is in 10khz
      UCHAR  ucEngineClockHigh; //clockfrequency >> 16. 
      UCHAR  vddcIndex;         //2-bit vddc index;
      USHORT tdpLimit;
      //please initalize to 0
      USHORT rsv1;
      //please initialize to 0s
      ULONG rsv2[2];
}ATOM_PPLIB_SUMO_CLOCK_INFO;

typedef struct _ATOM_PPLIB_STATE_V2
{
      //number of valid dpm levels in this state; Driver uses it to calculate the whole 
      //size of the state: sizeof(ATOM_PPLIB_STATE_V2) + (ucNumDPMLevels - 1) * sizeof(UCHAR)
      UCHAR ucNumDPMLevels;
      
      //a index to the array of nonClockInfos
      UCHAR nonClockInfoIndex;
      /**
      * Driver will read the first ucNumDPMLevels in this array
      */
      UCHAR clockInfoIndex[1];
} ATOM_PPLIB_STATE_V2;

typedef struct _StateArray{
    //how many states we have 
    UCHAR ucNumEntries;
    
    ATOM_PPLIB_STATE_V2 states[1];
}StateArray;


typedef struct _ClockInfoArray{
    //how many clock levels we have
    UCHAR ucNumEntries;
    
    //sizeof(ATOM_PPLIB_CLOCK_INFO)
    UCHAR ucEntrySize;
    
    UCHAR clockInfo[1];
}ClockInfoArray;

typedef struct _NonClockInfoArray{

    //how many non-clock levels we have. normally should be same as number of states
    UCHAR ucNumEntries;
    //sizeof(ATOM_PPLIB_NONCLOCK_INFO)
    UCHAR ucEntrySize;
    
    ATOM_PPLIB_NONCLOCK_INFO nonClockInfo[1];
}NonClockInfoArray;

typedef struct _ATOM_PPLIB_Clock_Voltage_Dependency_Record
{
    USHORT usClockLow;
    UCHAR  ucClockHigh;
    USHORT usVoltage;
}ATOM_PPLIB_Clock_Voltage_Dependency_Record;

typedef struct _ATOM_PPLIB_Clock_Voltage_Dependency_Table
{
    UCHAR ucNumEntries;                                                // Number of entries.
    ATOM_PPLIB_Clock_Voltage_Dependency_Record entries[1];             // Dynamically allocate entries.
}ATOM_PPLIB_Clock_Voltage_Dependency_Table;

typedef struct _ATOM_PPLIB_Clock_Voltage_Limit_Record
{
    USHORT usSclkLow;
    UCHAR  ucSclkHigh;
    USHORT usMclkLow;
    UCHAR  ucMclkHigh;
    USHORT usVddc;
    USHORT usVddci;
}ATOM_PPLIB_Clock_Voltage_Limit_Record;

typedef struct _ATOM_PPLIB_Clock_Voltage_Limit_Table
{
    UCHAR ucNumEntries;                                                // Number of entries.
    ATOM_PPLIB_Clock_Voltage_Limit_Record entries[1];                  // Dynamically allocate entries.
}ATOM_PPLIB_Clock_Voltage_Limit_Table;

union _ATOM_PPLIB_CAC_Leakage_Record
{
    struct
    {
        USHORT usVddc;          // We use this field for the "fake" standardized VDDC for power calculations; For CI and newer, we use this as the real VDDC value. in CI we read it as StdVoltageHiSidd
        ULONG  ulLeakageValue;  // For CI and newer we use this as the "fake" standar VDDC value. in CI we read it as StdVoltageLoSidd

    };
    struct
     {
        USHORT usVddc1;
        USHORT usVddc2;
        USHORT usVddc3;
     };
};

typedef union _ATOM_PPLIB_CAC_Leakage_Record ATOM_PPLIB_CAC_Leakage_Record;

typedef struct _ATOM_PPLIB_CAC_Leakage_Table
{
    UCHAR ucNumEntries;                                                 // Number of entries.
    ATOM_PPLIB_CAC_Leakage_Record entries[1];                           // Dynamically allocate entries.
}ATOM_PPLIB_CAC_Leakage_Table;

typedef struct _ATOM_PPLIB_PhaseSheddingLimits_Record
{
    USHORT usVoltage;
    USHORT usSclkLow;
    UCHAR  ucSclkHigh;
    USHORT usMclkLow;
    UCHAR  ucMclkHigh;
}ATOM_PPLIB_PhaseSheddingLimits_Record;

typedef struct _ATOM_PPLIB_PhaseSheddingLimits_Table
{
    UCHAR ucNumEntries;                                                 // Number of entries.
    ATOM_PPLIB_PhaseSheddingLimits_Record entries[1];                   // Dynamically allocate entries.
}ATOM_PPLIB_PhaseSheddingLimits_Table;

typedef struct _VCEClockInfo{
    USHORT usEVClkLow;
    UCHAR  ucEVClkHigh;
    USHORT usECClkLow;
    UCHAR  ucECClkHigh;
}VCEClockInfo;

typedef struct _VCEClockInfoArray{
    UCHAR ucNumEntries;
    VCEClockInfo entries[1];
}VCEClockInfoArray;

typedef struct _ATOM_PPLIB_VCE_Clock_Voltage_Limit_Record
{
    USHORT usVoltage;
    UCHAR  ucVCEClockInfoIndex;
}ATOM_PPLIB_VCE_Clock_Voltage_Limit_Record;

typedef struct _ATOM_PPLIB_VCE_Clock_Voltage_Limit_Table
{
    UCHAR numEntries;
    ATOM_PPLIB_VCE_Clock_Voltage_Limit_Record entries[1];
}ATOM_PPLIB_VCE_Clock_Voltage_Limit_Table;

typedef struct _ATOM_PPLIB_VCE_State_Record
{
    UCHAR  ucVCEClockInfoIndex;
    UCHAR  ucClockInfoIndex; //highest 2 bits indicates memory p-states, lower 6bits indicates index to ClockInfoArrary
}ATOM_PPLIB_VCE_State_Record;

typedef struct _ATOM_PPLIB_VCE_State_Table
{
    UCHAR numEntries;
    ATOM_PPLIB_VCE_State_Record entries[1];
}ATOM_PPLIB_VCE_State_Table;


typedef struct _ATOM_PPLIB_VCE_Table
{
      UCHAR revid;
//    VCEClockInfoArray array;
//    ATOM_PPLIB_VCE_Clock_Voltage_Limit_Table limits;
//    ATOM_PPLIB_VCE_State_Table states;
}ATOM_PPLIB_VCE_Table;


typedef struct _UVDClockInfo{
    USHORT usVClkLow;
    UCHAR  ucVClkHigh;
    USHORT usDClkLow;
    UCHAR  ucDClkHigh;
}UVDClockInfo;

typedef struct _UVDClockInfoArray{
    UCHAR ucNumEntries;
    UVDClockInfo entries[1];
}UVDClockInfoArray;

typedef struct _ATOM_PPLIB_UVD_Clock_Voltage_Limit_Record
{
    USHORT usVoltage;
    UCHAR  ucUVDClockInfoIndex;
}ATOM_PPLIB_UVD_Clock_Voltage_Limit_Record;

typedef struct _ATOM_PPLIB_UVD_Clock_Voltage_Limit_Table
{
    UCHAR numEntries;
    ATOM_PPLIB_UVD_Clock_Voltage_Limit_Record entries[1];
}ATOM_PPLIB_UVD_Clock_Voltage_Limit_Table;

typedef struct _ATOM_PPLIB_UVD_Table
{
      UCHAR revid;
//    UVDClockInfoArray array;
//    ATOM_PPLIB_UVD_Clock_Voltage_Limit_Table limits;
}ATOM_PPLIB_UVD_Table;

typedef struct _ATOM_PPLIB_SAMClk_Voltage_Limit_Record
{
      USHORT usVoltage;
      USHORT usSAMClockLow;
      UCHAR  ucSAMClockHigh;
}ATOM_PPLIB_SAMClk_Voltage_Limit_Record;

typedef struct _ATOM_PPLIB_SAMClk_Voltage_Limit_Table{
    UCHAR numEntries;
    ATOM_PPLIB_SAMClk_Voltage_Limit_Record entries[1];
}ATOM_PPLIB_SAMClk_Voltage_Limit_Table;

typedef struct _ATOM_PPLIB_SAMU_Table
{
      UCHAR revid;
      ATOM_PPLIB_SAMClk_Voltage_Limit_Table limits;
}ATOM_PPLIB_SAMU_Table;

typedef struct _ATOM_PPLIB_ACPClk_Voltage_Limit_Record
{
      USHORT usVoltage;
      USHORT usACPClockLow;
      UCHAR  ucACPClockHigh;
}ATOM_PPLIB_ACPClk_Voltage_Limit_Record;

typedef struct _ATOM_PPLIB_ACPClk_Voltage_Limit_Table{
    UCHAR numEntries;
    ATOM_PPLIB_ACPClk_Voltage_Limit_Record entries[1];
}ATOM_PPLIB_ACPClk_Voltage_Limit_Table;

typedef struct _ATOM_PPLIB_ACP_Table
{
      UCHAR revid;
      ATOM_PPLIB_ACPClk_Voltage_Limit_Table limits;
}ATOM_PPLIB_ACP_Table;

typedef struct _ATOM_PowerTune_Table{
    USHORT usTDP;
    USHORT usConfigurableTDP;
    USHORT usTDC;
    USHORT usBatteryPowerLimit;
    USHORT usSmallPowerLimit;
    USHORT usLowCACLeakage;
    USHORT usHighCACLeakage;
}ATOM_PowerTune_Table;

typedef struct _ATOM_PPLIB_POWERTUNE_Table
{
      UCHAR revid;
      ATOM_PowerTune_Table power_tune_table;
}ATOM_PPLIB_POWERTUNE_Table;

typedef struct _ATOM_PPLIB_POWERTUNE_Table_V1
{
      UCHAR revid;
      ATOM_PowerTune_Table power_tune_table;
      USHORT usMaximumPowerDeliveryLimit;
      USHORT usReserve[7];
} ATOM_PPLIB_POWERTUNE_Table_V1;

#define ATOM_PPM_A_A    1
#define ATOM_PPM_A_I    2
typedef struct _ATOM_PPLIB_PPM_Table
{
      UCHAR  ucRevId;
      UCHAR  ucPpmDesign;          //A+I or A+A
      USHORT usCpuCoreNumber;
      ULONG  ulPlatformTDP;
      ULONG  ulSmallACPlatformTDP;
      ULONG  ulPlatformTDC;
      ULONG  ulSmallACPlatformTDC;
      ULONG  ulApuTDP;
      ULONG  ulDGpuTDP;  
      ULONG  ulDGpuUlvPower;
      ULONG  ulTjmax;
} ATOM_PPLIB_PPM_Table;

#pragma pack()

#endif
