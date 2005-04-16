/****************************************************************************
 * Perceptive Solutions, Inc. PCI-2000 device driver for Linux.
 *
 * psi_roy.h - Linux Host Driver for PCI-2000 IntelliCache SCSI Adapters
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

#ifndef	ROY_HOST
#define	ROY_HOST

/************************************************/
/*		PCI setup								*/
/************************************************/
#define	VENDOR_PSI			0x1256
#define	DEVICE_ROY_1		0x5201		/* 'R1' */

/************************************************/
/*		controller constants					*/
/************************************************/
#define MAXADAPTER			4			// Increase this and the sizes of the arrays below, if you need more.
#define	MAX_BUS				2
#define	MAX_UNITS			16
#define	TIMEOUT_COMMAND		400			// number of milliSecondos for command busy timeout

/************************************************/
/*		I/O address offsets						*/
/************************************************/
#define RTR_MAILBOX						0x040
#define RTR_LOCAL_DOORBELL				0x060
#define RTR_PCI_DOORBELL				0x064

/************************************************/
/*												*/
/*			Host command codes					*/
/*												*/
/************************************************/
#define	CMD_READ_CHS		0x01		/* read sectors as specified (CHS mode) */
#define	CMD_READ			0x02		/* read sectors as specified (RBA mode) */
#define	CMD_READ_SG			0x03		/* read sectors using scatter/gather list */
#define	CMD_WRITE_CHS		0x04		/* write sectors as specified (CHS mode) */
#define	CMD_WRITE			0x05		/* write sectors as specified (RBA mode) */
#define	CMD_WRITE_SG		0x06		/* write sectors using scatter/gather list (LBA mode) */
#define	CMD_READ_CHS_SG		0x07		/* read sectors using scatter/gather list (CHS mode) */
#define	CMD_WRITE_CHS_SG	0x08		/* write sectors using scatter/gather list (CHS mode) */
#define	CMD_VERIFY_CHS		0x09		/* verify data on sectors as specified (CHS mode) */
#define	CMD_VERIFY			0x0A		/* verify data on sectors as specified (RBA mode) */
#define	CMD_DASD_CDB		0x0B		/* process CDB for a DASD device */
#define	CMD_DASD_CDB_SG		0x0C		/* process CDB for a DASD device with scatter/gather */

#define	CMD_READ_ABS		0x10		/* read absolute disk */
#define	CMD_WRITE_ABS		0x11		/* write absolute disk */
#define	CMD_VERIFY_ABS		0x12		/* verify absolute disk */
#define	CMD_TEST_READY		0x13		/* test unit ready and return status code */
#define	CMD_LOCK_DOOR		0x14		/* lock device door */
#define	CMD_UNLOCK_DOOR		0x15		/* unlock device door */
#define	CMD_EJECT_MEDIA		0x16		/* eject the media */
#define	CMD_UPDATE_CAP		0x17		/* update capacity information */
#define	CMD_TEST_PRIV		0x18		/* test and setup private format media */


#define	CMD_SCSI_THRU		0x30		/* SCSI pass through CDB */
#define	CMD_SCSI_THRU_SG	0x31		/* SCSI pass through CDB with scatter/gather */
#define	CMD_SCSI_REQ_SENSE	0x32		/* SCSI pass through request sense after check condition */

#define	CMD_DASD_RAID_RQ	0x35		/* request DASD RAID drive data */
#define	CMD_DASD_RAID_RQ0	0x31			/* byte 1 subcommand to query for RAID 0 informatation */
#define	CMD_DASD_RAID_RQ1	0x32			/* byte 1 subcommand to query for RAID 1 informatation */
#define	CMD_DASD_RAID_RQ5	0x33			/* byte 1 subcommand to query for RAID 5 informatation */

