/* vcan.c - Virtual CAN interface
 *
 * Copyright (c) 2002-2017 Volkswagen Group Electronic Research
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * The provided data structures and external interfaces from this code
 * are not restricted to be used by modules with a GPL compatible license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/can.h>
#include <linux/can/can-ml.h>
#include <linux/can/dev.h>
#include <linux/can/skb.h>
#include <linux/slab.h>
#include <net/rtnetlink.h>

#define DRV_NAME "vcan"

MODULE_DESCRIPTION("virtual CAN interface");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Urs Thuermann <urs.thuermann@volkswagen.de>");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);

/* CAN test feature:
 * Enable the echo on driver level for testing the CAN core echo modes.
 * See Documentation/networking/can.rst for details.
 */

static bool echo; /* echo testing. Default: 0 (Off) */
module_param(echo, bool, 0444);
MODULE_PARM_DESC(echo, "Echo sent frames (for testing). Default: 0 (Off)");

static void vcan_rx(struct sk_buff *skb, struct net_device *dev)
{
	struct canfd_frame *cfd = (struct canfd_frame *)skb->data;
	struct net_device_stats *stats = &dev->stats;

	stats->rx_packets++;
	stats->rx_bytes += cfd->len;

	skb->pkt_type  = PACKET_BROADCAST;
	skb->dev       = dev;
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	netif_rx_ni(skb);
}

static netdev_tx_t vcan_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct canfd_frame *cfd = (struct canfd_frame *)skb->data;
	struct net_device_stats *stats = &dev->stats;
	int loop, len;

	if (can_dropped_invalid_skb(dev, skb))
		return NETDEV_TX_OK;

	len = cfd->can_id & CAN_RTR_FLAG ? 0 : cfd->len;
	stats->tx_packets++;
	stats->tx_bytes += len;

	/* set flag whether this packet has to be looped back */
	loop = skb->pkt_type == PACKET_LOOPBACK;

	if (!echo) {
		/* no echo handling available inside this driver */
		if (loop) {
			/* only count the packets here, because the
			 * CAN core already did the echo for us
			 */
			stats->rx_packets++;
			stats->rx_bytes += len;
		}
		consume_skb(skb);
		return NETDEV_TX_OK;
	}

	/* perform standard echo handling for CAN network interfaces */

	if (loop) {
		skb = can_create_echo_skb(skb);
		if (!skb)
			return NETDEV_TX_OK;

		/* receive with packet counting */
		vcan_rx(skb, dev);
	} else {
		/* no looped packets => no counting */
		consume_skb(skb);
	}
	return NETDEV_TX_OK;
}

static int vcan_change_mtu(struct net_device *dev, int new_mtu)
{
	/* Do not allow changing the MTU while running */
	if (dev->flags & IFF_UP)
		return -EBUSY;

	if (new_mtu != CAN_MTU && new_mtu != CANFD_MTU)
		return -EINVAL;

	dev->mtu = new_mtu;
	return 0;
}

static const struct net_device_ops vcan_netdev_ops = {
	.ndo_start_xmit = vcan_tx,
	.ndo_change_mtu = vcan_change_mtu,
};

static void vcan_setup(struct net_device *dev)
{
	dev->type		= ARPHRD_CAN;
	dev->mtu		= CANFD_MTU;
	dev->hard_header_len	= 0;
	dev->addr_len		= 0;
	dev->tx_queue_len	= 0;
	dev->flags		= IFF_NOARP;
	can_set_ml_priv(dev, netdev_priv(dev));

	/* set flags according to driver capabilities */
	if (echo)
		dev->flags |= IFF_ECHO;

	dev->netdev_ops		= &vcan_netdev_ops;
	dev->needs_free_netdev	= true;
}

static struct rtnl_link_ops vcan_link_ops __read_mostly = {
	.kind = DRV_NAME,
	.priv_size = sizeof(struct can_ml_priv),
	.setup = vcan_setup,
};

static __init int vcan_init_module(void)
{
	pr_info("Virtual CAN interface driver\n");

	if (echo)
		pr_info("enabled echo on driver level.\n");

	return rtnl_link_register(&vcan_link_ops);
}

static __exit void vcan_cleanup_module(void)
{
	rtnl_link_unregister(&vcan_link_ops);
}

module_init(vcan_init_module);
module_exit(vcan_cleanup_module);
