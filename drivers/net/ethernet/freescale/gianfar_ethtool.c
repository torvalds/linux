/*
 *  drivers/net/ethernet/freescale/gianfar_ethtool.c
 *
 *  Gianfar Ethernet Driver
 *  Ethtool support for Gianfar Enet
 *  Based on e1000 ethtool support
 *
 *  Author: Andy Fleming
 *  Maintainer: Kumar Gala
 *  Modifier: Sandeep Gopalpet <sandeep.kumar@freescale.com>
 *
 *  Copyright 2003-2006, 2008-2009, 2011 Freescale Semiconductor, Inc.
 *
 *  This software may be used and distributed according to
 *  the terms of the GNU Public License, Version 2, incorporated herein
 *  by reference.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/net_tstamp.h>
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
#include <linux/sort.h>
#include <linux/if_vlan.h>

#include "gianfar.h"

extern void gfar_start(struct net_device *dev);
extern int gfar_clean_rx_ring(struct gfar_priv_rx_q *rx_queue,
			      int rx_work_limit);

#define GFAR_MAX_COAL_USECS 0xffff
#define GFAR_MAX_COAL_FRAMES 0xff
static void gfar_fill_stats(struct net_device *dev, struct ethtool_stats *dummy,
			    u64 *buf);
static void gfar_gstrings(struct net_device *dev, u32 stringset, u8 * buf);
static int gfar_gcoalesce(struct net_device *dev,
			  struct ethtool_coalesce *cvals);
static int gfar_scoalesce(struct net_device *dev,
			  struct ethtool_coalesce *cvals);
static void gfar_gringparam(struct net_device *dev,
			    struct ethtool_ringparam *rvals);
static int gfar_sringparam(struct net_device *dev,
			   struct ethtool_ringparam *rvals);
static void gfar_gdrvinfo(struct net_device *dev,
			  struct ethtool_drvinfo *drvinfo);

static const char stat_gstrings[][ETH_GSTRING_LEN] = {
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
static void gfar_fill_stats(struct net_device *dev, struct ethtool_stats *dummy,
			    u64 *buf)
{
	int i;
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	atomic64_t *extra = (atomic64_t *)&priv->extra_stats;

	for (i = 0; i < GFAR_EXTRA_STATS_LEN; i++)
		buf[i] = atomic64_read(&extra[i]);

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_RMON) {
		u32 __iomem *rmon = (u32 __iomem *) &regs->rmon;

		for (; i < GFAR_STATS_LEN; i++, rmon++)
			buf[i] = (u64) gfar_read(rmon);
	}
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
static void gfar_gdrvinfo(struct net_device *dev,
			  struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, DRV_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, gfar_driver_version,
		sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, "N/A", sizeof(drvinfo->bus_info));
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
static void gfar_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			  void *regbuf)
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
static unsigned int gfar_usecs2ticks(struct gfar_private *priv,
				     unsigned int usecs)
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
static unsigned int gfar_ticks2usecs(struct gfar_private *priv,
				     unsigned int ticks)
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
static int gfar_gcoalesce(struct net_device *dev,
			  struct ethtool_coalesce *cvals)
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
static int gfar_scoalesce(struct net_device *dev,
			  struct ethtool_coalesce *cvals)
{
	struct gfar_private *priv = netdev_priv(dev);
	int i = 0;

	if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_COALESCE))
		return -EOPNOTSUPP;

	/* Set up rx coalescing */
	/* As of now, we will enable/disable coalescing for all
	 * queues together in case of eTSEC2, this will be modified
	 * along with the ethtool interface
	 */
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
static void gfar_gringparam(struct net_device *dev,
			    struct ethtool_ringparam *rvals)
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
 * the rings.
 */
static int gfar_sringparam(struct net_device *dev,
			   struct ethtool_ringparam *rvals)
{
	struct gfar_private *priv = netdev_priv(dev);
	int err = 0, i = 0;

	if (rvals->rx_pending > GFAR_RX_MAX_RING_SIZE)
		return -EINVAL;

	if (!is_power_of_2(rvals->rx_pending)) {
		netdev_err(dev, "Ring sizes must be a power of 2\n");
		return -EINVAL;
	}

	if (rvals->tx_pending > GFAR_TX_MAX_RING_SIZE)
		return -EINVAL;

	if (!is_power_of_2(rvals->tx_pending)) {
		netdev_err(dev, "Ring sizes must be a power of 2\n");
		return -EINVAL;
	}


	if (dev->flags & IFF_UP) {
		unsigned long flags;

		/* Halt TX and RX, and process the frames which
		 * have already been received
		 */
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
		priv->tx_queue[i]->num_txbdfree =
			priv->tx_queue[i]->tx_ring_size;
	}

	/* Rebuild the rings with the new size */
	if (dev->flags & IFF_UP) {
		err = startup_gfar(dev);
		netif_tx_wake_all_queues(dev);
	}
	return err;
}

