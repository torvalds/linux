/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later */
/*
 * Copyright 2008 - 2015 Freescale Semiconductor Inc.
 */

#ifndef __MAC_H
#define __MAC_H

#include <linux/device.h>
#include <linux/if_ether.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/list.h>

#include "fman_port.h"
#include "fman.h"
#include "fman_mac.h"

struct fman_mac;
struct mac_priv_s;

#define PORT_NUM 2
struct mac_device {
	void __iomem		*vaddr;
	struct device		*dev;
	struct resource		*res;
	u8			 addr[ETH_ALEN];
	struct fman_port	*port[PORT_NUM];
	struct phylink		*phylink;
	struct phylink_config	phylink_config;
	phy_interface_t		phy_if;

	bool promisc;
	bool allmulti;

	const struct phylink_mac_ops *phylink_ops;
	int (*enable)(struct fman_mac *mac_dev);
	void (*disable)(struct fman_mac *mac_dev);
	int (*set_promisc)(struct fman_mac *mac_dev, bool enable);
	int (*change_addr)(struct fman_mac *mac_dev, const enet_addr_t *enet_addr);
	int (*set_allmulti)(struct fman_mac *mac_dev, bool enable);
	int (*set_tstamp)(struct fman_mac *mac_dev, bool enable);
	int (*set_multi)(struct net_device *net_dev,
			 struct mac_device *mac_dev);
	int (*set_exception)(struct fman_mac *mac_dev,
			     enum fman_mac_exceptions exception, bool enable);
	int (*add_hash_mac_addr)(struct fman_mac *mac_dev,
				 enet_addr_t *eth_addr);
	int (*remove_hash_mac_addr)(struct fman_mac *mac_dev,
				    enet_addr_t *eth_addr);

	void (*update_speed)(struct mac_device *mac_dev, int speed);

	struct fman_mac		*fman_mac;
	struct mac_priv_s	*priv;

	struct device		*fman_dev;
	struct device		*fman_port_devs[PORT_NUM];
};

static inline struct mac_device
*fman_config_to_mac(struct phylink_config *config)
{
	return container_of(config, struct mac_device, phylink_config);
}

struct dpaa_eth_data {
	struct mac_device *mac_dev;
	int mac_hw_id;
	int fman_hw_id;
};

extern const char	*mac_driver_description;

int fman_set_multi(struct net_device *net_dev, struct mac_device *mac_dev);

#endif	/* __MAC_H */
