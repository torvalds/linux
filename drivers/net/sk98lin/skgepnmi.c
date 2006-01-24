/*****************************************************************************
 *
 * Name:	skgepnmi.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.111 $
 * Date:	$Date: 2003/09/15 13:35:35 $
 * Purpose:	Private Network Management Interface
 *
 ****************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect GmbH.
 *	(C)Copyright 2002-2003 Marvell.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/


#ifndef _lint
static const char SysKonnectFileId[] =
	"@(#) $Id: skgepnmi.c,v 1.111 2003/09/15 13:35:35 tschilli Exp $ (C) Marvell.";
#endif /* !_lint */

#include "h/skdrv1st.h"
#include "h/sktypes.h"
#include "h/xmac_ii.h"
#include "h/skdebug.h"
#include "h/skqueue.h"
#include "h/skgepnmi.h"
#include "h/skgesirq.h"
#include "h/skcsum.h"
#include "h/skvpd.h"
#include "h/skgehw.h"
#include "h/skgeinit.h"
#include "h/skdrv2nd.h"
#include "h/skgepnm2.h"
#ifdef SK_POWER_MGMT
#include "h/skgepmgt.h"
#endif
/* defines *******************************************************************/

#ifndef DEBUG
#define PNMI_STATIC	static
#else	/* DEBUG */
#define PNMI_STATIC
#endif /* DEBUG */

/*
 * Public Function prototypes
 */
int SkPnmiInit(SK_AC *pAC, SK_IOC IoC, int level);
int SkPnmiGetVar(SK_AC *pAC, SK_IOC IoC, SK_U32 Id, void *pBuf,
	unsigned int *pLen, SK_U32 Instance, SK_U32 NetIndex);
int SkPnmiPreSetVar(SK_AC *pAC, SK_IOC IoC, SK_U32 Id, void *pBuf,
	unsigned int *pLen, SK_U32 Instance, SK_U32 NetIndex);
int SkPnmiSetVar(SK_AC *pAC, SK_IOC IoC, SK_U32 Id, void *pBuf,
	unsigned int *pLen, SK_U32 Instance, SK_U32 NetIndex);
int SkPnmiGetStruct(SK_AC *pAC, SK_IOC IoC, void *pBuf,
	unsigned int *pLen, SK_U32 NetIndex);
int SkPnmiPreSetStruct(SK_AC *pAC, SK_IOC IoC, void *pBuf,
	unsigned int *pLen, SK_U32 NetIndex);
int SkPnmiSetStruct(SK_AC *pAC, SK_IOC IoC, void *pBuf,
	unsigned int *pLen, SK_U32 NetIndex);
int SkPnmiEvent(SK_AC *pAC, SK_IOC IoC, SK_U32 Event, SK_EVPARA Param);
int SkPnmiGenIoctl(SK_AC *pAC, SK_IOC IoC, void * pBuf,
	unsigned int * pLen, SK_U32 NetIndex);


/*
 * Private Function prototypes
 */

PNMI_STATIC SK_U8 CalculateLinkModeStatus(SK_AC *pAC, SK_IOC IoC, unsigned int
	PhysPortIndex);
PNMI_STATIC SK_U8 CalculateLinkStatus(SK_AC *pAC, SK_IOC IoC, unsigned int
	PhysPortIndex);
PNMI_STATIC void CopyMac(char *pDst, SK_MAC_ADDR *pMac);
PNMI_STATIC void CopyTrapQueue(SK_AC *pAC, char *pDstBuf);
PNMI_STATIC SK_U64 GetPhysStatVal(SK_AC *pAC, SK_IOC IoC,
	unsigned int PhysPortIndex, unsigned int StatIndex);
PNMI_STATIC SK_U64 GetStatVal(SK_AC *pAC, SK_IOC IoC, unsigned int LogPortIndex,
	unsigned int StatIndex, SK_U32 NetIndex);
PNMI_STATIC char* GetTrapEntry(SK_AC *pAC, SK_U32 TrapId, unsigned int Size);
PNMI_STATIC void GetTrapQueueLen(SK_AC *pAC, unsigned int *pLen,
	unsigned int *pEntries);
PNMI_STATIC int GetVpdKeyArr(SK_AC *pAC, SK_IOC IoC, char *pKeyArr,
	unsigned int KeyArrLen, unsigned int *pKeyNo);
PNMI_STATIC int LookupId(SK_U32 Id);
PNMI_STATIC int MacUpdate(SK_AC *pAC, SK_IOC IoC, unsigned int FirstMac,
	unsigned int LastMac);
PNMI_STATIC int PnmiStruct(SK_AC *pAC, SK_IOC IoC, int Action, char *pBuf,
	unsigned int *pLen, SK_U32 NetIndex);
PNMI_STATIC int PnmiVar(SK_AC *pAC, SK_IOC IoC, int Action, SK_U32 Id,
	char *pBuf, unsigned int *pLen, SK_U32 Instance, SK_U32 NetIndex);
PNMI_STATIC void QueueRlmtNewMacTrap(SK_AC *pAC, unsigned int ActiveMac);
PNMI_STATIC void QueueRlmtPortTrap(SK_AC *pAC, SK_U32 TrapId,
	unsigned int PortIndex);
PNMI_STATIC void QueueSensorTrap(SK_AC *pAC, SK_U32 TrapId,
	unsigned int SensorIndex);
PNMI_STATIC void QueueSimpleTrap(SK_AC *pAC, SK_U32 TrapId);
PNMI_STATIC void ResetCounter(SK_AC *pAC, SK_IOC IoC, SK_U32 NetIndex);
PNMI_STATIC int RlmtUpdate(SK_AC *pAC, SK_IOC IoC, SK_U32 NetIndex);
PNMI_STATIC int SirqUpdate(SK_AC *pAC, SK_IOC IoC);
PNMI_STATIC void VirtualConf(SK_AC *pAC, SK_IOC IoC, SK_U32 Id, char *pBuf);
PNMI_STATIC int Vct(SK_AC *pAC, SK_IOC IoC, int Action, SK_U32 Id, char *pBuf,
	unsigned int *pLen, SK_U32 Instance, unsigned int TableIndex, SK_U32 NetIndex);
PNMI_STATIC void CheckVctStatus(SK_AC *, SK_IOC, char *, SK_U32, SK_U32);

/*
 * Table to correlate OID with handler function and index to
 * hardware register stored in StatAddress if applicable.
 */
#include "skgemib.c"

/* global variables **********************************************************/

/*
 * Overflow status register bit table and corresponding counter
 * dependent on MAC type - the number relates to the size of overflow
 * mask returned by the pFnMacOverflow function
 */
PNMI_STATIC const SK_U16 StatOvrflwBit[][SK_PNMI_MAC_TYPES] = {
/* Bit0  */	{ SK_PNMI_HTX, 				SK_PNMI_HTX_UNICAST},
/* Bit1  */	{ SK_PNMI_HTX_OCTETHIGH, 	SK_PNMI_HTX_BROADCAST},
/* Bit2  */	{ SK_PNMI_HTX_OCTETLOW, 	SK_PNMI_HTX_PMACC},
/* Bit3  */	{ SK_PNMI_HTX_BROADCAST, 	SK_PNMI_HTX_MULTICAST},
/* Bit4  */	{ SK_PNMI_HTX_MULTICAST, 	SK_PNMI_HTX_OCTETLOW},
/* Bit5  */	{ SK_PNMI_HTX_UNICAST, 		SK_PNMI_HTX_OCTETHIGH},
/* Bit6  */	{ SK_PNMI_HTX_LONGFRAMES, 	SK_PNMI_HTX_64},
/* Bit7  */	{ SK_PNMI_HTX_BURST, 		SK_PNMI_HTX_127},
/* Bit8  */	{ SK_PNMI_HTX_PMACC, 		SK_PNMI_HTX_255},
/* Bit9  */	{ SK_PNMI_HTX_MACC, 		SK_PNMI_HTX_511},
/* Bit10 */	{ SK_PNMI_HTX_SINGLE_COL, 	SK_PNMI_HTX_1023},
/* Bit11 */	{ SK_PNMI_HTX_MULTI_COL, 	SK_PNMI_HTX_MAX},
/* Bit12 */	{ SK_PNMI_HTX_EXCESS_COL, 	SK_PNMI_HTX_LONGFRAMES},
/* Bit13 */	{ SK_PNMI_HTX_LATE_COL, 	SK_PNMI_HTX_RESERVED},
/* Bit14 */	{ SK_PNMI_HTX_DEFFERAL, 	SK_PNMI_HTX_COL},
/* Bit15 */	{ SK_PNMI_HTX_EXCESS_DEF, 	SK_PNMI_HTX_LATE_COL},
/* Bit16 */	{ SK_PNMI_HTX_UNDERRUN, 	SK_PNMI_HTX_EXCESS_COL},
/* Bit17 */	{ SK_PNMI_HTX_CARRIER, 		SK_PNMI_HTX_MULTI_COL},
/* Bit18 */	{ SK_PNMI_HTX_UTILUNDER, 	SK_PNMI_HTX_SINGLE_COL},
/* Bit19 */	{ SK_PNMI_HTX_UTILOVER, 	SK_PNMI_HTX_UNDERRUN},
/* Bit20 */	{ SK_PNMI_HTX_64, 			SK_PNMI_HTX_RESERVED},
/* Bit21 */	{ SK_PNMI_HTX_127, 			SK_PNMI_HTX_RESERVED},
/* Bit22 */	{ SK_PNMI_HTX_255, 			SK_PNMI_HTX_RESERVED},
/* Bit23 */	{ SK_PNMI_HTX_511, 			SK_PNMI_HTX_RESERVED},
/* Bit24 */	{ SK_PNMI_HTX_1023, 		SK_PNMI_HTX_RESERVED},
/* Bit25 */	{ SK_PNMI_HTX_MAX, 			SK_PNMI_HTX_RESERVED},
/* Bit26 */	{ SK_PNMI_HTX_RESERVED, 	SK_PNMI_HTX_RESERVED},
/* Bit27 */	{ SK_PNMI_HTX_RESERVED, 	SK_PNMI_HTX_RESERVED},
/* Bit28 */	{ SK_PNMI_HTX_RESERVED, 	SK_PNMI_HTX_RESERVED},
/* Bit29 */	{ SK_PNMI_HTX_RESERVED, 	SK_PNMI_HTX_RESERVED},
/* Bit30 */	{ SK_PNMI_HTX_RESERVED, 	SK_PNMI_HTX_RESERVED},
/* Bit31 */	{ SK_PNMI_HTX_RESERVED, 	SK_PNMI_HTX_RESERVED},
/* Bit32 */	{ SK_PNMI_HRX, 				SK_PNMI_HRX_UNICAST},
/* Bit33 */	{ SK_PNMI_HRX_OCTETHIGH, 	SK_PNMI_HRX_BROADCAST},
/* Bit34 */	{ SK_PNMI_HRX_OCTETLOW, 	SK_PNMI_HRX_PMACC},
/* Bit35 */	{ SK_PNMI_HRX_BROADCAST, 	SK_PNMI_HRX_MULTICAST},
/* Bit36 */	{ SK_PNMI_HRX_MULTICAST, 	SK_PNMI_HRX_FCS},
/* Bit37 */	{ SK_PNMI_HRX_UNICAST, 		SK_PNMI_HRX_RESERVED},
/* Bit38 */	{ SK_PNMI_HRX_PMACC, 		SK_PNMI_HRX_OCTETLOW},
/* Bit39 */	{ SK_PNMI_HRX_MACC, 		SK_PNMI_HRX_OCTETHIGH},
/* Bit40 */	{ SK_PNMI_HRX_PMACC_ERR, 	SK_PNMI_HRX_BADOCTETLOW},
/* Bit41 */	{ SK_PNMI_HRX_MACC_UNKWN,	SK_PNMI_HRX_BADOCTETHIGH},
/* Bit42 */	{ SK_PNMI_HRX_BURST, 		SK_PNMI_HRX_UNDERSIZE},
/* Bit43 */	{ SK_PNMI_HRX_MISSED, 		SK_PNMI_HRX_RUNT},
/* Bit44 */	{ SK_PNMI_HRX_FRAMING, 		SK_PNMI_HRX_64},
/* Bit45 */	{ SK_PNMI_HRX_OVERFLOW, 	SK_PNMI_HRX_127},
/* Bit46 */	{ SK_PNMI_HRX_JABBER, 		SK_PNMI_HRX_255},
/* Bit47 */	{ SK_PNMI_HRX_CARRIER, 		SK_PNMI_HRX_511},
/* Bit48 */	{ SK_PNMI_HRX_IRLENGTH, 	SK_PNMI_HRX_1023},
/* Bit49 */	{ SK_PNMI_HRX_SYMBOL, 		SK_PNMI_HRX_MAX},
/* Bit50 */	{ SK_PNMI_HRX_SHORTS, 		SK_PNMI_HRX_LONGFRAMES},
/* Bit51 */	{ SK_PNMI_HRX_RUNT, 		SK_PNMI_HRX_TOO_LONG},
/* Bit52 */	{ SK_PNMI_HRX_TOO_LONG, 	SK_PNMI_HRX_JABBER},
/* Bit53 */	{ SK_PNMI_HRX_FCS, 			SK_PNMI_HRX_RESERVED},
/* Bit54 */	{ SK_PNMI_HRX_RESERVED, 	SK_PNMI_HRX_OVERFLOW},
/* Bit55 */	{ SK_PNMI_HRX_CEXT, 		SK_PNMI_HRX_RESERVED},
/* Bit56 */	{ SK_PNMI_HRX_UTILUNDER, 	SK_PNMI_HRX_RESERVED},
/* Bit57 */	{ SK_PNMI_HRX_UTILOVER, 	SK_PNMI_HRX_RESERVED},
/* Bit58 */	{ SK_PNMI_HRX_64, 			SK_PNMI_HRX_RESERVED},
/* Bit59 */	{ SK_PNMI_HRX_127, 			SK_PNMI_HRX_RESERVED},
/* Bit60 */	{ SK_PNMI_HRX_255, 			SK_PNMI_HRX_RESERVED},
/* Bit61 */	{ SK_PNMI_HRX_511, 			SK_PNMI_HRX_RESERVED},
/* Bit62 */	{ SK_PNMI_HRX_1023, 		SK_PNMI_HRX_RESERVED},
/* Bit63 */	{ SK_PNMI_HRX_MAX, 			SK_PNMI_HRX_RESERVED}
};

/*
 * Table for hardware register saving on resets and port switches
 */
