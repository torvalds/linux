/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is based on code from OCTEON SDK by Cavium Networks.
 *
 * Copyright (c) 2003-2010 Cavium Networks
 */

/*
 * External interface for the Cavium Octeon ethernet driver.
 */
#ifndef OCTEON_ETHERNET_H
#define OCTEON_ETHERNET_H

#include <linux/of.h>
#include <linux/phy.h>

#ifdef CONFIG_CAVIUM_OCTEON_SOC

#include <asm/octeon/octeon.h>

#include <asm/octeon/cvmx-asxx-defs.h>
#include <asm/octeon/cvmx-config.h>
#include <asm/octeon/cvmx-fau.h>
#include <asm/octeon/cvmx-gmxx-defs.h>
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-util.h>
#include <asm/octeon/cvmx-ipd.h>
#include <asm/octeon/cvmx-ipd-defs.h>
#include <asm/octeon/cvmx-npi-defs.h>
#include <asm/octeon/cvmx-pip.h>
#include <asm/octeon/cvmx-pko.h>
#include <asm/octeon/cvmx-pow.h>
#include <asm/octeon/cvmx-scratch.h>
#include <asm/octeon/cvmx-spi.h>
#include <asm/octeon/cvmx-spxx-defs.h>
#include <asm/octeon/cvmx-stxx-defs.h>
#include <asm/octeon/cvmx-wqe.h>

#else

#include "octeon-stubs.h"

#endif

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
	/* PHY mode */
	phy_interface_t phy_mode;
	/* List of outstanding tx buffers per queue */
	struct sk_buff_head tx_free_list[16];
	unsigned int last_speed;
	unsigned int last_link;
	/* Last negotiated link state */
	u64 link_info;
	/* Called periodically to check link status */
	void (*poll)(struct net_device *dev);
	struct delayed_work	port_periodic_work;
	struct device_node	*of_node;
};

int cvm_oct_free_work(void *work_queue_entry);

int cvm_oct_rgmii_open(struct net_device *dev);

int cvm_oct_sgmii_init(struct net_device *dev);
int cvm_oct_sgmii_open(struct net_device *dev);

int cvm_oct_spi_init(struct net_device *dev);
void cvm_oct_spi_uninit(struct net_device *dev);

int cvm_oct_common_init(struct net_device *dev);
void cvm_oct_common_uninit(struct net_device *dev);
void cvm_oct_adjust_link(struct net_device *dev);
int cvm_oct_common_stop(struct net_device *dev);
int cvm_oct_common_open(struct net_device *dev,
			void (*link_poll)(struct net_device *));
void cvm_oct_note_carrier(struct octeon_ethernet *priv,
			  union cvmx_helper_link_info li);
void cvm_oct_link_poll(struct net_device *dev);

extern int always_use_pow;
extern int pow_send_group;
extern int pow_receive_groups;
extern char pow_send_list[];
extern struct net_device *cvm_oct_device[];
extern atomic_t cvm_oct_poll_queue_stopping;
extern u64 cvm_oct_tx_poll_interval;

extern int rx_napi_weight;

#endif
