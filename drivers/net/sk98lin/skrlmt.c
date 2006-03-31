/******************************************************************************
 *
 * Name:	skrlmt.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.69 $
 * Date:	$Date: 2003/04/15 09:39:22 $
 * Purpose:	Manage links on SK-NET Adapters, esp. redundant ones.
 *
 ******************************************************************************/

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

/******************************************************************************
 *
 * Description:
 *
 * This module contains code for Link ManagemenT (LMT) of SK-NET Adapters.
 * It is mainly intended for adapters with more than one link.
 * For such adapters, this module realizes Redundant Link ManagemenT (RLMT).
 *
 * Include File Hierarchy:
 *
 *	"skdrv1st.h"
 *	"skdrv2nd.h"
 *
 ******************************************************************************/

#ifndef	lint
static const char SysKonnectFileId[] =
	"@(#) $Id: skrlmt.c,v 1.69 2003/04/15 09:39:22 tschilli Exp $ (C) Marvell.";
#endif	/* !defined(lint) */

#define __SKRLMT_C

#ifdef __cplusplus
extern "C" {
#endif	/* cplusplus */

#include "h/skdrv1st.h"
#include "h/skdrv2nd.h"

/* defines ********************************************************************/

#ifndef SK_HWAC_LINK_LED
#define SK_HWAC_LINK_LED(a,b,c,d)
#endif	/* !defined(SK_HWAC_LINK_LED) */

#ifndef DEBUG
#define RLMT_STATIC	static
#else	/* DEBUG */
#define RLMT_STATIC

#ifndef SK_LITTLE_ENDIAN
/* First 32 bits */
#define OFFS_LO32	1

/* Second 32 bits */
#define OFFS_HI32	0
#else	/* SK_LITTLE_ENDIAN */
/* First 32 bits */
#define OFFS_LO32	0

/* Second 32 bits */
#define OFFS_HI32	1
#endif	/* SK_LITTLE_ENDIAN */

#endif	/* DEBUG */

/* ----- Private timeout values ----- */

#define SK_RLMT_MIN_TO_VAL			   125000	/* 1/8 sec. */
#define SK_RLMT_DEF_TO_VAL			  1000000	/* 1 sec. */
#define SK_RLMT_PORTDOWN_TIM_VAL	   900000	/* another 0.9 sec. */
#define SK_RLMT_PORTSTART_TIM_VAL	   100000	/* 0.1 sec. */
#define SK_RLMT_PORTUP_TIM_VAL		  2500000	/* 2.5 sec. */
#define SK_RLMT_SEG_TO_VAL			900000000	/* 15 min. */

/* Assume tick counter increment is 1 - may be set OS-dependent. */
#ifndef SK_TICK_INCR
#define SK_TICK_INCR	SK_CONSTU64(1)
#endif	/* !defined(SK_TICK_INCR) */

/*
 * Amount that a time stamp must be later to be recognized as "substantially
 * later". This is about 1/128 sec, but above 1 tick counter increment.
 */
#define SK_RLMT_BC_DELTA		(1 + ((SK_TICKS_PER_SEC >> 7) > SK_TICK_INCR ? \
									(SK_TICKS_PER_SEC >> 7) : SK_TICK_INCR))

/* ----- Private RLMT defaults ----- */

#define SK_RLMT_DEF_PREF_PORT	0					/* "Lower" port. */
#define SK_RLMT_DEF_MODE 		SK_RLMT_CHECK_LINK	/* Default RLMT Mode. */

/* ----- Private RLMT checking states ----- */

#define SK_RLMT_RCS_SEG			1		/* RLMT Check State: check seg. */
#define SK_RLMT_RCS_START_SEG	2		/* RLMT Check State: start check seg. */
#define SK_RLMT_RCS_SEND_SEG	4		/* RLMT Check State: send BPDU packet */
#define SK_RLMT_RCS_REPORT_SEG	8		/* RLMT Check State: report seg. */

/* ----- Private PORT checking states ----- */

#define SK_RLMT_PCS_TX			1		/* Port Check State: check tx. */
#define SK_RLMT_PCS_RX			2		/* Port Check State: check rx. */

/* ----- Private PORT events ----- */

/* Note: Update simulation when changing these. */
#define SK_RLMT_PORTSTART_TIM	1100	/* Port start timeout. */
#define SK_RLMT_PORTUP_TIM		1101	/* Port can now go up. */
#define SK_RLMT_PORTDOWN_RX_TIM	1102	/* Port did not receive once ... */
#define SK_RLMT_PORTDOWN		1103	/* Port went down. */
#define SK_RLMT_PORTDOWN_TX_TIM	1104	/* Partner did not receive ... */

/* ----- Private RLMT events ----- */

/* Note: Update simulation when changing these. */
#define SK_RLMT_TIM				2100	/* RLMT timeout. */
#define SK_RLMT_SEG_TIM			2101	/* RLMT segmentation check timeout. */

#define TO_SHORTEN(tim)	((tim) / 2)

/* Error numbers and messages. */
#define SKERR_RLMT_E001		(SK_ERRBASE_RLMT + 0)
#define SKERR_RLMT_E001_MSG	"No Packet."
#define SKERR_RLMT_E002		(SKERR_RLMT_E001 + 1)
#define SKERR_RLMT_E002_MSG	"Short Packet."
#define SKERR_RLMT_E003		(SKERR_RLMT_E002 + 1)
#define SKERR_RLMT_E003_MSG	"Unknown RLMT event."
#define SKERR_RLMT_E004		(SKERR_RLMT_E003 + 1)
#define SKERR_RLMT_E004_MSG	"PortsUp incorrect."
#define SKERR_RLMT_E005		(SKERR_RLMT_E004 + 1)
#define SKERR_RLMT_E005_MSG	\
 "Net seems to be segmented (different root bridges are reported on the ports)."
#define SKERR_RLMT_E006		(SKERR_RLMT_E005 + 1)
#define SKERR_RLMT_E006_MSG	"Duplicate MAC Address detected."
#define SKERR_RLMT_E007		(SKERR_RLMT_E006 + 1)
#define SKERR_RLMT_E007_MSG	"LinksUp incorrect."
#define SKERR_RLMT_E008		(SKERR_RLMT_E007 + 1)
#define SKERR_RLMT_E008_MSG	"Port not started but link came up."
#define SKERR_RLMT_E009		(SKERR_RLMT_E008 + 1)
#define SKERR_RLMT_E009_MSG	"Corrected illegal setting of Preferred Port."
#define SKERR_RLMT_E010		(SKERR_RLMT_E009 + 1)
#define SKERR_RLMT_E010_MSG	"Ignored illegal Preferred Port."

/* LLC field values. */
#define LLC_COMMAND_RESPONSE_BIT		1
#define LLC_TEST_COMMAND				0xE3
#define LLC_UI							0x03

/* RLMT Packet fields. */
#define	SK_RLMT_DSAP					0
#define	SK_RLMT_SSAP					0
#define SK_RLMT_CTRL					(LLC_TEST_COMMAND)
#define SK_RLMT_INDICATOR0				0x53	/* S */
#define SK_RLMT_INDICATOR1				0x4B	/* K */
#define SK_RLMT_INDICATOR2				0x2D	/* - */
#define SK_RLMT_INDICATOR3				0x52	/* R */
#define SK_RLMT_INDICATOR4				0x4C	/* L */
#define SK_RLMT_INDICATOR5				0x4D	/* M */
#define SK_RLMT_INDICATOR6				0x54	/* T */
#define SK_RLMT_PACKET_VERSION			0

/* RLMT SPT Flag values. */
#define	SK_RLMT_SPT_FLAG_CHANGE			0x01
#define	SK_RLMT_SPT_FLAG_CHANGE_ACK		0x80

/* RLMT SPT Packet fields. */
#define	SK_RLMT_SPT_DSAP				0x42
#define	SK_RLMT_SPT_SSAP				0x42
#define SK_RLMT_SPT_CTRL				(LLC_UI)
#define	SK_RLMT_SPT_PROTOCOL_ID0		0x00
#define	SK_RLMT_SPT_PROTOCOL_ID1		0x00
#define	SK_RLMT_SPT_PROTOCOL_VERSION_ID	0x00
#define	SK_RLMT_SPT_BPDU_TYPE			0x00
#define	SK_RLMT_SPT_FLAGS				0x00	/* ?? */
#define	SK_RLMT_SPT_ROOT_ID0			0xFF	/* Lowest possible priority. */
#define	SK_RLMT_SPT_ROOT_ID1			0xFF	/* Lowest possible priority. */

/* Remaining 6 bytes will be the current port address. */
#define	SK_RLMT_SPT_ROOT_PATH_COST0		0x00
#define	SK_RLMT_SPT_ROOT_PATH_COST1		0x00
#define	SK_RLMT_SPT_ROOT_PATH_COST2		0x00
#define	SK_RLMT_SPT_ROOT_PATH_COST3		0x00
#define	SK_RLMT_SPT_BRIDGE_ID0			0xFF	/* Lowest possible priority. */
#define	SK_RLMT_SPT_BRIDGE_ID1			0xFF	/* Lowest possible priority. */

/* Remaining 6 bytes will be the current port address. */
#define	SK_RLMT_SPT_PORT_ID0			0xFF	/* Lowest possible priority. */
#define	SK_RLMT_SPT_PORT_ID1			0xFF	/* Lowest possible priority. */
#define	SK_RLMT_SPT_MSG_AGE0			0x00
#define	SK_RLMT_SPT_MSG_AGE1			0x00
#define	SK_RLMT_SPT_MAX_AGE0			0x00
#define	SK_RLMT_SPT_MAX_AGE1			0xFF
#define	SK_RLMT_SPT_HELLO_TIME0			0x00
#define	SK_RLMT_SPT_HELLO_TIME1			0xFF
#define	SK_RLMT_SPT_FWD_DELAY0			0x00
#define	SK_RLMT_SPT_FWD_DELAY1			0x40

/* Size defines. */
#define SK_RLMT_MIN_PACKET_SIZE			34
#define SK_RLMT_MAX_PACKET_SIZE			(SK_RLMT_MAX_TX_BUF_SIZE)
#define SK_PACKET_DATA_LEN				(SK_RLMT_MAX_PACKET_SIZE - \
										SK_RLMT_MIN_PACKET_SIZE)

/* ----- RLMT packet types ----- */
#define SK_PACKET_ANNOUNCE				1	/* Port announcement. */
#define SK_PACKET_ALIVE					2	/* Alive packet to port. */
#define SK_PACKET_ADDR_CHANGED			3	/* Port address changed. */
#define SK_PACKET_CHECK_TX				4	/* Check your tx line. */

#ifdef SK_LITTLE_ENDIAN
#define SK_U16_TO_NETWORK_ORDER(Val,Addr) { \
	SK_U8	*_Addr = (SK_U8*)(Addr); \
	SK_U16	_Val = (SK_U16)(Val); \
	*_Addr++ = (SK_U8)(_Val >> 8); \
	*_Addr = (SK_U8)(_Val & 0xFF); \
}
#endif	/* SK_LITTLE_ENDIAN */

#ifdef SK_BIG_ENDIAN
#define SK_U16_TO_NETWORK_ORDER(Val,Addr) (*(SK_U16*)(Addr) = (SK_U16)(Val))
#endif	/* SK_BIG_ENDIAN */

#define AUTONEG_FAILED	SK_FALSE
#define AUTONEG_SUCCESS	SK_TRUE


/* typedefs *******************************************************************/

/* RLMT packet.  Length: SK_RLMT_MAX_PACKET_SIZE (60) bytes. */
typedef struct s_RlmtPacket {
	SK_U8	DstAddr[SK_MAC_ADDR_LEN];
	SK_U8	SrcAddr[SK_MAC_ADDR_LEN];
	SK_U8	TypeLen[2];
	SK_U8	DSap;
	SK_U8	SSap;
	SK_U8	Ctrl;
	SK_U8	Indicator[7];
	SK_U8	RlmtPacketType[2];
	SK_U8	Align1[2];
	SK_U8	Random[4];				/* Random value of requesting(!) station. */
	SK_U8	RlmtPacketVersion[2];	/* RLMT Packet version. */
	SK_U8	Data[SK_PACKET_DATA_LEN];
} SK_RLMT_PACKET;

typedef struct s_SpTreeRlmtPacket {
	SK_U8	DstAddr[SK_MAC_ADDR_LEN];
	SK_U8	SrcAddr[SK_MAC_ADDR_LEN];
	SK_U8	TypeLen[2];
	SK_U8	DSap;
	SK_U8	SSap;
	SK_U8	Ctrl;
	SK_U8	ProtocolId[2];
	SK_U8	ProtocolVersionId;
	SK_U8	BpduType;
	SK_U8	Flags;
	SK_U8	RootId[8];
	SK_U8	RootPathCost[4];
	SK_U8	BridgeId[8];
	SK_U8	PortId[2];
	SK_U8	MessageAge[2];
	SK_U8	MaxAge[2];
	SK_U8	HelloTime[2];
	SK_U8	ForwardDelay[2];
} SK_SPTREE_PACKET;

/* global variables ***********************************************************/

SK_MAC_ADDR	SkRlmtMcAddr =	{{0x01,  0x00,  0x5A,  0x52,  0x4C,  0x4D}};
SK_MAC_ADDR	BridgeMcAddr =	{{0x01,  0x80,  0xC2,  0x00,  0x00,  0x00}};

/* local variables ************************************************************/

/* None. */

/* functions ******************************************************************/

RLMT_STATIC void	SkRlmtCheckSwitch(
	SK_AC	*pAC,
	SK_IOC	IoC,
	SK_U32	NetIdx);
RLMT_STATIC void	SkRlmtCheckSeg(
	SK_AC	*pAC,
	SK_IOC	IoC,
	SK_U32	NetIdx);
RLMT_STATIC void	SkRlmtEvtSetNets(
	SK_AC		*pAC,
	SK_IOC		IoC,
	SK_EVPARA	Para);

/******************************************************************************
 *
 *	SkRlmtInit - initialize data, set state to init
 *
 * Description:
 *
 *	SK_INIT_DATA
 *	============
 *
 *	This routine initializes all RLMT-related variables to a known state.
 *	The initial state is SK_RLMT_RS_INIT.
 *	All ports are initialized to SK_RLMT_PS_INIT.
 *
 *
 *	SK_INIT_IO
 *	==========
 *
 *	Nothing.
 *
 *
 *	SK_INIT_RUN
 *	===========
 *
 *	Determine the adapter's random value.
 *	Set the hw registers, the "logical MAC address", the
 *	RLMT multicast address, and eventually the BPDU multicast address.
 *
 * Context:
 *	init, pageable
 *
 * Returns:
 *	Nothing.
 */
