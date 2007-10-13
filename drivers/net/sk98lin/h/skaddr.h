/******************************************************************************
 *
 * Name:	skaddr.h
 * Project:	Gigabit Ethernet Adapters, ADDR-Modul
 * Version:	$Revision: 1.29 $
 * Date:	$Date: 2003/05/13 16:57:24 $
 * Purpose:	Header file for Address Management (MC, UC, Prom).
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
 * This module is intended to manage multicast addresses and promiscuous mode
 * on GEnesis adapters.
 *
 * Include File Hierarchy:
 *
 *	"skdrv1st.h"
 *	...
 *	"sktypes.h"
 *	"skqueue.h"
 *	"skaddr.h"
 *	...
 *	"skdrv2nd.h"
 *
 ******************************************************************************/

#ifndef __INC_SKADDR_H
#define __INC_SKADDR_H

#ifdef __cplusplus
extern "C" {
#endif	/* cplusplus */

/* defines ********************************************************************/

#define SK_MAC_ADDR_LEN				6	/* Length of MAC address. */
#define	SK_MAX_ADDRS				14	/* #Addrs for exact match. */

/* ----- Common return values ----- */

#define SK_ADDR_SUCCESS				0	/* Function returned successfully. */
#define SK_ADDR_ILLEGAL_PORT			100	/* Port number too high. */
#define SK_ADDR_TOO_EARLY			101	/* Function called too early. */

/* ----- Clear/Add flag bits ----- */

#define SK_ADDR_PERMANENT			1	/* RLMT Address */

/* ----- Additional Clear flag bits ----- */

#define SK_MC_SW_ONLY				2	/* Do not update HW when clearing. */

/* ----- Override flag bits ----- */

#define SK_ADDR_LOGICAL_ADDRESS		0
#define SK_ADDR_VIRTUAL_ADDRESS		(SK_ADDR_LOGICAL_ADDRESS)	/* old */
#define SK_ADDR_PHYSICAL_ADDRESS	1
#define SK_ADDR_CLEAR_LOGICAL		2
#define SK_ADDR_SET_LOGICAL			4

/* ----- Override return values ----- */

#define SK_ADDR_OVERRIDE_SUCCESS	(SK_ADDR_SUCCESS)
#define SK_ADDR_DUPLICATE_ADDRESS	1
#define SK_ADDR_MULTICAST_ADDRESS	2

/* ----- Partitioning of excact match table ----- */

#define SK_ADDR_EXACT_MATCHES		16	/* #Exact match entries. */

#define SK_ADDR_FIRST_MATCH_RLMT	1
#define SK_ADDR_LAST_MATCH_RLMT		2
#define SK_ADDR_FIRST_MATCH_DRV		3
#define SK_ADDR_LAST_MATCH_DRV		(SK_ADDR_EXACT_MATCHES - 1)

/* ----- SkAddrMcAdd/SkAddrMcUpdate return values ----- */

#define SK_MC_FILTERING_EXACT		0	/* Exact filtering. */
#define SK_MC_FILTERING_INEXACT		1	/* Inexact filtering. */

/* ----- Additional SkAddrMcAdd return values ----- */

#define SK_MC_ILLEGAL_ADDRESS		2	/* Illegal address. */
#define SK_MC_ILLEGAL_PORT			3	/* Illegal port (not the active one). */
#define SK_MC_RLMT_OVERFLOW			4	/* Too many RLMT mc addresses. */

/* Promiscuous mode bits ----- */

#define SK_PROM_MODE_NONE			0	/* Normal receive. */
#define SK_PROM_MODE_LLC			1	/* Receive all LLC frames. */
#define SK_PROM_MODE_ALL_MC			2	/* Receive all multicast frames. */
/* #define SK_PROM_MODE_NON_LLC		4 */	/* Receive all non-LLC frames. */

/* Macros */

#ifdef OLD_STUFF
#ifndef SK_ADDR_EQUAL
/*
 * "&" instead of "&&" allows better optimization on IA-64.
 * The replacement is safe here, as all bytes exist.
 */
#ifndef SK_ADDR_DWORD_COMPARE
#define SK_ADDR_EQUAL(A1,A2)	( \
	(((SK_U8 *)(A1))[5] == ((SK_U8 *)(A2))[5]) & \
	(((SK_U8 *)(A1))[4] == ((SK_U8 *)(A2))[4]) & \
	(((SK_U8 *)(A1))[3] == ((SK_U8 *)(A2))[3]) & \
	(((SK_U8 *)(A1))[2] == ((SK_U8 *)(A2))[2]) & \
	(((SK_U8 *)(A1))[1] == ((SK_U8 *)(A2))[1]) & \
	(((SK_U8 *)(A1))[0] == ((SK_U8 *)(A2))[0]))
#else	/* SK_ADDR_DWORD_COMPARE */
#define SK_ADDR_EQUAL(A1,A2)	( \
	(*(SK_U32 *)&(((SK_U8 *)(A1))[2]) == *(SK_U32 *)&(((SK_U8 *)(A2))[2])) & \
	(*(SK_U32 *)&(((SK_U8 *)(A1))[0]) == *(SK_U32 *)&(((SK_U8 *)(A2))[0])))
#endif	/* SK_ADDR_DWORD_COMPARE */
#endif	/* SK_ADDR_EQUAL */
#endif /* 0 */

