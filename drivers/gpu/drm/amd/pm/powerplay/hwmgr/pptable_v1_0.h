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

#ifndef TONGA_PPTABLE_H
#define TONGA_PPTABLE_H

/** \file
 * This is a PowerPlay table header file
 */
#pragma pack(push, 1)

#include "hwmgr.h"

#define ATOM_TONGA_PP_FANPARAMETERS_TACHOMETER_PULSES_PER_REVOLUTION_MASK 0x0f
#define ATOM_TONGA_PP_FANPARAMETERS_NOFAN                                 0x80    /* No fan is connected to this controller. */

#define ATOM_TONGA_PP_THERMALCONTROLLER_NONE      0
#define ATOM_TONGA_PP_THERMALCONTROLLER_LM96163   17
#define ATOM_TONGA_PP_THERMALCONTROLLER_TONGA     21
#define ATOM_TONGA_PP_THERMALCONTROLLER_FIJI      22

/*
 * Thermal controller 'combo type' to use an external controller for Fan control and an internal controller for thermal.
 * We probably should reserve the bit 0x80 for this use.
 * To keep the number of these types low we should also use the same code for all ASICs (i.e. do not distinguish RV6xx and RV7xx Internal here).
 * The driver can pick the correct internal controller based on the ASIC.
 */

#define ATOM_TONGA_PP_THERMALCONTROLLER_ADT7473_WITH_INTERNAL   0x89    /* ADT7473 Fan Control + Internal Thermal Controller */
#define ATOM_TONGA_PP_THERMALCONTROLLER_EMC2103_WITH_INTERNAL   0x8D    /* EMC2103 Fan Control + Internal Thermal Controller */

/*/* ATOM_TONGA_POWERPLAYTABLE::ulPlatformCaps */
#define ATOM_TONGA_PP_PLATFORM_CAP_VDDGFX_CONTROL              0x1            /* This cap indicates whether vddgfx will be a separated power rail. */
#define ATOM_TONGA_PP_PLATFORM_CAP_POWERPLAY                   0x2            /* This cap indicates whether this is a mobile part and CCC need to show Powerplay page. */
#define ATOM_TONGA_PP_PLATFORM_CAP_SBIOSPOWERSOURCE            0x4            /* This cap indicates whether power source notificaiton is done by SBIOS directly. */
#define ATOM_TONGA_PP_PLATFORM_CAP_DISABLE_VOLTAGE_ISLAND      0x8            /* Enable the option to overwrite voltage island feature to be disabled, regardless of VddGfx power rail support. */
#define ____RETIRE16____                                0x10
#define ATOM_TONGA_PP_PLATFORM_CAP_HARDWAREDC                 0x20            /* This cap indicates whether power source notificaiton is done by GPIO directly. */
#define ____RETIRE64____                                0x40
#define ____RETIRE128____                               0x80
#define ____RETIRE256____                              0x100
#define ____RETIRE512____                              0x200
#define ____RETIRE1024____                             0x400
#define ____RETIRE2048____                             0x800
#define ATOM_TONGA_PP_PLATFORM_CAP_MVDD_CONTROL             0x1000            /* This cap indicates dynamic MVDD is required. Uncheck to disable it. */
#define ____RETIRE2000____                            0x2000
#define ____RETIRE4000____                            0x4000
#define ATOM_TONGA_PP_PLATFORM_CAP_VDDCI_CONTROL            0x8000            /* This cap indicates dynamic VDDCI is required. Uncheck to disable it. */
#define ____RETIRE10000____                          0x10000
#define ATOM_TONGA_PP_PLATFORM_CAP_BACO                    0x20000            /* Enable to indicate the driver supports BACO state. */

#define ATOM_TONGA_PP_PLATFORM_CAP_OUTPUT_THERMAL2GPIO17         0x100000     /* Enable to indicate the driver supports thermal2GPIO17. */
#define ATOM_TONGA_PP_PLATFORM_COMBINE_PCC_WITH_THERMAL_SIGNAL  0x1000000     /* Enable to indicate if thermal and PCC are sharing the same GPIO */
#define ATOM_TONGA_PLATFORM_LOAD_POST_PRODUCTION_FIRMWARE       0x2000000

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

#define ATOM_Tonga_DISALLOW_ON_DC                       0x00004000
#define ATOM_Tonga_ENABLE_VARIBRIGHT                    0x00008000

