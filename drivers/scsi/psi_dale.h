/****************************************************************************
 * Perceptive Solutions, Inc. PCI-2220I device driver for Linux.
 *
 * psi_dalei.h - Linux Host Driver for PCI-2220i EIDE Adapters
 *
 * Copyright (c) 1997-1999 Perceptive Solutions, Inc.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain the above copyright notice and this comment without
 * modification.
 *
 * Technical updates and product information at:
 *  http://www.psidisk.com
 *
 * Please send questions, comments, bug reports to:
 *  tech@psidisk.com Technical Support
 *
 ****************************************************************************/

/************************************************/
/*		Some defines that we like 				*/
/************************************************/
#define	CHAR		char
#define	UCHAR		unsigned char
#define	SHORT		short
#define	USHORT		unsigned short
#define	BOOL		unsigned short
#define	LONG		long
#define	ULONG		unsigned long
#define	VOID		void

/************************************************/
/*		Dale PCI setup							*/
/************************************************/
#define	VENDOR_PSI			0x1256
#define	DEVICE_DALE_1		0x4401		/* 'D1' */
#define	DEVICE_BIGD_1		0x4201		/* 'B1' */
#define	DEVICE_BIGD_2		0x4202		/* 'B2' */

/************************************************/
/*		Misc konstants							*/
/************************************************/
#define	DALE_MAXDRIVES			4
#define	BIGD_MAXDRIVES			8
#define	SECTORSXFER				8
#define	ATAPI_TRANSFER			8192
#define	BYTES_PER_SECTOR		512
#define	DEFAULT_TIMING_MODE		5

/************************************************/
/*		EEPROM locations						*/
/************************************************/
#define	DALE_FLASH_PAGE_SIZE	128				// number of bytes per page
#define	DALE_FLASH_SIZE			65536L

#define	DALE_FLASH_BIOS			0x00080000L		// BIOS base address
#define	DALE_FLASH_SETUP		0x00088000L		// SETUP PROGRAM base address offset from BIOS
#define	DALE_FLASH_RAID			0x00088400L		// RAID signature storage
#define	DALE_FLASH_FACTORY		0x00089000L		// FACTORY data base address offset from BIOS

#define	DALE_FLASH_BIOS_SIZE	32768U			// size of FLASH BIOS REGION

/************************************************/
/*		DALE Register address offsets			*/
/************************************************/
#define	REG_DATA					0x80
#define	REG_ERROR					0x84
#define	REG_SECTOR_COUNT			0x88
#define	REG_LBA_0					0x8C
#define	REG_LBA_8					0x90
#define	REG_LBA_16					0x94
#define	REG_LBA_24					0x98
#define	REG_STAT_CMD				0x9C
#define	REG_STAT_SEL				0xA0
#define	REG_FAIL					0xB0
#define	REG_ALT_STAT				0xB8
#define	REG_DRIVE_ADRS				0xBC

#define	DALE_DATA_SLOW				0x00040000L
#define	DALE_DATA_MODE2				0x00040000L
#define	DALE_DATA_MODE3				0x00050000L
#define	DALE_DATA_MODE4				0x00060000L
#define	DALE_DATA_MODE5				0x00070000L

#define	BIGD_DATA_SLOW				0x00000000L
#define	BIGD_DATA_MODE0				0x00000000L
#define	BIGD_DATA_MODE2				0x00000000L
#define	BIGD_DATA_MODE3				0x00000008L
#define	BIGD_DATA_MODE4				0x00000010L
#define	BIGD_DATA_MODE5				0x00000020L

#define RTR_LOCAL_RANGE				0x000
#define RTR_LOCAL_REMAP				0x004
#define RTR_EXP_RANGE				0x010
#define RTR_EXP_REMAP				0x014
#define RTR_REGIONS					0x018
#define RTR_DM_MASK					0x01C
#define RTR_DM_LOCAL_BASE			0x020
#define RTR_DM_IO_BASE				0x024
#define RTR_DM_PCI_REMAP			0x028
#define RTR_DM_IO_CONFIG			0x02C
#define RTR_MAILBOX					0x040
#define RTR_LOCAL_DOORBELL			0x060
#define RTR_PCI_DOORBELL			0x064
#define RTR_INT_CONTROL_STATUS 		0x068
#define RTR_EEPROM_CONTROL_STATUS	0x06C

