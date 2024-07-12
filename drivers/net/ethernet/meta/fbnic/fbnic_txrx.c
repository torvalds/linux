// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/pci.h>

#include "fbnic.h"
#include "fbnic_netdev.h"
#include "fbnic_txrx.h"

netdev_tx_t fbnic_xmit_frame(struct sk_buff *skb, struct net_device *dev)
{
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
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
