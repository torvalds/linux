// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/iopoll.h>
#include <linux/vmalloc.h>
#include <net/netdev_queues.h>

#include "rnpgbe_lib.h"
#include "rnpgbe.h"
#include "rnpgbe_mbx_fw.h"

static void rnpgbe_mbx_work(struct work_struct *work)
{
	struct mucse *mucse = container_of(work, struct mucse, mbx_work);

	mucse_fw_irq_handler(&mucse->hw);
}

/**
 * rnpgbe_msix_other - Other irq handler
 * @irq: interrupt number
 * @data: private data
 *
 * Return: IRQ_HANDLED
 **/
static irqreturn_t rnpgbe_msix_other(int irq, void *data)
{
	struct mucse *mucse = (struct mucse *)data;

	queue_work(system_percpu_wq, &mucse->mbx_work);

	return IRQ_HANDLED;
}

static void rnpgbe_irq_disable_queues(struct mucse_q_vector *q_vector)
{
	struct mucse_ring *ring;

	/*
	 * TX/RX pairs share interrupt control registers; update them through
	 * the TX ring list.
	 */
	mucse_for_each_ring(ring, q_vector->tx) {
		writel(INT_VALID, ring->trig);
		writel((RX_INT_MASK | TX_INT_MASK), ring->irq_mask);
	}
	/* flush posted writes to ensure hardware sees the mask */
	if (q_vector->tx.ring)
		readl(q_vector->tx.ring->irq_mask);
}

static void rnpgbe_irq_enable_queues(struct mucse_q_vector *q_vector)
{
	struct mucse_ring *ring;

	/*
	 * TX/RX pairs share interrupt control registers; update them through
	 * the TX ring list.
	 */
	mucse_for_each_ring(ring, q_vector->tx) {
		writel(0, ring->irq_mask);

		/* Re-trigger hw to re-check events lost while masked. */
		writel(INT_VALID | TX_INT_MASK | RX_INT_MASK, ring->trig);
	}
}

/**
 * rnpgbe_clean_tx_irq - Reclaim resources after transmit completes
 * @q_vector: structure containing interrupt and ring information
 * @tx_ring: tx ring to clean
 * @napi_budget: Used to determine if we are in netpoll
 *
 * Return: true if cleaning completed within the budget, otherwise false
 **/
static bool rnpgbe_clean_tx_irq(struct mucse_q_vector *q_vector,
				struct mucse_ring *tx_ring,
				int napi_budget)
{
	int budget = q_vector->mucse->tx_work_limit;
	u64 total_bytes = 0, total_packets = 0;
	struct mucse *mucse = q_vector->mucse;
	struct mucse_tx_buffer *tx_buffer;
	struct rnpgbe_tx_desc *tx_desc;
	int i = tx_ring->next_to_clean;

	tx_buffer = &tx_ring->tx_buffer_info[i];
	tx_desc = M_TX_DESC(tx_ring, i);
	i -= tx_ring->count;

	do {
		struct rnpgbe_tx_desc *eop_desc = tx_buffer->next_to_watch;

		/* if next_to_watch is not set then there is no work pending */
		if (!eop_desc)
			break;

		/* prevent any other reads prior to eop_desc */
		dma_rmb();

		/* if eop DD is not set pending work has not been completed */
		if (!(eop_desc->vlan_cmd & cpu_to_le32(M_TXD_STAT_DD)))
			break;
		/* clear next_to_watch to prevent false hangs */
		tx_buffer->next_to_watch = NULL;
		total_bytes += tx_buffer->bytecount;
		total_packets += tx_buffer->gso_segs;
		napi_consume_skb(tx_buffer->skb, napi_budget);
		if (tx_buffer->mapped_as_page) {
			dma_unmap_page(tx_ring->dev,
				       dma_unmap_addr(tx_buffer, dma),
				       dma_unmap_len(tx_buffer, len),
				       DMA_TO_DEVICE);
		} else {
			dma_unmap_single(tx_ring->dev,
					 dma_unmap_addr(tx_buffer, dma),
					 dma_unmap_len(tx_buffer, len),
					 DMA_TO_DEVICE);
		}
		tx_buffer->skb = NULL;
		dma_unmap_len_set(tx_buffer, len, 0);

		/* unmap remaining buffers */
		while (tx_desc != eop_desc) {
			tx_buffer++;
			tx_desc++;
			i++;
			if (unlikely(!i)) {
				i -= tx_ring->count;
				tx_buffer = tx_ring->tx_buffer_info;
				tx_desc = M_TX_DESC(tx_ring, 0);
			}

			/* unmap any remaining paged data */
			if (dma_unmap_len(tx_buffer, len)) {
				dma_unmap_page(tx_ring->dev,
					       dma_unmap_addr(tx_buffer, dma),
					       dma_unmap_len(tx_buffer, len),
					       DMA_TO_DEVICE);
				dma_unmap_len_set(tx_buffer, len, 0);
			}
		}

		/* move us one more past the eop_desc for start of next pkt */
		tx_buffer++;
		tx_desc++;
		i++;
		if (unlikely(!i)) {
			i -= tx_ring->count;
			tx_buffer = tx_ring->tx_buffer_info;
			tx_desc = M_TX_DESC(tx_ring, 0);
		}

		prefetch(tx_desc);
		budget--;
	} while (likely(budget > 0));

	i += tx_ring->count;
	tx_ring->next_to_clean = i;
	u64_stats_update_begin(&tx_ring->stats->syncp);
	tx_ring->stats->bytes += total_bytes;
	tx_ring->stats->packets += total_packets;
	u64_stats_update_end(&tx_ring->stats->syncp);

#define TX_WAKE_THRESHOLD (DESC_NEEDED * 2)
	__netif_txq_completed_wake(txring_txq(tx_ring),
				   total_packets, total_bytes,
				   mucse_desc_unused(tx_ring),
				   TX_WAKE_THRESHOLD,
				   !netif_running(mucse->netdev));

	return !!budget;
}