PNMI_STATIC const SK_PNMI_STATADDR StatAddr[SK_PNMI_MAX_IDX][SK_PNMI_MAC_TYPES] = {
	/* SK_PNMI_HTX */
	{{XM_TXF_OK, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HTX_OCTETHIGH */
	{{XM_TXO_OK_HI, SK_TRUE}, {GM_TXO_OK_HI, SK_TRUE}},
	/* SK_PNMI_HTX_OCTETLOW */
	{{XM_TXO_OK_LO, SK_FALSE}, {GM_TXO_OK_LO, SK_FALSE}},
	/* SK_PNMI_HTX_BROADCAST */
	{{XM_TXF_BC_OK, SK_TRUE}, {GM_TXF_BC_OK, SK_TRUE}},
	/* SK_PNMI_HTX_MULTICAST */
	{{XM_TXF_MC_OK, SK_TRUE}, {GM_TXF_MC_OK, SK_TRUE}},
	/* SK_PNMI_HTX_UNICAST */
	{{XM_TXF_UC_OK, SK_TRUE}, {GM_TXF_UC_OK, SK_TRUE}},
	/* SK_PNMI_HTX_BURST */
	{{XM_TXE_BURST, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HTX_PMACC */
	{{XM_TXF_MPAUSE, SK_TRUE}, {GM_TXF_MPAUSE, SK_TRUE}},
	/* SK_PNMI_HTX_MACC */
	{{XM_TXF_MCTRL, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HTX_COL */
	{{0, SK_FALSE}, {GM_TXF_COL, SK_TRUE}},
	/* SK_PNMI_HTX_SINGLE_COL */
	{{XM_TXF_SNG_COL, SK_TRUE}, {GM_TXF_SNG_COL, SK_TRUE}},
	/* SK_PNMI_HTX_MULTI_COL */
	{{XM_TXF_MUL_COL, SK_TRUE}, {GM_TXF_MUL_COL, SK_TRUE}},
	/* SK_PNMI_HTX_EXCESS_COL */
	{{XM_TXF_ABO_COL, SK_TRUE}, {GM_TXF_ABO_COL, SK_TRUE}},
	/* SK_PNMI_HTX_LATE_COL */
	{{XM_TXF_LAT_COL, SK_TRUE}, {GM_TXF_LAT_COL, SK_TRUE}},
	/* SK_PNMI_HTX_DEFFERAL */
	{{XM_TXF_DEF, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HTX_EXCESS_DEF */
	{{XM_TXF_EX_DEF, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HTX_UNDERRUN */
	{{XM_TXE_FIFO_UR, SK_TRUE}, {GM_TXE_FIFO_UR, SK_TRUE}},
	/* SK_PNMI_HTX_CARRIER */
	{{XM_TXE_CS_ERR, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HTX_UTILUNDER */
	{{0, SK_FALSE}, {0, SK_FALSE}},
	/* SK_PNMI_HTX_UTILOVER */
	{{0, SK_FALSE}, {0, SK_FALSE}},
	/* SK_PNMI_HTX_64 */
	{{XM_TXF_64B, SK_TRUE}, {GM_TXF_64B, SK_TRUE}},
	/* SK_PNMI_HTX_127 */
	{{XM_TXF_127B, SK_TRUE}, {GM_TXF_127B, SK_TRUE}},
	/* SK_PNMI_HTX_255 */
	{{XM_TXF_255B, SK_TRUE}, {GM_TXF_255B, SK_TRUE}},
	/* SK_PNMI_HTX_511 */
	{{XM_TXF_511B, SK_TRUE}, {GM_TXF_511B, SK_TRUE}},
	/* SK_PNMI_HTX_1023 */
	{{XM_TXF_1023B, SK_TRUE}, {GM_TXF_1023B, SK_TRUE}},
	/* SK_PNMI_HTX_MAX */
	{{XM_TXF_MAX_SZ, SK_TRUE}, {GM_TXF_1518B, SK_TRUE}},
	/* SK_PNMI_HTX_LONGFRAMES  */
	{{XM_TXF_LONG, SK_TRUE}, {GM_TXF_MAX_SZ, SK_TRUE}},
	/* SK_PNMI_HTX_SYNC */
	{{0, SK_FALSE}, {0, SK_FALSE}},
	/* SK_PNMI_HTX_SYNC_OCTET */
	{{0, SK_FALSE}, {0, SK_FALSE}},
	/* SK_PNMI_HTX_RESERVED */
	{{0, SK_FALSE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX */
	{{XM_RXF_OK, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_OCTETHIGH */
	{{XM_RXO_OK_HI, SK_TRUE}, {GM_RXO_OK_HI, SK_TRUE}},
	/* SK_PNMI_HRX_OCTETLOW */
	{{XM_RXO_OK_LO, SK_FALSE}, {GM_RXO_OK_LO, SK_FALSE}},
	/* SK_PNMI_HRX_BADOCTETHIGH */
	{{0, SK_FALSE}, {GM_RXO_ERR_HI, SK_TRUE}},
	/* SK_PNMI_HRX_BADOCTETLOW */
	{{0, SK_FALSE}, {GM_RXO_ERR_LO, SK_TRUE}},
	/* SK_PNMI_HRX_BROADCAST */
	{{XM_RXF_BC_OK, SK_TRUE}, {GM_RXF_BC_OK, SK_TRUE}},
	/* SK_PNMI_HRX_MULTICAST */
	{{XM_RXF_MC_OK, SK_TRUE}, {GM_RXF_MC_OK, SK_TRUE}},
	/* SK_PNMI_HRX_UNICAST */
	{{XM_RXF_UC_OK, SK_TRUE}, {GM_RXF_UC_OK, SK_TRUE}},
	/* SK_PNMI_HRX_PMACC */
	{{XM_RXF_MPAUSE, SK_TRUE}, {GM_RXF_MPAUSE, SK_TRUE}},
	/* SK_PNMI_HRX_MACC */
	{{XM_RXF_MCTRL, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_PMACC_ERR */
	{{XM_RXF_INV_MP, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_MACC_UNKWN */
	{{XM_RXF_INV_MOC, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_BURST */
	{{XM_RXE_BURST, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_MISSED */
	{{XM_RXE_FMISS, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_FRAMING */
	{{XM_RXF_FRA_ERR, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_UNDERSIZE */
	{{0, SK_FALSE}, {GM_RXF_SHT, SK_TRUE}},
	/* SK_PNMI_HRX_OVERFLOW */
	{{XM_RXE_FIFO_OV, SK_TRUE}, {GM_RXE_FIFO_OV, SK_TRUE}},
	/* SK_PNMI_HRX_JABBER */
	{{XM_RXF_JAB_PKT, SK_TRUE}, {GM_RXF_JAB_PKT, SK_TRUE}},
	/* SK_PNMI_HRX_CARRIER */
	{{XM_RXE_CAR_ERR, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_IRLENGTH */
	{{XM_RXF_LEN_ERR, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_SYMBOL */
	{{XM_RXE_SYM_ERR, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_SHORTS */
	{{XM_RXE_SHT_ERR, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_RUNT */
	{{XM_RXE_RUNT, SK_TRUE}, {GM_RXE_FRAG, SK_TRUE}},
	/* SK_PNMI_HRX_TOO_LONG */
	{{XM_RXF_LNG_ERR, SK_TRUE}, {GM_RXF_LNG_ERR, SK_TRUE}},
	/* SK_PNMI_HRX_FCS */
	{{XM_RXF_FCS_ERR, SK_TRUE}, {GM_RXF_FCS_ERR, SK_TRUE}},
	/* SK_PNMI_HRX_CEXT */
	{{XM_RXF_CEX_ERR, SK_TRUE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_UTILUNDER */
	{{0, SK_FALSE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_UTILOVER */
	{{0, SK_FALSE}, {0, SK_FALSE}},
	/* SK_PNMI_HRX_64 */
	{{XM_RXF_64B, SK_TRUE}, {GM_RXF_64B, SK_TRUE}},
	/* SK_PNMI_HRX_127 */
	{{XM_RXF_127B, SK_TRUE}, {GM_RXF_127B, SK_TRUE}},
	/* SK_PNMI_HRX_255 */
	{{XM_RXF_255B, SK_TRUE}, {GM_RXF_255B, SK_TRUE}},
	/* SK_PNMI_HRX_511 */
	{{XM_RXF_511B, SK_TRUE}, {GM_RXF_511B, SK_TRUE}},
	/* SK_PNMI_HRX_1023 */
	{{XM_RXF_1023B, SK_TRUE}, {GM_RXF_1023B, SK_TRUE}},
	/* SK_PNMI_HRX_MAX */
	{{XM_RXF_MAX_SZ, SK_TRUE}, {GM_RXF_1518B, SK_TRUE}},
	/* SK_PNMI_HRX_LONGFRAMES */
	{{0, SK_FALSE}, {GM_RXF_MAX_SZ, SK_TRUE}},
	/* SK_PNMI_HRX_RESERVED */
	{{0, SK_FALSE}, {0, SK_FALSE}}
};


/*****************************************************************************
 *
 * Public functions
 *
 */

/*****************************************************************************
 *
 * SkPnmiInit - Init function of PNMI
 *
 * Description:
 *	SK_INIT_DATA: Initialises the data structures
 *	SK_INIT_IO:   Resets the XMAC statistics, determines the device and
 *	              connector type.
 *	SK_INIT_RUN:  Starts a timer event for port switch per hour
 *	              calculation.
 *
 * Returns:
 *	Always 0
 */
int SkPnmiInit(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Level)		/* Initialization level */
{
	unsigned int	PortMax;	/* Number of ports */
	unsigned int	PortIndex;	/* Current port index in loop */
	SK_U16		Val16;		/* Multiple purpose 16 bit variable */
	SK_U8		Val8;		/* Mulitple purpose 8 bit variable */
	SK_EVPARA	EventParam;	/* Event struct for timer event */
	SK_PNMI_VCT	*pVctBackupData;


	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiInit: Called, level=%d\n", Level));

	switch (Level) {

	case SK_INIT_DATA:
		SK_MEMSET((char *)&pAC->Pnmi, 0, sizeof(pAC->Pnmi));
		pAC->Pnmi.TrapBufFree = SK_PNMI_TRAP_QUEUE_LEN;
		pAC->Pnmi.StartUpTime = SK_PNMI_HUNDREDS_SEC(SkOsGetTime(pAC));
		pAC->Pnmi.RlmtChangeThreshold = SK_PNMI_DEF_RLMT_CHG_THRES;
		for (PortIndex = 0; PortIndex < SK_MAX_MACS; PortIndex ++) {

			pAC->Pnmi.Port[PortIndex].ActiveFlag = SK_FALSE;
			pAC->Pnmi.DualNetActiveFlag = SK_FALSE;
		}

#ifdef SK_PNMI_CHECK
		if (SK_PNMI_MAX_IDX != SK_PNMI_CNT_NO) {
			
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR049, SK_PNMI_ERR049MSG);

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_INIT | SK_DBGCAT_FATAL,
					   ("CounterOffset struct size (%d) differs from"
						"SK_PNMI_MAX_IDX (%d)\n",
						SK_PNMI_CNT_NO, SK_PNMI_MAX_IDX));
		}

		if (SK_PNMI_MAX_IDX !=
			(sizeof(StatAddr) / (sizeof(SK_PNMI_STATADDR) * SK_PNMI_MAC_TYPES))) {
			
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR050, SK_PNMI_ERR050MSG);

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_INIT | SK_DBGCAT_FATAL,
					   ("StatAddr table size (%d) differs from "
						"SK_PNMI_MAX_IDX (%d)\n",
						(sizeof(StatAddr) /
						 (sizeof(SK_PNMI_STATADDR) * SK_PNMI_MAC_TYPES)),
						 SK_PNMI_MAX_IDX));
		}
#endif /* SK_PNMI_CHECK */
		break;

	case SK_INIT_IO:
		/*
		 * Reset MAC counters
		 */
		PortMax = pAC->GIni.GIMacsFound;

		for (PortIndex = 0; PortIndex < PortMax; PortIndex ++) {

			pAC->GIni.GIFunc.pFnMacResetCounter(pAC, IoC, PortIndex);
		}
		
		/* Initialize DSP variables for Vct() to 0xff => Never written! */		
		for (PortIndex = 0; PortIndex < PortMax; PortIndex ++) {
			pAC->GIni.GP[PortIndex].PCableLen = 0xff;
			pVctBackupData = &pAC->Pnmi.VctBackup[PortIndex];
			pVctBackupData->PCableLen = 0xff;
		}
		
		/*
		 * Get pci bus speed
		 */
		SK_IN16(IoC, B0_CTST, &Val16);
		if ((Val16 & CS_BUS_CLOCK) == 0) {

			pAC->Pnmi.PciBusSpeed = 33;
		}
		else {
			pAC->Pnmi.PciBusSpeed = 66;
		}

		/*
		 * Get pci bus width
		 */
		SK_IN16(IoC, B0_CTST, &Val16);
		if ((Val16 & CS_BUS_SLOT_SZ) == 0) {

			pAC->Pnmi.PciBusWidth = 32;
		}
		else {
			pAC->Pnmi.PciBusWidth = 64;
		}

		/*
		 * Get chipset
		 */
		switch (pAC->GIni.GIChipId) {
		case CHIP_ID_GENESIS:
			pAC->Pnmi.Chipset = SK_PNMI_CHIPSET_XMAC;
			break;

		case CHIP_ID_YUKON:
			pAC->Pnmi.Chipset = SK_PNMI_CHIPSET_YUKON;
			break;

		default:
			break;
		}

		/*
		 * Get PMD and DeviceType
		 */
		SK_IN8(IoC, B2_PMD_TYP, &Val8);
		switch (Val8) {
		case 'S':
			pAC->Pnmi.PMD = 3;
			if (pAC->GIni.GIMacsFound > 1) {

				pAC->Pnmi.DeviceType = 0x00020002;
			}
			else {
				pAC->Pnmi.DeviceType = 0x00020001;
			}
			break;

		case 'L':
			pAC->Pnmi.PMD = 2;
			if (pAC->GIni.GIMacsFound > 1) {

				pAC->Pnmi.DeviceType = 0x00020004;
			}
			else {
				pAC->Pnmi.DeviceType = 0x00020003;
			}
			break;

		case 'C':
			pAC->Pnmi.PMD = 4;
			if (pAC->GIni.GIMacsFound > 1) {

				pAC->Pnmi.DeviceType = 0x00020006;
			}
			else {
				pAC->Pnmi.DeviceType = 0x00020005;
			}
			break;

		case 'T':
			pAC->Pnmi.PMD = 5;
			if (pAC->GIni.GIMacsFound > 1) {

				pAC->Pnmi.DeviceType = 0x00020008;
			}
			else {
				pAC->Pnmi.DeviceType = 0x00020007;
			}
			break;

		default :
			pAC->Pnmi.PMD = 1;
			pAC->Pnmi.DeviceType = 0;
			break;
		}

		/*
		 * Get connector
		 */
		SK_IN8(IoC, B2_CONN_TYP, &Val8);
		switch (Val8) {
		case 'C':
			pAC->Pnmi.Connector = 2;
			break;

		case 'D':
			pAC->Pnmi.Connector = 3;
			break;

		case 'F':
			pAC->Pnmi.Connector = 4;
			break;

		case 'J':
			pAC->Pnmi.Connector = 5;
			break;

		case 'V':
			pAC->Pnmi.Connector = 6;
			break;

		default:
			pAC->Pnmi.Connector = 1;
			break;
		}
		break;

	case SK_INIT_RUN:
		/*
		 * Start timer for RLMT change counter
		 */
		SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));
		SkTimerStart(pAC, IoC, &pAC->Pnmi.RlmtChangeEstimate.EstTimer,
			28125000, SKGE_PNMI, SK_PNMI_EVT_CHG_EST_TIMER,
			EventParam);
		break;

	default:
		break; /* Nothing todo */
	}

	return (0);
}

/*****************************************************************************
 *
 * SkPnmiGetVar - Retrieves the value of a single OID
 *
 * Description:
 *	Calls a general sub-function for all this stuff. If the instance
 *	-1 is passed, the values of all instances are returned in an
 *	array of values.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to take
 *	                         the data.
 *	SK_PNMI_ERR_UNKNOWN_OID  The requested OID is unknown
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
int SkPnmiGetVar(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
SK_U32 Id,		/* Object ID that is to be processed */
void *pBuf,		/* Buffer to which the management data will be copied */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiGetVar: Called, Id=0x%x, BufLen=%d, Instance=%d, NetIndex=%d\n",
			Id, *pLen, Instance, NetIndex));

	return (PnmiVar(pAC, IoC, SK_PNMI_GET, Id, (char *)pBuf, pLen,
		Instance, NetIndex));
}

/*****************************************************************************
 *
 * SkPnmiPreSetVar - Presets the value of a single OID
 *
 * Description:
 *	Calls a general sub-function for all this stuff. The preset does
 *	the same as a set, but returns just before finally setting the
 *	new value. This is useful to check if a set might be successfull.
 *	If the instance -1 is passed, an array of values is supposed and
 *	all instances of the OID will be set.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_OID  The requested OID is unknown.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
int SkPnmiPreSetVar(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
SK_U32 Id,		/* Object ID that is to be processed */
void *pBuf,		/* Buffer to which the management data will be copied */
unsigned int *pLen,	/* Total length of management data */
SK_U32 Instance,	/* Instance (1..n) that is to be set or -1 */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiPreSetVar: Called, Id=0x%x, BufLen=%d, Instance=%d, NetIndex=%d\n",
			Id, *pLen, Instance, NetIndex));


	return (PnmiVar(pAC, IoC, SK_PNMI_PRESET, Id, (char *)pBuf, pLen,
		Instance, NetIndex));
}

/*****************************************************************************
 *
 * SkPnmiSetVar - Sets the value of a single OID
 *
 * Description:
 *	Calls a general sub-function for all this stuff. The preset does
 *	the same as a set, but returns just before finally setting the
 *	new value. This is useful to check if a set might be successfull.
 *	If the instance -1 is passed, an array of values is supposed and
 *	all instances of the OID will be set.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_OID  The requested OID is unknown.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
int SkPnmiSetVar(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
SK_U32 Id,		/* Object ID that is to be processed */
void *pBuf,		/* Buffer to which the management data will be copied */
unsigned int *pLen,	/* Total length of management data */
SK_U32 Instance,	/* Instance (1..n) that is to be set or -1 */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiSetVar: Called, Id=0x%x, BufLen=%d, Instance=%d, NetIndex=%d\n",
			Id, *pLen, Instance, NetIndex));

	return (PnmiVar(pAC, IoC, SK_PNMI_SET, Id, (char *)pBuf, pLen,
		Instance, NetIndex));
}

/*****************************************************************************
 *
 * SkPnmiGetStruct - Retrieves the management database in SK_PNMI_STRUCT_DATA
 *
 * Description:
 *	Runs through the IdTable, queries the single OIDs and stores the
 *	returned data into the management database structure
 *	SK_PNMI_STRUCT_DATA. The offset of the OID in the structure
 *	is stored in the IdTable. The return value of the function will also
 *	be stored in SK_PNMI_STRUCT_DATA if the passed buffer has the
 *	minimum size of SK_PNMI_MIN_STRUCT_SIZE.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to take
 *	                         the data.
 *	SK_PNMI_ERR_UNKNOWN_NET  The requested NetIndex doesn't exist
 */
int SkPnmiGetStruct(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
void *pBuf,		/* Buffer to which the management data will be copied. */
unsigned int *pLen,	/* Length of buffer */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	int		Ret;
	unsigned int	TableIndex;
	unsigned int	DstOffset;
	unsigned int	InstanceNo;
	unsigned int	InstanceCnt;
	SK_U32		Instance;
	unsigned int	TmpLen;
	char		KeyArr[SK_PNMI_VPD_ENTRIES][SK_PNMI_VPD_KEY_SIZE];


	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiGetStruct: Called, BufLen=%d, NetIndex=%d\n",
			*pLen, NetIndex));

	if (*pLen < SK_PNMI_STRUCT_SIZE) {

		if (*pLen >= SK_PNMI_MIN_STRUCT_SIZE) {

			SK_PNMI_SET_STAT(pBuf, SK_PNMI_ERR_TOO_SHORT,
				(SK_U32)(-1));
		}

		*pLen = SK_PNMI_STRUCT_SIZE;
		return (SK_PNMI_ERR_TOO_SHORT);
	}

    /*
     * Check NetIndex
     */
	if (NetIndex >= pAC->Rlmt.NumNets) {
		return (SK_PNMI_ERR_UNKNOWN_NET);
	}

	/* Update statistic */
	SK_PNMI_CHECKFLAGS("SkPnmiGetStruct: On call");

	if ((Ret = MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1)) !=
		SK_PNMI_ERR_OK) {

		SK_PNMI_SET_STAT(pBuf, Ret, (SK_U32)(-1));
		*pLen = SK_PNMI_MIN_STRUCT_SIZE;
		return (Ret);
	}

	if ((Ret = RlmtUpdate(pAC, IoC, NetIndex)) != SK_PNMI_ERR_OK) {

		SK_PNMI_SET_STAT(pBuf, Ret, (SK_U32)(-1));
		*pLen = SK_PNMI_MIN_STRUCT_SIZE;
		return (Ret);
	}

	if ((Ret = SirqUpdate(pAC, IoC)) != SK_PNMI_ERR_OK) {

		SK_PNMI_SET_STAT(pBuf, Ret, (SK_U32)(-1));
		*pLen = SK_PNMI_MIN_STRUCT_SIZE;
		return (Ret);
	}

	/*
	 * Increment semaphores to indicate that an update was
	 * already done
	 */
	pAC->Pnmi.MacUpdatedFlag ++;
	pAC->Pnmi.RlmtUpdatedFlag ++;
	pAC->Pnmi.SirqUpdatedFlag ++;

	/* Get vpd keys for instance calculation */
	Ret = GetVpdKeyArr(pAC, IoC, &KeyArr[0][0], sizeof(KeyArr), &TmpLen);
	if (Ret != SK_PNMI_ERR_OK) {

		pAC->Pnmi.MacUpdatedFlag --;
		pAC->Pnmi.RlmtUpdatedFlag --;
		pAC->Pnmi.SirqUpdatedFlag --;

		SK_PNMI_CHECKFLAGS("SkPnmiGetStruct: On return");
		SK_PNMI_SET_STAT(pBuf, Ret, (SK_U32)(-1));
		*pLen = SK_PNMI_MIN_STRUCT_SIZE;
		return (SK_PNMI_ERR_GENERAL);
	}

	/* Retrieve values */
	SK_MEMSET((char *)pBuf, 0, SK_PNMI_STRUCT_SIZE);
	for (TableIndex = 0; TableIndex < ID_TABLE_SIZE; TableIndex ++) {

		InstanceNo = IdTable[TableIndex].InstanceNo;
		for (InstanceCnt = 1; InstanceCnt <= InstanceNo;
			InstanceCnt ++) {

			DstOffset = IdTable[TableIndex].Offset +
				(InstanceCnt - 1) *
				IdTable[TableIndex].StructSize;

			/*
			 * For the VPD the instance is not an index number
			 * but the key itself. Determin with the instance
			 * counter the VPD key to be used.
			 */
			if (IdTable[TableIndex].Id == OID_SKGE_VPD_KEY ||
				IdTable[TableIndex].Id == OID_SKGE_VPD_VALUE ||
				IdTable[TableIndex].Id == OID_SKGE_VPD_ACCESS ||
				IdTable[TableIndex].Id == OID_SKGE_VPD_ACTION) {

				SK_STRNCPY((char *)&Instance, KeyArr[InstanceCnt - 1], 4);
			}
			else {
				Instance = (SK_U32)InstanceCnt;
			}

			TmpLen = *pLen - DstOffset;
			Ret = IdTable[TableIndex].Func(pAC, IoC, SK_PNMI_GET,
				IdTable[TableIndex].Id, (char *)pBuf +
				DstOffset, &TmpLen, Instance, TableIndex, NetIndex);

			/*
			 * An unknown instance error means that we reached
			 * the last instance of that variable. Proceed with
			 * the next OID in the table and ignore the return
			 * code.
			 */
			if (Ret == SK_PNMI_ERR_UNKNOWN_INST) {

                break;
			}

			if (Ret != SK_PNMI_ERR_OK) {

				pAC->Pnmi.MacUpdatedFlag --;
				pAC->Pnmi.RlmtUpdatedFlag --;
				pAC->Pnmi.SirqUpdatedFlag --;

				SK_PNMI_CHECKFLAGS("SkPnmiGetStruct: On return");
				SK_PNMI_SET_STAT(pBuf, Ret, DstOffset);
				*pLen = SK_PNMI_MIN_STRUCT_SIZE;
				return (Ret);
			}
		}
	}

	pAC->Pnmi.MacUpdatedFlag --;
	pAC->Pnmi.RlmtUpdatedFlag --;
	pAC->Pnmi.SirqUpdatedFlag --;

	*pLen = SK_PNMI_STRUCT_SIZE;
	SK_PNMI_CHECKFLAGS("SkPnmiGetStruct: On return");
	SK_PNMI_SET_STAT(pBuf, SK_PNMI_ERR_OK, (SK_U32)(-1));
	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * SkPnmiPreSetStruct - Presets the management database in SK_PNMI_STRUCT_DATA
 *
 * Description:
 *	Calls a general sub-function for all this set stuff. The preset does
 *	the same as a set, but returns just before finally setting the
 *	new value. This is useful to check if a set might be successfull.
 *	The sub-function runs through the IdTable, checks which OIDs are able
 *	to set, and calls the handler function of the OID to perform the
 *	preset. The return value of the function will also be stored in
 *	SK_PNMI_STRUCT_DATA if the passed buffer has the minimum size of
 *	SK_PNMI_MIN_STRUCT_SIZE.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 */
int SkPnmiPreSetStruct(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
void *pBuf,		/* Buffer which contains the data to be set */
unsigned int *pLen,	/* Length of buffer */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiPreSetStruct: Called, BufLen=%d, NetIndex=%d\n",
			*pLen, NetIndex));

	return (PnmiStruct(pAC, IoC, SK_PNMI_PRESET, (char *)pBuf,
    					pLen, NetIndex));
}

/*****************************************************************************
 *
 * SkPnmiSetStruct - Sets the management database in SK_PNMI_STRUCT_DATA
 *
 * Description:
 *	Calls a general sub-function for all this set stuff. The return value
 *	of the function will also be stored in SK_PNMI_STRUCT_DATA if the
 *	passed buffer has the minimum size of SK_PNMI_MIN_STRUCT_SIZE.
 *	The sub-function runs through the IdTable, checks which OIDs are able
 *	to set, and calls the handler function of the OID to perform the
 *	set. The return value of the function will also be stored in
 *	SK_PNMI_STRUCT_DATA if the passed buffer has the minimum size of
 *	SK_PNMI_MIN_STRUCT_SIZE.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 */
int SkPnmiSetStruct(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
void *pBuf,		/* Buffer which contains the data to be set */
unsigned int *pLen,	/* Length of buffer */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiSetStruct: Called, BufLen=%d, NetIndex=%d\n",
			*pLen, NetIndex));

	return (PnmiStruct(pAC, IoC, SK_PNMI_SET, (char *)pBuf,
    					pLen, NetIndex));
}

/*****************************************************************************
 *
 * SkPnmiEvent - Event handler
 *
 * Description:
 *	Handles the following events:
 *	SK_PNMI_EVT_SIRQ_OVERFLOW     When a hardware counter overflows an
 *	                              interrupt will be generated which is
 *	                              first handled by SIRQ which generates a
 *	                              this event. The event increments the
 *	                              upper 32 bit of the 64 bit counter.
 *	SK_PNMI_EVT_SEN_XXX           The event is generated by the I2C module
 *	                              when a sensor reports a warning or
 *	                              error. The event will store a trap
 *	                              message in the trap buffer.
 *	SK_PNMI_EVT_CHG_EST_TIMER     The timer event was initiated by this
 *	                              module and is used to calculate the
 *	                              port switches per hour.
 *	SK_PNMI_EVT_CLEAR_COUNTER     The event clears all counters and
 *	                              timestamps.
 *	SK_PNMI_EVT_XMAC_RESET        The event is generated by the driver
 *	                              before a hard reset of the XMAC is
 *	                              performed. All counters will be saved
 *	                              and added to the hardware counter
 *	                              values after reset to grant continuous
 *	                              counter values.
 *	SK_PNMI_EVT_RLMT_PORT_UP      Generated by RLMT to notify that a port
 *	                              went logically up. A trap message will
 *	                              be stored to the trap buffer.
 *	SK_PNMI_EVT_RLMT_PORT_DOWN    Generated by RLMT to notify that a port
 *	                              went logically down. A trap message will
 *	                              be stored to the trap buffer.
 *	SK_PNMI_EVT_RLMT_SEGMENTATION Generated by RLMT to notify that two
 *	                              spanning tree root bridges were
 *	                              detected. A trap message will be stored
 *	                              to the trap buffer.
 *	SK_PNMI_EVT_RLMT_ACTIVE_DOWN  Notifies PNMI that an active port went
 *	                              down. PNMI will not further add the
 *	                              statistic values to the virtual port.
 *	SK_PNMI_EVT_RLMT_ACTIVE_UP    Notifies PNMI that a port went up and
 *	                              is now an active port. PNMI will now
 *	                              add the statistic data of this port to
 *	                              the virtual port.
 *	SK_PNMI_EVT_RLMT_SET_NETS     Notifies PNMI about the net mode. The first parameter
 *	                              contains the number of nets. 1 means single net, 2 means
 *	                              dual net. The second parameter is -1
 *
 * Returns:
 *	Always 0
 */
int SkPnmiEvent(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
SK_U32 Event,		/* Event-Id */
SK_EVPARA Param)	/* Event dependent parameter */
{
	unsigned int	PhysPortIndex;
    unsigned int	MaxNetNumber;
	int			CounterIndex;
	int			Ret;
	SK_U16		MacStatus;
	SK_U64		OverflowStatus;
	SK_U64		Mask;
	int			MacType;
	SK_U64		Value;
	SK_U32		Val32;
	SK_U16		Register;
	SK_EVPARA	EventParam;
	SK_U64		NewestValue;
	SK_U64		OldestValue;
	SK_U64		Delta;
	SK_PNMI_ESTIMATE *pEst;
	SK_U32		NetIndex;
	SK_GEPORT	*pPrt;
	SK_PNMI_VCT	*pVctBackupData;
	SK_U32		RetCode;
	int		i;
	SK_U32		CableLength;


#ifdef DEBUG
	if (Event != SK_PNMI_EVT_XMAC_RESET) {

		SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
			("PNMI: SkPnmiEvent: Called, Event=0x%x, Param=0x%x\n",
			(unsigned int)Event, (unsigned int)Param.Para64));
	}
#endif /* DEBUG */
	SK_PNMI_CHECKFLAGS("SkPnmiEvent: On call");

	MacType = pAC->GIni.GIMacType;
	
	switch (Event) {

	case SK_PNMI_EVT_SIRQ_OVERFLOW:
		PhysPortIndex = (int)Param.Para32[0];
		MacStatus = (SK_U16)Param.Para32[1];
#ifdef DEBUG
		if (PhysPortIndex >= SK_MAX_MACS) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_SIRQ_OVERFLOW parameter"
				 " wrong, PhysPortIndex=0x%x\n",
				PhysPortIndex));
			return (0);
		}
#endif /* DEBUG */
		OverflowStatus = 0;

		/*
		 * Check which source caused an overflow interrupt.
		 */
		if ((pAC->GIni.GIFunc.pFnMacOverflow(pAC, IoC, PhysPortIndex,
				MacStatus, &OverflowStatus) != 0) ||
			(OverflowStatus == 0)) {

			SK_PNMI_CHECKFLAGS("SkPnmiEvent: On return");
			return (0);
		}

		/*
		 * Check the overflow status register and increment
		 * the upper dword of corresponding counter.
		 */
		for (CounterIndex = 0; CounterIndex < sizeof(Mask) * 8;
			CounterIndex ++) {

			Mask = (SK_U64)1 << CounterIndex;
			if ((OverflowStatus & Mask) == 0) {

				continue;
			}

			switch (StatOvrflwBit[CounterIndex][MacType]) {

			case SK_PNMI_HTX_UTILUNDER:
			case SK_PNMI_HTX_UTILOVER:
				if (MacType == SK_MAC_XMAC) {
					XM_IN16(IoC, PhysPortIndex, XM_TX_CMD, &Register);
					Register |= XM_TX_SAM_LINE;
					XM_OUT16(IoC, PhysPortIndex, XM_TX_CMD, Register);
				}
				break;

			case SK_PNMI_HRX_UTILUNDER:
			case SK_PNMI_HRX_UTILOVER:
				if (MacType == SK_MAC_XMAC) {
					XM_IN16(IoC, PhysPortIndex, XM_RX_CMD, &Register);
					Register |= XM_RX_SAM_LINE;
					XM_OUT16(IoC, PhysPortIndex, XM_RX_CMD, Register);
				}
				break;

			case SK_PNMI_HTX_OCTETHIGH:
			case SK_PNMI_HTX_OCTETLOW:
			case SK_PNMI_HTX_RESERVED:
			case SK_PNMI_HRX_OCTETHIGH:
			case SK_PNMI_HRX_OCTETLOW:
			case SK_PNMI_HRX_IRLENGTH:
			case SK_PNMI_HRX_RESERVED:
			
			/*
			 * the following counters aren't be handled (id > 63)
			 */
			case SK_PNMI_HTX_SYNC:
			case SK_PNMI_HTX_SYNC_OCTET:
				break;

			case SK_PNMI_HRX_LONGFRAMES:
				if (MacType == SK_MAC_GMAC) {
					pAC->Pnmi.Port[PhysPortIndex].
						CounterHigh[CounterIndex] ++;
				}
				break;

			default:
				pAC->Pnmi.Port[PhysPortIndex].
					CounterHigh[CounterIndex] ++;
			}
		}
		break;

	case SK_PNMI_EVT_SEN_WAR_LOW:
#ifdef DEBUG
		if ((unsigned int)Param.Para64 >= (unsigned int)pAC->I2c.MaxSens) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_SEN_WAR_LOW parameter wrong, SensorIndex=%d\n",
				(unsigned int)Param.Para64));
			return (0);
		}
#endif /* DEBUG */

		/*
		 * Store a trap message in the trap buffer and generate
		 * an event for user space applications with the
		 * SK_DRIVER_SENDEVENT macro.
		 */
		QueueSensorTrap(pAC, OID_SKGE_TRAP_SEN_WAR_LOW,
			(unsigned int)Param.Para64);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		break;

	case SK_PNMI_EVT_SEN_WAR_UPP:
#ifdef DEBUG
		if ((unsigned int)Param.Para64 >= (unsigned int)pAC->I2c.MaxSens) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_SEN_WAR_UPP parameter wrong, SensorIndex=%d\n",
				(unsigned int)Param.Para64));
			return (0);
		}
#endif /* DEBUG */

		/*
		 * Store a trap message in the trap buffer and generate
		 * an event for user space applications with the
		 * SK_DRIVER_SENDEVENT macro.
		 */
		QueueSensorTrap(pAC, OID_SKGE_TRAP_SEN_WAR_UPP,
			(unsigned int)Param.Para64);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		break;

	case SK_PNMI_EVT_SEN_ERR_LOW:
#ifdef DEBUG
		if ((unsigned int)Param.Para64 >= (unsigned int)pAC->I2c.MaxSens) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_SEN_ERR_LOW parameter wrong, SensorIndex=%d\n",
				(unsigned int)Param.Para64));
			return (0);
		}
#endif /* DEBUG */

		/*
		 * Store a trap message in the trap buffer and generate
		 * an event for user space applications with the
		 * SK_DRIVER_SENDEVENT macro.
		 */
		QueueSensorTrap(pAC, OID_SKGE_TRAP_SEN_ERR_LOW,
			(unsigned int)Param.Para64);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		break;
	
	case SK_PNMI_EVT_SEN_ERR_UPP:
#ifdef DEBUG
		if ((unsigned int)Param.Para64 >= (unsigned int)pAC->I2c.MaxSens) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_SEN_ERR_UPP parameter wrong, SensorIndex=%d\n",
				(unsigned int)Param.Para64));
			return (0);
		}
#endif /* DEBUG */

		/*
		 * Store a trap message in the trap buffer and generate
		 * an event for user space applications with the
		 * SK_DRIVER_SENDEVENT macro.
		 */
		QueueSensorTrap(pAC, OID_SKGE_TRAP_SEN_ERR_UPP,
			(unsigned int)Param.Para64);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		break;

	case SK_PNMI_EVT_CHG_EST_TIMER:
		/*
		 * Calculate port switch average on a per hour basis
		 *   Time interval for check       : 28125 ms
		 *   Number of values for average  : 8
		 *
		 * Be careful in changing these values, on change check
		 *   - typedef of SK_PNMI_ESTIMATE (Size of EstValue
		 *     array one less than value number)
		 *   - Timer initialization SkTimerStart() in SkPnmiInit
		 *   - Delta value below must be multiplicated with
		 *     power of 2
		 *
		 */
		pEst = &pAC->Pnmi.RlmtChangeEstimate;
		CounterIndex = pEst->EstValueIndex + 1;
		if (CounterIndex == 7) {

			CounterIndex = 0;
		}
		pEst->EstValueIndex = CounterIndex;

		NewestValue = pAC->Pnmi.RlmtChangeCts;
		OldestValue = pEst->EstValue[CounterIndex];
		pEst->EstValue[CounterIndex] = NewestValue;

		/*
		 * Calculate average. Delta stores the number of
		 * port switches per 28125 * 8 = 225000 ms
		 */
		if (NewestValue >= OldestValue) {

			Delta = NewestValue - OldestValue;
		}
		else {
			/* Overflow situation */
			Delta = (SK_U64)(0 - OldestValue) + NewestValue;
		}

		/*
		 * Extrapolate delta to port switches per hour.
		 *     Estimate = Delta * (3600000 / 225000)
		 *              = Delta * 16
		 *              = Delta << 4
		 */
		pAC->Pnmi.RlmtChangeEstimate.Estimate = Delta << 4;

		/*
		 * Check if threshold is exceeded. If the threshold is
		 * permanently exceeded every 28125 ms an event will be
		 * generated to remind the user of this condition.
		 */
		if ((pAC->Pnmi.RlmtChangeThreshold != 0) &&
			(pAC->Pnmi.RlmtChangeEstimate.Estimate >=
			pAC->Pnmi.RlmtChangeThreshold)) {

			QueueSimpleTrap(pAC, OID_SKGE_TRAP_RLMT_CHANGE_THRES);
			(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		}

		SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));
		SkTimerStart(pAC, IoC, &pAC->Pnmi.RlmtChangeEstimate.EstTimer,
			28125000, SKGE_PNMI, SK_PNMI_EVT_CHG_EST_TIMER,
			EventParam);
		break;

	case SK_PNMI_EVT_CLEAR_COUNTER:
		/*
		 *  Param.Para32[0] contains the NetIndex (0 ..1).
		 *  Param.Para32[1] is reserved, contains -1.
		 */
		NetIndex = (SK_U32)Param.Para32[0];

#ifdef DEBUG
		if (NetIndex >= pAC->Rlmt.NumNets) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_CLEAR_COUNTER parameter wrong, NetIndex=%d\n",
				NetIndex));

			return (0);
		}
#endif /* DEBUG */

		/*
		 * Set all counters and timestamps to zero.
		 * The according NetIndex is required as a
		 * parameter of the event.
		 */
		ResetCounter(pAC, IoC, NetIndex);
		break;

	case SK_PNMI_EVT_XMAC_RESET:
		/*
		 * To grant continuous counter values store the current
		 * XMAC statistic values to the entries 1..n of the
		 * CounterOffset array. XMAC Errata #2
		 */
#ifdef DEBUG
		if ((unsigned int)Param.Para64 >= SK_MAX_MACS) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_XMAC_RESET parameter wrong, PhysPortIndex=%d\n",
				(unsigned int)Param.Para64));
			return (0);
		}
#endif
		PhysPortIndex = (unsigned int)Param.Para64;

		/*
		 * Update XMAC statistic to get fresh values
		 */
		Ret = MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1);
		if (Ret != SK_PNMI_ERR_OK) {

			SK_PNMI_CHECKFLAGS("SkPnmiEvent: On return");
			return (0);
		}
		/*
		 * Increment semaphore to indicate that an update was
		 * already done
		 */
		pAC->Pnmi.MacUpdatedFlag ++;

		for (CounterIndex = 0; CounterIndex < SK_PNMI_MAX_IDX;
			CounterIndex ++) {

			if (!StatAddr[CounterIndex][MacType].GetOffset) {

				continue;
			}

			pAC->Pnmi.Port[PhysPortIndex].CounterOffset[CounterIndex] =
				GetPhysStatVal(pAC, IoC, PhysPortIndex, CounterIndex);
			
			pAC->Pnmi.Port[PhysPortIndex].CounterHigh[CounterIndex] = 0;
		}

		pAC->Pnmi.MacUpdatedFlag --;
		break;

	case SK_PNMI_EVT_RLMT_PORT_UP:
		PhysPortIndex = (unsigned int)Param.Para32[0];
#ifdef DEBUG
		if (PhysPortIndex >= SK_MAX_MACS) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_RLMT_PORT_UP parameter"
                 " wrong, PhysPortIndex=%d\n", PhysPortIndex));

			return (0);
		}
#endif /* DEBUG */

		/*
		 * Store a trap message in the trap buffer and generate an event for
		 * user space applications with the SK_DRIVER_SENDEVENT macro.
		 */
		QueueRlmtPortTrap(pAC, OID_SKGE_TRAP_RLMT_PORT_UP, PhysPortIndex);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);

		/* Bugfix for XMAC errata (#10620)*/
		if (MacType == SK_MAC_XMAC) {
			/* Add incremental difference to offset (#10620)*/
			(void)pAC->GIni.GIFunc.pFnMacStatistic(pAC, IoC, PhysPortIndex,
				XM_RXE_SHT_ERR, &Val32);
			
			Value = (((SK_U64)pAC->Pnmi.Port[PhysPortIndex].
				 CounterHigh[SK_PNMI_HRX_SHORTS] << 32) | (SK_U64)Val32);
			pAC->Pnmi.Port[PhysPortIndex].CounterOffset[SK_PNMI_HRX_SHORTS] +=
				Value - pAC->Pnmi.Port[PhysPortIndex].RxShortZeroMark;
		}
		
		/* Tell VctStatus() that a link was up meanwhile. */
		pAC->Pnmi.VctStatus[PhysPortIndex] |= SK_PNMI_VCT_LINK;		
		break;

    case SK_PNMI_EVT_RLMT_PORT_DOWN:
		PhysPortIndex = (unsigned int)Param.Para32[0];

#ifdef DEBUG
		if (PhysPortIndex >= SK_MAX_MACS) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_RLMT_PORT_DOWN parameter"
                 " wrong, PhysPortIndex=%d\n", PhysPortIndex));

			return (0);
		}
#endif /* DEBUG */

		/*
		 * Store a trap message in the trap buffer and generate an event for
		 * user space applications with the SK_DRIVER_SENDEVENT macro.
		 */
		QueueRlmtPortTrap(pAC, OID_SKGE_TRAP_RLMT_PORT_DOWN, PhysPortIndex);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);

		/* Bugfix #10620 - get zero level for incremental difference */
		if (MacType == SK_MAC_XMAC) {

			(void)pAC->GIni.GIFunc.pFnMacStatistic(pAC, IoC, PhysPortIndex,
				XM_RXE_SHT_ERR, &Val32);
			
			pAC->Pnmi.Port[PhysPortIndex].RxShortZeroMark =
				(((SK_U64)pAC->Pnmi.Port[PhysPortIndex].
				 CounterHigh[SK_PNMI_HRX_SHORTS] << 32) | (SK_U64)Val32);
		}
		break;

	case SK_PNMI_EVT_RLMT_ACTIVE_DOWN:
		PhysPortIndex = (unsigned int)Param.Para32[0];
		NetIndex = (SK_U32)Param.Para32[1];

#ifdef DEBUG
		if (PhysPortIndex >= SK_MAX_MACS) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_RLMT_ACTIVE_DOWN parameter too high, PhysPort=%d\n",
				PhysPortIndex));
		}

		if (NetIndex >= pAC->Rlmt.NumNets) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_RLMT_ACTIVE_DOWN parameter too high, NetIndex=%d\n",
				NetIndex));
		}
#endif /* DEBUG */

		/*
		 * For now, ignore event if NetIndex != 0.
		 */
		if (Param.Para32[1] != 0) {

			return (0);
		}

		/*
		 * Nothing to do if port is already inactive
		 */
		if (!pAC->Pnmi.Port[PhysPortIndex].ActiveFlag) {

			return (0);
		}

		/*
		 * Update statistic counters to calculate new offset for the virtual
		 * port and increment semaphore to indicate that an update was already
		 * done.
		 */
		if (MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1) !=
			SK_PNMI_ERR_OK) {

			SK_PNMI_CHECKFLAGS("SkPnmiEvent: On return");
			return (0);
		}
		pAC->Pnmi.MacUpdatedFlag ++;

		/*
		 * Calculate new counter offset for virtual port to grant continous
		 * counting on port switches. The virtual port consists of all currently
		 * active ports. The port down event indicates that a port is removed
		 * from the virtual port. Therefore add the counter value of the removed
		 * port to the CounterOffset for the virtual port to grant the same
		 * counter value.
		 */
		for (CounterIndex = 0; CounterIndex < SK_PNMI_MAX_IDX;
			CounterIndex ++) {

			if (!StatAddr[CounterIndex][MacType].GetOffset) {

				continue;
			}

			Value = GetPhysStatVal(pAC, IoC, PhysPortIndex, CounterIndex);

			pAC->Pnmi.VirtualCounterOffset[CounterIndex] += Value;
		}

		/*
		 * Set port to inactive
		 */
		pAC->Pnmi.Port[PhysPortIndex].ActiveFlag = SK_FALSE;

		pAC->Pnmi.MacUpdatedFlag --;
		break;

	case SK_PNMI_EVT_RLMT_ACTIVE_UP:
		PhysPortIndex = (unsigned int)Param.Para32[0];
		NetIndex = (SK_U32)Param.Para32[1];

