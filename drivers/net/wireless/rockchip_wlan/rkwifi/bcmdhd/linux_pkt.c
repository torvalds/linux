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

#include <typedefs.h>
#include <bcmendian.h>
#include <linuxver.h>
#include <bcmdefs.h>

#include <linux/random.h>

#include <osl.h>
#include <bcmutils.h>
#include <pcicfg.h>
#include <dngl_stats.h>
#include <dhd.h>

#if defined(BCMASSERT_LOG) && !defined(OEM_ANDROID)
#include <bcm_assert_log.h>
#endif
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
				DHD_PREALLOC_OSL_BUF, STATIC_BUF_SIZE + STATIC_BUF_TOTAL_LEN))) {
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
			skb_buff_ptr = wifi_platform_prealloc(adapter, DHD_PREALLOC_SKB_BUF, 0);
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

static struct sk_buff *
BCMFASTPATH(osl_alloc_skb)(osl_t *osh, unsigned int len)
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
struct sk_buff *
BCMFASTPATH(osl_pkt_tonative)(osl_t *osh, void *pkt)
{
	struct sk_buff *nskb;
#ifdef BCMDBG_CTRACE
	struct sk_buff *nskb1, *nskb2;
#endif
#ifdef BCMDBG_PKT
	unsigned long flags;
#endif

	if (osh->pub.pkttag)
		OSL_PKTTAG_CLEAR(pkt);

	/* Decrement the packet counter */
	for (nskb = (struct sk_buff *)pkt; nskb; nskb = nskb->next) {
#ifdef BCMDBG_PKT
		OSL_PKTLIST_LOCK(&osh->cmn->pktlist_lock, flags);
		pktlist_remove(&(osh->cmn->pktlist), (void *) nskb);
		OSL_PKTLIST_UNLOCK(&osh->cmn->pktlist_lock, flags);
#endif  /* BCMDBG_PKT */
		atomic_sub(PKTISCHAINED(nskb) ? PKTCCNT(nskb) : 1, &osh->cmn->pktalloced);

#ifdef BCMDBG_CTRACE
		for (nskb1 = nskb; nskb1 != NULL; nskb1 = nskb2) {
			if (PKTISCHAINED(nskb1)) {
				nskb2 = PKTCLINK(nskb1);
			} else {
				nskb2 = NULL;
			}

			DEL_CTRACE(osh, nskb1);
		}
#endif /* BCMDBG_CTRACE */
	}
	return (struct sk_buff *)pkt;
}

/* Convert a native(OS) packet to driver packet.
 * In the process, native packet is destroyed, there is no copying
 * Also, a packettag is zeroed out
 */
#ifdef BCMDBG_PKT
void *
osl_pkt_frmnative(osl_t *osh, void *pkt, int line, char *file)
#else /* BCMDBG_PKT pkt logging for debugging */
#ifdef BCMDBG_CTRACE
void *
BCMFASTPATH(osl_pkt_frmnative)(osl_t *osh, void *pkt, int line, char *file)
#else
void *
BCMFASTPATH(osl_pkt_frmnative)(osl_t *osh, void *pkt)
#endif /* BCMDBG_CTRACE */
#endif /* BCMDBG_PKT */
{
	struct sk_buff *cskb;
	struct sk_buff *nskb;
	unsigned long pktalloced = 0;
#ifdef BCMDBG_PKT
	unsigned long flags;
#endif

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

#ifdef BCMDBG_PKT
			OSL_PKTLIST_LOCK(&osh->cmn->pktlist_lock, flags);
			pktlist_add(&(osh->cmn->pktlist), (void *) nskb, line, file);
			OSL_PKTLIST_UNLOCK(&osh->cmn->pktlist_lock, flags);
#endif  /* BCMDBG_PKT */

#ifdef BCMDBG_CTRACE
			ADD_CTRACE(osh, nskb, file, line);
#endif /* BCMDBG_CTRACE */
		}
	}

	/* Increment the packet counter */
	atomic_add(pktalloced, &osh->cmn->pktalloced);

	return (void *)pkt;
}

