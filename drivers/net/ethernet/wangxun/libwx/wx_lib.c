// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/etherdevice.h>
#include <net/page_pool.h>
#include <linux/iopoll.h>
#include <linux/pci.h>

#include "wx_type.h"
#include "wx_lib.h"

/**
 * wx_poll - NAPI polling RX/TX cleanup routine
 * @napi: napi struct with our devices info in it
 * @budget: amount of work driver is allowed to do this pass, in packets
 *
 * This function will clean all queues associated with a q_vector.
 **/
static int wx_poll(struct napi_struct *napi, int budget)
{
	return 0;
}

/**
 * wx_set_rss_queues: Allocate queues for RSS
 * @wx: board private structure to initialize
 *
 * This is our "base" multiqueue mode.  RSS (Receive Side Scaling) will try
 * to allocate one Rx queue per CPU, and if available, one Tx queue per CPU.
 *
 **/
static void wx_set_rss_queues(struct wx *wx)
{
	wx->num_rx_queues = wx->mac.max_rx_queues;
	wx->num_tx_queues = wx->mac.max_tx_queues;
}

static void wx_set_num_queues(struct wx *wx)
{
	/* Start with base case */
	wx->num_rx_queues = 1;
	wx->num_tx_queues = 1;
	wx->queues_per_pool = 1;

	wx_set_rss_queues(wx);
}

/**
 * wx_acquire_msix_vectors - acquire MSI-X vectors
 * @wx: board private structure
 *
 * Attempts to acquire a suitable range of MSI-X vector interrupts. Will
 * return a negative error code if unable to acquire MSI-X vectors for any
 * reason.
 */
static int wx_acquire_msix_vectors(struct wx *wx)
{
	struct irq_affinity affd = {0, };
	int nvecs, i;

	nvecs = min_t(int, num_online_cpus(), wx->mac.max_msix_vectors);

	wx->msix_entries = kcalloc(nvecs,
				   sizeof(struct msix_entry),
				   GFP_KERNEL);
	if (!wx->msix_entries)
		return -ENOMEM;

	nvecs = pci_alloc_irq_vectors_affinity(wx->pdev, nvecs,
					       nvecs,
					       PCI_IRQ_MSIX | PCI_IRQ_AFFINITY,
					       &affd);
	if (nvecs < 0) {
		wx_err(wx, "Failed to allocate MSI-X interrupts. Err: %d\n", nvecs);
		kfree(wx->msix_entries);
		wx->msix_entries = NULL;
		return nvecs;
	}

	for (i = 0; i < nvecs; i++) {
		wx->msix_entries[i].entry = i;
		wx->msix_entries[i].vector = pci_irq_vector(wx->pdev, i);
	}

	/* one for msix_other */
	nvecs -= 1;
	wx->num_q_vectors = nvecs;
	wx->num_rx_queues = nvecs;
	wx->num_tx_queues = nvecs;

	return 0;
}

/**
 * wx_set_interrupt_capability - set MSI-X or MSI if supported
 * @wx: board private structure to initialize
 *
 * Attempt to configure the interrupts using the best available
 * capabilities of the hardware and the kernel.
 **/
static int wx_set_interrupt_capability(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;
	int nvecs, ret;

	/* We will try to get MSI-X interrupts first */
	ret = wx_acquire_msix_vectors(wx);
	if (ret == 0 || (ret == -ENOMEM))
		return ret;

	wx->num_rx_queues = 1;
	wx->num_tx_queues = 1;
	wx->num_q_vectors = 1;

	/* minmum one for queue, one for misc*/
	nvecs = 1;
	nvecs = pci_alloc_irq_vectors(pdev, nvecs,
				      nvecs, PCI_IRQ_MSI | PCI_IRQ_LEGACY);
	if (nvecs == 1) {
		if (pdev->msi_enabled)
			wx_err(wx, "Fallback to MSI.\n");
		else
			wx_err(wx, "Fallback to LEGACY.\n");
	} else {
		wx_err(wx, "Failed to allocate MSI/LEGACY interrupts. Error: %d\n", nvecs);
		return nvecs;
	}

	pdev->irq = pci_irq_vector(pdev, 0);

	return 0;
}