#ifdef DEBUG
		if (PhysPortIndex >= SK_MAX_MACS) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_RLMT_ACTIVE_UP parameter too high, PhysPort=%d\n",
				PhysPortIndex));
		}

		if (NetIndex >= pAC->Rlmt.NumNets) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_RLMT_ACTIVE_UP parameter too high, NetIndex=%d\n",
				NetIndex));
		}
#endif /* DEBUG */

		/*
		 * For now, ignore event if NetIndex != 0.
		 */
		if (Param.Para32[1] != 0) {

			return (0);
		}

		/*
		 * Nothing to do if port is already active
		 */
		if (pAC->Pnmi.Port[PhysPortIndex].ActiveFlag) {

			return (0);
		}

		/*
		 * Statistic maintenance
		 */
		pAC->Pnmi.RlmtChangeCts ++;
		pAC->Pnmi.RlmtChangeTime = SK_PNMI_HUNDREDS_SEC(SkOsGetTime(pAC));

		/*
		 * Store a trap message in the trap buffer and generate an event for
		 * user space applications with the SK_DRIVER_SENDEVENT macro.
		 */
		QueueRlmtNewMacTrap(pAC, PhysPortIndex);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);

		/*
		 * Update statistic counters to calculate new offset for the virtual
		 * port and increment semaphore to indicate that an update was
		 * already done.
		 */
		if (MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1) !=
			SK_PNMI_ERR_OK) {

			SK_PNMI_CHECKFLAGS("SkPnmiEvent: On return");
			return (0);
		}
		pAC->Pnmi.MacUpdatedFlag ++;

		/*
		 * Calculate new counter offset for virtual port to grant continous
		 * counting on port switches. A new port is added to the virtual port.
		 * Therefore substract the counter value of the new port from the
		 * CounterOffset for the virtual port to grant the same value.
		 */
		for (CounterIndex = 0; CounterIndex < SK_PNMI_MAX_IDX;
			CounterIndex ++) {

			if (!StatAddr[CounterIndex][MacType].GetOffset) {

				continue;
			}

			Value = GetPhysStatVal(pAC, IoC, PhysPortIndex, CounterIndex);

			pAC->Pnmi.VirtualCounterOffset[CounterIndex] -= Value;
		}

		/* Set port to active */
		pAC->Pnmi.Port[PhysPortIndex].ActiveFlag = SK_TRUE;

		pAC->Pnmi.MacUpdatedFlag --;
		break;

	case SK_PNMI_EVT_RLMT_SEGMENTATION:
		/*
		 * Para.Para32[0] contains the NetIndex.
		 */

		/*
		 * Store a trap message in the trap buffer and generate an event for
		 * user space applications with the SK_DRIVER_SENDEVENT macro.
		 */
		QueueSimpleTrap(pAC, OID_SKGE_TRAP_RLMT_SEGMENTATION);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		break;

    case SK_PNMI_EVT_RLMT_SET_NETS:
		/*
		 *  Param.Para32[0] contains the number of Nets.
		 *  Param.Para32[1] is reserved, contains -1.
		 */
	    /*
    	 * Check number of nets
		 */
		MaxNetNumber = pAC->GIni.GIMacsFound;
		if (((unsigned int)Param.Para32[0] < 1)
			|| ((unsigned int)Param.Para32[0] > MaxNetNumber)) {
			return (SK_PNMI_ERR_UNKNOWN_NET);
		}

        if ((unsigned int)Param.Para32[0] == 1) { /* single net mode */
        	pAC->Pnmi.DualNetActiveFlag = SK_FALSE;
        }
        else { /* dual net mode */
        	pAC->Pnmi.DualNetActiveFlag = SK_TRUE;
        }
        break;

    case SK_PNMI_EVT_VCT_RESET:
		PhysPortIndex = Param.Para32[0];
		pPrt = &pAC->GIni.GP[PhysPortIndex];
		pVctBackupData = &pAC->Pnmi.VctBackup[PhysPortIndex];
		
		if (pAC->Pnmi.VctStatus[PhysPortIndex] & SK_PNMI_VCT_PENDING) {
			RetCode = SkGmCableDiagStatus(pAC, IoC, PhysPortIndex, SK_FALSE);
			if (RetCode == 2) {
				/*
				 * VCT test is still running.
				 * Start VCT timer counter again.
				 */
				SK_MEMSET((char *) &Param, 0, sizeof(Param));
				Param.Para32[0] = PhysPortIndex;
				Param.Para32[1] = -1;
				SkTimerStart(pAC, IoC,
					&pAC->Pnmi.VctTimeout[PhysPortIndex].VctTimer,
				4000000, SKGE_PNMI, SK_PNMI_EVT_VCT_RESET, Param);
				break;
			}
			pAC->Pnmi.VctStatus[PhysPortIndex] &= ~SK_PNMI_VCT_PENDING;
			pAC->Pnmi.VctStatus[PhysPortIndex] |=
				(SK_PNMI_VCT_NEW_VCT_DATA | SK_PNMI_VCT_TEST_DONE);
			
			/* Copy results for later use to PNMI struct. */
			for (i = 0; i < 4; i++)  {
				if (pPrt->PMdiPairSts[i] == SK_PNMI_VCT_NORMAL_CABLE) {
					if ((pPrt->PMdiPairLen[i] > 35) &&
						(pPrt->PMdiPairLen[i] < 0xff)) {
						pPrt->PMdiPairSts[i] = SK_PNMI_VCT_IMPEDANCE_MISMATCH;
					}
				}
				if ((pPrt->PMdiPairLen[i] > 35) &&
					(pPrt->PMdiPairLen[i] != 0xff)) {
					CableLength = 1000 *
						(((175 * pPrt->PMdiPairLen[i]) / 210) - 28);
				}
				else {
					CableLength = 0;
				}
				pVctBackupData->PMdiPairLen[i] = CableLength;
				pVctBackupData->PMdiPairSts[i] = pPrt->PMdiPairSts[i];
			}
			
			Param.Para32[0] = PhysPortIndex;
			Param.Para32[1] = -1;
			SkEventQueue(pAC, SKGE_DRV, SK_DRV_PORT_RESET, Param);
			SkEventDispatcher(pAC, IoC);
		}
		
		break;

	default:
		break;
	}

	SK_PNMI_CHECKFLAGS("SkPnmiEvent: On return");
	return (0);
}


/******************************************************************************
 *
 * Private functions
 *
 */

/*****************************************************************************
 *
 * PnmiVar - Gets, presets, and sets single OIDs
 *
 * Description:
 *	Looks up the requested OID, calls the corresponding handler
 *	function, and passes the parameters with the get, preset, or
 *	set command. The function is called by SkGePnmiGetVar,
 *	SkGePnmiPreSetVar, or SkGePnmiSetVar.
 *
 * Returns:
 *	SK_PNMI_ERR_XXX. For details have a look at the description of the
 *	calling functions.
 *	SK_PNMI_ERR_UNKNOWN_NET  The requested NetIndex doesn't exist
 */
PNMI_STATIC int PnmiVar(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* Total length of pBuf management data  */
SK_U32 Instance,	/* Instance (1..n) that is to be set or -1 */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	unsigned int	TableIndex;
	int		Ret;


	if ((TableIndex = LookupId(Id)) == (unsigned int)(-1)) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_OID);
	}
	
    /* Check NetIndex */
	if (NetIndex >= pAC->Rlmt.NumNets) {
		return (SK_PNMI_ERR_UNKNOWN_NET);
	}

	SK_PNMI_CHECKFLAGS("PnmiVar: On call");

	Ret = IdTable[TableIndex].Func(pAC, IoC, Action, Id, pBuf, pLen,
		Instance, TableIndex, NetIndex);

	SK_PNMI_CHECKFLAGS("PnmiVar: On return");

	return (Ret);
}

/*****************************************************************************
 *
 * PnmiStruct - Presets and Sets data in structure SK_PNMI_STRUCT_DATA
 *
 * Description:
 *	The return value of the function will also be stored in
 *	SK_PNMI_STRUCT_DATA if the passed buffer has the minimum size of
 *	SK_PNMI_MIN_STRUCT_SIZE. The sub-function runs through the IdTable,
 *	checks which OIDs are able to set, and calls the handler function of
 *	the OID to perform the set. The return value of the function will
 *	also be stored in SK_PNMI_STRUCT_DATA if the passed buffer has the
 *	minimum size of SK_PNMI_MIN_STRUCT_SIZE. The function is called
 *	by SkGePnmiPreSetStruct and SkGePnmiSetStruct.
 *
 * Returns:
 *	SK_PNMI_ERR_XXX. The codes are described in the calling functions.
 *	SK_PNMI_ERR_UNKNOWN_NET  The requested NetIndex doesn't exist
 */
PNMI_STATIC int PnmiStruct(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int  Action,	/* PRESET/SET action to be performed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* Length of pBuf management data buffer */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	int		Ret;
	unsigned int	TableIndex;
	unsigned int	DstOffset;
	unsigned int	Len;
	unsigned int	InstanceNo;
	unsigned int	InstanceCnt;
	SK_U32		Instance;
	SK_U32		Id;


	/* Check if the passed buffer has the right size */
	if (*pLen < SK_PNMI_STRUCT_SIZE) {

		/* Check if we can return the error within the buffer */
		if (*pLen >= SK_PNMI_MIN_STRUCT_SIZE) {

			SK_PNMI_SET_STAT(pBuf, SK_PNMI_ERR_TOO_SHORT,
				(SK_U32)(-1));
		}

		*pLen = SK_PNMI_STRUCT_SIZE;
		return (SK_PNMI_ERR_TOO_SHORT);
	}
	
    /* Check NetIndex */
	if (NetIndex >= pAC->Rlmt.NumNets) {
		return (SK_PNMI_ERR_UNKNOWN_NET);
	}
	
	SK_PNMI_CHECKFLAGS("PnmiStruct: On call");

	/*
	 * Update the values of RLMT and SIRQ and increment semaphores to
	 * indicate that an update was already done.
	 */
	if ((Ret = RlmtUpdate(pAC, IoC, NetIndex)) != SK_PNMI_ERR_OK) {

		SK_PNMI_SET_STAT(pBuf, Ret, (SK_U32)(-1));
		*pLen = SK_PNMI_MIN_STRUCT_SIZE;
		return (Ret);
	}

	if ((Ret = SirqUpdate(pAC, IoC)) != SK_PNMI_ERR_OK) {

		SK_PNMI_SET_STAT(pBuf, Ret, (SK_U32)(-1));
		*pLen = SK_PNMI_MIN_STRUCT_SIZE;
		return (Ret);
	}

	pAC->Pnmi.RlmtUpdatedFlag ++;
	pAC->Pnmi.SirqUpdatedFlag ++;

	/* Preset/Set values */
	for (TableIndex = 0; TableIndex < ID_TABLE_SIZE; TableIndex ++) {

		if ((IdTable[TableIndex].Access != SK_PNMI_RW) &&
			(IdTable[TableIndex].Access != SK_PNMI_WO)) {

			continue;
		}

		InstanceNo = IdTable[TableIndex].InstanceNo;
		Id = IdTable[TableIndex].Id;

		for (InstanceCnt = 1; InstanceCnt <= InstanceNo;
			InstanceCnt ++) {

			DstOffset = IdTable[TableIndex].Offset +
				(InstanceCnt - 1) *
				IdTable[TableIndex].StructSize;

			/*
			 * Because VPD multiple instance variables are
			 * not setable we do not need to evaluate VPD
			 * instances. Have a look to VPD instance
			 * calculation in SkPnmiGetStruct().
			 */
			Instance = (SK_U32)InstanceCnt;

			/*
			 * Evaluate needed buffer length
			 */
			Len = 0;
			Ret = IdTable[TableIndex].Func(pAC, IoC,
				SK_PNMI_GET, IdTable[TableIndex].Id,
				NULL, &Len, Instance, TableIndex, NetIndex);

			if (Ret == SK_PNMI_ERR_UNKNOWN_INST) {

				break;
			}
			if (Ret != SK_PNMI_ERR_TOO_SHORT) {

				pAC->Pnmi.RlmtUpdatedFlag --;
				pAC->Pnmi.SirqUpdatedFlag --;

				SK_PNMI_CHECKFLAGS("PnmiStruct: On return");
				SK_PNMI_SET_STAT(pBuf,
					SK_PNMI_ERR_GENERAL, DstOffset);
				*pLen = SK_PNMI_MIN_STRUCT_SIZE;
				return (SK_PNMI_ERR_GENERAL);
			}
			if (Id == OID_SKGE_VPD_ACTION) {

				switch (*(pBuf + DstOffset)) {

				case SK_PNMI_VPD_CREATE:
					Len = 3 + *(pBuf + DstOffset + 3);
					break;

				case SK_PNMI_VPD_DELETE:
					Len = 3;
					break;

				default:
					Len = 1;
					break;
				}
			}

			/* Call the OID handler function */
			Ret = IdTable[TableIndex].Func(pAC, IoC, Action,
				IdTable[TableIndex].Id, pBuf + DstOffset,
				&Len, Instance, TableIndex, NetIndex);

			if (Ret != SK_PNMI_ERR_OK) {

				pAC->Pnmi.RlmtUpdatedFlag --;
				pAC->Pnmi.SirqUpdatedFlag --;

				SK_PNMI_CHECKFLAGS("PnmiStruct: On return");
				SK_PNMI_SET_STAT(pBuf, SK_PNMI_ERR_BAD_VALUE,
					DstOffset);
				*pLen = SK_PNMI_MIN_STRUCT_SIZE;
				return (SK_PNMI_ERR_BAD_VALUE);
			}
		}
	}

	pAC->Pnmi.RlmtUpdatedFlag --;
	pAC->Pnmi.SirqUpdatedFlag --;

	SK_PNMI_CHECKFLAGS("PnmiStruct: On return");
	SK_PNMI_SET_STAT(pBuf, SK_PNMI_ERR_OK, (SK_U32)(-1));
	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * LookupId - Lookup an OID in the IdTable
 *
 * Description:
 *	Scans the IdTable to find the table entry of an OID.
 *
 * Returns:
 *	The table index or -1 if not found.
 */
PNMI_STATIC int LookupId(
SK_U32 Id)		/* Object identifier to be searched */
{
	int i;

	for (i = 0; i < ID_TABLE_SIZE; i++) {

		if (IdTable[i].Id == Id) {

			return i;
		}
	}

	return (-1);
}

/*****************************************************************************
 *
 * OidStruct - Handler of OID_SKGE_ALL_DATA
 *
 * Description:
 *	This OID performs a Get/Preset/SetStruct call and returns all data
 *	in a SK_PNMI_STRUCT_DATA structure.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
PNMI_STATIC int OidStruct(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	if (Id != OID_SKGE_ALL_DATA) {

		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR003,
			SK_PNMI_ERR003MSG);

		*pLen = 0;
		return (SK_PNMI_ERR_GENERAL);
	}

	/*
	 * Check instance. We only handle single instance variables
	 */
	if (Instance != (SK_U32)(-1) && Instance != 1) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_INST);
	}

	switch (Action) {

	case SK_PNMI_GET:
		return (SkPnmiGetStruct(pAC, IoC, pBuf, pLen, NetIndex));

	case SK_PNMI_PRESET:
		return (SkPnmiPreSetStruct(pAC, IoC, pBuf, pLen, NetIndex));

	case SK_PNMI_SET:
		return (SkPnmiSetStruct(pAC, IoC, pBuf, pLen, NetIndex));
	}

	SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR004, SK_PNMI_ERR004MSG);

	*pLen = 0;
	return (SK_PNMI_ERR_GENERAL);
}

/*****************************************************************************
 *
 * Perform - OID handler of OID_SKGE_ACTION
 *
 * Description:
 *	None.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
PNMI_STATIC int Perform(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	int	Ret;
	SK_U32	ActionOp;


	/*
	 * Check instance. We only handle single instance variables
	 */
	if (Instance != (SK_U32)(-1) && Instance != 1) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_INST);
	}

	if (*pLen < sizeof(SK_U32)) {

		*pLen = sizeof(SK_U32);
		return (SK_PNMI_ERR_TOO_SHORT);
	}

	/* Check if a get should be performed */
	if (Action == SK_PNMI_GET) {

		/* A get is easy. We always return the same value */
		ActionOp = (SK_U32)SK_PNMI_ACT_IDLE;
		SK_PNMI_STORE_U32(pBuf, ActionOp);
		*pLen = sizeof(SK_U32);

		return (SK_PNMI_ERR_OK);
	}

	/* Continue with PRESET/SET action */
	if (*pLen > sizeof(SK_U32)) {

		return (SK_PNMI_ERR_BAD_VALUE);
	}

	/* Check if the command is a known one */
	SK_PNMI_READ_U32(pBuf, ActionOp);
	if (*pLen > sizeof(SK_U32) ||
		(ActionOp != SK_PNMI_ACT_IDLE &&
		ActionOp != SK_PNMI_ACT_RESET &&
		ActionOp != SK_PNMI_ACT_SELFTEST &&
		ActionOp != SK_PNMI_ACT_RESETCNT)) {

		*pLen = 0;
		return (SK_PNMI_ERR_BAD_VALUE);
	}

	/* A preset ends here */
	if (Action == SK_PNMI_PRESET) {

		return (SK_PNMI_ERR_OK);
	}

	switch (ActionOp) {

	case SK_PNMI_ACT_IDLE:
		/* Nothing to do */
		break;

	case SK_PNMI_ACT_RESET:
		/*
		 * Perform a driver reset or something that comes near
		 * to this.
		 */
		Ret = SK_DRIVER_RESET(pAC, IoC);
		if (Ret != 0) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR005,
				SK_PNMI_ERR005MSG);

			return (SK_PNMI_ERR_GENERAL);
		}
		break;

	case SK_PNMI_ACT_SELFTEST:
		/*
		 * Perform a driver selftest or something similar to this.
		 * Currently this feature is not used and will probably
		 * implemented in another way.
		 */
		Ret = SK_DRIVER_SELFTEST(pAC, IoC);
		pAC->Pnmi.TestResult = Ret;
		break;

	case SK_PNMI_ACT_RESETCNT:
		/* Set all counters and timestamps to zero */
		ResetCounter(pAC, IoC, NetIndex);
		break;

	default:
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR006,
			SK_PNMI_ERR006MSG);

		return (SK_PNMI_ERR_GENERAL);
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * Mac8023Stat - OID handler of OID_GEN_XXX and OID_802_3_XXX
 *
 * Description:
 *	Retrieves the statistic values of the virtual port (logical
 *	index 0). Only special OIDs of NDIS are handled which consist
 *	of a 32 bit instead of a 64 bit value. The OIDs are public
 *	because perhaps some other platform can use them too.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
PNMI_STATIC int Mac8023Stat(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex,	/* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	int     Ret;
	SK_U64  StatVal;
	SK_U32  StatVal32;
	SK_BOOL Is64BitReq = SK_FALSE;

	/*
	 * Only the active Mac is returned
	 */
	if (Instance != (SK_U32)(-1) && Instance != 1) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_INST);
	}

	/*
	 * Check action type
	 */
	if (Action != SK_PNMI_GET) {

		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}

	/* Check length */
	switch (Id) {

	case OID_802_3_PERMANENT_ADDRESS:
	case OID_802_3_CURRENT_ADDRESS:
		if (*pLen < sizeof(SK_MAC_ADDR)) {

			*pLen = sizeof(SK_MAC_ADDR);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	default:
#ifndef SK_NDIS_64BIT_CTR
		if (*pLen < sizeof(SK_U32)) {
			*pLen = sizeof(SK_U32);
			return (SK_PNMI_ERR_TOO_SHORT);
		}

#else /* SK_NDIS_64BIT_CTR */

		/* for compatibility, at least 32bit are required for OID */
		if (*pLen < sizeof(SK_U32)) {
			/*
			* but indicate handling for 64bit values,
			* if insufficient space is provided
			*/
			*pLen = sizeof(SK_U64);
			return (SK_PNMI_ERR_TOO_SHORT);
		}

		Is64BitReq = (*pLen < sizeof(SK_U64)) ? SK_FALSE : SK_TRUE;
#endif /* SK_NDIS_64BIT_CTR */
		break;
	}

	/*
	 * Update all statistics, because we retrieve virtual MAC, which
	 * consists of multiple physical statistics and increment semaphore
	 * to indicate that an update was already done.
	 */
	Ret = MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1);
	if ( Ret != SK_PNMI_ERR_OK) {

		*pLen = 0;
		return (Ret);
	}
	pAC->Pnmi.MacUpdatedFlag ++;

	/*
	 * Get value (MAC Index 0 identifies the virtual MAC)
	 */
	switch (Id) {

	case OID_802_3_PERMANENT_ADDRESS:
		CopyMac(pBuf, &pAC->Addr.Net[NetIndex].PermanentMacAddress);
		*pLen = sizeof(SK_MAC_ADDR);
		break;

	case OID_802_3_CURRENT_ADDRESS:
		CopyMac(pBuf, &pAC->Addr.Net[NetIndex].CurrentMacAddress);
		*pLen = sizeof(SK_MAC_ADDR);
		break;

	default:
		StatVal = GetStatVal(pAC, IoC, 0, IdTable[TableIndex].Param, NetIndex);

		/* by default 32bit values are evaluated */
		if (!Is64BitReq) {
			StatVal32 = (SK_U32)StatVal;
			SK_PNMI_STORE_U32(pBuf, StatVal32);
			*pLen = sizeof(SK_U32);
		}
		else {
			SK_PNMI_STORE_U64(pBuf, StatVal);
			*pLen = sizeof(SK_U64);
		}
		break;
	}

	pAC->Pnmi.MacUpdatedFlag --;

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * MacPrivateStat - OID handler function of OID_SKGE_STAT_XXX
 *
 * Description:
 *	Retrieves the MAC statistic data.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
PNMI_STATIC int MacPrivateStat(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	unsigned int	LogPortMax;
	unsigned int	LogPortIndex;
	unsigned int	PhysPortMax;
	unsigned int	Limit;
	unsigned int	Offset;
	int				MacType;
	int				Ret;
	SK_U64			StatVal;
	
	

	/* Calculate instance if wished. MAC index 0 is the virtual MAC */
	PhysPortMax = pAC->GIni.GIMacsFound;
	LogPortMax = SK_PNMI_PORT_PHYS2LOG(PhysPortMax);
	
	MacType = pAC->GIni.GIMacType;

	if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) { /* Dual net mode */
		LogPortMax--;
	}

	if ((Instance != (SK_U32)(-1))) { /* Only one specific instance is queried */
		/* Check instance range */
		if ((Instance < 1) || (Instance > LogPortMax)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}
		LogPortIndex = SK_PNMI_PORT_INST2LOG(Instance);
		Limit = LogPortIndex + 1;
	}

	else { /* Instance == (SK_U32)(-1), get all Instances of that OID */

		LogPortIndex = 0;
		Limit = LogPortMax;
	}

	/* Check action */
	if (Action != SK_PNMI_GET) {

		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}

	/* Check length */
	if (*pLen < (Limit - LogPortIndex) * sizeof(SK_U64)) {

		*pLen = (Limit - LogPortIndex) * sizeof(SK_U64);
		return (SK_PNMI_ERR_TOO_SHORT);
	}

	/*
	 * Update MAC statistic and increment semaphore to indicate that
	 * an update was already done.
	 */
	Ret = MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1);
	if (Ret != SK_PNMI_ERR_OK) {

		*pLen = 0;
		return (Ret);
	}
	pAC->Pnmi.MacUpdatedFlag ++;

	/* Get value */
	Offset = 0;
	for (; LogPortIndex < Limit; LogPortIndex ++) {

		switch (Id) {

/* XXX not yet implemented due to XMAC problems
		case OID_SKGE_STAT_TX_UTIL:
			return (SK_PNMI_ERR_GENERAL);
*/
/* XXX not yet implemented due to XMAC problems
		case OID_SKGE_STAT_RX_UTIL:
			return (SK_PNMI_ERR_GENERAL);
*/
		case OID_SKGE_STAT_RX:
			if (MacType == SK_MAC_GMAC) {
				StatVal =
					GetStatVal(pAC, IoC, LogPortIndex,
							   SK_PNMI_HRX_BROADCAST, NetIndex) +
					GetStatVal(pAC, IoC, LogPortIndex,
							   SK_PNMI_HRX_MULTICAST, NetIndex) +
					GetStatVal(pAC, IoC, LogPortIndex,
							   SK_PNMI_HRX_UNICAST, NetIndex) +
					GetStatVal(pAC, IoC, LogPortIndex,
							   SK_PNMI_HRX_UNDERSIZE, NetIndex);
			}
			else {
				StatVal = GetStatVal(pAC, IoC, LogPortIndex,
					IdTable[TableIndex].Param, NetIndex);
			}
			break;

		case OID_SKGE_STAT_TX:
			if (MacType == SK_MAC_GMAC) {
				StatVal =
					GetStatVal(pAC, IoC, LogPortIndex,
							   SK_PNMI_HTX_BROADCAST, NetIndex) +
					GetStatVal(pAC, IoC, LogPortIndex,
							   SK_PNMI_HTX_MULTICAST, NetIndex) +
					GetStatVal(pAC, IoC, LogPortIndex,
							   SK_PNMI_HTX_UNICAST, NetIndex);
			}
			else {
				StatVal = GetStatVal(pAC, IoC, LogPortIndex,
					IdTable[TableIndex].Param, NetIndex);
			}
			break;

		default:
			StatVal = GetStatVal(pAC, IoC, LogPortIndex,
				IdTable[TableIndex].Param, NetIndex);
		}
		SK_PNMI_STORE_U64(pBuf + Offset, StatVal);

		Offset += sizeof(SK_U64);
	}
	*pLen = Offset;

	pAC->Pnmi.MacUpdatedFlag --;

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * Addr - OID handler function of OID_SKGE_PHYS_CUR_ADDR and _FAC_ADDR
 *
 * Description:
 *	Get/Presets/Sets the current and factory MAC address. The MAC
 *	address of the virtual port, which is reported to the OS, may
 *	not be changed, but the physical ones. A set to the virtual port
 *	will be ignored. No error should be reported because otherwise
 *	a multiple instance set (-1) would always fail.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