#define RTR_DMA0_MODE				0x0080
#define RTR_DMA0_PCI_ADDR			0x0084
#define RTR_DMA0_LOCAL_ADDR			0x0088
#define RTR_DMA0_COUNT				0x008C
#define RTR_DMA0_DESC_PTR			0x0090
#define RTR_DMA1_MODE				0x0094
#define RTR_DMA1_PCI_ADDR			0x0098
#define RTR_DMA1_LOCAL_ADDR			0x009C
#define RTR_DMA1_COUNT				0x00A0
#define RTR_DMA1_DESC_PTR			0x00A4
#define RTR_DMA_COMMAND_STATUS		0x00A8
#define RTR_DMA_ARB0				0x00AC
#define RTR_DMA_ARB1				0x00B0

#define RTL_DMA0_MODE				0x00
#define RTL_DMA0_PCI_ADDR			0x04
#define RTL_DMA0_LOCAL_ADDR			0x08
#define RTL_DMA0_COUNT				0x0C
#define RTL_DMA0_DESC_PTR			0x10
#define RTL_DMA1_MODE				0x14
#define RTL_DMA1_PCI_ADDR			0x18
#define RTL_DMA1_LOCAL_ADDR			0x1C
#define RTL_DMA1_COUNT				0x20
#define RTL_DMA1_DESC_PTR			0x24
#define RTL_DMA_COMMAND_STATUS		0x28
#define RTL_DMA_ARB0				0x2C
#define RTL_DMA_ARB1				0x30

/************************************************/
/*		Dale Scratchpad locations				*/
/************************************************/
#define	DALE_CHANNEL_DEVICE_0		0		// device channel locations
#define	DALE_CHANNEL_DEVICE_1		1
#define	DALE_CHANNEL_DEVICE_2		2
#define	DALE_CHANNEL_DEVICE_3		3

#define	DALE_SCRATCH_DEVICE_0		4		// device type codes
#define	DALE_SCRATCH_DEVICE_1		5
#define DALE_SCRATCH_DEVICE_2		6
#define	DALE_SCRATCH_DEVICE_3		7

#define	DALE_RAID_0_STATUS			8
#define DALE_RAID_1_STATUS			9

#define	DALE_TIMING_MODE			12		// bus master timing mode (2, 3, 4, 5)
#define	DALE_NUM_DRIVES				13		// number of addressable drives on this board
#define	DALE_RAID_ON				14 		// RAID status On
#define	DALE_LAST_ERROR				15		// Last error code from BIOS

/************************************************/
/*		BigD Scratchpad locations				*/
/************************************************/
#define	BIGD_DEVICE_0			0		// device channel locations
#define	BIGD_DEVICE_1			1
#define	BIGD_DEVICE_2			2
#define	BIGD_DEVICE_3			3

#define	BIGD_DEVICE_4			4		// device type codes
#define	BIGD_DEVICE_5			5
#define BIGD_DEVICE_6			6
#define	BIGD_DEVICE_7			7

#define	BIGD_ALARM_IMAGE		11		// ~image of alarm fail register		
#define	BIGD_TIMING_MODE		12		// bus master timing mode (2, 3, 4, 5)
#define	BIGD_NUM_DRIVES			13		// number of addressable drives on this board
#define	BIGD_RAID_ON			14 		// RAID status is on for the whole board
#define	BIGD_LAST_ERROR			15		// Last error code from BIOS

#define	BIGD_RAID_0_STATUS		16
#define BIGD_RAID_1_STATUS		17
#define	BIGD_RAID_2_STATUS		18
#define	BIGD_RAID_3_STATUS		19
#define	BIGD_RAID_4_STATUS		20
#define BIGD_RAID_5_STATUS		21
#define	BIGD_RAID_6_STATUS		22
#define	BIGD_RAID_7_STATUS		23

/************************************************/
/*		Dale cable select bits					*/
/************************************************/
#define	SEL_NONE					0x00
#define	SEL_1						0x01
#define	SEL_2						0x02
#define	SEL_3						0x04
#define	SEL_4						0x08
#define	SEL_NEW_SPEED_1				0x20
#define	SEL_COPY					0x40
#define	SEL_IRQ_OFF					0x80

/************************************************/
/*		Device/Geometry controls				*/
/************************************************/
#define GEOMETRY_NONE	 			0x0				// No device
#define GEOMETRY_SET				0x1				// Geometry set
#define	GEOMETRY_LBA				0x2				// Geometry set in default LBA mode
#define	GEOMETRY_PHOENIX			0x3				// Geometry set in Pheonix BIOS compatibility mode

