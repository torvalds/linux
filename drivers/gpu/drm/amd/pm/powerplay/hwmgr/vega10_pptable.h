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
#ifndef _VEGA10_PPTABLE_H_
#define _VEGA10_PPTABLE_H_

#pragma pack(push, 1)

#define ATOM_VEGA10_PP_FANPARAMETERS_TACHOMETER_PULSES_PER_REVOLUTION_MASK 0x0f
#define ATOM_VEGA10_PP_FANPARAMETERS_NOFAN                                 0x80

#define ATOM_VEGA10_PP_THERMALCONTROLLER_NONE      0
#define ATOM_VEGA10_PP_THERMALCONTROLLER_LM96163   17
#define ATOM_VEGA10_PP_THERMALCONTROLLER_VEGA10    24

#define ATOM_VEGA10_PP_THERMALCONTROLLER_ADT7473_WITH_INTERNAL   0x89
#define ATOM_VEGA10_PP_THERMALCONTROLLER_EMC2103_WITH_INTERNAL   0x8D

#define ATOM_VEGA10_PP_PLATFORM_CAP_POWERPLAY                   0x1
#define ATOM_VEGA10_PP_PLATFORM_CAP_SBIOSPOWERSOURCE            0x2
#define ATOM_VEGA10_PP_PLATFORM_CAP_HARDWAREDC                  0x4
#define ATOM_VEGA10_PP_PLATFORM_CAP_BACO                        0x8
#define ATOM_VEGA10_PP_PLATFORM_COMBINE_PCC_WITH_THERMAL_SIGNAL 0x10


/* ATOM_PPLIB_NONCLOCK_INFO::usClassification */
#define ATOM_PPLIB_CLASSIFICATION_UI_MASK               0x0007
#define ATOM_PPLIB_CLASSIFICATION_UI_SHIFT              0
#define ATOM_PPLIB_CLASSIFICATION_UI_NONE               0
#define ATOM_PPLIB_CLASSIFICATION_UI_BATTERY            1
#define ATOM_PPLIB_CLASSIFICATION_UI_BALANCED           3
#define ATOM_PPLIB_CLASSIFICATION_UI_PERFORMANCE        5
/* 2, 4, 6, 7 are reserved */

#define ATOM_PPLIB_CLASSIFICATION_BOOT                  0x0008
#define ATOM_PPLIB_CLASSIFICATION_THERMAL               0x0010
#define ATOM_PPLIB_CLASSIFICATION_LIMITEDPOWERSOURCE    0x0020
#define ATOM_PPLIB_CLASSIFICATION_REST                  0x0040
#define ATOM_PPLIB_CLASSIFICATION_FORCED                0x0080
#define ATOM_PPLIB_CLASSIFICATION_ACPI                  0x1000

/* ATOM_PPLIB_NONCLOCK_INFO::usClassification2 */
#define ATOM_PPLIB_CLASSIFICATION2_LIMITEDPOWERSOURCE_2 0x0001

#define ATOM_Vega10_DISALLOW_ON_DC                   0x00004000
#define ATOM_Vega10_ENABLE_VARIBRIGHT                0x00008000

#define ATOM_Vega10_TABLE_REVISION_VEGA10         8

#define ATOM_Vega10_VoltageMode_AVFS_Interpolate     0
#define ATOM_Vega10_VoltageMode_AVFS_WorstCase       1
#define ATOM_Vega10_VoltageMode_Static               2

