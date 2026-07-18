/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#ifndef __EEA_NET_H__
#define __EEA_NET_H__

#include <linux/ethtool.h>
#include <linux/netdevice.h>

#include "eea_adminq.h"
#include "eea_ethtool.h"
#include "eea_ring.h"

#define EEA_VER_MAJOR		1
#define EEA_VER_MINOR		0
#define EEA_VER_SUB_MINOR	0

struct eea_tx_meta;

struct eea_net_tx {
	struct eea_net *enet;

	struct eea_ring *ering;

	struct eea_tx_meta *meta;
	struct eea_tx_meta *free;

	struct device *dma_dev;

	u32 index;

	char name[16];

	struct eea_tx_stats stats;
};

struct eea_rx_meta {
	struct eea_rx_meta *next;

	struct page *page;
	dma_addr_t dma;
	u32 offset;
	u32 sync_for_cpu;
	u32 frags;

	struct page *hdr_page;
	void *hdr_addr;
	dma_addr_t hdr_dma;

	u32 id;

	u32 truesize;
	u32 headroom;
	u32 tailroom;

	u32 len;

	bool in_use;
};

struct eea_net_rx_pkt_ctx {
	u16 idx;

	bool data_valid;
	bool do_drop;

	u32 recv_len;
	struct sk_buff *head_skb;
};

struct eea_net_rx {
	struct eea_net *enet;

	struct eea_ring *ering;

	struct eea_rx_meta *meta;
	struct eea_rx_meta *free;

	struct device *dma_dev;

	u32 index;

	u32 flags;

	u32 headroom;

	struct napi_struct *napi;

	struct eea_rx_stats stats;

	char name[16];

	struct eea_net_rx_pkt_ctx pkt;

	struct page_pool *pp;
};

struct eea_net_cfg {
	u32 rx_ring_depth;
	u32 tx_ring_depth;
	u32 rx_ring_num;
	u32 tx_ring_num;

	u8 rx_sq_desc_size;
	u8 rx_cq_desc_size;
	u8 tx_sq_desc_size;
	u8 tx_cq_desc_size;

	u32 split_hdr;

	struct hwtstamp_config ts_cfg;
};

struct eea_net_init_ctx {
	struct eea_net_cfg cfg;

	struct eea_net_tx *tx;
	struct eea_net_rx **rx;

	struct net_device *netdev;
	struct eea_device *edev;
};

enum {
	EEA_LINK_ERR_NONE,
	EEA_LINK_ERR_HA_RESET_DEV,
	EEA_LINK_ERR_LINK_DOWN,
};

struct eea_irq_blk {
	struct napi_struct napi;
	u16 msix_vec;
	bool ready;
	struct eea_net_rx *rx;
	char irq_name[32];
	int irq;
	int idx;

};

struct eea_net {
	struct eea_device *edev;
	struct net_device *netdev;

	struct eea_aq adminq;

	struct eea_net_tx *tx;
	struct eea_net_rx **rx;

	struct eea_net_cfg cfg;
	struct eea_net_cfg cfg_hw;

	struct eea_irq_blk *irq_blks;

	u32 link_err;

	bool started;
	bool wait_pci_ready;

	u8 duplex;
	u32 speed;

	u64 hw_ts_offset;

	/* Protect the tx and rx of struct eea_net, when eea_stats accesses the
	 * stats from rx and tx queues.
	 */
	spinlock_t stats_lock;
};

int eea_net_probe(struct eea_device *edev);
void eea_net_remove(struct eea_device *edev, bool ha);
void eea_net_shutdown(struct eea_device *edev);

int eea_reset_hw_resources(struct eea_net *enet, struct eea_net_init_ctx *ctx);
void eea_init_ctx(struct eea_net *enet, struct eea_net_init_ctx *ctx);
int eea_queues_check_and_reset(struct eea_device *edev);

/* rx apis */

void enet_rx_stop(struct eea_net_rx *rx);
void enet_rx_start(struct eea_net_rx *rx);

void eea_free_rx(struct eea_net_rx *rx, struct eea_net_cfg *cfg);
struct eea_net_rx *eea_alloc_rx(struct eea_net_init_ctx *ctx, u32 idx);

/* tx apis */
int eea_poll_tx(struct eea_net_tx *tx, int budget);
netdev_tx_t eea_tx_xmit(struct sk_buff *skb, struct net_device *netdev);

void eea_free_tx(struct eea_net_tx *tx, struct eea_net_cfg *cfg);
int eea_alloc_tx(struct eea_net_init_ctx *ctx, struct eea_net_tx *tx, u32 idx);

#endif
