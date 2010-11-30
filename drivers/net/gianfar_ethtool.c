/*
 *  drivers/net/gianfar_ethtool.c
 *
 *  Gianfar Ethernet Driver
 *  Ethtool support for Gianfar Enet
 *  Based on e1000 ethtool support
 *
 *  Author: Andy Fleming
 *  Maintainer: Kumar Gala
 *  Modifier: Sandeep Gopalpet <sandeep.kumar@freescale.com>
 *
 *  Copyright 2003-2006, 2008-2009 Freescale Semiconductor, Inc.
 *
 *  This software may be used and distributed according to
 *  the terms of the GNU Public License, Version 2, incorporated herein
 *  by reference.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/crc32.h>
#include <asm/types.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/phy.h>

#include "gianfar.h"

extern void gfar_start(struct net_device *dev);
extern int gfar_clean_rx_ring(struct gfar_priv_rx_q *rx_queue, int rx_work_limit);

#define GFAR_MAX_COAL_USECS 0xffff
#define GFAR_MAX_COAL_FRAMES 0xff
static void gfar_fill_stats(struct net_device *dev, struct ethtool_stats *dummy,
		     u64 * buf);
static void gfar_gstrings(struct net_device *dev, u32 stringset, u8 * buf);
static int gfar_gcoalesce(struct net_device *dev, struct ethtool_coalesce *cvals);
static int gfar_scoalesce(struct net_device *dev, struct ethtool_coalesce *cvals);
static void gfar_gringparam(struct net_device *dev, struct ethtool_ringparam *rvals);
static int gfar_sringparam(struct net_device *dev, struct ethtool_ringparam *rvals);
static void gfar_gdrvinfo(struct net_device *dev, struct ethtool_drvinfo *drvinfo);

static char stat_gstrings[][ETH_GSTRING_LEN] = {
	"rx-dropped-by-kernel",
	"rx-large-frame-errors",
	"rx-short-frame-errors",
	"rx-non-octet-errors",
	"rx-crc-errors",
	"rx-overrun-errors",
	"rx-busy-errors",
	"rx-babbling-errors",
	"rx-truncated-frames",
	"ethernet-bus-error",
	"tx-babbling-errors",
	"tx-underrun-errors",
	"rx-skb-missing-errors",
	"tx-timeout-errors",
	"tx-rx-64-frames",
	"tx-rx-65-127-frames",
	"tx-rx-128-255-frames",
	"tx-rx-256-511-frames",
	"tx-rx-512-1023-frames",
	"tx-rx-1024-1518-frames",
	"tx-rx-1519-1522-good-vlan",
	"rx-bytes",
	"rx-packets",
	"rx-fcs-errors",
	"receive-multicast-packet",
	"receive-broadcast-packet",
	"rx-control-frame-packets",
	"rx-pause-frame-packets",
	"rx-unknown-op-code",
	"rx-alignment-error",
	"rx-frame-length-error",
	"rx-code-error",
	"rx-carrier-sense-error",
	"rx-undersize-packets",
	"rx-oversize-packets",
	"rx-fragmented-frames",
	"rx-jabber-frames",
	"rx-dropped-frames",
	"tx-byte-counter",
	"tx-packets",
	"tx-multicast-packets",
	"tx-broadcast-packets",
	"tx-pause-control-frames",
	"tx-deferral-packets",
	"tx-excessive-deferral-packets",
	"tx-single-collision-packets",
	"tx-multiple-collision-packets",
	"tx-late-collision-packets",
	"tx-excessive-collision-packets",
	"tx-total-collision",
	"reserved",
	"tx-dropped-frames",
	"tx-jabber-frames",
	"tx-fcs-errors",
	"tx-control-frames",
	"tx-oversize-frames",
	"tx-undersize-frames",
	"tx-fragmented-frames",
};

/* Fill in a buffer with the strings which correspond to the
 * stats */
static void gfar_gstrings(struct net_device *dev, u32 stringset, u8 * buf)
{
	struct gfar_private *priv = netdev_priv(dev);

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_RMON)
		memcpy(buf, stat_gstrings, GFAR_STATS_LEN * ETH_GSTRING_LEN);
	else
		memcpy(buf, stat_gstrings,
				GFAR_EXTRA_STATS_LEN * ETH_GSTRING_LEN);
}