typedef struct _ATOM_Vega10_POWERPLAYTABLE {
	struct atom_common_table_header sHeader;
	UCHAR  ucTableRevision;
	USHORT usTableSize;                        /* the size of header structure */
	ULONG  ulGoldenPPID;                       /* PPGen use only */
	ULONG  ulGoldenRevision;                   /* PPGen use only */
	USHORT usFormatID;                         /* PPGen use only */
	ULONG  ulPlatformCaps;                     /* See ATOM_Vega10_CAPS_* */
	ULONG  ulMaxODEngineClock;                 /* For Overdrive. */
	ULONG  ulMaxODMemoryClock;                 /* For Overdrive. */
	USHORT usPowerControlLimit;
	USHORT usUlvVoltageOffset;                 /* in mv units */
	USHORT usUlvSmnclkDid;
	USHORT usUlvMp1clkDid;
	USHORT usUlvGfxclkBypass;
	USHORT usGfxclkSlewRate;
	UCHAR  ucGfxVoltageMode;
	UCHAR  ucSocVoltageMode;
	UCHAR  ucUclkVoltageMode;
	UCHAR  ucUvdVoltageMode;
	UCHAR  ucVceVoltageMode;
	UCHAR  ucMp0VoltageMode;
	UCHAR  ucDcefVoltageMode;
	USHORT usStateArrayOffset;                 /* points to ATOM_Vega10_State_Array */
	USHORT usFanTableOffset;                   /* points to ATOM_Vega10_Fan_Table */
	USHORT usThermalControllerOffset;          /* points to ATOM_Vega10_Thermal_Controller */
	USHORT usSocclkDependencyTableOffset;      /* points to ATOM_Vega10_SOCCLK_Dependency_Table */
	USHORT usMclkDependencyTableOffset;        /* points to ATOM_Vega10_MCLK_Dependency_Table */
	USHORT usGfxclkDependencyTableOffset;      /* points to ATOM_Vega10_GFXCLK_Dependency_Table */
	USHORT usDcefclkDependencyTableOffset;     /* points to ATOM_Vega10_DCEFCLK_Dependency_Table */
	USHORT usVddcLookupTableOffset;            /* points to ATOM_Vega10_Voltage_Lookup_Table */
	USHORT usVddmemLookupTableOffset;          /* points to ATOM_Vega10_Voltage_Lookup_Table */
	USHORT usMMDependencyTableOffset;          /* points to ATOM_Vega10_MM_Dependency_Table */
	USHORT usVCEStateTableOffset;              /* points to ATOM_Vega10_VCE_State_Table */
	USHORT usReserve;                          /* No PPM Support for Vega10 */
	USHORT usPowerTuneTableOffset;             /* points to ATOM_Vega10_PowerTune_Table */
	USHORT usHardLimitTableOffset;             /* points to ATOM_Vega10_Hard_Limit_Table */
	USHORT usVddciLookupTableOffset;           /* points to ATOM_Vega10_Voltage_Lookup_Table */
	USHORT usPCIETableOffset;                  /* points to ATOM_Vega10_PCIE_Table */
	USHORT usPixclkDependencyTableOffset;      /* points to ATOM_Vega10_PIXCLK_Dependency_Table */
	USHORT usDispClkDependencyTableOffset;     /* points to ATOM_Vega10_DISPCLK_Dependency_Table */
	USHORT usPhyClkDependencyTableOffset;      /* points to ATOM_Vega10_PHYCLK_Dependency_Table */
} ATOM_Vega10_POWERPLAYTABLE;

typedef struct _ATOM_Vega10_State {
	UCHAR  ucSocClockIndexHigh;
	UCHAR  ucSocClockIndexLow;
	UCHAR  ucGfxClockIndexHigh;
	UCHAR  ucGfxClockIndexLow;
	UCHAR  ucMemClockIndexHigh;
	UCHAR  ucMemClockIndexLow;
	USHORT usClassification;
	ULONG  ulCapsAndSettings;
	USHORT usClassification2;
} ATOM_Vega10_State;

typedef struct _ATOM_Vega10_State_Array {
	UCHAR ucRevId;
	UCHAR ucNumEntries;                                         /* Number of entries. */
	ATOM_Vega10_State states[1];                             /* Dynamically allocate entries. */
} ATOM_Vega10_State_Array;

