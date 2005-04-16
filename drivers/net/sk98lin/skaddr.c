/******************************************************************************
 *
 * Name:	skaddr.c
 * Project:	Gigabit Ethernet Adapters, ADDR-Module
 * Version:	$Revision: 1.52 $
 * Date:	$Date: 2003/06/02 13:46:15 $
 * Purpose:	Manage Addresses (Multicast and Unicast) and Promiscuous Mode.
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
 * This module is intended to manage multicast addresses, address override,
 * and promiscuous mode on GEnesis and Yukon adapters.
 *
 * Address Layout:
 *	port address:		physical MAC address
 *	1st exact match:	logical MAC address (GEnesis only)
 *	2nd exact match:	RLMT multicast (GEnesis only)
 *	exact match 3-13:	OS-specific multicasts (GEnesis only)
 *
 * Include File Hierarchy:
 *
 *	"skdrv1st.h"
 *	"skdrv2nd.h"
 *
 ******************************************************************************/

#if (defined(DEBUG) || ((!defined(LINT)) && (!defined(SK_SLIM))))
static const char SysKonnectFileId[] =
	"@(#) $Id: skaddr.c,v 1.52 2003/06/02 13:46:15 tschilli Exp $ (C) Marvell.";
#endif /* DEBUG ||!LINT || !SK_SLIM */

#define __SKADDR_C

#ifdef __cplusplus
extern "C" {
#endif	/* cplusplus */

#include "h/skdrv1st.h"
#include "h/skdrv2nd.h"

/* defines ********************************************************************/


#define XMAC_POLY	0xEDB88320UL	/* CRC32-Poly - XMAC: Little Endian */
#define GMAC_POLY	0x04C11DB7L	/* CRC16-Poly - GMAC: Little Endian */
#define HASH_BITS	6				/* #bits in hash */
#define	SK_MC_BIT	0x01

/* Error numbers and messages. */

#define SKERR_ADDR_E001		(SK_ERRBASE_ADDR + 0)
#define SKERR_ADDR_E001MSG	"Bad Flags."
#define SKERR_ADDR_E002		(SKERR_ADDR_E001 + 1)
#define SKERR_ADDR_E002MSG	"New Error."

/* typedefs *******************************************************************/

/* None. */

/* global variables ***********************************************************/

/* 64-bit hash values with all bits set. */

static const SK_U16	OnesHash[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

/* local variables ************************************************************/

#ifdef DEBUG
static int	Next0[SK_MAX_MACS] = {0};
#endif	/* DEBUG */

/* functions ******************************************************************/

/******************************************************************************
 *
 *	SkAddrInit - initialize data, set state to init
 *
 * Description:
 *
 *	SK_INIT_DATA
 *	============
 *
 *	This routine clears the multicast tables and resets promiscuous mode.
 *	Some entries are reserved for the "logical MAC address", the
 *	SK-RLMT multicast address, and the BPDU multicast address.
 *
 *
 *	SK_INIT_IO
 *	==========
 *
 *	All permanent MAC addresses are read from EPROM.
 *	If the current MAC addresses are not already set in software,
 *	they are set to the values of the permanent addresses.
 *	The current addresses are written to the corresponding MAC.
 *
 *
 *	SK_INIT_RUN
 *	===========
 *
 *	Nothing.
 *
 * Context:
 *	init, pageable
 *
 * Returns:
 *	SK_ADDR_SUCCESS
 */
int	SkAddrInit(
SK_AC	*pAC,	/* the adapter context */
SK_IOC	IoC,	/* I/O context */
int		Level)	/* initialization level */
{
	int			j;
	SK_U32		i;
	SK_U8		*InAddr;
	SK_U16		*OutAddr;
	SK_ADDR_PORT	*pAPort;

	switch (Level) {
	case SK_INIT_DATA:
		SK_MEMSET((char *) &pAC->Addr, (SK_U8) 0,
            (SK_U16) sizeof(SK_ADDR));

		for (i = 0; i < SK_MAX_MACS; i++) {
			pAPort = &pAC->Addr.Port[i];
			pAPort->PromMode = SK_PROM_MODE_NONE;
			
			pAPort->FirstExactMatchRlmt = SK_ADDR_FIRST_MATCH_RLMT;
			pAPort->FirstExactMatchDrv = SK_ADDR_FIRST_MATCH_DRV;
			pAPort->NextExactMatchRlmt = SK_ADDR_FIRST_MATCH_RLMT;
			pAPort->NextExactMatchDrv = SK_ADDR_FIRST_MATCH_DRV;
		}
#ifdef xDEBUG
		for (i = 0; i < SK_MAX_MACS; i++) {
			if (pAC->Addr.Port[i].NextExactMatchRlmt <
				SK_ADDR_FIRST_MATCH_RLMT) {
				Next0[i] |= 4;
			}
		}
#endif	/* DEBUG */
		/* pAC->Addr.InitDone = SK_INIT_DATA; */
		break;

    case SK_INIT_IO:
#ifndef SK_NO_RLMT
		for (i = 0; i < SK_MAX_NETS; i++) {
			pAC->Addr.Net[i].ActivePort = pAC->Rlmt.Net[i].ActivePort;
		}
#endif /* !SK_NO_RLMT */
#ifdef xDEBUG
		for (i = 0; i < SK_MAX_MACS; i++) {
			if (pAC->Addr.Port[i].NextExactMatchRlmt <
				SK_ADDR_FIRST_MATCH_RLMT) {
				Next0[i] |= 8;
			}
		}
#endif	/* DEBUG */
		
		/* Read permanent logical MAC address from Control Register File. */
		for (j = 0; j < SK_MAC_ADDR_LEN; j++) {
			InAddr = (SK_U8 *) &pAC->Addr.Net[0].PermanentMacAddress.a[j];
			SK_IN8(IoC, B2_MAC_1 + j, InAddr);
		}

		if (!pAC->Addr.Net[0].CurrentMacAddressSet) {
			/* Set the current logical MAC address to the permanent one. */
			pAC->Addr.Net[0].CurrentMacAddress =
				pAC->Addr.Net[0].PermanentMacAddress;
			pAC->Addr.Net[0].CurrentMacAddressSet = SK_TRUE;
		}

		/* Set the current logical MAC address. */
		pAC->Addr.Port[pAC->Addr.Net[0].ActivePort].Exact[0] =
			pAC->Addr.Net[0].CurrentMacAddress;
#if SK_MAX_NETS > 1
		/* Set logical MAC address for net 2 to (log | 3). */
		if (!pAC->Addr.Net[1].CurrentMacAddressSet) {
			pAC->Addr.Net[1].PermanentMacAddress =
				pAC->Addr.Net[0].PermanentMacAddress;
			pAC->Addr.Net[1].PermanentMacAddress.a[5] |= 3;
			/* Set the current logical MAC address to the permanent one. */
			pAC->Addr.Net[1].CurrentMacAddress =
				pAC->Addr.Net[1].PermanentMacAddress;
			pAC->Addr.Net[1].CurrentMacAddressSet = SK_TRUE;
		}
#endif	/* SK_MAX_NETS > 1 */

#ifdef DEBUG
		for (i = 0; i < (SK_U32) pAC->GIni.GIMacsFound; i++) {
			SK_DBG_MSG(pAC, SK_DBGMOD_ADDR, SK_DBGCAT_INIT,
				("Permanent MAC Address (Net%d): %02X %02X %02X %02X %02X %02X\n",
					i,
					pAC->Addr.Net[i].PermanentMacAddress.a[0],
					pAC->Addr.Net[i].PermanentMacAddress.a[1],
					pAC->Addr.Net[i].PermanentMacAddress.a[2],
					pAC->Addr.Net[i].PermanentMacAddress.a[3],
					pAC->Addr.Net[i].PermanentMacAddress.a[4],
					pAC->Addr.Net[i].PermanentMacAddress.a[5]))
			
			SK_DBG_MSG(pAC, SK_DBGMOD_ADDR, SK_DBGCAT_INIT,
				("Logical MAC Address (Net%d): %02X %02X %02X %02X %02X %02X\n",
					i,
					pAC->Addr.Net[i].CurrentMacAddress.a[0],
					pAC->Addr.Net[i].CurrentMacAddress.a[1],
					pAC->Addr.Net[i].CurrentMacAddress.a[2],
					pAC->Addr.Net[i].CurrentMacAddress.a[3],
					pAC->Addr.Net[i].CurrentMacAddress.a[4],
					pAC->Addr.Net[i].CurrentMacAddress.a[5]))
		}
#endif	/* DEBUG */

		for (i = 0; i < (SK_U32) pAC->GIni.GIMacsFound; i++) {
			pAPort = &pAC->Addr.Port[i];

			/* Read permanent port addresses from Control Register File. */
			for (j = 0; j < SK_MAC_ADDR_LEN; j++) {
				InAddr = (SK_U8 *) &pAPort->PermanentMacAddress.a[j];
				SK_IN8(IoC, B2_MAC_2 + 8 * i + j, InAddr);
			}

			if (!pAPort->CurrentMacAddressSet) {
				/*
				 * Set the current and previous physical MAC address
				 * of this port to its permanent MAC address.
				 */
				pAPort->CurrentMacAddress = pAPort->PermanentMacAddress;
				pAPort->PreviousMacAddress = pAPort->PermanentMacAddress;
				pAPort->CurrentMacAddressSet = SK_TRUE;
			}

			/* Set port's current physical MAC address. */
			OutAddr = (SK_U16 *) &pAPort->CurrentMacAddress.a[0];
#ifdef GENESIS
			if (pAC->GIni.GIGenesis) {
				XM_OUTADDR(IoC, i, XM_SA, OutAddr);
			}
#endif /* GENESIS */
#ifdef YUKON
			if (!pAC->GIni.GIGenesis) {
				GM_OUTADDR(IoC, i, GM_SRC_ADDR_1L, OutAddr);
			}
#endif /* YUKON */
#ifdef DEBUG
			SK_DBG_MSG(pAC, SK_DBGMOD_ADDR, SK_DBGCAT_INIT,
				("SkAddrInit: Permanent Physical MAC Address: %02X %02X %02X %02X %02X %02X\n",
					pAPort->PermanentMacAddress.a[0],
					pAPort->PermanentMacAddress.a[1],
					pAPort->PermanentMacAddress.a[2],
					pAPort->PermanentMacAddress.a[3],
					pAPort->PermanentMacAddress.a[4],
					pAPort->PermanentMacAddress.a[5]))
			
			SK_DBG_MSG(pAC, SK_DBGMOD_ADDR, SK_DBGCAT_INIT,
				("SkAddrInit: Physical MAC Address: %02X %02X %02X %02X %02X %02X\n",
					pAPort->CurrentMacAddress.a[0],
					pAPort->CurrentMacAddress.a[1],
					pAPort->CurrentMacAddress.a[2],
					pAPort->CurrentMacAddress.a[3],
					pAPort->CurrentMacAddress.a[4],
					pAPort->CurrentMacAddress.a[5]))
#endif /* DEBUG */
		}
		/* pAC->Addr.InitDone = SK_INIT_IO; */
		break;

	case SK_INIT_RUN:
#ifdef xDEBUG
		for (i = 0; i < SK_MAX_MACS; i++) {
			if (pAC->Addr.Port[i].NextExactMatchRlmt <
				SK_ADDR_FIRST_MATCH_RLMT) {
				Next0[i] |= 16;
			}
		}
#endif	/* DEBUG */

		/* pAC->Addr.InitDone = SK_INIT_RUN; */
		break;

	default:	/* error */
		break;
	}

	return (SK_ADDR_SUCCESS);
	
}	/* SkAddrInit */

#ifndef SK_SLIM

/******************************************************************************
 *
 *	SkAddrMcClear - clear the multicast table
 *
 * Description:
 *	This routine clears the multicast table.
 *
 *	If not suppressed by Flag SK_MC_SW_ONLY, the hardware is updated
 *	immediately.
 *
 *	It calls either SkAddrXmacMcClear or SkAddrGmacMcClear, according
 *	to the adapter in use. The real work is done there.
 *
 * Context:
 *	runtime, pageable
 *	may be called starting with SK_INIT_DATA with flag SK_MC_SW_ONLY
 *	may be called after SK_INIT_IO without limitation
 *
 * Returns:
 *	SK_ADDR_SUCCESS
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrMcClear(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* I/O context */
SK_U32	PortNumber,	/* Index of affected port */
int		Flags)		/* permanent/non-perm, sw-only */
{
	int ReturnCode;
	
	if (PortNumber >= (SK_U32) pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}
	
	if (pAC->GIni.GIGenesis) {
		ReturnCode = SkAddrXmacMcClear(pAC, IoC, PortNumber, Flags);
	}
	else {
		ReturnCode = SkAddrGmacMcClear(pAC, IoC, PortNumber, Flags);
	}

	return (ReturnCode);

}	/* SkAddrMcClear */

#endif /* !SK_SLIM */

#ifndef SK_SLIM

/******************************************************************************
 *
 *	SkAddrXmacMcClear - clear the multicast table
 *
 * Description:
 *	This routine clears the multicast table
 *	(either entry 2 or entries 3-16 and InexactFilter) of the given port.
 *	If not suppressed by Flag SK_MC_SW_ONLY, the hardware is updated
 *	immediately.
 *
 * Context:
 *	runtime, pageable
 *	may be called starting with SK_INIT_DATA with flag SK_MC_SW_ONLY
 *	may be called after SK_INIT_IO without limitation
 *
 * Returns:
 *	SK_ADDR_SUCCESS
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrXmacMcClear(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* I/O context */
SK_U32	PortNumber,	/* Index of affected port */
int		Flags)		/* permanent/non-perm, sw-only */
{
	int i;

	if (Flags & SK_ADDR_PERMANENT) {	/* permanent => RLMT */

		/* Clear RLMT multicast addresses. */
		pAC->Addr.Port[PortNumber].NextExactMatchRlmt = SK_ADDR_FIRST_MATCH_RLMT;
	}
	else {	/* not permanent => DRV */

		/* Clear InexactFilter */
		for (i = 0; i < 8; i++) {
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[i] = 0;
		}

		/* Clear DRV multicast addresses. */

		pAC->Addr.Port[PortNumber].NextExactMatchDrv = SK_ADDR_FIRST_MATCH_DRV;
	}

	if (!(Flags & SK_MC_SW_ONLY)) {
		(void) SkAddrXmacMcUpdate(pAC, IoC, PortNumber);
	}

	return (SK_ADDR_SUCCESS);
	
}	/* SkAddrXmacMcClear */

