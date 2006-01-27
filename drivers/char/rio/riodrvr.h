/*
** -----------------------------------------------------------------------------
**
**  Perle Specialix driver for Linux
**  Ported from existing RIO Driver for SCO sources.
 *
 *  (C) 1990 - 2000 Specialix International Ltd., Byfleet, Surrey, UK.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**	Module		: riodrvr.h
**	SID		: 1.3
**	Last Modified	: 11/6/98 09:22:46
**	Retrieved	: 11/6/98 09:22:46
**
**  ident @(#)riodrvr.h	1.3
**
** -----------------------------------------------------------------------------
*/

#ifndef __riodrvr_h
#define __riodrvr_h

#include <asm/param.h>		/* for HZ */

#ifdef SCCS_LABELS
static char *_riodrvr_h_sccs_ = "@(#)riodrvr.h	1.3";
#endif

#define MEMDUMP_SIZE	32
#define	MOD_DISABLE	(RIO_NOREAD|RIO_NOWRITE|RIO_NOXPRINT)


struct rio_info {
	int mode;		/* Intr or polled, word/byte */
	spinlock_t RIOIntrSem;	/* Interrupt thread sem */
	int current_chan;	/* current channel */
	int RIOFailed;		/* Not initialised ? */
	int RIOInstallAttempts;	/* no. of rio-install() calls */
	int RIOLastPCISearch;	/* status of last search */
	int RIONumHosts;	/* Number of RIO Hosts */
	struct Host *RIOHosts;	/* RIO Host values */
	struct Port **RIOPortp;	/* RIO port values */
/*
** 02.03.1999 ARG - ESIL 0820 fix
** We no longer use RIOBootMode
**
	int			RIOBootMode;		* RIO boot mode *
**
*/
	int RIOPrintDisabled;	/* RIO printing disabled ? */
	int RIOPrintLogState;	/* RIO printing state ? */
	int RIOPolling;		/* Polling ? */
/*
** 09.12.1998 ARG - ESIL 0776 part fix
** The 'RIO_QUICK_CHECK' ioctl was using RIOHalted.
** The fix for this ESIL introduces another member (RIORtaDisCons) here to be
** updated in RIOConCon() - to keep track of RTA connections/disconnections.
** 'RIO_QUICK_CHECK' now returns the value of RIORtaDisCons.
*/
	int RIOHalted;		/* halted ? */
	int RIORtaDisCons;	/* RTA connections/disconnections */
	uint RIOReadCheck;	/* Rio read check */
	uint RIONoMessage;	/* To display message or not */
	uint RIONumBootPkts;	/* how many packets for an RTA */
	uint RIOBootCount;	/* size of RTA code */
	uint RIOBooting;	/* count of outstanding boots */
	uint RIOSystemUp;	/* Booted ?? */
	uint RIOCounting;	/* for counting interrupts */
	uint RIOIntCount;	/* # of intr since last check */
	uint RIOTxCount;	/* number of xmit intrs  */
	uint RIORxCount;	/* number of rx intrs */
	uint RIORupCount;	/* number of rup intrs */
	int RIXTimer;
	int RIOBufferSize;	/* Buffersize */
	int RIOBufferMask;	/* Buffersize */

	int RIOFirstMajor;	/* First host card's major no */

	uint RIOLastPortsMapped;	/* highest port number known */
	uint RIOFirstPortsMapped;	/* lowest port number known */

	uint RIOLastPortsBooted;	/* highest port number running */
	uint RIOFirstPortsBooted;	/* lowest port number running */

	uint RIOLastPortsOpened;	/* highest port number running */
	uint RIOFirstPortsOpened;	/* lowest port number running */

	/* Flag to say that the topology information has been changed. */
	uint RIOQuickCheck;
	uint CdRegister;	/* ??? */
	int RIOSignalProcess;	/* Signalling process */
	int rio_debug;		/* To debug ... */
	int RIODebugWait;	/* For what ??? */
	int tpri;		/* Thread prio */
	int tid;		/* Thread id */
	uint _RIO_Polled;	/* Counter for polling */
	uint _RIO_Interrupted;	/* Counter for interrupt */
	int intr_tid;		/* iointset return value */
	int TxEnSem;		/* TxEnable Semaphore */


	struct Error RIOError;	/* to Identify what went wrong */
	struct Conf RIOConf;	/* Configuration ??? */
	struct ttystatics channel[RIO_PORTS];	/* channel information */
	char RIOBootPackets[1 + (SIXTY_FOUR_K / RTA_BOOT_DATA_SIZE)]
	    [RTA_BOOT_DATA_SIZE];
	struct Map RIOConnectTable[TOTAL_MAP_ENTRIES];
	struct Map RIOSavedTable[TOTAL_MAP_ENTRIES];

	/* RTA to host binding table for master/slave operation */
	ulong RIOBindTab[MAX_RTA_BINDINGS];
	/* RTA memory dump variable */
	uchar RIOMemDump[MEMDUMP_SIZE];
	struct ModuleInfo RIOModuleTypes[MAX_MODULE_TYPES];

};


#ifdef linux
#define debug(x)        printk x
#else
#define debug(x)	kkprintf x
#endif



#define RIO_RESET_INT	0x7d80
#define WRBYTE(x,y)		*(volatile unsigned char *)((x)) = \
					(unsigned char)(y)

#endif				/* __riodrvr.h */