PNMI_STATIC int Addr(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	int		Ret;
	unsigned int	LogPortMax;
	unsigned int	PhysPortMax;
	unsigned int	LogPortIndex;
	unsigned int	PhysPortIndex;
	unsigned int	Limit;
	unsigned int	Offset = 0;

	/*
	 * Calculate instance if wished. MAC index 0 is the virtual
	 * MAC.
	 */
	PhysPortMax = pAC->GIni.GIMacsFound;
	LogPortMax = SK_PNMI_PORT_PHYS2LOG(PhysPortMax);

	if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) { /* Dual net mode */
		LogPortMax--;
	}

	if ((Instance != (SK_U32)(-1))) { /* Only one specific instance is queried */
		/* Check instance range */
		if ((Instance < 1) || (Instance > LogPortMax)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}
		LogPortIndex = SK_PNMI_PORT_INST2LOG(Instance);
		Limit = LogPortIndex + 1;
	}
	else { /* Instance == (SK_U32)(-1), get all Instances of that OID */

		LogPortIndex = 0;
		Limit = LogPortMax;
	}

	/*
	 * Perform Action
	 */
	if (Action == SK_PNMI_GET) {

		/* Check length */
		if (*pLen < (Limit - LogPortIndex) * 6) {

			*pLen = (Limit - LogPortIndex) * 6;
			return (SK_PNMI_ERR_TOO_SHORT);
		}

		/*
		 * Get value
		 */
		for (; LogPortIndex < Limit; LogPortIndex ++) {

			switch (Id) {

			case OID_SKGE_PHYS_CUR_ADDR:
				if (LogPortIndex == 0) {
					CopyMac(pBuf + Offset, &pAC->Addr.Net[NetIndex].CurrentMacAddress);
				}
				else {
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(pAC, LogPortIndex);

					CopyMac(pBuf + Offset,
						&pAC->Addr.Port[PhysPortIndex].CurrentMacAddress);
				}
				Offset += 6;
				break;

			case OID_SKGE_PHYS_FAC_ADDR:
				if (LogPortIndex == 0) {
					CopyMac(pBuf + Offset,
						&pAC->Addr.Net[NetIndex].PermanentMacAddress);
				}
				else {
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
						pAC, LogPortIndex);

					CopyMac(pBuf + Offset,
						&pAC->Addr.Port[PhysPortIndex].PermanentMacAddress);
				}
				Offset += 6;
				break;

			default:
				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR008,
					SK_PNMI_ERR008MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
		}

		*pLen = Offset;
	}
	else {
		/*
		 * The logical MAC address may not be changed only
		 * the physical ones
		 */
		if (Id == OID_SKGE_PHYS_FAC_ADDR) {

			*pLen = 0;
			return (SK_PNMI_ERR_READ_ONLY);
		}

		/*
		 * Only the current address may be changed
		 */
		if (Id != OID_SKGE_PHYS_CUR_ADDR) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR009,
				SK_PNMI_ERR009MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		/* Check length */
		if (*pLen < (Limit - LogPortIndex) * 6) {

			*pLen = (Limit - LogPortIndex) * 6;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		if (*pLen > (Limit - LogPortIndex) * 6) {

			*pLen = 0;
			return (SK_PNMI_ERR_BAD_VALUE);
		}

		/*
		 * Check Action
		 */
		if (Action == SK_PNMI_PRESET) {

			*pLen = 0;
			return (SK_PNMI_ERR_OK);
		}

		/*
		 * Set OID_SKGE_MAC_CUR_ADDR
		 */
		for (; LogPortIndex < Limit; LogPortIndex ++, Offset += 6) {

			/*
			 * A set to virtual port and set of broadcast
			 * address will be ignored
			 */
			if (LogPortIndex == 0 || SK_MEMCMP(pBuf + Offset,
				"\xff\xff\xff\xff\xff\xff", 6) == 0) {

				continue;
			}

			PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(pAC,
				LogPortIndex);

			Ret = SkAddrOverride(pAC, IoC, PhysPortIndex,
				(SK_MAC_ADDR *)(pBuf + Offset),
				(LogPortIndex == 0 ? SK_ADDR_VIRTUAL_ADDRESS :
				SK_ADDR_PHYSICAL_ADDRESS));
			if (Ret != SK_ADDR_OVERRIDE_SUCCESS) {

				return (SK_PNMI_ERR_GENERAL);
			}
		}
		*pLen = Offset;
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * CsumStat - OID handler function of OID_SKGE_CHKSM_XXX
 *
 * Description:
 *	Retrieves the statistic values of the CSUM module. The CSUM data
 *	structure must be available in the SK_AC even if the CSUM module
 *	is not included, because PNMI reads the statistic data from the
 *	CSUM part of SK_AC directly.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
PNMI_STATIC int CsumStat(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	unsigned int	Index;
	unsigned int	Limit;
	unsigned int	Offset = 0;
	SK_U64		StatVal;


	/*
	 * Calculate instance if wished
	 */
	if (Instance != (SK_U32)(-1)) {

		if ((Instance < 1) || (Instance > SKCS_NUM_PROTOCOLS)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}
		Index = (unsigned int)Instance - 1;
		Limit = Index + 1;
	}
	else {
		Index = 0;
		Limit = SKCS_NUM_PROTOCOLS;
	}

	/*
	 * Check action
	 */
	if (Action != SK_PNMI_GET) {

		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}

	/* Check length */
	if (*pLen < (Limit - Index) * sizeof(SK_U64)) {

		*pLen = (Limit - Index) * sizeof(SK_U64);
		return (SK_PNMI_ERR_TOO_SHORT);
	}

	/*
	 * Get value
	 */
	for (; Index < Limit; Index ++) {

		switch (Id) {

		case OID_SKGE_CHKSM_RX_OK_CTS:
			StatVal = pAC->Csum.ProtoStats[NetIndex][Index].RxOkCts;
			break;

		case OID_SKGE_CHKSM_RX_UNABLE_CTS:
			StatVal = pAC->Csum.ProtoStats[NetIndex][Index].RxUnableCts;
			break;

		case OID_SKGE_CHKSM_RX_ERR_CTS:
			StatVal = pAC->Csum.ProtoStats[NetIndex][Index].RxErrCts;
			break;

		case OID_SKGE_CHKSM_TX_OK_CTS:
			StatVal = pAC->Csum.ProtoStats[NetIndex][Index].TxOkCts;
			break;

		case OID_SKGE_CHKSM_TX_UNABLE_CTS:
			StatVal = pAC->Csum.ProtoStats[NetIndex][Index].TxUnableCts;
			break;

		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR010,
				SK_PNMI_ERR010MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		SK_PNMI_STORE_U64(pBuf + Offset, StatVal);
		Offset += sizeof(SK_U64);
	}

	/*
	 * Store used buffer space
	 */
	*pLen = Offset;

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * SensorStat - OID handler function of OID_SKGE_SENSOR_XXX
 *
 * Description:
 *	Retrieves the statistic values of the I2C module, which handles
 *	the temperature and voltage sensors.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
PNMI_STATIC int SensorStat(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	unsigned int	i;
	unsigned int	Index;
	unsigned int	Limit;
	unsigned int	Offset;
	unsigned int	Len;
	SK_U32		Val32;
	SK_U64		Val64;


	/*
	 * Calculate instance if wished
	 */
	if ((Instance != (SK_U32)(-1))) {

		if ((Instance < 1) || (Instance > (SK_U32)pAC->I2c.MaxSens)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}

		Index = (unsigned int)Instance -1;
		Limit = (unsigned int)Instance;
	}
	else {
		Index = 0;
		Limit = (unsigned int) pAC->I2c.MaxSens;
	}

	/*
	 * Check action
	 */
	if (Action != SK_PNMI_GET) {

		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}

	/* Check length */
	switch (Id) {

	case OID_SKGE_SENSOR_VALUE:
	case OID_SKGE_SENSOR_WAR_THRES_LOW:
	case OID_SKGE_SENSOR_WAR_THRES_UPP:
	case OID_SKGE_SENSOR_ERR_THRES_LOW:
	case OID_SKGE_SENSOR_ERR_THRES_UPP:
		if (*pLen < (Limit - Index) * sizeof(SK_U32)) {

			*pLen = (Limit - Index) * sizeof(SK_U32);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_SENSOR_DESCR:
		for (Offset = 0, i = Index; i < Limit; i ++) {

			Len = (unsigned int)
				SK_STRLEN(pAC->I2c.SenTable[i].SenDesc) + 1;
			if (Len >= SK_PNMI_STRINGLEN2) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR011,
					SK_PNMI_ERR011MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			Offset += Len;
		}
		if (*pLen < Offset) {

			*pLen = Offset;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_SENSOR_INDEX:
	case OID_SKGE_SENSOR_TYPE:
	case OID_SKGE_SENSOR_STATUS:
		if (*pLen < Limit - Index) {

			*pLen = Limit - Index;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_SENSOR_WAR_CTS:
	case OID_SKGE_SENSOR_WAR_TIME:
	case OID_SKGE_SENSOR_ERR_CTS:
	case OID_SKGE_SENSOR_ERR_TIME:
		if (*pLen < (Limit - Index) * sizeof(SK_U64)) {

			*pLen = (Limit - Index) * sizeof(SK_U64);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	default:
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR012,
			SK_PNMI_ERR012MSG);

		*pLen = 0;
		return (SK_PNMI_ERR_GENERAL);

	}

	/*
	 * Get value
	 */
	for (Offset = 0; Index < Limit; Index ++) {

		switch (Id) {

		case OID_SKGE_SENSOR_INDEX:
			*(pBuf + Offset) = (char)Index;
			Offset += sizeof(char);
			break;

		case OID_SKGE_SENSOR_DESCR:
			Len = SK_STRLEN(pAC->I2c.SenTable[Index].SenDesc);
			SK_MEMCPY(pBuf + Offset + 1,
				pAC->I2c.SenTable[Index].SenDesc, Len);
			*(pBuf + Offset) = (char)Len;
			Offset += Len + 1;
			break;

		case OID_SKGE_SENSOR_TYPE:
			*(pBuf + Offset) =
				(char)pAC->I2c.SenTable[Index].SenType;
			Offset += sizeof(char);
			break;

		case OID_SKGE_SENSOR_VALUE:
			Val32 = (SK_U32)pAC->I2c.SenTable[Index].SenValue;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_SENSOR_WAR_THRES_LOW:
			Val32 = (SK_U32)pAC->I2c.SenTable[Index].
				SenThreWarnLow;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_SENSOR_WAR_THRES_UPP:
			Val32 = (SK_U32)pAC->I2c.SenTable[Index].
				SenThreWarnHigh;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_SENSOR_ERR_THRES_LOW:
			Val32 = (SK_U32)pAC->I2c.SenTable[Index].
				SenThreErrLow;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_SENSOR_ERR_THRES_UPP:
			Val32 = pAC->I2c.SenTable[Index].SenThreErrHigh;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_SENSOR_STATUS:
			*(pBuf + Offset) =
				(char)pAC->I2c.SenTable[Index].SenErrFlag;
			Offset += sizeof(char);
			break;

		case OID_SKGE_SENSOR_WAR_CTS:
			Val64 = pAC->I2c.SenTable[Index].SenWarnCts;
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		case OID_SKGE_SENSOR_ERR_CTS:
			Val64 = pAC->I2c.SenTable[Index].SenErrCts;
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		case OID_SKGE_SENSOR_WAR_TIME:
			Val64 = SK_PNMI_HUNDREDS_SEC(pAC->I2c.SenTable[Index].
				SenBegWarnTS);
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		case OID_SKGE_SENSOR_ERR_TIME:
			Val64 = SK_PNMI_HUNDREDS_SEC(pAC->I2c.SenTable[Index].
				SenBegErrTS);
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		default:
			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_ERR,
				("SensorStat: Unknown OID should be handled before"));

			return (SK_PNMI_ERR_GENERAL);
		}
	}

	/*
	 * Store used buffer space
	 */
	*pLen = Offset;

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * Vpd - OID handler function of OID_SKGE_VPD_XXX
 *
 * Description:
 *	Get/preset/set of VPD data. As instance the name of a VPD key
 *	can be passed. The Instance parameter is a SK_U32 and can be
 *	used as a string buffer for the VPD key, because their maximum
 *	length is 4 byte.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
PNMI_STATIC int Vpd(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	SK_VPD_STATUS	*pVpdStatus;
	unsigned int	BufLen;
	char		Buf[256];
	char		KeyArr[SK_PNMI_VPD_ENTRIES][SK_PNMI_VPD_KEY_SIZE];
	char		KeyStr[SK_PNMI_VPD_KEY_SIZE];
	unsigned int	KeyNo;
	unsigned int	Offset;
	unsigned int	Index;
	unsigned int	FirstIndex;
	unsigned int	LastIndex;
	unsigned int	Len;
	int		Ret;
	SK_U32		Val32;

	/*
	 * Get array of all currently stored VPD keys
	 */
	Ret = GetVpdKeyArr(pAC, IoC, &KeyArr[0][0], sizeof(KeyArr), &KeyNo);
	if (Ret != SK_PNMI_ERR_OK) {
		*pLen = 0;
		return (Ret);
	}

	/*
	 * If instance is not -1, try to find the requested VPD key for
	 * the multiple instance variables. The other OIDs as for example
	 * OID VPD_ACTION are single instance variables and must be
	 * handled separatly.
	 */
	FirstIndex = 0;
	LastIndex = KeyNo;

	if ((Instance != (SK_U32)(-1))) {

		if (Id == OID_SKGE_VPD_KEY || Id == OID_SKGE_VPD_VALUE ||
			Id == OID_SKGE_VPD_ACCESS) {

			SK_STRNCPY(KeyStr, (char *)&Instance, 4);
			KeyStr[4] = 0;

			for (Index = 0; Index < KeyNo; Index ++) {

				if (SK_STRCMP(KeyStr, KeyArr[Index]) == 0) {
					FirstIndex = Index;
					LastIndex = Index+1;
					break;
				}
			}
			if (Index == KeyNo) {

				*pLen = 0;
				return (SK_PNMI_ERR_UNKNOWN_INST);
			}
		}
		else if (Instance != 1) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}
	}

	/*
	 * Get value, if a query should be performed
	 */
	if (Action == SK_PNMI_GET) {

		switch (Id) {

		case OID_SKGE_VPD_FREE_BYTES:
			/* Check length of buffer */
			if (*pLen < sizeof(SK_U32)) {

				*pLen = sizeof(SK_U32);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			/* Get number of free bytes */
			pVpdStatus = VpdStat(pAC, IoC);
			if (pVpdStatus == NULL) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR017,
					SK_PNMI_ERR017MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			if ((pVpdStatus->vpd_status & VPD_VALID) == 0) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR018,
					SK_PNMI_ERR018MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			
			Val32 = (SK_U32)pVpdStatus->vpd_free_rw;
			SK_PNMI_STORE_U32(pBuf, Val32);
			*pLen = sizeof(SK_U32);
			break;

		case OID_SKGE_VPD_ENTRIES_LIST:
			/* Check length */
			for (Len = 0, Index = 0; Index < KeyNo; Index ++) {

				Len += SK_STRLEN(KeyArr[Index]) + 1;
			}
			if (*pLen < Len) {

				*pLen = Len;
				return (SK_PNMI_ERR_TOO_SHORT);
			}

			/* Get value */
			*(pBuf) = (char)Len - 1;
			for (Offset = 1, Index = 0; Index < KeyNo; Index ++) {

				Len = SK_STRLEN(KeyArr[Index]);
				SK_MEMCPY(pBuf + Offset, KeyArr[Index], Len);

				Offset += Len;

				if (Index < KeyNo - 1) {

					*(pBuf + Offset) = ' ';
					Offset ++;
				}
			}
			*pLen = Offset;
			break;

		case OID_SKGE_VPD_ENTRIES_NUMBER:
			/* Check length */
			if (*pLen < sizeof(SK_U32)) {

				*pLen = sizeof(SK_U32);
				return (SK_PNMI_ERR_TOO_SHORT);
			}

			Val32 = (SK_U32)KeyNo;
			SK_PNMI_STORE_U32(pBuf, Val32);
			*pLen = sizeof(SK_U32);
			break;

		case OID_SKGE_VPD_KEY:
			/* Check buffer length, if it is large enough */
			for (Len = 0, Index = FirstIndex;
				Index < LastIndex; Index ++) {

				Len += SK_STRLEN(KeyArr[Index]) + 1;
			}
			if (*pLen < Len) {

				*pLen = Len;
				return (SK_PNMI_ERR_TOO_SHORT);
			}

			/*
			 * Get the key to an intermediate buffer, because
			 * we have to prepend a length byte.
			 */
			for (Offset = 0, Index = FirstIndex;
				Index < LastIndex; Index ++) {

				Len = SK_STRLEN(KeyArr[Index]);

				*(pBuf + Offset) = (char)Len;
				SK_MEMCPY(pBuf + Offset + 1, KeyArr[Index],
					Len);
				Offset += Len + 1;
			}
			*pLen = Offset;
			break;

		case OID_SKGE_VPD_VALUE:
			/* Check the buffer length if it is large enough */
			for (Offset = 0, Index = FirstIndex;
				Index < LastIndex; Index ++) {

				BufLen = 256;
				if (VpdRead(pAC, IoC, KeyArr[Index], Buf,
					(int *)&BufLen) > 0 ||
					BufLen >= SK_PNMI_VPD_DATALEN) {

					SK_ERR_LOG(pAC, SK_ERRCL_SW,
						SK_PNMI_ERR021,
						SK_PNMI_ERR021MSG);

					return (SK_PNMI_ERR_GENERAL);
				}
				Offset += BufLen + 1;
			}
			if (*pLen < Offset) {

				*pLen = Offset;
				return (SK_PNMI_ERR_TOO_SHORT);
			}

			/*
			 * Get the value to an intermediate buffer, because
			 * we have to prepend a length byte.
			 */
			for (Offset = 0, Index = FirstIndex;
				Index < LastIndex; Index ++) {

				BufLen = 256;
				if (VpdRead(pAC, IoC, KeyArr[Index], Buf,
					(int *)&BufLen) > 0 ||
					BufLen >= SK_PNMI_VPD_DATALEN) {

					SK_ERR_LOG(pAC, SK_ERRCL_SW,
						SK_PNMI_ERR022,
						SK_PNMI_ERR022MSG);

					*pLen = 0;
					return (SK_PNMI_ERR_GENERAL);
				}

				*(pBuf + Offset) = (char)BufLen;
				SK_MEMCPY(pBuf + Offset + 1, Buf, BufLen);
				Offset += BufLen + 1;
			}
			*pLen = Offset;
			break;

		case OID_SKGE_VPD_ACCESS:
			if (*pLen < LastIndex - FirstIndex) {

				*pLen = LastIndex - FirstIndex;
				return (SK_PNMI_ERR_TOO_SHORT);
			}

			for (Offset = 0, Index = FirstIndex;
				Index < LastIndex; Index ++) {

				if (VpdMayWrite(KeyArr[Index])) {

					*(pBuf + Offset) = SK_PNMI_VPD_RW;
				}
				else {
					*(pBuf + Offset) = SK_PNMI_VPD_RO;
				}
				Offset ++;
			}
			*pLen = Offset;
			break;

		case OID_SKGE_VPD_ACTION:
			Offset = LastIndex - FirstIndex;
			if (*pLen < Offset) {

				*pLen = Offset;
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			SK_MEMSET(pBuf, 0, Offset);
			*pLen = Offset;
			break;

		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR023,
				SK_PNMI_ERR023MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}
	}
	else {
		/* The only OID which can be set is VPD_ACTION */
		if (Id != OID_SKGE_VPD_ACTION) {

			if (Id == OID_SKGE_VPD_FREE_BYTES ||
				Id == OID_SKGE_VPD_ENTRIES_LIST ||
				Id == OID_SKGE_VPD_ENTRIES_NUMBER ||
				Id == OID_SKGE_VPD_KEY ||
				Id == OID_SKGE_VPD_VALUE ||
				Id == OID_SKGE_VPD_ACCESS) {

				*pLen = 0;
				return (SK_PNMI_ERR_READ_ONLY);
			}

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR024,
				SK_PNMI_ERR024MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		/*
		 * From this point we handle VPD_ACTION. Check the buffer
		 * length. It should at least have the size of one byte.
		 */
		if (*pLen < 1) {

			*pLen = 1;
			return (SK_PNMI_ERR_TOO_SHORT);
		}

		/*
		 * The first byte contains the VPD action type we should
		 * perform.
		 */
		switch (*pBuf) {

		case SK_PNMI_VPD_IGNORE:
			/* Nothing to do */
			break;

		case SK_PNMI_VPD_CREATE:
			/*
			 * We have to create a new VPD entry or we modify
			 * an existing one. Check first the buffer length.
			 */
			if (*pLen < 4) {

				*pLen = 4;
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			KeyStr[0] = pBuf[1];
			KeyStr[1] = pBuf[2];
			KeyStr[2] = 0;

			/*
			 * Is the entry writable or does it belong to the
			 * read-only area?
			 */
			if (!VpdMayWrite(KeyStr)) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}

			Offset = (int)pBuf[3] & 0xFF;

			SK_MEMCPY(Buf, pBuf + 4, Offset);
			Buf[Offset] = 0;

			/* A preset ends here */
			if (Action == SK_PNMI_PRESET) {

				return (SK_PNMI_ERR_OK);
			}

			/* Write the new entry or modify an existing one */
			Ret = VpdWrite(pAC, IoC, KeyStr, Buf);
			if (Ret == SK_PNMI_VPD_NOWRITE ) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}
			else if (Ret != SK_PNMI_VPD_OK) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR025,
					SK_PNMI_ERR025MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}

			/*
			 * Perform an update of the VPD data. This is
			 * not mandantory, but just to be sure.
			 */
			Ret = VpdUpdate(pAC, IoC);
			if (Ret != SK_PNMI_VPD_OK) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR026,
					SK_PNMI_ERR026MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			break;

		case SK_PNMI_VPD_DELETE:
			/* Check if the buffer size is plausible */
			if (*pLen < 3) {

				*pLen = 3;
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			if (*pLen > 3) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}
			KeyStr[0] = pBuf[1];
			KeyStr[1] = pBuf[2];
			KeyStr[2] = 0;

			/* Find the passed key in the array */
			for (Index = 0; Index < KeyNo; Index ++) {

				if (SK_STRCMP(KeyStr, KeyArr[Index]) == 0) {

					break;
				}
			}
			/*
			 * If we cannot find the key it is wrong, so we
			 * return an appropriate error value.
			 */
			if (Index == KeyNo) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}

			if (Action == SK_PNMI_PRESET) {

				return (SK_PNMI_ERR_OK);
			}

			/* Ok, you wanted it and you will get it */
			Ret = VpdDelete(pAC, IoC, KeyStr);
			if (Ret != SK_PNMI_VPD_OK) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR027,
					SK_PNMI_ERR027MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}

			/*
			 * Perform an update of the VPD data. This is
			 * not mandantory, but just to be sure.
			 */
			Ret = VpdUpdate(pAC, IoC);
			if (Ret != SK_PNMI_VPD_OK) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR028,
					SK_PNMI_ERR028MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			break;

		default:
			*pLen = 0;
			return (SK_PNMI_ERR_BAD_VALUE);
		}
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * General - OID handler function of various single instance OIDs
 *
 * Description:
 *	The code is simple. No description necessary.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
PNMI_STATIC int General(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	int		Ret;
	unsigned int	Index;
	unsigned int	Len;
	unsigned int	Offset;
	unsigned int	Val;
	SK_U8		Val8;
	SK_U16		Val16;
	SK_U32		Val32;
	SK_U64		Val64;
	SK_U64		Val64RxHwErrs = 0;
	SK_U64		Val64TxHwErrs = 0;
	SK_BOOL		Is64BitReq = SK_FALSE;
	char		Buf[256];
	int			MacType;

	/*
	 * Check instance. We only handle single instance variables.
	 */
	if (Instance != (SK_U32)(-1) && Instance != 1) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_INST);
	}

	/*
	 * Check action. We only allow get requests.
	 */
	if (Action != SK_PNMI_GET) {

		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}
	
	MacType = pAC->GIni.GIMacType;
	
	/*
	 * Check length for the various supported OIDs
	 */
	switch (Id) {

	case OID_GEN_XMIT_ERROR:
	case OID_GEN_RCV_ERROR:
	case OID_GEN_RCV_NO_BUFFER:
#ifndef SK_NDIS_64BIT_CTR
		if (*pLen < sizeof(SK_U32)) {
			*pLen = sizeof(SK_U32);
			return (SK_PNMI_ERR_TOO_SHORT);
		}

#else /* SK_NDIS_64BIT_CTR */

		/*
		 * for compatibility, at least 32bit are required for oid
		 */
		if (*pLen < sizeof(SK_U32)) {
			/*
			* but indicate handling for 64bit values,
			* if insufficient space is provided
			*/
			*pLen = sizeof(SK_U64);
			return (SK_PNMI_ERR_TOO_SHORT);
		}

		Is64BitReq = (*pLen < sizeof(SK_U64)) ? SK_FALSE : SK_TRUE;
#endif /* SK_NDIS_64BIT_CTR */
		break;

	case OID_SKGE_PORT_NUMBER:
	case OID_SKGE_DEVICE_TYPE:
	case OID_SKGE_RESULT:
	case OID_SKGE_RLMT_MONITOR_NUMBER:
	case OID_GEN_TRANSMIT_QUEUE_LENGTH:
	case OID_SKGE_TRAP_NUMBER:
	case OID_SKGE_MDB_VERSION:
	case OID_SKGE_BOARDLEVEL:
	case OID_SKGE_CHIPID:
	case OID_SKGE_RAMSIZE:
		if (*pLen < sizeof(SK_U32)) {

			*pLen = sizeof(SK_U32);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_CHIPSET:
		if (*pLen < sizeof(SK_U16)) {

			*pLen = sizeof(SK_U16);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_BUS_TYPE:
	case OID_SKGE_BUS_SPEED:
	case OID_SKGE_BUS_WIDTH:
	case OID_SKGE_SENSOR_NUMBER:
	case OID_SKGE_CHKSM_NUMBER:
	case OID_SKGE_VAUXAVAIL:
		if (*pLen < sizeof(SK_U8)) {

			*pLen = sizeof(SK_U8);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_TX_SW_QUEUE_LEN:
	case OID_SKGE_TX_SW_QUEUE_MAX:
	case OID_SKGE_TX_RETRY:
	case OID_SKGE_RX_INTR_CTS:
	case OID_SKGE_TX_INTR_CTS:
	case OID_SKGE_RX_NO_BUF_CTS:
	case OID_SKGE_TX_NO_BUF_CTS:
	case OID_SKGE_TX_USED_DESCR_NO:
	case OID_SKGE_RX_DELIVERED_CTS:
	case OID_SKGE_RX_OCTETS_DELIV_CTS:
	case OID_SKGE_RX_HW_ERROR_CTS:
	case OID_SKGE_TX_HW_ERROR_CTS:
	case OID_SKGE_IN_ERRORS_CTS:
	case OID_SKGE_OUT_ERROR_CTS:
	case OID_SKGE_ERR_RECOVERY_CTS:
	case OID_SKGE_SYSUPTIME:
		if (*pLen < sizeof(SK_U64)) {

			*pLen = sizeof(SK_U64);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	default:
		/* Checked later */
		break;
	}

	/* Update statistic */
	if (Id == OID_SKGE_RX_HW_ERROR_CTS ||
		Id == OID_SKGE_TX_HW_ERROR_CTS ||
		Id == OID_SKGE_IN_ERRORS_CTS ||
		Id == OID_SKGE_OUT_ERROR_CTS ||
		Id == OID_GEN_XMIT_ERROR ||
		Id == OID_GEN_RCV_ERROR) {

		/* Force the XMAC to update its statistic counters and
		 * Increment semaphore to indicate that an update was
		 * already done.
		 */
		Ret = MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1);
		if (Ret != SK_PNMI_ERR_OK) {

			*pLen = 0;
			return (Ret);
		}
		pAC->Pnmi.MacUpdatedFlag ++;

		/*
		 * Some OIDs consist of multiple hardware counters. Those
		 * values which are contained in all of them will be added
		 * now.
		 */
		switch (Id) {

		case OID_SKGE_RX_HW_ERROR_CTS:
		case OID_SKGE_IN_ERRORS_CTS:
		case OID_GEN_RCV_ERROR:
			Val64RxHwErrs =
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_MISSED, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_FRAMING, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_OVERFLOW, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_JABBER, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_CARRIER, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_IRLENGTH, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_SYMBOL, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_SHORTS, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_RUNT, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_TOO_LONG, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_FCS, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_CEXT, NetIndex);
	        break;

		case OID_SKGE_TX_HW_ERROR_CTS:
		case OID_SKGE_OUT_ERROR_CTS:
		case OID_GEN_XMIT_ERROR:
			Val64TxHwErrs =
				GetStatVal(pAC, IoC, 0, SK_PNMI_HTX_EXCESS_COL, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HTX_LATE_COL, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HTX_UNDERRUN, NetIndex) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HTX_CARRIER, NetIndex);
			break;
		}
	}

	/*
	 * Retrieve value
	 */
	switch (Id) {

	case OID_SKGE_SUPPORTED_LIST:
		Len = ID_TABLE_SIZE * sizeof(SK_U32);
		if (*pLen < Len) {

			*pLen = Len;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		for (Offset = 0, Index = 0; Offset < Len;
			Offset += sizeof(SK_U32), Index ++) {

			Val32 = (SK_U32)IdTable[Index].Id;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
		}
		*pLen = Len;
		break;

	case OID_SKGE_BOARDLEVEL:
		Val32 = (SK_U32)pAC->GIni.GILevel;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_SKGE_PORT_NUMBER:
		Val32 = (SK_U32)pAC->GIni.GIMacsFound;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_SKGE_DEVICE_TYPE:
		Val32 = (SK_U32)pAC->Pnmi.DeviceType;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_SKGE_DRIVER_DESCR:
		if (pAC->Pnmi.pDriverDescription == NULL) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR007,
				SK_PNMI_ERR007MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		Len = SK_STRLEN(pAC->Pnmi.pDriverDescription) + 1;
		if (Len > SK_PNMI_STRINGLEN1) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR029,
				SK_PNMI_ERR029MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		if (*pLen < Len) {

			*pLen = Len;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		*pBuf = (char)(Len - 1);
		SK_MEMCPY(pBuf + 1, pAC->Pnmi.pDriverDescription, Len - 1);
		*pLen = Len;
		break;

	case OID_SKGE_DRIVER_VERSION:
		if (pAC->Pnmi.pDriverVersion == NULL) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR030,
				SK_PNMI_ERR030MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		Len = SK_STRLEN(pAC->Pnmi.pDriverVersion) + 1;
		if (Len > SK_PNMI_STRINGLEN1) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR031,
				SK_PNMI_ERR031MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		if (*pLen < Len) {

			*pLen = Len;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		*pBuf = (char)(Len - 1);
		SK_MEMCPY(pBuf + 1, pAC->Pnmi.pDriverVersion, Len - 1);
		*pLen = Len;
		break;

	case OID_SKGE_DRIVER_RELDATE:
		if (pAC->Pnmi.pDriverReleaseDate == NULL) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR030,
				SK_PNMI_ERR053MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		Len = SK_STRLEN(pAC->Pnmi.pDriverReleaseDate) + 1;
		if (Len > SK_PNMI_STRINGLEN1) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR031,
				SK_PNMI_ERR054MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		if (*pLen < Len) {

			*pLen = Len;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		*pBuf = (char)(Len - 1);
		SK_MEMCPY(pBuf + 1, pAC->Pnmi.pDriverReleaseDate, Len - 1);
		*pLen = Len;
		break;

	case OID_SKGE_DRIVER_FILENAME:
		if (pAC->Pnmi.pDriverFileName == NULL) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR030,
				SK_PNMI_ERR055MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		Len = SK_STRLEN(pAC->Pnmi.pDriverFileName) + 1;
		if (Len > SK_PNMI_STRINGLEN1) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR031,
				SK_PNMI_ERR056MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		if (*pLen < Len) {

			*pLen = Len;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		*pBuf = (char)(Len - 1);
		SK_MEMCPY(pBuf + 1, pAC->Pnmi.pDriverFileName, Len - 1);
		*pLen = Len;
		break;

	case OID_SKGE_HW_DESCR:
		/*
		 * The hardware description is located in the VPD. This
		 * query may move to the initialisation routine. But
		 * the VPD data is cached and therefore a call here
		 * will not make much difference.
		 */
		Len = 256;
		if (VpdRead(pAC, IoC, VPD_NAME, Buf, (int *)&Len) > 0) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR032,
				SK_PNMI_ERR032MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}
		Len ++;
		if (Len > SK_PNMI_STRINGLEN1) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR033,
				SK_PNMI_ERR033MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}
		if (*pLen < Len) {

			*pLen = Len;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		*pBuf = (char)(Len - 1);
		SK_MEMCPY(pBuf + 1, Buf, Len - 1);
		*pLen = Len;
		break;

	case OID_SKGE_HW_VERSION:
		/* Oh, I love to do some string manipulation */
		if (*pLen < 5) {

			*pLen = 5;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		Val8 = (SK_U8)pAC->GIni.GIPciHwRev;
		pBuf[0] = 4;
		pBuf[1] = 'v';
		pBuf[2] = (char)(0x30 | ((Val8 >> 4) & 0x0F));
		pBuf[3] = '.';
		pBuf[4] = (char)(0x30 | (Val8 & 0x0F));
		*pLen = 5;
		break;

	case OID_SKGE_CHIPSET:
		Val16 = pAC->Pnmi.Chipset;
		SK_PNMI_STORE_U16(pBuf, Val16);
		*pLen = sizeof(SK_U16);
		break;

	case OID_SKGE_CHIPID:
		Val32 = pAC->GIni.GIChipId;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_SKGE_RAMSIZE:
		Val32 = pAC->GIni.GIRamSize;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_SKGE_VAUXAVAIL:
		*pBuf = (char) pAC->GIni.GIVauxAvail;
		*pLen = sizeof(char);
		break;

	case OID_SKGE_BUS_TYPE:
		*pBuf = (char) SK_PNMI_BUS_PCI;
		*pLen = sizeof(char);
		break;

	case OID_SKGE_BUS_SPEED:
		*pBuf = pAC->Pnmi.PciBusSpeed;
		*pLen = sizeof(char);
		break;

	case OID_SKGE_BUS_WIDTH:
		*pBuf = pAC->Pnmi.PciBusWidth;
		*pLen = sizeof(char);
		break;

	case OID_SKGE_RESULT:
		Val32 = pAC->Pnmi.TestResult;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_SKGE_SENSOR_NUMBER:
		*pBuf = (char)pAC->I2c.MaxSens;
		*pLen = sizeof(char);
		break;

	case OID_SKGE_CHKSM_NUMBER:
		*pBuf = SKCS_NUM_PROTOCOLS;
		*pLen = sizeof(char);
		break;

	case OID_SKGE_TRAP_NUMBER:
		GetTrapQueueLen(pAC, &Len, &Val);
		Val32 = (SK_U32)Val;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_SKGE_TRAP:
		GetTrapQueueLen(pAC, &Len, &Val);
		if (*pLen < Len) {

			*pLen = Len;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		CopyTrapQueue(pAC, pBuf);
		*pLen = Len;
		break;

	case OID_SKGE_RLMT_MONITOR_NUMBER:
/* XXX Not yet implemented by RLMT therefore we return zero elements */
		Val32 = 0;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_SKGE_TX_SW_QUEUE_LEN:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.BufPort[NetIndex].TxSwQueueLen;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.BufPort[0].TxSwQueueLen +
					pAC->Pnmi.BufPort[1].TxSwQueueLen;
			}			
		}
		else {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.Port[NetIndex].TxSwQueueLen;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.Port[0].TxSwQueueLen +
					pAC->Pnmi.Port[1].TxSwQueueLen;
			}			
		}
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;


	case OID_SKGE_TX_SW_QUEUE_MAX:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.BufPort[NetIndex].TxSwQueueMax;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.BufPort[0].TxSwQueueMax +
					pAC->Pnmi.BufPort[1].TxSwQueueMax;
			}
		}
		else {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.Port[NetIndex].TxSwQueueMax;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.Port[0].TxSwQueueMax +
					pAC->Pnmi.Port[1].TxSwQueueMax;
			}
		}
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_TX_RETRY:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.BufPort[NetIndex].TxRetryCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.BufPort[0].TxRetryCts +
					pAC->Pnmi.BufPort[1].TxRetryCts;
			}
		}
		else {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.Port[NetIndex].TxRetryCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.Port[0].TxRetryCts +
					pAC->Pnmi.Port[1].TxRetryCts;
			}
		}
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_RX_INTR_CTS:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.BufPort[NetIndex].RxIntrCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.BufPort[0].RxIntrCts +
					pAC->Pnmi.BufPort[1].RxIntrCts;
			}
		}
		else {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.Port[NetIndex].RxIntrCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.Port[0].RxIntrCts +
					pAC->Pnmi.Port[1].RxIntrCts;
			}
		}
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_TX_INTR_CTS:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.BufPort[NetIndex].TxIntrCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.BufPort[0].TxIntrCts +
					pAC->Pnmi.BufPort[1].TxIntrCts;
			}
		}
		else {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.Port[NetIndex].TxIntrCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.Port[0].TxIntrCts +
					pAC->Pnmi.Port[1].TxIntrCts;
			}
		}
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_RX_NO_BUF_CTS:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.BufPort[NetIndex].RxNoBufCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.BufPort[0].RxNoBufCts +
					pAC->Pnmi.BufPort[1].RxNoBufCts;
			}
		}
		else {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.Port[NetIndex].RxNoBufCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.Port[0].RxNoBufCts +
					pAC->Pnmi.Port[1].RxNoBufCts;
			}
		}
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_TX_NO_BUF_CTS:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.BufPort[NetIndex].TxNoBufCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.BufPort[0].TxNoBufCts +
					pAC->Pnmi.BufPort[1].TxNoBufCts;
			}
		}
		else {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.Port[NetIndex].TxNoBufCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.Port[0].TxNoBufCts +
					pAC->Pnmi.Port[1].TxNoBufCts;
			}
		}
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_TX_USED_DESCR_NO:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.BufPort[NetIndex].TxUsedDescrNo;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.BufPort[0].TxUsedDescrNo +
					pAC->Pnmi.BufPort[1].TxUsedDescrNo;
			}
		}
		else {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.Port[NetIndex].TxUsedDescrNo;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.Port[0].TxUsedDescrNo +
					pAC->Pnmi.Port[1].TxUsedDescrNo;
			}
		}
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_RX_DELIVERED_CTS:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.BufPort[NetIndex].RxDeliveredCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.BufPort[0].RxDeliveredCts +
					pAC->Pnmi.BufPort[1].RxDeliveredCts;
			}
		}
		else {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.Port[NetIndex].RxDeliveredCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.Port[0].RxDeliveredCts +
					pAC->Pnmi.Port[1].RxDeliveredCts;
			}
		}
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_RX_OCTETS_DELIV_CTS:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.BufPort[NetIndex].RxOctetsDeliveredCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.BufPort[0].RxOctetsDeliveredCts +
					pAC->Pnmi.BufPort[1].RxOctetsDeliveredCts;
			}
		}
		else {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.Port[NetIndex].RxOctetsDeliveredCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.Port[0].RxOctetsDeliveredCts +
					pAC->Pnmi.Port[1].RxOctetsDeliveredCts;
			}
		}
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_RX_HW_ERROR_CTS:
		SK_PNMI_STORE_U64(pBuf, Val64RxHwErrs);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_TX_HW_ERROR_CTS:
		SK_PNMI_STORE_U64(pBuf, Val64TxHwErrs);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_IN_ERRORS_CTS:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = Val64RxHwErrs + pAC->Pnmi.BufPort[NetIndex].RxNoBufCts;
			}
			/* Single net mode */
			else {
				Val64 = Val64RxHwErrs +
					pAC->Pnmi.BufPort[0].RxNoBufCts +
					pAC->Pnmi.BufPort[1].RxNoBufCts;
			}
		}
		else {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = Val64RxHwErrs + pAC->Pnmi.Port[NetIndex].RxNoBufCts;
			}
			/* Single net mode */
			else {
				Val64 = Val64RxHwErrs +
					pAC->Pnmi.Port[0].RxNoBufCts +
					pAC->Pnmi.Port[1].RxNoBufCts;
			}
		}
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_OUT_ERROR_CTS:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = Val64TxHwErrs + pAC->Pnmi.BufPort[NetIndex].TxNoBufCts;
			}
			/* Single net mode */
			else {
				Val64 = Val64TxHwErrs +
					pAC->Pnmi.BufPort[0].TxNoBufCts +
					pAC->Pnmi.BufPort[1].TxNoBufCts;
			}
		}
		else {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = Val64TxHwErrs + pAC->Pnmi.Port[NetIndex].TxNoBufCts;
			}
			/* Single net mode */
			else {
				Val64 = Val64TxHwErrs +
					pAC->Pnmi.Port[0].TxNoBufCts +
					pAC->Pnmi.Port[1].TxNoBufCts;
			}
		}
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_ERR_RECOVERY_CTS:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.BufPort[NetIndex].ErrRecoveryCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.BufPort[0].ErrRecoveryCts +
					pAC->Pnmi.BufPort[1].ErrRecoveryCts;
			}
		}
		else {
			/* Dual net mode */
			if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
				Val64 = pAC->Pnmi.Port[NetIndex].ErrRecoveryCts;
			}
			/* Single net mode */
			else {
				Val64 = pAC->Pnmi.Port[0].ErrRecoveryCts +
					pAC->Pnmi.Port[1].ErrRecoveryCts;
			}
		}
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_SYSUPTIME:
		Val64 = SK_PNMI_HUNDREDS_SEC(SkOsGetTime(pAC));
		Val64 -= pAC->Pnmi.StartUpTime;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_MDB_VERSION:
		Val32 = SK_PNMI_MDB_VERSION;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_GEN_RCV_ERROR:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			Val64 = Val64RxHwErrs + pAC->Pnmi.BufPort[NetIndex].RxNoBufCts;
		}
		else {
			Val64 = Val64RxHwErrs + pAC->Pnmi.Port[NetIndex].RxNoBufCts;
		}

		/*
		 * by default 32bit values are evaluated
		 */
		if (!Is64BitReq) {
			Val32 = (SK_U32)Val64;
			SK_PNMI_STORE_U32(pBuf, Val32);
			*pLen = sizeof(SK_U32);
		}
		else {
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
		}
		break;

	case OID_GEN_XMIT_ERROR:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			Val64 = Val64TxHwErrs + pAC->Pnmi.BufPort[NetIndex].TxNoBufCts;
		}
		else {
			Val64 = Val64TxHwErrs + pAC->Pnmi.Port[NetIndex].TxNoBufCts;
		}

		/*
		 * by default 32bit values are evaluated
		 */
		if (!Is64BitReq) {
			Val32 = (SK_U32)Val64;
			SK_PNMI_STORE_U32(pBuf, Val32);
			*pLen = sizeof(SK_U32);
		}
		else {
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
		}
		break;

	case OID_GEN_RCV_NO_BUFFER:
		/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
		if (MacType == SK_MAC_XMAC) {
			Val64 = pAC->Pnmi.BufPort[NetIndex].RxNoBufCts;
		}
		else {
			Val64 = pAC->Pnmi.Port[NetIndex].RxNoBufCts;
		}

		/*
		 * by default 32bit values are evaluated
		 */
		if (!Is64BitReq) {
			Val32 = (SK_U32)Val64;
			SK_PNMI_STORE_U32(pBuf, Val32);
			*pLen = sizeof(SK_U32);
		}
		else {
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
		}
		break;

	case OID_GEN_TRANSMIT_QUEUE_LENGTH:
		Val32 = (SK_U32)pAC->Pnmi.Port[NetIndex].TxSwQueueLen;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	default:
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR034,
			SK_PNMI_ERR034MSG);

		*pLen = 0;
		return (SK_PNMI_ERR_GENERAL);
	}

	if (Id == OID_SKGE_RX_HW_ERROR_CTS ||
		Id == OID_SKGE_TX_HW_ERROR_CTS ||
		Id == OID_SKGE_IN_ERRORS_CTS ||
		Id == OID_SKGE_OUT_ERROR_CTS ||
		Id == OID_GEN_XMIT_ERROR ||
		Id == OID_GEN_RCV_ERROR) {

		pAC->Pnmi.MacUpdatedFlag --;
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * Rlmt - OID handler function of OID_SKGE_RLMT_XXX single instance.
 *
 * Description:
 *	Get/Presets/Sets the RLMT OIDs.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
PNMI_STATIC int Rlmt(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	int		Ret;
	unsigned int	PhysPortIndex;
	unsigned int	PhysPortMax;
	SK_EVPARA	EventParam;
	SK_U32		Val32;
	SK_U64		Val64;


	/*
	 * Check instance. Only single instance OIDs are allowed here.
	 */
	if (Instance != (SK_U32)(-1) && Instance != 1) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_INST);
	}

	/*
	 * Perform the requested action.
	 */
	if (Action == SK_PNMI_GET) {

		/*
		 * Check if the buffer length is large enough.
		 */

		switch (Id) {

		case OID_SKGE_RLMT_MODE:
		case OID_SKGE_RLMT_PORT_ACTIVE:
		case OID_SKGE_RLMT_PORT_PREFERRED:
			if (*pLen < sizeof(SK_U8)) {

				*pLen = sizeof(SK_U8);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			break;

		case OID_SKGE_RLMT_PORT_NUMBER:
			if (*pLen < sizeof(SK_U32)) {

				*pLen = sizeof(SK_U32);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			break;

		case OID_SKGE_RLMT_CHANGE_CTS:
		case OID_SKGE_RLMT_CHANGE_TIME:
		case OID_SKGE_RLMT_CHANGE_ESTIM:
		case OID_SKGE_RLMT_CHANGE_THRES:
			if (*pLen < sizeof(SK_U64)) {

				*pLen = sizeof(SK_U64);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			break;

		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR035,
				SK_PNMI_ERR035MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		/*
		 * Update RLMT statistic and increment semaphores to indicate
		 * that an update was already done. Maybe RLMT will hold its
		 * statistic always up to date some time. Then we can
		 * remove this type of call.
		 */
		if ((Ret = RlmtUpdate(pAC, IoC, NetIndex)) != SK_PNMI_ERR_OK) {

			*pLen = 0;
			return (Ret);
		}
		pAC->Pnmi.RlmtUpdatedFlag ++;

		/*
		 * Retrieve Value
		*/
		switch (Id) {

		case OID_SKGE_RLMT_MODE:
			*pBuf = (char)pAC->Rlmt.Net[0].RlmtMode;
			*pLen = sizeof(char);
			break;

		case OID_SKGE_RLMT_PORT_NUMBER:
			Val32 = (SK_U32)pAC->GIni.GIMacsFound;
			SK_PNMI_STORE_U32(pBuf, Val32);
			*pLen = sizeof(SK_U32);
			break;

		case OID_SKGE_RLMT_PORT_ACTIVE:
			*pBuf = 0;
			/*
			 * If multiple ports may become active this OID
			 * doesn't make sense any more. A new variable in
			 * the port structure should be created. However,
			 * for this variable the first active port is
			 * returned.
			 */
			PhysPortMax = pAC->GIni.GIMacsFound;

			for (PhysPortIndex = 0; PhysPortIndex < PhysPortMax;
				PhysPortIndex ++) {

				if (pAC->Pnmi.Port[PhysPortIndex].ActiveFlag) {

					*pBuf = (char)SK_PNMI_PORT_PHYS2LOG(PhysPortIndex);
					break;
				}
			}
			*pLen = sizeof(char);
			break;

		case OID_SKGE_RLMT_PORT_PREFERRED:
			*pBuf = (char)SK_PNMI_PORT_PHYS2LOG(pAC->Rlmt.Net[NetIndex].Preference);
			*pLen = sizeof(char);
			break;

		case OID_SKGE_RLMT_CHANGE_CTS:
			Val64 = pAC->Pnmi.RlmtChangeCts;
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
			break;

		case OID_SKGE_RLMT_CHANGE_TIME:
			Val64 = pAC->Pnmi.RlmtChangeTime;
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
			break;

		case OID_SKGE_RLMT_CHANGE_ESTIM:
			Val64 = pAC->Pnmi.RlmtChangeEstimate.Estimate;
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
			break;

		case OID_SKGE_RLMT_CHANGE_THRES:
			Val64 = pAC->Pnmi.RlmtChangeThreshold;
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
			break;

		default:
			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_ERR,
				("Rlmt: Unknown OID should be handled before"));

			pAC->Pnmi.RlmtUpdatedFlag --;
			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		pAC->Pnmi.RlmtUpdatedFlag --;
	}
	else {
		/* Perform a preset or set */
		switch (Id) {

		case OID_SKGE_RLMT_MODE:
			/* Check if the buffer length is plausible */
			if (*pLen < sizeof(char)) {

				*pLen = sizeof(char);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			/* Check if the value range is correct */
			if (*pLen != sizeof(char) ||
				(*pBuf & SK_PNMI_RLMT_MODE_CHK_LINK) == 0 ||
				*(SK_U8 *)pBuf > 15) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}
			/* The preset ends here */
			if (Action == SK_PNMI_PRESET) {

				*pLen = 0;
				return (SK_PNMI_ERR_OK);
			}
			/* Send an event to RLMT to change the mode */
			SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));
			EventParam.Para32[0] |= (SK_U32)(*pBuf);
			EventParam.Para32[1] = 0;
			if (SkRlmtEvent(pAC, IoC, SK_RLMT_MODE_CHANGE,
				EventParam) > 0) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR037,
					SK_PNMI_ERR037MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			break;

		case OID_SKGE_RLMT_PORT_PREFERRED:
			/* Check if the buffer length is plausible */
			if (*pLen < sizeof(char)) {

				*pLen = sizeof(char);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			/* Check if the value range is correct */
			if (*pLen != sizeof(char) || *(SK_U8 *)pBuf >
				(SK_U8)pAC->GIni.GIMacsFound) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}
			/* The preset ends here */
			if (Action == SK_PNMI_PRESET) {

				*pLen = 0;
				return (SK_PNMI_ERR_OK);
			}

			/*
			 * Send an event to RLMT change the preferred port.
			 * A param of -1 means automatic mode. RLMT will
			 * make the decision which is the preferred port.
			 */
			SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));
			EventParam.Para32[0] = (SK_U32)(*pBuf) - 1;
			EventParam.Para32[1] = NetIndex;
			if (SkRlmtEvent(pAC, IoC, SK_RLMT_PREFPORT_CHANGE,
				EventParam) > 0) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR038,
					SK_PNMI_ERR038MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			break;

		case OID_SKGE_RLMT_CHANGE_THRES:
			/* Check if the buffer length is plausible */
			if (*pLen < sizeof(SK_U64)) {

				*pLen = sizeof(SK_U64);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			/*
			 * There are not many restrictions to the
			 * value range.
			 */
			if (*pLen != sizeof(SK_U64)) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}
			/* A preset ends here */
			if (Action == SK_PNMI_PRESET) {

				*pLen = 0;
				return (SK_PNMI_ERR_OK);
			}
			/*
			 * Store the new threshold, which will be taken
			 * on the next timer event.
			 */
			SK_PNMI_READ_U64(pBuf, Val64);
			pAC->Pnmi.RlmtChangeThreshold = Val64;
			break;

		default:
			/* The other OIDs are not be able for set */
			*pLen = 0;
			return (SK_PNMI_ERR_READ_ONLY);
		}
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * RlmtStat - OID handler function of OID_SKGE_RLMT_XXX multiple instance.
 *
 * Description:
 *	Performs get requests on multiple instance variables.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
PNMI_STATIC int RlmtStat(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	unsigned int	PhysPortMax;
	unsigned int	PhysPortIndex;
	unsigned int	Limit;
	unsigned int	Offset;
	int		Ret;
	SK_U32		Val32;
	SK_U64		Val64;

	/*
	 * Calculate the port indexes from the instance.
	 */
	PhysPortMax = pAC->GIni.GIMacsFound;

	if ((Instance != (SK_U32)(-1))) {
		/* Check instance range */
		if ((Instance < 1) || (Instance > PhysPortMax)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}

		/* Single net mode */
		PhysPortIndex = Instance - 1;

		/* Dual net mode */
		if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
			PhysPortIndex = NetIndex;
		}

		/* Both net modes */
		Limit = PhysPortIndex + 1;
	}
	else {
		/* Single net mode */
		PhysPortIndex = 0;
		Limit = PhysPortMax;

		/* Dual net mode */
		if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
			PhysPortIndex = NetIndex;
			Limit = PhysPortIndex + 1;
		}
	}

	/*
	 * Currently only get requests are allowed.
	 */
	if (Action != SK_PNMI_GET) {

		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}

	/*
	 * Check if the buffer length is large enough.
	 */
	switch (Id) {

	case OID_SKGE_RLMT_PORT_INDEX:
	case OID_SKGE_RLMT_STATUS:
		if (*pLen < (Limit - PhysPortIndex) * sizeof(SK_U32)) {

			*pLen = (Limit - PhysPortIndex) * sizeof(SK_U32);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_RLMT_TX_HELLO_CTS:
	case OID_SKGE_RLMT_RX_HELLO_CTS:
	case OID_SKGE_RLMT_TX_SP_REQ_CTS:
	case OID_SKGE_RLMT_RX_SP_CTS:
		if (*pLen < (Limit - PhysPortIndex) * sizeof(SK_U64)) {

			*pLen = (Limit - PhysPortIndex) * sizeof(SK_U64);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	default:
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR039,
			SK_PNMI_ERR039MSG);

		*pLen = 0;
		return (SK_PNMI_ERR_GENERAL);

	}

	/*
	 * Update statistic and increment semaphores to indicate that
	 * an update was already done.
	 */
	if ((Ret = RlmtUpdate(pAC, IoC, NetIndex)) != SK_PNMI_ERR_OK) {

		*pLen = 0;
		return (Ret);
	}
	pAC->Pnmi.RlmtUpdatedFlag ++;

	/*
	 * Get value
	 */
	Offset = 0;
	for (; PhysPortIndex < Limit; PhysPortIndex ++) {

		switch (Id) {

		case OID_SKGE_RLMT_PORT_INDEX:
			Val32 = PhysPortIndex;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_RLMT_STATUS:
			if (pAC->Rlmt.Port[PhysPortIndex].PortState ==
				SK_RLMT_PS_INIT ||
				pAC->Rlmt.Port[PhysPortIndex].PortState ==
				SK_RLMT_PS_DOWN) {

				Val32 = SK_PNMI_RLMT_STATUS_ERROR;
			}
			else if (pAC->Pnmi.Port[PhysPortIndex].ActiveFlag) {

				Val32 = SK_PNMI_RLMT_STATUS_ACTIVE;
			}
			else {
				Val32 = SK_PNMI_RLMT_STATUS_STANDBY;
			}
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_RLMT_TX_HELLO_CTS:
			Val64 = pAC->Rlmt.Port[PhysPortIndex].TxHelloCts;
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		case OID_SKGE_RLMT_RX_HELLO_CTS:
			Val64 = pAC->Rlmt.Port[PhysPortIndex].RxHelloCts;
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		case OID_SKGE_RLMT_TX_SP_REQ_CTS:
			Val64 = pAC->Rlmt.Port[PhysPortIndex].TxSpHelloReqCts;
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		case OID_SKGE_RLMT_RX_SP_CTS:
			Val64 = pAC->Rlmt.Port[PhysPortIndex].RxSpHelloCts;
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		default:
			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_ERR,
				("RlmtStat: Unknown OID should be errored before"));

			pAC->Pnmi.RlmtUpdatedFlag --;
			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}
	}
	*pLen = Offset;

	pAC->Pnmi.RlmtUpdatedFlag --;

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * MacPrivateConf - OID handler function of OIDs concerning the configuration
 *
 * Description:
 *	Get/Presets/Sets the OIDs concerning the configuration.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
