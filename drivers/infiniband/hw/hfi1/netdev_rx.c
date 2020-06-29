// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright(c) 2020 Intel Corporation.
 *
 */

/*
 * This file contains HFI1 support for netdev RX functionality
 */

#include "sdma.h"
#include "verbs.h"
#include "netdev.h"
#include "hfi.h"

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <rdma/ib_verbs.h>

static int hfi1_netdev_setup_ctxt(struct hfi1_netdev_priv *priv,
				  struct hfi1_ctxtdata *uctxt)
{
	unsigned int rcvctrl_ops;
	struct hfi1_devdata *dd = priv->dd;
	int ret;

	uctxt->rhf_rcv_function_map = netdev_rhf_rcv_functions;
	uctxt->do_interrupt = &handle_receive_interrupt_napi_sp;

	/* Now allocate the RcvHdr queue and eager buffers. */
	ret = hfi1_create_rcvhdrq(dd, uctxt);
	if (ret)
		goto done;

	ret = hfi1_setup_eagerbufs(uctxt);
	if (ret)
		goto done;

	clear_rcvhdrtail(uctxt);

	rcvctrl_ops = HFI1_RCVCTRL_CTXT_DIS;
	rcvctrl_ops |= HFI1_RCVCTRL_INTRAVAIL_DIS;

	if (!HFI1_CAP_KGET_MASK(uctxt->flags, MULTI_PKT_EGR))
		rcvctrl_ops |= HFI1_RCVCTRL_ONE_PKT_EGR_ENB;
	if (HFI1_CAP_KGET_MASK(uctxt->flags, NODROP_EGR_FULL))
		rcvctrl_ops |= HFI1_RCVCTRL_NO_EGR_DROP_ENB;
	if (HFI1_CAP_KGET_MASK(uctxt->flags, NODROP_RHQ_FULL))
		rcvctrl_ops |= HFI1_RCVCTRL_NO_RHQ_DROP_ENB;
	if (HFI1_CAP_KGET_MASK(uctxt->flags, DMA_RTAIL))
		rcvctrl_ops |= HFI1_RCVCTRL_TAILUPD_ENB;

	hfi1_rcvctrl(uctxt->dd, rcvctrl_ops, uctxt);
done:
	return ret;
}

static int hfi1_netdev_allocate_ctxt(struct hfi1_devdata *dd,
				     struct hfi1_ctxtdata **ctxt)
{
	struct hfi1_ctxtdata *uctxt;
	int ret;

	if (dd->flags & HFI1_FROZEN)
		return -EIO;

	ret = hfi1_create_ctxtdata(dd->pport, dd->node, &uctxt);
	if (ret < 0) {
		dd_dev_err(dd, "Unable to create ctxtdata, failing open\n");
		return -ENOMEM;
	}

	uctxt->flags = HFI1_CAP_KGET(MULTI_PKT_EGR) |
		HFI1_CAP_KGET(NODROP_RHQ_FULL) |
		HFI1_CAP_KGET(NODROP_EGR_FULL) |
		HFI1_CAP_KGET(DMA_RTAIL);
	/* Netdev contexts are always NO_RDMA_RTAIL */
	uctxt->fast_handler = handle_receive_interrupt_napi_fp;
	uctxt->slow_handler = handle_receive_interrupt_napi_sp;
	hfi1_set_seq_cnt(uctxt, 1);
	uctxt->is_vnic = true;

	hfi1_stats.sps_ctxts++;

	dd_dev_info(dd, "created netdev context %d\n", uctxt->ctxt);
	*ctxt = uctxt;

	return 0;
}

static void hfi1_netdev_deallocate_ctxt(struct hfi1_devdata *dd,
					struct hfi1_ctxtdata *uctxt)
{
	flush_wc();

	/*
	 * Disable receive context and interrupt available, reset all
	 * RcvCtxtCtrl bits to default values.
	 */
	hfi1_rcvctrl(dd, HFI1_RCVCTRL_CTXT_DIS |
		     HFI1_RCVCTRL_TIDFLOW_DIS |
		     HFI1_RCVCTRL_INTRAVAIL_DIS |
		     HFI1_RCVCTRL_ONE_PKT_EGR_DIS |
		     HFI1_RCVCTRL_NO_RHQ_DROP_DIS |
		     HFI1_RCVCTRL_NO_EGR_DROP_DIS, uctxt);

