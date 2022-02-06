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
 * struct ipoib_txreq - IPOIB transmit descriptor
 * @txreq: sdma transmit request
 * @sdma_hdr: 9b ib headers
 * @sdma_status: status returned by sdma engine
 * @complete: non-zero implies complete
 * @priv: ipoib netdev private data
 * @txq: txq on which skb was output
 * @skb: skb to send
 */
struct ipoib_txreq {
	struct sdma_txreq           txreq;
	struct hfi1_sdma_header     *sdma_hdr;
	int                         sdma_status;
	int                         complete;
	struct hfi1_ipoib_dev_priv *priv;
	struct hfi1_ipoib_txq      *txq;
	struct sk_buff             *skb;
};

/**
 * struct hfi1_ipoib_circ_buf - List of items to be processed
 * @items: ring of items each a power of two size
 * @max_items: max items + 1 that the ring can contain
 * @shift: log2 of size for getting txreq
 * @sent_txreqs: count of txreqs posted to sdma
 * @tail: ring tail
 * @stops: count of stops of queue
 * @ring_full: ring has been filled
 * @no_desc: descriptor shortage seen
 * @complete_txreqs: count of txreqs completed by sdma
 * @head: ring head
 */
struct hfi1_ipoib_circ_buf {
	void *items;
	u32 max_items;
	u32 shift;
	/* consumer cache line */
	u64 ____cacheline_aligned_in_smp sent_txreqs;
	u32 avail;
	u32 tail;
	atomic_t stops;
	atomic_t ring_full;
	atomic_t no_desc;
	/* producer cache line */
	u64 ____cacheline_aligned_in_smp complete_txreqs;
	u32 head;
};

/**
 * struct hfi1_ipoib_txq - IPOIB per Tx queue information
 * @priv: private pointer
 * @sde: sdma engine
 * @tx_list: tx request list
 * @sent_txreqs: count of txreqs posted to sdma
 * @flow: tracks when list needs to be flushed for a flow change
 * @q_idx: ipoib Tx queue index
 * @pkts_sent: indicator packets have been sent from this queue
 * @wait: iowait structure
 * @napi: pointer to tx napi interface
 * @tx_ring: ring of ipoib txreqs to be reaped by napi callback
 */
struct hfi1_ipoib_txq {
	struct napi_struct napi;
	struct hfi1_ipoib_dev_priv *priv;
	struct sdma_engine *sde;
	struct list_head tx_list;
	union hfi1_ipoib_flow flow;
	u8 q_idx;
	bool pkts_sent;
	struct iowait wait;

	struct hfi1_ipoib_circ_buf ____cacheline_aligned_in_smp tx_ring;
};

struct hfi1_ipoib_dev_priv {
	struct hfi1_devdata *dd;
	struct net_device   *netdev;
	struct ib_device    *device;
	struct hfi1_ipoib_txq *txqs;
	const struct net_device_ops *netdev_ops;
	struct rvt_qp *qp;
	u32 qkey;
	u16 pkey;
	u16 pkey_index;
	u8 port_num;
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

int hfi1_ipoib_send(struct net_device *dev,
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
			     u32 port_num,
			     enum rdma_netdev_t type,
			     struct rdma_netdev_alloc_params *params);

void hfi1_ipoib_tx_timeout(struct net_device *dev, unsigned int q);

#endif /* _IPOIB_H */
