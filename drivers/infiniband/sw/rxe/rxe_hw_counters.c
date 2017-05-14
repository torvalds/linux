/*
 * Copyright (c) 2017 Mellanox Technologies Ltd. All rights reserved.
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
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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

#include "rxe.h"
#include "rxe_hw_counters.h"

const char * const rxe_counter_name[] = {
	[RXE_CNT_SENT_PKTS]           =  "sent_pkts",
	[RXE_CNT_RCVD_PKTS]           =  "rcvd_pkts",
	[RXE_CNT_DUP_REQ]             =  "duplicate_request",
	[RXE_CNT_OUT_OF_SEQ_REQ]      =  "out_of_sequence",
	[RXE_CNT_RCV_RNR]             =  "rcvd_rnr_err",
	[RXE_CNT_SND_RNR]             =  "send_rnr_err",
	[RXE_CNT_RCV_SEQ_ERR]         =  "rcvd_seq_err",
	[RXE_CNT_COMPLETER_SCHED]     =  "ack_deffered",
	[RXE_CNT_RETRY_EXCEEDED]      =  "retry_exceeded_err",
	[RXE_CNT_RNR_RETRY_EXCEEDED]  =  "retry_rnr_exceeded_err",
	[RXE_CNT_COMP_RETRY]          =  "completer_retry_err",
	[RXE_CNT_SEND_ERR]            =  "send_err",
};

int rxe_ib_get_hw_stats(struct ib_device *ibdev,
			struct rdma_hw_stats *stats,
			u8 port, int index)
{
	struct rxe_dev *dev = to_rdev(ibdev);
	unsigned int cnt;

	if (!port || !stats)
		return -EINVAL;

	for (cnt = 0; cnt  < ARRAY_SIZE(rxe_counter_name); cnt++)
		stats->value[cnt] = dev->stats_counters[cnt];

	return ARRAY_SIZE(rxe_counter_name);
}

struct rdma_hw_stats *rxe_ib_alloc_hw_stats(struct ib_device *ibdev,
					    u8 port_num)
{
	BUILD_BUG_ON(ARRAY_SIZE(rxe_counter_name) != RXE_NUM_OF_COUNTERS);
	/* We support only per port stats */
	if (!port_num)
		return NULL;

	return rdma_alloc_hw_stats_struct(rxe_counter_name,
					  ARRAY_SIZE(rxe_counter_name),
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}
