// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) VF Ethernet Driver
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
#include <net/netdev_queues.h>

#include "octep_vf_config.h"
#include "octep_vf_main.h"

struct workqueue_struct *octep_vf_wq;

/* Supported Devices */
static const struct pci_device_id octep_vf_pci_id_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CN93_VF)},
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CNF95N_VF)},
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CN98_VF)},
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CN10KA_VF)},
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CNF10KA_VF)},
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CNF10KB_VF)},
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CN10KB_VF)},
	{0, },
};
MODULE_DEVICE_TABLE(pci, octep_vf_pci_id_tbl);

MODULE_AUTHOR("Veerasenareddy Burru <vburru@marvell.com>");
MODULE_DESCRIPTION(OCTEP_VF_DRV_STRING);
MODULE_LICENSE("GPL");

/**
 * octep_vf_alloc_ioq_vectors() - Allocate Tx/Rx Queue interrupt info.
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
static int octep_vf_alloc_ioq_vectors(struct octep_vf_device *oct)
{
	struct octep_vf_ioq_vector *ioq_vector;
	int i;

	for (i = 0; i < oct->num_oqs; i++) {
		oct->ioq_vector[i] = vzalloc(sizeof(*oct->ioq_vector[i]));
		if (!oct->ioq_vector[i])
			goto free_ioq_vector;

		ioq_vector = oct->ioq_vector[i];
		ioq_vector->iq = oct->iq[i];
		ioq_vector->oq = oct->oq[i];
		ioq_vector->octep_vf_dev = oct;
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
 * octep_vf_free_ioq_vectors() - Free Tx/Rx Queue interrupt vector info.
 *
 * @oct: Octeon device private data structure.
 */
static void octep_vf_free_ioq_vectors(struct octep_vf_device *oct)
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
 * octep_vf_enable_msix_range() - enable MSI-x interrupts.
 *
 * @oct: Octeon device private data structure.
 *
 * Allocate and enable all MSI-x interrupts (queue and non-queue interrupts)
 * for the Octeon device.
 *
 * Return: 0, on successfully enabling all MSI-x interrupts.
 *         -1, if failed to enable any MSI-x interrupt.
 */
