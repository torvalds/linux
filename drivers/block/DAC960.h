/*

  Linux Driver for Mylex DAC960/AcceleRAID/eXtremeRAID PCI RAID Controllers

  Copyright 1998-2001 by Leonard N. Zubkoff <lnz@dandelion.com>

  This program is free software; you may redistribute and/or modify it under
  the terms of the GNU General Public License Version 2 as published by the
  Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
  for complete details.

  The author respectfully requests that any modifications to this software be
  sent directly to him for evaluation and testing.

*/


/*
  Define the maximum number of DAC960 Controllers supported by this driver.
*/

#define DAC960_MaxControllers			8


/*
  Define the maximum number of Controller Channels supported by DAC960
  V1 and V2 Firmware Controllers.
*/

#define DAC960_V1_MaxChannels			3
#define DAC960_V2_MaxChannels			4


/*
  Define the maximum number of Targets per Channel supported by DAC960
  V1 and V2 Firmware Controllers.
*/

#define DAC960_V1_MaxTargets			16
#define DAC960_V2_MaxTargets			128


/*
  Define the maximum number of Logical Drives supported by DAC960
  V1 and V2 Firmware Controllers.
*/

#define DAC960_MaxLogicalDrives			32


/*
  Define the maximum number of Physical Devices supported by DAC960
  V1 and V2 Firmware Controllers.
*/

#define DAC960_V1_MaxPhysicalDevices		45
#define DAC960_V2_MaxPhysicalDevices		272

/*
  Define the pci dma mask supported by DAC960 V1 and V2 Firmware Controlers
 */

#define DAC690_V1_PciDmaMask	0xffffffff
#define DAC690_V2_PciDmaMask	0xffffffffffffffffULL

/*
  Define a Boolean data type.
*/

typedef enum { false, true } __attribute__ ((packed)) boolean;


/*
  Define a 32/64 bit I/O Address data type.
*/

typedef unsigned long DAC960_IO_Address_T;


/*
  Define a 32/64 bit PCI Bus Address data type.
*/

typedef unsigned long DAC960_PCI_Address_T;


/*
  Define a 32 bit Bus Address data type.
*/

typedef unsigned int DAC960_BusAddress32_T;


/*
  Define a 64 bit Bus Address data type.
*/

typedef unsigned long long DAC960_BusAddress64_T;


/*
  Define a 32 bit Byte Count data type.
*/

typedef unsigned int DAC960_ByteCount32_T;


/*
  Define a 64 bit Byte Count data type.
*/

typedef unsigned long long DAC960_ByteCount64_T;


/*
  dma_loaf is used by helper routines to divide a region of
  dma mapped memory into smaller pieces, where those pieces
  are not of uniform size.
 */

struct dma_loaf {
	void	*cpu_base;
	dma_addr_t dma_base;
	size_t  length;
	void	*cpu_free;
	dma_addr_t dma_free;
};

/*
  Define the SCSI INQUIRY Standard Data structure.
*/

typedef struct DAC960_SCSI_Inquiry
{
  unsigned char PeripheralDeviceType:5;			/* Byte 0 Bits 0-4 */
  unsigned char PeripheralQualifier:3;			/* Byte 0 Bits 5-7 */
  unsigned char DeviceTypeModifier:7;			/* Byte 1 Bits 0-6 */
  boolean RMB:1;					/* Byte 1 Bit 7 */
  unsigned char ANSI_ApprovedVersion:3;			/* Byte 2 Bits 0-2 */
  unsigned char ECMA_Version:3;				/* Byte 2 Bits 3-5 */
  unsigned char ISO_Version:2;				/* Byte 2 Bits 6-7 */
  unsigned char ResponseDataFormat:4;			/* Byte 3 Bits 0-3 */
  unsigned char :2;					/* Byte 3 Bits 4-5 */
  boolean TrmIOP:1;					/* Byte 3 Bit 6 */
  boolean AENC:1;					/* Byte 3 Bit 7 */
  unsigned char AdditionalLength;			/* Byte 4 */
  unsigned char :8;					/* Byte 5 */
  unsigned char :8;					/* Byte 6 */
  boolean SftRe:1;					/* Byte 7 Bit 0 */
  boolean CmdQue:1;					/* Byte 7 Bit 1 */
  boolean :1;						/* Byte 7 Bit 2 */
  boolean Linked:1;					/* Byte 7 Bit 3 */
  boolean Sync:1;					/* Byte 7 Bit 4 */
  boolean WBus16:1;					/* Byte 7 Bit 5 */
  boolean WBus32:1;					/* Byte 7 Bit 6 */
  boolean RelAdr:1;					/* Byte 7 Bit 7 */
  unsigned char VendorIdentification[8];		/* Bytes 8-15 */
  unsigned char ProductIdentification[16];		/* Bytes 16-31 */
  unsigned char ProductRevisionLevel[4];		/* Bytes 32-35 */
}
DAC960_SCSI_Inquiry_T;


/*
  Define the SCSI INQUIRY Unit Serial Number structure.
*/

typedef struct DAC960_SCSI_Inquiry_UnitSerialNumber
{
  unsigned char PeripheralDeviceType:5;			/* Byte 0 Bits 0-4 */
  unsigned char PeripheralQualifier:3;			/* Byte 0 Bits 5-7 */
  unsigned char PageCode;				/* Byte 1 */
  unsigned char :8;					/* Byte 2 */
  unsigned char PageLength;				/* Byte 3 */
  unsigned char ProductSerialNumber[28];		/* Bytes 4-31 */
}
DAC960_SCSI_Inquiry_UnitSerialNumber_T;


/*
  Define the SCSI REQUEST SENSE Sense Key type.
*/

typedef enum
{
  DAC960_SenseKey_NoSense =			0x0,
  DAC960_SenseKey_RecoveredError =		0x1,
  DAC960_SenseKey_NotReady =			0x2,
  DAC960_SenseKey_MediumError =			0x3,
  DAC960_SenseKey_HardwareError =		0x4,
  DAC960_SenseKey_IllegalRequest =		0x5,
  DAC960_SenseKey_UnitAttention =		0x6,
  DAC960_SenseKey_DataProtect =			0x7,
  DAC960_SenseKey_BlankCheck =			0x8,
  DAC960_SenseKey_VendorSpecific =		0x9,
  DAC960_SenseKey_CopyAborted =			0xA,
  DAC960_SenseKey_AbortedCommand =		0xB,
  DAC960_SenseKey_Equal =			0xC,
  DAC960_SenseKey_VolumeOverflow =		0xD,
  DAC960_SenseKey_Miscompare =			0xE,
  DAC960_SenseKey_Reserved =			0xF
}
__attribute__ ((packed))
DAC960_SCSI_RequestSenseKey_T;


/*
  Define the SCSI REQUEST SENSE structure.
*/

typedef struct DAC960_SCSI_RequestSense
{
  unsigned char ErrorCode:7;				/* Byte 0 Bits 0-6 */
  boolean Valid:1;					/* Byte 0 Bit 7 */
  unsigned char SegmentNumber;				/* Byte 1 */
  DAC960_SCSI_RequestSenseKey_T SenseKey:4;		/* Byte 2 Bits 0-3 */
  unsigned char :1;					/* Byte 2 Bit 4 */
  boolean ILI:1;					/* Byte 2 Bit 5 */
  boolean EOM:1;					/* Byte 2 Bit 6 */
  boolean Filemark:1;					/* Byte 2 Bit 7 */
  unsigned char Information[4];				/* Bytes 3-6 */
  unsigned char AdditionalSenseLength;			/* Byte 7 */
  unsigned char CommandSpecificInformation[4];		/* Bytes 8-11 */
  unsigned char AdditionalSenseCode;			/* Byte 12 */
  unsigned char AdditionalSenseCodeQualifier;		/* Byte 13 */
}
DAC960_SCSI_RequestSense_T;


/*
  Define the DAC960 V1 Firmware Command Opcodes.
*/

typedef enum
{
  /* I/O Commands */
  DAC960_V1_ReadExtended =			0x33,
  DAC960_V1_WriteExtended =			0x34,
  DAC960_V1_ReadAheadExtended =			0x35,
  DAC960_V1_ReadExtendedWithScatterGather =	0xB3,
  DAC960_V1_WriteExtendedWithScatterGather =	0xB4,
  DAC960_V1_Read =				0x36,
  DAC960_V1_ReadWithScatterGather =		0xB6,
  DAC960_V1_Write =				0x37,
  DAC960_V1_WriteWithScatterGather =		0xB7,
  DAC960_V1_DCDB =				0x04,
  DAC960_V1_DCDBWithScatterGather =		0x84,
  DAC960_V1_Flush =				0x0A,
  /* Controller Status Related Commands */
  DAC960_V1_Enquiry =				0x53,
  DAC960_V1_Enquiry2 =				0x1C,
  DAC960_V1_GetLogicalDriveElement =		0x55,
  DAC960_V1_GetLogicalDriveInformation =	0x19,
  DAC960_V1_IOPortRead =			0x39,
  DAC960_V1_IOPortWrite =			0x3A,
  DAC960_V1_GetSDStats =			0x3E,
  DAC960_V1_GetPDStats =			0x3F,
  DAC960_V1_PerformEventLogOperation =		0x72,
  /* Device Related Commands */
  DAC960_V1_StartDevice =			0x10,
  DAC960_V1_GetDeviceState =			0x50,
  DAC960_V1_StopChannel =			0x13,
  DAC960_V1_StartChannel =			0x12,
  DAC960_V1_ResetChannel =			0x1A,
  /* Commands Associated with Data Consistency and Errors */
  DAC960_V1_Rebuild =				0x09,
  DAC960_V1_RebuildAsync =			0x16,
  DAC960_V1_CheckConsistency =			0x0F,
  DAC960_V1_CheckConsistencyAsync =		0x1E,
  DAC960_V1_RebuildStat =			0x0C,
  DAC960_V1_GetRebuildProgress =		0x27,
  DAC960_V1_RebuildControl =			0x1F,
  DAC960_V1_ReadBadBlockTable =			0x0B,
  DAC960_V1_ReadBadDataTable =			0x25,
  DAC960_V1_ClearBadDataTable =			0x26,
  DAC960_V1_GetErrorTable =			0x17,
  DAC960_V1_AddCapacityAsync =			0x2A,
  DAC960_V1_BackgroundInitializationControl =	0x2B,
  /* Configuration Related Commands */
  DAC960_V1_ReadConfig2 =			0x3D,
  DAC960_V1_WriteConfig2 =			0x3C,
  DAC960_V1_ReadConfigurationOnDisk =		0x4A,
  DAC960_V1_WriteConfigurationOnDisk =		0x4B,
  DAC960_V1_ReadConfiguration =			0x4E,
  DAC960_V1_ReadBackupConfiguration =		0x4D,
  DAC960_V1_WriteConfiguration =		0x4F,
  DAC960_V1_AddConfiguration =			0x4C,
  DAC960_V1_ReadConfigurationLabel =		0x48,
  DAC960_V1_WriteConfigurationLabel =		0x49,
  /* Firmware Upgrade Related Commands */
  DAC960_V1_LoadImage =				0x20,
  DAC960_V1_StoreImage =			0x21,
  DAC960_V1_ProgramImage =			0x22,
  /* Diagnostic Commands */
  DAC960_V1_SetDiagnosticMode =			0x31,
  DAC960_V1_RunDiagnostic =			0x32,
  /* Subsystem Service Commands */
  DAC960_V1_GetSubsystemData =			0x70,
  DAC960_V1_SetSubsystemParameters =		0x71,
  /* Version 2.xx Firmware Commands */
  DAC960_V1_Enquiry_Old =			0x05,
  DAC960_V1_GetDeviceState_Old =		0x14,
  DAC960_V1_Read_Old =				0x02,
  DAC960_V1_Write_Old =				0x03,
  DAC960_V1_ReadWithScatterGather_Old =		0x82,
  DAC960_V1_WriteWithScatterGather_Old =	0x83
}
__attribute__ ((packed))
DAC960_V1_CommandOpcode_T;


/*
  Define the DAC960 V1 Firmware Command Identifier type.
*/

typedef unsigned char DAC960_V1_CommandIdentifier_T;


/*
  Define the DAC960 V1 Firmware Command Status Codes.
*/

#define DAC960_V1_NormalCompletion		0x0000	/* Common */
#define DAC960_V1_CheckConditionReceived	0x0002	/* Common */
#define DAC960_V1_NoDeviceAtAddress		0x0102	/* Common */
#define DAC960_V1_InvalidDeviceAddress		0x0105	/* Common */
#define DAC960_V1_InvalidParameter		0x0105	/* Common */
#define DAC960_V1_IrrecoverableDataError	0x0001	/* I/O */
#define DAC960_V1_LogicalDriveNonexistentOrOffline 0x0002 /* I/O */
#define DAC960_V1_AccessBeyondEndOfLogicalDrive	0x0105	/* I/O */
#define DAC960_V1_BadDataEncountered		0x010C	/* I/O */
#define DAC960_V1_DeviceBusy			0x0008	/* DCDB */
#define DAC960_V1_DeviceNonresponsive		0x000E	/* DCDB */
#define DAC960_V1_CommandTerminatedAbnormally	0x000F	/* DCDB */
#define DAC960_V1_UnableToStartDevice		0x0002	/* Device */
#define DAC960_V1_InvalidChannelOrTargetOrModifier 0x0105 /* Device */
#define DAC960_V1_ChannelBusy			0x0106	/* Device */
#define DAC960_V1_ChannelNotStopped		0x0002	/* Device */
#define DAC960_V1_AttemptToRebuildOnlineDrive	0x0002	/* Consistency */
#define DAC960_V1_RebuildBadBlocksEncountered	0x0003	/* Consistency */
#define DAC960_V1_NewDiskFailedDuringRebuild	0x0004	/* Consistency */
#define DAC960_V1_RebuildOrCheckAlreadyInProgress 0x0106 /* Consistency */
#define DAC960_V1_DependentDiskIsDead		0x0002	/* Consistency */
#define DAC960_V1_InconsistentBlocksFound	0x0003	/* Consistency */
#define DAC960_V1_InvalidOrNonredundantLogicalDrive 0x0105 /* Consistency */
#define DAC960_V1_NoRebuildOrCheckInProgress	0x0105	/* Consistency */
#define DAC960_V1_RebuildInProgress_DataValid	0x0000	/* Consistency */
#define DAC960_V1_RebuildFailed_LogicalDriveFailure 0x0002 /* Consistency */
#define DAC960_V1_RebuildFailed_BadBlocksOnOther 0x0003	/* Consistency */
#define DAC960_V1_RebuildFailed_NewDriveFailed	0x0004	/* Consistency */
#define DAC960_V1_RebuildSuccessful		0x0100	/* Consistency */
#define DAC960_V1_RebuildSuccessfullyTerminated	0x0107	/* Consistency */
#define DAC960_V1_BackgroundInitSuccessful	0x0100	/* Consistency */
#define DAC960_V1_BackgroundInitAborted		0x0005	/* Consistency */
#define DAC960_V1_NoBackgroundInitInProgress	0x0105	/* Consistency */
#define DAC960_V1_AddCapacityInProgress		0x0004	/* Consistency */
#define DAC960_V1_AddCapacityFailedOrSuspended	0x00F4	/* Consistency */
#define DAC960_V1_Config2ChecksumError		0x0002	/* Configuration */
#define DAC960_V1_ConfigurationSuspended	0x0106	/* Configuration */
#define DAC960_V1_FailedToConfigureNVRAM	0x0105	/* Configuration */
#define DAC960_V1_ConfigurationNotSavedStateChange 0x0106 /* Configuration */
#define DAC960_V1_SubsystemNotInstalled		0x0001	/* Subsystem */
#define DAC960_V1_SubsystemFailed		0x0002	/* Subsystem */
#define DAC960_V1_SubsystemBusy			0x0106	/* Subsystem */

typedef unsigned short DAC960_V1_CommandStatus_T;


/*
  Define the DAC960 V1 Firmware Enquiry Command reply structure.
*/

typedef struct DAC960_V1_Enquiry
{
  unsigned char NumberOfLogicalDrives;			/* Byte 0 */
  unsigned int :24;					/* Bytes 1-3 */
  unsigned int LogicalDriveSizes[32];			/* Bytes 4-131 */
  unsigned short FlashAge;				/* Bytes 132-133 */
  struct {
    boolean DeferredWriteError:1;			/* Byte 134 Bit 0 */
    boolean BatteryLow:1;				/* Byte 134 Bit 1 */
    unsigned char :6;					/* Byte 134 Bits 2-7 */
  } StatusFlags;
  unsigned char :8;					/* Byte 135 */
  unsigned char MinorFirmwareVersion;			/* Byte 136 */
  unsigned char MajorFirmwareVersion;			/* Byte 137 */
  enum {
    DAC960_V1_NoStandbyRebuildOrCheckInProgress =		    0x00,
    DAC960_V1_StandbyRebuildInProgress =			    0x01,
    DAC960_V1_BackgroundRebuildInProgress =			    0x02,
    DAC960_V1_BackgroundCheckInProgress =			    0x03,
    DAC960_V1_StandbyRebuildCompletedWithError =		    0xFF,
    DAC960_V1_BackgroundRebuildOrCheckFailed_DriveFailed =	    0xF0,
    DAC960_V1_BackgroundRebuildOrCheckFailed_LogicalDriveFailed =   0xF1,
    DAC960_V1_BackgroundRebuildOrCheckFailed_OtherCauses =	    0xF2,
    DAC960_V1_BackgroundRebuildOrCheckSuccessfullyTerminated =	    0xF3
  } __attribute__ ((packed)) RebuildFlag;		/* Byte 138 */
  unsigned char MaxCommands;				/* Byte 139 */
  unsigned char OfflineLogicalDriveCount;		/* Byte 140 */
  unsigned char :8;					/* Byte 141 */
  unsigned short EventLogSequenceNumber;		/* Bytes 142-143 */
  unsigned char CriticalLogicalDriveCount;		/* Byte 144 */
  unsigned int :24;					/* Bytes 145-147 */
  unsigned char DeadDriveCount;				/* Byte 148 */
  unsigned char :8;					/* Byte 149 */
  unsigned char RebuildCount;				/* Byte 150 */
  struct {
    unsigned char :3;					/* Byte 151 Bits 0-2 */
    boolean BatteryBackupUnitPresent:1;			/* Byte 151 Bit 3 */
    unsigned char :3;					/* Byte 151 Bits 4-6 */
    unsigned char :1;					/* Byte 151 Bit 7 */
  } MiscFlags;
  struct {
    unsigned char TargetID;
    unsigned char Channel;
  } DeadDrives[21];					/* Bytes 152-194 */
  unsigned char Reserved[62];				/* Bytes 195-255 */
}
__attribute__ ((packed))
DAC960_V1_Enquiry_T;


/*
  Define the DAC960 V1 Firmware Enquiry2 Command reply structure.
*/

