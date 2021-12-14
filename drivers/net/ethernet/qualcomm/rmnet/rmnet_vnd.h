/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2013-2017, The Linux Foundation. All rights reserved.
 *
 * RMNET Data Virtual Network Device APIs
 */

#ifndef _RMNET_VND_H_
#define _RMNET_VND_H_

int rmnet_vnd_do_flow_control(struct net_device *dev, int enable);
int rmnet_vnd_newlink(u8 id, struct net_device *rmnet_dev,
		      struct rmnet_port *port,
		      struct net_device *real_dev,
		      struct rmnet_endpoint *ep,
		      struct netlink_ext_ack *extack);
int rmnet_vnd_dellink(u8 id, struct rmnet_port *port,
		      struct rmnet_endpoint *ep);
void rmnet_vnd_rx_fixup(struct sk_buff *skb, struct net_device *dev);
void rmnet_vnd_tx_fixup(struct sk_buff *skb, struct net_device *dev);
void rmnet_vnd_setup(struct net_device *dev);
int rmnet_vnd_validate_real_dev_mtu(struct net_device *real_dev);
int rmnet_vnd_update_dev_mtu(struct rmnet_port *port,
			     struct net_device *real_dev);
#endif /* _RMNET_VND_H_ */
