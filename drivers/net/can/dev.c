// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2005 Marc Kleine-Budde, Pengutronix
 * Copyright (C) 2006 Andrey Volkov, Varma Electronics
 * Copyright (C) 2008-2009 Wolfgang Grandegger <wg@grandegger.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/workqueue.h>
#include <linux/can.h>
#include <linux/can/can-ml.h>
#include <linux/can/dev.h>
#include <linux/can/skb.h>
#include <linux/can/netlink.h>
#include <linux/can/led.h>
#include <linux/of.h>
#include <net/rtnetlink.h>

#define MOD_DESC "CAN device driver interface"

MODULE_DESCRIPTION(MOD_DESC);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wolfgang Grandegger <wg@grandegger.com>");

/* CAN DLC to real data length conversion helpers */

static const u8 dlc2len[] = {0, 1, 2, 3, 4, 5, 6, 7,
			     8, 12, 16, 20, 24, 32, 48, 64};

/* get data length from raw data length code (DLC) */
u8 can_fd_dlc2len(u8 dlc)
{
	return dlc2len[dlc & 0x0F];
}
EXPORT_SYMBOL_GPL(can_fd_dlc2len);

static const u8 len2dlc[] = {0, 1, 2, 3, 4, 5, 6, 7, 8,		/* 0 - 8 */
			     9, 9, 9, 9,			/* 9 - 12 */
			     10, 10, 10, 10,			/* 13 - 16 */
			     11, 11, 11, 11,			/* 17 - 20 */
			     12, 12, 12, 12,			/* 21 - 24 */
			     13, 13, 13, 13, 13, 13, 13, 13,	/* 25 - 32 */
			     14, 14, 14, 14, 14, 14, 14, 14,	/* 33 - 40 */
			     14, 14, 14, 14, 14, 14, 14, 14,	/* 41 - 48 */
			     15, 15, 15, 15, 15, 15, 15, 15,	/* 49 - 56 */
			     15, 15, 15, 15, 15, 15, 15, 15};	/* 57 - 64 */

/* map the sanitized data length to an appropriate data length code */
u8 can_fd_len2dlc(u8 len)
{
	if (unlikely(len > 64))
		return 0xF;

	return len2dlc[len];
}
EXPORT_SYMBOL_GPL(can_fd_len2dlc);

#ifdef CONFIG_CAN_CALC_BITTIMING
#define CAN_CALC_MAX_ERROR 50 /* in one-tenth of a percent */

/* Bit-timing calculation derived from:
 *
 * Code based on LinCAN sources and H8S2638 project
 * Copyright 2004-2006 Pavel Pisa - DCE FELK CVUT cz
 * Copyright 2005      Stanislav Marek
 * email: pisa@cmp.felk.cvut.cz
 *
 * Calculates proper bit-timing parameters for a specified bit-rate
 * and sample-point, which can then be used to set the bit-timing
 * registers of the CAN controller. You can find more information
 * in the header file linux/can/netlink.h.
 */
static int
can_update_sample_point(const struct can_bittiming_const *btc,
			unsigned int sample_point_nominal, unsigned int tseg,
			unsigned int *tseg1_ptr, unsigned int *tseg2_ptr,
			unsigned int *sample_point_error_ptr)
{
	unsigned int sample_point_error, best_sample_point_error = UINT_MAX;
	unsigned int sample_point, best_sample_point = 0;
	unsigned int tseg1, tseg2;
	int i;

	for (i = 0; i <= 1; i++) {
		tseg2 = tseg + CAN_SYNC_SEG -
			(sample_point_nominal * (tseg + CAN_SYNC_SEG)) /
			1000 - i;
		tseg2 = clamp(tseg2, btc->tseg2_min, btc->tseg2_max);
		tseg1 = tseg - tseg2;
		if (tseg1 > btc->tseg1_max) {
			tseg1 = btc->tseg1_max;
			tseg2 = tseg - tseg1;
		}

		sample_point = 1000 * (tseg + CAN_SYNC_SEG - tseg2) /
			(tseg + CAN_SYNC_SEG);
		sample_point_error = abs(sample_point_nominal - sample_point);

		if (sample_point <= sample_point_nominal &&
		    sample_point_error < best_sample_point_error) {
			best_sample_point = sample_point;
			best_sample_point_error = sample_point_error;
			*tseg1_ptr = tseg1;
			*tseg2_ptr = tseg2;
		}
	}

	if (sample_point_error_ptr)
		*sample_point_error_ptr = best_sample_point_error;

	return best_sample_point;
}

