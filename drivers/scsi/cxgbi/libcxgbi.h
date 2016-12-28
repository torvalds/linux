/*
 * libcxgbi.h: Chelsio common library for T3/T4 iSCSI driver.
 *
 * Copyright (c) 2010-2015 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 * Written by: Rakesh Ranjan (rranjan@chelsio.com)
 */

#ifndef	__LIBCXGBI_H__
#define	__LIBCXGBI_H__

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/scatterlist.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include <scsi/scsi_device.h>
#include <scsi/libiscsi_tcp.h>

#include <libcxgb_ppm.h>

enum cxgbi_dbg_flag {
	CXGBI_DBG_ISCSI,
	CXGBI_DBG_DDP,
	CXGBI_DBG_TOE,
	CXGBI_DBG_SOCK,

	CXGBI_DBG_PDU_TX,
	CXGBI_DBG_PDU_RX,
	CXGBI_DBG_DEV,
};

#define log_debug(level, fmt, ...)	\
	do {	\
		if (dbg_level & (level)) \
			pr_info(fmt, ##__VA_ARGS__); \
	} while (0)

#define pr_info_ipaddr(fmt_trail,					\
			addr1, addr2, args_trail...)			\
do {									\
	if (!((1 << CXGBI_DBG_SOCK) & dbg_level))			\
		break;							\
	pr_info("%pISpc - %pISpc, " fmt_trail,				\
		addr1, addr2, args_trail);				\
} while (0)

/* max. connections per adapter */
#define CXGBI_MAX_CONN		16384

/* always allocate rooms for AHS */
#define SKB_TX_ISCSI_PDU_HEADER_MAX	\
	(sizeof(struct iscsi_hdr) + ISCSI_MAX_AHS_SIZE)

#define	ISCSI_PDU_NONPAYLOAD_LEN	312 /* bhs(48) + ahs(256) + digest(8)*/

/*
 * align pdu size to multiple of 512 for better performance
 */
#define cxgbi_align_pdu_size(n) do { n = (n) & (~511); } while (0)

#define ULP2_MODE_ISCSI		2

#define ULP2_MAX_PKT_SIZE	16224
#define ULP2_MAX_PDU_PAYLOAD	\
	(ULP2_MAX_PKT_SIZE - ISCSI_PDU_NONPAYLOAD_LEN)

/*
 * For iscsi connections HW may inserts digest bytes into the pdu. Those digest
 * bytes are not sent by the host but are part of the TCP payload and therefore
 * consume TCP sequence space.
 */
static const unsigned int ulp2_extra_len[] = { 0, 4, 4, 8 };
static inline unsigned int cxgbi_ulp_extra_len(int submode)
{
	return ulp2_extra_len[submode & 3];
}

#define CPL_RX_DDP_STATUS_DDP_SHIFT	16 /* ddp'able */
#define CPL_RX_DDP_STATUS_PAD_SHIFT	19 /* pad error */
#define CPL_RX_DDP_STATUS_HCRC_SHIFT	20 /* hcrc error */
#define CPL_RX_DDP_STATUS_DCRC_SHIFT	21 /* dcrc error */

/*
 * sge_opaque_hdr -
 * Opaque version of structure the SGE stores at skb->head of TX_DATA packets
 * and for which we must reserve space.
 */
struct sge_opaque_hdr {
	void *dev;
	dma_addr_t addr[MAX_SKB_FRAGS + 1];
};

struct cxgbi_sock {
	struct cxgbi_device *cdev;

	int tid;
	int atid;
	unsigned long flags;
	unsigned int mtu;
	unsigned short rss_qid;
	unsigned short txq_idx;
	unsigned short advmss;
	unsigned int tx_chan;
	unsigned int rx_chan;
	unsigned int mss_idx;
	unsigned int smac_idx;
	unsigned char port_id;
	int wr_max_cred;
	int wr_cred;
	int wr_una_cred;
	unsigned char hcrc_len;
	unsigned char dcrc_len;