typedef struct _ATOM_Vega10_CLK_Dependency_Record {
	ULONG  ulClk;                                               /* Frequency of Clock */
	UCHAR  ucVddInd;                                            /* Base voltage */
} ATOM_Vega10_CLK_Dependency_Record;

typedef struct _ATOM_Vega10_GFXCLK_Dependency_Record {
	ULONG  ulClk;                                               /* Clock Frequency */
	UCHAR  ucVddInd;                                            /* SOC_VDD index */
	USHORT usCKSVOffsetandDisable;                              /* Bits 0~30: Voltage offset for CKS, Bit 31: Disable/enable for the GFXCLK level. */
	USHORT usAVFSOffset;                                        /* AVFS Voltage offset */
} ATOM_Vega10_GFXCLK_Dependency_Record;

typedef struct _ATOM_Vega10_GFXCLK_Dependency_Record_V2 {
	ULONG  ulClk;
	UCHAR  ucVddInd;
	USHORT usCKSVOffsetandDisable;
	USHORT usAVFSOffset;
	UCHAR  ucACGEnable;
	UCHAR  ucReserved[3];
} ATOM_Vega10_GFXCLK_Dependency_Record_V2;

typedef struct _ATOM_Vega10_MCLK_Dependency_Record {
	ULONG  ulMemClk;                                            /* Clock Frequency */
	UCHAR  ucVddInd;                                            /* SOC_VDD index */
	UCHAR  ucVddMemInd;                                         /* MEM_VDD - only non zero for MCLK record */
	UCHAR  ucVddciInd;                                          /* VDDCI   = only non zero for MCLK record */
} ATOM_Vega10_MCLK_Dependency_Record;

typedef struct _ATOM_Vega10_GFXCLK_Dependency_Table {
    UCHAR ucRevId;
    UCHAR ucNumEntries;                                         /* Number of entries. */
    ATOM_Vega10_GFXCLK_Dependency_Record entries[1];            /* Dynamically allocate entries. */
} ATOM_Vega10_GFXCLK_Dependency_Table;

typedef struct _ATOM_Vega10_MCLK_Dependency_Table {
    UCHAR ucRevId;
    UCHAR ucNumEntries;                                         /* Number of entries. */
    ATOM_Vega10_MCLK_Dependency_Record entries[1];            /* Dynamically allocate entries. */
} ATOM_Vega10_MCLK_Dependency_Table;

typedef struct _ATOM_Vega10_SOCCLK_Dependency_Table {
    UCHAR ucRevId;
    UCHAR ucNumEntries;                                         /* Number of entries. */
    ATOM_Vega10_CLK_Dependency_Record entries[1];            /* Dynamically allocate entries. */
} ATOM_Vega10_SOCCLK_Dependency_Table;

typedef struct _ATOM_Vega10_DCEFCLK_Dependency_Table {
    UCHAR ucRevId;
    UCHAR ucNumEntries;                                         /* Number of entries. */
    ATOM_Vega10_CLK_Dependency_Record entries[1];            /* Dynamically allocate entries. */
} ATOM_Vega10_DCEFCLK_Dependency_Table;

typedef struct _ATOM_Vega10_PIXCLK_Dependency_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries;                                         /* Number of entries. */
	ATOM_Vega10_CLK_Dependency_Record entries[1];            /* Dynamically allocate entries. */
} ATOM_Vega10_PIXCLK_Dependency_Table;

typedef struct _ATOM_Vega10_DISPCLK_Dependency_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries;                                         /* Number of entries.*/
	ATOM_Vega10_CLK_Dependency_Record entries[1];            /* Dynamically allocate entries. */
} ATOM_Vega10_DISPCLK_Dependency_Table;

typedef struct _ATOM_Vega10_PHYCLK_Dependency_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries;                                         /* Number of entries. */
	ATOM_Vega10_CLK_Dependency_Record entries[1];            /* Dynamically allocate entries. */
} ATOM_Vega10_PHYCLK_Dependency_Table;

