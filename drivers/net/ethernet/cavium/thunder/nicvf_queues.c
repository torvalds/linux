/*
 * Copyright (C) 2015 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/etherdevice.h>
#include <net/ip.h>
#include <net/tso.h>

#include "nic_reg.h"
#include "nic.h"
#include "q_struct.h"
#include "nicvf_queues.h"

static void nicvf_get_page(struct nicvf *nic)
{
	if (!nic->rb_pageref || !nic->rb_page)
		return;

	atomic_add(nic->rb_pageref, &nic->rb_page->_count);
	nic->rb_pageref = 0;
}

/* Poll a register for a specific value */
static int nicvf_poll_reg(struct nicvf *nic, int qidx,
			  u64 reg, int bit_pos, int bits, int val)
{
	u64 bit_mask;
	u64 reg_val;
	int timeout = 10;

	bit_mask = (1ULL << bits) - 1;
	bit_mask = (bit_mask << bit_pos);

	while (timeout) {
		reg_val = nicvf_queue_reg_read(nic, reg, qidx);
		if (((reg_val & bit_mask) >> bit_pos) == val)
			return 0;
		usleep_range(1000, 2000);
		timeout--;
	}
	netdev_err(nic->netdev, "Poll on reg 0x%llx failed\n", reg);
	return 1;
}

/* Allocate memory for a queue's descriptors */
static int nicvf_alloc_q_desc_mem(struct nicvf *nic, struct q_desc_mem *dmem,
				  int q_len, int desc_size, int align_bytes)
{
	dmem->q_len = q_len;
	dmem->size = (desc_size * q_len) + align_bytes;
	/* Save address, need it while freeing */
	dmem->unalign_base = dma_zalloc_coherent(&nic->pdev->dev, dmem->size,
						&dmem->dma, GFP_KERNEL);
	if (!dmem->unalign_base)
		return -ENOMEM;

	/* Align memory address for 'align_bytes' */
	dmem->phys_base = NICVF_ALIGNED_ADDR((u64)dmem->dma, align_bytes);
	dmem->base = dmem->unalign_base + (dmem->phys_base - dmem->dma);
	return 0;
}

/* Free queue's descriptor memory */
static void nicvf_free_q_desc_mem(struct nicvf *nic, struct q_desc_mem *dmem)
{
	if (!dmem)
		return;

	dma_free_coherent(&nic->pdev->dev, dmem->size,
			  dmem->unalign_base, dmem->dma);
	dmem->unalign_base = NULL;
	dmem->base = NULL;
}

/* Allocate buffer for packet reception
 * HW returns memory address where packet is DMA'ed but not a pointer
 * into RBDR ring, so save buffer address at the start of fragment and
 * align the start address to a cache aligned address
 */
static inline int nicvf_alloc_rcv_buffer(struct nicvf *nic, gfp_t gfp,
					 u32 buf_len, u64 **rbuf)
{
	int order = (PAGE_SIZE <= 4096) ?  PAGE_ALLOC_COSTLY_ORDER : 0;

	/* Check if request can be accomodated in previous allocated page */
	if (nic->rb_page &&
	    ((nic->rb_page_offset + buf_len) < (PAGE_SIZE << order))) {
		nic->rb_pageref++;
		goto ret;
	}

	nicvf_get_page(nic);
	nic->rb_page = NULL;

	/* Allocate a new page */
	if (!nic->rb_page) {
		nic->rb_page = alloc_pages(gfp | __GFP_COMP | __GFP_NOWARN,
					   order);
		if (!nic->rb_page) {
			nic->drv_stats.rcv_buffer_alloc_failures++;
			return -ENOMEM;
		}
		nic->rb_page_offset = 0;
	}

ret:
	*rbuf = (u64 *)((u64)page_address(nic->rb_page) + nic->rb_page_offset);
	nic->rb_page_offset += buf_len;

	return 0;
}

/* Build skb around receive buffer */
static struct sk_buff *nicvf_rb_ptr_to_skb(struct nicvf *nic,
					   u64 rb_ptr, int len)
{
	void *data;
	struct sk_buff *skb;

	data = phys_to_virt(rb_ptr);

	/* Now build an skb to give to stack */
	skb = build_skb(data, RCV_FRAG_LEN);
	if (!skb) {
		put_page(virt_to_page(data));
		return NULL;
	}

	prefetch(skb->data);
	return skb;
}

/* Allocate RBDR ring and populate receive buffers */
static int  nicvf_init_rbdr(struct nicvf *nic, struct rbdr *rbdr,
			    int ring_len, int buf_size)
{
	int idx;
	u64 *rbuf;
	struct rbdr_entry_t *desc;
	int err;

	err = nicvf_alloc_q_desc_mem(nic, &rbdr->dmem, ring_len,
				     sizeof(struct rbdr_entry_t),
				     NICVF_RCV_BUF_ALIGN_BYTES);
	if (err)
		return err;

	rbdr->desc = rbdr->dmem.base;
	/* Buffer size has to be in multiples of 128 bytes */
	rbdr->dma_size = buf_size;
	rbdr->enable = true;
	rbdr->thresh = RBDR_THRESH;

	nic->rb_page = NULL;
	for (idx = 0; idx < ring_len; idx++) {
		err = nicvf_alloc_rcv_buffer(nic, GFP_KERNEL, RCV_FRAG_LEN,
					     &rbuf);
		if (err)
			return err;

		desc = GET_RBDR_DESC(rbdr, idx);
		desc->buf_addr = virt_to_phys(rbuf) >> NICVF_RCV_BUF_ALIGN;
	}

	nicvf_get_page(nic);

	return 0;
}

/* Free RBDR ring and its receive buffers */
static void nicvf_free_rbdr(struct nicvf *nic, struct rbdr *rbdr)
{
	int head, tail;
	u64 buf_addr;
	struct rbdr_entry_t *desc;

	if (!rbdr)
		return;

	rbdr->enable = false;
	if (!rbdr->dmem.base)
		return;

	head = rbdr->head;
	tail = rbdr->tail;

	/* Free SKBs */
	while (head != tail) {
		desc = GET_RBDR_DESC(rbdr, head);
		buf_addr = desc->buf_addr << NICVF_RCV_BUF_ALIGN;
		put_page(virt_to_page(phys_to_virt(buf_addr)));
		head++;
		head &= (rbdr->dmem.q_len - 1);
	}
	/* Free SKB of tail desc */
	desc = GET_RBDR_DESC(rbdr, tail);
	buf_addr = desc->buf_addr << NICVF_RCV_BUF_ALIGN;
	put_page(virt_to_page(phys_to_virt(buf_addr)));

	/* Free RBDR ring */
	nicvf_free_q_desc_mem(nic, &rbdr->dmem);
}

