/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019 Chelsio Communications.  All rights reserved. */

#ifndef __CXGB4_TC_MQPRIO_H__
#define __CXGB4_TC_MQPRIO_H__

#include <net/pkt_cls.h>

#define CXGB4_EOSW_TXQ_DEFAULT_DESC_NUM 128

enum cxgb4_mqprio_state {
	CXGB4_MQPRIO_STATE_DISABLED = 0,
	CXGB4_MQPRIO_STATE_ACTIVE,
};

struct cxgb4_tc_port_mqprio {
	enum cxgb4_mqprio_state state; /* Current MQPRIO offload state */
	struct tc_mqprio_qopt_offload mqprio; /* MQPRIO offload params */
	struct sge_eosw_txq *eosw_txq; /* Netdev SW Tx queue array */
};

struct cxgb4_tc_mqprio {
	struct cxgb4_tc_port_mqprio *port_mqprio; /* Per port MQPRIO info */
};

int cxgb4_setup_tc_mqprio(struct net_device *dev,
			  struct tc_mqprio_qopt_offload *mqprio);
int cxgb4_init_tc_mqprio(struct adapter *adap);
void cxgb4_cleanup_tc_mqprio(struct adapter *adap);
#endif /* __CXGB4_TC_MQPRIO_H__ */