/**
 * rnpgbe_poll - NAPI polling callback
 * @napi: structure for representing this polling device
 * @budget: polling budget
 *
 * Clean completed TX packets and complete NAPI polling when all queues are
 * clean.
 *
 * Return: 0 if all TX queues are clean, otherwise @budget
 **/
static int rnpgbe_poll(struct napi_struct *napi, int budget)
{
	struct mucse_q_vector *q_vector =
		container_of(napi, struct mucse_q_vector, napi);
	bool clean_complete = true;
	struct mucse_ring *ring;
	int work_done = 0;

	mucse_for_each_ring(ring, q_vector->tx) {
		if (!rnpgbe_clean_tx_irq(q_vector, ring, budget))
			clean_complete = false;
	}

	/* Exit if we are called by netpoll */
	if (unlikely(!budget))
		return 0;

	if (!clean_complete)
		return budget;

	if (likely(napi_complete_done(napi, work_done)))
		rnpgbe_irq_enable_queues(q_vector);

	return work_done;
}

/**
 * rnpgbe_request_mbx_irq - Register mbx routine
 * @mucse: pointer to private structure
 *
 * Register the dedicated mailbox handler on vector 0.
 *
 * Return: 0 on success, negative on failure
 **/
int rnpgbe_request_mbx_irq(struct mucse *mucse)
{
	struct pci_dev *pdev = mucse->pdev;
	int err = 0;

	snprintf(mucse->mbx_name, sizeof(mucse->mbx_name),
		 "rnpgbe-mbx:%s", pci_name(pdev));
	INIT_WORK(&mucse->mbx_work, rnpgbe_mbx_work);

	err = request_irq(pci_irq_vector(pdev, 0), rnpgbe_msix_other, 0,
			  mucse->mbx_name, mucse);
	if (!err)
		mucse->mbx_irq_requested = true;

	return err;
}

/**
 * rnpgbe_free_mbx_irq - Remove mbx routine
 * @mucse: pointer to private structure
 **/
void rnpgbe_free_mbx_irq(struct mucse *mucse)
{
	struct pci_dev *pdev = mucse->pdev;

	if (mucse->mbx_irq_requested) {
		mucse->mbx_irq_requested = false;
		free_irq(pci_irq_vector(pdev, 0), mucse);
		cancel_work_sync(&mucse->mbx_work);
	}
}

/**
 * rnpgbe_set_num_queues - Set initial TX/RX queue counts
 * @mucse: pointer to private structure
 *
 * Start with one TX queue and one RX queue.
 **/
static void rnpgbe_set_num_queues(struct mucse *mucse)
{
	mucse->num_tx_queues = 1;
	mucse->num_rx_queues = 1;
}

/**
 * rnpgbe_set_interrupt_capability - Set MSI-X interrupt capability
 * @mucse: pointer to private structure
 *
 * Allocate one MSI-X vector for the mailbox and one or more vectors for
 * the data path.
 *
 * Return: 0 on success, negative on failure
 **/
static int rnpgbe_set_interrupt_capability(struct mucse *mucse)
{
	int v_budget;

	v_budget = min3(mucse->num_tx_queues, mucse->num_rx_queues,
			MAX_Q_VECTORS);
	v_budget = min_t(int, v_budget, num_online_cpus());
	/* add one vector for mbx */
	v_budget += 1;

	/* Vector 0 is reserved for mailbox events and must not share NAPI. */
	v_budget = pci_alloc_irq_vectors(mucse->pdev, 2, v_budget,
					 PCI_IRQ_MSIX);
	if (v_budget < 0)
		return v_budget;

	/* q_vectors do not include the mailbox vector. */
	mucse->num_q_vectors = v_budget - 1;

	return 0;
}

/**
 * mucse_add_ring - Add ring to ring container
 * @ring: ring to be added
 * @head: ring container
 **/
static void mucse_add_ring(struct mucse_ring *ring,
			   struct mucse_ring_container *head)
{
	ring->next = head->ring;
	head->ring = ring;
	head->count++;
}

/**
 * rnpgbe_alloc_q_vector - Allocate memory for a single interrupt vector
 * @mucse: pointer to private structure
 * @eth_queue_idx: queue_index idx for this q_vector
 * @vector_idx: q_vector array index
 * @r_idx: starting hardware ring index
 * @r_count: number of TX/RX ring pairs
 * @step: ring step
 *
 * Return: 0 on success. If allocation fails we return -ENOMEM.
 **/
