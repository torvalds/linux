/*
* Copyright (c) 2010 Intel-NE, Inc.  All rights reserved.
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

#ifndef __NES_MGT_H
#define __NES_MGT_H

#define MPA_FRAMING 6	/* length is 2 bytes, crc is 4 bytes */

int nes_init_mgt_qp(struct nes_device *nesdev, struct net_device *netdev, struct nes_vnic *nesvnic);
void nes_queue_mgt_skbs(struct sk_buff *skb, struct nes_vnic *nesvnic, struct nes_qp *nesqp);
void nes_destroy_mgt(struct nes_vnic *nesvnic);
void nes_destroy_pau_qp(struct nes_device *nesdev, struct nes_qp *nesqp);

struct nes_hw_mgt {
	struct nes_hw_nic_rq_wqe *rq_vbase;	/* virtual address of rq */
	dma_addr_t rq_pbase;			/* PCI memory for host rings */
	struct sk_buff *rx_skb[NES_NIC_WQ_SIZE];
	u16 qp_id;
	u16 sq_head;
	u16 rq_head;
	u16 rq_tail;
	u16 rq_size;
	u8 replenishing_rq;
	u8 reserved;
	spinlock_t rq_lock;
};

struct nes_vnic_mgt {
	struct nes_vnic        *nesvnic;
	struct nes_hw_mgt      mgt;
	struct nes_hw_nic_cq   mgt_cq;
	atomic_t               rx_skbs_needed;
	struct timer_list      rq_wqes_timer;
	atomic_t               rx_skb_timer_running;
};

#define MAX_FPDU_FRAGS 4
struct pau_fpdu_frag {
	struct sk_buff         *skb;
	u64                    physaddr;
	u32                    frag_len;
	bool                   cmplt;
};

struct pau_fpdu_info {
	struct nes_qp          *nesqp;
	struct nes_cqp_request *cqp_request;
	void                   *hdr_vbase;
	dma_addr_t             hdr_pbase;
	int                    hdr_len;
	u16                    data_len;
	u16                    frag_cnt;
	struct pau_fpdu_frag   frags[MAX_FPDU_FRAGS];
};

enum pau_qh_state {
	PAU_DEL_QH,
	PAU_ADD_LB_QH,
	PAU_READY
};

struct pau_qh_chg {
	struct nes_device *nesdev;
	struct nes_vnic *nesvnic;
	struct nes_qp *nesqp;
};

#endif          /* __NES_MGT_H */
