// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/iopoll.h>
#include <linux/pci.h>

#include "fbnic.h"
#include "fbnic_netdev.h"
#include "fbnic_txrx.h"

static u32 __iomem *fbnic_ring_csr_base(const struct fbnic_ring *ring)
{
	unsigned long csr_base = (unsigned long)ring->doorbell;

	csr_base &= ~(FBNIC_QUEUE_STRIDE * sizeof(u32) - 1);

	return (u32 __iomem *)csr_base;
}

static u32 fbnic_ring_rd32(struct fbnic_ring *ring, unsigned int csr)
{
	u32 __iomem *csr_base = fbnic_ring_csr_base(ring);

	return readl(csr_base + csr);
}

static void fbnic_ring_wr32(struct fbnic_ring *ring, unsigned int csr, u32 val)
{
	u32 __iomem *csr_base = fbnic_ring_csr_base(ring);

	writel(val, csr_base + csr);
}

netdev_tx_t fbnic_xmit_frame(struct sk_buff *skb, struct net_device *dev)
{
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static void fbnic_nv_irq_disable(struct fbnic_napi_vector *nv)
{
	struct fbnic_dev *fbd = nv->fbd;
	u32 v_idx = nv->v_idx;

	fbnic_wr32(fbd, FBNIC_INTR_MASK_SET(v_idx / 32), 1 << (v_idx % 32));
}

static int fbnic_poll(struct napi_struct *napi, int budget)
{
	return 0;
}

static irqreturn_t fbnic_msix_clean_rings(int __always_unused irq, void *data)
{
	struct fbnic_napi_vector *nv = data;

	napi_schedule_irqoff(&nv->napi);

	return IRQ_HANDLED;
}

static void fbnic_remove_tx_ring(struct fbnic_net *fbn,
				 struct fbnic_ring *txr)
{
	if (!(txr->flags & FBNIC_RING_F_STATS))
		return;

	/* Remove pointer to the Tx ring */
	WARN_ON(fbn->tx[txr->q_idx] && fbn->tx[txr->q_idx] != txr);
	fbn->tx[txr->q_idx] = NULL;
}

static void fbnic_remove_rx_ring(struct fbnic_net *fbn,
				 struct fbnic_ring *rxr)
{
	if (!(rxr->flags & FBNIC_RING_F_STATS))
		return;

	/* Remove pointer to the Rx ring */
	WARN_ON(fbn->rx[rxr->q_idx] && fbn->rx[rxr->q_idx] != rxr);
	fbn->rx[rxr->q_idx] = NULL;
}

static void fbnic_free_napi_vector(struct fbnic_net *fbn,
				   struct fbnic_napi_vector *nv)
{
	struct fbnic_dev *fbd = nv->fbd;
	u32 v_idx = nv->v_idx;
	int i, j;

	for (i = 0; i < nv->txt_count; i++) {
		fbnic_remove_tx_ring(fbn, &nv->qt[i].sub0);
		fbnic_remove_tx_ring(fbn, &nv->qt[i].cmpl);
	}

	for (j = 0; j < nv->rxt_count; j++, i++) {
		fbnic_remove_rx_ring(fbn, &nv->qt[i].sub0);
		fbnic_remove_rx_ring(fbn, &nv->qt[i].sub1);
		fbnic_remove_rx_ring(fbn, &nv->qt[i].cmpl);
	}

	fbnic_free_irq(fbd, v_idx, nv);
	netif_napi_del(&nv->napi);
	list_del(&nv->napis);
	kfree(nv);
}

void fbnic_free_napi_vectors(struct fbnic_net *fbn)
{
	struct fbnic_napi_vector *nv, *temp;

	list_for_each_entry_safe(nv, temp, &fbn->napis, napis)
		fbnic_free_napi_vector(fbn, nv);
}

static void fbnic_name_napi_vector(struct fbnic_napi_vector *nv)
{
	unsigned char *dev_name = nv->napi.dev->name;

	if (!nv->rxt_count)
		snprintf(nv->name, sizeof(nv->name), "%s-Tx-%u", dev_name,
			 nv->v_idx - FBNIC_NON_NAPI_VECTORS);
	else
		snprintf(nv->name, sizeof(nv->name), "%s-TxRx-%u", dev_name,
			 nv->v_idx - FBNIC_NON_NAPI_VECTORS);
}

static void fbnic_ring_init(struct fbnic_ring *ring, u32 __iomem *doorbell,
			    int q_idx, u8 flags)
{
	ring->doorbell = doorbell;
	ring->q_idx = q_idx;
	ring->flags = flags;
}

static int fbnic_alloc_napi_vector(struct fbnic_dev *fbd, struct fbnic_net *fbn,
				   unsigned int v_count, unsigned int v_idx,
				   unsigned int txq_count, unsigned int txq_idx,
				   unsigned int rxq_count, unsigned int rxq_idx)
{
	int txt_count = txq_count, rxt_count = rxq_count;
	u32 __iomem *uc_addr = fbd->uc_addr0;
	struct fbnic_napi_vector *nv;
	struct fbnic_q_triad *qt;
	int qt_count, err;
	u32 __iomem *db;

	qt_count = txt_count + rxq_count;
	if (!qt_count)
		return -EINVAL;

	/* If MMIO has already failed there are no rings to initialize */
	if (!uc_addr)
		return -EIO;

	/* Allocate NAPI vector and queue triads */
	nv = kzalloc(struct_size(nv, qt, qt_count), GFP_KERNEL);
	if (!nv)
		return -ENOMEM;

	/* Record queue triad counts */
	nv->txt_count = txt_count;
	nv->rxt_count = rxt_count;

	/* Provide pointer back to fbnic and MSI-X vectors */
	nv->fbd = fbd;
	nv->v_idx = v_idx;

	/* Record IRQ to NAPI struct */
	netif_napi_set_irq(&nv->napi,
			   pci_irq_vector(to_pci_dev(fbd->dev), nv->v_idx));

	/* Tie napi to netdev */
	list_add(&nv->napis, &fbn->napis);
	netif_napi_add(fbn->netdev, &nv->napi, fbnic_poll);

	/* Tie nv back to PCIe dev */
	nv->dev = fbd->dev;

	/* Initialize vector name */
	fbnic_name_napi_vector(nv);

	/* Request the IRQ for napi vector */
	err = fbnic_request_irq(fbd, v_idx, &fbnic_msix_clean_rings,
				IRQF_SHARED, nv->name, nv);
	if (err)
		goto napi_del;

	/* Initialize queue triads */
	qt = nv->qt;

	while (txt_count) {
		/* Configure Tx queue */
		db = &uc_addr[FBNIC_QUEUE(txq_idx) + FBNIC_QUEUE_TWQ0_TAIL];

		/* Assign Tx queue to netdev if applicable */
		if (txq_count > 0) {
			u8 flags = FBNIC_RING_F_CTX | FBNIC_RING_F_STATS;

			fbnic_ring_init(&qt->sub0, db, txq_idx, flags);
			fbn->tx[txq_idx] = &qt->sub0;
			txq_count--;
		} else {
			fbnic_ring_init(&qt->sub0, db, 0,
					FBNIC_RING_F_DISABLED);
		}

		/* Configure Tx completion queue */
		db = &uc_addr[FBNIC_QUEUE(txq_idx) + FBNIC_QUEUE_TCQ_HEAD];
		fbnic_ring_init(&qt->cmpl, db, 0, 0);

		/* Update Tx queue index */
		txt_count--;
		txq_idx += v_count;

		/* Move to next queue triad */
		qt++;
	}

	while (rxt_count) {
		/* Configure header queue */
		db = &uc_addr[FBNIC_QUEUE(rxq_idx) + FBNIC_QUEUE_BDQ_HPQ_TAIL];
		fbnic_ring_init(&qt->sub0, db, 0, FBNIC_RING_F_CTX);

		/* Configure payload queue */
		db = &uc_addr[FBNIC_QUEUE(rxq_idx) + FBNIC_QUEUE_BDQ_PPQ_TAIL];
		fbnic_ring_init(&qt->sub1, db, 0, FBNIC_RING_F_CTX);

		/* Configure Rx completion queue */
		db = &uc_addr[FBNIC_QUEUE(rxq_idx) + FBNIC_QUEUE_RCQ_HEAD];
		fbnic_ring_init(&qt->cmpl, db, rxq_idx, FBNIC_RING_F_STATS);
		fbn->rx[rxq_idx] = &qt->cmpl;

		/* Update Rx queue index */
		rxt_count--;
		rxq_idx += v_count;

		/* Move to next queue triad */
		qt++;
	}

	return 0;

napi_del:
	netif_napi_del(&nv->napi);
	list_del(&nv->napis);
	kfree(nv);
	return err;
}

int fbnic_alloc_napi_vectors(struct fbnic_net *fbn)
{
	unsigned int txq_idx = 0, rxq_idx = 0, v_idx = FBNIC_NON_NAPI_VECTORS;
	unsigned int num_tx = fbn->num_tx_queues;
	unsigned int num_rx = fbn->num_rx_queues;
	unsigned int num_napi = fbn->num_napi;
	struct fbnic_dev *fbd = fbn->fbd;
	int err;

	/* Allocate 1 Tx queue per napi vector */
	if (num_napi < FBNIC_MAX_TXQS && num_napi == num_tx + num_rx) {
		while (num_tx) {
			err = fbnic_alloc_napi_vector(fbd, fbn,
						      num_napi, v_idx,
						      1, txq_idx, 0, 0);
			if (err)
				goto free_vectors;

			/* Update counts and index */
			num_tx--;
			txq_idx++;

			v_idx++;
		}
	}

	/* Allocate Tx/Rx queue pairs per vector, or allocate remaining Rx */
	while (num_rx | num_tx) {
		int tqpv = DIV_ROUND_UP(num_tx, num_napi - txq_idx);
		int rqpv = DIV_ROUND_UP(num_rx, num_napi - rxq_idx);

		err = fbnic_alloc_napi_vector(fbd, fbn, num_napi, v_idx,
					      tqpv, txq_idx, rqpv, rxq_idx);
		if (err)
			goto free_vectors;

		/* Update counts and index */
		num_tx -= tqpv;
		txq_idx++;

		num_rx -= rqpv;
		rxq_idx++;

		v_idx++;
	}

	return 0;

free_vectors:
	fbnic_free_napi_vectors(fbn);

	return -ENOMEM;
}

static void fbnic_free_ring_resources(struct device *dev,
				      struct fbnic_ring *ring)
{
	kvfree(ring->buffer);
	ring->buffer = NULL;

	/* If size is not set there are no descriptors present */
	if (!ring->size)
		return;

	dma_free_coherent(dev, ring->size, ring->desc, ring->dma);
	ring->size_mask = 0;
	ring->size = 0;
}

static int fbnic_alloc_tx_ring_desc(struct fbnic_net *fbn,
				    struct fbnic_ring *txr)
{
	struct device *dev = fbn->netdev->dev.parent;
	size_t size;

	/* Round size up to nearest 4K */
	size = ALIGN(array_size(sizeof(*txr->desc), fbn->txq_size), 4096);

	txr->desc = dma_alloc_coherent(dev, size, &txr->dma,
				       GFP_KERNEL | __GFP_NOWARN);
	if (!txr->desc)
		return -ENOMEM;

	/* txq_size should be a power of 2, so mask is just that -1 */
	txr->size_mask = fbn->txq_size - 1;
	txr->size = size;

	return 0;
}

static int fbnic_alloc_tx_ring_buffer(struct fbnic_ring *txr)
{
	size_t size = array_size(sizeof(*txr->tx_buf), txr->size_mask + 1);

	txr->tx_buf = kvzalloc(size, GFP_KERNEL | __GFP_NOWARN);

	return txr->tx_buf ? 0 : -ENOMEM;
}

static int fbnic_alloc_tx_ring_resources(struct fbnic_net *fbn,
					 struct fbnic_ring *txr)
{
	struct device *dev = fbn->netdev->dev.parent;
	int err;

	if (txr->flags & FBNIC_RING_F_DISABLED)
		return 0;

	err = fbnic_alloc_tx_ring_desc(fbn, txr);
	if (err)
		return err;

	if (!(txr->flags & FBNIC_RING_F_CTX))
		return 0;

	err = fbnic_alloc_tx_ring_buffer(txr);
	if (err)
		goto free_desc;

	return 0;

free_desc:
	fbnic_free_ring_resources(dev, txr);
	return err;
}

static void fbnic_free_qt_resources(struct fbnic_net *fbn,
				    struct fbnic_q_triad *qt)
{
	struct device *dev = fbn->netdev->dev.parent;

	fbnic_free_ring_resources(dev, &qt->cmpl);
	fbnic_free_ring_resources(dev, &qt->sub1);
	fbnic_free_ring_resources(dev, &qt->sub0);
}

static int fbnic_alloc_tx_qt_resources(struct fbnic_net *fbn,
				       struct fbnic_q_triad *qt)
{
	struct device *dev = fbn->netdev->dev.parent;
	int err;

	err = fbnic_alloc_tx_ring_resources(fbn, &qt->sub0);
	if (err)
		return err;

	err = fbnic_alloc_tx_ring_resources(fbn, &qt->cmpl);
	if (err)
		goto free_sub0;

	return 0;

free_sub0:
	fbnic_free_ring_resources(dev, &qt->sub0);
	return err;
}

static void fbnic_free_nv_resources(struct fbnic_net *fbn,
				    struct fbnic_napi_vector *nv)
{
	int i;

	/* Free Tx Resources  */
	for (i = 0; i < nv->txt_count; i++)
		fbnic_free_qt_resources(fbn, &nv->qt[i]);
}

static int fbnic_alloc_nv_resources(struct fbnic_net *fbn,
				    struct fbnic_napi_vector *nv)
{
	int i, err;

	/* Allocate Tx Resources */
	for (i = 0; i < nv->txt_count; i++) {
		err = fbnic_alloc_tx_qt_resources(fbn, &nv->qt[i]);
		if (err)
			goto free_resources;
	}

	return 0;

free_resources:
	while (i--)
		fbnic_free_qt_resources(fbn, &nv->qt[i]);
	return err;
}

void fbnic_free_resources(struct fbnic_net *fbn)
{
	struct fbnic_napi_vector *nv;

	list_for_each_entry(nv, &fbn->napis, napis)
		fbnic_free_nv_resources(fbn, nv);
}

int fbnic_alloc_resources(struct fbnic_net *fbn)
{
	struct fbnic_napi_vector *nv;
	int err = -ENODEV;

	list_for_each_entry(nv, &fbn->napis, napis) {
		err = fbnic_alloc_nv_resources(fbn, nv);
		if (err)
			goto free_resources;
	}

	return 0;

free_resources:
	list_for_each_entry_continue_reverse(nv, &fbn->napis, napis)
		fbnic_free_nv_resources(fbn, nv);

	return err;
}

static void fbnic_disable_twq0(struct fbnic_ring *txr)
{
	u32 twq_ctl = fbnic_ring_rd32(txr, FBNIC_QUEUE_TWQ0_CTL);

	twq_ctl &= ~FBNIC_QUEUE_TWQ_CTL_ENABLE;

	fbnic_ring_wr32(txr, FBNIC_QUEUE_TWQ0_CTL, twq_ctl);
}

static void fbnic_disable_tcq(struct fbnic_ring *txr)
{
	fbnic_ring_wr32(txr, FBNIC_QUEUE_TCQ_CTL, 0);
	fbnic_ring_wr32(txr, FBNIC_QUEUE_TIM_MASK, FBNIC_QUEUE_TIM_MASK_MASK);
}

void fbnic_napi_disable(struct fbnic_net *fbn)
{
	struct fbnic_napi_vector *nv;

	list_for_each_entry(nv, &fbn->napis, napis) {
		napi_disable(&nv->napi);

		fbnic_nv_irq_disable(nv);
	}
}

void fbnic_disable(struct fbnic_net *fbn)
{
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_napi_vector *nv;
	int i;

	list_for_each_entry(nv, &fbn->napis, napis) {
		/* Disable Tx queue triads */
		for (i = 0; i < nv->txt_count; i++) {
			struct fbnic_q_triad *qt = &nv->qt[i];

			fbnic_disable_twq0(&qt->sub0);
			fbnic_disable_tcq(&qt->cmpl);
		}
	}

	fbnic_wrfl(fbd);
}

static void fbnic_tx_flush(struct fbnic_dev *fbd)
{
	netdev_warn(fbd->netdev, "tiggerring Tx flush\n");

	fbnic_rmw32(fbd, FBNIC_TMI_DROP_CTRL, FBNIC_TMI_DROP_CTRL_EN,
		    FBNIC_TMI_DROP_CTRL_EN);
}

static void fbnic_tx_flush_off(struct fbnic_dev *fbd)
{
	fbnic_rmw32(fbd, FBNIC_TMI_DROP_CTRL, FBNIC_TMI_DROP_CTRL_EN, 0);
}

struct fbnic_idle_regs {
	u32 reg_base;
	u8 reg_cnt;
};

static bool fbnic_all_idle(struct fbnic_dev *fbd,
			   const struct fbnic_idle_regs *regs,
			   unsigned int nregs)
{
	unsigned int i, j;

	for (i = 0; i < nregs; i++) {
		for (j = 0; j < regs[i].reg_cnt; j++) {
			if (fbnic_rd32(fbd, regs[i].reg_base + j) != ~0U)
				return false;
		}
	}
	return true;
}

static void fbnic_idle_dump(struct fbnic_dev *fbd,
			    const struct fbnic_idle_regs *regs,
			    unsigned int nregs, const char *dir, int err)
{
	unsigned int i, j;

	netdev_err(fbd->netdev, "error waiting for %s idle %d\n", dir, err);
	for (i = 0; i < nregs; i++)
		for (j = 0; j < regs[i].reg_cnt; j++)
			netdev_err(fbd->netdev, "0x%04x: %08x\n",
				   regs[i].reg_base + j,
				   fbnic_rd32(fbd, regs[i].reg_base + j));
}

int fbnic_wait_all_queues_idle(struct fbnic_dev *fbd, bool may_fail)
{
	static const struct fbnic_idle_regs tx[] = {
		{ FBNIC_QM_TWQ_IDLE(0),	FBNIC_QM_TWQ_IDLE_CNT, },
		{ FBNIC_QM_TQS_IDLE(0),	FBNIC_QM_TQS_IDLE_CNT, },
		{ FBNIC_QM_TDE_IDLE(0),	FBNIC_QM_TDE_IDLE_CNT, },
		{ FBNIC_QM_TCQ_IDLE(0),	FBNIC_QM_TCQ_IDLE_CNT, },
	};
	bool idle;
	int err;

	err = read_poll_timeout_atomic(fbnic_all_idle, idle, idle, 2, 500000,
				       false, fbd, tx, ARRAY_SIZE(tx));
	if (err == -ETIMEDOUT) {
		fbnic_tx_flush(fbd);
		err = read_poll_timeout_atomic(fbnic_all_idle, idle, idle,
					       2, 500000, false,
					       fbd, tx, ARRAY_SIZE(tx));
		fbnic_tx_flush_off(fbd);
	}
	if (err) {
		fbnic_idle_dump(fbd, tx, ARRAY_SIZE(tx), "Tx", err);
		if (may_fail)
			return err;
	}

	return err;
}

void fbnic_flush(struct fbnic_net *fbn)
{
	struct fbnic_napi_vector *nv;

	list_for_each_entry(nv, &fbn->napis, napis) {
		int i;

		/* Flush any processed Tx Queue Triads and drop the rest */
		for (i = 0; i < nv->txt_count; i++) {
			struct fbnic_q_triad *qt = &nv->qt[i];
			struct netdev_queue *tx_queue;

			/* Reset completion queue descriptor ring */
			memset(qt->cmpl.desc, 0, qt->cmpl.size);

			/* Nothing else to do if Tx queue is disabled */
			if (qt->sub0.flags & FBNIC_RING_F_DISABLED)
				continue;

			/* Reset BQL associated with Tx queue */
			tx_queue = netdev_get_tx_queue(nv->napi.dev,
						       qt->sub0.q_idx);
			netdev_tx_reset_queue(tx_queue);

			/* Disassociate Tx queue from NAPI */
			netif_queue_set_napi(nv->napi.dev, qt->sub0.q_idx,
					     NETDEV_QUEUE_TYPE_TX, NULL);
		}
	}
}

static void fbnic_enable_twq0(struct fbnic_ring *twq)
{
	u32 log_size = fls(twq->size_mask);

	if (!twq->size_mask)
		return;

	/* Reset head/tail */
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_CTL, FBNIC_QUEUE_TWQ_CTL_RESET);
	twq->tail = 0;
	twq->head = 0;

	/* Store descriptor ring address and size */
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_BAL, lower_32_bits(twq->dma));
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_BAH, upper_32_bits(twq->dma));

	/* Write lower 4 bits of log size as 64K ring size is 0 */
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_SIZE, log_size & 0xf);

	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_CTL, FBNIC_QUEUE_TWQ_CTL_ENABLE);
}