static int rnpgbe_alloc_q_vector(struct mucse *mucse,
				 int eth_queue_idx, int vector_idx, int r_idx,
				 int r_count, int step)
{
	int rxr_idx = r_idx, txr_idx = r_idx;
	struct mucse_hw *hw = &mucse->hw;
	struct mucse_q_vector *q_vector;
	int txr_count, rxr_count, idx;
	struct mucse_ring *ring;
	int ring_count;

	/*
	 * TX and RX rings are always allocated as pairs with the same hardware
	 * ring index. Interrupt control is shared by the pair, so the TX ring
	 * list is used to update the common registers.
	 */
	txr_count = r_count;
	rxr_count = r_count;
	ring_count = txr_count + rxr_count;

	q_vector = kzalloc_flex(*q_vector, ring, ring_count);
	if (!q_vector)
		return -ENOMEM;

	netif_napi_add(mucse->netdev, &q_vector->napi, rnpgbe_poll);
	/* tie q_vector and mucse together */
	mucse->q_vector[vector_idx] = q_vector;
	q_vector->mucse = mucse;
	q_vector->hw_vector = vector_idx;
	/* Vector 0 is reserved for the mailbox. */
	q_vector->hw_vector++;

	ring = q_vector->ring;

	for (idx = 0; idx < txr_count; idx++) {
		ring->dev = &mucse->pdev->dev;
		mucse_add_ring(ring, &q_vector->tx);
		ring->count = mucse->tx_ring_item_count;
		ring->netdev = mucse->netdev;
		ring->queue_index = eth_queue_idx + idx;
		ring->rnpgbe_queue_idx = txr_idx;
		ring->ring_addr = hw->hw_addr + RING_OFFSET(txr_idx);
		ring->irq_mask = ring->ring_addr + RNPGBE_DMA_INT_MASK;
		ring->trig = ring->ring_addr + RNPGBE_DMA_INT_TRIG;
		ring->q_vector = q_vector;
		ring->pfvfnum = hw->pfvfnum;
		ring->stats = &mucse->tx_stats[ring->queue_index];
		mucse->tx_ring[ring->queue_index] = ring;
		txr_idx += step;
		ring++;
	}

	for (idx = 0; idx < rxr_count; idx++) {
		mucse_add_ring(ring, &q_vector->rx);
		ring->queue_index = eth_queue_idx + idx;
		ring->rnpgbe_queue_idx = rxr_idx;
		ring->ring_addr = hw->hw_addr + RING_OFFSET(rxr_idx);
		ring->irq_mask = ring->ring_addr + RNPGBE_DMA_INT_MASK;
		ring->trig = ring->ring_addr + RNPGBE_DMA_INT_TRIG;
		ring->q_vector = q_vector;
		mucse->rx_ring[ring->queue_index] = ring;
		rxr_idx += step;
		ring++;
	}

	return 0;
}

/**
 * rnpgbe_free_q_vector - Free memory allocated for specific interrupt vector
 * @mucse: pointer to private structure
 * @vector_idx: q_vector array index
 *
 * This function frees the memory allocated to the q_vector.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void rnpgbe_free_q_vector(struct mucse *mucse, int vector_idx)
{
	struct mucse_q_vector *q_vector = mucse->q_vector[vector_idx];
	struct mucse_ring *ring;

	mucse_for_each_ring(ring, q_vector->tx)
		mucse->tx_ring[ring->queue_index] = NULL;
	mucse_for_each_ring(ring, q_vector->rx)
		mucse->rx_ring[ring->queue_index] = NULL;
	mucse->q_vector[vector_idx] = NULL;
	netif_napi_del(&q_vector->napi);
	kfree(q_vector);
}

/**
 * rnpgbe_alloc_q_vectors - Allocate memory for interrupt vectors
 * @mucse: pointer to private structure
 *
 * Return: 0 on success, or -ENOMEM on allocation failure.
 **/
static int rnpgbe_alloc_q_vectors(struct mucse *mucse)
{
	int err, ring_cnt, v_remaining = mucse->num_q_vectors;
	int r_remaining = min_t(int, mucse->num_tx_queues,
			      mucse->num_rx_queues);
	int num_q_vectors = 0;
	int eth_queue_idx = 0;
	int vector_idx = 0;
	int ring_step = 1;
	int ring_idx = 0;

	for (; r_remaining > 0 && v_remaining > 0; v_remaining--) {
		ring_cnt = DIV_ROUND_UP(r_remaining, v_remaining);
		err = rnpgbe_alloc_q_vector(mucse, eth_queue_idx,
					    vector_idx, ring_idx, ring_cnt,
					    ring_step);
		if (err)
			goto err_free_q_vector;
		ring_idx += ring_step * ring_cnt;
		eth_queue_idx += ring_cnt;
		r_remaining -= ring_cnt;
		num_q_vectors++;
		vector_idx++;
	}
	/* Record the number of queue vectors actually allocated */
	mucse->num_q_vectors = num_q_vectors;
	mucse->num_tx_queues = eth_queue_idx;
	mucse->num_rx_queues = eth_queue_idx;

	return 0;

err_free_q_vector:
	mucse->num_tx_queues = 0;
	mucse->num_rx_queues = 0;
	mucse->num_q_vectors = 0;

	while (vector_idx--)
		rnpgbe_free_q_vector(mucse, vector_idx);

	return err;
}

/**
 * rnpgbe_reset_interrupt_capability - Reset irq capability setup
 * @mucse: pointer to private structure
 **/
static void rnpgbe_reset_interrupt_capability(struct mucse *mucse)
{
	if (!mucse->pdev->msix_enabled)
		return;

	pci_free_irq_vectors(mucse->pdev);
}

/**
 * rnpgbe_init_interrupt_scheme - Initialize the interrupt scheme
 * @mucse: pointer to private structure
 *
 * Set up one TX/RX queue pair with a dedicated mailbox vector.
 *
 * Return: 0 on success, negative on failure
 **/