/* Return a new packet. zero out pkttag */
#ifdef BCMDBG_PKT
void *
BCMFASTPATH(linux_pktget)(osl_t *osh, uint len, int line, char *file)
#else /* BCMDBG_PKT */
#ifdef BCMDBG_CTRACE
void *
BCMFASTPATH(linux_pktget)(osl_t *osh, uint len, int line, char *file)
#else
#ifdef BCM_OBJECT_TRACE
void *
BCMFASTPATH(linux_pktget)(osl_t *osh, uint len, int line, const char *caller)
#else
void *
BCMFASTPATH(linux_pktget)(osl_t *osh, uint len)
#endif /* BCM_OBJECT_TRACE */
#endif /* BCMDBG_CTRACE */
#endif /* BCMDBG_PKT */
{
	struct sk_buff *skb;
#ifdef BCMDBG_PKT
	unsigned long flags;
#endif
	uchar num = 0;
	if (lmtest != FALSE) {
		get_random_bytes(&num, sizeof(uchar));
		if ((num + 1) <= (256 * lmtest / 100))
			return NULL;
	}

	if ((skb = osl_alloc_skb(osh, len))) {
#ifdef BCMDBG
		skb_put(skb, len);
#else
		skb->tail += len;
		skb->len  += len;
#endif
		skb->priority = 0;

#ifdef BCMDBG_CTRACE
		ADD_CTRACE(osh, skb, file, line);
#endif
#ifdef BCMDBG_PKT
		OSL_PKTLIST_LOCK(&osh->cmn->pktlist_lock, flags);
		pktlist_add(&(osh->cmn->pktlist), (void *) skb, line, file);
		OSL_PKTLIST_UNLOCK(&osh->cmn->pktlist_lock, flags);
#endif
		atomic_inc(&osh->cmn->pktalloced);
#ifdef BCM_OBJECT_TRACE
		bcm_object_trace_opr(skb, BCM_OBJDBG_ADD_PKT, caller, line);
#endif /* BCM_OBJECT_TRACE */
	}

	return ((void*) skb);
}

/* Free the driver packet. Free the tag if present */
#ifdef BCM_OBJECT_TRACE
void
BCMFASTPATH(linux_pktfree)(osl_t *osh, void *p, bool send, int line, const char *caller)
#else
void
BCMFASTPATH(linux_pktfree)(osl_t *osh, void *p, bool send)
#endif /* BCM_OBJECT_TRACE */
{
	struct sk_buff *skb, *nskb;
#ifdef BCMDBG_PKT
	unsigned long flags;
#endif
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

#ifdef BCMDBG_CTRACE
		DEL_CTRACE(osh, skb);
#endif
#ifdef BCMDBG_PKT
		OSL_PKTLIST_LOCK(&osh->cmn->pktlist_lock, flags);
		pktlist_remove(&(osh->cmn->pktlist), (void *) skb);
		OSL_PKTLIST_UNLOCK(&osh->cmn->pktlist_lock, flags);
#endif

#ifdef BCM_OBJECT_TRACE
		bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE, caller, line);
#endif /* BCM_OBJECT_TRACE */

		if (skb->destructor || irqs_disabled()) {
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
	OSL_STATIC_PKT_LOCK(&bcm_static_skb->osl_pkt_lock, flags);

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
			OSL_STATIC_PKT_UNLOCK(&bcm_static_skb->osl_pkt_lock, flags);
			return skb;
		}
	}

	OSL_STATIC_PKT_UNLOCK(&bcm_static_skb->osl_pkt_lock, flags);
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

#if defined (ENHANCED_STATIC_BUF)
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
	OSL_STATIC_PKT_LOCK(&bcm_static_skb->osl_pkt_lock, flags);

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
			OSL_STATIC_PKT_UNLOCK(&bcm_static_skb->osl_pkt_lock, flags);
			return;
		}
	}

	OSL_STATIC_PKT_UNLOCK(&bcm_static_skb->osl_pkt_lock, flags);
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
#endif
	up(&bcm_static_skb->osl_pkt_sem);
