/* SPDX-License-Identifier: GPL-2.0 */
/*
 * OS Abstraction Layer
 *
 * Copyright (C) 1999-2019, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: osl.h 813810 2019-04-08 12:25:30Z $
 */

#ifndef _osl_h_
#define _osl_h_

#include <osl_decl.h>

enum {
	TAIL_BYTES_TYPE_FCS = 1,
	TAIL_BYTES_TYPE_ICV = 2,
	TAIL_BYTES_TYPE_MIC = 3
};

#define OSL_PKTTAG_SZ   48 /* standard linux pkttag size is 48 bytes */

/* Drivers use PKTFREESETCB to register a callback function when a packet is freed by OSL */
typedef void (*pktfree_cb_fn_t)(void *ctx, void *pkt, unsigned int status);

/* Drivers use REGOPSSET() to register register read/write funcitons */
typedef unsigned int (*osl_rreg_fn_t)(void *ctx, volatile void *reg, unsigned int size);
typedef void  (*osl_wreg_fn_t)(void *ctx, volatile void *reg, unsigned int val, unsigned int size);

#if defined(WL_UNITTEST)
#include <utest_osl.h>
#else
#include <linux_osl.h>
#include <linux_pkt.h>
#endif // endif

#ifndef PKTDBG_TRACE
#define PKTDBG_TRACE(osh, pkt, bit)	BCM_REFERENCE(osh)
#endif // endif

#define PKTCTFMAP(osh, p)		BCM_REFERENCE(osh)

/* --------------------------------------------------------------------------
** Register manipulation macros.
*/

#define	SET_REG(osh, r, mask, val)	W_REG((osh), (r), ((R_REG((osh), r) & ~(mask)) | (val)))

#ifndef AND_REG
#define AND_REG(osh, r, v)		W_REG(osh, (r), R_REG(osh, r) & (v))
#endif   /* !AND_REG */

#ifndef OR_REG
#define OR_REG(osh, r, v)		W_REG(osh, (r), R_REG(osh, r) | (v))
#endif   /* !OR_REG */

#if !defined(OSL_SYSUPTIME)
#define OSL_SYSUPTIME() (0)
#define OSL_SYSUPTIME_NOT_DEFINED 1
#endif /* !defined(OSL_SYSUPTIME) */

#if !defined(OSL_SYSUPTIME_US)
#define OSL_SYSUPTIME_US() (0)
#define OSL_SYSUPTIME_US_NOT_DEFINED 1
#endif /* !defined(OSL_SYSUPTIME) */

#if defined(OSL_SYSUPTIME_NOT_DEFINED) && defined(OSL_SYSUPTIME_US_NOT_DEFINED)
#define OSL_SYSUPTIME_SUPPORT FALSE
#else
#define OSL_SYSUPTIME_SUPPORT TRUE
#endif /* OSL_SYSUPTIME */

#ifndef OSL_GET_LOCALTIME
#define OSL_GET_LOCALTIME(sec, usec)	\
	do { \
		BCM_REFERENCE(sec); \
		BCM_REFERENCE(usec); \
	} while (0)
#endif /* OSL_GET_LOCALTIME */

#ifndef OSL_LOCALTIME_NS
#define OSL_LOCALTIME_NS()	(OSL_SYSUPTIME_US() * NSEC_PER_USEC)
#endif /* OSL_LOCALTIME_NS */

#ifndef OSL_SYSTZTIME_US
#define OSL_SYSTZTIME_US()	OSL_SYSUPTIME_US()
#endif /* OSL_GET_SYSTZTIME */

#ifndef OSL_SYS_HALT
#define OSL_SYS_HALT()	do {} while (0)
#endif // endif

#ifndef DMB
#if defined(STB)
#define DMB()	mb();
#else /* STB */
#define DMB()	do {} while (0)
#endif /* STB */
#endif /* DMB */

#ifndef OSL_MEM_AVAIL
#define OSL_MEM_AVAIL()	(0xffffffff)
#endif // endif

#ifndef OSL_OBFUSCATE_BUF
/* For security reasons printing pointers is not allowed.
 * Some OSLs implement OSL_OBFUSCATE_BUF to OS specific obfuscate API.
 * If OSL_OBFUSCATE_BUF() is not implemented in OSL, then default to
 * printing the input pointer
 */
#define OSL_OBFUSCATE_BUF(x) (x)
#endif /* OSL_OBFUSCATE_BUF */

#if !defined(PKTC_DONGLE)