typedef struct DAC960_V1_Enquiry2
{
  struct {
    enum {
      DAC960_V1_P_PD_PU =			0x01,
      DAC960_V1_PL =				0x02,
      DAC960_V1_PG =				0x10,
      DAC960_V1_PJ =				0x11,
      DAC960_V1_PR =				0x12,
      DAC960_V1_PT =				0x13,
      DAC960_V1_PTL0 =				0x14,
      DAC960_V1_PRL =				0x15,
      DAC960_V1_PTL1 =				0x16,
      DAC960_V1_1164P =				0x20
    } __attribute__ ((packed)) SubModel;		/* Byte 0 */
    unsigned char ActualChannels;			/* Byte 1 */
    enum {
      DAC960_V1_FiveChannelBoard =		0x01,
      DAC960_V1_ThreeChannelBoard =		0x02,
      DAC960_V1_TwoChannelBoard =		0x03,
      DAC960_V1_ThreeChannelASIC_DAC =		0x04
    } __attribute__ ((packed)) Model;			/* Byte 2 */
    enum {
      DAC960_V1_EISA_Controller =		0x01,
      DAC960_V1_MicroChannel_Controller =	0x02,
      DAC960_V1_PCI_Controller =		0x03,
      DAC960_V1_SCSItoSCSI_Controller =		0x08
    } __attribute__ ((packed)) ProductFamily;		/* Byte 3 */
  } HardwareID;						/* Bytes 0-3 */
  /* MajorVersion.MinorVersion-FirmwareType-TurnID */
  struct {
    unsigned char MajorVersion;				/* Byte 4 */
    unsigned char MinorVersion;				/* Byte 5 */
    unsigned char TurnID;				/* Byte 6 */
    char FirmwareType;					/* Byte 7 */
  } FirmwareID;						/* Bytes 4-7 */
  unsigned char :8;					/* Byte 8 */
  unsigned int :24;					/* Bytes 9-11 */
  unsigned char ConfiguredChannels;			/* Byte 12 */
  unsigned char ActualChannels;				/* Byte 13 */
  unsigned char MaxTargets;				/* Byte 14 */
  unsigned char MaxTags;				/* Byte 15 */
  unsigned char MaxLogicalDrives;			/* Byte 16 */
  unsigned char MaxArms;				/* Byte 17 */
  unsigned char MaxSpans;				/* Byte 18 */
  unsigned char :8;					/* Byte 19 */
  unsigned int :32;					/* Bytes 20-23 */
  unsigned int MemorySize;				/* Bytes 24-27 */
  unsigned int CacheSize;				/* Bytes 28-31 */
  unsigned int FlashMemorySize;				/* Bytes 32-35 */
  unsigned int NonVolatileMemorySize;			/* Bytes 36-39 */
  struct {
    enum {
      DAC960_V1_RamType_DRAM =			0x0,
      DAC960_V1_RamType_EDO =			0x1,
      DAC960_V1_RamType_SDRAM =			0x2,
      DAC960_V1_RamType_Last =			0x7
    } __attribute__ ((packed)) RamType:3;		/* Byte 40 Bits 0-2 */
    enum {
      DAC960_V1_ErrorCorrection_None =		0x0,
      DAC960_V1_ErrorCorrection_Parity =	0x1,
      DAC960_V1_ErrorCorrection_ECC =		0x2,
      DAC960_V1_ErrorCorrection_Last =		0x7
    } __attribute__ ((packed)) ErrorCorrection:3;	/* Byte 40 Bits 3-5 */
    boolean FastPageMode:1;				/* Byte 40 Bit 6 */
    boolean LowPowerMemory:1;				/* Byte 40 Bit 7 */
    unsigned char :8;					/* Bytes 41 */
  } MemoryType;
  unsigned short ClockSpeed;				/* Bytes 42-43 */
  unsigned short MemorySpeed;				/* Bytes 44-45 */
  unsigned short HardwareSpeed;				/* Bytes 46-47 */
  unsigned int :32;					/* Bytes 48-51 */
  unsigned int :32;					/* Bytes 52-55 */
  unsigned char :8;					/* Byte 56 */
  unsigned char :8;					/* Byte 57 */
  unsigned short :16;					/* Bytes 58-59 */
  unsigned short MaxCommands;				/* Bytes 60-61 */
  unsigned short MaxScatterGatherEntries;		/* Bytes 62-63 */
  unsigned short MaxDriveCommands;			/* Bytes 64-65 */
  unsigned short MaxIODescriptors;			/* Bytes 66-67 */
  unsigned short MaxCombinedSectors;			/* Bytes 68-69 */
  unsigned char Latency;				/* Byte 70 */
  unsigned char :8;					/* Byte 71 */
  unsigned char SCSITimeout;				/* Byte 72 */
  unsigned char :8;					/* Byte 73 */
  unsigned short MinFreeLines;				/* Bytes 74-75 */
  unsigned int :32;					/* Bytes 76-79 */
  unsigned int :32;					/* Bytes 80-83 */
  unsigned char RebuildRateConstant;			/* Byte 84 */
  unsigned char :8;					/* Byte 85 */
  unsigned char :8;					/* Byte 86 */
  unsigned char :8;					/* Byte 87 */
  unsigned int :32;					/* Bytes 88-91 */
  unsigned int :32;					/* Bytes 92-95 */
  unsigned short PhysicalDriveBlockSize;		/* Bytes 96-97 */
  unsigned short LogicalDriveBlockSize;			/* Bytes 98-99 */
  unsigned short MaxBlocksPerCommand;			/* Bytes 100-101 */
  unsigned short BlockFactor;				/* Bytes 102-103 */
  unsigned short CacheLineSize;				/* Bytes 104-105 */
  struct {
    enum {
      DAC960_V1_Narrow_8bit =			0x0,
      DAC960_V1_Wide_16bit =			0x1,
      DAC960_V1_Wide_32bit =			0x2
    } __attribute__ ((packed)) BusWidth:2;		/* Byte 106 Bits 0-1 */
    enum {
      DAC960_V1_Fast =				0x0,
      DAC960_V1_Ultra =				0x1,
      DAC960_V1_Ultra2 =			0x2
    } __attribute__ ((packed)) BusSpeed:2;		/* Byte 106 Bits 2-3 */
    boolean Differential:1;				/* Byte 106 Bit 4 */
    unsigned char :3;					/* Byte 106 Bits 5-7 */
  } SCSICapability;
  unsigned char :8;					/* Byte 107 */
  unsigned int :32;					/* Bytes 108-111 */
  unsigned short FirmwareBuildNumber;			/* Bytes 112-113 */
  enum {
    DAC960_V1_AEMI =				0x01,
    DAC960_V1_OEM1 =				0x02,
    DAC960_V1_OEM2 =				0x04,
    DAC960_V1_OEM3 =				0x08,
    DAC960_V1_Conner =				0x10,
    DAC960_V1_SAFTE =				0x20
  } __attribute__ ((packed)) FaultManagementType;	/* Byte 114 */
  unsigned char :8;					/* Byte 115 */
  struct {
    boolean Clustering:1;				/* Byte 116 Bit 0 */
    boolean MylexOnlineRAIDExpansion:1;			/* Byte 116 Bit 1 */
    boolean ReadAhead:1;				/* Byte 116 Bit 2 */
    boolean BackgroundInitialization:1;			/* Byte 116 Bit 3 */
    unsigned int :28;					/* Bytes 116-119 */
  } FirmwareFeatures;
  unsigned int :32;					/* Bytes 120-123 */
  unsigned int :32;					/* Bytes 124-127 */
}
DAC960_V1_Enquiry2_T;


/*
  Define the DAC960 V1 Firmware Logical Drive State type.
*/

typedef enum
{
  DAC960_V1_LogicalDrive_Online =		0x03,
  DAC960_V1_LogicalDrive_Critical =		0x04,
  DAC960_V1_LogicalDrive_Offline =		0xFF
}
__attribute__ ((packed))
DAC960_V1_LogicalDriveState_T;


/*
  Define the DAC960 V1 Firmware Logical Drive Information structure.
*/

typedef struct DAC960_V1_LogicalDriveInformation
{
  unsigned int LogicalDriveSize;			/* Bytes 0-3 */
  DAC960_V1_LogicalDriveState_T LogicalDriveState;	/* Byte 4 */
  unsigned char RAIDLevel:7;				/* Byte 5 Bits 0-6 */
  boolean WriteBack:1;					/* Byte 5 Bit 7 */
  unsigned short :16;					/* Bytes 6-7 */
}
DAC960_V1_LogicalDriveInformation_T;


/*
  Define the DAC960 V1 Firmware Get Logical Drive Information Command
  reply structure.
*/

typedef DAC960_V1_LogicalDriveInformation_T
	DAC960_V1_LogicalDriveInformationArray_T[DAC960_MaxLogicalDrives];


/*
  Define the DAC960 V1 Firmware Perform Event Log Operation Types.
*/

typedef enum
{
  DAC960_V1_GetEventLogEntry =			0x00
}
__attribute__ ((packed))
DAC960_V1_PerformEventLogOpType_T;


/*
  Define the DAC960 V1 Firmware Get Event Log Entry Command reply structure.
*/

typedef struct DAC960_V1_EventLogEntry
{
  unsigned char MessageType;				/* Byte 0 */
  unsigned char MessageLength;				/* Byte 1 */
  unsigned char TargetID:5;				/* Byte 2 Bits 0-4 */
  unsigned char Channel:3;				/* Byte 2 Bits 5-7 */
  unsigned char LogicalUnit:6;				/* Byte 3 Bits 0-5 */
  unsigned char :2;					/* Byte 3 Bits 6-7 */
  unsigned short SequenceNumber;			/* Bytes 4-5 */
  unsigned char ErrorCode:7;				/* Byte 6 Bits 0-6 */
  boolean Valid:1;					/* Byte 6 Bit 7 */
  unsigned char SegmentNumber;				/* Byte 7 */
  DAC960_SCSI_RequestSenseKey_T SenseKey:4;		/* Byte 8 Bits 0-3 */
  unsigned char :1;					/* Byte 8 Bit 4 */
  boolean ILI:1;					/* Byte 8 Bit 5 */
  boolean EOM:1;					/* Byte 8 Bit 6 */
  boolean Filemark:1;					/* Byte 8 Bit 7 */
  unsigned char Information[4];				/* Bytes 9-12 */
  unsigned char AdditionalSenseLength;			/* Byte 13 */
  unsigned char CommandSpecificInformation[4];		/* Bytes 14-17 */
  unsigned char AdditionalSenseCode;			/* Byte 18 */
  unsigned char AdditionalSenseCodeQualifier;		/* Byte 19 */
  unsigned char Dummy[12];				/* Bytes 20-31 */
}
DAC960_V1_EventLogEntry_T;


/*
  Define the DAC960 V1 Firmware Physical Device State type.
*/

typedef enum
{
    DAC960_V1_Device_Dead =			0x00,
    DAC960_V1_Device_WriteOnly =		0x02,
    DAC960_V1_Device_Online =			0x03,
    DAC960_V1_Device_Standby =			0x10
}
__attribute__ ((packed))
DAC960_V1_PhysicalDeviceState_T;


/*
  Define the DAC960 V1 Firmware Get Device State Command reply structure.
  The structure is padded by 2 bytes for compatibility with Version 2.xx
  Firmware.
*/

typedef struct DAC960_V1_DeviceState
{
  boolean Present:1;					/* Byte 0 Bit 0 */
  unsigned char :7;					/* Byte 0 Bits 1-7 */
  enum {
    DAC960_V1_OtherType =			0x0,
    DAC960_V1_DiskType =			0x1,
    DAC960_V1_SequentialType =			0x2,
    DAC960_V1_CDROM_or_WORM_Type =		0x3
    } __attribute__ ((packed)) DeviceType:2;		/* Byte 1 Bits 0-1 */
  boolean :1;						/* Byte 1 Bit 2 */
  boolean Fast20:1;					/* Byte 1 Bit 3 */
  boolean Sync:1;					/* Byte 1 Bit 4 */
  boolean Fast:1;					/* Byte 1 Bit 5 */
  boolean Wide:1;					/* Byte 1 Bit 6 */
  boolean TaggedQueuingSupported:1;			/* Byte 1 Bit 7 */
  DAC960_V1_PhysicalDeviceState_T DeviceState;		/* Byte 2 */
  unsigned char :8;					/* Byte 3 */
  unsigned char SynchronousMultiplier;			/* Byte 4 */
  unsigned char SynchronousOffset:5;			/* Byte 5 Bits 0-4 */
  unsigned char :3;					/* Byte 5 Bits 5-7 */
  unsigned int DiskSize __attribute__ ((packed));	/* Bytes 6-9 */
  unsigned short :16;					/* Bytes 10-11 */
}
DAC960_V1_DeviceState_T;


/*
  Define the DAC960 V1 Firmware Get Rebuild Progress Command reply structure.
*/

typedef struct DAC960_V1_RebuildProgress
{
  unsigned int LogicalDriveNumber;			/* Bytes 0-3 */
  unsigned int LogicalDriveSize;			/* Bytes 4-7 */
  unsigned int RemainingBlocks;				/* Bytes 8-11 */
}
DAC960_V1_RebuildProgress_T;


/*
  Define the DAC960 V1 Firmware Background Initialization Status Command
  reply structure.
*/

typedef struct DAC960_V1_BackgroundInitializationStatus
{
  unsigned int LogicalDriveSize;			/* Bytes 0-3 */
  unsigned int BlocksCompleted;				/* Bytes 4-7 */
  unsigned char Reserved1[12];				/* Bytes 8-19 */
  unsigned int LogicalDriveNumber;			/* Bytes 20-23 */
  unsigned char RAIDLevel;				/* Byte 24 */
  enum {
    DAC960_V1_BackgroundInitializationInvalid =	    0x00,
    DAC960_V1_BackgroundInitializationStarted =	    0x02,
    DAC960_V1_BackgroundInitializationInProgress =  0x04,
    DAC960_V1_BackgroundInitializationSuspended =   0x05,
    DAC960_V1_BackgroundInitializationCancelled =   0x06
  } __attribute__ ((packed)) Status;			/* Byte 25 */
  unsigned char Reserved2[6];				/* Bytes 26-31 */
}
DAC960_V1_BackgroundInitializationStatus_T;


/*
  Define the DAC960 V1 Firmware Error Table Entry structure.
*/

typedef struct DAC960_V1_ErrorTableEntry
{
  unsigned char ParityErrorCount;			/* Byte 0 */
  unsigned char SoftErrorCount;				/* Byte 1 */
  unsigned char HardErrorCount;				/* Byte 2 */
  unsigned char MiscErrorCount;				/* Byte 3 */
}
DAC960_V1_ErrorTableEntry_T;


/*
  Define the DAC960 V1 Firmware Get Error Table Command reply structure.
*/

typedef struct DAC960_V1_ErrorTable
{
  DAC960_V1_ErrorTableEntry_T
    ErrorTableEntries[DAC960_V1_MaxChannels][DAC960_V1_MaxTargets];
}
DAC960_V1_ErrorTable_T;


/*
  Define the DAC960 V1 Firmware Read Config2 Command reply structure.
*/

typedef struct DAC960_V1_Config2
{
  unsigned char :1;					/* Byte 0 Bit 0 */
  boolean ActiveNegationEnabled:1;			/* Byte 0 Bit 1 */
  unsigned char :5;					/* Byte 0 Bits 2-6 */
  boolean NoRescanIfResetReceivedDuringScan:1;		/* Byte 0 Bit 7 */
  boolean StorageWorksSupportEnabled:1;			/* Byte 1 Bit 0 */
  boolean HewlettPackardSupportEnabled:1;		/* Byte 1 Bit 1 */
  boolean NoDisconnectOnFirstCommand:1;			/* Byte 1 Bit 2 */
  unsigned char :2;					/* Byte 1 Bits 3-4 */
  boolean AEMI_ARM:1;					/* Byte 1 Bit 5 */
  boolean AEMI_OFM:1;					/* Byte 1 Bit 6 */
  unsigned char :1;					/* Byte 1 Bit 7 */
  enum {
    DAC960_V1_OEMID_Mylex =			0x00,
    DAC960_V1_OEMID_IBM =			0x08,
    DAC960_V1_OEMID_HP =			0x0A,
    DAC960_V1_OEMID_DEC =			0x0C,
    DAC960_V1_OEMID_Siemens =			0x10,
    DAC960_V1_OEMID_Intel =			0x12
  } __attribute__ ((packed)) OEMID;			/* Byte 2 */
  unsigned char OEMModelNumber;				/* Byte 3 */
  unsigned char PhysicalSector;				/* Byte 4 */
  unsigned char LogicalSector;				/* Byte 5 */
  unsigned char BlockFactor;				/* Byte 6 */
  boolean ReadAheadEnabled:1;				/* Byte 7 Bit 0 */
  boolean LowBIOSDelay:1;				/* Byte 7 Bit 1 */
  unsigned char :2;					/* Byte 7 Bits 2-3 */
  boolean ReassignRestrictedToOneSector:1;		/* Byte 7 Bit 4 */
  unsigned char :1;					/* Byte 7 Bit 5 */
  boolean ForceUnitAccessDuringWriteRecovery:1;		/* Byte 7 Bit 6 */
  boolean EnableLeftSymmetricRAID5Algorithm:1;		/* Byte 7 Bit 7 */
  unsigned char DefaultRebuildRate;			/* Byte 8 */
  unsigned char :8;					/* Byte 9 */
  unsigned char BlocksPerCacheLine;			/* Byte 10 */
  unsigned char BlocksPerStripe;			/* Byte 11 */
  struct {
    enum {
      DAC960_V1_Async =				0x0,
      DAC960_V1_Sync_8MHz =			0x1,
      DAC960_V1_Sync_5MHz =			0x2,
      DAC960_V1_Sync_10or20MHz =		0x3	/* Byte 11 Bits 0-1 */
    } __attribute__ ((packed)) Speed:2;
    boolean Force8Bit:1;				/* Byte 11 Bit 2 */
    boolean DisableFast20:1;				/* Byte 11 Bit 3 */
    unsigned char :3;					/* Byte 11 Bits 4-6 */
    boolean EnableTaggedQueuing:1;			/* Byte 11 Bit 7 */
  } __attribute__ ((packed)) ChannelParameters[6];	/* Bytes 12-17 */
  unsigned char SCSIInitiatorID;			/* Byte 18 */
  unsigned char :8;					/* Byte 19 */
  enum {
    DAC960_V1_StartupMode_ControllerSpinUp =	0x00,
    DAC960_V1_StartupMode_PowerOnSpinUp =	0x01
  } __attribute__ ((packed)) StartupMode;		/* Byte 20 */
  unsigned char SimultaneousDeviceSpinUpCount;		/* Byte 21 */
  unsigned char SecondsDelayBetweenSpinUps;		/* Byte 22 */
  unsigned char Reserved1[29];				/* Bytes 23-51 */
  boolean BIOSDisabled:1;				/* Byte 52 Bit 0 */
  boolean CDROMBootEnabled:1;				/* Byte 52 Bit 1 */
  unsigned char :3;					/* Byte 52 Bits 2-4 */
  enum {
    DAC960_V1_Geometry_128_32 =			0x0,
    DAC960_V1_Geometry_255_63 =			0x1,
    DAC960_V1_Geometry_Reserved1 =		0x2,
    DAC960_V1_Geometry_Reserved2 =		0x3
  } __attribute__ ((packed)) DriveGeometry:2;		/* Byte 52 Bits 5-6 */
  unsigned char :1;					/* Byte 52 Bit 7 */
  unsigned char Reserved2[9];				/* Bytes 53-61 */
  unsigned short Checksum;				/* Bytes 62-63 */
}
DAC960_V1_Config2_T;


/*
  Define the DAC960 V1 Firmware DCDB request structure.
*/

typedef struct DAC960_V1_DCDB
{
  unsigned char TargetID:4;				 /* Byte 0 Bits 0-3 */
  unsigned char Channel:4;				 /* Byte 0 Bits 4-7 */
  enum {
    DAC960_V1_DCDB_NoDataTransfer =		0,
    DAC960_V1_DCDB_DataTransferDeviceToSystem = 1,
    DAC960_V1_DCDB_DataTransferSystemToDevice = 2,
    DAC960_V1_DCDB_IllegalDataTransfer =	3
  } __attribute__ ((packed)) Direction:2;		 /* Byte 1 Bits 0-1 */
  boolean EarlyStatus:1;				 /* Byte 1 Bit 2 */
  unsigned char :1;					 /* Byte 1 Bit 3 */
  enum {
    DAC960_V1_DCDB_Timeout_24_hours =		0,
    DAC960_V1_DCDB_Timeout_10_seconds =		1,
    DAC960_V1_DCDB_Timeout_60_seconds =		2,
    DAC960_V1_DCDB_Timeout_10_minutes =		3
  } __attribute__ ((packed)) Timeout:2;			 /* Byte 1 Bits 4-5 */
  boolean NoAutomaticRequestSense:1;			 /* Byte 1 Bit 6 */
  boolean DisconnectPermitted:1;			 /* Byte 1 Bit 7 */
  unsigned short TransferLength;			 /* Bytes 2-3 */
  DAC960_BusAddress32_T BusAddress;			 /* Bytes 4-7 */
  unsigned char CDBLength:4;				 /* Byte 8 Bits 0-3 */
  unsigned char TransferLengthHigh4:4;			 /* Byte 8 Bits 4-7 */
  unsigned char SenseLength;				 /* Byte 9 */
  unsigned char CDB[12];				 /* Bytes 10-21 */
  unsigned char SenseData[64];				 /* Bytes 22-85 */
  unsigned char Status;					 /* Byte 86 */
  unsigned char :8;					 /* Byte 87 */
}
DAC960_V1_DCDB_T;


/*
  Define the DAC960 V1 Firmware Scatter/Gather List Type 1 32 Bit Address
  32 Bit Byte Count structure.
*/

typedef struct DAC960_V1_ScatterGatherSegment
{
  DAC960_BusAddress32_T SegmentDataPointer;		/* Bytes 0-3 */
  DAC960_ByteCount32_T SegmentByteCount;		/* Bytes 4-7 */
}
DAC960_V1_ScatterGatherSegment_T;


/*
  Define the 13 Byte DAC960 V1 Firmware Command Mailbox structure.  Bytes 13-15
  are not used.  The Command Mailbox structure is padded to 16 bytes for
  efficient access.
*/

typedef union DAC960_V1_CommandMailbox
{
  unsigned int Words[4];				/* Words 0-3 */
  unsigned char Bytes[16];				/* Bytes 0-15 */
  struct {
    DAC960_V1_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_V1_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    unsigned char Dummy[14];				/* Bytes 2-15 */
  } __attribute__ ((packed)) Common;
  struct {
    DAC960_V1_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_V1_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    unsigned char Dummy1[6];				/* Bytes 2-7 */
    DAC960_BusAddress32_T BusAddress;			/* Bytes 8-11 */
    unsigned char Dummy2[4];				/* Bytes 12-15 */
  } __attribute__ ((packed)) Type3;
  struct {
    DAC960_V1_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_V1_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    unsigned char CommandOpcode2;			/* Byte 2 */
    unsigned char Dummy1[5];				/* Bytes 3-7 */
    DAC960_BusAddress32_T BusAddress;			/* Bytes 8-11 */
    unsigned char Dummy2[4];				/* Bytes 12-15 */
  } __attribute__ ((packed)) Type3B;
  struct {
    DAC960_V1_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_V1_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    unsigned char Dummy1[5];				/* Bytes 2-6 */
    unsigned char LogicalDriveNumber:6;			/* Byte 7 Bits 0-6 */
    boolean AutoRestore:1;				/* Byte 7 Bit 7 */
    unsigned char Dummy2[8];				/* Bytes 8-15 */
  } __attribute__ ((packed)) Type3C;
  struct {
    DAC960_V1_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_V1_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    unsigned char Channel;				/* Byte 2 */
    unsigned char TargetID;				/* Byte 3 */
    DAC960_V1_PhysicalDeviceState_T DeviceState:5;	/* Byte 4 Bits 0-4 */
    unsigned char Modifier:3;				/* Byte 4 Bits 5-7 */
    unsigned char Dummy1[3];				/* Bytes 5-7 */
    DAC960_BusAddress32_T BusAddress;			/* Bytes 8-11 */
    unsigned char Dummy2[4];				/* Bytes 12-15 */
  } __attribute__ ((packed)) Type3D;
  struct {
    DAC960_V1_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_V1_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    DAC960_V1_PerformEventLogOpType_T OperationType;	/* Byte 2 */
    unsigned char OperationQualifier;			/* Byte 3 */
    unsigned short SequenceNumber;			/* Bytes 4-5 */
    unsigned char Dummy1[2];				/* Bytes 6-7 */
    DAC960_BusAddress32_T BusAddress;			/* Bytes 8-11 */
    unsigned char Dummy2[4];				/* Bytes 12-15 */
  } __attribute__ ((packed)) Type3E;
  struct {
    DAC960_V1_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_V1_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    unsigned char Dummy1[2];				/* Bytes 2-3 */
    unsigned char RebuildRateConstant;			/* Byte 4 */
    unsigned char Dummy2[3];				/* Bytes 5-7 */
    DAC960_BusAddress32_T BusAddress;			/* Bytes 8-11 */
    unsigned char Dummy3[4];				/* Bytes 12-15 */
  } __attribute__ ((packed)) Type3R;
  struct {
    DAC960_V1_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_V1_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    unsigned short TransferLength;			/* Bytes 2-3 */
    unsigned int LogicalBlockAddress;			/* Bytes 4-7 */
    DAC960_BusAddress32_T BusAddress;			/* Bytes 8-11 */
    unsigned char LogicalDriveNumber;			/* Byte 12 */
    unsigned char Dummy[3];				/* Bytes 13-15 */
  } __attribute__ ((packed)) Type4;
  struct {
    DAC960_V1_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_V1_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    struct {
      unsigned short TransferLength:11;			/* Bytes 2-3 */
      unsigned char LogicalDriveNumber:5;		/* Byte 3 Bits 3-7 */
    } __attribute__ ((packed)) LD;
    unsigned int LogicalBlockAddress;			/* Bytes 4-7 */
    DAC960_BusAddress32_T BusAddress;			/* Bytes 8-11 */
    unsigned char ScatterGatherCount:6;			/* Byte 12 Bits 0-5 */
    enum {
      DAC960_V1_ScatterGather_32BitAddress_32BitByteCount = 0x0,
      DAC960_V1_ScatterGather_32BitAddress_16BitByteCount = 0x1,
      DAC960_V1_ScatterGather_32BitByteCount_32BitAddress = 0x2,
      DAC960_V1_ScatterGather_16BitByteCount_32BitAddress = 0x3
    } __attribute__ ((packed)) ScatterGatherType:2;	/* Byte 12 Bits 6-7 */
    unsigned char Dummy[3];				/* Bytes 13-15 */
  } __attribute__ ((packed)) Type5;
  struct {
    DAC960_V1_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_V1_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    unsigned char CommandOpcode2;			/* Byte 2 */
    unsigned char :8;					/* Byte 3 */
    DAC960_BusAddress32_T CommandMailboxesBusAddress;	/* Bytes 4-7 */
    DAC960_BusAddress32_T StatusMailboxesBusAddress;	/* Bytes 8-11 */
    unsigned char Dummy[4];				/* Bytes 12-15 */
  } __attribute__ ((packed)) TypeX;
}
DAC960_V1_CommandMailbox_T;


