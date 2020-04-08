/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux Packet (skb) interface
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
 * $Id: linux_pkt.h 701430 2017-05-25 00:03:02Z $
 */

#ifndef _linux_pkt_h_
#define _linux_pkt_h_

#include <typedefs.h>

#ifdef __ARM_ARCH_7A__
#define PKT_HEADROOM_DEFAULT NET_SKB_PAD /**< NET_SKB_PAD is defined in a linux kernel header */
#else
#define PKT_HEADROOM_DEFAULT 16
#endif /* __ARM_ARCH_7A__ */

#ifdef BCMDRIVER
/*
 * BINOSL selects the slightly slower function-call-based binary compatible osl.
 * Macros expand to calls to functions defined in linux_osl.c .
 */
/* Because the non BINOSL implemenation of the PKT OSL routines are macros (for
 * performance reasons),  we need the Linux headers.
 */
#include <linuxver.h>

/* packet primitives */
#ifdef BCM_OBJECT_TRACE
#define	PKTGET(osh, len, send)		linux_pktget((osh), (len), __LINE__, __FUNCTION__)
#define	PKTDUP(osh, skb)		osl_pktdup((osh), (skb), __LINE__, __FUNCTION__)
#else
#define	PKTGET(osh, len, send)		linux_pktget((osh), (len))
#define	PKTDUP(osh, skb)		osl_pktdup((osh), (skb))
#endif /* BCM_OBJECT_TRACE */
#define PKTLIST_DUMP(osh, buf)		BCM_REFERENCE(osh)
#define PKTDBG_TRACE(osh, pkt, bit)	BCM_REFERENCE(osh)
#if defined(BCM_OBJECT_TRACE)
#define	PKTFREE(osh, skb, send)		linux_pktfree((osh), (skb), (send), __LINE__, __FUNCTION__)
#else
#define	PKTFREE(osh, skb, send)		linux_pktfree((osh), (skb), (send))
#endif /* BCM_OBJECT_TRACE */
#ifdef CONFIG_DHD_USE_STATIC_BUF
#define	PKTGET_STATIC(osh, len, send)		osl_pktget_static((osh), (len))
#define	PKTFREE_STATIC(osh, skb, send)		osl_pktfree_static((osh), (skb), (send))
#else
#define	PKTGET_STATIC	PKTGET
#define	PKTFREE_STATIC	PKTFREE
#endif /* CONFIG_DHD_USE_STATIC_BUF */
#define	PKTDATA(osh, skb)		({BCM_REFERENCE(osh); (((struct sk_buff*)(skb))->data);})
#define	PKTLEN(osh, skb)		({BCM_REFERENCE(osh); (((struct sk_buff*)(skb))->len);})
#define	PKTHEAD(osh, skb)		({BCM_REFERENCE(osh); (((struct sk_buff*)(skb))->head);})
#define PKTSETHEAD(osh, skb, h)		({BCM_REFERENCE(osh); \
					(((struct sk_buff *)(skb))->head = (h));})
#define PKTHEADROOM(osh, skb)		(PKTDATA(osh, skb)-(((struct sk_buff*)(skb))->head))
#define PKTEXPHEADROOM(osh, skb, b)	\
	({ \
	 BCM_REFERENCE(osh); \
	 skb_realloc_headroom((struct sk_buff*)(skb), (b)); \
	 })
#define PKTTAILROOM(osh, skb)		\
	({ \
	 BCM_REFERENCE(osh); \
	 skb_tailroom((struct sk_buff*)(skb)); \
	 })
#define PKTPADTAILROOM(osh, skb, padlen) \
	({ \
	 BCM_REFERENCE(osh); \
	 skb_pad((struct sk_buff*)(skb), (padlen)); \
	 })
#define	PKTNEXT(osh, skb)		({BCM_REFERENCE(osh); (((struct sk_buff*)(skb))->next);})
#define	PKTSETNEXT(osh, skb, x)		\
	({ \
	 BCM_REFERENCE(osh); \
	 (((struct sk_buff*)(skb))->next = (struct sk_buff*)(x)); \
	 })
#define	PKTSETLEN(osh, skb, len)	\
	({ \
	 BCM_REFERENCE(osh); \
	 __skb_trim((struct sk_buff*)(skb), (len)); \
	 })
