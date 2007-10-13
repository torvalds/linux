/*****************************************************************************
 *
 * Name:	skgepnm2.h
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.36 $
 * Date:	$Date: 2003/05/23 12:45:13 $
 * Purpose:	Defines for Private Network Management Interface
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

#ifndef _SKGEPNM2_H_
#define _SKGEPNM2_H_

/*
 * General definitions
 */
#define SK_PNMI_CHIPSET_XMAC	1	/* XMAC11800FP */
#define SK_PNMI_CHIPSET_YUKON	2	/* YUKON */

#define	SK_PNMI_BUS_PCI		1	/* PCI bus*/

/*
 * Actions
 */
#define SK_PNMI_ACT_IDLE		1
#define SK_PNMI_ACT_RESET		2
#define SK_PNMI_ACT_SELFTEST	3
#define SK_PNMI_ACT_RESETCNT	4

/*
 * VPD releated defines
 */

#define SK_PNMI_VPD_RW		1
#define SK_PNMI_VPD_RO		2

#define SK_PNMI_VPD_OK			0
#define SK_PNMI_VPD_NOTFOUND	1
#define SK_PNMI_VPD_CUT			2
#define SK_PNMI_VPD_TIMEOUT		3
#define SK_PNMI_VPD_FULL		4
#define SK_PNMI_VPD_NOWRITE		5
#define SK_PNMI_VPD_FATAL		6

#define SK_PNMI_VPD_IGNORE	0
#define SK_PNMI_VPD_CREATE	1
#define SK_PNMI_VPD_DELETE	2


/*
 * RLMT related defines
 */
#define SK_PNMI_DEF_RLMT_CHG_THRES	240	/* 4 changes per minute */


/*
 * VCT internal status values
 */
#define SK_PNMI_VCT_PENDING	32
#define SK_PNMI_VCT_TEST_DONE	64
#define SK_PNMI_VCT_LINK	128

/*
 * Internal table definitions
 */
#define SK_PNMI_GET		0
#define SK_PNMI_PRESET	1
#define SK_PNMI_SET		2

#define SK_PNMI_RO		0
#define SK_PNMI_RW		1
#define SK_PNMI_WO		2

typedef struct s_OidTabEntry {
	SK_U32			Id;
	SK_U32			InstanceNo;
	unsigned int	StructSize;
	unsigned int	Offset;
	int				Access;
	int				(* Func)(SK_AC *pAc, SK_IOC pIo, int action,
							 SK_U32 Id, char* pBuf, unsigned int* pLen,
							 SK_U32 Instance, unsigned int TableIndex,
							 SK_U32 NetNumber);
	SK_U16			Param;
} SK_PNMI_TAB_ENTRY;


/*
 * Trap lengths
 */
#define SK_PNMI_TRAP_SIMPLE_LEN			17
#define SK_PNMI_TRAP_SENSOR_LEN_BASE	46
#define SK_PNMI_TRAP_RLMT_CHANGE_LEN	23
#define SK_PNMI_TRAP_RLMT_PORT_LEN		23

/*
 * Number of MAC types supported
 */
#define SK_PNMI_MAC_TYPES	(SK_MAC_GMAC + 1)

/*
 * MAC statistic data list (overall set for MAC types used)
 */
enum SK_MACSTATS {
	SK_PNMI_HTX				= 0,
	SK_PNMI_HTX_OCTET,
	SK_PNMI_HTX_OCTETHIGH 	= SK_PNMI_HTX_OCTET,
	SK_PNMI_HTX_OCTETLOW,
	SK_PNMI_HTX_BROADCAST,
	SK_PNMI_HTX_MULTICAST,
	SK_PNMI_HTX_UNICAST,
	SK_PNMI_HTX_BURST,
	SK_PNMI_HTX_PMACC,
	SK_PNMI_HTX_MACC,
	SK_PNMI_HTX_COL,
	SK_PNMI_HTX_SINGLE_COL,
	SK_PNMI_HTX_MULTI_COL,
	SK_PNMI_HTX_EXCESS_COL,
	SK_PNMI_HTX_LATE_COL,
	SK_PNMI_HTX_DEFFERAL,
	SK_PNMI_HTX_EXCESS_DEF,
	SK_PNMI_HTX_UNDERRUN,
	SK_PNMI_HTX_CARRIER,
	SK_PNMI_HTX_UTILUNDER,
	SK_PNMI_HTX_UTILOVER,
	SK_PNMI_HTX_64,
	SK_PNMI_HTX_127,
	SK_PNMI_HTX_255,
	SK_PNMI_HTX_511,
	SK_PNMI_HTX_1023,
	SK_PNMI_HTX_MAX,
	SK_PNMI_HTX_LONGFRAMES,
	SK_PNMI_HTX_SYNC,
	SK_PNMI_HTX_SYNC_OCTET,
	SK_PNMI_HTX_RESERVED,
	
