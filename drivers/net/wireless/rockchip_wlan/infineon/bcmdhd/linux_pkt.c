/*
 * Linux Packet (skb) interface
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 * $Id: linux_pkt.c 691183 2017-03-21 05:49:14Z $
 */

#include <typedefs.h>
#include <bcmendian.h>
#include <linuxver.h>
#include <bcmdefs.h>

#include <linux/random.h>

#include <osl.h>
#include <bcmutils.h>
#include <pcicfg.h>

#if defined(BCMASSERT_LOG) && !defined(OEM_ANDROID)
#include <bcm_assert_log.h>
#endif // endif
#include <linux/fs.h>
#include "linux_osl_priv.h"

#ifdef CONFIG_DHD_USE_STATIC_BUF

bcm_static_buf_t *bcm_static_buf = 0;
bcm_static_pkt_t *bcm_static_skb = 0;

void* wifi_platform_prealloc(void *adapter, int section, unsigned long size);
#endif /* CONFIG_DHD_USE_STATIC_BUF */

#ifdef BCM_OBJECT_TRACE
/* don't clear the first 4 byte that is the pkt sn */
#define OSL_PKTTAG_CLEAR(p) \
do { \
	struct sk_buff *s = (struct sk_buff *)(p); \
	uint tagsz = sizeof(s->cb); \
	ASSERT(OSL_PKTTAG_SZ <= tagsz); \
	memset(s->cb + 4, 0, tagsz - 4); \
} while (0)
#else
#define OSL_PKTTAG_CLEAR(p) \
do { \
	struct sk_buff *s = (struct sk_buff *)(p); \
	uint tagsz = sizeof(s->cb); \
	ASSERT(OSL_PKTTAG_SZ <= tagsz); \
	memset(s->cb, 0, tagsz); \
} while (0)
#endif /* BCM_OBJECT_TRACE */

int osl_static_mem_init(osl_t *osh, void *adapter)
{
#ifdef CONFIG_DHD_USE_STATIC_BUF
		if (!bcm_static_buf && adapter) {
			if (!(bcm_static_buf = (bcm_static_buf_t *)wifi_platform_prealloc(adapter,
				3, STATIC_BUF_SIZE + STATIC_BUF_TOTAL_LEN))) {
				printk("can not alloc static buf!\n");
				bcm_static_skb = NULL;
				ASSERT(osh->magic == OS_HANDLE_MAGIC);
				return -ENOMEM;
			} else {
				printk("succeed to alloc static buf\n");
			}

			spin_lock_init(&bcm_static_buf->static_lock);

			bcm_static_buf->buf_ptr = (unsigned char *)bcm_static_buf + STATIC_BUF_SIZE;
		}

#if defined(BCMSDIO) || defined(DHD_USE_STATIC_CTRLBUF)
		if (!bcm_static_skb && adapter) {
			int i;
			void *skb_buff_ptr = 0;
			bcm_static_skb = (bcm_static_pkt_t *)((char *)bcm_static_buf + 2048);
			skb_buff_ptr = wifi_platform_prealloc(adapter, 4, 0);
			if (!skb_buff_ptr) {
				printk("cannot alloc static buf!\n");
				bcm_static_buf = NULL;
				bcm_static_skb = NULL;
				ASSERT(osh->magic == OS_HANDLE_MAGIC);
				return -ENOMEM;
			}

			bcopy(skb_buff_ptr, bcm_static_skb, sizeof(struct sk_buff *) *
				(STATIC_PKT_MAX_NUM));
			for (i = 0; i < STATIC_PKT_MAX_NUM; i++) {
				bcm_static_skb->pkt_use[i] = 0;
			}

#ifdef DHD_USE_STATIC_CTRLBUF
			spin_lock_init(&bcm_static_skb->osl_pkt_lock);
			bcm_static_skb->last_allocated_index = 0;
#else
			sema_init(&bcm_static_skb->osl_pkt_sem, 1);
#endif /* DHD_USE_STATIC_CTRLBUF */
		}
#endif /* BCMSDIO || DHD_USE_STATIC_CTRLBUF */
#endif /* CONFIG_DHD_USE_STATIC_BUF */

	return 0;
}