#define ATOM_Tonga_TABLE_REVISION_TONGA                 7

typedef struct _ATOM_Tonga_POWERPLAYTABLE {
	ATOM_COMMON_TABLE_HEADER sHeader;

	UCHAR  ucTableRevision;
	USHORT usTableSize;						/*the size of header structure */

	ULONG	ulGoldenPPID;
	ULONG	ulGoldenRevision;
	USHORT	usFormatID;

	USHORT	usVoltageTime;					 /*in microseconds */
	ULONG	ulPlatformCaps;					  /*See ATOM_Tonga_CAPS_* */

	ULONG	ulMaxODEngineClock; 			   /*For Overdrive.  */
	ULONG	ulMaxODMemoryClock; 			   /*For Overdrive. */

	USHORT	usPowerControlLimit;
	USHORT	usUlvVoltageOffset;				  /*in mv units */

	USHORT	usStateArrayOffset;				  /*points to ATOM_Tonga_State_Array */
	USHORT	usFanTableOffset;				  /*points to ATOM_Tonga_Fan_Table */
	USHORT	usThermalControllerOffset;		   /*points to ATOM_Tonga_Thermal_Controller */
	USHORT	usReserv;						   /*CustomThermalPolicy removed for Tonga. Keep this filed as reserved. */

	USHORT	usMclkDependencyTableOffset;	   /*points to ATOM_Tonga_MCLK_Dependency_Table */
	USHORT	usSclkDependencyTableOffset;	   /*points to ATOM_Tonga_SCLK_Dependency_Table */
	USHORT	usVddcLookupTableOffset;		   /*points to ATOM_Tonga_Voltage_Lookup_Table */
	USHORT	usVddgfxLookupTableOffset; 		/*points to ATOM_Tonga_Voltage_Lookup_Table */

	USHORT	usMMDependencyTableOffset;		  /*points to ATOM_Tonga_MM_Dependency_Table */

	USHORT	usVCEStateTableOffset;			   /*points to ATOM_Tonga_VCE_State_Table; */

	USHORT	usPPMTableOffset;				  /*points to ATOM_Tonga_PPM_Table */
	USHORT	usPowerTuneTableOffset;			  /*points to ATOM_PowerTune_Table */

	USHORT	usHardLimitTableOffset; 		   /*points to ATOM_Tonga_Hard_Limit_Table */

	USHORT	usPCIETableOffset;				  /*points to ATOM_Tonga_PCIE_Table */

	USHORT	usGPIOTableOffset;				  /*points to ATOM_Tonga_GPIO_Table */

	USHORT	usReserved[6];					   /*TODO: modify reserved size to fit structure aligning */
} ATOM_Tonga_POWERPLAYTABLE;

typedef struct _ATOM_Tonga_State {
	UCHAR  ucEngineClockIndexHigh;
	UCHAR  ucEngineClockIndexLow;

	UCHAR  ucMemoryClockIndexHigh;
	UCHAR  ucMemoryClockIndexLow;

	UCHAR  ucPCIEGenLow;
	UCHAR  ucPCIEGenHigh;

	UCHAR  ucPCIELaneLow;
	UCHAR  ucPCIELaneHigh;

	USHORT usClassification;
	ULONG ulCapsAndSettings;
	USHORT usClassification2;
	UCHAR  ucUnused[4];
} ATOM_Tonga_State;

typedef struct _ATOM_Tonga_State_Array {
	UCHAR ucRevId;
	UCHAR ucNumEntries;		/* Number of entries. */
	ATOM_Tonga_State entries[1];	/* Dynamically allocate entries. */
} ATOM_Tonga_State_Array;

typedef struct _ATOM_Tonga_MCLK_Dependency_Record {
	UCHAR  ucVddcInd;	/* Vddc voltage */
	USHORT usVddci;
	USHORT usVddgfxOffset;	/* Offset relative to Vddc voltage */
	USHORT usMvdd;
	ULONG ulMclk;
	USHORT usReserved;
} ATOM_Tonga_MCLK_Dependency_Record;

typedef struct _ATOM_Tonga_MCLK_Dependency_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries; 										/* Number of entries. */
	ATOM_Tonga_MCLK_Dependency_Record entries[];				/* Dynamically allocate entries. */
} ATOM_Tonga_MCLK_Dependency_Table;

