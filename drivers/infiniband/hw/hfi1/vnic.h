#ifndef _HFI1_VNIC_H
#define _HFI1_VNIC_H
/*
 * Copyright(c) 2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <rdma/opa_vnic.h>
#include "hfi.h"
#include "sdma.h"

#define HFI1_VNIC_MAX_TXQ     16
#define HFI1_VNIC_MAX_PAD     12

/* L2 header definitions */
#define HFI1_L2_TYPE_OFFSET     0x7
#define HFI1_L2_TYPE_SHFT       0x5
#define HFI1_L2_TYPE_MASK       0x3

#define HFI1_GET_L2_TYPE(hdr)                                            \
	((*((u8 *)(hdr) + HFI1_L2_TYPE_OFFSET) >> HFI1_L2_TYPE_SHFT) &   \
	 HFI1_L2_TYPE_MASK)

/* L4 type definitions */
#define HFI1_L4_TYPE_OFFSET 8

#define HFI1_GET_L4_TYPE(data)   \
	(*((u8 *)(data) + HFI1_L4_TYPE_OFFSET))

/* L4 header definitions */
#define HFI1_VNIC_L4_HDR_OFFSET  OPA_VNIC_L2_HDR_LEN

#define HFI1_VNIC_GET_L4_HDR(data)   \
	(*((u16 *)((u8 *)(data) + HFI1_VNIC_L4_HDR_OFFSET)))

#define HFI1_VNIC_GET_VESWID(data)   \
	(HFI1_VNIC_GET_L4_HDR(data) & 0xFFF)

/* Service class */
#define HFI1_VNIC_SC_OFFSET_LOW 6
#define HFI1_VNIC_SC_OFFSET_HI  7
#define HFI1_VNIC_SC_SHIFT      4

#define HFI1_VNIC_MAX_QUEUE 16

/**
 * struct hfi1_vnic_sdma - VNIC per Tx ring SDMA information
 * @dd - device data pointer
 * @sde - sdma engine
 * @vinfo - vnic info pointer
 * @wait - iowait structure
 * @stx - sdma tx request
 * @state - vnic Tx ring SDMA state
 * @q_idx - vnic Tx queue index
 */
struct hfi1_vnic_sdma {
	struct hfi1_devdata *dd;
	struct sdma_engine  *sde;
	struct hfi1_vnic_vport_info *vinfo;
	struct iowait wait;
	struct sdma_txreq stx;
	unsigned int state;
	u8 q_idx;
};

/**
 * struct hfi1_vnic_rx_queue - HFI1 VNIC receive queue
 * @idx: queue index
 * @vinfo: pointer to vport information
 * @netdev: network device
 * @napi: netdev napi structure
 * @skbq: queue of received socket buffers
 */
struct hfi1_vnic_rx_queue {
	u8                           idx;
	struct hfi1_vnic_vport_info *vinfo;
	struct net_device           *netdev;
	struct napi_struct           napi;
	struct sk_buff_head          skbq;
};

/**
 * struct hfi1_vnic_vport_info - HFI1 VNIC virtual port information
 * @dd: device data pointer
 * @netdev: net device pointer
 * @flags: state flags
 * @lock: vport lock
 * @num_tx_q: number of transmit queues
 * @num_rx_q: number of receive queues
 * @vesw_id: virtual switch id
 * @rxq: Array of receive queues
 * @stats: per queue stats
 * @sdma: VNIC SDMA structure per TXQ
 */
struct hfi1_vnic_vport_info {
	struct hfi1_devdata *dd;
	struct net_device   *netdev;
	unsigned long        flags;

	/* Lock used around state updates */
	struct mutex         lock;

	u8  num_tx_q;
	u8  num_rx_q;
	u16 vesw_id;
	struct hfi1_vnic_rx_queue rxq[HFI1_NUM_VNIC_CTXT];

	struct opa_vnic_stats  stats[HFI1_VNIC_MAX_QUEUE];
	struct hfi1_vnic_sdma  sdma[HFI1_VNIC_MAX_TXQ];
};

#define v_dbg(format, arg...) \
	netdev_dbg(vinfo->netdev, format, ## arg)
#define v_err(format, arg...) \
	netdev_err(vinfo->netdev, format, ## arg)
#define v_info(format, arg...) \
	netdev_info(vinfo->netdev, format, ## arg)

/* vnic hfi1 internal functions */
void hfi1_vnic_setup(struct hfi1_devdata *dd);
void hfi1_vnic_cleanup(struct hfi1_devdata *dd);
int hfi1_vnic_txreq_init(struct hfi1_devdata *dd);
void hfi1_vnic_txreq_deinit(struct hfi1_devdata *dd);

void hfi1_vnic_bypass_rcv(struct hfi1_packet *packet);
void hfi1_vnic_sdma_init(struct hfi1_vnic_vport_info *vinfo);
bool hfi1_vnic_sdma_write_avail(struct hfi1_vnic_vport_info *vinfo,
				u8 q_idx);

/* vnic rdma netdev operations */
struct net_device *hfi1_vnic_alloc_rn(struct ib_device *device,
				      u8 port_num,
				      enum rdma_netdev_t type,
				      const char *name,
				      unsigned char name_assign_type,
				      void (*setup)(struct net_device *));
int hfi1_vnic_send_dma(struct hfi1_devdata *dd, u8 q_idx,
		       struct hfi1_vnic_vport_info *vinfo,
		       struct sk_buff *skb, u64 pbc, u8 plen);

#endif /* _HFI1_VNIC_H */
