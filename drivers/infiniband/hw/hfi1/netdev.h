/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2020 Intel Corporation.
 *
 */

#ifndef HFI1_NETDEV_H
#define HFI1_NETDEV_H

#include "hfi.h"

#include <linux/netdevice.h>
#include <linux/xarray.h>

/**
 * struct hfi1_netdev_rxq - Receive Queue for HFI
 * Both IPoIB and VNIC netdevices will be working on the rx abstraction.
 * @napi: napi object
 * @rx: ptr to netdev_rx
 * @rcd:  ptr to receive context data
 */
struct hfi1_netdev_rxq {
	struct napi_struct napi;
	struct hfi1_netdev_rx *rx;
	struct hfi1_ctxtdata *rcd;
};

/*
 * Number of netdev contexts used. Ensure it is less than or equal to
 * max queues supported by VNIC (HFI1_VNIC_MAX_QUEUE).
 */
#define HFI1_MAX_NETDEV_CTXTS   8

/* Number of NETDEV RSM entries */
#define NUM_NETDEV_MAP_ENTRIES HFI1_MAX_NETDEV_CTXTS

/**
 * struct hfi1_netdev_rx: data required to setup and run HFI netdev.
 * @rx_napi:	the dummy netdevice to support "polling" the receive contexts
 * @dd:		hfi1_devdata
 * @rxq:	pointer to dummy netdev receive queues.
 * @num_rx_q:	number of receive queues
 * @rmt_index:	first free index in RMT Array
 * @msix_start: first free MSI-X interrupt vector.
 * @dev_tbl:	netdev table for unique identifier VNIC and IPoIb VLANs.
 * @enabled:	atomic counter of netdevs enabling receive queues.
 *		When 0 NAPI will be disabled.
 * @netdevs:	atomic counter of netdevs using dummy netdev.
 *		When 0 receive queues will be freed.
 */
struct hfi1_netdev_rx {
	struct net_device rx_napi;
	struct hfi1_devdata *dd;
	struct hfi1_netdev_rxq *rxq;
	int num_rx_q;
	int rmt_start;
	struct xarray dev_tbl;
	/* count of enabled napi polls */
	atomic_t enabled;
	/* count of netdevs on top */
	atomic_t netdevs;
};

static inline
int hfi1_netdev_ctxt_count(struct hfi1_devdata *dd)
{
	return dd->netdev_rx->num_rx_q;
}

static inline
struct hfi1_ctxtdata *hfi1_netdev_get_ctxt(struct hfi1_devdata *dd, int ctxt)
{
	return dd->netdev_rx->rxq[ctxt].rcd;
}

static inline
int hfi1_netdev_get_free_rmt_idx(struct hfi1_devdata *dd)
{
	return dd->netdev_rx->rmt_start;
}

static inline
void hfi1_netdev_set_free_rmt_idx(struct hfi1_devdata *dd, int rmt_idx)
{
	dd->netdev_rx->rmt_start = rmt_idx;
}

u32 hfi1_num_netdev_contexts(struct hfi1_devdata *dd, u32 available_contexts,
			     struct cpumask *cpu_mask);

void hfi1_netdev_enable_queues(struct hfi1_devdata *dd);
void hfi1_netdev_disable_queues(struct hfi1_devdata *dd);
int hfi1_netdev_rx_init(struct hfi1_devdata *dd);
int hfi1_netdev_rx_destroy(struct hfi1_devdata *dd);
int hfi1_alloc_rx(struct hfi1_devdata *dd);
void hfi1_free_rx(struct hfi1_devdata *dd);
int hfi1_netdev_add_data(struct hfi1_devdata *dd, int id, void *data);
void *hfi1_netdev_remove_data(struct hfi1_devdata *dd, int id);
void *hfi1_netdev_get_data(struct hfi1_devdata *dd, int id);
void *hfi1_netdev_get_first_data(struct hfi1_devdata *dd, int *start_id);

/* chip.c  */
int hfi1_netdev_rx_napi(struct napi_struct *napi, int budget);

#endif /* HFI1_NETDEV_H */