void	SkRlmtInit(
SK_AC	*pAC,	/* Adapter Context */
SK_IOC	IoC,	/* I/O Context */
int		Level)	/* Initialization Level */
{
	SK_U32		i, j;
	SK_U64		Random;
	SK_EVPARA	Para;
    SK_MAC_ADDR		VirtualMacAddress;
    SK_MAC_ADDR		PhysicalAMacAddress;
    SK_BOOL		VirtualMacAddressSet;
    SK_BOOL		PhysicalAMacAddressSet;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_INIT,
		("RLMT Init level %d.\n", Level))

	switch (Level) {
	case SK_INIT_DATA:	/* Initialize data structures. */
		SK_MEMSET((char *)&pAC->Rlmt, 0, sizeof(SK_RLMT));

		for (i = 0; i < SK_MAX_MACS; i++) {
			pAC->Rlmt.Port[i].PortState = SK_RLMT_PS_INIT;
			pAC->Rlmt.Port[i].LinkDown = SK_TRUE;
			pAC->Rlmt.Port[i].PortDown = SK_TRUE;
			pAC->Rlmt.Port[i].PortStarted = SK_FALSE;
			pAC->Rlmt.Port[i].PortNoRx = SK_FALSE;
			pAC->Rlmt.Port[i].RootIdSet = SK_FALSE;
			pAC->Rlmt.Port[i].PortNumber = i;
			pAC->Rlmt.Port[i].Net = &pAC->Rlmt.Net[0];
			pAC->Rlmt.Port[i].AddrPort = &pAC->Addr.Port[i];
		}

		pAC->Rlmt.NumNets = 1;
		for (i = 0; i < SK_MAX_NETS; i++) {
			pAC->Rlmt.Net[i].RlmtState = SK_RLMT_RS_INIT;
			pAC->Rlmt.Net[i].RootIdSet = SK_FALSE;
			pAC->Rlmt.Net[i].PrefPort = SK_RLMT_DEF_PREF_PORT;
			pAC->Rlmt.Net[i].Preference = 0xFFFFFFFF;	  /* Automatic. */
			/* Just assuming. */
			pAC->Rlmt.Net[i].ActivePort = pAC->Rlmt.Net[i].PrefPort;
			pAC->Rlmt.Net[i].RlmtMode = SK_RLMT_DEF_MODE;
			pAC->Rlmt.Net[i].TimeoutValue = SK_RLMT_DEF_TO_VAL;
			pAC->Rlmt.Net[i].NetNumber = i;
		}

		pAC->Rlmt.Net[0].Port[0] = &pAC->Rlmt.Port[0];
		pAC->Rlmt.Net[0].Port[1] = &pAC->Rlmt.Port[1];
#if SK_MAX_NETS > 1
		pAC->Rlmt.Net[1].Port[0] = &pAC->Rlmt.Port[1];
#endif	/* SK_MAX_NETS > 1 */
		break;

	case SK_INIT_IO:	/* GIMacsFound first available here. */
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_INIT,
			("RLMT: %d MACs were detected.\n", pAC->GIni.GIMacsFound))

		pAC->Rlmt.Net[0].NumPorts = pAC->GIni.GIMacsFound;

		/* Initialize HW registers? */
		if (pAC->GIni.GIMacsFound == 1) {
			Para.Para32[0] = SK_RLMT_MODE_CLS;
			Para.Para32[1] = 0;
			(void)SkRlmtEvent(pAC, IoC, SK_RLMT_MODE_CHANGE, Para);
		}
		break;

	case SK_INIT_RUN:
		/* Ensure RLMT is set to one net. */
		if (pAC->Rlmt.NumNets > 1) {
			Para.Para32[0] = 1;
			Para.Para32[1] = -1;
			SkRlmtEvtSetNets(pAC, IoC, Para);
		}

		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			Random = SkOsGetTime(pAC);
			*(SK_U32*)&pAC->Rlmt.Port[i].Random = *(SK_U32*)&Random;

			for (j = 0; j < 4; j++) {
				pAC->Rlmt.Port[i].Random[j] ^= pAC->Rlmt.Port[i].AddrPort->
					CurrentMacAddress.a[SK_MAC_ADDR_LEN - 1 - j];
			}

			(void)SkAddrMcClear(pAC, IoC, i, SK_ADDR_PERMANENT | SK_MC_SW_ONLY);
			
			/* Add RLMT MC address. */
			(void)SkAddrMcAdd(pAC, IoC, i, &SkRlmtMcAddr, SK_ADDR_PERMANENT);

			if (pAC->Rlmt.Net[0].RlmtMode & SK_RLMT_CHECK_SEG) {
				/* Add BPDU MC address. */
				(void)SkAddrMcAdd(pAC, IoC, i, &BridgeMcAddr, SK_ADDR_PERMANENT);
			}

			(void)SkAddrMcUpdate(pAC, IoC, i);
		}

    	VirtualMacAddressSet = SK_FALSE;
		/* Read virtual MAC address from Control Register File. */
		for (j = 0; j < SK_MAC_ADDR_LEN; j++) {
			
            SK_IN8(IoC, B2_MAC_1 + j, &VirtualMacAddress.a[j]);
            VirtualMacAddressSet |= VirtualMacAddress.a[j];
		}
    	
        PhysicalAMacAddressSet = SK_FALSE;
		/* Read physical MAC address for MAC A from Control Register File. */
		for (j = 0; j < SK_MAC_ADDR_LEN; j++) {
			
            SK_IN8(IoC, B2_MAC_2 + j, &PhysicalAMacAddress.a[j]);
            PhysicalAMacAddressSet |= PhysicalAMacAddress.a[j];
		}

        /* check if the two mac addresses contain reasonable values */
        if (!VirtualMacAddressSet || !PhysicalAMacAddressSet) {

            pAC->Rlmt.RlmtOff = SK_TRUE;
        }

        /* if the two mac addresses are equal switch off the RLMT_PRE_LOOKAHEAD
           and the RLMT_LOOKAHEAD macros */
        else if (SK_ADDR_EQUAL(PhysicalAMacAddress.a, VirtualMacAddress.a)) {

            pAC->Rlmt.RlmtOff = SK_TRUE;
        }
		else {
			pAC->Rlmt.RlmtOff = SK_FALSE;
		}
		break;

	default:	/* error */
		break;
	}
	return;
}	/* SkRlmtInit */


/******************************************************************************
 *
 *	SkRlmtBuildCheckChain - build the check chain
 *
 * Description:
 *	This routine builds the local check chain:
 *	- Each port that is up checks the next port.
 *	- The last port that is up checks the first port that is up.
 *
 * Notes:
 *	- Currently only local ports are considered when building the chain.
 *	- Currently the SuspectState is just reset;
 *	  it would be better to save it ...
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtBuildCheckChain(
SK_AC	*pAC,	/* Adapter Context */
SK_U32	NetIdx)	/* Net Number */
{
	SK_U32			i;
	SK_U32			NumMacsUp;
	SK_RLMT_PORT *	FirstMacUp;
	SK_RLMT_PORT *	PrevMacUp;

	FirstMacUp	= NULL;
	PrevMacUp	= NULL;
	
	if (!(pAC->Rlmt.Net[NetIdx].RlmtMode & SK_RLMT_CHECK_LOC_LINK)) {
		for (i = 0; i < pAC->Rlmt.Net[i].NumPorts; i++) {
			pAC->Rlmt.Net[NetIdx].Port[i]->PortsChecked = 0;
		}
		return;	/* Done. */
	}
			
	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SkRlmtBuildCheckChain.\n"))

	NumMacsUp = 0;

	for (i = 0; i < pAC->Rlmt.Net[NetIdx].NumPorts; i++) {
		pAC->Rlmt.Net[NetIdx].Port[i]->PortsChecked = 0;
		pAC->Rlmt.Net[NetIdx].Port[i]->PortsSuspect = 0;
		pAC->Rlmt.Net[NetIdx].Port[i]->CheckingState &=
			~(SK_RLMT_PCS_RX | SK_RLMT_PCS_TX);

		/*
		 * If more than two links are detected we should consider
		 * checking at least two other ports:
		 * 1. the next port that is not LinkDown and
		 * 2. the next port that is not PortDown.
		 */
		if (!pAC->Rlmt.Net[NetIdx].Port[i]->LinkDown) {
			if (NumMacsUp == 0) {
				FirstMacUp = pAC->Rlmt.Net[NetIdx].Port[i];
			}
			else {
				PrevMacUp->PortCheck[
					pAC->Rlmt.Net[NetIdx].Port[i]->PortsChecked].CheckAddr =
					pAC->Rlmt.Net[NetIdx].Port[i]->AddrPort->CurrentMacAddress;
				PrevMacUp->PortCheck[
					PrevMacUp->PortsChecked].SuspectTx = SK_FALSE;
				PrevMacUp->PortsChecked++;
			}
			PrevMacUp = pAC->Rlmt.Net[NetIdx].Port[i];
			NumMacsUp++;
		}
	}

	if (NumMacsUp > 1) {
		PrevMacUp->PortCheck[PrevMacUp->PortsChecked].CheckAddr =
			FirstMacUp->AddrPort->CurrentMacAddress;
		PrevMacUp->PortCheck[PrevMacUp->PortsChecked].SuspectTx =
			SK_FALSE;
		PrevMacUp->PortsChecked++;
	}

#ifdef DEBUG
	for (i = 0; i < pAC->Rlmt.Net[NetIdx].NumPorts; i++) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Port %d checks %d other ports: %2X.\n", i,
				pAC->Rlmt.Net[NetIdx].Port[i]->PortsChecked,
				pAC->Rlmt.Net[NetIdx].Port[i]->PortCheck[0].CheckAddr.a[5]))
	}
#endif	/* DEBUG */

	return;
}	/* SkRlmtBuildCheckChain */


/******************************************************************************
 *
 *	SkRlmtBuildPacket - build an RLMT packet
 *
 * Description:
 *	This routine sets up an RLMT packet.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	NULL or pointer to RLMT mbuf
 */
RLMT_STATIC SK_MBUF	*SkRlmtBuildPacket(
SK_AC		*pAC,		/* Adapter Context */
SK_IOC		IoC,		/* I/O Context */
SK_U32		PortNumber,	/* Sending port */
SK_U16		PacketType,	/* RLMT packet type */
SK_MAC_ADDR	*SrcAddr,	/* Source address */
SK_MAC_ADDR	*DestAddr)	/* Destination address */
{
	int		i;
	SK_U16		Length;
	SK_MBUF		*pMb;
	SK_RLMT_PACKET	*pPacket;

#ifdef DEBUG
	SK_U8	CheckSrc  = 0;
	SK_U8	CheckDest = 0;
	
	for (i = 0; i < SK_MAC_ADDR_LEN; ++i) {
		CheckSrc  |= SrcAddr->a[i];
		CheckDest |= DestAddr->a[i];
	}

	if ((CheckSrc == 0) || (CheckDest == 0)) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_ERR,
			("SkRlmtBuildPacket: Invalid %s%saddr.\n",
			 (CheckSrc == 0 ? "Src" : ""), (CheckDest == 0 ? "Dest" : "")))
	}
#endif

	if ((pMb = SkDrvAllocRlmtMbuf(pAC, IoC, SK_RLMT_MAX_PACKET_SIZE)) != NULL) {
		pPacket = (SK_RLMT_PACKET*)pMb->pData;
		for (i = 0; i < SK_MAC_ADDR_LEN; i++) {
			pPacket->DstAddr[i] = DestAddr->a[i];
			pPacket->SrcAddr[i] = SrcAddr->a[i];
		}
		pPacket->DSap = SK_RLMT_DSAP;
		pPacket->SSap = SK_RLMT_SSAP;
		pPacket->Ctrl = SK_RLMT_CTRL;
		pPacket->Indicator[0] = SK_RLMT_INDICATOR0;
		pPacket->Indicator[1] = SK_RLMT_INDICATOR1;
		pPacket->Indicator[2] = SK_RLMT_INDICATOR2;
		pPacket->Indicator[3] = SK_RLMT_INDICATOR3;
		pPacket->Indicator[4] = SK_RLMT_INDICATOR4;
		pPacket->Indicator[5] = SK_RLMT_INDICATOR5;
		pPacket->Indicator[6] = SK_RLMT_INDICATOR6;

		SK_U16_TO_NETWORK_ORDER(PacketType, &pPacket->RlmtPacketType[0]);

		for (i = 0; i < 4; i++) {
			pPacket->Random[i] = pAC->Rlmt.Port[PortNumber].Random[i];
		}
		
		SK_U16_TO_NETWORK_ORDER(
			SK_RLMT_PACKET_VERSION, &pPacket->RlmtPacketVersion[0]);

		for (i = 0; i < SK_PACKET_DATA_LEN; i++) {
			pPacket->Data[i] = 0x00;
		}

		Length = SK_RLMT_MAX_PACKET_SIZE;	/* Or smaller. */
		pMb->Length = Length;
		pMb->PortIdx = PortNumber;
		Length -= 14;
		SK_U16_TO_NETWORK_ORDER(Length, &pPacket->TypeLen[0]);

		if (PacketType == SK_PACKET_ALIVE) {
			pAC->Rlmt.Port[PortNumber].TxHelloCts++;
		}
	}

	return (pMb);
}	/* SkRlmtBuildPacket */


/******************************************************************************
 *
 *	SkRlmtBuildSpanningTreePacket - build spanning tree check packet
 *
 * Description:
 *	This routine sets up a BPDU packet for spanning tree check.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	NULL or pointer to RLMT mbuf
 */
RLMT_STATIC SK_MBUF	*SkRlmtBuildSpanningTreePacket(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* I/O Context */
SK_U32	PortNumber)	/* Sending port */
{
	unsigned			i;
	SK_U16				Length;
	SK_MBUF				*pMb;
	SK_SPTREE_PACKET	*pSPacket;

	if ((pMb = SkDrvAllocRlmtMbuf(pAC, IoC, SK_RLMT_MAX_PACKET_SIZE)) !=
		NULL) {
		pSPacket = (SK_SPTREE_PACKET*)pMb->pData;
		for (i = 0; i < SK_MAC_ADDR_LEN; i++) {
			pSPacket->DstAddr[i] = BridgeMcAddr.a[i];
			pSPacket->SrcAddr[i] =
				pAC->Addr.Port[PortNumber].CurrentMacAddress.a[i];
		}
		pSPacket->DSap = SK_RLMT_SPT_DSAP;
		pSPacket->SSap = SK_RLMT_SPT_SSAP;
		pSPacket->Ctrl = SK_RLMT_SPT_CTRL;

		pSPacket->ProtocolId[0] = SK_RLMT_SPT_PROTOCOL_ID0;
		pSPacket->ProtocolId[1] = SK_RLMT_SPT_PROTOCOL_ID1;
		pSPacket->ProtocolVersionId = SK_RLMT_SPT_PROTOCOL_VERSION_ID;
		pSPacket->BpduType = SK_RLMT_SPT_BPDU_TYPE;
		pSPacket->Flags = SK_RLMT_SPT_FLAGS;
		pSPacket->RootId[0] = SK_RLMT_SPT_ROOT_ID0;
		pSPacket->RootId[1] = SK_RLMT_SPT_ROOT_ID1;
		pSPacket->RootPathCost[0] = SK_RLMT_SPT_ROOT_PATH_COST0;
		pSPacket->RootPathCost[1] = SK_RLMT_SPT_ROOT_PATH_COST1;
		pSPacket->RootPathCost[2] = SK_RLMT_SPT_ROOT_PATH_COST2;
		pSPacket->RootPathCost[3] = SK_RLMT_SPT_ROOT_PATH_COST3;
		pSPacket->BridgeId[0] = SK_RLMT_SPT_BRIDGE_ID0;
		pSPacket->BridgeId[1] = SK_RLMT_SPT_BRIDGE_ID1;

		/*
		 * Use logical MAC address as bridge ID and filter these packets
		 * on receive.
		 */
		for (i = 0; i < SK_MAC_ADDR_LEN; i++) {
			pSPacket->BridgeId[i + 2] = pSPacket->RootId[i + 2] =
				pAC->Addr.Net[pAC->Rlmt.Port[PortNumber].Net->NetNumber].
					CurrentMacAddress.a[i];
		}
		pSPacket->PortId[0] = SK_RLMT_SPT_PORT_ID0;
		pSPacket->PortId[1] = SK_RLMT_SPT_PORT_ID1;
		pSPacket->MessageAge[0] = SK_RLMT_SPT_MSG_AGE0;
		pSPacket->MessageAge[1] = SK_RLMT_SPT_MSG_AGE1;
		pSPacket->MaxAge[0] = SK_RLMT_SPT_MAX_AGE0;
		pSPacket->MaxAge[1] = SK_RLMT_SPT_MAX_AGE1;
		pSPacket->HelloTime[0] = SK_RLMT_SPT_HELLO_TIME0;
		pSPacket->HelloTime[1] = SK_RLMT_SPT_HELLO_TIME1;
		pSPacket->ForwardDelay[0] = SK_RLMT_SPT_FWD_DELAY0;
		pSPacket->ForwardDelay[1] = SK_RLMT_SPT_FWD_DELAY1;

		Length = SK_RLMT_MAX_PACKET_SIZE;	/* Or smaller. */
		pMb->Length = Length;
		pMb->PortIdx = PortNumber;
		Length -= 14;
		SK_U16_TO_NETWORK_ORDER(Length, &pSPacket->TypeLen[0]);

		pAC->Rlmt.Port[PortNumber].TxSpHelloReqCts++;
	}

	return (pMb);
}	/* SkRlmtBuildSpanningTreePacket */