static void fbnic_enable_tcq(struct fbnic_napi_vector *nv,
			     struct fbnic_ring *tcq)
{
	u32 log_size = fls(tcq->size_mask);

	if (!tcq->size_mask)
		return;

	/* Reset head/tail */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_CTL, FBNIC_QUEUE_TCQ_CTL_RESET);
	tcq->tail = 0;
	tcq->head = 0;

	/* Store descriptor ring address and size */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_BAL, lower_32_bits(tcq->dma));
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_BAH, upper_32_bits(tcq->dma));

	/* Write lower 4 bits of log size as 64K ring size is 0 */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_SIZE, log_size & 0xf);

	/* Store interrupt information for the completion queue */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TIM_CTL, nv->v_idx);
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TIM_THRESHOLD, tcq->size_mask / 2);
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TIM_MASK, 0);

	/* Enable queue */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_CTL, FBNIC_QUEUE_TCQ_CTL_ENABLE);
}

void fbnic_enable(struct fbnic_net *fbn)
{
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_napi_vector *nv;
	int i;

	list_for_each_entry(nv, &fbn->napis, napis) {
		/* Setup Tx Queue Triads */
		for (i = 0; i < nv->txt_count; i++) {
			struct fbnic_q_triad *qt = &nv->qt[i];

			fbnic_enable_twq0(&qt->sub0);
			fbnic_enable_tcq(nv, &qt->cmpl);
		}
	}

	fbnic_wrfl(fbd);
}

static void fbnic_nv_irq_enable(struct fbnic_napi_vector *nv)
{
	struct fbnic_dev *fbd = nv->fbd;
	u32 val;

	val = FBNIC_INTR_CQ_REARM_INTR_UNMASK;

	fbnic_wr32(fbd, FBNIC_INTR_CQ_REARM(nv->v_idx), val);
}

void fbnic_napi_enable(struct fbnic_net *fbn)
{
	struct fbnic_napi_vector *nv;

	list_for_each_entry(nv, &fbn->napis, napis) {
		napi_enable(&nv->napi);

		fbnic_nv_irq_enable(nv);
	}
}