/* Refill receive buffer descriptors with new buffers.
 */
static void nicvf_refill_rbdr(struct nicvf *nic, gfp_t gfp)
{
	struct queue_set *qs = nic->qs;
	int rbdr_idx = qs->rbdr_cnt;
	int tail, qcount;
	int refill_rb_cnt;
	struct rbdr *rbdr;
	struct rbdr_entry_t *desc;
	u64 *rbuf;
	int new_rb = 0;

refill:
	if (!rbdr_idx)
		return;
	rbdr_idx--;
	rbdr = &qs->rbdr[rbdr_idx];
	/* Check if it's enabled */
	if (!rbdr->enable)
		goto next_rbdr;

	/* Get no of desc's to be refilled */
	qcount = nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_STATUS0, rbdr_idx);
	qcount &= 0x7FFFF;
	/* Doorbell can be ringed with a max of ring size minus 1 */
	if (qcount >= (qs->rbdr_len - 1))
		goto next_rbdr;
	else
		refill_rb_cnt = qs->rbdr_len - qcount - 1;

	/* Start filling descs from tail */
	tail = nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_TAIL, rbdr_idx) >> 3;
	while (refill_rb_cnt) {
		tail++;
		tail &= (rbdr->dmem.q_len - 1);

		if (nicvf_alloc_rcv_buffer(nic, gfp, RCV_FRAG_LEN, &rbuf))
			break;

		desc = GET_RBDR_DESC(rbdr, tail);
		desc->buf_addr = virt_to_phys(rbuf) >> NICVF_RCV_BUF_ALIGN;
		refill_rb_cnt--;
		new_rb++;
	}

	nicvf_get_page(nic);

	/* make sure all memory stores are done before ringing doorbell */
	smp_wmb();

	/* Check if buffer allocation failed */
	if (refill_rb_cnt)
		nic->rb_alloc_fail = true;
	else
		nic->rb_alloc_fail = false;

	/* Notify HW */
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_DOOR,
			      rbdr_idx, new_rb);
next_rbdr:
	/* Re-enable RBDR interrupts only if buffer allocation is success */
	if (!nic->rb_alloc_fail && rbdr->enable)
		nicvf_enable_intr(nic, NICVF_INTR_RBDR, rbdr_idx);

	if (rbdr_idx)
		goto refill;
}

/* Alloc rcv buffers in non-atomic mode for better success */
void nicvf_rbdr_work(struct work_struct *work)
{
	struct nicvf *nic = container_of(work, struct nicvf, rbdr_work.work);

	nicvf_refill_rbdr(nic, GFP_KERNEL);
	if (nic->rb_alloc_fail)
		schedule_delayed_work(&nic->rbdr_work, msecs_to_jiffies(10));
	else
		nic->rb_work_scheduled = false;
}

/* In Softirq context, alloc rcv buffers in atomic mode */
void nicvf_rbdr_task(unsigned long data)
{
	struct nicvf *nic = (struct nicvf *)data;

	nicvf_refill_rbdr(nic, GFP_ATOMIC);
	if (nic->rb_alloc_fail) {
		nic->rb_work_scheduled = true;
		schedule_delayed_work(&nic->rbdr_work, msecs_to_jiffies(10));
	}
}

/* Initialize completion queue */
static int nicvf_init_cmp_queue(struct nicvf *nic,
				struct cmp_queue *cq, int q_len)
{
	int err;

	err = nicvf_alloc_q_desc_mem(nic, &cq->dmem, q_len, CMP_QUEUE_DESC_SIZE,
				     NICVF_CQ_BASE_ALIGN_BYTES);
	if (err)
		return err;

	cq->desc = cq->dmem.base;
	cq->thresh = pass1_silicon(nic->pdev) ? 0 : CMP_QUEUE_CQE_THRESH;
	nic->cq_coalesce_usecs = (CMP_QUEUE_TIMER_THRESH * 0.05) - 1;

	return 0;
}

static void nicvf_free_cmp_queue(struct nicvf *nic, struct cmp_queue *cq)
{
	if (!cq)
		return;
	if (!cq->dmem.base)
		return;

	nicvf_free_q_desc_mem(nic, &cq->dmem);
}

/* Initialize transmit queue */
static int nicvf_init_snd_queue(struct nicvf *nic,
				struct snd_queue *sq, int q_len)
{
	int err;

	err = nicvf_alloc_q_desc_mem(nic, &sq->dmem, q_len, SND_QUEUE_DESC_SIZE,
				     NICVF_SQ_BASE_ALIGN_BYTES);
	if (err)
		return err;

	sq->desc = sq->dmem.base;
	sq->skbuff = kcalloc(q_len, sizeof(u64), GFP_KERNEL);
	if (!sq->skbuff)
		return -ENOMEM;
	sq->head = 0;
	sq->tail = 0;
	atomic_set(&sq->free_cnt, q_len - 1);
	sq->thresh = SND_QUEUE_THRESH;

	/* Preallocate memory for TSO segment's header */
	sq->tso_hdrs = dma_alloc_coherent(&nic->pdev->dev,
					  q_len * TSO_HEADER_SIZE,
					  &sq->tso_hdrs_phys, GFP_KERNEL);
	if (!sq->tso_hdrs)
		return -ENOMEM;

	return 0;
}

static void nicvf_free_snd_queue(struct nicvf *nic, struct snd_queue *sq)
{
	if (!sq)
		return;
	if (!sq->dmem.base)
		return;

	if (sq->tso_hdrs)
		dma_free_coherent(&nic->pdev->dev,
				  sq->dmem.q_len * TSO_HEADER_SIZE,
				  sq->tso_hdrs, sq->tso_hdrs_phys);

	kfree(sq->skbuff);
	nicvf_free_q_desc_mem(nic, &sq->dmem);
}

static void nicvf_reclaim_snd_queue(struct nicvf *nic,
				    struct queue_set *qs, int qidx)
{
	/* Disable send queue */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_CFG, qidx, 0);
	/* Check if SQ is stopped */
	if (nicvf_poll_reg(nic, qidx, NIC_QSET_SQ_0_7_STATUS, 21, 1, 0x01))
		return;
	/* Reset send queue */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_CFG, qidx, NICVF_SQ_RESET);
}

static void nicvf_reclaim_rcv_queue(struct nicvf *nic,
				    struct queue_set *qs, int qidx)
{
	union nic_mbx mbx = {};

	/* Make sure all packets in the pipeline are written back into mem */
	mbx.msg.msg = NIC_MBOX_MSG_RQ_SW_SYNC;
	nicvf_send_msg_to_pf(nic, &mbx);
}