/******************************************************************************
 *
 *	SkRlmtSend - build and send check packets
 *
 * Description:
 *	Depending on the RLMT state and the checking state, several packets
 *	are sent through the indicated port.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing.
 */
RLMT_STATIC void	SkRlmtSend(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* I/O Context */
SK_U32	PortNumber)	/* Sending port */
{
	unsigned	j;
	SK_EVPARA	Para;
	SK_RLMT_PORT	*pRPort;

	pRPort = &pAC->Rlmt.Port[PortNumber];
	if (pAC->Rlmt.Port[PortNumber].Net->RlmtMode & SK_RLMT_CHECK_LOC_LINK) {
		if (pRPort->CheckingState & (SK_RLMT_PCS_TX | SK_RLMT_PCS_RX)) {
			/* Port is suspicious. Send the RLMT packet to the RLMT mc addr. */
			if ((Para.pParaPtr = SkRlmtBuildPacket(pAC, IoC, PortNumber,
				SK_PACKET_ALIVE, &pAC->Addr.Port[PortNumber].CurrentMacAddress,
				&SkRlmtMcAddr)) != NULL) {
				SkEventQueue(pAC, SKGE_DRV, SK_DRV_RLMT_SEND, Para);
			}
		}
		else {
			/*
			 * Send a directed RLMT packet to all ports that are
			 * checked by the indicated port.
			 */
			for (j = 0; j < pRPort->PortsChecked; j++) {
				if ((Para.pParaPtr = SkRlmtBuildPacket(pAC, IoC, PortNumber,
					SK_PACKET_ALIVE, &pAC->Addr.Port[PortNumber].CurrentMacAddress,
					&pRPort->PortCheck[j].CheckAddr)) != NULL) {
					SkEventQueue(pAC, SKGE_DRV, SK_DRV_RLMT_SEND, Para);
				}
			}
		}
	}

	if ((pAC->Rlmt.Port[PortNumber].Net->RlmtMode & SK_RLMT_CHECK_SEG) &&
		(pAC->Rlmt.Port[PortNumber].Net->CheckingState & SK_RLMT_RCS_SEND_SEG)) {
		/*
		 * Send a BPDU packet to make a connected switch tell us
		 * the correct root bridge.
		 */
		if ((Para.pParaPtr =
			SkRlmtBuildSpanningTreePacket(pAC, IoC, PortNumber)) != NULL) {
			pAC->Rlmt.Port[PortNumber].Net->CheckingState &= ~SK_RLMT_RCS_SEND_SEG;
			pRPort->RootIdSet = SK_FALSE;

			SkEventQueue(pAC, SKGE_DRV, SK_DRV_RLMT_SEND, Para);
			SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_TX,
				("SkRlmtSend: BPDU Packet on Port %u.\n", PortNumber))
		}
	}
	return;
}	/* SkRlmtSend */


/******************************************************************************
 *
 *	SkRlmtPortReceives - check if port is (going) down and bring it up
 *
 * Description:
 *	This routine checks if a port who received a non-BPDU packet
 *	needs to go up or needs to be stopped going down.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing.
 */
RLMT_STATIC void	SkRlmtPortReceives(
SK_AC	*pAC,			/* Adapter Context */
SK_IOC	IoC,			/* I/O Context */
SK_U32	PortNumber)		/* Port to check */
{
	SK_RLMT_PORT	*pRPort;
	SK_EVPARA		Para;

	pRPort = &pAC->Rlmt.Port[PortNumber];
	pRPort->PortNoRx = SK_FALSE;

	if ((pRPort->PortState == SK_RLMT_PS_DOWN) &&
		!(pRPort->CheckingState & SK_RLMT_PCS_TX)) {
		/*
		 * Port is marked down (rx), but received a non-BPDU packet.
		 * Bring it up.
		 */
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
			("SkRlmtPacketReceive: Received on PortDown.\n"))

		pRPort->PortState = SK_RLMT_PS_GOING_UP;
		pRPort->GuTimeStamp = SkOsGetTime(pAC);
		Para.Para32[0] = PortNumber;
		Para.Para32[1] = (SK_U32)-1;
		SkTimerStart(pAC, IoC, &pRPort->UpTimer, SK_RLMT_PORTUP_TIM_VAL,
			SKGE_RLMT, SK_RLMT_PORTUP_TIM, Para);
		pRPort->CheckingState &= ~SK_RLMT_PCS_RX;
		/* pAC->Rlmt.CheckSwitch = SK_TRUE; */
		SkRlmtCheckSwitch(pAC, IoC, pRPort->Net->NetNumber);
	}	/* PortDown && !SuspectTx */
	else if (pRPort->CheckingState & SK_RLMT_PCS_RX) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
			("SkRlmtPacketReceive: Stop bringing port down.\n"))
		SkTimerStop(pAC, IoC, &pRPort->DownRxTimer);
		pRPort->CheckingState &= ~SK_RLMT_PCS_RX;
		/* pAC->Rlmt.CheckSwitch = SK_TRUE; */
		SkRlmtCheckSwitch(pAC, IoC, pRPort->Net->NetNumber);
	}	/* PortGoingDown */

	return;
}	/* SkRlmtPortReceives */


/******************************************************************************
 *
 *	SkRlmtPacketReceive - receive a packet for closer examination
 *
 * Description:
 *	This routine examines a packet more closely than SK_RLMT_LOOKAHEAD.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing.
 */
RLMT_STATIC void	SkRlmtPacketReceive(
SK_AC	*pAC,	/* Adapter Context */
SK_IOC	IoC,	/* I/O Context */
SK_MBUF	*pMb)	/* Received packet */
{
#ifdef xDEBUG
	extern	void DumpData(char *p, int size);
#endif	/* DEBUG */
	int					i;
	unsigned			j;
	SK_U16				PacketType;
	SK_U32				PortNumber;
	SK_ADDR_PORT		*pAPort;
	SK_RLMT_PORT		*pRPort;
	SK_RLMT_PACKET		*pRPacket;
	SK_SPTREE_PACKET	*pSPacket;
	SK_EVPARA			Para;

	PortNumber	= pMb->PortIdx;
	pAPort = &pAC->Addr.Port[PortNumber];
	pRPort = &pAC->Rlmt.Port[PortNumber];

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
		("SkRlmtPacketReceive: PortNumber == %d.\n", PortNumber))

	pRPacket = (SK_RLMT_PACKET*)pMb->pData;
	pSPacket = (SK_SPTREE_PACKET*)pRPacket;

#ifdef xDEBUG
	DumpData((char *)pRPacket, 32);
#endif	/* DEBUG */

	if ((pRPort->PacketsPerTimeSlot - pRPort->BpduPacketsPerTimeSlot) != 0) {
		SkRlmtPortReceives(pAC, IoC, PortNumber);
	}
	
	/* Check destination address. */

	if (!SK_ADDR_EQUAL(pAPort->CurrentMacAddress.a, pRPacket->DstAddr) &&
		!SK_ADDR_EQUAL(SkRlmtMcAddr.a, pRPacket->DstAddr) &&
		!SK_ADDR_EQUAL(BridgeMcAddr.a, pRPacket->DstAddr)) {

		/* Not sent to current MAC or registered MC address => Trash it. */
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
			("SkRlmtPacketReceive: Not for me.\n"))

		SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
		return;
	}
	else if (SK_ADDR_EQUAL(pAPort->CurrentMacAddress.a, pRPacket->SrcAddr)) {

		/*
		 * Was sent by same port (may happen during port switching
		 * or in case of duplicate MAC addresses).
		 */

		/*
		 * Check for duplicate address here:
		 * If Packet.Random != My.Random => DupAddr.
		 */
		for (i = 3; i >= 0; i--) {
			if (pRPort->Random[i] != pRPacket->Random[i]) {
				break;
			}
		}

		/*
		 * CAUTION: Do not check for duplicate MAC address in RLMT Alive Reply
		 * packets (they have the LLC_COMMAND_RESPONSE_BIT set in
		 * pRPacket->SSap).
		 */
		if (i >= 0 && pRPacket->DSap == SK_RLMT_DSAP &&
			pRPacket->Ctrl == SK_RLMT_CTRL &&
			pRPacket->SSap == SK_RLMT_SSAP &&
			pRPacket->Indicator[0] == SK_RLMT_INDICATOR0 &&
			pRPacket->Indicator[1] == SK_RLMT_INDICATOR1 &&
			pRPacket->Indicator[2] == SK_RLMT_INDICATOR2 &&
			pRPacket->Indicator[3] == SK_RLMT_INDICATOR3 &&
			pRPacket->Indicator[4] == SK_RLMT_INDICATOR4 &&
			pRPacket->Indicator[5] == SK_RLMT_INDICATOR5 &&
			pRPacket->Indicator[6] == SK_RLMT_INDICATOR6) {
			SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
				("SkRlmtPacketReceive: Duplicate MAC Address.\n"))

			/* Error Log entry. */
			SK_ERR_LOG(pAC, SK_ERRCL_COMM, SKERR_RLMT_E006, SKERR_RLMT_E006_MSG);
		}
		else {
			/* Simply trash it. */
			SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
				("SkRlmtPacketReceive: Sent by me.\n"))
		}

		SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
		return;
	}

	/* Check SuspectTx entries. */
	if (pRPort->PortsSuspect > 0) {
		for (j = 0; j < pRPort->PortsChecked; j++) {
			if (pRPort->PortCheck[j].SuspectTx &&
				SK_ADDR_EQUAL(
					pRPacket->SrcAddr, pRPort->PortCheck[j].CheckAddr.a)) {
				pRPort->PortCheck[j].SuspectTx = SK_FALSE;
				pRPort->PortsSuspect--;
				break;
			}
		}
	}

	/* Determine type of packet. */
	if (pRPacket->DSap == SK_RLMT_DSAP &&
		pRPacket->Ctrl == SK_RLMT_CTRL &&
		(pRPacket->SSap & ~LLC_COMMAND_RESPONSE_BIT) == SK_RLMT_SSAP &&
		pRPacket->Indicator[0] == SK_RLMT_INDICATOR0 &&
		pRPacket->Indicator[1] == SK_RLMT_INDICATOR1 &&
		pRPacket->Indicator[2] == SK_RLMT_INDICATOR2 &&
		pRPacket->Indicator[3] == SK_RLMT_INDICATOR3 &&
		pRPacket->Indicator[4] == SK_RLMT_INDICATOR4 &&
		pRPacket->Indicator[5] == SK_RLMT_INDICATOR5 &&
		pRPacket->Indicator[6] == SK_RLMT_INDICATOR6) {

		/* It's an RLMT packet. */
		PacketType = (SK_U16)((pRPacket->RlmtPacketType[0] << 8) |
			pRPacket->RlmtPacketType[1]);

		switch (PacketType) {
		case SK_PACKET_ANNOUNCE:	/* Not yet used. */
#if 0
			/* Build the check chain. */
			SkRlmtBuildCheckChain(pAC);
#endif	/* 0 */

			SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
				("SkRlmtPacketReceive: Announce.\n"))

			SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
			break;

		case SK_PACKET_ALIVE:
			if (pRPacket->SSap & LLC_COMMAND_RESPONSE_BIT) {
				SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
					("SkRlmtPacketReceive: Alive Reply.\n"))

				if (!(pAC->Addr.Port[PortNumber].PromMode & SK_PROM_MODE_LLC) ||
					SK_ADDR_EQUAL(
						pRPacket->DstAddr, pAPort->CurrentMacAddress.a)) {
					/* Obviously we could send something. */
					if (pRPort->CheckingState & SK_RLMT_PCS_TX) {
						pRPort->CheckingState &=  ~SK_RLMT_PCS_TX;
						SkTimerStop(pAC, IoC, &pRPort->DownTxTimer);
					}

					if ((pRPort->PortState == SK_RLMT_PS_DOWN) &&
						!(pRPort->CheckingState & SK_RLMT_PCS_RX)) {
						pRPort->PortState = SK_RLMT_PS_GOING_UP;
						pRPort->GuTimeStamp = SkOsGetTime(pAC);

						SkTimerStop(pAC, IoC, &pRPort->DownTxTimer);

						Para.Para32[0] = PortNumber;
						Para.Para32[1] = (SK_U32)-1;
						SkTimerStart(pAC, IoC, &pRPort->UpTimer,
							SK_RLMT_PORTUP_TIM_VAL, SKGE_RLMT,
							SK_RLMT_PORTUP_TIM, Para);
					}
				}

				/* Mark sending port as alive? */
				SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
			}
			else {	/* Alive Request Packet. */
				SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
					("SkRlmtPacketReceive: Alive Request.\n"))

				pRPort->RxHelloCts++;

				/* Answer. */
				for (i = 0; i < SK_MAC_ADDR_LEN; i++) {
					pRPacket->DstAddr[i] = pRPacket->SrcAddr[i];
					pRPacket->SrcAddr[i] =
						pAC->Addr.Port[PortNumber].CurrentMacAddress.a[i];
				}
				pRPacket->SSap |= LLC_COMMAND_RESPONSE_BIT;

				Para.pParaPtr = pMb;
				SkEventQueue(pAC, SKGE_DRV, SK_DRV_RLMT_SEND, Para);
			}
			break;

		case SK_PACKET_CHECK_TX:
			SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
				("SkRlmtPacketReceive: Check your tx line.\n"))

			/* A port checking us requests us to check our tx line. */
			pRPort->CheckingState |= SK_RLMT_PCS_TX;

			/* Start PortDownTx timer. */
			Para.Para32[0] = PortNumber;
			Para.Para32[1] = (SK_U32)-1;
			SkTimerStart(pAC, IoC, &pRPort->DownTxTimer,
				SK_RLMT_PORTDOWN_TIM_VAL, SKGE_RLMT,
				SK_RLMT_PORTDOWN_TX_TIM, Para);

			SkDrvFreeRlmtMbuf(pAC, IoC, pMb);

			if ((Para.pParaPtr = SkRlmtBuildPacket(pAC, IoC, PortNumber,
				SK_PACKET_ALIVE, &pAC->Addr.Port[PortNumber].CurrentMacAddress,
				&SkRlmtMcAddr)) != NULL) {
				SkEventQueue(pAC, SKGE_DRV, SK_DRV_RLMT_SEND, Para);
			}
			break;

		case SK_PACKET_ADDR_CHANGED:
			SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
				("SkRlmtPacketReceive: Address Change.\n"))

			/* Build the check chain. */
			SkRlmtBuildCheckChain(pAC, pRPort->Net->NetNumber);
			SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
			break;

		default:
			SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
				("SkRlmtPacketReceive: Unknown RLMT packet.\n"))

			/* RA;:;: ??? */
			SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
		}
	}
	else if (pSPacket->DSap == SK_RLMT_SPT_DSAP &&
		pSPacket->Ctrl == SK_RLMT_SPT_CTRL &&
		(pSPacket->SSap & ~LLC_COMMAND_RESPONSE_BIT) == SK_RLMT_SPT_SSAP) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
			("SkRlmtPacketReceive: BPDU Packet.\n"))

		/* Spanning Tree packet. */
		pRPort->RxSpHelloCts++;

		if (!SK_ADDR_EQUAL(&pSPacket->RootId[2], &pAC->Addr.Net[pAC->Rlmt.
			Port[PortNumber].Net->NetNumber].CurrentMacAddress.a[0])) {
			/*
			 * Check segmentation if a new root bridge is set and
			 * the segmentation check is not currently running.
			 */
			if (!SK_ADDR_EQUAL(&pSPacket->RootId[2], &pRPort->Root.Id[2]) &&
				(pAC->Rlmt.Port[PortNumber].Net->LinksUp > 1) &&
				(pAC->Rlmt.Port[PortNumber].Net->RlmtMode & SK_RLMT_CHECK_SEG)
				!= 0 && (pAC->Rlmt.Port[PortNumber].Net->CheckingState &
				SK_RLMT_RCS_SEG) == 0) {
				pAC->Rlmt.Port[PortNumber].Net->CheckingState |=
					SK_RLMT_RCS_START_SEG | SK_RLMT_RCS_SEND_SEG;
			}

			/* Store tree view of this port. */
			for (i = 0; i < 8; i++) {
				pRPort->Root.Id[i] = pSPacket->RootId[i];
			}
			pRPort->RootIdSet = SK_TRUE;

			SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_DUMP,
				("Root ID %d: %02x %02x %02x %02x %02x %02x %02x %02x.\n",
					PortNumber,
					pRPort->Root.Id[0], pRPort->Root.Id[1],
					pRPort->Root.Id[2], pRPort->Root.Id[3],
					pRPort->Root.Id[4], pRPort->Root.Id[5],
					pRPort->Root.Id[6], pRPort->Root.Id[7]))
		}

		SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
		if ((pAC->Rlmt.Port[PortNumber].Net->CheckingState &
			SK_RLMT_RCS_REPORT_SEG) != 0) {
			SkRlmtCheckSeg(pAC, IoC, pAC->Rlmt.Port[PortNumber].Net->NetNumber);
		}
	}
	else {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_RX,
			("SkRlmtPacketReceive: Unknown Packet Type.\n"))

		/* Unknown packet. */
		SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
	}
	return;
}	/* SkRlmtPacketReceive */


