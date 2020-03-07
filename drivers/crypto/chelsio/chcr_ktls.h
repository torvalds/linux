/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2020 Chelsio Communications.  All rights reserved. */

#ifndef __CHCR_KTLS_H__
#define __CHCR_KTLS_H__

#ifdef CONFIG_CHELSIO_TLS_DEVICE
#include <net/tls.h>
#include "cxgb4.h"
#include "t4_msg.h"
#include "t4_tcb.h"
#include "l2t.h"
#include "chcr_common.h"

#define CHCR_TCB_STATE_CLOSED	0

enum chcr_ktls_conn_state {
	KTLS_CONN_CLOSED,
};

struct chcr_ktls_info {
	struct sock *sk;
	spinlock_t lock; /* state machine lock */
	struct adapter *adap;
	struct l2t_entry *l2te;
	struct net_device *netdev;
	int tid;
	int atid;
	int rx_qid;
	u32 prev_seq;
	u32 tcp_start_seq_number;
	enum chcr_ktls_conn_state connection_state;
	u8 tx_chan;
	u8 smt_idx;
	u8 port_id;
	u8 ip_family;
};

struct chcr_ktls_ofld_ctx_tx {
	struct tls_offload_context_tx base;
	struct chcr_ktls_info *chcr_info;
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
	struct uld_ctx *u_ctx = adap->uld[CXGB4_ULD_CRYPTO].handle;

	if (!u_ctx)
		return -1;
	return u_ctx->lldi.rxq_ids[0];
}

void chcr_enable_ktls(struct adapter *adap);
void chcr_disable_ktls(struct adapter *adap);
#endif /* CONFIG_CHELSIO_TLS_DEVICE */
#endif /* __CHCR_KTLS_H__ */