static void nicvf_reclaim_cmp_queue(struct nicvf *nic,
				    struct queue_set *qs, int qidx)
{
	/* Disable timer threshold (doesn't get reset upon CQ reset */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_CFG2, qidx, 0);
	/* Disable completion queue */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_CFG, qidx, 0);
	/* Reset completion queue */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_CFG, qidx, NICVF_CQ_RESET);
}

static void nicvf_reclaim_rbdr(struct nicvf *nic,
			       struct rbdr *rbdr, int qidx)
{
	u64 tmp, fifo_state;
	int timeout = 10;

	/* Save head and tail pointers for feeing up buffers */
	rbdr->head = nicvf_queue_reg_read(nic,
					  NIC_QSET_RBDR_0_1_HEAD,
					  qidx) >> 3;
	rbdr->tail = nicvf_queue_reg_read(nic,
					  NIC_QSET_RBDR_0_1_TAIL,
					  qidx) >> 3;

	/* If RBDR FIFO is in 'FAIL' state then do a reset first
	 * before relaiming.
	 */
	fifo_state = nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_STATUS0, qidx);
	if (((fifo_state >> 62) & 0x03) == 0x3)
		nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_CFG,
				      qidx, NICVF_RBDR_RESET);

	/* Disable RBDR */
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_CFG, qidx, 0);
	if (nicvf_poll_reg(nic, qidx, NIC_QSET_RBDR_0_1_STATUS0, 62, 2, 0x00))
		return;
	while (1) {
		tmp = nicvf_queue_reg_read(nic,
					   NIC_QSET_RBDR_0_1_PREFETCH_STATUS,
					   qidx);
		if ((tmp & 0xFFFFFFFF) == ((tmp >> 32) & 0xFFFFFFFF))
			break;
		usleep_range(1000, 2000);
		timeout--;
		if (!timeout) {
			netdev_err(nic->netdev,
				   "Failed polling on prefetch status\n");
			return;
		}
	}
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_CFG,
			      qidx, NICVF_RBDR_RESET);

	if (nicvf_poll_reg(nic, qidx, NIC_QSET_RBDR_0_1_STATUS0, 62, 2, 0x02))
		return;
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_CFG, qidx, 0x00);
	if (nicvf_poll_reg(nic, qidx, NIC_QSET_RBDR_0_1_STATUS0, 62, 2, 0x00))
		return;
}

void nicvf_config_vlan_stripping(struct nicvf *nic, netdev_features_t features)
{
	u64 rq_cfg;
	int sqs;

	rq_cfg = nicvf_queue_reg_read(nic, NIC_QSET_RQ_GEN_CFG, 0);

	/* Enable first VLAN stripping */
	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		rq_cfg |= (1ULL << 25);
	else
		rq_cfg &= ~(1ULL << 25);
	nicvf_queue_reg_write(nic, NIC_QSET_RQ_GEN_CFG, 0, rq_cfg);

	/* Configure Secondary Qsets, if any */
	for (sqs = 0; sqs < nic->sqs_count; sqs++)
		if (nic->snicvf[sqs])
			nicvf_queue_reg_write(nic->snicvf[sqs],
					      NIC_QSET_RQ_GEN_CFG, 0, rq_cfg);
}

/* Configures receive queue */
static void nicvf_rcv_queue_config(struct nicvf *nic, struct queue_set *qs,
				   int qidx, bool enable)
{
	union nic_mbx mbx = {};
	struct rcv_queue *rq;
	struct rq_cfg rq_cfg;

	rq = &qs->rq[qidx];
	rq->enable = enable;

	/* Disable receive queue */
	nicvf_queue_reg_write(nic, NIC_QSET_RQ_0_7_CFG, qidx, 0);

	if (!rq->enable) {
		nicvf_reclaim_rcv_queue(nic, qs, qidx);
		return;
	}

	rq->cq_qs = qs->vnic_id;
	rq->cq_idx = qidx;
	rq->start_rbdr_qs = qs->vnic_id;
	rq->start_qs_rbdr_idx = qs->rbdr_cnt - 1;
	rq->cont_rbdr_qs = qs->vnic_id;
	rq->cont_qs_rbdr_idx = qs->rbdr_cnt - 1;
	/* all writes of RBDR data to be loaded into L2 Cache as well*/
	rq->caching = 1;

	/* Send a mailbox msg to PF to config RQ */
	mbx.rq.msg = NIC_MBOX_MSG_RQ_CFG;
	mbx.rq.qs_num = qs->vnic_id;
	mbx.rq.rq_num = qidx;
	mbx.rq.cfg = (rq->caching << 26) | (rq->cq_qs << 19) |
			  (rq->cq_idx << 16) | (rq->cont_rbdr_qs << 9) |
			  (rq->cont_qs_rbdr_idx << 8) |
			  (rq->start_rbdr_qs << 1) | (rq->start_qs_rbdr_idx);
	nicvf_send_msg_to_pf(nic, &mbx);

	mbx.rq.msg = NIC_MBOX_MSG_RQ_BP_CFG;
	mbx.rq.cfg = (1ULL << 63) | (1ULL << 62) | (qs->vnic_id << 0);
	nicvf_send_msg_to_pf(nic, &mbx);

	/* RQ drop config
	 * Enable CQ drop to reserve sufficient CQEs for all tx packets
	 */
	mbx.rq.msg = NIC_MBOX_MSG_RQ_DROP_CFG;
	mbx.rq.cfg = (1ULL << 62) | (RQ_CQ_DROP << 8);
	nicvf_send_msg_to_pf(nic, &mbx);

	nicvf_queue_reg_write(nic, NIC_QSET_RQ_GEN_CFG, 0, 0x00);
	if (!nic->sqs_mode)
		nicvf_config_vlan_stripping(nic, nic->netdev->features);

	/* Enable Receive queue */
	memset(&rq_cfg, 0, sizeof(struct rq_cfg));
	rq_cfg.ena = 1;
	rq_cfg.tcp_ena = 0;
	nicvf_queue_reg_write(nic, NIC_QSET_RQ_0_7_CFG, qidx, *(u64 *)&rq_cfg);
}

/* Configures completion queue */
void nicvf_cmp_queue_config(struct nicvf *nic, struct queue_set *qs,
			    int qidx, bool enable)
{
	struct cmp_queue *cq;
	struct cq_cfg cq_cfg;

	cq = &qs->cq[qidx];
	cq->enable = enable;

	if (!cq->enable) {
		nicvf_reclaim_cmp_queue(nic, qs, qidx);
		return;
	}

	/* Reset completion queue */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_CFG, qidx, NICVF_CQ_RESET);

	if (!cq->enable)
		return;

	spin_lock_init(&cq->lock);
	/* Set completion queue base address */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_BASE,
			      qidx, (u64)(cq->dmem.phys_base));

	/* Enable Completion queue */
	memset(&cq_cfg, 0, sizeof(struct cq_cfg));
	cq_cfg.ena = 1;
	cq_cfg.reset = 0;
	cq_cfg.caching = 0;
	cq_cfg.qsize = CMP_QSIZE;
	cq_cfg.avg_con = 0;
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_CFG, qidx, *(u64 *)&cq_cfg);

	/* Set threshold value for interrupt generation */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_THRESH, qidx, cq->thresh);
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_CFG2,
			      qidx, CMP_QUEUE_TIMER_THRESH);
}