#ifndef SK_ADDR_EQUAL
#ifndef SK_ADDR_DWORD_COMPARE
#define SK_ADDR_EQUAL(A1,A2)	( \
	(((SK_U8 SK_FAR *)(A1))[5] == ((SK_U8 SK_FAR *)(A2))[5]) & \
	(((SK_U8 SK_FAR *)(A1))[4] == ((SK_U8 SK_FAR *)(A2))[4]) & \
	(((SK_U8 SK_FAR *)(A1))[3] == ((SK_U8 SK_FAR *)(A2))[3]) & \
	(((SK_U8 SK_FAR *)(A1))[2] == ((SK_U8 SK_FAR *)(A2))[2]) & \
	(((SK_U8 SK_FAR *)(A1))[1] == ((SK_U8 SK_FAR *)(A2))[1]) & \
	(((SK_U8 SK_FAR *)(A1))[0] == ((SK_U8 SK_FAR *)(A2))[0]))
#else	/* SK_ADDR_DWORD_COMPARE */
#define SK_ADDR_EQUAL(A1,A2)	( \
	(*(SK_U16 SK_FAR *)&(((SK_U8 SK_FAR *)(A1))[4]) == \
	*(SK_U16 SK_FAR *)&(((SK_U8 SK_FAR *)(A2))[4])) && \
	(*(SK_U32 SK_FAR *)&(((SK_U8 SK_FAR *)(A1))[0]) == \
	*(SK_U32 SK_FAR *)&(((SK_U8 SK_FAR *)(A2))[0])))
#endif	/* SK_ADDR_DWORD_COMPARE */
#endif	/* SK_ADDR_EQUAL */

/* typedefs *******************************************************************/

typedef struct s_MacAddr {
	SK_U8	a[SK_MAC_ADDR_LEN];
} SK_MAC_ADDR;


/* SK_FILTER is used to ensure alignment of the filter. */
typedef union s_InexactFilter {
	SK_U8	Bytes[8];
	SK_U64	Val;	/* Dummy entry for alignment only. */
} SK_FILTER64;


typedef struct s_AddrNet SK_ADDR_NET;


typedef struct s_AddrPort {

/* ----- Public part (read-only) ----- */

	SK_MAC_ADDR	CurrentMacAddress;	/* Current physical MAC Address. */
	SK_MAC_ADDR	PermanentMacAddress;	/* Permanent physical MAC Address. */
	int		PromMode;		/* Promiscuous Mode. */

/* ----- Private part ----- */

	SK_MAC_ADDR	PreviousMacAddress;	/* Prev. phys. MAC Address. */
	SK_BOOL		CurrentMacAddressSet;	/* CurrentMacAddress is set. */
	SK_U8		Align01;

	SK_U32		FirstExactMatchRlmt;
	SK_U32		NextExactMatchRlmt;
	SK_U32		FirstExactMatchDrv;
	SK_U32		NextExactMatchDrv;
	SK_MAC_ADDR	Exact[SK_ADDR_EXACT_MATCHES];
	SK_FILTER64	InexactFilter;			/* For 64-bit hash register. */
	SK_FILTER64	InexactRlmtFilter;		/* For 64-bit hash register. */
	SK_FILTER64	InexactDrvFilter;		/* For 64-bit hash register. */
} SK_ADDR_PORT;


struct s_AddrNet {
/* ----- Public part (read-only) ----- */

	SK_MAC_ADDR		CurrentMacAddress;	/* Logical MAC Address. */
	SK_MAC_ADDR		PermanentMacAddress;	/* Logical MAC Address. */

/* ----- Private part ----- */

	SK_U32			ActivePort;		/* View of module ADDR. */
	SK_BOOL			CurrentMacAddressSet;	/* CurrentMacAddress is set. */
	SK_U8			Align01;
	SK_U16			Align02;
};


typedef struct s_Addr {

/* ----- Public part (read-only) ----- */

	SK_ADDR_NET		Net[SK_MAX_NETS];
	SK_ADDR_PORT	Port[SK_MAX_MACS];

/* ----- Private part ----- */
} SK_ADDR;

/* function prototypes ********************************************************/

#ifndef SK_KR_PROTO

/* Functions provided by SkAddr */

/* ANSI/C++ compliant function prototypes */

extern	int	SkAddrInit(
	SK_AC	*pAC,
	SK_IOC	IoC,
	int	Level);

extern	int	SkAddrMcClear(
	SK_AC	*pAC,
	SK_IOC	IoC,
	SK_U32	PortNumber,
	int	Flags);

extern	int	SkAddrMcAdd(
	SK_AC		*pAC,
	SK_IOC		IoC,
	SK_U32		PortNumber,
	SK_MAC_ADDR	*pMc,
	int		Flags);

extern	int	SkAddrMcUpdate(
	SK_AC	*pAC,
	SK_IOC	IoC,
	SK_U32	PortNumber);

extern	int	SkAddrOverride(
	SK_AC		*pAC,
	SK_IOC		IoC,
	SK_U32		PortNumber,
	SK_MAC_ADDR	SK_FAR *pNewAddr,
	int		Flags);

extern	int	SkAddrPromiscuousChange(
	SK_AC	*pAC,
	SK_IOC	IoC,
	SK_U32	PortNumber,
	int	NewPromMode);

#ifndef SK_SLIM
extern	int	SkAddrSwap(
	SK_AC	*pAC,
	SK_IOC	IoC,
	SK_U32	FromPortNumber,
	SK_U32	ToPortNumber);
#endif

#else	/* defined(SK_KR_PROTO)) */

/* Non-ANSI/C++ compliant function prototypes */

#error KR-style prototypes are not yet provided.

#endif	/* defined(SK_KR_PROTO)) */


#ifdef __cplusplus
}
#endif	/* __cplusplus */

#endif	/* __INC_SKADDR_H */
