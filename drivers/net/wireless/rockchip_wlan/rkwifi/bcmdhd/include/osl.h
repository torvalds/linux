/*
 * OS Abstraction Layer
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _osl_h_
#define _osl_h_

#include <osl_decl.h>

enum {
	TAIL_BYTES_TYPE_FCS = 1,
	TAIL_BYTES_TYPE_ICV = 2,
	TAIL_BYTES_TYPE_MIC = 3
};

#ifdef DHD_EFI
#define OSL_PKTTAG_SZ	40 /* Size of PktTag */
#elif defined(MACOSX)
#define OSL_PKTTAG_SZ	56
#elif defined(__linux__)
#define OSL_PKTTAG_SZ   48 /* standard linux pkttag size is 48 bytes */
#else
#ifndef OSL_PKTTAG_SZ
#define OSL_PKTTAG_SZ	32 /* Size of PktTag */
#endif /* !OSL_PKTTAG_SZ */
#endif /* DHD_EFI */

/* Drivers use PKTFREESETCB to register a callback function when a packet is freed by OSL */
typedef void (*pktfree_cb_fn_t)(void *ctx, void *pkt, unsigned int status);

/* Drivers use REGOPSSET() to register register read/write funcitons */
typedef unsigned int (*osl_rreg_fn_t)(void *ctx, volatile void *reg, unsigned int size);
typedef void  (*osl_wreg_fn_t)(void *ctx, volatile void *reg, unsigned int val, unsigned int size);

#if defined(EFI)
#include <efi_osl.h>
#elif defined(WL_UNITTEST)
#include <utest_osl.h>
#elif defined(__linux__)
#include <linux_osl.h>
#include <linux_pkt.h>
#elif defined(NDIS)
#include <ndis_osl.h>
#elif defined(_RTE_)
#include <rte_osl.h>
#include <hnd_pkt.h>
#elif defined(MACOSX)
#include <macosx_osl.h>
#else
#error "Unsupported OSL requested"
#endif /* defined(DOS) */

#ifndef PKTDBG_TRACE
#define PKTDBG_TRACE(osh, pkt, bit)	BCM_REFERENCE(osh)
#endif

#ifndef BCM_UPTIME_PROFILE
#define OSL_GETCYCLES_PROF(x)
#endif

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

#if !defined(OSL_CPU_COUNTS_PER_US)
#define OSL_CPU_COUNTS_PER_US() (0)
#define OSL_CPU_COUNTS_PER_US_NOT_DEFINED 1
#endif /* !defined(OSL_CPU_COUNTS_PER_US) */

#ifndef OSL_SYS_HALT
#ifdef __COVERITY__
/*
 * For Coverity builds, provide a definition that allows Coverity
 * to model the lack of return. This avoids Coverity False Positive
 * defects associated with data inconsistency being detected after
 * we otherwise would have halted.
 */
#define OSL_SYS_HALT()   __coverity_panic__()
#else /* __COVERITY__ */
#define OSL_SYS_HALT()	do {} while (0)
#endif /* __COVERITY__ */
#endif /* OSL_SYS_HALT */

#ifndef DMB
#define DMB()	do {} while (0)
#endif /* DMB */

#ifndef OSL_MEM_AVAIL
#define OSL_MEM_AVAIL()	(0xffffffff)
#endif

#ifndef OSL_OBFUSCATE_BUF
#if defined (_RTE_)
#define OSL_OBFUSCATE_BUF(x) osl_obfuscate_ptr(x)
#else
#define OSL_OBFUSCATE_BUF(x) (x)
#endif	/* _RTE_ */
#endif	/* OSL_OBFUSCATE_BUF */

#ifndef OSL_GET_HCAPISTIMESYNC
#if defined (_RTE_)
#define OSL_GET_HCAPISTIMESYNC() osl_get_hcapistimesync()
#else
#define OSL_GET_HCAPISTIMESYNC()
#endif	/* _RTE_ */
#endif	/*  OSL_GET_HCAPISTIMESYNC */

#ifndef OSL_GET_HCAPISPKTTXS
#if defined (_RTE_)
#define OSL_GET_HCAPISPKTTXS() osl_get_hcapispkttxs()
#else
#define OSL_GET_HCAPISPKTTXS()
#endif	/* _RTE_ */
#endif	/*  OSL_GET_HCAPISPKTTXS */

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
#endif /* !PKTC_DONGLE */

#ifndef PKTSETCHAINED
#define PKTSETCHAINED(osh, skb)		BCM_REFERENCE(osh)
#endif
#ifndef PKTCLRCHAINED
#define PKTCLRCHAINED(osh, skb)		BCM_REFERENCE(osh)
#endif
#ifndef PKTISCHAINED
#define PKTISCHAINED(skb)		FALSE
#endif

#ifndef PKTGETPROFILEIDX
#define PKTGETPROFILEIDX(p)		(-1)
#endif