typedef struct _ATOM_Tonga_SCLK_Dependency_Record {
	UCHAR  ucVddInd;											/* Base voltage */
	USHORT usVddcOffset;										/* Offset relative to base voltage */
	ULONG ulSclk;
	USHORT usEdcCurrent;
	UCHAR  ucReliabilityTemperature;
	UCHAR  ucCKSVOffsetandDisable;							  /* Bits 0~6: Voltage offset for CKS, Bit 7: Disable/enable for the SCLK level. */
} ATOM_Tonga_SCLK_Dependency_Record;

typedef struct _ATOM_Tonga_SCLK_Dependency_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries; 										/* Number of entries. */
	ATOM_Tonga_SCLK_Dependency_Record entries[];				 /* Dynamically allocate entries. */
} ATOM_Tonga_SCLK_Dependency_Table;

typedef struct _ATOM_Polaris_SCLK_Dependency_Record {
	UCHAR  ucVddInd;											/* Base voltage */
	USHORT usVddcOffset;										/* Offset relative to base voltage */
	ULONG ulSclk;
	USHORT usEdcCurrent;
	UCHAR  ucReliabilityTemperature;
	UCHAR  ucCKSVOffsetandDisable;			/* Bits 0~6: Voltage offset for CKS, Bit 7: Disable/enable for the SCLK level. */
	ULONG  ulSclkOffset;
} ATOM_Polaris_SCLK_Dependency_Record;

typedef struct _ATOM_Polaris_SCLK_Dependency_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries;							/* Number of entries. */
	ATOM_Polaris_SCLK_Dependency_Record entries[1];				 /* Dynamically allocate entries. */
} ATOM_Polaris_SCLK_Dependency_Table;

typedef struct _ATOM_Tonga_PCIE_Record {
	UCHAR ucPCIEGenSpeed;
	UCHAR usPCIELaneWidth;
	UCHAR ucReserved[2];
} ATOM_Tonga_PCIE_Record;

typedef struct _ATOM_Tonga_PCIE_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries; 										/* Number of entries. */
	ATOM_Tonga_PCIE_Record entries[1];							/* Dynamically allocate entries. */
} ATOM_Tonga_PCIE_Table;

typedef struct _ATOM_Polaris10_PCIE_Record {
	UCHAR ucPCIEGenSpeed;
	UCHAR usPCIELaneWidth;
	UCHAR ucReserved[2];
	ULONG ulPCIE_Sclk;
} ATOM_Polaris10_PCIE_Record;

typedef struct _ATOM_Polaris10_PCIE_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries;                                         /* Number of entries. */
	ATOM_Polaris10_PCIE_Record entries[1];                      /* Dynamically allocate entries. */
} ATOM_Polaris10_PCIE_Table;


typedef struct _ATOM_Tonga_MM_Dependency_Record {
	UCHAR   ucVddcInd;											 /* VDDC voltage */
	USHORT  usVddgfxOffset;									  /* Offset relative to VDDC voltage */
	ULONG  ulDClk;												/* UVD D-clock */
	ULONG  ulVClk;												/* UVD V-clock */
	ULONG  ulEClk;												/* VCE clock */
	ULONG  ulAClk;												/* ACP clock */
	ULONG  ulSAMUClk;											/* SAMU clock */
} ATOM_Tonga_MM_Dependency_Record;

typedef struct _ATOM_Tonga_MM_Dependency_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries; 										/* Number of entries. */
	ATOM_Tonga_MM_Dependency_Record entries[1]; 			   /* Dynamically allocate entries. */
} ATOM_Tonga_MM_Dependency_Table;

typedef struct _ATOM_Tonga_Voltage_Lookup_Record {
	USHORT usVdd;											   /* Base voltage */
	USHORT usCACLow;
	USHORT usCACMid;
	USHORT usCACHigh;
} ATOM_Tonga_Voltage_Lookup_Record;

typedef struct _ATOM_Tonga_Voltage_Lookup_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries; 										/* Number of entries. */
	ATOM_Tonga_Voltage_Lookup_Record entries[1];				/* Dynamically allocate entries. */
} ATOM_Tonga_Voltage_Lookup_Table;

