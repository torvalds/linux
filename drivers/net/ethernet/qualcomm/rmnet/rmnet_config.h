/* Copyright (c) 2013-2014, 2016-2017 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * RMNET Data configuration engine
 *
 */

#include <linux/skbuff.h>

#ifndef _RMNET_CONFIG_H_
#define _RMNET_CONFIG_H_

#define RMNET_MAX_LOGICAL_EP 255

/* Information about the next device to deliver the packet to.
 * Exact usage of this parameter depends on the rmnet_mode.
 */
struct rmnet_endpoint {
	u8 rmnet_mode;
	u8 mux_id;
	struct net_device *egress_dev;
};

/* One instance of this structure is instantiated for each real_dev associated
 * with rmnet.
 */
struct rmnet_port {
	struct net_device *dev;
	struct rmnet_endpoint local_ep;
	struct rmnet_endpoint muxed_ep[RMNET_MAX_LOGICAL_EP];
	u32 ingress_data_format;
	u32 egress_data_format;
	struct net_device *rmnet_devices[RMNET_MAX_LOGICAL_EP];
	u8 nr_rmnet_devs;
};

extern struct rtnl_link_ops rmnet_link_ops;

struct rmnet_priv {
	struct rmnet_endpoint local_ep;
	u8 mux_id;
	struct net_device *real_dev;
};

struct rmnet_port *rmnet_get_port(struct net_device *real_dev);

#endif /* _RMNET_CONFIG_H_ */