/* Configures transmit queue */
static void nicvf_snd_queue_config(struct nicvf *nic, struct queue_set *qs,
				   int qidx, bool enable)
{
	union nic_mbx mbx = {};
	struct snd_queue *sq;
	struct sq_cfg sq_cfg;

	sq = &qs->sq[qidx];
	sq->enable = enable;

	if (!sq->enable) {
		nicvf_reclaim_snd_queue(nic, qs, qidx);
		return;
	}

	/* Reset send queue */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_CFG, qidx, NICVF_SQ_RESET);

	sq->cq_qs = qs->vnic_id;
	sq->cq_idx = qidx;

	/* Send a mailbox msg to PF to config SQ */
	mbx.sq.msg = NIC_MBOX_MSG_SQ_CFG;
	mbx.sq.qs_num = qs->vnic_id;
	mbx.sq.sq_num = qidx;
	mbx.sq.sqs_mode = nic->sqs_mode;
	mbx.sq.cfg = (sq->cq_qs << 3) | sq->cq_idx;
	nicvf_send_msg_to_pf(nic, &mbx);

	/* Set queue base address */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_BASE,
			      qidx, (u64)(sq->dmem.phys_base));

	/* Enable send queue  & set queue size */
	memset(&sq_cfg, 0, sizeof(struct sq_cfg));
	sq_cfg.ena = 1;
	sq_cfg.reset = 0;
	sq_cfg.ldwb = 0;
	sq_cfg.qsize = SND_QSIZE;
	sq_cfg.tstmp_bgx_intf = 0;
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_CFG, qidx, *(u64 *)&sq_cfg);

	/* Set threshold value for interrupt generation */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_THRESH, qidx, sq->thresh);

	/* Set queue:cpu affinity for better load distribution */
	if (cpu_online(qidx)) {
		cpumask_set_cpu(qidx, &sq->affinity_mask);
		netif_set_xps_queue(nic->netdev,
				    &sq->affinity_mask, qidx);
	}
}

/* Configures receive buffer descriptor ring */
static void nicvf_rbdr_config(struct nicvf *nic, struct queue_set *qs,
			      int qidx, bool enable)
{
	struct rbdr *rbdr;
	struct rbdr_cfg rbdr_cfg;

	rbdr = &qs->rbdr[qidx];
	nicvf_reclaim_rbdr(nic, rbdr, qidx);
	if (!enable)
		return;

	/* Set descriptor base address */
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_BASE,
			      qidx, (u64)(rbdr->dmem.phys_base));

	/* Enable RBDR  & set queue size */
	/* Buffer size should be in multiples of 128 bytes */
	memset(&rbdr_cfg, 0, sizeof(struct rbdr_cfg));
	rbdr_cfg.ena = 1;
	rbdr_cfg.reset = 0;
	rbdr_cfg.ldwb = 0;
	rbdr_cfg.qsize = RBDR_SIZE;
	rbdr_cfg.avg_con = 0;
	rbdr_cfg.lines = rbdr->dma_size / 128;
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_CFG,
			      qidx, *(u64 *)&rbdr_cfg);

	/* Notify HW */
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_DOOR,
			      qidx, qs->rbdr_len - 1);

	/* Set threshold value for interrupt generation */
	nicvf_queue_reg_write(nic, NIC_QSET_RBDR_0_1_THRESH,
			      qidx, rbdr->thresh - 1);
}

/* Requests PF to assign and enable Qset */
void nicvf_qset_config(struct nicvf *nic, bool enable)
{
	union nic_mbx mbx = {};
	struct queue_set *qs = nic->qs;
	struct qs_cfg *qs_cfg;

	if (!qs) {
		netdev_warn(nic->netdev,
			    "Qset is still not allocated, don't init queues\n");
		return;
	}

	qs->enable = enable;
	qs->vnic_id = nic->vf_id;

	/* Send a mailbox msg to PF to config Qset */
	mbx.qs.msg = NIC_MBOX_MSG_QS_CFG;
	mbx.qs.num = qs->vnic_id;
	mbx.qs.sqs_count = nic->sqs_count;

	mbx.qs.cfg = 0;
	qs_cfg = (struct qs_cfg *)&mbx.qs.cfg;
	if (qs->enable) {
		qs_cfg->ena = 1;
#ifdef __BIG_ENDIAN
		qs_cfg->be = 1;
#endif
		qs_cfg->vnic = qs->vnic_id;
	}
	nicvf_send_msg_to_pf(nic, &mbx);
}

static void nicvf_free_resources(struct nicvf *nic)
{
	int qidx;
	struct queue_set *qs = nic->qs;

	/* Free receive buffer descriptor ring */
	for (qidx = 0; qidx < qs->rbdr_cnt; qidx++)
		nicvf_free_rbdr(nic, &qs->rbdr[qidx]);

	/* Free completion queue */
	for (qidx = 0; qidx < qs->cq_cnt; qidx++)
		nicvf_free_cmp_queue(nic, &qs->cq[qidx]);

	/* Free send queue */
	for (qidx = 0; qidx < qs->sq_cnt; qidx++)
		nicvf_free_snd_queue(nic, &qs->sq[qidx]);
}

static int nicvf_alloc_resources(struct nicvf *nic)
{
	int qidx;
	struct queue_set *qs = nic->qs;

	/* Alloc receive buffer descriptor ring */
	for (qidx = 0; qidx < qs->rbdr_cnt; qidx++) {
		if (nicvf_init_rbdr(nic, &qs->rbdr[qidx], qs->rbdr_len,
				    DMA_BUFFER_LEN))
			goto alloc_fail;
	}

	/* Alloc send queue */
	for (qidx = 0; qidx < qs->sq_cnt; qidx++) {
		if (nicvf_init_snd_queue(nic, &qs->sq[qidx], qs->sq_len))
			goto alloc_fail;
	}

	/* Alloc completion queue */
	for (qidx = 0; qidx < qs->cq_cnt; qidx++) {
		if (nicvf_init_cmp_queue(nic, &qs->cq[qidx], qs->cq_len))
			goto alloc_fail;
	}

	return 0;
alloc_fail:
	nicvf_free_resources(nic);
	return -ENOMEM;
}