	if (uctxt->msix_intr != CCE_NUM_MSIX_VECTORS)
		msix_free_irq(dd, uctxt->msix_intr);

	uctxt->msix_intr = CCE_NUM_MSIX_VECTORS;
	uctxt->event_flags = 0;

	hfi1_clear_tids(uctxt);
	hfi1_clear_ctxt_pkey(dd, uctxt);

	hfi1_stats.sps_ctxts--;

	hfi1_free_ctxt(uctxt);
}

static int hfi1_netdev_allot_ctxt(struct hfi1_netdev_priv *priv,
				  struct hfi1_ctxtdata **ctxt)
{
	int rc;
	struct hfi1_devdata *dd = priv->dd;

	rc = hfi1_netdev_allocate_ctxt(dd, ctxt);
	if (rc) {
		dd_dev_err(dd, "netdev ctxt alloc failed %d\n", rc);
		return rc;
	}

	rc = hfi1_netdev_setup_ctxt(priv, *ctxt);
	if (rc) {
		dd_dev_err(dd, "netdev ctxt setup failed %d\n", rc);
		hfi1_netdev_deallocate_ctxt(dd, *ctxt);
		*ctxt = NULL;
	}

	return rc;
}

/**
 * hfi1_num_netdev_contexts - Count of netdev recv contexts to use.
 * @dd: device on which to allocate netdev contexts
 * @available_contexts: count of available receive contexts
 * @cpu_mask: mask of possible cpus to include for contexts
 *
 * Return: count of physical cores on a node or the remaining available recv
 * contexts for netdev recv context usage up to the maximum of
 * HFI1_MAX_NETDEV_CTXTS.
 * A value of 0 can be returned when acceleration is explicitly turned off,
 * a memory allocation error occurs or when there are no available contexts.
 *
 */
u32 hfi1_num_netdev_contexts(struct hfi1_devdata *dd, u32 available_contexts,
			     struct cpumask *cpu_mask)
{
	cpumask_var_t node_cpu_mask;
	unsigned int available_cpus;

	if (!HFI1_CAP_IS_KSET(AIP))
		return 0;

	/* Always give user contexts priority over netdev contexts */
	if (available_contexts == 0) {
		dd_dev_info(dd, "No receive contexts available for netdevs.\n");
		return 0;
	}

	if (!zalloc_cpumask_var(&node_cpu_mask, GFP_KERNEL)) {
		dd_dev_err(dd, "Unable to allocate cpu_mask for netdevs.\n");
		return 0;
	}

	cpumask_and(node_cpu_mask, cpu_mask,
		    cpumask_of_node(pcibus_to_node(dd->pcidev->bus)));

	available_cpus = cpumask_weight(node_cpu_mask);

	free_cpumask_var(node_cpu_mask);

	return min3(available_cpus, available_contexts,
		    (u32)HFI1_MAX_NETDEV_CTXTS);
}

static int hfi1_netdev_rxq_init(struct net_device *dev)
{
	int i;
	int rc;
	struct hfi1_netdev_priv *priv = hfi1_netdev_priv(dev);
	struct hfi1_devdata *dd = priv->dd;

	priv->num_rx_q = dd->num_netdev_contexts;
	priv->rxq = kcalloc_node(priv->num_rx_q, sizeof(struct hfi1_netdev_rxq),
				 GFP_KERNEL, dd->node);

	if (!priv->rxq) {
		dd_dev_err(dd, "Unable to allocate netdev queue data\n");
		return (-ENOMEM);
	}

	for (i = 0; i < priv->num_rx_q; i++) {
		struct hfi1_netdev_rxq *rxq = &priv->rxq[i];

		rc = hfi1_netdev_allot_ctxt(priv, &rxq->rcd);
		if (rc)
			goto bail_context_irq_failure;

		hfi1_rcd_get(rxq->rcd);
		rxq->priv = priv;
		rxq->rcd->napi = &rxq->napi;
		dd_dev_info(dd, "Setting rcv queue %d napi to context %d\n",
			    i, rxq->rcd->ctxt);
		/*
		 * Disable BUSY_POLL on this NAPI as this is not supported
		 * right now.
		 */
		set_bit(NAPI_STATE_NO_BUSY_POLL, &rxq->napi.state);
		netif_napi_add(dev, &rxq->napi, hfi1_netdev_rx_napi, 64);
		rc = msix_netdev_request_rcd_irq(rxq->rcd);
		if (rc)
			goto bail_context_irq_failure;
	}

	return 0;

bail_context_irq_failure:
	dd_dev_err(dd, "Unable to allot receive context\n");
	for (; i >= 0; i--) {
		struct hfi1_netdev_rxq *rxq = &priv->rxq[i];

		if (rxq->rcd) {
			hfi1_netdev_deallocate_ctxt(dd, rxq->rcd);
			hfi1_rcd_put(rxq->rcd);
			rxq->rcd = NULL;
		}
	}
	kfree(priv->rxq);
	priv->rxq = NULL;

	return rc;
}