static int octep_vf_enable_msix_range(struct octep_vf_device *oct)
{
	int num_msix, msix_allocated;
	int i;

	/* Generic interrupts apart from input/output queues */
	//num_msix = oct->num_oqs + CFG_GET_NON_IOQ_MSIX(oct->conf);
	num_msix = oct->num_oqs;
	oct->msix_entries = kcalloc(num_msix, sizeof(struct msix_entry), GFP_KERNEL);
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
 * octep_vf_disable_msix() - disable MSI-x interrupts.
 *
 * @oct: Octeon device private data structure.
 *
 * Disable MSI-x on the Octeon device.
 */
static void octep_vf_disable_msix(struct octep_vf_device *oct)
{
	pci_disable_msix(oct->pdev);
	kfree(oct->msix_entries);
	oct->msix_entries = NULL;
	dev_info(&oct->pdev->dev, "Disabled MSI-X\n");
}

/**
 * octep_vf_ioq_intr_handler() - handler for all Tx/Rx queue interrupts.
 *
 * @irq: Interrupt number.
 * @data: interrupt data contains pointers to Tx/Rx queue private data
 *         and correspong NAPI context.
 *
 * this is common handler for all non-queue (generic) interrupts.
 */
static irqreturn_t octep_vf_ioq_intr_handler(int irq, void *data)
{
	struct octep_vf_ioq_vector *ioq_vector = data;
	struct octep_vf_device *oct = ioq_vector->octep_vf_dev;

	return oct->hw_ops.ioq_intr_handler(ioq_vector);
}

/**
 * octep_vf_request_irqs() - Register interrupt handlers.
 *
 * @oct: Octeon device private data structure.
 *
 * Register handlers for all queue and non-queue interrupts.
 *
 * Return: 0, on successful registration of all interrupt handlers.
 *         -1, on any error.
 */
static int octep_vf_request_irqs(struct octep_vf_device *oct)
{
	struct net_device *netdev = oct->netdev;
	struct octep_vf_ioq_vector *ioq_vector;
	struct msix_entry *msix_entry;
	int ret, i;

	/* Request IRQs for Tx/Rx queues */
	for (i = 0; i < oct->num_oqs; i++) {
		ioq_vector = oct->ioq_vector[i];
		msix_entry = &oct->msix_entries[i];

		snprintf(ioq_vector->name, sizeof(ioq_vector->name),
			 "%s-q%d", netdev->name, i);
		ret = request_irq(msix_entry->vector,
				  octep_vf_ioq_intr_handler, 0,
				  ioq_vector->name, ioq_vector);
		if (ret) {
			netdev_err(netdev,
				   "request_irq failed for Q-%d; err=%d",
				   i, ret);
			goto ioq_irq_err;
		}

		cpumask_set_cpu(i % num_online_cpus(),
				&ioq_vector->affinity_mask);
		irq_set_affinity_hint(msix_entry->vector,
				      &ioq_vector->affinity_mask);
	}

	return 0;
ioq_irq_err:
	while (i) {
		--i;
		free_irq(oct->msix_entries[i].vector, oct);
	}
	return -1;
}

/**
 * octep_vf_free_irqs() - free all registered interrupts.
 *
 * @oct: Octeon device private data structure.
 *
 * Free all queue and non-queue interrupts of the Octeon device.
 */
static void octep_vf_free_irqs(struct octep_vf_device *oct)
{
	int i;

	for (i = 0; i < oct->num_irqs; i++) {
		irq_set_affinity_hint(oct->msix_entries[i].vector, NULL);
		free_irq(oct->msix_entries[i].vector, oct->ioq_vector[i]);
	}
	netdev_info(oct->netdev, "IRQs freed\n");
}

/**
 * octep_vf_setup_irqs() - setup interrupts for the Octeon device.
 *
 * @oct: Octeon device private data structure.
 *
 * Allocate data structures to hold per interrupt information, allocate/enable
 * MSI-x interrupt and register interrupt handlers.
 *
 * Return: 0, on successful allocation and registration of all interrupts.
 *         -1, on any error.
 */
static int octep_vf_setup_irqs(struct octep_vf_device *oct)
{
	if (octep_vf_alloc_ioq_vectors(oct))
		goto ioq_vector_err;

	if (octep_vf_enable_msix_range(oct))
		goto enable_msix_err;

	if (octep_vf_request_irqs(oct))
		goto request_irq_err;

	return 0;

request_irq_err:
	octep_vf_disable_msix(oct);
enable_msix_err:
	octep_vf_free_ioq_vectors(oct);
ioq_vector_err:
	return -1;
}

/**
 * octep_vf_clean_irqs() - free all interrupts and its resources.
 *
 * @oct: Octeon device private data structure.
 */
static void octep_vf_clean_irqs(struct octep_vf_device *oct)
{
	octep_vf_free_irqs(oct);
	octep_vf_disable_msix(oct);
	octep_vf_free_ioq_vectors(oct);
}

/**
 * octep_vf_enable_ioq_irq() - Enable MSI-x interrupt of a Tx/Rx queue.
 *
 * @iq: Octeon Tx queue data structure.
 * @oq: Octeon Rx queue data structure.
 */
static void octep_vf_enable_ioq_irq(struct octep_vf_iq *iq, struct octep_vf_oq *oq)
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
	smp_wmb();
	writeq(1UL << OCTEP_VF_OQ_INTR_RESEND_BIT, oq->pkts_sent_reg);
	writeq(1UL << OCTEP_VF_IQ_INTR_RESEND_BIT, iq->inst_cnt_reg);
}

/**
 * octep_vf_napi_poll() - NAPI poll function for Tx/Rx.
 *
 * @napi: pointer to napi context.
 * @budget: max number of packets to be processed in single invocation.
 */
static int octep_vf_napi_poll(struct napi_struct *napi, int budget)
{
	struct octep_vf_ioq_vector *ioq_vector =
		container_of(napi, struct octep_vf_ioq_vector, napi);
	u32 tx_pending, rx_done;

	tx_pending = octep_vf_iq_process_completions(ioq_vector->iq, 64);
	rx_done = octep_vf_oq_process_rx(ioq_vector->oq, budget);

	/* need more polling if tx completion processing is still pending or
	 * processed at least 'budget' number of rx packets.
	 */
	if (tx_pending || rx_done >= budget)
		return budget;

	if (likely(napi_complete_done(napi, rx_done)))
		octep_vf_enable_ioq_irq(ioq_vector->iq, ioq_vector->oq);

	return rx_done;
}

