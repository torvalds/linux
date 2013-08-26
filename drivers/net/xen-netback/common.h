/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __XEN_NETBACK__COMMON_H__
#define __XEN_NETBACK__COMMON_H__

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include <xen/interface/io/netif.h>
#include <xen/interface/grant_table.h>
#include <xen/grant_table.h>
#include <xen/xenbus.h>

typedef unsigned int pending_ring_idx_t;
#define INVALID_PENDING_RING_IDX (~0U)

/* For the head field in pending_tx_info: it is used to indicate
 * whether this tx info is the head of one or more coalesced requests.
 *
 * When head != INVALID_PENDING_RING_IDX, it means the start of a new
 * tx requests queue and the end of previous queue.
 *
 * An example sequence of head fields (I = INVALID_PENDING_RING_IDX):
 *
 * ...|0 I I I|5 I|9 I I I|...
 * -->|<-INUSE----------------
 *
 * After consuming the first slot(s) we have:
 *
 * ...|V V V V|5 I|9 I I I|...
 * -----FREE->|<-INUSE--------
 *
 * where V stands for "valid pending ring index". Any number other
 * than INVALID_PENDING_RING_IDX is OK. These entries are considered
 * free and can contain any number other than
 * INVALID_PENDING_RING_IDX. In practice we use 0.
 *
 * The in use non-INVALID_PENDING_RING_IDX (say 0, 5 and 9 in the
 * above example) number is the index into pending_tx_info and
 * mmap_pages arrays.
 */
struct pending_tx_info {
	struct xen_netif_tx_request req; /* coalesced tx request */
	pending_ring_idx_t head; /* head != INVALID_PENDING_RING_IDX
				  * if it is head of one or more tx
				  * reqs
				  */
};

#define XEN_NETIF_TX_RING_SIZE __CONST_RING_SIZE(xen_netif_tx, PAGE_SIZE)
#define XEN_NETIF_RX_RING_SIZE __CONST_RING_SIZE(xen_netif_rx, PAGE_SIZE)

struct xenvif_rx_meta {
	int id;
	int size;
	int gso_size;
};

/* Discriminate from any valid pending_idx value. */
#define INVALID_PENDING_IDX 0xFFFF

#define MAX_BUFFER_OFFSET PAGE_SIZE

#define MAX_PENDING_REQS 256

struct xenvif {
	/* Unique identifier for this interface. */
	domid_t          domid;
	unsigned int     handle;

	/* Use NAPI for guest TX */
	struct napi_struct napi;
	/* When feature-split-event-channels = 0, tx_irq = rx_irq. */
	unsigned int tx_irq;
	/* Only used when feature-split-event-channels = 1 */
	char tx_irq_name[IFNAMSIZ+4]; /* DEVNAME-tx */
	struct xen_netif_tx_back_ring tx;
	struct sk_buff_head tx_queue;
	struct page *mmap_pages[MAX_PENDING_REQS];
	pending_ring_idx_t pending_prod;
	pending_ring_idx_t pending_cons;
	u16 pending_ring[MAX_PENDING_REQS];
	struct pending_tx_info pending_tx_info[MAX_PENDING_REQS];

	/* Coalescing tx requests before copying makes number of grant
	 * copy ops greater or equal to number of slots required. In
	 * worst case a tx request consumes 2 gnttab_copy.
	 */
	struct gnttab_copy tx_copy_ops[2*MAX_PENDING_REQS];


	/* Use kthread for guest RX */
	struct task_struct *task;
	wait_queue_head_t wq;
	/* When feature-split-event-channels = 0, tx_irq = rx_irq. */
	unsigned int rx_irq;
	/* Only used when feature-split-event-channels = 1 */
	char rx_irq_name[IFNAMSIZ+4]; /* DEVNAME-rx */
	struct xen_netif_rx_back_ring rx;
	struct sk_buff_head rx_queue;

	/* Allow xenvif_start_xmit() to peek ahead in the rx request
	 * ring.  This is a prediction of what rx_req_cons will be
	 * once all queued skbs are put on the ring.
	 */
	RING_IDX rx_req_cons_peek;

	/* Given MAX_BUFFER_OFFSET of 4096 the worst case is that each
	 * head/fragment page uses 2 copy operations because it
	 * straddles two buffers in the frontend.
	 */
	struct gnttab_copy grant_copy_op[2*XEN_NETIF_RX_RING_SIZE];
	struct xenvif_rx_meta meta[2*XEN_NETIF_RX_RING_SIZE];


	u8               fe_dev_addr[6];

	/* Frontend feature information. */
	u8 can_sg:1;
	u8 gso:1;
	u8 gso_prefix:1;
	u8 csum:1;

	/* Internal feature information. */
	u8 can_queue:1;	    /* can queue packets for receiver? */

	/* Transmit shaping: allow 'credit_bytes' every 'credit_usec'. */
	unsigned long   credit_bytes;
	unsigned long   credit_usec;
	unsigned long   remaining_credit;
	struct timer_list credit_timeout;

	/* Statistics */
	unsigned long rx_gso_checksum_fixup;

	/* Miscellaneous private stuff. */
	struct net_device *dev;
};

static inline struct xenbus_device *xenvif_to_xenbus_device(struct xenvif *vif)
{
	return to_xenbus_device(vif->dev->dev.parent);
}

struct xenvif *xenvif_alloc(struct device *parent,
			    domid_t domid,
			    unsigned int handle);

int xenvif_connect(struct xenvif *vif, unsigned long tx_ring_ref,
		   unsigned long rx_ring_ref, unsigned int tx_evtchn,
		   unsigned int rx_evtchn);
void xenvif_disconnect(struct xenvif *vif);

int xenvif_xenbus_init(void);
void xenvif_xenbus_fini(void);

int xenvif_schedulable(struct xenvif *vif);

int xen_netbk_rx_ring_full(struct xenvif *vif);

int xen_netbk_must_stop_queue(struct xenvif *vif);

/* (Un)Map communication rings. */
void xen_netbk_unmap_frontend_rings(struct xenvif *vif);
int xen_netbk_map_frontend_rings(struct xenvif *vif,
				 grant_ref_t tx_ring_ref,
				 grant_ref_t rx_ring_ref);

/* Check for SKBs from frontend and schedule backend processing */
void xen_netbk_check_rx_xenvif(struct xenvif *vif);

/* Queue an SKB for transmission to the frontend */
void xen_netbk_queue_tx_skb(struct xenvif *vif, struct sk_buff *skb);
/* Notify xenvif that ring now has space to send an skb to the frontend */
void xenvif_notify_tx_completion(struct xenvif *vif);

/* Prevent the device from generating any further traffic. */
void xenvif_carrier_off(struct xenvif *vif);

/* Returns number of ring slots required to send an skb to the frontend */
unsigned int xen_netbk_count_skb_slots(struct xenvif *vif, struct sk_buff *skb);

int xen_netbk_tx_action(struct xenvif *vif, int budget);
void xen_netbk_rx_action(struct xenvif *vif);

int xen_netbk_kthread(void *data);

extern bool separate_tx_rx_irq;

#endif /* __XEN_NETBACK__COMMON_H__ */