/* Fill in an array of 64-bit statistics from various sources.
 * This array will be appended to the end of the ethtool_stats
 * structure, and returned to user space
 */
static void gfar_fill_stats(struct net_device *dev, struct ethtool_stats *dummy, u64 * buf)
{
	int i;
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u64 *extra = (u64 *) & priv->extra_stats;

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_RMON) {
		u32 __iomem *rmon = (u32 __iomem *) &regs->rmon;
		struct gfar_stats *stats = (struct gfar_stats *) buf;

		for (i = 0; i < GFAR_RMON_LEN; i++)
			stats->rmon[i] = (u64) gfar_read(&rmon[i]);

		for (i = 0; i < GFAR_EXTRA_STATS_LEN; i++)
			stats->extra[i] = extra[i];
	} else
		for (i = 0; i < GFAR_EXTRA_STATS_LEN; i++)
			buf[i] = extra[i];
}

static int gfar_sset_count(struct net_device *dev, int sset)
{
	struct gfar_private *priv = netdev_priv(dev);

	switch (sset) {
	case ETH_SS_STATS:
		if (priv->device_flags & FSL_GIANFAR_DEV_HAS_RMON)
			return GFAR_STATS_LEN;
		else
			return GFAR_EXTRA_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

/* Fills in the drvinfo structure with some basic info */
static void gfar_gdrvinfo(struct net_device *dev, struct
	      ethtool_drvinfo *drvinfo)
{
	strncpy(drvinfo->driver, DRV_NAME, GFAR_INFOSTR_LEN);
	strncpy(drvinfo->version, gfar_driver_version, GFAR_INFOSTR_LEN);
	strncpy(drvinfo->fw_version, "N/A", GFAR_INFOSTR_LEN);
	strncpy(drvinfo->bus_info, "N/A", GFAR_INFOSTR_LEN);
	drvinfo->regdump_len = 0;
	drvinfo->eedump_len = 0;
}


static int gfar_ssettings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct phy_device *phydev = priv->phydev;

	if (NULL == phydev)
		return -ENODEV;

	return phy_ethtool_sset(phydev, cmd);
}


/* Return the current settings in the ethtool_cmd structure */
static int gfar_gsettings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct phy_device *phydev = priv->phydev;
	struct gfar_priv_rx_q *rx_queue = NULL;
	struct gfar_priv_tx_q *tx_queue = NULL;

	if (NULL == phydev)
		return -ENODEV;
	tx_queue = priv->tx_queue[0];
	rx_queue = priv->rx_queue[0];

	/* etsec-1.7 and older versions have only one txic
	 * and rxic regs although they support multiple queues */
	cmd->maxtxpkt = get_icft_value(tx_queue->txic);
	cmd->maxrxpkt = get_icft_value(rx_queue->rxic);

	return phy_ethtool_gset(phydev, cmd);
}

/* Return the length of the register structure */
static int gfar_reglen(struct net_device *dev)
{
	return sizeof (struct gfar);
}

/* Return a dump of the GFAR register space */
static void gfar_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *regbuf)
{
	int i;
	struct gfar_private *priv = netdev_priv(dev);
	u32 __iomem *theregs = (u32 __iomem *) priv->gfargrp[0].regs;
	u32 *buf = (u32 *) regbuf;

	for (i = 0; i < sizeof (struct gfar) / sizeof (u32); i++)
		buf[i] = gfar_read(&theregs[i]);
}

/* Convert microseconds to ethernet clock ticks, which changes
 * depending on what speed the controller is running at */
static unsigned int gfar_usecs2ticks(struct gfar_private *priv, unsigned int usecs)
{
	unsigned int count;

	/* The timer is different, depending on the interface speed */
	switch (priv->phydev->speed) {
	case SPEED_1000:
		count = GFAR_GBIT_TIME;
		break;
	case SPEED_100:
		count = GFAR_100_TIME;
		break;
	case SPEED_10:
	default:
		count = GFAR_10_TIME;
		break;
	}

	/* Make sure we return a number greater than 0
	 * if usecs > 0 */
	return (usecs * 1000 + count - 1) / count;
}