#define	DEVICE_NONE					0x0				// No device present
#define	DEVICE_INACTIVE				0x1				// device present but not registered active
#define	DEVICE_ATAPI				0x2				// ATAPI device (CD_ROM, Tape, Etc...)
#define	DEVICE_DASD_NONLBA			0x3				// Non LBA incompatible device
#define	DEVICE_DASD_LBA				0x4				// LBA compatible device

/************************************************/
/*		BigD fail register bits					*/
/************************************************/
#define	FAIL_NONE				0x00
#define	FAIL_0					0x01
#define	FAIL_1					0x02
#define	FAIL_2					0x04
#define	FAIL_MULTIPLE			0x08
#define	FAIL_GOOD				0x20
#define	FAIL_AUDIBLE			0x40
#define	FAIL_ANY				0x80

/************************************************/
/*		Setup Structure Definitions				*/
/************************************************/
typedef struct		// device setup parameters
	{
	UCHAR	geometryControl;	// geometry control flags
	UCHAR	device;				// device code
	USHORT	sectors;			// number of sectors per track
	USHORT	heads;				// number of heads
	USHORT	cylinders;			// number of cylinders for this device
	ULONG	blocks;				// number of blocks on device
	ULONG	realCapacity;		// number of real blocks on this device for drive changed testing
	} SETUP_DEVICE, *PSETUP_DEVICE;

typedef struct		// master setup structure
	{
	USHORT			startupDelay;
	BOOL			promptBIOS;
	BOOL			fastFormat;
	BOOL			shareInterrupt;
	BOOL			rebootRebuild;
	USHORT			timingMode;
	USHORT			spare5;
	USHORT			spare6;
	SETUP_DEVICE	setupDevice[BIGD_MAXDRIVES];
	}	SETUP, *PSETUP;

/************************************************/
/*		RAID Structure Definitions				*/
/************************************************/
typedef	struct
	{
	UCHAR	signature;			// 0x55 our mirror signature
	UCHAR	status;				// current status bits
	UCHAR	pairIdentifier;		// unique identifier for pair
	ULONG	reconstructPoint;	// recontruction point for hot reconstruct
	}	DISK_MIRROR;

typedef struct	DEVICE_RAID1
	{
	long		TotalSectors;
	DISK_MIRROR DiskRaid1;
	}	DEVICE_RAID1, *PDEVICE_RAID1;

#define	DISK_MIRROR_POSITION	0x01A8
#define	SIGNATURE				0x55

#define	MASK_SERIAL_NUMBER	0x0FFE			// mask for serial number matching
#define	MASK_SERIAL_UNIT	0x0001			// mask for unit portion of serial number

// Status bits
#define	UCBF_MIRRORED		0x0010								// drive has a pair
#define	UCBF_MATCHED		0x0020								// drive pair is matched
#define	UCBF_SURVIVOR		0x0040								// this unit is a survivor of a pair
#define	UCBF_REBUILD		0x0080								// rebuild in progress on this device

// SCSI controls for RAID
#define	SC_MY_RAID			0xBF			// our special CDB command byte for Win95... interface
#define	MY_SCSI_QUERY1		0x32			// byte 1 subcommand to query driver for RAID 1 informatation
#define	MY_SCSI_REBUILD		0x40			// byte 1 subcommand to reconstruct a mirrored pair
#define MY_SCSI_DEMOFAIL	0x54			// byte 1 subcommand for RAID failure demonstration
#define	MY_SCSI_ALARMMUTE	0x60			// byte 1 subcommand to mute any alarm currently on

/************************************************/
/*		Timeout konstants		 				*/
/************************************************/
#define	TIMEOUT_READY				100			// 100 mSec
#define	TIMEOUT_DRQ					300			// 300 mSec
#define	TIMEOUT_DATA				(3 * HZ)	// 3 seconds

/************************************************/
/*		Misc. macros			 				*/
/************************************************/
#define ANY2SCSI(up, p)					\
((UCHAR *)up)[0] = (((ULONG)(p)) >> 8);	\
((UCHAR *)up)[1] = ((ULONG)(p));

#define SCSI2LONG(up)					\
( (((long)*(((UCHAR *)up))) << 16)		\
+ (((long)(((UCHAR *)up)[1])) << 8)		\
+ ((long)(((UCHAR *)up)[2])) )

