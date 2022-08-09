// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2017 Mellanox Technologies Ltd. All rights reserved.
 */

#include "rxe.h"
#include "rxe_hw_counters.h"

static const struct rdma_stat_desc rxe_counter_descs[] = {
	[RXE_CNT_SENT_PKTS].name           =  "sent_pkts",
	[RXE_CNT_RCVD_PKTS].name           =  "rcvd_pkts",
	[RXE_CNT_DUP_REQ].name             =  "duplicate_request",
	[RXE_CNT_OUT_OF_SEQ_REQ].name      =  "out_of_seq_request",
	[RXE_CNT_RCV_RNR].name             =  "rcvd_rnr_err",
	[RXE_CNT_SND_RNR].name             =  "send_rnr_err",
	[RXE_CNT_RCV_SEQ_ERR].name         =  "rcvd_seq_err",
	[RXE_CNT_COMPLETER_SCHED].name     =  "ack_deferred",
	[RXE_CNT_RETRY_EXCEEDED].name      =  "retry_exceeded_err",
	[RXE_CNT_RNR_RETRY_EXCEEDED].name  =  "retry_rnr_exceeded_err",
	[RXE_CNT_COMP_RETRY].name          =  "completer_retry_err",
	[RXE_CNT_SEND_ERR].name            =  "send_err",
	[RXE_CNT_LINK_DOWNED].name         =  "link_downed",
	[RXE_CNT_RDMA_SEND].name           =  "rdma_sends",
	[RXE_CNT_RDMA_RECV].name           =  "rdma_recvs",
};

int rxe_ib_get_hw_stats(struct ib_device *ibdev,
			struct rdma_hw_stats *stats,
			u32 port, int index)
{
	struct rxe_dev *dev = to_rdev(ibdev);
	unsigned int cnt;

	if (!port || !stats)
		return -EINVAL;

	for (cnt = 0; cnt < ARRAY_SIZE(rxe_counter_descs); cnt++)
		stats->value[cnt] = atomic64_read(&dev->stats_counters[cnt]);

	return ARRAY_SIZE(rxe_counter_descs);
}

struct rdma_hw_stats *rxe_ib_alloc_hw_port_stats(struct ib_device *ibdev,
						 u32 port_num)
{
	BUILD_BUG_ON(ARRAY_SIZE(rxe_counter_descs) != RXE_NUM_OF_COUNTERS);

	return rdma_alloc_hw_stats_struct(rxe_counter_descs,
					  ARRAY_SIZE(rxe_counter_descs),
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}