/* Convert ethernet clock ticks to microseconds */
static unsigned int gfar_ticks2usecs(struct gfar_private *priv, unsigned int ticks)
{
	unsigned int count;

	/* The timer is different, depending on the interface speed */
	switch (priv->phydev->speed) {
	case SPEED_1000:
		count = GFAR_GBIT_TIME;
		break;
	case SPEED_100:
		count = GFAR_100_TIME;
		break;
	case SPEED_10:
	default:
		count = GFAR_10_TIME;
		break;
	}

	/* Make sure we return a number greater than 0 */
	/* if ticks is > 0 */
	return (ticks * count) / 1000;
}

/* Get the coalescing parameters, and put them in the cvals
 * structure.  */
static int gfar_gcoalesce(struct net_device *dev, struct ethtool_coalesce *cvals)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar_priv_rx_q *rx_queue = NULL;
	struct gfar_priv_tx_q *tx_queue = NULL;
	unsigned long rxtime;
	unsigned long rxcount;
	unsigned long txtime;
	unsigned long txcount;

	if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_COALESCE))
		return -EOPNOTSUPP;

	if (NULL == priv->phydev)
		return -ENODEV;

	rx_queue = priv->rx_queue[0];
	tx_queue = priv->tx_queue[0];

	rxtime  = get_ictt_value(rx_queue->rxic);
	rxcount = get_icft_value(rx_queue->rxic);
	txtime  = get_ictt_value(tx_queue->txic);
	txcount = get_icft_value(tx_queue->txic);
	cvals->rx_coalesce_usecs = gfar_ticks2usecs(priv, rxtime);
	cvals->rx_max_coalesced_frames = rxcount;

	cvals->tx_coalesce_usecs = gfar_ticks2usecs(priv, txtime);
	cvals->tx_max_coalesced_frames = txcount;

	cvals->use_adaptive_rx_coalesce = 0;
	cvals->use_adaptive_tx_coalesce = 0;

	cvals->pkt_rate_low = 0;
	cvals->rx_coalesce_usecs_low = 0;
	cvals->rx_max_coalesced_frames_low = 0;
	cvals->tx_coalesce_usecs_low = 0;
	cvals->tx_max_coalesced_frames_low = 0;

	/* When the packet rate is below pkt_rate_high but above
	 * pkt_rate_low (both measured in packets per second) the
	 * normal {rx,tx}_* coalescing parameters are used.
	 */

	/* When the packet rate is (measured in packets per second)
	 * is above pkt_rate_high, the {rx,tx}_*_high parameters are
	 * used.
	 */
	cvals->pkt_rate_high = 0;
	cvals->rx_coalesce_usecs_high = 0;
	cvals->rx_max_coalesced_frames_high = 0;
	cvals->tx_coalesce_usecs_high = 0;
	cvals->tx_max_coalesced_frames_high = 0;

	/* How often to do adaptive coalescing packet rate sampling,
	 * measured in seconds.  Must not be zero.
	 */
	cvals->rate_sample_interval = 0;

	return 0;
}

/* Change the coalescing values.
 * Both cvals->*_usecs and cvals->*_frames have to be > 0
 * in order for coalescing to be active
 */
