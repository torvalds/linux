// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Cavium, Inc.
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/etherdevice.h>
#include <linux/iommu.h>
#include <net/ip.h>
#include <net/tso.h>

#include "nic_reg.h"
#include "nic.h"
#include "q_struct.h"
#include "nicvf_queues.h"

static inline void nicvf_sq_add_gather_subdesc(struct snd_queue *sq, int qentry,
					       int size, u64 data);
static void nicvf_get_page(struct nicvf *nic)
{
	if (!nic->rb_pageref || !nic->rb_page)
		return;

	page_ref_add(nic->rb_page, nic->rb_pageref);
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
	dmem->unalign_base = dma_alloc_coherent(&nic->pdev->dev, dmem->size,
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

#define XDP_PAGE_REFCNT_REFILL 256

/* Allocate a new page or recycle one if possible
 *
 * We cannot optimize dma mapping here, since
 * 1. It's only one RBDR ring for 8 Rx queues.
 * 2. CQE_RX gives address of the buffer where pkt has been DMA'ed
 *    and not idx into RBDR ring, so can't refer to saved info.
 * 3. There are multiple receive buffers per page
 */
static inline struct pgcache *nicvf_alloc_page(struct nicvf *nic,
					       struct rbdr *rbdr, gfp_t gfp)
{
	int ref_count;
	struct page *page = NULL;
	struct pgcache *pgcache, *next;

	/* Check if page is already allocated */
	pgcache = &rbdr->pgcache[rbdr->pgidx];
	page = pgcache->page;
	/* Check if page can be recycled */
	if (page) {
		ref_count = page_ref_count(page);
		/* This page can be recycled if internal ref_count and page's
		 * ref_count are equal, indicating that the page has been used
		 * once for packet transmission. For non-XDP mode, internal
		 * ref_count is always '1'.
		 */
		if (rbdr->is_xdp) {
			if (ref_count == pgcache->ref_count)
				pgcache->ref_count--;
			else
				page = NULL;
		} else if (ref_count != 1) {
			page = NULL;
		}
	}

	if (!page) {
		page = alloc_pages(gfp | __GFP_COMP | __GFP_NOWARN, 0);
		if (!page)
			return NULL;

		this_cpu_inc(nic->pnicvf->drv_stats->page_alloc);

		/* Check for space */
		if (rbdr->pgalloc >= rbdr->pgcnt) {
			/* Page can still be used */
			nic->rb_page = page;
			return NULL;
		}

		/* Save the page in page cache */
		pgcache->page = page;
		pgcache->dma_addr = 0;
		pgcache->ref_count = 0;
		rbdr->pgalloc++;
	}

	/* Take additional page references for recycling */
	if (rbdr->is_xdp) {
		/* Since there is single RBDR (i.e single core doing
		 * page recycling) per 8 Rx queues, in XDP mode adjusting
		 * page references atomically is the biggest bottleneck, so
		 * take bunch of references at a time.
		 *
		 * So here, below reference counts defer by '1'.
		 */
		if (!pgcache->ref_count) {
			pgcache->ref_count = XDP_PAGE_REFCNT_REFILL;
			page_ref_add(page, XDP_PAGE_REFCNT_REFILL);
		}
	} else {
		/* In non-XDP case, single 64K page is divided across multiple
		 * receive buffers, so cost of recycling is less anyway.
		 * So we can do with just one extra reference.
		 */
		page_ref_add(page, 1);
	}

	rbdr->pgidx++;
	rbdr->pgidx &= (rbdr->pgcnt - 1);

	/* Prefetch refcount of next page in page cache */
	next = &rbdr->pgcache[rbdr->pgidx];
	page = next->page;
	if (page)
		prefetch(&page->_refcount);

	return pgcache;
}

/* Allocate buffer for packet reception */
static inline int nicvf_alloc_rcv_buffer(struct nicvf *nic, struct rbdr *rbdr,
					 gfp_t gfp, u32 buf_len, u64 *rbuf)
{
	struct pgcache *pgcache = NULL;

	/* Check if request can be accomodated in previous allocated page.
	 * But in XDP mode only one buffer per page is permitted.
	 */
	if (!rbdr->is_xdp && nic->rb_page &&
	    ((nic->rb_page_offset + buf_len) <= PAGE_SIZE)) {
		nic->rb_pageref++;
		goto ret;
	}

	nicvf_get_page(nic);
	nic->rb_page = NULL;

	/* Get new page, either recycled or new one */
	pgcache = nicvf_alloc_page(nic, rbdr, gfp);
	if (!pgcache && !nic->rb_page) {
		this_cpu_inc(nic->pnicvf->drv_stats->rcv_buffer_alloc_failures);
		return -ENOMEM;
	}

	nic->rb_page_offset = 0;

	/* Reserve space for header modifications by BPF program */
	if (rbdr->is_xdp)
		buf_len += XDP_PACKET_HEADROOM;

	/* Check if it's recycled */
	if (pgcache)
		nic->rb_page = pgcache->page;
ret:
	if (rbdr->is_xdp && pgcache && pgcache->dma_addr) {
		*rbuf = pgcache->dma_addr;
	} else {
		/* HW will ensure data coherency, CPU sync not required */
		*rbuf = (u64)dma_map_page_attrs(&nic->pdev->dev, nic->rb_page,
						nic->rb_page_offset, buf_len,
						DMA_FROM_DEVICE,
						DMA_ATTR_SKIP_CPU_SYNC);
		if (dma_mapping_error(&nic->pdev->dev, (dma_addr_t)*rbuf)) {
			if (!nic->rb_page_offset)
				__free_pages(nic->rb_page, 0);
			nic->rb_page = NULL;
			return -ENOMEM;
		}
		if (pgcache)
			pgcache->dma_addr = *rbuf + XDP_PACKET_HEADROOM;
		nic->rb_page_offset += buf_len;
	}

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
	u64 rbuf;
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
	rbdr->head = 0;
	rbdr->tail = 0;

	/* Initialize page recycling stuff.
	 *
	 * Can't use single buffer per page especially with 64K pages.
	 * On embedded platforms i.e 81xx/83xx available memory itself
	 * is low and minimum ring size of RBDR is 8K, that takes away
	 * lots of memory.
	 *
	 * But for XDP it has to be a single buffer per page.
	 */
	if (!nic->pnicvf->xdp_prog) {
		rbdr->pgcnt = ring_len / (PAGE_SIZE / buf_size);
		rbdr->is_xdp = false;
	} else {
		rbdr->pgcnt = ring_len;
		rbdr->is_xdp = true;
	}
	rbdr->pgcnt = roundup_pow_of_two(rbdr->pgcnt);
	rbdr->pgcache = kcalloc(rbdr->pgcnt, sizeof(*rbdr->pgcache),
				GFP_KERNEL);
	if (!rbdr->pgcache)
		return -ENOMEM;
	rbdr->pgidx = 0;
	rbdr->pgalloc = 0;

	nic->rb_page = NULL;
	for (idx = 0; idx < ring_len; idx++) {
		err = nicvf_alloc_rcv_buffer(nic, rbdr, GFP_KERNEL,
					     RCV_FRAG_LEN, &rbuf);
		if (err) {
			/* To free already allocated and mapped ones */
			rbdr->tail = idx - 1;
			return err;
		}

		desc = GET_RBDR_DESC(rbdr, idx);
		desc->buf_addr = rbuf & ~(NICVF_RCV_BUF_ALIGN_BYTES - 1);
	}

	nicvf_get_page(nic);

	return 0;
}

/* Free RBDR ring and its receive buffers */
static void nicvf_free_rbdr(struct nicvf *nic, struct rbdr *rbdr)
{
	int head, tail;
	u64 buf_addr, phys_addr;
	struct pgcache *pgcache;
	struct rbdr_entry_t *desc;

	if (!rbdr)
		return;

	rbdr->enable = false;
	if (!rbdr->dmem.base)
		return;

	head = rbdr->head;
	tail = rbdr->tail;

	/* Release page references */
	while (head != tail) {
		desc = GET_RBDR_DESC(rbdr, head);
		buf_addr = desc->buf_addr;
		phys_addr = nicvf_iova_to_phys(nic, buf_addr);
		dma_unmap_page_attrs(&nic->pdev->dev, buf_addr, RCV_FRAG_LEN,
				     DMA_FROM_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
		if (phys_addr)
			put_page(virt_to_page(phys_to_virt(phys_addr)));
		head++;
		head &= (rbdr->dmem.q_len - 1);
	}
	/* Release buffer of tail desc */
	desc = GET_RBDR_DESC(rbdr, tail);
	buf_addr = desc->buf_addr;
	phys_addr = nicvf_iova_to_phys(nic, buf_addr);
	dma_unmap_page_attrs(&nic->pdev->dev, buf_addr, RCV_FRAG_LEN,
			     DMA_FROM_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
	if (phys_addr)
		put_page(virt_to_page(phys_to_virt(phys_addr)));

	/* Sync page cache info */
	smp_rmb();

	/* Release additional page references held for recycling */
	head = 0;
	while (head < rbdr->pgcnt) {
		pgcache = &rbdr->pgcache[head];
		if (pgcache->page && page_ref_count(pgcache->page) != 0) {
			if (rbdr->is_xdp) {
				page_ref_sub(pgcache->page,
					     pgcache->ref_count - 1);
			}
			put_page(pgcache->page);
		}
		head++;
	}

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
	u64 rbuf;
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

	/* Sync page cache info */
	smp_rmb();

	/* Start filling descs from tail */
	tail = nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_TAIL, rbdr_idx) >> 3;
	while (refill_rb_cnt) {
		tail++;
		tail &= (rbdr->dmem.q_len - 1);

		if (nicvf_alloc_rcv_buffer(nic, rbdr, gfp, RCV_FRAG_LEN, &rbuf))
			break;

		desc = GET_RBDR_DESC(rbdr, tail);
		desc->buf_addr = rbuf & ~(NICVF_RCV_BUF_ALIGN_BYTES - 1);
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
	if (!nic->rb_alloc_fail && rbdr->enable &&
	    netif_running(nic->pnicvf->netdev))
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
void nicvf_rbdr_task(struct tasklet_struct *t)
{
	struct nicvf *nic = from_tasklet(nic, t, rbdr_task);

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
				struct snd_queue *sq, int q_len, int qidx)
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
	sq->thresh = SND_QUEUE_THRESH;

	/* Check if this SQ is a XDP TX queue */
	if (nic->sqs_mode)
		qidx += ((nic->sqs_id + 1) * MAX_SND_QUEUES_PER_QS);
	if (qidx < nic->pnicvf->xdp_tx_queues) {
		/* Alloc memory to save page pointers for XDP_TX */
		sq->xdp_page = kcalloc(q_len, sizeof(u64), GFP_KERNEL);
		if (!sq->xdp_page)
			return -ENOMEM;
		sq->xdp_desc_cnt = 0;
		sq->xdp_free_cnt = q_len - 1;
		sq->is_xdp = true;
	} else {
		sq->xdp_page = NULL;
		sq->xdp_desc_cnt = 0;
		sq->xdp_free_cnt = 0;
		sq->is_xdp = false;

		atomic_set(&sq->free_cnt, q_len - 1);

		/* Preallocate memory for TSO segment's header */
		sq->tso_hdrs = dma_alloc_coherent(&nic->pdev->dev,
						  q_len * TSO_HEADER_SIZE,
						  &sq->tso_hdrs_phys,
						  GFP_KERNEL);
		if (!sq->tso_hdrs)
			return -ENOMEM;
	}

	return 0;
}

void nicvf_unmap_sndq_buffers(struct nicvf *nic, struct snd_queue *sq,
			      int hdr_sqe, u8 subdesc_cnt)
{
	u8 idx;
	struct sq_gather_subdesc *gather;

	/* Unmap DMA mapped skb data buffers */
	for (idx = 0; idx < subdesc_cnt; idx++) {
		hdr_sqe++;
		hdr_sqe &= (sq->dmem.q_len - 1);
		gather = (struct sq_gather_subdesc *)GET_SQ_DESC(sq, hdr_sqe);
		/* HW will ensure data coherency, CPU sync not required */
		dma_unmap_page_attrs(&nic->pdev->dev, gather->addr,
				     gather->size, DMA_TO_DEVICE,
				     DMA_ATTR_SKIP_CPU_SYNC);
	}
}

static void nicvf_free_snd_queue(struct nicvf *nic, struct snd_queue *sq)
{
	struct sk_buff *skb;
	struct page *page;
	struct sq_hdr_subdesc *hdr;
	struct sq_hdr_subdesc *tso_sqe;

	if (!sq)
		return;
	if (!sq->dmem.base)
		return;

	if (sq->tso_hdrs) {
		dma_free_coherent(&nic->pdev->dev,
				  sq->dmem.q_len * TSO_HEADER_SIZE,
				  sq->tso_hdrs, sq->tso_hdrs_phys);
		sq->tso_hdrs = NULL;
	}

	/* Free pending skbs in the queue */
	smp_rmb();
	while (sq->head != sq->tail) {
		skb = (struct sk_buff *)sq->skbuff[sq->head];
		if (!skb || !sq->xdp_page)
			goto next;

		page = (struct page *)sq->xdp_page[sq->head];
		if (!page)
			goto next;
		else
			put_page(page);

		hdr = (struct sq_hdr_subdesc *)GET_SQ_DESC(sq, sq->head);
		/* Check for dummy descriptor used for HW TSO offload on 88xx */
		if (hdr->dont_send) {
			/* Get actual TSO descriptors and unmap them */
			tso_sqe =
			 (struct sq_hdr_subdesc *)GET_SQ_DESC(sq, hdr->rsvd2);
			nicvf_unmap_sndq_buffers(nic, sq, hdr->rsvd2,
						 tso_sqe->subdesc_cnt);
		} else {
			nicvf_unmap_sndq_buffers(nic, sq, sq->head,
						 hdr->subdesc_cnt);
		}
		if (skb)
			dev_kfree_skb_any(skb);
next:
		sq->head++;
		sq->head &= (sq->dmem.q_len - 1);
	}
	kfree(sq->skbuff);
	kfree(sq->xdp_page);
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

static void nicvf_reset_rcv_queue_stats(struct nicvf *nic)
{
	union nic_mbx mbx = {};

	/* Reset all RQ/SQ and VF stats */
	mbx.reset_stat.msg = NIC_MBOX_MSG_RESET_STAT_COUNTER;
	mbx.reset_stat.rx_stat_mask = 0x3FFF;
	mbx.reset_stat.tx_stat_mask = 0x1F;
	mbx.reset_stat.rq_stat_mask = 0xFFFF;
	mbx.reset_stat.sq_stat_mask = 0xFFFF;
	nicvf_send_msg_to_pf(nic, &mbx);
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
		xdp_rxq_info_unreg(&rq->xdp_rxq);
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

	/* Driver have no proper error path for failed XDP RX-queue info reg */
	WARN_ON(xdp_rxq_info_reg(&rq->xdp_rxq, nic->netdev, qidx) < 0);

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
	mbx.rq.cfg = BIT_ULL(63) | BIT_ULL(62) |
		     (RQ_PASS_RBDR_LVL << 16) | (RQ_PASS_CQ_LVL << 8) |
		     (qs->vnic_id << 0);
	nicvf_send_msg_to_pf(nic, &mbx);

	/* RQ drop config
	 * Enable CQ drop to reserve sufficient CQEs for all tx packets
	 */
	mbx.rq.msg = NIC_MBOX_MSG_RQ_DROP_CFG;
	mbx.rq.cfg = BIT_ULL(63) | BIT_ULL(62) |
		     (RQ_PASS_RBDR_LVL << 40) | (RQ_DROP_RBDR_LVL << 32) |
		     (RQ_PASS_CQ_LVL << 16) | (RQ_DROP_CQ_LVL << 8);
	nicvf_send_msg_to_pf(nic, &mbx);

	if (!nic->sqs_mode && (qidx == 0)) {
		/* Enable checking L3/L4 length and TCP/UDP checksums
		 * Also allow IPv6 pkts with zero UDP checksum.
		 */
		nicvf_queue_reg_write(nic, NIC_QSET_RQ_GEN_CFG, 0,
				      (BIT(24) | BIT(23) | BIT(21) | BIT(20)));
		nicvf_config_vlan_stripping(nic, nic->netdev->features);
	}

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
	cq_cfg.qsize = ilog2(qs->cq_len >> 10);
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
	sq_cfg.qsize = ilog2(qs->sq_len >> 10);
	sq_cfg.tstmp_bgx_intf = 0;
	/* CQ's level at which HW will stop processing SQEs to avoid
	 * transmitting a pkt with no space in CQ to post CQE_TX.
	 */
	sq_cfg.cq_limit = (CMP_QUEUE_PIPELINE_RSVD * 256) / qs->cq_len;
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
		/* Enable Tx timestamping capability */
		if (nic->ptp_clock)
			qs_cfg->send_tstmp_ena = 1;
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
		if (nicvf_init_snd_queue(nic, &qs->sq[qidx], qs->sq_len, qidx))
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
	qs->rbdr_cnt = DEFAULT_RBDR_CNT;
	qs->rq_cnt = min_t(u8, MAX_RCV_QUEUES_PER_QS, num_online_cpus());
	qs->sq_cnt = min_t(u8, MAX_SND_QUEUES_PER_QS, num_online_cpus());
	qs->cq_cnt = max_t(u8, qs->rq_cnt, qs->sq_cnt);

	/* Set queue lengths */
	qs->rbdr_len = RCV_BUF_COUNT;
	qs->sq_len = SND_QUEUE_LEN;
	qs->cq_len = CMP_QUEUE_LEN;

	nic->rx_queues = qs->rq_cnt;
	nic->tx_queues = qs->sq_cnt;
	nic->xdp_tx_queues = 0;

	return 0;
}

int nicvf_config_data_transfer(struct nicvf *nic, bool enable)
{
	bool disable = false;
	struct queue_set *qs = nic->qs;
	struct queue_set *pqs = nic->pnicvf->qs;
	int qidx;

	if (!qs)
		return 0;

	/* Take primary VF's queue lengths.
	 * This is needed to take queue lengths set from ethtool
	 * into consideration.
	 */
	if (nic->sqs_mode && pqs) {
		qs->cq_len = pqs->cq_len;
		qs->sq_len = pqs->sq_len;
	}

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

	/* Reset RXQ's stats.
	 * SQ's stats will get reset automatically once SQ is reset.
	 */
	nicvf_reset_rcv_queue_stats(nic);

	return 0;
}

/* Get a free desc from SQ
 * returns descriptor ponter & descriptor number
 */
static inline int nicvf_get_sq_desc(struct snd_queue *sq, int desc_cnt)
{
	int qentry;

	qentry = sq->tail;
	if (!sq->is_xdp)
		atomic_sub(desc_cnt, &sq->free_cnt);
	else
		sq->xdp_free_cnt -= desc_cnt;
	sq->tail += desc_cnt;
	sq->tail &= (sq->dmem.q_len - 1);

	return qentry;
}

/* Rollback to previous tail pointer when descriptors not used */
static inline void nicvf_rollback_sq_desc(struct snd_queue *sq,
					  int qentry, int desc_cnt)
{
	sq->tail = qentry;
	atomic_add(desc_cnt, &sq->free_cnt);
}

/* Free descriptor back to SQ for future use */
void nicvf_put_sq_desc(struct snd_queue *sq, int desc_cnt)
{
	if (!sq->is_xdp)
		atomic_add(desc_cnt, &sq->free_cnt);
	else
		sq->xdp_free_cnt += desc_cnt;
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
	u64 head;
	struct sk_buff *skb;
	struct nicvf *nic = netdev_priv(netdev);
	struct sq_hdr_subdesc *hdr;

	head = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_HEAD, qidx) >> 4;
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

/* XDP Transmit APIs */
void nicvf_xdp_sq_doorbell(struct nicvf *nic,
			   struct snd_queue *sq, int sq_num)
{
	if (!sq->xdp_desc_cnt)
		return;

	/* make sure all memory stores are done before ringing doorbell */
	wmb();

	/* Inform HW to xmit all TSO segments */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_DOOR,
			      sq_num, sq->xdp_desc_cnt);
	sq->xdp_desc_cnt = 0;
}

static inline void
nicvf_xdp_sq_add_hdr_subdesc(struct snd_queue *sq, int qentry,
			     int subdesc_cnt, u64 data, int len)
{
	struct sq_hdr_subdesc *hdr;

	hdr = (struct sq_hdr_subdesc *)GET_SQ_DESC(sq, qentry);
	memset(hdr, 0, SND_QUEUE_DESC_SIZE);
	hdr->subdesc_type = SQ_DESC_TYPE_HEADER;
	hdr->subdesc_cnt = subdesc_cnt;
	hdr->tot_len = len;
	hdr->post_cqe = 1;
	sq->xdp_page[qentry] = (u64)virt_to_page((void *)data);
}

int nicvf_xdp_sq_append_pkt(struct nicvf *nic, struct snd_queue *sq,
			    u64 bufaddr, u64 dma_addr, u16 len)
{
	int subdesc_cnt = MIN_SQ_DESC_PER_PKT_XMIT;
	int qentry;

	if (subdesc_cnt > sq->xdp_free_cnt)
		return 0;

	qentry = nicvf_get_sq_desc(sq, subdesc_cnt);

	nicvf_xdp_sq_add_hdr_subdesc(sq, qentry, subdesc_cnt - 1, bufaddr, len);

	qentry = nicvf_get_nxt_sqentry(sq, qentry);
	nicvf_sq_add_gather_subdesc(sq, qentry, len, dma_addr);

	sq->xdp_desc_cnt += subdesc_cnt;

	return 1;
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

#define POST_CQE_DESC_COUNT 2

/* Get the number of SQ descriptors needed to xmit this skb */
static int nicvf_sq_subdesc_required(struct nicvf *nic, struct sk_buff *skb)
{
	int subdesc_cnt = MIN_SQ_DESC_PER_PKT_XMIT;

	if (skb_shinfo(skb)->gso_size && !nic->hw_tso) {
		subdesc_cnt = nicvf_tso_count_subdescs(skb);
		return subdesc_cnt;
	}

	/* Dummy descriptors to get TSO pkt completion notification */
	if (nic->t88 && nic->hw_tso && skb_shinfo(skb)->gso_size)
		subdesc_cnt += POST_CQE_DESC_COUNT;

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
	union {
		struct iphdr *v4;
		struct ipv6hdr *v6;
		unsigned char *hdr;
	} ip;

	ip.hdr = skb_network_header(skb);
	hdr = (struct sq_hdr_subdesc *)GET_SQ_DESC(sq, qentry);
	memset(hdr, 0, SND_QUEUE_DESC_SIZE);
	hdr->subdesc_type = SQ_DESC_TYPE_HEADER;

	if (nic->t88 && nic->hw_tso && skb_shinfo(skb)->gso_size) {
		/* post_cqe = 0, to avoid HW posting a CQE for every TSO
		 * segment transmitted on 88xx.
		 */
		hdr->subdesc_cnt = subdesc_cnt - POST_CQE_DESC_COUNT;
	} else {
		sq->skbuff[qentry] = (u64)skb;
		/* Enable notification via CQE after processing SQE */
		hdr->post_cqe = 1;
		/* No of subdescriptors following this */
		hdr->subdesc_cnt = subdesc_cnt;
	}
	hdr->tot_len = len;

	/* Offload checksum calculation to HW */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (ip.v4->version == 4)
			hdr->csum_l3 = 1; /* Enable IP csum calculation */
		hdr->l3_offset = skb_network_offset(skb);
		hdr->l4_offset = skb_transport_offset(skb);

		proto = (ip.v4->version == 4) ? ip.v4->protocol :
			ip.v6->nexthdr;

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
		this_cpu_inc(nic->pnicvf->drv_stats->tx_tso);
	}

	/* Check if timestamp is requested */
	if (!(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) {
		skb_tx_timestamp(skb);
		return;
	}

	/* Tx timestamping not supported along with TSO, so ignore request */
	if (skb_shinfo(skb)->gso_size)
		return;

	/* HW supports only a single outstanding packet to timestamp */
	if (!atomic_add_unless(&nic->pnicvf->tx_ptp_skbs, 1, 1))
		return;

	/* Mark the SKB for later reference */
	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	/* Finally enable timestamp generation
	 * Since 'post_cqe' is also set, two CQEs will be posted
	 * for this packet i.e CQE_TYPE_SEND and CQE_TYPE_SEND_PTP.
	 */
	hdr->tstmp = 1;
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

/* Add HDR + IMMEDIATE subdescriptors right after descriptors of a TSO
 * packet so that a CQE is posted as a notifation for transmission of
 * TSO packet.
 */
static inline void nicvf_sq_add_cqe_subdesc(struct snd_queue *sq, int qentry,
					    int tso_sqe, struct sk_buff *skb)
{
	struct sq_imm_subdesc *imm;
	struct sq_hdr_subdesc *hdr;

	sq->skbuff[qentry] = (u64)skb;

	hdr = (struct sq_hdr_subdesc *)GET_SQ_DESC(sq, qentry);
	memset(hdr, 0, SND_QUEUE_DESC_SIZE);
	hdr->subdesc_type = SQ_DESC_TYPE_HEADER;
	/* Enable notification via CQE after processing SQE */
	hdr->post_cqe = 1;
	/* There is no packet to transmit here */
	hdr->dont_send = 1;
	hdr->subdesc_cnt = POST_CQE_DESC_COUNT - 1;
	hdr->tot_len = 1;
	/* Actual TSO header SQE index, needed for cleanup */
	hdr->rsvd2 = tso_sqe;

	qentry = nicvf_get_nxt_sqentry(sq, qentry);
	imm = (struct sq_imm_subdesc *)GET_SQ_DESC(sq, qentry);
	memset(imm, 0, SND_QUEUE_DESC_SIZE);
	imm->subdesc_type = SQ_DESC_TYPE_IMMEDIATE;
	imm->len = 1;
}

static inline void nicvf_sq_doorbell(struct nicvf *nic, struct sk_buff *skb,
				     int sq_num, int desc_cnt)
{
	struct netdev_queue *txq;

	txq = netdev_get_tx_queue(nic->pnicvf->netdev,
				  skb_get_queue_mapping(skb));

	netdev_tx_sent_queue(txq, skb->len);

	/* make sure all memory stores are done before ringing doorbell */
	smp_wmb();

	/* Inform HW to xmit all TSO segments */
	nicvf_queue_reg_write(nic, NIC_QSET_SQ_0_7_DOOR,
			      sq_num, desc_cnt);
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
	int hdr_len;

	hdr_len = tso_start(skb, &tso);

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

	nicvf_sq_doorbell(nic, skb, sq_num, desc_cnt);

	this_cpu_inc(nic->pnicvf->drv_stats->tx_tso);
	return 1;
}

/* Append an skb to a SQ for packet transfer. */
int nicvf_sq_append_skb(struct nicvf *nic, struct snd_queue *sq,
			struct sk_buff *skb, u8 sq_num)
{
	int i, size;
	int subdesc_cnt, hdr_sqe = 0;
	int qentry;
	u64 dma_addr;

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
	hdr_sqe = qentry;

	/* Add SQ gather subdescs */
	qentry = nicvf_get_nxt_sqentry(sq, qentry);
	size = skb_is_nonlinear(skb) ? skb_headlen(skb) : skb->len;
	/* HW will ensure data coherency, CPU sync not required */
	dma_addr = dma_map_page_attrs(&nic->pdev->dev, virt_to_page(skb->data),
				      offset_in_page(skb->data), size,
				      DMA_TO_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
	if (dma_mapping_error(&nic->pdev->dev, dma_addr)) {
		nicvf_rollback_sq_desc(sq, qentry, subdesc_cnt);
		return 0;
	}

	nicvf_sq_add_gather_subdesc(sq, qentry, size, dma_addr);

	/* Check for scattered buffer */
	if (!skb_is_nonlinear(skb))
		goto doorbell;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		qentry = nicvf_get_nxt_sqentry(sq, qentry);
		size = skb_frag_size(frag);
		dma_addr = dma_map_page_attrs(&nic->pdev->dev,
					      skb_frag_page(frag),
					      skb_frag_off(frag), size,
					      DMA_TO_DEVICE,
					      DMA_ATTR_SKIP_CPU_SYNC);
		if (dma_mapping_error(&nic->pdev->dev, dma_addr)) {
			/* Free entire chain of mapped buffers
			 * here 'i' = frags mapped + above mapped skb->data
			 */
			nicvf_unmap_sndq_buffers(nic, sq, hdr_sqe, i);
			nicvf_rollback_sq_desc(sq, qentry, subdesc_cnt);
			return 0;
		}
		nicvf_sq_add_gather_subdesc(sq, qentry, size, dma_addr);
	}

doorbell:
	if (nic->t88 && skb_shinfo(skb)->gso_size) {
		qentry = nicvf_get_nxt_sqentry(sq, qentry);
		nicvf_sq_add_cqe_subdesc(sq, qentry, hdr_sqe, skb);
	}

	nicvf_sq_doorbell(nic, skb, sq_num, subdesc_cnt);

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

static void nicvf_unmap_rcv_buffer(struct nicvf *nic, u64 dma_addr,
				   u64 buf_addr, bool xdp)
{
	struct page *page = NULL;
	int len = RCV_FRAG_LEN;

	if (xdp) {
		page = virt_to_page(phys_to_virt(buf_addr));
		/* Check if it's a recycled page, if not
		 * unmap the DMA mapping.
		 *
		 * Recycled page holds an extra reference.
		 */
		if (page_ref_count(page) != 1)
			return;

		len += XDP_PACKET_HEADROOM;
		/* Receive buffers in XDP mode are mapped from page start */
		dma_addr &= PAGE_MASK;
	}
	dma_unmap_page_attrs(&nic->pdev->dev, dma_addr, len,
			     DMA_FROM_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
}

/* Returns SKB for a received packet */
struct sk_buff *nicvf_get_rcv_skb(struct nicvf *nic,
				  struct cqe_rx_t *cqe_rx, bool xdp)
{
	int frag;
	int payload_len = 0;
	struct sk_buff *skb = NULL;
	struct page *page;
	int offset;
	u16 *rb_lens = NULL;
	u64 *rb_ptrs = NULL;
	u64 phys_addr;

	rb_lens = (void *)cqe_rx + (3 * sizeof(u64));
	/* Except 88xx pass1 on all other chips CQE_RX2_S is added to
	 * CQE_RX at word6, hence buffer pointers move by word
	 *
	 * Use existing 'hw_tso' flag which will be set for all chips
	 * except 88xx pass1 instead of a additional cache line
	 * access (or miss) by using pci dev's revision.
	 */
	if (!nic->hw_tso)
		rb_ptrs = (void *)cqe_rx + (6 * sizeof(u64));
	else
		rb_ptrs = (void *)cqe_rx + (7 * sizeof(u64));

	for (frag = 0; frag < cqe_rx->rb_cnt; frag++) {
		payload_len = rb_lens[frag_num(frag)];
		phys_addr = nicvf_iova_to_phys(nic, *rb_ptrs);
		if (!phys_addr) {
			if (skb)
				dev_kfree_skb_any(skb);
			return NULL;
		}

		if (!frag) {
			/* First fragment */
			nicvf_unmap_rcv_buffer(nic,
					       *rb_ptrs - cqe_rx->align_pad,
					       phys_addr, xdp);
			skb = nicvf_rb_ptr_to_skb(nic,
						  phys_addr - cqe_rx->align_pad,
						  payload_len);
			if (!skb)
				return NULL;
			skb_reserve(skb, cqe_rx->align_pad);
			skb_put(skb, payload_len);
		} else {
			/* Add fragments */
			nicvf_unmap_rcv_buffer(nic, *rb_ptrs, phys_addr, xdp);
			page = virt_to_page(phys_to_virt(phys_addr));
			offset = phys_to_virt(phys_addr) - page_address(page);
			skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page,
					offset, payload_len, RCV_FRAG_LEN);
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
	netif_err(nic, rx_err, nic->netdev,
		  "RX error CQE err_level 0x%x err_opcode 0x%x\n",
		  cqe_rx->err_level, cqe_rx->err_opcode);

	switch (cqe_rx->err_opcode) {
	case CQ_RX_ERROP_RE_PARTIAL:
		this_cpu_inc(nic->drv_stats->rx_bgx_truncated_pkts);
		break;
	case CQ_RX_ERROP_RE_JABBER:
		this_cpu_inc(nic->drv_stats->rx_jabber_errs);
		break;
	case CQ_RX_ERROP_RE_FCS:
		this_cpu_inc(nic->drv_stats->rx_fcs_errs);
		break;
	case CQ_RX_ERROP_RE_RX_CTL:
		this_cpu_inc(nic->drv_stats->rx_bgx_errs);
		break;
	case CQ_RX_ERROP_PREL2_ERR:
		this_cpu_inc(nic->drv_stats->rx_prel2_errs);
		break;
	case CQ_RX_ERROP_L2_MAL:
		this_cpu_inc(nic->drv_stats->rx_l2_hdr_malformed);
		break;
	case CQ_RX_ERROP_L2_OVERSIZE:
		this_cpu_inc(nic->drv_stats->rx_oversize);
		break;
	case CQ_RX_ERROP_L2_UNDERSIZE:
		this_cpu_inc(nic->drv_stats->rx_undersize);
		break;
	case CQ_RX_ERROP_L2_LENMISM:
		this_cpu_inc(nic->drv_stats->rx_l2_len_mismatch);
		break;
	case CQ_RX_ERROP_L2_PCLP:
		this_cpu_inc(nic->drv_stats->rx_l2_pclp);
		break;
	case CQ_RX_ERROP_IP_NOT:
		this_cpu_inc(nic->drv_stats->rx_ip_ver_errs);
		break;
	case CQ_RX_ERROP_IP_CSUM_ERR:
		this_cpu_inc(nic->drv_stats->rx_ip_csum_errs);
		break;
	case CQ_RX_ERROP_IP_MAL:
		this_cpu_inc(nic->drv_stats->rx_ip_hdr_malformed);
		break;
	case CQ_RX_ERROP_IP_MALD:
		this_cpu_inc(nic->drv_stats->rx_ip_payload_malformed);
		break;
	case CQ_RX_ERROP_IP_HOP:
		this_cpu_inc(nic->drv_stats->rx_ip_ttl_errs);
		break;
	case CQ_RX_ERROP_L3_PCLP:
		this_cpu_inc(nic->drv_stats->rx_l3_pclp);
		break;
	case CQ_RX_ERROP_L4_MAL:
		this_cpu_inc(nic->drv_stats->rx_l4_malformed);
		break;
	case CQ_RX_ERROP_L4_CHK:
		this_cpu_inc(nic->drv_stats->rx_l4_csum_errs);
		break;
	case CQ_RX_ERROP_UDP_LEN:
		this_cpu_inc(nic->drv_stats->rx_udp_len_errs);
		break;
	case CQ_RX_ERROP_L4_PORT:
		this_cpu_inc(nic->drv_stats->rx_l4_port_errs);
		break;
	case CQ_RX_ERROP_TCP_FLAG:
		this_cpu_inc(nic->drv_stats->rx_tcp_flag_errs);
		break;
	case CQ_RX_ERROP_TCP_OFFSET:
		this_cpu_inc(nic->drv_stats->rx_tcp_offset_errs);
		break;
	case CQ_RX_ERROP_L4_PCLP:
		this_cpu_inc(nic->drv_stats->rx_l4_pclp);
		break;
	case CQ_RX_ERROP_RBDR_TRUNC:
		this_cpu_inc(nic->drv_stats->rx_truncated_pkts);
		break;
	}

	return 1;
}

/* Check for errors in the send cmp.queue entry */
int nicvf_check_cqe_tx_errs(struct nicvf *nic, struct cqe_send_t *cqe_tx)
{
	switch (cqe_tx->send_status) {
	case CQ_TX_ERROP_DESC_FAULT:
		this_cpu_inc(nic->drv_stats->tx_desc_fault);
		break;
	case CQ_TX_ERROP_HDR_CONS_ERR:
		this_cpu_inc(nic->drv_stats->tx_hdr_cons_err);
		break;
	case CQ_TX_ERROP_SUBDC_ERR:
		this_cpu_inc(nic->drv_stats->tx_subdesc_err);
		break;
	case CQ_TX_ERROP_MAX_SIZE_VIOL:
		this_cpu_inc(nic->drv_stats->tx_max_size_exceeded);
		break;
	case CQ_TX_ERROP_IMM_SIZE_OFLOW:
		this_cpu_inc(nic->drv_stats->tx_imm_size_oflow);
		break;
	case CQ_TX_ERROP_DATA_SEQUENCE_ERR:
		this_cpu_inc(nic->drv_stats->tx_data_seq_err);
		break;
	case CQ_TX_ERROP_MEM_SEQUENCE_ERR:
		this_cpu_inc(nic->drv_stats->tx_mem_seq_err);
		break;
	case CQ_TX_ERROP_LOCK_VIOL:
		this_cpu_inc(nic->drv_stats->tx_lock_viol);
		break;
	case CQ_TX_ERROP_DATA_FAULT:
		this_cpu_inc(nic->drv_stats->tx_data_fault);
		break;
	case CQ_TX_ERROP_TSTMP_CONFLICT:
		this_cpu_inc(nic->drv_stats->tx_tstmp_conflict);
		break;
	case CQ_TX_ERROP_TSTMP_TIMEOUT:
		this_cpu_inc(nic->drv_stats->tx_tstmp_timeout);
		break;
	case CQ_TX_ERROP_MEM_FAULT:
		this_cpu_inc(nic->drv_stats->tx_mem_fault);
		break;
	case CQ_TX_ERROP_CK_OVERLAP:
		this_cpu_inc(nic->drv_stats->tx_csum_overlap);
		break;
	case CQ_TX_ERROP_CK_OFLOW:
		this_cpu_inc(nic->drv_stats->tx_csum_overflow);
		break;
	}

	return 1;
}