typedef struct _ATOM_Tonga_Fan_Table {
	UCHAR   ucRevId;						 /* Change this if the table format changes or version changes so that the other fields are not the same. */
	UCHAR   ucTHyst;						 /* Temperature hysteresis. Integer. */
	USHORT  usTMin; 						 /* The temperature, in 0.01 centigrades, below which we just run at a minimal PWM. */
	USHORT  usTMed; 						 /* The middle temperature where we change slopes. */
	USHORT  usTHigh;						 /* The high point above TMed for adjusting the second slope. */
	USHORT  usPWMMin;						 /* The minimum PWM value in percent (0.01% increments). */
	USHORT  usPWMMed;						 /* The PWM value (in percent) at TMed. */
	USHORT  usPWMHigh;						 /* The PWM value at THigh. */
	USHORT  usTMax; 						 /* The max temperature */
	UCHAR   ucFanControlMode;				  /* Legacy or Fuzzy Fan mode */
	USHORT  usFanPWMMax;					  /* Maximum allowed fan power in percent */
	USHORT  usFanOutputSensitivity;		  /* Sensitivity of fan reaction to temepature changes */
	USHORT  usFanRPMMax;					  /* The default value in RPM */
	ULONG  ulMinFanSCLKAcousticLimit;	   /* Minimum Fan Controller SCLK Frequency Acoustic Limit. */
	UCHAR   ucTargetTemperature;			 /* Advanced fan controller target temperature. */
	UCHAR   ucMinimumPWMLimit; 			  /* The minimum PWM that the advanced fan controller can set.	This should be set to the highest PWM that will run the fan at its lowest RPM. */
	USHORT  usReserved;
} ATOM_Tonga_Fan_Table;

typedef struct _ATOM_Fiji_Fan_Table {
	UCHAR   ucRevId;						 /* Change this if the table format changes or version changes so that the other fields are not the same. */
	UCHAR   ucTHyst;						 /* Temperature hysteresis. Integer. */
	USHORT  usTMin; 						 /* The temperature, in 0.01 centigrades, below which we just run at a minimal PWM. */
	USHORT  usTMed; 						 /* The middle temperature where we change slopes. */
	USHORT  usTHigh;						 /* The high point above TMed for adjusting the second slope. */
	USHORT  usPWMMin;						 /* The minimum PWM value in percent (0.01% increments). */
	USHORT  usPWMMed;						 /* The PWM value (in percent) at TMed. */
	USHORT  usPWMHigh;						 /* The PWM value at THigh. */
	USHORT  usTMax; 						 /* The max temperature */
	UCHAR   ucFanControlMode;				  /* Legacy or Fuzzy Fan mode */
	USHORT  usFanPWMMax;					  /* Maximum allowed fan power in percent */
	USHORT  usFanOutputSensitivity;		  /* Sensitivity of fan reaction to temepature changes */
	USHORT  usFanRPMMax;					  /* The default value in RPM */
	ULONG  ulMinFanSCLKAcousticLimit;		/* Minimum Fan Controller SCLK Frequency Acoustic Limit. */
	UCHAR   ucTargetTemperature;			 /* Advanced fan controller target temperature. */
	UCHAR   ucMinimumPWMLimit; 			  /* The minimum PWM that the advanced fan controller can set.	This should be set to the highest PWM that will run the fan at its lowest RPM. */
	USHORT  usFanGainEdge;
	USHORT  usFanGainHotspot;
	USHORT  usFanGainLiquid;
	USHORT  usFanGainVrVddc;
	USHORT  usFanGainVrMvdd;
	USHORT  usFanGainPlx;
	USHORT  usFanGainHbm;
	USHORT  usReserved;
} ATOM_Fiji_Fan_Table;