/*
  Define the DAC960 V2 Firmware Command Opcodes.
*/

typedef enum
{
  DAC960_V2_MemCopy =				0x01,
  DAC960_V2_SCSI_10_Passthru =			0x02,
  DAC960_V2_SCSI_255_Passthru =			0x03,
  DAC960_V2_SCSI_10 =				0x04,
  DAC960_V2_SCSI_256 =				0x05,
  DAC960_V2_IOCTL =				0x20
}
__attribute__ ((packed))
DAC960_V2_CommandOpcode_T;


/*
  Define the DAC960 V2 Firmware IOCTL Opcodes.
*/

typedef enum
{
  DAC960_V2_GetControllerInfo =			0x01,
  DAC960_V2_GetLogicalDeviceInfoValid =		0x03,
  DAC960_V2_GetPhysicalDeviceInfoValid =	0x05,
  DAC960_V2_GetHealthStatus =			0x11,
  DAC960_V2_GetEvent =				0x15,
  DAC960_V2_StartDiscovery =			0x81,
  DAC960_V2_SetDeviceState =			0x82,
  DAC960_V2_RebuildDeviceStart =		0x88,
  DAC960_V2_RebuildDeviceStop =			0x89,
  DAC960_V2_ConsistencyCheckStart =		0x8C,
  DAC960_V2_ConsistencyCheckStop =		0x8D,
  DAC960_V2_SetMemoryMailbox =			0x8E,
  DAC960_V2_PauseDevice =			0x92,
  DAC960_V2_TranslatePhysicalToLogicalDevice =	0xC5
}
__attribute__ ((packed))
DAC960_V2_IOCTL_Opcode_T;


/*
  Define the DAC960 V2 Firmware Command Identifier type.
*/

typedef unsigned short DAC960_V2_CommandIdentifier_T;


/*
  Define the DAC960 V2 Firmware Command Status Codes.
*/

#define DAC960_V2_NormalCompletion		0x00
#define DAC960_V2_AbormalCompletion		0x02
#define DAC960_V2_DeviceBusy			0x08
#define DAC960_V2_DeviceNonresponsive		0x0E
#define DAC960_V2_DeviceNonresponsive2		0x0F
#define DAC960_V2_DeviceRevervationConflict	0x18

typedef unsigned char DAC960_V2_CommandStatus_T;


/*
  Define the DAC960 V2 Firmware Memory Type structure.
*/

typedef struct DAC960_V2_MemoryType
{
  enum {
    DAC960_V2_MemoryType_Reserved =		0x00,
    DAC960_V2_MemoryType_DRAM =			0x01,
    DAC960_V2_MemoryType_EDRAM =		0x02,
    DAC960_V2_MemoryType_EDO =			0x03,
    DAC960_V2_MemoryType_SDRAM =		0x04,
    DAC960_V2_MemoryType_Last =			0x1F
  } __attribute__ ((packed)) MemoryType:5;		/* Byte 0 Bits 0-4 */
  boolean :1;						/* Byte 0 Bit 5 */
  boolean MemoryParity:1;				/* Byte 0 Bit 6 */
  boolean MemoryECC:1;					/* Byte 0 Bit 7 */
}
DAC960_V2_MemoryType_T;


/*
  Define the DAC960 V2 Firmware Processor Type structure.
*/

typedef enum
{
  DAC960_V2_ProcessorType_i960CA =		0x01,
  DAC960_V2_ProcessorType_i960RD =		0x02,
  DAC960_V2_ProcessorType_i960RN =		0x03,
  DAC960_V2_ProcessorType_i960RP =		0x04,
  DAC960_V2_ProcessorType_NorthBay =		0x05,
  DAC960_V2_ProcessorType_StrongArm =		0x06,
  DAC960_V2_ProcessorType_i960RM =		0x07
}
__attribute__ ((packed))
DAC960_V2_ProcessorType_T;


/*
  Define the DAC960 V2 Firmware Get Controller Info reply structure.
*/

typedef struct DAC960_V2_ControllerInfo
{
  unsigned char :8;					/* Byte 0 */
  enum {
    DAC960_V2_SCSI_Bus =			0x00,
    DAC960_V2_Fibre_Bus =			0x01,
    DAC960_V2_PCI_Bus =				0x03
  } __attribute__ ((packed)) BusInterfaceType;		/* Byte 1 */
  enum {
    DAC960_V2_DAC960E =				0x01,
    DAC960_V2_DAC960M =				0x08,
    DAC960_V2_DAC960PD =			0x10,
    DAC960_V2_DAC960PL =			0x11,
    DAC960_V2_DAC960PU =			0x12,
    DAC960_V2_DAC960PE =			0x13,
    DAC960_V2_DAC960PG =			0x14,
    DAC960_V2_DAC960PJ =			0x15,
    DAC960_V2_DAC960PTL0 =			0x16,
    DAC960_V2_DAC960PR =			0x17,
    DAC960_V2_DAC960PRL =			0x18,
    DAC960_V2_DAC960PT =			0x19,
    DAC960_V2_DAC1164P =			0x1A,
    DAC960_V2_DAC960PTL1 =			0x1B,
    DAC960_V2_EXR2000P =			0x1C,
    DAC960_V2_EXR3000P =			0x1D,
    DAC960_V2_AcceleRAID352 =			0x1E,
    DAC960_V2_AcceleRAID170 =			0x1F,
    DAC960_V2_AcceleRAID160 =			0x20,
    DAC960_V2_DAC960S =				0x60,
    DAC960_V2_DAC960SU =			0x61,
    DAC960_V2_DAC960SX =			0x62,
    DAC960_V2_DAC960SF =			0x63,
    DAC960_V2_DAC960SS =			0x64,
    DAC960_V2_DAC960FL =			0x65,
    DAC960_V2_DAC960LL =			0x66,
    DAC960_V2_DAC960FF =			0x67,
    DAC960_V2_DAC960HP =			0x68,
    DAC960_V2_RAIDBRICK =			0x69,
    DAC960_V2_METEOR_FL =			0x6A,
    DAC960_V2_METEOR_FF =			0x6B
  } __attribute__ ((packed)) ControllerType;		/* Byte 2 */
  unsigned char :8;					/* Byte 3 */
  unsigned short BusInterfaceSpeedMHz;			/* Bytes 4-5 */
  unsigned char BusWidthBits;				/* Byte 6 */
  unsigned char FlashCodeTypeOrProductID;		/* Byte 7 */
  unsigned char NumberOfHostPortsPresent;		/* Byte 8 */
  unsigned char Reserved1[7];				/* Bytes 9-15 */
  unsigned char BusInterfaceName[16];			/* Bytes 16-31 */
  unsigned char ControllerName[16];			/* Bytes 32-47 */
  unsigned char Reserved2[16];				/* Bytes 48-63 */
  /* Firmware Release Information */
  unsigned char FirmwareMajorVersion;			/* Byte 64 */
  unsigned char FirmwareMinorVersion;			/* Byte 65 */
  unsigned char FirmwareTurnNumber;			/* Byte 66 */
  unsigned char FirmwareBuildNumber;			/* Byte 67 */
  unsigned char FirmwareReleaseDay;			/* Byte 68 */
  unsigned char FirmwareReleaseMonth;			/* Byte 69 */
  unsigned char FirmwareReleaseYearHigh2Digits;		/* Byte 70 */
  unsigned char FirmwareReleaseYearLow2Digits;		/* Byte 71 */
  /* Hardware Release Information */
  unsigned char HardwareRevision;			/* Byte 72 */
  unsigned int :24;					/* Bytes 73-75 */
  unsigned char HardwareReleaseDay;			/* Byte 76 */
  unsigned char HardwareReleaseMonth;			/* Byte 77 */
  unsigned char HardwareReleaseYearHigh2Digits;		/* Byte 78 */
  unsigned char HardwareReleaseYearLow2Digits;		/* Byte 79 */
  /* Hardware Manufacturing Information */
  unsigned char ManufacturingBatchNumber;		/* Byte 80 */
  unsigned char :8;					/* Byte 81 */
  unsigned char ManufacturingPlantNumber;		/* Byte 82 */
  unsigned char :8;					/* Byte 83 */
  unsigned char HardwareManufacturingDay;		/* Byte 84 */
  unsigned char HardwareManufacturingMonth;		/* Byte 85 */
  unsigned char HardwareManufacturingYearHigh2Digits;	/* Byte 86 */
  unsigned char HardwareManufacturingYearLow2Digits;	/* Byte 87 */
  unsigned char MaximumNumberOfPDDperXLD;		/* Byte 88 */
  unsigned char MaximumNumberOfILDperXLD;		/* Byte 89 */
  unsigned short NonvolatileMemorySizeKB;		/* Bytes 90-91 */
  unsigned char MaximumNumberOfXLD;			/* Byte 92 */
  unsigned int :24;					/* Bytes 93-95 */
  /* Unique Information per Controller */
  unsigned char ControllerSerialNumber[16];		/* Bytes 96-111 */
  unsigned char Reserved3[16];				/* Bytes 112-127 */
  /* Vendor Information */
  unsigned int :24;					/* Bytes 128-130 */
  unsigned char OEM_Code;				/* Byte 131 */
  unsigned char VendorName[16];				/* Bytes 132-147 */
  /* Other Physical/Controller/Operation Information */
  boolean BBU_Present:1;				/* Byte 148 Bit 0 */
  boolean ActiveActiveClusteringMode:1;			/* Byte 148 Bit 1 */
  unsigned char :6;					/* Byte 148 Bits 2-7 */
  unsigned char :8;					/* Byte 149 */
  unsigned short :16;					/* Bytes 150-151 */
  /* Physical Device Scan Information */
  boolean PhysicalScanActive:1;				/* Byte 152 Bit 0 */
  unsigned char :7;					/* Byte 152 Bits 1-7 */
  unsigned char PhysicalDeviceChannelNumber;		/* Byte 153 */
  unsigned char PhysicalDeviceTargetID;			/* Byte 154 */
  unsigned char PhysicalDeviceLogicalUnit;		/* Byte 155 */
  /* Maximum Command Data Transfer Sizes */
  unsigned short MaximumDataTransferSizeInBlocks;	/* Bytes 156-157 */
  unsigned short MaximumScatterGatherEntries;		/* Bytes 158-159 */
  /* Logical/Physical Device Counts */
  unsigned short LogicalDevicesPresent;			/* Bytes 160-161 */
  unsigned short LogicalDevicesCritical;		/* Bytes 162-163 */
  unsigned short LogicalDevicesOffline;			/* Bytes 164-165 */
  unsigned short PhysicalDevicesPresent;		/* Bytes 166-167 */
  unsigned short PhysicalDisksPresent;			/* Bytes 168-169 */
  unsigned short PhysicalDisksCritical;			/* Bytes 170-171 */
  unsigned short PhysicalDisksOffline;			/* Bytes 172-173 */
  unsigned short MaximumParallelCommands;		/* Bytes 174-175 */
  /* Channel and Target ID Information */
  unsigned char NumberOfPhysicalChannelsPresent;	/* Byte 176 */
  unsigned char NumberOfVirtualChannelsPresent;		/* Byte 177 */
  unsigned char NumberOfPhysicalChannelsPossible;	/* Byte 178 */
  unsigned char NumberOfVirtualChannelsPossible;	/* Byte 179 */
  unsigned char MaximumTargetsPerChannel[16];		/* Bytes 180-195 */
  unsigned char Reserved4[12];				/* Bytes 196-207 */
  /* Memory/Cache Information */
  unsigned short MemorySizeMB;				/* Bytes 208-209 */
  unsigned short CacheSizeMB;				/* Bytes 210-211 */
  unsigned int ValidCacheSizeInBytes;			/* Bytes 212-215 */
  unsigned int DirtyCacheSizeInBytes;			/* Bytes 216-219 */
  unsigned short MemorySpeedMHz;			/* Bytes 220-221 */
  unsigned char MemoryDataWidthBits;			/* Byte 222 */
  DAC960_V2_MemoryType_T MemoryType;			/* Byte 223 */
  unsigned char CacheMemoryTypeName[16];		/* Bytes 224-239 */
  /* Execution Memory Information */
  unsigned short ExecutionMemorySizeMB;			/* Bytes 240-241 */
  unsigned short ExecutionL2CacheSizeMB;		/* Bytes 242-243 */
  unsigned char Reserved5[8];				/* Bytes 244-251 */
  unsigned short ExecutionMemorySpeedMHz;		/* Bytes 252-253 */
  unsigned char ExecutionMemoryDataWidthBits;		/* Byte 254 */
  DAC960_V2_MemoryType_T ExecutionMemoryType;		/* Byte 255 */
  unsigned char ExecutionMemoryTypeName[16];		/* Bytes 256-271 */
  /* First CPU Type Information */
  unsigned short FirstProcessorSpeedMHz;		/* Bytes 272-273 */
  DAC960_V2_ProcessorType_T FirstProcessorType;		/* Byte 274 */
  unsigned char FirstProcessorCount;			/* Byte 275 */
  unsigned char Reserved6[12];				/* Bytes 276-287 */
  unsigned char FirstProcessorName[16];			/* Bytes 288-303 */
  /* Second CPU Type Information */
  unsigned short SecondProcessorSpeedMHz;		/* Bytes 304-305 */
  DAC960_V2_ProcessorType_T SecondProcessorType;	/* Byte 306 */
  unsigned char SecondProcessorCount;			/* Byte 307 */
  unsigned char Reserved7[12];				/* Bytes 308-319 */
  unsigned char SecondProcessorName[16];		/* Bytes 320-335 */
  /* Debugging/Profiling/Command Time Tracing Information */
  unsigned short CurrentProfilingDataPageNumber;	/* Bytes 336-337 */
  unsigned short ProgramsAwaitingProfilingData;		/* Bytes 338-339 */
  unsigned short CurrentCommandTimeTraceDataPageNumber;	/* Bytes 340-341 */
  unsigned short ProgramsAwaitingCommandTimeTraceData;	/* Bytes 342-343 */
  unsigned char Reserved8[8];				/* Bytes 344-351 */
  /* Error Counters on Physical Devices */
  unsigned short PhysicalDeviceBusResets;		/* Bytes 352-353 */
  unsigned short PhysicalDeviceParityErrors;		/* Bytes 355-355 */
  unsigned short PhysicalDeviceSoftErrors;		/* Bytes 356-357 */
  unsigned short PhysicalDeviceCommandsFailed;		/* Bytes 358-359 */
  unsigned short PhysicalDeviceMiscellaneousErrors;	/* Bytes 360-361 */
  unsigned short PhysicalDeviceCommandTimeouts;		/* Bytes 362-363 */
  unsigned short PhysicalDeviceSelectionTimeouts;	/* Bytes 364-365 */
  unsigned short PhysicalDeviceRetriesDone;		/* Bytes 366-367 */
  unsigned short PhysicalDeviceAbortsDone;		/* Bytes 368-369 */
  unsigned short PhysicalDeviceHostCommandAbortsDone;	/* Bytes 370-371 */
  unsigned short PhysicalDevicePredictedFailuresDetected; /* Bytes 372-373 */
  unsigned short PhysicalDeviceHostCommandsFailed;	/* Bytes 374-375 */
  unsigned short PhysicalDeviceHardErrors;		/* Bytes 376-377 */
  unsigned char Reserved9[6];				/* Bytes 378-383 */
  /* Error Counters on Logical Devices */
  unsigned short LogicalDeviceSoftErrors;		/* Bytes 384-385 */
  unsigned short LogicalDeviceCommandsFailed;		/* Bytes 386-387 */
  unsigned short LogicalDeviceHostCommandAbortsDone;	/* Bytes 388-389 */
  unsigned short :16;					/* Bytes 390-391 */
  /* Error Counters on Controller */
  unsigned short ControllerMemoryErrors;		/* Bytes 392-393 */
  unsigned short ControllerHostCommandAbortsDone;	/* Bytes 394-395 */
  unsigned int :32;					/* Bytes 396-399 */
  /* Long Duration Activity Information */
  unsigned short BackgroundInitializationsActive;	/* Bytes 400-401 */
  unsigned short LogicalDeviceInitializationsActive;	/* Bytes 402-403 */
  unsigned short PhysicalDeviceInitializationsActive;	/* Bytes 404-405 */
  unsigned short ConsistencyChecksActive;		/* Bytes 406-407 */
  unsigned short RebuildsActive;			/* Bytes 408-409 */
  unsigned short OnlineExpansionsActive;		/* Bytes 410-411 */
  unsigned short PatrolActivitiesActive;		/* Bytes 412-413 */
  unsigned short :16;					/* Bytes 414-415 */
  /* Flash ROM Information */
  unsigned char FlashType;				/* Byte 416 */
  unsigned char :8;					/* Byte 417 */
  unsigned short FlashSizeMB;				/* Bytes 418-419 */
  unsigned int FlashLimit;				/* Bytes 420-423 */
  unsigned int FlashCount;				/* Bytes 424-427 */
  unsigned int :32;					/* Bytes 428-431 */
  unsigned char FlashTypeName[16];			/* Bytes 432-447 */
  /* Firmware Run Time Information */
  unsigned char RebuildRate;				/* Byte 448 */
  unsigned char BackgroundInitializationRate;		/* Byte 449 */
  unsigned char ForegroundInitializationRate;		/* Byte 450 */
  unsigned char ConsistencyCheckRate;			/* Byte 451 */
  unsigned int :32;					/* Bytes 452-455 */
  unsigned int MaximumDP;				/* Bytes 456-459 */
  unsigned int FreeDP;					/* Bytes 460-463 */
  unsigned int MaximumIOP;				/* Bytes 464-467 */
  unsigned int FreeIOP;					/* Bytes 468-471 */
  unsigned short MaximumCombLengthInBlocks;		/* Bytes 472-473 */
  unsigned short NumberOfConfigurationGroups;		/* Bytes 474-475 */
  boolean InstallationAbortStatus:1;			/* Byte 476 Bit 0 */
  boolean MaintenanceModeStatus:1;			/* Byte 476 Bit 1 */
  unsigned int :24;					/* Bytes 476-479 */
  unsigned char Reserved10[32];				/* Bytes 480-511 */
  unsigned char Reserved11[512];			/* Bytes 512-1023 */
}
DAC960_V2_ControllerInfo_T;


/*
  Define the DAC960 V2 Firmware Logical Device State type.
*/

typedef enum
{
  DAC960_V2_LogicalDevice_Online =		0x01,
  DAC960_V2_LogicalDevice_Offline =		0x08,
  DAC960_V2_LogicalDevice_Critical =		0x09
}
__attribute__ ((packed))
DAC960_V2_LogicalDeviceState_T;


/*
  Define the DAC960 V2 Firmware Get Logical Device Info reply structure.
*/