	void *l2t;
	struct sk_buff *wr_pending_head;
	struct sk_buff *wr_pending_tail;
	struct sk_buff *cpl_close;
	struct sk_buff *cpl_abort_req;
	struct sk_buff *cpl_abort_rpl;
	struct sk_buff *skb_ulp_lhdr;
	spinlock_t lock;
	struct kref refcnt;
	unsigned int state;
	unsigned int csk_family;
	union {
		struct sockaddr_in saddr;
		struct sockaddr_in6 saddr6;
	};
	union {
		struct sockaddr_in daddr;
		struct sockaddr_in6 daddr6;
	};
	struct dst_entry *dst;
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
	u32 snd_win;
	u32 rcv_win;
};

/*
 * connection states
 */
enum cxgbi_sock_states{
	CTP_CLOSED,
	CTP_CONNECTING,
	CTP_ACTIVE_OPEN,
	CTP_ESTABLISHED,
	CTP_ACTIVE_CLOSE,
	CTP_PASSIVE_CLOSE,
	CTP_CLOSE_WAIT_1,
	CTP_CLOSE_WAIT_2,
	CTP_ABORTING,
};

/*
 * Connection flags -- many to track some close related events.
 */
enum cxgbi_sock_flags {
	CTPF_ABORT_RPL_RCVD,	/*received one ABORT_RPL_RSS message */
	CTPF_ABORT_REQ_RCVD,	/*received one ABORT_REQ_RSS message */
	CTPF_ABORT_RPL_PENDING,	/* expecting an abort reply */
	CTPF_TX_DATA_SENT,	/* already sent a TX_DATA WR */
	CTPF_ACTIVE_CLOSE_NEEDED,/* need to be closed */
	CTPF_HAS_ATID,		/* reserved atid */
	CTPF_HAS_TID,		/* reserved hw tid */
	CTPF_OFFLOAD_DOWN,	/* offload function off */
};

struct cxgbi_skb_rx_cb {
	__u32 ddigest;
	__u32 pdulen;
};

struct cxgbi_skb_tx_cb {
	void *l2t;
	struct sk_buff *wr_next;
};

enum cxgbi_skcb_flags {
	SKCBF_TX_NEED_HDR,	/* packet needs a header */
	SKCBF_TX_MEM_WRITE,     /* memory write */
	SKCBF_TX_FLAG_COMPL,    /* wr completion flag */
	SKCBF_RX_COALESCED,	/* received whole pdu */
	SKCBF_RX_HDR,		/* received pdu header */
	SKCBF_RX_DATA,		/* received pdu payload */
	SKCBF_RX_STATUS,	/* received ddp status */
	SKCBF_RX_ISCSI_COMPL,   /* received iscsi completion */
	SKCBF_RX_DATA_DDPD,	/* pdu payload ddp'd */
	SKCBF_RX_HCRC_ERR,	/* header digest error */
	SKCBF_RX_DCRC_ERR,	/* data digest error */
	SKCBF_RX_PAD_ERR,	/* padding byte error */
};

struct cxgbi_skb_cb {
	unsigned char ulp_mode;
	unsigned long flags;
	unsigned int seq;
	union {
		struct cxgbi_skb_rx_cb rx;
		struct cxgbi_skb_tx_cb tx;
	};
};

#define CXGBI_SKB_CB(skb)	((struct cxgbi_skb_cb *)&((skb)->cb[0]))
#define cxgbi_skcb_flags(skb)		(CXGBI_SKB_CB(skb)->flags)
#define cxgbi_skcb_ulp_mode(skb)	(CXGBI_SKB_CB(skb)->ulp_mode)
#define cxgbi_skcb_tcp_seq(skb)		(CXGBI_SKB_CB(skb)->seq)
#define cxgbi_skcb_rx_ddigest(skb)	(CXGBI_SKB_CB(skb)->rx.ddigest)
#define cxgbi_skcb_rx_pdulen(skb)	(CXGBI_SKB_CB(skb)->rx.pdulen)
#define cxgbi_skcb_tx_wr_next(skb)	(CXGBI_SKB_CB(skb)->tx.wr_next)

