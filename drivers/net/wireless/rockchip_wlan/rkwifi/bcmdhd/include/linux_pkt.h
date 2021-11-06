/*
 * Linux Packet (skb) interface
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
#ifndef BINOSL
/* Because the non BINOSL implemenation of the PKT OSL routines are macros (for
 * performance reasons),  we need the Linux headers.
 */
#include <linuxver.h>

/* packet primitives */
#ifndef BCMDBG_PKT
#ifdef BCMDBG_CTRACE
#define	PKTGET(osh, len, send)		linux_pktget((osh), (len), __LINE__, __FILE__)
#define	PKTDUP(osh, skb)		osl_pktdup((osh), (skb), __LINE__, __FILE__)
#else
#ifdef BCM_OBJECT_TRACE
#define	PKTGET(osh, len, send)		linux_pktget((osh), (len), __LINE__, __FUNCTION__)
#define	PKTDUP(osh, skb)		osl_pktdup((osh), (skb), __LINE__, __FUNCTION__)
#else
#define	PKTGET(osh, len, send)		linux_pktget((osh), (len))
#define	PKTDUP(osh, skb)		osl_pktdup((osh), (skb))
#endif /* BCM_OBJECT_TRACE */
#endif /* BCMDBG_CTRACE */
#define PKTLIST_DUMP(osh, buf)		BCM_REFERENCE(osh)
#define PKTDBG_TRACE(osh, pkt, bit)	BCM_REFERENCE(osh)
#else /* BCMDBG_PKT pkt logging for debugging */
#define	PKTGET(osh, len, send)		linux_pktget((osh), (len), __LINE__, __FILE__)
#define	PKTDUP(osh, skb)		osl_pktdup((osh), (skb), __LINE__, __FILE__)
#define PKTLIST_DUMP(osh, buf) 		osl_pktlist_dump(osh, buf)
#define BCMDBG_PTRACE
#define PKTLIST_IDX(skb)		((uint16 *)((char *)PKTTAG(skb) + \
					sizeof(((struct sk_buff*)(skb))->cb) - sizeof(uint16)))
#define PKTDBG_TRACE(osh, pkt, bit)     osl_pkttrace(osh, pkt, bit)
#endif /* BCMDBG_PKT */
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
#define	PKTSOCK(osh, skb)		({BCM_REFERENCE(osh); (((struct sk_buff*)(skb))->sk);})
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
#define PKTIDAVAIL()            (0xFFFFFFFFu)
#define PKTSHRINK(osh, m)		({BCM_REFERENCE(osh); m;})
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) && defined(TSQ_MULTIPLIER)
#define PKTORPHAN(skb, tsq)          osl_pkt_orphan_partial(skb, tsq)
extern void osl_pkt_orphan_partial(struct sk_buff *skb, int tsq);
#else
#define PKTORPHAN(skb, tsq)          ({BCM_REFERENCE(skb); 0;})
#endif /* Linux Version >= 3.6 */

#ifdef BCMDBG_CTRACE
#define	DEL_CTRACE(zosh, zskb) { \
	unsigned long zflags; \
	OSL_CTRACE_LOCK(&(zosh)->ctrace_lock, zflags); \
	list_del(&(zskb)->ctrace_list); \
	(zosh)->ctrace_num--; \
	(zskb)->ctrace_start = 0; \
	(zskb)->ctrace_count = 0; \
	OSL_CTRACE_UNLOCK(&(zosh)->ctrace_lock, zflags); \
}

#define	UPDATE_CTRACE(zskb, zfile, zline) { \
	struct sk_buff *_zskb = (struct sk_buff *)(zskb); \
	if (_zskb->ctrace_count < CTRACE_NUM) { \
		_zskb->func[_zskb->ctrace_count] = zfile; \
		_zskb->line[_zskb->ctrace_count] = zline; \
		_zskb->ctrace_count++; \
	} \
	else { \
		_zskb->func[_zskb->ctrace_start] = zfile; \
		_zskb->line[_zskb->ctrace_start] = zline; \
		_zskb->ctrace_start++; \
		if (_zskb->ctrace_start >= CTRACE_NUM) \
			_zskb->ctrace_start = 0; \
	} \
}