#define	PKTCGETATTR(skb)	(0)
#define	PKTCSETATTR(skb, f, p, b) BCM_REFERENCE(skb)
#define	PKTCCLRATTR(skb)	BCM_REFERENCE(skb)
#define	PKTCCNT(skb)		(1)
#define	PKTCLEN(skb)		PKTLEN(NULL, skb)
#define	PKTCGETFLAGS(skb)	(0)
#define	PKTCSETFLAGS(skb, f)	BCM_REFERENCE(skb)
#define	PKTCCLRFLAGS(skb)	BCM_REFERENCE(skb)
#define	PKTCFLAGS(skb)		(0)
#define	PKTCSETCNT(skb, c)	BCM_REFERENCE(skb)
#define	PKTCINCRCNT(skb)	BCM_REFERENCE(skb)
#define	PKTCADDCNT(skb, c)	BCM_REFERENCE(skb)
#define	PKTCSETLEN(skb, l)	BCM_REFERENCE(skb)
#define	PKTCADDLEN(skb, l)	BCM_REFERENCE(skb)
#define	PKTCSETFLAG(skb, fb)	BCM_REFERENCE(skb)
#define	PKTCCLRFLAG(skb, fb)	BCM_REFERENCE(skb)
#define	PKTCLINK(skb)		NULL
#define	PKTSETCLINK(skb, x)	BCM_REFERENCE(skb)
#define FOREACH_CHAINED_PKT(skb, nskb) \
	for ((nskb) = NULL; (skb) != NULL; (skb) = (nskb))
#define	PKTCFREE		PKTFREE
#define PKTCENQTAIL(h, t, p) \
do { \
	if ((t) == NULL) { \
		(h) = (t) = (p); \
	} \
} while (0)
#endif // endif

#ifndef PKTSETCHAINED
#define PKTSETCHAINED(osh, skb)		BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTCLRCHAINED
#define PKTCLRCHAINED(osh, skb)		BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTISCHAINED
#define PKTISCHAINED(skb)		FALSE
#endif // endif

/* Lbuf with fraglist */
#ifndef PKTFRAGPKTID
#define PKTFRAGPKTID(osh, lb)		(0)
#endif // endif
#ifndef PKTSETFRAGPKTID
#define PKTSETFRAGPKTID(osh, lb, id)	BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTFRAGTOTNUM
#define PKTFRAGTOTNUM(osh, lb)		(0)
#endif // endif
#ifndef PKTSETFRAGTOTNUM
#define PKTSETFRAGTOTNUM(osh, lb, tot)	BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTFRAGTOTLEN
#define PKTFRAGTOTLEN(osh, lb)		(0)
#endif // endif
#ifndef PKTSETFRAGTOTLEN
#define PKTSETFRAGTOTLEN(osh, lb, len)	BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTIFINDEX
#define PKTIFINDEX(osh, lb)		(0)
#endif // endif
#ifndef PKTSETIFINDEX
#define PKTSETIFINDEX(osh, lb, idx)	BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTGETLF
#define	PKTGETLF(osh, len, send, lbuf_type)	(0)
#endif // endif

/* in rx path, reuse totlen as used len */
#ifndef PKTFRAGUSEDLEN
#define PKTFRAGUSEDLEN(osh, lb)			(0)
#endif // endif
#ifndef PKTSETFRAGUSEDLEN
#define PKTSETFRAGUSEDLEN(osh, lb, len)		BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTFRAGLEN
#define PKTFRAGLEN(osh, lb, ix)			(0)
#endif // endif
#ifndef PKTSETFRAGLEN
#define PKTSETFRAGLEN(osh, lb, ix, len)		BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTFRAGDATA_LO
#define PKTFRAGDATA_LO(osh, lb, ix)		(0)
#endif // endif
#ifndef PKTSETFRAGDATA_LO
#define PKTSETFRAGDATA_LO(osh, lb, ix, addr)	BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTFRAGDATA_HI
#define PKTFRAGDATA_HI(osh, lb, ix)		(0)
#endif // endif
#ifndef PKTSETFRAGDATA_HI
#define PKTSETFRAGDATA_HI(osh, lb, ix, addr)	BCM_REFERENCE(osh)
#endif // endif

/* RX FRAG */
#ifndef PKTISRXFRAG
#define PKTISRXFRAG(osh, lb)    	(0)
#endif // endif
#ifndef PKTSETRXFRAG
#define PKTSETRXFRAG(osh, lb)		BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTRESETRXFRAG
#define PKTRESETRXFRAG(osh, lb)		BCM_REFERENCE(osh)
#endif // endif

/* TX FRAG */
#ifndef PKTISTXFRAG
#define PKTISTXFRAG(osh, lb)		(0)
#endif // endif
#ifndef PKTSETTXFRAG
#define PKTSETTXFRAG(osh, lb)		BCM_REFERENCE(osh)
#endif // endif

/* Need Rx completion used for AMPDU reordering */
#ifndef PKTNEEDRXCPL
#define PKTNEEDRXCPL(osh, lb)           (TRUE)
#endif // endif
#ifndef PKTSETNORXCPL
#define PKTSETNORXCPL(osh, lb)          BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTRESETNORXCPL
#define PKTRESETNORXCPL(osh, lb)        BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTISFRAG
#define PKTISFRAG(osh, lb)		(0)
#endif // endif
#ifndef PKTFRAGISCHAINED
#define PKTFRAGISCHAINED(osh, i)	(0)
#endif // endif
/* TRIM Tail bytes from lfrag */
#ifndef PKTFRAG_TRIM_TAILBYTES
#define PKTFRAG_TRIM_TAILBYTES(osh, p, len, type)	PKTSETLEN(osh, p, PKTLEN(osh, p) - len)
#endif // endif
#ifndef PKTISHDRCONVTD
#define PKTISHDRCONVTD(osh, lb)		(0)
#endif // endif