PNMI_STATIC int MacPrivateConf(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	unsigned int	PhysPortMax;
	unsigned int	PhysPortIndex;
	unsigned int	LogPortMax;
	unsigned int	LogPortIndex;
	unsigned int	Limit;
	unsigned int	Offset;
	char		Val8;
	char 		*pBufPtr;
	int			Ret;
	SK_EVPARA	EventParam;
	SK_U32		Val32;

	/*
	 * Calculate instance if wished. MAC index 0 is the virtual MAC.
	 */
	PhysPortMax = pAC->GIni.GIMacsFound;
	LogPortMax = SK_PNMI_PORT_PHYS2LOG(PhysPortMax);

	if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) { /* Dual net mode */
		LogPortMax--;
	}

	if ((Instance != (SK_U32)(-1))) { /* Only one specific instance is queried */
		/* Check instance range */
		if ((Instance < 1) || (Instance > LogPortMax)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}
		LogPortIndex = SK_PNMI_PORT_INST2LOG(Instance);
		Limit = LogPortIndex + 1;
	}

	else { /* Instance == (SK_U32)(-1), get all Instances of that OID */

		LogPortIndex = 0;
		Limit = LogPortMax;
	}

	/*
	 * Perform action
	 */
	if (Action == SK_PNMI_GET) {

		/* Check length */
		switch (Id) {

		case OID_SKGE_PMD:
		case OID_SKGE_CONNECTOR:
		case OID_SKGE_LINK_CAP:
		case OID_SKGE_LINK_MODE:
		case OID_SKGE_LINK_MODE_STATUS:
		case OID_SKGE_LINK_STATUS:
		case OID_SKGE_FLOWCTRL_CAP:
		case OID_SKGE_FLOWCTRL_MODE:
		case OID_SKGE_FLOWCTRL_STATUS:
		case OID_SKGE_PHY_OPERATION_CAP:
		case OID_SKGE_PHY_OPERATION_MODE:
		case OID_SKGE_PHY_OPERATION_STATUS:
		case OID_SKGE_SPEED_CAP:
		case OID_SKGE_SPEED_MODE:
		case OID_SKGE_SPEED_STATUS:
#ifdef SK_PHY_LP_MODE
		case OID_SKGE_PHY_LP_MODE:
#endif
			if (*pLen < (Limit - LogPortIndex) * sizeof(SK_U8)) {

				*pLen = (Limit - LogPortIndex) * sizeof(SK_U8);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			break;

        case OID_SKGE_MTU:
        case OID_SKGE_PHY_TYPE:
			if (*pLen < (Limit - LogPortIndex) * sizeof(SK_U32)) {

				*pLen = (Limit - LogPortIndex) * sizeof(SK_U32);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			break;

		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR041,
				SK_PNMI_ERR041MSG);
			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		/*
		 * Update statistic and increment semaphore to indicate
		 * that an update was already done.
		 */
		if ((Ret = SirqUpdate(pAC, IoC)) != SK_PNMI_ERR_OK) {

			*pLen = 0;
			return (Ret);
		}
		pAC->Pnmi.SirqUpdatedFlag ++;

		/*
		 * Get value
		 */
		Offset = 0;
		for (; LogPortIndex < Limit; LogPortIndex ++) {

			pBufPtr = pBuf + Offset;
			
			switch (Id) {

			case OID_SKGE_PMD:
				*pBufPtr = pAC->Pnmi.PMD;
				Offset += sizeof(char);
				break;

			case OID_SKGE_CONNECTOR:
				*pBufPtr = pAC->Pnmi.Connector;
				Offset += sizeof(char);
				break;

			case OID_SKGE_PHY_TYPE:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						continue;
					}
					else {
						/* Get value for physical ports */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);
						Val32 = pAC->GIni.GP[PhysPortIndex].PhyType;
						SK_PNMI_STORE_U32(pBufPtr, Val32);
					}
				}
				else { /* DualNetMode */
					
					Val32 = pAC->GIni.GP[NetIndex].PhyType;
					SK_PNMI_STORE_U32(pBufPtr, Val32);
				}
				Offset += sizeof(SK_U32);
				break;

#ifdef SK_PHY_LP_MODE
			case OID_SKGE_PHY_LP_MODE:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						continue;
					}
					else {
						/* Get value for physical ports */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(pAC, LogPortIndex);
						Val8 = (SK_U8) pAC->GIni.GP[PhysPortIndex].PPhyPowerState;
						*pBufPtr = Val8;
					}
				}
				else { /* DualNetMode */
					
					Val8 = (SK_U8) pAC->GIni.GP[PhysPortIndex].PPhyPowerState;
					*pBufPtr = Val8;
				}
				Offset += sizeof(SK_U8);
				break;
