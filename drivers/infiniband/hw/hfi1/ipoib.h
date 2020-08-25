/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2020 Intel Corporation.
 *
 */

/*
 * This file contains HFI1 support for IPOIB functionality
 */

#ifndef HFI1_IPOIB_H
#define HFI1_IPOIB_H

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/atomic.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/if_infiniband.h>

#include "hfi.h"
#include "iowait.h"
#include "netdev.h"

#include <rdma/ib_verbs.h>

#define HFI1_IPOIB_ENTROPY_SHIFT   24

#define HFI1_IPOIB_TXREQ_NAME_LEN   32

#define HFI1_IPOIB_PSEUDO_LEN 20
#define HFI1_IPOIB_ENCAP_LEN 4

struct hfi1_ipoib_dev_priv;

union hfi1_ipoib_flow {
	u16 as_int;
	struct {
		u8 tx_queue;
		u8 sc5;
	} __attribute__((__packed__));
};

/**
 * struct hfi1_ipoib_circ_buf - List of items to be processed
 * @items: ring of items
 * @head: ring head
 * @tail: ring tail
 * @max_items: max items + 1 that the ring can contain
 * @producer_lock: producer sync lock
 * @consumer_lock: consumer sync lock
 */
struct hfi1_ipoib_circ_buf {
	void **items;
	unsigned long head;
	unsigned long tail;
	unsigned long max_items;
	spinlock_t producer_lock; /* head sync lock */
	spinlock_t consumer_lock; /* tail sync lock */
};

/**
 * struct hfi1_ipoib_txq - IPOIB per Tx queue information
 * @priv: private pointer
 * @sde: sdma engine
 * @tx_list: tx request list
 * @sent_txreqs: count of txreqs posted to sdma
 * @stops: count of stops of queue
 * @ring_full: ring has been filled
 * @no_desc: descriptor shortage seen
 * @flow: tracks when list needs to be flushed for a flow change
 * @q_idx: ipoib Tx queue index
 * @pkts_sent: indicator packets have been sent from this queue
 * @wait: iowait structure
 * @complete_txreqs: count of txreqs completed by sdma
 * @napi: pointer to tx napi interface
 * @tx_ring: ring of ipoib txreqs to be reaped by napi callback
 */
struct hfi1_ipoib_txq {
	struct hfi1_ipoib_dev_priv *priv;
	struct sdma_engine *sde;
	struct list_head tx_list;
	u64 sent_txreqs;
	atomic_t stops;
	atomic_t ring_full;
	atomic_t no_desc;
	union hfi1_ipoib_flow flow;
	u8 q_idx;
	bool pkts_sent;
	struct iowait wait;

	atomic64_t ____cacheline_aligned_in_smp complete_txreqs;
	struct napi_struct *napi;
	struct hfi1_ipoib_circ_buf tx_ring;
};

struct hfi1_ipoib_dev_priv {
	struct hfi1_devdata *dd;
	struct net_device   *netdev;
	struct ib_device    *device;
	struct hfi1_ipoib_txq *txqs;
	struct kmem_cache *txreq_cache;
	struct napi_struct *tx_napis;
	u16 pkey;
	u16 pkey_index;
	u32 qkey;
	u8 port_num;

	const struct net_device_ops *netdev_ops;
	struct rvt_qp *qp;
	struct pcpu_sw_netstats __percpu *netstats;
};

/* hfi1 ipoib rdma netdev's private data structure */
struct hfi1_ipoib_rdma_netdev {
	struct rdma_netdev rn;  /* keep this first */
	/* followed by device private data */
	struct hfi1_ipoib_dev_priv dev_priv;
};

static inline struct hfi1_ipoib_dev_priv *
hfi1_ipoib_priv(const struct net_device *dev)
{
	return &((struct hfi1_ipoib_rdma_netdev *)netdev_priv(dev))->dev_priv;
}

static inline void
hfi1_ipoib_update_rx_netstats(struct hfi1_ipoib_dev_priv *priv,
			      u64 packets,
			      u64 bytes)
{
	struct pcpu_sw_netstats *netstats = this_cpu_ptr(priv->netstats);

	u64_stats_update_begin(&netstats->syncp);
	netstats->rx_packets += packets;
	netstats->rx_bytes += bytes;
	u64_stats_update_end(&netstats->syncp);
}

static inline void
hfi1_ipoib_update_tx_netstats(struct hfi1_ipoib_dev_priv *priv,
			      u64 packets,
			      u64 bytes)
{
	struct pcpu_sw_netstats *netstats = this_cpu_ptr(priv->netstats);

	u64_stats_update_begin(&netstats->syncp);
	netstats->tx_packets += packets;
	netstats->tx_bytes += bytes;
	u64_stats_update_end(&netstats->syncp);
}

int hfi1_ipoib_send_dma(struct net_device *dev,
			struct sk_buff *skb,
			struct ib_ah *address,
			u32 dqpn);

int hfi1_ipoib_txreq_init(struct hfi1_ipoib_dev_priv *priv);
void hfi1_ipoib_txreq_deinit(struct hfi1_ipoib_dev_priv *priv);

int hfi1_ipoib_rxq_init(struct net_device *dev);
void hfi1_ipoib_rxq_deinit(struct net_device *dev);

void hfi1_ipoib_napi_tx_enable(struct net_device *dev);
void hfi1_ipoib_napi_tx_disable(struct net_device *dev);

struct sk_buff *hfi1_ipoib_prepare_skb(struct hfi1_netdev_rxq *rxq,
				       int size, void *data);

int hfi1_ipoib_rn_get_params(struct ib_device *device,
			     u8 port_num,
			     enum rdma_netdev_t type,
			     struct rdma_netdev_alloc_params *params);

#endif /* _IPOIB_H */