int gfar_set_features(struct net_device *dev, netdev_features_t features)
{
	struct gfar_private *priv = netdev_priv(dev);
	unsigned long flags;
	int err = 0, i = 0;
	netdev_features_t changed = dev->features ^ features;

	if (changed & (NETIF_F_HW_VLAN_TX|NETIF_F_HW_VLAN_RX))
		gfar_vlan_mode(dev, features);

	if (!(changed & NETIF_F_RXCSUM))
		return 0;

	if (dev->flags & IFF_UP) {
		/* Halt TX and RX, and process the frames which
		 * have already been received
		 */
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

		dev->features = features;

		err = startup_gfar(dev);
		netif_tx_wake_all_queues(dev);
	}
	return err;
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

static void ethflow_to_filer_rules (struct gfar_private *priv, u64 ethflow)
{
	u32 fcr = 0x0, fpr = FPR_FILER_MASK;

	if (ethflow & RXH_L2DA) {
		fcr = RQFCR_PID_DAH |RQFCR_CMP_NOMATCH |
		      RQFCR_HASH | RQFCR_AND | RQFCR_HASHTBL_0;
		priv->ftp_rqfpr[priv->cur_filer_idx] = fpr;
		priv->ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;

		fcr = RQFCR_PID_DAL | RQFCR_AND | RQFCR_CMP_NOMATCH |
		      RQFCR_HASH | RQFCR_AND | RQFCR_HASHTBL_0;
		priv->ftp_rqfpr[priv->cur_filer_idx] = fpr;
		priv->ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

	if (ethflow & RXH_VLAN) {
		fcr = RQFCR_PID_VID | RQFCR_CMP_NOMATCH | RQFCR_HASH |
		      RQFCR_AND | RQFCR_HASHTBL_0;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->ftp_rqfpr[priv->cur_filer_idx] = fpr;
		priv->ftp_rqfcr[priv->cur_filer_idx] = fcr;
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

	if (ethflow & RXH_IP_SRC) {
		fcr = RQFCR_PID_SIA | RQFCR_CMP_NOMATCH | RQFCR_HASH |
		      RQFCR_AND | RQFCR_HASHTBL_0;
		priv->ftp_rqfpr[priv->cur_filer_idx] = fpr;
		priv->ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

	if (ethflow & (RXH_IP_DST)) {
		fcr = RQFCR_PID_DIA | RQFCR_CMP_NOMATCH | RQFCR_HASH |
		      RQFCR_AND | RQFCR_HASHTBL_0;
		priv->ftp_rqfpr[priv->cur_filer_idx] = fpr;
		priv->ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

	if (ethflow & RXH_L3_PROTO) {
		fcr = RQFCR_PID_L4P | RQFCR_CMP_NOMATCH | RQFCR_HASH |
		      RQFCR_AND | RQFCR_HASHTBL_0;
		priv->ftp_rqfpr[priv->cur_filer_idx] = fpr;
		priv->ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

	if (ethflow & RXH_L4_B_0_1) {
		fcr = RQFCR_PID_SPT | RQFCR_CMP_NOMATCH | RQFCR_HASH |
		      RQFCR_AND | RQFCR_HASHTBL_0;
		priv->ftp_rqfpr[priv->cur_filer_idx] = fpr;
		priv->ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

	if (ethflow & RXH_L4_B_2_3) {
		fcr = RQFCR_PID_DPT | RQFCR_CMP_NOMATCH | RQFCR_HASH |
		      RQFCR_AND | RQFCR_HASHTBL_0;
		priv->ftp_rqfpr[priv->cur_filer_idx] = fpr;
		priv->ftp_rqfcr[priv->cur_filer_idx] = fcr;
		gfar_write_filer(priv, priv->cur_filer_idx, fcr, fpr);
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}
}

static int gfar_ethflow_to_filer_table(struct gfar_private *priv, u64 ethflow,
				       u64 class)
{
	unsigned int last_rule_idx = priv->cur_filer_idx;
	unsigned int cmp_rqfpr;
	unsigned int *local_rqfpr;
	unsigned int *local_rqfcr;
	int i = 0x0, k = 0x0;
	int j = MAX_FILER_IDX, l = 0x0;
	int ret = 1;

	local_rqfpr = kmalloc_array(MAX_FILER_IDX + 1, sizeof(unsigned int),
				    GFP_KERNEL);
	local_rqfcr = kmalloc_array(MAX_FILER_IDX + 1, sizeof(unsigned int),
				    GFP_KERNEL);
	if (!local_rqfpr || !local_rqfcr) {
		ret = 0;
		goto err;
	}

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
	default:
		pr_err("Right now this class is not supported\n");
		ret = 0;
		goto err;
	}

	for (i = 0; i < MAX_FILER_IDX + 1; i++) {
		local_rqfpr[j] = priv->ftp_rqfpr[i];
		local_rqfcr[j] = priv->ftp_rqfcr[i];
		j--;
		if ((priv->ftp_rqfcr[i] ==
		     (RQFCR_PID_PARSE | RQFCR_CLE | RQFCR_AND)) &&
		    (priv->ftp_rqfpr[i] == cmp_rqfpr))
			break;
	}

	if (i == MAX_FILER_IDX + 1) {
		pr_err("No parse rule found, can't create hash rules\n");
		ret = 0;
		goto err;
	}

	/* If a match was found, then it begins the starting of a cluster rule
	 * if it was already programmed, we need to overwrite these rules
	 */
	for (l = i+1; l < MAX_FILER_IDX; l++) {
		if ((priv->ftp_rqfcr[l] & RQFCR_CLE) &&
		    !(priv->ftp_rqfcr[l] & RQFCR_AND)) {
			priv->ftp_rqfcr[l] = RQFCR_CLE | RQFCR_CMP_EXACT |
					     RQFCR_HASHTBL_0 | RQFCR_PID_MASK;
			priv->ftp_rqfpr[l] = FPR_FILER_MASK;
			gfar_write_filer(priv, l, priv->ftp_rqfcr[l],
					 priv->ftp_rqfpr[l]);
			break;
		}

		if (!(priv->ftp_rqfcr[l] & RQFCR_CLE) &&
			(priv->ftp_rqfcr[l] & RQFCR_AND))
			continue;
		else {
			local_rqfpr[j] = priv->ftp_rqfpr[l];
			local_rqfcr[j] = priv->ftp_rqfcr[l];
			j--;
		}
	}

	priv->cur_filer_idx = l - 1;
	last_rule_idx = l;

	/* hash rules */
	ethflow_to_filer_rules(priv, ethflow);

	/* Write back the popped out rules again */
	for (k = j+1; k < MAX_FILER_IDX; k++) {
		priv->ftp_rqfpr[priv->cur_filer_idx] = local_rqfpr[k];
		priv->ftp_rqfcr[priv->cur_filer_idx] = local_rqfcr[k];
		gfar_write_filer(priv, priv->cur_filer_idx,
				 local_rqfcr[k], local_rqfpr[k]);
		if (!priv->cur_filer_idx)
			break;
		priv->cur_filer_idx = priv->cur_filer_idx - 1;
	}

err:
	kfree(local_rqfcr);
	kfree(local_rqfpr);
	return ret;
}

static int gfar_set_hash_opts(struct gfar_private *priv,
			      struct ethtool_rxnfc *cmd)
{
	/* write the filer rules here */
	if (!gfar_ethflow_to_filer_table(priv, cmd->data, cmd->flow_type))
		return -EINVAL;

	return 0;
}

static int gfar_check_filer_hardware(struct gfar_private *priv)
{
	struct gfar __iomem *regs = NULL;
	u32 i;

	regs = priv->gfargrp[0].regs;

	/* Check if we are in FIFO mode */
	i = gfar_read(&regs->ecntrl);
	i &= ECNTRL_FIFM;
	if (i == ECNTRL_FIFM) {
		netdev_notice(priv->ndev, "Interface in FIFO mode\n");
		i = gfar_read(&regs->rctrl);
		i &= RCTRL_PRSDEP_MASK | RCTRL_PRSFM;
		if (i == (RCTRL_PRSDEP_MASK | RCTRL_PRSFM)) {
			netdev_info(priv->ndev,
				    "Receive Queue Filtering enabled\n");
		} else {
			netdev_warn(priv->ndev,
				    "Receive Queue Filtering disabled\n");
			return -EOPNOTSUPP;
		}
	}
	/* Or in standard mode */
	else {
		i = gfar_read(&regs->rctrl);
		i &= RCTRL_PRSDEP_MASK;
		if (i == RCTRL_PRSDEP_MASK) {
			netdev_info(priv->ndev,
				    "Receive Queue Filtering enabled\n");
		} else {
			netdev_warn(priv->ndev,
				    "Receive Queue Filtering disabled\n");
			return -EOPNOTSUPP;
		}
	}

	/* Sets the properties for arbitrary filer rule
	 * to the first 4 Layer 4 Bytes
	 */
	regs->rbifx = 0xC0C1C2C3;
	return 0;
}

static int gfar_comp_asc(const void *a, const void *b)
{
	return memcmp(a, b, 4);
}

static int gfar_comp_desc(const void *a, const void *b)
{
	return -memcmp(a, b, 4);
}

static void gfar_swap(void *a, void *b, int size)
{
	u32 *_a = a;
	u32 *_b = b;

	swap(_a[0], _b[0]);
	swap(_a[1], _b[1]);
	swap(_a[2], _b[2]);
	swap(_a[3], _b[3]);
}

/* Write a mask to filer cache */
static void gfar_set_mask(u32 mask, struct filer_table *tab)
{
	tab->fe[tab->index].ctrl = RQFCR_AND | RQFCR_PID_MASK | RQFCR_CMP_EXACT;
	tab->fe[tab->index].prop = mask;
	tab->index++;
}

/* Sets parse bits (e.g. IP or TCP) */
static void gfar_set_parse_bits(u32 value, u32 mask, struct filer_table *tab)
{
	gfar_set_mask(mask, tab);
	tab->fe[tab->index].ctrl = RQFCR_CMP_EXACT | RQFCR_PID_PARSE |
				   RQFCR_AND;
	tab->fe[tab->index].prop = value;
	tab->index++;
}

static void gfar_set_general_attribute(u32 value, u32 mask, u32 flag,
				       struct filer_table *tab)
{
	gfar_set_mask(mask, tab);
	tab->fe[tab->index].ctrl = RQFCR_CMP_EXACT | RQFCR_AND | flag;
	tab->fe[tab->index].prop = value;
	tab->index++;
}

/* For setting a tuple of value and mask of type flag
 * Example:
 * IP-Src = 10.0.0.0/255.0.0.0
 * value: 0x0A000000 mask: FF000000 flag: RQFPR_IPV4
 *
 * Ethtool gives us a value=0 and mask=~0 for don't care a tuple
 * For a don't care mask it gives us a 0
 *
 * The check if don't care and the mask adjustment if mask=0 is done for VLAN
 * and MAC stuff on an upper level (due to missing information on this level).
 * For these guys we can discard them if they are value=0 and mask=0.
 *
 * Further the all masks are one-padded for better hardware efficiency.
 */
static void gfar_set_attribute(u32 value, u32 mask, u32 flag,
			       struct filer_table *tab)
{
	switch (flag) {
		/* 3bit */
	case RQFCR_PID_PRI:
		if (!(value | mask))
			return;
		mask |= RQFCR_PID_PRI_MASK;
		break;
		/* 8bit */
	case RQFCR_PID_L4P:
	case RQFCR_PID_TOS:
		if (!~(mask | RQFCR_PID_L4P_MASK))
			return;
		if (!mask)
			mask = ~0;
		else
			mask |= RQFCR_PID_L4P_MASK;
		break;
		/* 12bit */
	case RQFCR_PID_VID:
		if (!(value | mask))
			return;
		mask |= RQFCR_PID_VID_MASK;
		break;
		/* 16bit */
	case RQFCR_PID_DPT:
	case RQFCR_PID_SPT:
	case RQFCR_PID_ETY:
		if (!~(mask | RQFCR_PID_PORT_MASK))
			return;
		if (!mask)
			mask = ~0;
		else
			mask |= RQFCR_PID_PORT_MASK;
		break;
		/* 24bit */
	case RQFCR_PID_DAH:
	case RQFCR_PID_DAL:
	case RQFCR_PID_SAH:
	case RQFCR_PID_SAL:
		if (!(value | mask))
			return;
		mask |= RQFCR_PID_MAC_MASK;
		break;
		/* for all real 32bit masks */
	default:
		if (!~mask)
			return;
		if (!mask)
			mask = ~0;
		break;
	}
	gfar_set_general_attribute(value, mask, flag, tab);
}

/* Translates value and mask for UDP, TCP or SCTP */
static void gfar_set_basic_ip(struct ethtool_tcpip4_spec *value,
			      struct ethtool_tcpip4_spec *mask,
			      struct filer_table *tab)
{
	gfar_set_attribute(value->ip4src, mask->ip4src, RQFCR_PID_SIA, tab);
	gfar_set_attribute(value->ip4dst, mask->ip4dst, RQFCR_PID_DIA, tab);
	gfar_set_attribute(value->pdst, mask->pdst, RQFCR_PID_DPT, tab);
	gfar_set_attribute(value->psrc, mask->psrc, RQFCR_PID_SPT, tab);
	gfar_set_attribute(value->tos, mask->tos, RQFCR_PID_TOS, tab);
}

/* Translates value and mask for RAW-IP4 */
static void gfar_set_user_ip(struct ethtool_usrip4_spec *value,
			     struct ethtool_usrip4_spec *mask,
			     struct filer_table *tab)
{
	gfar_set_attribute(value->ip4src, mask->ip4src, RQFCR_PID_SIA, tab);
	gfar_set_attribute(value->ip4dst, mask->ip4dst, RQFCR_PID_DIA, tab);
	gfar_set_attribute(value->tos, mask->tos, RQFCR_PID_TOS, tab);
	gfar_set_attribute(value->proto, mask->proto, RQFCR_PID_L4P, tab);
	gfar_set_attribute(value->l4_4_bytes, mask->l4_4_bytes, RQFCR_PID_ARB,
			   tab);

}

/* Translates value and mask for ETHER spec */
static void gfar_set_ether(struct ethhdr *value, struct ethhdr *mask,
			   struct filer_table *tab)
{
	u32 upper_temp_mask = 0;
	u32 lower_temp_mask = 0;

	/* Source address */
	if (!is_broadcast_ether_addr(mask->h_source)) {
		if (is_zero_ether_addr(mask->h_source)) {
			upper_temp_mask = 0xFFFFFFFF;
			lower_temp_mask = 0xFFFFFFFF;
		} else {
			upper_temp_mask = mask->h_source[0] << 16 |
					  mask->h_source[1] << 8  |
					  mask->h_source[2];
			lower_temp_mask = mask->h_source[3] << 16 |
					  mask->h_source[4] << 8  |
					  mask->h_source[5];
		}
		/* Upper 24bit */
		gfar_set_attribute(value->h_source[0] << 16 |
				   value->h_source[1] << 8  |
				   value->h_source[2],
				   upper_temp_mask, RQFCR_PID_SAH, tab);
		/* And the same for the lower part */
		gfar_set_attribute(value->h_source[3] << 16 |
				   value->h_source[4] << 8  |
				   value->h_source[5],
				   lower_temp_mask, RQFCR_PID_SAL, tab);
	}
	/* Destination address */
	if (!is_broadcast_ether_addr(mask->h_dest)) {
		/* Special for destination is limited broadcast */
		if ((is_broadcast_ether_addr(value->h_dest) &&
		    is_zero_ether_addr(mask->h_dest))) {
			gfar_set_parse_bits(RQFPR_EBC, RQFPR_EBC, tab);
		} else {
			if (is_zero_ether_addr(mask->h_dest)) {
				upper_temp_mask = 0xFFFFFFFF;
				lower_temp_mask = 0xFFFFFFFF;
			} else {
				upper_temp_mask = mask->h_dest[0] << 16 |
						  mask->h_dest[1] << 8  |
						  mask->h_dest[2];
				lower_temp_mask = mask->h_dest[3] << 16 |
						  mask->h_dest[4] << 8  |
						  mask->h_dest[5];
			}

			/* Upper 24bit */
			gfar_set_attribute(value->h_dest[0] << 16 |
					   value->h_dest[1] << 8  |
					   value->h_dest[2],
					   upper_temp_mask, RQFCR_PID_DAH, tab);
			/* And the same for the lower part */
			gfar_set_attribute(value->h_dest[3] << 16 |
					   value->h_dest[4] << 8  |
					   value->h_dest[5],
					   lower_temp_mask, RQFCR_PID_DAL, tab);
		}
	}

	gfar_set_attribute(value->h_proto, mask->h_proto, RQFCR_PID_ETY, tab);
}

/* Convert a rule to binary filter format of gianfar */
static int gfar_convert_to_filer(struct ethtool_rx_flow_spec *rule,
				 struct filer_table *tab)
{
	u32 vlan = 0, vlan_mask = 0;
	u32 id = 0, id_mask = 0;
	u32 cfi = 0, cfi_mask = 0;
	u32 prio = 0, prio_mask = 0;
	u32 old_index = tab->index;

	/* Check if vlan is wanted */
	if ((rule->flow_type & FLOW_EXT) && (rule->m_ext.vlan_tci != 0xFFFF)) {
		if (!rule->m_ext.vlan_tci)
			rule->m_ext.vlan_tci = 0xFFFF;

		vlan = RQFPR_VLN;
		vlan_mask = RQFPR_VLN;

		/* Separate the fields */
		id = rule->h_ext.vlan_tci & VLAN_VID_MASK;
		id_mask = rule->m_ext.vlan_tci & VLAN_VID_MASK;
		cfi = rule->h_ext.vlan_tci & VLAN_CFI_MASK;
		cfi_mask = rule->m_ext.vlan_tci & VLAN_CFI_MASK;
		prio = (rule->h_ext.vlan_tci & VLAN_PRIO_MASK) >>
		       VLAN_PRIO_SHIFT;
		prio_mask = (rule->m_ext.vlan_tci & VLAN_PRIO_MASK) >>
			    VLAN_PRIO_SHIFT;

		if (cfi == VLAN_TAG_PRESENT && cfi_mask == VLAN_TAG_PRESENT) {
			vlan |= RQFPR_CFI;
			vlan_mask |= RQFPR_CFI;
		} else if (cfi != VLAN_TAG_PRESENT &&
			   cfi_mask == VLAN_TAG_PRESENT) {
			vlan_mask |= RQFPR_CFI;
		}
	}

	switch (rule->flow_type & ~FLOW_EXT) {
	case TCP_V4_FLOW:
		gfar_set_parse_bits(RQFPR_IPV4 | RQFPR_TCP | vlan,
				    RQFPR_IPV4 | RQFPR_TCP | vlan_mask, tab);
		gfar_set_basic_ip(&rule->h_u.tcp_ip4_spec,
				  &rule->m_u.tcp_ip4_spec, tab);
		break;
	case UDP_V4_FLOW:
		gfar_set_parse_bits(RQFPR_IPV4 | RQFPR_UDP | vlan,
				    RQFPR_IPV4 | RQFPR_UDP | vlan_mask, tab);
		gfar_set_basic_ip(&rule->h_u.udp_ip4_spec,
				  &rule->m_u.udp_ip4_spec, tab);
		break;
	case SCTP_V4_FLOW:
		gfar_set_parse_bits(RQFPR_IPV4 | vlan, RQFPR_IPV4 | vlan_mask,
				    tab);
		gfar_set_attribute(132, 0, RQFCR_PID_L4P, tab);
		gfar_set_basic_ip((struct ethtool_tcpip4_spec *)&rule->h_u,
				  (struct ethtool_tcpip4_spec *)&rule->m_u,
				  tab);
		break;
	case IP_USER_FLOW:
		gfar_set_parse_bits(RQFPR_IPV4 | vlan, RQFPR_IPV4 | vlan_mask,
				    tab);
		gfar_set_user_ip((struct ethtool_usrip4_spec *) &rule->h_u,
				 (struct ethtool_usrip4_spec *) &rule->m_u,
				 tab);
		break;
	case ETHER_FLOW:
		if (vlan)
			gfar_set_parse_bits(vlan, vlan_mask, tab);
		gfar_set_ether((struct ethhdr *) &rule->h_u,
			       (struct ethhdr *) &rule->m_u, tab);
		break;
	default:
		return -1;
	}

	/* Set the vlan attributes in the end */
	if (vlan) {
		gfar_set_attribute(id, id_mask, RQFCR_PID_VID, tab);
		gfar_set_attribute(prio, prio_mask, RQFCR_PID_PRI, tab);
	}

	/* If there has been nothing written till now, it must be a default */
	if (tab->index == old_index) {
		gfar_set_mask(0xFFFFFFFF, tab);
		tab->fe[tab->index].ctrl = 0x20;
		tab->fe[tab->index].prop = 0x0;
		tab->index++;
	}

	/* Remove last AND */
	tab->fe[tab->index - 1].ctrl &= (~RQFCR_AND);

	/* Specify which queue to use or to drop */
	if (rule->ring_cookie == RX_CLS_FLOW_DISC)
		tab->fe[tab->index - 1].ctrl |= RQFCR_RJE;
	else
		tab->fe[tab->index - 1].ctrl |= (rule->ring_cookie << 10);

	/* Only big enough entries can be clustered */
	if (tab->index > (old_index + 2)) {
		tab->fe[old_index + 1].ctrl |= RQFCR_CLE;
		tab->fe[tab->index - 1].ctrl |= RQFCR_CLE;
	}

	/* In rare cases the cache can be full while there is
	 * free space in hw
	 */
	if (tab->index > MAX_FILER_CACHE_IDX - 1)
		return -EBUSY;

	return 0;
}

/* Copy size filer entries */
static void gfar_copy_filer_entries(struct gfar_filer_entry dst[0],
				    struct gfar_filer_entry src[0], s32 size)
{
	while (size > 0) {
		size--;
		dst[size].ctrl = src[size].ctrl;
		dst[size].prop = src[size].prop;
	}
}

/* Delete the contents of the filer-table between start and end
 * and collapse them
 */
static int gfar_trim_filer_entries(u32 begin, u32 end, struct filer_table *tab)
{
	int length;

	if (end > MAX_FILER_CACHE_IDX || end < begin)
		return -EINVAL;

	end++;
	length = end - begin;

	/* Copy */
	while (end < tab->index) {
		tab->fe[begin].ctrl = tab->fe[end].ctrl;
		tab->fe[begin++].prop = tab->fe[end++].prop;

	}
	/* Fill up with don't cares */
	while (begin < tab->index) {
		tab->fe[begin].ctrl = 0x60;
		tab->fe[begin].prop = 0xFFFFFFFF;
		begin++;
	}

	tab->index -= length;
	return 0;
}

/* Make space on the wanted location */
static int gfar_expand_filer_entries(u32 begin, u32 length,
				     struct filer_table *tab)
{
	if (length == 0 || length + tab->index > MAX_FILER_CACHE_IDX ||
	    begin > MAX_FILER_CACHE_IDX)
		return -EINVAL;

	gfar_copy_filer_entries(&(tab->fe[begin + length]), &(tab->fe[begin]),
				tab->index - length + 1);

	tab->index += length;
	return 0;
}

static int gfar_get_next_cluster_start(int start, struct filer_table *tab)
{
	for (; (start < tab->index) && (start < MAX_FILER_CACHE_IDX - 1);
	     start++) {
		if ((tab->fe[start].ctrl & (RQFCR_AND | RQFCR_CLE)) ==
		    (RQFCR_AND | RQFCR_CLE))
			return start;
	}
	return -1;
}

static int gfar_get_next_cluster_end(int start, struct filer_table *tab)
{
	for (; (start < tab->index) && (start < MAX_FILER_CACHE_IDX - 1);
	     start++) {
		if ((tab->fe[start].ctrl & (RQFCR_AND | RQFCR_CLE)) ==
		    (RQFCR_CLE))
			return start;
	}
	return -1;
}

/* Uses hardwares clustering option to reduce
 * the number of filer table entries
 */
static void gfar_cluster_filer(struct filer_table *tab)
{
	s32 i = -1, j, iend, jend;

	while ((i = gfar_get_next_cluster_start(++i, tab)) != -1) {
		j = i;
		while ((j = gfar_get_next_cluster_start(++j, tab)) != -1) {
			/* The cluster entries self and the previous one
			 * (a mask) must be identical!
			 */
			if (tab->fe[i].ctrl != tab->fe[j].ctrl)
				break;
			if (tab->fe[i].prop != tab->fe[j].prop)
				break;
			if (tab->fe[i - 1].ctrl != tab->fe[j - 1].ctrl)
				break;
			if (tab->fe[i - 1].prop != tab->fe[j - 1].prop)
				break;
			iend = gfar_get_next_cluster_end(i, tab);
			jend = gfar_get_next_cluster_end(j, tab);
			if (jend == -1 || iend == -1)
				break;

			/* First we make some free space, where our cluster
			 * element should be. Then we copy it there and finally
			 * delete in from its old location.
			 */
			if (gfar_expand_filer_entries(iend, (jend - j), tab) ==
			    -EINVAL)
				break;

			gfar_copy_filer_entries(&(tab->fe[iend + 1]),
						&(tab->fe[jend + 1]), jend - j);

			if (gfar_trim_filer_entries(jend - 1,
						    jend + (jend - j),
						    tab) == -EINVAL)
				return;

			/* Mask out cluster bit */
			tab->fe[iend].ctrl &= ~(RQFCR_CLE);
		}
	}
}

/* Swaps the masked bits of a1<>a2 and b1<>b2 */
static void gfar_swap_bits(struct gfar_filer_entry *a1,
			   struct gfar_filer_entry *a2,
			   struct gfar_filer_entry *b1,
			   struct gfar_filer_entry *b2, u32 mask)
{
	u32 temp[4];
	temp[0] = a1->ctrl & mask;
	temp[1] = a2->ctrl & mask;
	temp[2] = b1->ctrl & mask;
	temp[3] = b2->ctrl & mask;

	a1->ctrl &= ~mask;
	a2->ctrl &= ~mask;
	b1->ctrl &= ~mask;
	b2->ctrl &= ~mask;

	a1->ctrl |= temp[1];
	a2->ctrl |= temp[0];
	b1->ctrl |= temp[3];
	b2->ctrl |= temp[2];
}

/* Generate a list consisting of masks values with their start and
 * end of validity and block as indicator for parts belonging
 * together (glued by ANDs) in mask_table
 */
static u32 gfar_generate_mask_table(struct gfar_mask_entry *mask_table,
				    struct filer_table *tab)
{
	u32 i, and_index = 0, block_index = 1;

	for (i = 0; i < tab->index; i++) {

		/* LSByte of control = 0 sets a mask */
		if (!(tab->fe[i].ctrl & 0xF)) {
			mask_table[and_index].mask = tab->fe[i].prop;
			mask_table[and_index].start = i;
			mask_table[and_index].block = block_index;
			if (and_index >= 1)
				mask_table[and_index - 1].end = i - 1;
			and_index++;
		}
		/* cluster starts and ends will be separated because they should
		 * hold their position
		 */
		if (tab->fe[i].ctrl & RQFCR_CLE)
			block_index++;
		/* A not set AND indicates the end of a depended block */
		if (!(tab->fe[i].ctrl & RQFCR_AND))
			block_index++;
	}

	mask_table[and_index - 1].end = i - 1;

	return and_index;
}

/* Sorts the entries of mask_table by the values of the masks.
 * Important: The 0xFF80 flags of the first and last entry of a
 * block must hold their position (which queue, CLusterEnable, ReJEct,
 * AND)
 */
static void gfar_sort_mask_table(struct gfar_mask_entry *mask_table,
				 struct filer_table *temp_table, u32 and_index)
{
	/* Pointer to compare function (_asc or _desc) */
	int (*gfar_comp)(const void *, const void *);

	u32 i, size = 0, start = 0, prev = 1;
	u32 old_first, old_last, new_first, new_last;

	gfar_comp = &gfar_comp_desc;

	for (i = 0; i < and_index; i++) {
		if (prev != mask_table[i].block) {
			old_first = mask_table[start].start + 1;
			old_last = mask_table[i - 1].end;
			sort(mask_table + start, size,
			     sizeof(struct gfar_mask_entry),
			     gfar_comp, &gfar_swap);

			/* Toggle order for every block. This makes the
			 * thing more efficient!
			 */
			if (gfar_comp == gfar_comp_desc)
				gfar_comp = &gfar_comp_asc;
			else
				gfar_comp = &gfar_comp_desc;

			new_first = mask_table[start].start + 1;
			new_last = mask_table[i - 1].end;

			gfar_swap_bits(&temp_table->fe[new_first],
				       &temp_table->fe[old_first],
				       &temp_table->fe[new_last],
				       &temp_table->fe[old_last],
				       RQFCR_QUEUE | RQFCR_CLE |
				       RQFCR_RJE | RQFCR_AND);

			start = i;
			size = 0;
		}
		size++;
		prev = mask_table[i].block;
	}
}

/* Reduces the number of masks needed in the filer table to save entries
 * This is done by sorting the masks of a depended block. A depended block is
 * identified by gluing ANDs or CLE. The sorting order toggles after every
 * block. Of course entries in scope of a mask must change their location with
 * it.
 */
static int gfar_optimize_filer_masks(struct filer_table *tab)
{
	struct filer_table *temp_table;
	struct gfar_mask_entry *mask_table;

	u32 and_index = 0, previous_mask = 0, i = 0, j = 0, size = 0;
	s32 ret = 0;

	/* We need a copy of the filer table because
	 * we want to change its order
	 */
	temp_table = kmemdup(tab, sizeof(*temp_table), GFP_KERNEL);
	if (temp_table == NULL)
		return -ENOMEM;

	mask_table = kcalloc(MAX_FILER_CACHE_IDX / 2 + 1,
			     sizeof(struct gfar_mask_entry), GFP_KERNEL);

	if (mask_table == NULL) {
		ret = -ENOMEM;
		goto end;
	}

	and_index = gfar_generate_mask_table(mask_table, tab);

	gfar_sort_mask_table(mask_table, temp_table, and_index);

	/* Now we can copy the data from our duplicated filer table to
	 * the real one in the order the mask table says
	 */
	for (i = 0; i < and_index; i++) {
		size = mask_table[i].end - mask_table[i].start + 1;
		gfar_copy_filer_entries(&(tab->fe[j]),
				&(temp_table->fe[mask_table[i].start]), size);
		j += size;
	}

	/* And finally we just have to check for duplicated masks and drop the
	 * second ones
	 */
	for (i = 0; i < tab->index && i < MAX_FILER_CACHE_IDX; i++) {
		if (tab->fe[i].ctrl == 0x80) {
			previous_mask = i++;
			break;
		}
	}
	for (; i < tab->index && i < MAX_FILER_CACHE_IDX; i++) {
		if (tab->fe[i].ctrl == 0x80) {
			if (tab->fe[i].prop == tab->fe[previous_mask].prop) {
				/* Two identical ones found!
				 * So drop the second one!
				 */
				gfar_trim_filer_entries(i, i, tab);
			} else
				/* Not identical! */
				previous_mask = i;
		}
	}

	kfree(mask_table);
end:	kfree(temp_table);
	return ret;
}

/* Write the bit-pattern from software's buffer to hardware registers */
static int gfar_write_filer_table(struct gfar_private *priv,
				  struct filer_table *tab)
{
	u32 i = 0;
	if (tab->index > MAX_FILER_IDX - 1)
		return -EBUSY;

	/* Avoid inconsistent filer table to be processed */
	lock_rx_qs(priv);

	/* Fill regular entries */
	for (; i < MAX_FILER_IDX - 1 && (tab->fe[i].ctrl | tab->fe[i].ctrl);
	     i++)
		gfar_write_filer(priv, i, tab->fe[i].ctrl, tab->fe[i].prop);
	/* Fill the rest with fall-troughs */
	for (; i < MAX_FILER_IDX - 1; i++)
		gfar_write_filer(priv, i, 0x60, 0xFFFFFFFF);
	/* Last entry must be default accept
	 * because that's what people expect
	 */
	gfar_write_filer(priv, i, 0x20, 0x0);

	unlock_rx_qs(priv);

	return 0;
}

static int gfar_check_capability(struct ethtool_rx_flow_spec *flow,
				 struct gfar_private *priv)
{

	if (flow->flow_type & FLOW_EXT)	{
		if (~flow->m_ext.data[0] || ~flow->m_ext.data[1])
			netdev_warn(priv->ndev,
				    "User-specific data not supported!\n");
		if (~flow->m_ext.vlan_etype)
			netdev_warn(priv->ndev,
				    "VLAN-etype not supported!\n");
	}
	if (flow->flow_type == IP_USER_FLOW)
		if (flow->h_u.usr_ip4_spec.ip_ver != ETH_RX_NFC_IP4)
			netdev_warn(priv->ndev,
				    "IP-Version differing from IPv4 not supported!\n");

	return 0;
}

static int gfar_process_filer_changes(struct gfar_private *priv)
{
	struct ethtool_flow_spec_container *j;
	struct filer_table *tab;
	s32 i = 0;
	s32 ret = 0;

	/* So index is set to zero, too! */
	tab = kzalloc(sizeof(*tab), GFP_KERNEL);
	if (tab == NULL)
		return -ENOMEM;

	/* Now convert the existing filer data from flow_spec into
	 * filer tables binary format
	 */
	list_for_each_entry(j, &priv->rx_list.list, list) {
		ret = gfar_convert_to_filer(&j->fs, tab);
		if (ret == -EBUSY) {
			netdev_err(priv->ndev,
				   "Rule not added: No free space!\n");
			goto end;
		}
		if (ret == -1) {
			netdev_err(priv->ndev,
				   "Rule not added: Unsupported Flow-type!\n");
			goto end;
		}
	}

	i = tab->index;

	/* Optimizations to save entries */
	gfar_cluster_filer(tab);
	gfar_optimize_filer_masks(tab);

	pr_debug("\n\tSummary:\n"
		 "\tData on hardware: %d\n"
		 "\tCompression rate: %d%%\n",
		 tab->index, 100 - (100 * tab->index) / i);

	/* Write everything to hardware */
	ret = gfar_write_filer_table(priv, tab);
	if (ret == -EBUSY) {
		netdev_err(priv->ndev, "Rule not added: No free space!\n");
		goto end;
	}

end:
	kfree(tab);
	return ret;
}

static void gfar_invert_masks(struct ethtool_rx_flow_spec *flow)
{
	u32 i = 0;

	for (i = 0; i < sizeof(flow->m_u); i++)
		flow->m_u.hdata[i] ^= 0xFF;

	flow->m_ext.vlan_etype ^= 0xFFFF;
	flow->m_ext.vlan_tci ^= 0xFFFF;
	flow->m_ext.data[0] ^= ~0;
	flow->m_ext.data[1] ^= ~0;
}

static int gfar_add_cls(struct gfar_private *priv,
			struct ethtool_rx_flow_spec *flow)
{
	struct ethtool_flow_spec_container *temp, *comp;
	int ret = 0;

	temp = kmalloc(sizeof(*temp), GFP_KERNEL);
	if (temp == NULL)
		return -ENOMEM;
	memcpy(&temp->fs, flow, sizeof(temp->fs));

	gfar_invert_masks(&temp->fs);
	ret = gfar_check_capability(&temp->fs, priv);
	if (ret)
		goto clean_mem;
	/* Link in the new element at the right @location */
	if (list_empty(&priv->rx_list.list)) {
		ret = gfar_check_filer_hardware(priv);
		if (ret != 0)
			goto clean_mem;
		list_add(&temp->list, &priv->rx_list.list);
		goto process;
	} else {
		list_for_each_entry(comp, &priv->rx_list.list, list) {
			if (comp->fs.location > flow->location) {
				list_add_tail(&temp->list, &comp->list);
				goto process;
			}
			if (comp->fs.location == flow->location) {
				netdev_err(priv->ndev,
					   "Rule not added: ID %d not free!\n",
					   flow->location);
				ret = -EBUSY;
				goto clean_mem;
			}
		}
		list_add_tail(&temp->list, &priv->rx_list.list);
	}

process:
	ret = gfar_process_filer_changes(priv);
	if (ret)
		goto clean_list;
	priv->rx_list.count++;
	return ret;

clean_list:
	list_del(&temp->list);
clean_mem:
	kfree(temp);
	return ret;
}

static int gfar_del_cls(struct gfar_private *priv, u32 loc)
{
	struct ethtool_flow_spec_container *comp;
	u32 ret = -EINVAL;

	if (list_empty(&priv->rx_list.list))
		return ret;

	list_for_each_entry(comp, &priv->rx_list.list, list) {
		if (comp->fs.location == loc) {
			list_del(&comp->list);
			kfree(comp);
			priv->rx_list.count--;
			gfar_process_filer_changes(priv);
			ret = 0;
			break;
		}
	}

	return ret;
}

static int gfar_get_cls(struct gfar_private *priv, struct ethtool_rxnfc *cmd)
{
	struct ethtool_flow_spec_container *comp;
	u32 ret = -EINVAL;

	list_for_each_entry(comp, &priv->rx_list.list, list) {
		if (comp->fs.location == cmd->fs.location) {
			memcpy(&cmd->fs, &comp->fs, sizeof(cmd->fs));
			gfar_invert_masks(&cmd->fs);
			ret = 0;
			break;
		}
	}

	return ret;
}

static int gfar_get_cls_all(struct gfar_private *priv,
			    struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	struct ethtool_flow_spec_container *comp;
	u32 i = 0;

	list_for_each_entry(comp, &priv->rx_list.list, list) {
		if (i == cmd->rule_cnt)
			return -EMSGSIZE;
		rule_locs[i] = comp->fs.location;
		i++;
	}

	cmd->data = MAX_FILER_IDX;
	cmd->rule_cnt = i;

	return 0;
}

static int gfar_set_nfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct gfar_private *priv = netdev_priv(dev);
	int ret = 0;

	mutex_lock(&priv->rx_queue_access);

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		ret = gfar_set_hash_opts(priv, cmd);
		break;
	case ETHTOOL_SRXCLSRLINS:
		if ((cmd->fs.ring_cookie != RX_CLS_FLOW_DISC &&
		     cmd->fs.ring_cookie >= priv->num_rx_queues) ||
		    cmd->fs.location >= MAX_FILER_IDX) {
			ret = -EINVAL;
			break;
		}
		ret = gfar_add_cls(priv, &cmd->fs);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		ret = gfar_del_cls(priv, cmd->fs.location);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&priv->rx_queue_access);

	return ret;
}

static int gfar_get_nfc(struct net_device *dev, struct ethtool_rxnfc *cmd,
			u32 *rule_locs)
{
	struct gfar_private *priv = netdev_priv(dev);
	int ret = 0;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = priv->num_rx_queues;
		break;
	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = priv->rx_list.count;
		break;
	case ETHTOOL_GRXCLSRULE:
		ret = gfar_get_cls(priv, cmd);
		break;
	case ETHTOOL_GRXCLSRLALL:
		ret = gfar_get_cls_all(priv, cmd, rule_locs);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int gfar_phc_index = -1;
EXPORT_SYMBOL(gfar_phc_index);

static int gfar_get_ts_info(struct net_device *dev,
			    struct ethtool_ts_info *info)
{
	struct gfar_private *priv = netdev_priv(dev);

	if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_TIMER)) {
		info->so_timestamping = SOF_TIMESTAMPING_RX_SOFTWARE |
					SOF_TIMESTAMPING_SOFTWARE;
		info->phc_index = -1;
		return 0;
	}
	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;
	info->phc_index = gfar_phc_index;
	info->tx_types = (1 << HWTSTAMP_TX_OFF) |
			 (1 << HWTSTAMP_TX_ON);
	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_ALL);
	return 0;
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
	.get_msglevel = gfar_get_msglevel,
	.set_msglevel = gfar_set_msglevel,
#ifdef CONFIG_PM
	.get_wol = gfar_get_wol,
	.set_wol = gfar_set_wol,
#endif
	.set_rxnfc = gfar_set_nfc,
	.get_rxnfc = gfar_get_nfc,
	.get_ts_info = gfar_get_ts_info,
};