static inline void cxgbi_skcb_set_flag(struct sk_buff *skb,
					enum cxgbi_skcb_flags flag)
{
	__set_bit(flag, &(cxgbi_skcb_flags(skb)));
}

static inline void cxgbi_skcb_clear_flag(struct sk_buff *skb,
					enum cxgbi_skcb_flags flag)
{
	__clear_bit(flag, &(cxgbi_skcb_flags(skb)));
}

static inline int cxgbi_skcb_test_flag(const struct sk_buff *skb,
				       enum cxgbi_skcb_flags flag)
{
	return test_bit(flag, &(cxgbi_skcb_flags(skb)));
}

static inline void cxgbi_sock_set_flag(struct cxgbi_sock *csk,
					enum cxgbi_sock_flags flag)
{
	__set_bit(flag, &csk->flags);
	log_debug(1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx, bit %d.\n",
		csk, csk->state, csk->flags, flag);
}

static inline void cxgbi_sock_clear_flag(struct cxgbi_sock *csk,
					enum cxgbi_sock_flags flag)
{
	__clear_bit(flag, &csk->flags);
	log_debug(1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx, bit %d.\n",
		csk, csk->state, csk->flags, flag);
}

static inline int cxgbi_sock_flag(struct cxgbi_sock *csk,
				enum cxgbi_sock_flags flag)
{
	if (csk == NULL)
		return 0;
	return test_bit(flag, &csk->flags);
}

static inline void cxgbi_sock_set_state(struct cxgbi_sock *csk, int state)
{
	log_debug(1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx, state -> %u.\n",
		csk, csk->state, csk->flags, state);
	csk->state = state;
}

static inline void cxgbi_sock_free(struct kref *kref)
{
	struct cxgbi_sock *csk = container_of(kref,
						struct cxgbi_sock,
						refcnt);
	if (csk) {
		log_debug(1 << CXGBI_DBG_SOCK,
			"free csk 0x%p, state %u, flags 0x%lx\n",
			csk, csk->state, csk->flags);
		kfree(csk);
	}
}

static inline void __cxgbi_sock_put(const char *fn, struct cxgbi_sock *csk)
{
	log_debug(1 << CXGBI_DBG_SOCK,
		"%s, put csk 0x%p, ref %u-1.\n",
		fn, csk, atomic_read(&csk->refcnt.refcount));
	kref_put(&csk->refcnt, cxgbi_sock_free);
}
#define cxgbi_sock_put(csk)	__cxgbi_sock_put(__func__, csk)

static inline void __cxgbi_sock_get(const char *fn, struct cxgbi_sock *csk)
{
	log_debug(1 << CXGBI_DBG_SOCK,
		"%s, get csk 0x%p, ref %u+1.\n",
		fn, csk, atomic_read(&csk->refcnt.refcount));
	kref_get(&csk->refcnt);
}
#define cxgbi_sock_get(csk)	__cxgbi_sock_get(__func__, csk)

static inline int cxgbi_sock_is_closing(struct cxgbi_sock *csk)
{
	return csk->state >= CTP_ACTIVE_CLOSE;
}

static inline int cxgbi_sock_is_established(struct cxgbi_sock *csk)
{
	return csk->state == CTP_ESTABLISHED;
}

static inline void cxgbi_sock_purge_write_queue(struct cxgbi_sock *csk)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(&csk->write_queue)))
		__kfree_skb(skb);
}

static inline unsigned int cxgbi_sock_compute_wscale(unsigned int win)
{
	unsigned int wscale = 0;

	while (wscale < 14 && (65535 << wscale) < win)
		wscale++;
	return wscale;
}

static inline struct sk_buff *alloc_wr(int wrlen, int dlen, gfp_t gfp)
{
	struct sk_buff *skb = alloc_skb(wrlen + dlen, gfp);

	if (skb) {
		__skb_put(skb, wrlen);
		memset(skb->head, 0, wrlen + dlen);
	} else
		pr_info("alloc cpl wr skb %u+%u, OOM.\n", wrlen, dlen);
	return skb;
}


