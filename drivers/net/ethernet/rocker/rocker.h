/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * drivers/net/ethernet/rocker/rocker.h - Rocker switch device driver
 * Copyright (c) 2014-2016 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2014 Scott Feldman <sfeldma@gmail.com>
 */

#ifndef _ROCKER_H
#define _ROCKER_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <net/neighbour.h>
#include <net/switchdev.h>

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

struct rocker_port {
	struct net_device *dev;
	struct rocker *rocker;
	void *wpriv;
	unsigned int port_number;
	u32 pport;
	struct napi_struct napi_tx;
	struct napi_struct napi_rx;
	struct rocker_dma_ring_info tx_ring;
	struct rocker_dma_ring_info rx_ring;
};

struct rocker_port *rocker_port_dev_lower_find(struct net_device *dev,
					       struct rocker *rocker);

struct rocker_world_ops;

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
	struct notifier_block fib_nb;
	struct rocker_world_ops *wops;
	struct workqueue_struct *rocker_owq;
	void *wpriv;
};

typedef int (*rocker_cmd_prep_cb_t)(const struct rocker_port *rocker_port,
				    struct rocker_desc_info *desc_info,
				    void *priv);

typedef int (*rocker_cmd_proc_cb_t)(const struct rocker_port *rocker_port,
				    const struct rocker_desc_info *desc_info,
				    void *priv);

int rocker_cmd_exec(struct rocker_port *rocker_port, bool nowait,
		    rocker_cmd_prep_cb_t prepare, void *prepare_priv,
		    rocker_cmd_proc_cb_t process, void *process_priv);

int rocker_port_set_learning(struct rocker_port *rocker_port,
			     bool learning);

struct rocker_world_ops {
	const char *kind;
	size_t priv_size;
	size_t port_priv_size;
	u8 mode;
	int (*init)(struct rocker *rocker);
	void (*fini)(struct rocker *rocker);
	int (*port_pre_init)(struct rocker_port *rocker_port);
	int (*port_init)(struct rocker_port *rocker_port);
	void (*port_fini)(struct rocker_port *rocker_port);
	void (*port_post_fini)(struct rocker_port *rocker_port);
	int (*port_open)(struct rocker_port *rocker_port);
	void (*port_stop)(struct rocker_port *rocker_port);
	int (*port_attr_stp_state_set)(struct rocker_port *rocker_port,
				       u8 state);
	int (*port_attr_bridge_flags_set)(struct rocker_port *rocker_port,
					  unsigned long brport_flags);
	int (*port_attr_bridge_flags_support_get)(const struct rocker_port *
						  rocker_port,
						  unsigned long *
						  p_brport_flags);
	int (*port_attr_bridge_ageing_time_set)(struct rocker_port *rocker_port,
						u32 ageing_time);
	int (*port_obj_vlan_add)(struct rocker_port *rocker_port,
				 const struct switchdev_obj_port_vlan *vlan);
	int (*port_obj_vlan_del)(struct rocker_port *rocker_port,
				 const struct switchdev_obj_port_vlan *vlan);
	int (*port_obj_fdb_add)(struct rocker_port *rocker_port,
				u16 vid, const unsigned char *addr);
	int (*port_obj_fdb_del)(struct rocker_port *rocker_port,
				u16 vid, const unsigned char *addr);
	int (*port_master_linked)(struct rocker_port *rocker_port,
				  struct net_device *master);
	int (*port_master_unlinked)(struct rocker_port *rocker_port,
				    struct net_device *master);
	int (*port_neigh_update)(struct rocker_port *rocker_port,
				 struct neighbour *n);
	int (*port_neigh_destroy)(struct rocker_port *rocker_port,
				  struct neighbour *n);
	int (*port_ev_mac_vlan_seen)(struct rocker_port *rocker_port,
				     const unsigned char *addr,
				     __be16 vlan_id);
	int (*fib4_add)(struct rocker *rocker,
			const struct fib_entry_notifier_info *fen_info);
	int (*fib4_del)(struct rocker *rocker,
			const struct fib_entry_notifier_info *fen_info);
	void (*fib4_abort)(struct rocker *rocker);
};

extern struct rocker_world_ops rocker_ofdpa_ops;

#endif