#endif /* !SK_SLIM */

#ifndef SK_SLIM

/******************************************************************************
 *
 *	SkAddrGmacMcClear - clear the multicast table
 *
 * Description:
 *	This routine clears the multicast hashing table (InexactFilter)
 *	(either the RLMT or the driver bits) of the given port.
 *
 *	If not suppressed by Flag SK_MC_SW_ONLY, the hardware is updated
 *	immediately.
 *
 * Context:
 *	runtime, pageable
 *	may be called starting with SK_INIT_DATA with flag SK_MC_SW_ONLY
 *	may be called after SK_INIT_IO without limitation
 *
 * Returns:
 *	SK_ADDR_SUCCESS
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrGmacMcClear(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* I/O context */
SK_U32	PortNumber,	/* Index of affected port */
int		Flags)		/* permanent/non-perm, sw-only */
{
	int i;

#ifdef DEBUG
	SK_DBG_MSG(pAC, SK_DBGMOD_ADDR, SK_DBGCAT_CTRL,
		("GMAC InexactFilter (not cleared): %02X %02X %02X %02X %02X %02X %02X %02X\n",
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[0],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[1],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[2],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[3],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[4],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[5],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[6],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[7]))
#endif	/* DEBUG */

	/* Clear InexactFilter */
	for (i = 0; i < 8; i++) {
		pAC->Addr.Port[PortNumber].InexactFilter.Bytes[i] = 0;
	}
	
	if (Flags & SK_ADDR_PERMANENT) {	/* permanent => RLMT */
		
		/* Copy DRV bits to InexactFilter. */
		for (i = 0; i < 8; i++) {
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[i] |=
				pAC->Addr.Port[PortNumber].InexactDrvFilter.Bytes[i];
			
			/* Clear InexactRlmtFilter. */
			pAC->Addr.Port[PortNumber].InexactRlmtFilter.Bytes[i] = 0;

		}		
	}
	else {	/* not permanent => DRV */
		
		/* Copy RLMT bits to InexactFilter. */
		for (i = 0; i < 8; i++) {
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[i] |=
				pAC->Addr.Port[PortNumber].InexactRlmtFilter.Bytes[i];
			
			/* Clear InexactDrvFilter. */
			pAC->Addr.Port[PortNumber].InexactDrvFilter.Bytes[i] = 0;
		}
	}
	
#ifdef DEBUG
	SK_DBG_MSG(pAC, SK_DBGMOD_ADDR, SK_DBGCAT_CTRL,
		("GMAC InexactFilter (cleared): %02X %02X %02X %02X %02X %02X %02X %02X\n",
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[0],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[1],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[2],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[3],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[4],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[5],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[6],
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[7]))
#endif	/* DEBUG */
	
	if (!(Flags & SK_MC_SW_ONLY)) {
		(void) SkAddrGmacMcUpdate(pAC, IoC, PortNumber);
	}
	
	return (SK_ADDR_SUCCESS);

}	/* SkAddrGmacMcClear */

