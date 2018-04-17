/*
 * Copyright (c) 2016 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __CXGBIT_H__
#define __CXGBIT_H__

#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/completion.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/inet.h>
#include <linux/wait.h>
#include <linux/kref.h>
#include <linux/timer.h>
#include <linux/io.h>

#include <asm/byteorder.h>

#include <net/net_namespace.h>

#include <target/iscsi/iscsi_transport.h>
#include <iscsi_target_parameters.h>
#include <iscsi_target_login.h>

#include "t4_regs.h"
#include "t4_msg.h"
#include "cxgb4.h"
#include "cxgb4_uld.h"
#include "l2t.h"
#include "libcxgb_ppm.h"
#include "cxgbit_lro.h"

extern struct mutex cdev_list_lock;
extern struct list_head cdev_list_head;
struct cxgbit_np;

struct cxgbit_sock;

struct cxgbit_cmd {
	struct scatterlist sg;
	struct cxgbi_task_tag_info ttinfo;
	bool setup_ddp;
	bool release;
};

#define CXGBIT_MAX_ISO_PAYLOAD	\
	min_t(u32, MAX_SKB_FRAGS * PAGE_SIZE, 65535)

struct cxgbit_iso_info {
	u8 flags;
	u32 mpdu;
	u32 len;
	u32 burst_len;
};

enum cxgbit_skcb_flags {
	SKCBF_TX_NEED_HDR	= (1 << 0), /* packet needs a header */
	SKCBF_TX_FLAG_COMPL	= (1 << 1), /* wr completion flag */
	SKCBF_TX_ISO		= (1 << 2), /* iso cpl in tx skb */
	SKCBF_RX_LRO		= (1 << 3), /* lro skb */
};

struct cxgbit_skb_rx_cb {
	u8 opcode;
	void *pdu_cb;
	void (*backlog_fn)(struct cxgbit_sock *, struct sk_buff *);
};

struct cxgbit_skb_tx_cb {
	u8 submode;
	u32 extra_len;
};

union cxgbit_skb_cb {
	struct {
		u8 flags;
		union {
			struct cxgbit_skb_tx_cb tx;
			struct cxgbit_skb_rx_cb rx;
		};
	};

	struct {
		/* This member must be first. */
		struct l2t_skb_cb l2t;
		struct sk_buff *wr_next;
	};
};

#define CXGBIT_SKB_CB(skb)	((union cxgbit_skb_cb *)&((skb)->cb[0]))
#define cxgbit_skcb_flags(skb)		(CXGBIT_SKB_CB(skb)->flags)
#define cxgbit_skcb_submode(skb)	(CXGBIT_SKB_CB(skb)->tx.submode)
#define cxgbit_skcb_tx_wr_next(skb)	(CXGBIT_SKB_CB(skb)->wr_next)
#define cxgbit_skcb_tx_extralen(skb)	(CXGBIT_SKB_CB(skb)->tx.extra_len)
#define cxgbit_skcb_rx_opcode(skb)	(CXGBIT_SKB_CB(skb)->rx.opcode)
#define cxgbit_skcb_rx_backlog_fn(skb)	(CXGBIT_SKB_CB(skb)->rx.backlog_fn)
#define cxgbit_rx_pdu_cb(skb)		(CXGBIT_SKB_CB(skb)->rx.pdu_cb)

static inline void *cplhdr(struct sk_buff *skb)
{
	return skb->data;
}

enum cxgbit_cdev_flags {
	CDEV_STATE_UP = 0,
	CDEV_ISO_ENABLE,
	CDEV_DDP_ENABLE,
};

#define NP_INFO_HASH_SIZE 32

struct np_info {
	struct np_info *next;
	struct cxgbit_np *cnp;
	unsigned int stid;
};

struct cxgbit_list_head {
	struct list_head list;
	/* device lock */
	spinlock_t lock;
};