/******************************************************************************
 *
 *	SkRlmtCheckPort - check if a port works
 *
 * Description:
 *	This routine checks if a port whose link is up received something
 *	and if it seems to transmit successfully.
 *
 *	# PortState: PsInit, PsLinkDown, PsDown, PsGoingUp, PsUp
 *	# PortCheckingState (Bitfield): ChkTx, ChkRx, ChkSeg
 *	# RlmtCheckingState (Bitfield): ChkSeg, StartChkSeg, ReportSeg
 *
 *	if (Rx - RxBpdu == 0) {	# No rx.
 *		if (state == PsUp) {
 *			PortCheckingState |= ChkRx
 *		}
 *		if (ModeCheckSeg && (Timeout ==
 *			TO_SHORTEN(RLMT_DEFAULT_TIMEOUT))) {
 *			RlmtCheckingState |= ChkSeg)
 *			PortCheckingState |= ChkSeg
 *		}
 *		NewTimeout = TO_SHORTEN(Timeout)
 *		if (NewTimeout < RLMT_MIN_TIMEOUT) {
 *			NewTimeout = RLMT_MIN_TIMEOUT
 *			PortState = PsDown
 *			...
 *		}
 *	}
 *	else {	# something was received
 *		# Set counter to 0 at LinkDown?
 *		#   No - rx may be reported after LinkDown ???
 *		PortCheckingState &= ~ChkRx
 *		NewTimeout = RLMT_DEFAULT_TIMEOUT
 *		if (RxAck == 0) {
 *			possible reasons:
 *			is my tx line bad? --
 *				send RLMT multicast and report
 *				back internally? (only possible
 *				between ports on same adapter)
 *		}
 *		if (RxChk == 0) {
 *			possible reasons:
 *			- tx line of port set to check me
 *			  maybe bad
 *			- no other port/adapter available or set
 *			  to check me
 *			- adapter checking me has a longer
 *			  timeout
 *			??? anything that can be done here?
 *		}
 *	}
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	New timeout value.
 */
RLMT_STATIC SK_U32	SkRlmtCheckPort(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* I/O Context */
SK_U32	PortNumber)	/* Port to check */
{
	unsigned		i;
	SK_U32			NewTimeout;
	SK_RLMT_PORT	*pRPort;
	SK_EVPARA		Para;

	pRPort = &pAC->Rlmt.Port[PortNumber];

	if ((pRPort->PacketsPerTimeSlot - pRPort->BpduPacketsPerTimeSlot) == 0) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SkRlmtCheckPort %d: No (%d) receives in last time slot.\n",
				PortNumber, pRPort->PacketsPerTimeSlot))

		/*
		 * Check segmentation if there was no receive at least twice
		 * in a row (PortNoRx is already set) and the segmentation
		 * check is not currently running.
		 */

		if (pRPort->PortNoRx && (pAC->Rlmt.Port[PortNumber].Net->LinksUp > 1) &&
			(pAC->Rlmt.Port[PortNumber].Net->RlmtMode & SK_RLMT_CHECK_SEG) &&
			!(pAC->Rlmt.Port[PortNumber].Net->CheckingState & SK_RLMT_RCS_SEG)) {
			pAC->Rlmt.Port[PortNumber].Net->CheckingState |=
				SK_RLMT_RCS_START_SEG | SK_RLMT_RCS_SEND_SEG;
		}

		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SkRlmtCheckPort: PortsSuspect %d, PcsRx %d.\n",
				pRPort->PortsSuspect, pRPort->CheckingState & SK_RLMT_PCS_RX))

		if (pRPort->PortState != SK_RLMT_PS_DOWN) {
			NewTimeout = TO_SHORTEN(pAC->Rlmt.Port[PortNumber].Net->TimeoutValue);
			if (NewTimeout < SK_RLMT_MIN_TO_VAL) {
				NewTimeout = SK_RLMT_MIN_TO_VAL;
			}

			if (!(pRPort->CheckingState & SK_RLMT_PCS_RX)) {
				Para.Para32[0] = PortNumber;
				pRPort->CheckingState |= SK_RLMT_PCS_RX;

				/*
				 * What shall we do if the port checked by this one receives
				 * our request frames?  What's bad - our rx line or his tx line?
				 */
				Para.Para32[1] = (SK_U32)-1;
				SkTimerStart(pAC, IoC, &pRPort->DownRxTimer,
					SK_RLMT_PORTDOWN_TIM_VAL, SKGE_RLMT,
					SK_RLMT_PORTDOWN_RX_TIM, Para);

				for (i = 0; i < pRPort->PortsChecked; i++) {
					if (pRPort->PortCheck[i].SuspectTx) {
						continue;
					}
					pRPort->PortCheck[i].SuspectTx = SK_TRUE;
					pRPort->PortsSuspect++;
					if ((Para.pParaPtr =
						SkRlmtBuildPacket(pAC, IoC, PortNumber, SK_PACKET_CHECK_TX,
							&pAC->Addr.Port[PortNumber].CurrentMacAddress,
							&pRPort->PortCheck[i].CheckAddr)) != NULL) {
						SkEventQueue(pAC, SKGE_DRV, SK_DRV_RLMT_SEND, Para);
					}
				}
			}
		}
		else {	/* PortDown -- or all partners suspect. */
			NewTimeout = SK_RLMT_DEF_TO_VAL;
		}
		pRPort->PortNoRx = SK_TRUE;
	}
	else {	/* A non-BPDU packet was received. */
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SkRlmtCheckPort %d: %d (%d) receives in last time slot.\n",
				PortNumber,
				pRPort->PacketsPerTimeSlot - pRPort->BpduPacketsPerTimeSlot,
				pRPort->PacketsPerTimeSlot))
		
		SkRlmtPortReceives(pAC, IoC, PortNumber);
		if (pAC->Rlmt.CheckSwitch) {
			SkRlmtCheckSwitch(pAC, IoC, pRPort->Net->NetNumber);
		}

		NewTimeout = SK_RLMT_DEF_TO_VAL;
	}

	return (NewTimeout);
}	/* SkRlmtCheckPort */


/******************************************************************************
 *
 *	SkRlmtSelectBcRx - select new active port, criteria 1 (CLP)
 *
 * Description:
 *	This routine selects the port that received a broadcast frame
 *	substantially later than all other ports.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	SK_BOOL
 */
RLMT_STATIC SK_BOOL	SkRlmtSelectBcRx(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* I/O Context */
SK_U32	Active,		/* Active port */
SK_U32	PrefPort,	/* Preferred port */
SK_U32	*pSelect)	/* New active port */
{
	SK_U64		BcTimeStamp;
	SK_U32		i;
	SK_BOOL		PortFound;

	BcTimeStamp = 0;	/* Not totally necessary, but feeling better. */
	PortFound = SK_FALSE;
	
	/* Select port with the latest TimeStamp. */
	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {

		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("TimeStamp Port %d (Down: %d, NoRx: %d): %08x %08x.\n",
				i,
   				pAC->Rlmt.Port[i].PortDown, pAC->Rlmt.Port[i].PortNoRx,
				*((SK_U32*)(&pAC->Rlmt.Port[i].BcTimeStamp) + OFFS_HI32),
				*((SK_U32*)(&pAC->Rlmt.Port[i].BcTimeStamp) + OFFS_LO32)))

		if (!pAC->Rlmt.Port[i].PortDown && !pAC->Rlmt.Port[i].PortNoRx) {
			if (!PortFound || pAC->Rlmt.Port[i].BcTimeStamp > BcTimeStamp) {
				BcTimeStamp = pAC->Rlmt.Port[i].BcTimeStamp;
				*pSelect = i;
				PortFound = SK_TRUE;
			}
		}
	}

	if (PortFound) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Port %d received the last broadcast.\n", *pSelect))

		/* Look if another port's time stamp is similar. */
		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			if (i == *pSelect) {
				continue;
			}
			if (!pAC->Rlmt.Port[i].PortDown && !pAC->Rlmt.Port[i].PortNoRx &&
				(pAC->Rlmt.Port[i].BcTimeStamp >
				 BcTimeStamp - SK_RLMT_BC_DELTA ||
				pAC->Rlmt.Port[i].BcTimeStamp +
				 SK_RLMT_BC_DELTA > BcTimeStamp)) {
				PortFound = SK_FALSE;
				
				SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
					("Port %d received a broadcast at a similar time.\n", i))
				break;
			}
		}
	}

#ifdef DEBUG
	if (PortFound) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_SELECT_BCRX found Port %d receiving the substantially "
			 "latest broadcast (%u).\n",
				*pSelect,
				BcTimeStamp - pAC->Rlmt.Port[1 - *pSelect].BcTimeStamp))
	}
#endif	/* DEBUG */

	return (PortFound);
}	/* SkRlmtSelectBcRx */


/******************************************************************************
 *
 *	SkRlmtSelectNotSuspect - select new active port, criteria 2 (CLP)
 *
 * Description:
 *	This routine selects a good port (it is PortUp && !SuspectRx).
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	SK_BOOL
 */
RLMT_STATIC SK_BOOL	SkRlmtSelectNotSuspect(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* I/O Context */
SK_U32	Active,		/* Active port */
SK_U32	PrefPort,	/* Preferred port */
SK_U32	*pSelect)	/* New active port */
{
	SK_U32		i;
	SK_BOOL		PortFound;

	PortFound = SK_FALSE;

	/* Select first port that is PortUp && !SuspectRx. */
	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		if (!pAC->Rlmt.Port[i].PortDown &&
			!(pAC->Rlmt.Port[i].CheckingState & SK_RLMT_PCS_RX)) {
			*pSelect = i;
			if (!pAC->Rlmt.Port[Active].PortDown &&
				!(pAC->Rlmt.Port[Active].CheckingState & SK_RLMT_PCS_RX)) {
				*pSelect = Active;
			}
			if (!pAC->Rlmt.Port[PrefPort].PortDown &&
				!(pAC->Rlmt.Port[PrefPort].CheckingState & SK_RLMT_PCS_RX)) {
				*pSelect = PrefPort;
			}
			PortFound = SK_TRUE;
			SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
				("SK_RLMT_SELECT_NOTSUSPECT found Port %d up and not check RX.\n",
					*pSelect))
			break;
		}
	}
	return (PortFound);
}	/* SkRlmtSelectNotSuspect */


/******************************************************************************
 *
 *	SkRlmtSelectUp - select new active port, criteria 3, 4 (CLP)
 *
 * Description:
 *	This routine selects a port that is up.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	SK_BOOL
 */
RLMT_STATIC SK_BOOL	SkRlmtSelectUp(
SK_AC	*pAC,			/* Adapter Context */
SK_IOC	IoC,			/* I/O Context */
SK_U32	Active,			/* Active port */
SK_U32	PrefPort,		/* Preferred port */
SK_U32	*pSelect,		/* New active port */
SK_BOOL	AutoNegDone)	/* Successfully auto-negotiated? */
{
	SK_U32		i;
	SK_BOOL		PortFound;

	PortFound = SK_FALSE;

	/* Select first port that is PortUp. */
	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		if (pAC->Rlmt.Port[i].PortState == SK_RLMT_PS_UP &&
			pAC->GIni.GP[i].PAutoNegFail != AutoNegDone) {
			*pSelect = i;
			if (pAC->Rlmt.Port[Active].PortState == SK_RLMT_PS_UP &&
				pAC->GIni.GP[Active].PAutoNegFail != AutoNegDone) {
				*pSelect = Active;
			}
			if (pAC->Rlmt.Port[PrefPort].PortState == SK_RLMT_PS_UP &&
				pAC->GIni.GP[PrefPort].PAutoNegFail != AutoNegDone) {
				*pSelect = PrefPort;
			}
			PortFound = SK_TRUE;
			SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
				("SK_RLMT_SELECT_UP found Port %d up.\n", *pSelect))
			break;
		}
	}
	return (PortFound);
}	/* SkRlmtSelectUp */


/******************************************************************************
 *
 *	SkRlmtSelectGoingUp - select new active port, criteria 5, 6 (CLP)
 *
 * Description:
 *	This routine selects the port that is going up for the longest time.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	SK_BOOL
 */
