/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010 ASIX Electronics Corporation
 * Copyright (c) 2020 Samsung Electronics Co., Ltd.
 *
 * ASIX AX88796C SPI Fast Ethernet Linux driver
 */

#ifndef _AX88796C_IOCTL_H
#define _AX88796C_IOCTL_H

#include <linux/ethtool.h>
#include <linux/netdevice.h>

#include "ax88796c_main.h"

extern const struct ethtool_ops ax88796c_ethtool_ops;

bool ax88796c_check_power(const struct ax88796c_device *ax_local);
bool ax88796c_check_power_and_wake(struct ax88796c_device *ax_local);
void ax88796c_set_power_saving(struct ax88796c_device *ax_local, u8 ps_level);
int ax88796c_mdio_read(struct mii_bus *mdiobus, int phy_id, int loc);
int ax88796c_mdio_write(struct mii_bus *mdiobus, int phy_id, int loc, u16 val);
int ax88796c_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);

#endif