typedef struct _ATOM_Vega10_MM_Dependency_Record {
    UCHAR  ucVddcInd;                                           /* SOC_VDD voltage */
    ULONG  ulDClk;                                              /* UVD D-clock */
    ULONG  ulVClk;                                              /* UVD V-clock */
    ULONG  ulEClk;                                              /* VCE clock */
    ULONG  ulPSPClk;                                            /* PSP clock */
} ATOM_Vega10_MM_Dependency_Record;

typedef struct _ATOM_Vega10_MM_Dependency_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries;                                         /* Number of entries */
	ATOM_Vega10_MM_Dependency_Record entries[1];             /* Dynamically allocate entries */
} ATOM_Vega10_MM_Dependency_Table;

typedef struct _ATOM_Vega10_PCIE_Record {
	ULONG ulLCLK;                                               /* LClock */
	UCHAR ucPCIEGenSpeed;                                       /* PCIE Speed */
	UCHAR ucPCIELaneWidth;                                      /* PCIE Lane Width */
} ATOM_Vega10_PCIE_Record;

typedef struct _ATOM_Vega10_PCIE_Table {
	UCHAR  ucRevId;
	UCHAR  ucNumEntries;                                        /* Number of entries */
	ATOM_Vega10_PCIE_Record entries[1];                      /* Dynamically allocate entries. */
} ATOM_Vega10_PCIE_Table;

typedef struct _ATOM_Vega10_Voltage_Lookup_Record {
	USHORT usVdd;                                               /* Base voltage */
} ATOM_Vega10_Voltage_Lookup_Record;

typedef struct _ATOM_Vega10_Voltage_Lookup_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries;                                          /* Number of entries */
	ATOM_Vega10_Voltage_Lookup_Record entries[1];             /* Dynamically allocate entries */
} ATOM_Vega10_Voltage_Lookup_Table;

typedef struct _ATOM_Vega10_Fan_Table {
	UCHAR   ucRevId;                         /* Change this if the table format changes or version changes so that the other fields are not the same. */
	USHORT  usFanOutputSensitivity;          /* Sensitivity of fan reaction to temepature changes. */
	USHORT  usFanRPMMax;                     /* The default value in RPM. */
	USHORT  usThrottlingRPM;
	USHORT  usFanAcousticLimit;              /* Minimum Fan Controller Frequency Acoustic Limit. */
	USHORT  usTargetTemperature;             /* The default ideal temperature in Celcius. */
	USHORT  usMinimumPWMLimit;               /* The minimum PWM that the advanced fan controller can set. */
	USHORT  usTargetGfxClk;                   /* The ideal Fan Controller GFXCLK Frequency Acoustic Limit. */
	USHORT  usFanGainEdge;
	USHORT  usFanGainHotspot;
	USHORT  usFanGainLiquid;
	USHORT  usFanGainVrVddc;
	USHORT  usFanGainVrMvdd;
	USHORT  usFanGainPlx;
	USHORT  usFanGainHbm;
	UCHAR   ucEnableZeroRPM;
	USHORT  usFanStopTemperature;
	USHORT  usFanStartTemperature;
} ATOM_Vega10_Fan_Table;

typedef struct _ATOM_Vega10_Fan_Table_V2 {
	UCHAR   ucRevId;
	USHORT  usFanOutputSensitivity;
	USHORT  usFanAcousticLimitRpm;
	USHORT  usThrottlingRPM;
	USHORT  usTargetTemperature;
	USHORT  usMinimumPWMLimit;
	USHORT  usTargetGfxClk;
	USHORT  usFanGainEdge;
	USHORT  usFanGainHotspot;
	USHORT  usFanGainLiquid;
	USHORT  usFanGainVrVddc;
	USHORT  usFanGainVrMvdd;
	USHORT  usFanGainPlx;
	USHORT  usFanGainHbm;
	UCHAR   ucEnableZeroRPM;
	USHORT  usFanStopTemperature;
	USHORT  usFanStartTemperature;
	UCHAR   ucFanParameters;
	UCHAR   ucFanMinRPM;
	UCHAR   ucFanMaxRPM;
} ATOM_Vega10_Fan_Table_V2;

