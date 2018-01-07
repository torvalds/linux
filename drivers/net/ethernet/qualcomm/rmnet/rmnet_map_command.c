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
 */

#include <linux/netdevice.h>
#include "rmnet_config.h"
#include "rmnet_map.h"
#include "rmnet_private.h"
#include "rmnet_vnd.h"

static u8 rmnet_map_do_flow_control(struct sk_buff *skb,
				    struct rmnet_port *port,
				    int enable)
{
	struct rmnet_map_control_command *cmd;
	struct rmnet_endpoint *ep;
	struct net_device *vnd;
	u16 ip_family;
	u16 fc_seq;
	u32 qos_id;
	u8 mux_id;
	int r;

	mux_id = RMNET_MAP_GET_MUX_ID(skb);
	cmd = RMNET_MAP_GET_CMD_START(skb);

	if (mux_id >= RMNET_MAX_LOGICAL_EP) {
		kfree_skb(skb);
		return RX_HANDLER_CONSUMED;
	}

	ep = rmnet_get_endpoint(port, mux_id);
	vnd = ep->egress_dev;

	ip_family = cmd->flow_control.ip_family;
	fc_seq = ntohs(cmd->flow_control.flow_control_seq_num);
	qos_id = ntohl(cmd->flow_control.qos_id);

	/* Ignore the ip family and pass the sequence number for both v4 and v6
	 * sequence. User space does not support creating dedicated flows for
	 * the 2 protocols
	 */
	r = rmnet_vnd_do_flow_control(vnd, enable);
	if (r) {
		kfree_skb(skb);
		return RMNET_MAP_COMMAND_UNSUPPORTED;
	} else {
		return RMNET_MAP_COMMAND_ACK;
	}
}

static void rmnet_map_send_ack(struct sk_buff *skb,
			       unsigned char type,
			       struct rmnet_port *port)
{
	struct rmnet_map_control_command *cmd;
	int xmit_status;

	if (port->data_format & RMNET_INGRESS_FORMAT_MAP_CKSUMV4) {
		if (skb->len < sizeof(struct rmnet_map_header) +
		    RMNET_MAP_GET_LENGTH(skb) +
		    sizeof(struct rmnet_map_dl_csum_trailer)) {
			kfree_skb(skb);
			return;
		}

		skb_trim(skb, skb->len -
			 sizeof(struct rmnet_map_dl_csum_trailer));
	}

	skb->protocol = htons(ETH_P_MAP);

	cmd = RMNET_MAP_GET_CMD_START(skb);
	cmd->cmd_type = type & 0x03;

	netif_tx_lock(skb->dev);
	xmit_status = skb->dev->netdev_ops->ndo_start_xmit(skb, skb->dev);
	netif_tx_unlock(skb->dev);
}

/* Process MAP command frame and send N/ACK message as appropriate. Message cmd
 * name is decoded here and appropriate handler is called.
 */
void rmnet_map_command(struct sk_buff *skb, struct rmnet_port *port)
{
	struct rmnet_map_control_command *cmd;
	unsigned char command_name;
	unsigned char rc = 0;

	cmd = RMNET_MAP_GET_CMD_START(skb);
	command_name = cmd->command_name;

	switch (command_name) {
	case RMNET_MAP_COMMAND_FLOW_ENABLE:
		rc = rmnet_map_do_flow_control(skb, port, 1);
		break;

	case RMNET_MAP_COMMAND_FLOW_DISABLE:
		rc = rmnet_map_do_flow_control(skb, port, 0);
		break;

	default:
		rc = RMNET_MAP_COMMAND_UNSUPPORTED;
		kfree_skb(skb);
		break;
	}
	if (rc == RMNET_MAP_COMMAND_ACK)
		rmnet_map_send_ack(skb, rc, port);
}
