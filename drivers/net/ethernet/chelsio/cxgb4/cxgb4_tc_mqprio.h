/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019 Chelsio Communications.  All rights reserved. */

#ifndef __CXGB4_TC_MQPRIO_H__
#define __CXGB4_TC_MQPRIO_H__

#include <net/pkt_cls.h>

#define CXGB4_EOSW_TXQ_DEFAULT_DESC_NUM 128

#define CXGB4_EOHW_TXQ_DEFAULT_DESC_NUM 1024

#define CXGB4_EOHW_RXQ_DEFAULT_DESC_NUM 1024
#define CXGB4_EOHW_RXQ_DEFAULT_DESC_SIZE 64
#define CXGB4_EOHW_RXQ_DEFAULT_INTR_USEC 5
#define CXGB4_EOHW_RXQ_DEFAULT_PKT_CNT 8

#define CXGB4_EOHW_FLQ_DEFAULT_DESC_NUM 72

#define CXGB4_FLOWC_WAIT_TIMEOUT (5 * HZ)

enum cxgb4_mqprio_state {
	CXGB4_MQPRIO_STATE_DISABLED = 0,
	CXGB4_MQPRIO_STATE_ACTIVE,
};

struct cxgb4_tc_port_mqprio {
	enum cxgb4_mqprio_state state; /* Current MQPRIO offload state */
	struct tc_mqprio_qopt_offload mqprio; /* MQPRIO offload params */
	struct sge_eosw_txq *eosw_txq; /* Netdev SW Tx queue array */
	u8 tc_hwtc_map[TC_QOPT_MAX_QUEUE]; /* MQPRIO tc to hardware tc map */
};

struct cxgb4_tc_mqprio {
	refcount_t refcnt; /* Refcount for adapter-wide resources */
	struct cxgb4_tc_port_mqprio *port_mqprio; /* Per port MQPRIO info */
};

int cxgb4_setup_tc_mqprio(struct net_device *dev,
			  struct tc_mqprio_qopt_offload *mqprio);
void cxgb4_mqprio_stop_offload(struct adapter *adap);
int cxgb4_init_tc_mqprio(struct adapter *adap);
void cxgb4_cleanup_tc_mqprio(struct adapter *adap);
#endif /* __CXGB4_TC_MQPRIO_H__ */