#ifndef SK_ADDR_CHEAT

/******************************************************************************
 *
 *	SkXmacMcHash - hash multicast address
 *
 * Description:
 *	This routine computes the hash value for a multicast address.
 *	A CRC32 algorithm is used.
 *
 * Notes:
 *	The code was adapted from the XaQti data sheet.
 *
 * Context:
 *	runtime, pageable
 *
 * Returns:
 *	Hash value of multicast address.
 */
SK_U32 SkXmacMcHash(
unsigned char *pMc)	/* Multicast address */
{
	SK_U32 Idx;
	SK_U32 Bit;
	SK_U32 Data;
	SK_U32 Crc;

	Crc = 0xFFFFFFFFUL;
	for (Idx = 0; Idx < SK_MAC_ADDR_LEN; Idx++) {
		Data = *pMc++;
		for (Bit = 0; Bit < 8; Bit++, Data >>= 1) {
			Crc = (Crc >> 1) ^ (((Crc ^ Data) & 1) ? XMAC_POLY : 0);
		}
	}

	return (Crc & ((1 << HASH_BITS) - 1));

}	/* SkXmacMcHash */


/******************************************************************************
 *
 *	SkGmacMcHash - hash multicast address
 *
 * Description:
 *	This routine computes the hash value for a multicast address.
 *	A CRC16 algorithm is used.
 *
 * Notes:
 *
 *
 * Context:
 *	runtime, pageable
 *
 * Returns:
 *	Hash value of multicast address.
 */
SK_U32 SkGmacMcHash(
unsigned char *pMc)	/* Multicast address */
{
	SK_U32 Data;
	SK_U32 TmpData;
	SK_U32 Crc;
	int Byte;
	int Bit;

	Crc = 0xFFFFFFFFUL;
	for (Byte = 0; Byte < 6; Byte++) {
		/* Get next byte. */
		Data = (SK_U32) pMc[Byte];
		
		/* Change bit order in byte. */
		TmpData = Data;
		for (Bit = 0; Bit < 8; Bit++) {
			if (TmpData & 1L) {
				Data |=  1L << (7 - Bit);
			}
			else {
				Data &= ~(1L << (7 - Bit));
			}
			TmpData >>= 1;
		}
		
		Crc ^= (Data << 24);
		for (Bit = 0; Bit < 8; Bit++) {
			if (Crc & 0x80000000) {
				Crc = (Crc << 1) ^ GMAC_POLY;
			}
			else {
				Crc <<= 1;
			}
		}
	}
	
	return (Crc & ((1 << HASH_BITS) - 1));

}	/* SkGmacMcHash */

#endif	/* !SK_ADDR_CHEAT */

/******************************************************************************
 *
 *	SkAddrMcAdd - add a multicast address to a port
 *
 * Description:
 *	This routine enables reception for a given address on the given port.
 *
 *	It calls either SkAddrXmacMcAdd or SkAddrGmacMcAdd, according to the
 *	adapter in use. The real work is done there.
 *
 * Notes:
 *	The return code is only valid for SK_PROM_MODE_NONE.
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_DATA
 *
 * Returns:
 *	SK_MC_FILTERING_EXACT
 *	SK_MC_FILTERING_INEXACT
 *	SK_MC_ILLEGAL_ADDRESS
 *	SK_MC_ILLEGAL_PORT
 *	SK_MC_RLMT_OVERFLOW
 */
int	SkAddrMcAdd(
SK_AC		*pAC,		/* adapter context */
SK_IOC		IoC,		/* I/O context */
SK_U32		PortNumber,	/* Port Number */
SK_MAC_ADDR	*pMc,		/* multicast address to be added */
int			Flags)		/* permanent/non-permanent */
{
	int ReturnCode;
	
	if (PortNumber >= (SK_U32) pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}
	
	if (pAC->GIni.GIGenesis) {
		ReturnCode = SkAddrXmacMcAdd(pAC, IoC, PortNumber, pMc, Flags);
	}
	else {
		ReturnCode = SkAddrGmacMcAdd(pAC, IoC, PortNumber, pMc, Flags);
	}

	return (ReturnCode);

}	/* SkAddrMcAdd */


/******************************************************************************
 *
 *	SkAddrXmacMcAdd - add a multicast address to a port
 *
 * Description:
 *	This routine enables reception for a given address on the given port.
 *
 * Notes:
 *	The return code is only valid for SK_PROM_MODE_NONE.
 *
 *	The multicast bit is only checked if there are no free exact match
 *	entries.
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_DATA
 *
 * Returns:
 *	SK_MC_FILTERING_EXACT
 *	SK_MC_FILTERING_INEXACT
 *	SK_MC_ILLEGAL_ADDRESS
 *	SK_MC_RLMT_OVERFLOW
 */
int	SkAddrXmacMcAdd(
SK_AC		*pAC,		/* adapter context */
SK_IOC		IoC,		/* I/O context */
SK_U32		PortNumber,	/* Port Number */
SK_MAC_ADDR	*pMc,		/* multicast address to be added */
int		Flags)		/* permanent/non-permanent */
{
	int	i;
	SK_U8	Inexact;
#ifndef SK_ADDR_CHEAT
	SK_U32 HashBit;
#endif	/* !defined(SK_ADDR_CHEAT) */

	if (Flags & SK_ADDR_PERMANENT) {	/* permanent => RLMT */
#ifdef xDEBUG
		if (pAC->Addr.Port[PortNumber].NextExactMatchRlmt <
			SK_ADDR_FIRST_MATCH_RLMT) {
			Next0[PortNumber] |= 1;
			return (SK_MC_RLMT_OVERFLOW);
		}
#endif	/* DEBUG */
		
		if (pAC->Addr.Port[PortNumber].NextExactMatchRlmt >
			SK_ADDR_LAST_MATCH_RLMT) {
			return (SK_MC_RLMT_OVERFLOW);
		}

		/* Set a RLMT multicast address. */

		pAC->Addr.Port[PortNumber].Exact[
			pAC->Addr.Port[PortNumber].NextExactMatchRlmt++] = *pMc;

		return (SK_MC_FILTERING_EXACT);
	}

#ifdef xDEBUG
	if (pAC->Addr.Port[PortNumber].NextExactMatchDrv <
		SK_ADDR_FIRST_MATCH_DRV) {
			Next0[PortNumber] |= 2;
		return (SK_MC_RLMT_OVERFLOW);
	}
#endif	/* DEBUG */
	
	if (pAC->Addr.Port[PortNumber].NextExactMatchDrv <= SK_ADDR_LAST_MATCH_DRV) {

		/* Set exact match entry. */
		pAC->Addr.Port[PortNumber].Exact[
			pAC->Addr.Port[PortNumber].NextExactMatchDrv++] = *pMc;

		/* Clear InexactFilter */
		for (i = 0; i < 8; i++) {
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[i] = 0;
		}
	}
	else {
		if (!(pMc->a[0] & SK_MC_BIT)) {
			/* Hashing only possible with multicast addresses */
			return (SK_MC_ILLEGAL_ADDRESS);
		}
#ifndef SK_ADDR_CHEAT
		/* Compute hash value of address. */
		HashBit = 63 - SkXmacMcHash(&pMc->a[0]);

		/* Add bit to InexactFilter. */
		pAC->Addr.Port[PortNumber].InexactFilter.Bytes[HashBit / 8] |=
			1 << (HashBit % 8);
#else	/* SK_ADDR_CHEAT */
		/* Set all bits in InexactFilter. */
		for (i = 0; i < 8; i++) {
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[i] = 0xFF;
		}
#endif	/* SK_ADDR_CHEAT */
	}

	for (Inexact = 0, i = 0; i < 8; i++) {
		Inexact |= pAC->Addr.Port[PortNumber].InexactFilter.Bytes[i];
	}

	if (Inexact == 0 && pAC->Addr.Port[PortNumber].PromMode == 0) {
		return (SK_MC_FILTERING_EXACT);
	}
	else {
		return (SK_MC_FILTERING_INEXACT);
	}

}	/* SkAddrXmacMcAdd */