/**
 * octep_vf_napi_add() - Add NAPI poll for all Tx/Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
static void octep_vf_napi_add(struct octep_vf_device *oct)
{
	int i;

	for (i = 0; i < oct->num_oqs; i++) {
		netdev_dbg(oct->netdev, "Adding NAPI on Q-%d\n", i);
		netif_napi_add(oct->netdev, &oct->ioq_vector[i]->napi, octep_vf_napi_poll);
		oct->oq[i]->napi = &oct->ioq_vector[i]->napi;
	}
}

/**
 * octep_vf_napi_delete() - delete NAPI poll callback for all Tx/Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
static void octep_vf_napi_delete(struct octep_vf_device *oct)
{
	int i;

	for (i = 0; i < oct->num_oqs; i++) {
		netdev_dbg(oct->netdev, "Deleting NAPI on Q-%d\n", i);
		netif_napi_del(&oct->ioq_vector[i]->napi);
		oct->oq[i]->napi = NULL;
	}
}

/**
 * octep_vf_napi_enable() - enable NAPI for all Tx/Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
static void octep_vf_napi_enable(struct octep_vf_device *oct)
{
	int i;

	for (i = 0; i < oct->num_oqs; i++) {
		netdev_dbg(oct->netdev, "Enabling NAPI on Q-%d\n", i);
		napi_enable(&oct->ioq_vector[i]->napi);
	}
}

/**
 * octep_vf_napi_disable() - disable NAPI for all Tx/Rx queues.
 *
 * @oct: Octeon device private data structure.
 */
static void octep_vf_napi_disable(struct octep_vf_device *oct)
{
	int i;

	for (i = 0; i < oct->num_oqs; i++) {
		netdev_dbg(oct->netdev, "Disabling NAPI on Q-%d\n", i);
		napi_disable(&oct->ioq_vector[i]->napi);
	}
}

static void octep_vf_link_up(struct net_device *netdev)
{
	netif_carrier_on(netdev);
	netif_tx_start_all_queues(netdev);
}

static void octep_vf_set_rx_state(struct octep_vf_device *oct, bool up)
{
	int err;

	err = octep_vf_mbox_set_rx_state(oct, up);
	if (err)
		netdev_err(oct->netdev, "Set Rx state to %d failed with err:%d\n", up, err);
}

static int octep_vf_get_link_status(struct octep_vf_device *oct)
{
	int err;

	err = octep_vf_mbox_get_link_status(oct, &oct->link_info.oper_up);
	if (err)
		netdev_err(oct->netdev, "Get link status failed with err:%d\n", err);
	return oct->link_info.oper_up;
}

static void octep_vf_set_link_status(struct octep_vf_device *oct, bool up)
{
	int err;

	err = octep_vf_mbox_set_link_status(oct, up);
	if (err) {
		netdev_err(oct->netdev, "Set link status to %d failed with err:%d\n", up, err);
		return;
	}
	oct->link_info.oper_up = up;
}

/**
 * octep_vf_open() - start the octeon network device.
 *
 * @netdev: pointer to kernel network device.
 *
 * setup Tx/Rx queues, interrupts and enable hardware operation of Tx/Rx queues
 * and interrupts..
 *
 * Return: 0, on successfully setting up device and bring it up.
 *         -1, on any error.
 */
static int octep_vf_open(struct net_device *netdev)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	int err, ret;

	netdev_info(netdev, "Starting netdev ...\n");
	netif_carrier_off(netdev);

	oct->hw_ops.reset_io_queues(oct);

	if (octep_vf_setup_iqs(oct))
		goto setup_iq_err;
	if (octep_vf_setup_oqs(oct))
		goto setup_oq_err;
	if (octep_vf_setup_irqs(oct))
		goto setup_irq_err;

	err = netif_set_real_num_tx_queues(netdev, oct->num_oqs);
	if (err)
		goto set_queues_err;
	err = netif_set_real_num_rx_queues(netdev, oct->num_iqs);
	if (err)
		goto set_queues_err;

	octep_vf_napi_add(oct);
	octep_vf_napi_enable(oct);

	oct->link_info.admin_up = 1;
	octep_vf_set_rx_state(oct, true);

	ret = octep_vf_get_link_status(oct);
	if (!ret)
		octep_vf_set_link_status(oct, true);

	/* Enable the input and output queues for this Octeon device */
	oct->hw_ops.enable_io_queues(oct);

	/* Enable Octeon device interrupts */
	oct->hw_ops.enable_interrupts(oct);

	octep_vf_oq_dbell_init(oct);

	ret = octep_vf_get_link_status(oct);
	if (ret)
		octep_vf_link_up(netdev);

	return 0;

set_queues_err:
	octep_vf_napi_disable(oct);
	octep_vf_napi_delete(oct);
	octep_vf_clean_irqs(oct);
setup_irq_err:
	octep_vf_free_oqs(oct);