#endif /* DHD_USE_STATIC_CTRLBUF */
	linux_pktfree(osh, p, send);
}
#endif /* CONFIG_DHD_USE_STATIC_BUF */

/* Clone a packet.
 * The pkttag contents are NOT cloned.
 */
#ifdef BCMDBG_PKT
void *
osl_pktdup(osl_t *osh, void *skb, int line, char *file)
#else /* BCMDBG_PKT */
#ifdef BCMDBG_CTRACE
void *
osl_pktdup(osl_t *osh, void *skb, int line, char *file)
#else
#ifdef BCM_OBJECT_TRACE
void *
osl_pktdup(osl_t *osh, void *skb, int line, const char *caller)
#else
void *
osl_pktdup(osl_t *osh, void *skb)
#endif /* BCM_OBJECT_TRACE */
#endif /* BCMDBG_CTRACE */
#endif /* BCMDBG_PKT */
{
	void * p;
#ifdef BCMDBG_PKT
	unsigned long irqflags;
#endif

	ASSERT(!PKTISCHAINED(skb));

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

#ifdef BCMDBG_CTRACE
	ADD_CTRACE(osh, (struct sk_buff *)p, file, line);
#endif
#ifdef BCMDBG_PKT
	OSL_PKTLIST_LOCK(&osh->cmn->pktlist_lock, irqflags);
	pktlist_add(&(osh->cmn->pktlist), (void *) p, line, file);
	OSL_PKTLIST_UNLOCK(&osh->cmn->pktlist_lock, irqflags);
#endif
	return (p);
}

#ifdef BCMDBG_CTRACE
int osl_pkt_is_frmnative(osl_t *osh, struct sk_buff *pkt)
{
	unsigned long flags;
	struct sk_buff *skb;
	int ck = FALSE;

	OSL_CTRACE_LOCK(&osh->ctrace_lock, flags);

	list_for_each_entry(skb, &osh->ctrace_list, ctrace_list) {
		if (pkt == skb) {
			ck = TRUE;
			break;
		}
	}

	OSL_CTRACE_UNLOCK(&osh->ctrace_lock, flags);
	return ck;
}

void osl_ctrace_dump(osl_t *osh, struct bcmstrbuf *b)
{
	unsigned long flags;
	struct sk_buff *skb;
	int idx = 0;
	int i, j;

	OSL_CTRACE_LOCK(&osh->ctrace_lock, flags);

	if (b != NULL)
		bcm_bprintf(b, " Total %d sbk not free\n", osh->ctrace_num);
	else
		printk(" Total %d sbk not free\n", osh->ctrace_num);

	list_for_each_entry(skb, &osh->ctrace_list, ctrace_list) {
		if (b != NULL)
			bcm_bprintf(b, "[%d] skb %p:\n", ++idx, skb);
		else
			printk("[%d] skb %p:\n", ++idx, skb);

		for (i = 0; i < skb->ctrace_count; i++) {
			j = (skb->ctrace_start + i) % CTRACE_NUM;
			if (b != NULL)
				bcm_bprintf(b, "    [%s(%d)]\n", skb->func[j], skb->line[j]);
			else
				printk("    [%s(%d)]\n", skb->func[j], skb->line[j]);
		}
		if (b != NULL)
			bcm_bprintf(b, "\n");
		else
			printk("\n");
	}

	OSL_CTRACE_UNLOCK(&osh->ctrace_lock, flags);

	return;
}
#endif /* BCMDBG_CTRACE */

#ifdef BCMDBG_PKT
#ifdef BCMDBG_PTRACE
void
osl_pkttrace(osl_t *osh, void *pkt, uint16 bit)
{
	pktlist_trace(&(osh->cmn->pktlist), pkt, bit);
}
#endif /* BCMDBG_PTRACE */