typedef struct DAC960_V2_LogicalDeviceInfo
{
  unsigned char :8;					/* Byte 0 */
  unsigned char Channel;				/* Byte 1 */
  unsigned char TargetID;				/* Byte 2 */
  unsigned char LogicalUnit;				/* Byte 3 */
  DAC960_V2_LogicalDeviceState_T LogicalDeviceState;	/* Byte 4 */
  unsigned char RAIDLevel;				/* Byte 5 */
  unsigned char StripeSize;				/* Byte 6 */
  unsigned char CacheLineSize;				/* Byte 7 */
  struct {
    enum {
      DAC960_V2_ReadCacheDisabled =		0x0,
      DAC960_V2_ReadCacheEnabled =		0x1,
      DAC960_V2_ReadAheadEnabled =		0x2,
      DAC960_V2_IntelligentReadAheadEnabled =	0x3,
      DAC960_V2_ReadCache_Last =		0x7
    } __attribute__ ((packed)) ReadCache:3;		/* Byte 8 Bits 0-2 */
    enum {
      DAC960_V2_WriteCacheDisabled =		0x0,
      DAC960_V2_LogicalDeviceReadOnly =		0x1,
      DAC960_V2_WriteCacheEnabled =		0x2,
      DAC960_V2_IntelligentWriteCacheEnabled =	0x3,
      DAC960_V2_WriteCache_Last =		0x7
    } __attribute__ ((packed)) WriteCache:3;		/* Byte 8 Bits 3-5 */
    boolean :1;						/* Byte 8 Bit 6 */
    boolean LogicalDeviceInitialized:1;			/* Byte 8 Bit 7 */
  } LogicalDeviceControl;				/* Byte 8 */
  /* Logical Device Operations Status */
  boolean ConsistencyCheckInProgress:1;			/* Byte 9 Bit 0 */
  boolean RebuildInProgress:1;				/* Byte 9 Bit 1 */
  boolean BackgroundInitializationInProgress:1;		/* Byte 9 Bit 2 */
  boolean ForegroundInitializationInProgress:1;		/* Byte 9 Bit 3 */
  boolean DataMigrationInProgress:1;			/* Byte 9 Bit 4 */
  boolean PatrolOperationInProgress:1;			/* Byte 9 Bit 5 */
  unsigned char :2;					/* Byte 9 Bits 6-7 */
  unsigned char RAID5WriteUpdate;			/* Byte 10 */
  unsigned char RAID5Algorithm;				/* Byte 11 */
  unsigned short LogicalDeviceNumber;			/* Bytes 12-13 */
  /* BIOS Info */
  boolean BIOSDisabled:1;				/* Byte 14 Bit 0 */
  boolean CDROMBootEnabled:1;				/* Byte 14 Bit 1 */
  boolean DriveCoercionEnabled:1;			/* Byte 14 Bit 2 */
  boolean WriteSameDisabled:1;				/* Byte 14 Bit 3 */
  boolean HBA_ModeEnabled:1;				/* Byte 14 Bit 4 */
  enum {
    DAC960_V2_Geometry_128_32 =			0x0,
    DAC960_V2_Geometry_255_63 =			0x1,
    DAC960_V2_Geometry_Reserved1 =		0x2,
    DAC960_V2_Geometry_Reserved2 =		0x3
  } __attribute__ ((packed)) DriveGeometry:2;		/* Byte 14 Bits 5-6 */
  boolean SuperReadAheadEnabled:1;			/* Byte 14 Bit 7 */
  unsigned char :8;					/* Byte 15 */
  /* Error Counters */
  unsigned short SoftErrors;				/* Bytes 16-17 */
  unsigned short CommandsFailed;			/* Bytes 18-19 */
  unsigned short HostCommandAbortsDone;			/* Bytes 20-21 */
  unsigned short DeferredWriteErrors;			/* Bytes 22-23 */
  unsigned int :32;					/* Bytes 24-27 */
  unsigned int :32;					/* Bytes 28-31 */
  /* Device Size Information */
  unsigned short :16;					/* Bytes 32-33 */
  unsigned short DeviceBlockSizeInBytes;		/* Bytes 34-35 */
  unsigned int OriginalDeviceSize;			/* Bytes 36-39 */
  unsigned int ConfigurableDeviceSize;			/* Bytes 40-43 */
  unsigned int :32;					/* Bytes 44-47 */
  unsigned char LogicalDeviceName[32];			/* Bytes 48-79 */
  unsigned char SCSI_InquiryData[36];			/* Bytes 80-115 */
  unsigned char Reserved1[12];				/* Bytes 116-127 */
  DAC960_ByteCount64_T LastReadBlockNumber;		/* Bytes 128-135 */
  DAC960_ByteCount64_T LastWrittenBlockNumber;		/* Bytes 136-143 */
  DAC960_ByteCount64_T ConsistencyCheckBlockNumber;	/* Bytes 144-151 */
  DAC960_ByteCount64_T RebuildBlockNumber;		/* Bytes 152-159 */
  DAC960_ByteCount64_T BackgroundInitializationBlockNumber; /* Bytes 160-167 */
  DAC960_ByteCount64_T ForegroundInitializationBlockNumber; /* Bytes 168-175 */
  DAC960_ByteCount64_T DataMigrationBlockNumber;	/* Bytes 176-183 */
  DAC960_ByteCount64_T PatrolOperationBlockNumber;	/* Bytes 184-191 */
  unsigned char Reserved2[64];				/* Bytes 192-255 */
}
DAC960_V2_LogicalDeviceInfo_T;


/*
  Define the DAC960 V2 Firmware Physical Device State type.
*/

typedef enum
{
    DAC960_V2_Device_Unconfigured =		0x00,
    DAC960_V2_Device_Online =			0x01,
    DAC960_V2_Device_Rebuild =			0x03,
    DAC960_V2_Device_Missing =			0x04,
    DAC960_V2_Device_Critical =			0x05,
    DAC960_V2_Device_Dead =			0x08,
    DAC960_V2_Device_SuspectedDead =		0x0C,
    DAC960_V2_Device_CommandedOffline =		0x10,
    DAC960_V2_Device_Standby =			0x21,
    DAC960_V2_Device_InvalidState =		0xFF
}
__attribute__ ((packed))
DAC960_V2_PhysicalDeviceState_T;


/*
  Define the DAC960 V2 Firmware Get Physical Device Info reply structure.
*/

typedef struct DAC960_V2_PhysicalDeviceInfo
{
  unsigned char :8;					/* Byte 0 */
  unsigned char Channel;				/* Byte 1 */
  unsigned char TargetID;				/* Byte 2 */
  unsigned char LogicalUnit;				/* Byte 3 */
  /* Configuration Status Bits */
  boolean PhysicalDeviceFaultTolerant:1;		/* Byte 4 Bit 0 */
  boolean PhysicalDeviceConnected:1;			/* Byte 4 Bit 1 */
  boolean PhysicalDeviceLocalToController:1;		/* Byte 4 Bit 2 */
  unsigned char :5;					/* Byte 4 Bits 3-7 */
  /* Multiple Host/Controller Status Bits */
  boolean RemoteHostSystemDead:1;			/* Byte 5 Bit 0 */
  boolean RemoteControllerDead:1;			/* Byte 5 Bit 1 */
  unsigned char :6;					/* Byte 5 Bits 2-7 */
  DAC960_V2_PhysicalDeviceState_T PhysicalDeviceState;	/* Byte 6 */
  unsigned char NegotiatedDataWidthBits;		/* Byte 7 */
  unsigned short NegotiatedSynchronousMegaTransfers;	/* Bytes 8-9 */
  /* Multiported Physical Device Information */
  unsigned char NumberOfPortConnections;		/* Byte 10 */
  unsigned char DriveAccessibilityBitmap;		/* Byte 11 */
  unsigned int :32;					/* Bytes 12-15 */
  unsigned char NetworkAddress[16];			/* Bytes 16-31 */
  unsigned short MaximumTags;				/* Bytes 32-33 */
  /* Physical Device Operations Status */
  boolean ConsistencyCheckInProgress:1;			/* Byte 34 Bit 0 */
  boolean RebuildInProgress:1;				/* Byte 34 Bit 1 */
  boolean MakingDataConsistentInProgress:1;		/* Byte 34 Bit 2 */
  boolean PhysicalDeviceInitializationInProgress:1;	/* Byte 34 Bit 3 */
  boolean DataMigrationInProgress:1;			/* Byte 34 Bit 4 */
  boolean PatrolOperationInProgress:1;			/* Byte 34 Bit 5 */
  unsigned char :2;					/* Byte 34 Bits 6-7 */
  unsigned char LongOperationStatus;			/* Byte 35 */
  unsigned char ParityErrors;				/* Byte 36 */
  unsigned char SoftErrors;				/* Byte 37 */
  unsigned char HardErrors;				/* Byte 38 */
  unsigned char MiscellaneousErrors;			/* Byte 39 */
  unsigned char CommandTimeouts;			/* Byte 40 */
  unsigned char Retries;				/* Byte 41 */
  unsigned char Aborts;					/* Byte 42 */
  unsigned char PredictedFailuresDetected;		/* Byte 43 */
  unsigned int :32;					/* Bytes 44-47 */
  unsigned short :16;					/* Bytes 48-49 */
  unsigned short DeviceBlockSizeInBytes;		/* Bytes 50-51 */
  unsigned int OriginalDeviceSize;			/* Bytes 52-55 */
  unsigned int ConfigurableDeviceSize;			/* Bytes 56-59 */
  unsigned int :32;					/* Bytes 60-63 */
  unsigned char PhysicalDeviceName[16];			/* Bytes 64-79 */
  unsigned char Reserved1[16];				/* Bytes 80-95 */
  unsigned char Reserved2[32];				/* Bytes 96-127 */
  unsigned char SCSI_InquiryData[36];			/* Bytes 128-163 */
  unsigned char Reserved3[20];				/* Bytes 164-183 */
  unsigned char Reserved4[8];				/* Bytes 184-191 */
  DAC960_ByteCount64_T LastReadBlockNumber;		/* Bytes 192-199 */
  DAC960_ByteCount64_T LastWrittenBlockNumber;		/* Bytes 200-207 */
  DAC960_ByteCount64_T ConsistencyCheckBlockNumber;	/* Bytes 208-215 */
  DAC960_ByteCount64_T RebuildBlockNumber;		/* Bytes 216-223 */
  DAC960_ByteCount64_T MakingDataConsistentBlockNumber;	/* Bytes 224-231 */
  DAC960_ByteCount64_T DeviceInitializationBlockNumber; /* Bytes 232-239 */
  DAC960_ByteCount64_T DataMigrationBlockNumber;	/* Bytes 240-247 */
  DAC960_ByteCount64_T PatrolOperationBlockNumber;	/* Bytes 248-255 */
  unsigned char Reserved5[256];				/* Bytes 256-511 */
}
DAC960_V2_PhysicalDeviceInfo_T;


/*
  Define the DAC960 V2 Firmware Health Status Buffer structure.
*/

typedef struct DAC960_V2_HealthStatusBuffer
{
  unsigned int MicrosecondsFromControllerStartTime;	/* Bytes 0-3 */
  unsigned int MillisecondsFromControllerStartTime;	/* Bytes 4-7 */
  unsigned int SecondsFrom1January1970;			/* Bytes 8-11 */
  unsigned int :32;					/* Bytes 12-15 */
  unsigned int StatusChangeCounter;			/* Bytes 16-19 */
  unsigned int :32;					/* Bytes 20-23 */
  unsigned int DebugOutputMessageBufferIndex;		/* Bytes 24-27 */
  unsigned int CodedMessageBufferIndex;			/* Bytes 28-31 */
  unsigned int CurrentTimeTracePageNumber;		/* Bytes 32-35 */
  unsigned int CurrentProfilerPageNumber;		/* Bytes 36-39 */
  unsigned int NextEventSequenceNumber;			/* Bytes 40-43 */
  unsigned int :32;					/* Bytes 44-47 */
  unsigned char Reserved1[16];				/* Bytes 48-63 */
  unsigned char Reserved2[64];				/* Bytes 64-127 */
}
DAC960_V2_HealthStatusBuffer_T;


/*
  Define the DAC960 V2 Firmware Get Event reply structure.
*/

typedef struct DAC960_V2_Event
{
  unsigned int EventSequenceNumber;			/* Bytes 0-3 */
  unsigned int EventTime;				/* Bytes 4-7 */
  unsigned int EventCode;				/* Bytes 8-11 */
  unsigned char :8;					/* Byte 12 */
  unsigned char Channel;				/* Byte 13 */
  unsigned char TargetID;				/* Byte 14 */
  unsigned char LogicalUnit;				/* Byte 15 */
  unsigned int :32;					/* Bytes 16-19 */
  unsigned int EventSpecificParameter;			/* Bytes 20-23 */
  unsigned char RequestSenseData[40];			/* Bytes 24-63 */
}
DAC960_V2_Event_T;


/*
  Define the DAC960 V2 Firmware Command Control Bits structure.
*/

typedef struct DAC960_V2_CommandControlBits
{
  boolean ForceUnitAccess:1;				/* Byte 0 Bit 0 */
  boolean DisablePageOut:1;				/* Byte 0 Bit 1 */
  boolean :1;						/* Byte 0 Bit 2 */
  boolean AdditionalScatterGatherListMemory:1;		/* Byte 0 Bit 3 */
  boolean DataTransferControllerToHost:1;		/* Byte 0 Bit 4 */
  boolean :1;						/* Byte 0 Bit 5 */
  boolean NoAutoRequestSense:1;				/* Byte 0 Bit 6 */
  boolean DisconnectProhibited:1;			/* Byte 0 Bit 7 */
}
DAC960_V2_CommandControlBits_T;


/*
  Define the DAC960 V2 Firmware Command Timeout structure.
*/

typedef struct DAC960_V2_CommandTimeout
{
  unsigned char TimeoutValue:6;				/* Byte 0 Bits 0-5 */
  enum {
    DAC960_V2_TimeoutScale_Seconds =		0,
    DAC960_V2_TimeoutScale_Minutes =		1,
    DAC960_V2_TimeoutScale_Hours =		2,
    DAC960_V2_TimeoutScale_Reserved =		3
  } __attribute__ ((packed)) TimeoutScale:2;		/* Byte 0 Bits 6-7 */
}
DAC960_V2_CommandTimeout_T;


/*
  Define the DAC960 V2 Firmware Physical Device structure.
*/

typedef struct DAC960_V2_PhysicalDevice
{
  unsigned char LogicalUnit;				/* Byte 0 */
  unsigned char TargetID;				/* Byte 1 */
  unsigned char Channel:3;				/* Byte 2 Bits 0-2 */
  unsigned char Controller:5;				/* Byte 2 Bits 3-7 */
}
__attribute__ ((packed))
DAC960_V2_PhysicalDevice_T;


/*
  Define the DAC960 V2 Firmware Logical Device structure.
*/

typedef struct DAC960_V2_LogicalDevice
{
  unsigned short LogicalDeviceNumber;			/* Bytes 0-1 */
  unsigned char :3;					/* Byte 2 Bits 0-2 */
  unsigned char Controller:5;				/* Byte 2 Bits 3-7 */
}
__attribute__ ((packed))
DAC960_V2_LogicalDevice_T;


/*
  Define the DAC960 V2 Firmware Operation Device type.
*/

typedef enum
{
  DAC960_V2_Physical_Device =			0x00,
  DAC960_V2_RAID_Device =			0x01,
  DAC960_V2_Physical_Channel =			0x02,
  DAC960_V2_RAID_Channel =			0x03,
  DAC960_V2_Physical_Controller =		0x04,
  DAC960_V2_RAID_Controller =			0x05,
  DAC960_V2_Configuration_Group =		0x10,
  DAC960_V2_Enclosure =				0x11
}
__attribute__ ((packed))
DAC960_V2_OperationDevice_T;


/*
  Define the DAC960 V2 Firmware Translate Physical To Logical Device structure.
*/

typedef struct DAC960_V2_PhysicalToLogicalDevice
{
  unsigned short LogicalDeviceNumber;			/* Bytes 0-1 */
  unsigned short :16;					/* Bytes 2-3 */
  unsigned char PreviousBootController;			/* Byte 4 */
  unsigned char PreviousBootChannel;			/* Byte 5 */
  unsigned char PreviousBootTargetID;			/* Byte 6 */
  unsigned char PreviousBootLogicalUnit;		/* Byte 7 */
}
DAC960_V2_PhysicalToLogicalDevice_T;



/*
  Define the DAC960 V2 Firmware Scatter/Gather List Entry structure.
*/

typedef struct DAC960_V2_ScatterGatherSegment
{
  DAC960_BusAddress64_T SegmentDataPointer;		/* Bytes 0-7 */
  DAC960_ByteCount64_T SegmentByteCount;		/* Bytes 8-15 */
}
DAC960_V2_ScatterGatherSegment_T;


/*
  Define the DAC960 V2 Firmware Data Transfer Memory Address structure.
*/

typedef union DAC960_V2_DataTransferMemoryAddress
{
  DAC960_V2_ScatterGatherSegment_T ScatterGatherSegments[2]; /* Bytes 0-31 */
  struct {
    unsigned short ScatterGatherList0Length;		/* Bytes 0-1 */
    unsigned short ScatterGatherList1Length;		/* Bytes 2-3 */
    unsigned short ScatterGatherList2Length;		/* Bytes 4-5 */
    unsigned short :16;					/* Bytes 6-7 */
    DAC960_BusAddress64_T ScatterGatherList0Address;	/* Bytes 8-15 */
    DAC960_BusAddress64_T ScatterGatherList1Address;	/* Bytes 16-23 */
    DAC960_BusAddress64_T ScatterGatherList2Address;	/* Bytes 24-31 */
  } ExtendedScatterGather;
}
DAC960_V2_DataTransferMemoryAddress_T;


/*
  Define the 64 Byte DAC960 V2 Firmware Command Mailbox structure.
*/