struct cxgbit_device {
	struct list_head list;
	struct cxgb4_lld_info lldi;
	struct np_info *np_hash_tab[NP_INFO_HASH_SIZE];
	/* np lock */
	spinlock_t np_lock;
	u8 selectq[MAX_NPORTS][2];
	struct cxgbit_list_head cskq;
	u32 mdsl;
	struct kref kref;
	unsigned long flags;
};

struct cxgbit_wr_wait {
	struct completion completion;
	int ret;
};

enum cxgbit_csk_state {
	CSK_STATE_IDLE = 0,
	CSK_STATE_LISTEN,
	CSK_STATE_CONNECTING,
	CSK_STATE_ESTABLISHED,
	CSK_STATE_ABORTING,
	CSK_STATE_CLOSING,
	CSK_STATE_MORIBUND,
	CSK_STATE_DEAD,
};

enum cxgbit_csk_flags {
	CSK_TX_DATA_SENT = 0,
	CSK_LOGIN_PDU_DONE,
	CSK_LOGIN_DONE,
	CSK_DDP_ENABLE,
	CSK_ABORT_RPL_WAIT,
};

struct cxgbit_sock_common {
	struct cxgbit_device *cdev;
	struct sockaddr_storage local_addr;
	struct sockaddr_storage remote_addr;
	struct cxgbit_wr_wait wr_wait;
	enum cxgbit_csk_state state;
	unsigned long flags;
};

struct cxgbit_np {
	struct cxgbit_sock_common com;
	wait_queue_head_t accept_wait;
	struct iscsi_np *np;
	struct completion accept_comp;
	struct list_head np_accept_list;
	/* np accept lock */
	spinlock_t np_accept_lock;
	struct kref kref;
	unsigned int stid;
};

struct cxgbit_sock {
	struct cxgbit_sock_common com;
	struct cxgbit_np *cnp;
	struct iscsi_conn *conn;
	struct l2t_entry *l2t;
	struct dst_entry *dst;
	struct list_head list;
	struct sk_buff_head rxq;
	struct sk_buff_head txq;
	struct sk_buff_head ppodq;
	struct sk_buff_head backlogq;
	struct sk_buff_head skbq;
	struct sk_buff *wr_pending_head;
	struct sk_buff *wr_pending_tail;
	struct sk_buff *skb;
	struct sk_buff *lro_skb;
	struct sk_buff *lro_hskb;
	struct list_head accept_node;
	/* socket lock */
	spinlock_t lock;
	wait_queue_head_t waitq;
	wait_queue_head_t ack_waitq;
	bool lock_owner;
	struct kref kref;
	u32 max_iso_npdu;
	u32 wr_cred;
	u32 wr_una_cred;
	u32 wr_max_cred;
	u32 snd_una;
	u32 tid;
	u32 snd_nxt;
	u32 rcv_nxt;
	u32 smac_idx;
	u32 tx_chan;
	u32 mtu;
	u32 write_seq;
	u32 rx_credits;
	u32 snd_win;
	u32 rcv_win;
	u16 mss;
	u16 emss;
	u16 plen;
	u16 rss_qid;
	u16 txq_idx;
	u16 ctrlq_idx;
	u8 tos;
	u8 port_id;
#define CXGBIT_SUBMODE_HCRC 0x1
#define CXGBIT_SUBMODE_DCRC 0x2
	u8 submode;
#ifdef CONFIG_CHELSIO_T4_DCB
	u8 dcb_priority;
#endif
	u8 snd_wscale;
};

void _cxgbit_free_cdev(struct kref *kref);
void _cxgbit_free_csk(struct kref *kref);
void _cxgbit_free_cnp(struct kref *kref);

static inline void cxgbit_get_cdev(struct cxgbit_device *cdev)
{
	kref_get(&cdev->kref);
}

static inline void cxgbit_put_cdev(struct cxgbit_device *cdev)
{
	kref_put(&cdev->kref, _cxgbit_free_cdev);
}

static inline void cxgbit_get_csk(struct cxgbit_sock *csk)
{
	kref_get(&csk->kref);
}