char *
osl_pktlist_dump(osl_t *osh, char *buf)
{
	pktlist_dump(&(osh->cmn->pktlist), buf);
	return buf;
}

void
osl_pktlist_add(osl_t *osh, void *p, int line, char *file)
{
	unsigned long flags;
	OSL_PKTLIST_LOCK(&osh->cmn->pktlist_lock, flags);
	pktlist_add(&(osh->cmn->pktlist), p, line, file);
	OSL_PKTLIST_UNLOCK(&osh->cmn->pktlist_lock, flags);
}

void
osl_pktlist_remove(osl_t *osh, void *p)
{
	unsigned long flags;
	OSL_PKTLIST_LOCK(&osh->cmn->pktlist_lock, flags);
	pktlist_remove(&(osh->cmn->pktlist), p);
	OSL_PKTLIST_UNLOCK(&osh->cmn->pktlist_lock, flags);
}
#endif /* BCMDBG_PKT */

/*
 * BINOSL selects the slightly slower function-call-based binary compatible osl.
 */
#ifdef BINOSL
bool
osl_pktshared(void *skb)
{
	return (((struct sk_buff*)skb)->cloned);
}

uchar*
osl_pktdata(osl_t *osh, void *skb)
{
	return (((struct sk_buff*)skb)->data);
}

uint
osl_pktlen(osl_t *osh, void *skb)
{
	return (((struct sk_buff*)skb)->len);
}

uint
osl_pktheadroom(osl_t *osh, void *skb)
{
	return (uint) skb_headroom((struct sk_buff *) skb);
}

uint
osl_pkttailroom(osl_t *osh, void *skb)
{
	return (uint) skb_tailroom((struct sk_buff *) skb);
}

void*
osl_pktnext(osl_t *osh, void *skb)
{
	return (((struct sk_buff*)skb)->next);
}

void
osl_pktsetnext(void *skb, void *x)
{
	((struct sk_buff*)skb)->next = (struct sk_buff*)x;
}

void
osl_pktsetlen(osl_t *osh, void *skb, uint len)
{
	__skb_trim((struct sk_buff*)skb, len);
}

uchar*
osl_pktpush(osl_t *osh, void *skb, int bytes)
{
	return (skb_push((struct sk_buff*)skb, bytes));
}

uchar*
osl_pktpull(osl_t *osh, void *skb, int bytes)
{
	return (skb_pull((struct sk_buff*)skb, bytes));
}

void*
osl_pkttag(void *skb)
{
	return ((void*)(((struct sk_buff*)skb)->cb));
}

void*
osl_pktlink(void *skb)
{
	return (((struct sk_buff*)skb)->prev);
}

void
osl_pktsetlink(void *skb, void *x)
{
	((struct sk_buff*)skb)->prev = (struct sk_buff*)x;
}

uint
osl_pktprio(void *skb)
{
	return (((struct sk_buff*)skb)->priority);
}

void
osl_pktsetprio(void *skb, uint x)
{
	((struct sk_buff*)skb)->priority = x;
}
#endif	/* BINOSL */

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
osl_pkt_orphan_partial(struct sk_buff *skb, int tsq)
{
	uint32 fraction;
	static void *p_tcp_wfree = NULL;

	if (tsq <= 0)
		return;

	if (!skb->destructor || skb->destructor == sock_wfree)
		return;

	if (unlikely(!p_tcp_wfree)) {
		/* this is a hack to get tcp_wfree pointer since it's not
		 * exported. There are two possible call back function pointer
		 * stored in skb->destructor: tcp_wfree and sock_wfree.
		 * This expansion logic should only apply to TCP traffic which
		 * uses tcp_wfree as skb destructor
		 */
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
	fraction = skb->truesize * (tsq - 1) / tsq;
	skb->truesize -= fraction;
	atomic_sub(fraction, (atomic_t *)&skb->sk->sk_wmem_alloc);
	skb_orphan(skb);
}
#endif /* LINUX_VERSION >= 3.6.0 && TSQ_MULTIPLIER */
