// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2005 Marc Kleine-Budde, Pengutronix
 * Copyright (C) 2006 Andrey Volkov, Varma Electronics
 * Copyright (C) 2008-2009 Wolfgang Grandegger <wg@grandegger.com>
 */

#include <linux/can/dev.h>
#include <linux/can/netlink.h>
#include <linux/module.h>

#define MOD_DESC "CAN device driver interface"

MODULE_DESCRIPTION(MOD_DESC);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wolfgang Grandegger <wg@grandegger.com>");

/* Local echo of CAN messages
 *
 * CAN network devices *should* support a local echo functionality
 * (see Documentation/networking/can.rst). To test the handling of CAN
 * interfaces that do not support the local echo both driver types are
 * implemented. In the case that the driver does not support the echo
 * the IFF_ECHO remains clear in dev->flags. This causes the PF_CAN core
 * to perform the echo as a fallback solution.
 */
void can_flush_echo_skb(struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	int i;

	for (i = 0; i < priv->echo_skb_max; i++) {
		if (priv->echo_skb[i]) {
			kfree_skb(priv->echo_skb[i]);
			priv->echo_skb[i] = NULL;
			stats->tx_dropped++;
			stats->tx_aborted_errors++;
		}
	}
}

/* Put the skb on the stack to be looped backed locally lateron
 *
 * The function is typically called in the start_xmit function
 * of the device driver. The driver must protect access to
 * priv->echo_skb, if necessary.
 */
int can_put_echo_skb(struct sk_buff *skb, struct net_device *dev,
		     unsigned int idx, unsigned int frame_len)
{
	struct can_priv *priv = netdev_priv(dev);

	BUG_ON(idx >= priv->echo_skb_max);

	/* check flag whether this packet has to be looped back */
	if (!(dev->flags & IFF_ECHO) ||
	    (skb->protocol != htons(ETH_P_CAN) &&
	     skb->protocol != htons(ETH_P_CANFD))) {
		kfree_skb(skb);
		return 0;
	}

	if (!priv->echo_skb[idx]) {
		skb = can_create_echo_skb(skb);
		if (!skb)
			return -ENOMEM;

		/* make settings for echo to reduce code in irq context */
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->dev = dev;

		/* save frame_len to reuse it when transmission is completed */
		can_skb_prv(skb)->frame_len = frame_len;

		if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

		skb_tx_timestamp(skb);

		/* save this skb for tx interrupt echo handling */
		priv->echo_skb[idx] = skb;
	} else {
		/* locking problem with netif_stop_queue() ?? */
		netdev_err(dev, "%s: BUG! echo_skb %d is occupied!\n", __func__, idx);
		kfree_skb(skb);
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(can_put_echo_skb);

struct sk_buff *
__can_get_echo_skb(struct net_device *dev, unsigned int idx, u8 *len_ptr,
		   unsigned int *frame_len_ptr)
{
	struct can_priv *priv = netdev_priv(dev);

	if (idx >= priv->echo_skb_max) {
		netdev_err(dev, "%s: BUG! Trying to access can_priv::echo_skb out of bounds (%u/max %u)\n",
			   __func__, idx, priv->echo_skb_max);
		return NULL;
	}

	if (priv->echo_skb[idx]) {
		/* Using "struct canfd_frame::len" for the frame
		 * length is supported on both CAN and CANFD frames.
		 */
		struct sk_buff *skb = priv->echo_skb[idx];
		struct can_skb_priv *can_skb_priv = can_skb_prv(skb);
		struct canfd_frame *cf = (struct canfd_frame *)skb->data;

		if (skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS)
			skb_tstamp_tx(skb, skb_hwtstamps(skb));

		/* get the real payload length for netdev statistics */
		if (cf->can_id & CAN_RTR_FLAG)
			*len_ptr = 0;
		else
			*len_ptr = cf->len;

		if (frame_len_ptr)
			*frame_len_ptr = can_skb_priv->frame_len;

		priv->echo_skb[idx] = NULL;

		if (skb->pkt_type == PACKET_LOOPBACK) {
			skb->pkt_type = PACKET_BROADCAST;
		} else {
			dev_consume_skb_any(skb);
			return NULL;
		}

		return skb;
	}

	return NULL;
}

/* Get the skb from the stack and loop it back locally
 *
 * The function is typically called when the TX done interrupt
 * is handled in the device driver. The driver must protect
 * access to priv->echo_skb, if necessary.
 */
unsigned int can_get_echo_skb(struct net_device *dev, unsigned int idx,
			      unsigned int *frame_len_ptr)
{
	struct sk_buff *skb;
	u8 len;

	skb = __can_get_echo_skb(dev, idx, &len, frame_len_ptr);
	if (!skb)
		return 0;

	skb_get(skb);
	if (netif_rx(skb) == NET_RX_SUCCESS)
		dev_consume_skb_any(skb);
	else
		dev_kfree_skb_any(skb);

	return len;
}
EXPORT_SYMBOL_GPL(can_get_echo_skb);

/* Remove the skb from the stack and free it.
 *
 * The function is typically called when TX failed.
 */
void can_free_echo_skb(struct net_device *dev, unsigned int idx,
		       unsigned int *frame_len_ptr)
{
	struct can_priv *priv = netdev_priv(dev);

	if (idx >= priv->echo_skb_max) {
		netdev_err(dev, "%s: BUG! Trying to access can_priv::echo_skb out of bounds (%u/max %u)\n",
			   __func__, idx, priv->echo_skb_max);
		return;
	}

	if (priv->echo_skb[idx]) {
		struct sk_buff *skb = priv->echo_skb[idx];
		struct can_skb_priv *can_skb_priv = can_skb_prv(skb);

		if (frame_len_ptr)
			*frame_len_ptr = can_skb_priv->frame_len;

		dev_kfree_skb_any(skb);
		priv->echo_skb[idx] = NULL;
	}
}
EXPORT_SYMBOL_GPL(can_free_echo_skb);

struct sk_buff *alloc_can_skb(struct net_device *dev, struct can_frame **cf)
{
	struct sk_buff *skb;

	skb = netdev_alloc_skb(dev, sizeof(struct can_skb_priv) +
			       sizeof(struct can_frame));
	if (unlikely(!skb)) {
		*cf = NULL;

		return NULL;
	}

	skb->protocol = htons(ETH_P_CAN);
	skb->pkt_type = PACKET_BROADCAST;
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);

	can_skb_reserve(skb);
	can_skb_prv(skb)->ifindex = dev->ifindex;
	can_skb_prv(skb)->skbcnt = 0;

	*cf = skb_put_zero(skb, sizeof(struct can_frame));

	return skb;
}
EXPORT_SYMBOL_GPL(alloc_can_skb);

struct sk_buff *alloc_canfd_skb(struct net_device *dev,
				struct canfd_frame **cfd)
{
	struct sk_buff *skb;

	skb = netdev_alloc_skb(dev, sizeof(struct can_skb_priv) +
			       sizeof(struct canfd_frame));
	if (unlikely(!skb)) {
		*cfd = NULL;

		return NULL;
	}

	skb->protocol = htons(ETH_P_CANFD);
	skb->pkt_type = PACKET_BROADCAST;
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);

	can_skb_reserve(skb);
	can_skb_prv(skb)->ifindex = dev->ifindex;
	can_skb_prv(skb)->skbcnt = 0;

	*cfd = skb_put_zero(skb, sizeof(struct canfd_frame));

	return skb;
}
EXPORT_SYMBOL_GPL(alloc_canfd_skb);

