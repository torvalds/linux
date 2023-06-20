// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/vmalloc.h>

#include "octep_config.h"
#include "octep_main.h"
#include "octep_ctrl_net.h"

struct workqueue_struct *octep_wq;

/* Supported Devices */
static const struct pci_device_id octep_pci_id_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CN93_PF)},
	{0, },
};
MODULE_DEVICE_TABLE(pci, octep_pci_id_tbl);

MODULE_AUTHOR("Veerasenareddy Burru <vburru@marvell.com>");
MODULE_DESCRIPTION(OCTEP_DRV_STRING);
MODULE_LICENSE("GPL");

/**
 * octep_alloc_ioq_vectors() - Allocate Tx/Rx Queue interrupt info.
 *
 * @oct: Octeon device private data structure.
 *
 * Allocate resources to hold per Tx/Rx queue interrupt info.
 * This is the information passed to interrupt handler, from which napi poll
 * is scheduled and includes quick access to private data of Tx/Rx queue
 * corresponding to the interrupt being handled.
 *
 * Return: 0, on successful allocation of resources for all queue interrupts.
 *         -1, if failed to allocate any resource.
 */
static int octep_alloc_ioq_vectors(struct octep_device *oct)
{
	int i;
	struct octep_ioq_vector *ioq_vector;

	for (i = 0; i < oct->num_oqs; i++) {
		oct->ioq_vector[i] = vzalloc(sizeof(*oct->ioq_vector[i]));
		if (!oct->ioq_vector[i])
			goto free_ioq_vector;

		ioq_vector = oct->ioq_vector[i];
		ioq_vector->iq = oct->iq[i];
		ioq_vector->oq = oct->oq[i];
		ioq_vector->octep_dev = oct;
	}

	dev_info(&oct->pdev->dev, "Allocated %d IOQ vectors\n", oct->num_oqs);
	return 0;

free_ioq_vector:
	while (i) {
		i--;
		vfree(oct->ioq_vector[i]);
		oct->ioq_vector[i] = NULL;
	}
	return -1;
}

/**
 * octep_free_ioq_vectors() - Free Tx/Rx Queue interrupt vector info.
 *
 * @oct: Octeon device private data structure.
 */
static void octep_free_ioq_vectors(struct octep_device *oct)
{
	int i;

	for (i = 0; i < oct->num_oqs; i++) {
		if (oct->ioq_vector[i]) {
			vfree(oct->ioq_vector[i]);
			oct->ioq_vector[i] = NULL;
		}
	}
	netdev_info(oct->netdev, "Freed IOQ Vectors\n");
}

/**
 * octep_enable_msix_range() - enable MSI-x interrupts.
 *
 * @oct: Octeon device private data structure.
 *
 * Allocate and enable all MSI-x interrupts (queue and non-queue interrupts)
 * for the Octeon device.
 *
 * Return: 0, on successfully enabling all MSI-x interrupts.
 *         -1, if failed to enable any MSI-x interrupt.
 */
static int octep_enable_msix_range(struct octep_device *oct)
{
	int num_msix, msix_allocated;
	int i;

	/* Generic interrupts apart from input/output queues */
	num_msix = oct->num_oqs + CFG_GET_NON_IOQ_MSIX(oct->conf);
	oct->msix_entries = kcalloc(num_msix,
				    sizeof(struct msix_entry), GFP_KERNEL);
	if (!oct->msix_entries)
		goto msix_alloc_err;

	for (i = 0; i < num_msix; i++)
		oct->msix_entries[i].entry = i;

	msix_allocated = pci_enable_msix_range(oct->pdev, oct->msix_entries,
					       num_msix, num_msix);
	if (msix_allocated != num_msix) {
		dev_err(&oct->pdev->dev,
			"Failed to enable %d msix irqs; got only %d\n",
			num_msix, msix_allocated);
		goto enable_msix_err;
	}
	oct->num_irqs = msix_allocated;
	dev_info(&oct->pdev->dev, "MSI-X enabled successfully\n");

	return 0;

enable_msix_err:
	if (msix_allocated > 0)
		pci_disable_msix(oct->pdev);
	kfree(oct->msix_entries);
	oct->msix_entries = NULL;
msix_alloc_err:
	return -1;
}

/**
 * octep_disable_msix() - disable MSI-x interrupts.
 *
 * @oct: Octeon device private data structure.
 *
 * Disable MSI-x on the Octeon device.
 */
static void octep_disable_msix(struct octep_device *oct)
{
	pci_disable_msix(oct->pdev);
	kfree(oct->msix_entries);
	oct->msix_entries = NULL;
	dev_info(&oct->pdev->dev, "Disabled MSI-X\n");
}