int rnpgbe_init_interrupt_scheme(struct mucse *mucse)
{
	int err;

	rnpgbe_set_num_queues(mucse);

	err = rnpgbe_set_interrupt_capability(mucse);
	if (err)
		return err;

	err = rnpgbe_alloc_q_vectors(mucse);
	if (err) {
		rnpgbe_reset_interrupt_capability(mucse);
		return err;
	}

	return 0;
}

/**
 * rnpgbe_free_q_vectors - Free memory allocated for interrupt vectors
 * @mucse: pointer to private structure
 *
 * This function frees the memory allocated to the q_vectors.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void rnpgbe_free_q_vectors(struct mucse *mucse)
{
	int vector_idx = mucse->num_q_vectors;

	mucse->num_q_vectors = 0;

	while (vector_idx--)
		rnpgbe_free_q_vector(mucse, vector_idx);
}

/**
 * rnpgbe_clear_interrupt_scheme - Clear the current interrupt scheme settings
 * @mucse: pointer to private structure
 *
 * Clear interrupt specific resources and reset the structure
 **/
void rnpgbe_clear_interrupt_scheme(struct mucse *mucse)
{
	rnpgbe_free_q_vectors(mucse);
	rnpgbe_reset_interrupt_capability(mucse);
}

/**
 * rnpgbe_msix_clean_rings - MSI-x interrupt handler for ring irq
 * @irq: interrupt number
 * @data: private data
 *
 * rnpgbe_msix_clean_rings handle irq from ring, start napi
 * Return: IRQ_HANDLED
 **/
static irqreturn_t rnpgbe_msix_clean_rings(int irq, void *data)
{
	struct mucse_q_vector *q_vector = (struct mucse_q_vector *)data;

	rnpgbe_irq_disable_queues(q_vector);
	napi_schedule_irqoff(&q_vector->napi);

	return IRQ_HANDLED;
}

/**
 * rnpgbe_request_irq - Register data-path MSI-X handlers
 * @mucse: pointer to private structure
 *
 * Register interrupt handlers for the data-path MSI-X vectors.
 *
 * Return: 0 on success, negative value on failure
 **/
int rnpgbe_request_irq(struct mucse *mucse)
{
	struct net_device *netdev = mucse->netdev;
	struct pci_dev *pdev = mucse->pdev;
	struct mucse_q_vector *q_vector;
	int err, i;

	for (i = 0; i < mucse->num_q_vectors; i++) {
		q_vector = mucse->q_vector[i];

		snprintf(q_vector->name, sizeof(q_vector->name),
			 "%s-%s-%d", netdev->name, "TxRx", i);

		err = request_irq(pci_irq_vector(pdev, i + 1),
				  rnpgbe_msix_clean_rings, 0,
				  q_vector->name, q_vector);
		if (err) {
			dev_err(&pdev->dev, "MSI-X req err %d: %d\n",
				i + 1, err);
			goto err_free_irqs;
		}
	}

	return 0;
err_free_irqs:
	while (i--) {
		q_vector = mucse->q_vector[i];
		synchronize_irq(pci_irq_vector(pdev, i + 1));
		free_irq(pci_irq_vector(pdev, i + 1), q_vector);
	}

	return err;
}

/**
 * rnpgbe_free_irq - Free data-path MSI-X handlers
 * @mucse: pointer to private structure
 **/
void rnpgbe_free_irq(struct mucse *mucse)
{
	struct pci_dev *pdev = mucse->pdev;
	struct mucse_q_vector *q_vector;

	for (int i = 0; i < mucse->num_q_vectors; i++) {
		q_vector = mucse->q_vector[i];
		if (!q_vector)
			continue;

		free_irq(pci_irq_vector(pdev, i + 1), q_vector);
	}
}

/**
 * rnpgbe_set_ring_vector - Set the ring_vector registers,
 * mapping interrupt causes to vectors
 * @mucse: pointer to private structure
 * @queue: queue to map the corresponding interrupt to
 * @vector: the vector num to map to the corresponding queue
 *
 */
static void rnpgbe_set_ring_vector(struct mucse *mucse,
				   u8 queue, u8 vector)
{
	struct mucse_hw *hw = &mucse->hw;
	u32 data;

	data = hw->pfvfnum << 24;
	data |= (vector << 8);
	data |= vector;
	writel(data, hw->ring_msix_base + RING_VECTOR(queue));
}

/**
 * rnpgbe_configure_msix - Configure MSI-X hardware
 * @mucse: pointer to private structure
 *
 * rnpgbe_configure_msix sets up the hardware to properly generate MSI-X
 * interrupts.
 **/
static void rnpgbe_configure_msix(struct mucse *mucse)
{
	struct mucse_q_vector *q_vector;

	for (int i = 0; i < mucse->num_q_vectors; i++) {
		struct mucse_ring *ring;

		q_vector = mucse->q_vector[i];
		/* TX/RX pairs share vector registers.
		 * Update them through TX.
		 */
		mucse_for_each_ring(ring, q_vector->tx) {
			rnpgbe_set_ring_vector(mucse, ring->rnpgbe_queue_idx,
					       q_vector->hw_vector);
		}
	}
}

static void rnpgbe_irq_enable(struct mucse *mucse)
{
	for (int i = 0; i < mucse->num_q_vectors; i++)
		rnpgbe_irq_enable_queues(mucse->q_vector[i]);
}

/**
 * rnpgbe_irq_disable - Mask queue interrupt generation on the NIC
 * @mucse: board private structure
 **/