/**
 * wx_cache_ring_rss - Descriptor ring to register mapping for RSS
 * @wx: board private structure to initialize
 *
 * Cache the descriptor ring offsets for RSS, ATR, FCoE, and SR-IOV.
 *
 **/
static void wx_cache_ring_rss(struct wx *wx)
{
	u16 i;

	for (i = 0; i < wx->num_rx_queues; i++)
		wx->rx_ring[i]->reg_idx = i;

	for (i = 0; i < wx->num_tx_queues; i++)
		wx->tx_ring[i]->reg_idx = i;
}

static void wx_add_ring(struct wx_ring *ring, struct wx_ring_container *head)
{
	ring->next = head->ring;
	head->ring = ring;
	head->count++;
}

/**
 * wx_alloc_q_vector - Allocate memory for a single interrupt vector
 * @wx: board private structure to initialize
 * @v_count: q_vectors allocated on wx, used for ring interleaving
 * @v_idx: index of vector in wx struct
 * @txr_count: total number of Tx rings to allocate
 * @txr_idx: index of first Tx ring to allocate
 * @rxr_count: total number of Rx rings to allocate
 * @rxr_idx: index of first Rx ring to allocate
 *
 * We allocate one q_vector.  If allocation fails we return -ENOMEM.
 **/
static int wx_alloc_q_vector(struct wx *wx,
			     unsigned int v_count, unsigned int v_idx,
			     unsigned int txr_count, unsigned int txr_idx,
			     unsigned int rxr_count, unsigned int rxr_idx)
{
	struct wx_q_vector *q_vector;
	int ring_count, default_itr;
	struct wx_ring *ring;

	/* note this will allocate space for the ring structure as well! */
	ring_count = txr_count + rxr_count;

	q_vector = kzalloc(struct_size(q_vector, ring, ring_count),
			   GFP_KERNEL);
	if (!q_vector)
		return -ENOMEM;

	/* initialize NAPI */
	netif_napi_add(wx->netdev, &q_vector->napi,
		       wx_poll);

	/* tie q_vector and wx together */
	wx->q_vector[v_idx] = q_vector;
	q_vector->wx = wx;
	q_vector->v_idx = v_idx;
	if (cpu_online(v_idx))
		q_vector->numa_node = cpu_to_node(v_idx);

	/* initialize pointer to rings */
	ring = q_vector->ring;

	if (wx->mac.type == wx_mac_sp)
		default_itr = WX_12K_ITR;
	else
		default_itr = WX_7K_ITR;
	/* initialize ITR */
	if (txr_count && !rxr_count)
		/* tx only vector */
		q_vector->itr = wx->tx_itr_setting ?
				default_itr : wx->tx_itr_setting;
	else
		/* rx or rx/tx vector */
		q_vector->itr = wx->rx_itr_setting ?
				default_itr : wx->rx_itr_setting;

	while (txr_count) {
		/* assign generic ring traits */
		ring->dev = &wx->pdev->dev;
		ring->netdev = wx->netdev;

		/* configure backlink on ring */
		ring->q_vector = q_vector;

		/* update q_vector Tx values */
		wx_add_ring(ring, &q_vector->tx);

		/* apply Tx specific ring traits */
		ring->count = wx->tx_ring_count;

		ring->queue_index = txr_idx;

		/* assign ring to wx */
		wx->tx_ring[txr_idx] = ring;

		/* update count and index */
		txr_count--;
		txr_idx += v_count;

		/* push pointer to next ring */
		ring++;
	}

	while (rxr_count) {
		/* assign generic ring traits */
		ring->dev = &wx->pdev->dev;
		ring->netdev = wx->netdev;

		/* configure backlink on ring */
		ring->q_vector = q_vector;

		/* update q_vector Rx values */
		wx_add_ring(ring, &q_vector->rx);

		/* apply Rx specific ring traits */
		ring->count = wx->rx_ring_count;
		ring->queue_index = rxr_idx;

		/* assign ring to wx */
		wx->rx_ring[rxr_idx] = ring;

		/* update count and index */
		rxr_count--;
		rxr_idx += v_count;

		/* push pointer to next ring */
		ring++;
	}

	return 0;
}