/**
 * octep_non_ioq_intr_handler() - common handler for all generic interrupts.
 *
 * @irq: Interrupt number.
 * @data: interrupt data.
 *
 * this is common handler for all non-queue (generic) interrupts.
 */
static irqreturn_t octep_non_ioq_intr_handler(int irq, void *data)
{
	struct octep_device *oct = data;

	return oct->hw_ops.non_ioq_intr_handler(oct);
}

/**
 * octep_ioq_intr_handler() - handler for all Tx/Rx queue interrupts.
 *
 * @irq: Interrupt number.
 * @data: interrupt data contains pointers to Tx/Rx queue private data
 *         and correspong NAPI context.
 *
 * this is common handler for all non-queue (generic) interrupts.
 */
static irqreturn_t octep_ioq_intr_handler(int irq, void *data)
{
	struct octep_ioq_vector *ioq_vector = data;
	struct octep_device *oct = ioq_vector->octep_dev;

	return oct->hw_ops.ioq_intr_handler(ioq_vector);
}

/**
 * octep_request_irqs() - Register interrupt handlers.
 *
 * @oct: Octeon device private data structure.
 *
 * Register handlers for all queue and non-queue interrupts.
 *
 * Return: 0, on successful registration of all interrupt handlers.
 *         -1, on any error.
 */
static int octep_request_irqs(struct octep_device *oct)
{
	struct net_device *netdev = oct->netdev;
	struct octep_ioq_vector *ioq_vector;
	struct msix_entry *msix_entry;
	char **non_ioq_msix_names;
	int num_non_ioq_msix;
	int ret, i, j;

	num_non_ioq_msix = CFG_GET_NON_IOQ_MSIX(oct->conf);
	non_ioq_msix_names = CFG_GET_NON_IOQ_MSIX_NAMES(oct->conf);

	oct->non_ioq_irq_names = kcalloc(num_non_ioq_msix,
					 OCTEP_MSIX_NAME_SIZE, GFP_KERNEL);
	if (!oct->non_ioq_irq_names)
		goto alloc_err;

	/* First few MSI-X interrupts are non-queue interrupts */
	for (i = 0; i < num_non_ioq_msix; i++) {
		char *irq_name;

		irq_name = &oct->non_ioq_irq_names[i * OCTEP_MSIX_NAME_SIZE];
		msix_entry = &oct->msix_entries[i];

		snprintf(irq_name, OCTEP_MSIX_NAME_SIZE,
			 "%s-%s", netdev->name, non_ioq_msix_names[i]);
		ret = request_irq(msix_entry->vector,
				  octep_non_ioq_intr_handler, 0,
				  irq_name, oct);
		if (ret) {
			netdev_err(netdev,
				   "request_irq failed for %s; err=%d",
				   irq_name, ret);
			goto non_ioq_irq_err;
		}
	}

	/* Request IRQs for Tx/Rx queues */
	for (j = 0; j < oct->num_oqs; j++) {
		ioq_vector = oct->ioq_vector[j];
		msix_entry = &oct->msix_entries[j + num_non_ioq_msix];

		snprintf(ioq_vector->name, sizeof(ioq_vector->name),
			 "%s-q%d", netdev->name, j);
		ret = request_irq(msix_entry->vector,
				  octep_ioq_intr_handler, 0,
				  ioq_vector->name, ioq_vector);
		if (ret) {
			netdev_err(netdev,
				   "request_irq failed for Q-%d; err=%d",
				   j, ret);
			goto ioq_irq_err;
		}

		cpumask_set_cpu(j % num_online_cpus(),
				&ioq_vector->affinity_mask);
		irq_set_affinity_hint(msix_entry->vector,
				      &ioq_vector->affinity_mask);
	}

	return 0;
ioq_irq_err:
	while (j) {
		--j;
		ioq_vector = oct->ioq_vector[j];
		msix_entry = &oct->msix_entries[j + num_non_ioq_msix];

		irq_set_affinity_hint(msix_entry->vector, NULL);
		free_irq(msix_entry->vector, ioq_vector);
	}
non_ioq_irq_err:
	while (i) {
		--i;
		free_irq(oct->msix_entries[i].vector, oct);
	}
	kfree(oct->non_ioq_irq_names);
	oct->non_ioq_irq_names = NULL;
alloc_err:
	return -1;
}

/**
 * octep_free_irqs() - free all registered interrupts.
 *
 * @oct: Octeon device private data structure.
 *
 * Free all queue and non-queue interrupts of the Octeon device.
 */
