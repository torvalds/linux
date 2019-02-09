/*
 * Copyright (c) 2018 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __CHTLS_H__
#define __CHTLS_H__

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <crypto/authenc.h>
#include <crypto/ctr.h>
#include <crypto/gf128mul.h>
#include <crypto/internal/aead.h>
#include <crypto/null.h>
#include <crypto/internal/skcipher.h>
#include <crypto/aead.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/hash.h>
#include <linux/tls.h>
#include <net/tls.h>

#include "t4fw_api.h"
#include "t4_msg.h"
#include "cxgb4.h"
#include "cxgb4_uld.h"
#include "l2t.h"
#include "chcr_algo.h"
#include "chcr_core.h"
#include "chcr_crypto.h"

#define MAX_IVS_PAGE			256
#define TLS_KEY_CONTEXT_SZ		64
#define CIPHER_BLOCK_SIZE		16
#define GCM_TAG_SIZE			16
#define KEY_ON_MEM_SZ			16
#define AEAD_EXPLICIT_DATA_SIZE		8
#define TLS_HEADER_LENGTH		5
#define SCMD_CIPH_MODE_AES_GCM		2
/* Any MFS size should work and come from openssl */
#define TLS_MFS				16384

#define RSS_HDR sizeof(struct rss_header)
#define TLS_WR_CPL_LEN \
	(sizeof(struct fw_tlstx_data_wr) + sizeof(struct cpl_tx_tls_sfo))

enum {
	CHTLS_KEY_CONTEXT_DSGL,
	CHTLS_KEY_CONTEXT_IMM,
	CHTLS_KEY_CONTEXT_DDR,
};

enum {
	CHTLS_LISTEN_START,
	CHTLS_LISTEN_STOP,
};

/* Flags for return value of CPL message handlers */
enum {
	CPL_RET_BUF_DONE =    1,   /* buffer processing done */
	CPL_RET_BAD_MSG =     2,   /* bad CPL message */
	CPL_RET_UNKNOWN_TID = 4    /* unexpected unknown TID */
};

#define LISTEN_INFO_HASH_SIZE 32
#define RSPQ_HASH_BITS 5
struct listen_info {
	struct listen_info *next;  /* Link to next entry */
	struct sock *sk;           /* The listening socket */
	unsigned int stid;         /* The server TID */
};

enum {
	T4_LISTEN_START_PENDING,
	T4_LISTEN_STARTED
};

enum csk_flags {
	CSK_CALLBACKS_CHKD,	/* socket callbacks have been sanitized */
	CSK_ABORT_REQ_RCVD,	/* received one ABORT_REQ_RSS message */
	CSK_TX_MORE_DATA,	/* sending ULP data; don't set SHOVE bit */
	CSK_TX_WAIT_IDLE,	/* suspend Tx until in-flight data is ACKed */
	CSK_ABORT_SHUTDOWN,	/* shouldn't send more abort requests */
	CSK_ABORT_RPL_PENDING,	/* expecting an abort reply */
	CSK_CLOSE_CON_REQUESTED,/* we've sent a close_conn_req */
	CSK_TX_DATA_SENT,	/* sent a TX_DATA WR on this connection */
	CSK_TX_FAILOVER,	/* Tx traffic failing over */
	CSK_UPDATE_RCV_WND,	/* Need to update rcv window */
	CSK_RST_ABORTED,	/* outgoing RST was aborted */
	CSK_TLS_HANDSHK,	/* TLS Handshake */
	CSK_CONN_INLINE,	/* Connection on HW */
};

enum chtls_cdev_state {
	CHTLS_CDEV_STATE_UP = 1
};

struct listen_ctx {
	struct sock *lsk;
	struct chtls_dev *cdev;
	struct sk_buff_head synq;
	u32 state;
};

struct key_map {
	unsigned long *addr;
	unsigned int start;
	unsigned int available;
	unsigned int size;
	spinlock_t lock; /* lock for key id request from map */
} __packed;

struct tls_scmd {
	u32 seqno_numivs;
	u32 ivgen_hdrlen;
};

struct chtls_dev {
	struct tls_device tlsdev;
	struct list_head list;
	struct cxgb4_lld_info *lldi;
	struct pci_dev *pdev;
	struct listen_info *listen_hash_tab[LISTEN_INFO_HASH_SIZE];
	spinlock_t listen_lock; /* lock for listen list */
	struct net_device **ports;
	struct tid_info *tids;
	unsigned int pfvf;
	const unsigned short *mtus;

	struct idr hwtid_idr;
	struct idr stid_idr;

	spinlock_t idr_lock ____cacheline_aligned_in_smp;