/*
 * The number of WRs needed for an skb depends on the number of fragments
 * in the skb and whether it has any payload in its main body.  This maps the
 * length of the gather list represented by an skb into the # of necessary WRs.
 * The extra two fragments are for iscsi bhs and payload padding.
 */
#define SKB_WR_LIST_SIZE	 (MAX_SKB_FRAGS + 2)

static inline void cxgbi_sock_reset_wr_list(struct cxgbi_sock *csk)
{
	csk->wr_pending_head = csk->wr_pending_tail = NULL;
}

static inline void cxgbi_sock_enqueue_wr(struct cxgbi_sock *csk,
					  struct sk_buff *skb)
{
	cxgbi_skcb_tx_wr_next(skb) = NULL;
	/*
	 * We want to take an extra reference since both us and the driver
	 * need to free the packet before it's really freed. We know there's
	 * just one user currently so we use atomic_set rather than skb_get
	 * to avoid the atomic op.
	 */
	atomic_set(&skb->users, 2);

	if (!csk->wr_pending_head)
		csk->wr_pending_head = skb;
	else
		cxgbi_skcb_tx_wr_next(csk->wr_pending_tail) = skb;
	csk->wr_pending_tail = skb;
}

static inline int cxgbi_sock_count_pending_wrs(const struct cxgbi_sock *csk)
{
	int n = 0;
	const struct sk_buff *skb = csk->wr_pending_head;

	while (skb) {
		n += skb->csum;
		skb = cxgbi_skcb_tx_wr_next(skb);
	}
	return n;
}

static inline struct sk_buff *cxgbi_sock_peek_wr(const struct cxgbi_sock *csk)
{
	return csk->wr_pending_head;
}

static inline struct sk_buff *cxgbi_sock_dequeue_wr(struct cxgbi_sock *csk)
{
	struct sk_buff *skb = csk->wr_pending_head;

	if (likely(skb)) {
		csk->wr_pending_head = cxgbi_skcb_tx_wr_next(skb);
		cxgbi_skcb_tx_wr_next(skb) = NULL;
	}
	return skb;
}

void cxgbi_sock_check_wr_invariants(const struct cxgbi_sock *);
void cxgbi_sock_purge_wr_queue(struct cxgbi_sock *);
void cxgbi_sock_skb_entail(struct cxgbi_sock *, struct sk_buff *);
void cxgbi_sock_fail_act_open(struct cxgbi_sock *, int);
void cxgbi_sock_act_open_req_arp_failure(void *, struct sk_buff *);
void cxgbi_sock_closed(struct cxgbi_sock *);
void cxgbi_sock_established(struct cxgbi_sock *, unsigned int, unsigned int);
void cxgbi_sock_rcv_abort_rpl(struct cxgbi_sock *);
void cxgbi_sock_rcv_peer_close(struct cxgbi_sock *);
void cxgbi_sock_rcv_close_conn_rpl(struct cxgbi_sock *, u32);
void cxgbi_sock_rcv_wr_ack(struct cxgbi_sock *, unsigned int, unsigned int,
				int);
unsigned int cxgbi_sock_select_mss(struct cxgbi_sock *, unsigned int);
void cxgbi_sock_free_cpl_skbs(struct cxgbi_sock *);

struct cxgbi_hba {
	struct net_device *ndev;
	struct net_device *vdev;	/* vlan dev */
	struct Scsi_Host *shost;
	struct cxgbi_device *cdev;
	__be32 ipv4addr;
	unsigned char port_id;
};

struct cxgbi_ports_map {
	unsigned int max_connect;
	unsigned int used;
	unsigned short sport_base;
	spinlock_t lock;
	unsigned int next;
	struct cxgbi_sock **port_csk;
};