static void octep_free_irqs(struct octep_device *oct)
{
	int i;

	/* First few MSI-X interrupts are non queue interrupts; free them */
	for (i = 0; i < CFG_GET_NON_IOQ_MSIX(oct->conf); i++)
		free_irq(oct->msix_entries[i].vector, oct);
	kfree(oct->non_ioq_irq_names);

	/* Free IRQs for Input/Output (Tx/Rx) queues */
	for (i = CFG_GET_NON_IOQ_MSIX(oct->conf); i < oct->num_irqs; i++) {
		irq_set_affinity_hint(oct->msix_entries[i].vector, NULL);
		free_irq(oct->msix_entries[i].vector,
			 oct->ioq_vector[i - CFG_GET_NON_IOQ_MSIX(oct->conf)]);
	}
	netdev_info(oct->netdev, "IRQs freed\n");
}

/**
 * octep_setup_irqs() - setup interrupts for the Octeon device.
 *
 * @oct: Octeon device private data structure.
 *
 * Allocate data structures to hold per interrupt information, allocate/enable
 * MSI-x interrupt and register interrupt handlers.
 *
 * Return: 0, on successful allocation and registration of all interrupts.
 *         -1, on any error.
 */
static int octep_setup_irqs(struct octep_device *oct)
{
	if (octep_alloc_ioq_vectors(oct))
		goto ioq_vector_err;

	if (octep_enable_msix_range(oct))
		goto enable_msix_err;

	if (octep_request_irqs(oct))
		goto request_irq_err;

	return 0;

request_irq_err:
	octep_disable_msix(oct);
enable_msix_err:
	octep_free_ioq_vectors(oct);
ioq_vector_err:
	return -1;
}

/**
 * octep_clean_irqs() - free all interrupts and its resources.
 *
 * @oct: Octeon device private data structure.
 */
static void octep_clean_irqs(struct octep_device *oct)
{
	octep_free_irqs(oct);
	octep_disable_msix(oct);
	octep_free_ioq_vectors(oct);
}

/**
 * octep_enable_ioq_irq() - Enable MSI-x interrupt of a Tx/Rx queue.
 *
 * @iq: Octeon Tx queue data structure.
 * @oq: Octeon Rx queue data structure.
 */
static void octep_enable_ioq_irq(struct octep_iq *iq, struct octep_oq *oq)
{
	u32 pkts_pend = oq->pkts_pending;

	netdev_dbg(iq->netdev, "enabling intr for Q-%u\n", iq->q_no);
	if (iq->pkts_processed) {
		writel(iq->pkts_processed, iq->inst_cnt_reg);
		iq->pkt_in_done -= iq->pkts_processed;
		iq->pkts_processed = 0;
	}
	if (oq->last_pkt_count - pkts_pend) {
		writel(oq->last_pkt_count - pkts_pend, oq->pkts_sent_reg);
		oq->last_pkt_count = pkts_pend;
	}

	/* Flush the previous wrties before writing to RESEND bit */
	wmb();
	writeq(1UL << OCTEP_OQ_INTR_RESEND_BIT, oq->pkts_sent_reg);
	writeq(1UL << OCTEP_IQ_INTR_RESEND_BIT, iq->inst_cnt_reg);
}

/**
 * octep_napi_poll() - NAPI poll function for Tx/Rx.
 *
 * @napi: pointer to napi context.
 * @budget: max number of packets to be processed in single invocation.
 */
static int octep_napi_poll(struct napi_struct *napi, int budget)
{
	struct octep_ioq_vector *ioq_vector =
		container_of(napi, struct octep_ioq_vector, napi);
	u32 tx_pending, rx_done;

	tx_pending = octep_iq_process_completions(ioq_vector->iq, budget);
	rx_done = octep_oq_process_rx(ioq_vector->oq, budget);

	/* need more polling if tx completion processing is still pending or
	 * processed at least 'budget' number of rx packets.
	 */
	if (tx_pending || rx_done >= budget)
		return budget;

	napi_complete(napi);
	octep_enable_ioq_irq(ioq_vector->iq, ioq_vector->oq);
	return rx_done;
}

