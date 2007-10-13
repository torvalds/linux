/******************************************************************************
 *
 * Name:	skrlmt.h
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.37 $
 * Date:	$Date: 2003/04/15 09:43:43 $
 * Purpose:	Header file for Redundant Link ManagemenT.
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
 * This is the header file for Redundant Link ManagemenT.
 *
 * Include File Hierarchy:
 *
 *	"skdrv1st.h"
 *	...
 *	"sktypes.h"
 *	"skqueue.h"
 *	"skaddr.h"
 *	"skrlmt.h"
 *	...
 *	"skdrv2nd.h"
 *
 ******************************************************************************/

#ifndef __INC_SKRLMT_H
#define __INC_SKRLMT_H

#ifdef __cplusplus
extern "C" {
#endif	/* cplusplus */

/* defines ********************************************************************/

#define	SK_RLMT_NET_DOWN_TEMP	1	/* NET_DOWN due to last port down. */
#define	SK_RLMT_NET_DOWN_FINAL	2	/* NET_DOWN due to RLMT_STOP. */

/* ----- Default queue sizes - must be multiples of 8 KB ----- */

/* Less than 8 KB free in RX queue => pause frames. */
#define SK_RLMT_STANDBY_QRXSIZE	128	/* Size of rx standby queue in KB. */
#define SK_RLMT_STANDBY_QXASIZE	32	/* Size of async standby queue in KB. */
#define SK_RLMT_STANDBY_QXSSIZE	0	/* Size of sync standby queue in KB. */

#define SK_RLMT_MAX_TX_BUF_SIZE	60	/* Maximum RLMT transmit size. */

/* ----- PORT states ----- */

#define SK_RLMT_PS_INIT			0	/* Port state: Init. */
#define SK_RLMT_PS_LINK_DOWN	1	/* Port state: Link down. */
#define SK_RLMT_PS_DOWN			2	/* Port state: Port down. */
#define SK_RLMT_PS_GOING_UP		3	/* Port state: Going up. */
#define SK_RLMT_PS_UP			4	/* Port state: Up. */

/* ----- RLMT states ----- */

#define SK_RLMT_RS_INIT			0	/* RLMT state: Init. */
#define SK_RLMT_RS_NET_DOWN		1	/* RLMT state: Net down. */
#define SK_RLMT_RS_NET_UP		2	/* RLMT state: Net up. */

/* ----- PORT events ----- */

#define SK_RLMT_LINK_UP			1001	/* Link came up. */
#define SK_RLMT_LINK_DOWN		1002	/* Link went down. */
#define SK_RLMT_PORT_ADDR		1003	/* Port address changed. */

/* ----- RLMT events ----- */

#define SK_RLMT_START			2001	/* Start RLMT. */
#define SK_RLMT_STOP			2002	/* Stop RLMT. */
#define SK_RLMT_PACKET_RECEIVED	2003	/* Packet was received for RLMT. */
#define SK_RLMT_STATS_CLEAR		2004	/* Clear statistics. */
#define SK_RLMT_STATS_UPDATE	2005	/* Update statistics. */
#define SK_RLMT_PREFPORT_CHANGE	2006	/* Change preferred port. */
#define SK_RLMT_MODE_CHANGE		2007	/* New RlmtMode. */
#define SK_RLMT_SET_NETS		2008	/* Number of Nets (1 or 2). */

/* ----- RLMT mode bits ----- */

/*
 * CAUTION:	These defines are private to RLMT.
 *			Please use the RLMT mode defines below.
 */

#define SK_RLMT_CHECK_LINK		  1		/* Check Link. */
#define SK_RLMT_CHECK_LOC_LINK	  2		/* Check other link on same adapter. */
#define SK_RLMT_CHECK_SEG		  4		/* Check segmentation. */

#ifndef RLMT_CHECK_REMOTE
#define SK_RLMT_CHECK_OTHERS	SK_RLMT_CHECK_LOC_LINK
#else	/* RLMT_CHECK_REMOTE */
#define SK_RLMT_CHECK_REM_LINK	  8		/* Check link(s) on other adapter(s). */
#define SK_RLMT_MAX_REMOTE_PORTS_CHECKED	3
#define SK_RLMT_CHECK_OTHERS	\
		(SK_RLMT_CHECK_LOC_LINK | SK_RLMT_CHECK_REM_LINK)
#endif	/* RLMT_CHECK_REMOTE */

#ifndef SK_RLMT_ENABLE_TRANSPARENT
#define SK_RLMT_TRANSPARENT		  0		/* RLMT transparent - inactive. */
#else	/* SK_RLMT_ENABLE_TRANSPARENT */
#define SK_RLMT_TRANSPARENT		128		/* RLMT transparent. */
#endif	/* SK_RLMT_ENABLE_TRANSPARENT */

/* ----- RLMT modes ----- */

/* Check Link State. */
#define SK_RLMT_MODE_CLS	(SK_RLMT_CHECK_LINK)

/* Check Local Ports: check other links on the same adapter. */
#define SK_RLMT_MODE_CLP	(SK_RLMT_CHECK_LINK | SK_RLMT_CHECK_LOC_LINK)

/* Check Local Ports and Segmentation Status. */
#define SK_RLMT_MODE_CLPSS	\
		(SK_RLMT_CHECK_LINK | SK_RLMT_CHECK_LOC_LINK | SK_RLMT_CHECK_SEG)

#ifdef RLMT_CHECK_REMOTE
/* Check Local and Remote Ports: check links (local or remote). */
	Name of define TBD!
#define SK_RLMT_MODE_CRP	\
		(SK_RLMT_CHECK_LINK | SK_RLMT_CHECK_LOC_LINK | SK_RLMT_CHECK_REM_LINK)

/* Check Local and Remote Ports and Segmentation Status. */
	Name of define TBD!
#define SK_RLMT_MODE_CRPSS	\
		(SK_RLMT_CHECK_LINK | SK_RLMT_CHECK_LOC_LINK | \
		SK_RLMT_CHECK_REM_LINK | SK_RLMT_CHECK_SEG)
#endif	/* RLMT_CHECK_REMOTE */

/* ----- RLMT lookahead result bits ----- */

#define SK_RLMT_RX_RLMT			1	/* Give packet to RLMT. */
#define SK_RLMT_RX_PROTOCOL		2	/* Give packet to protocol. */

/* Macros */

#if 0
SK_AC		*pAC		/* adapter context */
SK_U32		PortNum		/* receiving port */
unsigned	PktLen		/* received packet's length */
SK_BOOL		IsBc		/* Flag: packet is broadcast */
unsigned	*pOffset	/* offs. of bytes to present to SK_RLMT_LOOKAHEAD */
unsigned	*pNumBytes	/* #Bytes to present to SK_RLMT_LOOKAHEAD */
#endif	/* 0 */

#define SK_RLMT_PRE_LOOKAHEAD(pAC,PortNum,PktLen,IsBc,pOffset,pNumBytes) { \
	SK_AC	*_pAC; \
	SK_U32	_PortNum; \
	_pAC = (pAC); \
	_PortNum = (SK_U32)(PortNum); \
	/* _pAC->Rlmt.Port[_PortNum].PacketsRx++; */ \
	_pAC->Rlmt.Port[_PortNum].PacketsPerTimeSlot++; \
    if (_pAC->Rlmt.RlmtOff) { \
		*(pNumBytes) = 0; \
    } \
    else {\
        if ((_pAC->Rlmt.Port[_PortNum].Net->RlmtMode & SK_RLMT_TRANSPARENT) != 0) { \
    		*(pNumBytes) = 0; \
    	} \
    	else if (IsBc) { \
    		if (_pAC->Rlmt.Port[_PortNum].Net->RlmtMode != SK_RLMT_MODE_CLS) { \
    			*(pNumBytes) = 6; \
    			*(pOffset) = 6; \
    		} \
    		else { \
    			*(pNumBytes) = 0; \
    		} \
    	} \
    	else { \
    		if ((PktLen) > SK_RLMT_MAX_TX_BUF_SIZE) { \
    			/* _pAC->Rlmt.Port[_PortNum].DataPacketsPerTimeSlot++; */ \
    			*(pNumBytes) = 0; \
    		} \
    		else { \
    			*(pNumBytes) = 6; \
    			*(pOffset) = 0; \
    		} \
    	} \
    } \
}