setup_oq_err:
	octep_vf_free_iqs(oct);
setup_iq_err:
	return -1;
}

/**
 * octep_vf_stop() - stop the octeon network device.
 *
 * @netdev: pointer to kernel network device.
 *
 * stop the device Tx/Rx operations, bring down the link and
 * free up all resources allocated for Tx/Rx queues and interrupts.
 */
static int octep_vf_stop(struct net_device *netdev)
{
	struct octep_vf_device *oct = netdev_priv(netdev);

	netdev_info(netdev, "Stopping the device ...\n");

	/* Stop Tx from stack */
	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	octep_vf_set_link_status(oct, false);
	octep_vf_set_rx_state(oct, false);

	oct->link_info.admin_up = 0;
	oct->link_info.oper_up = 0;

	oct->hw_ops.disable_interrupts(oct);
	octep_vf_napi_disable(oct);
	octep_vf_napi_delete(oct);

	octep_vf_clean_irqs(oct);
	octep_vf_clean_iqs(oct);

	oct->hw_ops.disable_io_queues(oct);
	oct->hw_ops.reset_io_queues(oct);
	octep_vf_free_oqs(oct);
	octep_vf_free_iqs(oct);
	netdev_info(netdev, "Device stopped !!\n");
	return 0;
}

/**
 * octep_vf_iq_full_check() - check if a Tx queue is full.
 *
 * @iq: Octeon Tx queue data structure.
 *
 * Return: 0, if the Tx queue is not full.
 *         1, if the Tx queue is full.
 */
static int octep_vf_iq_full_check(struct octep_vf_iq *iq)
{
	int ret;

	ret = netif_subqueue_maybe_stop(iq->netdev, iq->q_no, IQ_INSTR_SPACE(iq),
					OCTEP_VF_WAKE_QUEUE_THRESHOLD,
					OCTEP_VF_WAKE_QUEUE_THRESHOLD);
	switch (ret) {
	case 0: /* Stopped the queue, since IQ is full */
		return 1;
	case -1: /*
		  * Pending updates in write index from
		  * iq_process_completion in other cpus
		  * caused queues to get re-enabled after
		  * being stopped
		  */
		iq->stats->restart_cnt++;
		fallthrough;
	case 1: /* Queue left enabled, since IQ is not yet full*/
		return 0;
	}

	return 1;
}

/**
 * octep_vf_start_xmit() - Enqueue packet to Octoen hardware Tx Queue.
 *
 * @skb: packet skbuff pointer.
 * @netdev: kernel network device.
 *
 * Return: NETDEV_TX_BUSY, if Tx Queue is full.
 *         NETDEV_TX_OK, if successfully enqueued to hardware Tx queue.
 */
static netdev_tx_t octep_vf_start_xmit(struct sk_buff *skb,
				       struct net_device *netdev)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	netdev_features_t feat  = netdev->features;
	struct octep_vf_tx_sglist_desc *sglist;
	struct octep_vf_tx_buffer *tx_buffer;
	struct octep_vf_tx_desc_hw *hw_desc;
	struct skb_shared_info *shinfo;
	struct octep_vf_instr_hdr *ih;
	struct octep_vf_iq *iq;
	skb_frag_t *frag;
	u16 nr_frags, si;
	int xmit_more;
	u16 q_no, wi;

	if (skb_put_padto(skb, ETH_ZLEN))
		return NETDEV_TX_OK;

	q_no = skb_get_queue_mapping(skb);
	if (q_no >= oct->num_iqs) {
		netdev_err(netdev, "Invalid Tx skb->queue_mapping=%d\n", q_no);
		q_no = q_no % oct->num_iqs;
	}

	iq = oct->iq[q_no];

	shinfo = skb_shinfo(skb);
	nr_frags = shinfo->nr_frags;

	wi = iq->host_write_index;
	hw_desc = &iq->desc_ring[wi];
	hw_desc->ih64 = 0;

	tx_buffer = iq->buff_info + wi;
	tx_buffer->skb = skb;

	ih = &hw_desc->ih;
	ih->tlen = skb->len;
	ih->pkind = oct->fw_info.pkind;
	ih->fsz = oct->fw_info.fsz;
	ih->tlen = skb->len + ih->fsz;

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

		memset(sglist, 0, OCTEP_VF_SGLIST_SIZE_PER_PKT);
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
		hw_desc->dptr = tx_buffer->sglist_dma;
	}
	if (oct->fw_info.tx_ol_flags) {
		if ((feat & (NETIF_F_TSO)) && (skb_is_gso(skb))) {
			hw_desc->txm.ol_flags = OCTEP_VF_TX_OFFLOAD_CKSUM;
			hw_desc->txm.ol_flags |= OCTEP_VF_TX_OFFLOAD_TSO;
			hw_desc->txm.gso_size =  skb_shinfo(skb)->gso_size;
			hw_desc->txm.gso_segs =  skb_shinfo(skb)->gso_segs;
		} else if (feat & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM)) {
			hw_desc->txm.ol_flags = OCTEP_VF_TX_OFFLOAD_CKSUM;
		}
		/* due to ESR txm will be swapped by hw */
		hw_desc->txm64[0] = (__force u64)cpu_to_be64(hw_desc->txm64[0]);
	}

	xmit_more = netdev_xmit_more();

	netdev_tx_sent_queue(iq->netdev_q, skb->len);

	skb_tx_timestamp(skb);
	iq->fill_cnt++;
	wi++;
	iq->host_write_index = wi & iq->ring_size_mask;

	/* octep_iq_full_check stops the queue and returns
	 * true if so, in case the queue has become full
	 * by inserting current packet. If so, we can
	 * go ahead and ring doorbell.
	 */
	if (!octep_vf_iq_full_check(iq) && xmit_more &&
	    iq->fill_cnt < iq->fill_threshold)
		return NETDEV_TX_OK;

	goto ring_dbell;

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
ring_dbell:
	/* Flush the hw descriptors before writing to doorbell */
	smp_wmb();
	writel(iq->fill_cnt, iq->doorbell_reg);
	iq->stats->instr_posted += iq->fill_cnt;
	iq->fill_cnt = 0;
	return NETDEV_TX_OK;
}

