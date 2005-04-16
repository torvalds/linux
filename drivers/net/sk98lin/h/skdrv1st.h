/******************************************************************************
 *
 * Name:	skdrv1st.h
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.4 $
 * Date:	$Date: 2003/11/12 14:28:14 $
 * Purpose:	First header file for driver and all other modules
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
 * This is the first include file of the driver, which includes all
 * neccessary system header files and some of the GEnesis header files.
 * It also defines some basic items.
 *
 * Include File Hierarchy:
 *
 *	see skge.c
 *
 ******************************************************************************/

#ifndef __INC_SKDRV1ST_H
#define __INC_SKDRV1ST_H

/* Check kernel version */
#include <linux/version.h>

typedef struct s_AC	SK_AC;

/* Set card versions */
#define SK_FAR

/* override some default functions with optimized linux functions */

#define SK_PNMI_STORE_U16(p,v)		memcpy((char*)(p),(char*)&(v),2)
#define SK_PNMI_STORE_U32(p,v)		memcpy((char*)(p),(char*)&(v),4)
#define SK_PNMI_STORE_U64(p,v)		memcpy((char*)(p),(char*)&(v),8)
#define SK_PNMI_READ_U16(p,v)		memcpy((char*)&(v),(char*)(p),2)
#define SK_PNMI_READ_U32(p,v)		memcpy((char*)&(v),(char*)(p),4)
#define SK_PNMI_READ_U64(p,v)		memcpy((char*)&(v),(char*)(p),8)

#define SK_ADDR_EQUAL(a1,a2)		(!memcmp(a1,a2,6))

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/bitops.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <linux/init.h>
#include <asm/uaccess.h>
#include <net/checksum.h>

#define SK_CS_CALCULATE_CHECKSUM
#ifndef CONFIG_X86_64
#define SkCsCalculateChecksum(p,l)	((~ip_compute_csum(p, l)) & 0xffff)
#else
#define SkCsCalculateChecksum(p,l)	((~ip_fast_csum(p, l)) & 0xffff)
#endif

#include	"h/sktypes.h"
#include	"h/skerror.h"
#include	"h/skdebug.h"
#include	"h/lm80.h"
#include	"h/xmac_ii.h"

#ifdef __LITTLE_ENDIAN
#define SK_LITTLE_ENDIAN
#else
#define SK_BIG_ENDIAN
#endif

#define SK_NET_DEVICE	net_device


/* we use gethrtime(), return unit: nanoseconds */
#define SK_TICKS_PER_SEC	100

#define	SK_MEM_MAPPED_IO

// #define SK_RLMT_SLOW_LOOKAHEAD

#define SK_MAX_MACS		2
#define SK_MAX_NETS		2

#define SK_IOC			char __iomem *

typedef struct s_DrvRlmtMbuf SK_MBUF;

#define	SK_CONST64	INT64_C
#define	SK_CONSTU64	UINT64_C

#define SK_MEMCPY(dest,src,size)	memcpy(dest,src,size)
#define SK_MEMCMP(s1,s2,size)		memcmp(s1,s2,size)
#define SK_MEMSET(dest,val,size)	memset(dest,val,size)
#define SK_STRLEN(pStr)			strlen((char*)(pStr))
#define SK_STRNCPY(pDest,pSrc,size)	strncpy((char*)(pDest),(char*)(pSrc),size)
#define SK_STRCMP(pStr1,pStr2)		strcmp((char*)(pStr1),(char*)(pStr2))

/* macros to access the adapter */
#define SK_OUT8(b,a,v)		writeb((v), ((b)+(a)))	
#define SK_OUT16(b,a,v)		writew((v), ((b)+(a)))	
#define SK_OUT32(b,a,v)		writel((v), ((b)+(a)))	
#define SK_IN8(b,a,pv)		(*(pv) = readb((b)+(a)))
#define SK_IN16(b,a,pv)		(*(pv) = readw((b)+(a)))
#define SK_IN32(b,a,pv)		(*(pv) = readl((b)+(a)))

#define int8_t		char
#define int16_t		short
#define int32_t		long
#define int64_t		long long
#define uint8_t		u_char
#define uint16_t	u_short
#define uint32_t	u_long
#define uint64_t	unsigned long long
#define t_scalar_t	int
#define t_uscalar_t	unsigned int
#define uintptr_t	unsigned long

#define __CONCAT__(A,B) A##B

#define INT32_C(a)		__CONCAT__(a,L)
#define INT64_C(a)		__CONCAT__(a,LL)
#define UINT32_C(a)		__CONCAT__(a,UL)
#define UINT64_C(a)		__CONCAT__(a,ULL)

#ifdef DEBUG
#define SK_DBG_PRINTF		printk
#ifndef SK_DEBUG_CHKMOD
#define SK_DEBUG_CHKMOD		0
#endif
#ifndef SK_DEBUG_CHKCAT
#define SK_DEBUG_CHKCAT		0
#endif
/* those come from the makefile */
#define SK_DBG_CHKMOD(pAC)	(SK_DEBUG_CHKMOD)
#define SK_DBG_CHKCAT(pAC)	(SK_DEBUG_CHKCAT)

extern void SkDbgPrintf(const char *format,...);

#define SK_DBGMOD_DRV			0x00010000

/**** possible driver debug categories ********************************/
#define SK_DBGCAT_DRV_ENTRY		0x00010000
#define SK_DBGCAT_DRV_SAP		0x00020000
#define SK_DBGCAT_DRV_MCA		0x00040000
#define SK_DBGCAT_DRV_TX_PROGRESS	0x00080000
#define SK_DBGCAT_DRV_RX_PROGRESS	0x00100000
#define SK_DBGCAT_DRV_PROGRESS		0x00200000
#define SK_DBGCAT_DRV_MSG		0x00400000
#define SK_DBGCAT_DRV_PROM		0x00800000
#define SK_DBGCAT_DRV_TX_FRAME		0x01000000
#define SK_DBGCAT_DRV_ERROR		0x02000000
#define SK_DBGCAT_DRV_INT_SRC		0x04000000
#define SK_DBGCAT_DRV_EVENT		0x08000000

#endif

#define SK_ERR_LOG		SkErrorLog

extern void SkErrorLog(SK_AC*, int, int, char*);

#endif

