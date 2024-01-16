/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later */
/*
 * Copyright 2008 - 2015 Freescale Semiconductor Inc.
 */

#ifndef __MEMAC_H
#define __MEMAC_H

#include "fman_mac.h"

#include <linux/netdevice.h>
#include <linux/phy_fixed.h>

struct mac_device;

int memac_initialization(struct mac_device *mac_dev,
			 struct device_node *mac_node,
			 struct fman_mac_params *params);

#endif /* __MEMAC_H */