/******************************************************************************
 *
 *	SkAddrGmacMcAdd - add a multicast address to a port
 *
 * Description:
 *	This routine enables reception for a given address on the given port.
 *
 * Notes:
 *	The return code is only valid for SK_PROM_MODE_NONE.
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_DATA
 *
 * Returns:
 *	SK_MC_FILTERING_INEXACT
 *	SK_MC_ILLEGAL_ADDRESS
 */
int	SkAddrGmacMcAdd(
SK_AC		*pAC,		/* adapter context */
SK_IOC		IoC,		/* I/O context */
SK_U32		PortNumber,	/* Port Number */
SK_MAC_ADDR	*pMc,		/* multicast address to be added */
int		Flags)		/* permanent/non-permanent */
{
	int	i;
#ifndef SK_ADDR_CHEAT
	SK_U32 HashBit;
#endif	/* !defined(SK_ADDR_CHEAT) */
		
	if (!(pMc->a[0] & SK_MC_BIT)) {
		/* Hashing only possible with multicast addresses */
		return (SK_MC_ILLEGAL_ADDRESS);
	}
	
#ifndef SK_ADDR_CHEAT
	
	/* Compute hash value of address. */
	HashBit = SkGmacMcHash(&pMc->a[0]);
	
	if (Flags & SK_ADDR_PERMANENT) {	/* permanent => RLMT */
		
		/* Add bit to InexactRlmtFilter. */
		pAC->Addr.Port[PortNumber].InexactRlmtFilter.Bytes[HashBit / 8] |=
			1 << (HashBit % 8);
		
		/* Copy bit to InexactFilter. */
		for (i = 0; i < 8; i++) {
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[i] |=
				pAC->Addr.Port[PortNumber].InexactRlmtFilter.Bytes[i];
		}
#ifdef DEBUG
		SK_DBG_MSG(pAC, SK_DBGMOD_ADDR, SK_DBGCAT_CTRL,
		("GMAC InexactRlmtFilter: %02X %02X %02X %02X %02X %02X %02X %02X\n",
			pAC->Addr.Port[PortNumber].InexactRlmtFilter.Bytes[0],
			pAC->Addr.Port[PortNumber].InexactRlmtFilter.Bytes[1],
			pAC->Addr.Port[PortNumber].InexactRlmtFilter.Bytes[2],
			pAC->Addr.Port[PortNumber].InexactRlmtFilter.Bytes[3],
			pAC->Addr.Port[PortNumber].InexactRlmtFilter.Bytes[4],
			pAC->Addr.Port[PortNumber].InexactRlmtFilter.Bytes[5],
			pAC->Addr.Port[PortNumber].InexactRlmtFilter.Bytes[6],
			pAC->Addr.Port[PortNumber].InexactRlmtFilter.Bytes[7]))
#endif	/* DEBUG */
	}
	else {	/* not permanent => DRV */
		
		/* Add bit to InexactDrvFilter. */
		pAC->Addr.Port[PortNumber].InexactDrvFilter.Bytes[HashBit / 8] |=
			1 << (HashBit % 8);
		
		/* Copy bit to InexactFilter. */
		for (i = 0; i < 8; i++) {
			pAC->Addr.Port[PortNumber].InexactFilter.Bytes[i] |=
				pAC->Addr.Port[PortNumber].InexactDrvFilter.Bytes[i];
		}
#ifdef DEBUG
		SK_DBG_MSG(pAC, SK_DBGMOD_ADDR, SK_DBGCAT_CTRL,
		("GMAC InexactDrvFilter: %02X %02X %02X %02X %02X %02X %02X %02X\n",
			pAC->Addr.Port[PortNumber].InexactDrvFilter.Bytes[0],
			pAC->Addr.Port[PortNumber].InexactDrvFilter.Bytes[1],
			pAC->Addr.Port[PortNumber].InexactDrvFilter.Bytes[2],
			pAC->Addr.Port[PortNumber].InexactDrvFilter.Bytes[3],
			pAC->Addr.Port[PortNumber].InexactDrvFilter.Bytes[4],
			pAC->Addr.Port[PortNumber].InexactDrvFilter.Bytes[5],
			pAC->Addr.Port[PortNumber].InexactDrvFilter.Bytes[6],
			pAC->Addr.Port[PortNumber].InexactDrvFilter.Bytes[7]))
#endif	/* DEBUG */
	}
	
#else	/* SK_ADDR_CHEAT */
	
	/* Set all bits in InexactFilter. */
	for (i = 0; i < 8; i++) {
		pAC->Addr.Port[PortNumber].InexactFilter.Bytes[i] = 0xFF;
	}
#endif	/* SK_ADDR_CHEAT */
		
	return (SK_MC_FILTERING_INEXACT);
	
}	/* SkAddrGmacMcAdd */

#endif /* !SK_SLIM */

/******************************************************************************
 *
 *	SkAddrMcUpdate - update the HW MC address table and set the MAC address
 *
 * Description:
 *	This routine enables reception of the addresses contained in a local
 *	table for a given port.
 *	It also programs the port's current physical MAC address.
 *
 *	It calls either SkAddrXmacMcUpdate or SkAddrGmacMcUpdate, according
 *	to the adapter in use. The real work is done there.
 *
 * Notes:
 *	The return code is only valid for SK_PROM_MODE_NONE.
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	SK_MC_FILTERING_EXACT
 *	SK_MC_FILTERING_INEXACT
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrMcUpdate(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* I/O context */
SK_U32	PortNumber)	/* Port Number */
{
	int ReturnCode = 0;
#if (!defined(SK_SLIM) || defined(DEBUG))
	if (PortNumber >= (SK_U32) pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}
#endif /* !SK_SLIM || DEBUG */

#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		ReturnCode = SkAddrXmacMcUpdate(pAC, IoC, PortNumber);
	}
#endif /* GENESIS */
#ifdef YUKON
	if (!pAC->GIni.GIGenesis) {
		ReturnCode = SkAddrGmacMcUpdate(pAC, IoC, PortNumber);
	}
#endif /* YUKON */
	return (ReturnCode);

}	/* SkAddrMcUpdate */


#ifdef GENESIS