/**
 * wx_free_q_vector - Free memory allocated for specific interrupt vector
 * @wx: board private structure to initialize
 * @v_idx: Index of vector to be freed
 *
 * This function frees the memory allocated to the q_vector.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void wx_free_q_vector(struct wx *wx, int v_idx)
{
	struct wx_q_vector *q_vector = wx->q_vector[v_idx];
	struct wx_ring *ring;

	wx_for_each_ring(ring, q_vector->tx)
		wx->tx_ring[ring->queue_index] = NULL;

	wx_for_each_ring(ring, q_vector->rx)
		wx->rx_ring[ring->queue_index] = NULL;

	wx->q_vector[v_idx] = NULL;
	netif_napi_del(&q_vector->napi);
	kfree_rcu(q_vector, rcu);
}

/**
 * wx_alloc_q_vectors - Allocate memory for interrupt vectors
 * @wx: board private structure to initialize
 *
 * We allocate one q_vector per queue interrupt.  If allocation fails we
 * return -ENOMEM.
 **/
static int wx_alloc_q_vectors(struct wx *wx)
{
	unsigned int rxr_idx = 0, txr_idx = 0, v_idx = 0;
	unsigned int rxr_remaining = wx->num_rx_queues;
	unsigned int txr_remaining = wx->num_tx_queues;
	unsigned int q_vectors = wx->num_q_vectors;
	int rqpv, tqpv;
	int err;

	for (; v_idx < q_vectors; v_idx++) {
		rqpv = DIV_ROUND_UP(rxr_remaining, q_vectors - v_idx);
		tqpv = DIV_ROUND_UP(txr_remaining, q_vectors - v_idx);
		err = wx_alloc_q_vector(wx, q_vectors, v_idx,
					tqpv, txr_idx,
					rqpv, rxr_idx);

		if (err)
			goto err_out;

		/* update counts and index */
		rxr_remaining -= rqpv;
		txr_remaining -= tqpv;
		rxr_idx++;
		txr_idx++;
	}

	return 0;

err_out:
	wx->num_tx_queues = 0;
	wx->num_rx_queues = 0;
	wx->num_q_vectors = 0;

	while (v_idx--)
		wx_free_q_vector(wx, v_idx);

	return -ENOMEM;
}

/**
 * wx_free_q_vectors - Free memory allocated for interrupt vectors
 * @wx: board private structure to initialize
 *
 * This function frees the memory allocated to the q_vectors.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void wx_free_q_vectors(struct wx *wx)
{
	int v_idx = wx->num_q_vectors;

	wx->num_tx_queues = 0;
	wx->num_rx_queues = 0;
	wx->num_q_vectors = 0;

	while (v_idx--)
		wx_free_q_vector(wx, v_idx);
}

void wx_reset_interrupt_capability(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;

	if (!pdev->msi_enabled && !pdev->msix_enabled)
		return;

	pci_free_irq_vectors(wx->pdev);
	if (pdev->msix_enabled) {
		kfree(wx->msix_entries);
		wx->msix_entries = NULL;
	}
}
EXPORT_SYMBOL(wx_reset_interrupt_capability);

/**
 * wx_clear_interrupt_scheme - Clear the current interrupt scheme settings
 * @wx: board private structure to clear interrupt scheme on
 *
 * We go through and clear interrupt specific resources and reset the structure
 * to pre-load conditions
 **/
