/*
 * This file is based on code from OCTEON SDK by Cavium Networks.
 *
 * Copyright (c) 2003-2007 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/phy.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/ratelimit.h>
#include <net/dst.h>

#include <asm/octeon/octeon.h>

#include "ethernet-defines.h"
#include "octeon-ethernet.h"
#include "ethernet-util.h"
#include "ethernet-mdio.h"

#include <asm/octeon/cvmx-helper.h>

#include <asm/octeon/cvmx-gmxx-defs.h>

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