typedef struct _ATOM_Vega10_Fan_Table_V3 {
	UCHAR   ucRevId;
	USHORT  usFanOutputSensitivity;
	USHORT  usFanAcousticLimitRpm;
	USHORT  usThrottlingRPM;
	USHORT  usTargetTemperature;
	USHORT  usMinimumPWMLimit;
	USHORT  usTargetGfxClk;
	USHORT  usFanGainEdge;
	USHORT  usFanGainHotspot;
	USHORT  usFanGainLiquid;
	USHORT  usFanGainVrVddc;
	USHORT  usFanGainVrMvdd;
	USHORT  usFanGainPlx;
	USHORT  usFanGainHbm;
	UCHAR   ucEnableZeroRPM;
	USHORT  usFanStopTemperature;
	USHORT  usFanStartTemperature;
	UCHAR   ucFanParameters;
	UCHAR   ucFanMinRPM;
	UCHAR   ucFanMaxRPM;
	USHORT  usMGpuThrottlingRPM;
} ATOM_Vega10_Fan_Table_V3;

typedef struct _ATOM_Vega10_Thermal_Controller {
	UCHAR ucRevId;
	UCHAR ucType;           /* one of ATOM_VEGA10_PP_THERMALCONTROLLER_*/
	UCHAR ucI2cLine;        /* as interpreted by DAL I2C */
	UCHAR ucI2cAddress;
	UCHAR ucFanParameters;  /* Fan Control Parameters. */
	UCHAR ucFanMinRPM;      /* Fan Minimum RPM (hundreds) -- for display purposes only.*/
	UCHAR ucFanMaxRPM;      /* Fan Maximum RPM (hundreds) -- for display purposes only.*/
    UCHAR ucFlags;          /* to be defined */
} ATOM_Vega10_Thermal_Controller;

typedef struct _ATOM_Vega10_VCE_State_Record
{
    UCHAR  ucVCEClockIndex;         /*index into usVCEDependencyTableOffset of 'ATOM_Vega10_MM_Dependency_Table' type */
    UCHAR  ucFlag;                  /* 2 bits indicates memory p-states */
    UCHAR  ucSCLKIndex;             /* index into ATOM_Vega10_SCLK_Dependency_Table */
    UCHAR  ucMCLKIndex;             /* index into ATOM_Vega10_MCLK_Dependency_Table */
} ATOM_Vega10_VCE_State_Record;

typedef struct _ATOM_Vega10_VCE_State_Table
{
    UCHAR ucRevId;
    UCHAR ucNumEntries;
    ATOM_Vega10_VCE_State_Record entries[1];
} ATOM_Vega10_VCE_State_Table;

typedef struct _ATOM_Vega10_PowerTune_Table {
	UCHAR  ucRevId;
	USHORT usSocketPowerLimit;
	USHORT usBatteryPowerLimit;
	USHORT usSmallPowerLimit;
	USHORT usTdcLimit;
	USHORT usEdcLimit;
	USHORT usSoftwareShutdownTemp;
	USHORT usTemperatureLimitHotSpot;
	USHORT usTemperatureLimitLiquid1;
	USHORT usTemperatureLimitLiquid2;
	USHORT usTemperatureLimitHBM;
	USHORT usTemperatureLimitVrSoc;
	USHORT usTemperatureLimitVrMem;
	USHORT usTemperatureLimitPlx;
	USHORT usLoadLineResistance;
	UCHAR  ucLiquid1_I2C_address;
	UCHAR  ucLiquid2_I2C_address;
	UCHAR  ucVr_I2C_address;
	UCHAR  ucPlx_I2C_address;
	UCHAR  ucLiquid_I2C_LineSCL;
	UCHAR  ucLiquid_I2C_LineSDA;
	UCHAR  ucVr_I2C_LineSCL;
	UCHAR  ucVr_I2C_LineSDA;
	UCHAR  ucPlx_I2C_LineSCL;
	UCHAR  ucPlx_I2C_LineSDA;
	USHORT usTemperatureLimitTedge;
} ATOM_Vega10_PowerTune_Table;