static int gfar_scoalesce(struct net_device *dev, struct ethtool_coalesce *cvals)
{
	struct gfar_private *priv = netdev_priv(dev);
	int i = 0;

	if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_COALESCE))
		return -EOPNOTSUPP;

	/* Set up rx coalescing */
	/* As of now, we will enable/disable coalescing for all
	 * queues together in case of eTSEC2, this will be modified
	 * along with the ethtool interface */
	if ((cvals->rx_coalesce_usecs == 0) ||
	    (cvals->rx_max_coalesced_frames == 0)) {
		for (i = 0; i < priv->num_rx_queues; i++)
			priv->rx_queue[i]->rxcoalescing = 0;
	} else {
		for (i = 0; i < priv->num_rx_queues; i++)
			priv->rx_queue[i]->rxcoalescing = 1;
	}

	if (NULL == priv->phydev)
		return -ENODEV;

	/* Check the bounds of the values */
	if (cvals->rx_coalesce_usecs > GFAR_MAX_COAL_USECS) {
		pr_info("Coalescing is limited to %d microseconds\n",
				GFAR_MAX_COAL_USECS);
		return -EINVAL;
	}

	if (cvals->rx_max_coalesced_frames > GFAR_MAX_COAL_FRAMES) {
		pr_info("Coalescing is limited to %d frames\n",
				GFAR_MAX_COAL_FRAMES);
		return -EINVAL;
	}

	for (i = 0; i < priv->num_rx_queues; i++) {
		priv->rx_queue[i]->rxic = mk_ic_value(
			cvals->rx_max_coalesced_frames,
			gfar_usecs2ticks(priv, cvals->rx_coalesce_usecs));
	}

	/* Set up tx coalescing */
	if ((cvals->tx_coalesce_usecs == 0) ||
	    (cvals->tx_max_coalesced_frames == 0)) {
		for (i = 0; i < priv->num_tx_queues; i++)
			priv->tx_queue[i]->txcoalescing = 0;
	} else {
		for (i = 0; i < priv->num_tx_queues; i++)
			priv->tx_queue[i]->txcoalescing = 1;
	}

	/* Check the bounds of the values */
	if (cvals->tx_coalesce_usecs > GFAR_MAX_COAL_USECS) {
		pr_info("Coalescing is limited to %d microseconds\n",
				GFAR_MAX_COAL_USECS);
		return -EINVAL;
	}

	if (cvals->tx_max_coalesced_frames > GFAR_MAX_COAL_FRAMES) {
		pr_info("Coalescing is limited to %d frames\n",
				GFAR_MAX_COAL_FRAMES);
		return -EINVAL;
	}

	for (i = 0; i < priv->num_tx_queues; i++) {
		priv->tx_queue[i]->txic = mk_ic_value(
			cvals->tx_max_coalesced_frames,
			gfar_usecs2ticks(priv, cvals->tx_coalesce_usecs));
	}

	gfar_configure_coalescing(priv, 0xFF, 0xFF);

	return 0;
}

/* Fills in rvals with the current ring parameters.  Currently,
 * rx, rx_mini, and rx_jumbo rings are the same size, as mini and
 * jumbo are ignored by the driver */
static void gfar_gringparam(struct net_device *dev, struct ethtool_ringparam *rvals)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar_priv_tx_q *tx_queue = NULL;
	struct gfar_priv_rx_q *rx_queue = NULL;

	tx_queue = priv->tx_queue[0];
	rx_queue = priv->rx_queue[0];

	rvals->rx_max_pending = GFAR_RX_MAX_RING_SIZE;
	rvals->rx_mini_max_pending = GFAR_RX_MAX_RING_SIZE;
	rvals->rx_jumbo_max_pending = GFAR_RX_MAX_RING_SIZE;
	rvals->tx_max_pending = GFAR_TX_MAX_RING_SIZE;

	/* Values changeable by the user.  The valid values are
	 * in the range 1 to the "*_max_pending" counterpart above.
	 */
	rvals->rx_pending = rx_queue->rx_ring_size;
	rvals->rx_mini_pending = rx_queue->rx_ring_size;
	rvals->rx_jumbo_pending = rx_queue->rx_ring_size;
	rvals->tx_pending = tx_queue->tx_ring_size;
}

/* Change the current ring parameters, stopping the controller if
 * necessary so that we don't mess things up while we're in
 * motion.  We wait for the ring to be clean before reallocating
 * the rings. */
static int gfar_sringparam(struct net_device *dev, struct ethtool_ringparam *rvals)
{
	struct gfar_private *priv = netdev_priv(dev);
	int err = 0, i = 0;

	if (rvals->rx_pending > GFAR_RX_MAX_RING_SIZE)
		return -EINVAL;

	if (!is_power_of_2(rvals->rx_pending)) {
		printk("%s: Ring sizes must be a power of 2\n",
				dev->name);
		return -EINVAL;
	}

	if (rvals->tx_pending > GFAR_TX_MAX_RING_SIZE)
		return -EINVAL;

	if (!is_power_of_2(rvals->tx_pending)) {
		printk("%s: Ring sizes must be a power of 2\n",
				dev->name);
		return -EINVAL;
	}


	if (dev->flags & IFF_UP) {
		unsigned long flags;

		/* Halt TX and RX, and process the frames which
		 * have already been received */
		local_irq_save(flags);
		lock_tx_qs(priv);
		lock_rx_qs(priv);

		gfar_halt(dev);

		unlock_rx_qs(priv);
		unlock_tx_qs(priv);
		local_irq_restore(flags);

		for (i = 0; i < priv->num_rx_queues; i++)
			gfar_clean_rx_ring(priv->rx_queue[i],
					priv->rx_queue[i]->rx_ring_size);

		/* Now we take down the rings to rebuild them */
		stop_gfar(dev);
	}

	/* Change the size */
	for (i = 0; i < priv->num_rx_queues; i++) {
		priv->rx_queue[i]->rx_ring_size = rvals->rx_pending;
		priv->tx_queue[i]->tx_ring_size = rvals->tx_pending;
		priv->tx_queue[i]->num_txbdfree = priv->tx_queue[i]->tx_ring_size;
	}

	/* Rebuild the rings with the new size */
	if (dev->flags & IFF_UP) {
		err = startup_gfar(dev);
		netif_tx_wake_all_queues(dev);
	}
	return err;
}

