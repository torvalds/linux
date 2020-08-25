/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019 Chelsio Communications.  All rights reserved. */

#ifndef __CXGB4_TC_MATCHALL_H__
#define __CXGB4_TC_MATCHALL_H__

#include <net/pkt_cls.h>

enum cxgb4_matchall_state {
	CXGB4_MATCHALL_STATE_DISABLED = 0,
	CXGB4_MATCHALL_STATE_ENABLED,
};

struct cxgb4_matchall_egress_entry {
	enum cxgb4_matchall_state state; /* Current MATCHALL offload state */
	u8 hwtc; /* Traffic class bound to port */
	u64 cookie; /* Used to identify the MATCHALL rule offloaded */
};

struct cxgb4_matchall_ingress_entry {
	enum cxgb4_matchall_state state; /* Current MATCHALL offload state */
	u32 tid[CXGB4_FILTER_TYPE_MAX]; /* Index to hardware filter entries */
	/* Filter entries */
	struct ch_filter_specification fs[CXGB4_FILTER_TYPE_MAX];
	u16 viid_mirror; /* Identifier for allocated Mirror VI */
	u64 bytes; /* # of bytes hitting the filter */
	u64 packets; /* # of packets hitting the filter */
	u64 last_used; /* Last updated jiffies time */
};

struct cxgb4_tc_port_matchall {
	struct cxgb4_matchall_egress_entry egress; /* Egress offload info */
	struct cxgb4_matchall_ingress_entry ingress; /* Ingress offload info */
};

struct cxgb4_tc_matchall {
	struct cxgb4_tc_port_matchall *port_matchall; /* Per port entry */
};

int cxgb4_tc_matchall_replace(struct net_device *dev,
			      struct tc_cls_matchall_offload *cls_matchall,
			      bool ingress);
int cxgb4_tc_matchall_destroy(struct net_device *dev,
			      struct tc_cls_matchall_offload *cls_matchall,
			      bool ingress);
int cxgb4_tc_matchall_stats(struct net_device *dev,
			    struct tc_cls_matchall_offload *cls_matchall);

int cxgb4_init_tc_matchall(struct adapter *adap);
void cxgb4_cleanup_tc_matchall(struct adapter *adap);
#endif /* __CXGB4_TC_MATCHALL_H__ */