#endif

			case OID_SKGE_LINK_CAP:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						/* Get value for virtual port */
						VirtualConf(pAC, IoC, Id, pBufPtr);
					}
					else {
						/* Get value for physical ports */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);

						*pBufPtr = pAC->GIni.GP[PhysPortIndex].PLinkCap;
					}
				}
				else { /* DualNetMode */
					
					*pBufPtr = pAC->GIni.GP[NetIndex].PLinkCap;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_LINK_MODE:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						/* Get value for virtual port */
						VirtualConf(pAC, IoC, Id, pBufPtr);
					}
					else {
						/* Get value for physical ports */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);

						*pBufPtr = pAC->GIni.GP[PhysPortIndex].PLinkModeConf;
					}
				}
				else { /* DualNetMode */
				
					*pBufPtr = pAC->GIni.GP[NetIndex].PLinkModeConf;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_LINK_MODE_STATUS:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						/* Get value for virtual port */
						VirtualConf(pAC, IoC, Id, pBufPtr);
					}
					else {
						/* Get value for physical port */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);

						*pBufPtr =
							CalculateLinkModeStatus(pAC, IoC, PhysPortIndex);
					}
				}
				else { /* DualNetMode */
					
					*pBufPtr = CalculateLinkModeStatus(pAC, IoC, NetIndex);
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_LINK_STATUS:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						/* Get value for virtual port */
						VirtualConf(pAC, IoC, Id, pBufPtr);
					}
					else {
						/* Get value for physical ports */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);
	
						*pBufPtr = CalculateLinkStatus(pAC, IoC, PhysPortIndex);
					}
				}
				else { /* DualNetMode */

					*pBufPtr = CalculateLinkStatus(pAC, IoC, NetIndex);
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_FLOWCTRL_CAP:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						/* Get value for virtual port */
						VirtualConf(pAC, IoC, Id, pBufPtr);
					}
					else {
						/* Get value for physical ports */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);
	
						*pBufPtr = pAC->GIni.GP[PhysPortIndex].PFlowCtrlCap;
					}
				}
				else { /* DualNetMode */
				
					*pBufPtr = pAC->GIni.GP[NetIndex].PFlowCtrlCap;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_FLOWCTRL_MODE:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						/* Get value for virtual port */
						VirtualConf(pAC, IoC, Id, pBufPtr);
					}
					else {
						/* Get value for physical port */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);
	
						*pBufPtr = pAC->GIni.GP[PhysPortIndex].PFlowCtrlMode;
					}
				}
				else { /* DualNetMode */

					*pBufPtr = pAC->GIni.GP[NetIndex].PFlowCtrlMode;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_FLOWCTRL_STATUS:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						/* Get value for virtual port */
						VirtualConf(pAC, IoC, Id, pBufPtr);
					}
					else {
						/* Get value for physical port */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);
	
						*pBufPtr = pAC->GIni.GP[PhysPortIndex].PFlowCtrlStatus;
					}
				}
				else { /* DualNetMode */

					*pBufPtr = pAC->GIni.GP[NetIndex].PFlowCtrlStatus;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_PHY_OPERATION_CAP:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						/* Get value for virtual port */
						VirtualConf(pAC, IoC, Id, pBufPtr);
					}
					else {
						/* Get value for physical ports */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);
	
						*pBufPtr = pAC->GIni.GP[PhysPortIndex].PMSCap;
					}
				}
				else { /* DualNetMode */
				
					*pBufPtr = pAC->GIni.GP[NetIndex].PMSCap;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_PHY_OPERATION_MODE:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						/* Get value for virtual port */
						VirtualConf(pAC, IoC, Id, pBufPtr);
					}
					else {
						/* Get value for physical port */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);

						*pBufPtr = pAC->GIni.GP[PhysPortIndex].PMSMode;
					}
				}
				else { /* DualNetMode */
				
					*pBufPtr = pAC->GIni.GP[NetIndex].PMSMode;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_PHY_OPERATION_STATUS:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						/* Get value for virtual port */
						VirtualConf(pAC, IoC, Id, pBufPtr);
					}
					else {
						/* Get value for physical port */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);
	
						*pBufPtr = pAC->GIni.GP[PhysPortIndex].PMSStatus;
					}
				}
				else {
				
					*pBufPtr = pAC->GIni.GP[NetIndex].PMSStatus;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_SPEED_CAP:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						/* Get value for virtual port */
						VirtualConf(pAC, IoC, Id, pBufPtr);
					}
					else {
						/* Get value for physical ports */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);
	
						*pBufPtr = pAC->GIni.GP[PhysPortIndex].PLinkSpeedCap;
					}
				}
				else { /* DualNetMode */
				
					*pBufPtr = pAC->GIni.GP[NetIndex].PLinkSpeedCap;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_SPEED_MODE:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						/* Get value for virtual port */
						VirtualConf(pAC, IoC, Id, pBufPtr);
					}
					else {
						/* Get value for physical port */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);
	
						*pBufPtr = pAC->GIni.GP[PhysPortIndex].PLinkSpeed;
					}
				}
				else { /* DualNetMode */

					*pBufPtr = pAC->GIni.GP[NetIndex].PLinkSpeed;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_SPEED_STATUS:
				if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
					if (LogPortIndex == 0) {
						/* Get value for virtual port */
						VirtualConf(pAC, IoC, Id, pBufPtr);
					}
					else {
						/* Get value for physical port */
						PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
							pAC, LogPortIndex);
	
						*pBufPtr = pAC->GIni.GP[PhysPortIndex].PLinkSpeedUsed;
					}
				}
				else { /* DualNetMode */

					*pBufPtr = pAC->GIni.GP[NetIndex].PLinkSpeedUsed;
				}
				Offset += sizeof(char);
				break;
			
			case OID_SKGE_MTU:
				Val32 = SK_DRIVER_GET_MTU(pAC, IoC, NetIndex);
				SK_PNMI_STORE_U32(pBufPtr, Val32);
				Offset += sizeof(SK_U32);
				break;

			default:
				SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_ERR,
					("MacPrivateConf: Unknown OID should be handled before"));

				pAC->Pnmi.SirqUpdatedFlag --;
				return (SK_PNMI_ERR_GENERAL);
			}
		}
		*pLen = Offset;
		pAC->Pnmi.SirqUpdatedFlag --;

		return (SK_PNMI_ERR_OK);
	}

	/*
	 * From here SET or PRESET action. Check if the passed
	 * buffer length is plausible.
	 */
	switch (Id) {

	case OID_SKGE_LINK_MODE:
	case OID_SKGE_FLOWCTRL_MODE:
	case OID_SKGE_PHY_OPERATION_MODE:
	case OID_SKGE_SPEED_MODE:
		if (*pLen < Limit - LogPortIndex) {

			*pLen = Limit - LogPortIndex;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		if (*pLen != Limit - LogPortIndex) {

			*pLen = 0;
			return (SK_PNMI_ERR_BAD_VALUE);
		}
		break;

#ifdef SK_PHY_LP_MODE
	case OID_SKGE_PHY_LP_MODE:
		if (*pLen < Limit - LogPortIndex) {

			*pLen = Limit - LogPortIndex;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;
#endif

	case OID_SKGE_MTU:
		if (*pLen < sizeof(SK_U32)) {

			*pLen = sizeof(SK_U32);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		if (*pLen != sizeof(SK_U32)) {

			*pLen = 0;
			return (SK_PNMI_ERR_BAD_VALUE);
		}
		break;

    default:
		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}

	/*
	 * Perform preset or set
	 */
	Offset = 0;
	for (; LogPortIndex < Limit; LogPortIndex ++) {

		switch (Id) {

		case OID_SKGE_LINK_MODE:
			/* Check the value range */
			Val8 = *(pBuf + Offset);
			if (Val8 == 0) {

				Offset += sizeof(char);
				break;
			}
			if (Val8 < SK_LMODE_HALF ||
				(LogPortIndex != 0 && Val8 > SK_LMODE_AUTOSENSE) ||
				(LogPortIndex == 0 && Val8 > SK_LMODE_INDETERMINATED)) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}

			/* The preset ends here */
			if (Action == SK_PNMI_PRESET) {

				return (SK_PNMI_ERR_OK);
			}

			if (LogPortIndex == 0) {

				/*
				 * The virtual port consists of all currently
				 * active ports. Find them and send an event
				 * with the new link mode to SIRQ.
				 */
				for (PhysPortIndex = 0;
					PhysPortIndex < PhysPortMax;
					PhysPortIndex ++) {

					if (!pAC->Pnmi.Port[PhysPortIndex].
						ActiveFlag) {

						continue;
					}

					EventParam.Para32[0] = PhysPortIndex;
					EventParam.Para32[1] = (SK_U32)Val8;
					if (SkGeSirqEvent(pAC, IoC,
						SK_HWEV_SET_LMODE,
						EventParam) > 0) {

						SK_ERR_LOG(pAC, SK_ERRCL_SW,
							SK_PNMI_ERR043,
							SK_PNMI_ERR043MSG);

						*pLen = 0;
						return (SK_PNMI_ERR_GENERAL);
					}
				}
			}
			else {
				/*
				 * Send an event with the new link mode to
				 * the SIRQ module.
				 */
				EventParam.Para32[0] = SK_PNMI_PORT_LOG2PHYS(
					pAC, LogPortIndex);
				EventParam.Para32[1] = (SK_U32)Val8;
				if (SkGeSirqEvent(pAC, IoC, SK_HWEV_SET_LMODE,
					EventParam) > 0) {

					SK_ERR_LOG(pAC, SK_ERRCL_SW,
						SK_PNMI_ERR043,
						SK_PNMI_ERR043MSG);

					*pLen = 0;
					return (SK_PNMI_ERR_GENERAL);
				}
			}
			Offset += sizeof(char);
			break;

		case OID_SKGE_FLOWCTRL_MODE:
			/* Check the value range */
			Val8 = *(pBuf + Offset);
			if (Val8 == 0) {

				Offset += sizeof(char);
				break;
			}
			if (Val8 < SK_FLOW_MODE_NONE ||
				(LogPortIndex != 0 && Val8 > SK_FLOW_MODE_SYM_OR_REM) ||
				(LogPortIndex == 0 && Val8 > SK_FLOW_MODE_INDETERMINATED)) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}

			/* The preset ends here */
			if (Action == SK_PNMI_PRESET) {

				return (SK_PNMI_ERR_OK);
			}

			if (LogPortIndex == 0) {

				/*
				 * The virtual port consists of all currently
				 * active ports. Find them and send an event
				 * with the new flow control mode to SIRQ.
				 */
				for (PhysPortIndex = 0;
					PhysPortIndex < PhysPortMax;
					PhysPortIndex ++) {

					if (!pAC->Pnmi.Port[PhysPortIndex].
						ActiveFlag) {

						continue;
					}

					EventParam.Para32[0] = PhysPortIndex;
					EventParam.Para32[1] = (SK_U32)Val8;
					if (SkGeSirqEvent(pAC, IoC,
						SK_HWEV_SET_FLOWMODE,
						EventParam) > 0) {

						SK_ERR_LOG(pAC, SK_ERRCL_SW,
							SK_PNMI_ERR044,
							SK_PNMI_ERR044MSG);

						*pLen = 0;
						return (SK_PNMI_ERR_GENERAL);
					}
				}
			}
			else {
				/*
				 * Send an event with the new flow control
				 * mode to the SIRQ module.
				 */
				EventParam.Para32[0] = SK_PNMI_PORT_LOG2PHYS(
					pAC, LogPortIndex);
				EventParam.Para32[1] = (SK_U32)Val8;
				if (SkGeSirqEvent(pAC, IoC,
					SK_HWEV_SET_FLOWMODE, EventParam)
					> 0) {

					SK_ERR_LOG(pAC, SK_ERRCL_SW,
						SK_PNMI_ERR044,
						SK_PNMI_ERR044MSG);

					*pLen = 0;
					return (SK_PNMI_ERR_GENERAL);
				}
			}
			Offset += sizeof(char);
			break;

		case OID_SKGE_PHY_OPERATION_MODE :
			/* Check the value range */
			Val8 = *(pBuf + Offset);
			if (Val8 == 0) {
				/* mode of this port remains unchanged */
				Offset += sizeof(char);
				break;
			}
			if (Val8 < SK_MS_MODE_AUTO ||
				(LogPortIndex != 0 && Val8 > SK_MS_MODE_SLAVE) ||
				(LogPortIndex == 0 && Val8 > SK_MS_MODE_INDETERMINATED)) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}

			/* The preset ends here */
			if (Action == SK_PNMI_PRESET) {

				return (SK_PNMI_ERR_OK);
			}

			if (LogPortIndex == 0) {

				/*
				 * The virtual port consists of all currently
				 * active ports. Find them and send an event
				 * with new master/slave (role) mode to SIRQ.
				 */
				for (PhysPortIndex = 0;
					PhysPortIndex < PhysPortMax;
					PhysPortIndex ++) {

					if (!pAC->Pnmi.Port[PhysPortIndex].
						ActiveFlag) {

						continue;
					}

					EventParam.Para32[0] = PhysPortIndex;
					EventParam.Para32[1] = (SK_U32)Val8;
					if (SkGeSirqEvent(pAC, IoC,
						SK_HWEV_SET_ROLE,
						EventParam) > 0) {

						SK_ERR_LOG(pAC, SK_ERRCL_SW,
							SK_PNMI_ERR042,
							SK_PNMI_ERR042MSG);

						*pLen = 0;
						return (SK_PNMI_ERR_GENERAL);
					}
				}
			}
			else {
				/*
				 * Send an event with the new master/slave
				 * (role) mode to the SIRQ module.
				 */
				EventParam.Para32[0] = SK_PNMI_PORT_LOG2PHYS(
					pAC, LogPortIndex);
				EventParam.Para32[1] = (SK_U32)Val8;
				if (SkGeSirqEvent(pAC, IoC,
					SK_HWEV_SET_ROLE, EventParam) > 0) {

					SK_ERR_LOG(pAC, SK_ERRCL_SW,
						SK_PNMI_ERR042,
						SK_PNMI_ERR042MSG);

					*pLen = 0;
					return (SK_PNMI_ERR_GENERAL);
				}
			}

			Offset += sizeof(char);
			break;

		case OID_SKGE_SPEED_MODE:
			/* Check the value range */
			Val8 = *(pBuf + Offset);
			if (Val8 == 0) {

				Offset += sizeof(char);
				break;
			}
			if (Val8 < (SK_LSPEED_AUTO) ||
				(LogPortIndex != 0 && Val8 > (SK_LSPEED_1000MBPS)) ||
				(LogPortIndex == 0 && Val8 > (SK_LSPEED_INDETERMINATED))) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}

			/* The preset ends here */
			if (Action == SK_PNMI_PRESET) {

				return (SK_PNMI_ERR_OK);
			}

			if (LogPortIndex == 0) {

				/*
				 * The virtual port consists of all currently
				 * active ports. Find them and send an event
				 * with the new flow control mode to SIRQ.
				 */
				for (PhysPortIndex = 0;
					PhysPortIndex < PhysPortMax;
					PhysPortIndex ++) {

					if (!pAC->Pnmi.Port[PhysPortIndex].ActiveFlag) {

						continue;
					}

					EventParam.Para32[0] = PhysPortIndex;
					EventParam.Para32[1] = (SK_U32)Val8;
					if (SkGeSirqEvent(pAC, IoC,
						SK_HWEV_SET_SPEED,
						EventParam) > 0) {

						SK_ERR_LOG(pAC, SK_ERRCL_SW,
							SK_PNMI_ERR045,
							SK_PNMI_ERR045MSG);

						*pLen = 0;
						return (SK_PNMI_ERR_GENERAL);
					}
				}
			}
			else {
				/*
				 * Send an event with the new flow control
				 * mode to the SIRQ module.
				 */
				EventParam.Para32[0] = SK_PNMI_PORT_LOG2PHYS(
					pAC, LogPortIndex);
				EventParam.Para32[1] = (SK_U32)Val8;
				if (SkGeSirqEvent(pAC, IoC,
					SK_HWEV_SET_SPEED,
					EventParam) > 0) {

					SK_ERR_LOG(pAC, SK_ERRCL_SW,
						SK_PNMI_ERR045,
						SK_PNMI_ERR045MSG);

					*pLen = 0;
					return (SK_PNMI_ERR_GENERAL);
				}
			}
			Offset += sizeof(char);
			break;

		case OID_SKGE_MTU :
			/* Check the value range */
			Val32 = *(SK_U32*)(pBuf + Offset);
			if (Val32 == 0) {
				/* mtu of this port remains unchanged */
				Offset += sizeof(SK_U32);
				break;
			}
			if (SK_DRIVER_PRESET_MTU(pAC, IoC, NetIndex, Val32) != 0) {
				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}

			/* The preset ends here */
			if (Action == SK_PNMI_PRESET) {
				return (SK_PNMI_ERR_OK);
			}

			if (SK_DRIVER_SET_MTU(pAC, IoC, NetIndex, Val32) != 0) {
				return (SK_PNMI_ERR_GENERAL);
			}

			Offset += sizeof(SK_U32);
			break;
		
#ifdef SK_PHY_LP_MODE
		case OID_SKGE_PHY_LP_MODE:
			/* The preset ends here */
			if (Action == SK_PNMI_PRESET) {

				return (SK_PNMI_ERR_OK);
			}

			if (!pAC->Pnmi.DualNetActiveFlag) { /* SingleNetMode */
				if (LogPortIndex == 0) {
					Offset = 0;
					continue;
				}
				else {
					/* Set value for physical ports */
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(pAC, LogPortIndex);

					switch (*(pBuf + Offset)) {
						case 0:
							/* If LowPowerMode is active, we can leave it. */
							if (pAC->GIni.GP[PhysPortIndex].PPhyPowerState) {

								Val32 = SkGmLeaveLowPowerMode(pAC, IoC, PhysPortIndex);
								
								if (pAC->GIni.GP[PhysPortIndex].PPhyPowerState < 3)	{
									
									SkDrvInitAdapter(pAC);
								}
								break;
							}
							else {
								*pLen = 0;
								return (SK_PNMI_ERR_GENERAL);
							}
						case 1:
						case 2:
						case 3:
						case 4:
							/* If no LowPowerMode is active, we can enter it. */
							if (!pAC->GIni.GP[PhysPortIndex].PPhyPowerState) {

								if ((*(pBuf + Offset)) < 3)	{
								
									SkDrvDeInitAdapter(pAC);
								}

								Val32 = SkGmEnterLowPowerMode(pAC, IoC, PhysPortIndex, *pBuf);
								break;
							}
							else {
								*pLen = 0;
								return (SK_PNMI_ERR_GENERAL);
							}
						default:
							*pLen = 0;
							return (SK_PNMI_ERR_BAD_VALUE);
					}
				}
			}
			else { /* DualNetMode */
				
				switch (*(pBuf + Offset)) {
					case 0:
						/* If we are in a LowPowerMode, we can leave it. */
						if (pAC->GIni.GP[PhysPortIndex].PPhyPowerState) {

							Val32 = SkGmLeaveLowPowerMode(pAC, IoC, PhysPortIndex);
							
							if (pAC->GIni.GP[PhysPortIndex].PPhyPowerState < 3)	{

								SkDrvInitAdapter(pAC);
							}
							break;
						}
						else {
							*pLen = 0;
							return (SK_PNMI_ERR_GENERAL);
						}
					
					case 1:
					case 2:
					case 3:
					case 4:
						/* If we are not already in LowPowerMode, we can enter it. */
						if (!pAC->GIni.GP[PhysPortIndex].PPhyPowerState) {

							if ((*(pBuf + Offset)) < 3)	{

								SkDrvDeInitAdapter(pAC);
							}
							else {

								Val32 = SkGmEnterLowPowerMode(pAC, IoC, PhysPortIndex, *pBuf);
							}
							break;
						}
						else {
							*pLen = 0;
							return (SK_PNMI_ERR_GENERAL);
						}
					
					default:
						*pLen = 0;
						return (SK_PNMI_ERR_BAD_VALUE);
				}
			}
			Offset += sizeof(SK_U8);
			break;
#endif

		default:
            SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_ERR,
                ("MacPrivateConf: Unknown OID should be handled before set"));

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * Monitor - OID handler function for RLMT_MONITOR_XXX
 *
 * Description:
 *	Because RLMT currently does not support the monitoring of
 *	remote adapter cards, we return always an empty table.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
PNMI_STATIC int Monitor(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	unsigned int	Index;
	unsigned int	Limit;
	unsigned int	Offset;
	unsigned int	Entries;

	
	/*
	 * Calculate instance if wished.
	 */
	/* XXX Not yet implemented. Return always an empty table. */
	Entries = 0;

	if ((Instance != (SK_U32)(-1))) {

		if ((Instance < 1) || (Instance > Entries)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}

		Index = (unsigned int)Instance - 1;
		Limit = (unsigned int)Instance;
	}
	else {
		Index = 0;
		Limit = Entries;
	}

	/*
	 * Get/Set value
	*/
	if (Action == SK_PNMI_GET) {

		for (Offset=0; Index < Limit; Index ++) {

			switch (Id) {

			case OID_SKGE_RLMT_MONITOR_INDEX:
			case OID_SKGE_RLMT_MONITOR_ADDR:
			case OID_SKGE_RLMT_MONITOR_ERRS:
			case OID_SKGE_RLMT_MONITOR_TIMESTAMP:
			case OID_SKGE_RLMT_MONITOR_ADMIN:
				break;

			default:
				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR046,
					SK_PNMI_ERR046MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
		}
		*pLen = Offset;
	}
	else {
		/* Only MONITOR_ADMIN can be set */
		if (Id != OID_SKGE_RLMT_MONITOR_ADMIN) {

			*pLen = 0;
			return (SK_PNMI_ERR_READ_ONLY);
		}

		/* Check if the length is plausible */
		if (*pLen < (Limit - Index)) {

			return (SK_PNMI_ERR_TOO_SHORT);
		}
		/* Okay, we have a wide value range */
		if (*pLen != (Limit - Index)) {

			*pLen = 0;
			return (SK_PNMI_ERR_BAD_VALUE);
		}
/*
		for (Offset=0; Index < Limit; Index ++) {
		}
*/
/*
 * XXX Not yet implemented. Return always BAD_VALUE, because the table
 * is empty.
 */
		*pLen = 0;
		return (SK_PNMI_ERR_BAD_VALUE);
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * VirtualConf - Calculates the values of configuration OIDs for virtual port
 *
 * Description:
 *	We handle here the get of the configuration group OIDs, which are
 *	a little bit complicated. The virtual port consists of all currently
 *	active physical ports. If multiple ports are active and configured
 *	differently we get in some trouble to return a single value. So we
 *	get the value of the first active port and compare it with that of
 *	the other active ports. If they are not the same, we return a value
 *	that indicates that the state is indeterminated.
 *
 * Returns:
 *	Nothing
 */
PNMI_STATIC void VirtualConf(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf)		/* Buffer used for the management data transfer */
{
	unsigned int	PhysPortMax;
	unsigned int	PhysPortIndex;
	SK_U8		Val8;
	SK_U32		Val32;
	SK_BOOL		PortActiveFlag;
	SK_GEPORT	*pPrt;

	*pBuf = 0;
	PortActiveFlag = SK_FALSE;
	PhysPortMax = pAC->GIni.GIMacsFound;
	
	for (PhysPortIndex = 0; PhysPortIndex < PhysPortMax;
		PhysPortIndex ++) {

		pPrt = &pAC->GIni.GP[PhysPortIndex];

		/* Check if the physical port is active */
		if (!pAC->Pnmi.Port[PhysPortIndex].ActiveFlag) {

			continue;
		}

		PortActiveFlag = SK_TRUE;

		switch (Id) {

		case OID_SKGE_PHY_TYPE:
			/* Check if it is the first active port */
			if (*pBuf == 0) {
				Val32 = pPrt->PhyType;
				SK_PNMI_STORE_U32(pBuf, Val32);
				continue;
			}

		case OID_SKGE_LINK_CAP:

			/*
			 * Different capabilities should not happen, but
			 * in the case of the cases OR them all together.
			 * From a curious point of view the virtual port
			 * is capable of all found capabilities.
			 */
			*pBuf |= pPrt->PLinkCap;
			break;

		case OID_SKGE_LINK_MODE:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pPrt->PLinkModeConf;
				continue;
			}

			/*
			 * If we find an active port with a different link
			 * mode than the first one we return a value that
			 * indicates that the link mode is indeterminated.
			 */
			if (*pBuf != pPrt->PLinkModeConf) {

				*pBuf = SK_LMODE_INDETERMINATED;
			}
			break;

		case OID_SKGE_LINK_MODE_STATUS:
			/* Get the link mode of the physical port */
			Val8 = CalculateLinkModeStatus(pAC, IoC, PhysPortIndex);

			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = Val8;
				continue;
			}

			/*
			 * If we find an active port with a different link
			 * mode status than the first one we return a value
			 * that indicates that the link mode status is
			 * indeterminated.
			 */
			if (*pBuf != Val8) {

				*pBuf = SK_LMODE_STAT_INDETERMINATED;
			}
			break;

		case OID_SKGE_LINK_STATUS:
			/* Get the link status of the physical port */
			Val8 = CalculateLinkStatus(pAC, IoC, PhysPortIndex);

			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = Val8;
				continue;
			}

			/*
			 * If we find an active port with a different link
			 * status than the first one, we return a value
			 * that indicates that the link status is
			 * indeterminated.
			 */
			if (*pBuf != Val8) {

				*pBuf = SK_PNMI_RLMT_LSTAT_INDETERMINATED;
			}
			break;

		case OID_SKGE_FLOWCTRL_CAP:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pPrt->PFlowCtrlCap;
				continue;
			}

			/*
			 * From a curious point of view the virtual port
			 * is capable of all found capabilities.
			 */
			*pBuf |= pPrt->PFlowCtrlCap;
			break;

		case OID_SKGE_FLOWCTRL_MODE:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pPrt->PFlowCtrlMode;
				continue;
			}

			/*
			 * If we find an active port with a different flow
			 * control mode than the first one, we return a value
			 * that indicates that the mode is indeterminated.
			 */
			if (*pBuf != pPrt->PFlowCtrlMode) {

				*pBuf = SK_FLOW_MODE_INDETERMINATED;
			}
			break;

		case OID_SKGE_FLOWCTRL_STATUS:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pPrt->PFlowCtrlStatus;
				continue;
			}

			/*
			 * If we find an active port with a different flow
			 * control status than the first one, we return a
			 * value that indicates that the status is
			 * indeterminated.
			 */
			if (*pBuf != pPrt->PFlowCtrlStatus) {

				*pBuf = SK_FLOW_STAT_INDETERMINATED;
			}
			break;
		
		case OID_SKGE_PHY_OPERATION_CAP:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pPrt->PMSCap;
				continue;
			}

			/*
			 * From a curious point of view the virtual port
			 * is capable of all found capabilities.
			 */
			*pBuf |= pPrt->PMSCap;
			break;

		case OID_SKGE_PHY_OPERATION_MODE:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pPrt->PMSMode;
				continue;
			}

			/*
			 * If we find an active port with a different master/
			 * slave mode than the first one, we return a value
			 * that indicates that the mode is indeterminated.
			 */
			if (*pBuf != pPrt->PMSMode) {

				*pBuf = SK_MS_MODE_INDETERMINATED;
			}
			break;

		case OID_SKGE_PHY_OPERATION_STATUS:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pPrt->PMSStatus;
				continue;
			}

			/*
			 * If we find an active port with a different master/
			 * slave status than the first one, we return a
			 * value that indicates that the status is
			 * indeterminated.
			 */
			if (*pBuf != pPrt->PMSStatus) {

				*pBuf = SK_MS_STAT_INDETERMINATED;
			}
			break;
		
		case OID_SKGE_SPEED_MODE:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pPrt->PLinkSpeed;
				continue;
			}

			/*
			 * If we find an active port with a different flow
			 * control mode than the first one, we return a value
			 * that indicates that the mode is indeterminated.
			 */
			if (*pBuf != pPrt->PLinkSpeed) {

				*pBuf = SK_LSPEED_INDETERMINATED;
			}
			break;
		
		case OID_SKGE_SPEED_STATUS:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pPrt->PLinkSpeedUsed;
				continue;
			}

			/*
			 * If we find an active port with a different flow
			 * control status than the first one, we return a
			 * value that indicates that the status is
			 * indeterminated.
			 */
			if (*pBuf != pPrt->PLinkSpeedUsed) {

				*pBuf = SK_LSPEED_STAT_INDETERMINATED;
			}
			break;
		}
	}

	/*
	 * If no port is active return an indeterminated answer
	 */
	if (!PortActiveFlag) {

		switch (Id) {

		case OID_SKGE_LINK_CAP:
			*pBuf = SK_LMODE_CAP_INDETERMINATED;
			break;

		case OID_SKGE_LINK_MODE:
			*pBuf = SK_LMODE_INDETERMINATED;
			break;

		case OID_SKGE_LINK_MODE_STATUS:
			*pBuf = SK_LMODE_STAT_INDETERMINATED;
			break;

		case OID_SKGE_LINK_STATUS:
			*pBuf = SK_PNMI_RLMT_LSTAT_INDETERMINATED;
			break;

		case OID_SKGE_FLOWCTRL_CAP:
		case OID_SKGE_FLOWCTRL_MODE:
			*pBuf = SK_FLOW_MODE_INDETERMINATED;
			break;

		case OID_SKGE_FLOWCTRL_STATUS:
			*pBuf = SK_FLOW_STAT_INDETERMINATED;
			break;
			
		case OID_SKGE_PHY_OPERATION_CAP:
			*pBuf = SK_MS_CAP_INDETERMINATED;
			break;

		case OID_SKGE_PHY_OPERATION_MODE:
			*pBuf = SK_MS_MODE_INDETERMINATED;
			break;

		case OID_SKGE_PHY_OPERATION_STATUS:
			*pBuf = SK_MS_STAT_INDETERMINATED;
			break;
		case OID_SKGE_SPEED_CAP:
			*pBuf = SK_LSPEED_CAP_INDETERMINATED;
			break;

		case OID_SKGE_SPEED_MODE:
			*pBuf = SK_LSPEED_INDETERMINATED;
			break;

		case OID_SKGE_SPEED_STATUS:
			*pBuf = SK_LSPEED_STAT_INDETERMINATED;
			break;
		}
	}
}

/*****************************************************************************
 *
 * CalculateLinkStatus - Determins the link status of a physical port
 *
 * Description:
 *	Determins the link status the following way:
 *	  LSTAT_PHY_DOWN:  Link is down
 *	  LSTAT_AUTONEG:   Auto-negotiation failed
 *	  LSTAT_LOG_DOWN:  Link is up but RLMT did not yet put the port
 *	                   logically up.
 *	  LSTAT_LOG_UP:    RLMT marked the port as up
 *
 * Returns:
 *	Link status of physical port
 */
PNMI_STATIC SK_U8 CalculateLinkStatus(
SK_AC *pAC,			/* Pointer to adapter context */
SK_IOC IoC,			/* IO context handle */
unsigned int PhysPortIndex)	/* Physical port index */
{
	SK_U8	Result;

	if (!pAC->GIni.GP[PhysPortIndex].PHWLinkUp) {

		Result = SK_PNMI_RLMT_LSTAT_PHY_DOWN;
	}
	else if (pAC->GIni.GP[PhysPortIndex].PAutoNegFail > 0) {

		Result = SK_PNMI_RLMT_LSTAT_AUTONEG;
				}
	else if (!pAC->Rlmt.Port[PhysPortIndex].PortDown) {

		Result = SK_PNMI_RLMT_LSTAT_LOG_UP;
	}
	else {
		Result = SK_PNMI_RLMT_LSTAT_LOG_DOWN;
	}

	return (Result);
}

/*****************************************************************************
 *
 * CalculateLinkModeStatus - Determins the link mode status of a phys. port
 *
 * Description:
 *	The COMMON module only tells us if the mode is half or full duplex.
 *	But in the decade of auto sensing it is useful for the user to
 *	know if the mode was negotiated or forced. Therefore we have a
 *	look to the mode, which was last used by the negotiation process.
 *
 * Returns:
 *	The link mode status
 */
PNMI_STATIC SK_U8 CalculateLinkModeStatus(
SK_AC *pAC,			/* Pointer to adapter context */
SK_IOC IoC,			/* IO context handle */
unsigned int PhysPortIndex)	/* Physical port index */
{
	SK_U8	Result;

	/* Get the current mode, which can be full or half duplex */
	Result = pAC->GIni.GP[PhysPortIndex].PLinkModeStatus;

	/* Check if no valid mode could be found (link is down) */
	if (Result < SK_LMODE_STAT_HALF) {

		Result = SK_LMODE_STAT_UNKNOWN;
	}
	else if (pAC->GIni.GP[PhysPortIndex].PLinkMode >= SK_LMODE_AUTOHALF) {

		/*
		 * Auto-negotiation was used to bring up the link. Change
		 * the already found duplex status that it indicates
		 * auto-negotiation was involved.
		 */
		if (Result == SK_LMODE_STAT_HALF) {

			Result = SK_LMODE_STAT_AUTOHALF;
		}
		else if (Result == SK_LMODE_STAT_FULL) {

			Result = SK_LMODE_STAT_AUTOFULL;
		}
	}

	return (Result);
}