#define	CMD_DASD_SCSI_INQ	0x36		/* do DASD inquire and return in SCSI format */
#define	CMD_DASD_CAP		0x37		/* read DASD capacity */
#define	CMD_DASD_INQ		0x38		/* do DASD inquire for type data and return SCSI/EIDE inquiry */
#define	CMD_SCSI_INQ		0x39		/* do SCSI inquire */
#define	CMD_READ_SETUP		0x3A		/* Get setup structures from controller */
#define	CMD_WRITE_SETUP		0x3B		/* Put setup structures in controller and burn in flash */
#define	CMD_READ_CONFIG		0x3C		/* Get the entire configuration and setup structures */
#define	CMD_WRITE_CONFIG	0x3D		/* Put the entire configuration and setup structures in flash */

#define	CMD_TEXT_DEVICE		0x3E		/* obtain device text */
#define	CMD_TEXT_SIGNON		0x3F		/* get sign on banner */

#define	CMD_QUEUE			0x40		/* any command below this generates a queue tag interrupt to host*/

#define	CMD_PREFETCH		0x40		/* prefetch sectors as specified */
#define	CMD_TEST_WRITE		0x41		/* Test a device for write protect */
#define	CMD_LAST_STATUS		0x42		/* get last command status and error data*/
#define	CMD_ABORT			0x43		/* abort command as specified */
#define	CMD_ERROR			0x44		/* fetch error code from a tagged op */
#define	CMD_DONE			0x45		/* done with operation */
#define	CMD_DIAGNOSTICS		0x46		/* execute controller diagnostics and wait for results */
#define	CMD_FEATURE_MODE	0x47		/* feature mode control word */
#define	CMD_DASD_INQUIRE	0x48		/* inquire as to DASD SCSI device (32 possible) */
#define	CMD_FEATURE_QUERY	0x49		/* query the feature control word */
#define	CMD_DASD_EJECT		0x4A		/* Eject removable media for DASD type */
#define	CMD_DASD_LOCK		0x4B		/* Lock removable media for DASD type */
#define	CMD_DASD_TYPE		0x4C		/* obtain DASD device type */
#define	CMD_NUM_DEV			0x4D		/* obtain the number of devices connected to the controller */
#define	CMD_GET_PARMS		0x4E		/* obtain device parameters */
#define	CMD_SPECIFY			0x4F		/* specify operating system for scatter/gather operations */

#define	CMD_RAID_GET_DEV	0x50		/* read RAID device geometry */
#define CMD_RAID_READ		0x51		/* read RAID 1 parameter block */
#define	CMD_RAID_WRITE		0x52		/* write RAID 1 parameter block */
#define	CMD_RAID_LITEUP		0x53		/* Light up the drive light for identification */
#define	CMD_RAID_REBUILD	0x54		/* issue a RAID 1 pair rebuild */
#define	CMD_RAID_MUTE		0x55		/* mute RAID failure alarm */
#define	CMD_RAID_FAIL		0x56		/* induce a RAID failure */
#define	CMD_RAID_STATUS		0x57		/* get status of RAID pair */
#define	CMD_RAID_STOP		0x58		/* stop any reconstruct in progress */
#define CMD_RAID_START		0x59		/* start reconstruct */
#define	CMD_RAID0_READ		0x5A		/* read RAID 0 parameter block */
#define	CMD_RAID0_WRITE		0x5B		/* write RAID 0 parameter block */
#define	CMD_RAID5_READ		0x5C		/* read RAID 5 parameter block */
#define	CMD_RAID5_WRITE		0x5D		/* write RAID 5 parameter block */

#define	CMD_ERASE_TABLES	0x5F		/* erase partition table and RAID signatutures */

#define	CMD_SCSI_GET		0x60		/* get SCSI pass through devices */
#define	CMD_SCSI_TIMEOUT	0x61		/* set SCSI pass through timeout */
#define	CMD_SCSI_ERROR		0x62		/* get SCSI pass through request sense length and residual data count */
#define	CMD_GET_SPARMS		0x63		/* get SCSI bus and user parms */
#define	CMD_SCSI_ABORT		0x64		/* abort by setting time-out to zero */