/******************************************************************************
 *
 *	SkAddrXmacMcUpdate - update the HW MC address table and set the MAC address
 *
 * Description:
 *	This routine enables reception of the addresses contained in a local
 *	table for a given port.
 *	It also programs the port's current physical MAC address.
 *
 * Notes:
 *	The return code is only valid for SK_PROM_MODE_NONE.
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	SK_MC_FILTERING_EXACT
 *	SK_MC_FILTERING_INEXACT
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrXmacMcUpdate(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* I/O context */
SK_U32	PortNumber)	/* Port Number */
{
	SK_U32		i;
	SK_U8		Inexact;
	SK_U16		*OutAddr;
	SK_ADDR_PORT	*pAPort;

	SK_DBG_MSG(pAC,SK_DBGMOD_ADDR, SK_DBGCAT_CTRL,
		("SkAddrXmacMcUpdate on Port %u.\n", PortNumber))
	
	pAPort = &pAC->Addr.Port[PortNumber];

#ifdef DEBUG
	SK_DBG_MSG(pAC,SK_DBGMOD_ADDR, SK_DBGCAT_CTRL,
		("Next0 on Port %d: %d\n", PortNumber, Next0[PortNumber]))
#endif /* DEBUG */

	/* Start with 0 to also program the logical MAC address. */
	for (i = 0; i < pAPort->NextExactMatchRlmt; i++) {
		/* Set exact match address i on XMAC */
		OutAddr = (SK_U16 *) &pAPort->Exact[i].a[0];
		XM_OUTADDR(IoC, PortNumber, XM_EXM(i), OutAddr);
	}

	/* Clear other permanent exact match addresses on XMAC */
	if (pAPort->NextExactMatchRlmt <= SK_ADDR_LAST_MATCH_RLMT) {
		
		SkXmClrExactAddr(pAC, IoC, PortNumber, pAPort->NextExactMatchRlmt,
			SK_ADDR_LAST_MATCH_RLMT);
	}

	for (i = pAPort->FirstExactMatchDrv; i < pAPort->NextExactMatchDrv; i++) {
		OutAddr = (SK_U16 *) &pAPort->Exact[i].a[0];
		XM_OUTADDR(IoC, PortNumber, XM_EXM(i), OutAddr);
	}

	/* Clear other non-permanent exact match addresses on XMAC */
	if (pAPort->NextExactMatchDrv <= SK_ADDR_LAST_MATCH_DRV) {
		
		SkXmClrExactAddr(pAC, IoC, PortNumber, pAPort->NextExactMatchDrv,
			SK_ADDR_LAST_MATCH_DRV);
	}

	for (Inexact = 0, i = 0; i < 8; i++) {
		Inexact |= pAPort->InexactFilter.Bytes[i];
	}

	if (pAPort->PromMode & SK_PROM_MODE_ALL_MC) {
		
		/* Set all bits in 64-bit hash register. */
		XM_OUTHASH(IoC, PortNumber, XM_HSM, &OnesHash);
		
		/* Enable Hashing */
		SkMacHashing(pAC, IoC, (int) PortNumber, SK_TRUE);
	}
	else if (Inexact != 0) {
		
		/* Set 64-bit hash register to InexactFilter. */
		XM_OUTHASH(IoC, PortNumber, XM_HSM, &pAPort->InexactFilter.Bytes[0]);
		
		/* Enable Hashing */
		SkMacHashing(pAC, IoC, (int) PortNumber, SK_TRUE);
	}
	else {
		/* Disable Hashing */
		SkMacHashing(pAC, IoC, (int) PortNumber, SK_FALSE);
	}

	if (pAPort->PromMode != SK_PROM_MODE_NONE) {
		(void) SkAddrXmacPromiscuousChange(pAC, IoC, PortNumber, pAPort->PromMode);
	}

	/* Set port's current physical MAC address. */
	OutAddr = (SK_U16 *) &pAPort->CurrentMacAddress.a[0];
	
	XM_OUTADDR(IoC, PortNumber, XM_SA, OutAddr);

#ifdef xDEBUG
	for (i = 0; i < pAPort->NextExactMatchRlmt; i++) {
		SK_U8		InAddr8[6];
		SK_U16		*InAddr;

		/* Get exact match address i from port PortNumber. */
		InAddr = (SK_U16 *) &InAddr8[0];
		
		XM_INADDR(IoC, PortNumber, XM_EXM(i), InAddr);
		
		SK_DBG_MSG(pAC,SK_DBGMOD_ADDR, SK_DBGCAT_CTRL,
			("SkAddrXmacMcUpdate: MC address %d on Port %u: ",
			 "%02x %02x %02x %02x %02x %02x -- %02x %02x %02x %02x %02x %02x\n",
				i,
				PortNumber,
				InAddr8[0],
				InAddr8[1],
				InAddr8[2],
				InAddr8[3],
				InAddr8[4],
				InAddr8[5],
				pAPort->Exact[i].a[0],
				pAPort->Exact[i].a[1],
				pAPort->Exact[i].a[2],
				pAPort->Exact[i].a[3],
				pAPort->Exact[i].a[4],
				pAPort->Exact[i].a[5]))
	}
#endif /* DEBUG */

	/* Determine return value. */
	if (Inexact == 0 && pAPort->PromMode == 0) {
		return (SK_MC_FILTERING_EXACT);
	}
	else {
		return (SK_MC_FILTERING_INEXACT);
	}
	
}	/* SkAddrXmacMcUpdate */

#endif  /* GENESIS */

#ifdef YUKON

/******************************************************************************
 *
 *	SkAddrGmacMcUpdate - update the HW MC address table and set the MAC address
 *
 * Description:
 *	This routine enables reception of the addresses contained in a local
 *	table for a given port.
 *	It also programs the port's current physical MAC address.
 *
 * Notes:
 *	The return code is only valid for SK_PROM_MODE_NONE.
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	SK_MC_FILTERING_EXACT
 *	SK_MC_FILTERING_INEXACT
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrGmacMcUpdate(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* I/O context */
SK_U32	PortNumber)	/* Port Number */
{
#ifndef SK_SLIM
	SK_U32		i;
	SK_U8		Inexact;
#endif	/* not SK_SLIM */
	SK_U16		*OutAddr;
	SK_ADDR_PORT	*pAPort;

	SK_DBG_MSG(pAC,SK_DBGMOD_ADDR, SK_DBGCAT_CTRL,
		("SkAddrGmacMcUpdate on Port %u.\n", PortNumber))
	
	pAPort = &pAC->Addr.Port[PortNumber];

#ifdef DEBUG
	SK_DBG_MSG(pAC,SK_DBGMOD_ADDR, SK_DBGCAT_CTRL,
		("Next0 on Port %d: %d\n", PortNumber, Next0[PortNumber]))
#endif /* DEBUG */
	
#ifndef SK_SLIM
	for (Inexact = 0, i = 0; i < 8; i++) {
		Inexact |= pAPort->InexactFilter.Bytes[i];
	}
	
	/* Set 64-bit hash register to InexactFilter. */
	GM_OUTHASH(IoC, PortNumber, GM_MC_ADDR_H1,
		&pAPort->InexactFilter.Bytes[0]);
	
	if (pAPort->PromMode & SK_PROM_MODE_ALL_MC) {				
		
		/* Set all bits in 64-bit hash register. */
		GM_OUTHASH(IoC, PortNumber, GM_MC_ADDR_H1, &OnesHash);
		
		/* Enable Hashing */
		SkMacHashing(pAC, IoC, (int) PortNumber, SK_TRUE);
	}
	else {	
		/* Enable Hashing. */
		SkMacHashing(pAC, IoC, (int) PortNumber, SK_TRUE);
	}
	
	if (pAPort->PromMode != SK_PROM_MODE_NONE) {
		(void) SkAddrGmacPromiscuousChange(pAC, IoC, PortNumber, pAPort->PromMode);
	}
#else /* SK_SLIM */

	/* Set all bits in 64-bit hash register. */
	GM_OUTHASH(IoC, PortNumber, GM_MC_ADDR_H1, &OnesHash);

	/* Enable Hashing */
	SkMacHashing(pAC, IoC, (int) PortNumber, SK_TRUE);
	
	(void) SkAddrGmacPromiscuousChange(pAC, IoC, PortNumber, pAPort->PromMode);
	
#endif /* SK_SLIM */
	
	/* Set port's current physical MAC address. */
	OutAddr = (SK_U16 *) &pAPort->CurrentMacAddress.a[0];
	GM_OUTADDR(IoC, PortNumber, GM_SRC_ADDR_1L, OutAddr);
	
	/* Set port's current logical MAC address. */
	OutAddr = (SK_U16 *) &pAPort->Exact[0].a[0];
	GM_OUTADDR(IoC, PortNumber, GM_SRC_ADDR_2L, OutAddr);
	
#ifdef DEBUG
	SK_DBG_MSG(pAC, SK_DBGMOD_ADDR, SK_DBGCAT_CTRL,
		("SkAddrGmacMcUpdate: Permanent Physical MAC Address: %02X %02X %02X %02X %02X %02X\n",
			pAPort->Exact[0].a[0],
			pAPort->Exact[0].a[1],
			pAPort->Exact[0].a[2],
			pAPort->Exact[0].a[3],
			pAPort->Exact[0].a[4],
			pAPort->Exact[0].a[5]))
	
	SK_DBG_MSG(pAC, SK_DBGMOD_ADDR, SK_DBGCAT_CTRL,
		("SkAddrGmacMcUpdate: Physical MAC Address: %02X %02X %02X %02X %02X %02X\n",
			pAPort->CurrentMacAddress.a[0],
			pAPort->CurrentMacAddress.a[1],
			pAPort->CurrentMacAddress.a[2],
			pAPort->CurrentMacAddress.a[3],
			pAPort->CurrentMacAddress.a[4],
			pAPort->CurrentMacAddress.a[5]))
#endif /* DEBUG */
	
#ifndef SK_SLIM
	/* Determine return value. */
	if (Inexact == 0 && pAPort->PromMode == 0) {
		return (SK_MC_FILTERING_EXACT);
	}
	else {
		return (SK_MC_FILTERING_INEXACT);
	}
#else /* SK_SLIM */
	return (SK_MC_FILTERING_INEXACT);
#endif /* SK_SLIM */
	
}	/* SkAddrGmacMcUpdate */