typedef struct _ATOM_Polaris_Fan_Table {
	UCHAR   ucRevId;						 /* Change this if the table format changes or version changes so that the other fields are not the same. */
	UCHAR   ucTHyst;						 /* Temperature hysteresis. Integer. */
	USHORT  usTMin; 						 /* The temperature, in 0.01 centigrades, below which we just run at a minimal PWM. */
	USHORT  usTMed; 						 /* The middle temperature where we change slopes. */
	USHORT  usTHigh;						 /* The high point above TMed for adjusting the second slope. */
	USHORT  usPWMMin;						 /* The minimum PWM value in percent (0.01% increments). */
	USHORT  usPWMMed;						 /* The PWM value (in percent) at TMed. */
	USHORT  usPWMHigh;						 /* The PWM value at THigh. */
	USHORT  usTMax; 						 /* The max temperature */
	UCHAR   ucFanControlMode;				  /* Legacy or Fuzzy Fan mode */
	USHORT  usFanPWMMax;					  /* Maximum allowed fan power in percent */
	USHORT  usFanOutputSensitivity;		  /* Sensitivity of fan reaction to temepature changes */
	USHORT  usFanRPMMax;					  /* The default value in RPM */
	ULONG  ulMinFanSCLKAcousticLimit;		/* Minimum Fan Controller SCLK Frequency Acoustic Limit. */
	UCHAR   ucTargetTemperature;			 /* Advanced fan controller target temperature. */
	UCHAR   ucMinimumPWMLimit; 			  /* The minimum PWM that the advanced fan controller can set.	This should be set to the highest PWM that will run the fan at its lowest RPM. */
	USHORT  usFanGainEdge;
	USHORT  usFanGainHotspot;
	USHORT  usFanGainLiquid;
	USHORT  usFanGainVrVddc;
	USHORT  usFanGainVrMvdd;
	USHORT  usFanGainPlx;
	USHORT  usFanGainHbm;
	UCHAR   ucEnableZeroRPM;
	UCHAR   ucFanStopTemperature;
	UCHAR   ucFanStartTemperature;
	USHORT  usReserved;
} ATOM_Polaris_Fan_Table;

typedef struct _ATOM_Tonga_Thermal_Controller {
	UCHAR ucRevId;
	UCHAR ucType;		   /* one of ATOM_TONGA_PP_THERMALCONTROLLER_* */
	UCHAR ucI2cLine;		/* as interpreted by DAL I2C */
	UCHAR ucI2cAddress;
	UCHAR ucFanParameters;	/* Fan Control Parameters. */
	UCHAR ucFanMinRPM; 	 /* Fan Minimum RPM (hundreds) -- for display purposes only. */
	UCHAR ucFanMaxRPM; 	 /* Fan Maximum RPM (hundreds) -- for display purposes only. */
	UCHAR ucReserved;
	UCHAR ucFlags;		   /* to be defined */
} ATOM_Tonga_Thermal_Controller;

typedef struct _ATOM_Tonga_VCE_State_Record {
	UCHAR  ucVCEClockIndex;	/*index into usVCEDependencyTableOffset of 'ATOM_Tonga_MM_Dependency_Table' type */
	UCHAR  ucFlag;		/* 2 bits indicates memory p-states */
	UCHAR  ucSCLKIndex;		/*index into ATOM_Tonga_SCLK_Dependency_Table */
	UCHAR  ucMCLKIndex;		/*index into ATOM_Tonga_MCLK_Dependency_Table */
} ATOM_Tonga_VCE_State_Record;

typedef struct _ATOM_Tonga_VCE_State_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries;
	ATOM_Tonga_VCE_State_Record entries[1];
} ATOM_Tonga_VCE_State_Table;

typedef struct _ATOM_Tonga_PowerTune_Table {
	UCHAR  ucRevId;
	USHORT usTDP;
	USHORT usConfigurableTDP;
	USHORT usTDC;
	USHORT usBatteryPowerLimit;
	USHORT usSmallPowerLimit;
	USHORT usLowCACLeakage;
	USHORT usHighCACLeakage;
	USHORT usMaximumPowerDeliveryLimit;
	USHORT usTjMax;
	USHORT usPowerTuneDataSetID;
	USHORT usEDCLimit;
	USHORT usSoftwareShutdownTemp;
	USHORT usClockStretchAmount;
	USHORT usReserve[2];
} ATOM_Tonga_PowerTune_Table;

