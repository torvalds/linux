/*
 *  drivers/net/gianfar_ethtool.c
 *
 *  Gianfar Ethernet Driver
 *  Ethtool support for Gianfar Enet
 *  Based on e1000 ethtool support
 *
 *  Author: Andy Fleming
 *  Maintainer: Kumar Gala
 *
 *  Copyright (c) 2003,2004 Freescale Semiconductor, Inc.
 *
 *  This software may be used and distributed according to
 *  the terms of the GNU Public License, Version 2, incorporated herein
 *  by reference.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
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
extern int gfar_clean_rx_ring(struct net_device *dev, int rx_work_limit);

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
	u64 *extra = (u64 *) & priv->extra_stats;

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_RMON) {
		u32 __iomem *rmon = (u32 __iomem *) & priv->regs->rmon;
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

	if (NULL == phydev)
		return -ENODEV;

	cmd->maxtxpkt = get_icft_value(priv->txic);
	cmd->maxrxpkt = get_icft_value(priv->rxic);

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
	u32 __iomem *theregs = (u32 __iomem *) priv->regs;
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
	return ((usecs * 1000 + count - 1) / count);
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
	return ((ticks * count) / 1000);
}

/* Get the coalescing parameters, and put them in the cvals
 * structure.  */
static int gfar_gcoalesce(struct net_device *dev, struct ethtool_coalesce *cvals)
{
	struct gfar_private *priv = netdev_priv(dev);
	unsigned long rxtime;
	unsigned long rxcount;
	unsigned long txtime;
	unsigned long txcount;

	if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_COALESCE))
		return -EOPNOTSUPP;

	if (NULL == priv->phydev)
		return -ENODEV;

	rxtime  = get_ictt_value(priv->rxic);
	rxcount = get_icft_value(priv->rxic);
	txtime  = get_ictt_value(priv->txic);
	txcount = get_icft_value(priv->txic);
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

	if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_COALESCE))
		return -EOPNOTSUPP;

	/* Set up rx coalescing */
	if ((cvals->rx_coalesce_usecs == 0) ||
	    (cvals->rx_max_coalesced_frames == 0))
		priv->rxcoalescing = 0;
	else
		priv->rxcoalescing = 1;

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

	priv->rxic = mk_ic_value(cvals->rx_max_coalesced_frames,
		gfar_usecs2ticks(priv, cvals->rx_coalesce_usecs));

	/* Set up tx coalescing */
	if ((cvals->tx_coalesce_usecs == 0) ||
	    (cvals->tx_max_coalesced_frames == 0))
		priv->txcoalescing = 0;
	else
		priv->txcoalescing = 1;

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

	priv->txic = mk_ic_value(cvals->tx_max_coalesced_frames,
		gfar_usecs2ticks(priv, cvals->tx_coalesce_usecs));

	gfar_write(&priv->regs->rxic, 0);
	if (priv->rxcoalescing)
		gfar_write(&priv->regs->rxic, priv->rxic);

	gfar_write(&priv->regs->txic, 0);
	if (priv->txcoalescing)
		gfar_write(&priv->regs->txic, priv->txic);

	return 0;
}

/* Fills in rvals with the current ring parameters.  Currently,
 * rx, rx_mini, and rx_jumbo rings are the same size, as mini and
 * jumbo are ignored by the driver */
static void gfar_gringparam(struct net_device *dev, struct ethtool_ringparam *rvals)
{
	struct gfar_private *priv = netdev_priv(dev);

	rvals->rx_max_pending = GFAR_RX_MAX_RING_SIZE;
	rvals->rx_mini_max_pending = GFAR_RX_MAX_RING_SIZE;
	rvals->rx_jumbo_max_pending = GFAR_RX_MAX_RING_SIZE;
	rvals->tx_max_pending = GFAR_TX_MAX_RING_SIZE;

	/* Values changeable by the user.  The valid values are
	 * in the range 1 to the "*_max_pending" counterpart above.
	 */
	rvals->rx_pending = priv->rx_ring_size;
	rvals->rx_mini_pending = priv->rx_ring_size;
	rvals->rx_jumbo_pending = priv->rx_ring_size;
	rvals->tx_pending = priv->tx_ring_size;
}

/* Change the current ring parameters, stopping the controller if
 * necessary so that we don't mess things up while we're in
 * motion.  We wait for the ring to be clean before reallocating
 * the rings. */
static int gfar_sringparam(struct net_device *dev, struct ethtool_ringparam *rvals)
{
	struct gfar_private *priv = netdev_priv(dev);
	int err = 0;

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
		spin_lock_irqsave(&priv->txlock, flags);
		spin_lock(&priv->rxlock);

		gfar_halt(dev);

		spin_unlock(&priv->rxlock);
		spin_unlock_irqrestore(&priv->txlock, flags);

		gfar_clean_rx_ring(dev, priv->rx_ring_size);

		/* Now we take down the rings to rebuild them */
		stop_gfar(dev);
	}

	/* Change the size */
	priv->rx_ring_size = rvals->rx_pending;
	priv->tx_ring_size = rvals->tx_pending;
	priv->num_txbdfree = priv->tx_ring_size;

	/* Rebuild the rings with the new size */
	if (dev->flags & IFF_UP) {
		err = startup_gfar(dev);
		netif_wake_queue(dev);
	}
	return err;
}

static int gfar_set_rx_csum(struct net_device *dev, uint32_t data)
{
	struct gfar_private *priv = netdev_priv(dev);
	unsigned long flags;
	int err = 0;

	if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_CSUM))
		return -EOPNOTSUPP;

	if (dev->flags & IFF_UP) {
		/* Halt TX and RX, and process the frames which
		 * have already been received */
		spin_lock_irqsave(&priv->txlock, flags);
		spin_lock(&priv->rxlock);

		gfar_halt(dev);

		spin_unlock(&priv->rxlock);
		spin_unlock_irqrestore(&priv->txlock, flags);

		gfar_clean_rx_ring(dev, priv->rx_ring_size);

		/* Now we take down the rings to rebuild them */
		stop_gfar(dev);
	}

	spin_lock_irqsave(&priv->bflock, flags);
	priv->rx_csum_enable = data;
	spin_unlock_irqrestore(&priv->bflock, flags);

	if (dev->flags & IFF_UP) {
		err = startup_gfar(dev);
		netif_wake_queue(dev);
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

	spin_lock_irqsave(&priv->bflock, flags);
	priv->wol_en = wol->wolopts & WAKE_MAGIC ? 1 : 0;
	device_set_wakeup_enable(&dev->dev, priv->wol_en);
	spin_unlock_irqrestore(&priv->bflock, flags);

	return 0;
}
#endif

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
};