RLMT_STATIC SK_BOOL	SkRlmtSelectGoingUp(
SK_AC	*pAC,			/* Adapter Context */
SK_IOC	IoC,			/* I/O Context */
SK_U32	Active,			/* Active port */
SK_U32	PrefPort,		/* Preferred port */
SK_U32	*pSelect,		/* New active port */
SK_BOOL	AutoNegDone)	/* Successfully auto-negotiated? */
{
	SK_U64		GuTimeStamp;
	SK_U32		i;
	SK_BOOL		PortFound;

	GuTimeStamp = 0;
	PortFound = SK_FALSE;

	/* Select port that is PortGoingUp for the longest time. */
	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		if (pAC->Rlmt.Port[i].PortState == SK_RLMT_PS_GOING_UP &&
			pAC->GIni.GP[i].PAutoNegFail != AutoNegDone) {
			GuTimeStamp = pAC->Rlmt.Port[i].GuTimeStamp;
			*pSelect = i;
			PortFound = SK_TRUE;
			break;
		}
	}

	if (!PortFound) {
		return (SK_FALSE);
	}

	for (i = *pSelect + 1; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		if (pAC->Rlmt.Port[i].PortState == SK_RLMT_PS_GOING_UP &&
			pAC->Rlmt.Port[i].GuTimeStamp < GuTimeStamp &&
			pAC->GIni.GP[i].PAutoNegFail != AutoNegDone) {
			GuTimeStamp = pAC->Rlmt.Port[i].GuTimeStamp;
			*pSelect = i;
		}
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_SELECT_GOINGUP found Port %d going up.\n", *pSelect))
	return (SK_TRUE);
}	/* SkRlmtSelectGoingUp */


/******************************************************************************
 *
 *	SkRlmtSelectDown - select new active port, criteria 7, 8 (CLP)
 *
 * Description:
 *	This routine selects a port that is down.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	SK_BOOL
 */
RLMT_STATIC SK_BOOL	SkRlmtSelectDown(
SK_AC	*pAC,			/* Adapter Context */
SK_IOC	IoC,			/* I/O Context */
SK_U32	Active,			/* Active port */
SK_U32	PrefPort,		/* Preferred port */
SK_U32	*pSelect,		/* New active port */
SK_BOOL	AutoNegDone)	/* Successfully auto-negotiated? */
{
	SK_U32		i;
	SK_BOOL		PortFound;

	PortFound = SK_FALSE;

	/* Select first port that is PortDown. */
	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		if (pAC->Rlmt.Port[i].PortState == SK_RLMT_PS_DOWN &&
			pAC->GIni.GP[i].PAutoNegFail != AutoNegDone) {
			*pSelect = i;
			if (pAC->Rlmt.Port[Active].PortState == SK_RLMT_PS_DOWN &&
				pAC->GIni.GP[Active].PAutoNegFail != AutoNegDone) {
				*pSelect = Active;
			}
			if (pAC->Rlmt.Port[PrefPort].PortState == SK_RLMT_PS_DOWN &&
				pAC->GIni.GP[PrefPort].PAutoNegFail != AutoNegDone) {
				*pSelect = PrefPort;
			}
			PortFound = SK_TRUE;
			SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
				("SK_RLMT_SELECT_DOWN found Port %d down.\n", *pSelect))
			break;
		}
	}
	return (PortFound);
}	/* SkRlmtSelectDown */


/******************************************************************************
 *
 *	SkRlmtCheckSwitch - select new active port and switch to it
 *
 * Description:
 *	This routine decides which port should be the active one and queues
 *	port switching if necessary.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing.
 */
RLMT_STATIC void	SkRlmtCheckSwitch(
SK_AC	*pAC,	/* Adapter Context */
SK_IOC	IoC,	/* I/O Context */
SK_U32	NetIdx)	/* Net index */
{
	SK_EVPARA	Para;
	SK_U32		Active;
	SK_U32		PrefPort;
	SK_U32		i;
	SK_BOOL		PortFound;

	Active = pAC->Rlmt.Net[NetIdx].ActivePort;	/* Index of active port. */
	PrefPort = pAC->Rlmt.Net[NetIdx].PrefPort;	/* Index of preferred port. */
	PortFound = SK_FALSE;
	pAC->Rlmt.CheckSwitch = SK_FALSE;

#if 0	/* RW 2001/10/18 - active port becomes always prefered one */
	if (pAC->Rlmt.Net[NetIdx].Preference == 0xFFFFFFFF) { /* Automatic */
		/* disable auto-fail back */
		PrefPort = Active;
	}
#endif

	if (pAC->Rlmt.Net[NetIdx].LinksUp == 0) {
		/* Last link went down - shut down the net. */
		pAC->Rlmt.Net[NetIdx].RlmtState = SK_RLMT_RS_NET_DOWN;
		Para.Para32[0] = SK_RLMT_NET_DOWN_TEMP;
		Para.Para32[1] = NetIdx;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_NET_DOWN, Para);

		Para.Para32[0] = pAC->Rlmt.Net[NetIdx].
			Port[pAC->Rlmt.Net[NetIdx].ActivePort]->PortNumber;
		Para.Para32[1] = NetIdx;
		SkEventQueue(pAC, SKGE_PNMI, SK_PNMI_EVT_RLMT_ACTIVE_DOWN, Para);
		return;
	}	/* pAC->Rlmt.LinksUp == 0 */
	else if (pAC->Rlmt.Net[NetIdx].LinksUp == 1 &&
		pAC->Rlmt.Net[NetIdx].RlmtState == SK_RLMT_RS_NET_DOWN) {
		/* First link came up - get the net up. */
		pAC->Rlmt.Net[NetIdx].RlmtState = SK_RLMT_RS_NET_UP;

		/*
		 * If pAC->Rlmt.ActivePort != Para.Para32[0],
		 * the DRV switches to the port that came up.
		 */
		for (i = 0; i < pAC->Rlmt.Net[NetIdx].NumPorts; i++) {
			if (!pAC->Rlmt.Net[NetIdx].Port[i]->LinkDown) {
				if (!pAC->Rlmt.Net[NetIdx].Port[Active]->LinkDown) {
					i = Active;
				}
				if (!pAC->Rlmt.Net[NetIdx].Port[PrefPort]->LinkDown) {
					i = PrefPort;
				}
				PortFound = SK_TRUE;
				break;
			}
		}

		if (PortFound) {
			Para.Para32[0] = pAC->Rlmt.Net[NetIdx].Port[i]->PortNumber;
			Para.Para32[1] = NetIdx;
			SkEventQueue(pAC, SKGE_PNMI, SK_PNMI_EVT_RLMT_ACTIVE_UP, Para);

			pAC->Rlmt.Net[NetIdx].ActivePort = i;
			Para.Para32[0] = pAC->Rlmt.Net[NetIdx].Port[i]->PortNumber;
			Para.Para32[1] = NetIdx;
			SkEventQueue(pAC, SKGE_DRV, SK_DRV_NET_UP, Para);

			if ((pAC->Rlmt.Net[NetIdx].RlmtMode & SK_RLMT_TRANSPARENT) == 0 &&
				(Para.pParaPtr = SkRlmtBuildPacket(pAC, IoC,
				pAC->Rlmt.Net[NetIdx].Port[i]->PortNumber,
				SK_PACKET_ANNOUNCE, &pAC->Addr.Net[NetIdx].
				CurrentMacAddress, &SkRlmtMcAddr)) != NULL) {
				/*
				 * Send announce packet to RLMT multicast address to force
				 * switches to learn the new location of the logical MAC address.
				 */
				SkEventQueue(pAC, SKGE_DRV, SK_DRV_RLMT_SEND, Para);
			}
		}
		else {
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_RLMT_E007, SKERR_RLMT_E007_MSG);
		}

		return;
	}	/* LinksUp == 1 && RlmtState == SK_RLMT_RS_NET_DOWN */
	else {	/* Cannot be reached in dual-net mode. */
		Para.Para32[0] = Active;

		/*
		 * Preselection:
		 *	If RLMT Mode != CheckLinkState
		 *		select port that received a broadcast frame substantially later
		 *		than all other ports
		 *	else select first port that is not SuspectRx
		 *	else select first port that is PortUp
		 *	else select port that is PortGoingUp for the longest time
		 *	else select first port that is PortDown
		 *	else stop.
		 *
		 * For the preselected port:
		 *	If ActivePort is equal in quality, select ActivePort.
		 *
		 *	If PrefPort is equal in quality, select PrefPort.
		 *
		 *	If ActivePort != SelectedPort,
		 *		If old ActivePort is LinkDown,
		 *			SwitchHard
		 *		else
		 *			SwitchSoft
		 */
		/* check of ChgBcPrio flag added */
		if ((pAC->Rlmt.Net[0].RlmtMode != SK_RLMT_MODE_CLS) &&
			(!pAC->Rlmt.Net[0].ChgBcPrio)) {
			
			if (!PortFound) {
				PortFound = SkRlmtSelectBcRx(
					pAC, IoC, Active, PrefPort, &Para.Para32[1]);
			}

			if (!PortFound) {
				PortFound = SkRlmtSelectNotSuspect(
					pAC, IoC, Active, PrefPort, &Para.Para32[1]);
			}
		}	/* pAC->Rlmt.RlmtMode != SK_RLMT_MODE_CLS */

		/* with changed priority for last broadcast received */
		if ((pAC->Rlmt.Net[0].RlmtMode != SK_RLMT_MODE_CLS) &&
			(pAC->Rlmt.Net[0].ChgBcPrio)) {
			if (!PortFound) {
				PortFound = SkRlmtSelectNotSuspect(
					pAC, IoC, Active, PrefPort, &Para.Para32[1]);
			}

			if (!PortFound) {
				PortFound = SkRlmtSelectBcRx(
					pAC, IoC, Active, PrefPort, &Para.Para32[1]);
			}
		}	/* pAC->Rlmt.RlmtMode != SK_RLMT_MODE_CLS */

		if (!PortFound) {
			PortFound = SkRlmtSelectUp(
				pAC, IoC, Active, PrefPort, &Para.Para32[1], AUTONEG_SUCCESS);
		}

		if (!PortFound) {
			PortFound = SkRlmtSelectUp(
				pAC, IoC, Active, PrefPort, &Para.Para32[1], AUTONEG_FAILED);
		}

		if (!PortFound) {
			PortFound = SkRlmtSelectGoingUp(
				pAC, IoC, Active, PrefPort, &Para.Para32[1], AUTONEG_SUCCESS);
		}

		if (!PortFound) {
			PortFound = SkRlmtSelectGoingUp(
				pAC, IoC, Active, PrefPort, &Para.Para32[1], AUTONEG_FAILED);
		}

		if (pAC->Rlmt.Net[0].RlmtMode != SK_RLMT_MODE_CLS) {
			if (!PortFound) {
				PortFound = SkRlmtSelectDown(pAC, IoC,
					Active, PrefPort, &Para.Para32[1], AUTONEG_SUCCESS);
			}

			if (!PortFound) {
				PortFound = SkRlmtSelectDown(pAC, IoC,
					Active, PrefPort, &Para.Para32[1], AUTONEG_FAILED);
			}
		}	/* pAC->Rlmt.RlmtMode != SK_RLMT_MODE_CLS */

		if (PortFound) {

			if (Para.Para32[1] != Active) {
				SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
					("Active: %d, Para1: %d.\n", Active, Para.Para32[1]))
				pAC->Rlmt.Net[NetIdx].ActivePort = Para.Para32[1];
				Para.Para32[0] = pAC->Rlmt.Net[NetIdx].
					Port[Para.Para32[0]]->PortNumber;
				Para.Para32[1] = pAC->Rlmt.Net[NetIdx].
					Port[Para.Para32[1]]->PortNumber;
				SK_HWAC_LINK_LED(pAC, IoC, Para.Para32[1], SK_LED_ACTIVE);
				if (pAC->Rlmt.Port[Active].LinkDown) {
					SkEventQueue(pAC, SKGE_DRV, SK_DRV_SWITCH_HARD, Para);
				}
				else {
					SK_HWAC_LINK_LED(pAC, IoC, Para.Para32[0], SK_LED_STANDBY);
					SkEventQueue(pAC, SKGE_DRV, SK_DRV_SWITCH_SOFT, Para);
				}
				Para.Para32[1] = NetIdx;
				Para.Para32[0] =
					pAC->Rlmt.Net[NetIdx].Port[Para.Para32[0]]->PortNumber;
				SkEventQueue(pAC, SKGE_PNMI, SK_PNMI_EVT_RLMT_ACTIVE_DOWN, Para);
				Para.Para32[0] = pAC->Rlmt.Net[NetIdx].
					Port[pAC->Rlmt.Net[NetIdx].ActivePort]->PortNumber;
				SkEventQueue(pAC, SKGE_PNMI, SK_PNMI_EVT_RLMT_ACTIVE_UP, Para);
				if ((pAC->Rlmt.Net[NetIdx].RlmtMode & SK_RLMT_TRANSPARENT) == 0 &&
					(Para.pParaPtr = SkRlmtBuildPacket(pAC, IoC, Para.Para32[0],
					SK_PACKET_ANNOUNCE, &pAC->Addr.Net[NetIdx].CurrentMacAddress,
					&SkRlmtMcAddr)) != NULL) {
					/*
					 * Send announce packet to RLMT multicast address to force
					 * switches to learn the new location of the logical
					 * MAC address.
					 */
					SkEventQueue(pAC, SKGE_DRV, SK_DRV_RLMT_SEND, Para);
				}	/* (Para.pParaPtr = SkRlmtBuildPacket(...)) != NULL */
			}	/* Para.Para32[1] != Active */
		}	/* PortFound */
		else {
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_RLMT_E004, SKERR_RLMT_E004_MSG);
		}
	}	/* LinksUp > 1 || LinksUp == 1 && RlmtState != SK_RLMT_RS_NET_DOWN */
	return;
}	/* SkRlmtCheckSwitch */


/******************************************************************************
 *
 *	SkRlmtCheckSeg - Report if segmentation is detected
 *
 * Description:
 *	This routine checks if the ports see different root bridges and reports
 *	segmentation in such a case.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing.
 */
