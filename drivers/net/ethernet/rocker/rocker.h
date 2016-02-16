/*
 * drivers/net/ethernet/rocker/rocker.h - Rocker switch device driver
 * Copyright (c) 2014-2016 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2014 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _ROCKER_H
#define _ROCKER_H

#include <linux/types.h>
#include <linux/hashtable.h>
#include <linux/if_vlan.h>

#include "rocker_hw.h"

struct rocker_desc_info {
	char *data; /* mapped */
	size_t data_size;
	size_t tlv_size;
	struct rocker_desc *desc;
	dma_addr_t mapaddr;
};

struct rocker_dma_ring_info {
	size_t size;
	u32 head;
	u32 tail;
	struct rocker_desc *desc; /* mapped */
	dma_addr_t mapaddr;
	struct rocker_desc_info *desc_info;
	unsigned int type;
};

struct rocker;

enum {
	ROCKER_CTRL_LINK_LOCAL_MCAST,
	ROCKER_CTRL_LOCAL_ARP,
	ROCKER_CTRL_IPV4_MCAST,
	ROCKER_CTRL_IPV6_MCAST,
	ROCKER_CTRL_DFLT_BRIDGING,
	ROCKER_CTRL_DFLT_OVS,
	ROCKER_CTRL_MAX,
};

#define ROCKER_INTERNAL_VLAN_ID_BASE	0x0f00
#define ROCKER_N_INTERNAL_VLANS		255
#define ROCKER_VLAN_BITMAP_LEN		BITS_TO_LONGS(VLAN_N_VID)
#define ROCKER_INTERNAL_VLAN_BITMAP_LEN	BITS_TO_LONGS(ROCKER_N_INTERNAL_VLANS)

struct rocker_port {
	struct net_device *dev;
	struct net_device *bridge_dev;
	struct rocker *rocker;
	unsigned int port_number;
	u32 pport;
	__be16 internal_vlan_id;
	int stp_state;
	u32 brport_flags;
	unsigned long ageing_time;
	bool ctrls[ROCKER_CTRL_MAX];
	unsigned long vlan_bitmap[ROCKER_VLAN_BITMAP_LEN];
	struct napi_struct napi_tx;
	struct napi_struct napi_rx;
	struct rocker_dma_ring_info tx_ring;
	struct rocker_dma_ring_info rx_ring;
};

struct rocker {
	struct pci_dev *pdev;
	u8 __iomem *hw_addr;
	struct msix_entry *msix_entries;
	unsigned int port_count;
	struct rocker_port **ports;
	struct {
		u64 id;
	} hw;
	spinlock_t cmd_ring_lock;		/* for cmd ring accesses */
	struct rocker_dma_ring_info cmd_ring;
	struct rocker_dma_ring_info event_ring;
	DECLARE_HASHTABLE(flow_tbl, 16);
	spinlock_t flow_tbl_lock;		/* for flow tbl accesses */
	u64 flow_tbl_next_cookie;
	DECLARE_HASHTABLE(group_tbl, 16);
	spinlock_t group_tbl_lock;		/* for group tbl accesses */
	struct timer_list fdb_cleanup_timer;
	DECLARE_HASHTABLE(fdb_tbl, 16);
	spinlock_t fdb_tbl_lock;		/* for fdb tbl accesses */
	unsigned long internal_vlan_bitmap[ROCKER_INTERNAL_VLAN_BITMAP_LEN];
	DECLARE_HASHTABLE(internal_vlan_tbl, 8);
	spinlock_t internal_vlan_tbl_lock;	/* for vlan tbl accesses */
	DECLARE_HASHTABLE(neigh_tbl, 16);
	spinlock_t neigh_tbl_lock;		/* for neigh tbl accesses */
	u32 neigh_tbl_next_index;
};

#endif
