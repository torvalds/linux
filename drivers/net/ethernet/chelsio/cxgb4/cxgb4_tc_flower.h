/*
 * This file is part of the Chelsio T4/T5/T6 Ethernet driver for Linux.
 *
 * Copyright (c) 2017 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __CXGB4_TC_FLOWER_H
#define __CXGB4_TC_FLOWER_H

#include <net/pkt_cls.h>

struct ch_tc_flower_stats {
	u64 prev_packet_count;
	u64 packet_count;
	u64 byte_count;
	u64 last_used;
};

struct ch_tc_flower_entry {
	struct ch_filter_specification fs;
	struct ch_tc_flower_stats stats;
	unsigned long tc_flower_cookie;
	struct hlist_node link;
	struct rcu_head rcu;
	spinlock_t lock; /* lock for stats */
	u32 filter_id;
};

int cxgb4_tc_flower_replace(struct net_device *dev,
			    struct tc_cls_flower_offload *cls);
int cxgb4_tc_flower_destroy(struct net_device *dev,
			    struct tc_cls_flower_offload *cls);
int cxgb4_tc_flower_stats(struct net_device *dev,
			  struct tc_cls_flower_offload *cls);

void cxgb4_init_tc_flower(struct adapter *adap);
void cxgb4_cleanup_tc_flower(struct adapter *adap);
#endif /* __CXGB4_TC_FLOWER_H */