#define XANY2SCSI(up, p)				\
((UCHAR *)up)[0] = ((long)(p)) >> 24;	\
((UCHAR *)up)[1] = ((long)(p)) >> 16;	\
((UCHAR *)up)[2] = ((long)(p)) >> 8;	\
((UCHAR *)up)[3] = ((long)(p));

#define XSCSI2LONG(up)					\
( (((long)(((UCHAR *)up)[0])) << 24)	\
+ (((long)(((UCHAR *)up)[1])) << 16)	\
+ (((long)(((UCHAR *)up)[2])) <<  8)	\
+ ((long)(((UCHAR *)up)[3])) )

#define	SelectSpigot(padapter,spigot)	outb_p (spigot, padapter->regStatSel)
#define WriteCommand(padapter,cmd)		outb_p (cmd, padapter->regStatCmd)
#define	AtapiDevice(padapter,b)			outb_p (b, padapter->regLba24);
#define	AtapiCountLo(padapter,b)		outb_p (b, padapter->regLba8)
#define	AtapiCountHi(padapter,b)		outb_p (b, padapter->regLba16)

/************************************************/
/*		SCSI CDB operation codes 				*/
/************************************************/
#define SCSIOP_TEST_UNIT_READY		0x00
#define SCSIOP_REZERO_UNIT			0x01
#define SCSIOP_REWIND				0x01
#define SCSIOP_REQUEST_BLOCK_ADDR	0x02
#define SCSIOP_REQUEST_SENSE		0x03
#define SCSIOP_FORMAT_UNIT			0x04
#define SCSIOP_READ_BLOCK_LIMITS	0x05
#define SCSIOP_REASSIGN_BLOCKS		0x07
#define SCSIOP_READ6				0x08
#define SCSIOP_RECEIVE				0x08
#define SCSIOP_WRITE6				0x0A
#define SCSIOP_PRINT				0x0A
#define SCSIOP_SEND					0x0A
#define SCSIOP_SEEK6				0x0B
#define SCSIOP_TRACK_SELECT			0x0B
#define SCSIOP_SLEW_PRINT			0x0B
#define SCSIOP_SEEK_BLOCK			0x0C
#define SCSIOP_PARTITION			0x0D
#define SCSIOP_READ_REVERSE			0x0F
#define SCSIOP_WRITE_FILEMARKS		0x10
#define SCSIOP_FLUSH_BUFFER			0x10
#define SCSIOP_SPACE				0x11
#define SCSIOP_INQUIRY				0x12
#define SCSIOP_VERIFY6				0x13
#define SCSIOP_RECOVER_BUF_DATA		0x14
#define SCSIOP_MODE_SELECT			0x15
#define SCSIOP_RESERVE_UNIT			0x16
#define SCSIOP_RELEASE_UNIT			0x17
#define SCSIOP_COPY					0x18
#define SCSIOP_ERASE				0x19
#define SCSIOP_MODE_SENSE			0x1A
#define SCSIOP_START_STOP_UNIT		0x1B
#define SCSIOP_STOP_PRINT			0x1B
#define SCSIOP_LOAD_UNLOAD			0x1B
#define SCSIOP_RECEIVE_DIAGNOSTIC	0x1C
#define SCSIOP_SEND_DIAGNOSTIC		0x1D
#define SCSIOP_MEDIUM_REMOVAL		0x1E
#define SCSIOP_READ_CAPACITY		0x25
#define SCSIOP_READ					0x28
#define SCSIOP_WRITE				0x2A
#define SCSIOP_SEEK					0x2B
#define SCSIOP_LOCATE				0x2B
#define SCSIOP_WRITE_VERIFY			0x2E
#define SCSIOP_VERIFY				0x2F
#define SCSIOP_SEARCH_DATA_HIGH		0x30
#define SCSIOP_SEARCH_DATA_EQUAL	0x31
#define SCSIOP_SEARCH_DATA_LOW		0x32
#define SCSIOP_SET_LIMITS			0x33
#define SCSIOP_READ_POSITION		0x34
#define SCSIOP_SYNCHRONIZE_CACHE	0x35
#define SCSIOP_COMPARE				0x39
#define SCSIOP_COPY_COMPARE			0x3A
#define SCSIOP_WRITE_DATA_BUFF		0x3B
#define SCSIOP_READ_DATA_BUFF		0x3C
#define SCSIOP_CHANGE_DEFINITION	0x40
#define SCSIOP_READ_SUB_CHANNEL		0x42
#define SCSIOP_READ_TOC				0x43
#define SCSIOP_READ_HEADER			0x44
#define SCSIOP_PLAY_AUDIO			0x45
#define SCSIOP_PLAY_AUDIO_MSF		0x47
#define SCSIOP_PLAY_TRACK_INDEX		0x48
#define SCSIOP_PLAY_TRACK_RELATIVE	0x49
#define SCSIOP_PAUSE_RESUME			0x4B
#define SCSIOP_LOG_SELECT			0x4C
#define SCSIOP_LOG_SENSE			0x4D
#define SCSIOP_MODE_SELECT10		0x55
#define SCSIOP_MODE_SENSE10			0x5A
#define SCSIOP_LOAD_UNLOAD_SLOT		0xA6
#define SCSIOP_MECHANISM_STATUS		0xBD
#define SCSIOP_READ_CD				0xBE