#define	ADD_CTRACE(zosh, zskb, zfile, zline) { \
	unsigned long zflags; \
	OSL_CTRACE_LOCK(&(zosh)->ctrace_lock, zflags); \
	list_add(&(zskb)->ctrace_list, &(zosh)->ctrace_list); \
	(zosh)->ctrace_num++; \
	UPDATE_CTRACE(zskb, zfile, zline); \
	OSL_CTRACE_UNLOCK(&(zosh)->ctrace_lock, zflags); \
}

#define PKTCALLER(zskb)	UPDATE_CTRACE((struct sk_buff *)zskb, (char *)__FUNCTION__, __LINE__)
#endif /* BCMDBG_CTRACE */

#define	PKTSETFAST(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define	PKTCLRFAST(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define	PKTISFAST(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb); FALSE;})
#define PKTLITIDX(skb)		({BCM_REFERENCE(skb); 0;})
#define PKTSETLITIDX(skb, idx)	({BCM_REFERENCE(skb); BCM_REFERENCE(idx);})
#define PKTRESETLITIDX(skb)	({BCM_REFERENCE(skb);})
#define PKTRITIDX(skb)		({BCM_REFERENCE(skb); 0;})
#define PKTSETRITIDX(skb, idx)	({BCM_REFERENCE(skb); BCM_REFERENCE(idx);})
#define PKTRESETRITIDX(skb)	({BCM_REFERENCE(skb);})

#define	PKTSETSKIPCT(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define	PKTCLRSKIPCT(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define	PKTSKIPCT(osh, skb)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})

#define PKTFRAGLEN(osh, lb, ix)			(0)
#define PKTSETFRAGLEN(osh, lb, ix, len)		BCM_REFERENCE(osh)

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

#ifdef BCMDBG_PKT /* pkt logging for debugging */
extern void *linux_pktget(osl_t *osh, uint len, int line, char *file);
extern void *osl_pkt_frmnative(osl_t *osh, void *skb, int line, char *file);
extern void *osl_pktdup(osl_t *osh, void *skb, int line, char *file);
extern void osl_pktlist_add(osl_t *osh, void *p, int line, char *file);
extern void osl_pktlist_remove(osl_t *osh, void *p);
extern char *osl_pktlist_dump(osl_t *osh, char *buf);
#ifdef BCMDBG_PTRACE
extern void osl_pkttrace(osl_t *osh, void *pkt, uint16 bit);
#endif /* BCMDBG_PTRACE */
#else /* BCMDBG_PKT */
#ifdef BCMDBG_CTRACE
#define PKT_CTRACE_DUMP(osh, b)	osl_ctrace_dump((osh), (b))
extern void *linux_pktget(osl_t *osh, uint len, int line, char *file);
extern void *osl_pkt_frmnative(osl_t *osh, void *skb, int line, char *file);
extern int osl_pkt_is_frmnative(osl_t *osh, struct sk_buff *pkt);
extern void *osl_pktdup(osl_t *osh, void *skb, int line, char *file);
struct bcmstrbuf;
extern void osl_ctrace_dump(osl_t *osh, struct bcmstrbuf *b);
#else
#ifdef BCM_OBJECT_TRACE
extern void *linux_pktget(osl_t *osh, uint len, int line, const char *caller);
extern void *osl_pktdup(osl_t *osh, void *skb, int line, const char *caller);
#else
extern void *linux_pktget(osl_t *osh, uint len);
extern void *osl_pktdup(osl_t *osh, void *skb);
#endif /* BCM_OBJECT_TRACE */
extern void *osl_pkt_frmnative(osl_t *osh, void *skb);
#endif /* BCMDBG_CTRACE */
#endif /* BCMDBG_PKT */
extern struct sk_buff *osl_pkt_tonative(osl_t *osh, void *pkt);
#ifdef BCMDBG_PKT
#define PKTFRMNATIVE(osh, skb)  osl_pkt_frmnative(((osl_t *)osh), \
				(struct sk_buff*)(skb), __LINE__, __FILE__)