static int can_calc_bittiming(struct net_device *dev, struct can_bittiming *bt,
			      const struct can_bittiming_const *btc)
{
	struct can_priv *priv = netdev_priv(dev);
	unsigned int bitrate;			/* current bitrate */
	unsigned int bitrate_error;		/* difference between current and nominal value */
	unsigned int best_bitrate_error = UINT_MAX;
	unsigned int sample_point_error;	/* difference between current and nominal value */
	unsigned int best_sample_point_error = UINT_MAX;
	unsigned int sample_point_nominal;	/* nominal sample point */
	unsigned int best_tseg = 0;		/* current best value for tseg */
	unsigned int best_brp = 0;		/* current best value for brp */
	unsigned int brp, tsegall, tseg, tseg1 = 0, tseg2 = 0;
	u64 v64;

	/* Use CiA recommended sample points */
	if (bt->sample_point) {
		sample_point_nominal = bt->sample_point;
	} else {
		if (bt->bitrate > 800000)
			sample_point_nominal = 750;
		else if (bt->bitrate > 500000)
			sample_point_nominal = 800;
		else
			sample_point_nominal = 875;
	}

	/* tseg even = round down, odd = round up */
	for (tseg = (btc->tseg1_max + btc->tseg2_max) * 2 + 1;
	     tseg >= (btc->tseg1_min + btc->tseg2_min) * 2; tseg--) {
		tsegall = CAN_SYNC_SEG + tseg / 2;

		/* Compute all possible tseg choices (tseg=tseg1+tseg2) */
		brp = priv->clock.freq / (tsegall * bt->bitrate) + tseg % 2;

		/* choose brp step which is possible in system */
		brp = (brp / btc->brp_inc) * btc->brp_inc;
		if (brp < btc->brp_min || brp > btc->brp_max)
			continue;

		bitrate = priv->clock.freq / (brp * tsegall);
		bitrate_error = abs(bt->bitrate - bitrate);

		/* tseg brp biterror */
		if (bitrate_error > best_bitrate_error)
			continue;

		/* reset sample point error if we have a better bitrate */
		if (bitrate_error < best_bitrate_error)
			best_sample_point_error = UINT_MAX;

		can_update_sample_point(btc, sample_point_nominal, tseg / 2,
					&tseg1, &tseg2, &sample_point_error);
		if (sample_point_error > best_sample_point_error)
			continue;

		best_sample_point_error = sample_point_error;
		best_bitrate_error = bitrate_error;
		best_tseg = tseg / 2;
		best_brp = brp;

		if (bitrate_error == 0 && sample_point_error == 0)
			break;
	}

	if (best_bitrate_error) {
		/* Error in one-tenth of a percent */
		v64 = (u64)best_bitrate_error * 1000;
		do_div(v64, bt->bitrate);
		bitrate_error = (u32)v64;
		if (bitrate_error > CAN_CALC_MAX_ERROR) {
			netdev_err(dev,
				   "bitrate error %d.%d%% too high\n",
				   bitrate_error / 10, bitrate_error % 10);
			return -EDOM;
		}
		netdev_warn(dev, "bitrate error %d.%d%%\n",
			    bitrate_error / 10, bitrate_error % 10);
	}

	/* real sample point */
	bt->sample_point = can_update_sample_point(btc, sample_point_nominal,
						   best_tseg, &tseg1, &tseg2,
						   NULL);

	v64 = (u64)best_brp * 1000 * 1000 * 1000;
	do_div(v64, priv->clock.freq);
	bt->tq = (u32)v64;
	bt->prop_seg = tseg1 / 2;
	bt->phase_seg1 = tseg1 - bt->prop_seg;
	bt->phase_seg2 = tseg2;

	/* check for sjw user settings */
	if (!bt->sjw || !btc->sjw_max) {
		bt->sjw = 1;
	} else {
		/* bt->sjw is at least 1 -> sanitize upper bound to sjw_max */
		if (bt->sjw > btc->sjw_max)
			bt->sjw = btc->sjw_max;
		/* bt->sjw must not be higher than tseg2 */
		if (tseg2 < bt->sjw)
			bt->sjw = tseg2;
	}

	bt->brp = best_brp;

	/* real bitrate */
	bt->bitrate = priv->clock.freq /
		(bt->brp * (CAN_SYNC_SEG + tseg1 + tseg2));

	return 0;
}
#else /* !CONFIG_CAN_CALC_BITTIMING */
static int can_calc_bittiming(struct net_device *dev, struct can_bittiming *bt,
			      const struct can_bittiming_const *btc)
{
	netdev_err(dev, "bit-timing calculation not available\n");
	return -EINVAL;
}
#endif /* CONFIG_CAN_CALC_BITTIMING */

/* Checks the validity of the specified bit-timing parameters prop_seg,
 * phase_seg1, phase_seg2 and sjw and tries to determine the bitrate
 * prescaler value brp. You can find more information in the header
 * file linux/can/netlink.h.
 */
static int can_fixup_bittiming(struct net_device *dev, struct can_bittiming *bt,
			       const struct can_bittiming_const *btc)
{
	struct can_priv *priv = netdev_priv(dev);
	int tseg1, alltseg;
	u64 brp64;

	tseg1 = bt->prop_seg + bt->phase_seg1;
	if (!bt->sjw)
		bt->sjw = 1;
	if (bt->sjw > btc->sjw_max ||
	    tseg1 < btc->tseg1_min || tseg1 > btc->tseg1_max ||
	    bt->phase_seg2 < btc->tseg2_min || bt->phase_seg2 > btc->tseg2_max)
		return -ERANGE;

	brp64 = (u64)priv->clock.freq * (u64)bt->tq;
	if (btc->brp_inc > 1)
		do_div(brp64, btc->brp_inc);
	brp64 += 500000000UL - 1;
	do_div(brp64, 1000000000UL); /* the practicable BRP */
	if (btc->brp_inc > 1)
		brp64 *= btc->brp_inc;
	bt->brp = (u32)brp64;

	if (bt->brp < btc->brp_min || bt->brp > btc->brp_max)
		return -EINVAL;

	alltseg = bt->prop_seg + bt->phase_seg1 + bt->phase_seg2 + 1;
	bt->bitrate = priv->clock.freq / (bt->brp * alltseg);
	bt->sample_point = ((tseg1 + 1) * 1000) / alltseg;

	return 0;
}

