// SPDX-License-Identifier: GPL-2.0
/*
 * This file is based on code from OCTEON SDK by Cavium Networks.
 *
 * Copyright (c) 2003-2007 Cavium Networks
 */

#include <linux/phy.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/ratelimit.h>
#include <net/dst.h>

#include "octeon-ethernet.h"
#include "ethernet-defines.h"
#include "ethernet-util.h"
#include "ethernet-mdio.h"

int cvm_oct_sgmii_open(struct net_device *dev)
{
	return cvm_oct_common_open(dev, cvm_oct_link_poll);
}

int cvm_oct_sgmii_init(struct net_device *dev)
{
	cvm_oct_common_init(dev);

	/* FIXME: Need autoneg logic */
	return 0;
}