#define CXGBI_FLAG_DEV_T3		0x1
#define CXGBI_FLAG_DEV_T4		0x2
#define CXGBI_FLAG_ADAPTER_RESET	0x4
#define CXGBI_FLAG_IPV4_SET		0x10
#define CXGBI_FLAG_USE_PPOD_OFLDQ       0x40
#define CXGBI_FLAG_DDP_OFF		0x100

struct cxgbi_device {
	struct list_head list_head;
	struct list_head rcu_node;
	unsigned int flags;
	struct net_device **ports;
	void *lldev;
	struct cxgbi_hba **hbas;
	const unsigned short *mtus;
	unsigned char nmtus;
	unsigned char nports;
	struct pci_dev *pdev;
	struct dentry *debugfs_root;
	struct iscsi_transport *itp;
	struct module *owner;

	unsigned int pfvf;
	unsigned int rx_credit_thres;
	unsigned int skb_tx_rsvd;
	unsigned int skb_rx_extra;	/* for msg coalesced mode */
	unsigned int tx_max_size;
	unsigned int rx_max_size;
	struct cxgbi_ports_map pmap;

	void (*dev_ddp_cleanup)(struct cxgbi_device *);
	struct cxgbi_ppm* (*cdev2ppm)(struct cxgbi_device *);
	int (*csk_ddp_set_map)(struct cxgbi_ppm *, struct cxgbi_sock *,
			       struct cxgbi_task_tag_info *);
	void (*csk_ddp_clear_map)(struct cxgbi_device *cdev,
				  struct cxgbi_ppm *,
				  struct cxgbi_task_tag_info *);
	int (*csk_ddp_setup_digest)(struct cxgbi_sock *,
				unsigned int, int, int, int);
	int (*csk_ddp_setup_pgidx)(struct cxgbi_sock *,
				unsigned int, int, bool);

	void (*csk_release_offload_resources)(struct cxgbi_sock *);
	int (*csk_rx_pdu_ready)(struct cxgbi_sock *, struct sk_buff *);
	u32 (*csk_send_rx_credits)(struct cxgbi_sock *, u32);
	int (*csk_push_tx_frames)(struct cxgbi_sock *, int);
	void (*csk_send_abort_req)(struct cxgbi_sock *);
	void (*csk_send_close_req)(struct cxgbi_sock *);
	int (*csk_alloc_cpls)(struct cxgbi_sock *);
	int (*csk_init_act_open)(struct cxgbi_sock *);

	void *dd_data;
};
#define cxgbi_cdev_priv(cdev)	((cdev)->dd_data)

struct cxgbi_conn {
	struct cxgbi_endpoint *cep;
	struct iscsi_conn *iconn;
	struct cxgbi_hba *chba;
	u32 task_idx_bits;
	unsigned int ddp_full;
	unsigned int ddp_tag_full;
};

struct cxgbi_endpoint {
	struct cxgbi_conn *cconn;
	struct cxgbi_hba *chba;
	struct cxgbi_sock *csk;
};

#define MAX_PDU_FRAGS	((ULP2_MAX_PDU_PAYLOAD + 512 - 1) / 512)
struct cxgbi_task_data {
	unsigned short nr_frags;
	struct page_frag frags[MAX_PDU_FRAGS];
	struct sk_buff *skb;
	unsigned int dlen;
	unsigned int offset;
	unsigned int count;
	unsigned int sgoffset;
	struct cxgbi_task_tag_info ttinfo;
};
#define iscsi_task_cxgbi_data(task) \
	((task)->dd_data + sizeof(struct iscsi_tcp_task))

static inline void *cxgbi_alloc_big_mem(unsigned int size,
					gfp_t gfp)
{
	void *p = kzalloc(size, gfp | __GFP_NOWARN);

	if (!p)
		p = vzalloc(size);

	return p;
}

static inline void cxgbi_free_big_mem(void *addr)
{
	kvfree(addr);
}