/**
 * octep_napi_add() - Add NAPI poll for all Tx/Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
static void octep_napi_add(struct octep_device *oct)
{
	int i;

	for (i = 0; i < oct->num_oqs; i++) {
		netdev_dbg(oct->netdev, "Adding NAPI on Q-%d\n", i);
		netif_napi_add(oct->netdev, &oct->ioq_vector[i]->napi,
			       octep_napi_poll, 64);
		oct->oq[i]->napi = &oct->ioq_vector[i]->napi;
	}
}

/**
 * octep_napi_delete() - delete NAPI poll callback for all Tx/Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
static void octep_napi_delete(struct octep_device *oct)
{
	int i;

	for (i = 0; i < oct->num_oqs; i++) {
		netdev_dbg(oct->netdev, "Deleting NAPI on Q-%d\n", i);
		netif_napi_del(&oct->ioq_vector[i]->napi);
		oct->oq[i]->napi = NULL;
	}
}

/**
 * octep_napi_enable() - enable NAPI for all Tx/Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
static void octep_napi_enable(struct octep_device *oct)
{
	int i;

	for (i = 0; i < oct->num_oqs; i++) {
		netdev_dbg(oct->netdev, "Enabling NAPI on Q-%d\n", i);
		napi_enable(&oct->ioq_vector[i]->napi);
	}
}

/**
 * octep_napi_disable() - disable NAPI for all Tx/Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
static void octep_napi_disable(struct octep_device *oct)
{
	int i;

	for (i = 0; i < oct->num_oqs; i++) {
		netdev_dbg(oct->netdev, "Disabling NAPI on Q-%d\n", i);
		napi_disable(&oct->ioq_vector[i]->napi);
	}
}

static void octep_link_up(struct net_device *netdev)
{
	netif_carrier_on(netdev);
	netif_tx_start_all_queues(netdev);
}

/**
 * octep_open() - start the octeon network device.
 *
 * @netdev: pointer to kernel network device.
 *
 * setup Tx/Rx queues, interrupts and enable hardware operation of Tx/Rx queues
 * and interrupts..
 *
 * Return: 0, on successfully setting up device and bring it up.
 *         -1, on any error.
 */
static int octep_open(struct net_device *netdev)
{
	struct octep_device *oct = netdev_priv(netdev);
	int err, ret;

	netdev_info(netdev, "Starting netdev ...\n");
	netif_carrier_off(netdev);

	oct->hw_ops.reset_io_queues(oct);

	if (octep_setup_iqs(oct))
		goto setup_iq_err;
	if (octep_setup_oqs(oct))
		goto setup_oq_err;
	if (octep_setup_irqs(oct))
		goto setup_irq_err;

	err = netif_set_real_num_tx_queues(netdev, oct->num_oqs);
	if (err)
		goto set_queues_err;
	err = netif_set_real_num_rx_queues(netdev, oct->num_iqs);
	if (err)
		goto set_queues_err;

	octep_napi_add(oct);
	octep_napi_enable(oct);

	oct->link_info.admin_up = 1;
	octep_set_rx_state(oct, true);

	ret = octep_get_link_status(oct);
	if (!ret)
		octep_set_link_status(oct, true);

	/* Enable the input and output queues for this Octeon device */
	oct->hw_ops.enable_io_queues(oct);

	/* Enable Octeon device interrupts */
	oct->hw_ops.enable_interrupts(oct);

	octep_oq_dbell_init(oct);

	ret = octep_get_link_status(oct);
	if (ret)
		octep_link_up(netdev);

	return 0;

set_queues_err:
	octep_napi_disable(oct);
	octep_napi_delete(oct);
	octep_clean_irqs(oct);
setup_irq_err:
	octep_free_oqs(oct);
setup_oq_err:
	octep_free_iqs(oct);
setup_iq_err:
	return -1;
}

/**
 * octep_stop() - stop the octeon network device.
 *
 * @netdev: pointer to kernel network device.
 *
 * stop the device Tx/Rx operations, bring down the link and
 * free up all resources allocated for Tx/Rx queues and interrupts.
 */
static int octep_stop(struct net_device *netdev)
{
	struct octep_device *oct = netdev_priv(netdev);

	netdev_info(netdev, "Stopping the device ...\n");

	/* Stop Tx from stack */
	netif_tx_stop_all_queues(netdev);
	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	octep_set_link_status(oct, false);
	octep_set_rx_state(oct, false);

	oct->link_info.admin_up = 0;
	oct->link_info.oper_up = 0;

	oct->hw_ops.disable_interrupts(oct);
	octep_napi_disable(oct);
	octep_napi_delete(oct);

	octep_clean_irqs(oct);
	octep_clean_iqs(oct);

	oct->hw_ops.disable_io_queues(oct);
	oct->hw_ops.reset_io_queues(oct);
	octep_free_oqs(oct);
	octep_free_iqs(oct);
	netdev_info(netdev, "Device stopped !!\n");
	return 0;
}

/**
 * octep_iq_full_check() - check if a Tx queue is full.
 *
 * @iq: Octeon Tx queue data structure.
 *
 * Return: 0, if the Tx queue is not full.
 *         1, if the Tx queue is full.
 */