#define	CMD_CHIRP_CHIRP		0x77		/* make a chirp chirp sound */
#define	CMD_GET_LAST_DONE	0x78		/* get tag of last done in progress */
#define	CMD_GET_FEATURES	0x79		/* get feature code and ESN */
#define CMD_CLEAR_CACHE		0x7A		/* Clear cache on specified device */
#define	CMD_BIOS_TEST		0x7B		/* Test whether or not to load BIOS */
#define	CMD_WAIT_FLUSH		0x7C		/* wait for cache flushed and invalidate read cache */
#define	CMD_RESET_BUS		0x7D		/* reset the SCSI bus */
#define	CMD_STARTUP_QRY		0x7E		/* startup in progress query */
#define	CMD_RESET			0x7F		/* reset the controller */

#define	CMD_RESTART_RESET	0x80		/* reload and restart the controller at any reset issued */
#define	CMD_SOFT_RESET		0x81		/* do a soft reset NOW! */

/************************************************/
/*												*/
/*				Host return errors				*/
/*												*/
/************************************************/
#define	ERR08_TAGGED		0x80		/* doorbell error ored with tag */

#define	ERR16_NONE			0x0000		/* no errors */
#define	ERR16_SC_COND_MET	0x0004		/* SCSI status - Condition Met */
#define	ERR16_CMD			0x0101		/* command error */
#define	ERR16_SC_CHECK_COND	0x0002		/* SCSI status - Check Condition */
#define	ERR16_CMD_NOT		0x0201		/* command not supported */
#define ERR16_NO_DEVICE     0x0301		/* invalid device selection */
#define	ERR16_SECTOR		0x0202		/* bad sector */
#define	ERR16_PROTECT		0x0303		/* write protected */
#define	ERR16_NOSECTOR		0x0404		/* sector not found */
#define	ERR16_MEDIA			0x0C0C		/* invalid media */
#define	ERR16_CONTROL		0x2020		/* controller error */
#define	ERR16_CONTROL_DMA	0x2120		/* controller DMA engine error */
#define	ERR16_NO_ALARM		0x2220		/* alarm is not active */
#define	ERR16_OP_BUSY		0x2320		/* operation busy */
#define	ERR16_SEEK			0x4040		/* seek failure */
#define	ERR16_DEVICE_FAIL	0x4140		/* device has failed */
#define ERR16_TIMEOUT		0x8080		/* timeout error */
#define	ERR16_DEV_NOT_READY	0xAAAA		/* drive not ready */
#define	ERR16_UNDEFINED		0xBBBB		/* undefined error */
#define	ERR16_WRITE_FAULT	0xCCCC		/* write fault */
#define ERR16_INVALID_DEV	0x4001		/* invalid device access */
#define	ERR16_DEVICE_BUSY	0x4002		/* device is busy */
#define	ERR16_MEMORY		0x4003		/* device pass thru requires too much memory */
#define	ERR16_NO_FEATURE	0x40FA		/* feature no implemented */
#define	ERR16_NOTAG			0x40FD		/* no tag space available */
#define	ERR16_NOT_READY		0x40FE		/* controller not ready error */
#define	ERR16_SETUP_FLASH	0x5050		/* error when writing setup to flash memory */
#define	ERR16_SETUP_SIZE	0x5051		/* setup block size error */
#define	ERR16_SENSE			0xFFFF		/* sense opereration failed */
#define	ERR16_SC_BUSY		0x0008		/* SCSI status - Busy */
#define	ERR16_SC_RES_CONFL	0x0018		/* SCSI status - Reservation Conflict */
#define	ERR16_SC_CMD_TERM	0x0022		/* SCSI status - Command Terminated */
#define	ERR16_SC_OTHER		0x00FF		/* SCSI status - not recognized (any value masked) */
#define	ERR16_MEDIA_CHANGED	0x8001		/* devices media has been changed */