struct sk_buff *alloc_can_err_skb(struct net_device *dev, struct can_frame **cf)
{
	struct sk_buff *skb;

	skb = alloc_can_skb(dev, cf);
	if (unlikely(!skb))
		return NULL;

	(*cf)->can_id = CAN_ERR_FLAG;
	(*cf)->len = CAN_ERR_DLC;

	return skb;
}
EXPORT_SYMBOL_GPL(alloc_can_err_skb);

/* Check for outgoing skbs that have not been created by the CAN subsystem */
static bool can_skb_headroom_valid(struct net_device *dev, struct sk_buff *skb)
{
	/* af_packet creates a headroom of HH_DATA_MOD bytes which is fine */
	if (WARN_ON_ONCE(skb_headroom(skb) < sizeof(struct can_skb_priv)))
		return false;

	/* af_packet does not apply CAN skb specific settings */
	if (skb->ip_summed == CHECKSUM_NONE) {
		/* init headroom */
		can_skb_prv(skb)->ifindex = dev->ifindex;
		can_skb_prv(skb)->skbcnt = 0;

		skb->ip_summed = CHECKSUM_UNNECESSARY;

		/* perform proper loopback on capable devices */
		if (dev->flags & IFF_ECHO)
			skb->pkt_type = PACKET_LOOPBACK;
		else
			skb->pkt_type = PACKET_HOST;

		skb_reset_mac_header(skb);
		skb_reset_network_header(skb);
		skb_reset_transport_header(skb);
	}

	return true;
}

/* Drop a given socketbuffer if it does not contain a valid CAN frame. */
bool can_dropped_invalid_skb(struct net_device *dev, struct sk_buff *skb)
{
	const struct canfd_frame *cfd = (struct canfd_frame *)skb->data;
	struct can_priv *priv = netdev_priv(dev);

	if (skb->protocol == htons(ETH_P_CAN)) {
		if (unlikely(skb->len != CAN_MTU ||
			     cfd->len > CAN_MAX_DLEN))
			goto inval_skb;
	} else if (skb->protocol == htons(ETH_P_CANFD)) {
		if (unlikely(skb->len != CANFD_MTU ||
			     cfd->len > CANFD_MAX_DLEN))
			goto inval_skb;
	} else {
		goto inval_skb;
	}

	if (!can_skb_headroom_valid(dev, skb)) {
		goto inval_skb;
	} else if (priv->ctrlmode & CAN_CTRLMODE_LISTENONLY) {
		netdev_info_once(dev,
				 "interface in listen only mode, dropping skb\n");
		goto inval_skb;
	}

	return false;

inval_skb:
	kfree_skb(skb);
	dev->stats.tx_dropped++;
	return true;
}
EXPORT_SYMBOL_GPL(can_dropped_invalid_skb);