/* Checks the validity of predefined bitrate settings */
static int
can_validate_bitrate(struct net_device *dev, struct can_bittiming *bt,
		     const u32 *bitrate_const,
		     const unsigned int bitrate_const_cnt)
{
	struct can_priv *priv = netdev_priv(dev);
	unsigned int i;

	for (i = 0; i < bitrate_const_cnt; i++) {
		if (bt->bitrate == bitrate_const[i])
			break;
	}

	if (i >= priv->bitrate_const_cnt)
		return -EINVAL;

	return 0;
}

static int can_get_bittiming(struct net_device *dev, struct can_bittiming *bt,
			     const struct can_bittiming_const *btc,
			     const u32 *bitrate_const,
			     const unsigned int bitrate_const_cnt)
{
	int err;

	/* Depending on the given can_bittiming parameter structure the CAN
	 * timing parameters are calculated based on the provided bitrate OR
	 * alternatively the CAN timing parameters (tq, prop_seg, etc.) are
	 * provided directly which are then checked and fixed up.
	 */
	if (!bt->tq && bt->bitrate && btc)
		err = can_calc_bittiming(dev, bt, btc);
	else if (bt->tq && !bt->bitrate && btc)
		err = can_fixup_bittiming(dev, bt, btc);
	else if (!bt->tq && bt->bitrate && bitrate_const)
		err = can_validate_bitrate(dev, bt, bitrate_const,
					   bitrate_const_cnt);
	else
		err = -EINVAL;

	return err;
}

static void can_update_state_error_stats(struct net_device *dev,
					 enum can_state new_state)
{
	struct can_priv *priv = netdev_priv(dev);

	if (new_state <= priv->state)
		return;

	switch (new_state) {
	case CAN_STATE_ERROR_WARNING:
		priv->can_stats.error_warning++;
		break;
	case CAN_STATE_ERROR_PASSIVE:
		priv->can_stats.error_passive++;
		break;
	case CAN_STATE_BUS_OFF:
		priv->can_stats.bus_off++;
		break;
	default:
		break;
	}
}

static int can_tx_state_to_frame(struct net_device *dev, enum can_state state)
{
	switch (state) {
	case CAN_STATE_ERROR_ACTIVE:
		return CAN_ERR_CRTL_ACTIVE;
	case CAN_STATE_ERROR_WARNING:
		return CAN_ERR_CRTL_TX_WARNING;
	case CAN_STATE_ERROR_PASSIVE:
		return CAN_ERR_CRTL_TX_PASSIVE;
	default:
		return 0;
	}
}

static int can_rx_state_to_frame(struct net_device *dev, enum can_state state)
{
	switch (state) {
	case CAN_STATE_ERROR_ACTIVE:
		return CAN_ERR_CRTL_ACTIVE;
	case CAN_STATE_ERROR_WARNING:
		return CAN_ERR_CRTL_RX_WARNING;
	case CAN_STATE_ERROR_PASSIVE:
		return CAN_ERR_CRTL_RX_PASSIVE;
	default:
		return 0;
	}
}

static const char *can_get_state_str(const enum can_state state)
{
	switch (state) {
	case CAN_STATE_ERROR_ACTIVE:
		return "Error Active";
	case CAN_STATE_ERROR_WARNING:
		return "Error Warning";
	case CAN_STATE_ERROR_PASSIVE:
		return "Error Passive";
	case CAN_STATE_BUS_OFF:
		return "Bus Off";
	case CAN_STATE_STOPPED:
		return "Stopped";
	case CAN_STATE_SLEEPING:
		return "Sleeping";
	default:
		return "<unknown>";
	}

	return "<unknown>";
}

void can_change_state(struct net_device *dev, struct can_frame *cf,
		      enum can_state tx_state, enum can_state rx_state)
{
	struct can_priv *priv = netdev_priv(dev);
	enum can_state new_state = max(tx_state, rx_state);

	if (unlikely(new_state == priv->state)) {
		netdev_warn(dev, "%s: oops, state did not change", __func__);
		return;
	}

	netdev_dbg(dev, "Controller changed from %s State (%d) into %s State (%d).\n",
		   can_get_state_str(priv->state), priv->state,
		   can_get_state_str(new_state), new_state);

	can_update_state_error_stats(dev, new_state);
	priv->state = new_state;

	if (!cf)
		return;

	if (unlikely(new_state == CAN_STATE_BUS_OFF)) {
		cf->can_id |= CAN_ERR_BUSOFF;
		return;
	}