#define	ERR32_NONE			0x00000000	/* no errors */
#define	ERR32_SC_COND_MET	0x00000004	/* SCSI status - Condition Met */
#define	ERR32_CMD			0x00010101	/* command error */
#define	ERR32_SC_CHECK_COND	0x00020002	/* SCSI status - Check Condition */
#define	ERR32_CMD_NOT		0x00030201	/* command not supported */
#define ERR32_NO_DEVICE     0x00040301	/* invalid device selection */
#define	ERR32_SECTOR		0x00050202	/* bad sector */
#define	ERR32_PROTECT		0x00060303	/* write protected */
#define	ERR32_NOSECTOR		0x00070404	/* sector not found */
#define	ERR32_MEDIA			0x00080C0C	/* invalid media */
#define	ERR32_CONTROL		0x00092020	/* controller error */
#define	ERR32_CONTROL_DMA	0x000A2120	/* Controller DMA error */
#define	ERR32_NO_ALARM		0x000B2220 	/* alarm is not active */
#define	ERR32_OP_BUSY		0x000C2320	/* operation busy */
#define	ERR32_SEEK			0x000D4040	/* seek failure */
#define	ERR32_DEVICE_FAIL	0x000E4140	/* device has failed */
#define ERR32_TIMEOUT		0x000F8080	/* timeout error */
#define	ERR32_DEV_NOT_READY	0x0010AAAA	/* drive not ready */
#define	ERR32_UNDEFINED		0x0011BBBB	/* undefined error */
#define	ERR32_WRITE_FAULT	0x0012CCCC	/* write fault */
#define ERR32_INVALID_DEV	0x00134001	/* invalid device access */
#define	ERR32_DEVICE_BUSY	0x00144002	/* device is busy */
#define	ERR32_MEMORY		0x00154003	/* device pass thru requires too much memory */
#define	ERR32_NO_FEATURE	0x001640FA	/* feature no implemented */
#define	ERR32_NOTAG			0x001740FD	/* no tag space available */
#define	ERR32_NOT_READY		0x001840FE	/* controller not ready error */
#define	ERR32_SETUP_FLASH	0x00195050	/* error when writing setup to flash memory */
#define	ERR32_SETUP_SIZE	0x001A5051	/* setup block size error */
#define	ERR32_SENSE			0x001BFFFF	/* sense opereration failed */
#define	ERR32_SC_BUSY		0x001C0008	/* SCSI status - Busy */
#define	ERR32_SC_RES_CONFL	0x001D0018	/* SCSI status - Reservation Conflict */
#define	ERR32_SC_CMD_TERM	0x001E0022	/* SCSI status - Command Terminated */
#define	ERR32_SC_OTHER		0x001F00FF	/* SCSI status - not recognized (any value masked) */
#define	ERR32_MEDIA_CHANGED	0x00208001	/* devices media has been changed */

/************************************************/
/*												*/
/*	Host Operating System specification codes	*/
/*												*/
/************************************************/
#define	SPEC_INTERRUPT		0x80		/* specification requires host interrupt */
#define	SPEC_BACKWARD_SG	0x40		/* specification requires scatter/gather items reversed */
#define	SPEC_DOS_BLOCK		0x01		/* DOS DASD blocking on pass through */
#define	SPEC_OS2_V3			0x02		/* OS/2 Warp */
#define	SPCE_SCO_3242		0x04		/* SCO 3.4.2.2 */
#define	SPEC_QNX_4X			0x05		/* QNX 4.XX */
#define	SPEC_NOVELL_NWPA	0x08		/* Novell NWPA scatter/gather support */

/************************************************/
/*												*/
/*	Inquire structures							*/
/*												*/
/************************************************/
typedef	struct	_CNT_SCSI_INQ
	{
	UCHAR	devt;						/* 00: device type */
	UCHAR	devtm;						/* 01: device type modifier */
	UCHAR	svers;						/* 02: SCSI version */
	UCHAR	rfmt;						/* 03: response data format */
	UCHAR	adlen;						/* 04: additional length of data */
	UCHAR	res1;						/* 05: */
	UCHAR	res2;						/* 06: */
	UCHAR	fncs;						/* 07: functional capabilities */
	UCHAR	vid[8];						/* 08: vendor ID */
	UCHAR	pid[16];					/* 10: product ID */
	UCHAR	rev[4];						/* 20: product revision */
	}	CNT_SCSI_INQ;