// IDE command definitions
#define IDE_COMMAND_ATAPI_RESET		0x08
#define IDE_COMMAND_READ			0x20
#define IDE_COMMAND_WRITE			0x30
#define IDE_COMMAND_RECALIBRATE		0x10
#define IDE_COMMAND_SEEK			0x70
#define IDE_COMMAND_SET_PARAMETERS	0x91
#define IDE_COMMAND_VERIFY			0x40
#define IDE_COMMAND_ATAPI_PACKET	0xA0
#define IDE_COMMAND_ATAPI_IDENTIFY	0xA1
#define	IDE_CMD_READ_MULTIPLE		0xC4
#define	IDE_CMD_WRITE_MULTIPLE		0xC5
#define	IDE_CMD_SET_MULTIPLE		0xC6
#define IDE_COMMAND_IDENTIFY		0xEC

// IDE status definitions
#define IDE_STATUS_ERROR			0x01
#define IDE_STATUS_INDEX			0x02
#define IDE_STATUS_CORRECTED_ERROR	0x04
#define IDE_STATUS_DRQ				0x08
#define IDE_STATUS_DSC				0x10
#define	IDE_STATUS_WRITE_FAULT		0x20
#define IDE_STATUS_DRDY				0x40
#define IDE_STATUS_BUSY				0x80

typedef struct _ATAPI_STATUS
	{
	CHAR	check		:1;
	CHAR	reserved1	:1;
	CHAR	corr		:1;
	CHAR	drq			:1;
	CHAR	dsc			:1;
	CHAR	reserved2	:1;
	CHAR	drdy		:1;
	CHAR	bsy			:1;
	}	ATAPI_STATUS;

typedef struct _ATAPI_REASON
	{
	CHAR	cod			:1;
	CHAR	io			:1;
	CHAR	reserved1	:6;
	}	ATAPI_REASON;

typedef struct _ATAPI_ERROR
	{
	CHAR	ili			:1;
	CHAR	eom			:1;
	CHAR	abort		:1;
	CHAR	mcr			:1;
	CHAR	senseKey	:4;
	}	ATAPI_ERROR;

// IDE error definitions
#define	IDE_ERROR_AMNF				0x01
#define	IDE_ERROR_TKONF				0x02
#define	IDE_ERROR_ABRT				0x04
#define	IDE_ERROR_MCR				0x08
#define	IDE_ERROR_IDFN				0x10
#define	IDE_ERROR_MC				0x20
#define	IDE_ERROR_UNC				0x40
#define	IDE_ERROR_BBK				0x80

// SCSI read capacity structure
typedef	struct _READ_CAPACITY_DATA
	{
	ULONG blks;				/* total blocks (converted to little endian) */
	ULONG blksiz;			/* size of each (converted to little endian) */
	}	READ_CAPACITY_DATA, *PREAD_CAPACITY_DATA;

// SCSI inquiry data
typedef struct _INQUIRYDATA
	{
	UCHAR DeviceType			:5;
	UCHAR DeviceTypeQualifier	:3;
	UCHAR DeviceTypeModifier	:7;
	UCHAR RemovableMedia		:1;
    UCHAR Versions;
    UCHAR ResponseDataFormat;
    UCHAR AdditionalLength;
    UCHAR Reserved[2];
	UCHAR SoftReset				:1;
	UCHAR CommandQueue			:1;
	UCHAR Reserved2				:1;
	UCHAR LinkedCommands		:1;
	UCHAR Synchronous			:1;
	UCHAR Wide16Bit				:1;
	UCHAR Wide32Bit				:1;
	UCHAR RelativeAddressing	:1;
    UCHAR VendorId[8];
    UCHAR ProductId[16];
    UCHAR ProductRevisionLevel[4];
    UCHAR VendorSpecific[20];
    UCHAR Reserved3[40];
	}	INQUIRYDATA, *PINQUIRYDATA;

