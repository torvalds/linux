/****************************************************************************
 * Perceptive Solutions, Inc. PCI-2000 device driver for Linux.
 *
 * pci2000.h - Linux Host Driver for PCI-2000 IntelliCache SCSI Adapters
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
#ifndef _PCI2000_H
#define _PCI2000_H

#include <linux/types.h>

#ifndef	PSI_EIDE_SCSIOP
#define	PSI_EIDE_SCSIOP	1

#define	LINUXVERSION(v,p,s)    (((v)<<16) + ((p)<<8) + (s))

/************************************************/
/*		definition of standard data types		*/
/************************************************/
#define	CHAR	char
#define	UCHAR	unsigned char
#define	SHORT	short
#define	USHORT	unsigned short
#define	BOOL	long
#define	LONG	long
#define	ULONG	unsigned long
#define	VOID	void

typedef	CHAR	*PCHAR;
typedef	UCHAR	*PUCHAR;
typedef	SHORT	*PSHORT;
typedef	USHORT	*PUSHORT;
typedef	BOOL	*PBOOL;
typedef	LONG	*PLONG;
typedef	ULONG	*PULONG;
typedef	VOID	*PVOID;


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

#endif

// function prototypes
int Pci2000_Detect			(struct scsi_host_template *tpnt);
int Pci2000_Command			(Scsi_Cmnd *SCpnt);
int Pci2000_QueueCommand	(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *));
int Pci2000_Abort			(Scsi_Cmnd *SCpnt);
int Pci2000_Reset			(Scsi_Cmnd *SCpnt, unsigned int flags);
int Pci2000_Release			(struct Scsi_Host *pshost);
int Pci2000_BiosParam		(struct scsi_device *sdev,
					struct block_device *bdev,
					sector_t capacity, int geom[]);

#endif