void wx_clear_interrupt_scheme(struct wx *wx)
{
	wx_free_q_vectors(wx);
	wx_reset_interrupt_capability(wx);
}
EXPORT_SYMBOL(wx_clear_interrupt_scheme);

int wx_init_interrupt_scheme(struct wx *wx)
{
	int ret;

	/* Number of supported queues */
	wx_set_num_queues(wx);

	/* Set interrupt mode */
	ret = wx_set_interrupt_capability(wx);
	if (ret) {
		wx_err(wx, "Allocate irq vectors for failed.\n");
		return ret;
	}

	/* Allocate memory for queues */
	ret = wx_alloc_q_vectors(wx);
	if (ret) {
		wx_err(wx, "Unable to allocate memory for queue vectors.\n");
		wx_reset_interrupt_capability(wx);
		return ret;
	}

	wx_cache_ring_rss(wx);

	return 0;
}
EXPORT_SYMBOL(wx_init_interrupt_scheme);

irqreturn_t wx_msix_clean_rings(int __always_unused irq, void *data)
{
	struct wx_q_vector *q_vector = data;

	/* EIAM disabled interrupts (on this vector) for us */
	if (q_vector->rx.ring || q_vector->tx.ring)
		napi_schedule_irqoff(&q_vector->napi);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL(wx_msix_clean_rings);

void wx_free_irq(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;
	int vector;

	if (!(pdev->msix_enabled)) {
		free_irq(pdev->irq, wx);
		return;
	}

	for (vector = 0; vector < wx->num_q_vectors; vector++) {
		struct wx_q_vector *q_vector = wx->q_vector[vector];
		struct msix_entry *entry = &wx->msix_entries[vector];

		/* free only the irqs that were actually requested */
		if (!q_vector->rx.ring && !q_vector->tx.ring)
			continue;

		free_irq(entry->vector, q_vector);
	}

	free_irq(wx->msix_entries[vector].vector, wx);
}
EXPORT_SYMBOL(wx_free_irq);

/**
 * wx_setup_isb_resources - allocate interrupt status resources
 * @wx: board private structure
 *
 * Return 0 on success, negative on failure
 **/
int wx_setup_isb_resources(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;

	wx->isb_mem = dma_alloc_coherent(&pdev->dev,
					 sizeof(u32) * 4,
					 &wx->isb_dma,
					 GFP_KERNEL);
	if (!wx->isb_mem) {
		wx_err(wx, "Alloc isb_mem failed\n");
		return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL(wx_setup_isb_resources);

/**
 * wx_free_isb_resources - allocate all queues Rx resources
 * @wx: board private structure
 *
 * Return 0 on success, negative on failure
 **/
void wx_free_isb_resources(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;

	dma_free_coherent(&pdev->dev, sizeof(u32) * 4,
			  wx->isb_mem, wx->isb_dma);
	wx->isb_mem = NULL;
}
EXPORT_SYMBOL(wx_free_isb_resources);

u32 wx_misc_isb(struct wx *wx, enum wx_isb_idx idx)
{
	u32 cur_tag = 0;

	cur_tag = wx->isb_mem[WX_ISB_HEADER];
	wx->isb_tag[idx] = cur_tag;

	return (__force u32)cpu_to_le32(wx->isb_mem[idx]);
}
EXPORT_SYMBOL(wx_misc_isb);

/**
 * wx_set_ivar - set the IVAR registers, mapping interrupt causes to vectors
 * @wx: pointer to wx struct
 * @direction: 0 for Rx, 1 for Tx, -1 for other causes
 * @queue: queue to map the corresponding interrupt to
 * @msix_vector: the vector to map to the corresponding queue
 *
 **/
static void wx_set_ivar(struct wx *wx, s8 direction,
			u16 queue, u16 msix_vector)
{
	u32 ivar, index;

	if (direction == -1) {
		/* other causes */
		msix_vector |= WX_PX_IVAR_ALLOC_VAL;
		index = 0;
		ivar = rd32(wx, WX_PX_MISC_IVAR);
		ivar &= ~(0xFF << index);
		ivar |= (msix_vector << index);
		wr32(wx, WX_PX_MISC_IVAR, ivar);
	} else {
		/* tx or rx causes */
		msix_vector |= WX_PX_IVAR_ALLOC_VAL;
		index = ((16 * (queue & 1)) + (8 * direction));
		ivar = rd32(wx, WX_PX_IVAR(queue >> 1));
		ivar &= ~(0xFF << index);
		ivar |= (msix_vector << index);
		wr32(wx, WX_PX_IVAR(queue >> 1), ivar);
	}
}

/**
 * wx_write_eitr - write EITR register in hardware specific way
 * @q_vector: structure containing interrupt and ring information
 *
 * This function is made to be called by ethtool and by the driver
 * when it needs to update EITR registers at runtime.  Hardware
 * specific quirks/differences are taken care of here.
 */
static void wx_write_eitr(struct wx_q_vector *q_vector)
{
	struct wx *wx = q_vector->wx;
	int v_idx = q_vector->v_idx;
	u32 itr_reg;

	if (wx->mac.type == wx_mac_sp)
		itr_reg = q_vector->itr & WX_SP_MAX_EITR;
	else
		itr_reg = q_vector->itr & WX_EM_MAX_EITR;

	itr_reg |= WX_PX_ITR_CNT_WDIS;

	wr32(wx, WX_PX_ITR(v_idx), itr_reg);
}

/**
 * wx_configure_vectors - Configure vectors for hardware
 * @wx: board private structure
 *
 * wx_configure_vectors sets up the hardware to properly generate MSI-X/MSI/LEGACY
 * interrupts.
 **/
void wx_configure_vectors(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;
	u32 eitrsel = 0;
	u16 v_idx;

	if (pdev->msix_enabled) {
		/* Populate MSIX to EITR Select */
		wr32(wx, WX_PX_ITRSEL, eitrsel);
		/* use EIAM to auto-mask when MSI-X interrupt is asserted
		 * this saves a register write for every interrupt
		 */
		wr32(wx, WX_PX_GPIE, WX_PX_GPIE_MODEL);
	} else {
		/* legacy interrupts, use EIAM to auto-mask when reading EICR,
		 * specifically only auto mask tx and rx interrupts.
		 */
		wr32(wx, WX_PX_GPIE, 0);
	}

	/* Populate the IVAR table and set the ITR values to the
	 * corresponding register.
	 */
	for (v_idx = 0; v_idx < wx->num_q_vectors; v_idx++) {
		struct wx_q_vector *q_vector = wx->q_vector[v_idx];
		struct wx_ring *ring;

		wx_for_each_ring(ring, q_vector->rx)
			wx_set_ivar(wx, 0, ring->reg_idx, v_idx);

		wx_for_each_ring(ring, q_vector->tx)
			wx_set_ivar(wx, 1, ring->reg_idx, v_idx);

		wx_write_eitr(q_vector);
	}

	wx_set_ivar(wx, -1, 0, v_idx);
	if (pdev->msix_enabled)
		wr32(wx, WX_PX_ITR(v_idx), 1950);
}
EXPORT_SYMBOL(wx_configure_vectors);

/**
 * wx_free_rx_resources - Free Rx Resources
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 **/
static void wx_free_rx_resources(struct wx_ring *rx_ring)
{
	kvfree(rx_ring->rx_buffer_info);
	rx_ring->rx_buffer_info = NULL;

	/* if not set, then don't free */
	if (!rx_ring->desc)
		return;

	dma_free_coherent(rx_ring->dev, rx_ring->size,
			  rx_ring->desc, rx_ring->dma);

	rx_ring->desc = NULL;

	if (rx_ring->page_pool) {
		page_pool_destroy(rx_ring->page_pool);
		rx_ring->page_pool = NULL;
	}
}

/**
 * wx_free_all_rx_resources - Free Rx Resources for All Queues
 * @wx: pointer to hardware structure
 *
 * Free all receive software resources
 **/
static void wx_free_all_rx_resources(struct wx *wx)
{
	int i;

	for (i = 0; i < wx->num_rx_queues; i++)
		wx_free_rx_resources(wx->rx_ring[i]);
}

/**
 * wx_free_tx_resources - Free Tx Resources per Queue
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 **/
static void wx_free_tx_resources(struct wx_ring *tx_ring)
{
	kvfree(tx_ring->tx_buffer_info);
	tx_ring->tx_buffer_info = NULL;

	/* if not set, then don't free */
	if (!tx_ring->desc)
		return;

	dma_free_coherent(tx_ring->dev, tx_ring->size,
			  tx_ring->desc, tx_ring->dma);
	tx_ring->desc = NULL;
}

/**
 * wx_free_all_tx_resources - Free Tx Resources for All Queues
 * @wx: pointer to hardware structure
 *
 * Free all transmit software resources
 **/
static void wx_free_all_tx_resources(struct wx *wx)
{
	int i;

	for (i = 0; i < wx->num_tx_queues; i++)
		wx_free_tx_resources(wx->tx_ring[i]);
}

void wx_free_resources(struct wx *wx)
{
	wx_free_isb_resources(wx);
	wx_free_all_rx_resources(wx);
	wx_free_all_tx_resources(wx);
}
EXPORT_SYMBOL(wx_free_resources);

static int wx_alloc_page_pool(struct wx_ring *rx_ring)
{
	int ret = 0;

	struct page_pool_params pp_params = {
		.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
		.order = 0,
		.pool_size = rx_ring->size,
		.nid = dev_to_node(rx_ring->dev),
		.dev = rx_ring->dev,
		.dma_dir = DMA_FROM_DEVICE,
		.offset = 0,
		.max_len = PAGE_SIZE,
	};

	rx_ring->page_pool = page_pool_create(&pp_params);
	if (IS_ERR(rx_ring->page_pool)) {
		rx_ring->page_pool = NULL;
		ret = PTR_ERR(rx_ring->page_pool);
	}

	return ret;
}

/**
 * wx_setup_rx_resources - allocate Rx resources (Descriptors)
 * @rx_ring: rx descriptor ring (for a specific queue) to setup
 *
 * Returns 0 on success, negative on failure
 **/
static int wx_setup_rx_resources(struct wx_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	int orig_node = dev_to_node(dev);
	int numa_node = NUMA_NO_NODE;
	int size, ret;

	size = sizeof(struct wx_rx_buffer) * rx_ring->count;

	if (rx_ring->q_vector)
		numa_node = rx_ring->q_vector->numa_node;

	rx_ring->rx_buffer_info = kvmalloc_node(size, GFP_KERNEL, numa_node);
	if (!rx_ring->rx_buffer_info)
		rx_ring->rx_buffer_info = kvmalloc(size, GFP_KERNEL);
	if (!rx_ring->rx_buffer_info)
		goto err;

	/* Round up to nearest 4K */
	rx_ring->size = rx_ring->count * sizeof(union wx_rx_desc);
	rx_ring->size = ALIGN(rx_ring->size, 4096);

	set_dev_node(dev, numa_node);
	rx_ring->desc = dma_alloc_coherent(dev, rx_ring->size,
					   &rx_ring->dma, GFP_KERNEL);
	if (!rx_ring->desc) {
		set_dev_node(dev, orig_node);
		rx_ring->desc = dma_alloc_coherent(dev, rx_ring->size,
						   &rx_ring->dma, GFP_KERNEL);
	}

	if (!rx_ring->desc)
		goto err;

	ret = wx_alloc_page_pool(rx_ring);
	if (ret < 0) {
		dev_err(rx_ring->dev, "Page pool creation failed: %d\n", ret);
		goto err;
	}

	return 0;
err:
	kvfree(rx_ring->rx_buffer_info);
	rx_ring->rx_buffer_info = NULL;
	dev_err(dev, "Unable to allocate memory for the Rx descriptor ring\n");
	return -ENOMEM;
}

/**
 * wx_setup_all_rx_resources - allocate all queues Rx resources
 * @wx: pointer to hardware structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int wx_setup_all_rx_resources(struct wx *wx)
{
	int i, err = 0;

	for (i = 0; i < wx->num_rx_queues; i++) {
		err = wx_setup_rx_resources(wx->rx_ring[i]);
		if (!err)
			continue;

		wx_err(wx, "Allocation for Rx Queue %u failed\n", i);
		goto err_setup_rx;
	}

		return 0;
err_setup_rx:
	/* rewind the index freeing the rings as we go */
	while (i--)
		wx_free_rx_resources(wx->rx_ring[i]);
	return err;
}

/**
 * wx_setup_tx_resources - allocate Tx resources (Descriptors)
 * @tx_ring: tx descriptor ring (for a specific queue) to setup
 *
 * Return 0 on success, negative on failure
 **/
static int wx_setup_tx_resources(struct wx_ring *tx_ring)
{
	struct device *dev = tx_ring->dev;
	int orig_node = dev_to_node(dev);
	int numa_node = NUMA_NO_NODE;
	int size;

	size = sizeof(struct wx_tx_buffer) * tx_ring->count;

	if (tx_ring->q_vector)
		numa_node = tx_ring->q_vector->numa_node;

	tx_ring->tx_buffer_info = kvmalloc_node(size, GFP_KERNEL, numa_node);
	if (!tx_ring->tx_buffer_info)
		tx_ring->tx_buffer_info = kvmalloc(size, GFP_KERNEL);
	if (!tx_ring->tx_buffer_info)
		goto err;

	/* round up to nearest 4K */
	tx_ring->size = tx_ring->count * sizeof(union wx_tx_desc);
	tx_ring->size = ALIGN(tx_ring->size, 4096);

	set_dev_node(dev, numa_node);
	tx_ring->desc = dma_alloc_coherent(dev, tx_ring->size,
					   &tx_ring->dma, GFP_KERNEL);
	if (!tx_ring->desc) {
		set_dev_node(dev, orig_node);
		tx_ring->desc = dma_alloc_coherent(dev, tx_ring->size,
						   &tx_ring->dma, GFP_KERNEL);
	}

	if (!tx_ring->desc)
		goto err;

	return 0;

err:
	kvfree(tx_ring->tx_buffer_info);
	tx_ring->tx_buffer_info = NULL;
	dev_err(dev, "Unable to allocate memory for the Tx descriptor ring\n");
	return -ENOMEM;
}

/**
 * wx_setup_all_tx_resources - allocate all queues Tx resources
 * @wx: pointer to private structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int wx_setup_all_tx_resources(struct wx *wx)
{
	int i, err = 0;

	for (i = 0; i < wx->num_tx_queues; i++) {
		err = wx_setup_tx_resources(wx->tx_ring[i]);
		if (!err)
			continue;

		wx_err(wx, "Allocation for Tx Queue %u failed\n", i);
		goto err_setup_tx;
	}

	return 0;
err_setup_tx:
	/* rewind the index freeing the rings as we go */
	while (i--)
		wx_free_tx_resources(wx->tx_ring[i]);
	return err;
}

int wx_setup_resources(struct wx *wx)
{
	int err;

	/* allocate transmit descriptors */
	err = wx_setup_all_tx_resources(wx);
	if (err)
		return err;

	/* allocate receive descriptors */
	err = wx_setup_all_rx_resources(wx);
	if (err)
		goto err_free_tx;

	err = wx_setup_isb_resources(wx);
	if (err)
		goto err_free_rx;

	return 0;

err_free_rx:
	wx_free_all_rx_resources(wx);
err_free_tx:
	wx_free_all_tx_resources(wx);

	return err;
}
EXPORT_SYMBOL(wx_setup_resources);

MODULE_LICENSE("GPL");