#endif /* YUKON */

#ifndef SK_NO_MAO

/******************************************************************************
 *
 *	SkAddrOverride - override a port's MAC address
 *
 * Description:
 *	This routine overrides the MAC address of one port.
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	SK_ADDR_SUCCESS if successful.
 *	SK_ADDR_DUPLICATE_ADDRESS if duplicate MAC address.
 *	SK_ADDR_MULTICAST_ADDRESS if multicast or broadcast address.
 *	SK_ADDR_TOO_EARLY if SK_INIT_IO was not executed before.
 */
int	SkAddrOverride(
SK_AC		*pAC,				/* adapter context */
SK_IOC		IoC,				/* I/O context */
SK_U32		PortNumber,			/* Port Number */
SK_MAC_ADDR	SK_FAR *pNewAddr,	/* new MAC address */
int			Flags)				/* logical/physical MAC address */
{
#ifndef SK_NO_RLMT
	SK_EVPARA	Para;
#endif /* !SK_NO_RLMT */
	SK_U32		NetNumber;
	SK_U32		i;
	SK_U16		SK_FAR *OutAddr;

#ifndef SK_NO_RLMT
	NetNumber = pAC->Rlmt.Port[PortNumber].Net->NetNumber;
#else
	NetNumber = 0;
#endif /* SK_NO_RLMT */
#if (!defined(SK_SLIM) || defined(DEBUG))
	if (PortNumber >= (SK_U32) pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}
#endif /* !SK_SLIM || DEBUG */
	if (pNewAddr != NULL && (pNewAddr->a[0] & SK_MC_BIT) != 0) {
		return (SK_ADDR_MULTICAST_ADDRESS);
	}

	if (!pAC->Addr.Net[NetNumber].CurrentMacAddressSet) {
		return (SK_ADDR_TOO_EARLY);
	}

	if (Flags & SK_ADDR_SET_LOGICAL) {	/* Activate logical MAC address. */
		/* Parameter *pNewAddr is ignored. */
		for (i = 0; i < (SK_U32) pAC->GIni.GIMacsFound; i++) {
			if (!pAC->Addr.Port[i].CurrentMacAddressSet) {
				return (SK_ADDR_TOO_EARLY);
			}
		}
#ifndef SK_NO_RLMT
		/* Set PortNumber to number of net's active port. */
		PortNumber = pAC->Rlmt.Net[NetNumber].
			Port[pAC->Addr.Net[NetNumber].ActivePort]->PortNumber;
#endif /* !SK_NO_RLMT */
		pAC->Addr.Port[PortNumber].Exact[0] =
			pAC->Addr.Net[NetNumber].CurrentMacAddress;

		/* Write address to first exact match entry of active port. */
		(void) SkAddrMcUpdate(pAC, IoC, PortNumber);
	}
	else if (Flags & SK_ADDR_CLEAR_LOGICAL) {
		/* Deactivate logical MAC address. */
		/* Parameter *pNewAddr is ignored. */
		for (i = 0; i < (SK_U32) pAC->GIni.GIMacsFound; i++) {
			if (!pAC->Addr.Port[i].CurrentMacAddressSet) {
				return (SK_ADDR_TOO_EARLY);
			}
		}
#ifndef SK_NO_RLMT
		/* Set PortNumber to number of net's active port. */
		PortNumber = pAC->Rlmt.Net[NetNumber].
			Port[pAC->Addr.Net[NetNumber].ActivePort]->PortNumber;
#endif /* !SK_NO_RLMT */
		for (i = 0; i < SK_MAC_ADDR_LEN; i++ ) {
			pAC->Addr.Port[PortNumber].Exact[0].a[i] = 0;
		}

		/* Write address to first exact match entry of active port. */
		(void) SkAddrMcUpdate(pAC, IoC, PortNumber);
	}
	else if (Flags & SK_ADDR_PHYSICAL_ADDRESS) {	/* Physical MAC address. */
		if (SK_ADDR_EQUAL(pNewAddr->a,
			pAC->Addr.Net[NetNumber].CurrentMacAddress.a)) {
			return (SK_ADDR_DUPLICATE_ADDRESS);
		}

		for (i = 0; i < (SK_U32) pAC->GIni.GIMacsFound; i++) {
			if (!pAC->Addr.Port[i].CurrentMacAddressSet) {
				return (SK_ADDR_TOO_EARLY);
			}

			if (SK_ADDR_EQUAL(pNewAddr->a,
				pAC->Addr.Port[i].CurrentMacAddress.a)) {
				if (i == PortNumber) {
					return (SK_ADDR_SUCCESS);
				}
				else {
					return (SK_ADDR_DUPLICATE_ADDRESS);
				}
			}
		}

		pAC->Addr.Port[PortNumber].PreviousMacAddress =
			pAC->Addr.Port[PortNumber].CurrentMacAddress;
		pAC->Addr.Port[PortNumber].CurrentMacAddress = *pNewAddr;

		/* Change port's physical MAC address. */
		OutAddr = (SK_U16 SK_FAR *) pNewAddr;
#ifdef GENESIS
		if (pAC->GIni.GIGenesis) {
			XM_OUTADDR(IoC, PortNumber, XM_SA, OutAddr);
		}
#endif /* GENESIS */
#ifdef YUKON
		if (!pAC->GIni.GIGenesis) {
			GM_OUTADDR(IoC, PortNumber, GM_SRC_ADDR_1L, OutAddr);
		}
#endif /* YUKON */

#ifndef SK_NO_RLMT
		/* Report address change to RLMT. */
		Para.Para32[0] = PortNumber;
		Para.Para32[0] = -1;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_PORT_ADDR, Para);
#endif /* !SK_NO_RLMT */
	}
	else {	/* Logical MAC address. */
		if (SK_ADDR_EQUAL(pNewAddr->a,
			pAC->Addr.Net[NetNumber].CurrentMacAddress.a)) {
			return (SK_ADDR_SUCCESS);
		}
		
		for (i = 0; i < (SK_U32) pAC->GIni.GIMacsFound; i++) {
			if (!pAC->Addr.Port[i].CurrentMacAddressSet) {
				return (SK_ADDR_TOO_EARLY);
			}

			if (SK_ADDR_EQUAL(pNewAddr->a,
				pAC->Addr.Port[i].CurrentMacAddress.a)) {
				return (SK_ADDR_DUPLICATE_ADDRESS);
			}
		}
		
		/*
		 * In case that the physical and the logical MAC addresses are equal
		 * we must also change the physical MAC address here.
		 * In this case we have an adapter which initially was programmed with
		 * two identical MAC addresses.
		 */
		if (SK_ADDR_EQUAL(pAC->Addr.Port[PortNumber].CurrentMacAddress.a,
				pAC->Addr.Port[PortNumber].Exact[0].a)) {
			
			pAC->Addr.Port[PortNumber].PreviousMacAddress =
				pAC->Addr.Port[PortNumber].CurrentMacAddress;
			pAC->Addr.Port[PortNumber].CurrentMacAddress = *pNewAddr;
			
#ifndef SK_NO_RLMT
			/* Report address change to RLMT. */
			Para.Para32[0] = PortNumber;
			Para.Para32[0] = -1;
			SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_PORT_ADDR, Para);
#endif /* !SK_NO_RLMT */
		}
		
