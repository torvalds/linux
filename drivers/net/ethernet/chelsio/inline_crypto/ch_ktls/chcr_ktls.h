/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2020 Chelsio Communications.  All rights reserved. */

#ifndef __CHCR_KTLS_H__
#define __CHCR_KTLS_H__

#include "cxgb4.h"
#include "t4_msg.h"
#include "t4_tcb.h"
#include "l2t.h"
#include "chcr_common.h"
#include "cxgb4_uld.h"
#include "clip_tbl.h"

#define CHCR_KTLS_DRV_MODULE_NAME "ch_ktls"
#define CHCR_KTLS_DRV_VERSION "1.0.0.0-ko"
#define CHCR_KTLS_DRV_DESC "Chelsio NIC TLS ULD Driver"

#define CHCR_TCB_STATE_CLOSED	0
#define CHCR_KTLS_KEY_CTX_LEN	16
#define CHCR_SET_TCB_FIELD_LEN	sizeof(struct cpl_set_tcb_field)
#define CHCR_PLAIN_TX_DATA_LEN	(sizeof(struct fw_ulptx_wr) +\
				 sizeof(struct ulp_txpkt) +\
				 sizeof(struct ulptx_idata) +\
				 sizeof(struct cpl_tx_data))

#define CHCR_KTLS_WR_SIZE	(CHCR_PLAIN_TX_DATA_LEN +\
				 sizeof(struct cpl_tx_sec_pdu))
#define FALLBACK		35

enum ch_ktls_open_state {
	CH_KTLS_OPEN_SUCCESS = 0,
	CH_KTLS_OPEN_PENDING = 1,
	CH_KTLS_OPEN_FAILURE = 2,
};

struct chcr_ktls_info {
	struct sock *sk;
	spinlock_t lock; /* lock for pending_close */
	struct ktls_key_ctx key_ctx;
	struct adapter *adap;
	struct l2t_entry *l2te;
	struct net_device *netdev;
	struct completion completion;
	u64 iv;
	u64 record_no;
	int tid;
	int atid;
	int rx_qid;
	u32 iv_size;
	u32 prev_seq;
	u32 prev_ack;
	u32 salt_size;
	u32 key_ctx_len;
	u32 scmd0_seqno_numivs;
	u32 scmd0_ivgen_hdrlen;
	u32 tcp_start_seq_number;
	u32 scmd0_short_seqno_numivs;
	u32 scmd0_short_ivgen_hdrlen;
	u16 prev_win;
	u8 tx_chan;
	u8 smt_idx;
	u8 port_id;
	u8 ip_family;
	u8 first_qset;
	enum ch_ktls_open_state open_state;
	bool pending_close;
};

struct chcr_ktls_ofld_ctx_tx {
	struct tls_offload_context_tx base;
	struct chcr_ktls_info *chcr_info;
};

struct chcr_ktls_uld_ctx {
	struct list_head entry;
	struct cxgb4_lld_info lldi;
};

static inline struct chcr_ktls_ofld_ctx_tx *
chcr_get_ktls_tx_context(struct tls_context *tls_ctx)
{
	BUILD_BUG_ON(sizeof(struct chcr_ktls_ofld_ctx_tx) >
		     TLS_OFFLOAD_CONTEXT_SIZE_TX);
	return container_of(tls_offload_ctx_tx(tls_ctx),
			    struct chcr_ktls_ofld_ctx_tx,
			    base);
}

static inline int chcr_get_first_rx_qid(struct adapter *adap)
{
	/* u_ctx is saved in adap, fetch it */
	struct chcr_ktls_uld_ctx *u_ctx = adap->uld[CXGB4_ULD_KTLS].handle;

	if (!u_ctx)
		return -1;
	return u_ctx->lldi.rxq_ids[0];
}

typedef int (*chcr_handler_func)(struct adapter *adap, unsigned char *input);
#endif /* __CHCR_KTLS_H__ */