#if 0
SK_AC		*pAC		/* adapter context */
SK_U32		PortNum		/* receiving port */
SK_U8		*pLaPacket,	/* received packet's data (points to pOffset) */
SK_BOOL		IsBc		/* Flag: packet is broadcast */
SK_BOOL		IsMc		/* Flag: packet is multicast */
unsigned	*pForRlmt	/* Result: bits SK_RLMT_RX_RLMT, SK_RLMT_RX_PROTOCOL */
SK_RLMT_LOOKAHEAD() expects *pNumBytes from
packet offset *pOffset (s.a.) at *pLaPacket.

If you use SK_RLMT_LOOKAHEAD in a path where you already know if the packet is
BC, MC, or UC, you should use constants for IsBc and IsMc, so that your compiler
can trash unneeded parts of the if construction.
#endif	/* 0 */

#define SK_RLMT_LOOKAHEAD(pAC,PortNum,pLaPacket,IsBc,IsMc,pForRlmt) { \
	SK_AC	*_pAC; \
	SK_U32	_PortNum; \
	SK_U8	*_pLaPacket; \
	_pAC = (pAC); \
	_PortNum = (SK_U32)(PortNum); \
	_pLaPacket = (SK_U8 *)(pLaPacket); \
	if (IsBc) {\
		if (!SK_ADDR_EQUAL(_pLaPacket, _pAC->Addr.Net[_pAC->Rlmt.Port[ \
			_PortNum].Net->NetNumber].CurrentMacAddress.a)) { \
			_pAC->Rlmt.Port[_PortNum].BcTimeStamp = SkOsGetTime(_pAC); \
			_pAC->Rlmt.CheckSwitch = SK_TRUE; \
		} \
		/* _pAC->Rlmt.Port[_PortNum].DataPacketsPerTimeSlot++; */ \
		*(pForRlmt) = SK_RLMT_RX_PROTOCOL; \
	} \
	else if (IsMc) { \
		if (SK_ADDR_EQUAL(_pLaPacket, BridgeMcAddr.a)) { \
			_pAC->Rlmt.Port[_PortNum].BpduPacketsPerTimeSlot++; \
			if (_pAC->Rlmt.Port[_PortNum].Net->RlmtMode & SK_RLMT_CHECK_SEG) { \
				*(pForRlmt) = SK_RLMT_RX_RLMT | SK_RLMT_RX_PROTOCOL; \
			} \
			else { \
				*(pForRlmt) = SK_RLMT_RX_PROTOCOL; \
			} \
		} \
		else if (SK_ADDR_EQUAL(_pLaPacket, SkRlmtMcAddr.a)) { \
			*(pForRlmt) = SK_RLMT_RX_RLMT; \
		} \
		else { \
			/* _pAC->Rlmt.Port[_PortNum].DataPacketsPerTimeSlot++; */ \
			*(pForRlmt) = SK_RLMT_RX_PROTOCOL; \
		} \
	} \
	else { \
		if (SK_ADDR_EQUAL( \
			_pLaPacket, \
			_pAC->Addr.Port[_PortNum].CurrentMacAddress.a)) { \
			*(pForRlmt) = SK_RLMT_RX_RLMT; \
		} \
		else { \
			/* _pAC->Rlmt.Port[_PortNum].DataPacketsPerTimeSlot++; */ \
			*(pForRlmt) = SK_RLMT_RX_PROTOCOL; \
		} \
	} \
}