	SK_PNMI_HRX,
	SK_PNMI_HRX_OCTET,
	SK_PNMI_HRX_OCTETHIGH	= SK_PNMI_HRX_OCTET,
	SK_PNMI_HRX_OCTETLOW,
	SK_PNMI_HRX_BADOCTET,
	SK_PNMI_HRX_BADOCTETHIGH = SK_PNMI_HRX_BADOCTET,
	SK_PNMI_HRX_BADOCTETLOW,
	SK_PNMI_HRX_BROADCAST,
	SK_PNMI_HRX_MULTICAST,
	SK_PNMI_HRX_UNICAST,
	SK_PNMI_HRX_PMACC,
	SK_PNMI_HRX_MACC,
	SK_PNMI_HRX_PMACC_ERR,
	SK_PNMI_HRX_MACC_UNKWN,
	SK_PNMI_HRX_BURST,
	SK_PNMI_HRX_MISSED,
	SK_PNMI_HRX_FRAMING,
	SK_PNMI_HRX_UNDERSIZE,
	SK_PNMI_HRX_OVERFLOW,
	SK_PNMI_HRX_JABBER,
	SK_PNMI_HRX_CARRIER,
	SK_PNMI_HRX_IRLENGTH,
	SK_PNMI_HRX_SYMBOL,
	SK_PNMI_HRX_SHORTS,
	SK_PNMI_HRX_RUNT,
	SK_PNMI_HRX_TOO_LONG,
	SK_PNMI_HRX_FCS,
	SK_PNMI_HRX_CEXT,
	SK_PNMI_HRX_UTILUNDER,
	SK_PNMI_HRX_UTILOVER,
	SK_PNMI_HRX_64,
	SK_PNMI_HRX_127,
	SK_PNMI_HRX_255,
	SK_PNMI_HRX_511,
	SK_PNMI_HRX_1023,
	SK_PNMI_HRX_MAX,
	SK_PNMI_HRX_LONGFRAMES,
	
	SK_PNMI_HRX_RESERVED,
	
	SK_PNMI_MAX_IDX		/* NOTE: Ensure SK_PNMI_CNT_NO is set to this value */
};

/*
 * MAC specific data
 */
typedef struct s_PnmiStatAddr {
	SK_U16		Reg;		/* MAC register containing the value */
	SK_BOOL		GetOffset;	/* TRUE: Offset managed by PNMI (call GetStatVal())*/
} SK_PNMI_STATADDR;


/*
 * SK_PNMI_STRUCT_DATA copy offset evaluation macros
 */
#define SK_PNMI_OFF(e)		((SK_U32)(SK_UPTR)&(((SK_PNMI_STRUCT_DATA *)0)->e))
#define SK_PNMI_MAI_OFF(e)	((SK_U32)(SK_UPTR)&(((SK_PNMI_STRUCT_DATA *)0)->e))
#define SK_PNMI_VPD_OFF(e)	((SK_U32)(SK_UPTR)&(((SK_PNMI_VPD *)0)->e))
#define SK_PNMI_SEN_OFF(e)	((SK_U32)(SK_UPTR)&(((SK_PNMI_SENSOR *)0)->e))
#define SK_PNMI_CHK_OFF(e)	((SK_U32)(SK_UPTR)&(((SK_PNMI_CHECKSUM *)0)->e))
#define SK_PNMI_STA_OFF(e)	((SK_U32)(SK_UPTR)&(((SK_PNMI_STAT *)0)->e))
#define SK_PNMI_CNF_OFF(e)	((SK_U32)(SK_UPTR)&(((SK_PNMI_CONF *)0)->e))
#define SK_PNMI_RLM_OFF(e)	((SK_U32)(SK_UPTR)&(((SK_PNMI_RLMT *)0)->e))
#define SK_PNMI_MON_OFF(e)	((SK_U32)(SK_UPTR)&(((SK_PNMI_RLMT_MONITOR *)0)->e))
#define SK_PNMI_TRP_OFF(e)	((SK_U32)(SK_UPTR)&(((SK_PNMI_TRAP *)0)->e))

#define SK_PNMI_SET_STAT(b,s,o)	{SK_U32	Val32; char *pVal; \
					Val32 = (s); \
					pVal = (char *)(b) + ((SK_U32)(SK_UPTR) \
						&(((SK_PNMI_STRUCT_DATA *)0)-> \
						ReturnStatus.ErrorStatus)); \
					SK_PNMI_STORE_U32(pVal, Val32); \
					Val32 = (o); \
					pVal = (char *)(b) + ((SK_U32)(SK_UPTR) \
						&(((SK_PNMI_STRUCT_DATA *)0)-> \
						ReturnStatus.ErrorOffset)); \
					SK_PNMI_STORE_U32(pVal, Val32);}