int octep_vf_get_if_stats(struct octep_vf_device *oct)
{
	struct octep_vf_iface_rxtx_stats vf_stats;
	int ret, size;

	memset(&vf_stats, 0, sizeof(struct octep_vf_iface_rxtx_stats));
	ret = octep_vf_mbox_bulk_read(oct, OCTEP_PFVF_MBOX_CMD_GET_STATS,
				      (u8 *)&vf_stats, &size);

	if (ret)
		return ret;

	memcpy(&oct->iface_rx_stats, &vf_stats.iface_rx_stats,
	       sizeof(struct octep_vf_iface_rx_stats));
	memcpy(&oct->iface_tx_stats, &vf_stats.iface_tx_stats,
	       sizeof(struct octep_vf_iface_tx_stats));

	return 0;
}

int octep_vf_get_link_info(struct octep_vf_device *oct)
{
	int ret, size;

	ret = octep_vf_mbox_bulk_read(oct, OCTEP_PFVF_MBOX_CMD_GET_LINK_INFO,
				      (u8 *)&oct->link_info, &size);
	if (ret) {
		dev_err(&oct->pdev->dev, "Get VF link info failed via VF Mbox\n");
		return ret;
	}
	return 0;
}

/**
 * octep_vf_get_stats64() - Get Octeon network device statistics.
 *
 * @netdev: kernel network device.
 * @stats: pointer to stats structure to be filled in.
 */
static void octep_vf_get_stats64(struct net_device *netdev,
				 struct rtnl_link_stats64 *stats)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	u64 tx_packets, tx_bytes, rx_packets, rx_bytes;
	int q;

	tx_packets = 0;
	tx_bytes = 0;
	rx_packets = 0;
	rx_bytes = 0;
	for (q = 0; q < OCTEP_VF_MAX_QUEUES; q++) {
		tx_packets += oct->stats_iq[q].instr_completed;
		tx_bytes += oct->stats_iq[q].bytes_sent;
		rx_packets += oct->stats_oq[q].packets;
		rx_bytes += oct->stats_oq[q].bytes;
	}
	stats->tx_packets = tx_packets;
	stats->tx_bytes = tx_bytes;
	stats->rx_packets = rx_packets;
	stats->rx_bytes = rx_bytes;
}

/**
 * octep_vf_tx_timeout_task - work queue task to Handle Tx queue timeout.
 *
 * @work: pointer to Tx queue timeout work_struct
 *
 * Stop and start the device so that it frees up all queue resources
 * and restarts the queues, that potentially clears a Tx queue timeout
 * condition.
 **/
static void octep_vf_tx_timeout_task(struct work_struct *work)
{
	struct octep_vf_device *oct = container_of(work, struct octep_vf_device,
						tx_timeout_task);
	struct net_device *netdev = oct->netdev;

	rtnl_lock();
	if (netif_running(netdev)) {
		octep_vf_stop(netdev);
		octep_vf_open(netdev);
	}
	rtnl_unlock();
	netdev_put(netdev, NULL);
}