#ifdef SK_RLMT_FAST_LOOKAHEAD
Error: SK_RLMT_FAST_LOOKAHEAD no longer used. Use new macros for lookahead.
#endif	/* SK_RLMT_FAST_LOOKAHEAD */
#ifdef SK_RLMT_SLOW_LOOKAHEAD
Error: SK_RLMT_SLOW_LOOKAHEAD no longer used. Use new macros for lookahead.
#endif	/* SK_RLMT_SLOW_LOOKAHEAD */

/* typedefs *******************************************************************/

#ifdef SK_RLMT_MBUF_PRIVATE
typedef struct s_RlmtMbuf {
	some content
} SK_RLMT_MBUF;
#endif	/* SK_RLMT_MBUF_PRIVATE */


#ifdef SK_LA_INFO
typedef struct s_Rlmt_PacketInfo {
	unsigned	PacketLength;			/* Length of packet. */
	unsigned	PacketType;				/* Directed/Multicast/Broadcast. */
} SK_RLMT_PINFO;
#endif	/* SK_LA_INFO */


typedef struct s_RootId {
	SK_U8		Id[8];					/* Root Bridge Id. */
} SK_RLMT_ROOT_ID;


typedef struct s_port {
	SK_MAC_ADDR	CheckAddr;
	SK_BOOL		SuspectTx;
} SK_PORT_CHECK;


typedef struct s_RlmtNet SK_RLMT_NET;