#ifndef PKTCLRPROFILEIDX
#define PKTCLRPROFILEIDX(p)
#endif

#ifndef PKTSETPROFILEIDX
#define PKTSETPROFILEIDX(p, idx)	BCM_REFERENCE(idx)
#endif

#ifndef _RTE_
/* Lbuf with fraglist */
#ifndef PKTFRAGPKTID
#define PKTFRAGPKTID(osh, lb)		(0)
#endif
#ifndef PKTSETFRAGPKTID
#define PKTSETFRAGPKTID(osh, lb, id)	BCM_REFERENCE(osh)
#endif
#ifndef PKTFRAGTOTNUM
#define PKTFRAGTOTNUM(osh, lb)		(0)
#endif
#ifndef PKTSETFRAGTOTNUM
#define PKTSETFRAGTOTNUM(osh, lb, tot)	BCM_REFERENCE(osh)
#endif
#ifndef PKTFRAGTOTLEN
#define PKTFRAGTOTLEN(osh, lb)		(0)
#endif
#ifndef PKTSETFRAGTOTLEN
#define PKTSETFRAGTOTLEN(osh, lb, len)	BCM_REFERENCE(osh)
#endif
#ifndef PKTIFINDEX
#define PKTIFINDEX(osh, lb)		(0)
#endif
#ifndef PKTSETIFINDEX
#define PKTSETIFINDEX(osh, lb, idx)	BCM_REFERENCE(osh)
#endif
#ifndef PKTGETLF
#define	PKTGETLF(osh, len, send, lbuf_type)	(0)
#endif

/* in rx path, reuse totlen as used len */
#ifndef PKTFRAGUSEDLEN
#define PKTFRAGUSEDLEN(osh, lb)			(0)
#endif
#ifndef PKTSETFRAGUSEDLEN
#define PKTSETFRAGUSEDLEN(osh, lb, len)		BCM_REFERENCE(osh)
#endif
#ifndef PKTFRAGLEN
#define PKTFRAGLEN(osh, lb, ix)			(0)
#endif
#ifndef PKTSETFRAGLEN
#define PKTSETFRAGLEN(osh, lb, ix, len)		BCM_REFERENCE(osh)
#endif
#ifndef PKTFRAGDATA_LO
#define PKTFRAGDATA_LO(osh, lb, ix)		(0)
#endif
#ifndef PKTSETFRAGDATA_LO
#define PKTSETFRAGDATA_LO(osh, lb, ix, addr)	BCM_REFERENCE(osh)
#endif
#ifndef PKTFRAGDATA_HI
#define PKTFRAGDATA_HI(osh, lb, ix)		(0)
#endif
#ifndef PKTSETFRAGDATA_HI
#define PKTSETFRAGDATA_HI(osh, lb, ix, addr)	BCM_REFERENCE(osh)
#endif

#ifndef PKTFRAGMOVE
#define PKTFRAGMOVE(osh, dst, src) (BCM_REFERENCE(osh), BCM_REFERENCE(dst), BCM_REFERENCE(src))
#endif

/* RX FRAG */
#ifndef PKTISRXFRAG
#define PKTISRXFRAG(osh, lb)		(0)
#endif
#ifndef PKTSETRXFRAG
#define PKTSETRXFRAG(osh, lb)		BCM_REFERENCE(osh)
#endif
#ifndef PKTRESETRXFRAG
#define PKTRESETRXFRAG(osh, lb)		BCM_REFERENCE(osh)
#endif

/* TX FRAG */
#ifndef PKTISTXFRAG
#define PKTISTXFRAG(osh, lb)		(0)
#endif
#ifndef PKTSETTXFRAG
#define PKTSETTXFRAG(osh, lb)		BCM_REFERENCE(osh)
#endif

/* TX ALFRAG */
#ifndef PKTISTXALFRAG
#define PKTISTXALFRAG(osh, lb)		(0)
#endif
#ifndef PKTSETTXALFRAG
#define PKTSETTXALFRAG(osh, lb)		BCM_REFERENCE(osh)
#endif
#ifndef PKTRESETTXALFRAG
#define PKTRESETTXALFRAG(osh, lb)	BCM_REFERENCE(osh)
#endif

#ifndef PKTNUMMPDUS
#define PKTNUMMPDUS(osh, lb)		(1)
#endif
#ifndef PKTNUMPKTS
#define PKTNUMPKTS(osh, lb)		(1)
#endif

#ifndef PKTISHWCSO
#define PKTISHWCSO(osh, lb)		(FALSE)
#endif

#ifndef PKTISSUBMSDUTOEHDR
#define PKTISSUBMSDUTOEHDR(osh, lb)	(FALSE)
#endif

#ifndef PKT_IS_HOST_SFHLLC
#define PKT_IS_HOST_SFHLLC(osh, lb)	(FALSE)
#endif