typedef union DAC960_V2_CommandMailbox
{
  unsigned int Words[16];				/* Words 0-15 */
  struct {
    DAC960_V2_CommandIdentifier_T CommandIdentifier;	/* Bytes 0-1 */
    DAC960_V2_CommandOpcode_T CommandOpcode;		/* Byte 2 */
    DAC960_V2_CommandControlBits_T CommandControlBits;	/* Byte 3 */
    DAC960_ByteCount32_T DataTransferSize:24;		/* Bytes 4-6 */
    unsigned char DataTransferPageNumber;		/* Byte 7 */
    DAC960_BusAddress64_T RequestSenseBusAddress;	/* Bytes 8-15 */
    unsigned int :24;					/* Bytes 16-18 */
    DAC960_V2_CommandTimeout_T CommandTimeout;		/* Byte 19 */
    unsigned char RequestSenseSize;			/* Byte 20 */
    unsigned char IOCTL_Opcode;				/* Byte 21 */
    unsigned char Reserved[10];				/* Bytes 22-31 */
    DAC960_V2_DataTransferMemoryAddress_T
      DataTransferMemoryAddress;			/* Bytes 32-63 */
  } Common;
  struct {
    DAC960_V2_CommandIdentifier_T CommandIdentifier;	/* Bytes 0-1 */
    DAC960_V2_CommandOpcode_T CommandOpcode;		/* Byte 2 */
    DAC960_V2_CommandControlBits_T CommandControlBits;	/* Byte 3 */
    DAC960_ByteCount32_T DataTransferSize;		/* Bytes 4-7 */
    DAC960_BusAddress64_T RequestSenseBusAddress;	/* Bytes 8-15 */
    DAC960_V2_PhysicalDevice_T PhysicalDevice;		/* Bytes 16-18 */
    DAC960_V2_CommandTimeout_T CommandTimeout;		/* Byte 19 */
    unsigned char RequestSenseSize;			/* Byte 20 */
    unsigned char CDBLength;				/* Byte 21 */
    unsigned char SCSI_CDB[10];				/* Bytes 22-31 */
    DAC960_V2_DataTransferMemoryAddress_T
      DataTransferMemoryAddress;			/* Bytes 32-63 */
  } SCSI_10;
  struct {
    DAC960_V2_CommandIdentifier_T CommandIdentifier;	/* Bytes 0-1 */
    DAC960_V2_CommandOpcode_T CommandOpcode;		/* Byte 2 */
    DAC960_V2_CommandControlBits_T CommandControlBits;	/* Byte 3 */
    DAC960_ByteCount32_T DataTransferSize;		/* Bytes 4-7 */
    DAC960_BusAddress64_T RequestSenseBusAddress;	/* Bytes 8-15 */
    DAC960_V2_PhysicalDevice_T PhysicalDevice;		/* Bytes 16-18 */
    DAC960_V2_CommandTimeout_T CommandTimeout;		/* Byte 19 */
    unsigned char RequestSenseSize;			/* Byte 20 */
    unsigned char CDBLength;				/* Byte 21 */
    unsigned short :16;					/* Bytes 22-23 */
    DAC960_BusAddress64_T SCSI_CDB_BusAddress;		/* Bytes 24-31 */
    DAC960_V2_DataTransferMemoryAddress_T
      DataTransferMemoryAddress;			/* Bytes 32-63 */
  } SCSI_255;
  struct {
    DAC960_V2_CommandIdentifier_T CommandIdentifier;	/* Bytes 0-1 */
    DAC960_V2_CommandOpcode_T CommandOpcode;		/* Byte 2 */
    DAC960_V2_CommandControlBits_T CommandControlBits;	/* Byte 3 */
    DAC960_ByteCount32_T DataTransferSize:24;		/* Bytes 4-6 */
    unsigned char DataTransferPageNumber;		/* Byte 7 */
    DAC960_BusAddress64_T RequestSenseBusAddress;	/* Bytes 8-15 */
    unsigned short :16;					/* Bytes 16-17 */
    unsigned char ControllerNumber;			/* Byte 18 */
    DAC960_V2_CommandTimeout_T CommandTimeout;		/* Byte 19 */
    unsigned char RequestSenseSize;			/* Byte 20 */
    unsigned char IOCTL_Opcode;				/* Byte 21 */
    unsigned char Reserved[10];				/* Bytes 22-31 */
    DAC960_V2_DataTransferMemoryAddress_T
      DataTransferMemoryAddress;			/* Bytes 32-63 */
  } ControllerInfo;
  struct {
    DAC960_V2_CommandIdentifier_T CommandIdentifier;	/* Bytes 0-1 */
    DAC960_V2_CommandOpcode_T CommandOpcode;		/* Byte 2 */
    DAC960_V2_CommandControlBits_T CommandControlBits;	/* Byte 3 */
    DAC960_ByteCount32_T DataTransferSize:24;		/* Bytes 4-6 */
    unsigned char DataTransferPageNumber;		/* Byte 7 */
    DAC960_BusAddress64_T RequestSenseBusAddress;	/* Bytes 8-15 */
    DAC960_V2_LogicalDevice_T LogicalDevice;		/* Bytes 16-18 */
    DAC960_V2_CommandTimeout_T CommandTimeout;		/* Byte 19 */
    unsigned char RequestSenseSize;			/* Byte 20 */
    unsigned char IOCTL_Opcode;				/* Byte 21 */
    unsigned char Reserved[10];				/* Bytes 22-31 */
    DAC960_V2_DataTransferMemoryAddress_T
      DataTransferMemoryAddress;			/* Bytes 32-63 */
  } LogicalDeviceInfo;
  struct {
    DAC960_V2_CommandIdentifier_T CommandIdentifier;	/* Bytes 0-1 */
    DAC960_V2_CommandOpcode_T CommandOpcode;		/* Byte 2 */
    DAC960_V2_CommandControlBits_T CommandControlBits;	/* Byte 3 */
    DAC960_ByteCount32_T DataTransferSize:24;		/* Bytes 4-6 */
    unsigned char DataTransferPageNumber;		/* Byte 7 */
    DAC960_BusAddress64_T RequestSenseBusAddress;	/* Bytes 8-15 */
    DAC960_V2_PhysicalDevice_T PhysicalDevice;		/* Bytes 16-18 */
    DAC960_V2_CommandTimeout_T CommandTimeout;		/* Byte 19 */
    unsigned char RequestSenseSize;			/* Byte 20 */
    unsigned char IOCTL_Opcode;				/* Byte 21 */
    unsigned char Reserved[10];				/* Bytes 22-31 */
    DAC960_V2_DataTransferMemoryAddress_T
      DataTransferMemoryAddress;			/* Bytes 32-63 */
  } PhysicalDeviceInfo;
  struct {
    DAC960_V2_CommandIdentifier_T CommandIdentifier;	/* Bytes 0-1 */
    DAC960_V2_CommandOpcode_T CommandOpcode;		/* Byte 2 */
    DAC960_V2_CommandControlBits_T CommandControlBits;	/* Byte 3 */
    DAC960_ByteCount32_T DataTransferSize:24;		/* Bytes 4-6 */
    unsigned char DataTransferPageNumber;		/* Byte 7 */
    DAC960_BusAddress64_T RequestSenseBusAddress;	/* Bytes 8-15 */
    unsigned short EventSequenceNumberHigh16;		/* Bytes 16-17 */
    unsigned char ControllerNumber;			/* Byte 18 */
    DAC960_V2_CommandTimeout_T CommandTimeout;		/* Byte 19 */
    unsigned char RequestSenseSize;			/* Byte 20 */
    unsigned char IOCTL_Opcode;				/* Byte 21 */
    unsigned short EventSequenceNumberLow16;		/* Bytes 22-23 */
    unsigned char Reserved[8];				/* Bytes 24-31 */
    DAC960_V2_DataTransferMemoryAddress_T
      DataTransferMemoryAddress;			/* Bytes 32-63 */
  } GetEvent;
  struct {
    DAC960_V2_CommandIdentifier_T CommandIdentifier;	/* Bytes 0-1 */
    DAC960_V2_CommandOpcode_T CommandOpcode;		/* Byte 2 */
    DAC960_V2_CommandControlBits_T CommandControlBits;	/* Byte 3 */
    DAC960_ByteCount32_T DataTransferSize:24;		/* Bytes 4-6 */
    unsigned char DataTransferPageNumber;		/* Byte 7 */
    DAC960_BusAddress64_T RequestSenseBusAddress;	/* Bytes 8-15 */
    DAC960_V2_LogicalDevice_T LogicalDevice;		/* Bytes 16-18 */
    DAC960_V2_CommandTimeout_T CommandTimeout;		/* Byte 19 */
    unsigned char RequestSenseSize;			/* Byte 20 */
    unsigned char IOCTL_Opcode;				/* Byte 21 */
    union {
      DAC960_V2_LogicalDeviceState_T LogicalDeviceState;
      DAC960_V2_PhysicalDeviceState_T PhysicalDeviceState;
    } DeviceState;					/* Byte 22 */
    unsigned char Reserved[9];				/* Bytes 23-31 */
    DAC960_V2_DataTransferMemoryAddress_T
      DataTransferMemoryAddress;			/* Bytes 32-63 */
  } SetDeviceState;
  struct {
    DAC960_V2_CommandIdentifier_T CommandIdentifier;	/* Bytes 0-1 */
    DAC960_V2_CommandOpcode_T CommandOpcode;		/* Byte 2 */
    DAC960_V2_CommandControlBits_T CommandControlBits;	/* Byte 3 */
    DAC960_ByteCount32_T DataTransferSize:24;		/* Bytes 4-6 */
    unsigned char DataTransferPageNumber;		/* Byte 7 */
    DAC960_BusAddress64_T RequestSenseBusAddress;	/* Bytes 8-15 */
    DAC960_V2_LogicalDevice_T LogicalDevice;		/* Bytes 16-18 */
    DAC960_V2_CommandTimeout_T CommandTimeout;		/* Byte 19 */
    unsigned char RequestSenseSize;			/* Byte 20 */
    unsigned char IOCTL_Opcode;				/* Byte 21 */
    boolean RestoreConsistency:1;			/* Byte 22 Bit 0 */
    boolean InitializedAreaOnly:1;			/* Byte 22 Bit 1 */
    unsigned char :6;					/* Byte 22 Bits 2-7 */
    unsigned char Reserved[9];				/* Bytes 23-31 */
    DAC960_V2_DataTransferMemoryAddress_T
      DataTransferMemoryAddress;			/* Bytes 32-63 */
  } ConsistencyCheck;
  struct {
    DAC960_V2_CommandIdentifier_T CommandIdentifier;	/* Bytes 0-1 */
    DAC960_V2_CommandOpcode_T CommandOpcode;		/* Byte 2 */
    DAC960_V2_CommandControlBits_T CommandControlBits;	/* Byte 3 */
    unsigned char FirstCommandMailboxSizeKB;		/* Byte 4 */
    unsigned char FirstStatusMailboxSizeKB;		/* Byte 5 */
    unsigned char SecondCommandMailboxSizeKB;		/* Byte 6 */
    unsigned char SecondStatusMailboxSizeKB;		/* Byte 7 */
    DAC960_BusAddress64_T RequestSenseBusAddress;	/* Bytes 8-15 */
    unsigned int :24;					/* Bytes 16-18 */
    DAC960_V2_CommandTimeout_T CommandTimeout;		/* Byte 19 */
    unsigned char RequestSenseSize;			/* Byte 20 */
    unsigned char IOCTL_Opcode;				/* Byte 21 */
    unsigned char HealthStatusBufferSizeKB;		/* Byte 22 */
    unsigned char :8;					/* Byte 23 */
    DAC960_BusAddress64_T HealthStatusBufferBusAddress; /* Bytes 24-31 */
    DAC960_BusAddress64_T FirstCommandMailboxBusAddress; /* Bytes 32-39 */
    DAC960_BusAddress64_T FirstStatusMailboxBusAddress; /* Bytes 40-47 */
    DAC960_BusAddress64_T SecondCommandMailboxBusAddress; /* Bytes 48-55 */
    DAC960_BusAddress64_T SecondStatusMailboxBusAddress; /* Bytes 56-63 */
  } SetMemoryMailbox;
  struct {
    DAC960_V2_CommandIdentifier_T CommandIdentifier;	/* Bytes 0-1 */
    DAC960_V2_CommandOpcode_T CommandOpcode;		/* Byte 2 */
    DAC960_V2_CommandControlBits_T CommandControlBits;	/* Byte 3 */
    DAC960_ByteCount32_T DataTransferSize:24;		/* Bytes 4-6 */
    unsigned char DataTransferPageNumber;		/* Byte 7 */
    DAC960_BusAddress64_T RequestSenseBusAddress;	/* Bytes 8-15 */
    DAC960_V2_PhysicalDevice_T PhysicalDevice;		/* Bytes 16-18 */
    DAC960_V2_CommandTimeout_T CommandTimeout;		/* Byte 19 */
    unsigned char RequestSenseSize;			/* Byte 20 */
    unsigned char IOCTL_Opcode;				/* Byte 21 */
    DAC960_V2_OperationDevice_T OperationDevice;	/* Byte 22 */
    unsigned char Reserved[9];				/* Bytes 23-31 */
    DAC960_V2_DataTransferMemoryAddress_T
      DataTransferMemoryAddress;			/* Bytes 32-63 */
  } DeviceOperation;
}
DAC960_V2_CommandMailbox_T;


/*
  Define the DAC960 Driver IOCTL requests.
*/

#define DAC960_IOCTL_GET_CONTROLLER_COUNT	0xDAC001
#define DAC960_IOCTL_GET_CONTROLLER_INFO	0xDAC002
#define DAC960_IOCTL_V1_EXECUTE_COMMAND		0xDAC003
#define DAC960_IOCTL_V2_EXECUTE_COMMAND		0xDAC004
#define DAC960_IOCTL_V2_GET_HEALTH_STATUS	0xDAC005


/*
  Define the DAC960_IOCTL_GET_CONTROLLER_INFO reply structure.
*/

typedef struct DAC960_ControllerInfo
{
  unsigned char ControllerNumber;
  unsigned char FirmwareType;
  unsigned char Channels;
  unsigned char Targets;
  unsigned char PCI_Bus;
  unsigned char PCI_Device;
  unsigned char PCI_Function;
  unsigned char IRQ_Channel;
  DAC960_PCI_Address_T PCI_Address;
  unsigned char ModelName[20];
  unsigned char FirmwareVersion[12];
}
DAC960_ControllerInfo_T;


/*
  Define the User Mode DAC960_IOCTL_V1_EXECUTE_COMMAND request structure.
*/

typedef struct DAC960_V1_UserCommand
{
  unsigned char ControllerNumber;
  DAC960_V1_CommandMailbox_T CommandMailbox;
  int DataTransferLength;
  void __user *DataTransferBuffer;
  DAC960_V1_DCDB_T __user *DCDB;
}
DAC960_V1_UserCommand_T;


/*
  Define the Kernel Mode DAC960_IOCTL_V1_EXECUTE_COMMAND request structure.
*/

typedef struct DAC960_V1_KernelCommand
{
  unsigned char ControllerNumber;
  DAC960_V1_CommandMailbox_T CommandMailbox;
  int DataTransferLength;
  void *DataTransferBuffer;
  DAC960_V1_DCDB_T *DCDB;
  DAC960_V1_CommandStatus_T CommandStatus;
  void (*CompletionFunction)(struct DAC960_V1_KernelCommand *);
  void *CompletionData;
}
DAC960_V1_KernelCommand_T;


/*
  Define the User Mode DAC960_IOCTL_V2_EXECUTE_COMMAND request structure.
*/

typedef struct DAC960_V2_UserCommand
{
  unsigned char ControllerNumber;
  DAC960_V2_CommandMailbox_T CommandMailbox;
  int DataTransferLength;
  int RequestSenseLength;
  void __user *DataTransferBuffer;
  void __user *RequestSenseBuffer;
}
DAC960_V2_UserCommand_T;


/*
  Define the Kernel Mode DAC960_IOCTL_V2_EXECUTE_COMMAND request structure.
*/

typedef struct DAC960_V2_KernelCommand
{
  unsigned char ControllerNumber;
  DAC960_V2_CommandMailbox_T CommandMailbox;
  int DataTransferLength;
  int RequestSenseLength;
  void *DataTransferBuffer;
  void *RequestSenseBuffer;
  DAC960_V2_CommandStatus_T CommandStatus;
  void (*CompletionFunction)(struct DAC960_V2_KernelCommand *);
  void *CompletionData;
}
DAC960_V2_KernelCommand_T;


/*
  Define the User Mode DAC960_IOCTL_V2_GET_HEALTH_STATUS request structure.
*/

typedef struct DAC960_V2_GetHealthStatus
{
  unsigned char ControllerNumber;
  DAC960_V2_HealthStatusBuffer_T __user *HealthStatusBuffer;
}
DAC960_V2_GetHealthStatus_T;


/*
  Import the Kernel Mode IOCTL interface.
*/

extern int DAC960_KernelIOCTL(unsigned int Request, void *Argument);


/*
  DAC960_DriverVersion protects the private portion of this file.
*/

#ifdef DAC960_DriverVersion


/*
  Define the maximum Driver Queue Depth and Controller Queue Depth supported
  by DAC960 V1 and V2 Firmware Controllers.
*/

#define DAC960_MaxDriverQueueDepth		511
#define DAC960_MaxControllerQueueDepth		512


/*
  Define the maximum number of Scatter/Gather Segments supported for any
  DAC960 V1 and V2 Firmware controller.
*/

#define DAC960_V1_ScatterGatherLimit		33
#define DAC960_V2_ScatterGatherLimit		128


/*
  Define the number of Command Mailboxes and Status Mailboxes used by the
  DAC960 V1 and V2 Firmware Memory Mailbox Interface.
*/

#define DAC960_V1_CommandMailboxCount		256
#define DAC960_V1_StatusMailboxCount		1024
#define DAC960_V2_CommandMailboxCount		512
#define DAC960_V2_StatusMailboxCount		512


/*
  Define the DAC960 Controller Monitoring Timer Interval.
*/

#define DAC960_MonitoringTimerInterval		(10 * HZ)


/*
  Define the DAC960 Controller Secondary Monitoring Interval.
*/

#define DAC960_SecondaryMonitoringInterval	(60 * HZ)


/*
  Define the DAC960 Controller Health Status Monitoring Interval.
*/

#define DAC960_HealthStatusMonitoringInterval	(1 * HZ)


/*
  Define the DAC960 Controller Progress Reporting Interval.
*/

#define DAC960_ProgressReportingInterval	(60 * HZ)


/*
  Define the maximum number of Partitions allowed for each Logical Drive.
*/

#define DAC960_MaxPartitions			8
#define DAC960_MaxPartitionsBits		3

/*
  Define the DAC960 Controller fixed Block Size and Block Size Bits.
*/

#define DAC960_BlockSize			512
#define DAC960_BlockSizeBits			9


/*
  Define the number of Command structures that should be allocated as a
  group to optimize kernel memory allocation.
*/

#define DAC960_V1_CommandAllocationGroupSize	11
#define DAC960_V2_CommandAllocationGroupSize	29


/*
  Define the Controller Line Buffer, Progress Buffer, User Message, and
  Initial Status Buffer sizes.
*/

#define DAC960_LineBufferSize			100
#define DAC960_ProgressBufferSize		200
#define DAC960_UserMessageSize			200
#define DAC960_InitialStatusBufferSize		(8192-32)


/*
  Define the DAC960 Controller Firmware Types.
*/

typedef enum
{
  DAC960_V1_Controller =			1,
  DAC960_V2_Controller =			2
}
DAC960_FirmwareType_T;


/*
  Define the DAC960 Controller Hardware Types.
*/

typedef enum
{
  DAC960_BA_Controller =			1,	/* eXtremeRAID 2000 */
  DAC960_LP_Controller =			2,	/* AcceleRAID 352 */
  DAC960_LA_Controller =			3,	/* DAC1164P */
  DAC960_PG_Controller =			4,	/* DAC960PTL/PJ/PG */
  DAC960_PD_Controller =			5,	/* DAC960PU/PD/PL/P */
  DAC960_P_Controller =				6,	/* DAC960PU/PD/PL/P */
  DAC960_GEM_Controller =			7,	/* AcceleRAID 4/5/600 */
}
DAC960_HardwareType_T;


/*
  Define the Driver Message Levels.
*/

typedef enum DAC960_MessageLevel
{
  DAC960_AnnounceLevel =			0,
  DAC960_InfoLevel =				1,
  DAC960_NoticeLevel =				2,
  DAC960_WarningLevel =				3,
  DAC960_ErrorLevel =				4,
  DAC960_ProgressLevel =			5,
  DAC960_CriticalLevel =			6,
  DAC960_UserCriticalLevel =			7
}
DAC960_MessageLevel_T;

static char
  *DAC960_MessageLevelMap[] =
    { KERN_NOTICE, KERN_NOTICE, KERN_NOTICE, KERN_WARNING,
      KERN_ERR, KERN_CRIT, KERN_CRIT, KERN_CRIT };


/*
  Define Driver Message macros.
*/