static void rnpgbe_irq_disable(struct mucse *mucse)
{
	struct pci_dev *pdev = mucse->pdev;

	for (int i = 0; i < mucse->num_q_vectors; i++) {
		rnpgbe_irq_disable_queues(mucse->q_vector[i]);
		synchronize_irq(pci_irq_vector(pdev, i + 1));
	}
}

static void rnpgbe_napi_enable_all(struct mucse *mucse)
{
	for (int i = 0; i < mucse->num_q_vectors; i++)
		napi_enable(&mucse->q_vector[i]->napi);
}

static void rnpgbe_napi_disable_all(struct mucse *mucse)
{
	for (int i = 0; i < mucse->num_q_vectors; i++)
		napi_disable(&mucse->q_vector[i]->napi);
}

static void rnpgbe_stop_tx_ring(struct mucse_ring *tx_ring)
{
	if (!tx_ring->tx_buffer_info)
		return;

	/* Stop hw. No new descriptors are fetched after TX_START=0.
	 * DMA for descriptors fetched before the stop may still be in flight.
	 */
	mucse_ring_wr32(tx_ring, RNPGBE_TX_START, 0);
	/* Flush posted write to ensure hardware sees TX_START=0 */
	(void)mucse_ring_rd32(tx_ring, RNPGBE_TX_START);
}

static int rnpgbe_wait_tx_dma_idle(struct mucse *mucse)
{
	struct mucse_hw *hw = &mucse->hw;
	u32 dma_status;
	int err;

	/* A timeout indicates a terminal AXI fault. Hardware stops all PCIe
	 * DMA requests in this state, including requests accepted before the
	 * fault. Teardown may therefore release mappings and descriptor memory.
	 * Recovery requires a chip-level reset.
	 */
	err = readl_poll_timeout(hw->hw_addr + RNPGBE_DMA_STATUS,
				 dma_status,
				 (dma_status & RNPGBE_DMA_TX_STATUS) ==
				 RNPGBE_DMA_TX_STATUS,
				 10, 100000);
	if (err) {
		set_bit(__MUCSE_AXI_FAULT, &mucse->state);
		dev_err(&mucse->pdev->dev,
			"TX DMA failed to quiesce, status %#x\n", dma_status);
	}

	return err;
}

/**
 * rnpgbe_stop_all_tx_rings - Stop TX DMA on all queues
 * @mucse: board private structure
 *
 * Return: 0 when TX DMA was quiesced, negative errno otherwise
 **/
static int rnpgbe_stop_all_tx_rings(struct mucse *mucse)
{
	struct mucse_hw *hw = &mucse->hw;
	u32 dma_axi_ctl;
	int err = 0;

	for (int i = 0; i < mucse->num_tx_queues; i++)
		rnpgbe_stop_tx_ring(mucse->tx_ring[i]);

	if (mucse->num_tx_queues &&
	    !test_bit(__MUCSE_AXI_FAULT, &mucse->state))
		err = rnpgbe_wait_tx_dma_idle(mucse);

	dma_axi_ctl = mucse_hw_rd32(hw, RNPGBE_DMA_AXI_EN);
	dma_axi_ctl &= ~TX_AXI_RW_EN;
	mucse_hw_wr32(hw, RNPGBE_DMA_AXI_EN, dma_axi_ctl);
	/* Flush the posted write before continuing. */
	(void)mucse_hw_rd32(hw, RNPGBE_DMA_AXI_EN);

	if (!test_bit(__MUCSE_AXI_FAULT, &mucse->state))
		return 0;

	return err ? err : -EIO;
}

/**
 * rnpgbe_clean_tx_ring - Free Tx Buffers
 * @tx_ring: ring to be cleaned
 **/
static void rnpgbe_clean_tx_ring(struct mucse_ring *tx_ring)
{
	struct mucse_tx_buffer *tx_buffer;
	u16 i = tx_ring->next_to_clean;
	unsigned long size;

	/* ring already cleared, nothing to do */
	if (!tx_ring->tx_buffer_info)
		return;

	tx_buffer = &tx_ring->tx_buffer_info[i];

	while (i != tx_ring->next_to_use) {
		struct rnpgbe_tx_desc *eop_desc, *tx_desc;

		dev_kfree_skb_any(tx_buffer->skb);
		/* unmap skb header data */
		if (dma_unmap_len(tx_buffer, len)) {
			if (tx_buffer->mapped_as_page) {
				dma_unmap_page(tx_ring->dev,
					       dma_unmap_addr(tx_buffer, dma),
					       dma_unmap_len(tx_buffer, len),
					       DMA_TO_DEVICE);
			} else {
				dma_unmap_single(tx_ring->dev,
						 dma_unmap_addr(tx_buffer, dma),
						 dma_unmap_len(tx_buffer, len),
						 DMA_TO_DEVICE);
			}
		}
		eop_desc = tx_buffer->next_to_watch;
		tx_desc = M_TX_DESC(tx_ring, i);
		/* unmap remaining buffers */
		while (tx_desc != eop_desc) {
			tx_buffer++;
			tx_desc++;
			i++;
			if (unlikely(i == tx_ring->count)) {
				i = 0;
				tx_buffer = tx_ring->tx_buffer_info;
				tx_desc = M_TX_DESC(tx_ring, 0);
			}

			/* unmap any remaining paged data */
			if (dma_unmap_len(tx_buffer, len))
				dma_unmap_page(tx_ring->dev,
					       dma_unmap_addr(tx_buffer, dma),
					       dma_unmap_len(tx_buffer, len),
					       DMA_TO_DEVICE);
		}
		/* move us one more past the eop_desc for start of next pkt */
		tx_buffer++;
		i++;
		if (unlikely(i == tx_ring->count)) {
			i = 0;
			tx_buffer = tx_ring->tx_buffer_info;
		}
	}

	netdev_tx_reset_queue(txring_txq(tx_ring));
	size = sizeof(struct mucse_tx_buffer) * tx_ring->count;
	memset(tx_ring->tx_buffer_info, 0, size);
	/* Zero out the descriptor ring */
	memset(tx_ring->desc, 0, tx_ring->size);
	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
}