static inline int octep_iq_full_check(struct octep_iq *iq)
{
	if (likely((iq->max_count - atomic_read(&iq->instr_pending)) >=
		   OCTEP_WAKE_QUEUE_THRESHOLD))
		return 0;

	/* Stop the queue if unable to send */
	netif_stop_subqueue(iq->netdev, iq->q_no);

	/* check again and restart the queue, in case NAPI has just freed
	 * enough Tx ring entries.
	 */
	if (unlikely((iq->max_count - atomic_read(&iq->instr_pending)) >=
		     OCTEP_WAKE_QUEUE_THRESHOLD)) {
		netif_start_subqueue(iq->netdev, iq->q_no);
		iq->stats.restart_cnt++;
		return 0;
	}

	return 1;
}

/**
 * octep_start_xmit() - Enqueue packet to Octoen hardware Tx Queue.
 *
 * @skb: packet skbuff pointer.
 * @netdev: kernel network device.
 *
 * Return: NETDEV_TX_BUSY, if Tx Queue is full.
 *         NETDEV_TX_OK, if successfully enqueued to hardware Tx queue.
 */
static netdev_tx_t octep_start_xmit(struct sk_buff *skb,
				    struct net_device *netdev)
{
	struct octep_device *oct = netdev_priv(netdev);
	struct octep_tx_sglist_desc *sglist;
	struct octep_tx_buffer *tx_buffer;
	struct octep_tx_desc_hw *hw_desc;
	struct skb_shared_info *shinfo;
	struct octep_instr_hdr *ih;
	struct octep_iq *iq;
	skb_frag_t *frag;
	u16 nr_frags, si;
	u16 q_no, wi;

	q_no = skb_get_queue_mapping(skb);
	if (q_no >= oct->num_iqs) {
		netdev_err(netdev, "Invalid Tx skb->queue_mapping=%d\n", q_no);
		q_no = q_no % oct->num_iqs;
	}

	iq = oct->iq[q_no];
	if (octep_iq_full_check(iq)) {
		iq->stats.tx_busy++;
		return NETDEV_TX_BUSY;
	}

	shinfo = skb_shinfo(skb);
	nr_frags = shinfo->nr_frags;

	wi = iq->host_write_index;
	hw_desc = &iq->desc_ring[wi];
	hw_desc->ih64 = 0;

	tx_buffer = iq->buff_info + wi;
	tx_buffer->skb = skb;

	ih = &hw_desc->ih;
	ih->tlen = skb->len;
	ih->pkind = oct->pkind;

	if (!nr_frags) {
		tx_buffer->gather = 0;
		tx_buffer->dma = dma_map_single(iq->dev, skb->data,
						skb->len, DMA_TO_DEVICE);
		if (dma_mapping_error(iq->dev, tx_buffer->dma))
			goto dma_map_err;
		hw_desc->dptr = tx_buffer->dma;
	} else {
		/* Scatter/Gather */
		dma_addr_t dma;
		u16 len;

		sglist = tx_buffer->sglist;

		ih->gsz = nr_frags + 1;
		ih->gather = 1;
		tx_buffer->gather = 1;

		len = skb_headlen(skb);
		dma = dma_map_single(iq->dev, skb->data, len, DMA_TO_DEVICE);
		if (dma_mapping_error(iq->dev, dma))
			goto dma_map_err;

		dma_sync_single_for_cpu(iq->dev, tx_buffer->sglist_dma,
					OCTEP_SGLIST_SIZE_PER_PKT,
					DMA_TO_DEVICE);
		memset(sglist, 0, OCTEP_SGLIST_SIZE_PER_PKT);
		sglist[0].len[3] = len;
		sglist[0].dma_ptr[0] = dma;

		si = 1; /* entry 0 is main skb, mapped above */
		frag = &shinfo->frags[0];
		while (nr_frags--) {
			len = skb_frag_size(frag);
			dma = skb_frag_dma_map(iq->dev, frag, 0,
					       len, DMA_TO_DEVICE);
			if (dma_mapping_error(iq->dev, dma))
				goto dma_map_sg_err;

			sglist[si >> 2].len[3 - (si & 3)] = len;
			sglist[si >> 2].dma_ptr[si & 3] = dma;

			frag++;
			si++;
		}
		dma_sync_single_for_device(iq->dev, tx_buffer->sglist_dma,
					   OCTEP_SGLIST_SIZE_PER_PKT,
					   DMA_TO_DEVICE);

		hw_desc->dptr = tx_buffer->sglist_dma;
	}

	/* Flush the hw descriptor before writing to doorbell */
	wmb();

	/* Ring Doorbell to notify the NIC there is a new packet */
	writel(1, iq->doorbell_reg);
	atomic_inc(&iq->instr_pending);
	wi++;
	if (wi == iq->max_count)
		wi = 0;
	iq->host_write_index = wi;

