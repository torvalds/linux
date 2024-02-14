/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later */
/*
 * Copyright 2008 - 2015 Freescale Semiconductor Inc.
 */

#ifndef __MAC_H
#define __MAC_H

#include <linux/device.h>
#include <linux/if_ether.h>
#include <linux/phy.h>
#include <linux/list.h>

#include "fman_port.h"
#include "fman.h"
#include "fman_mac.h"

struct fman_mac;
struct mac_priv_s;

struct mac_device {
	void __iomem		*vaddr;
	struct device		*dev;
	struct resource		*res;
	u8			 addr[ETH_ALEN];
	struct fman_port	*port[2];
	u32			 if_support;
	struct phy_device	*phy_dev;
	phy_interface_t		phy_if;
	struct device_node	*phy_node;
	struct net_device	*net_dev;

	bool autoneg_pause;
	bool rx_pause_req;
	bool tx_pause_req;
	bool rx_pause_active;
	bool tx_pause_active;
	bool promisc;
	bool allmulti;

	int (*enable)(struct fman_mac *mac_dev);
	void (*disable)(struct fman_mac *mac_dev);
	void (*adjust_link)(struct mac_device *mac_dev);
	int (*set_promisc)(struct fman_mac *mac_dev, bool enable);
	int (*change_addr)(struct fman_mac *mac_dev, const enet_addr_t *enet_addr);
	int (*set_allmulti)(struct fman_mac *mac_dev, bool enable);
	int (*set_tstamp)(struct fman_mac *mac_dev, bool enable);
	int (*set_multi)(struct net_device *net_dev,
			 struct mac_device *mac_dev);
	int (*set_rx_pause)(struct fman_mac *mac_dev, bool en);
	int (*set_tx_pause)(struct fman_mac *mac_dev, u8 priority,
			    u16 pause_time, u16 thresh_time);
	int (*set_exception)(struct fman_mac *mac_dev,
			     enum fman_mac_exceptions exception, bool enable);
	int (*add_hash_mac_addr)(struct fman_mac *mac_dev,
				 enet_addr_t *eth_addr);
	int (*remove_hash_mac_addr)(struct fman_mac *mac_dev,
				    enet_addr_t *eth_addr);

	void (*update_speed)(struct mac_device *mac_dev, int speed);

	struct fman_mac		*fman_mac;
	struct mac_priv_s	*priv;
};

struct dpaa_eth_data {
	struct mac_device *mac_dev;
	int mac_hw_id;
	int fman_hw_id;
};

extern const char	*mac_driver_description;

int fman_set_mac_active_pause(struct mac_device *mac_dev, bool rx, bool tx);

void fman_get_pause_cfg(struct mac_device *mac_dev, bool *rx_pause,
			bool *tx_pause);
int fman_set_multi(struct net_device *net_dev, struct mac_device *mac_dev);

#endif	/* __MAC_H */