int nicvf_set_qset_resources(struct nicvf *nic)
{
	struct queue_set *qs;

	qs = devm_kzalloc(&nic->pdev->dev, sizeof(*qs), GFP_KERNEL);
	if (!qs)
		return -ENOMEM;
	nic->qs = qs;

	/* Set count of each queue */
	qs->rbdr_cnt = RBDR_CNT;
	qs->rq_cnt = RCV_QUEUE_CNT;
	qs->sq_cnt = SND_QUEUE_CNT;
	qs->cq_cnt = CMP_QUEUE_CNT;

	/* Set queue lengths */
	qs->rbdr_len = RCV_BUF_COUNT;
	qs->sq_len = SND_QUEUE_LEN;
	qs->cq_len = CMP_QUEUE_LEN;

	nic->rx_queues = qs->rq_cnt;
	nic->tx_queues = qs->sq_cnt;

	return 0;
}

int nicvf_config_data_transfer(struct nicvf *nic, bool enable)
{
	bool disable = false;
	struct queue_set *qs = nic->qs;
	int qidx;

	if (!qs)
		return 0;

	if (enable) {
		if (nicvf_alloc_resources(nic))
			return -ENOMEM;

		for (qidx = 0; qidx < qs->sq_cnt; qidx++)
			nicvf_snd_queue_config(nic, qs, qidx, enable);
		for (qidx = 0; qidx < qs->cq_cnt; qidx++)
			nicvf_cmp_queue_config(nic, qs, qidx, enable);
		for (qidx = 0; qidx < qs->rbdr_cnt; qidx++)
			nicvf_rbdr_config(nic, qs, qidx, enable);
		for (qidx = 0; qidx < qs->rq_cnt; qidx++)
			nicvf_rcv_queue_config(nic, qs, qidx, enable);
	} else {
		for (qidx = 0; qidx < qs->rq_cnt; qidx++)
			nicvf_rcv_queue_config(nic, qs, qidx, disable);
		for (qidx = 0; qidx < qs->rbdr_cnt; qidx++)
			nicvf_rbdr_config(nic, qs, qidx, disable);
		for (qidx = 0; qidx < qs->sq_cnt; qidx++)
			nicvf_snd_queue_config(nic, qs, qidx, disable);
		for (qidx = 0; qidx < qs->cq_cnt; qidx++)
			nicvf_cmp_queue_config(nic, qs, qidx, disable);

		nicvf_free_resources(nic);
	}

	return 0;
}

/* Get a free desc from SQ
 * returns descriptor ponter & descriptor number
 */
static inline int nicvf_get_sq_desc(struct snd_queue *sq, int desc_cnt)
{
	int qentry;

	qentry = sq->tail;
	atomic_sub(desc_cnt, &sq->free_cnt);
	sq->tail += desc_cnt;
	sq->tail &= (sq->dmem.q_len - 1);

	return qentry;
}

/* Free descriptor back to SQ for future use */
void nicvf_put_sq_desc(struct snd_queue *sq, int desc_cnt)
{
	atomic_add(desc_cnt, &sq->free_cnt);
	sq->head += desc_cnt;
	sq->head &= (sq->dmem.q_len - 1);
}

static inline int nicvf_get_nxt_sqentry(struct snd_queue *sq, int qentry)
{
	qentry++;
	qentry &= (sq->dmem.q_len - 1);
	return qentry;
}

void nicvf_sq_enable(struct nicvf *nic, struct snd_queue *sq, int qidx)
{
	u64 sq_cfg;

	sq_cfg = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_CFG, qidx);
	sq_cfg |= NICVF_SQ_EN;
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_CFG, qidx, sq_cfg);
	/* Ring doorbell so that H/W restarts processing SQEs */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_DOOR, qidx, 0);
}

void nicvf_sq_disable(struct nicvf *nic, int qidx)
{
	u64 sq_cfg;

	sq_cfg = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_CFG, qidx);
	sq_cfg &= ~NICVF_SQ_EN;
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_CFG, qidx, sq_cfg);
}

void nicvf_sq_free_used_descs(struct net_device *netdev, struct snd_queue *sq,
			      int qidx)
{
	u64 head, tail;
	struct sk_buff *skb;
	struct nicvf *nic = netdev_priv(netdev);
	struct sq_hdr_subdesc *hdr;

	head = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_HEAD, qidx) >> 4;
	tail = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_TAIL, qidx) >> 4;
	while (sq->head != head) {
		hdr = (struct sq_hdr_subdesc *)GET_SQ_DESC(sq, sq->head);
		if (hdr->subdesc_type != SQ_DESC_TYPE_HEADER) {
			nicvf_put_sq_desc(sq, 1);
			continue;
		}
		skb = (struct sk_buff *)sq->skbuff[sq->head];
		if (skb)
			dev_kfree_skb_any(skb);
		atomic64_add(1, (atomic64_t *)&netdev->stats.tx_packets);
		atomic64_add(hdr->tot_len,
			     (atomic64_t *)&netdev->stats.tx_bytes);
		nicvf_put_sq_desc(sq, hdr->subdesc_cnt + 1);
	}
}

/* Calculate no of SQ subdescriptors needed to transmit all
 * segments of this TSO packet.
 * Taken from 'Tilera network driver' with a minor modification.
 */
static int nicvf_tso_count_subdescs(struct sk_buff *skb)
{
	struct skb_shared_info *sh = skb_shinfo(skb);
	unsigned int sh_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
	unsigned int data_len = skb->len - sh_len;
	unsigned int p_len = sh->gso_size;
	long f_id = -1;    /* id of the current fragment */
	long f_size = skb_headlen(skb) - sh_len;  /* current fragment size */
	long f_used = 0;  /* bytes used from the current fragment */
	long n;            /* size of the current piece of payload */
	int num_edescs = 0;
	int segment;

	for (segment = 0; segment < sh->gso_segs; segment++) {
		unsigned int p_used = 0;

		/* One edesc for header and for each piece of the payload. */
		for (num_edescs++; p_used < p_len; num_edescs++) {
			/* Advance as needed. */
			while (f_used >= f_size) {
				f_id++;
				f_size = skb_frag_size(&sh->frags[f_id]);
				f_used = 0;
			}

			/* Use bytes from the current fragment. */
			n = p_len - p_used;
			if (n > f_size - f_used)
				n = f_size - f_used;
			f_used += n;
			p_used += n;
		}

		/* The last segment may be less than gso_size. */
		data_len -= p_len;
		if (data_len < p_len)
			p_len = data_len;
	}

	/* '+ gso_segs' for SQ_HDR_SUDESCs for each segment */
	return num_edescs + sh->gso_segs;
}