typedef struct _ATOM_Fiji_PowerTune_Table {
	UCHAR  ucRevId;
	USHORT usTDP;
	USHORT usConfigurableTDP;
	USHORT usTDC;
	USHORT usBatteryPowerLimit;
	USHORT usSmallPowerLimit;
	USHORT usLowCACLeakage;
	USHORT usHighCACLeakage;
	USHORT usMaximumPowerDeliveryLimit;
	USHORT usTjMax;  /* For Fiji, this is also usTemperatureLimitEdge; */
	USHORT usPowerTuneDataSetID;
	USHORT usEDCLimit;
	USHORT usSoftwareShutdownTemp;
	USHORT usClockStretchAmount;
	USHORT usTemperatureLimitHotspot;  /*The following are added for Fiji */
	USHORT usTemperatureLimitLiquid1;
	USHORT usTemperatureLimitLiquid2;
	USHORT usTemperatureLimitVrVddc;
	USHORT usTemperatureLimitVrMvdd;
	USHORT usTemperatureLimitPlx;
	UCHAR  ucLiquid1_I2C_address;  /*Liquid */
	UCHAR  ucLiquid2_I2C_address;
	UCHAR  ucLiquid_I2C_Line;
	UCHAR  ucVr_I2C_address;	/*VR */
	UCHAR  ucVr_I2C_Line;
	UCHAR  ucPlx_I2C_address;  /*PLX */
	UCHAR  ucPlx_I2C_Line;
	USHORT usReserved;
} ATOM_Fiji_PowerTune_Table;

typedef struct _ATOM_Polaris_PowerTune_Table
{
    UCHAR  ucRevId;
    USHORT usTDP;
    USHORT usConfigurableTDP;
    USHORT usTDC;
    USHORT usBatteryPowerLimit;
    USHORT usSmallPowerLimit;
    USHORT usLowCACLeakage;
    USHORT usHighCACLeakage;
    USHORT usMaximumPowerDeliveryLimit;
    USHORT usTjMax;  // For Fiji, this is also usTemperatureLimitEdge;
    USHORT usPowerTuneDataSetID;
    USHORT usEDCLimit;
    USHORT usSoftwareShutdownTemp;
    USHORT usClockStretchAmount;
    USHORT usTemperatureLimitHotspot;  //The following are added for Fiji
    USHORT usTemperatureLimitLiquid1;
    USHORT usTemperatureLimitLiquid2;
    USHORT usTemperatureLimitVrVddc;
    USHORT usTemperatureLimitVrMvdd;
    USHORT usTemperatureLimitPlx;
    UCHAR  ucLiquid1_I2C_address;  //Liquid
    UCHAR  ucLiquid2_I2C_address;
    UCHAR  ucLiquid_I2C_Line;
    UCHAR  ucVr_I2C_address;  //VR
    UCHAR  ucVr_I2C_Line;
    UCHAR  ucPlx_I2C_address;  //PLX
    UCHAR  ucPlx_I2C_Line;
    USHORT usBoostPowerLimit;
    UCHAR  ucCKS_LDO_REFSEL;
    UCHAR  ucHotSpotOnly;
    UCHAR  ucReserve;
    USHORT usReserve;
} ATOM_Polaris_PowerTune_Table;

#define ATOM_PPM_A_A    1
#define ATOM_PPM_A_I    2
typedef struct _ATOM_Tonga_PPM_Table {
	UCHAR   ucRevId;
	UCHAR   ucPpmDesign;		  /*A+I or A+A */
	USHORT  usCpuCoreNumber;
	ULONG  ulPlatformTDP;
	ULONG  ulSmallACPlatformTDP;
	ULONG  ulPlatformTDC;
	ULONG  ulSmallACPlatformTDC;
	ULONG  ulApuTDP;
	ULONG  ulDGpuTDP;
	ULONG  ulDGpuUlvPower;
	ULONG  ulTjmax;
} ATOM_Tonga_PPM_Table;

typedef struct _ATOM_Tonga_Hard_Limit_Record {
	ULONG  ulSCLKLimit;
	ULONG  ulMCLKLimit;
	USHORT  usVddcLimit;
	USHORT  usVddciLimit;
	USHORT  usVddgfxLimit;
} ATOM_Tonga_Hard_Limit_Record;

typedef struct _ATOM_Tonga_Hard_Limit_Table {
	UCHAR ucRevId;
	UCHAR ucNumEntries;
	ATOM_Tonga_Hard_Limit_Record entries[1];
} ATOM_Tonga_Hard_Limit_Table;

typedef struct _ATOM_Tonga_GPIO_Table {
	UCHAR  ucRevId;
	UCHAR  ucVRHotTriggeredSclkDpmIndex;		/* If VRHot signal is triggered SCLK will be limited to this DPM level */
	UCHAR  ucReserve[5];
} ATOM_Tonga_GPIO_Table;

typedef struct _PPTable_Generic_SubTable_Header {
	UCHAR  ucRevId;
} PPTable_Generic_SubTable_Header;


#pragma pack(pop)


#endif
