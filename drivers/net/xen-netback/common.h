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

struct xen_netbk;

struct xenvif {
	/* Unique identifier for this interface. */
	domid_t          domid;
	unsigned int     handle;

	/* Reference to netback processing backend. */
	struct xen_netbk *netbk;

	u8               fe_dev_addr[6];

	/* Physical parameters of the comms window. */
	unsigned int     irq;

	/* List of frontends to notify after a batch of frames sent. */
	struct list_head notify_list;

	/* The shared rings and indexes. */
	struct xen_netif_tx_back_ring tx;
	struct xen_netif_rx_back_ring rx;

	/* Frontend feature information. */
	u8 can_sg:1;
	u8 gso:1;
	u8 gso_prefix:1;
	u8 csum:1;

	/* Internal feature information. */
	u8 can_queue:1;	    /* can queue packets for receiver? */

	/*
	 * Allow xenvif_start_xmit() to peek ahead in the rx request
	 * ring.  This is a prediction of what rx_req_cons will be
	 * once all queued skbs are put on the ring.
	 */
	RING_IDX rx_req_cons_peek;

	/* Transmit shaping: allow 'credit_bytes' every 'credit_usec'. */
	unsigned long   credit_bytes;
	unsigned long   credit_usec;
	unsigned long   remaining_credit;
	struct timer_list credit_timeout;

	/* Statistics */
	unsigned long rx_gso_checksum_fixup;

	/* Miscellaneous private stuff. */
	struct list_head schedule_list;
	atomic_t         refcnt;
	struct net_device *dev;

	wait_queue_head_t waiting_to_free;
};

static inline struct xenbus_device *xenvif_to_xenbus_device(struct xenvif *vif)
{
	return to_xenbus_device(vif->dev->dev.parent);
}

#define XEN_NETIF_TX_RING_SIZE __CONST_RING_SIZE(xen_netif_tx, PAGE_SIZE)
#define XEN_NETIF_RX_RING_SIZE __CONST_RING_SIZE(xen_netif_rx, PAGE_SIZE)

struct xenvif *xenvif_alloc(struct device *parent,
			    domid_t domid,
			    unsigned int handle);

int xenvif_connect(struct xenvif *vif, unsigned long tx_ring_ref,
		   unsigned long rx_ring_ref, unsigned int evtchn);
void xenvif_disconnect(struct xenvif *vif);

void xenvif_get(struct xenvif *vif);
void xenvif_put(struct xenvif *vif);

int xenvif_xenbus_init(void);

int xenvif_schedulable(struct xenvif *vif);

int xen_netbk_rx_ring_full(struct xenvif *vif);

int xen_netbk_must_stop_queue(struct xenvif *vif);

/* (Un)Map communication rings. */
void xen_netbk_unmap_frontend_rings(struct xenvif *vif);
int xen_netbk_map_frontend_rings(struct xenvif *vif,
				 grant_ref_t tx_ring_ref,
				 grant_ref_t rx_ring_ref);

/* (De)Register a xenvif with the netback backend. */
void xen_netbk_add_xenvif(struct xenvif *vif);
void xen_netbk_remove_xenvif(struct xenvif *vif);

/* (De)Schedule backend processing for a xenvif */
void xen_netbk_schedule_xenvif(struct xenvif *vif);
void xen_netbk_deschedule_xenvif(struct xenvif *vif);

/* Check for SKBs from frontend and schedule backend processing */
void xen_netbk_check_rx_xenvif(struct xenvif *vif);
/* Receive an SKB from the frontend */
void xenvif_receive_skb(struct xenvif *vif, struct sk_buff *skb);

/* Queue an SKB for transmission to the frontend */
void xen_netbk_queue_tx_skb(struct xenvif *vif, struct sk_buff *skb);
/* Notify xenvif that ring now has space to send an skb to the frontend */
void xenvif_notify_tx_completion(struct xenvif *vif);

/* Prevent the device from generating any further traffic. */
void xenvif_carrier_off(struct xenvif *vif);

/* Returns number of ring slots required to send an skb to the frontend */
unsigned int xen_netbk_count_skb_slots(struct xenvif *vif, struct sk_buff *skb);

#endif /* __XEN_NETBACK__COMMON_H__ */