/* Get the number of SQ descriptors needed to xmit this skb */
static int nicvf_sq_subdesc_required(struct nicvf *nic, struct sk_buff *skb)
{
	int subdesc_cnt = MIN_SQ_DESC_PER_PKT_XMIT;

	if (skb_shinfo(skb)->gso_size && !nic->hw_tso) {
		subdesc_cnt = nicvf_tso_count_subdescs(skb);
		return subdesc_cnt;
	}

	if (skb_shinfo(skb)->nr_frags)
		subdesc_cnt += skb_shinfo(skb)->nr_frags;

	return subdesc_cnt;
}

/* Add SQ HEADER subdescriptor.
 * First subdescriptor for every send descriptor.
 */
static inline void
nicvf_sq_add_hdr_subdesc(struct nicvf *nic, struct snd_queue *sq, int qentry,
			 int subdesc_cnt, struct sk_buff *skb, int len)
{
	int proto;
	struct sq_hdr_subdesc *hdr;

	hdr = (struct sq_hdr_subdesc *)GET_SQ_DESC(sq, qentry);
	sq->skbuff[qentry] = (u64)skb;

	memset(hdr, 0, SND_QUEUE_DESC_SIZE);
	hdr->subdesc_type = SQ_DESC_TYPE_HEADER;
	/* Enable notification via CQE after processing SQE */
	hdr->post_cqe = 1;
	/* No of subdescriptors following this */
	hdr->subdesc_cnt = subdesc_cnt;
	hdr->tot_len = len;

	/* Offload checksum calculation to HW */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		hdr->csum_l3 = 1; /* Enable IP csum calculation */
		hdr->l3_offset = skb_network_offset(skb);
		hdr->l4_offset = skb_transport_offset(skb);

		proto = ip_hdr(skb)->protocol;
		switch (proto) {
		case IPPROTO_TCP:
			hdr->csum_l4 = SEND_L4_CSUM_TCP;
			break;
		case IPPROTO_UDP:
			hdr->csum_l4 = SEND_L4_CSUM_UDP;
			break;
		case IPPROTO_SCTP:
			hdr->csum_l4 = SEND_L4_CSUM_SCTP;
			break;
		}
	}

	if (nic->hw_tso && skb_shinfo(skb)->gso_size) {
		hdr->tso = 1;
		hdr->tso_start = skb_transport_offset(skb) + tcp_hdrlen(skb);
		hdr->tso_max_paysize = skb_shinfo(skb)->gso_size;
		/* For non-tunneled pkts, point this to L2 ethertype */
		hdr->inner_l3_offset = skb_network_offset(skb) - 2;
		nic->drv_stats.tx_tso++;
	}
}

/* SQ GATHER subdescriptor
 * Must follow HDR descriptor
 */
static inline void nicvf_sq_add_gather_subdesc(struct snd_queue *sq, int qentry,
					       int size, u64 data)
{
	struct sq_gather_subdesc *gather;

	qentry &= (sq->dmem.q_len - 1);
	gather = (struct sq_gather_subdesc *)GET_SQ_DESC(sq, qentry);

	memset(gather, 0, SND_QUEUE_DESC_SIZE);
	gather->subdesc_type = SQ_DESC_TYPE_GATHER;
	gather->ld_type = NIC_SEND_LD_TYPE_E_LDD;
	gather->size = size;
	gather->addr = data;
}

/* Segment a TSO packet into 'gso_size' segments and append
 * them to SQ for transfer
 */
static int nicvf_sq_append_tso(struct nicvf *nic, struct snd_queue *sq,
			       int sq_num, int qentry, struct sk_buff *skb)
{
	struct tso_t tso;
	int seg_subdescs = 0, desc_cnt = 0;
	int seg_len, total_len, data_left;
	int hdr_qentry = qentry;
	int hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);

	tso_start(skb, &tso);
	total_len = skb->len - hdr_len;
	while (total_len > 0) {
		char *hdr;

		/* Save Qentry for adding HDR_SUBDESC at the end */
		hdr_qentry = qentry;

		data_left = min_t(int, skb_shinfo(skb)->gso_size, total_len);
		total_len -= data_left;

		/* Add segment's header */
		qentry = nicvf_get_nxt_sqentry(sq, qentry);
		hdr = sq->tso_hdrs + qentry * TSO_HEADER_SIZE;
		tso_build_hdr(skb, hdr, &tso, data_left, total_len == 0);
		nicvf_sq_add_gather_subdesc(sq, qentry, hdr_len,
					    sq->tso_hdrs_phys +
					    qentry * TSO_HEADER_SIZE);
		/* HDR_SUDESC + GATHER */
		seg_subdescs = 2;
		seg_len = hdr_len;

		/* Add segment's payload fragments */
		while (data_left > 0) {
			int size;

			size = min_t(int, tso.size, data_left);

			qentry = nicvf_get_nxt_sqentry(sq, qentry);
			nicvf_sq_add_gather_subdesc(sq, qentry, size,
						    virt_to_phys(tso.data));
			seg_subdescs++;
			seg_len += size;

			data_left -= size;
			tso_build_data(skb, &tso, size);
		}
		nicvf_sq_add_hdr_subdesc(nic, sq, hdr_qentry,
					 seg_subdescs - 1, skb, seg_len);
		sq->skbuff[hdr_qentry] = (u64)NULL;
		qentry = nicvf_get_nxt_sqentry(sq, qentry);

		desc_cnt += seg_subdescs;
	}
	/* Save SKB in the last segment for freeing */
	sq->skbuff[hdr_qentry] = (u64)skb;

	/* make sure all memory stores are done before ringing doorbell */
	smp_wmb();

	/* Inform HW to xmit all TSO segments */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_DOOR,
			      sq_num, desc_cnt);
	nic->drv_stats.tx_tso++;
	return 1;
}