#ifndef PKT_SET_HOST_SFHLLC
#define PKT_SET_HOST_SFHLLC(osh, lb)	BCM_REFERENCE(osh)
#endif

#ifndef PKT_IS_HOST_SFHLLC_DONE
#define PKT_IS_HOST_SFHLLC_DONE(osh, lb)	(FALSE)
#endif

#ifndef PKT_SET_HOST_SFHLLC_DONE
#define PKT_SET_HOST_SFHLLC_DONE(osh, lb)	BCM_REFERENCE(osh)
#endif

/* Need Rx completion used for AMPDU reordering */
#ifndef PKTNEEDRXCPL
#define PKTNEEDRXCPL(osh, lb)           (TRUE)
#endif
#ifndef PKTSETNORXCPL
#define PKTSETNORXCPL(osh, lb)          BCM_REFERENCE(osh)
#endif
#ifndef PKTRESETNORXCPL
#define PKTRESETNORXCPL(osh, lb)        BCM_REFERENCE(osh)
#endif
#ifndef PKTISFRAG
#define PKTISFRAG(osh, lb)		(0)
#endif
#ifndef PKTFRAGISCHAINED
#define PKTFRAGISCHAINED(osh, i)	(0)
#endif
#ifndef PKTISHDRCONVTD
#define PKTISHDRCONVTD(osh, lb)		(0)
#endif

/* Forwarded pkt indication */
#ifndef PKTISFRWDPKT
#define PKTISFRWDPKT(osh, lb)		0
#endif
#ifndef PKTSETFRWDPKT
#define PKTSETFRWDPKT(osh, lb)		BCM_REFERENCE(osh)
#endif
#ifndef PKTRESETFRWDPKT
#define PKTRESETFRWDPKT(osh, lb)	BCM_REFERENCE(osh)
#endif

/* PKT consumed for totlen calculation */
#ifndef PKTISUSEDTOTLEN
#define PKTISUSEDTOTLEN(osh, lb)		0
#endif
#ifndef PKTSETUSEDTOTLEN
#define PKTSETUSEDTOTLEN(osh, lb)		BCM_REFERENCE(osh)
#endif
#ifndef PKTRESETUSEDTOTLEN
#define PKTRESETUSEDTOTLEN(osh, lb)             BCM_REFERENCE(osh)
#endif

/* UDR Packet Indication */
#ifndef PKTISUDR
#define PKTISUDR(osh, lb)			0
#endif

#ifndef PKTSETUDR
#define PKTSETUDR(osh, lb)			BCM_REFERENCE(osh)
#endif

#ifndef PKTSETUDR
#define PKTRESETUDR(osh, lb)			BCM_REFERENCE(osh)
#endif
#endif	/* _RTE_ */

#if !(defined(__linux__))
#define PKTLIST_INIT(x)			BCM_REFERENCE(x)
#define PKTLIST_ENQ(x, y)		BCM_REFERENCE(x)
#define PKTLIST_DEQ(x)			BCM_REFERENCE(x)
#define PKTLIST_UNLINK(x, y)		BCM_REFERENCE(x)
#define PKTLIST_FINI(x)			BCM_REFERENCE(x)
#endif

#ifndef ROMMABLE_ASSERT
#define ROMMABLE_ASSERT(exp) ASSERT(exp)
#endif /* ROMMABLE_ASSERT */

#ifndef MALLOC_NOPERSIST
	#define MALLOC_NOPERSIST MALLOC
#endif /* !MALLOC_NOPERSIST */

#ifndef MALLOC_PERSIST
	#define MALLOC_PERSIST MALLOC
#endif /* !MALLOC_PERSIST */

#ifndef MALLOC_RA
	#define MALLOC_RA(osh, size, callsite) MALLOCZ(osh, size)
#endif /* !MALLOC_RA */

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

#ifndef BCMDBGPERF
#define PERF_TRACE_START(id)				do {} while (0)
#define PERF_TRACE_END(id)				do {} while (0)
#define PERF_TRACE_END2(id, mycounters)			do {} while (0)
#define PERF_TRACE_END3(id, mycounters, coreunit)	do {} while (0)
#define UPDATE_PERF_TRACE_COUNTER(counter, val)		do {} while (0)
#define ADD_PERF_TRACE_COUNTER(counter, val)		do {} while (0)
#endif /* OSL_MEMCHECK */

/* Virtual/physical address translation. */
#if !defined(OSL_VIRT_TO_PHYS_ADDR)
	#define OSL_VIRT_TO_PHYS_ADDR(va)	((void*)(uintptr)(va))
#endif

#if !defined(OSL_PHYS_TO_VIRT_ADDR)
	#define OSL_PHYS_TO_VIRT_ADDR(pa)	((void*)(uintptr)(pa))
#endif

#endif	/* _osl_h_ */
