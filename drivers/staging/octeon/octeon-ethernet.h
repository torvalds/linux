/*
 * This file is based on code from OCTEON SDK by Cavium Networks.
 *
 * Copyright (c) 2003-2010 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

/*
 * External interface for the Cavium Octeon ethernet driver.
 */
#ifndef OCTEON_ETHERNET_H
#define OCTEON_ETHERNET_H

#include <linux/of.h>

#include <asm/octeon/cvmx-helper-board.h>

/**
 * This is the definition of the Ethernet driver's private
 * driver state stored in netdev_priv(dev).
 */
struct octeon_ethernet {
	/* PKO hardware output port */
	int port;
	/* PKO hardware queue for the port */
	int queue;
	/* Hardware fetch and add to count outstanding tx buffers */
	int fau;
	/* My netdev. */
	struct net_device *netdev;
	/*
	 * Type of port. This is one of the enums in
	 * cvmx_helper_interface_mode_t
	 */
	int imode;
	/* List of outstanding tx buffers per queue */
	struct sk_buff_head tx_free_list[16];
	/* Device statistics */
	struct net_device_stats stats;
	struct phy_device *phydev;
	unsigned int last_link;
	/* Last negotiated link state */
	uint64_t link_info;
	/* Called periodically to check link status */
	void (*poll)(struct net_device *dev);
	struct delayed_work	port_periodic_work;
	struct work_struct	port_work;	/* may be unused. */
	struct device_node	*of_node;
};

int cvm_oct_free_work(void *work_queue_entry);

extern int cvm_oct_rgmii_init(struct net_device *dev);
extern void cvm_oct_rgmii_uninit(struct net_device *dev);
extern int cvm_oct_rgmii_open(struct net_device *dev);

extern int cvm_oct_sgmii_init(struct net_device *dev);
extern int cvm_oct_sgmii_open(struct net_device *dev);

extern int cvm_oct_spi_init(struct net_device *dev);
extern void cvm_oct_spi_uninit(struct net_device *dev);
extern int cvm_oct_xaui_init(struct net_device *dev);
extern int cvm_oct_xaui_open(struct net_device *dev);

extern int cvm_oct_common_init(struct net_device *dev);
extern void cvm_oct_common_uninit(struct net_device *dev);
void cvm_oct_adjust_link(struct net_device *dev);
int cvm_oct_common_stop(struct net_device *dev);
int cvm_oct_common_open(struct net_device *dev,
			void (*link_poll)(struct net_device *), bool poll_now);
void cvm_oct_note_carrier(struct octeon_ethernet *priv,
			  cvmx_helper_link_info_t li);
void cvm_oct_link_poll(struct net_device *dev);

extern int always_use_pow;
extern int pow_send_group;
extern int pow_receive_group;
extern char pow_send_list[];
extern struct net_device *cvm_oct_device[];
extern struct workqueue_struct *cvm_oct_poll_queue;
extern atomic_t cvm_oct_poll_queue_stopping;
extern u64 cvm_oct_tx_poll_interval;

extern int rx_napi_weight;

#endif