RLMT_STATIC void	SkRlmtCheckSeg(
SK_AC	*pAC,	/* Adapter Context */
SK_IOC	IoC,	/* I/O Context */
SK_U32	NetIdx)	/* Net number */
{
	SK_EVPARA	Para;
	SK_RLMT_NET	*pNet;
	SK_U32		i, j;
	SK_BOOL		Equal;

	pNet = &pAC->Rlmt.Net[NetIdx];
	pNet->RootIdSet = SK_FALSE;
	Equal = SK_TRUE;

	for (i = 0; i < pNet->NumPorts; i++) {
		if (pNet->Port[i]->LinkDown || !pNet->Port[i]->RootIdSet) {
			continue;
		}

		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_DUMP,
			("Root ID %d: %02x %02x %02x %02x %02x %02x %02x %02x.\n", i,
				pNet->Port[i]->Root.Id[0], pNet->Port[i]->Root.Id[1],
				pNet->Port[i]->Root.Id[2], pNet->Port[i]->Root.Id[3],
				pNet->Port[i]->Root.Id[4], pNet->Port[i]->Root.Id[5],
				pNet->Port[i]->Root.Id[6], pNet->Port[i]->Root.Id[7]))

		if (!pNet->RootIdSet) {
			pNet->Root = pNet->Port[i]->Root;
			pNet->RootIdSet = SK_TRUE;
			continue;
		}

		for (j = 0; j < 8; j ++) {
			Equal &= pNet->Port[i]->Root.Id[j] == pNet->Root.Id[j];
			if (!Equal) {
				break;
			}
		}
		
		if (!Equal) {
			SK_ERR_LOG(pAC, SK_ERRCL_COMM, SKERR_RLMT_E005, SKERR_RLMT_E005_MSG);
			Para.Para32[0] = NetIdx;
			Para.Para32[1] = (SK_U32)-1;
			SkEventQueue(pAC, SKGE_PNMI, SK_PNMI_EVT_RLMT_SEGMENTATION, Para);

			pNet->CheckingState &= ~SK_RLMT_RCS_REPORT_SEG;

			/* 2000-03-06 RA: New. */
			Para.Para32[0] = NetIdx;
			Para.Para32[1] = (SK_U32)-1;
			SkTimerStart(pAC, IoC, &pNet->SegTimer, SK_RLMT_SEG_TO_VAL,
				SKGE_RLMT, SK_RLMT_SEG_TIM, Para);
			break;
		}
	}	/* for (i = 0; i < pNet->NumPorts; i++) */

	/* 2000-03-06 RA: Moved here. */
	/* Segmentation check not running anymore. */
	pNet->CheckingState &= ~SK_RLMT_RCS_SEG;

}	/* SkRlmtCheckSeg */


/******************************************************************************
 *
 *	SkRlmtPortStart - initialize port variables and start port
 *
 * Description:
 *	This routine initializes a port's variables and issues a PORT_START
 *	to the HWAC module.  This handles retries if the start fails or the
 *	link eventually goes down.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtPortStart(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* I/O Context */
SK_U32	PortNumber)	/* Port number */
{
	SK_EVPARA	Para;

	pAC->Rlmt.Port[PortNumber].PortState = SK_RLMT_PS_LINK_DOWN;
	pAC->Rlmt.Port[PortNumber].PortStarted = SK_TRUE;
	pAC->Rlmt.Port[PortNumber].LinkDown = SK_TRUE;
	pAC->Rlmt.Port[PortNumber].PortDown = SK_TRUE;
	pAC->Rlmt.Port[PortNumber].CheckingState = 0;
	pAC->Rlmt.Port[PortNumber].RootIdSet = SK_FALSE;
	Para.Para32[0] = PortNumber;
	Para.Para32[1] = (SK_U32)-1;
	SkEventQueue(pAC, SKGE_HWAC, SK_HWEV_PORT_START, Para);
}	/* SkRlmtPortStart */


/******************************************************************************
 *
 *	SkRlmtEvtPortStartTim - PORT_START_TIM
 *
 * Description:
 *	This routine handles PORT_START_TIM events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtPortStartTim(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 PortNumber; SK_U32 -1 */
{
	SK_U32			i;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_PORTSTART_TIMEOUT Port %d Event BEGIN.\n", Para.Para32[0]))

		if (Para.Para32[1] != (SK_U32)-1) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad Parameter.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_PORTSTART_TIMEOUT Event EMPTY.\n"))
		return;
	}

	/*
	 * Used to start non-preferred ports if the preferred one
	 * does not come up.
	 * This timeout needs only be set when starting the first
	 * (preferred) port.
	 */
	if (pAC->Rlmt.Port[Para.Para32[0]].LinkDown) {
		/* PORT_START failed. */
		for (i = 0; i < pAC->Rlmt.Port[Para.Para32[0]].Net->NumPorts; i++) {
			if (!pAC->Rlmt.Port[Para.Para32[0]].Net->Port[i]->PortStarted) {
				SkRlmtPortStart(pAC, IoC,
					pAC->Rlmt.Port[Para.Para32[0]].Net->Port[i]->PortNumber);
			}
		}
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_PORTSTART_TIMEOUT Event END.\n"))
}	/* SkRlmtEvtPortStartTim */


/******************************************************************************
 *
 *	SkRlmtEvtLinkUp - LINK_UP
 *
 * Description:
 *	This routine handles LLINK_UP events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtLinkUp(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 PortNumber; SK_U32 Undefined */
{
	SK_U32			i;
	SK_RLMT_PORT	*pRPort;
	SK_EVPARA		Para2;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_LINK_UP Port %d Event BEGIN.\n", Para.Para32[0]))

	pRPort = &pAC->Rlmt.Port[Para.Para32[0]];
	if (!pRPort->PortStarted) {
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_RLMT_E008, SKERR_RLMT_E008_MSG);

		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
				("SK_RLMT_LINK_UP Event EMPTY.\n"))
		return;
	}

	if (!pRPort->LinkDown) {
		/* RA;:;: Any better solution? */
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_LINK_UP Event EMPTY.\n"))
		return;
	}

	SkTimerStop(pAC, IoC, &pRPort->UpTimer);
	SkTimerStop(pAC, IoC, &pRPort->DownRxTimer);
	SkTimerStop(pAC, IoC, &pRPort->DownTxTimer);

	/* Do something if timer already fired? */

	pRPort->LinkDown = SK_FALSE;
	pRPort->PortState = SK_RLMT_PS_GOING_UP;
	pRPort->GuTimeStamp = SkOsGetTime(pAC);
	pRPort->BcTimeStamp = 0;
	pRPort->Net->LinksUp++;
	if (pRPort->Net->LinksUp == 1) {
		SK_HWAC_LINK_LED(pAC, IoC, Para.Para32[0], SK_LED_ACTIVE);
	}
	else {
		SK_HWAC_LINK_LED(pAC, IoC, Para.Para32[0], SK_LED_STANDBY);
	}

	for (i = 0; i < pRPort->Net->NumPorts; i++) {
		if (!pRPort->Net->Port[i]->PortStarted) {
			SkRlmtPortStart(pAC, IoC, pRPort->Net->Port[i]->PortNumber);
		}
	}

	SkRlmtCheckSwitch(pAC, IoC, pRPort->Net->NetNumber);

	if (pRPort->Net->LinksUp >= 2) {
		if (pRPort->Net->RlmtMode & SK_RLMT_CHECK_LOC_LINK) {
			/* Build the check chain. */
			SkRlmtBuildCheckChain(pAC, pRPort->Net->NetNumber);
		}
	}

	/* If the first link comes up, start the periodical RLMT timeout. */
	if (pRPort->Net->NumPorts > 1 && pRPort->Net->LinksUp == 1 &&
		(pRPort->Net->RlmtMode & SK_RLMT_CHECK_OTHERS) != 0) {
		Para2.Para32[0] = pRPort->Net->NetNumber;
		Para2.Para32[1] = (SK_U32)-1;
		SkTimerStart(pAC, IoC, &pRPort->Net->LocTimer,
			pRPort->Net->TimeoutValue, SKGE_RLMT, SK_RLMT_TIM, Para2);
	}

	Para2 = Para;
	Para2.Para32[1] = (SK_U32)-1;
	SkTimerStart(pAC, IoC, &pRPort->UpTimer, SK_RLMT_PORTUP_TIM_VAL,
		SKGE_RLMT, SK_RLMT_PORTUP_TIM, Para2);
	
	/* Later: if (pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_LOC_LINK) && */
	if ((pRPort->Net->RlmtMode & SK_RLMT_TRANSPARENT) == 0 &&
		(pRPort->Net->RlmtMode & SK_RLMT_CHECK_LINK) != 0 &&
		(Para2.pParaPtr =
			SkRlmtBuildPacket(pAC, IoC, Para.Para32[0], SK_PACKET_ANNOUNCE,
			&pAC->Addr.Port[Para.Para32[0]].CurrentMacAddress, &SkRlmtMcAddr)
		) != NULL) {
		/* Send "new" packet to RLMT multicast address. */
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_RLMT_SEND, Para2);
	}

	if (pRPort->Net->RlmtMode & SK_RLMT_CHECK_SEG) {
		if ((Para2.pParaPtr =
			SkRlmtBuildSpanningTreePacket(pAC, IoC, Para.Para32[0])) != NULL) {
			pAC->Rlmt.Port[Para.Para32[0]].RootIdSet = SK_FALSE;
			pRPort->Net->CheckingState |=
				SK_RLMT_RCS_SEG | SK_RLMT_RCS_REPORT_SEG;

			SkEventQueue(pAC, SKGE_DRV, SK_DRV_RLMT_SEND, Para2);

			Para.Para32[1] = (SK_U32)-1;
			SkTimerStart(pAC, IoC, &pRPort->Net->SegTimer,
				SK_RLMT_SEG_TO_VAL, SKGE_RLMT, SK_RLMT_SEG_TIM, Para);
		}
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_LINK_UP Event END.\n"))
}	/* SkRlmtEvtLinkUp */


/******************************************************************************
 *
 *	SkRlmtEvtPortUpTim - PORT_UP_TIM
 *
 * Description:
 *	This routine handles PORT_UP_TIM events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtPortUpTim(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 PortNumber; SK_U32 -1 */
{
	SK_RLMT_PORT	*pRPort;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_PORTUP_TIM Port %d Event BEGIN.\n", Para.Para32[0]))

	if (Para.Para32[1] != (SK_U32)-1) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad Parameter.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_PORTUP_TIM Event EMPTY.\n"))
		return;
	}

	pRPort = &pAC->Rlmt.Port[Para.Para32[0]];
	if (pRPort->LinkDown || (pRPort->PortState == SK_RLMT_PS_UP)) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_PORTUP_TIM Port %d Event EMPTY.\n", Para.Para32[0]))
		return;
	}

	pRPort->PortDown = SK_FALSE;
	pRPort->PortState = SK_RLMT_PS_UP;
	pRPort->Net->PortsUp++;
	if (pRPort->Net->RlmtState != SK_RLMT_RS_INIT) {
		if (pAC->Rlmt.NumNets <= 1) {
			SkRlmtCheckSwitch(pAC, IoC, pRPort->Net->NetNumber);
		}
		SkEventQueue(pAC, SKGE_PNMI, SK_PNMI_EVT_RLMT_PORT_UP, Para);
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_PORTUP_TIM Event END.\n"))
}	/* SkRlmtEvtPortUpTim */


/******************************************************************************
 *
 *	SkRlmtEvtPortDownTim - PORT_DOWN_*
 *
 * Description:
 *	This routine handles PORT_DOWN_* events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtPortDownX(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_U32		Event,	/* Event code */
SK_EVPARA	Para)	/* SK_U32 PortNumber; SK_U32 -1 */
{
	SK_RLMT_PORT	*pRPort;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_PORTDOWN* Port %d Event (%d) BEGIN.\n",
			Para.Para32[0], Event))

	if (Para.Para32[1] != (SK_U32)-1) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad Parameter.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_PORTDOWN* Event EMPTY.\n"))
		return;
	}

	pRPort = &pAC->Rlmt.Port[Para.Para32[0]];
	if (!pRPort->PortStarted || (Event == SK_RLMT_PORTDOWN_TX_TIM &&
		!(pRPort->CheckingState & SK_RLMT_PCS_TX))) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_PORTDOWN* Event (%d) EMPTY.\n", Event))
		return;
	}
	
	/* Stop port's timers. */
	SkTimerStop(pAC, IoC, &pRPort->UpTimer);
	SkTimerStop(pAC, IoC, &pRPort->DownRxTimer);
	SkTimerStop(pAC, IoC, &pRPort->DownTxTimer);

	if (pRPort->PortState != SK_RLMT_PS_LINK_DOWN) {
		pRPort->PortState = SK_RLMT_PS_DOWN;
	}

	if (!pRPort->PortDown) {
		pRPort->Net->PortsUp--;
		pRPort->PortDown = SK_TRUE;
		SkEventQueue(pAC, SKGE_PNMI, SK_PNMI_EVT_RLMT_PORT_DOWN, Para);
	}

	pRPort->PacketsPerTimeSlot = 0;
	/* pRPort->DataPacketsPerTimeSlot = 0; */
	pRPort->BpduPacketsPerTimeSlot = 0;
	pRPort->BcTimeStamp = 0;

	/*
	 * RA;:;: To be checked:
	 * - actions at RLMT_STOP: We should not switch anymore.
	 */
	if (pRPort->Net->RlmtState != SK_RLMT_RS_INIT) {
		if (Para.Para32[0] ==
			pRPort->Net->Port[pRPort->Net->ActivePort]->PortNumber) {
			/* Active Port went down. */
			SkRlmtCheckSwitch(pAC, IoC, pRPort->Net->NetNumber);
		}
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_PORTDOWN* Event (%d) END.\n", Event))
}	/* SkRlmtEvtPortDownX */


/******************************************************************************
 *
 *	SkRlmtEvtLinkDown - LINK_DOWN
 *
 * Description:
 *	This routine handles LINK_DOWN events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtLinkDown(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 PortNumber; SK_U32 Undefined */
{
	SK_RLMT_PORT	*pRPort;

	pRPort = &pAC->Rlmt.Port[Para.Para32[0]];
	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_LINK_DOWN Port %d Event BEGIN.\n", Para.Para32[0]))

	if (!pAC->Rlmt.Port[Para.Para32[0]].LinkDown) {
		pRPort->Net->LinksUp--;
		pRPort->LinkDown = SK_TRUE;
		pRPort->PortState = SK_RLMT_PS_LINK_DOWN;
		SK_HWAC_LINK_LED(pAC, IoC, Para.Para32[0], SK_LED_OFF);

		if ((pRPort->Net->RlmtMode & SK_RLMT_CHECK_LOC_LINK) != 0) {
			/* Build the check chain. */
			SkRlmtBuildCheckChain(pAC, pRPort->Net->NetNumber);
		}

		/* Ensure that port is marked down. */
		Para.Para32[1] = -1;
		(void)SkRlmtEvent(pAC, IoC, SK_RLMT_PORTDOWN, Para);
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_LINK_DOWN Event END.\n"))
}	/* SkRlmtEvtLinkDown */


/******************************************************************************
 *
 *	SkRlmtEvtPortAddr - PORT_ADDR
 *
 * Description:
 *	This routine handles PORT_ADDR events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtPortAddr(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 PortNumber; SK_U32 -1 */
{
	SK_U32			i, j;
	SK_RLMT_PORT	*pRPort;
	SK_MAC_ADDR		*pOldMacAddr;
	SK_MAC_ADDR		*pNewMacAddr;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_PORT_ADDR Port %d Event BEGIN.\n", Para.Para32[0]))

	if (Para.Para32[1] != (SK_U32)-1) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad Parameter.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_PORT_ADDR Event EMPTY.\n"))
		return;
	}

	/* Port's physical MAC address changed. */
	pOldMacAddr = &pAC->Addr.Port[Para.Para32[0]].PreviousMacAddress;
	pNewMacAddr = &pAC->Addr.Port[Para.Para32[0]].CurrentMacAddress;

	/*
	 * NOTE: This is not scalable for solutions where ports are
	 *	 checked remotely.  There, we need to send an RLMT
	 *	 address change packet - and how do we ensure delivery?
	 */
	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		pRPort = &pAC->Rlmt.Port[i];
		for (j = 0; j < pRPort->PortsChecked; j++) {
			if (SK_ADDR_EQUAL(
				pRPort->PortCheck[j].CheckAddr.a, pOldMacAddr->a)) {
				pRPort->PortCheck[j].CheckAddr = *pNewMacAddr;
			}
		}
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_PORT_ADDR Event END.\n"))
}	/* SkRlmtEvtPortAddr */


