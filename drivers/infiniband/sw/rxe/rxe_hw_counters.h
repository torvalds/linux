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
	RXE_CNT_COMPLETER_SCHED,
	RXE_CNT_RETRY_EXCEEDED,
	RXE_CNT_RNR_RETRY_EXCEEDED,
	RXE_CNT_COMP_RETRY,
	RXE_CNT_SEND_ERR,
	RXE_NUM_OF_COUNTERS
};

struct rdma_hw_stats *rxe_ib_alloc_hw_stats(struct ib_device *ibdev,
					    u8 port_num);
int rxe_ib_get_hw_stats(struct ib_device *ibdev,
			struct rdma_hw_stats *stats,
			u8 port, int index);
#endif /* RXE_HW_COUNTERS_H */