static int gfar_set_rx_csum(struct net_device *dev, uint32_t data)
{
	struct gfar_private *priv = netdev_priv(dev);
	unsigned long flags;
	int err = 0, i = 0;

	if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_CSUM))
		return -EOPNOTSUPP;


	if (dev->flags & IFF_UP) {
		/* Halt TX and RX, and process the frames which
		 * have already been received */
		local_irq_save(flags);
		lock_tx_qs(priv);
		lock_rx_qs(priv);

		gfar_halt(dev);

		unlock_tx_qs(priv);
		unlock_rx_qs(priv);
		local_irq_restore(flags);

		for (i = 0; i < priv->num_rx_queues; i++)
			gfar_clean_rx_ring(priv->rx_queue[i],
					priv->rx_queue[i]->rx_ring_size);

		/* Now we take down the rings to rebuild them */
		stop_gfar(dev);
	}

	spin_lock_irqsave(&priv->bflock, flags);
	priv->rx_csum_enable = data;
	spin_unlock_irqrestore(&priv->bflock, flags);

	if (dev->flags & IFF_UP) {
		err = startup_gfar(dev);
		netif_tx_wake_all_queues(dev);
	}
	return err;
}

static uint32_t gfar_get_rx_csum(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);

	if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_CSUM))
		return 0;

	return priv->rx_csum_enable;
}

static int gfar_set_tx_csum(struct net_device *dev, uint32_t data)
{
	struct gfar_private *priv = netdev_priv(dev);

	if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_CSUM))
		return -EOPNOTSUPP;

	netif_tx_lock_bh(dev);

	if (data)
		dev->features |= NETIF_F_IP_CSUM;
	else
		dev->features &= ~NETIF_F_IP_CSUM;

	netif_tx_unlock_bh(dev);

	return 0;
}

static uint32_t gfar_get_tx_csum(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);

	if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_CSUM))
		return 0;

	return (dev->features & NETIF_F_IP_CSUM) != 0;
}

static uint32_t gfar_get_msglevel(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	return priv->msg_enable;
}

static void gfar_set_msglevel(struct net_device *dev, uint32_t data)
{
	struct gfar_private *priv = netdev_priv(dev);
	priv->msg_enable = data;
}

#ifdef CONFIG_PM
static void gfar_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct gfar_private *priv = netdev_priv(dev);

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_MAGIC_PACKET) {
		wol->supported = WAKE_MAGIC;
		wol->wolopts = priv->wol_en ? WAKE_MAGIC : 0;
	} else {
		wol->supported = wol->wolopts = 0;
	}
}

static int gfar_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct gfar_private *priv = netdev_priv(dev);
	unsigned long flags;

	if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_MAGIC_PACKET) &&
	    wol->wolopts != 0)
		return -EINVAL;

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EINVAL;

	device_set_wakeup_enable(&dev->dev, wol->wolopts & WAKE_MAGIC);

	spin_lock_irqsave(&priv->bflock, flags);
	priv->wol_en =  !!device_may_wakeup(&dev->dev);
	spin_unlock_irqrestore(&priv->bflock, flags);

	return 0;
}
#endif

static int gfar_ethflow_to_class(int flow_type, u64 *class)
{
	switch (flow_type) {
	case TCP_V4_FLOW:
		*class = CLASS_CODE_TCP_IPV4;
		break;
	case UDP_V4_FLOW:
		*class = CLASS_CODE_UDP_IPV4;
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
		*class = CLASS_CODE_AH_ESP_IPV4;
		break;
	case SCTP_V4_FLOW:
		*class = CLASS_CODE_SCTP_IPV4;
		break;
	case TCP_V6_FLOW:
		*class = CLASS_CODE_TCP_IPV6;
		break;
	case UDP_V6_FLOW:
		*class = CLASS_CODE_UDP_IPV6;
		break;
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
		*class = CLASS_CODE_AH_ESP_IPV6;
		break;
	case SCTP_V6_FLOW:
		*class = CLASS_CODE_SCTP_IPV6;
		break;
	default:
		return 0;
	}

	return 1;
}

