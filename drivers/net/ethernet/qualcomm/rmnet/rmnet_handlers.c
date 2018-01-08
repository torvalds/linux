/* Copyright (c) 2013-2017, The Linux Foundation. All rights reserved.
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
 * RMNET Data ingress/egress handler
 *
 */

#include <linux/netdevice.h>
#include <linux/netdev_features.h>
#include "rmnet_private.h"
#include "rmnet_config.h"
#include "rmnet_vnd.h"
#include "rmnet_map.h"
#include "rmnet_handlers.h"

#define RMNET_IP_VERSION_4 0x40
#define RMNET_IP_VERSION_6 0x60

/* Helper Functions */

static void rmnet_set_skb_proto(struct sk_buff *skb)
{
	switch (skb->data[0] & 0xF0) {
	case RMNET_IP_VERSION_4:
		skb->protocol = htons(ETH_P_IP);
		break;
	case RMNET_IP_VERSION_6:
		skb->protocol = htons(ETH_P_IPV6);
		break;
	default:
		skb->protocol = htons(ETH_P_MAP);
		break;
	}
}

/* Generic handler */

static void
rmnet_deliver_skb(struct sk_buff *skb)
{
	struct rmnet_priv *priv = netdev_priv(skb->dev);

	skb_reset_transport_header(skb);
	skb_reset_network_header(skb);
	rmnet_vnd_rx_fixup(skb, skb->dev);

	skb->pkt_type = PACKET_HOST;
	skb_set_mac_header(skb, 0);
	gro_cells_receive(&priv->gro_cells, skb);
}

/* MAP handler */

static void
__rmnet_map_ingress_handler(struct sk_buff *skb,
			    struct rmnet_port *port)
{
	struct rmnet_endpoint *ep;
	u8 mux_id;
	u16 len;

	if (RMNET_MAP_GET_CD_BIT(skb)) {
		if (port->ingress_data_format
		    & RMNET_INGRESS_FORMAT_MAP_COMMANDS)
			return rmnet_map_command(skb, port);

		goto free_skb;
	}

	mux_id = RMNET_MAP_GET_MUX_ID(skb);
	len = RMNET_MAP_GET_LENGTH(skb) - RMNET_MAP_GET_PAD(skb);

	if (mux_id >= RMNET_MAX_LOGICAL_EP)
		goto free_skb;

	ep = rmnet_get_endpoint(port, mux_id);
	if (!ep)
		goto free_skb;

	skb->dev = ep->egress_dev;

	/* Subtract MAP header */
	skb_pull(skb, sizeof(struct rmnet_map_header));
	skb_trim(skb, len);
	rmnet_set_skb_proto(skb);
	rmnet_deliver_skb(skb);
	return;

free_skb:
	kfree_skb(skb);
}

static void
rmnet_map_ingress_handler(struct sk_buff *skb,
			  struct rmnet_port *port)
{
	struct sk_buff *skbn;

	if (port->ingress_data_format & RMNET_INGRESS_FORMAT_DEAGGREGATION) {
		while ((skbn = rmnet_map_deaggregate(skb)) != NULL)
			__rmnet_map_ingress_handler(skbn, port);

		consume_skb(skb);
	} else {
		__rmnet_map_ingress_handler(skb, port);
	}
}

static int rmnet_map_egress_handler(struct sk_buff *skb,
				    struct rmnet_port *port, u8 mux_id,
				    struct net_device *orig_dev)
{
	int required_headroom, additional_header_len;
	struct rmnet_map_header *map_header;

	additional_header_len = 0;
	required_headroom = sizeof(struct rmnet_map_header);

	if (skb_headroom(skb) < required_headroom) {
		if (pskb_expand_head(skb, required_headroom, 0, GFP_KERNEL))
			return RMNET_MAP_CONSUMED;
	}

	map_header = rmnet_map_add_map_header(skb, additional_header_len, 0);
	if (!map_header)
		return RMNET_MAP_CONSUMED;

	if (port->egress_data_format & RMNET_EGRESS_FORMAT_MUXING) {
		if (mux_id == 0xff)
			map_header->mux_id = 0;
		else
			map_header->mux_id = mux_id;
	}

	skb->protocol = htons(ETH_P_MAP);

	return RMNET_MAP_SUCCESS;
}

static void
rmnet_bridge_handler(struct sk_buff *skb, struct net_device *bridge_dev)
{
	if (bridge_dev) {
		skb->dev = bridge_dev;
		dev_queue_xmit(skb);
	}
}

/* Ingress / Egress Entry Points */

/* Processes packet as per ingress data format for receiving device. Logical
 * endpoint is determined from packet inspection. Packet is then sent to the
 * egress device listed in the logical endpoint configuration.
 */
rx_handler_result_t rmnet_rx_handler(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct rmnet_port *port;
	struct net_device *dev;

	if (!skb)
		goto done;

	dev = skb->dev;
	port = rmnet_get_port(dev);

	switch (port->rmnet_mode) {
	case RMNET_EPMODE_VND:
		if (port->ingress_data_format & RMNET_INGRESS_FORMAT_MAP)
			rmnet_map_ingress_handler(skb, port);
		break;
	case RMNET_EPMODE_BRIDGE:
		rmnet_bridge_handler(skb, port->bridge_ep);
		break;
	}

done:
	return RX_HANDLER_CONSUMED;
}

/* Modifies packet as per logical endpoint configuration and egress data format
 * for egress device configured in logical endpoint. Packet is then transmitted
 * on the egress device.
 */
void rmnet_egress_handler(struct sk_buff *skb)
{
	struct net_device *orig_dev;
	struct rmnet_port *port;
	struct rmnet_priv *priv;
	u8 mux_id;

	orig_dev = skb->dev;
	priv = netdev_priv(orig_dev);
	skb->dev = priv->real_dev;
	mux_id = priv->mux_id;

	port = rmnet_get_port(skb->dev);
	if (!port) {
		kfree_skb(skb);
		return;
	}

	if (port->egress_data_format & RMNET_EGRESS_FORMAT_MAP) {
		switch (rmnet_map_egress_handler(skb, port, mux_id, orig_dev)) {
		case RMNET_MAP_CONSUMED:
			return;

		case RMNET_MAP_SUCCESS:
			break;

		default:
			kfree_skb(skb);
			return;
		}
	}

	rmnet_vnd_tx_fixup(skb, orig_dev);

	dev_queue_xmit(skb);
}