	cf->can_id |= CAN_ERR_CRTL;
	cf->data[1] |= tx_state >= rx_state ?
		       can_tx_state_to_frame(dev, tx_state) : 0;
	cf->data[1] |= tx_state <= rx_state ?
		       can_rx_state_to_frame(dev, rx_state) : 0;
}
EXPORT_SYMBOL_GPL(can_change_state);

/* Local echo of CAN messages
 *
 * CAN network devices *should* support a local echo functionality
 * (see Documentation/networking/can.rst). To test the handling of CAN
 * interfaces that do not support the local echo both driver types are
 * implemented. In the case that the driver does not support the echo
 * the IFF_ECHO remains clear in dev->flags. This causes the PF_CAN core
 * to perform the echo as a fallback solution.
 */
static void can_flush_echo_skb(struct net_device *dev)
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
		     unsigned int idx)
{
	struct can_priv *priv = netdev_priv(dev);

	BUG_ON(idx >= priv->echo_skb_max);

	/* check flag whether this packet has to be looped back */
	if (!(dev->flags & IFF_ECHO) || skb->pkt_type != PACKET_LOOPBACK ||
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
		skb->pkt_type = PACKET_BROADCAST;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->dev = dev;

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
__can_get_echo_skb(struct net_device *dev, unsigned int idx, u8 *len_ptr)
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
		struct canfd_frame *cf = (struct canfd_frame *)skb->data;

		/* get the real payload length for netdev statistics */
		if (cf->can_id & CAN_RTR_FLAG)
			*len_ptr = 0;
		else
			*len_ptr = cf->len;

		priv->echo_skb[idx] = NULL;

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
unsigned int can_get_echo_skb(struct net_device *dev, unsigned int idx)
{
	struct sk_buff *skb;
	u8 len;

	skb = __can_get_echo_skb(dev, idx, &len);
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
void can_free_echo_skb(struct net_device *dev, unsigned int idx)
{
	struct can_priv *priv = netdev_priv(dev);

	BUG_ON(idx >= priv->echo_skb_max);

	if (priv->echo_skb[idx]) {
		dev_kfree_skb_any(priv->echo_skb[idx]);
		priv->echo_skb[idx] = NULL;
	}
}
EXPORT_SYMBOL_GPL(can_free_echo_skb);

/* CAN device restart for bus-off recovery */
static void can_restart(struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct sk_buff *skb;
	struct can_frame *cf;
	int err;

	BUG_ON(netif_carrier_ok(dev));

	/* No synchronization needed because the device is bus-off and
	 * no messages can come in or go out.
	 */
	can_flush_echo_skb(dev);

	/* send restart message upstream */
	skb = alloc_can_err_skb(dev, &cf);
	if (!skb)
		goto restart;

	cf->can_id |= CAN_ERR_RESTARTED;

	netif_rx_ni(skb);

	stats->rx_packets++;
	stats->rx_bytes += cf->len;

restart:
	netdev_dbg(dev, "restarted\n");
	priv->can_stats.restarts++;

	/* Now restart the device */
	err = priv->do_set_mode(dev, CAN_MODE_START);

	netif_carrier_on(dev);
	if (err)
		netdev_err(dev, "Error %d during restart", err);
}

static void can_restart_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct can_priv *priv = container_of(dwork, struct can_priv,
					     restart_work);

	can_restart(priv->dev);
}

int can_restart_now(struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);

	/* A manual restart is only permitted if automatic restart is
	 * disabled and the device is in the bus-off state
	 */
	if (priv->restart_ms)
		return -EINVAL;
	if (priv->state != CAN_STATE_BUS_OFF)
		return -EBUSY;

	cancel_delayed_work_sync(&priv->restart_work);
	can_restart(dev);

	return 0;
}

/* CAN bus-off
 *
 * This functions should be called when the device goes bus-off to
 * tell the netif layer that no more packets can be sent or received.
 * If enabled, a timer is started to trigger bus-off recovery.
 */
void can_bus_off(struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);

	if (priv->restart_ms)
		netdev_info(dev, "bus-off, scheduling restart in %d ms\n",
			    priv->restart_ms);
	else
		netdev_info(dev, "bus-off\n");

	netif_carrier_off(dev);

	if (priv->restart_ms)
		schedule_delayed_work(&priv->restart_work,
				      msecs_to_jiffies(priv->restart_ms));
}
EXPORT_SYMBOL_GPL(can_bus_off);

static void can_setup(struct net_device *dev)
{
	dev->type = ARPHRD_CAN;
	dev->mtu = CAN_MTU;
	dev->hard_header_len = 0;
	dev->addr_len = 0;
	dev->tx_queue_len = 10;

	/* New-style flags. */
	dev->flags = IFF_NOARP;
	dev->features = NETIF_F_HW_CSUM;
}

struct sk_buff *alloc_can_skb(struct net_device *dev, struct can_frame **cf)
{
	struct sk_buff *skb;

