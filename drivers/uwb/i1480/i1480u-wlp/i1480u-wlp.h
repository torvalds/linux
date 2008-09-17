/*
 * Intel 1480 Wireless UWB Link USB
 * Header formats, constants, general internal interfaces
 *
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * This is not an standard interface.
 *
 * FIXME: docs
 *
 * i1480u-wlp is pretty simple: two endpoints, one for tx, one for
 * rx. rx is polled. Network packets (ethernet, whatever) are wrapped
 * in i1480 TX or RX headers (for sending over the air), and these
 * packets are wrapped in UNTD headers (for sending to the WLP UWB
 * controller).
 *
 * UNTD packets (UNTD hdr + i1480 hdr + network packet) packets
 * cannot be bigger than i1480u_MAX_FRG_SIZE. When this happens, the
 * i1480 packet is broken in chunks/packets:
 *
 * UNTD-1st.hdr + i1480.hdr + payload
 * UNTD-next.hdr + payload
 * ...
 * UNTD-last.hdr + payload
 *
 * so that each packet is smaller or equal than i1480u_MAX_FRG_SIZE.
 *
 * All HW structures and bitmaps are little endian, so we need to play
 * ugly tricks when defining bitfields. Hoping for the day GCC
 * implements __attribute__((endian(1234))).
 *
 * FIXME: ROADMAP to the whole implementation
 */

#ifndef __i1480u_wlp_h__
#define __i1480u_wlp_h__

#include <linux/usb.h>
#include <linux/netdevice.h>
#include <linux/uwb.h>		/* struct uwb_rc, struct uwb_notifs_handler */
#include <linux/wlp.h>
#include "../i1480-wlp.h"

#undef i1480u_FLOW_CONTROL	/* Enable flow control code */

/**
 * Basic flow control
 */
enum {
	i1480u_TX_INFLIGHT_MAX = 1000,
	i1480u_TX_INFLIGHT_THRESHOLD = 100,
};

/** Maximum size of a transaction that we can tx/rx */
enum {
	/* Maximum packet size computed as follows: max UNTD header (8) +
	 * i1480 RX header (8) + max Ethernet header and payload (4096) +
	 * Padding added by skb_reserve (2) to make post Ethernet payload
	 * start on 16 byte boundary*/
	i1480u_MAX_RX_PKT_SIZE = 4114,
	i1480u_MAX_FRG_SIZE = 512,
	i1480u_RX_BUFS = 9,
};


/**
 * UNTD packet type
 *
 * We need to fragment any payload whose UNTD packet is going to be
 * bigger than i1480u_MAX_FRG_SIZE.
 */
enum i1480u_pkt_type {
	i1480u_PKT_FRAG_1ST = 0x1,
	i1480u_PKT_FRAG_NXT = 0x0,
	i1480u_PKT_FRAG_LST = 0x2,
	i1480u_PKT_FRAG_CMP = 0x3
};
enum {
	i1480u_PKT_NONE = 0x4,
};

/** USB Network Transfer Descriptor - common */
struct untd_hdr {
	u8     type;
	__le16 len;
} __attribute__((packed));

static inline enum i1480u_pkt_type untd_hdr_type(const struct untd_hdr *hdr)
{
	return hdr->type & 0x03;
}

static inline int untd_hdr_rx_tx(const struct untd_hdr *hdr)
{
	return (hdr->type >> 2) & 0x01;
}

static inline void untd_hdr_set_type(struct untd_hdr *hdr, enum i1480u_pkt_type type)
{
	hdr->type = (hdr->type & ~0x03) | type;
}

static inline void untd_hdr_set_rx_tx(struct untd_hdr *hdr, int rx_tx)
{
	hdr->type = (hdr->type & ~0x04) | (rx_tx << 2);
}


/**
 * USB Network Transfer Descriptor - Complete Packet
 *
 * This is for a packet that is smaller (header + payload) than
 * i1480u_MAX_FRG_SIZE.
 *
 * @hdr.total_len is the size of the payload; the payload doesn't
 * count this header nor the padding, but includes the size of i1480
 * header.
 */
struct untd_hdr_cmp {
	struct untd_hdr	hdr;
	u8		padding;
} __attribute__((packed));


/**
 * USB Network Transfer Descriptor - First fragment
 *
 * @hdr.len is the size of the *whole packet* (excluding UNTD
 * headers); @fragment_len is the size of the payload (excluding UNTD
 * headers, but including i1480 headers).
 */
struct untd_hdr_1st {
	struct untd_hdr	hdr;
	__le16		fragment_len;
	u8		padding[3];
} __attribute__((packed));