/**
 * rnpgbe_clean_all_tx_rings - Stop TX DMA and free Tx buffers for all queues
 * @mucse: board private structure
 **/
void rnpgbe_clean_all_tx_rings(struct mucse *mucse)
{
	/* rnpgbe_stop_all_tx_rings() logs and latches any AXI fault. Hardware
	 * stops DMA requests in this state, so cleanup intentionally ignores
	 * an error and releases the mappings and descriptor memory.
	 */
	rnpgbe_stop_all_tx_rings(mucse);

	for (int i = 0; i < mucse->num_tx_queues; i++)
		rnpgbe_clean_tx_ring(mucse->tx_ring[i]);
}

void rnpgbe_down(struct mucse *mucse)
{
	struct net_device *netdev = mucse->netdev;

	rnpgbe_irq_disable(mucse);
	rnpgbe_napi_disable_all(mucse);
	synchronize_net();
	rnpgbe_irq_disable(mucse);
	netif_tx_disable(netdev);
	rnpgbe_clean_all_tx_rings(mucse);
}

/**
 * rnpgbe_up_complete - Final step for port up
 * @mucse: pointer to private structure
 **/
void rnpgbe_up_complete(struct mucse *mucse)
{
	struct net_device *netdev = mucse->netdev;

	rnpgbe_configure_msix(mucse);
	rnpgbe_napi_enable_all(mucse);
	rnpgbe_irq_enable(mucse);
	netif_tx_start_all_queues(netdev);
}

/**
 * rnpgbe_free_tx_resources - Free Tx Resources per Queue
 * @tx_ring: tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 **/
static void rnpgbe_free_tx_resources(struct mucse_ring *tx_ring)
{
	vfree(tx_ring->tx_buffer_info);
	tx_ring->tx_buffer_info = NULL;
	/* if not set, then don't free */
	if (!tx_ring->desc)
		return;

	dma_free_coherent(tx_ring->dev, tx_ring->size, tx_ring->desc,
			  tx_ring->dma);
	tx_ring->desc = NULL;
}

/**
 * rnpgbe_setup_tx_resources - allocate Tx resources (Descriptors)
 * @tx_ring: tx descriptor ring (for a specific queue) to setup
 *
 * Return: 0 on success, negative on failure
 **/
static int rnpgbe_setup_tx_resources(struct mucse_ring *tx_ring)
{
	struct device *dev = tx_ring->dev;
	int size;

	size = sizeof(struct mucse_tx_buffer) * tx_ring->count;

	tx_ring->tx_buffer_info = vzalloc(size);
	if (!tx_ring->tx_buffer_info)
		goto err_return;
	/* round up to nearest 4K */
	tx_ring->size = tx_ring->count * sizeof(struct rnpgbe_tx_desc);
	tx_ring->size = ALIGN(tx_ring->size, 4096);
	tx_ring->desc = dma_alloc_coherent(dev, tx_ring->size, &tx_ring->dma,
					   GFP_KERNEL);
	if (!tx_ring->desc)
		goto err_free_buffer;

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	return 0;

err_free_buffer:
	vfree(tx_ring->tx_buffer_info);
err_return:
	tx_ring->tx_buffer_info = NULL;
	return -ENOMEM;
}

/**
 * rnpgbe_configure_tx_ring - Configure Tx ring after Reset
 * @mucse: pointer to private structure
 * @ring: structure containing ring specific data
 *
 * Configure the Tx descriptor ring after a reset.
 **/
static void rnpgbe_configure_tx_ring(struct mucse *mucse,
				     struct mucse_ring *ring)
{
	struct mucse_hw *hw = &mucse->hw;

	mucse_ring_wr32(ring, RNPGBE_TX_BASE_ADDR_LO, (u32)ring->dma);
	mucse_ring_wr32(ring, RNPGBE_TX_BASE_ADDR_HI,
			(u32)(((u64)ring->dma) >> 32) |
			((u32)hw->pfvfnum << 24));
	mucse_ring_wr32(ring, RNPGBE_TX_LEN, ring->count);
	ring->next_to_clean = mucse_ring_rd32(ring, RNPGBE_TX_HEAD) %
			      ring->count;
	ring->next_to_use = ring->next_to_clean;
	ring->tail = ring->ring_addr + RNPGBE_TX_TAIL;
	writel(ring->next_to_use, ring->tail);
	mucse_ring_wr32(ring, RNPGBE_TX_FETCH_CTRL, M_DEFAULT_TX_FETCH);
	mucse_ring_wr32(ring, RNPGBE_TX_INT_TIMER,
			M_DEFAULT_INT_TIMER * hw->cycles_per_us);
	mucse_ring_wr32(ring, RNPGBE_TX_INT_PKTCNT, M_DEFAULT_INT_PKTCNT);
}