/*****************************************************************************
 *
 * GetVpdKeyArr - Obtain an array of VPD keys
 *
 * Description:
 *	Read the VPD keys and build an array of VPD keys, which are
 *	easy to access.
 *
 * Returns:
 *	SK_PNMI_ERR_OK	     Task successfully performed.
 *	SK_PNMI_ERR_GENERAL  Something went wrong.
 */
PNMI_STATIC int GetVpdKeyArr(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
char *pKeyArr,		/* Ptr KeyArray */
unsigned int KeyArrLen,	/* Length of array in bytes */
unsigned int *pKeyNo)	/* Number of keys */
{
	unsigned int		BufKeysLen = SK_PNMI_VPD_BUFSIZE;
	char			BufKeys[SK_PNMI_VPD_BUFSIZE];
	unsigned int		StartOffset;
	unsigned int		Offset;
	int			Index;
	int			Ret;


	SK_MEMSET(pKeyArr, 0, KeyArrLen);

	/*
	 * Get VPD key list
	 */
	Ret = VpdKeys(pAC, IoC, (char *)&BufKeys, (int *)&BufKeysLen,
		(int *)pKeyNo);
	if (Ret > 0) {

		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR014,
			SK_PNMI_ERR014MSG);

		return (SK_PNMI_ERR_GENERAL);
	}
	/* If no keys are available return now */
	if (*pKeyNo == 0 || BufKeysLen == 0) {

		return (SK_PNMI_ERR_OK);
	}
	/*
	 * If the key list is too long for us trunc it and give a
	 * errorlog notification. This case should not happen because
	 * the maximum number of keys is limited due to RAM limitations
	 */
	if (*pKeyNo > SK_PNMI_VPD_ENTRIES) {

		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR015,
			SK_PNMI_ERR015MSG);

		*pKeyNo = SK_PNMI_VPD_ENTRIES;
	}

	/*
	 * Now build an array of fixed string length size and copy
	 * the keys together.
	 */
	for (Index = 0, StartOffset = 0, Offset = 0; Offset < BufKeysLen;
		Offset ++) {

		if (BufKeys[Offset] != 0) {

			continue;
		}

		if (Offset - StartOffset > SK_PNMI_VPD_KEY_SIZE) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR016,
				SK_PNMI_ERR016MSG);
			return (SK_PNMI_ERR_GENERAL);
		}

		SK_STRNCPY(pKeyArr + Index * SK_PNMI_VPD_KEY_SIZE,
			&BufKeys[StartOffset], SK_PNMI_VPD_KEY_SIZE);

		Index ++;
		StartOffset = Offset + 1;
	}

	/* Last key not zero terminated? Get it anyway */
	if (StartOffset < Offset) {

		SK_STRNCPY(pKeyArr + Index * SK_PNMI_VPD_KEY_SIZE,
			&BufKeys[StartOffset], SK_PNMI_VPD_KEY_SIZE);
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * SirqUpdate - Let the SIRQ update its internal values
 *
 * Description:
 *	Just to be sure that the SIRQ module holds its internal data
 *	structures up to date, we send an update event before we make
 *	any access.
 *
 * Returns:
 *	SK_PNMI_ERR_OK	     Task successfully performed.
 *	SK_PNMI_ERR_GENERAL  Something went wrong.
 */
PNMI_STATIC int SirqUpdate(
SK_AC *pAC,	/* Pointer to adapter context */
SK_IOC IoC)	/* IO context handle */
{
	SK_EVPARA	EventParam;


	/* Was the module already updated during the current PNMI call? */
	if (pAC->Pnmi.SirqUpdatedFlag > 0) {

		return (SK_PNMI_ERR_OK);
	}

	/* Send an synchronuous update event to the module */
	SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));
	if (SkGeSirqEvent(pAC, IoC, SK_HWEV_UPDATE_STAT, EventParam) > 0) {

		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR047,
			SK_PNMI_ERR047MSG);

		return (SK_PNMI_ERR_GENERAL);
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * RlmtUpdate - Let the RLMT update its internal values
 *
 * Description:
 *	Just to be sure that the RLMT module holds its internal data
 *	structures up to date, we send an update event before we make
 *	any access.
 *
 * Returns:
 *	SK_PNMI_ERR_OK	     Task successfully performed.
 *	SK_PNMI_ERR_GENERAL  Something went wrong.
 */
PNMI_STATIC int RlmtUpdate(
SK_AC *pAC,	/* Pointer to adapter context */
SK_IOC IoC,	/* IO context handle */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode allways zero */
{
	SK_EVPARA	EventParam;


	/* Was the module already updated during the current PNMI call? */
	if (pAC->Pnmi.RlmtUpdatedFlag > 0) {

		return (SK_PNMI_ERR_OK);
	}

	/* Send an synchronuous update event to the module */
	SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));
	EventParam.Para32[0] = NetIndex;
	EventParam.Para32[1] = (SK_U32)-1;
	if (SkRlmtEvent(pAC, IoC, SK_RLMT_STATS_UPDATE, EventParam) > 0) {

		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR048,
			SK_PNMI_ERR048MSG);

		return (SK_PNMI_ERR_GENERAL);
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * MacUpdate - Force the XMAC to output the current statistic
 *
 * Description:
 *	The XMAC holds its statistic internally. To obtain the current
 *	values we must send a command so that the statistic data will
 *	be written to a predefined memory area on the adapter.
 *
 * Returns:
 *	SK_PNMI_ERR_OK	     Task successfully performed.
 *	SK_PNMI_ERR_GENERAL  Something went wrong.
 */
PNMI_STATIC int MacUpdate(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
unsigned int FirstMac,	/* Index of the first Mac to be updated */
unsigned int LastMac)	/* Index of the last Mac to be updated */
{
	unsigned int	MacIndex;

	/*
	 * Were the statistics already updated during the
	 * current PNMI call?
	 */
	if (pAC->Pnmi.MacUpdatedFlag > 0) {

		return (SK_PNMI_ERR_OK);
	}

	/* Send an update command to all MACs specified */
	for (MacIndex = FirstMac; MacIndex <= LastMac; MacIndex ++) {

		/*
		 * 2002-09-13 pweber:	Freeze the current SW counters.
		 *                      (That should be done as close as
		 *                      possible to the update of the
		 *                      HW counters)
		 */
		if (pAC->GIni.GIMacType == SK_MAC_XMAC) {
			pAC->Pnmi.BufPort[MacIndex] = pAC->Pnmi.Port[MacIndex];
		}
			
		/* 2002-09-13 pweber:  Update the HW counter  */
		if (pAC->GIni.GIFunc.pFnMacUpdateStats(pAC, IoC, MacIndex) != 0) {

			return (SK_PNMI_ERR_GENERAL);
		}
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * GetStatVal - Retrieve an XMAC statistic counter
 *
 * Description:
 *	Retrieves the statistic counter of a virtual or physical port. The
 *	virtual port is identified by the index 0. It consists of all
 *	currently active ports. To obtain the counter value for this port
 *	we must add the statistic counter of all active ports. To grant
 *	continuous counter values for the virtual port even when port
 *	switches occur we must additionally add a delta value, which was
 *	calculated during a SK_PNMI_EVT_RLMT_ACTIVE_UP event.
 *
 * Returns:
 *	Requested statistic value
 */
PNMI_STATIC SK_U64 GetStatVal(
SK_AC *pAC,					/* Pointer to adapter context */
SK_IOC IoC,					/* IO context handle */
unsigned int LogPortIndex,	/* Index of the logical Port to be processed */
unsigned int StatIndex,		/* Index to statistic value */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode allways zero */
{
	unsigned int	PhysPortIndex;
	unsigned int	PhysPortMax;
	SK_U64			Val = 0;


	if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {	/* Dual net mode */

		PhysPortIndex = NetIndex;
		
		Val = GetPhysStatVal(pAC, IoC, PhysPortIndex, StatIndex);
	}
	else {	/* Single Net mode */

		if (LogPortIndex == 0) {

			PhysPortMax = pAC->GIni.GIMacsFound;

			/* Add counter of all active ports */
			for (PhysPortIndex = 0; PhysPortIndex < PhysPortMax;
				PhysPortIndex ++) {

				if (pAC->Pnmi.Port[PhysPortIndex].ActiveFlag) {

					Val += GetPhysStatVal(pAC, IoC, PhysPortIndex, StatIndex);
				}
			}

			/* Correct value because of port switches */
			Val += pAC->Pnmi.VirtualCounterOffset[StatIndex];
		}
		else {
			/* Get counter value of physical port */
			PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(pAC, LogPortIndex);
			
			Val = GetPhysStatVal(pAC, IoC, PhysPortIndex, StatIndex);
		}
	}
	return (Val);
}

/*****************************************************************************
 *
 * GetPhysStatVal - Get counter value for physical port
 *
 * Description:
 *	Builds a 64bit counter value. Except for the octet counters
 *	the lower 32bit are counted in hardware and the upper 32bit
 *	in software by monitoring counter overflow interrupts in the
 *	event handler. To grant continous counter values during XMAC
 *	resets (caused by a workaround) we must add a delta value.
 *	The delta was calculated in the event handler when a
 *	SK_PNMI_EVT_XMAC_RESET was received.
 *
 * Returns:
 *	Counter value
 */
PNMI_STATIC SK_U64 GetPhysStatVal(
SK_AC *pAC,					/* Pointer to adapter context */
SK_IOC IoC,					/* IO context handle */
unsigned int PhysPortIndex,	/* Index of the logical Port to be processed */
unsigned int StatIndex)		/* Index to statistic value */
{
	SK_U64	Val = 0;
	SK_U32	LowVal = 0;
	SK_U32	HighVal = 0;
	SK_U16	Word;
	int		MacType;
	unsigned int HelpIndex;
	SK_GEPORT	*pPrt;
	
	SK_PNMI_PORT	*pPnmiPrt;
	SK_GEMACFUNC	*pFnMac;
	
	pPrt = &pAC->GIni.GP[PhysPortIndex];
	
	MacType = pAC->GIni.GIMacType;
	
	/* 2002-09-17 pweber: For XMAC, use the frozen SW counters (BufPort) */
	if (MacType == SK_MAC_XMAC) {
		pPnmiPrt = &pAC->Pnmi.BufPort[PhysPortIndex];
	}
	else {
		pPnmiPrt = &pAC->Pnmi.Port[PhysPortIndex];
	}
	
	pFnMac   = &pAC->GIni.GIFunc;

	switch (StatIndex) {
	case SK_PNMI_HTX:
		if (MacType == SK_MAC_GMAC) {
			(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
							StatAddr[SK_PNMI_HTX_BROADCAST][MacType].Reg,
							&LowVal);
			(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
							StatAddr[SK_PNMI_HTX_MULTICAST][MacType].Reg,
							&HighVal);
			LowVal += HighVal;
			(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
							StatAddr[SK_PNMI_HTX_UNICAST][MacType].Reg,
							&HighVal);
			LowVal += HighVal;
		}
		else {
			(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
										  StatAddr[StatIndex][MacType].Reg,
										  &LowVal);
		}
		HighVal = pPnmiPrt->CounterHigh[StatIndex];
		break;
	
	case SK_PNMI_HRX:
		if (MacType == SK_MAC_GMAC) {
			(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
							StatAddr[SK_PNMI_HRX_BROADCAST][MacType].Reg,
							&LowVal);
			(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
							StatAddr[SK_PNMI_HRX_MULTICAST][MacType].Reg,
							&HighVal);
			LowVal += HighVal;
			(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
							StatAddr[SK_PNMI_HRX_UNICAST][MacType].Reg,
							&HighVal);
			LowVal += HighVal;
		}
		else {
			(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
										  StatAddr[StatIndex][MacType].Reg,
										  &LowVal);
		}
		HighVal = pPnmiPrt->CounterHigh[StatIndex];
		break;

	case SK_PNMI_HTX_OCTET:
	case SK_PNMI_HRX_OCTET:
		(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
									  StatAddr[StatIndex][MacType].Reg,
									  &HighVal);
		(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
									  StatAddr[StatIndex + 1][MacType].Reg,
									  &LowVal);
		break;

	case SK_PNMI_HTX_BURST:
	case SK_PNMI_HTX_EXCESS_DEF:
	case SK_PNMI_HTX_CARRIER:
		/* Not supported by GMAC */
		if (MacType == SK_MAC_GMAC) {
			return (Val);
		}

		(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
									  StatAddr[StatIndex][MacType].Reg,
									  &LowVal);
		HighVal = pPnmiPrt->CounterHigh[StatIndex];
		break;

	case SK_PNMI_HTX_MACC:
		/* GMAC only supports PAUSE MAC control frames */
		if (MacType == SK_MAC_GMAC) {
			HelpIndex = SK_PNMI_HTX_PMACC;
		}
		else {
			HelpIndex = StatIndex;
		}
		
		(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
								StatAddr[HelpIndex][MacType].Reg,
								&LowVal);

		HighVal = pPnmiPrt->CounterHigh[StatIndex];
		break;

	case SK_PNMI_HTX_COL:
	case SK_PNMI_HRX_UNDERSIZE:
		/* Not supported by XMAC */
		if (MacType == SK_MAC_XMAC) {
			return (Val);
		}

		(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
									  StatAddr[StatIndex][MacType].Reg,
									  &LowVal);
		HighVal = pPnmiPrt->CounterHigh[StatIndex];
		break;

	case SK_PNMI_HTX_DEFFERAL:
		/* Not supported by GMAC */
		if (MacType == SK_MAC_GMAC) {
			return (Val);
		}
		
		/*
		 * XMAC counts frames with deferred transmission
		 * even in full-duplex mode.
		 *
		 * In full-duplex mode the counter remains constant!
		 */
		if ((pPrt->PLinkModeStatus == SK_LMODE_STAT_AUTOFULL) ||
			(pPrt->PLinkModeStatus == SK_LMODE_STAT_FULL)) {

			LowVal = 0;
			HighVal = 0;
		}
		else {
			/* Otherwise get contents of hardware register */
			(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
										  StatAddr[StatIndex][MacType].Reg,
										  &LowVal);
			HighVal = pPnmiPrt->CounterHigh[StatIndex];
		}
		break;

	case SK_PNMI_HRX_BADOCTET:
		/* Not supported by XMAC */
		if (MacType == SK_MAC_XMAC) {
			return (Val);
		}

		(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
									  StatAddr[StatIndex][MacType].Reg,
									  &HighVal);
		(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
									  StatAddr[StatIndex + 1][MacType].Reg,
                                      &LowVal);
		break;

	case SK_PNMI_HTX_OCTETLOW:
	case SK_PNMI_HRX_OCTETLOW:
	case SK_PNMI_HRX_BADOCTETLOW:
		return (Val);

	case SK_PNMI_HRX_LONGFRAMES:
		/* For XMAC the SW counter is managed by PNMI */
		if (MacType == SK_MAC_XMAC) {
			return (pPnmiPrt->StatRxLongFrameCts);
		}
		
		(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
									  StatAddr[StatIndex][MacType].Reg,
									  &LowVal);
		HighVal = pPnmiPrt->CounterHigh[StatIndex];
		break;
		
	case SK_PNMI_HRX_TOO_LONG:
		(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
								StatAddr[StatIndex][MacType].Reg,
								&LowVal);
		HighVal = pPnmiPrt->CounterHigh[StatIndex];
		
		Val = (((SK_U64)HighVal << 32) | (SK_U64)LowVal);

		if (MacType == SK_MAC_GMAC) {
			/* For GMAC the SW counter is additionally managed by PNMI */
			Val += pPnmiPrt->StatRxFrameTooLongCts;
		}
		else {
			/*
			 * Frames longer than IEEE 802.3 frame max size are counted
			 * by XMAC in frame_too_long counter even reception of long
			 * frames was enabled and the frame was correct.
			 * So correct the value by subtracting RxLongFrame counter.
			 */
			Val -= pPnmiPrt->StatRxLongFrameCts;
		}

		LowVal = (SK_U32)Val;
		HighVal = (SK_U32)(Val >> 32);
		break;
		
	case SK_PNMI_HRX_SHORTS:
		/* Not supported by GMAC */
		if (MacType == SK_MAC_GMAC) {
			/* GM_RXE_FRAG?? */
			return (Val);
		}
		
		/*
		 * XMAC counts short frame errors even if link down (#10620)
		 *
		 * If link-down the counter remains constant
		 */
		if (pPrt->PLinkModeStatus != SK_LMODE_STAT_UNKNOWN) {

			/* Otherwise get incremental difference */
			(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
										  StatAddr[StatIndex][MacType].Reg,
										  &LowVal);
			HighVal = pPnmiPrt->CounterHigh[StatIndex];

			Val = (((SK_U64)HighVal << 32) | (SK_U64)LowVal);
			Val -= pPnmiPrt->RxShortZeroMark;

			LowVal = (SK_U32)Val;
			HighVal = (SK_U32)(Val >> 32);
		}
		break;

	case SK_PNMI_HRX_MACC:
	case SK_PNMI_HRX_MACC_UNKWN:
	case SK_PNMI_HRX_BURST:
	case SK_PNMI_HRX_MISSED:
	case SK_PNMI_HRX_FRAMING:
	case SK_PNMI_HRX_CARRIER:
	case SK_PNMI_HRX_IRLENGTH:
	case SK_PNMI_HRX_SYMBOL:
	case SK_PNMI_HRX_CEXT:
		/* Not supported by GMAC */
		if (MacType == SK_MAC_GMAC) {
			return (Val);
		}

		(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
									  StatAddr[StatIndex][MacType].Reg,
									  &LowVal);
		HighVal = pPnmiPrt->CounterHigh[StatIndex];
		break;

	case SK_PNMI_HRX_PMACC_ERR:
		/* For GMAC the SW counter is managed by PNMI */
		if (MacType == SK_MAC_GMAC) {
			return (pPnmiPrt->StatRxPMaccErr);
		}
		
		(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
									  StatAddr[StatIndex][MacType].Reg,
									  &LowVal);
		HighVal = pPnmiPrt->CounterHigh[StatIndex];
		break;

	/* SW counter managed by PNMI */
	case SK_PNMI_HTX_SYNC:
		LowVal = (SK_U32)pPnmiPrt->StatSyncCts;
		HighVal = (SK_U32)(pPnmiPrt->StatSyncCts >> 32);
		break;

	/* SW counter managed by PNMI */
	case SK_PNMI_HTX_SYNC_OCTET:
		LowVal = (SK_U32)pPnmiPrt->StatSyncOctetsCts;
		HighVal = (SK_U32)(pPnmiPrt->StatSyncOctetsCts >> 32);
		break;

	case SK_PNMI_HRX_FCS:
		/*
		 * Broadcom filters FCS errors and counts it in
		 * Receive Error Counter register
		 */
		if (pPrt->PhyType == SK_PHY_BCOM) {
			/* do not read while not initialized (PHY_READ hangs!)*/
			if (pPrt->PState != SK_PRT_RESET) {
				SkXmPhyRead(pAC, IoC, PhysPortIndex, PHY_BCOM_RE_CTR, &Word);
				
				LowVal = Word;
			}
			HighVal = pPnmiPrt->CounterHigh[StatIndex];
		}
		else {
			(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
										  StatAddr[StatIndex][MacType].Reg,
										  &LowVal);
			HighVal = pPnmiPrt->CounterHigh[StatIndex];
		}
		break;

	default:
		(void)pFnMac->pFnMacStatistic(pAC, IoC, PhysPortIndex,
									  StatAddr[StatIndex][MacType].Reg,
									  &LowVal);
		HighVal = pPnmiPrt->CounterHigh[StatIndex];
		break;
	}

	Val = (((SK_U64)HighVal << 32) | (SK_U64)LowVal);

	/* Correct value because of possible XMAC reset. XMAC Errata #2 */
	Val += pPnmiPrt->CounterOffset[StatIndex];

	return (Val);
}

/*****************************************************************************
 *
 * ResetCounter - Set all counters and timestamps to zero
 *
 * Description:
 *	Notifies other common modules which store statistic data to
 *	reset their counters and finally reset our own counters.
 *
 * Returns:
 *	Nothing
 */
PNMI_STATIC void ResetCounter(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
SK_U32 NetIndex)
{
	unsigned int	PhysPortIndex;
	SK_EVPARA	EventParam;


	SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));

	/* Notify sensor module */
	SkEventQueue(pAC, SKGE_I2C, SK_I2CEV_CLEAR, EventParam);

	/* Notify RLMT module */
	EventParam.Para32[0] = NetIndex;
	EventParam.Para32[1] = (SK_U32)-1;
	SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_STATS_CLEAR, EventParam);
	EventParam.Para32[1] = 0;

	/* Notify SIRQ module */
	SkEventQueue(pAC, SKGE_HWAC, SK_HWEV_CLEAR_STAT, EventParam);

	/* Notify CSUM module */
#ifdef SK_USE_CSUM
	EventParam.Para32[0] = NetIndex;
	EventParam.Para32[1] = (SK_U32)-1;
	SkEventQueue(pAC, SKGE_CSUM, SK_CSUM_EVENT_CLEAR_PROTO_STATS,
		EventParam);
#endif /* SK_USE_CSUM */
	
	/* Clear XMAC statistic */
	for (PhysPortIndex = 0; PhysPortIndex <
		(unsigned int)pAC->GIni.GIMacsFound; PhysPortIndex ++) {

		(void)pAC->GIni.GIFunc.pFnMacResetCounter(pAC, IoC, PhysPortIndex);

		SK_MEMSET((char *)&pAC->Pnmi.Port[PhysPortIndex].CounterHigh,
			0, sizeof(pAC->Pnmi.Port[PhysPortIndex].CounterHigh));
		SK_MEMSET((char *)&pAC->Pnmi.Port[PhysPortIndex].
			CounterOffset, 0, sizeof(pAC->Pnmi.Port[
			PhysPortIndex].CounterOffset));
		SK_MEMSET((char *)&pAC->Pnmi.Port[PhysPortIndex].StatSyncCts,
			0, sizeof(pAC->Pnmi.Port[PhysPortIndex].StatSyncCts));
		SK_MEMSET((char *)&pAC->Pnmi.Port[PhysPortIndex].
			StatSyncOctetsCts, 0, sizeof(pAC->Pnmi.Port[
			PhysPortIndex].StatSyncOctetsCts));
		SK_MEMSET((char *)&pAC->Pnmi.Port[PhysPortIndex].
			StatRxLongFrameCts, 0, sizeof(pAC->Pnmi.Port[
			PhysPortIndex].StatRxLongFrameCts));
		SK_MEMSET((char *)&pAC->Pnmi.Port[PhysPortIndex].
				  StatRxFrameTooLongCts, 0, sizeof(pAC->Pnmi.Port[
			PhysPortIndex].StatRxFrameTooLongCts));
		SK_MEMSET((char *)&pAC->Pnmi.Port[PhysPortIndex].
				  StatRxPMaccErr, 0, sizeof(pAC->Pnmi.Port[
			PhysPortIndex].StatRxPMaccErr));
	}

	/*
	 * Clear local statistics
	 */
	SK_MEMSET((char *)&pAC->Pnmi.VirtualCounterOffset, 0,
		  sizeof(pAC->Pnmi.VirtualCounterOffset));
	pAC->Pnmi.RlmtChangeCts = 0;
	pAC->Pnmi.RlmtChangeTime = 0;
	SK_MEMSET((char *)&pAC->Pnmi.RlmtChangeEstimate.EstValue[0], 0,
		sizeof(pAC->Pnmi.RlmtChangeEstimate.EstValue));
	pAC->Pnmi.RlmtChangeEstimate.EstValueIndex = 0;
	pAC->Pnmi.RlmtChangeEstimate.Estimate = 0;
	pAC->Pnmi.Port[NetIndex].TxSwQueueMax = 0;
	pAC->Pnmi.Port[NetIndex].TxRetryCts = 0;
	pAC->Pnmi.Port[NetIndex].RxIntrCts = 0;
	pAC->Pnmi.Port[NetIndex].TxIntrCts = 0;
	pAC->Pnmi.Port[NetIndex].RxNoBufCts = 0;
	pAC->Pnmi.Port[NetIndex].TxNoBufCts = 0;
	pAC->Pnmi.Port[NetIndex].TxUsedDescrNo = 0;
	pAC->Pnmi.Port[NetIndex].RxDeliveredCts = 0;
	pAC->Pnmi.Port[NetIndex].RxOctetsDeliveredCts = 0;
	pAC->Pnmi.Port[NetIndex].ErrRecoveryCts = 0;
}

/*****************************************************************************
 *
 * GetTrapEntry - Get an entry in the trap buffer
 *
 * Description:
 *	The trap buffer stores various events. A user application somehow
 *	gets notified that an event occured and retrieves the trap buffer
 *	contens (or simply polls the buffer). The buffer is organized as
 *	a ring which stores the newest traps at the beginning. The oldest
 *	traps are overwritten by the newest ones. Each trap entry has a
 *	unique number, so that applications may detect new trap entries.
 *
 * Returns:
 *	A pointer to the trap entry
 */
PNMI_STATIC char* GetTrapEntry(
SK_AC *pAC,		/* Pointer to adapter context */
SK_U32 TrapId,		/* SNMP ID of the trap */
unsigned int Size)	/* Space needed for trap entry */
{
	unsigned int		BufPad = pAC->Pnmi.TrapBufPad;
	unsigned int		BufFree = pAC->Pnmi.TrapBufFree;
	unsigned int		Beg = pAC->Pnmi.TrapQueueBeg;
	unsigned int		End = pAC->Pnmi.TrapQueueEnd;
	char			*pBuf = &pAC->Pnmi.TrapBuf[0];
	int			Wrap;
	unsigned int		NeededSpace;
	unsigned int		EntrySize;
	SK_U32			Val32;
	SK_U64			Val64;


	/* Last byte of entry will get a copy of the entry length */
	Size ++;

	/*
	 * Calculate needed buffer space */
	if (Beg >= Size) {

		NeededSpace = Size;
		Wrap = SK_FALSE;
	}
	else {
		NeededSpace = Beg + Size;
		Wrap = SK_TRUE;
	}

	/*
	 * Check if enough buffer space is provided. Otherwise
	 * free some entries. Leave one byte space between begin
	 * and end of buffer to make it possible to detect whether
	 * the buffer is full or empty
	 */
	while (BufFree < NeededSpace + 1) {

		if (End == 0) {

			End = SK_PNMI_TRAP_QUEUE_LEN;
		}

		EntrySize = (unsigned int)*((unsigned char *)pBuf + End - 1);
		BufFree += EntrySize;
		End -= EntrySize;
#ifdef DEBUG
		SK_MEMSET(pBuf + End, (char)(-1), EntrySize);
#endif /* DEBUG */
		if (End == BufPad) {
#ifdef DEBUG
			SK_MEMSET(pBuf, (char)(-1), End);
#endif /* DEBUG */
			BufFree += End;
			End = 0;
			BufPad = 0;
		}
	}

	/*
	 * Insert new entry as first entry. Newest entries are
	 * stored at the beginning of the queue.
	 */
	if (Wrap) {

		BufPad = Beg;
		Beg = SK_PNMI_TRAP_QUEUE_LEN - Size;
	}
	else {
		Beg = Beg - Size;
	}
	BufFree -= NeededSpace;

	/* Save the current offsets */
	pAC->Pnmi.TrapQueueBeg = Beg;
	pAC->Pnmi.TrapQueueEnd = End;
	pAC->Pnmi.TrapBufPad = BufPad;
	pAC->Pnmi.TrapBufFree = BufFree;

	/* Initialize the trap entry */
	*(pBuf + Beg + Size - 1) = (char)Size;
	*(pBuf + Beg) = (char)Size;
	Val32 = (pAC->Pnmi.TrapUnique) ++;
	SK_PNMI_STORE_U32(pBuf + Beg + 1, Val32);
	SK_PNMI_STORE_U32(pBuf + Beg + 1 + sizeof(SK_U32), TrapId);
	Val64 = SK_PNMI_HUNDREDS_SEC(SkOsGetTime(pAC));
	SK_PNMI_STORE_U64(pBuf + Beg + 1 + 2 * sizeof(SK_U32), Val64);

	return (pBuf + Beg);
}

/*****************************************************************************
 *
 * CopyTrapQueue - Copies the trap buffer for the TRAP OID
 *
 * Description:
 *	On a query of the TRAP OID the trap buffer contents will be
 *	copied continuously to the request buffer, which must be large
 *	enough. No length check is performed.
 *
 * Returns:
 *	Nothing
 */
PNMI_STATIC void CopyTrapQueue(
SK_AC *pAC,		/* Pointer to adapter context */
char *pDstBuf)		/* Buffer to which the queued traps will be copied */
{
	unsigned int	BufPad = pAC->Pnmi.TrapBufPad;
	unsigned int	Trap = pAC->Pnmi.TrapQueueBeg;
	unsigned int	End = pAC->Pnmi.TrapQueueEnd;
	char		*pBuf = &pAC->Pnmi.TrapBuf[0];
	unsigned int	Len;
	unsigned int	DstOff = 0;


	while (Trap != End) {

		Len = (unsigned int)*(pBuf + Trap);

		/*
		 * Last byte containing a copy of the length will
		 * not be copied.
		 */
		*(pDstBuf + DstOff) = (char)(Len - 1);
		SK_MEMCPY(pDstBuf + DstOff + 1, pBuf + Trap + 1, Len - 2);
		DstOff += Len - 1;

		Trap += Len;
		if (Trap == SK_PNMI_TRAP_QUEUE_LEN) {

			Trap = BufPad;
		}
	}
}

/*****************************************************************************
 *
 * GetTrapQueueLen - Get the length of the trap buffer
 *
 * Description:
 *	Evaluates the number of currently stored traps and the needed
 *	buffer size to retrieve them.
 *
 * Returns:
 *	Nothing
 */
