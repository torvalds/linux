/*+M*************************************************************************
 * Perceptive Solutions, Inc. PSI-240I device driver proc support for Linux.
 *
 * Copyright (c) 1997 Perceptive Solutions, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *	File Name:		psi240i.h
 *
 *	Description:	Header file for the SCSI driver for the PSI240I
 *					EIDE interface card.
 *
 *-M*************************************************************************/
#ifndef _PSI240I_H
#define _PSI240I_H

#include <linux/types.h>

#ifndef	PSI_EIDE_SCSIOP
#define	PSI_EIDE_SCSIOP	1

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
/*		Timeout konstants		 				*/
/************************************************/
#define	TIMEOUT_READY				10		// 100 mSec
#define	TIMEOUT_DRQ					40		// 400 mSec

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
#define IDE_COMMAND_WRITE_DMA		0xCA
#define IDE_COMMAND_READ_DMA		0xC8
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

// IDE error definitions
#define	IDE_ERROR_AMNF				0x01
#define	IDE_ERROR_TKONF				0x02
#define	IDE_ERROR_ABRT				0x04
#define	IDE_ERROR_MCR				0x08
#define	IDE_ERROR_IDFN				0x10
#define	IDE_ERROR_MC				0x20
#define	IDE_ERROR_UNC				0x40
#define	IDE_ERROR_BBK				0x80

//	IDE interface structure
typedef struct _IDE_STRUCT
	{
	union
		{
		UCHAR	ide[9];
		struct
			{
			USHORT	data;
			UCHAR	sectors;
			UCHAR	lba[4];
			UCHAR	cmd;
			UCHAR	spigot;
			}	ides;
		} ide;
	}	IDE_STRUCT;

// SCSI read capacity structure
typedef	struct _READ_CAPACITY_DATA
	{
	ULONG blks;				/* total blocks (converted to little endian) */
	ULONG blksiz;			/* size of each (converted to little endian) */
	}	READ_CAPACITY_DATA, *PREAD_CAPACITY_DATA;

// SCSI inquiry data
#ifndef HOSTS_C

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
#endif

// IDE IDENTIFY data
typedef struct _IDENTIFY_DATA
	{
    USHORT GeneralConfiguration;            // 00
    USHORT NumberOfCylinders;               // 02
    USHORT Reserved1;                       // 04
    USHORT NumberOfHeads;                   // 06
    USHORT UnformattedBytesPerTrack;        // 08
    USHORT UnformattedBytesPerSector;       // 0A
    USHORT SectorsPerTrack;                 // 0C
    USHORT VendorUnique1[3];                // 0E
    USHORT SerialNumber[10];                // 14
    USHORT BufferType;                      // 28
    USHORT BufferSectorSize;                // 2A
    USHORT NumberOfEccBytes;                // 2C
    USHORT FirmwareRevision[4];             // 2E
    USHORT ModelNumber[20];                 // 36
    UCHAR  MaximumBlockTransfer;            // 5E
    UCHAR  VendorUnique2;                   // 5F
    USHORT DoubleWordIo;                    // 60
    USHORT Capabilities;                    // 62
    USHORT Reserved2;                       // 64
    UCHAR  VendorUnique3;                   // 66
    UCHAR  PioCycleTimingMode;              // 67
    UCHAR  VendorUnique4;                   // 68
    UCHAR  DmaCycleTimingMode;              // 69
    USHORT TranslationFieldsValid:1;        // 6A
    USHORT Reserved3:15;
    USHORT NumberOfCurrentCylinders;        // 6C
    USHORT NumberOfCurrentHeads;            // 6E
    USHORT CurrentSectorsPerTrack;          // 70
    ULONG  CurrentSectorCapacity;           // 72
    USHORT Reserved4[197];                  // 76
	}	IDENTIFY_DATA, *PIDENTIFY_DATA;

// Identify data without the Reserved4.
typedef struct _IDENTIFY_DATA2 {
    USHORT GeneralConfiguration;            // 00
    USHORT NumberOfCylinders;               // 02
    USHORT Reserved1;                       // 04
    USHORT NumberOfHeads;                   // 06
    USHORT UnformattedBytesPerTrack;        // 08
    USHORT UnformattedBytesPerSector;       // 0A
    USHORT SectorsPerTrack;                 // 0C
    USHORT VendorUnique1[3];                // 0E
    USHORT SerialNumber[10];                // 14
    USHORT BufferType;                      // 28
    USHORT BufferSectorSize;                // 2A
    USHORT NumberOfEccBytes;                // 2C
    USHORT FirmwareRevision[4];             // 2E
    USHORT ModelNumber[20];                 // 36
    UCHAR  MaximumBlockTransfer;            // 5E
    UCHAR  VendorUnique2;                   // 5F
    USHORT DoubleWordIo;                    // 60
    USHORT Capabilities;                    // 62
    USHORT Reserved2;                       // 64
    UCHAR  VendorUnique3;                   // 66
    UCHAR  PioCycleTimingMode;              // 67
    UCHAR  VendorUnique4;                   // 68
    UCHAR  DmaCycleTimingMode;              // 69
	USHORT TranslationFieldsValid:1;     	// 6A
    USHORT Reserved3:15;
    USHORT NumberOfCurrentCylinders;        // 6C
    USHORT NumberOfCurrentHeads;            // 6E
    USHORT CurrentSectorsPerTrack;          // 70
    ULONG  CurrentSectorCapacity;           // 72
	}	IDENTIFY_DATA2, *PIDENTIFY_DATA2;

#endif	// PSI_EIDE_SCSIOP

// function prototypes
int Psi240i_Command			(Scsi_Cmnd *SCpnt);
int Psi240i_Abort			(Scsi_Cmnd *SCpnt);
int Psi240i_Reset			(Scsi_Cmnd *SCpnt, unsigned int flags);
#endif