/**
 * rnpgbe_configure_tx - Configure Transmit Unit after Reset
 * @mucse: pointer to private structure
 *
 * Configure the Tx DMA after a reset.
 *
 * Return: 0 on success, negative errno if TX DMA did not quiesce
 **/
int rnpgbe_configure_tx(struct mucse *mucse)
{
	struct mucse_hw *hw = &mucse->hw;
	u32 i, dma_axi_ctl;
	int err;

	err = rnpgbe_stop_all_tx_rings(mucse);
	if (err)
		return err;

	for (i = 0; i < mucse->num_tx_queues; i++)
		rnpgbe_configure_tx_ring(mucse, mucse->tx_ring[i]);

	/* Ensure all ring configuration is visible before enabling TX DMA. */
	wmb();
	dma_axi_ctl = mucse_hw_rd32(hw, RNPGBE_DMA_AXI_EN);
	dma_axi_ctl |= TX_AXI_RW_EN;
	mucse_hw_wr32(hw, RNPGBE_DMA_AXI_EN, dma_axi_ctl);
	/* Ensure TX_AXI_RW_EN is visible before starting the rings. */
	(void)mucse_hw_rd32(hw, RNPGBE_DMA_AXI_EN);

	for (i = 0; i < mucse->num_tx_queues; i++)
		mucse_ring_wr32(mucse->tx_ring[i], RNPGBE_TX_START, 1);

	return 0;
}

/**
 * rnpgbe_setup_all_tx_resources - allocate all queues Tx resources
 * @mucse: pointer to private structure
 *
 * Allocate memory for tx_ring.
 *
 * Return: 0 on success, negative on failure
 **/
int rnpgbe_setup_all_tx_resources(struct mucse *mucse)
{
	int i, err = 0;

	for (i = 0; i < mucse->num_tx_queues; i++) {
		err = rnpgbe_setup_tx_resources(mucse->tx_ring[i]);
		if (!err)
			continue;

		goto err_free_res;
	}

	return 0;
err_free_res:
	while (i--)
		rnpgbe_free_tx_resources(mucse->tx_ring[i]);
	return err;
}

/**
 * rnpgbe_free_all_tx_resources - Free Tx Resources for All Queues
 * @mucse: pointer to private structure
 *
 * Free all transmit software resources
 **/
void rnpgbe_free_all_tx_resources(struct mucse *mucse)
{
	for (int i = 0; i < (mucse->num_tx_queues); i++)
		rnpgbe_free_tx_resources(mucse->tx_ring[i]);
}

static int rnpgbe_tx_map(struct mucse_ring *tx_ring,
			 struct mucse_tx_buffer *first, u32 mac_ip_len,
			 u32 tx_flags)
{
	struct mucse_tx_buffer *tx_buffer;
	struct rnpgbe_tx_desc *tx_desc;
	struct skb_shared_info *shinfo;
	unsigned int data_len, size;
	struct sk_buff *skb;
	skb_frag_t *frag;
	dma_addr_t dma;
	int nr_frags;
	int frag_idx;
	/* Hardware requires this in the upper 8 bits of
	 * the descriptor address
	 */
	u64 fun_id;
	u16 i;

	skb = first->skb;
	shinfo = skb_shinfo(skb);
	fun_id = (u64)tx_ring->pfvfnum << 56;
	nr_frags = shinfo->nr_frags;
	i = tx_ring->next_to_use;
	frag_idx = 0;
	tx_desc = M_TX_DESC(tx_ring, i);
	size = skb_headlen(skb);
	data_len = skb->data_len;

	if (size) {
		dma = dma_map_single(tx_ring->dev, skb->data, size,
				     DMA_TO_DEVICE);
		first->mapped_as_page = false;
	} else if (data_len) {
		while (frag_idx < nr_frags &&
		       !skb_frag_size(&shinfo->frags[frag_idx]))
			frag_idx++;

		if (frag_idx == nr_frags)
			goto err_unmap;

		frag = &shinfo->frags[frag_idx];
		size = skb_frag_size(frag);

		dma = skb_frag_dma_map(tx_ring->dev, frag, 0,
				       size, DMA_TO_DEVICE);
		first->mapped_as_page = true;
		data_len -= size;
		frag_idx++;
	} else {
		goto err_unmap;
	}

	tx_buffer = first;

	dma_unmap_len_set(tx_buffer, len, 0);
	dma_unmap_addr_set(tx_buffer, dma, 0);

	for (;; frag_idx++) {
		if (dma_mapping_error(tx_ring->dev, dma))
			goto err_unmap;

		/* record length, and DMA address */
		dma_unmap_len_set(tx_buffer, len, size);
		dma_unmap_addr_set(tx_buffer, dma, dma);

		tx_desc->pkt_addr = cpu_to_le64(dma | fun_id);

		while (unlikely(size > M_MAX_DATA_PER_TXD)) {
			tx_desc->vlan_cmd_bsz = build_ctob(tx_flags,
							   mac_ip_len,
							   M_MAX_DATA_PER_TXD);
			i++;
			tx_desc++;
			if (i == tx_ring->count) {
				tx_desc = M_TX_DESC(tx_ring, 0);
				i = 0;
			}

			tx_buffer = &tx_ring->tx_buffer_info[i];
			dma_unmap_len_set(tx_buffer, len, 0);
			dma += M_MAX_DATA_PER_TXD;
			size -= M_MAX_DATA_PER_TXD;
			tx_desc->pkt_addr = cpu_to_le64(dma | fun_id);
		}

		if (likely(!data_len))
			break;
		tx_desc->vlan_cmd_bsz = build_ctob(tx_flags, mac_ip_len, size);
		i++;
		tx_desc++;
		if (i == tx_ring->count) {
			tx_desc = M_TX_DESC(tx_ring, 0);
			i = 0;
		}

		while (frag_idx < nr_frags &&
		       !skb_frag_size(&shinfo->frags[frag_idx]))
			frag_idx++;

		if (frag_idx == nr_frags)
			goto err_unmap;

		frag = &shinfo->frags[frag_idx];
		size = skb_frag_size(frag);
		data_len -= size;
		dma = skb_frag_dma_map(tx_ring->dev, frag, 0, size,
				       DMA_TO_DEVICE);
		tx_buffer = &tx_ring->tx_buffer_info[i];
		tx_buffer->mapped_as_page = true;
	}

	/* write last descriptor with RS and EOP bits */
	tx_desc->vlan_cmd_bsz = build_ctob(tx_flags | M_TXD_CMD_EOP |
					   M_TXD_CMD_RS,
					   mac_ip_len, size);

	/* Force memory writes to complete before letting h/w know there
	 * are new descriptors to fetch.  (Only applicable for weak-ordered
	 * memory model archs, such as IA-64).
	 *
	 * We also need this memory barrier to make certain all of the
	 * status bits have been updated before next_to_watch is written.
	 */
	dma_wmb();
	/* set next_to_watch value indicating a packet is present */
	first->next_to_watch = tx_desc;
	i++;
	if (i == tx_ring->count)
		i = 0;
	tx_ring->next_to_use = i;
	skb_tx_timestamp(skb);
	netdev_tx_sent_queue(txring_txq(tx_ring), first->bytecount);
	/* notify HW of packet */
	writel(i, tx_ring->tail);

	return 0;
err_unmap:
	for (;;) {
		tx_buffer = &tx_ring->tx_buffer_info[i];
		if (dma_unmap_len(tx_buffer, len)) {
			if (tx_buffer->mapped_as_page) {
				dma_unmap_page(tx_ring->dev,
					       dma_unmap_addr(tx_buffer, dma),
					       dma_unmap_len(tx_buffer, len),
					       DMA_TO_DEVICE);
			} else {
				dma_unmap_single(tx_ring->dev,
						 dma_unmap_addr(tx_buffer, dma),
						 dma_unmap_len(tx_buffer, len),
						 DMA_TO_DEVICE);
			}
		}
		dma_unmap_len_set(tx_buffer, len, 0);
		dma_unmap_addr_set(tx_buffer, dma, 0);
		if (tx_buffer == first)
			break;
		if (i == 0)
			i += tx_ring->count;
		i--;
	}
	dev_kfree_skb_any(first->skb);
	first->skb = NULL;
	tx_ring->next_to_use = i;

	return -ENOMEM;
}