/**
 * USB Network Transfer Descriptor - Next / Last [Rest]
 *
 * @hdr.len is the size of the payload, not including headrs.
 */
struct untd_hdr_rst {
	struct untd_hdr	hdr;
	u8		padding;
} __attribute__((packed));


/**
 * Transmission context
 *
 * Wraps all the stuff needed to track a pending/active tx
 * operation.
 */
struct i1480u_tx {
	struct list_head list_node;
	struct i1480u *i1480u;
	struct urb *urb;

	struct sk_buff *skb;
	struct wlp_tx_hdr *wlp_tx_hdr;

	void *buf;	/* if NULL, no new buf was used */
	size_t buf_size;
};

/**
 * Basic flow control
 *
 * We maintain a basic flow control counter. "count" how many TX URBs are
 * outstanding. Only allow "max"
 * TX URBs to be outstanding. If this value is reached the queue will be
 * stopped. The queue will be restarted when there are
 * "threshold" URBs outstanding.
 * Maintain a counter of how many time the TX queue needed to be restarted
 * due to the "max" being exceeded and the "threshold" reached again. The
 * timestamp "restart_ts" is to keep track from when the counter was last
 * queried (see sysfs handling of file wlp_tx_inflight).
 */
struct i1480u_tx_inflight {
	atomic_t count;
	unsigned long max;
	unsigned long threshold;
	unsigned long restart_ts;
	atomic_t restart_count;
};

/**
 * Instance of a i1480u WLP interface
 *
 * Keeps references to the USB device that wraps it, as well as it's
 * interface and associated UWB host controller. As well, it also
 * keeps a link to the netdevice for integration into the networking
 * stack.
 * We maintian separate error history for the tx and rx endpoints because
 * the implementation does not rely on locking - having one shared
 * structure between endpoints may cause problems. Adding locking to the
 * implementation will have higher cost than adding a separate structure.
 */
struct i1480u {
	struct usb_device *usb_dev;
	struct usb_interface *usb_iface;
	struct net_device *net_dev;

	spinlock_t lock;
	struct net_device_stats stats;

	/* RX context handling */
	struct sk_buff *rx_skb;
	struct uwb_dev_addr rx_srcaddr;
	size_t rx_untd_pkt_size;
	struct i1480u_rx_buf {
		struct i1480u *i1480u;	/* back pointer */
		struct urb *urb;
		struct sk_buff *data;	/* i1480u_MAX_RX_PKT_SIZE each */
	} rx_buf[i1480u_RX_BUFS];	/* N bufs */

	spinlock_t tx_list_lock;	/* TX context */
	struct list_head tx_list;
	u8 tx_stream;

	struct stats lqe_stats, rssi_stats;	/* radio statistics */

	/* Options we can set from sysfs */
	struct wlp_options options;
	struct uwb_notifs_handler uwb_notifs_handler;
	struct edc tx_errors;
	struct edc rx_errors;
	struct wlp wlp;
#ifdef i1480u_FLOW_CONTROL
	struct urb *notif_urb;
	struct edc notif_edc;		/* error density counter */
	u8 notif_buffer[1];
#endif
	struct i1480u_tx_inflight tx_inflight;
};

/* Internal interfaces */
extern void i1480u_rx_cb(struct urb *urb);
extern int i1480u_rx_setup(struct i1480u *);
extern void i1480u_rx_release(struct i1480u *);
extern void i1480u_tx_release(struct i1480u *);
extern int i1480u_xmit_frame(struct wlp *, struct sk_buff *,
			     struct uwb_dev_addr *);
extern void i1480u_stop_queue(struct wlp *);
extern void i1480u_start_queue(struct wlp *);
extern int i1480u_sysfs_setup(struct i1480u *);
extern void i1480u_sysfs_release(struct i1480u *);

/* netdev interface */
extern int i1480u_open(struct net_device *);
extern int i1480u_stop(struct net_device *);
extern int i1480u_hard_start_xmit(struct sk_buff *, struct net_device *);
extern void i1480u_tx_timeout(struct net_device *);
extern int i1480u_set_config(struct net_device *, struct ifmap *);
extern struct net_device_stats *i1480u_get_stats(struct net_device *);
extern int i1480u_change_mtu(struct net_device *, int);
extern void i1480u_uwb_notifs_cb(void *, struct uwb_dev *, enum uwb_notifs);

/* bandwidth allocation callback */
extern void  i1480u_bw_alloc_cb(struct uwb_rsv *);

/* Sys FS */
extern struct attribute_group i1480u_wlp_attr_group;

#endif /* #ifndef __i1480u_wlp_h__ */