static inline void cxgbit_put_csk(struct cxgbit_sock *csk)
{
	kref_put(&csk->kref, _cxgbit_free_csk);
}

static inline void cxgbit_get_cnp(struct cxgbit_np *cnp)
{
	kref_get(&cnp->kref);
}

static inline void cxgbit_put_cnp(struct cxgbit_np *cnp)
{
	kref_put(&cnp->kref, _cxgbit_free_cnp);
}

static inline void cxgbit_sock_reset_wr_list(struct cxgbit_sock *csk)
{
	csk->wr_pending_tail = NULL;
	csk->wr_pending_head = NULL;
}

static inline struct sk_buff *cxgbit_sock_peek_wr(const struct cxgbit_sock *csk)
{
	return csk->wr_pending_head;
}

static inline void
cxgbit_sock_enqueue_wr(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	cxgbit_skcb_tx_wr_next(skb) = NULL;

	skb_get(skb);

	if (!csk->wr_pending_head)
		csk->wr_pending_head = skb;
	else
		cxgbit_skcb_tx_wr_next(csk->wr_pending_tail) = skb;
	csk->wr_pending_tail = skb;
}

static inline struct sk_buff *cxgbit_sock_dequeue_wr(struct cxgbit_sock *csk)
{
	struct sk_buff *skb = csk->wr_pending_head;

	if (likely(skb)) {
		csk->wr_pending_head = cxgbit_skcb_tx_wr_next(skb);
		cxgbit_skcb_tx_wr_next(skb) = NULL;
	}
	return skb;
}

typedef void (*cxgbit_cplhandler_func)(struct cxgbit_device *,
				       struct sk_buff *);

int cxgbit_setup_np(struct iscsi_np *, struct sockaddr_storage *);
int cxgbit_setup_conn_digest(struct cxgbit_sock *);
int cxgbit_accept_np(struct iscsi_np *, struct iscsi_conn *);
void cxgbit_free_np(struct iscsi_np *);
void cxgbit_abort_conn(struct cxgbit_sock *csk);
void cxgbit_free_conn(struct iscsi_conn *);
extern cxgbit_cplhandler_func cxgbit_cplhandlers[NUM_CPL_CMDS];
int cxgbit_get_login_rx(struct iscsi_conn *, struct iscsi_login *);
int cxgbit_rx_data_ack(struct cxgbit_sock *);
int cxgbit_l2t_send(struct cxgbit_device *, struct sk_buff *,
		    struct l2t_entry *);
void cxgbit_push_tx_frames(struct cxgbit_sock *);
int cxgbit_put_login_tx(struct iscsi_conn *, struct iscsi_login *, u32);
int cxgbit_xmit_pdu(struct iscsi_conn *, struct iscsi_cmd *,
		    struct iscsi_datain_req *, const void *, u32);
void cxgbit_get_r2t_ttt(struct iscsi_conn *, struct iscsi_cmd *,
			struct iscsi_r2t *);
u32 cxgbit_send_tx_flowc_wr(struct cxgbit_sock *);
int cxgbit_ofld_send(struct cxgbit_device *, struct sk_buff *);
void cxgbit_get_rx_pdu(struct iscsi_conn *);
int cxgbit_validate_params(struct iscsi_conn *);
struct cxgbit_device *cxgbit_find_device(struct net_device *, u8 *);

/* DDP */
int cxgbit_ddp_init(struct cxgbit_device *);
int cxgbit_setup_conn_pgidx(struct cxgbit_sock *, u32);
int cxgbit_reserve_ttt(struct cxgbit_sock *, struct iscsi_cmd *);
void cxgbit_release_cmd(struct iscsi_conn *, struct iscsi_cmd *);

static inline
struct cxgbi_ppm *cdev2ppm(struct cxgbit_device *cdev)
{
	return (struct cxgbi_ppm *)(*cdev->lldi.iscsi_ppm);
}
#endif /* __CXGBIT_H__ */
