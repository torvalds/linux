/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 * Copyright 2019-2020 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/netdevice.h>
#include "ef100_rep.h"

netdev_tx_t __ef100_hard_start_xmit(struct sk_buff *skb,
				    struct efx_nic *efx,
				    struct net_device *net_dev,
				    struct efx_rep *efv);
int ef100_netdev_event(struct notifier_block *this,
		       unsigned long event, void *ptr);
int ef100_probe_netdev(struct efx_probe_data *probe_data);
void ef100_remove_netdev(struct efx_probe_data *probe_data);