/******************************************************************************
 *
 *	SkRlmtEvtStart - START
 *
 * Description:
 *	This routine handles START events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtStart(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 NetNumber; SK_U32 -1 */
{
	SK_EVPARA	Para2;
	SK_U32		PortIdx;
	SK_U32		PortNumber;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_START Net %d Event BEGIN.\n", Para.Para32[0]))

	if (Para.Para32[1] != (SK_U32)-1) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad Parameter.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_START Event EMPTY.\n"))
		return;
	}

	if (Para.Para32[0] >= pAC->Rlmt.NumNets) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad NetNumber %d.\n", Para.Para32[0]))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_START Event EMPTY.\n"))
		return;
	}

	if (pAC->Rlmt.Net[Para.Para32[0]].RlmtState != SK_RLMT_RS_INIT) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_START Event EMPTY.\n"))
		return;
	}

	if (pAC->Rlmt.NetsStarted >= pAC->Rlmt.NumNets) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("All nets should have been started.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_START Event EMPTY.\n"))
		return;
	}

	if (pAC->Rlmt.Net[Para.Para32[0]].PrefPort >=
		pAC->Rlmt.Net[Para.Para32[0]].NumPorts) {
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_RLMT_E009, SKERR_RLMT_E009_MSG);

		/* Change PrefPort to internal default. */
		Para2.Para32[0] = 0xFFFFFFFF;
		Para2.Para32[1] = Para.Para32[0];
		(void)SkRlmtEvent(pAC, IoC, SK_RLMT_PREFPORT_CHANGE, Para2);
	}

	PortIdx = pAC->Rlmt.Net[Para.Para32[0]].PrefPort;
	PortNumber = pAC->Rlmt.Net[Para.Para32[0]].Port[PortIdx]->PortNumber;

	pAC->Rlmt.Net[Para.Para32[0]].LinksUp = 0;
	pAC->Rlmt.Net[Para.Para32[0]].PortsUp = 0;
	pAC->Rlmt.Net[Para.Para32[0]].CheckingState = 0;
	pAC->Rlmt.Net[Para.Para32[0]].RlmtState = SK_RLMT_RS_NET_DOWN;

	/* Start preferred port. */
	SkRlmtPortStart(pAC, IoC, PortNumber);

	/* Start Timer (for first port only). */
	Para2.Para32[0] = PortNumber;
	Para2.Para32[1] = (SK_U32)-1;
	SkTimerStart(pAC, IoC, &pAC->Rlmt.Port[PortNumber].UpTimer,
		SK_RLMT_PORTSTART_TIM_VAL, SKGE_RLMT, SK_RLMT_PORTSTART_TIM, Para2);

	pAC->Rlmt.NetsStarted++;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_START Event END.\n"))
}	/* SkRlmtEvtStart */


/******************************************************************************
 *
 *	SkRlmtEvtStop - STOP
 *
 * Description:
 *	This routine handles STOP events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtStop(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 NetNumber; SK_U32 -1 */
{
	SK_EVPARA	Para2;
	SK_U32		PortNumber;
	SK_U32		i;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_STOP Net %d Event BEGIN.\n", Para.Para32[0]))

	if (Para.Para32[1] != (SK_U32)-1) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad Parameter.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_STOP Event EMPTY.\n"))
		return;
	}

	if (Para.Para32[0] >= pAC->Rlmt.NumNets) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad NetNumber %d.\n", Para.Para32[0]))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_STOP Event EMPTY.\n"))
		return;
	}

	if (pAC->Rlmt.Net[Para.Para32[0]].RlmtState == SK_RLMT_RS_INIT) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_STOP Event EMPTY.\n"))
		return;
	}

	if (pAC->Rlmt.NetsStarted == 0) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("All nets are stopped.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_STOP Event EMPTY.\n"))
		return;
	}

	/* Stop RLMT timers. */
	SkTimerStop(pAC, IoC, &pAC->Rlmt.Net[Para.Para32[0]].LocTimer);
	SkTimerStop(pAC, IoC, &pAC->Rlmt.Net[Para.Para32[0]].SegTimer);

	/* Stop net. */
	pAC->Rlmt.Net[Para.Para32[0]].RlmtState = SK_RLMT_RS_INIT;
	pAC->Rlmt.Net[Para.Para32[0]].RootIdSet = SK_FALSE;
	Para2.Para32[0] = SK_RLMT_NET_DOWN_FINAL;
	Para2.Para32[1] = Para.Para32[0];			/* Net# */
	SkEventQueue(pAC, SKGE_DRV, SK_DRV_NET_DOWN, Para2);

	/* Stop ports. */
	for (i = 0; i < pAC->Rlmt.Net[Para.Para32[0]].NumPorts; i++) {
		PortNumber = pAC->Rlmt.Net[Para.Para32[0]].Port[i]->PortNumber;
		if (pAC->Rlmt.Port[PortNumber].PortState != SK_RLMT_PS_INIT) {
			SkTimerStop(pAC, IoC, &pAC->Rlmt.Port[PortNumber].UpTimer);
			SkTimerStop(pAC, IoC, &pAC->Rlmt.Port[PortNumber].DownRxTimer);
			SkTimerStop(pAC, IoC, &pAC->Rlmt.Port[PortNumber].DownTxTimer);

			pAC->Rlmt.Port[PortNumber].PortState = SK_RLMT_PS_INIT;
			pAC->Rlmt.Port[PortNumber].RootIdSet = SK_FALSE;
			pAC->Rlmt.Port[PortNumber].PortStarted = SK_FALSE;
			Para2.Para32[0] = PortNumber;
			Para2.Para32[1] = (SK_U32)-1;
			SkEventQueue(pAC, SKGE_HWAC, SK_HWEV_PORT_STOP, Para2);
		}
	}

	pAC->Rlmt.NetsStarted--;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_STOP Event END.\n"))
}	/* SkRlmtEvtStop */


/******************************************************************************
 *
 *	SkRlmtEvtTim - TIM
 *
 * Description:
 *	This routine handles TIM events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtTim(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 NetNumber; SK_U32 -1 */
{
	SK_RLMT_PORT	*pRPort;
	SK_U32			Timeout;
	SK_U32			NewTimeout;
	SK_U32			PortNumber;
	SK_U32			i;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_TIM Event BEGIN.\n"))

	if (Para.Para32[1] != (SK_U32)-1) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad Parameter.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_TIM Event EMPTY.\n"))
		return;
	}

	if ((pAC->Rlmt.Net[Para.Para32[0]].RlmtMode & SK_RLMT_CHECK_OTHERS) == 0 ||
		pAC->Rlmt.Net[Para.Para32[0]].LinksUp == 0) {
		/* Mode changed or all links down: No more link checking. */
		return;
	}

#if 0
	pAC->Rlmt.SwitchCheckCounter--;
	if (pAC->Rlmt.SwitchCheckCounter == 0) {
		pAC->Rlmt.SwitchCheckCounter;
	}
#endif	/* 0 */

	NewTimeout = SK_RLMT_DEF_TO_VAL;
	for (i = 0; i < pAC->Rlmt.Net[Para.Para32[0]].NumPorts; i++) {
		PortNumber = pAC->Rlmt.Net[Para.Para32[0]].Port[i]->PortNumber;
		pRPort = &pAC->Rlmt.Port[PortNumber];
		if (!pRPort->LinkDown) {
			Timeout = SkRlmtCheckPort(pAC, IoC, PortNumber);
			if (Timeout < NewTimeout) {
				NewTimeout = Timeout;
			}

			/*
			 * These counters should be set to 0 for all ports before the
			 * first frame is sent in the next loop.
			 */
			pRPort->PacketsPerTimeSlot = 0;
			/* pRPort->DataPacketsPerTimeSlot = 0; */
			pRPort->BpduPacketsPerTimeSlot = 0;
		}
	}
	pAC->Rlmt.Net[Para.Para32[0]].TimeoutValue = NewTimeout;

	if (pAC->Rlmt.Net[Para.Para32[0]].LinksUp > 1) {
		/*
		 * If checking remote ports, also send packets if
		 *   (LinksUp == 1) &&
		 *   this port checks at least one (remote) port.
		 */

		/*
		 * Must be new loop, as SkRlmtCheckPort can request to
		 * check segmentation when e.g. checking the last port.
		 */
		for (i = 0; i < pAC->Rlmt.Net[Para.Para32[0]].NumPorts; i++) {
			if (!pAC->Rlmt.Net[Para.Para32[0]].Port[i]->LinkDown) {
				SkRlmtSend(pAC, IoC,
					pAC->Rlmt.Net[Para.Para32[0]].Port[i]->PortNumber);
			}
		}
	}

	SkTimerStart(pAC, IoC, &pAC->Rlmt.Net[Para.Para32[0]].LocTimer,
		pAC->Rlmt.Net[Para.Para32[0]].TimeoutValue, SKGE_RLMT, SK_RLMT_TIM,
		Para);

	if (pAC->Rlmt.Net[Para.Para32[0]].LinksUp > 1 &&
		(pAC->Rlmt.Net[Para.Para32[0]].RlmtMode & SK_RLMT_CHECK_SEG) &&
		(pAC->Rlmt.Net[Para.Para32[0]].CheckingState & SK_RLMT_RCS_START_SEG)) {
		SkTimerStart(pAC, IoC, &pAC->Rlmt.Net[Para.Para32[0]].SegTimer,
			SK_RLMT_SEG_TO_VAL, SKGE_RLMT, SK_RLMT_SEG_TIM, Para);
		pAC->Rlmt.Net[Para.Para32[0]].CheckingState &= ~SK_RLMT_RCS_START_SEG;
		pAC->Rlmt.Net[Para.Para32[0]].CheckingState |=
			SK_RLMT_RCS_SEG | SK_RLMT_RCS_REPORT_SEG;
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_TIM Event END.\n"))
}	/* SkRlmtEvtTim */


/******************************************************************************
 *
 *	SkRlmtEvtSegTim - SEG_TIM
 *
 * Description:
 *	This routine handles SEG_TIM events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtSegTim(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 NetNumber; SK_U32 -1 */
{
#ifdef xDEBUG
	int j;
#endif	/* DEBUG */

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_SEG_TIM Event BEGIN.\n"))

	if (Para.Para32[1] != (SK_U32)-1) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad Parameter.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_SEG_TIM Event EMPTY.\n"))
		return;
	}

#ifdef xDEBUG
	for (j = 0; j < pAC->Rlmt.Net[Para.Para32[0]].NumPorts; j++) {
		SK_ADDR_PORT	*pAPort;
		SK_U32			k;
		SK_U16			*InAddr;
		SK_U8			InAddr8[6];

		InAddr = (SK_U16 *)&InAddr8[0];
		pAPort = pAC->Rlmt.Net[Para.Para32[0]].Port[j]->AddrPort;
		for (k = 0; k < pAPort->NextExactMatchRlmt; k++) {
			/* Get exact match address k from port j. */
			XM_INADDR(IoC, pAC->Rlmt.Net[Para.Para32[0]].Port[j]->PortNumber,
				XM_EXM(k), InAddr);
			SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
				("MC address %d on Port %u: %02x %02x %02x %02x %02x %02x --  %02x %02x %02x %02x %02x %02x.\n",
					k, pAC->Rlmt.Net[Para.Para32[0]].Port[j]->PortNumber,
					InAddr8[0], InAddr8[1], InAddr8[2],
					InAddr8[3], InAddr8[4], InAddr8[5],
					pAPort->Exact[k].a[0], pAPort->Exact[k].a[1],
					pAPort->Exact[k].a[2], pAPort->Exact[k].a[3],
					pAPort->Exact[k].a[4], pAPort->Exact[k].a[5]))
		}
	}
#endif	/* xDEBUG */
				
	SkRlmtCheckSeg(pAC, IoC, Para.Para32[0]);

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_SEG_TIM Event END.\n"))
}	/* SkRlmtEvtSegTim */


/******************************************************************************
 *
 *	SkRlmtEvtPacketRx - PACKET_RECEIVED
 *
 * Description:
 *	This routine handles PACKET_RECEIVED events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtPacketRx(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_MBUF *pMb */
{
	SK_MBUF	*pMb;
	SK_MBUF	*pNextMb;
	SK_U32	NetNumber;

	
	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_PACKET_RECEIVED Event BEGIN.\n"))

	/* Should we ignore frames during port switching? */

#ifdef DEBUG
	pMb = Para.pParaPtr;
	if (pMb == NULL) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL, ("No mbuf.\n"))
	}
	else if (pMb->pNext != NULL) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("More than one mbuf or pMb->pNext not set.\n"))
	}
#endif	/* DEBUG */

	for (pMb = Para.pParaPtr; pMb != NULL; pMb = pNextMb) {
		pNextMb = pMb->pNext;
		pMb->pNext = NULL;

		NetNumber = pAC->Rlmt.Port[pMb->PortIdx].Net->NetNumber;
		if (pAC->Rlmt.Net[NetNumber].RlmtState == SK_RLMT_RS_INIT) {
			SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
		}
		else {
			SkRlmtPacketReceive(pAC, IoC, pMb);
		}
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_PACKET_RECEIVED Event END.\n"))
}	/* SkRlmtEvtPacketRx */


/******************************************************************************
 *
 *	SkRlmtEvtStatsClear - STATS_CLEAR
 *
 * Description:
 *	This routine handles STATS_CLEAR events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtStatsClear(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 NetNumber; SK_U32 -1 */
{
	SK_U32			i;
	SK_RLMT_PORT	*pRPort;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_STATS_CLEAR Event BEGIN.\n"))

	if (Para.Para32[1] != (SK_U32)-1) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad Parameter.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_STATS_CLEAR Event EMPTY.\n"))
		return;
	}

	if (Para.Para32[0] >= pAC->Rlmt.NumNets) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad NetNumber %d.\n", Para.Para32[0]))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_STATS_CLEAR Event EMPTY.\n"))
		return;
	}

	/* Clear statistics for logical and physical ports. */
	for (i = 0; i < pAC->Rlmt.Net[Para.Para32[0]].NumPorts; i++) {
		pRPort =
			&pAC->Rlmt.Port[pAC->Rlmt.Net[Para.Para32[0]].Port[i]->PortNumber];
		pRPort->TxHelloCts = 0;
		pRPort->RxHelloCts = 0;
		pRPort->TxSpHelloReqCts = 0;
		pRPort->RxSpHelloCts = 0;
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_STATS_CLEAR Event END.\n"))
}	/* SkRlmtEvtStatsClear */


/******************************************************************************
 *
 *	SkRlmtEvtStatsUpdate - STATS_UPDATE
 *
 * Description:
 *	This routine handles STATS_UPDATE events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtStatsUpdate(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 NetNumber; SK_U32 -1 */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_STATS_UPDATE Event BEGIN.\n"))

	if (Para.Para32[1] != (SK_U32)-1) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad Parameter.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_STATS_UPDATE Event EMPTY.\n"))
		return;
	}

	if (Para.Para32[0] >= pAC->Rlmt.NumNets) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad NetNumber %d.\n", Para.Para32[0]))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_STATS_UPDATE Event EMPTY.\n"))
		return;
	}

	/* Update statistics - currently always up-to-date. */

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_STATS_UPDATE Event END.\n"))
}	/* SkRlmtEvtStatsUpdate */