	netdev_tx_sent_queue(iq->netdev_q, skb->len);
	iq->stats.instr_posted++;
	skb_tx_timestamp(skb);
	return NETDEV_TX_OK;

dma_map_sg_err:
	if (si > 0) {
		dma_unmap_single(iq->dev, sglist[0].dma_ptr[0],
				 sglist[0].len[0], DMA_TO_DEVICE);
		sglist[0].len[0] = 0;
	}
	while (si > 1) {
		dma_unmap_page(iq->dev, sglist[si >> 2].dma_ptr[si & 3],
			       sglist[si >> 2].len[si & 3], DMA_TO_DEVICE);
		sglist[si >> 2].len[si & 3] = 0;
		si--;
	}
	tx_buffer->gather = 0;
dma_map_err:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/**
 * octep_get_stats64() - Get Octeon network device statistics.
 *
 * @netdev: kernel network device.
 * @stats: pointer to stats structure to be filled in.
 */
static void octep_get_stats64(struct net_device *netdev,
			      struct rtnl_link_stats64 *stats)
{
	u64 tx_packets, tx_bytes, rx_packets, rx_bytes;
	struct octep_device *oct = netdev_priv(netdev);
	int q;

	octep_get_if_stats(oct);
	tx_packets = 0;
	tx_bytes = 0;
	rx_packets = 0;
	rx_bytes = 0;
	for (q = 0; q < oct->num_oqs; q++) {
		struct octep_iq *iq = oct->iq[q];
		struct octep_oq *oq = oct->oq[q];

		tx_packets += iq->stats.instr_completed;
		tx_bytes += iq->stats.bytes_sent;
		rx_packets += oq->stats.packets;
		rx_bytes += oq->stats.bytes;
	}
	stats->tx_packets = tx_packets;
	stats->tx_bytes = tx_bytes;
	stats->rx_packets = rx_packets;
	stats->rx_bytes = rx_bytes;
	stats->multicast = oct->iface_rx_stats.mcast_pkts;
	stats->rx_errors = oct->iface_rx_stats.err_pkts;
	stats->collisions = oct->iface_tx_stats.xscol;
	stats->tx_fifo_errors = oct->iface_tx_stats.undflw;
}

/**
 * octep_tx_timeout_task - work queue task to Handle Tx queue timeout.
 *
 * @work: pointer to Tx queue timeout work_struct
 *
 * Stop and start the device so that it frees up all queue resources
 * and restarts the queues, that potentially clears a Tx queue timeout
 * condition.
 **/
static void octep_tx_timeout_task(struct work_struct *work)
{
	struct octep_device *oct = container_of(work, struct octep_device,
						tx_timeout_task);
	struct net_device *netdev = oct->netdev;

	rtnl_lock();
	if (netif_running(netdev)) {
		octep_stop(netdev);
		octep_open(netdev);
	}
	rtnl_unlock();
}

/**
 * octep_tx_timeout() - Handle Tx Queue timeout.
 *
 * @netdev: pointer to kernel network device.
 * @txqueue: Timed out Tx queue number.
 *
 * Schedule a work to handle Tx queue timeout.
 */
static void octep_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct octep_device *oct = netdev_priv(netdev);

	queue_work(octep_wq, &oct->tx_timeout_task);
}

static int octep_set_mac(struct net_device *netdev, void *p)
{
	struct octep_device *oct = netdev_priv(netdev);
	struct sockaddr *addr = (struct sockaddr *)p;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	err = octep_set_mac_addr(oct, addr->sa_data);
	if (err)
		return err;

	memcpy(oct->mac_addr, addr->sa_data, ETH_ALEN);
	eth_hw_addr_set(netdev, addr->sa_data);

	return 0;
}

static int octep_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct octep_device *oct = netdev_priv(netdev);
	struct octep_iface_link_info *link_info;
	int err = 0;

	link_info = &oct->link_info;
	if (link_info->mtu == new_mtu)
		return 0;

	err = octep_set_mtu(oct, new_mtu);
	if (!err) {
		oct->link_info.mtu = new_mtu;
		netdev->mtu = new_mtu;
	}

	return err;
}

static const struct net_device_ops octep_netdev_ops = {
	.ndo_open                = octep_open,
	.ndo_stop                = octep_stop,
	.ndo_start_xmit          = octep_start_xmit,
	.ndo_get_stats64         = octep_get_stats64,
	.ndo_tx_timeout          = octep_tx_timeout,
	.ndo_set_mac_address     = octep_set_mac,
	.ndo_change_mtu          = octep_change_mtu,
};

/**
 * octep_ctrl_mbox_task - work queue task to handle ctrl mbox messages.
 *
 * @work: pointer to ctrl mbox work_struct
 *
 * Poll ctrl mbox message queue and handle control messages from firmware.
 **/