/*
 * Time macros
 */
#ifndef SK_PNMI_HUNDREDS_SEC
#if SK_TICKS_PER_SEC == 100
#define SK_PNMI_HUNDREDS_SEC(t)	(t)
#else
#define SK_PNMI_HUNDREDS_SEC(t)	(((t) * 100) / (SK_TICKS_PER_SEC))
#endif /* !SK_TICKS_PER_SEC */
#endif /* !SK_PNMI_HUNDREDS_SEC */

/*
 * Macros to work around alignment problems
 */
#ifndef SK_PNMI_STORE_U16
#define SK_PNMI_STORE_U16(p,v)	{*(char *)(p) = *((char *)&(v)); \
					*((char *)(p) + 1) = \
						*(((char *)&(v)) + 1);}
#endif

#ifndef SK_PNMI_STORE_U32
#define SK_PNMI_STORE_U32(p,v)	{*(char *)(p) = *((char *)&(v)); \
					*((char *)(p) + 1) = \
						*(((char *)&(v)) + 1); \
					*((char *)(p) + 2) = \
						*(((char *)&(v)) + 2); \
					*((char *)(p) + 3) = \
						*(((char *)&(v)) + 3);}
#endif

#ifndef SK_PNMI_STORE_U64
#define SK_PNMI_STORE_U64(p,v)	{*(char *)(p) = *((char *)&(v)); \
					*((char *)(p) + 1) = \
						*(((char *)&(v)) + 1); \
					*((char *)(p) + 2) = \
						*(((char *)&(v)) + 2); \
					*((char *)(p) + 3) = \
						*(((char *)&(v)) + 3); \
					*((char *)(p) + 4) = \
						*(((char *)&(v)) + 4); \
					*((char *)(p) + 5) = \
						*(((char *)&(v)) + 5); \
					*((char *)(p) + 6) = \
						*(((char *)&(v)) + 6); \
					*((char *)(p) + 7) = \
						*(((char *)&(v)) + 7);}
#endif

#ifndef SK_PNMI_READ_U16
#define SK_PNMI_READ_U16(p,v)	{*((char *)&(v)) = *(char *)(p); \
					*(((char *)&(v)) + 1) = \
						*((char *)(p) + 1);}
#endif

#ifndef SK_PNMI_READ_U32
#define SK_PNMI_READ_U32(p,v)	{*((char *)&(v)) = *(char *)(p); \
					*(((char *)&(v)) + 1) = \
						*((char *)(p) + 1); \
					*(((char *)&(v)) + 2) = \
						*((char *)(p) + 2); \
					*(((char *)&(v)) + 3) = \
						*((char *)(p) + 3);}
#endif

#ifndef SK_PNMI_READ_U64
#define SK_PNMI_READ_U64(p,v)	{*((char *)&(v)) = *(char *)(p); \
					*(((char *)&(v)) + 1) = \
						*((char *)(p) + 1); \
					*(((char *)&(v)) + 2) = \
						*((char *)(p) + 2); \
					*(((char *)&(v)) + 3) = \
						*((char *)(p) + 3); \
					*(((char *)&(v)) + 4) = \
						*((char *)(p) + 4); \
					*(((char *)&(v)) + 5) = \
						*((char *)(p) + 5); \
					*(((char *)&(v)) + 6) = \
						*((char *)(p) + 6); \
					*(((char *)&(v)) + 7) = \
						*((char *)(p) + 7);}
#endif

/*
 * Macros for Debug
 */
#ifdef DEBUG

#define SK_PNMI_CHECKFLAGS(vSt)	{if (pAC->Pnmi.MacUpdatedFlag > 0 || \
					pAC->Pnmi.RlmtUpdatedFlag > 0 || \
					pAC->Pnmi.SirqUpdatedFlag > 0) { \
						SK_DBG_MSG(pAC, \
						SK_DBGMOD_PNMI, \
						SK_DBGCAT_CTRL,	\
						("PNMI: ERR: %s MacUFlag=%d, RlmtUFlag=%d, SirqUFlag=%d\n", \
						vSt, \
						pAC->Pnmi.MacUpdatedFlag, \
						pAC->Pnmi.RlmtUpdatedFlag, \
						pAC->Pnmi.SirqUpdatedFlag))}}

#else	/* !DEBUG */

#define SK_PNMI_CHECKFLAGS(vSt)	/* Nothing */

#endif	/* !DEBUG */

#endif	/* _SKGEPNM2_H_ */