/******************************************************************************
 *
 *	SkRlmtEvtPrefportChange - PREFPORT_CHANGE
 *
 * Description:
 *	This routine handles PREFPORT_CHANGE events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtPrefportChange(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 PortIndex; SK_U32 NetNumber */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_PREFPORT_CHANGE to Port %d Event BEGIN.\n", Para.Para32[0]))

	if (Para.Para32[1] >= pAC->Rlmt.NumNets) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad NetNumber %d.\n", Para.Para32[1]))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_PREFPORT_CHANGE Event EMPTY.\n"))
		return;
	}

	/* 0xFFFFFFFF == auto-mode. */
	if (Para.Para32[0] == 0xFFFFFFFF) {
		pAC->Rlmt.Net[Para.Para32[1]].PrefPort = SK_RLMT_DEF_PREF_PORT;
	}
	else {
		if (Para.Para32[0] >= pAC->Rlmt.Net[Para.Para32[1]].NumPorts) {
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_RLMT_E010, SKERR_RLMT_E010_MSG);

			SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
				("SK_RLMT_PREFPORT_CHANGE Event EMPTY.\n"))
			return;
		}

		pAC->Rlmt.Net[Para.Para32[1]].PrefPort = Para.Para32[0];
	}

	pAC->Rlmt.Net[Para.Para32[1]].Preference = Para.Para32[0];

	if (pAC->Rlmt.Net[Para.Para32[1]].RlmtState != SK_RLMT_RS_INIT) {
		SkRlmtCheckSwitch(pAC, IoC, Para.Para32[1]);
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_PREFPORT_CHANGE Event END.\n"))
}	/* SkRlmtEvtPrefportChange */


/******************************************************************************
 *
 *	SkRlmtEvtSetNets - SET_NETS
 *
 * Description:
 *	This routine handles SET_NETS events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtSetNets(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 NumNets; SK_U32 -1 */
{
	int i;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_SET_NETS Event BEGIN.\n"))

	if (Para.Para32[1] != (SK_U32)-1) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad Parameter.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_SET_NETS Event EMPTY.\n"))
		return;
	}

	if (Para.Para32[0] == 0 || Para.Para32[0] > SK_MAX_NETS ||
		Para.Para32[0] > (SK_U32)pAC->GIni.GIMacsFound) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad number of nets: %d.\n", Para.Para32[0]))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_SET_NETS Event EMPTY.\n"))
		return;
	}

	if (Para.Para32[0] == pAC->Rlmt.NumNets) {	/* No change. */
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_SET_NETS Event EMPTY.\n"))
		return;
	}

	/* Entering and leaving dual mode only allowed while nets are stopped. */
	if (pAC->Rlmt.NetsStarted > 0) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Changing dual mode only allowed while all nets are stopped.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_SET_NETS Event EMPTY.\n"))
		return;
	}

	if (Para.Para32[0] == 1) {
		if (pAC->Rlmt.NumNets > 1) {
			/* Clear logical MAC addr from second net's active port. */
			(void)SkAddrOverride(pAC, IoC, pAC->Rlmt.Net[1].Port[pAC->Addr.
				Net[1].ActivePort]->PortNumber, NULL, SK_ADDR_CLEAR_LOGICAL);
			pAC->Rlmt.Net[1].NumPorts = 0;
		}

		pAC->Rlmt.NumNets = Para.Para32[0];
		for (i = 0; (SK_U32)i < pAC->Rlmt.NumNets; i++) {
			pAC->Rlmt.Net[i].RlmtState = SK_RLMT_RS_INIT;
			pAC->Rlmt.Net[i].RootIdSet = SK_FALSE;
			pAC->Rlmt.Net[i].Preference = 0xFFFFFFFF;	  /* "Automatic" */
			pAC->Rlmt.Net[i].PrefPort = SK_RLMT_DEF_PREF_PORT;
			/* Just assuming. */
			pAC->Rlmt.Net[i].ActivePort = pAC->Rlmt.Net[i].PrefPort;
			pAC->Rlmt.Net[i].RlmtMode = SK_RLMT_DEF_MODE;
			pAC->Rlmt.Net[i].TimeoutValue = SK_RLMT_DEF_TO_VAL;
			pAC->Rlmt.Net[i].NetNumber = i;
		}

		pAC->Rlmt.Port[1].Net= &pAC->Rlmt.Net[0];
		pAC->Rlmt.Net[0].NumPorts = pAC->GIni.GIMacsFound;

		SkEventQueue(pAC, SKGE_PNMI, SK_PNMI_EVT_RLMT_SET_NETS, Para);

		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("RLMT: Changed to one net with two ports.\n"))
	}
	else if (Para.Para32[0] == 2) {
		pAC->Rlmt.Port[1].Net= &pAC->Rlmt.Net[1];
		pAC->Rlmt.Net[1].NumPorts = pAC->GIni.GIMacsFound - 1;
		pAC->Rlmt.Net[0].NumPorts =
			pAC->GIni.GIMacsFound - pAC->Rlmt.Net[1].NumPorts;
		
		pAC->Rlmt.NumNets = Para.Para32[0];
		for (i = 0; (SK_U32)i < pAC->Rlmt.NumNets; i++) {
			pAC->Rlmt.Net[i].RlmtState = SK_RLMT_RS_INIT;
			pAC->Rlmt.Net[i].RootIdSet = SK_FALSE;
			pAC->Rlmt.Net[i].Preference = 0xFFFFFFFF;	  /* "Automatic" */
			pAC->Rlmt.Net[i].PrefPort = SK_RLMT_DEF_PREF_PORT;
			/* Just assuming. */
			pAC->Rlmt.Net[i].ActivePort = pAC->Rlmt.Net[i].PrefPort;
			pAC->Rlmt.Net[i].RlmtMode = SK_RLMT_DEF_MODE;
			pAC->Rlmt.Net[i].TimeoutValue = SK_RLMT_DEF_TO_VAL;

			pAC->Rlmt.Net[i].NetNumber = i;
		}

		/* Set logical MAC addr on second net's active port. */
		(void)SkAddrOverride(pAC, IoC, pAC->Rlmt.Net[1].Port[pAC->Addr.
			Net[1].ActivePort]->PortNumber, NULL, SK_ADDR_SET_LOGICAL);

		SkEventQueue(pAC, SKGE_PNMI, SK_PNMI_EVT_RLMT_SET_NETS, Para);

		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("RLMT: Changed to two nets with one port each.\n"))
	}
	else {
		/* Not implemented for more than two nets. */
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SetNets not implemented for more than two nets.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_SET_NETS Event EMPTY.\n"))
		return;
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_SET_NETS Event END.\n"))
}	/* SkRlmtSetNets */


/******************************************************************************
 *
 *	SkRlmtEvtModeChange - MODE_CHANGE
 *
 * Description:
 *	This routine handles MODE_CHANGE events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtEvtModeChange(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_EVPARA	Para)	/* SK_U32 NewMode; SK_U32 NetNumber */
{
	SK_EVPARA	Para2;
	SK_U32		i;
	SK_U32		PrevRlmtMode;

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
		("SK_RLMT_MODE_CHANGE Event BEGIN.\n"))

	if (Para.Para32[1] >= pAC->Rlmt.NumNets) {
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Bad NetNumber %d.\n", Para.Para32[1]))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_MODE_CHANGE Event EMPTY.\n"))
		return;
	}

	Para.Para32[0] |= SK_RLMT_CHECK_LINK;

	if ((pAC->Rlmt.Net[Para.Para32[1]].NumPorts == 1) &&
		Para.Para32[0] != SK_RLMT_MODE_CLS) {
		pAC->Rlmt.Net[Para.Para32[1]].RlmtMode = SK_RLMT_MODE_CLS;
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Forced RLMT mode to CLS on single port net.\n"))
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_MODE_CHANGE Event EMPTY.\n"))
		return;
	}

	/* Update RLMT mode. */
	PrevRlmtMode = pAC->Rlmt.Net[Para.Para32[1]].RlmtMode;
	pAC->Rlmt.Net[Para.Para32[1]].RlmtMode = Para.Para32[0];

	if ((PrevRlmtMode & SK_RLMT_CHECK_LOC_LINK) !=
		(pAC->Rlmt.Net[Para.Para32[1]].RlmtMode & SK_RLMT_CHECK_LOC_LINK)) {
		/* SK_RLMT_CHECK_LOC_LINK bit changed. */
		if ((PrevRlmtMode & SK_RLMT_CHECK_OTHERS) == 0 &&
			pAC->Rlmt.Net[Para.Para32[1]].NumPorts > 1 &&
			pAC->Rlmt.Net[Para.Para32[1]].PortsUp >= 1) {
			/* 20001207 RA: Was "PortsUp == 1". */
			Para2.Para32[0] = Para.Para32[1];
			Para2.Para32[1] = (SK_U32)-1;
			SkTimerStart(pAC, IoC, &pAC->Rlmt.Net[Para.Para32[1]].LocTimer,
				pAC->Rlmt.Net[Para.Para32[1]].TimeoutValue,
				SKGE_RLMT, SK_RLMT_TIM, Para2);
		}
	}

	if ((PrevRlmtMode & SK_RLMT_CHECK_SEG) !=
		(pAC->Rlmt.Net[Para.Para32[1]].RlmtMode & SK_RLMT_CHECK_SEG)) {
		/* SK_RLMT_CHECK_SEG bit changed. */
		for (i = 0; i < pAC->Rlmt.Net[Para.Para32[1]].NumPorts; i++) {
			(void)SkAddrMcClear(pAC, IoC,
				pAC->Rlmt.Net[Para.Para32[1]].Port[i]->PortNumber,
				SK_ADDR_PERMANENT | SK_MC_SW_ONLY);

			/* Add RLMT MC address. */
			(void)SkAddrMcAdd(pAC, IoC,
				pAC->Rlmt.Net[Para.Para32[1]].Port[i]->PortNumber,
				&SkRlmtMcAddr, SK_ADDR_PERMANENT);

			if ((pAC->Rlmt.Net[Para.Para32[1]].RlmtMode &
				SK_RLMT_CHECK_SEG) != 0) {
				/* Add BPDU MC address. */
				(void)SkAddrMcAdd(pAC, IoC,
					pAC->Rlmt.Net[Para.Para32[1]].Port[i]->PortNumber,
					&BridgeMcAddr, SK_ADDR_PERMANENT);

				if (pAC->Rlmt.Net[Para.Para32[1]].RlmtState != SK_RLMT_RS_INIT) {
					if (!pAC->Rlmt.Net[Para.Para32[1]].Port[i]->LinkDown &&
						(Para2.pParaPtr = SkRlmtBuildSpanningTreePacket(
						pAC, IoC, i)) != NULL) {
						pAC->Rlmt.Net[Para.Para32[1]].Port[i]->RootIdSet =
							SK_FALSE;
						SkEventQueue(pAC, SKGE_DRV, SK_DRV_RLMT_SEND, Para2);
					}
				}
			}
			(void)SkAddrMcUpdate(pAC, IoC,
				pAC->Rlmt.Net[Para.Para32[1]].Port[i]->PortNumber);
		}	/* for ... */

		if ((pAC->Rlmt.Net[Para.Para32[1]].RlmtMode & SK_RLMT_CHECK_SEG) != 0) {
			Para2.Para32[0] = Para.Para32[1];
			Para2.Para32[1] = (SK_U32)-1;
			SkTimerStart(pAC, IoC, &pAC->Rlmt.Net[Para.Para32[1]].SegTimer,
				SK_RLMT_SEG_TO_VAL, SKGE_RLMT, SK_RLMT_SEG_TIM, Para2);
		}
	}	/* SK_RLMT_CHECK_SEG bit changed. */

	SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("SK_RLMT_MODE_CHANGE Event END.\n"))
}	/* SkRlmtEvtModeChange */


/******************************************************************************
 *
 *	SkRlmtEvent - a PORT- or an RLMT-specific event happened
 *
 * Description:
 *	This routine calls subroutines to handle PORT- and RLMT-specific events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	0
 */
int	SkRlmtEvent(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context */
SK_U32		Event,	/* Event code */
SK_EVPARA	Para)	/* Event-specific parameter */
{
	switch (Event) {
	
	/* ----- PORT events ----- */

	case SK_RLMT_PORTSTART_TIM:	/* From RLMT via TIME. */
		SkRlmtEvtPortStartTim(pAC, IoC, Para);
		break;
	case SK_RLMT_LINK_UP:		/* From SIRQ. */
		SkRlmtEvtLinkUp(pAC, IoC, Para);
		break;
	case SK_RLMT_PORTUP_TIM:	/* From RLMT via TIME. */
		SkRlmtEvtPortUpTim(pAC, IoC, Para);
		break;
	case SK_RLMT_PORTDOWN:			/* From RLMT. */
	case SK_RLMT_PORTDOWN_RX_TIM:	/* From RLMT via TIME. */
	case SK_RLMT_PORTDOWN_TX_TIM:	/* From RLMT via TIME. */
		SkRlmtEvtPortDownX(pAC, IoC, Event, Para);
		break;
	case SK_RLMT_LINK_DOWN:		/* From SIRQ. */
		SkRlmtEvtLinkDown(pAC, IoC, Para);
		break;
	case SK_RLMT_PORT_ADDR:		/* From ADDR. */
		SkRlmtEvtPortAddr(pAC, IoC, Para);
		break;

	/* ----- RLMT events ----- */

	case SK_RLMT_START:		/* From DRV. */
		SkRlmtEvtStart(pAC, IoC, Para);
		break;
	case SK_RLMT_STOP:		/* From DRV. */
		SkRlmtEvtStop(pAC, IoC, Para);
		break;
	case SK_RLMT_TIM:		/* From RLMT via TIME. */
		SkRlmtEvtTim(pAC, IoC, Para);
		break;
	case SK_RLMT_SEG_TIM:
		SkRlmtEvtSegTim(pAC, IoC, Para);
		break;
	case SK_RLMT_PACKET_RECEIVED:	/* From DRV. */
		SkRlmtEvtPacketRx(pAC, IoC, Para);
		break;
	case SK_RLMT_STATS_CLEAR:	/* From PNMI. */
		SkRlmtEvtStatsClear(pAC, IoC, Para);
		break;
	case SK_RLMT_STATS_UPDATE:	/* From PNMI. */
		SkRlmtEvtStatsUpdate(pAC, IoC, Para);
		break;
	case SK_RLMT_PREFPORT_CHANGE:	/* From PNMI. */
		SkRlmtEvtPrefportChange(pAC, IoC, Para);
		break;
	case SK_RLMT_MODE_CHANGE:	/* From PNMI. */
		SkRlmtEvtModeChange(pAC, IoC, Para);
		break;
	case SK_RLMT_SET_NETS:	/* From DRV. */
		SkRlmtEvtSetNets(pAC, IoC, Para);
		break;

	/* ----- Unknown events ----- */

	default:	/* Create error log entry. */
		SK_DBG_MSG(pAC, SK_DBGMOD_RLMT, SK_DBGCAT_CTRL,
			("Unknown RLMT Event %d.\n", Event))
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_RLMT_E003, SKERR_RLMT_E003_MSG);
		break;
	}	/* switch() */

	return (0);
}	/* SkRlmtEvent */

#ifdef __cplusplus
}
#endif	/* __cplusplus */