/**
 * octep_vf_tx_timeout() - Handle Tx Queue timeout.
 *
 * @netdev: pointer to kernel network device.
 * @txqueue: Timed out Tx queue number.
 *
 * Schedule a work to handle Tx queue timeout.
 */
static void octep_vf_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct octep_vf_device *oct = netdev_priv(netdev);

	netdev_hold(netdev, NULL, GFP_ATOMIC);
	schedule_work(&oct->tx_timeout_task);
}

static int octep_vf_set_mac(struct net_device *netdev, void *p)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	struct sockaddr *addr = (struct sockaddr *)p;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	err = octep_vf_mbox_set_mac_addr(oct, addr->sa_data);
	if (err)
		return err;

	memcpy(oct->mac_addr, addr->sa_data, ETH_ALEN);
	eth_hw_addr_set(netdev, addr->sa_data);

	return 0;
}

static int octep_vf_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	struct octep_vf_iface_link_info *link_info;
	int err;

	link_info = &oct->link_info;
	if (link_info->mtu == new_mtu)
		return 0;

	err = octep_vf_mbox_set_mtu(oct, new_mtu);
	if (!err) {
		oct->link_info.mtu = new_mtu;
		WRITE_ONCE(netdev->mtu, new_mtu);
	}
	return err;
}

static int octep_vf_set_features(struct net_device *netdev,
				 netdev_features_t features)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	u16 rx_offloads = 0, tx_offloads = 0;
	int err;

	/* We only support features received from firmware */
	if ((features & netdev->hw_features) != features)
		return -EINVAL;

	if (features & NETIF_F_TSO)
		tx_offloads |= OCTEP_VF_TX_OFFLOAD_TSO;

	if (features & NETIF_F_TSO6)
		tx_offloads |= OCTEP_VF_TX_OFFLOAD_TSO;

	if (features & NETIF_F_IP_CSUM)
		tx_offloads |= OCTEP_VF_TX_OFFLOAD_CKSUM;

	if (features & NETIF_F_IPV6_CSUM)
		tx_offloads |= OCTEP_VF_TX_OFFLOAD_CKSUM;

	if (features & NETIF_F_RXCSUM)
		rx_offloads |= OCTEP_VF_RX_OFFLOAD_CKSUM;

	err = octep_vf_mbox_set_offloads(oct, tx_offloads, rx_offloads);
	if (!err)
		netdev->features = features;

	return err;
}

static const struct net_device_ops octep_vf_netdev_ops = {
	.ndo_open                = octep_vf_open,
	.ndo_stop                = octep_vf_stop,
	.ndo_start_xmit          = octep_vf_start_xmit,
	.ndo_get_stats64         = octep_vf_get_stats64,
	.ndo_tx_timeout          = octep_vf_tx_timeout,
	.ndo_set_mac_address     = octep_vf_set_mac,
	.ndo_change_mtu          = octep_vf_change_mtu,
	.ndo_set_features        = octep_vf_set_features,
};

static const char *octep_vf_devid_to_str(struct octep_vf_device *oct)
{
	switch (oct->chip_id) {
	case OCTEP_PCI_DEVICE_ID_CN93_VF:
		return "CN93XX";
	case OCTEP_PCI_DEVICE_ID_CNF95N_VF:
		return "CNF95N";
	case OCTEP_PCI_DEVICE_ID_CN10KA_VF:
		return "CN10KA";
	case OCTEP_PCI_DEVICE_ID_CNF10KA_VF:
		return "CNF10KA";
	case OCTEP_PCI_DEVICE_ID_CNF10KB_VF:
		return "CNF10KB";
	case OCTEP_PCI_DEVICE_ID_CN10KB_VF:
		return "CN10KB";
	default:
		return "Unsupported";
	}
}

/**
 * octep_vf_device_setup() - Setup Octeon Device.
 *
 * @oct: Octeon device private data structure.
 *
 * Setup Octeon device hardware operations, configuration, etc ...
 */