// IDE IDENTIFY data
#pragma pack (1)
typedef struct _IDENTIFY_DATA
	{
    USHORT	GeneralConfiguration;		//  0
    USHORT	NumberOfCylinders;			//  1
    USHORT	Reserved1;					//  2
    USHORT	NumberOfHeads;				//  3
    USHORT	UnformattedBytesPerTrack;	//  4
    USHORT	UnformattedBytesPerSector;	//  5
    USHORT	SectorsPerTrack;			//  6
	USHORT	NumBytesISG;				//  7 Byte Len - inter-sector gap
	USHORT	NumBytesSync;				//  8          - sync field
	USHORT	NumWordsVUS;				//  9 Len - Vendor Unique Info
    USHORT	SerialNumber[10];			// 10
    USHORT	BufferType;					// 20
    USHORT	BufferSectorSize;			// 21
    USHORT	NumberOfEccBytes;			// 22
    USHORT	FirmwareRevision[4];		// 23
    USHORT	ModelNumber[20];			// 27
	USHORT	NumSectorsPerInt	:8;		// 47 Multiple Mode - Sec/Blk
	USHORT	Reserved2			:8;		// 47
	USHORT	DoubleWordMode;				// 48 flag for double word mode capable
	USHORT	VendorUnique1		:8;		// 49
	USHORT	SupportDMA			:1;		// 49 DMA supported
	USHORT	SupportLBA			:1;		// 49 LBA supported
	USHORT	SupportIORDYDisable	:1;		// 49 IORDY can be disabled
	USHORT	SupportIORDY		:1;		// 49 IORDY supported
	USHORT	ReservedPsuedoDMA	:1;		// 49 reserved for pseudo DMA mode support
	USHORT	Reserved3			:3;		// 49
	USHORT	Reserved4;					// 50
	USHORT	Reserved5			:8;		// 51 Transfer Cycle Timing - PIO
	USHORT	PIOCycleTime		:8;		// 51 Transfer Cycle Timing - PIO
	USHORT	Reserved6			:8;		// 52                       - DMA
	USHORT	DMACycleTime		:8;		// 52                       - DMA
	USHORT	Valid_54_58			:1;		// 53 words 54 - 58 are valid
	USHORT	Valid_64_70			:1;		// 53 words 64 - 70 are valid
	USHORT	Reserved7			:14;	// 53
	USHORT	LogNumCyl;					// 54 Current Translation - Num Cyl
	USHORT	LogNumHeads;				// 55                       Num Heads
	USHORT	LogSectorsPerTrack;			// 56                       Sec/Trk
	ULONG	LogTotalSectors;			// 57                       Total Sec
	USHORT	CurrentNumSecPerInt	:8;		// 59 current setting for number of sectors per interrupt
	USHORT	ValidNumSecPerInt	:1;		// 59 Current setting is valid for number of sectors per interrupt
	USHORT	Reserved8			:7;		// 59
	ULONG	LBATotalSectors;			// 60 LBA Mode - Sectors
	USHORT	DMASWordFlags;				// 62
	USHORT	DMAMWordFlags;				// 63
	USHORT	AdvancedPIOSupport  :8;		// 64 Flow control PIO transfer modes supported
	USHORT	Reserved9			:8;		// 64
	USHORT	MinMultiDMACycle;			// 65 minimum multiword DMA transfer cycle time per word
	USHORT	RecomendDMACycle;			// 66 Manufacturer's recommende multiword DMA transfer cycle time
	USHORT	MinPIOCycleWithoutFlow;		// 67 Minimum PIO transfer cycle time without flow control
	USHORT	MinPIOCylceWithFlow;		// 68 Minimum PIO transfer cycle time with IORDY flow control
	USHORT	ReservedSpace[256-69];		// 69
	}	IDENTIFY_DATA, *PIDENTIFY_DATA;

// ATAPI configuration bits
typedef struct _ATAPI_GENERAL_0
	{
	USHORT	CmdPacketSize		:2;		// Command packet size
	USHORT	Reserved1			:3;
	USHORT	CmdDrqType			:2;
	USHORT	Removable			:1;
	USHORT	DeviceType			:5;
	USHORT	Reserved2			:1;
	USHORT	ProtocolType		:2;
	}	ATAPI_GENERAL_0;

#pragma pack ()