PNMI_STATIC void GetTrapQueueLen(
SK_AC *pAC,		/* Pointer to adapter context */
unsigned int *pLen,	/* Length in Bytes of all queued traps */
unsigned int *pEntries)	/* Returns number of trapes stored in queue */
{
	unsigned int	BufPad = pAC->Pnmi.TrapBufPad;
	unsigned int	Trap = pAC->Pnmi.TrapQueueBeg;
	unsigned int	End = pAC->Pnmi.TrapQueueEnd;
	char		*pBuf = &pAC->Pnmi.TrapBuf[0];
	unsigned int	Len;
	unsigned int	Entries = 0;
	unsigned int	TotalLen = 0;


	while (Trap != End) {

		Len = (unsigned int)*(pBuf + Trap);
		TotalLen += Len - 1;
		Entries ++;

		Trap += Len;
		if (Trap == SK_PNMI_TRAP_QUEUE_LEN) {

			Trap = BufPad;
		}
	}

	*pEntries = Entries;
	*pLen = TotalLen;
}

/*****************************************************************************
 *
 * QueueSimpleTrap - Store a simple trap to the trap buffer
 *
 * Description:
 *	A simple trap is a trap with now additional data. It consists
 *	simply of a trap code.
 *
 * Returns:
 *	Nothing
 */
PNMI_STATIC void QueueSimpleTrap(
SK_AC *pAC,		/* Pointer to adapter context */
SK_U32 TrapId)		/* Type of sensor trap */
{
	GetTrapEntry(pAC, TrapId, SK_PNMI_TRAP_SIMPLE_LEN);
}

/*****************************************************************************
 *
 * QueueSensorTrap - Stores a sensor trap in the trap buffer
 *
 * Description:
 *	Gets an entry in the trap buffer and fills it with sensor related
 *	data.
 *
 * Returns:
 *	Nothing
 */
PNMI_STATIC void QueueSensorTrap(
SK_AC *pAC,			/* Pointer to adapter context */
SK_U32 TrapId,			/* Type of sensor trap */
unsigned int SensorIndex)	/* Index of sensor which caused the trap */
{
	char		*pBuf;
	unsigned int	Offset;
	unsigned int	DescrLen;
	SK_U32		Val32;


	/* Get trap buffer entry */
	DescrLen = SK_STRLEN(pAC->I2c.SenTable[SensorIndex].SenDesc);
	pBuf = GetTrapEntry(pAC, TrapId,
		SK_PNMI_TRAP_SENSOR_LEN_BASE + DescrLen);
	Offset = SK_PNMI_TRAP_SIMPLE_LEN;

	/* Store additionally sensor trap related data */
	Val32 = OID_SKGE_SENSOR_INDEX;
	SK_PNMI_STORE_U32(pBuf + Offset, Val32);
	*(pBuf + Offset + 4) = 4;
	Val32 = (SK_U32)SensorIndex;
	SK_PNMI_STORE_U32(pBuf + Offset + 5, Val32);
	Offset += 9;
	
	Val32 = (SK_U32)OID_SKGE_SENSOR_DESCR;
	SK_PNMI_STORE_U32(pBuf + Offset, Val32);
	*(pBuf + Offset + 4) = (char)DescrLen;
	SK_MEMCPY(pBuf + Offset + 5, pAC->I2c.SenTable[SensorIndex].SenDesc,
		DescrLen);
	Offset += DescrLen + 5;

	Val32 = OID_SKGE_SENSOR_TYPE;
	SK_PNMI_STORE_U32(pBuf + Offset, Val32);
	*(pBuf + Offset + 4) = 1;
	*(pBuf + Offset + 5) = (char)pAC->I2c.SenTable[SensorIndex].SenType;
	Offset += 6;

	Val32 = OID_SKGE_SENSOR_VALUE;
	SK_PNMI_STORE_U32(pBuf + Offset, Val32);
	*(pBuf + Offset + 4) = 4;
	Val32 = (SK_U32)pAC->I2c.SenTable[SensorIndex].SenValue;
	SK_PNMI_STORE_U32(pBuf + Offset + 5, Val32);
}

/*****************************************************************************
 *
 * QueueRlmtNewMacTrap - Store a port switch trap in the trap buffer
 *
 * Description:
 *	Nothing further to explain.
 *
 * Returns:
 *	Nothing
 */
PNMI_STATIC void QueueRlmtNewMacTrap(
SK_AC *pAC,		/* Pointer to adapter context */
unsigned int ActiveMac)	/* Index (0..n) of the currently active port */
{
	char	*pBuf;
	SK_U32	Val32;


	pBuf = GetTrapEntry(pAC, OID_SKGE_TRAP_RLMT_CHANGE_PORT,
		SK_PNMI_TRAP_RLMT_CHANGE_LEN);

	Val32 = OID_SKGE_RLMT_PORT_ACTIVE;
	SK_PNMI_STORE_U32(pBuf + SK_PNMI_TRAP_SIMPLE_LEN, Val32);
	*(pBuf + SK_PNMI_TRAP_SIMPLE_LEN + 4) = 1;
	*(pBuf + SK_PNMI_TRAP_SIMPLE_LEN + 5) = (char)ActiveMac;
}

/*****************************************************************************
 *
 * QueueRlmtPortTrap - Store port related RLMT trap to trap buffer
 *
 * Description:
 *	Nothing further to explain.
 *
 * Returns:
 *	Nothing
 */
PNMI_STATIC void QueueRlmtPortTrap(
SK_AC *pAC,		/* Pointer to adapter context */
SK_U32 TrapId,		/* Type of RLMT port trap */
unsigned int PortIndex)	/* Index of the port, which changed its state */
{
	char	*pBuf;
	SK_U32	Val32;


	pBuf = GetTrapEntry(pAC, TrapId, SK_PNMI_TRAP_RLMT_PORT_LEN);

	Val32 = OID_SKGE_RLMT_PORT_INDEX;
	SK_PNMI_STORE_U32(pBuf + SK_PNMI_TRAP_SIMPLE_LEN, Val32);
	*(pBuf + SK_PNMI_TRAP_SIMPLE_LEN + 4) = 1;
	*(pBuf + SK_PNMI_TRAP_SIMPLE_LEN + 5) = (char)PortIndex;
}

/*****************************************************************************
 *
 * CopyMac - Copies a MAC address
 *
 * Description:
 *	Nothing further to explain.
 *
 * Returns:
 *	Nothing
 */
PNMI_STATIC void CopyMac(
char *pDst,		/* Pointer to destination buffer */
SK_MAC_ADDR *pMac)	/* Pointer of Source */
{
	int	i;


	for (i = 0; i < sizeof(SK_MAC_ADDR); i ++) {

		*(pDst + i) = pMac->a[i];
	}
}

#ifdef SK_POWER_MGMT
/*****************************************************************************
 *
 * PowerManagement - OID handler function of PowerManagement OIDs
 *
 * Description:
 *	The code is simple. No description necessary.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

PNMI_STATIC int PowerManagement(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode allways zero */
{
	
	SK_U32	RetCode = SK_PNMI_ERR_GENERAL;

	/*
	 * Check instance. We only handle single instance variables
	 */
	if (Instance != (SK_U32)(-1) && Instance != 1) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_INST);
	}
	
    
    /* Check length */
    switch (Id) {

    case OID_PNP_CAPABILITIES:
        if (*pLen < sizeof(SK_PNP_CAPABILITIES)) {

            *pLen = sizeof(SK_PNP_CAPABILITIES);
            return (SK_PNMI_ERR_TOO_SHORT);
        }
        break;

	case OID_PNP_SET_POWER:
    case OID_PNP_QUERY_POWER:
    	if (*pLen < sizeof(SK_DEVICE_POWER_STATE))
    	{
    		*pLen = sizeof(SK_DEVICE_POWER_STATE);
    		return (SK_PNMI_ERR_TOO_SHORT);
    	}
        break;

    case OID_PNP_ADD_WAKE_UP_PATTERN:
    case OID_PNP_REMOVE_WAKE_UP_PATTERN:
		if (*pLen < sizeof(SK_PM_PACKET_PATTERN)) {

			*pLen = sizeof(SK_PM_PACKET_PATTERN);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

    case OID_PNP_ENABLE_WAKE_UP:
        if (*pLen < sizeof(SK_U32)) {

            *pLen = sizeof(SK_U32);
            return (SK_PNMI_ERR_TOO_SHORT);
        }
        break;
    }
	
    /*
	 * Perform action
	 */
	if (Action == SK_PNMI_GET) {

		/*
		 * Get value
		 */
		switch (Id) {

		case OID_PNP_CAPABILITIES:
			RetCode = SkPowerQueryPnPCapabilities(pAC, IoC, pBuf, pLen);
			break;

		case OID_PNP_QUERY_POWER:
			/* The Windows DDK describes: An OID_PNP_QUERY_POWER requests
			 the miniport to indicate whether it can transition its NIC
			 to the low-power state.
			 A miniport driver must always return NDIS_STATUS_SUCCESS
			 to a query of OID_PNP_QUERY_POWER. */
			*pLen = sizeof(SK_DEVICE_POWER_STATE);
            RetCode = SK_PNMI_ERR_OK;
			break;

			/* NDIS handles these OIDs as write-only.
			 * So in case of get action the buffer with written length = 0
			 * is returned
			 */
		case OID_PNP_SET_POWER:
		case OID_PNP_ADD_WAKE_UP_PATTERN:
		case OID_PNP_REMOVE_WAKE_UP_PATTERN:
			*pLen = 0;	
            RetCode = SK_PNMI_ERR_NOT_SUPPORTED;
			break;

		case OID_PNP_ENABLE_WAKE_UP:
			RetCode = SkPowerGetEnableWakeUp(pAC, IoC, pBuf, pLen);
			break;

		default:
			RetCode = SK_PNMI_ERR_GENERAL;
			break;
		}

		return (RetCode);
	}
	

	/*
	 * Perform preset or set
	 */
	
	/* POWER module does not support PRESET action */
	if (Action == SK_PNMI_PRESET) {
		return (SK_PNMI_ERR_OK);
	}

	switch (Id) {
	case OID_PNP_SET_POWER:
		RetCode = SkPowerSetPower(pAC, IoC, pBuf, pLen);	
		break;

	case OID_PNP_ADD_WAKE_UP_PATTERN:
		RetCode = SkPowerAddWakeUpPattern(pAC, IoC, pBuf, pLen);	
		break;
		
	case OID_PNP_REMOVE_WAKE_UP_PATTERN:
		RetCode = SkPowerRemoveWakeUpPattern(pAC, IoC, pBuf, pLen);	
		break;
		
	case OID_PNP_ENABLE_WAKE_UP:
		RetCode = SkPowerSetEnableWakeUp(pAC, IoC, pBuf, pLen);
		break;
		
	default:
		RetCode = SK_PNMI_ERR_READ_ONLY;
	}
	
	return (RetCode);
}
#endif /* SK_POWER_MGMT */

#ifdef SK_DIAG_SUPPORT
/*****************************************************************************
 *
 * DiagActions - OID handler function of Diagnostic driver 
 *
 * Description:
 *	The code is simple. No description necessary.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

PNMI_STATIC int DiagActions(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{

	SK_U32	DiagStatus;
	SK_U32	RetCode = SK_PNMI_ERR_GENERAL;

	/*
	 * Check instance. We only handle single instance variables.
	 */
	if (Instance != (SK_U32)(-1) && Instance != 1) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_INST);
	}

	/*
	 * Check length.
	 */
	switch (Id) {

	case OID_SKGE_DIAG_MODE:
		if (*pLen < sizeof(SK_U32)) {

			*pLen = sizeof(SK_U32);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	default:
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR040, SK_PNMI_ERR040MSG);
		*pLen = 0;
		return (SK_PNMI_ERR_GENERAL);
	}

	/* Perform action. */

	/* GET value. */
	if (Action == SK_PNMI_GET) {

		switch (Id) {

		case OID_SKGE_DIAG_MODE:
			DiagStatus = pAC->Pnmi.DiagAttached;
			SK_PNMI_STORE_U32(pBuf, DiagStatus);
			*pLen = sizeof(SK_U32);	
			RetCode = SK_PNMI_ERR_OK;
			break;

		default:
			*pLen = 0;	
			RetCode = SK_PNMI_ERR_GENERAL;
			break;
		}
		return (RetCode); 
	}

	/* From here SET or PRESET value. */
	
	/* PRESET value is not supported. */
	if (Action == SK_PNMI_PRESET) {
		return (SK_PNMI_ERR_OK); 
	}

	/* SET value. */
	switch (Id) {
		case OID_SKGE_DIAG_MODE:

			/* Handle the SET. */
			switch (*pBuf) {

				/* Attach the DIAG to this adapter. */
				case SK_DIAG_ATTACHED:
					/* Check if we come from running */
					if (pAC->Pnmi.DiagAttached == SK_DIAG_RUNNING) {

						RetCode = SkDrvLeaveDiagMode(pAC);

					}
					else if (pAC->Pnmi.DiagAttached == SK_DIAG_IDLE) {

						RetCode = SK_PNMI_ERR_OK;
					}	
					
					else {

						RetCode = SK_PNMI_ERR_GENERAL;

					}
					
					if (RetCode == SK_PNMI_ERR_OK) {

						pAC->Pnmi.DiagAttached = SK_DIAG_ATTACHED;
					}
					break;

				/* Enter the DIAG mode in the driver. */
				case SK_DIAG_RUNNING:
					RetCode = SK_PNMI_ERR_OK;
					
					/*
					 * If DiagAttached is set, we can tell the driver
					 * to enter the DIAG mode.
					 */
					if (pAC->Pnmi.DiagAttached == SK_DIAG_ATTACHED) {
						/* If DiagMode is not active, we can enter it. */
						if (!pAC->DiagModeActive) {

							RetCode = SkDrvEnterDiagMode(pAC); 
						}
						else {

							RetCode = SK_PNMI_ERR_GENERAL;
						}
					}
					else {

						RetCode = SK_PNMI_ERR_GENERAL;
					}
					
					if (RetCode == SK_PNMI_ERR_OK) {

						pAC->Pnmi.DiagAttached = SK_DIAG_RUNNING;
					}
					break;

				case SK_DIAG_IDLE:
					/* Check if we come from running */
					if (pAC->Pnmi.DiagAttached == SK_DIAG_RUNNING) {

						RetCode = SkDrvLeaveDiagMode(pAC);

					}
					else if (pAC->Pnmi.DiagAttached == SK_DIAG_ATTACHED) {

						RetCode = SK_PNMI_ERR_OK;
					}	
					
					else {

						RetCode = SK_PNMI_ERR_GENERAL;

					}

					if (RetCode == SK_PNMI_ERR_OK) {

						pAC->Pnmi.DiagAttached = SK_DIAG_IDLE;
					}
					break;

				default:
					RetCode = SK_PNMI_ERR_BAD_VALUE;
					break;
			}
			break;

		default:
			RetCode = SK_PNMI_ERR_GENERAL;
	}

	if (RetCode == SK_PNMI_ERR_OK) {
		*pLen = sizeof(SK_U32);
	}
	else {

		*pLen = 0;
	}
	return (RetCode);
}
#endif /* SK_DIAG_SUPPORT */

/*****************************************************************************
 *
 * Vct - OID handler function of  OIDs
 *
 * Description:
 *	The code is simple. No description necessary.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was performed successfully.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter).
 *	SK_PNMI_ERR_READ_ONLY	 Only the Get action is allowed.
 *
 */

PNMI_STATIC int Vct(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* GET/PRESET/SET action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,	/* On call: pBuf buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (-1,2..n) that is to be queried */
unsigned int TableIndex, /* Index to the Id table */
SK_U32 NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
	SK_GEPORT	*pPrt;
	SK_PNMI_VCT	*pVctBackupData;
	SK_U32		LogPortMax;
	SK_U32		PhysPortMax;
	SK_U32		PhysPortIndex;
	SK_U32		Limit;
	SK_U32		Offset;
	SK_BOOL		Link;
	SK_U32		RetCode = SK_PNMI_ERR_GENERAL;
	int		i;
	SK_EVPARA	Para;
	SK_U32		CableLength;
	
	/*
	 * Calculate the port indexes from the instance.
	 */
	PhysPortMax = pAC->GIni.GIMacsFound;
	LogPortMax = SK_PNMI_PORT_PHYS2LOG(PhysPortMax);
	
	/* Dual net mode? */
	if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
		LogPortMax--;
	}
	
	if ((Instance != (SK_U32) (-1))) {
		/* Check instance range. */
		if ((Instance < 2) || (Instance > LogPortMax)) {
			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}
		
		if (pAC->Pnmi.DualNetActiveFlag == SK_TRUE) {
			PhysPortIndex = NetIndex;
		}
		else {
			PhysPortIndex = Instance - 2;
		}
		Limit = PhysPortIndex + 1;
	}
	else {
		/*
		 * Instance == (SK_U32) (-1), get all Instances of that OID.
		 *
		 * Not implemented yet. May be used in future releases.
		 */
		PhysPortIndex = 0;
		Limit = PhysPortMax;
	}
	
	pPrt = &pAC->GIni.GP[PhysPortIndex];
	if (pPrt->PHWLinkUp) {
		Link = SK_TRUE;
	}
	else {
		Link = SK_FALSE;
	}
	
	/* Check MAC type */
	if (pPrt->PhyType != SK_PHY_MARV_COPPER) {
		*pLen = 0;
		return (SK_PNMI_ERR_GENERAL);
	}
	
	/* Initialize backup data pointer. */
	pVctBackupData = &pAC->Pnmi.VctBackup[PhysPortIndex];
	
	/* Check action type */
	if (Action == SK_PNMI_GET) {
		/* Check length */
		switch (Id) {
		
		case OID_SKGE_VCT_GET:
			if (*pLen < (Limit - PhysPortIndex) * sizeof(SK_PNMI_VCT)) {
				*pLen = (Limit - PhysPortIndex) * sizeof(SK_PNMI_VCT);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			break;
		
		case OID_SKGE_VCT_STATUS:
			if (*pLen < (Limit - PhysPortIndex) * sizeof(SK_U8)) {
				*pLen = (Limit - PhysPortIndex) * sizeof(SK_U8);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			break;
		
		default:
			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}	
		
		/* Get value */
		Offset = 0;
		for (; PhysPortIndex < Limit; PhysPortIndex++) {
			switch (Id) {
			
			case OID_SKGE_VCT_GET:
				if ((Link == SK_FALSE) &&
					(pAC->Pnmi.VctStatus[PhysPortIndex] & SK_PNMI_VCT_PENDING)) {
					RetCode = SkGmCableDiagStatus(pAC, IoC, PhysPortIndex, SK_FALSE);
					if (RetCode == 0) {
						pAC->Pnmi.VctStatus[PhysPortIndex] &= ~SK_PNMI_VCT_PENDING;
						pAC->Pnmi.VctStatus[PhysPortIndex] |=
							(SK_PNMI_VCT_NEW_VCT_DATA | SK_PNMI_VCT_TEST_DONE);
						
						/* Copy results for later use to PNMI struct. */
						for (i = 0; i < 4; i++)  {
							if (pPrt->PMdiPairSts[i] == SK_PNMI_VCT_NORMAL_CABLE) {
								if ((pPrt->PMdiPairLen[i] > 35) && (pPrt->PMdiPairLen[i] < 0xff)) {
									pPrt->PMdiPairSts[i] = SK_PNMI_VCT_IMPEDANCE_MISMATCH;
								}
							}
							if ((pPrt->PMdiPairLen[i] > 35) && (pPrt->PMdiPairLen[i] != 0xff)) {
								CableLength = 1000 * (((175 * pPrt->PMdiPairLen[i]) / 210) - 28);
							}
							else {
								CableLength = 0;
							}
							pVctBackupData->PMdiPairLen[i] = CableLength;
							pVctBackupData->PMdiPairSts[i] = pPrt->PMdiPairSts[i];
						}

						Para.Para32[0] = PhysPortIndex;
						Para.Para32[1] = -1;
						SkEventQueue(pAC, SKGE_DRV, SK_DRV_PORT_RESET, Para);
						SkEventDispatcher(pAC, IoC);
					}
					else {
						; /* VCT test is running. */
					}
				}
				
				/* Get all results. */
				CheckVctStatus(pAC, IoC, pBuf, Offset, PhysPortIndex);
				Offset += sizeof(SK_U8);
				*(pBuf + Offset) = pPrt->PCableLen;
				Offset += sizeof(SK_U8);
				for (i = 0; i < 4; i++)  {
					SK_PNMI_STORE_U32((pBuf + Offset), pVctBackupData->PMdiPairLen[i]);
					Offset += sizeof(SK_U32);
				}
				for (i = 0; i < 4; i++)  {
					*(pBuf + Offset) = pVctBackupData->PMdiPairSts[i];
					Offset += sizeof(SK_U8);
				}
				
				RetCode = SK_PNMI_ERR_OK;
				break;
		
			case OID_SKGE_VCT_STATUS:
				CheckVctStatus(pAC, IoC, pBuf, Offset, PhysPortIndex);
				Offset += sizeof(SK_U8);
				RetCode = SK_PNMI_ERR_OK;
				break;
			
			default:
				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
		} /* for */
		*pLen = Offset;
		return (RetCode);
	
	} /* if SK_PNMI_GET */
	
	/*
	 * From here SET or PRESET action. Check if the passed
	 * buffer length is plausible.
	 */
	
	/* Check length */
	switch (Id) {
	case OID_SKGE_VCT_SET:
		if (*pLen < (Limit - PhysPortIndex) * sizeof(SK_U32)) {
			*pLen = (Limit - PhysPortIndex) * sizeof(SK_U32);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;
	
	default:
		*pLen = 0;
		return (SK_PNMI_ERR_GENERAL);
	}
	
	/*
	 * Perform preset or set.
	 */
	
	/* VCT does not support PRESET action. */
	if (Action == SK_PNMI_PRESET) {
		return (SK_PNMI_ERR_OK);
	}
	
	Offset = 0;
	for (; PhysPortIndex < Limit; PhysPortIndex++) {
		switch (Id) {
		case OID_SKGE_VCT_SET: /* Start VCT test. */
			if (Link == SK_FALSE) {
				SkGeStopPort(pAC, IoC, PhysPortIndex, SK_STOP_ALL, SK_SOFT_RST);
				
				RetCode = SkGmCableDiagStatus(pAC, IoC, PhysPortIndex, SK_TRUE);
				if (RetCode == 0) { /* RetCode: 0 => Start! */
					pAC->Pnmi.VctStatus[PhysPortIndex] |= SK_PNMI_VCT_PENDING;
					pAC->Pnmi.VctStatus[PhysPortIndex] &= ~SK_PNMI_VCT_NEW_VCT_DATA;
					pAC->Pnmi.VctStatus[PhysPortIndex] &= ~SK_PNMI_VCT_LINK;
					
					/*
					 * Start VCT timer counter.
					 */
					SK_MEMSET((char *) &Para, 0, sizeof(Para));
					Para.Para32[0] = PhysPortIndex;
					Para.Para32[1] = -1;
					SkTimerStart(pAC, IoC, &pAC->Pnmi.VctTimeout[PhysPortIndex].VctTimer,
						4000000, SKGE_PNMI, SK_PNMI_EVT_VCT_RESET, Para);
					SK_PNMI_STORE_U32((pBuf + Offset), RetCode);
					RetCode = SK_PNMI_ERR_OK;
				}
				else { /* RetCode: 2 => Running! */
					SK_PNMI_STORE_U32((pBuf + Offset), RetCode);
					RetCode = SK_PNMI_ERR_OK;
				}
			}
			else { /* RetCode: 4 => Link! */
				RetCode = 4;
				SK_PNMI_STORE_U32((pBuf + Offset), RetCode);
				RetCode = SK_PNMI_ERR_OK;
			}
			Offset += sizeof(SK_U32);
			break;
	
		default:
			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}
	} /* for */
	*pLen = Offset;
	return (RetCode);

} /* Vct */


PNMI_STATIC void CheckVctStatus(
SK_AC		*pAC,
SK_IOC		IoC,
char		*pBuf,
SK_U32		Offset,
SK_U32		PhysPortIndex)
{
	SK_GEPORT 	*pPrt;
	SK_PNMI_VCT	*pVctData;
	SK_U32		RetCode;
	
	pPrt = &pAC->GIni.GP[PhysPortIndex];
	
	pVctData = (SK_PNMI_VCT *) (pBuf + Offset);
	pVctData->VctStatus = SK_PNMI_VCT_NONE;
	
	if (!pPrt->PHWLinkUp) {
		
		/* Was a VCT test ever made before? */
		if (pAC->Pnmi.VctStatus[PhysPortIndex] & SK_PNMI_VCT_TEST_DONE) {
			if ((pAC->Pnmi.VctStatus[PhysPortIndex] & SK_PNMI_VCT_LINK)) {
				pVctData->VctStatus |= SK_PNMI_VCT_OLD_VCT_DATA;
			}
			else {
				pVctData->VctStatus |= SK_PNMI_VCT_NEW_VCT_DATA;
			}
		}
		
		/* Check VCT test status. */
		RetCode = SkGmCableDiagStatus(pAC,IoC, PhysPortIndex, SK_FALSE);
		if (RetCode == 2) { /* VCT test is running. */
			pVctData->VctStatus |= SK_PNMI_VCT_RUNNING;
		}
		else { /* VCT data was copied to pAC here. Check PENDING state. */
			if (pAC->Pnmi.VctStatus[PhysPortIndex] & SK_PNMI_VCT_PENDING) {
				pVctData->VctStatus |= SK_PNMI_VCT_NEW_VCT_DATA;
			}
		}
		
		if (pPrt->PCableLen != 0xff) { /* Old DSP value. */
			pVctData->VctStatus |= SK_PNMI_VCT_OLD_DSP_DATA;
		}
	}
	else {
		
		/* Was a VCT test ever made before? */
		if (pAC->Pnmi.VctStatus[PhysPortIndex] & SK_PNMI_VCT_TEST_DONE) {
			pVctData->VctStatus &= ~SK_PNMI_VCT_NEW_VCT_DATA;
			pVctData->VctStatus |= SK_PNMI_VCT_OLD_VCT_DATA;
		}
		
		/* DSP only valid in 100/1000 modes. */
		if (pAC->GIni.GP[PhysPortIndex].PLinkSpeedUsed !=
			SK_LSPEED_STAT_10MBPS) {	
			pVctData->VctStatus |= SK_PNMI_VCT_NEW_DSP_DATA;
		}
	}
} /* CheckVctStatus */


/*****************************************************************************
 *
 *      SkPnmiGenIoctl - Handles new generic PNMI IOCTL, calls the needed
 *                       PNMI function depending on the subcommand and
 *                       returns all data belonging to the complete database
 *                       or OID request.
 *
 * Description:
 *	Looks up the requested subcommand, calls the corresponding handler
 *	function and passes all required parameters to it.
 *	The function is called by the driver. It is needed to handle the new
 *  generic PNMI IOCTL. This IOCTL is given to the driver and contains both
 *  the OID and a subcommand to decide what kind of request has to be done.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to take
 *	                         the data.
 *	SK_PNMI_ERR_UNKNOWN_OID  The requested OID is unknown
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                           exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */
int SkPnmiGenIoctl(
SK_AC		*pAC,		/* Pointer to adapter context struct */
SK_IOC		IoC,		/* I/O context */
void		*pBuf,		/* Buffer used for the management data transfer */
unsigned int *pLen,		/* Length of buffer */
SK_U32		NetIndex)	/* NetIndex (0..n), in single net mode always zero */
{
SK_I32	Mode;			/* Store value of subcommand. */
SK_U32	Oid;			/* Store value of OID. */
int		ReturnCode;		/* Store return value to show status of PNMI action. */
int 	HeaderLength;	/* Length of desired action plus OID. */

	ReturnCode = SK_PNMI_ERR_GENERAL;
	
	SK_MEMCPY(&Mode, pBuf, sizeof(SK_I32));
	SK_MEMCPY(&Oid, (char *) pBuf + sizeof(SK_I32), sizeof(SK_U32));
	HeaderLength = sizeof(SK_I32) + sizeof(SK_U32);
	*pLen = *pLen - HeaderLength;
	SK_MEMCPY((char *) pBuf + sizeof(SK_I32), (char *) pBuf + HeaderLength, *pLen);
	
	switch(Mode) {
	case SK_GET_SINGLE_VAR:
		ReturnCode = SkPnmiGetVar(pAC, IoC, Oid, 
				(char *) pBuf + sizeof(SK_I32), pLen,
				((SK_U32) (-1)), NetIndex);
		SK_PNMI_STORE_U32(pBuf, ReturnCode);
		*pLen = *pLen + sizeof(SK_I32);
		break;
	case SK_PRESET_SINGLE_VAR:
		ReturnCode = SkPnmiPreSetVar(pAC, IoC, Oid, 
				(char *) pBuf + sizeof(SK_I32), pLen,
				((SK_U32) (-1)), NetIndex);
		SK_PNMI_STORE_U32(pBuf, ReturnCode);
		*pLen = *pLen + sizeof(SK_I32);
		break;
	case SK_SET_SINGLE_VAR:
		ReturnCode = SkPnmiSetVar(pAC, IoC, Oid, 
				(char *) pBuf + sizeof(SK_I32), pLen,
				((SK_U32) (-1)), NetIndex);
		SK_PNMI_STORE_U32(pBuf, ReturnCode);
		*pLen = *pLen + sizeof(SK_I32);
		break;
	case SK_GET_FULL_MIB:
		ReturnCode = SkPnmiGetStruct(pAC, IoC, pBuf, pLen, NetIndex);
		break;
	case SK_PRESET_FULL_MIB:
		ReturnCode = SkPnmiPreSetStruct(pAC, IoC, pBuf, pLen, NetIndex);
		break;
	case SK_SET_FULL_MIB:
		ReturnCode = SkPnmiSetStruct(pAC, IoC, pBuf, pLen, NetIndex);
		break;
	default:
		break;
	}
	
	return (ReturnCode);

} /* SkGeIocGen */