typedef	struct	_CNT_IDE_INQ
	{
	USHORT	GeneralConfiguration;		/* 00 */
	USHORT	NumberOfCylinders;			/* 02 */
	USHORT	Reserved1;					/* 04 */
	USHORT	NumberOfHeads;				/* 06 */
	USHORT	UnformattedBytesPerTrack;	/* 08 */
	USHORT	UnformattedBytesPerSector;	/* 0A */
	USHORT	SectorsPerTrack;			/* 0C */
	USHORT	VendorUnique1[3];			/* 0E */
	USHORT	SerialNumber[10];			/* 14 */
	USHORT	BufferType;					/* 28 */
	USHORT	BufferSectorSize;			/* 2A */
	USHORT	NumberOfEccBytes;			/* 2C */
	USHORT	FirmwareRevision[4];		/* 2E */
	USHORT	ModelNumber[20];			/* 36 */
	UCHAR	MaximumBlockTransfer;		/* 5E */
	UCHAR	VendorUnique2;				/* 5F */
	USHORT	DoubleWordIo;				/* 60 */
	USHORT	Capabilities;				/* 62 */
	USHORT	Reserved2;					/* 64 */
	UCHAR	VendorUnique3;				/* 66 */
	UCHAR	PioCycleTimingMode;			/* 67 */
	UCHAR	VendorUnique4;				/* 68 */
	UCHAR	DmaCycleTimingMode;			/* 69 */
	USHORT	TranslationFieldsValid;		/* 6A */
	USHORT	NumberOfCurrentCylinders;	/* 6C */
	USHORT	NumberOfCurrentHeads;		/* 6E */
	USHORT	CurrentSectorsPerTrack;		/* 70 */
	ULONG	CurrentSectorCapacity;		/* 72 */
	}	CNT_IDE_INQ;

typedef struct	_DASD_INQUIRE
	{
	ULONG	type;						/* 0 = SCSI, 1 = IDE */
	union
		{
		CNT_SCSI_INQ	scsi;			/* SCSI inquire data */
		CNT_IDE_INQ		ide;			/* IDE inquire data */
		}	inq;
	}	DASD_INQUIRE;

/************************************************/
/*												*/
/*	Device Codes								*/
/*												*/
/************************************************/
#define DEVC_DASD			0x00		/* Direct-access Storage Device */
#define DEVC_SEQACESS		0x01		/* Sequential-access device */
#define DEVC_PRINTER		0x02		/* Printer device */
#define DEVC_PROCESSOR		0x03		/* Processor device */
#define DEVC_WRITEONCE		0x04		/* Write-once device */
#define DEVC_CDROM			0x05		/* CD-ROM device */
#define DEVC_SCANNER		0x06		/* Scanner device */
#define DEVC_OPTICAL		0x07		/* Optical memory device */
#define DEVC_MEDCHGR		0x08		/* Medium changer device */
#define	DEVC_DASD_REMOVABLE	0x80		/* Direct-access storage device, Removable */
#define	DEVC_NONE			0xFF		/* no device */

// SCSI controls for RAID
#define	SC_MY_RAID			0xBF			// our special CDB command byte for Win95... interface
#define	MY_SCSI_QUERY0		0x31			// byte 1 subcommand to query driver for RAID 0 informatation
#define	MY_SCSI_QUERY1		0x32			// byte 1 subcommand to query driver for RAID 1 informatation
#define	MY_SCSI_QUERY5		0x33			// byte 1 subcommand to query driver for RAID 5 informatation
#define	MY_SCSI_REBUILD		0x40			// byte 1 subcommand to reconstruct a mirrored pair
#define MY_SCSI_DEMOFAIL	0x54			// byte 1 subcommand for RAID failure demonstration
#define	MY_SCSI_ALARMMUTE	0x60			// byte 1 subcommand to mute any alarm currently on


#endif