netdev_tx_t rnpgbe_xmit_frame_ring(struct sk_buff *skb,
				   struct mucse_ring *tx_ring)
{
	/* hw requires non-zero mac_ip_len even without offload;
	 * Will be updated per-packet when offload is added
	 */
	u32 mac_ip_len = M_DEFAULT_MAC_IP_LEN;
	struct mucse_tx_buffer *first;
	u32 tx_flags = 0;
	unsigned short f;
	u16 count;

	count = TXD_USE_COUNT(skb_headlen(skb));
	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++) {
		skb_frag_t *frag_temp = &skb_shinfo(skb)->frags[f];

		count += TXD_USE_COUNT(skb_frag_size(frag_temp));
	}

	if (!netif_txq_maybe_stop(txring_txq(tx_ring),
				  mucse_desc_unused(tx_ring),
				  count + RESV_DESC_NEEDED,
				  count + RESV_DESC_NEEDED))
		return NETDEV_TX_BUSY;

	/* record the location of the first descriptor for this packet */
	first = &tx_ring->tx_buffer_info[tx_ring->next_to_use];
	first->skb = skb;
	first->bytecount = skb->len;
	first->gso_segs = 1;

	if (rnpgbe_tx_map(tx_ring, first, mac_ip_len, tx_flags)) {
		atomic64_inc(&tx_ring->stats->dropped);

		goto out;
	}

	/* NETDEV_TX_BUSY is expensive. So stop advancing the TX queue. */
	netif_txq_maybe_stop(txring_txq(tx_ring),
			     mucse_desc_unused(tx_ring),
			     DESC_NEEDED, DESC_NEEDED);
out:
	return NETDEV_TX_OK;
}

/**
 * rnpgbe_get_stats64 - Get stats for this netdev
 * @netdev: network interface device structure
 * @stats: stats data
 **/
void rnpgbe_get_stats64(struct net_device *netdev,
			struct rtnl_link_stats64 *stats)
{
	struct mucse *mucse = netdev_priv(netdev);
	int i;

	for (i = 0; i < RNPGBE_MAX_QUEUES; i++) {
		unsigned int start;
		u64 bytes, packets, dropped;
		struct mucse_queue_stats *queue_stats = &mucse->tx_stats[i];

		do {
			start = u64_stats_fetch_begin(&queue_stats->syncp);
			packets = queue_stats->packets;
			bytes = queue_stats->bytes;
		} while (u64_stats_fetch_retry(&queue_stats->syncp, start));
		dropped = atomic64_read(&queue_stats->dropped);

		stats->tx_packets += packets;
		stats->tx_dropped += dropped;
		stats->tx_bytes += bytes;
	}
}