/* Append an skb to a SQ for packet transfer. */
int nicvf_sq_append_skb(struct nicvf *nic, struct sk_buff *skb)
{
	int i, size;
	int subdesc_cnt;
	int sq_num, qentry;
	struct queue_set *qs;
	struct snd_queue *sq;

	sq_num = skb_get_queue_mapping(skb);
	if (sq_num >= MAX_SND_QUEUES_PER_QS) {
		/* Get secondary Qset's SQ structure */
		i = sq_num / MAX_SND_QUEUES_PER_QS;
		if (!nic->snicvf[i - 1]) {
			netdev_warn(nic->netdev,
				    "Secondary Qset#%d's ptr not initialized\n",
				    i - 1);
			return 1;
		}
		nic = (struct nicvf *)nic->snicvf[i - 1];
		sq_num = sq_num % MAX_SND_QUEUES_PER_QS;
	}

	qs = nic->qs;
	sq = &qs->sq[sq_num];

	subdesc_cnt = nicvf_sq_subdesc_required(nic, skb);
	if (subdesc_cnt > atomic_read(&sq->free_cnt))
		goto append_fail;

	qentry = nicvf_get_sq_desc(sq, subdesc_cnt);

	/* Check if its a TSO packet */
	if (skb_shinfo(skb)->gso_size && !nic->hw_tso)
		return nicvf_sq_append_tso(nic, sq, sq_num, qentry, skb);

	/* Add SQ header subdesc */
	nicvf_sq_add_hdr_subdesc(nic, sq, qentry, subdesc_cnt - 1,
				 skb, skb->len);

	/* Add SQ gather subdescs */
	qentry = nicvf_get_nxt_sqentry(sq, qentry);
	size = skb_is_nonlinear(skb) ? skb_headlen(skb) : skb->len;
	nicvf_sq_add_gather_subdesc(sq, qentry, size, virt_to_phys(skb->data));

	/* Check for scattered buffer */
	if (!skb_is_nonlinear(skb))
		goto doorbell;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const struct skb_frag_struct *frag;

		frag = &skb_shinfo(skb)->frags[i];

		qentry = nicvf_get_nxt_sqentry(sq, qentry);
		size = skb_frag_size(frag);
		nicvf_sq_add_gather_subdesc(sq, qentry, size,
					    virt_to_phys(
					    skb_frag_address(frag)));
	}

doorbell:
	/* make sure all memory stores are done before ringing doorbell */
	smp_wmb();

	/* Inform HW to xmit new packet */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_DOOR,
			      sq_num, subdesc_cnt);
	return 1;

append_fail:
	/* Use original PCI dev for debug log */
	nic = nic->pnicvf;
	netdev_dbg(nic->netdev, "Not enough SQ descriptors to xmit pkt\n");
	return 0;
}

static inline unsigned frag_num(unsigned i)
{
#ifdef __BIG_ENDIAN
	return (i & ~3) + 3 - (i & 3);
#else
	return i;
#endif
}

/* Returns SKB for a received packet */
struct sk_buff *nicvf_get_rcv_skb(struct nicvf *nic, struct cqe_rx_t *cqe_rx)
{
	int frag;
	int payload_len = 0;
	struct sk_buff *skb = NULL;
	struct sk_buff *skb_frag = NULL;
	struct sk_buff *prev_frag = NULL;
	u16 *rb_lens = NULL;
	u64 *rb_ptrs = NULL;

	rb_lens = (void *)cqe_rx + (3 * sizeof(u64));
	rb_ptrs = (void *)cqe_rx + (6 * sizeof(u64));

	netdev_dbg(nic->netdev, "%s rb_cnt %d rb0_ptr %llx rb0_sz %d\n",
		   __func__, cqe_rx->rb_cnt, cqe_rx->rb0_ptr, cqe_rx->rb0_sz);

	for (frag = 0; frag < cqe_rx->rb_cnt; frag++) {
		payload_len = rb_lens[frag_num(frag)];
		if (!frag) {
			/* First fragment */
			skb = nicvf_rb_ptr_to_skb(nic,
						  *rb_ptrs - cqe_rx->align_pad,
						  payload_len);
			if (!skb)
				return NULL;
			skb_reserve(skb, cqe_rx->align_pad);
			skb_put(skb, payload_len);
		} else {
			/* Add fragments */
			skb_frag = nicvf_rb_ptr_to_skb(nic, *rb_ptrs,
						       payload_len);
			if (!skb_frag) {
				dev_kfree_skb(skb);
				return NULL;
			}

			if (!skb_shinfo(skb)->frag_list)
				skb_shinfo(skb)->frag_list = skb_frag;
			else
				prev_frag->next = skb_frag;

			prev_frag = skb_frag;
			skb->len += payload_len;
			skb->data_len += payload_len;
			skb_frag->len = payload_len;
		}
		/* Next buffer pointer */
		rb_ptrs++;
	}
	return skb;
}

static u64 nicvf_int_type_to_mask(int int_type, int q_idx)
{
	u64 reg_val;

	switch (int_type) {
	case NICVF_INTR_CQ:
		reg_val = ((1ULL << q_idx) << NICVF_INTR_CQ_SHIFT);
		break;
	case NICVF_INTR_SQ:
		reg_val = ((1ULL << q_idx) << NICVF_INTR_SQ_SHIFT);
		break;
	case NICVF_INTR_RBDR:
		reg_val = ((1ULL << q_idx) << NICVF_INTR_RBDR_SHIFT);
		break;
	case NICVF_INTR_PKT_DROP:
		reg_val = (1ULL << NICVF_INTR_PKT_DROP_SHIFT);
		break;
	case NICVF_INTR_TCP_TIMER:
		reg_val = (1ULL << NICVF_INTR_TCP_TIMER_SHIFT);
		break;
	case NICVF_INTR_MBOX:
		reg_val = (1ULL << NICVF_INTR_MBOX_SHIFT);
		break;
	case NICVF_INTR_QS_ERR:
		reg_val = (1ULL << NICVF_INTR_QS_ERR_SHIFT);
		break;
	default:
		reg_val = 0;
	}

	return reg_val;
}

/* Enable interrupt */
void nicvf_enable_intr(struct nicvf *nic, int int_type, int q_idx)
{
	u64 mask = nicvf_int_type_to_mask(int_type, q_idx);

	if (!mask) {
		netdev_dbg(nic->netdev,
			   "Failed to enable interrupt: unknown type\n");
		return;
	}
	nicvf_reg_write(nic, NIC_VF_ENA_W1S,
			nicvf_reg_read(nic, NIC_VF_ENA_W1S) | mask);
}

/* Disable interrupt */
void nicvf_disable_intr(struct nicvf *nic, int int_type, int q_idx)
{
	u64 mask = nicvf_int_type_to_mask(int_type, q_idx);

	if (!mask) {
		netdev_dbg(nic->netdev,
			   "Failed to disable interrupt: unknown type\n");
		return;
	}

	nicvf_reg_write(nic, NIC_VF_ENA_W1C, mask);
}

/* Clear interrupt */
void nicvf_clear_intr(struct nicvf *nic, int int_type, int q_idx)
{
	u64 mask = nicvf_int_type_to_mask(int_type, q_idx);

	if (!mask) {
		netdev_dbg(nic->netdev,
			   "Failed to clear interrupt: unknown type\n");
		return;
	}

	nicvf_reg_write(nic, NIC_VF_INT, mask);
}

/* Check if interrupt is enabled */
int nicvf_is_intr_enabled(struct nicvf *nic, int int_type, int q_idx)
{
	u64 mask = nicvf_int_type_to_mask(int_type, q_idx);
	/* If interrupt type is unknown, we treat it disabled. */
	if (!mask) {
		netdev_dbg(nic->netdev,
			   "Failed to check interrupt enable: unknown type\n");
		return 0;
	}

	return mask & nicvf_reg_read(nic, NIC_VF_ENA_W1S);
}

