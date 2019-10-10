/* SPDX-License-Identifier: GPL-2.0-or-later */
/***************************************************************************
                          dpti_ioctl.h  -  description
                             -------------------
    begin                : Thu Sep 7 2000
    copyright            : (C) 2001 by Adaptec

    See Documentation/scsi/dpti.txt for history, notes, license info
    and credits
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *                                                                         *
 ***************************************************************************/

/***************************************************************************
 * This file is generated from  osd_unix.h                                 *
 * *************************************************************************/

#ifndef _dpti_ioctl_h
#define _dpti_ioctl_h

// IOCTL interface commands

#ifndef _IOWR
# define _IOWR(x,y,z)	(((x)<<8)|y)
#endif
#ifndef _IOW
# define _IOW(x,y,z)	(((x)<<8)|y)
#endif
#ifndef _IOR
# define _IOR(x,y,z)	(((x)<<8)|y)
#endif
#ifndef _IO
# define _IO(x,y)	(((x)<<8)|y)
#endif
/* EATA PassThrough Command	*/
#define EATAUSRCMD      _IOWR('D',65,EATA_CP)
/* Set Debug Level If Enabled	*/
#define DPT_DEBUG       _IOW('D',66,int)
/* Get Signature Structure	*/
#define DPT_SIGNATURE   _IOR('D',67,dpt_sig_S)
#if defined __bsdi__
#define DPT_SIGNATURE_PACKED   _IOR('D',67,dpt_sig_S_Packed)
#endif
/* Get Number Of DPT Adapters	*/
#define DPT_NUMCTRLS    _IOR('D',68,int)
/* Get Adapter Info Structure	*/
#define DPT_CTRLINFO    _IOR('D',69,CtrlInfo)
/* Get Statistics If Enabled	*/
#define DPT_STATINFO    _IO('D',70)
/* Clear Stats If Enabled	*/
#define DPT_CLRSTAT     _IO('D',71)
/* Get System Info Structure	*/
#define DPT_SYSINFO     _IOR('D',72,sysInfo_S)
/* Set Timeout Value		*/
#define DPT_TIMEOUT     _IO('D',73)
/* Get config Data  		*/
#define DPT_CONFIG      _IO('D',74)
/* Get Blink LED Code	        */
#define DPT_BLINKLED    _IOR('D',75,int)
/* Get Statistical information (if available) */
#define DPT_STATS_INFO        _IOR('D',80,STATS_DATA)
/* Clear the statistical information          */
#define DPT_STATS_CLEAR       _IO('D',81)
/* Get Performance metrics */
#define DPT_PERF_INFO        _IOR('D',82,dpt_perf_t)
/* Send an I2O command */
#define I2OUSRCMD	_IO('D',76)
/* Inform driver to re-acquire LCT information */
#define I2ORESCANCMD	_IO('D',77)
/* Inform driver to reset adapter */
#define I2ORESETCMD	_IO('D',78)
/* See if the target is mounted */
#define DPT_TARGET_BUSY	_IOR('D',79, TARGET_BUSY_T)


  /* Structure Returned From Get Controller Info                             */

typedef struct {
	uCHAR    state;            /* Operational state               */
	uCHAR    id;               /* Host adapter SCSI id            */
	int      vect;             /* Interrupt vector number         */
	int      base;             /* Base I/O address                */
	int      njobs;            /* # of jobs sent to HA            */
	int      qdepth;           /* Controller queue depth.         */
	int      wakebase;         /* mpx wakeup base index.          */
	uINT     SGsize;           /* Scatter/Gather list size.       */
	unsigned heads;            /* heads for drives on cntlr.      */
	unsigned sectors;          /* sectors for drives on cntlr.    */
	uCHAR    do_drive32;       /* Flag for Above 16 MB Ability    */
	uCHAR    BusQuiet;         /* SCSI Bus Quiet Flag             */
	char     idPAL[4];         /* 4 Bytes Of The ID Pal           */
	uCHAR    primary;          /* 1 For Primary, 0 For Secondary  */
	uCHAR    eataVersion;      /* EATA Version                    */
	uINT     cpLength;         /* EATA Command Packet Length      */
	uINT     spLength;         /* EATA Status Packet Length       */
	uCHAR    drqNum;           /* DRQ Index (0,5,6,7)             */
	uCHAR    flag1;            /* EATA Flags 1 (Byte 9)           */
	uCHAR    flag2;            /* EATA Flags 2 (Byte 30)          */
} CtrlInfo;

typedef struct {
	uSHORT length;		// Remaining length of this
	uSHORT drvrHBAnum;	// Relative HBA # used by the driver
	uINT baseAddr;		// Base I/O address
	uSHORT blinkState;	// Blink LED state (0=Not in blink LED)
	uCHAR pciBusNum;	// PCI Bus # (Optional)
	uCHAR pciDeviceNum;	// PCI Device # (Optional)
	uSHORT hbaFlags;	// Miscellaneous HBA flags
	uSHORT Interrupt;	// Interrupt set for this device.
#   if (defined(_DPT_ARC))
	uINT baseLength;
	ADAPTER_OBJECT *AdapterObject;
	LARGE_INTEGER DmaLogicalAddress;
	PVOID DmaVirtualAddress;
	LARGE_INTEGER ReplyLogicalAddress;
	PVOID ReplyVirtualAddress;
#   else
	uINT reserved1;		// Reserved for future expansion
	uINT reserved2;		// Reserved for future expansion
	uINT reserved3;		// Reserved for future expansion
#   endif
} drvrHBAinfo_S;

typedef struct TARGET_BUSY
{
  uLONG channel;
  uLONG id;
  uLONG lun;
  uLONG isBusy;
} TARGET_BUSY_T;

#endif