int osl_static_mem_deinit(osl_t *osh, void *adapter)
{
#ifdef CONFIG_DHD_USE_STATIC_BUF
	if (bcm_static_buf) {
		bcm_static_buf = 0;
	}
#ifdef BCMSDIO
	if (bcm_static_skb) {
		bcm_static_skb = 0;
	}
#endif /* BCMSDIO */
#endif /* CONFIG_DHD_USE_STATIC_BUF */
	return 0;
}

/*
 * To avoid ACP latency, a fwder buf will be sent directly to DDR using
 * DDR aliasing into non-ACP address space. Such Fwder buffers must be
 * explicitly managed from a coherency perspective.
 */
static inline void BCMFASTPATH
osl_fwderbuf_reset(osl_t *osh, struct sk_buff *skb)
{
}

static struct sk_buff * BCMFASTPATH
osl_alloc_skb(osl_t *osh, unsigned int len)
{
	struct sk_buff *skb;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
	gfp_t flags = (in_atomic() || irqs_disabled()) ? GFP_ATOMIC : GFP_KERNEL;
#ifdef DHD_USE_ATOMIC_PKTGET
	flags = GFP_ATOMIC;
#endif /* DHD_USE_ATOMIC_PKTGET */
	skb = __dev_alloc_skb(len, flags);
#else
	skb = dev_alloc_skb(len);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25) */

	return skb;
}

/* Convert a driver packet to native(OS) packet
 * In the process, packettag is zeroed out before sending up
 * IP code depends on skb->cb to be setup correctly with various options
 * In our case, that means it should be 0
 */
struct sk_buff * BCMFASTPATH
osl_pkt_tonative(osl_t *osh, void *pkt)
{
	struct sk_buff *nskb;

	if (osh->pub.pkttag)
		OSL_PKTTAG_CLEAR(pkt);

	/* Decrement the packet counter */
	for (nskb = (struct sk_buff *)pkt; nskb; nskb = nskb->next) {
		atomic_sub(PKTISCHAINED(nskb) ? PKTCCNT(nskb) : 1, &osh->cmn->pktalloced);

	}
	return (struct sk_buff *)pkt;
}

/* Convert a native(OS) packet to driver packet.
 * In the process, native packet is destroyed, there is no copying
 * Also, a packettag is zeroed out
 */
void * BCMFASTPATH
osl_pkt_frmnative(osl_t *osh, void *pkt)
{
	struct sk_buff *cskb;
	struct sk_buff *nskb;
	unsigned long pktalloced = 0;

	if (osh->pub.pkttag)
		OSL_PKTTAG_CLEAR(pkt);

	/* walk the PKTCLINK() list */
	for (cskb = (struct sk_buff *)pkt;
	     cskb != NULL;
	     cskb = PKTISCHAINED(cskb) ? PKTCLINK(cskb) : NULL) {

		/* walk the pkt buffer list */
		for (nskb = cskb; nskb; nskb = nskb->next) {

			/* Increment the packet counter */
			pktalloced++;

			/* clean the 'prev' pointer
			 * Kernel 3.18 is leaving skb->prev pointer set to skb
			 * to indicate a non-fragmented skb
			 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
			nskb->prev = NULL;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0) */

		}
	}

	/* Increment the packet counter */
	atomic_add(pktalloced, &osh->cmn->pktalloced);

	return (void *)pkt;
}

/* Return a new packet. zero out pkttag */
#ifdef BCM_OBJECT_TRACE
void * BCMFASTPATH
linux_pktget(osl_t *osh, uint len, int line, const char *caller)
#else
void * BCMFASTPATH
linux_pktget(osl_t *osh, uint len)
#endif /* BCM_OBJECT_TRACE */
{
	struct sk_buff *skb;
	uchar num = 0;
	if (lmtest != FALSE) {
		get_random_bytes(&num, sizeof(uchar));
		if ((num + 1) <= (256 * lmtest / 100))
			return NULL;
	}

	if ((skb = osl_alloc_skb(osh, len))) {
		skb->tail += len;
		skb->len  += len;
		skb->priority = 0;

		atomic_inc(&osh->cmn->pktalloced);
#ifdef BCM_OBJECT_TRACE
		bcm_object_trace_opr(skb, BCM_OBJDBG_ADD_PKT, caller, line);
#endif /* BCM_OBJECT_TRACE */
	}

	return ((void*) skb);
}