int octep_vf_device_setup(struct octep_vf_device *oct)
{
	struct pci_dev *pdev = oct->pdev;

	/* allocate memory for oct->conf */
	oct->conf = kzalloc(sizeof(*oct->conf), GFP_KERNEL);
	if (!oct->conf)
		return -ENOMEM;

	/* Map BAR region 0 */
	oct->mmio.hw_addr = ioremap(pci_resource_start(oct->pdev, 0),
				    pci_resource_len(oct->pdev, 0));
	if (!oct->mmio.hw_addr) {
		dev_err(&pdev->dev,
			"Failed to remap BAR0; start=0x%llx len=0x%llx\n",
			pci_resource_start(oct->pdev, 0),
			pci_resource_len(oct->pdev, 0));
		goto ioremap_err;
	}
	oct->mmio.mapped = 1;

	oct->chip_id = pdev->device;
	oct->rev_id = pdev->revision;
	dev_info(&pdev->dev, "chip_id = 0x%x\n", pdev->device);

	switch (oct->chip_id) {
	case OCTEP_PCI_DEVICE_ID_CN93_VF:
	case OCTEP_PCI_DEVICE_ID_CNF95N_VF:
	case OCTEP_PCI_DEVICE_ID_CN98_VF:
		dev_info(&pdev->dev, "Setting up OCTEON %s VF PASS%d.%d\n",
			 octep_vf_devid_to_str(oct), OCTEP_VF_MAJOR_REV(oct),
			 OCTEP_VF_MINOR_REV(oct));
		octep_vf_device_setup_cn93(oct);
		break;
	case OCTEP_PCI_DEVICE_ID_CNF10KA_VF:
	case OCTEP_PCI_DEVICE_ID_CN10KA_VF:
	case OCTEP_PCI_DEVICE_ID_CNF10KB_VF:
	case OCTEP_PCI_DEVICE_ID_CN10KB_VF:
		dev_info(&pdev->dev, "Setting up OCTEON %s VF PASS%d.%d\n",
			 octep_vf_devid_to_str(oct), OCTEP_VF_MAJOR_REV(oct),
			 OCTEP_VF_MINOR_REV(oct));
		octep_vf_device_setup_cnxk(oct);
		break;
	default:
		dev_err(&pdev->dev, "Unsupported device\n");
		goto unsupported_dev;
	}

	return 0;

unsupported_dev:
	iounmap(oct->mmio.hw_addr);
ioremap_err:
	kfree(oct->conf);
	return -EOPNOTSUPP;
}

/**
 * octep_vf_device_cleanup() - Cleanup Octeon Device.
 *
 * @oct: Octeon device private data structure.
 *
 * Cleanup Octeon device allocated resources.
 */
static void octep_vf_device_cleanup(struct octep_vf_device *oct)
{
	dev_info(&oct->pdev->dev, "Cleaning up Octeon Device ...\n");

	if (oct->mmio.mapped)
		iounmap(oct->mmio.hw_addr);

	kfree(oct->conf);
	oct->conf = NULL;
}

static int octep_vf_get_mac_addr(struct octep_vf_device *oct, u8 *addr)
{
	return octep_vf_mbox_get_mac_addr(oct, addr);
}

/**
 * octep_vf_probe() - Octeon PCI device probe handler.
 *
 * @pdev: PCI device structure.
 * @ent: entry in Octeon PCI device ID table.
 *
 * Initializes and enables the Octeon PCI device for network operations.
 * Initializes Octeon private data structure and registers a network device.
 */