#ifndef SK_NO_RLMT
		/* Set PortNumber to number of net's active port. */
		PortNumber = pAC->Rlmt.Net[NetNumber].
			Port[pAC->Addr.Net[NetNumber].ActivePort]->PortNumber;
#endif /* !SK_NO_RLMT */
		pAC->Addr.Net[NetNumber].CurrentMacAddress = *pNewAddr;
		pAC->Addr.Port[PortNumber].Exact[0] = *pNewAddr;
#ifdef DEBUG
		SK_DBG_MSG(pAC,SK_DBGMOD_ADDR, SK_DBGCAT_CTRL,
			("SkAddrOverride: Permanent MAC Address: %02X %02X %02X %02X %02X %02X\n",
				pAC->Addr.Net[NetNumber].PermanentMacAddress.a[0],
				pAC->Addr.Net[NetNumber].PermanentMacAddress.a[1],
				pAC->Addr.Net[NetNumber].PermanentMacAddress.a[2],
				pAC->Addr.Net[NetNumber].PermanentMacAddress.a[3],
				pAC->Addr.Net[NetNumber].PermanentMacAddress.a[4],
				pAC->Addr.Net[NetNumber].PermanentMacAddress.a[5]))
		
		SK_DBG_MSG(pAC,SK_DBGMOD_ADDR, SK_DBGCAT_CTRL,
			("SkAddrOverride: New logical MAC Address: %02X %02X %02X %02X %02X %02X\n",
				pAC->Addr.Net[NetNumber].CurrentMacAddress.a[0],
				pAC->Addr.Net[NetNumber].CurrentMacAddress.a[1],
				pAC->Addr.Net[NetNumber].CurrentMacAddress.a[2],
				pAC->Addr.Net[NetNumber].CurrentMacAddress.a[3],
				pAC->Addr.Net[NetNumber].CurrentMacAddress.a[4],
				pAC->Addr.Net[NetNumber].CurrentMacAddress.a[5]))
#endif /* DEBUG */

        /* Write address to first exact match entry of active port. */
		(void) SkAddrMcUpdate(pAC, IoC, PortNumber);
	}

	return (SK_ADDR_SUCCESS);
	
}	/* SkAddrOverride */


#endif /* SK_NO_MAO */

/******************************************************************************
 *
 *	SkAddrPromiscuousChange - set promiscuous mode for given port
 *
 * Description:
 *	This routine manages promiscuous mode:
 *	- none
 *	- all LLC frames
 *	- all MC frames
 *
 *	It calls either SkAddrXmacPromiscuousChange or
 *	SkAddrGmacPromiscuousChange, according to the adapter in use.
 *	The real work is done there.
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	SK_ADDR_SUCCESS
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrPromiscuousChange(
SK_AC	*pAC,			/* adapter context */
SK_IOC	IoC,			/* I/O context */
SK_U32	PortNumber,		/* port whose promiscuous mode changes */
int		NewPromMode)	/* new promiscuous mode */
{
	int ReturnCode = 0;
#if (!defined(SK_SLIM) || defined(DEBUG))
	if (PortNumber >= (SK_U32) pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}
#endif /* !SK_SLIM || DEBUG */

#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		ReturnCode =
			SkAddrXmacPromiscuousChange(pAC, IoC, PortNumber, NewPromMode);
	}
#endif /* GENESIS */
#ifdef YUKON
	if (!pAC->GIni.GIGenesis) {
		ReturnCode =
			SkAddrGmacPromiscuousChange(pAC, IoC, PortNumber, NewPromMode);
	}
#endif /* YUKON */

	return (ReturnCode);

}	/* SkAddrPromiscuousChange */

#ifdef GENESIS

/******************************************************************************
 *
 *	SkAddrXmacPromiscuousChange - set promiscuous mode for given port
 *
 * Description:
 *	This routine manages promiscuous mode:
 *	- none
 *	- all LLC frames
 *	- all MC frames
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	SK_ADDR_SUCCESS
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrXmacPromiscuousChange(
SK_AC	*pAC,			/* adapter context */
SK_IOC	IoC,			/* I/O context */
SK_U32	PortNumber,		/* port whose promiscuous mode changes */
int		NewPromMode)	/* new promiscuous mode */
{
	int			i;
	SK_BOOL		InexactModeBit;
	SK_U8		Inexact;
	SK_U8		HwInexact;
	SK_FILTER64	HwInexactFilter;
	SK_U16		LoMode;		/* Lower 16 bits of XMAC Mode Register. */
	int			CurPromMode = SK_PROM_MODE_NONE;

	/* Read CurPromMode from Hardware. */
	XM_IN16(IoC, PortNumber, XM_MODE, &LoMode);

	if ((LoMode & XM_MD_ENA_PROM) != 0) {
		/* Promiscuous mode! */
		CurPromMode |= SK_PROM_MODE_LLC;
	}
	
	for (Inexact = 0xFF, i = 0; i < 8; i++) {
		Inexact &= pAC->Addr.Port[PortNumber].InexactFilter.Bytes[i];
	}
	if (Inexact == 0xFF) {
		CurPromMode |= (pAC->Addr.Port[PortNumber].PromMode & SK_PROM_MODE_ALL_MC);
	}
	else {
		/* Get InexactModeBit (bit XM_MD_ENA_HASH in mode register) */
		XM_IN16(IoC, PortNumber, XM_MODE, &LoMode);
		
		InexactModeBit = (LoMode & XM_MD_ENA_HASH) != 0;

		/* Read 64-bit hash register from XMAC */
		XM_INHASH(IoC, PortNumber, XM_HSM, &HwInexactFilter.Bytes[0]);

		for (HwInexact = 0xFF, i = 0; i < 8; i++) {
			HwInexact &= HwInexactFilter.Bytes[i];
		}

		if (InexactModeBit && (HwInexact == 0xFF)) {
			CurPromMode |= SK_PROM_MODE_ALL_MC;
		}
	}

	pAC->Addr.Port[PortNumber].PromMode = NewPromMode;

	if (NewPromMode == CurPromMode) {
		return (SK_ADDR_SUCCESS);
	}

	if ((NewPromMode & SK_PROM_MODE_ALL_MC) &&
		!(CurPromMode & SK_PROM_MODE_ALL_MC)) {	/* All MC. */
		
		/* Set all bits in 64-bit hash register. */
		XM_OUTHASH(IoC, PortNumber, XM_HSM, &OnesHash);

		/* Enable Hashing */
		SkMacHashing(pAC, IoC, (int) PortNumber, SK_TRUE);
	}
	else if ((CurPromMode & SK_PROM_MODE_ALL_MC) &&
		!(NewPromMode & SK_PROM_MODE_ALL_MC)) {	/* Norm MC. */
		for (Inexact = 0, i = 0; i < 8; i++) {
			Inexact |= pAC->Addr.Port[PortNumber].InexactFilter.Bytes[i];
		}
		if (Inexact == 0) {
			/* Disable Hashing */
			SkMacHashing(pAC, IoC, (int) PortNumber, SK_FALSE);
		}
		else {
			/* Set 64-bit hash register to InexactFilter. */
			XM_OUTHASH(IoC, PortNumber, XM_HSM,
				&pAC->Addr.Port[PortNumber].InexactFilter.Bytes[0]);

			/* Enable Hashing */
			SkMacHashing(pAC, IoC, (int) PortNumber, SK_TRUE);
		}
	}

	if ((NewPromMode & SK_PROM_MODE_LLC) &&
		!(CurPromMode & SK_PROM_MODE_LLC)) {	/* Prom. LLC */
		/* Set the MAC in Promiscuous Mode */
		SkMacPromiscMode(pAC, IoC, (int) PortNumber, SK_TRUE);
	}
	else if ((CurPromMode & SK_PROM_MODE_LLC) &&
		!(NewPromMode & SK_PROM_MODE_LLC)) {	/* Norm. LLC. */
		/* Clear Promiscuous Mode */
		SkMacPromiscMode(pAC, IoC, (int) PortNumber, SK_FALSE);
	}
	
	return (SK_ADDR_SUCCESS);
	
}	/* SkAddrXmacPromiscuousChange */

#endif /* GENESIS */

#ifdef YUKON