#else /* BCMDBG_PKT */
#ifdef BCMDBG_CTRACE
#define PKTFRMNATIVE(osh, skb)  osl_pkt_frmnative(((osl_t *)osh), \
				(struct sk_buff*)(skb), __LINE__, __FILE__)
#define	PKTISFRMNATIVE(osh, skb) osl_pkt_is_frmnative((osl_t *)(osh), (struct sk_buff *)(skb))
#else
#define PKTFRMNATIVE(osh, skb)	osl_pkt_frmnative(((osl_t *)osh), (struct sk_buff*)(skb))
#endif /* BCMDBG_CTRACE */
#endif /* BCMDBG_PKT */
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

#else	/* BINOSL */

#define OSL_PREF_RANGE_LD(va, sz)
#define OSL_PREF_RANGE_ST(va, sz)

/* packet primitives */
#ifdef BCMDBG_PKT
#define	PKTGET(osh, len, send)		linux_pktget((osh), (len), __LINE__, __FILE__)
#define	PKTDUP(osh, skb)		osl_pktdup((osh), (skb), __LINE__, __FILE__)
#define PKTFRMNATIVE(osh, skb)		osl_pkt_frmnative((osh), (skb), __LINE__, __FILE__)
#define PKTLIST_DUMP(osh, buf) 		osl_pktlist_dump(osh, buf)
#define PKTDBG_TRACE(osh, pkt, bit)	BCM_REFERENCE(osh)
#else /* BCMDBG_PKT */
#ifdef BCMDBG_CTRACE
#define	PKTGET(osh, len, send)		linux_pktget((osh), (len), __LINE__, __FILE__)
#define	PKTDUP(osh, skb)		osl_pktdup((osh), (skb), __LINE__, __FILE__)
#define PKTFRMNATIVE(osh, skb)		osl_pkt_frmnative((osh), (skb), __LINE__, __FILE__)
#else
#ifdef BCM_OBJECT_TRACE
#define	PKTGET(osh, len, send)		linux_pktget((osh), (len), __LINE__, __FUNCTION__)
#define	PKTDUP(osh, skb)		osl_pktdup((osh), (skb), __LINE__, __FUNCTION__)
#else
#define	PKTGET(osh, len, send)		linux_pktget((osh), (len))
#define	PKTDUP(osh, skb)		osl_pktdup((osh), (skb))
#endif /* BCM_OBJECT_TRACE */
#define PKTFRMNATIVE(osh, skb)		osl_pkt_frmnative((osh), (skb))
#endif /* BCMDBG_CTRACE */
#define PKTLIST_DUMP(osh, buf)		({BCM_REFERENCE(osh); BCM_REFERENCE(buf);})
#define PKTDBG_TRACE(osh, pkt, bit)	({BCM_REFERENCE(osh); BCM_REFERENCE(pkt);})
#endif /* BCMDBG_PKT */
#if defined(BCM_OBJECT_TRACE)
#define	PKTFREE(osh, skb, send)		linux_pktfree((osh), (skb), (send), __LINE__, __FUNCTION__)
#else
#define	PKTFREE(osh, skb, send)		linux_pktfree((osh), (skb), (send))
#endif /* BCM_OBJECT_TRACE */
#define	PKTDATA(osh, skb)		osl_pktdata((osh), (skb))
#define	PKTLEN(osh, skb)		osl_pktlen((osh), (skb))
#define PKTHEADROOM(osh, skb)		osl_pktheadroom((osh), (skb))
#define PKTTAILROOM(osh, skb)		osl_pkttailroom((osh), (skb))
#define	PKTNEXT(osh, skb)		osl_pktnext((osh), (skb))
#define	PKTSETNEXT(osh, skb, x)		({BCM_REFERENCE(osh); osl_pktsetnext((skb), (x));})
#define	PKTSETLEN(osh, skb, len)	osl_pktsetlen((osh), (skb), (len))
#define	PKTPUSH(osh, skb, bytes)	osl_pktpush((osh), (skb), (bytes))
#define	PKTPULL(osh, skb, bytes)	osl_pktpull((osh), (skb), (bytes))
#define PKTTAG(skb)			osl_pkttag((skb))
#define PKTTONATIVE(osh, pkt)		osl_pkt_tonative((osh), (pkt))
#define	PKTLINK(skb)			osl_pktlink((skb))
#define	PKTSETLINK(skb, x)		osl_pktsetlink((skb), (x))
#define	PKTPRIO(skb)			osl_pktprio((skb))
#define	PKTSETPRIO(skb, x)		osl_pktsetprio((skb), (x))
#define PKTSHARED(skb)                  osl_pktshared((skb))
#define PKTSETPOOL(osh, skb, x, y)	({BCM_REFERENCE(osh); BCM_REFERENCE(skb);})
#define	PKTPOOL(osh, skb)		({BCM_REFERENCE(osh); BCM_REFERENCE(skb); FALSE;})
#define PKTFREELIST(skb)        PKTLINK(skb)
#define PKTSETFREELIST(skb, x)  PKTSETLINK((skb), (x))
#define PKTPTR(skb)             (skb)
#define PKTID(skb)              ({BCM_REFERENCE(skb); 0;})
#define PKTSETID(skb, id)       ({BCM_REFERENCE(skb); BCM_REFERENCE(id);})
#define PKTIDAVAIL()            (0xFFFFFFFFu)