	skb = netdev_alloc_skb(dev, sizeof(struct can_skb_priv) +
			       sizeof(struct can_frame));
	if (unlikely(!skb))
		return NULL;

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
	if (unlikely(!skb))
		return NULL;

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

/* Allocate and setup space for the CAN network device */
struct net_device *alloc_candev_mqs(int sizeof_priv, unsigned int echo_skb_max,
				    unsigned int txqs, unsigned int rxqs)
{
	struct net_device *dev;
	struct can_priv *priv;
	int size;

	/* We put the driver's priv, the CAN mid layer priv and the
	 * echo skb into the netdevice's priv. The memory layout for
	 * the netdev_priv is like this:
	 *
	 * +-------------------------+
	 * | driver's priv           |
	 * +-------------------------+
	 * | struct can_ml_priv      |
	 * +-------------------------+
	 * | array of struct sk_buff |
	 * +-------------------------+
	 */

	size = ALIGN(sizeof_priv, NETDEV_ALIGN) + sizeof(struct can_ml_priv);

	if (echo_skb_max)
		size = ALIGN(size, sizeof(struct sk_buff *)) +
			echo_skb_max * sizeof(struct sk_buff *);

	dev = alloc_netdev_mqs(size, "can%d", NET_NAME_UNKNOWN, can_setup,
			       txqs, rxqs);
	if (!dev)
		return NULL;

	priv = netdev_priv(dev);
	priv->dev = dev;

	dev->ml_priv = (void *)priv + ALIGN(sizeof_priv, NETDEV_ALIGN);

	if (echo_skb_max) {
		priv->echo_skb_max = echo_skb_max;
		priv->echo_skb = (void *)priv +
			(size - echo_skb_max * sizeof(struct sk_buff *));
	}

	priv->state = CAN_STATE_STOPPED;

	INIT_DELAYED_WORK(&priv->restart_work, can_restart_work);

	return dev;
}
EXPORT_SYMBOL_GPL(alloc_candev_mqs);

/* Free space of the CAN network device */
void free_candev(struct net_device *dev)
{
	free_netdev(dev);
}
EXPORT_SYMBOL_GPL(free_candev);

/* changing MTU and control mode for CAN/CANFD devices */
int can_change_mtu(struct net_device *dev, int new_mtu)
{
	struct can_priv *priv = netdev_priv(dev);

	/* Do not allow changing the MTU while running */
	if (dev->flags & IFF_UP)
		return -EBUSY;

	/* allow change of MTU according to the CANFD ability of the device */
	switch (new_mtu) {
	case CAN_MTU:
		/* 'CANFD-only' controllers can not switch to CAN_MTU */
		if (priv->ctrlmode_static & CAN_CTRLMODE_FD)
			return -EINVAL;

		priv->ctrlmode &= ~CAN_CTRLMODE_FD;
		break;

	case CANFD_MTU:
		/* check for potential CANFD ability */
		if (!(priv->ctrlmode_supported & CAN_CTRLMODE_FD) &&
		    !(priv->ctrlmode_static & CAN_CTRLMODE_FD))
			return -EINVAL;

		priv->ctrlmode |= CAN_CTRLMODE_FD;
		break;

	default:
		return -EINVAL;
	}

	dev->mtu = new_mtu;
	return 0;
}
EXPORT_SYMBOL_GPL(can_change_mtu);

/* Common open function when the device gets opened.
 *
 * This function should be called in the open function of the device
 * driver.
 */
int open_candev(struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);

	if (!priv->bittiming.bitrate) {
		netdev_err(dev, "bit-timing not yet defined\n");
		return -EINVAL;
	}

	/* For CAN FD the data bitrate has to be >= the arbitration bitrate */
	if ((priv->ctrlmode & CAN_CTRLMODE_FD) &&
	    (!priv->data_bittiming.bitrate ||
	     priv->data_bittiming.bitrate < priv->bittiming.bitrate)) {
		netdev_err(dev, "incorrect/missing data bit-timing\n");
		return -EINVAL;
	}