/******************************************************************************
 *
 *	SkAddrGmacPromiscuousChange - set promiscuous mode for given port
 *
 * Description:
 *	This routine manages promiscuous mode:
 *	- none
 *	- all LLC frames
 *	- all MC frames
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	SK_ADDR_SUCCESS
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrGmacPromiscuousChange(
SK_AC	*pAC,			/* adapter context */
SK_IOC	IoC,			/* I/O context */
SK_U32	PortNumber,		/* port whose promiscuous mode changes */
int		NewPromMode)	/* new promiscuous mode */
{
	SK_U16		ReceiveControl;	/* GMAC Receive Control Register */
	int		CurPromMode = SK_PROM_MODE_NONE;

	/* Read CurPromMode from Hardware. */
	GM_IN16(IoC, PortNumber, GM_RX_CTRL, &ReceiveControl);

	if ((ReceiveControl & (GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA)) == 0) {
		/* Promiscuous mode! */
		CurPromMode |= SK_PROM_MODE_LLC;
	}

	if ((ReceiveControl & GM_RXCR_MCF_ENA) == 0) {
		/* All Multicast mode! */
		CurPromMode |= (pAC->Addr.Port[PortNumber].PromMode & SK_PROM_MODE_ALL_MC);
	}

	pAC->Addr.Port[PortNumber].PromMode = NewPromMode;

	if (NewPromMode == CurPromMode) {
		return (SK_ADDR_SUCCESS);
	}
	
	if ((NewPromMode & SK_PROM_MODE_ALL_MC) &&
		!(CurPromMode & SK_PROM_MODE_ALL_MC)) {	/* All MC */
		
		/* Set all bits in 64-bit hash register. */
		GM_OUTHASH(IoC, PortNumber, GM_MC_ADDR_H1, &OnesHash);
		
		/* Enable Hashing */
		SkMacHashing(pAC, IoC, (int) PortNumber, SK_TRUE);
	}
	
	if ((CurPromMode & SK_PROM_MODE_ALL_MC) &&
		!(NewPromMode & SK_PROM_MODE_ALL_MC)) {	/* Norm. MC */

		/* Set 64-bit hash register to InexactFilter. */
		GM_OUTHASH(IoC, PortNumber, GM_MC_ADDR_H1,
			&pAC->Addr.Port[PortNumber].InexactFilter.Bytes[0]);

		/* Enable Hashing. */
		SkMacHashing(pAC, IoC, (int) PortNumber, SK_TRUE);
	}

	if ((NewPromMode & SK_PROM_MODE_LLC) &&
		!(CurPromMode & SK_PROM_MODE_LLC)) {	/* Prom. LLC */
		
		/* Set the MAC to Promiscuous Mode. */
		SkMacPromiscMode(pAC, IoC, (int) PortNumber, SK_TRUE);
	}
	else if ((CurPromMode & SK_PROM_MODE_LLC) &&
		!(NewPromMode & SK_PROM_MODE_LLC)) {	/* Norm. LLC */
		
		/* Clear Promiscuous Mode. */
		SkMacPromiscMode(pAC, IoC, (int) PortNumber, SK_FALSE);
	}

	return (SK_ADDR_SUCCESS);
	
}	/* SkAddrGmacPromiscuousChange */

#endif /* YUKON */

#ifndef SK_SLIM

/******************************************************************************
 *
 *	SkAddrSwap - swap address info
 *
 * Description:
 *	This routine swaps address info of two ports.
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	SK_ADDR_SUCCESS
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrSwap(
SK_AC	*pAC,			/* adapter context */
SK_IOC	IoC,			/* I/O context */
SK_U32	FromPortNumber,		/* Port1 Index */
SK_U32	ToPortNumber)		/* Port2 Index */
{
	int			i;
	SK_U8		Byte;
	SK_MAC_ADDR	MacAddr;
	SK_U32		DWord;

	if (FromPortNumber >= (SK_U32) pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}

	if (ToPortNumber >= (SK_U32) pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}

	if (pAC->Rlmt.Port[FromPortNumber].Net != pAC->Rlmt.Port[ToPortNumber].Net) {
		return (SK_ADDR_ILLEGAL_PORT);
	}

	/*
	 * Swap:
	 * - Exact Match Entries (GEnesis and Yukon)
	 *   Yukon uses first entry for the logical MAC
	 *   address (stored in the second GMAC register).
	 * - FirstExactMatchRlmt (GEnesis only)
	 * - NextExactMatchRlmt (GEnesis only)
	 * - FirstExactMatchDrv (GEnesis only)
	 * - NextExactMatchDrv (GEnesis only)
	 * - 64-bit filter (InexactFilter)
	 * - Promiscuous Mode
	 * of ports.
	 */

	for (i = 0; i < SK_ADDR_EXACT_MATCHES; i++) {
		MacAddr = pAC->Addr.Port[FromPortNumber].Exact[i];
		pAC->Addr.Port[FromPortNumber].Exact[i] =
			pAC->Addr.Port[ToPortNumber].Exact[i];
		pAC->Addr.Port[ToPortNumber].Exact[i] = MacAddr;
	}

	for (i = 0; i < 8; i++) {
		Byte = pAC->Addr.Port[FromPortNumber].InexactFilter.Bytes[i];
		pAC->Addr.Port[FromPortNumber].InexactFilter.Bytes[i] =
			pAC->Addr.Port[ToPortNumber].InexactFilter.Bytes[i];
		pAC->Addr.Port[ToPortNumber].InexactFilter.Bytes[i] = Byte;
	}
	
	i = pAC->Addr.Port[FromPortNumber].PromMode;
	pAC->Addr.Port[FromPortNumber].PromMode = pAC->Addr.Port[ToPortNumber].PromMode;
	pAC->Addr.Port[ToPortNumber].PromMode = i;
	
	if (pAC->GIni.GIGenesis) {
		DWord = pAC->Addr.Port[FromPortNumber].FirstExactMatchRlmt;
		pAC->Addr.Port[FromPortNumber].FirstExactMatchRlmt =
			pAC->Addr.Port[ToPortNumber].FirstExactMatchRlmt;
		pAC->Addr.Port[ToPortNumber].FirstExactMatchRlmt = DWord;
		
		DWord = pAC->Addr.Port[FromPortNumber].NextExactMatchRlmt;
		pAC->Addr.Port[FromPortNumber].NextExactMatchRlmt =
			pAC->Addr.Port[ToPortNumber].NextExactMatchRlmt;
		pAC->Addr.Port[ToPortNumber].NextExactMatchRlmt = DWord;
		
		DWord = pAC->Addr.Port[FromPortNumber].FirstExactMatchDrv;
		pAC->Addr.Port[FromPortNumber].FirstExactMatchDrv =
			pAC->Addr.Port[ToPortNumber].FirstExactMatchDrv;
		pAC->Addr.Port[ToPortNumber].FirstExactMatchDrv = DWord;
		
		DWord = pAC->Addr.Port[FromPortNumber].NextExactMatchDrv;
		pAC->Addr.Port[FromPortNumber].NextExactMatchDrv =
			pAC->Addr.Port[ToPortNumber].NextExactMatchDrv;
		pAC->Addr.Port[ToPortNumber].NextExactMatchDrv = DWord;
	}
	
	/* CAUTION: Solution works if only ports of one adapter are in use. */
	for (i = 0; (SK_U32) i < pAC->Rlmt.Net[pAC->Rlmt.Port[ToPortNumber].
		Net->NetNumber].NumPorts; i++) {
		if (pAC->Rlmt.Net[pAC->Rlmt.Port[ToPortNumber].Net->NetNumber].
			Port[i]->PortNumber == ToPortNumber) {
			pAC->Addr.Net[pAC->Rlmt.Port[ToPortNumber].Net->NetNumber].
				ActivePort = i;
			/* 20001207 RA: Was "ToPortNumber;". */
		}
	}
	
	(void) SkAddrMcUpdate(pAC, IoC, FromPortNumber);
	(void) SkAddrMcUpdate(pAC, IoC, ToPortNumber);

	return (SK_ADDR_SUCCESS);
	
}	/* SkAddrSwap */

#endif /* !SK_SLIM */

#ifdef __cplusplus
}
#endif	/* __cplusplus */