#define	PKTPUSH(osh, skb, bytes)	\
	({ \
	 BCM_REFERENCE(osh); \
	 skb_push((struct sk_buff*)(skb), (bytes)); \
	 })
#define	PKTPULL(osh, skb, bytes)	\
	({ \
	 BCM_REFERENCE(osh); \
	 skb_pull((struct sk_buff*)(skb), (bytes)); \
	 })
#define	PKTTAG(skb)			((void*)(((struct sk_buff*)(skb))->cb))
#define PKTSETPOOL(osh, skb, x, y)	BCM_REFERENCE(osh)
#define	PKTPOOL(osh, skb)		({BCM_REFERENCE(osh); BCM_REFERENCE(skb); FALSE;})
#define PKTFREELIST(skb)        PKTLINK(skb)
#define PKTSETFREELIST(skb, x)  PKTSETLINK((skb), (x))
#define PKTPTR(skb)             (skb)
#define PKTID(skb)              ({BCM_REFERENCE(skb); 0;})
#define PKTSETID(skb, id)       ({BCM_REFERENCE(skb); BCM_REFERENCE(id);})
#define PKTSHRINK(osh, m)		({BCM_REFERENCE(osh); m;})
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) && defined(TSQ_MULTIPLIER)
#define PKTORPHAN(skb, tsq)          osl_pkt_orphan_partial(skb, tsq)
extern void osl_pkt_orphan_partial(struct sk_buff *skb, int tsq);
#else
#define PKTORPHAN(skb, tsq)          ({BCM_REFERENCE(skb); 0;})
#endif /* LINUX VERSION >= 3.6 */

#define	PKTSETFAST(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define	PKTCLRFAST(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define	PKTISFAST(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb); FALSE;})

#define	PKTSETCTF(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define	PKTCLRCTF(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define	PKTISCTF(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb); FALSE;})

#define	PKTSETSKIPCT(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define	PKTCLRSKIPCT(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define	PKTSKIPCT(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define CTF_MARK(m)		({BCM_REFERENCE(m); 0;})

#define PKTFRAGLEN(osh, lb, ix)			(0)
#define PKTSETFRAGLEN(osh, lb, ix, len)		BCM_REFERENCE(osh)

#define PKTSETFWDERBUF(osh, skb)  ({ BCM_REFERENCE(osh); BCM_REFERENCE(skb); })
#define PKTCLRFWDERBUF(osh, skb)  ({ BCM_REFERENCE(osh); BCM_REFERENCE(skb); })
#define PKTISFWDERBUF(osh, skb)   ({ BCM_REFERENCE(osh); BCM_REFERENCE(skb); FALSE;})

#define	PKTSETTOBR(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define	PKTCLRTOBR(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define	PKTISTOBR(skb)	({BCM_REFERENCE(skb); FALSE;})

#ifdef BCMFA
#ifdef BCMFA_HW_HASH
#define PKTSETFAHIDX(skb, idx)	(((struct sk_buff*)(skb))->napt_idx = idx)
#else
#define PKTSETFAHIDX(skb, idx)	({BCM_REFERENCE(skb); BCM_REFERENCE(idx);})
#endif /* BCMFA_SW_HASH */
#define PKTGETFAHIDX(skb)	(((struct sk_buff*)(skb))->napt_idx)
#define PKTSETFADEV(skb, imp)	(((struct sk_buff*)(skb))->dev = imp)
#define PKTSETRXDEV(skb)	(((struct sk_buff*)(skb))->rxdev = ((struct sk_buff*)(skb))->dev)