	/* Switch carrier on if device was stopped while in bus-off state */
	if (!netif_carrier_ok(dev))
		netif_carrier_on(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(open_candev);

#ifdef CONFIG_OF
/* Common function that can be used to understand the limitation of
 * a transceiver when it provides no means to determine these limitations
 * at runtime.
 */
void of_can_transceiver(struct net_device *dev)
{
	struct device_node *dn;
	struct can_priv *priv = netdev_priv(dev);
	struct device_node *np = dev->dev.parent->of_node;
	int ret;

	dn = of_get_child_by_name(np, "can-transceiver");
	if (!dn)
		return;

	ret = of_property_read_u32(dn, "max-bitrate", &priv->bitrate_max);
	of_node_put(dn);
	if ((ret && ret != -EINVAL) || (!ret && !priv->bitrate_max))
		netdev_warn(dev, "Invalid value for transceiver max bitrate. Ignoring bitrate limit.\n");
}
EXPORT_SYMBOL_GPL(of_can_transceiver);
#endif

/* Common close function for cleanup before the device gets closed.
 *
 * This function should be called in the close function of the device
 * driver.
 */
void close_candev(struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);

	cancel_delayed_work_sync(&priv->restart_work);
	can_flush_echo_skb(dev);
}
EXPORT_SYMBOL_GPL(close_candev);

/* CAN netlink interface */
static const struct nla_policy can_policy[IFLA_CAN_MAX + 1] = {
	[IFLA_CAN_STATE]	= { .type = NLA_U32 },
	[IFLA_CAN_CTRLMODE]	= { .len = sizeof(struct can_ctrlmode) },
	[IFLA_CAN_RESTART_MS]	= { .type = NLA_U32 },
	[IFLA_CAN_RESTART]	= { .type = NLA_U32 },
	[IFLA_CAN_BITTIMING]	= { .len = sizeof(struct can_bittiming) },
	[IFLA_CAN_BITTIMING_CONST]
				= { .len = sizeof(struct can_bittiming_const) },
	[IFLA_CAN_CLOCK]	= { .len = sizeof(struct can_clock) },
	[IFLA_CAN_BERR_COUNTER]	= { .len = sizeof(struct can_berr_counter) },
	[IFLA_CAN_DATA_BITTIMING]
				= { .len = sizeof(struct can_bittiming) },
	[IFLA_CAN_DATA_BITTIMING_CONST]
				= { .len = sizeof(struct can_bittiming_const) },
	[IFLA_CAN_TERMINATION]	= { .type = NLA_U16 },
};

static int can_validate(struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	bool is_can_fd = false;

	/* Make sure that valid CAN FD configurations always consist of
	 * - nominal/arbitration bittiming
	 * - data bittiming
	 * - control mode with CAN_CTRLMODE_FD set
	 */

	if (!data)
		return 0;

	if (data[IFLA_CAN_CTRLMODE]) {
		struct can_ctrlmode *cm = nla_data(data[IFLA_CAN_CTRLMODE]);

		is_can_fd = cm->flags & cm->mask & CAN_CTRLMODE_FD;
	}

	if (is_can_fd) {
		if (!data[IFLA_CAN_BITTIMING] || !data[IFLA_CAN_DATA_BITTIMING])
			return -EOPNOTSUPP;
	}

	if (data[IFLA_CAN_DATA_BITTIMING]) {
		if (!is_can_fd || !data[IFLA_CAN_BITTIMING])
			return -EOPNOTSUPP;
	}

	return 0;
}

static int can_changelink(struct net_device *dev, struct nlattr *tb[],
			  struct nlattr *data[],
			  struct netlink_ext_ack *extack)
{
	struct can_priv *priv = netdev_priv(dev);
	int err;

	/* We need synchronization with dev->stop() */
	ASSERT_RTNL();

	if (data[IFLA_CAN_BITTIMING]) {
		struct can_bittiming bt;

		/* Do not allow changing bittiming while running */
		if (dev->flags & IFF_UP)
			return -EBUSY;

		/* Calculate bittiming parameters based on
		 * bittiming_const if set, otherwise pass bitrate
		 * directly via do_set_bitrate(). Bail out if neither
		 * is given.
		 */
		if (!priv->bittiming_const && !priv->do_set_bittiming)
			return -EOPNOTSUPP;

		memcpy(&bt, nla_data(data[IFLA_CAN_BITTIMING]), sizeof(bt));
		err = can_get_bittiming(dev, &bt,
					priv->bittiming_const,
					priv->bitrate_const,
					priv->bitrate_const_cnt);
		if (err)
			return err;

		if (priv->bitrate_max && bt.bitrate > priv->bitrate_max) {
			netdev_err(dev, "arbitration bitrate surpasses transceiver capabilities of %d bps\n",
				   priv->bitrate_max);
			return -EINVAL;
		}

		memcpy(&priv->bittiming, &bt, sizeof(bt));

		if (priv->do_set_bittiming) {
			/* Finally, set the bit-timing registers */
			err = priv->do_set_bittiming(dev);
			if (err)
				return err;
		}
	}

	if (data[IFLA_CAN_CTRLMODE]) {
		struct can_ctrlmode *cm;
		u32 ctrlstatic;
		u32 maskedflags;

		/* Do not allow changing controller mode while running */
		if (dev->flags & IFF_UP)
			return -EBUSY;
		cm = nla_data(data[IFLA_CAN_CTRLMODE]);
		ctrlstatic = priv->ctrlmode_static;
		maskedflags = cm->flags & cm->mask;

		/* check whether provided bits are allowed to be passed */
		if (cm->mask & ~(priv->ctrlmode_supported | ctrlstatic))
			return -EOPNOTSUPP;

		/* do not check for static fd-non-iso if 'fd' is disabled */
		if (!(maskedflags & CAN_CTRLMODE_FD))
			ctrlstatic &= ~CAN_CTRLMODE_FD_NON_ISO;

		/* make sure static options are provided by configuration */
		if ((maskedflags & ctrlstatic) != ctrlstatic)
			return -EOPNOTSUPP;

		/* clear bits to be modified and copy the flag values */
		priv->ctrlmode &= ~cm->mask;
		priv->ctrlmode |= maskedflags;

		/* CAN_CTRLMODE_FD can only be set when driver supports FD */
		if (priv->ctrlmode & CAN_CTRLMODE_FD)
			dev->mtu = CANFD_MTU;
		else
			dev->mtu = CAN_MTU;
	}

