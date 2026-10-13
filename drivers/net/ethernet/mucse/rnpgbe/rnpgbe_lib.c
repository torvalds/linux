// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#include <linux/pci.h>
#include <linux/netdevice.h>

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
 * rnpgbe_poll - NAPI polling callback
 * @napi: structure for representing this polling device
 * @budget: polling budget
 *
 * Complete NAPI polling and re-enable queue interrupts. Ring cleaning is
 * added when TX and RX support is enabled.
 *
 * Return: 0
 **/
static int rnpgbe_poll(struct napi_struct *napi, int budget)
{
	struct mucse_q_vector *q_vector =
		container_of(napi, struct mucse_q_vector, napi);
	int work_done = 0;

	/* Exit if we are called by netpoll */
	if (unlikely(!budget))
		return 0;

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
		mucse_add_ring(ring, &q_vector->tx);
		ring->queue_index = eth_queue_idx + idx;
		ring->rnpgbe_queue_idx = txr_idx;
		ring->ring_addr = hw->hw_addr + RING_OFFSET(txr_idx);
		ring->irq_mask = ring->ring_addr + RNPGBE_DMA_INT_MASK;
		ring->trig = ring->ring_addr + RNPGBE_DMA_INT_TRIG;
		ring->q_vector = q_vector;
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

void rnpgbe_down(struct mucse *mucse)
{
	rnpgbe_irq_disable(mucse);
	rnpgbe_napi_disable_all(mucse);
	synchronize_net();
	rnpgbe_irq_disable(mucse);
}

/**
 * rnpgbe_up_complete - Final step for port up
 * @mucse: pointer to private structure
 **/
void rnpgbe_up_complete(struct mucse *mucse)
{
	rnpgbe_configure_msix(mucse);
	rnpgbe_napi_enable_all(mucse);
	rnpgbe_irq_enable(mucse);
}