#define	AUX_TCP_FIN_RST	(1 << 0)
#define	AUX_FREED	(1 << 1)
#define PKTSETFAAUX(skb)	(((struct sk_buff*)(skb))->napt_flags |= AUX_TCP_FIN_RST)
#define	PKTCLRFAAUX(skb)	(((struct sk_buff*)(skb))->napt_flags &= (~AUX_TCP_FIN_RST))
#define	PKTISFAAUX(skb)		(((struct sk_buff*)(skb))->napt_flags & AUX_TCP_FIN_RST)
#define PKTSETFAFREED(skb)	(((struct sk_buff*)(skb))->napt_flags |= AUX_FREED)
#define	PKTCLRFAFREED(skb)	(((struct sk_buff*)(skb))->napt_flags &= (~AUX_FREED))
#define	PKTISFAFREED(skb)	(((struct sk_buff*)(skb))->napt_flags & AUX_FREED)
#define	PKTISFABRIDGED(skb)	PKTISFAAUX(skb)
#else
#define	PKTISFAAUX(skb)		({BCM_REFERENCE(skb); FALSE;})
#define	PKTISFABRIDGED(skb)	({BCM_REFERENCE(skb); FALSE;})
#define	PKTISFAFREED(skb)	({BCM_REFERENCE(skb); FALSE;})

#define	PKTCLRFAAUX(skb)	BCM_REFERENCE(skb)
#define PKTSETFAFREED(skb)	BCM_REFERENCE(skb)
#define	PKTCLRFAFREED(skb)	BCM_REFERENCE(skb)
#endif /* BCMFA */

#if defined(BCM_OBJECT_TRACE)
extern void linux_pktfree(osl_t *osh, void *skb, bool send, int line, const char *caller);
#else
extern void linux_pktfree(osl_t *osh, void *skb, bool send);
#endif /* BCM_OBJECT_TRACE */
extern void *osl_pktget_static(osl_t *osh, uint len);
extern void osl_pktfree_static(osl_t *osh, void *skb, bool send);
extern void osl_pktclone(osl_t *osh, void **pkt);

#ifdef BCM_OBJECT_TRACE
extern void *linux_pktget(osl_t *osh, uint len, int line, const char *caller);
extern void *osl_pktdup(osl_t *osh, void *skb, int line, const char *caller);
#else
extern void *linux_pktget(osl_t *osh, uint len);
extern void *osl_pktdup(osl_t *osh, void *skb);
#endif /* BCM_OBJECT_TRACE */
extern void *osl_pkt_frmnative(osl_t *osh, void *skb);
extern struct sk_buff *osl_pkt_tonative(osl_t *osh, void *pkt);
#define PKTFRMNATIVE(osh, skb)	osl_pkt_frmnative(((osl_t *)osh), (struct sk_buff*)(skb))
#define PKTTONATIVE(osh, pkt)		osl_pkt_tonative((osl_t *)(osh), (pkt))

#define	PKTLINK(skb)			(((struct sk_buff*)(skb))->prev)
#define	PKTSETLINK(skb, x)		(((struct sk_buff*)(skb))->prev = (struct sk_buff*)(x))
#define	PKTPRIO(skb)			(((struct sk_buff*)(skb))->priority)
#define	PKTSETPRIO(skb, x)		(((struct sk_buff*)(skb))->priority = (x))
#define PKTSUMNEEDED(skb)		(((struct sk_buff*)(skb))->ip_summed == CHECKSUM_HW)
#define PKTSETSUMGOOD(skb, x)		(((struct sk_buff*)(skb))->ip_summed = \
						((x) ? CHECKSUM_UNNECESSARY : CHECKSUM_NONE))
/* PKTSETSUMNEEDED and PKTSUMGOOD are not possible because skb->ip_summed is overloaded */
#define PKTSHARED(skb)                  (((struct sk_buff*)(skb))->cloned)

#ifdef CONFIG_NF_CONNTRACK_MARK
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define PKTMARK(p)                     (((struct sk_buff *)(p))->mark)
#define PKTSETMARK(p, m)               ((struct sk_buff *)(p))->mark = (m)
#else /* !2.6.0 */
#define PKTMARK(p)                     (((struct sk_buff *)(p))->nfmark)
#define PKTSETMARK(p, m)               ((struct sk_buff *)(p))->nfmark = (m)
#endif /* 2.6.0 */
#else /* CONFIG_NF_CONNTRACK_MARK */
#define PKTMARK(p)                     0
#define PKTSETMARK(p, m)
#endif /* CONFIG_NF_CONNTRACK_MARK */

#define PKTALLOCED(osh)		osl_pktalloced(osh)
extern uint osl_pktalloced(osl_t *osh);

#endif /* BCMDRIVER */

#endif	/* _linux_pkt_h_ */
