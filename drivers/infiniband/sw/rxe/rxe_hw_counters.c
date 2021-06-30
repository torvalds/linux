// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2017 Mellanox Technologies Ltd. All rights reserved.
 */

#include "rxe.h"
#include "rxe_hw_counters.h"

static const char * const rxe_counter_name[] = {
	[RXE_CNT_SENT_PKTS]           =  "sent_pkts",
	[RXE_CNT_RCVD_PKTS]           =  "rcvd_pkts",
	[RXE_CNT_DUP_REQ]             =  "duplicate_request",
	[RXE_CNT_OUT_OF_SEQ_REQ]      =  "out_of_seq_request",
	[RXE_CNT_RCV_RNR]             =  "rcvd_rnr_err",
	[RXE_CNT_SND_RNR]             =  "send_rnr_err",
	[RXE_CNT_RCV_SEQ_ERR]         =  "rcvd_seq_err",
	[RXE_CNT_COMPLETER_SCHED]     =  "ack_deferred",
	[RXE_CNT_RETRY_EXCEEDED]      =  "retry_exceeded_err",
	[RXE_CNT_RNR_RETRY_EXCEEDED]  =  "retry_rnr_exceeded_err",
	[RXE_CNT_COMP_RETRY]          =  "completer_retry_err",
	[RXE_CNT_SEND_ERR]            =  "send_err",
	[RXE_CNT_LINK_DOWNED]         =  "link_downed",
	[RXE_CNT_RDMA_SEND]           =  "rdma_sends",
	[RXE_CNT_RDMA_RECV]           =  "rdma_recvs",
};

int rxe_ib_get_hw_stats(struct ib_device *ibdev,
			struct rdma_hw_stats *stats,
			u32 port, int index)
{
	struct rxe_dev *dev = to_rdev(ibdev);
	unsigned int cnt;

	if (!port || !stats)
		return -EINVAL;

	for (cnt = 0; cnt  < ARRAY_SIZE(rxe_counter_name); cnt++)
		stats->value[cnt] = atomic64_read(&dev->stats_counters[cnt]);

	return ARRAY_SIZE(rxe_counter_name);
}

struct rdma_hw_stats *rxe_ib_alloc_hw_stats(struct ib_device *ibdev,
					    u32 port_num)
{
	BUILD_BUG_ON(ARRAY_SIZE(rxe_counter_name) != RXE_NUM_OF_COUNTERS);
	/* We support only per port stats */
	if (!port_num)
		return NULL;

	return rdma_alloc_hw_stats_struct(rxe_counter_name,
					  ARRAY_SIZE(rxe_counter_name),
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}
