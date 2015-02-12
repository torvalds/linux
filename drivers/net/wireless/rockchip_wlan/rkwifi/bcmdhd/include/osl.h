/*
 * OS Abstraction Layer
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: osl.h 474639 2014-05-01 23:52:31Z $
 */

#ifndef _osl_h_
#define _osl_h_

#include <osl_decl.h>

#define OSL_PKTTAG_SZ	32 /* Size of PktTag */

/* Drivers use PKTFREESETCB to register a callback function when a packet is freed by OSL */
typedef void (*pktfree_cb_fn_t)(void *ctx, void *pkt, unsigned int status);

/* Drivers use REGOPSSET() to register register read/write funcitons */
typedef unsigned int (*osl_rreg_fn_t)(void *ctx, volatile void *reg, unsigned int size);
typedef void  (*osl_wreg_fn_t)(void *ctx, volatile void *reg, unsigned int val, unsigned int size);



#include <linux_osl.h>

#ifndef PKTDBG_TRACE
#define PKTDBG_TRACE(osh, pkt, bit)	BCM_REFERENCE(osh)
#endif

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
#define OSL_SYSUPTIME_SUPPORT FALSE
#else
#define OSL_SYSUPTIME_SUPPORT TRUE
#endif /* OSL_SYSUPTIME */

#if !defined(PKTC) && !defined(PKTC_DONGLE)
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
#endif /* !linux || !PKTC */

#if !defined(HNDCTF) && !defined(PKTC_TX_DONGLE)
#define PKTSETCHAINED(osh, skb)		BCM_REFERENCE(osh)
#define PKTCLRCHAINED(osh, skb)		BCM_REFERENCE(osh)
#define PKTISCHAINED(skb)		FALSE
#endif

/* Lbuf with fraglist */
#define PKTFRAGPKTID(osh, lb)		(0)
#define PKTSETFRAGPKTID(osh, lb, id)	BCM_REFERENCE(osh)
#define PKTFRAGTOTNUM(osh, lb)		(0)
#define PKTSETFRAGTOTNUM(osh, lb, tot)	BCM_REFERENCE(osh)
#define PKTFRAGTOTLEN(osh, lb)		(0)
#define PKTSETFRAGTOTLEN(osh, lb, len)	BCM_REFERENCE(osh)
#define PKTIFINDEX(osh, lb)		(0)
#define PKTSETIFINDEX(osh, lb, idx)	BCM_REFERENCE(osh)
#define	PKTGETLF(osh, len, send, lbuf_type)	(0)

/* in rx path, reuse totlen as used len */
#define PKTFRAGUSEDLEN(osh, lb)			(0)
#define PKTSETFRAGUSEDLEN(osh, lb, len)		BCM_REFERENCE(osh)

#define PKTFRAGLEN(osh, lb, ix)			(0)
#define PKTSETFRAGLEN(osh, lb, ix, len)		BCM_REFERENCE(osh)
#define PKTFRAGDATA_LO(osh, lb, ix)		(0)
#define PKTSETFRAGDATA_LO(osh, lb, ix, addr)	BCM_REFERENCE(osh)
#define PKTFRAGDATA_HI(osh, lb, ix)		(0)
#define PKTSETFRAGDATA_HI(osh, lb, ix, addr)	BCM_REFERENCE(osh)

/* RX FRAG */
#define PKTISRXFRAG(osh, lb)    	(0)
#define PKTSETRXFRAG(osh, lb)		BCM_REFERENCE(osh)
#define PKTRESETRXFRAG(osh, lb)		BCM_REFERENCE(osh)

/* TX FRAG */
#define PKTISTXFRAG(osh, lb)		(0)
#define PKTSETTXFRAG(osh, lb)		BCM_REFERENCE(osh)

/* Need Rx completion used for AMPDU reordering */
#define PKTNEEDRXCPL(osh, lb)           (TRUE)
#define PKTSETNORXCPL(osh, lb)          BCM_REFERENCE(osh)
#define PKTRESETNORXCPL(osh, lb)        BCM_REFERENCE(osh)

#define PKTISFRAG(osh, lb)		(0)
#define PKTFRAGISCHAINED(osh, i)	(0)
/* TRIM Tail bytes from lfrag */
#define PKTFRAG_TRIM_TAILBYTES(osh, p, len)	PKTSETLEN(osh, p, PKTLEN(osh, p) - len)

#endif	/* _osl_h_ */