void nicvf_update_rq_stats(struct nicvf *nic, int rq_idx)
{
	struct rcv_queue *rq;

#define GET_RQ_STATS(reg) \
	nicvf_reg_read(nic, NIC_QSET_RQ_0_7_STAT_0_1 |\
			    (rq_idx << NIC_Q_NUM_SHIFT) | (reg << 3))

	rq = &nic->qs->rq[rq_idx];
	rq->stats.bytes = GET_RQ_STATS(RQ_SQ_STATS_OCTS);
	rq->stats.pkts = GET_RQ_STATS(RQ_SQ_STATS_PKTS);
}

void nicvf_update_sq_stats(struct nicvf *nic, int sq_idx)
{
	struct snd_queue *sq;

#define GET_SQ_STATS(reg) \
	nicvf_reg_read(nic, NIC_QSET_SQ_0_7_STAT_0_1 |\
			    (sq_idx << NIC_Q_NUM_SHIFT) | (reg << 3))

	sq = &nic->qs->sq[sq_idx];
	sq->stats.bytes = GET_SQ_STATS(RQ_SQ_STATS_OCTS);
	sq->stats.pkts = GET_SQ_STATS(RQ_SQ_STATS_PKTS);
}

/* Check for errors in the receive cmp.queue entry */
int nicvf_check_cqe_rx_errs(struct nicvf *nic, struct cqe_rx_t *cqe_rx)
{
	struct nicvf_hw_stats *stats = &nic->hw_stats;

	if (!cqe_rx->err_level && !cqe_rx->err_opcode)
		return 0;

	if (netif_msg_rx_err(nic))
		netdev_err(nic->netdev,
			   "%s: RX error CQE err_level 0x%x err_opcode 0x%x\n",
			   nic->netdev->name,
			   cqe_rx->err_level, cqe_rx->err_opcode);

	switch (cqe_rx->err_opcode) {
	case CQ_RX_ERROP_RE_PARTIAL:
		stats->rx_bgx_truncated_pkts++;
		break;
	case CQ_RX_ERROP_RE_JABBER:
		stats->rx_jabber_errs++;
		break;
	case CQ_RX_ERROP_RE_FCS:
		stats->rx_fcs_errs++;
		break;
	case CQ_RX_ERROP_RE_RX_CTL:
		stats->rx_bgx_errs++;
		break;
	case CQ_RX_ERROP_PREL2_ERR:
		stats->rx_prel2_errs++;
		break;
	case CQ_RX_ERROP_L2_MAL:
		stats->rx_l2_hdr_malformed++;
		break;
	case CQ_RX_ERROP_L2_OVERSIZE:
		stats->rx_oversize++;
		break;
	case CQ_RX_ERROP_L2_UNDERSIZE:
		stats->rx_undersize++;
		break;
	case CQ_RX_ERROP_L2_LENMISM:
		stats->rx_l2_len_mismatch++;
		break;
	case CQ_RX_ERROP_L2_PCLP:
		stats->rx_l2_pclp++;
		break;
	case CQ_RX_ERROP_IP_NOT:
		stats->rx_ip_ver_errs++;
		break;
	case CQ_RX_ERROP_IP_CSUM_ERR:
		stats->rx_ip_csum_errs++;
		break;
	case CQ_RX_ERROP_IP_MAL:
		stats->rx_ip_hdr_malformed++;
		break;
	case CQ_RX_ERROP_IP_MALD:
		stats->rx_ip_payload_malformed++;
		break;
	case CQ_RX_ERROP_IP_HOP:
		stats->rx_ip_ttl_errs++;
		break;
	case CQ_RX_ERROP_L3_PCLP:
		stats->rx_l3_pclp++;
		break;
	case CQ_RX_ERROP_L4_MAL:
		stats->rx_l4_malformed++;
		break;
	case CQ_RX_ERROP_L4_CHK:
		stats->rx_l4_csum_errs++;
		break;
	case CQ_RX_ERROP_UDP_LEN:
		stats->rx_udp_len_errs++;
		break;
	case CQ_RX_ERROP_L4_PORT:
		stats->rx_l4_port_errs++;
		break;
	case CQ_RX_ERROP_TCP_FLAG:
		stats->rx_tcp_flag_errs++;
		break;
	case CQ_RX_ERROP_TCP_OFFSET:
		stats->rx_tcp_offset_errs++;
		break;
	case CQ_RX_ERROP_L4_PCLP:
		stats->rx_l4_pclp++;
		break;
	case CQ_RX_ERROP_RBDR_TRUNC:
		stats->rx_truncated_pkts++;
		break;
	}

	return 1;
}

/* Check for errors in the send cmp.queue entry */
int nicvf_check_cqe_tx_errs(struct nicvf *nic,
			    struct cmp_queue *cq, struct cqe_send_t *cqe_tx)
{
	struct cmp_queue_stats *stats = &cq->stats;

	switch (cqe_tx->send_status) {
	case CQ_TX_ERROP_GOOD:
		stats->tx.good++;
		return 0;
	case CQ_TX_ERROP_DESC_FAULT:
		stats->tx.desc_fault++;
		break;
	case CQ_TX_ERROP_HDR_CONS_ERR:
		stats->tx.hdr_cons_err++;
		break;
	case CQ_TX_ERROP_SUBDC_ERR:
		stats->tx.subdesc_err++;
		break;
	case CQ_TX_ERROP_IMM_SIZE_OFLOW:
		stats->tx.imm_size_oflow++;
		break;
	case CQ_TX_ERROP_DATA_SEQUENCE_ERR:
		stats->tx.data_seq_err++;
		break;
	case CQ_TX_ERROP_MEM_SEQUENCE_ERR:
		stats->tx.mem_seq_err++;
		break;
	case CQ_TX_ERROP_LOCK_VIOL:
		stats->tx.lock_viol++;
		break;
	case CQ_TX_ERROP_DATA_FAULT:
		stats->tx.data_fault++;
		break;
	case CQ_TX_ERROP_TSTMP_CONFLICT:
		stats->tx.tstmp_conflict++;
		break;
	case CQ_TX_ERROP_TSTMP_TIMEOUT:
		stats->tx.tstmp_timeout++;
		break;
	case CQ_TX_ERROP_MEM_FAULT:
		stats->tx.mem_fault++;
		break;
	case CQ_TX_ERROP_CK_OVERLAP:
		stats->tx.csum_overlap++;
		break;
	case CQ_TX_ERROP_CK_OFLOW:
		stats->tx.csum_overflow++;
		break;
	}

	return 1;
}
