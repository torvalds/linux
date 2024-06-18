/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2017 Mellanox Technologies Ltd. All rights reserved.
 */

#ifndef RXE_HW_COUNTERS_H
#define RXE_HW_COUNTERS_H

/*
 * when adding counters to enum also add
 * them to rxe_counter_name[] vector.
 */
enum rxe_counters {
	RXE_CNT_SENT_PKTS,
	RXE_CNT_RCVD_PKTS,
	RXE_CNT_DUP_REQ,
	RXE_CNT_OUT_OF_SEQ_REQ,
	RXE_CNT_RCV_RNR,
	RXE_CNT_SND_RNR,
	RXE_CNT_RCV_SEQ_ERR,
	RXE_CNT_SENDER_SCHED,
	RXE_CNT_RETRY_EXCEEDED,
	RXE_CNT_RNR_RETRY_EXCEEDED,
	RXE_CNT_COMP_RETRY,
	RXE_CNT_SEND_ERR,
	RXE_CNT_LINK_DOWNED,
	RXE_CNT_RDMA_SEND,
	RXE_CNT_RDMA_RECV,
	RXE_NUM_OF_COUNTERS
};

struct rdma_hw_stats *rxe_ib_alloc_hw_port_stats(struct ib_device *ibdev,
						 u32 port_num);
int rxe_ib_get_hw_stats(struct ib_device *ibdev,
			struct rdma_hw_stats *stats,
			u32 port, int index);
#endif /* RXE_HW_COUNTERS_H */