typedef struct s_RlmtPort {

/* ----- Public part (read-only) ----- */

	SK_U8			PortState;				/* Current state of this port. */

	/* For PNMI */
	SK_BOOL			LinkDown;
	SK_BOOL			PortDown;
	SK_U8			Align01;

	SK_U32			PortNumber;				/* Number of port on adapter. */
	SK_RLMT_NET *	Net;					/* Net port belongs to. */

	SK_U64			TxHelloCts;
	SK_U64			RxHelloCts;
	SK_U64			TxSpHelloReqCts;
	SK_U64			RxSpHelloCts;

/* ----- Private part ----- */

/*	SK_U64			PacketsRx; */				/* Total packets received. */
	SK_U32			PacketsPerTimeSlot;		/* Packets rxed between TOs. */
/*	SK_U32			DataPacketsPerTimeSlot; */	/* Data packets ... */
	SK_U32			BpduPacketsPerTimeSlot;	/* BPDU packets rxed in TS. */
	SK_U64			BcTimeStamp;			/* Time of last BC receive. */
	SK_U64			GuTimeStamp;			/* Time of entering GOING_UP. */

	SK_TIMER		UpTimer;				/* Timer struct Link/Port up. */
	SK_TIMER		DownRxTimer;			/* Timer struct down rx. */
	SK_TIMER		DownTxTimer;			/* Timer struct down tx. */

	SK_U32			CheckingState;			/* Checking State. */

	SK_ADDR_PORT *	AddrPort;

	SK_U8			Random[4];				/* Random value. */
	unsigned		PortsChecked;			/* #ports checked. */
	unsigned		PortsSuspect;			/* #ports checked that are s. */
	SK_PORT_CHECK	PortCheck[1];
/*	SK_PORT_CHECK	PortCheck[SK_MAX_MACS - 1]; */

	SK_BOOL			PortStarted;			/* Port is started. */
	SK_BOOL			PortNoRx;				/* NoRx for >= 1 time slot. */
	SK_BOOL			RootIdSet;
	SK_RLMT_ROOT_ID	Root;					/* Root Bridge Id. */
} SK_RLMT_PORT;


struct s_RlmtNet {

/* ----- Public part (read-only) ----- */

	SK_U32			NetNumber;			/* Number of net. */

	SK_RLMT_PORT *	Port[SK_MAX_MACS];	/* Ports that belong to this net. */
	SK_U32			NumPorts;			/* Number of ports. */
	SK_U32			PrefPort;			/* Preferred port. */

	/* For PNMI */

	SK_U32			ChgBcPrio;			/* Change Priority of last broadcast received */
	SK_U32			RlmtMode;			/* Check ... */
	SK_U32			ActivePort;			/* Active port. */
	SK_U32			Preference;		/* 0xFFFFFFFF: Automatic. */

	SK_U8			RlmtState;			/* Current RLMT state. */

/* ----- Private part ----- */
	SK_BOOL			RootIdSet;
	SK_U16			Align01;

	int				LinksUp;			/* #Links up. */
	int				PortsUp;			/* #Ports up. */
	SK_U32			TimeoutValue;		/* RLMT timeout value. */

	SK_U32			CheckingState;		/* Checking State. */
	SK_RLMT_ROOT_ID	Root;				/* Root Bridge Id. */

	SK_TIMER		LocTimer;			/* Timer struct. */
	SK_TIMER		SegTimer;			/* Timer struct. */
};


typedef struct s_Rlmt {

/* ----- Public part (read-only) ----- */

	SK_U32			NumNets;			/* Number of nets. */
	SK_U32			NetsStarted;		/* Number of nets started. */
	SK_RLMT_NET		Net[SK_MAX_NETS];	/* Array of available nets. */
	SK_RLMT_PORT	Port[SK_MAX_MACS];	/* Array of available ports. */

/* ----- Private part ----- */
	SK_BOOL			CheckSwitch;
	SK_BOOL			RlmtOff;            /* set to zero if the Mac addresses 
                                           are equal or the second one 
                                           is zero */
	SK_U16			Align01;

} SK_RLMT;


extern	SK_MAC_ADDR	BridgeMcAddr;
extern	SK_MAC_ADDR	SkRlmtMcAddr;

/* function prototypes ********************************************************/


#ifndef SK_KR_PROTO

/* Functions provided by SkRlmt */

/* ANSI/C++ compliant function prototypes */

extern	void	SkRlmtInit(
	SK_AC	*pAC,
	SK_IOC	IoC,
	int		Level);

extern	int	SkRlmtEvent(
	SK_AC		*pAC,
	SK_IOC		IoC,
	SK_U32		Event,
	SK_EVPARA	Para);

#else	/* defined(SK_KR_PROTO) */

/* Non-ANSI/C++ compliant function prototypes */

#error KR-style function prototypes are not yet provided.

#endif	/* defined(SK_KR_PROTO)) */


#ifdef __cplusplus
}
#endif	/* __cplusplus */

#endif	/* __INC_SKRLMT_H */