	if (data[IFLA_CAN_RESTART_MS]) {
		/* Do not allow changing restart delay while running */
		if (dev->flags & IFF_UP)
			return -EBUSY;
		priv->restart_ms = nla_get_u32(data[IFLA_CAN_RESTART_MS]);
	}

	if (data[IFLA_CAN_RESTART]) {
		/* Do not allow a restart while not running */
		if (!(dev->flags & IFF_UP))
			return -EINVAL;
		err = can_restart_now(dev);
		if (err)
			return err;
	}

	if (data[IFLA_CAN_DATA_BITTIMING]) {
		struct can_bittiming dbt;

		/* Do not allow changing bittiming while running */
		if (dev->flags & IFF_UP)
			return -EBUSY;

		/* Calculate bittiming parameters based on
		 * data_bittiming_const if set, otherwise pass bitrate
		 * directly via do_set_bitrate(). Bail out if neither
		 * is given.
		 */
		if (!priv->data_bittiming_const && !priv->do_set_data_bittiming)
			return -EOPNOTSUPP;

		memcpy(&dbt, nla_data(data[IFLA_CAN_DATA_BITTIMING]),
		       sizeof(dbt));
		err = can_get_bittiming(dev, &dbt,
					priv->data_bittiming_const,
					priv->data_bitrate_const,
					priv->data_bitrate_const_cnt);
		if (err)
			return err;

		if (priv->bitrate_max && dbt.bitrate > priv->bitrate_max) {
			netdev_err(dev, "canfd data bitrate surpasses transceiver capabilities of %d bps\n",
				   priv->bitrate_max);
			return -EINVAL;
		}

		memcpy(&priv->data_bittiming, &dbt, sizeof(dbt));

		if (priv->do_set_data_bittiming) {
			/* Finally, set the bit-timing registers */
			err = priv->do_set_data_bittiming(dev);
			if (err)
				return err;
		}
	}

	if (data[IFLA_CAN_TERMINATION]) {
		const u16 termval = nla_get_u16(data[IFLA_CAN_TERMINATION]);
		const unsigned int num_term = priv->termination_const_cnt;
		unsigned int i;

		if (!priv->do_set_termination)
			return -EOPNOTSUPP;

		/* check whether given value is supported by the interface */
		for (i = 0; i < num_term; i++) {
			if (termval == priv->termination_const[i])
				break;
		}
		if (i >= num_term)
			return -EINVAL;

		/* Finally, set the termination value */
		err = priv->do_set_termination(dev, termval);
		if (err)
			return err;

		priv->termination = termval;
	}

	return 0;
}

static size_t can_get_size(const struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);
	size_t size = 0;

	if (priv->bittiming.bitrate)				/* IFLA_CAN_BITTIMING */
		size += nla_total_size(sizeof(struct can_bittiming));
	if (priv->bittiming_const)				/* IFLA_CAN_BITTIMING_CONST */
		size += nla_total_size(sizeof(struct can_bittiming_const));
	size += nla_total_size(sizeof(struct can_clock));	/* IFLA_CAN_CLOCK */
	size += nla_total_size(sizeof(u32));			/* IFLA_CAN_STATE */
	size += nla_total_size(sizeof(struct can_ctrlmode));	/* IFLA_CAN_CTRLMODE */
	size += nla_total_size(sizeof(u32));			/* IFLA_CAN_RESTART_MS */
	if (priv->do_get_berr_counter)				/* IFLA_CAN_BERR_COUNTER */
		size += nla_total_size(sizeof(struct can_berr_counter));
	if (priv->data_bittiming.bitrate)			/* IFLA_CAN_DATA_BITTIMING */
		size += nla_total_size(sizeof(struct can_bittiming));
	if (priv->data_bittiming_const)				/* IFLA_CAN_DATA_BITTIMING_CONST */
		size += nla_total_size(sizeof(struct can_bittiming_const));
	if (priv->termination_const) {
		size += nla_total_size(sizeof(priv->termination));		/* IFLA_CAN_TERMINATION */
		size += nla_total_size(sizeof(*priv->termination_const) *	/* IFLA_CAN_TERMINATION_CONST */
				       priv->termination_const_cnt);
	}
	if (priv->bitrate_const)				/* IFLA_CAN_BITRATE_CONST */
		size += nla_total_size(sizeof(*priv->bitrate_const) *
				       priv->bitrate_const_cnt);
	if (priv->data_bitrate_const)				/* IFLA_CAN_DATA_BITRATE_CONST */
		size += nla_total_size(sizeof(*priv->data_bitrate_const) *
				       priv->data_bitrate_const_cnt);
	size += sizeof(priv->bitrate_max);			/* IFLA_CAN_BITRATE_MAX */

	return size;
}