static int octep_vf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct octep_vf_device *octep_vf_dev;
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
		goto disable_pci_device;
	}

	err = pci_request_mem_regions(pdev, OCTEP_VF_DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "Failed to map PCI memory regions\n");
		goto disable_pci_device;
	}

	pci_set_master(pdev);

	netdev = alloc_etherdev_mq(sizeof(struct octep_vf_device),
				   OCTEP_VF_MAX_QUEUES);
	if (!netdev) {
		dev_err(&pdev->dev, "Failed to allocate netdev\n");
		err = -ENOMEM;
		goto mem_regions_release;
	}
	SET_NETDEV_DEV(netdev, &pdev->dev);

	octep_vf_dev = netdev_priv(netdev);
	octep_vf_dev->netdev = netdev;
	octep_vf_dev->pdev = pdev;
	octep_vf_dev->dev = &pdev->dev;
	pci_set_drvdata(pdev, octep_vf_dev);

	err = octep_vf_device_setup(octep_vf_dev);
	if (err) {
		dev_err(&pdev->dev, "Device setup failed\n");
		goto netdevice_free;
	}
	INIT_WORK(&octep_vf_dev->tx_timeout_task, octep_vf_tx_timeout_task);

	netdev->netdev_ops = &octep_vf_netdev_ops;
	octep_vf_set_ethtool_ops(netdev);
	netif_carrier_off(netdev);

	if (octep_vf_setup_mbox(octep_vf_dev)) {
		dev_err(&pdev->dev, "VF Mailbox setup failed\n");
		err = -ENOMEM;
		goto device_cleanup;
	}

	if (octep_vf_mbox_version_check(octep_vf_dev)) {
		dev_err(&pdev->dev, "PF VF Mailbox version mismatch\n");
		err = -EINVAL;
		goto delete_mbox;
	}

	if (octep_vf_mbox_get_fw_info(octep_vf_dev)) {
		dev_err(&pdev->dev, "unable to get fw info\n");
		err = -EINVAL;
		goto delete_mbox;
	}

	netdev->hw_features = NETIF_F_SG;
	if (OCTEP_VF_TX_IP_CSUM(octep_vf_dev->fw_info.tx_ol_flags))
		netdev->hw_features |= (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);

	if (OCTEP_VF_RX_IP_CSUM(octep_vf_dev->fw_info.rx_ol_flags))
		netdev->hw_features |= NETIF_F_RXCSUM;

	netdev->min_mtu = OCTEP_VF_MIN_MTU;
	netdev->max_mtu = OCTEP_VF_MAX_MTU;
	netdev->mtu = OCTEP_VF_DEFAULT_MTU;

	if (OCTEP_VF_TX_TSO(octep_vf_dev->fw_info.tx_ol_flags)) {
		netdev->hw_features |= NETIF_F_TSO;
		netif_set_tso_max_size(netdev, netdev->max_mtu);
	}

	netdev->features |= netdev->hw_features;
	octep_vf_get_mac_addr(octep_vf_dev, octep_vf_dev->mac_addr);
	eth_hw_addr_set(netdev, octep_vf_dev->mac_addr);
	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register netdev\n");
		goto delete_mbox;
	}
	dev_info(&pdev->dev, "Device probe successful\n");
	return 0;

delete_mbox:
	octep_vf_delete_mbox(octep_vf_dev);
device_cleanup:
	octep_vf_device_cleanup(octep_vf_dev);
netdevice_free:
	free_netdev(netdev);
mem_regions_release:
	pci_release_mem_regions(pdev);
disable_pci_device:
	pci_disable_device(pdev);
	dev_err(&pdev->dev, "Device probe failed\n");
	return err;
}

/**
 * octep_vf_remove() - Remove Octeon PCI device from driver control.
 *
 * @pdev: PCI device structure of the Octeon device.
 *
 * Cleanup all resources allocated for the Octeon device.
 * Unregister from network device and disable the PCI device.
 */
static void octep_vf_remove(struct pci_dev *pdev)
{
	struct octep_vf_device *oct = pci_get_drvdata(pdev);
	struct net_device *netdev;

	if (!oct)
		return;

	octep_vf_mbox_dev_remove(oct);
	cancel_work_sync(&oct->tx_timeout_task);
	netdev = oct->netdev;
	if (netdev->reg_state == NETREG_REGISTERED)
		unregister_netdev(netdev);
	octep_vf_delete_mbox(oct);
	octep_vf_device_cleanup(oct);
	pci_release_mem_regions(pdev);
	free_netdev(netdev);
	pci_disable_device(pdev);
}

static struct pci_driver octep_vf_driver = {
	.name = OCTEP_VF_DRV_NAME,
	.id_table = octep_vf_pci_id_tbl,
	.probe = octep_vf_probe,
	.remove = octep_vf_remove,
};

/**
 * octep_vf_init_module() - Module initialization.
 *
 * create common resource for the driver and register PCI driver.
 */
static int __init octep_vf_init_module(void)
{
	int ret;

	pr_info("%s: Loading %s ...\n", OCTEP_VF_DRV_NAME, OCTEP_VF_DRV_STRING);

	ret = pci_register_driver(&octep_vf_driver);
	if (ret < 0) {
		pr_err("%s: Failed to register PCI driver; err=%d\n",
		       OCTEP_VF_DRV_NAME, ret);
		return ret;
	}

	return ret;
}

/**
 * octep_vf_exit_module() - Module exit routine.
 *
 * unregister the driver with PCI subsystem and cleanup common resources.
 */
static void __exit octep_vf_exit_module(void)
{
	pr_info("%s: Unloading ...\n", OCTEP_VF_DRV_NAME);

	pci_unregister_driver(&octep_vf_driver);

	pr_info("%s: Unloading complete\n", OCTEP_VF_DRV_NAME);
}

module_init(octep_vf_init_module);
module_exit(octep_vf_exit_module);