static void hfi1_netdev_rxq_deinit(struct net_device *dev)
{
	int i;
	struct hfi1_netdev_priv *priv = hfi1_netdev_priv(dev);
	struct hfi1_devdata *dd = priv->dd;

	for (i = 0; i < priv->num_rx_q; i++) {
		struct hfi1_netdev_rxq *rxq = &priv->rxq[i];

		netif_napi_del(&rxq->napi);
		hfi1_netdev_deallocate_ctxt(dd, rxq->rcd);
		hfi1_rcd_put(rxq->rcd);
		rxq->rcd = NULL;
	}

	kfree(priv->rxq);
	priv->rxq = NULL;
	priv->num_rx_q = 0;
}

static void enable_queues(struct hfi1_netdev_priv *priv)
{
	int i;

	for (i = 0; i < priv->num_rx_q; i++) {
		struct hfi1_netdev_rxq *rxq = &priv->rxq[i];

		dd_dev_info(priv->dd, "enabling queue %d on context %d\n", i,
			    rxq->rcd->ctxt);
		napi_enable(&rxq->napi);
		hfi1_rcvctrl(priv->dd,
			     HFI1_RCVCTRL_CTXT_ENB | HFI1_RCVCTRL_INTRAVAIL_ENB,
			     rxq->rcd);
	}
}

static void disable_queues(struct hfi1_netdev_priv *priv)
{
	int i;

	msix_netdev_synchronize_irq(priv->dd);

	for (i = 0; i < priv->num_rx_q; i++) {
		struct hfi1_netdev_rxq *rxq = &priv->rxq[i];

		dd_dev_info(priv->dd, "disabling queue %d on context %d\n", i,
			    rxq->rcd->ctxt);

		/* wait for napi if it was scheduled */
		hfi1_rcvctrl(priv->dd,
			     HFI1_RCVCTRL_CTXT_DIS | HFI1_RCVCTRL_INTRAVAIL_DIS,
			     rxq->rcd);
		napi_synchronize(&rxq->napi);
		napi_disable(&rxq->napi);
	}
}

/**
 * hfi1_netdev_rx_init - Incrememnts netdevs counter. When called first time,
 * it allocates receive queue data and calls netif_napi_add
 * for each queue.
 *
 * @dd: hfi1 dev data
 */
int hfi1_netdev_rx_init(struct hfi1_devdata *dd)
{
	struct hfi1_netdev_priv *priv = hfi1_netdev_priv(dd->dummy_netdev);
	int res;

	if (atomic_fetch_inc(&priv->netdevs))
		return 0;

	mutex_lock(&hfi1_mutex);
	init_dummy_netdev(dd->dummy_netdev);
	res = hfi1_netdev_rxq_init(dd->dummy_netdev);
	mutex_unlock(&hfi1_mutex);
	return res;
}

/**
 * hfi1_netdev_rx_destroy - Decrements netdevs counter, when it reaches 0
 * napi is deleted and receive queses memory is freed.
 *
 * @dd: hfi1 dev data
 */
int hfi1_netdev_rx_destroy(struct hfi1_devdata *dd)
{
	struct hfi1_netdev_priv *priv = hfi1_netdev_priv(dd->dummy_netdev);

	/* destroy the RX queues only if it is the last netdev going away */
	if (atomic_fetch_add_unless(&priv->netdevs, -1, 0) == 1) {
		mutex_lock(&hfi1_mutex);
		hfi1_netdev_rxq_deinit(dd->dummy_netdev);
		mutex_unlock(&hfi1_mutex);
	}

	return 0;
}

/**
 * hfi1_netdev_alloc - Allocates netdev and private data. It is required
 * because RMT index and MSI-X interrupt can be set only
 * during driver initialization.
 *
 * @dd: hfi1 dev data
 */