static int can_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);
	struct can_ctrlmode cm = {.flags = priv->ctrlmode};
	struct can_berr_counter bec;
	enum can_state state = priv->state;

	if (priv->do_get_state)
		priv->do_get_state(dev, &state);

	if ((priv->bittiming.bitrate &&
	     nla_put(skb, IFLA_CAN_BITTIMING,
		     sizeof(priv->bittiming), &priv->bittiming)) ||

	    (priv->bittiming_const &&
	     nla_put(skb, IFLA_CAN_BITTIMING_CONST,
		     sizeof(*priv->bittiming_const), priv->bittiming_const)) ||

	    nla_put(skb, IFLA_CAN_CLOCK, sizeof(priv->clock), &priv->clock) ||
	    nla_put_u32(skb, IFLA_CAN_STATE, state) ||
	    nla_put(skb, IFLA_CAN_CTRLMODE, sizeof(cm), &cm) ||
	    nla_put_u32(skb, IFLA_CAN_RESTART_MS, priv->restart_ms) ||

	    (priv->do_get_berr_counter &&
	     !priv->do_get_berr_counter(dev, &bec) &&
	     nla_put(skb, IFLA_CAN_BERR_COUNTER, sizeof(bec), &bec)) ||

	    (priv->data_bittiming.bitrate &&
	     nla_put(skb, IFLA_CAN_DATA_BITTIMING,
		     sizeof(priv->data_bittiming), &priv->data_bittiming)) ||

	    (priv->data_bittiming_const &&
	     nla_put(skb, IFLA_CAN_DATA_BITTIMING_CONST,
		     sizeof(*priv->data_bittiming_const),
		     priv->data_bittiming_const)) ||

	    (priv->termination_const &&
	     (nla_put_u16(skb, IFLA_CAN_TERMINATION, priv->termination) ||
	      nla_put(skb, IFLA_CAN_TERMINATION_CONST,
		      sizeof(*priv->termination_const) *
		      priv->termination_const_cnt,
		      priv->termination_const))) ||

	    (priv->bitrate_const &&
	     nla_put(skb, IFLA_CAN_BITRATE_CONST,
		     sizeof(*priv->bitrate_const) *
		     priv->bitrate_const_cnt,
		     priv->bitrate_const)) ||

	    (priv->data_bitrate_const &&
	     nla_put(skb, IFLA_CAN_DATA_BITRATE_CONST,
		     sizeof(*priv->data_bitrate_const) *
		     priv->data_bitrate_const_cnt,
		     priv->data_bitrate_const)) ||

	    (nla_put(skb, IFLA_CAN_BITRATE_MAX,
		     sizeof(priv->bitrate_max),
		     &priv->bitrate_max))
	    )

		return -EMSGSIZE;

	return 0;
}

static size_t can_get_xstats_size(const struct net_device *dev)
{
	return sizeof(struct can_device_stats);
}

static int can_fill_xstats(struct sk_buff *skb, const struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);

	if (nla_put(skb, IFLA_INFO_XSTATS,
		    sizeof(priv->can_stats), &priv->can_stats))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int can_newlink(struct net *src_net, struct net_device *dev,
		       struct nlattr *tb[], struct nlattr *data[],
		       struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static void can_dellink(struct net_device *dev, struct list_head *head)
{
}

static struct rtnl_link_ops can_link_ops __read_mostly = {
	.kind		= "can",
	.maxtype	= IFLA_CAN_MAX,
	.policy		= can_policy,
	.setup		= can_setup,
	.validate	= can_validate,
	.newlink	= can_newlink,
	.changelink	= can_changelink,
	.dellink	= can_dellink,
	.get_size	= can_get_size,
	.fill_info	= can_fill_info,
	.get_xstats_size = can_get_xstats_size,
	.fill_xstats	= can_fill_xstats,
};

/* Register the CAN network device */
int register_candev(struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);

	/* Ensure termination_const, termination_const_cnt and
	 * do_set_termination consistency. All must be either set or
	 * unset.
	 */
	if ((!priv->termination_const != !priv->termination_const_cnt) ||
	    (!priv->termination_const != !priv->do_set_termination))
		return -EINVAL;

	if (!priv->bitrate_const != !priv->bitrate_const_cnt)
		return -EINVAL;

	if (!priv->data_bitrate_const != !priv->data_bitrate_const_cnt)
		return -EINVAL;

	dev->rtnl_link_ops = &can_link_ops;
	netif_carrier_off(dev);

	return register_netdev(dev);
}
EXPORT_SYMBOL_GPL(register_candev);

/* Unregister the CAN network device */
void unregister_candev(struct net_device *dev)
{
	unregister_netdev(dev);
}
EXPORT_SYMBOL_GPL(unregister_candev);

/* Test if a network device is a candev based device
 * and return the can_priv* if so.
 */
struct can_priv *safe_candev_priv(struct net_device *dev)
{
	if (dev->type != ARPHRD_CAN || dev->rtnl_link_ops != &can_link_ops)
		return NULL;

	return netdev_priv(dev);
}
EXPORT_SYMBOL_GPL(safe_candev_priv);

static __init int can_dev_init(void)
{
	int err;

	can_led_notifier_init();

	err = rtnl_link_register(&can_link_ops);
	if (!err)
		pr_info(MOD_DESC "\n");

	return err;
}
module_init(can_dev_init);

static __exit void can_dev_exit(void)
{
	rtnl_link_unregister(&can_link_ops);

	can_led_notifier_exit();
}
module_exit(can_dev_exit);

MODULE_ALIAS_RTNL_LINK("can");