/* Forwarded pkt indication */
#ifndef PKTISFRWDPKT
#define PKTISFRWDPKT(osh, lb)		0
#endif // endif
#ifndef PKTSETFRWDPKT
#define PKTSETFRWDPKT(osh, lb)		BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTRESETFRWDPKT
#define PKTRESETFRWDPKT(osh, lb)	BCM_REFERENCE(osh)
#endif // endif

/* SFD Frame */
#ifndef PKTISSFDFRAME
#define PKTISSFDFRAME(osh, lb)		(0)
#endif // endif
#ifndef PKTSETSFDFRAME
#define PKTSETSFDFRAME(osh, lb)		BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTRESETSFDFRAME
#define PKTRESETSFDFRAME(osh, lb)	BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTISSFDTXC
#define PKTISSFDTXC(osh, lb)		(0)
#endif // endif
#ifndef PKTSETSFDTXC
#define PKTSETSFDTXC(osh, lb)		BCM_REFERENCE(osh)
#endif // endif
#ifndef PKTRESETSFDTXC
#define PKTRESETSFDTXC(osh, lb)		BCM_REFERENCE(osh)
#endif // endif

#ifdef BCM_SECURE_DMA
#define SECURE_DMA_ENAB(osh) (1)
#else

#define SECURE_DMA_ENAB(osh) (0)
#ifndef BCMDMA64OSL
#define	SECURE_DMA_MAP(osh, va, size, direction, p, dmah, pcma, offset) ((dmaaddr_t) ((0)))
#else
#define	SECURE_DMA_MAP(osh, va, size, direction, p, dmah, pcma, offset) \
	((dmaaddr_t) {.hiaddr = 0, .loaddr = 0})
#endif // endif
#define	SECURE_DMA_DD_MAP(osh, va, size, direction, p, dmah) 0
#ifndef BCMDMA64OSL
#define	SECURE_DMA_MAP_TXMETA(osh, va, size, direction, p, dmah, pcma) ((dmaaddr_t) ((0)))
#else
#define	SECURE_DMA_MAP_TXMETA(osh, va, size, direction, p, dmah, pcma) \
	((dmaaddr_t) {.hiaddr = 0, .loaddr = 0})
#endif // endif
#define	SECURE_DMA_UNMAP(osh, pa, size, direction, p, dmah, pcma, offset)
#define	SECURE_DMA_UNMAP_ALL(osh, pcma)

#endif /* BCMDMA64OSL */

#ifndef ROMMABLE_ASSERT
#define ROMMABLE_ASSERT(exp) ASSERT(exp)
#endif /* ROMMABLE_ASSERT */

#ifndef MALLOC_NOPERSIST
	#define MALLOC_NOPERSIST MALLOC
#endif /* !MALLOC_NOPERSIST */

#ifndef MALLOC_PERSIST
	#define MALLOC_PERSIST MALLOC
#endif /* !MALLOC_PERSIST */

#ifndef MALLOC_NOPERSIST
	#define MALLOC_NOPERSIST MALLOC
#endif /* !MALLOC_NOPERSIST */

#ifndef MALLOC_PERSIST_ATTACH
	#define MALLOC_PERSIST_ATTACH MALLOC
#endif /* !MALLOC_PERSIST_ATTACH */

#ifndef MALLOCZ_PERSIST_ATTACH
	#define MALLOCZ_PERSIST_ATTACH MALLOCZ
#endif /* !MALLOCZ_PERSIST_ATTACH */

#ifndef MALLOCZ_NOPERSIST
	#define MALLOCZ_NOPERSIST MALLOCZ
#endif /* !MALLOCZ_NOPERSIST */

#ifndef MALLOCZ_PERSIST
	#define MALLOCZ_PERSIST MALLOCZ
#endif /* !MALLOCZ_PERSIST */

#ifndef MFREE_PERSIST
	#define MFREE_PERSIST MFREE
#endif /* !MFREE_PERSIST */

#ifndef MALLOC_SET_NOPERSIST
	#define MALLOC_SET_NOPERSIST(osh)	do { } while (0)
#endif /* !MALLOC_SET_NOPERSIST */

#ifndef MALLOC_CLEAR_NOPERSIST
	#define MALLOC_CLEAR_NOPERSIST(osh)	do { } while (0)
#endif /* !MALLOC_CLEAR_NOPERSIST */

#if defined(OSL_MEMCHECK)
#define MEMCHECK(f, l)	osl_memcheck(f, l)
#else
#define MEMCHECK(f, l)
#endif /* OSL_MEMCHECK */

#endif	/* _osl_h_ */