static inline void cxgbi_set_iscsi_ipv4(struct cxgbi_hba *chba, __be32 ipaddr)
{
	if (chba->cdev->flags & CXGBI_FLAG_IPV4_SET)
		chba->ipv4addr = ipaddr;
	else
		pr_info("set iscsi ipv4 NOT supported, using %s ipv4.\n",
			chba->ndev->name);
}

struct cxgbi_device *cxgbi_device_register(unsigned int, unsigned int);
void cxgbi_device_unregister(struct cxgbi_device *);
void cxgbi_device_unregister_all(unsigned int flag);
struct cxgbi_device *cxgbi_device_find_by_lldev(void *);
struct cxgbi_device *cxgbi_device_find_by_netdev(struct net_device *, int *);
struct cxgbi_device *cxgbi_device_find_by_netdev_rcu(struct net_device *,
						     int *);
int cxgbi_hbas_add(struct cxgbi_device *, u64, unsigned int,
			struct scsi_host_template *,
			struct scsi_transport_template *);
void cxgbi_hbas_remove(struct cxgbi_device *);

int cxgbi_device_portmap_create(struct cxgbi_device *cdev, unsigned int base,
			unsigned int max_conn);
void cxgbi_device_portmap_cleanup(struct cxgbi_device *cdev);

void cxgbi_conn_tx_open(struct cxgbi_sock *);
void cxgbi_conn_pdu_ready(struct cxgbi_sock *);
int cxgbi_conn_alloc_pdu(struct iscsi_task *, u8);
int cxgbi_conn_init_pdu(struct iscsi_task *, unsigned int , unsigned int);
int cxgbi_conn_xmit_pdu(struct iscsi_task *);

void cxgbi_cleanup_task(struct iscsi_task *task);

umode_t cxgbi_attr_is_visible(int param_type, int param);
void cxgbi_get_conn_stats(struct iscsi_cls_conn *, struct iscsi_stats *);
int cxgbi_set_conn_param(struct iscsi_cls_conn *,
			enum iscsi_param, char *, int);
int cxgbi_get_ep_param(struct iscsi_endpoint *ep, enum iscsi_param, char *);
struct iscsi_cls_conn *cxgbi_create_conn(struct iscsi_cls_session *, u32);
int cxgbi_bind_conn(struct iscsi_cls_session *,
			struct iscsi_cls_conn *, u64, int);
void cxgbi_destroy_session(struct iscsi_cls_session *);
struct iscsi_cls_session *cxgbi_create_session(struct iscsi_endpoint *,
			u16, u16, u32);
int cxgbi_set_host_param(struct Scsi_Host *,
			enum iscsi_host_param, char *, int);
int cxgbi_get_host_param(struct Scsi_Host *, enum iscsi_host_param, char *);
struct iscsi_endpoint *cxgbi_ep_connect(struct Scsi_Host *,
			struct sockaddr *, int);
int cxgbi_ep_poll(struct iscsi_endpoint *, int);
void cxgbi_ep_disconnect(struct iscsi_endpoint *);

int cxgbi_iscsi_init(struct iscsi_transport *,
			struct scsi_transport_template **);
void cxgbi_iscsi_cleanup(struct iscsi_transport *,
			struct scsi_transport_template **);
void cxgbi_parse_pdu_itt(struct iscsi_conn *, itt_t, int *, int *);
int cxgbi_ddp_init(struct cxgbi_device *, unsigned int, unsigned int,
			unsigned int, unsigned int);
int cxgbi_ddp_cleanup(struct cxgbi_device *);
void cxgbi_ddp_page_size_factor(int *);
void cxgbi_ddp_set_one_ppod(struct cxgbi_pagepod *,
			    struct cxgbi_task_tag_info *,
			    struct scatterlist **sg_pp, unsigned int *sg_off);
void cxgbi_ddp_ppm_setup(void **ppm_pp, struct cxgbi_device *,
			 struct cxgbi_tag_format *, unsigned int ppmax,
			 unsigned int llimit, unsigned int start,
			 unsigned int rsvd_factor);
#endif	/*__LIBCXGBI_H__*/