/* Free the driver packet. Free the tag if present */
#ifdef BCM_OBJECT_TRACE
void BCMFASTPATH
linux_pktfree(osl_t *osh, void *p, bool send, int line, const char *caller)
#else
void BCMFASTPATH
linux_pktfree(osl_t *osh, void *p, bool send)
#endif /* BCM_OBJECT_TRACE */
{
	struct sk_buff *skb, *nskb;
	if (osh == NULL)
		return;

	skb = (struct sk_buff*) p;

	if (send) {
		if (osh->pub.tx_fn) {
			osh->pub.tx_fn(osh->pub.tx_ctx, p, 0);
		}
	} else {
		if (osh->pub.rx_fn) {
			osh->pub.rx_fn(osh->pub.rx_ctx, p);
		}
	}

	PKTDBG_TRACE(osh, (void *) skb, PKTLIST_PKTFREE);

#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_CTRLBUF)
	if (skb && (skb->mac_len == PREALLOC_USED_MAGIC)) {
		printk("%s: pkt %p is from static pool\n",
			__FUNCTION__, p);
		dump_stack();
		return;
	}

	if (skb && (skb->mac_len == PREALLOC_FREE_MAGIC)) {
		printk("%s: pkt %p is from static pool and not in used\n",
			__FUNCTION__, p);
		dump_stack();
		return;
	}
#endif /* CONFIG_DHD_USE_STATIC_BUF && DHD_USE_STATIC_CTRLBUF */

	/* perversion: we use skb->next to chain multi-skb packets */
	while (skb) {
		nskb = skb->next;
		skb->next = NULL;

#ifdef BCM_OBJECT_TRACE
		bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE, caller, line);
#endif /* BCM_OBJECT_TRACE */

		{
			if (skb->destructor) {
				/* cannot kfree_skb() on hard IRQ (net/core/skbuff.c) if
				 * destructor exists
				 */
				dev_kfree_skb_any(skb);
			} else {
				/* can free immediately (even in_irq()) if destructor
				 * does not exist
				 */
				dev_kfree_skb(skb);
			}
		}
		atomic_dec(&osh->cmn->pktalloced);
		skb = nskb;
	}
}