#ifdef BCMDBG_PKT /* pkt logging for debugging */
extern void *linux_pktget(osl_t *osh, uint len, int line, char *file);
extern void *osl_pktdup(osl_t *osh, void *skb, int line, char *file);
extern void *osl_pkt_frmnative(osl_t *osh, void *skb, int line, char *file);
#else /* BCMDBG_PKT */
#ifdef BCM_OBJECT_TRACE
extern void *linux_pktget(osl_t *osh, uint len, int line, const char *caller);
extern void *osl_pktdup(osl_t *osh, void *skb, int line, const char *caller);
#else
extern void *linux_pktget(osl_t *osh, uint len);
extern void *osl_pktdup(osl_t *osh, void *skb);
#endif /* BCM_OBJECT_TRACE */
extern void *osl_pkt_frmnative(osl_t *osh, void *skb);
#endif /* BCMDBG_PKT */
#if defined(BCM_OBJECT_TRACE)
extern void linux_pktfree(osl_t *osh, void *skb, bool send, int line, const char *caller);
#else
extern void linux_pktfree(osl_t *osh, void *skb, bool send);
#endif /* BCM_OBJECT_TRACE */
extern uchar *osl_pktdata(osl_t *osh, void *skb);
extern uint osl_pktlen(osl_t *osh, void *skb);
extern uint osl_pktheadroom(osl_t *osh, void *skb);
extern uint osl_pkttailroom(osl_t *osh, void *skb);
extern void *osl_pktnext(osl_t *osh, void *skb);
extern void osl_pktsetnext(void *skb, void *x);
extern void osl_pktsetlen(osl_t *osh, void *skb, uint len);
extern uchar *osl_pktpush(osl_t *osh, void *skb, int bytes);
extern uchar *osl_pktpull(osl_t *osh, void *skb, int bytes);
extern void *osl_pkttag(void *skb);
extern void *osl_pktlink(void *skb);
extern void osl_pktsetlink(void *skb, void *x);
extern uint osl_pktprio(void *skb);
extern void osl_pktsetprio(void *skb, uint x);
extern struct sk_buff *osl_pkt_tonative(osl_t *osh, void *pkt);
extern bool osl_pktshared(void *skb);

#ifdef BCMDBG_PKT /* pkt logging for debugging */
extern char *osl_pktlist_dump(osl_t *osh, char *buf);
extern void osl_pktlist_add(osl_t *osh, void *p, int line, char *file);
extern void osl_pktlist_remove(osl_t *osh, void *p);
#endif /* BCMDBG_PKT */

#endif	/* BINOSL */

#define PKTALLOCED(osh)		osl_pktalloced(osh)
extern uint osl_pktalloced(osl_t *osh);

#define PKTPOOLHEAPCOUNT()            (0u)

#endif /* BCMDRIVER */

#endif	/* _linux_pkt_h_ */