typedef struct _ATOM_Vega10_PowerTune_Table_V2
{
	UCHAR  ucRevId;
	USHORT usSocketPowerLimit;
	USHORT usBatteryPowerLimit;
	USHORT usSmallPowerLimit;
	USHORT usTdcLimit;
	USHORT usEdcLimit;
	USHORT usSoftwareShutdownTemp;
	USHORT usTemperatureLimitHotSpot;
	USHORT usTemperatureLimitLiquid1;
	USHORT usTemperatureLimitLiquid2;
	USHORT usTemperatureLimitHBM;
	USHORT usTemperatureLimitVrSoc;
	USHORT usTemperatureLimitVrMem;
	USHORT usTemperatureLimitPlx;
	USHORT usLoadLineResistance;
	UCHAR ucLiquid1_I2C_address;
	UCHAR ucLiquid2_I2C_address;
	UCHAR ucLiquid_I2C_Line;
	UCHAR ucVr_I2C_address;
	UCHAR ucVr_I2C_Line;
	UCHAR ucPlx_I2C_address;
	UCHAR ucPlx_I2C_Line;
	USHORT usTemperatureLimitTedge;
} ATOM_Vega10_PowerTune_Table_V2;

typedef struct _ATOM_Vega10_PowerTune_Table_V3
{
	UCHAR  ucRevId;
	USHORT usSocketPowerLimit;
	USHORT usBatteryPowerLimit;
	USHORT usSmallPowerLimit;
	USHORT usTdcLimit;
	USHORT usEdcLimit;
	USHORT usSoftwareShutdownTemp;
	USHORT usTemperatureLimitHotSpot;
	USHORT usTemperatureLimitLiquid1;
	USHORT usTemperatureLimitLiquid2;
	USHORT usTemperatureLimitHBM;
	USHORT usTemperatureLimitVrSoc;
	USHORT usTemperatureLimitVrMem;
	USHORT usTemperatureLimitPlx;
	USHORT usLoadLineResistance;
	UCHAR  ucLiquid1_I2C_address;
	UCHAR  ucLiquid2_I2C_address;
	UCHAR  ucLiquid_I2C_Line;
	UCHAR  ucVr_I2C_address;
	UCHAR  ucVr_I2C_Line;
	UCHAR  ucPlx_I2C_address;
	UCHAR  ucPlx_I2C_Line;
	USHORT usTemperatureLimitTedge;
	USHORT usBoostStartTemperature;
	USHORT usBoostStopTemperature;
	ULONG  ulBoostClock;
	ULONG  Reserved[2];
} ATOM_Vega10_PowerTune_Table_V3;

typedef struct _ATOM_Vega10_Hard_Limit_Record {
    ULONG  ulSOCCLKLimit;
    ULONG  ulGFXCLKLimit;
    ULONG  ulMCLKLimit;
    USHORT usVddcLimit;
    USHORT usVddciLimit;
    USHORT usVddMemLimit;
} ATOM_Vega10_Hard_Limit_Record;

typedef struct _ATOM_Vega10_Hard_Limit_Table
{
    UCHAR ucRevId;
    UCHAR ucNumEntries;
    ATOM_Vega10_Hard_Limit_Record entries[1];
} ATOM_Vega10_Hard_Limit_Table;

typedef struct _Vega10_PPTable_Generic_SubTable_Header
{
    UCHAR  ucRevId;
} Vega10_PPTable_Generic_SubTable_Header;

#pragma pack(pop)

#endif