#define DAC960_Announce(Format, Arguments...) \
  DAC960_Message(DAC960_AnnounceLevel, Format, ##Arguments)

#define DAC960_Info(Format, Arguments...) \
  DAC960_Message(DAC960_InfoLevel, Format, ##Arguments)

#define DAC960_Notice(Format, Arguments...) \
  DAC960_Message(DAC960_NoticeLevel, Format, ##Arguments)

#define DAC960_Warning(Format, Arguments...) \
  DAC960_Message(DAC960_WarningLevel, Format, ##Arguments)

#define DAC960_Error(Format, Arguments...) \
  DAC960_Message(DAC960_ErrorLevel, Format, ##Arguments)

#define DAC960_Progress(Format, Arguments...) \
  DAC960_Message(DAC960_ProgressLevel, Format, ##Arguments)

#define DAC960_Critical(Format, Arguments...) \
  DAC960_Message(DAC960_CriticalLevel, Format, ##Arguments)

#define DAC960_UserCritical(Format, Arguments...) \
  DAC960_Message(DAC960_UserCriticalLevel, Format, ##Arguments)


struct DAC960_privdata {
	DAC960_HardwareType_T	HardwareType;
	DAC960_FirmwareType_T	FirmwareType;
	irqreturn_t (*InterruptHandler)(int, void *, struct pt_regs *);
	unsigned int		MemoryWindowSize;
};


/*
  Define the DAC960 V1 Firmware Controller Status Mailbox structure.
*/

typedef union DAC960_V1_StatusMailbox
{
  unsigned int Word;					/* Word 0 */
  struct {
    DAC960_V1_CommandIdentifier_T CommandIdentifier;	/* Byte 0 */
    unsigned char :7;					/* Byte 1 Bits 0-6 */
    boolean Valid:1;					/* Byte 1 Bit 7 */
    DAC960_V1_CommandStatus_T CommandStatus;		/* Bytes 2-3 */
  } Fields;
}
DAC960_V1_StatusMailbox_T;


/*
  Define the DAC960 V2 Firmware Controller Status Mailbox structure.
*/

typedef union DAC960_V2_StatusMailbox
{
  unsigned int Words[2];				/* Words 0-1 */
  struct {
    DAC960_V2_CommandIdentifier_T CommandIdentifier;	/* Bytes 0-1 */
    DAC960_V2_CommandStatus_T CommandStatus;		/* Byte 2 */
    unsigned char RequestSenseLength;			/* Byte 3 */
    int DataTransferResidue;				/* Bytes 4-7 */
  } Fields;
}
DAC960_V2_StatusMailbox_T;


/*
  Define the DAC960 Driver Command Types.
*/

typedef enum
{
  DAC960_ReadCommand =				1,
  DAC960_WriteCommand =				2,
  DAC960_ReadRetryCommand =			3,
  DAC960_WriteRetryCommand =			4,
  DAC960_MonitoringCommand =			5,
  DAC960_ImmediateCommand =			6,
  DAC960_QueuedCommand =			7
}
DAC960_CommandType_T;


/*
  Define the DAC960 Driver Command structure.
*/

typedef struct DAC960_Command
{
  int CommandIdentifier;
  DAC960_CommandType_T CommandType;
  struct DAC960_Controller *Controller;
  struct DAC960_Command *Next;
  struct completion *Completion;
  unsigned int LogicalDriveNumber;
  unsigned int BlockNumber;
  unsigned int BlockCount;
  unsigned int SegmentCount;
  int	DmaDirection;
  struct scatterlist *cmd_sglist;
  struct request *Request;
  union {
    struct {
      DAC960_V1_CommandMailbox_T CommandMailbox;
      DAC960_V1_KernelCommand_T *KernelCommand;
      DAC960_V1_CommandStatus_T CommandStatus;
      DAC960_V1_ScatterGatherSegment_T *ScatterGatherList;
      dma_addr_t ScatterGatherListDMA;
      struct scatterlist ScatterList[DAC960_V1_ScatterGatherLimit];
      unsigned int EndMarker[0];
    } V1;
    struct {
      DAC960_V2_CommandMailbox_T CommandMailbox;
      DAC960_V2_KernelCommand_T *KernelCommand;
      DAC960_V2_CommandStatus_T CommandStatus;
      unsigned char RequestSenseLength;
      int DataTransferResidue;
      DAC960_V2_ScatterGatherSegment_T *ScatterGatherList;
      dma_addr_t ScatterGatherListDMA;
      DAC960_SCSI_RequestSense_T *RequestSense;
      dma_addr_t RequestSenseDMA;
      struct scatterlist ScatterList[DAC960_V2_ScatterGatherLimit];
      unsigned int EndMarker[0];
    } V2;
  } FW;
}
DAC960_Command_T;


/*
  Define the DAC960 Driver Controller structure.
*/

typedef struct DAC960_Controller
{
  void __iomem *BaseAddress;
  void __iomem *MemoryMappedAddress;
  DAC960_FirmwareType_T FirmwareType;
  DAC960_HardwareType_T HardwareType;
  DAC960_IO_Address_T IO_Address;
  DAC960_PCI_Address_T PCI_Address;
  struct pci_dev *PCIDevice;
  unsigned char ControllerNumber;
  unsigned char ControllerName[4];
  unsigned char ModelName[20];
  unsigned char FullModelName[28];
  unsigned char FirmwareVersion[12];
  unsigned char Bus;
  unsigned char Device;
  unsigned char Function;
  unsigned char IRQ_Channel;
  unsigned char Channels;
  unsigned char Targets;
  unsigned char MemorySize;
  unsigned char LogicalDriveCount;
  unsigned short CommandAllocationGroupSize;
  unsigned short ControllerQueueDepth;
  unsigned short DriverQueueDepth;
  unsigned short MaxBlocksPerCommand;
  unsigned short ControllerScatterGatherLimit;
  unsigned short DriverScatterGatherLimit;
  u64		BounceBufferLimit;
  unsigned int CombinedStatusBufferLength;
  unsigned int InitialStatusLength;
  unsigned int CurrentStatusLength;
  unsigned int ProgressBufferLength;
  unsigned int UserStatusLength;
  struct dma_loaf DmaPages;
  unsigned long MonitoringTimerCount;
  unsigned long PrimaryMonitoringTime;
  unsigned long SecondaryMonitoringTime;
  unsigned long ShutdownMonitoringTimer;
  unsigned long LastProgressReportTime;
  unsigned long LastCurrentStatusTime;
  boolean ControllerInitialized;
  boolean MonitoringCommandDeferred;
  boolean EphemeralProgressMessage;
  boolean DriveSpinUpMessageDisplayed;
  boolean MonitoringAlertMode;
  boolean SuppressEnclosureMessages;
  struct timer_list MonitoringTimer;
  struct gendisk *disks[DAC960_MaxLogicalDrives];
  struct pci_pool *ScatterGatherPool;
  DAC960_Command_T *FreeCommands;
  unsigned char *CombinedStatusBuffer;
  unsigned char *CurrentStatusBuffer;
  struct request_queue *RequestQueue[DAC960_MaxLogicalDrives];
  int req_q_index;
  spinlock_t queue_lock;
  wait_queue_head_t CommandWaitQueue;
  wait_queue_head_t HealthStatusWaitQueue;
  DAC960_Command_T InitialCommand;
  DAC960_Command_T *Commands[DAC960_MaxDriverQueueDepth];
  struct proc_dir_entry *ControllerProcEntry;
  boolean LogicalDriveInitiallyAccessible[DAC960_MaxLogicalDrives];
  void (*QueueCommand)(DAC960_Command_T *Command);
  boolean (*ReadControllerConfiguration)(struct DAC960_Controller *);
  boolean (*ReadDeviceConfiguration)(struct DAC960_Controller *);
  boolean (*ReportDeviceConfiguration)(struct DAC960_Controller *);
  void (*QueueReadWriteCommand)(DAC960_Command_T *Command);
  union {
    struct {
      unsigned char GeometryTranslationHeads;
      unsigned char GeometryTranslationSectors;
      unsigned char PendingRebuildFlag;
      unsigned short StripeSize;
      unsigned short SegmentSize;
      unsigned short NewEventLogSequenceNumber;
      unsigned short OldEventLogSequenceNumber;
      unsigned short DeviceStateChannel;
      unsigned short DeviceStateTargetID;
      boolean DualModeMemoryMailboxInterface;
      boolean BackgroundInitializationStatusSupported;
      boolean SAFTE_EnclosureManagementEnabled;
      boolean NeedLogicalDriveInformation;
      boolean NeedErrorTableInformation;
      boolean NeedDeviceStateInformation;
      boolean NeedDeviceInquiryInformation;
      boolean NeedDeviceSerialNumberInformation;
      boolean NeedRebuildProgress;
      boolean NeedConsistencyCheckProgress;
      boolean NeedBackgroundInitializationStatus;
      boolean StartDeviceStateScan;
      boolean RebuildProgressFirst;
      boolean RebuildFlagPending;
      boolean RebuildStatusPending;

      dma_addr_t	FirstCommandMailboxDMA;
      DAC960_V1_CommandMailbox_T *FirstCommandMailbox;
      DAC960_V1_CommandMailbox_T *LastCommandMailbox;
      DAC960_V1_CommandMailbox_T *NextCommandMailbox;
      DAC960_V1_CommandMailbox_T *PreviousCommandMailbox1;
      DAC960_V1_CommandMailbox_T *PreviousCommandMailbox2;

      dma_addr_t	FirstStatusMailboxDMA;
      DAC960_V1_StatusMailbox_T *FirstStatusMailbox;
      DAC960_V1_StatusMailbox_T *LastStatusMailbox;
      DAC960_V1_StatusMailbox_T *NextStatusMailbox;

      DAC960_V1_DCDB_T *MonitoringDCDB;
      dma_addr_t MonitoringDCDB_DMA;

      DAC960_V1_Enquiry_T Enquiry;
      DAC960_V1_Enquiry_T *NewEnquiry;
      dma_addr_t NewEnquiryDMA;

      DAC960_V1_ErrorTable_T ErrorTable;
      DAC960_V1_ErrorTable_T *NewErrorTable;
      dma_addr_t NewErrorTableDMA;

      DAC960_V1_EventLogEntry_T *EventLogEntry;
      dma_addr_t EventLogEntryDMA;

      DAC960_V1_RebuildProgress_T *RebuildProgress;
      dma_addr_t RebuildProgressDMA;
      DAC960_V1_CommandStatus_T LastRebuildStatus;
      DAC960_V1_CommandStatus_T PendingRebuildStatus;

      DAC960_V1_LogicalDriveInformationArray_T LogicalDriveInformation;
      DAC960_V1_LogicalDriveInformationArray_T *NewLogicalDriveInformation;
      dma_addr_t NewLogicalDriveInformationDMA;

      DAC960_V1_BackgroundInitializationStatus_T
        	*BackgroundInitializationStatus;
      dma_addr_t BackgroundInitializationStatusDMA;
      DAC960_V1_BackgroundInitializationStatus_T
        	LastBackgroundInitializationStatus;

      DAC960_V1_DeviceState_T
	DeviceState[DAC960_V1_MaxChannels][DAC960_V1_MaxTargets];
      DAC960_V1_DeviceState_T *NewDeviceState;
      dma_addr_t	NewDeviceStateDMA;

      DAC960_SCSI_Inquiry_T
	InquiryStandardData[DAC960_V1_MaxChannels][DAC960_V1_MaxTargets];
      DAC960_SCSI_Inquiry_T *NewInquiryStandardData;
      dma_addr_t NewInquiryStandardDataDMA;

      DAC960_SCSI_Inquiry_UnitSerialNumber_T
	InquiryUnitSerialNumber[DAC960_V1_MaxChannels][DAC960_V1_MaxTargets];
      DAC960_SCSI_Inquiry_UnitSerialNumber_T *NewInquiryUnitSerialNumber;
      dma_addr_t NewInquiryUnitSerialNumberDMA;

      int DeviceResetCount[DAC960_V1_MaxChannels][DAC960_V1_MaxTargets];
      boolean DirectCommandActive[DAC960_V1_MaxChannels][DAC960_V1_MaxTargets];
    } V1;
    struct {
      unsigned int StatusChangeCounter;
      unsigned int NextEventSequenceNumber;
      unsigned int PhysicalDeviceIndex;
      boolean NeedLogicalDeviceInformation;
      boolean NeedPhysicalDeviceInformation;
      boolean NeedDeviceSerialNumberInformation;
      boolean StartLogicalDeviceInformationScan;
      boolean StartPhysicalDeviceInformationScan;
      struct pci_pool *RequestSensePool;

      dma_addr_t	FirstCommandMailboxDMA;
      DAC960_V2_CommandMailbox_T *FirstCommandMailbox;
      DAC960_V2_CommandMailbox_T *LastCommandMailbox;
      DAC960_V2_CommandMailbox_T *NextCommandMailbox;
      DAC960_V2_CommandMailbox_T *PreviousCommandMailbox1;
      DAC960_V2_CommandMailbox_T *PreviousCommandMailbox2;

      dma_addr_t	FirstStatusMailboxDMA;
      DAC960_V2_StatusMailbox_T *FirstStatusMailbox;
      DAC960_V2_StatusMailbox_T *LastStatusMailbox;
      DAC960_V2_StatusMailbox_T *NextStatusMailbox;

      dma_addr_t	HealthStatusBufferDMA;
      DAC960_V2_HealthStatusBuffer_T *HealthStatusBuffer;

      DAC960_V2_ControllerInfo_T ControllerInformation;
      DAC960_V2_ControllerInfo_T *NewControllerInformation;
      dma_addr_t	NewControllerInformationDMA;

      DAC960_V2_LogicalDeviceInfo_T
	*LogicalDeviceInformation[DAC960_MaxLogicalDrives];
      DAC960_V2_LogicalDeviceInfo_T *NewLogicalDeviceInformation;
      dma_addr_t	 NewLogicalDeviceInformationDMA;

      DAC960_V2_PhysicalDeviceInfo_T
	*PhysicalDeviceInformation[DAC960_V2_MaxPhysicalDevices];
      DAC960_V2_PhysicalDeviceInfo_T *NewPhysicalDeviceInformation;
      dma_addr_t	NewPhysicalDeviceInformationDMA;

      DAC960_SCSI_Inquiry_UnitSerialNumber_T *NewInquiryUnitSerialNumber;
      dma_addr_t	NewInquiryUnitSerialNumberDMA;
      DAC960_SCSI_Inquiry_UnitSerialNumber_T
	*InquiryUnitSerialNumber[DAC960_V2_MaxPhysicalDevices];

      DAC960_V2_Event_T *Event;
      dma_addr_t EventDMA;

      DAC960_V2_PhysicalToLogicalDevice_T *PhysicalToLogicalDevice;
      dma_addr_t PhysicalToLogicalDeviceDMA;

      DAC960_V2_PhysicalDevice_T
	LogicalDriveToVirtualDevice[DAC960_MaxLogicalDrives];
      boolean LogicalDriveFoundDuringScan[DAC960_MaxLogicalDrives];
    } V2;
  } FW;
  unsigned char ProgressBuffer[DAC960_ProgressBufferSize];
  unsigned char UserStatusBuffer[DAC960_UserMessageSize];
}
DAC960_Controller_T;


/*
  Simplify access to Firmware Version Dependent Data Structure Components
  and Functions.
*/

#define V1				FW.V1
#define V2				FW.V2
#define DAC960_QueueCommand(Command) \
  (Controller->QueueCommand)(Command)
#define DAC960_ReadControllerConfiguration(Controller) \
  (Controller->ReadControllerConfiguration)(Controller)
#define DAC960_ReadDeviceConfiguration(Controller) \
  (Controller->ReadDeviceConfiguration)(Controller)
#define DAC960_ReportDeviceConfiguration(Controller) \
  (Controller->ReportDeviceConfiguration)(Controller)
#define DAC960_QueueReadWriteCommand(Command) \
  (Controller->QueueReadWriteCommand)(Command)

/*
 * dma_addr_writeql is provided to write dma_addr_t types
 * to a 64-bit pci address space register.  The controller
 * will accept having the register written as two 32-bit
 * values.
 *
 * In HIGHMEM kernels, dma_addr_t is a 64-bit value.
 * without HIGHMEM,  dma_addr_t is a 32-bit value.
 *
 * The compiler should always fix up the assignment
 * to u.wq appropriately, depending upon the size of
 * dma_addr_t.
 */
static inline
void dma_addr_writeql(dma_addr_t addr, void __iomem *write_address)
{
	union {
		u64 wq;
		uint wl[2];
	} u;

	u.wq = addr;

	writel(u.wl[0], write_address);
	writel(u.wl[1], write_address + 4);
}

/*
  Define the DAC960 GEM Series Controller Interface Register Offsets.
 */

#define DAC960_GEM_RegisterWindowSize	0x600

typedef enum
{
  DAC960_GEM_InboundDoorBellRegisterReadSetOffset   =   0x214,
  DAC960_GEM_InboundDoorBellRegisterClearOffset     =   0x218,
  DAC960_GEM_OutboundDoorBellRegisterReadSetOffset  =   0x224,
  DAC960_GEM_OutboundDoorBellRegisterClearOffset    =   0x228,
  DAC960_GEM_InterruptStatusRegisterOffset          =   0x208,
  DAC960_GEM_InterruptMaskRegisterReadSetOffset     =   0x22C,
  DAC960_GEM_InterruptMaskRegisterClearOffset       =   0x230,
  DAC960_GEM_CommandMailboxBusAddressOffset         =   0x510,
  DAC960_GEM_CommandStatusOffset                    =   0x518,
  DAC960_GEM_ErrorStatusRegisterReadSetOffset       =   0x224,
  DAC960_GEM_ErrorStatusRegisterClearOffset         =   0x228,
}
DAC960_GEM_RegisterOffsets_T;

/*
  Define the structure of the DAC960 GEM Series Inbound Door Bell
 */

typedef union DAC960_GEM_InboundDoorBellRegister
{
  unsigned int All;
  struct {
    unsigned int :24;
    boolean HardwareMailboxNewCommand:1;
    boolean AcknowledgeHardwareMailboxStatus:1;
    boolean GenerateInterrupt:1;
    boolean ControllerReset:1;
    boolean MemoryMailboxNewCommand:1;
    unsigned int :3;
  } Write;
  struct {
    unsigned int :24;
    boolean HardwareMailboxFull:1;
    boolean InitializationInProgress:1;
    unsigned int :6;
  } Read;
}
DAC960_GEM_InboundDoorBellRegister_T;

/*
  Define the structure of the DAC960 GEM Series Outbound Door Bell Register.
 */
typedef union DAC960_GEM_OutboundDoorBellRegister
{
  unsigned int All;
  struct {
    unsigned int :24;
    boolean AcknowledgeHardwareMailboxInterrupt:1;
    boolean AcknowledgeMemoryMailboxInterrupt:1;
    unsigned int :6;
  } Write;
  struct {
    unsigned int :24;
    boolean HardwareMailboxStatusAvailable:1;
    boolean MemoryMailboxStatusAvailable:1;
    unsigned int :6;
  } Read;
}
DAC960_GEM_OutboundDoorBellRegister_T;

/*
  Define the structure of the DAC960 GEM Series Interrupt Mask Register.
 */
typedef union DAC960_GEM_InterruptMaskRegister
{
  unsigned int All;
  struct {
    unsigned int :16;
    unsigned int :8;
    unsigned int HardwareMailboxInterrupt:1;
    unsigned int MemoryMailboxInterrupt:1;
    unsigned int :6;
  } Bits;
}
DAC960_GEM_InterruptMaskRegister_T;

/*
  Define the structure of the DAC960 GEM Series Error Status Register.
 */

typedef union DAC960_GEM_ErrorStatusRegister
{
  unsigned int All;
  struct {
    unsigned int :24;
    unsigned int :5;
    boolean ErrorStatusPending:1;
    unsigned int :2;
  } Bits;
}
DAC960_GEM_ErrorStatusRegister_T;

/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 GEM Series Controller Interface Registers.
*/

static inline
void DAC960_GEM_HardwareMailboxNewCommand(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.HardwareMailboxNewCommand = true;
  writel(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_GEM_InboundDoorBellRegisterReadSetOffset);
}

static inline
void DAC960_GEM_AcknowledgeHardwareMailboxStatus(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.AcknowledgeHardwareMailboxStatus = true;
  writel(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_GEM_InboundDoorBellRegisterClearOffset);
}

static inline
void DAC960_GEM_GenerateInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.GenerateInterrupt = true;
  writel(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_GEM_InboundDoorBellRegisterReadSetOffset);
}

static inline
void DAC960_GEM_ControllerReset(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.ControllerReset = true;
  writel(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_GEM_InboundDoorBellRegisterReadSetOffset);
}

static inline
void DAC960_GEM_MemoryMailboxNewCommand(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.MemoryMailboxNewCommand = true;
  writel(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_GEM_InboundDoorBellRegisterReadSetOffset);
}

static inline
boolean DAC960_GEM_HardwareMailboxFullP(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readl(ControllerBaseAddress +
          DAC960_GEM_InboundDoorBellRegisterReadSetOffset);
  return InboundDoorBellRegister.Read.HardwareMailboxFull;
}

static inline
boolean DAC960_GEM_InitializationInProgressP(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readl(ControllerBaseAddress +
          DAC960_GEM_InboundDoorBellRegisterReadSetOffset);
  return InboundDoorBellRegister.Read.InitializationInProgress;
}

static inline
void DAC960_GEM_AcknowledgeHardwareMailboxInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
  writel(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_GEM_OutboundDoorBellRegisterClearOffset);
}

static inline
void DAC960_GEM_AcknowledgeMemoryMailboxInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
  writel(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_GEM_OutboundDoorBellRegisterClearOffset);
}

static inline
void DAC960_GEM_AcknowledgeInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
  OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
  writel(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_GEM_OutboundDoorBellRegisterClearOffset);
}

static inline
boolean DAC960_GEM_HardwareMailboxStatusAvailableP(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All =
    readl(ControllerBaseAddress +
          DAC960_GEM_OutboundDoorBellRegisterReadSetOffset);
  return OutboundDoorBellRegister.Read.HardwareMailboxStatusAvailable;
}

static inline
boolean DAC960_GEM_MemoryMailboxStatusAvailableP(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All =
    readl(ControllerBaseAddress +
          DAC960_GEM_OutboundDoorBellRegisterReadSetOffset);
  return OutboundDoorBellRegister.Read.MemoryMailboxStatusAvailable;
}

static inline
void DAC960_GEM_EnableInterrupts(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All = 0;
  InterruptMaskRegister.Bits.HardwareMailboxInterrupt = true;
  InterruptMaskRegister.Bits.MemoryMailboxInterrupt = true;
  writel(InterruptMaskRegister.All,
	 ControllerBaseAddress + DAC960_GEM_InterruptMaskRegisterClearOffset);
}

static inline
void DAC960_GEM_DisableInterrupts(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All = 0;
  InterruptMaskRegister.Bits.HardwareMailboxInterrupt = true;
  InterruptMaskRegister.Bits.MemoryMailboxInterrupt = true;
  writel(InterruptMaskRegister.All,
	 ControllerBaseAddress + DAC960_GEM_InterruptMaskRegisterReadSetOffset);
}

static inline
boolean DAC960_GEM_InterruptsEnabledP(void __iomem *ControllerBaseAddress)
{
  DAC960_GEM_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All =
    readl(ControllerBaseAddress +
          DAC960_GEM_InterruptMaskRegisterReadSetOffset);
  return !(InterruptMaskRegister.Bits.HardwareMailboxInterrupt ||
           InterruptMaskRegister.Bits.MemoryMailboxInterrupt);
}

static inline
void DAC960_GEM_WriteCommandMailbox(DAC960_V2_CommandMailbox_T
				     *MemoryCommandMailbox,
				   DAC960_V2_CommandMailbox_T
				     *CommandMailbox)
{
  memcpy(&MemoryCommandMailbox->Words[1], &CommandMailbox->Words[1],
	 sizeof(DAC960_V2_CommandMailbox_T) - sizeof(unsigned int));
  wmb();
  MemoryCommandMailbox->Words[0] = CommandMailbox->Words[0];
  mb();
}

static inline
void DAC960_GEM_WriteHardwareMailbox(void __iomem *ControllerBaseAddress,
				    dma_addr_t CommandMailboxDMA)
{
	dma_addr_writeql(CommandMailboxDMA,
		ControllerBaseAddress +
		DAC960_GEM_CommandMailboxBusAddressOffset);
}

static inline DAC960_V2_CommandIdentifier_T
DAC960_GEM_ReadCommandIdentifier(void __iomem *ControllerBaseAddress)
{
  return readw(ControllerBaseAddress + DAC960_GEM_CommandStatusOffset);
}

static inline DAC960_V2_CommandStatus_T
DAC960_GEM_ReadCommandStatus(void __iomem *ControllerBaseAddress)
{
  return readw(ControllerBaseAddress + DAC960_GEM_CommandStatusOffset + 2);
}

static inline boolean
DAC960_GEM_ReadErrorStatus(void __iomem *ControllerBaseAddress,
			  unsigned char *ErrorStatus,
			  unsigned char *Parameter0,
			  unsigned char *Parameter1)
{
  DAC960_GEM_ErrorStatusRegister_T ErrorStatusRegister;
  ErrorStatusRegister.All =
    readl(ControllerBaseAddress + DAC960_GEM_ErrorStatusRegisterReadSetOffset);
  if (!ErrorStatusRegister.Bits.ErrorStatusPending) return false;
  ErrorStatusRegister.Bits.ErrorStatusPending = false;
  *ErrorStatus = ErrorStatusRegister.All;
  *Parameter0 =
    readb(ControllerBaseAddress + DAC960_GEM_CommandMailboxBusAddressOffset + 0);
  *Parameter1 =
    readb(ControllerBaseAddress + DAC960_GEM_CommandMailboxBusAddressOffset + 1);
  writel(0x03000000, ControllerBaseAddress +
         DAC960_GEM_ErrorStatusRegisterClearOffset);
  return true;
}

/*
  Define the DAC960 BA Series Controller Interface Register Offsets.
*/

#define DAC960_BA_RegisterWindowSize		0x80

typedef enum
{
  DAC960_BA_InboundDoorBellRegisterOffset =	0x60,
  DAC960_BA_OutboundDoorBellRegisterOffset =	0x61,
  DAC960_BA_InterruptStatusRegisterOffset =	0x30,
  DAC960_BA_InterruptMaskRegisterOffset =	0x34,
  DAC960_BA_CommandMailboxBusAddressOffset =	0x50,
  DAC960_BA_CommandStatusOffset =		0x58,
  DAC960_BA_ErrorStatusRegisterOffset =		0x63
}
DAC960_BA_RegisterOffsets_T;


/*
  Define the structure of the DAC960 BA Series Inbound Door Bell Register.
*/

typedef union DAC960_BA_InboundDoorBellRegister
{
  unsigned char All;
  struct {
    boolean HardwareMailboxNewCommand:1;		/* Bit 0 */
    boolean AcknowledgeHardwareMailboxStatus:1;		/* Bit 1 */
    boolean GenerateInterrupt:1;			/* Bit 2 */
    boolean ControllerReset:1;				/* Bit 3 */
    boolean MemoryMailboxNewCommand:1;			/* Bit 4 */
    unsigned char :3;					/* Bits 5-7 */
  } Write;
  struct {
    boolean HardwareMailboxEmpty:1;			/* Bit 0 */
    boolean InitializationNotInProgress:1;		/* Bit 1 */
    unsigned char :6;					/* Bits 2-7 */
  } Read;
}
DAC960_BA_InboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 BA Series Outbound Door Bell Register.
*/

typedef union DAC960_BA_OutboundDoorBellRegister
{
  unsigned char All;
  struct {
    boolean AcknowledgeHardwareMailboxInterrupt:1;	/* Bit 0 */
    boolean AcknowledgeMemoryMailboxInterrupt:1;	/* Bit 1 */
    unsigned char :6;					/* Bits 2-7 */
  } Write;
  struct {
    boolean HardwareMailboxStatusAvailable:1;		/* Bit 0 */
    boolean MemoryMailboxStatusAvailable:1;		/* Bit 1 */
    unsigned char :6;					/* Bits 2-7 */
  } Read;
}
DAC960_BA_OutboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 BA Series Interrupt Mask Register.
*/

typedef union DAC960_BA_InterruptMaskRegister
{
  unsigned char All;
  struct {
    unsigned int :2;					/* Bits 0-1 */
    boolean DisableInterrupts:1;			/* Bit 2 */
    boolean DisableInterruptsI2O:1;			/* Bit 3 */
    unsigned int :4;					/* Bits 4-7 */
  } Bits;
}
DAC960_BA_InterruptMaskRegister_T;


/*
  Define the structure of the DAC960 BA Series Error Status Register.
*/

typedef union DAC960_BA_ErrorStatusRegister
{
  unsigned char All;
  struct {
    unsigned int :2;					/* Bits 0-1 */
    boolean ErrorStatusPending:1;			/* Bit 2 */
    unsigned int :5;					/* Bits 3-7 */
  } Bits;
}
DAC960_BA_ErrorStatusRegister_T;


/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 BA Series Controller Interface Registers.
*/

static inline
void DAC960_BA_HardwareMailboxNewCommand(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.HardwareMailboxNewCommand = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_BA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_BA_AcknowledgeHardwareMailboxStatus(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.AcknowledgeHardwareMailboxStatus = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_BA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_BA_GenerateInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.GenerateInterrupt = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_BA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_BA_ControllerReset(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.ControllerReset = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_BA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_BA_MemoryMailboxNewCommand(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.MemoryMailboxNewCommand = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_BA_InboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_BA_HardwareMailboxFullP(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_BA_InboundDoorBellRegisterOffset);
  return !InboundDoorBellRegister.Read.HardwareMailboxEmpty;
}

static inline
boolean DAC960_BA_InitializationInProgressP(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_BA_InboundDoorBellRegisterOffset);
  return !InboundDoorBellRegister.Read.InitializationNotInProgress;
}

static inline
void DAC960_BA_AcknowledgeHardwareMailboxInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
  writeb(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_BA_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_BA_AcknowledgeMemoryMailboxInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
  writeb(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_BA_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_BA_AcknowledgeInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
  OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
  writeb(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_BA_OutboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_BA_HardwareMailboxStatusAvailableP(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_BA_OutboundDoorBellRegisterOffset);
  return OutboundDoorBellRegister.Read.HardwareMailboxStatusAvailable;
}

static inline
boolean DAC960_BA_MemoryMailboxStatusAvailableP(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_BA_OutboundDoorBellRegisterOffset);
  return OutboundDoorBellRegister.Read.MemoryMailboxStatusAvailable;
}

static inline
void DAC960_BA_EnableInterrupts(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All = 0xFF;
  InterruptMaskRegister.Bits.DisableInterrupts = false;
  InterruptMaskRegister.Bits.DisableInterruptsI2O = true;
  writeb(InterruptMaskRegister.All,
	 ControllerBaseAddress + DAC960_BA_InterruptMaskRegisterOffset);
}

static inline
void DAC960_BA_DisableInterrupts(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All = 0xFF;
  InterruptMaskRegister.Bits.DisableInterrupts = true;
  InterruptMaskRegister.Bits.DisableInterruptsI2O = true;
  writeb(InterruptMaskRegister.All,
	 ControllerBaseAddress + DAC960_BA_InterruptMaskRegisterOffset);
}

static inline
boolean DAC960_BA_InterruptsEnabledP(void __iomem *ControllerBaseAddress)
{
  DAC960_BA_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All =
    readb(ControllerBaseAddress + DAC960_BA_InterruptMaskRegisterOffset);
  return !InterruptMaskRegister.Bits.DisableInterrupts;
}

static inline
void DAC960_BA_WriteCommandMailbox(DAC960_V2_CommandMailbox_T
				     *MemoryCommandMailbox,
				   DAC960_V2_CommandMailbox_T
				     *CommandMailbox)
{
  memcpy(&MemoryCommandMailbox->Words[1], &CommandMailbox->Words[1],
	 sizeof(DAC960_V2_CommandMailbox_T) - sizeof(unsigned int));
  wmb();
  MemoryCommandMailbox->Words[0] = CommandMailbox->Words[0];
  mb();
}


static inline
void DAC960_BA_WriteHardwareMailbox(void __iomem *ControllerBaseAddress,
				    dma_addr_t CommandMailboxDMA)
{
	dma_addr_writeql(CommandMailboxDMA,
		ControllerBaseAddress +
		DAC960_BA_CommandMailboxBusAddressOffset);
}

static inline DAC960_V2_CommandIdentifier_T
DAC960_BA_ReadCommandIdentifier(void __iomem *ControllerBaseAddress)
{
  return readw(ControllerBaseAddress + DAC960_BA_CommandStatusOffset);
}

static inline DAC960_V2_CommandStatus_T
DAC960_BA_ReadCommandStatus(void __iomem *ControllerBaseAddress)
{
  return readw(ControllerBaseAddress + DAC960_BA_CommandStatusOffset + 2);
}

static inline boolean
DAC960_BA_ReadErrorStatus(void __iomem *ControllerBaseAddress,
			  unsigned char *ErrorStatus,
			  unsigned char *Parameter0,
			  unsigned char *Parameter1)
{
  DAC960_BA_ErrorStatusRegister_T ErrorStatusRegister;
  ErrorStatusRegister.All =
    readb(ControllerBaseAddress + DAC960_BA_ErrorStatusRegisterOffset);
  if (!ErrorStatusRegister.Bits.ErrorStatusPending) return false;
  ErrorStatusRegister.Bits.ErrorStatusPending = false;
  *ErrorStatus = ErrorStatusRegister.All;
  *Parameter0 =
    readb(ControllerBaseAddress + DAC960_BA_CommandMailboxBusAddressOffset + 0);
  *Parameter1 =
    readb(ControllerBaseAddress + DAC960_BA_CommandMailboxBusAddressOffset + 1);
  writeb(0xFF, ControllerBaseAddress + DAC960_BA_ErrorStatusRegisterOffset);
  return true;
}


/*
  Define the DAC960 LP Series Controller Interface Register Offsets.
*/

#define DAC960_LP_RegisterWindowSize		0x80

typedef enum
{
  DAC960_LP_InboundDoorBellRegisterOffset =	0x20,
  DAC960_LP_OutboundDoorBellRegisterOffset =	0x2C,
  DAC960_LP_InterruptStatusRegisterOffset =	0x30,
  DAC960_LP_InterruptMaskRegisterOffset =	0x34,
  DAC960_LP_CommandMailboxBusAddressOffset =	0x10,
  DAC960_LP_CommandStatusOffset =		0x18,
  DAC960_LP_ErrorStatusRegisterOffset =		0x2E
}
DAC960_LP_RegisterOffsets_T;


/*
  Define the structure of the DAC960 LP Series Inbound Door Bell Register.
*/

typedef union DAC960_LP_InboundDoorBellRegister
{
  unsigned char All;
  struct {
    boolean HardwareMailboxNewCommand:1;		/* Bit 0 */
    boolean AcknowledgeHardwareMailboxStatus:1;		/* Bit 1 */
    boolean GenerateInterrupt:1;			/* Bit 2 */
    boolean ControllerReset:1;				/* Bit 3 */
    boolean MemoryMailboxNewCommand:1;			/* Bit 4 */
    unsigned char :3;					/* Bits 5-7 */
  } Write;
  struct {
    boolean HardwareMailboxFull:1;			/* Bit 0 */
    boolean InitializationInProgress:1;			/* Bit 1 */
    unsigned char :6;					/* Bits 2-7 */
  } Read;
}
DAC960_LP_InboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 LP Series Outbound Door Bell Register.
*/

typedef union DAC960_LP_OutboundDoorBellRegister
{
  unsigned char All;
  struct {
    boolean AcknowledgeHardwareMailboxInterrupt:1;	/* Bit 0 */
    boolean AcknowledgeMemoryMailboxInterrupt:1;	/* Bit 1 */
    unsigned char :6;					/* Bits 2-7 */
  } Write;
  struct {
    boolean HardwareMailboxStatusAvailable:1;		/* Bit 0 */
    boolean MemoryMailboxStatusAvailable:1;		/* Bit 1 */
    unsigned char :6;					/* Bits 2-7 */
  } Read;
}
DAC960_LP_OutboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 LP Series Interrupt Mask Register.
*/

typedef union DAC960_LP_InterruptMaskRegister
{
  unsigned char All;
  struct {
    unsigned int :2;					/* Bits 0-1 */
    boolean DisableInterrupts:1;			/* Bit 2 */
    unsigned int :5;					/* Bits 3-7 */
  } Bits;
}
DAC960_LP_InterruptMaskRegister_T;


/*
  Define the structure of the DAC960 LP Series Error Status Register.
*/

typedef union DAC960_LP_ErrorStatusRegister
{
  unsigned char All;
  struct {
    unsigned int :2;					/* Bits 0-1 */
    boolean ErrorStatusPending:1;			/* Bit 2 */
    unsigned int :5;					/* Bits 3-7 */
  } Bits;
}
DAC960_LP_ErrorStatusRegister_T;


/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 LP Series Controller Interface Registers.
*/

static inline
void DAC960_LP_HardwareMailboxNewCommand(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.HardwareMailboxNewCommand = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LP_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LP_AcknowledgeHardwareMailboxStatus(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.AcknowledgeHardwareMailboxStatus = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LP_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LP_GenerateInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.GenerateInterrupt = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LP_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LP_ControllerReset(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.ControllerReset = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LP_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LP_MemoryMailboxNewCommand(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.MemoryMailboxNewCommand = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LP_InboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_LP_HardwareMailboxFullP(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_LP_InboundDoorBellRegisterOffset);
  return InboundDoorBellRegister.Read.HardwareMailboxFull;
}

static inline
boolean DAC960_LP_InitializationInProgressP(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_LP_InboundDoorBellRegisterOffset);
  return InboundDoorBellRegister.Read.InitializationInProgress;
}

static inline
void DAC960_LP_AcknowledgeHardwareMailboxInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
  writeb(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LP_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_LP_AcknowledgeMemoryMailboxInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
  writeb(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LP_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_LP_AcknowledgeInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
  OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
  writeb(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LP_OutboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_LP_HardwareMailboxStatusAvailableP(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_LP_OutboundDoorBellRegisterOffset);
  return OutboundDoorBellRegister.Read.HardwareMailboxStatusAvailable;
}

static inline
boolean DAC960_LP_MemoryMailboxStatusAvailableP(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_LP_OutboundDoorBellRegisterOffset);
  return OutboundDoorBellRegister.Read.MemoryMailboxStatusAvailable;
}

static inline
void DAC960_LP_EnableInterrupts(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All = 0xFF;
  InterruptMaskRegister.Bits.DisableInterrupts = false;
  writeb(InterruptMaskRegister.All,
	 ControllerBaseAddress + DAC960_LP_InterruptMaskRegisterOffset);
}

static inline
void DAC960_LP_DisableInterrupts(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All = 0xFF;
  InterruptMaskRegister.Bits.DisableInterrupts = true;
  writeb(InterruptMaskRegister.All,
	 ControllerBaseAddress + DAC960_LP_InterruptMaskRegisterOffset);
}

static inline
boolean DAC960_LP_InterruptsEnabledP(void __iomem *ControllerBaseAddress)
{
  DAC960_LP_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All =
    readb(ControllerBaseAddress + DAC960_LP_InterruptMaskRegisterOffset);
  return !InterruptMaskRegister.Bits.DisableInterrupts;
}

static inline
void DAC960_LP_WriteCommandMailbox(DAC960_V2_CommandMailbox_T
				     *MemoryCommandMailbox,
				   DAC960_V2_CommandMailbox_T
				     *CommandMailbox)
{
  memcpy(&MemoryCommandMailbox->Words[1], &CommandMailbox->Words[1],
	 sizeof(DAC960_V2_CommandMailbox_T) - sizeof(unsigned int));
  wmb();
  MemoryCommandMailbox->Words[0] = CommandMailbox->Words[0];
  mb();
}

static inline
void DAC960_LP_WriteHardwareMailbox(void __iomem *ControllerBaseAddress,
				    dma_addr_t CommandMailboxDMA)
{
	dma_addr_writeql(CommandMailboxDMA,
		ControllerBaseAddress +
		DAC960_LP_CommandMailboxBusAddressOffset);
}

static inline DAC960_V2_CommandIdentifier_T
DAC960_LP_ReadCommandIdentifier(void __iomem *ControllerBaseAddress)
{
  return readw(ControllerBaseAddress + DAC960_LP_CommandStatusOffset);
}

static inline DAC960_V2_CommandStatus_T
DAC960_LP_ReadCommandStatus(void __iomem *ControllerBaseAddress)
{
  return readw(ControllerBaseAddress + DAC960_LP_CommandStatusOffset + 2);
}

static inline boolean
DAC960_LP_ReadErrorStatus(void __iomem *ControllerBaseAddress,
			  unsigned char *ErrorStatus,
			  unsigned char *Parameter0,
			  unsigned char *Parameter1)
{
  DAC960_LP_ErrorStatusRegister_T ErrorStatusRegister;
  ErrorStatusRegister.All =
    readb(ControllerBaseAddress + DAC960_LP_ErrorStatusRegisterOffset);
  if (!ErrorStatusRegister.Bits.ErrorStatusPending) return false;
  ErrorStatusRegister.Bits.ErrorStatusPending = false;
  *ErrorStatus = ErrorStatusRegister.All;
  *Parameter0 =
    readb(ControllerBaseAddress + DAC960_LP_CommandMailboxBusAddressOffset + 0);
  *Parameter1 =
    readb(ControllerBaseAddress + DAC960_LP_CommandMailboxBusAddressOffset + 1);
  writeb(0xFF, ControllerBaseAddress + DAC960_LP_ErrorStatusRegisterOffset);
  return true;
}


/*
  Define the DAC960 LA Series Controller Interface Register Offsets.
*/

#define DAC960_LA_RegisterWindowSize		0x80

typedef enum
{
  DAC960_LA_InboundDoorBellRegisterOffset =	0x60,
  DAC960_LA_OutboundDoorBellRegisterOffset =	0x61,
  DAC960_LA_InterruptMaskRegisterOffset =	0x34,
  DAC960_LA_CommandOpcodeRegisterOffset =	0x50,
  DAC960_LA_CommandIdentifierRegisterOffset =	0x51,
  DAC960_LA_MailboxRegister2Offset =		0x52,
  DAC960_LA_MailboxRegister3Offset =		0x53,
  DAC960_LA_MailboxRegister4Offset =		0x54,
  DAC960_LA_MailboxRegister5Offset =		0x55,
  DAC960_LA_MailboxRegister6Offset =		0x56,
  DAC960_LA_MailboxRegister7Offset =		0x57,
  DAC960_LA_MailboxRegister8Offset =		0x58,
  DAC960_LA_MailboxRegister9Offset =		0x59,
  DAC960_LA_MailboxRegister10Offset =		0x5A,
  DAC960_LA_MailboxRegister11Offset =		0x5B,
  DAC960_LA_MailboxRegister12Offset =		0x5C,
  DAC960_LA_StatusCommandIdentifierRegOffset =	0x5D,
  DAC960_LA_StatusRegisterOffset =		0x5E,
  DAC960_LA_ErrorStatusRegisterOffset =		0x63
}
DAC960_LA_RegisterOffsets_T;


/*
  Define the structure of the DAC960 LA Series Inbound Door Bell Register.
*/

typedef union DAC960_LA_InboundDoorBellRegister
{
  unsigned char All;
  struct {
    boolean HardwareMailboxNewCommand:1;		/* Bit 0 */
    boolean AcknowledgeHardwareMailboxStatus:1;		/* Bit 1 */
    boolean GenerateInterrupt:1;			/* Bit 2 */
    boolean ControllerReset:1;				/* Bit 3 */
    boolean MemoryMailboxNewCommand:1;			/* Bit 4 */
    unsigned char :3;					/* Bits 5-7 */
  } Write;
  struct {
    boolean HardwareMailboxEmpty:1;			/* Bit 0 */
    boolean InitializationNotInProgress:1;		/* Bit 1 */
    unsigned char :6;					/* Bits 2-7 */
  } Read;
}
DAC960_LA_InboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 LA Series Outbound Door Bell Register.
*/

typedef union DAC960_LA_OutboundDoorBellRegister
{
  unsigned char All;
  struct {
    boolean AcknowledgeHardwareMailboxInterrupt:1;	/* Bit 0 */
    boolean AcknowledgeMemoryMailboxInterrupt:1;	/* Bit 1 */
    unsigned char :6;					/* Bits 2-7 */
  } Write;
  struct {
    boolean HardwareMailboxStatusAvailable:1;		/* Bit 0 */
    boolean MemoryMailboxStatusAvailable:1;		/* Bit 1 */
    unsigned char :6;					/* Bits 2-7 */
  } Read;
}
DAC960_LA_OutboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 LA Series Interrupt Mask Register.
*/

typedef union DAC960_LA_InterruptMaskRegister
{
  unsigned char All;
  struct {
    unsigned char :2;					/* Bits 0-1 */
    boolean DisableInterrupts:1;			/* Bit 2 */
    unsigned char :5;					/* Bits 3-7 */
  } Bits;
}
DAC960_LA_InterruptMaskRegister_T;


/*
  Define the structure of the DAC960 LA Series Error Status Register.
*/

typedef union DAC960_LA_ErrorStatusRegister
{
  unsigned char All;
  struct {
    unsigned int :2;					/* Bits 0-1 */
    boolean ErrorStatusPending:1;			/* Bit 2 */
    unsigned int :5;					/* Bits 3-7 */
  } Bits;
}
DAC960_LA_ErrorStatusRegister_T;


/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 LA Series Controller Interface Registers.
*/

static inline
void DAC960_LA_HardwareMailboxNewCommand(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.HardwareMailboxNewCommand = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LA_AcknowledgeHardwareMailboxStatus(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.AcknowledgeHardwareMailboxStatus = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LA_GenerateInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.GenerateInterrupt = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LA_ControllerReset(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.ControllerReset = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LA_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_LA_MemoryMailboxNewCommand(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.MemoryMailboxNewCommand = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LA_InboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_LA_HardwareMailboxFullP(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_LA_InboundDoorBellRegisterOffset);
  return !InboundDoorBellRegister.Read.HardwareMailboxEmpty;
}

static inline
boolean DAC960_LA_InitializationInProgressP(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_LA_InboundDoorBellRegisterOffset);
  return !InboundDoorBellRegister.Read.InitializationNotInProgress;
}

static inline
void DAC960_LA_AcknowledgeHardwareMailboxInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
  writeb(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LA_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_LA_AcknowledgeMemoryMailboxInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
  writeb(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LA_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_LA_AcknowledgeInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
  OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
  writeb(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_LA_OutboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_LA_HardwareMailboxStatusAvailableP(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_LA_OutboundDoorBellRegisterOffset);
  return OutboundDoorBellRegister.Read.HardwareMailboxStatusAvailable;
}

static inline
boolean DAC960_LA_MemoryMailboxStatusAvailableP(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_LA_OutboundDoorBellRegisterOffset);
  return OutboundDoorBellRegister.Read.MemoryMailboxStatusAvailable;
}

static inline
void DAC960_LA_EnableInterrupts(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All = 0xFF;
  InterruptMaskRegister.Bits.DisableInterrupts = false;
  writeb(InterruptMaskRegister.All,
	 ControllerBaseAddress + DAC960_LA_InterruptMaskRegisterOffset);
}

static inline
void DAC960_LA_DisableInterrupts(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All = 0xFF;
  InterruptMaskRegister.Bits.DisableInterrupts = true;
  writeb(InterruptMaskRegister.All,
	 ControllerBaseAddress + DAC960_LA_InterruptMaskRegisterOffset);
}

static inline
boolean DAC960_LA_InterruptsEnabledP(void __iomem *ControllerBaseAddress)
{
  DAC960_LA_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All =
    readb(ControllerBaseAddress + DAC960_LA_InterruptMaskRegisterOffset);
  return !InterruptMaskRegister.Bits.DisableInterrupts;
}

static inline
void DAC960_LA_WriteCommandMailbox(DAC960_V1_CommandMailbox_T
				     *MemoryCommandMailbox,
				   DAC960_V1_CommandMailbox_T
				     *CommandMailbox)
{
  MemoryCommandMailbox->Words[1] = CommandMailbox->Words[1];
  MemoryCommandMailbox->Words[2] = CommandMailbox->Words[2];
  MemoryCommandMailbox->Words[3] = CommandMailbox->Words[3];
  wmb();
  MemoryCommandMailbox->Words[0] = CommandMailbox->Words[0];
  mb();
}

static inline
void DAC960_LA_WriteHardwareMailbox(void __iomem *ControllerBaseAddress,
				    DAC960_V1_CommandMailbox_T *CommandMailbox)
{
  writel(CommandMailbox->Words[0],
	 ControllerBaseAddress + DAC960_LA_CommandOpcodeRegisterOffset);
  writel(CommandMailbox->Words[1],
	 ControllerBaseAddress + DAC960_LA_MailboxRegister4Offset);
  writel(CommandMailbox->Words[2],
	 ControllerBaseAddress + DAC960_LA_MailboxRegister8Offset);
  writeb(CommandMailbox->Bytes[12],
	 ControllerBaseAddress + DAC960_LA_MailboxRegister12Offset);
}

static inline DAC960_V1_CommandIdentifier_T
DAC960_LA_ReadStatusCommandIdentifier(void __iomem *ControllerBaseAddress)
{
  return readb(ControllerBaseAddress
	       + DAC960_LA_StatusCommandIdentifierRegOffset);
}

static inline DAC960_V1_CommandStatus_T
DAC960_LA_ReadStatusRegister(void __iomem *ControllerBaseAddress)
{
  return readw(ControllerBaseAddress + DAC960_LA_StatusRegisterOffset);
}

static inline boolean
DAC960_LA_ReadErrorStatus(void __iomem *ControllerBaseAddress,
			  unsigned char *ErrorStatus,
			  unsigned char *Parameter0,
			  unsigned char *Parameter1)
{
  DAC960_LA_ErrorStatusRegister_T ErrorStatusRegister;
  ErrorStatusRegister.All =
    readb(ControllerBaseAddress + DAC960_LA_ErrorStatusRegisterOffset);
  if (!ErrorStatusRegister.Bits.ErrorStatusPending) return false;
  ErrorStatusRegister.Bits.ErrorStatusPending = false;
  *ErrorStatus = ErrorStatusRegister.All;
  *Parameter0 =
    readb(ControllerBaseAddress + DAC960_LA_CommandOpcodeRegisterOffset);
  *Parameter1 =
    readb(ControllerBaseAddress + DAC960_LA_CommandIdentifierRegisterOffset);
  writeb(0xFF, ControllerBaseAddress + DAC960_LA_ErrorStatusRegisterOffset);
  return true;
}

/*
  Define the DAC960 PG Series Controller Interface Register Offsets.
*/

#define DAC960_PG_RegisterWindowSize		0x2000

typedef enum
{
  DAC960_PG_InboundDoorBellRegisterOffset =	0x0020,
  DAC960_PG_OutboundDoorBellRegisterOffset =	0x002C,
  DAC960_PG_InterruptMaskRegisterOffset =	0x0034,
  DAC960_PG_CommandOpcodeRegisterOffset =	0x1000,
  DAC960_PG_CommandIdentifierRegisterOffset =	0x1001,
  DAC960_PG_MailboxRegister2Offset =		0x1002,
  DAC960_PG_MailboxRegister3Offset =		0x1003,
  DAC960_PG_MailboxRegister4Offset =		0x1004,
  DAC960_PG_MailboxRegister5Offset =		0x1005,
  DAC960_PG_MailboxRegister6Offset =		0x1006,
  DAC960_PG_MailboxRegister7Offset =		0x1007,
  DAC960_PG_MailboxRegister8Offset =		0x1008,
  DAC960_PG_MailboxRegister9Offset =		0x1009,
  DAC960_PG_MailboxRegister10Offset =		0x100A,
  DAC960_PG_MailboxRegister11Offset =		0x100B,
  DAC960_PG_MailboxRegister12Offset =		0x100C,
  DAC960_PG_StatusCommandIdentifierRegOffset =	0x1018,
  DAC960_PG_StatusRegisterOffset =		0x101A,
  DAC960_PG_ErrorStatusRegisterOffset =		0x103F
}
DAC960_PG_RegisterOffsets_T;


/*
  Define the structure of the DAC960 PG Series Inbound Door Bell Register.
*/

typedef union DAC960_PG_InboundDoorBellRegister
{
  unsigned int All;
  struct {
    boolean HardwareMailboxNewCommand:1;		/* Bit 0 */
    boolean AcknowledgeHardwareMailboxStatus:1;		/* Bit 1 */
    boolean GenerateInterrupt:1;			/* Bit 2 */
    boolean ControllerReset:1;				/* Bit 3 */
    boolean MemoryMailboxNewCommand:1;			/* Bit 4 */
    unsigned int :27;					/* Bits 5-31 */
  } Write;
  struct {
    boolean HardwareMailboxFull:1;			/* Bit 0 */
    boolean InitializationInProgress:1;			/* Bit 1 */
    unsigned int :30;					/* Bits 2-31 */
  } Read;
}
DAC960_PG_InboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 PG Series Outbound Door Bell Register.
*/

typedef union DAC960_PG_OutboundDoorBellRegister
{
  unsigned int All;
  struct {
    boolean AcknowledgeHardwareMailboxInterrupt:1;	/* Bit 0 */
    boolean AcknowledgeMemoryMailboxInterrupt:1;	/* Bit 1 */
    unsigned int :30;					/* Bits 2-31 */
  } Write;
  struct {
    boolean HardwareMailboxStatusAvailable:1;		/* Bit 0 */
    boolean MemoryMailboxStatusAvailable:1;		/* Bit 1 */
    unsigned int :30;					/* Bits 2-31 */
  } Read;
}
DAC960_PG_OutboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 PG Series Interrupt Mask Register.
*/

typedef union DAC960_PG_InterruptMaskRegister
{
  unsigned int All;
  struct {
    unsigned int MessageUnitInterruptMask1:2;		/* Bits 0-1 */
    boolean DisableInterrupts:1;			/* Bit 2 */
    unsigned int MessageUnitInterruptMask2:5;		/* Bits 3-7 */
    unsigned int Reserved0:24;				/* Bits 8-31 */
  } Bits;
}
DAC960_PG_InterruptMaskRegister_T;


/*
  Define the structure of the DAC960 PG Series Error Status Register.
*/

typedef union DAC960_PG_ErrorStatusRegister
{
  unsigned char All;
  struct {
    unsigned int :2;					/* Bits 0-1 */
    boolean ErrorStatusPending:1;			/* Bit 2 */
    unsigned int :5;					/* Bits 3-7 */
  } Bits;
}
DAC960_PG_ErrorStatusRegister_T;


/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 PG Series Controller Interface Registers.
*/

static inline
void DAC960_PG_HardwareMailboxNewCommand(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.HardwareMailboxNewCommand = true;
  writel(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_PG_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PG_AcknowledgeHardwareMailboxStatus(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.AcknowledgeHardwareMailboxStatus = true;
  writel(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_PG_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PG_GenerateInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.GenerateInterrupt = true;
  writel(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_PG_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PG_ControllerReset(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.ControllerReset = true;
  writel(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_PG_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PG_MemoryMailboxNewCommand(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.MemoryMailboxNewCommand = true;
  writel(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_PG_InboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_PG_HardwareMailboxFullP(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readl(ControllerBaseAddress + DAC960_PG_InboundDoorBellRegisterOffset);
  return InboundDoorBellRegister.Read.HardwareMailboxFull;
}

static inline
boolean DAC960_PG_InitializationInProgressP(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readl(ControllerBaseAddress + DAC960_PG_InboundDoorBellRegisterOffset);
  return InboundDoorBellRegister.Read.InitializationInProgress;
}

static inline
void DAC960_PG_AcknowledgeHardwareMailboxInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
  writel(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_PG_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_PG_AcknowledgeMemoryMailboxInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
  writel(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_PG_OutboundDoorBellRegisterOffset);
}

static inline
void DAC960_PG_AcknowledgeInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeHardwareMailboxInterrupt = true;
  OutboundDoorBellRegister.Write.AcknowledgeMemoryMailboxInterrupt = true;
  writel(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_PG_OutboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_PG_HardwareMailboxStatusAvailableP(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All =
    readl(ControllerBaseAddress + DAC960_PG_OutboundDoorBellRegisterOffset);
  return OutboundDoorBellRegister.Read.HardwareMailboxStatusAvailable;
}

static inline
boolean DAC960_PG_MemoryMailboxStatusAvailableP(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All =
    readl(ControllerBaseAddress + DAC960_PG_OutboundDoorBellRegisterOffset);
  return OutboundDoorBellRegister.Read.MemoryMailboxStatusAvailable;
}

static inline
void DAC960_PG_EnableInterrupts(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All = 0;
  InterruptMaskRegister.Bits.MessageUnitInterruptMask1 = 0x3;
  InterruptMaskRegister.Bits.DisableInterrupts = false;
  InterruptMaskRegister.Bits.MessageUnitInterruptMask2 = 0x1F;
  writel(InterruptMaskRegister.All,
	 ControllerBaseAddress + DAC960_PG_InterruptMaskRegisterOffset);
}

static inline
void DAC960_PG_DisableInterrupts(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All = 0;
  InterruptMaskRegister.Bits.MessageUnitInterruptMask1 = 0x3;
  InterruptMaskRegister.Bits.DisableInterrupts = true;
  InterruptMaskRegister.Bits.MessageUnitInterruptMask2 = 0x1F;
  writel(InterruptMaskRegister.All,
	 ControllerBaseAddress + DAC960_PG_InterruptMaskRegisterOffset);
}

static inline
boolean DAC960_PG_InterruptsEnabledP(void __iomem *ControllerBaseAddress)
{
  DAC960_PG_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All =
    readl(ControllerBaseAddress + DAC960_PG_InterruptMaskRegisterOffset);
  return !InterruptMaskRegister.Bits.DisableInterrupts;
}

static inline
void DAC960_PG_WriteCommandMailbox(DAC960_V1_CommandMailbox_T
				     *MemoryCommandMailbox,
				   DAC960_V1_CommandMailbox_T
				     *CommandMailbox)
{
  MemoryCommandMailbox->Words[1] = CommandMailbox->Words[1];
  MemoryCommandMailbox->Words[2] = CommandMailbox->Words[2];
  MemoryCommandMailbox->Words[3] = CommandMailbox->Words[3];
  wmb();
  MemoryCommandMailbox->Words[0] = CommandMailbox->Words[0];
  mb();
}

static inline
void DAC960_PG_WriteHardwareMailbox(void __iomem *ControllerBaseAddress,
				    DAC960_V1_CommandMailbox_T *CommandMailbox)
{
  writel(CommandMailbox->Words[0],
	 ControllerBaseAddress + DAC960_PG_CommandOpcodeRegisterOffset);
  writel(CommandMailbox->Words[1],
	 ControllerBaseAddress + DAC960_PG_MailboxRegister4Offset);
  writel(CommandMailbox->Words[2],
	 ControllerBaseAddress + DAC960_PG_MailboxRegister8Offset);
  writeb(CommandMailbox->Bytes[12],
	 ControllerBaseAddress + DAC960_PG_MailboxRegister12Offset);
}

static inline DAC960_V1_CommandIdentifier_T
DAC960_PG_ReadStatusCommandIdentifier(void __iomem *ControllerBaseAddress)
{
  return readb(ControllerBaseAddress
	       + DAC960_PG_StatusCommandIdentifierRegOffset);
}

static inline DAC960_V1_CommandStatus_T
DAC960_PG_ReadStatusRegister(void __iomem *ControllerBaseAddress)
{
  return readw(ControllerBaseAddress + DAC960_PG_StatusRegisterOffset);
}

static inline boolean
DAC960_PG_ReadErrorStatus(void __iomem *ControllerBaseAddress,
			  unsigned char *ErrorStatus,
			  unsigned char *Parameter0,
			  unsigned char *Parameter1)
{
  DAC960_PG_ErrorStatusRegister_T ErrorStatusRegister;
  ErrorStatusRegister.All =
    readb(ControllerBaseAddress + DAC960_PG_ErrorStatusRegisterOffset);
  if (!ErrorStatusRegister.Bits.ErrorStatusPending) return false;
  ErrorStatusRegister.Bits.ErrorStatusPending = false;
  *ErrorStatus = ErrorStatusRegister.All;
  *Parameter0 =
    readb(ControllerBaseAddress + DAC960_PG_CommandOpcodeRegisterOffset);
  *Parameter1 =
    readb(ControllerBaseAddress + DAC960_PG_CommandIdentifierRegisterOffset);
  writeb(0, ControllerBaseAddress + DAC960_PG_ErrorStatusRegisterOffset);
  return true;
}

/*
  Define the DAC960 PD Series Controller Interface Register Offsets.
*/

#define DAC960_PD_RegisterWindowSize		0x80

typedef enum
{
  DAC960_PD_CommandOpcodeRegisterOffset =	0x00,
  DAC960_PD_CommandIdentifierRegisterOffset =	0x01,
  DAC960_PD_MailboxRegister2Offset =		0x02,
  DAC960_PD_MailboxRegister3Offset =		0x03,
  DAC960_PD_MailboxRegister4Offset =		0x04,
  DAC960_PD_MailboxRegister5Offset =		0x05,
  DAC960_PD_MailboxRegister6Offset =		0x06,
  DAC960_PD_MailboxRegister7Offset =		0x07,
  DAC960_PD_MailboxRegister8Offset =		0x08,
  DAC960_PD_MailboxRegister9Offset =		0x09,
  DAC960_PD_MailboxRegister10Offset =		0x0A,
  DAC960_PD_MailboxRegister11Offset =		0x0B,
  DAC960_PD_MailboxRegister12Offset =		0x0C,
  DAC960_PD_StatusCommandIdentifierRegOffset =	0x0D,
  DAC960_PD_StatusRegisterOffset =		0x0E,
  DAC960_PD_ErrorStatusRegisterOffset =		0x3F,
  DAC960_PD_InboundDoorBellRegisterOffset =	0x40,
  DAC960_PD_OutboundDoorBellRegisterOffset =	0x41,
  DAC960_PD_InterruptEnableRegisterOffset =	0x43
}
DAC960_PD_RegisterOffsets_T;


/*
  Define the structure of the DAC960 PD Series Inbound Door Bell Register.
*/

typedef union DAC960_PD_InboundDoorBellRegister
{
  unsigned char All;
  struct {
    boolean NewCommand:1;				/* Bit 0 */
    boolean AcknowledgeStatus:1;			/* Bit 1 */
    boolean GenerateInterrupt:1;			/* Bit 2 */
    boolean ControllerReset:1;				/* Bit 3 */
    unsigned char :4;					/* Bits 4-7 */
  } Write;
  struct {
    boolean MailboxFull:1;				/* Bit 0 */
    boolean InitializationInProgress:1;			/* Bit 1 */
    unsigned char :6;					/* Bits 2-7 */
  } Read;
}
DAC960_PD_InboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 PD Series Outbound Door Bell Register.
*/

typedef union DAC960_PD_OutboundDoorBellRegister
{
  unsigned char All;
  struct {
    boolean AcknowledgeInterrupt:1;			/* Bit 0 */
    unsigned char :7;					/* Bits 1-7 */
  } Write;
  struct {
    boolean StatusAvailable:1;				/* Bit 0 */
    unsigned char :7;					/* Bits 1-7 */
  } Read;
}
DAC960_PD_OutboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 PD Series Interrupt Enable Register.
*/

typedef union DAC960_PD_InterruptEnableRegister
{
  unsigned char All;
  struct {
    boolean EnableInterrupts:1;				/* Bit 0 */
    unsigned char :7;					/* Bits 1-7 */
  } Bits;
}
DAC960_PD_InterruptEnableRegister_T;


/*
  Define the structure of the DAC960 PD Series Error Status Register.
*/

typedef union DAC960_PD_ErrorStatusRegister
{
  unsigned char All;
  struct {
    unsigned int :2;					/* Bits 0-1 */
    boolean ErrorStatusPending:1;			/* Bit 2 */
    unsigned int :5;					/* Bits 3-7 */
  } Bits;
}
DAC960_PD_ErrorStatusRegister_T;


/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 PD Series Controller Interface Registers.
*/

static inline
void DAC960_PD_NewCommand(void __iomem *ControllerBaseAddress)
{
  DAC960_PD_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.NewCommand = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_PD_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PD_AcknowledgeStatus(void __iomem *ControllerBaseAddress)
{
  DAC960_PD_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.AcknowledgeStatus = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_PD_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PD_GenerateInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_PD_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.GenerateInterrupt = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_PD_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_PD_ControllerReset(void __iomem *ControllerBaseAddress)
{
  DAC960_PD_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.ControllerReset = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_PD_InboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_PD_MailboxFullP(void __iomem *ControllerBaseAddress)
{
  DAC960_PD_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_PD_InboundDoorBellRegisterOffset);
  return InboundDoorBellRegister.Read.MailboxFull;
}

static inline
boolean DAC960_PD_InitializationInProgressP(void __iomem *ControllerBaseAddress)
{
  DAC960_PD_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_PD_InboundDoorBellRegisterOffset);
  return InboundDoorBellRegister.Read.InitializationInProgress;
}

static inline
void DAC960_PD_AcknowledgeInterrupt(void __iomem *ControllerBaseAddress)
{
  DAC960_PD_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeInterrupt = true;
  writeb(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_PD_OutboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_PD_StatusAvailableP(void __iomem *ControllerBaseAddress)
{
  DAC960_PD_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_PD_OutboundDoorBellRegisterOffset);
  return OutboundDoorBellRegister.Read.StatusAvailable;
}

static inline
void DAC960_PD_EnableInterrupts(void __iomem *ControllerBaseAddress)
{
  DAC960_PD_InterruptEnableRegister_T InterruptEnableRegister;
  InterruptEnableRegister.All = 0;
  InterruptEnableRegister.Bits.EnableInterrupts = true;
  writeb(InterruptEnableRegister.All,
	 ControllerBaseAddress + DAC960_PD_InterruptEnableRegisterOffset);
}

static inline
void DAC960_PD_DisableInterrupts(void __iomem *ControllerBaseAddress)
{
  DAC960_PD_InterruptEnableRegister_T InterruptEnableRegister;
  InterruptEnableRegister.All = 0;
  InterruptEnableRegister.Bits.EnableInterrupts = false;
  writeb(InterruptEnableRegister.All,
	 ControllerBaseAddress + DAC960_PD_InterruptEnableRegisterOffset);
}

static inline
boolean DAC960_PD_InterruptsEnabledP(void __iomem *ControllerBaseAddress)
{
  DAC960_PD_InterruptEnableRegister_T InterruptEnableRegister;
  InterruptEnableRegister.All =
    readb(ControllerBaseAddress + DAC960_PD_InterruptEnableRegisterOffset);
  return InterruptEnableRegister.Bits.EnableInterrupts;
}

static inline
void DAC960_PD_WriteCommandMailbox(void __iomem *ControllerBaseAddress,
				   DAC960_V1_CommandMailbox_T *CommandMailbox)
{
  writel(CommandMailbox->Words[0],
	 ControllerBaseAddress + DAC960_PD_CommandOpcodeRegisterOffset);
  writel(CommandMailbox->Words[1],
	 ControllerBaseAddress + DAC960_PD_MailboxRegister4Offset);
  writel(CommandMailbox->Words[2],
	 ControllerBaseAddress + DAC960_PD_MailboxRegister8Offset);
  writeb(CommandMailbox->Bytes[12],
	 ControllerBaseAddress + DAC960_PD_MailboxRegister12Offset);
}

static inline DAC960_V1_CommandIdentifier_T
DAC960_PD_ReadStatusCommandIdentifier(void __iomem *ControllerBaseAddress)
{
  return readb(ControllerBaseAddress
	       + DAC960_PD_StatusCommandIdentifierRegOffset);
}

static inline DAC960_V1_CommandStatus_T
DAC960_PD_ReadStatusRegister(void __iomem *ControllerBaseAddress)
{
  return readw(ControllerBaseAddress + DAC960_PD_StatusRegisterOffset);
}

static inline boolean
DAC960_PD_ReadErrorStatus(void __iomem *ControllerBaseAddress,
			  unsigned char *ErrorStatus,
			  unsigned char *Parameter0,
			  unsigned char *Parameter1)
{
  DAC960_PD_ErrorStatusRegister_T ErrorStatusRegister;
  ErrorStatusRegister.All =
    readb(ControllerBaseAddress + DAC960_PD_ErrorStatusRegisterOffset);
  if (!ErrorStatusRegister.Bits.ErrorStatusPending) return false;
  ErrorStatusRegister.Bits.ErrorStatusPending = false;
  *ErrorStatus = ErrorStatusRegister.All;
  *Parameter0 =
    readb(ControllerBaseAddress + DAC960_PD_CommandOpcodeRegisterOffset);
  *Parameter1 =
    readb(ControllerBaseAddress + DAC960_PD_CommandIdentifierRegisterOffset);
  writeb(0, ControllerBaseAddress + DAC960_PD_ErrorStatusRegisterOffset);
  return true;
}

static inline void DAC960_P_To_PD_TranslateEnquiry(void *Enquiry)
{
  memcpy(Enquiry + 132, Enquiry + 36, 64);
  memset(Enquiry + 36, 0, 96);
}

static inline void DAC960_P_To_PD_TranslateDeviceState(void *DeviceState)
{
  memcpy(DeviceState + 2, DeviceState + 3, 1);
  memcpy(DeviceState + 4, DeviceState + 5, 2);
  memcpy(DeviceState + 6, DeviceState + 8, 4);
}

static inline
void DAC960_PD_To_P_TranslateReadWriteCommand(DAC960_V1_CommandMailbox_T
					      *CommandMailbox)
{
  int LogicalDriveNumber = CommandMailbox->Type5.LD.LogicalDriveNumber;
  CommandMailbox->Bytes[3] &= 0x7;
  CommandMailbox->Bytes[3] |= CommandMailbox->Bytes[7] << 6;
  CommandMailbox->Bytes[7] = LogicalDriveNumber;
}

static inline
void DAC960_P_To_PD_TranslateReadWriteCommand(DAC960_V1_CommandMailbox_T
					      *CommandMailbox)
{
  int LogicalDriveNumber = CommandMailbox->Bytes[7];
  CommandMailbox->Bytes[7] = CommandMailbox->Bytes[3] >> 6;
  CommandMailbox->Bytes[3] &= 0x7;
  CommandMailbox->Bytes[3] |= LogicalDriveNumber << 3;
}


/*
  Define prototypes for the forward referenced DAC960 Driver Internal Functions.
*/

static void DAC960_FinalizeController(DAC960_Controller_T *);
static void DAC960_V1_QueueReadWriteCommand(DAC960_Command_T *);
static void DAC960_V2_QueueReadWriteCommand(DAC960_Command_T *); 
static void DAC960_RequestFunction(struct request_queue *);
static irqreturn_t DAC960_BA_InterruptHandler(int, void *, struct pt_regs *);
static irqreturn_t DAC960_LP_InterruptHandler(int, void *, struct pt_regs *);
static irqreturn_t DAC960_LA_InterruptHandler(int, void *, struct pt_regs *);
static irqreturn_t DAC960_PG_InterruptHandler(int, void *, struct pt_regs *);
static irqreturn_t DAC960_PD_InterruptHandler(int, void *, struct pt_regs *);
static irqreturn_t DAC960_P_InterruptHandler(int, void *, struct pt_regs *);
static void DAC960_V1_QueueMonitoringCommand(DAC960_Command_T *);
static void DAC960_V2_QueueMonitoringCommand(DAC960_Command_T *);
static void DAC960_MonitoringTimerFunction(unsigned long);
static void DAC960_Message(DAC960_MessageLevel_T, unsigned char *,
			   DAC960_Controller_T *, ...);
static void DAC960_CreateProcEntries(DAC960_Controller_T *);
static void DAC960_DestroyProcEntries(DAC960_Controller_T *);

#endif /* DAC960_DriverVersion */