#ifdef CONFIG_DHD_USE_STATIC_BUF
void*
osl_pktget_static(osl_t *osh, uint len)
{
	int i = 0;
	struct sk_buff *skb;
#ifdef DHD_USE_STATIC_CTRLBUF
	unsigned long flags;
#endif /* DHD_USE_STATIC_CTRLBUF */

	if (!bcm_static_skb)
		return linux_pktget(osh, len);

	if (len > DHD_SKB_MAX_BUFSIZE) {
		printk("%s: attempt to allocate huge packet (0x%x)\n", __FUNCTION__, len);
		return linux_pktget(osh, len);
	}

#ifdef DHD_USE_STATIC_CTRLBUF
	spin_lock_irqsave(&bcm_static_skb->osl_pkt_lock, flags);

	if (len <= DHD_SKB_2PAGE_BUFSIZE) {
		uint32 index;
		for (i = 0; i < STATIC_PKT_2PAGE_NUM; i++) {
			index = bcm_static_skb->last_allocated_index % STATIC_PKT_2PAGE_NUM;
			bcm_static_skb->last_allocated_index++;
			if (bcm_static_skb->skb_8k[index] &&
				bcm_static_skb->pkt_use[index] == 0) {
				break;
			}
		}

		if (i < STATIC_PKT_2PAGE_NUM) {
			bcm_static_skb->pkt_use[index] = 1;
			skb = bcm_static_skb->skb_8k[index];
			skb->data = skb->head;
#ifdef NET_SKBUFF_DATA_USES_OFFSET
			skb_set_tail_pointer(skb, PKT_HEADROOM_DEFAULT);
#else
			skb->tail = skb->data + PKT_HEADROOM_DEFAULT;
#endif /* NET_SKBUFF_DATA_USES_OFFSET */
			skb->data += PKT_HEADROOM_DEFAULT;
			skb->cloned = 0;
			skb->priority = 0;
#ifdef NET_SKBUFF_DATA_USES_OFFSET
			skb_set_tail_pointer(skb, len);
#else
			skb->tail = skb->data + len;
#endif /* NET_SKBUFF_DATA_USES_OFFSET */
			skb->len = len;
			skb->mac_len = PREALLOC_USED_MAGIC;
			spin_unlock_irqrestore(&bcm_static_skb->osl_pkt_lock, flags);
			return skb;
		}
	}

	spin_unlock_irqrestore(&bcm_static_skb->osl_pkt_lock, flags);
	printk("%s: all static pkt in use!\n", __FUNCTION__);
	return NULL;
#else
	down(&bcm_static_skb->osl_pkt_sem);

	if (len <= DHD_SKB_1PAGE_BUFSIZE) {
		for (i = 0; i < STATIC_PKT_1PAGE_NUM; i++) {
			if (bcm_static_skb->skb_4k[i] &&
				bcm_static_skb->pkt_use[i] == 0) {
				break;
			}
		}

		if (i != STATIC_PKT_1PAGE_NUM) {
			bcm_static_skb->pkt_use[i] = 1;

			skb = bcm_static_skb->skb_4k[i];
#ifdef NET_SKBUFF_DATA_USES_OFFSET
			skb_set_tail_pointer(skb, len);
#else
			skb->tail = skb->data + len;
#endif /* NET_SKBUFF_DATA_USES_OFFSET */
			skb->len = len;

			up(&bcm_static_skb->osl_pkt_sem);
			return skb;
		}
	}

	if (len <= DHD_SKB_2PAGE_BUFSIZE) {
		for (i = STATIC_PKT_1PAGE_NUM; i < STATIC_PKT_1_2PAGE_NUM; i++) {
			if (bcm_static_skb->skb_8k[i - STATIC_PKT_1PAGE_NUM] &&
				bcm_static_skb->pkt_use[i] == 0) {
				break;
			}
		}

		if ((i >= STATIC_PKT_1PAGE_NUM) && (i < STATIC_PKT_1_2PAGE_NUM)) {
			bcm_static_skb->pkt_use[i] = 1;
			skb = bcm_static_skb->skb_8k[i - STATIC_PKT_1PAGE_NUM];
#ifdef NET_SKBUFF_DATA_USES_OFFSET
			skb_set_tail_pointer(skb, len);
#else
			skb->tail = skb->data + len;
#endif /* NET_SKBUFF_DATA_USES_OFFSET */
			skb->len = len;

			up(&bcm_static_skb->osl_pkt_sem);
			return skb;
		}
	}

#if defined(ENHANCED_STATIC_BUF)
	if (bcm_static_skb->skb_16k &&
		bcm_static_skb->pkt_use[STATIC_PKT_MAX_NUM - 1] == 0) {
		bcm_static_skb->pkt_use[STATIC_PKT_MAX_NUM - 1] = 1;

		skb = bcm_static_skb->skb_16k;
#ifdef NET_SKBUFF_DATA_USES_OFFSET
		skb_set_tail_pointer(skb, len);
#else
		skb->tail = skb->data + len;
#endif /* NET_SKBUFF_DATA_USES_OFFSET */
		skb->len = len;

		up(&bcm_static_skb->osl_pkt_sem);
		return skb;
	}
#endif /* ENHANCED_STATIC_BUF */

	up(&bcm_static_skb->osl_pkt_sem);
	printk("%s: all static pkt in use!\n", __FUNCTION__);
	return linux_pktget(osh, len);
#endif /* DHD_USE_STATIC_CTRLBUF */
}