	struct net_device *egr_dev[NCHAN * 2];
	struct sk_buff *rspq_skb_cache[1 << RSPQ_HASH_BITS];
	struct sk_buff *askb;

	struct sk_buff_head deferq;
	struct work_struct deferq_task;

	struct list_head list_node;
	struct list_head rcu_node;
	struct list_head na_node;
	unsigned int send_page_order;
	int max_host_sndbuf;
	struct key_map kmap;
	unsigned int cdev_state;
};

struct chtls_hws {
	struct sk_buff_head sk_recv_queue;
	u8 txqid;
	u8 ofld;
	u16 type;
	u16 rstate;
	u16 keyrpl;
	u16 pldlen;
	u16 rcvpld;
	u16 compute;
	u16 expansion;
	u16 keylen;
	u16 pdus;
	u16 adjustlen;
	u16 ivsize;
	u16 txleft;
	u32 mfs;
	s32 txkey;
	s32 rxkey;
	u32 fcplenmax;
	u32 copied_seq;
	u64 tx_seq_no;
	struct tls_scmd scmd;
	struct tls12_crypto_info_aes_gcm_128 crypto_info;
};

struct chtls_sock {
	struct sock *sk;
	struct chtls_dev *cdev;
	struct l2t_entry *l2t_entry;    /* pointer to the L2T entry */
	struct net_device *egress_dev;  /* TX_CHAN for act open retry */

	struct sk_buff_head txq;
	struct sk_buff *wr_skb_head;
	struct sk_buff *wr_skb_tail;
	struct sk_buff *ctrl_skb_cache;
	struct sk_buff *txdata_skb_cache; /* abort path messages */
	struct kref kref;
	unsigned long flags;
	u32 opt2;
	u32 wr_credits;
	u32 wr_unacked;
	u32 wr_max_credits;
	u32 wr_nondata;
	u32 hwtid;               /* TCP Control Block ID */
	u32 txq_idx;
	u32 rss_qid;
	u32 tid;
	u32 idr;
	u32 mss;
	u32 ulp_mode;
	u32 tx_chan;
	u32 rx_chan;
	u32 sndbuf;
	u32 txplen_max;
	u32 mtu_idx;           /* MTU table index */
	u32 smac_idx;
	u8 port_id;
	u8 tos;
	u16 resv2;
	u32 delack_mode;
	u32 delack_seq;

	void *passive_reap_next;        /* placeholder for passive */
	struct chtls_hws tlshws;
	struct synq {
		struct sk_buff *next;
		struct sk_buff *prev;
	} synq;
	struct listen_ctx *listen_ctx;
};

struct tls_hdr {
	u8  type;
	u16 version;
	u16 length;
} __packed;

struct tlsrx_cmp_hdr {
	u8  type;
	u16 version;
	u16 length;

	u64 tls_seq;
	u16 reserved1;
	u8  res_to_mac_error;
} __packed;

/* res_to_mac_error fields */
#define TLSRX_HDR_PKT_INT_ERROR_S   4
#define TLSRX_HDR_PKT_INT_ERROR_M   0x1
#define TLSRX_HDR_PKT_INT_ERROR_V(x) \
	((x) << TLSRX_HDR_PKT_INT_ERROR_S)
#define TLSRX_HDR_PKT_INT_ERROR_G(x) \
	(((x) >> TLSRX_HDR_PKT_INT_ERROR_S) & TLSRX_HDR_PKT_INT_ERROR_M)
#define TLSRX_HDR_PKT_INT_ERROR_F   TLSRX_HDR_PKT_INT_ERROR_V(1U)

#define TLSRX_HDR_PKT_SPP_ERROR_S        3
#define TLSRX_HDR_PKT_SPP_ERROR_M        0x1
#define TLSRX_HDR_PKT_SPP_ERROR_V(x)     ((x) << TLSRX_HDR_PKT_SPP_ERROR)
#define TLSRX_HDR_PKT_SPP_ERROR_G(x)     \
	(((x) >> TLSRX_HDR_PKT_SPP_ERROR_S) & TLSRX_HDR_PKT_SPP_ERROR_M)
#define TLSRX_HDR_PKT_SPP_ERROR_F        TLSRX_HDR_PKT_SPP_ERROR_V(1U)

#define TLSRX_HDR_PKT_CCDX_ERROR_S       2
#define TLSRX_HDR_PKT_CCDX_ERROR_M       0x1
#define TLSRX_HDR_PKT_CCDX_ERROR_V(x)    ((x) << TLSRX_HDR_PKT_CCDX_ERROR_S)
#define TLSRX_HDR_PKT_CCDX_ERROR_G(x)    \
	(((x) >> TLSRX_HDR_PKT_CCDX_ERROR_S) & TLSRX_HDR_PKT_CCDX_ERROR_M)