static void ethflow_to_filer_rules (struct gfar_private *priv, u64 ethflow)
{
	u32 fcr = 0x0, fpr = FPR_FILER_MASK;

	if (ethflow & RXH_L2DA) {
		fcr = RQFCR_PID_DAH |RQFCR_CMP_NOMATCH |
			RQFCR_HASH | RQFCR_AND | RQFCR_HASHTBL_0;
		ftp_rqfpr[priv->cur_filer_idx] = fpr;
		ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;

		fcr = RQFCR_PID_DAL | RQFCR_AND | RQFCR_CMP_NOMATCH |
				RQFCR_HASH | RQFCR_AND | RQFCR_HASHTBL_0;
		ftp_rqfpr[priv->cur_filer_idx] = fpr;
		ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

	if (ethflow & RXH_VLAN) {
		fcr = RQFCR_PID_VID | RQFCR_CMP_NOMATCH | RQFCR_HASH |
				RQFCR_AND | RQFCR_HASHTBL_0;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		ftp_rqfpr[priv->cur_filer_idx] = fpr;
		ftp_rqfcr[priv->cur_filer_idx] = fcr;
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

	if (ethflow & RXH_IP_SRC) {
		fcr = RQFCR_PID_SIA | RQFCR_CMP_NOMATCH | RQFCR_HASH |
			RQFCR_AND | RQFCR_HASHTBL_0;
		ftp_rqfpr[priv->cur_filer_idx] = fpr;
		ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

	if (ethflow & (RXH_IP_DST)) {
		fcr = RQFCR_PID_DIA | RQFCR_CMP_NOMATCH | RQFCR_HASH |
			RQFCR_AND | RQFCR_HASHTBL_0;
		ftp_rqfpr[priv->cur_filer_idx] = fpr;
		ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

	if (ethflow & RXH_L3_PROTO) {
		fcr = RQFCR_PID_L4P | RQFCR_CMP_NOMATCH | RQFCR_HASH |
			RQFCR_AND | RQFCR_HASHTBL_0;
		ftp_rqfpr[priv->cur_filer_idx] = fpr;
		ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

	if (ethflow & RXH_L4_B_0_1) {
		fcr = RQFCR_PID_SPT | RQFCR_CMP_NOMATCH | RQFCR_HASH |
			RQFCR_AND | RQFCR_HASHTBL_0;
		ftp_rqfpr[priv->cur_filer_idx] = fpr;
		ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

	if (ethflow & RXH_L4_B_2_3) {
		fcr = RQFCR_PID_DPT | RQFCR_CMP_NOMATCH | RQFCR_HASH |
			RQFCR_AND | RQFCR_HASHTBL_0;
		ftp_rqfpr[priv->cur_filer_idx] = fpr;
		ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}
}

static int gfar_ethflow_to_filer_table(struct gfar_private *priv, u64 ethflow, u64 class)
{
	unsigned int last_rule_idx = priv->cur_filer_idx;
	unsigned int cmp_rqfpr;
	unsigned int local_rqfpr[MAX_FILER_IDX + 1];
	unsigned int local_rqfcr[MAX_FILER_IDX + 1];
	int i = 0x0, k = 0x0;
	int j = MAX_FILER_IDX, l = 0x0;

	switch (class) {
	case TCP_V4_FLOW:
		cmp_rqfpr = RQFPR_IPV4 |RQFPR_TCP;
		break;
	case UDP_V4_FLOW:
		cmp_rqfpr = RQFPR_IPV4 |RQFPR_UDP;
		break;
	case TCP_V6_FLOW:
		cmp_rqfpr = RQFPR_IPV6 |RQFPR_TCP;
		break;
	case UDP_V6_FLOW:
		cmp_rqfpr = RQFPR_IPV6 |RQFPR_UDP;
		break;
	case IPV4_FLOW:
		cmp_rqfpr = RQFPR_IPV4;
	case IPV6_FLOW:
		cmp_rqfpr = RQFPR_IPV6;
		break;
	default:
		printk(KERN_ERR "Right now this class is not supported\n");
		return 0;
	}

	for (i = 0; i < MAX_FILER_IDX + 1; i++) {
		local_rqfpr[j] = ftp_rqfpr[i];
		local_rqfcr[j] = ftp_rqfcr[i];
		j--;
		if ((ftp_rqfcr[i] == (RQFCR_PID_PARSE |
			RQFCR_CLE |RQFCR_AND)) &&
			(ftp_rqfpr[i] == cmp_rqfpr))
			break;
	}

	if (i == MAX_FILER_IDX + 1) {
		printk(KERN_ERR "No parse rule found, ");
		printk(KERN_ERR "can't create hash rules\n");
		return 0;
	}

	/* If a match was found, then it begins the starting of a cluster rule
	 * if it was already programmed, we need to overwrite these rules
	 */
	for (l = i+1; l < MAX_FILER_IDX; l++) {
		if ((ftp_rqfcr[l] & RQFCR_CLE) &&
			!(ftp_rqfcr[l] & RQFCR_AND)) {
			ftp_rqfcr[l] = RQFCR_CLE | RQFCR_CMP_EXACT |
				RQFCR_HASHTBL_0 | RQFCR_PID_MASK;
			ftp_rqfpr[l] = FPR_FILER_MASK;
			gfar_write_filer(priv, l, ftp_rqfcr[l], ftp_rqfpr[l]);
			break;
		}

		if (!(ftp_rqfcr[l] & RQFCR_CLE) && (ftp_rqfcr[l] & RQFCR_AND))
			continue;
		else {
			local_rqfpr[j] = ftp_rqfpr[l];
			local_rqfcr[j] = ftp_rqfcr[l];
			j--;
		}
	}

	priv->cur_filer_idx = l - 1;
	last_rule_idx = l;

	/* hash rules */
	ethflow_to_filer_rules(priv, ethflow);

	/* Write back the popped out rules again */
	for (k = j+1; k < MAX_FILER_IDX; k++) {
		ftp_rqfpr[priv->cur_filer_idx] = local_rqfpr[k];
		ftp_rqfcr[priv->cur_filer_idx] = local_rqfcr[k];
		gfar_write_filer(priv, priv->cur_filer_idx,
				local_rqfcr[k], local_rqfpr[k]);
		if (!priv->cur_filer_idx)
			break;
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

	return 1;
}

static int gfar_set_hash_opts(struct gfar_private *priv, struct ethtool_rxnfc *cmd)
{
	u64 class;

	if (!gfar_ethflow_to_class(cmd->flow_type, &class))
		return -EINVAL;

	if (class < CLASS_CODE_USER_PROG1 ||
			class > CLASS_CODE_SCTP_IPV6)
		return -EINVAL;

	/* write the filer rules here */
	if (!gfar_ethflow_to_filer_table(priv, cmd->data, cmd->flow_type))
		return -1;

	return 0;
}

static int gfar_set_nfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct gfar_private *priv = netdev_priv(dev);
	int ret = 0;

	switch(cmd->cmd) {
	case ETHTOOL_SRXFH:
		ret = gfar_set_hash_opts(priv, cmd);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

const struct ethtool_ops gfar_ethtool_ops = {
	.get_settings = gfar_gsettings,
	.set_settings = gfar_ssettings,
	.get_drvinfo = gfar_gdrvinfo,
	.get_regs_len = gfar_reglen,
	.get_regs = gfar_get_regs,
	.get_link = ethtool_op_get_link,
	.get_coalesce = gfar_gcoalesce,
	.set_coalesce = gfar_scoalesce,
	.get_ringparam = gfar_gringparam,
	.set_ringparam = gfar_sringparam,
	.get_strings = gfar_gstrings,
	.get_sset_count = gfar_sset_count,
	.get_ethtool_stats = gfar_fill_stats,
	.get_rx_csum = gfar_get_rx_csum,
	.get_tx_csum = gfar_get_tx_csum,
	.set_rx_csum = gfar_set_rx_csum,
	.set_tx_csum = gfar_set_tx_csum,
	.set_sg = ethtool_op_set_sg,
	.get_msglevel = gfar_get_msglevel,
	.set_msglevel = gfar_set_msglevel,
#ifdef CONFIG_PM
	.get_wol = gfar_get_wol,
	.set_wol = gfar_set_wol,
#endif
	.set_rxnfc = gfar_set_nfc,
};