static void octep_ctrl_mbox_task(struct work_struct *work)
{
	struct octep_device *oct = container_of(work, struct octep_device,
						ctrl_mbox_task);
	struct net_device *netdev = oct->netdev;
	struct octep_ctrl_net_f2h_req req = {};
	struct octep_ctrl_mbox_msg msg;
	int ret = 0;

	msg.msg = &req;
	while (true) {
		ret = octep_ctrl_mbox_recv(&oct->ctrl_mbox, &msg);
		if (ret)
			break;

		switch (req.hdr.cmd) {
		case OCTEP_CTRL_NET_F2H_CMD_LINK_STATUS:
			if (netif_running(netdev)) {
				if (req.link.state) {
					dev_info(&oct->pdev->dev, "netif_carrier_on\n");
					netif_carrier_on(netdev);
				} else {
					dev_info(&oct->pdev->dev, "netif_carrier_off\n");
					netif_carrier_off(netdev);
				}
			}
			break;
		default:
			pr_info("Unknown mbox req : %u\n", req.hdr.cmd);
			break;
		}
	}
}

/**
 * octep_device_setup() - Setup Octeon Device.
 *
 * @oct: Octeon device private data structure.
 *
 * Setup Octeon device hardware operations, configuration, etc ...
 */
int octep_device_setup(struct octep_device *oct)
{
	struct octep_ctrl_mbox *ctrl_mbox;
	struct pci_dev *pdev = oct->pdev;
	int i, ret;

	/* allocate memory for oct->conf */
	oct->conf = kzalloc(sizeof(*oct->conf), GFP_KERNEL);
	if (!oct->conf)
		return -ENOMEM;

	/* Map BAR regions */
	for (i = 0; i < OCTEP_MMIO_REGIONS; i++) {
		oct->mmio[i].hw_addr =
			ioremap(pci_resource_start(oct->pdev, i * 2),
				pci_resource_len(oct->pdev, i * 2));
		oct->mmio[i].mapped = 1;
	}

	oct->chip_id = pdev->device;
	oct->rev_id = pdev->revision;
	dev_info(&pdev->dev, "chip_id = 0x%x\n", pdev->device);

	switch (oct->chip_id) {
	case OCTEP_PCI_DEVICE_ID_CN93_PF:
		dev_info(&pdev->dev,
			 "Setting up OCTEON CN93XX PF PASS%d.%d\n",
			 OCTEP_MAJOR_REV(oct), OCTEP_MINOR_REV(oct));
		octep_device_setup_cn93_pf(oct);
		break;
	default:
		dev_err(&pdev->dev,
			"%s: unsupported device\n", __func__);
		goto unsupported_dev;
	}

	oct->pkind = CFG_GET_IQ_PKIND(oct->conf);

	/* Initialize control mbox */
	ctrl_mbox = &oct->ctrl_mbox;
	ctrl_mbox->barmem = CFG_GET_CTRL_MBOX_MEM_ADDR(oct->conf);
	ret = octep_ctrl_mbox_init(ctrl_mbox);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize control mbox\n");
		return -1;
	}
	oct->ctrl_mbox_ifstats_offset = OCTEP_CTRL_MBOX_SZ(ctrl_mbox->h2fq.elem_sz,
							   ctrl_mbox->h2fq.elem_cnt,
							   ctrl_mbox->f2hq.elem_sz,
							   ctrl_mbox->f2hq.elem_cnt);

	return 0;

unsupported_dev:
	return -1;
}

/**
 * octep_device_cleanup() - Cleanup Octeon Device.
 *
 * @oct: Octeon device private data structure.
 *
 * Cleanup Octeon device allocated resources.
 */
static void octep_device_cleanup(struct octep_device *oct)
{
	int i;

	dev_info(&oct->pdev->dev, "Cleaning up Octeon Device ...\n");

	for (i = 0; i < OCTEP_MAX_VF; i++) {
		vfree(oct->mbox[i]);
		oct->mbox[i] = NULL;
	}

	octep_ctrl_mbox_uninit(&oct->ctrl_mbox);

	oct->hw_ops.soft_reset(oct);
	for (i = 0; i < OCTEP_MMIO_REGIONS; i++) {
		if (oct->mmio[i].mapped)
			iounmap(oct->mmio[i].hw_addr);
	}

	kfree(oct->conf);
	oct->conf = NULL;
}

/**
 * octep_probe() - Octeon PCI device probe handler.
 *
 * @pdev: PCI device structure.
 * @ent: entry in Octeon PCI device ID table.
 *
 * Initializes and enables the Octeon PCI device for network operations.
 * Initializes Octeon private data structure and registers a network device.
 */