#define TLSRX_HDR_PKT_CCDX_ERROR_F       TLSRX_HDR_PKT_CCDX_ERROR_V(1U)

#define TLSRX_HDR_PKT_PAD_ERROR_S        1
#define TLSRX_HDR_PKT_PAD_ERROR_M        0x1
#define TLSRX_HDR_PKT_PAD_ERROR_V(x)     ((x) << TLSRX_HDR_PKT_PAD_ERROR_S)
#define TLSRX_HDR_PKT_PAD_ERROR_G(x)     \
	(((x) >> TLSRX_HDR_PKT_PAD_ERROR_S) & TLSRX_HDR_PKT_PAD_ERROR_M)
#define TLSRX_HDR_PKT_PAD_ERROR_F        TLSRX_HDR_PKT_PAD_ERROR_V(1U)

#define TLSRX_HDR_PKT_MAC_ERROR_S        0
#define TLSRX_HDR_PKT_MAC_ERROR_M        0x1
#define TLSRX_HDR_PKT_MAC_ERROR_V(x)     ((x) << TLSRX_HDR_PKT_MAC_ERROR)
#define TLSRX_HDR_PKT_MAC_ERROR_G(x)     \
	(((x) >> S_TLSRX_HDR_PKT_MAC_ERROR_S) & TLSRX_HDR_PKT_MAC_ERROR_M)
#define TLSRX_HDR_PKT_MAC_ERROR_F        TLSRX_HDR_PKT_MAC_ERROR_V(1U)

#define TLSRX_HDR_PKT_ERROR_M           0x1F
#define CONTENT_TYPE_ERROR		0x7F

struct ulp_mem_rw {
	__be32 cmd;
	__be32 len16;             /* command length */
	__be32 dlen;              /* data length in 32-byte units */
	__be32 lock_addr;
};

struct tls_key_wr {
	__be32 op_to_compl;
	__be32 flowid_len16;
	__be32 ftid;
	u8   reneg_to_write_rx;
	u8   protocol;
	__be16 mfs;
};

struct tls_key_req {
	struct tls_key_wr wr;
	struct ulp_mem_rw req;
	struct ulptx_idata sc_imm;
};

/*
 * This lives in skb->cb and is used to chain WRs in a linked list.
 */
struct wr_skb_cb {
	struct l2t_skb_cb l2t;          /* reserve space for l2t CB */
	struct sk_buff *next_wr;        /* next write request */
};

/* Per-skb backlog handler.  Run when a socket's backlog is processed. */
struct blog_skb_cb {
	void (*backlog_rcv)(struct sock *sk, struct sk_buff *skb);
	struct chtls_dev *cdev;
};

/*
 * Similar to tcp_skb_cb but with ULP elements added to support TLS,
 * etc.
 */
struct ulp_skb_cb {
	struct wr_skb_cb wr;		/* reserve space for write request */
	u16 flags;			/* TCP-like flags */
	u8 psh;
	u8 ulp_mode;			/* ULP mode/submode of sk_buff */
	u32 seq;			/* TCP sequence number */
	union { /* ULP-specific fields */
		struct {
			u8  type;
			u8  ofld;
			u8  iv;
		} tls;
	} ulp;
};

#define ULP_SKB_CB(skb) ((struct ulp_skb_cb *)&((skb)->cb[0]))
#define BLOG_SKB_CB(skb) ((struct blog_skb_cb *)(skb)->cb)

/*
 * Flags for ulp_skb_cb.flags.
 */
enum {
	ULPCB_FLAG_NEED_HDR  = 1 << 0,	/* packet needs a TX_DATA_WR header */
	ULPCB_FLAG_NO_APPEND = 1 << 1,	/* don't grow this skb */
	ULPCB_FLAG_BARRIER   = 1 << 2,	/* set TX_WAIT_IDLE after sending */
	ULPCB_FLAG_HOLD      = 1 << 3,	/* skb not ready for Tx yet */
	ULPCB_FLAG_COMPL     = 1 << 4,	/* request WR completion */
	ULPCB_FLAG_URG       = 1 << 5,	/* urgent data */
	ULPCB_FLAG_TLS_HDR   = 1 << 6,  /* payload with tls hdr */
	ULPCB_FLAG_NO_HDR    = 1 << 7,  /* not a ofld wr */
};