void
osl_pktfree_static(osl_t *osh, void *p, bool send)
{
	int i;
#ifdef DHD_USE_STATIC_CTRLBUF
	struct sk_buff *skb = (struct sk_buff *)p;
	unsigned long flags;
#endif /* DHD_USE_STATIC_CTRLBUF */

	if (!p) {
		return;
	}

	if (!bcm_static_skb) {
		linux_pktfree(osh, p, send);
		return;
	}

#ifdef DHD_USE_STATIC_CTRLBUF
	spin_lock_irqsave(&bcm_static_skb->osl_pkt_lock, flags);

	for (i = 0; i < STATIC_PKT_2PAGE_NUM; i++) {
		if (p == bcm_static_skb->skb_8k[i]) {
			if (bcm_static_skb->pkt_use[i] == 0) {
				printk("%s: static pkt idx %d(%p) is double free\n",
					__FUNCTION__, i, p);
			} else {
				bcm_static_skb->pkt_use[i] = 0;
			}

			if (skb->mac_len != PREALLOC_USED_MAGIC) {
				printk("%s: static pkt idx %d(%p) is not in used\n",
					__FUNCTION__, i, p);
			}

			skb->mac_len = PREALLOC_FREE_MAGIC;
			spin_unlock_irqrestore(&bcm_static_skb->osl_pkt_lock, flags);
			return;
		}
	}

	spin_unlock_irqrestore(&bcm_static_skb->osl_pkt_lock, flags);
	printk("%s: packet %p does not exist in the pool\n", __FUNCTION__, p);
#else
	down(&bcm_static_skb->osl_pkt_sem);
	for (i = 0; i < STATIC_PKT_1PAGE_NUM; i++) {
		if (p == bcm_static_skb->skb_4k[i]) {
			bcm_static_skb->pkt_use[i] = 0;
			up(&bcm_static_skb->osl_pkt_sem);
			return;
		}
	}

	for (i = STATIC_PKT_1PAGE_NUM; i < STATIC_PKT_1_2PAGE_NUM; i++) {
		if (p == bcm_static_skb->skb_8k[i - STATIC_PKT_1PAGE_NUM]) {
			bcm_static_skb->pkt_use[i] = 0;
			up(&bcm_static_skb->osl_pkt_sem);
			return;
		}
	}
#ifdef ENHANCED_STATIC_BUF
	if (p == bcm_static_skb->skb_16k) {
		bcm_static_skb->pkt_use[STATIC_PKT_MAX_NUM - 1] = 0;
		up(&bcm_static_skb->osl_pkt_sem);
		return;
	}
#endif // endif
	up(&bcm_static_skb->osl_pkt_sem);
#endif /* DHD_USE_STATIC_CTRLBUF */
	linux_pktfree(osh, p, send);
}
#endif /* CONFIG_DHD_USE_STATIC_BUF */

/* Clone a packet.
 * The pkttag contents are NOT cloned.
 */
#ifdef BCM_OBJECT_TRACE
void *
osl_pktdup(osl_t *osh, void *skb, int line, const char *caller)
#else
void *
osl_pktdup(osl_t *osh, void *skb)
#endif /* BCM_OBJECT_TRACE */
{
	void * p;

	ASSERT(!PKTISCHAINED(skb));

	/* clear the CTFBUF flag if set and map the rest of the buffer
	 * before cloning.
	 */
	PKTCTFMAP(osh, skb);

	if ((p = skb_clone((struct sk_buff *)skb, GFP_ATOMIC)) == NULL)
		return NULL;

	/* skb_clone copies skb->cb.. we don't want that */
	if (osh->pub.pkttag)
		OSL_PKTTAG_CLEAR(p);

	/* Increment the packet counter */
	atomic_inc(&osh->cmn->pktalloced);
#ifdef BCM_OBJECT_TRACE
	bcm_object_trace_opr(p, BCM_OBJDBG_ADD_PKT, caller, line);
#endif /* BCM_OBJECT_TRACE */

	return (p);
}

/*
 * BINOSL selects the slightly slower function-call-based binary compatible osl.
 */

uint
osl_pktalloced(osl_t *osh)
{
	if (atomic_read(&osh->cmn->refcount) == 1)
		return (atomic_read(&osh->cmn->pktalloced));
	else
		return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0) && defined(TSQ_MULTIPLIER)
#include <linux/kallsyms.h>
#include <net/sock.h>
void
osl_pkt_orphan_partial(struct sk_buff *skb)
{
	uint32 fraction;
	static void *p_tcp_wfree = NULL;

	if (!skb->destructor || skb->destructor == sock_wfree)
		return;

	if (unlikely(!p_tcp_wfree)) {
		char sym[KSYM_SYMBOL_LEN];
		sprint_symbol(sym, (unsigned long)skb->destructor);
		sym[9] = 0;
		if (!strcmp(sym, "tcp_wfree"))
			p_tcp_wfree = skb->destructor;
		else
			return;
	}

	if (unlikely(skb->destructor != p_tcp_wfree || !skb->sk))
		return;

	/* abstract a certain portion of skb truesize from the socket
	 * sk_wmem_alloc to allow more skb can be allocated for this
	 * socket for better cusion meeting WiFi device requirement
	 */
	fraction = skb->truesize * (TSQ_MULTIPLIER - 1) / TSQ_MULTIPLIER;
	skb->truesize -= fraction;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	atomic_sub(fraction, &skb->sk->sk_wmem_alloc.refs);
#else
	atomic_sub(fraction, &skb->sk->sk_wmem_alloc);
#endif // endif
}
#endif /* LINUX_VERSION >= 3.6.0 && TSQ_MULTIPLIER */