int hfi1_netdev_alloc(struct hfi1_devdata *dd)
{
	struct hfi1_netdev_priv *priv;
	const int netdev_size = sizeof(*dd->dummy_netdev) +
		sizeof(struct hfi1_netdev_priv);

	dd_dev_info(dd, "allocating netdev size %d\n", netdev_size);
	dd->dummy_netdev = kcalloc_node(1, netdev_size, GFP_KERNEL, dd->node);

	if (!dd->dummy_netdev)
		return -ENOMEM;

	priv = hfi1_netdev_priv(dd->dummy_netdev);
	priv->dd = dd;
	xa_init(&priv->dev_tbl);
	atomic_set(&priv->enabled, 0);
	atomic_set(&priv->netdevs, 0);

	return 0;
}

void hfi1_netdev_free(struct hfi1_devdata *dd)
{
	if (dd->dummy_netdev) {
		dd_dev_info(dd, "hfi1 netdev freed\n");
		kfree(dd->dummy_netdev);
		dd->dummy_netdev = NULL;
	}
}

/**
 * hfi1_netdev_enable_queues - This is napi enable function.
 * It enables napi objects associated with queues.
 * When at least one device has called it it increments atomic counter.
 * Disable function decrements counter and when it is 0,
 * calls napi_disable for every queue.
 *
 * @dd: hfi1 dev data
 */
void hfi1_netdev_enable_queues(struct hfi1_devdata *dd)
{
	struct hfi1_netdev_priv *priv;

	if (!dd->dummy_netdev)
		return;

	priv = hfi1_netdev_priv(dd->dummy_netdev);
	if (atomic_fetch_inc(&priv->enabled))
		return;

	mutex_lock(&hfi1_mutex);
	enable_queues(priv);
	mutex_unlock(&hfi1_mutex);
}

void hfi1_netdev_disable_queues(struct hfi1_devdata *dd)
{
	struct hfi1_netdev_priv *priv;

	if (!dd->dummy_netdev)
		return;

	priv = hfi1_netdev_priv(dd->dummy_netdev);
	if (atomic_dec_if_positive(&priv->enabled))
		return;

	mutex_lock(&hfi1_mutex);
	disable_queues(priv);
	mutex_unlock(&hfi1_mutex);
}

/**
 * hfi1_netdev_add_data - Registers data with unique identifier
 * to be requested later this is needed for VNIC and IPoIB VLANs
 * implementations.
 * This call is protected by mutex idr_lock.
 *
 * @dd: hfi1 dev data
 * @id: requested integer id up to INT_MAX
 * @data: data to be associated with index
 */
int hfi1_netdev_add_data(struct hfi1_devdata *dd, int id, void *data)
{
	struct hfi1_netdev_priv *priv = hfi1_netdev_priv(dd->dummy_netdev);

	return xa_insert(&priv->dev_tbl, id, data, GFP_NOWAIT);
}

/**
 * hfi1_netdev_remove_data - Removes data with previously given id.
 * Returns the reference to removed entry.
 *
 * @dd: hfi1 dev data
 * @id: requested integer id up to INT_MAX
 */
void *hfi1_netdev_remove_data(struct hfi1_devdata *dd, int id)
{
	struct hfi1_netdev_priv *priv = hfi1_netdev_priv(dd->dummy_netdev);

	return xa_erase(&priv->dev_tbl, id);
}

/**
 * hfi1_netdev_get_data - Gets data with given id
 *
 * @dd: hfi1 dev data
 * @id: requested integer id up to INT_MAX
 */
void *hfi1_netdev_get_data(struct hfi1_devdata *dd, int id)
{
	struct hfi1_netdev_priv *priv = hfi1_netdev_priv(dd->dummy_netdev);

	return xa_load(&priv->dev_tbl, id);
}

/**
 * hfi1_netdev_get_first_dat - Gets first entry with greater or equal id.
 *
 * @dd: hfi1 dev data
 * @id: requested integer id up to INT_MAX
 */
void *hfi1_netdev_get_first_data(struct hfi1_devdata *dd, int *start_id)
{
	struct hfi1_netdev_priv *priv = hfi1_netdev_priv(dd->dummy_netdev);
	unsigned long index = *start_id;
	void *ret;

	ret = xa_find(&priv->dev_tbl, &index, UINT_MAX, XA_PRESENT);
	*start_id = (int)index;
	return ret;
}
