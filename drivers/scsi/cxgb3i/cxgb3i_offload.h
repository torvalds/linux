/*
 * cxgb3i_offload.h: Chelsio S3xx iscsi offloaded tcp connection management
 *
 * Copyright (C) 2003-2008 Chelsio Communications.  All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the LICENSE file included in this
 * release for licensing terms and conditions.
 *
 * Written by:	Dimitris Michailidis (dm@chelsio.com)
 *		Karen Xie (kxie@chelsio.com)
 */

#ifndef _CXGB3I_OFFLOAD_H
#define _CXGB3I_OFFLOAD_H

#include <linux/skbuff.h>
#include <net/tcp.h>

#include "common.h"
#include "adapter.h"
#include "t3cdev.h"
#include "cxgb3_offload.h"

#define cxgb3i_log_error(fmt...) printk(KERN_ERR "cxgb3i: ERR! " fmt)
#define cxgb3i_log_warn(fmt...)	 printk(KERN_WARNING "cxgb3i: WARN! " fmt)
#define cxgb3i_log_info(fmt...)  printk(KERN_INFO "cxgb3i: " fmt)
#define cxgb3i_log_debug(fmt, args...) \
	printk(KERN_INFO "cxgb3i: %s - " fmt, __func__ , ## args)

/**
 * struct s3_conn - an iscsi tcp connection structure
 *
 * @dev:	net device of with connection
 * @cdev:	adapter t3cdev for net device
 * @flags:	see c3cn_flags below
 * @tid:	connection id assigned by the h/w
 * @qset:	queue set used by connection
 * @mss_idx:	Maximum Segment Size table index
 * @l2t:	ARP resolution entry for offload packets
 * @wr_max:	maximum in-flight writes
 * @wr_avail:	number of writes available
 * @wr_unacked:	writes since last request for completion notification
 * @wr_pending_head: head of pending write queue
 * @wr_pending_tail: tail of pending write queue
 * @cpl_close:	skb for cpl_close_req
 * @cpl_abort_req: skb for cpl_abort_req
 * @cpl_abort_rpl: skb for cpl_abort_rpl
 * @lock:	connection status lock
 * @refcnt:	reference count on connection
 * @state:	connection state
 * @saddr:	source ip/port address
 * @daddr:	destination ip/port address
 * @dst_cache:	reference to destination route
 * @receive_queue: received PDUs
 * @write_queue: un-pushed pending writes
 * @retry_timer: retry timer for various operations
 * @err:	connection error status
 * @callback_lock: lock for opaque user context
 * @user_data:	opaque user context
 * @rcv_nxt:	next receive seq. #
 * @copied_seq:	head of yet unread data
 * @rcv_wup:	rcv_nxt on last window update sent
 * @snd_nxt:	next sequence we send
 * @snd_una:	first byte we want an ack for
 * @write_seq:	tail+1 of data held in send buffer
 */
struct s3_conn {
	struct net_device *dev;
	struct t3cdev *cdev;
	unsigned long flags;
	int tid;
	int qset;
	int mss_idx;
	struct l2t_entry *l2t;
	int wr_max;
	int wr_avail;
	int wr_unacked;
	struct sk_buff *wr_pending_head;
	struct sk_buff *wr_pending_tail;
	struct sk_buff *cpl_close;
	struct sk_buff *cpl_abort_req;
	struct sk_buff *cpl_abort_rpl;
	spinlock_t lock;
	atomic_t refcnt;
	volatile unsigned int state;
	struct sockaddr_in saddr;
	struct sockaddr_in daddr;
	struct dst_entry *dst_cache;
	struct sk_buff_head receive_queue;
	struct sk_buff_head write_queue;
	struct timer_list retry_timer;
	int err;
	rwlock_t callback_lock;
	void *user_data;

	u32 rcv_nxt;
	u32 copied_seq;
	u32 rcv_wup;
	u32 snd_nxt;
	u32 snd_una;
	u32 write_seq;
};

/*
 * connection state
 */
enum conn_states {
	C3CN_STATE_CONNECTING = 1,
	C3CN_STATE_ESTABLISHED,
	C3CN_STATE_ACTIVE_CLOSE,
	C3CN_STATE_PASSIVE_CLOSE,
	C3CN_STATE_CLOSE_WAIT_1,
	C3CN_STATE_CLOSE_WAIT_2,
	C3CN_STATE_ABORTING,
	C3CN_STATE_CLOSED,
};

static inline unsigned int c3cn_is_closing(const struct s3_conn *c3cn)
{
	return c3cn->state >= C3CN_STATE_ACTIVE_CLOSE;
}
static inline unsigned int c3cn_is_established(const struct s3_conn *c3cn)
{
	return c3cn->state == C3CN_STATE_ESTABLISHED;
}

/*
 * Connection flags -- many to track some close related events.
 */
enum c3cn_flags {
	C3CN_ABORT_RPL_RCVD,	/* received one ABORT_RPL_RSS message */
	C3CN_ABORT_REQ_RCVD,	/* received one ABORT_REQ_RSS message */
	C3CN_ABORT_RPL_PENDING,	/* expecting an abort reply */
	C3CN_TX_DATA_SENT,	/* already sent a TX_DATA WR */
	C3CN_ACTIVE_CLOSE_NEEDED,	/* need to be closed */
};

/**
 * cxgb3i_sdev_data - Per adapter data.
 * Linked off of each Ethernet device port on the adapter.
 * Also available via the t3cdev structure since we have pointers to our port
 * net_device's there ...
 *
 * @list:	list head to link elements
 * @cdev:	t3cdev adapter
 * @client:	CPL client pointer
 * @ports:	array of adapter ports
 * @sport_map_next: next index into the port map
 * @sport_map:	source port map
 */
struct cxgb3i_sdev_data {
	struct list_head list;
	struct t3cdev *cdev;
	struct cxgb3_client *client;
	struct adap_ports ports;
	unsigned int sport_map_next;
	unsigned long sport_map[0];
};
#define NDEV2CDATA(ndev) (*(struct cxgb3i_sdev_data **)&(ndev)->ec_ptr)
#define CXGB3_SDEV_DATA(cdev) NDEV2CDATA((cdev)->lldev)

void cxgb3i_sdev_cleanup(void);
int cxgb3i_sdev_init(cxgb3_cpl_handler_func *);
void cxgb3i_sdev_add(struct t3cdev *, struct cxgb3_client *);
void cxgb3i_sdev_remove(struct t3cdev *);

struct s3_conn *cxgb3i_c3cn_create(void);
int cxgb3i_c3cn_connect(struct s3_conn *, struct sockaddr_in *);
void cxgb3i_c3cn_rx_credits(struct s3_conn *, int);
int cxgb3i_c3cn_send_pdus(struct s3_conn *, struct sk_buff *);
void cxgb3i_c3cn_release(struct s3_conn *);

/**
 * cxgb3_skb_cb - control block for received pdu state and ULP mode management.
 *
 * @flag:	see C3CB_FLAG_* below
 * @ulp_mode:	ULP mode/submode of sk_buff
 * @seq:	tcp sequence number
 */
struct cxgb3_skb_rx_cb {
	__u32 ddigest;			/* data digest */
	__u32 pdulen;			/* recovered pdu length */
};

struct cxgb3_skb_tx_cb {
	struct sk_buff *wr_next;	/* next wr */
};

struct cxgb3_skb_cb {
	__u8 flags;
	__u8 ulp_mode;
	__u32 seq;
	union {
		struct cxgb3_skb_rx_cb rx;
		struct cxgb3_skb_tx_cb tx;
	};
};

#define CXGB3_SKB_CB(skb)	((struct cxgb3_skb_cb *)&((skb)->cb[0]))
#define skb_flags(skb)		(CXGB3_SKB_CB(skb)->flags)
#define skb_ulp_mode(skb)	(CXGB3_SKB_CB(skb)->ulp_mode)
#define skb_tcp_seq(skb)	(CXGB3_SKB_CB(skb)->seq)
#define skb_rx_ddigest(skb)	(CXGB3_SKB_CB(skb)->rx.ddigest)
#define skb_rx_pdulen(skb)	(CXGB3_SKB_CB(skb)->rx.pdulen)
#define skb_tx_wr_next(skb)	(CXGB3_SKB_CB(skb)->tx.wr_next)

enum c3cb_flags {
	C3CB_FLAG_NEED_HDR = 1 << 0,	/* packet needs a TX_DATA_WR header */
	C3CB_FLAG_NO_APPEND = 1 << 1,	/* don't grow this skb */
	C3CB_FLAG_COMPL = 1 << 2,	/* request WR completion */
};

/**
 * sge_opaque_hdr -
 * Opaque version of structure the SGE stores at skb->head of TX_DATA packets
 * and for which we must reserve space.
 */
struct sge_opaque_hdr {
	void *dev;
	dma_addr_t addr[MAX_SKB_FRAGS + 1];
};

/* for TX: a skb must have a headroom of at least TX_HEADER_LEN bytes */
#define TX_HEADER_LEN \
		(sizeof(struct tx_data_wr) + sizeof(struct sge_opaque_hdr))
#define SKB_TX_HEADROOM		SKB_MAX_HEAD(TX_HEADER_LEN)

/*
 * get and set private ip for iscsi traffic
 */
#define cxgb3i_get_private_ipv4addr(ndev) \
	(((struct port_info *)(netdev_priv(ndev)))->iscsi_ipv4addr)
#define cxgb3i_set_private_ipv4addr(ndev, addr) \
	(((struct port_info *)(netdev_priv(ndev)))->iscsi_ipv4addr) = addr

/* max. connections per adapter */
#define CXGB3I_MAX_CONN		16384
#endif /* _CXGB3_OFFLOAD_H */