/* The ULP mode/submode of an skbuff */
#define skb_ulp_mode(skb)  (ULP_SKB_CB(skb)->ulp_mode)
#define TCP_PAGE(sk)   (sk->sk_frag.page)
#define TCP_OFF(sk)    (sk->sk_frag.offset)

static inline struct chtls_dev *to_chtls_dev(struct tls_device *tlsdev)
{
	return container_of(tlsdev, struct chtls_dev, tlsdev);
}

static inline void csk_set_flag(struct chtls_sock *csk,
				enum csk_flags flag)
{
	__set_bit(flag, &csk->flags);
}

static inline void csk_reset_flag(struct chtls_sock *csk,
				  enum csk_flags flag)
{
	__clear_bit(flag, &csk->flags);
}

static inline bool csk_conn_inline(const struct chtls_sock *csk)
{
	return test_bit(CSK_CONN_INLINE, &csk->flags);
}

static inline int csk_flag(const struct sock *sk, enum csk_flags flag)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);

	if (!csk_conn_inline(csk))
		return 0;
	return test_bit(flag, &csk->flags);
}

static inline int csk_flag_nochk(const struct chtls_sock *csk,
				 enum csk_flags flag)
{
	return test_bit(flag, &csk->flags);
}

static inline void *cplhdr(struct sk_buff *skb)
{
	return skb->data;
}

static inline int is_neg_adv(unsigned int status)
{
	return status == CPL_ERR_RTX_NEG_ADVICE ||
	       status == CPL_ERR_KEEPALV_NEG_ADVICE ||
	       status == CPL_ERR_PERSIST_NEG_ADVICE;
}

static inline void process_cpl_msg(void (*fn)(struct sock *, struct sk_buff *),
				   struct sock *sk,
				   struct sk_buff *skb)
{
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);

	bh_lock_sock(sk);
	if (unlikely(sock_owned_by_user(sk))) {
		BLOG_SKB_CB(skb)->backlog_rcv = fn;
		__sk_add_backlog(sk, skb);
	} else {
		fn(sk, skb);
	}
	bh_unlock_sock(sk);
}

static inline void chtls_sock_free(struct kref *ref)
{
	struct chtls_sock *csk = container_of(ref, struct chtls_sock,
					      kref);
	kfree(csk);
}

static inline void __chtls_sock_put(const char *fn, struct chtls_sock *csk)
{
	kref_put(&csk->kref, chtls_sock_free);
}

static inline void __chtls_sock_get(const char *fn,
				    struct chtls_sock *csk)
{
	kref_get(&csk->kref);
}

static inline void send_or_defer(struct sock *sk, struct tcp_sock *tp,
				 struct sk_buff *skb, int through_l2t)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);

	if (through_l2t) {
		/* send through L2T */
		cxgb4_l2t_send(csk->egress_dev, skb, csk->l2t_entry);
	} else {
		/* send directly */
		cxgb4_ofld_send(csk->egress_dev, skb);
	}
}

typedef int (*chtls_handler_func)(struct chtls_dev *, struct sk_buff *);
extern chtls_handler_func chtls_handlers[NUM_CPL_CMDS];
void chtls_install_cpl_ops(struct sock *sk);
int chtls_init_kmap(struct chtls_dev *cdev, struct cxgb4_lld_info *lldi);
void chtls_listen_stop(struct chtls_dev *cdev, struct sock *sk);
int chtls_listen_start(struct chtls_dev *cdev, struct sock *sk);
void chtls_close(struct sock *sk, long timeout);
int chtls_disconnect(struct sock *sk, int flags);
void chtls_shutdown(struct sock *sk, int how);
void chtls_destroy_sock(struct sock *sk);
int chtls_sendmsg(struct sock *sk, struct msghdr *msg, size_t size);
int chtls_recvmsg(struct sock *sk, struct msghdr *msg,
		  size_t len, int nonblock, int flags, int *addr_len);
int chtls_sendpage(struct sock *sk, struct page *page,
		   int offset, size_t size, int flags);
int send_tx_flowc_wr(struct sock *sk, int compl,
		     u32 snd_nxt, u32 rcv_nxt);
void chtls_tcp_push(struct sock *sk, int flags);
int chtls_push_frames(struct chtls_sock *csk, int comp);
int chtls_set_tcb_tflag(struct sock *sk, unsigned int bit_pos, int val);
int chtls_setkey(struct chtls_sock *csk, u32 keylen, u32 mode);
void skb_entail(struct sock *sk, struct sk_buff *skb, int flags);
unsigned int keyid_to_addr(int start_addr, int keyid);
void free_tls_keyid(struct sock *sk);
#endif