static int octep_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct octep_device *octep_dev = NULL;
	struct net_device *netdev;
	int err;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return  err;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(&pdev->dev, "Failed to set DMA mask !!\n");
		goto err_dma_mask;
	}

	err = pci_request_mem_regions(pdev, OCTEP_DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "Failed to map PCI memory regions\n");
		goto err_pci_regions;
	}

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);

	netdev = alloc_etherdev_mq(sizeof(struct octep_device),
				   OCTEP_MAX_QUEUES);
	if (!netdev) {
		dev_err(&pdev->dev, "Failed to allocate netdev\n");
		err = -ENOMEM;
		goto err_alloc_netdev;
	}
	SET_NETDEV_DEV(netdev, &pdev->dev);

	octep_dev = netdev_priv(netdev);
	octep_dev->netdev = netdev;
	octep_dev->pdev = pdev;
	octep_dev->dev = &pdev->dev;
	pci_set_drvdata(pdev, octep_dev);

	err = octep_device_setup(octep_dev);
	if (err) {
		dev_err(&pdev->dev, "Device setup failed\n");
		goto err_octep_config;
	}
	INIT_WORK(&octep_dev->tx_timeout_task, octep_tx_timeout_task);
	INIT_WORK(&octep_dev->ctrl_mbox_task, octep_ctrl_mbox_task);

	netdev->netdev_ops = &octep_netdev_ops;
	octep_set_ethtool_ops(netdev);
	netif_carrier_off(netdev);

	netdev->hw_features = NETIF_F_SG;
	netdev->features |= netdev->hw_features;
	netdev->min_mtu = OCTEP_MIN_MTU;
	netdev->max_mtu = OCTEP_MAX_MTU;
	netdev->mtu = OCTEP_DEFAULT_MTU;

	octep_get_mac_addr(octep_dev, octep_dev->mac_addr);
	eth_hw_addr_set(netdev, octep_dev->mac_addr);

	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register netdev\n");
		goto register_dev_err;
	}
	dev_info(&pdev->dev, "Device probe successful\n");
	return 0;

register_dev_err:
	octep_device_cleanup(octep_dev);
err_octep_config:
	free_netdev(netdev);
err_alloc_netdev:
	pci_disable_pcie_error_reporting(pdev);
	pci_release_mem_regions(pdev);
err_pci_regions:
err_dma_mask:
	pci_disable_device(pdev);
	return err;
}

/**
 * octep_remove() - Remove Octeon PCI device from driver control.
 *
 * @pdev: PCI device structure of the Octeon device.
 *
 * Cleanup all resources allocated for the Octeon device.
 * Unregister from network device and disable the PCI device.
 */
static void octep_remove(struct pci_dev *pdev)
{
	struct octep_device *oct = pci_get_drvdata(pdev);
	struct net_device *netdev;

	if (!oct)
		return;

	cancel_work_sync(&oct->tx_timeout_task);
	cancel_work_sync(&oct->ctrl_mbox_task);
	netdev = oct->netdev;
	if (netdev->reg_state == NETREG_REGISTERED)
		unregister_netdev(netdev);

	octep_device_cleanup(oct);
	pci_release_mem_regions(pdev);
	free_netdev(netdev);
	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver octep_driver = {
	.name = OCTEP_DRV_NAME,
	.id_table = octep_pci_id_tbl,
	.probe = octep_probe,
	.remove = octep_remove,
};

/**
 * octep_init_module() - Module initialiation.
 *
 * create common resource for the driver and register PCI driver.
 */
static int __init octep_init_module(void)
{
	int ret;

	pr_info("%s: Loading %s ...\n", OCTEP_DRV_NAME, OCTEP_DRV_STRING);

	/* work queue for all deferred tasks */
	octep_wq = create_singlethread_workqueue(OCTEP_DRV_NAME);
	if (!octep_wq) {
		pr_err("%s: Failed to create common workqueue\n",
		       OCTEP_DRV_NAME);
		return -ENOMEM;
	}

	ret = pci_register_driver(&octep_driver);
	if (ret < 0) {
		pr_err("%s: Failed to register PCI driver; err=%d\n",
		       OCTEP_DRV_NAME, ret);
		destroy_workqueue(octep_wq);
		return ret;
	}

	pr_info("%s: Loaded successfully !\n", OCTEP_DRV_NAME);

	return ret;
}

/**
 * octep_exit_module() - Module exit routine.
 *
 * unregister the driver with PCI subsystem and cleanup common resources.
 */
static void __exit octep_exit_module(void)
{
	pr_info("%s: Unloading ...\n", OCTEP_DRV_NAME);

	pci_unregister_driver(&octep_driver);
	destroy_workqueue(octep_wq);

	pr_info("%s: Unloading complete\n", OCTEP_DRV_NAME);
}

module_init(octep_init_module);
module_exit(octep_exit_module);
