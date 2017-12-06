/*
 * Broadcom BCM7xxx System Port Ethernet MAC driver
 *
 * Copyright (C) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <net/dsa.h>
#include <net/ip.h>
#include <net/ipv6.h>

#include "bcmsysport.h"

/* I/O accessors register helpers */
#define BCM_SYSPORT_IO_MACRO(name, offset) \
static inline u32 name##_readl(struct bcm_sysport_priv *priv, u32 off)	\
{									\
	u32 reg = readl_relaxed(priv->base + offset + off);		\
	return reg;							\
}									\
static inline void name##_writel(struct bcm_sysport_priv *priv,		\
				  u32 val, u32 off)			\
{									\
	writel_relaxed(val, priv->base + offset + off);			\
}									\

BCM_SYSPORT_IO_MACRO(intrl2_0, SYS_PORT_INTRL2_0_OFFSET);
BCM_SYSPORT_IO_MACRO(intrl2_1, SYS_PORT_INTRL2_1_OFFSET);
BCM_SYSPORT_IO_MACRO(umac, SYS_PORT_UMAC_OFFSET);
BCM_SYSPORT_IO_MACRO(gib, SYS_PORT_GIB_OFFSET);
BCM_SYSPORT_IO_MACRO(tdma, SYS_PORT_TDMA_OFFSET);
BCM_SYSPORT_IO_MACRO(rxchk, SYS_PORT_RXCHK_OFFSET);
BCM_SYSPORT_IO_MACRO(txchk, SYS_PORT_TXCHK_OFFSET);
BCM_SYSPORT_IO_MACRO(rbuf, SYS_PORT_RBUF_OFFSET);
BCM_SYSPORT_IO_MACRO(tbuf, SYS_PORT_TBUF_OFFSET);
BCM_SYSPORT_IO_MACRO(topctrl, SYS_PORT_TOPCTRL_OFFSET);

/* On SYSTEMPORT Lite, any register after RDMA_STATUS has the exact
 * same layout, except it has been moved by 4 bytes up, *sigh*
 */
static inline u32 rdma_readl(struct bcm_sysport_priv *priv, u32 off)
{
	if (priv->is_lite && off >= RDMA_STATUS)
		off += 4;
	return readl_relaxed(priv->base + SYS_PORT_RDMA_OFFSET + off);
}

static inline void rdma_writel(struct bcm_sysport_priv *priv, u32 val, u32 off)
{
	if (priv->is_lite && off >= RDMA_STATUS)
		off += 4;
	writel_relaxed(val, priv->base + SYS_PORT_RDMA_OFFSET + off);
}

static inline u32 tdma_control_bit(struct bcm_sysport_priv *priv, u32 bit)
{
	if (!priv->is_lite) {
		return BIT(bit);
	} else {
		if (bit >= ACB_ALGO)
			return BIT(bit + 1);
		else
			return BIT(bit);
	}
}

/* L2-interrupt masking/unmasking helpers, does automatic saving of the applied
 * mask in a software copy to avoid CPU_MASK_STATUS reads in hot-paths.
  */
#define BCM_SYSPORT_INTR_L2(which)	\
static inline void intrl2_##which##_mask_clear(struct bcm_sysport_priv *priv, \
						u32 mask)		\
{									\
	priv->irq##which##_mask &= ~(mask);				\
	intrl2_##which##_writel(priv, mask, INTRL2_CPU_MASK_CLEAR);	\
}									\
static inline void intrl2_##which##_mask_set(struct bcm_sysport_priv *priv, \
						u32 mask)		\
{									\
	intrl2_## which##_writel(priv, mask, INTRL2_CPU_MASK_SET);	\
	priv->irq##which##_mask |= (mask);				\
}									\

BCM_SYSPORT_INTR_L2(0)
BCM_SYSPORT_INTR_L2(1)

/* Register accesses to GISB/RBUS registers are expensive (few hundred
 * nanoseconds), so keep the check for 64-bits explicit here to save
 * one register write per-packet on 32-bits platforms.
 */
static inline void dma_desc_set_addr(struct bcm_sysport_priv *priv,
				     void __iomem *d,
				     dma_addr_t addr)
{
#ifdef CONFIG_PHYS_ADDR_T_64BIT
	writel_relaxed(upper_32_bits(addr) & DESC_ADDR_HI_MASK,
		     d + DESC_ADDR_HI_STATUS_LEN);
#endif
	writel_relaxed(lower_32_bits(addr), d + DESC_ADDR_LO);
}

static inline void tdma_port_write_desc_addr(struct bcm_sysport_priv *priv,
					     struct dma_desc *desc,
					     unsigned int port)
{
	/* Ports are latched, so write upper address first */
	tdma_writel(priv, desc->addr_status_len, TDMA_WRITE_PORT_HI(port));
	tdma_writel(priv, desc->addr_lo, TDMA_WRITE_PORT_LO(port));
}

/* Ethtool operations */
static int bcm_sysport_set_rx_csum(struct net_device *dev,
				   netdev_features_t wanted)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	u32 reg;

	priv->rx_chk_en = !!(wanted & NETIF_F_RXCSUM);
	reg = rxchk_readl(priv, RXCHK_CONTROL);
	if (priv->rx_chk_en)
		reg |= RXCHK_EN;
	else
		reg &= ~RXCHK_EN;

	/* If UniMAC forwards CRC, we need to skip over it to get
	 * a valid CHK bit to be set in the per-packet status word
	 */
	if (priv->rx_chk_en && priv->crc_fwd)
		reg |= RXCHK_SKIP_FCS;
	else
		reg &= ~RXCHK_SKIP_FCS;

	/* If Broadcom tags are enabled (e.g: using a switch), make
	 * sure we tell the RXCHK hardware to expect a 4-bytes Broadcom
	 * tag after the Ethernet MAC Source Address.
	 */
	if (netdev_uses_dsa(dev))
		reg |= RXCHK_BRCM_TAG_EN;
	else
		reg &= ~RXCHK_BRCM_TAG_EN;

	rxchk_writel(priv, reg, RXCHK_CONTROL);

	return 0;
}

static int bcm_sysport_set_tx_csum(struct net_device *dev,
				   netdev_features_t wanted)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	u32 reg;

	/* Hardware transmit checksum requires us to enable the Transmit status
	 * block prepended to the packet contents
	 */
	priv->tsb_en = !!(wanted & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM));
	reg = tdma_readl(priv, TDMA_CONTROL);
	if (priv->tsb_en)
		reg |= tdma_control_bit(priv, TSB_EN);
	else
		reg &= ~tdma_control_bit(priv, TSB_EN);
	tdma_writel(priv, reg, TDMA_CONTROL);

	return 0;
}

static int bcm_sysport_set_features(struct net_device *dev,
				    netdev_features_t features)
{
	netdev_features_t changed = features ^ dev->features;
	netdev_features_t wanted = dev->wanted_features;
	int ret = 0;

	if (changed & NETIF_F_RXCSUM)
		ret = bcm_sysport_set_rx_csum(dev, wanted);
	if (changed & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM))
		ret = bcm_sysport_set_tx_csum(dev, wanted);

	return ret;
}

/* Hardware counters must be kept in sync because the order/offset
 * is important here (order in structure declaration = order in hardware)
 */
static const struct bcm_sysport_stats bcm_sysport_gstrings_stats[] = {
	/* general stats */
	STAT_NETDEV64(rx_packets),
	STAT_NETDEV64(tx_packets),
	STAT_NETDEV64(rx_bytes),
	STAT_NETDEV64(tx_bytes),
	STAT_NETDEV(rx_errors),
	STAT_NETDEV(tx_errors),
	STAT_NETDEV(rx_dropped),
	STAT_NETDEV(tx_dropped),
	STAT_NETDEV(multicast),
	/* UniMAC RSV counters */
	STAT_MIB_RX("rx_64_octets", mib.rx.pkt_cnt.cnt_64),
	STAT_MIB_RX("rx_65_127_oct", mib.rx.pkt_cnt.cnt_127),
	STAT_MIB_RX("rx_128_255_oct", mib.rx.pkt_cnt.cnt_255),
	STAT_MIB_RX("rx_256_511_oct", mib.rx.pkt_cnt.cnt_511),
	STAT_MIB_RX("rx_512_1023_oct", mib.rx.pkt_cnt.cnt_1023),
	STAT_MIB_RX("rx_1024_1518_oct", mib.rx.pkt_cnt.cnt_1518),
	STAT_MIB_RX("rx_vlan_1519_1522_oct", mib.rx.pkt_cnt.cnt_mgv),
	STAT_MIB_RX("rx_1522_2047_oct", mib.rx.pkt_cnt.cnt_2047),
	STAT_MIB_RX("rx_2048_4095_oct", mib.rx.pkt_cnt.cnt_4095),
	STAT_MIB_RX("rx_4096_9216_oct", mib.rx.pkt_cnt.cnt_9216),
	STAT_MIB_RX("rx_pkts", mib.rx.pkt),
	STAT_MIB_RX("rx_bytes", mib.rx.bytes),
	STAT_MIB_RX("rx_multicast", mib.rx.mca),
	STAT_MIB_RX("rx_broadcast", mib.rx.bca),
	STAT_MIB_RX("rx_fcs", mib.rx.fcs),
	STAT_MIB_RX("rx_control", mib.rx.cf),
	STAT_MIB_RX("rx_pause", mib.rx.pf),
	STAT_MIB_RX("rx_unknown", mib.rx.uo),
	STAT_MIB_RX("rx_align", mib.rx.aln),
	STAT_MIB_RX("rx_outrange", mib.rx.flr),
	STAT_MIB_RX("rx_code", mib.rx.cde),
	STAT_MIB_RX("rx_carrier", mib.rx.fcr),
	STAT_MIB_RX("rx_oversize", mib.rx.ovr),
	STAT_MIB_RX("rx_jabber", mib.rx.jbr),
	STAT_MIB_RX("rx_mtu_err", mib.rx.mtue),
	STAT_MIB_RX("rx_good_pkts", mib.rx.pok),
	STAT_MIB_RX("rx_unicast", mib.rx.uc),
	STAT_MIB_RX("rx_ppp", mib.rx.ppp),
	STAT_MIB_RX("rx_crc", mib.rx.rcrc),
	/* UniMAC TSV counters */
	STAT_MIB_TX("tx_64_octets", mib.tx.pkt_cnt.cnt_64),
	STAT_MIB_TX("tx_65_127_oct", mib.tx.pkt_cnt.cnt_127),
	STAT_MIB_TX("tx_128_255_oct", mib.tx.pkt_cnt.cnt_255),
	STAT_MIB_TX("tx_256_511_oct", mib.tx.pkt_cnt.cnt_511),
	STAT_MIB_TX("tx_512_1023_oct", mib.tx.pkt_cnt.cnt_1023),
	STAT_MIB_TX("tx_1024_1518_oct", mib.tx.pkt_cnt.cnt_1518),
	STAT_MIB_TX("tx_vlan_1519_1522_oct", mib.tx.pkt_cnt.cnt_mgv),
	STAT_MIB_TX("tx_1522_2047_oct", mib.tx.pkt_cnt.cnt_2047),
	STAT_MIB_TX("tx_2048_4095_oct", mib.tx.pkt_cnt.cnt_4095),
	STAT_MIB_TX("tx_4096_9216_oct", mib.tx.pkt_cnt.cnt_9216),
	STAT_MIB_TX("tx_pkts", mib.tx.pkts),
	STAT_MIB_TX("tx_multicast", mib.tx.mca),
	STAT_MIB_TX("tx_broadcast", mib.tx.bca),
	STAT_MIB_TX("tx_pause", mib.tx.pf),
	STAT_MIB_TX("tx_control", mib.tx.cf),
	STAT_MIB_TX("tx_fcs_err", mib.tx.fcs),
	STAT_MIB_TX("tx_oversize", mib.tx.ovr),
	STAT_MIB_TX("tx_defer", mib.tx.drf),
	STAT_MIB_TX("tx_excess_defer", mib.tx.edf),
	STAT_MIB_TX("tx_single_col", mib.tx.scl),
	STAT_MIB_TX("tx_multi_col", mib.tx.mcl),
	STAT_MIB_TX("tx_late_col", mib.tx.lcl),
	STAT_MIB_TX("tx_excess_col", mib.tx.ecl),
	STAT_MIB_TX("tx_frags", mib.tx.frg),
	STAT_MIB_TX("tx_total_col", mib.tx.ncl),
	STAT_MIB_TX("tx_jabber", mib.tx.jbr),
	STAT_MIB_TX("tx_bytes", mib.tx.bytes),
	STAT_MIB_TX("tx_good_pkts", mib.tx.pok),
	STAT_MIB_TX("tx_unicast", mib.tx.uc),
	/* UniMAC RUNT counters */
	STAT_RUNT("rx_runt_pkts", mib.rx_runt_cnt),
	STAT_RUNT("rx_runt_valid_fcs", mib.rx_runt_fcs),
	STAT_RUNT("rx_runt_inval_fcs_align", mib.rx_runt_fcs_align),
	STAT_RUNT("rx_runt_bytes", mib.rx_runt_bytes),
	/* RXCHK misc statistics */
	STAT_RXCHK("rxchk_bad_csum", mib.rxchk_bad_csum, RXCHK_BAD_CSUM_CNTR),
	STAT_RXCHK("rxchk_other_pkt_disc", mib.rxchk_other_pkt_disc,
		   RXCHK_OTHER_DISC_CNTR),
	/* RBUF misc statistics */
	STAT_RBUF("rbuf_ovflow_cnt", mib.rbuf_ovflow_cnt, RBUF_OVFL_DISC_CNTR),
	STAT_RBUF("rbuf_err_cnt", mib.rbuf_err_cnt, RBUF_ERR_PKT_CNTR),
	STAT_MIB_SOFT("alloc_rx_buff_failed", mib.alloc_rx_buff_failed),
	STAT_MIB_SOFT("rx_dma_failed", mib.rx_dma_failed),
	STAT_MIB_SOFT("tx_dma_failed", mib.tx_dma_failed),
	/* Per TX-queue statistics are dynamically appended */
};

#define BCM_SYSPORT_STATS_LEN	ARRAY_SIZE(bcm_sysport_gstrings_stats)

static void bcm_sysport_get_drvinfo(struct net_device *dev,
				    struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strlcpy(info->version, "0.1", sizeof(info->version));
	strlcpy(info->bus_info, "platform", sizeof(info->bus_info));
}

static u32 bcm_sysport_get_msglvl(struct net_device *dev)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);

	return priv->msg_enable;
}

static void bcm_sysport_set_msglvl(struct net_device *dev, u32 enable)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);

	priv->msg_enable = enable;
}

static inline bool bcm_sysport_lite_stat_valid(enum bcm_sysport_stat_type type)
{
	switch (type) {
	case BCM_SYSPORT_STAT_NETDEV:
	case BCM_SYSPORT_STAT_NETDEV64:
	case BCM_SYSPORT_STAT_RXCHK:
	case BCM_SYSPORT_STAT_RBUF:
	case BCM_SYSPORT_STAT_SOFT:
		return true;
	default:
		return false;
	}
}

static int bcm_sysport_get_sset_count(struct net_device *dev, int string_set)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	const struct bcm_sysport_stats *s;
	unsigned int i, j;

	switch (string_set) {
	case ETH_SS_STATS:
		for (i = 0, j = 0; i < BCM_SYSPORT_STATS_LEN; i++) {
			s = &bcm_sysport_gstrings_stats[i];
			if (priv->is_lite &&
			    !bcm_sysport_lite_stat_valid(s->type))
				continue;
			j++;
		}
		/* Include per-queue statistics */
		return j + dev->num_tx_queues * NUM_SYSPORT_TXQ_STAT;
	default:
		return -EOPNOTSUPP;
	}
}

static void bcm_sysport_get_strings(struct net_device *dev,
				    u32 stringset, u8 *data)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	const struct bcm_sysport_stats *s;
	char buf[128];
	int i, j;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0, j = 0; i < BCM_SYSPORT_STATS_LEN; i++) {
			s = &bcm_sysport_gstrings_stats[i];
			if (priv->is_lite &&
			    !bcm_sysport_lite_stat_valid(s->type))
				continue;

			memcpy(data + j * ETH_GSTRING_LEN, s->stat_string,
			       ETH_GSTRING_LEN);
			j++;
		}

		for (i = 0; i < dev->num_tx_queues; i++) {
			snprintf(buf, sizeof(buf), "txq%d_packets", i);
			memcpy(data + j * ETH_GSTRING_LEN, buf,
			       ETH_GSTRING_LEN);
			j++;

			snprintf(buf, sizeof(buf), "txq%d_bytes", i);
			memcpy(data + j * ETH_GSTRING_LEN, buf,
			       ETH_GSTRING_LEN);
			j++;
		}
		break;
	default:
		break;
	}
}

static void bcm_sysport_update_mib_counters(struct bcm_sysport_priv *priv)
{
	int i, j = 0;

	for (i = 0; i < BCM_SYSPORT_STATS_LEN; i++) {
		const struct bcm_sysport_stats *s;
		u8 offset = 0;
		u32 val = 0;
		char *p;

		s = &bcm_sysport_gstrings_stats[i];
		switch (s->type) {
		case BCM_SYSPORT_STAT_NETDEV:
		case BCM_SYSPORT_STAT_NETDEV64:
		case BCM_SYSPORT_STAT_SOFT:
			continue;
		case BCM_SYSPORT_STAT_MIB_RX:
		case BCM_SYSPORT_STAT_MIB_TX:
		case BCM_SYSPORT_STAT_RUNT:
			if (priv->is_lite)
				continue;

			if (s->type != BCM_SYSPORT_STAT_MIB_RX)
				offset = UMAC_MIB_STAT_OFFSET;
			val = umac_readl(priv, UMAC_MIB_START + j + offset);
			break;
		case BCM_SYSPORT_STAT_RXCHK:
			val = rxchk_readl(priv, s->reg_offset);
			if (val == ~0)
				rxchk_writel(priv, 0, s->reg_offset);
			break;
		case BCM_SYSPORT_STAT_RBUF:
			val = rbuf_readl(priv, s->reg_offset);
			if (val == ~0)
				rbuf_writel(priv, 0, s->reg_offset);
			break;
		}

		j += s->stat_sizeof;
		p = (char *)priv + s->stat_offset;
		*(u32 *)p = val;
	}

	netif_dbg(priv, hw, priv->netdev, "updated MIB counters\n");
}

static void bcm_sysport_update_tx_stats(struct bcm_sysport_priv *priv,
					u64 *tx_bytes, u64 *tx_packets)
{
	struct bcm_sysport_tx_ring *ring;
	u64 bytes = 0, packets = 0;
	unsigned int start;
	unsigned int q;

	for (q = 0; q < priv->netdev->num_tx_queues; q++) {
		ring = &priv->tx_rings[q];
		do {
			start = u64_stats_fetch_begin_irq(&priv->syncp);
			bytes = ring->bytes;
			packets = ring->packets;
		} while (u64_stats_fetch_retry_irq(&priv->syncp, start));

		*tx_bytes += bytes;
		*tx_packets += packets;
	}
}

static void bcm_sysport_get_stats(struct net_device *dev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	struct bcm_sysport_stats64 *stats64 = &priv->stats64;
	struct u64_stats_sync *syncp = &priv->syncp;
	struct bcm_sysport_tx_ring *ring;
	u64 tx_bytes = 0, tx_packets = 0;
	unsigned int start;
	int i, j;

	if (netif_running(dev)) {
		bcm_sysport_update_mib_counters(priv);
		bcm_sysport_update_tx_stats(priv, &tx_bytes, &tx_packets);
		stats64->tx_bytes = tx_bytes;
		stats64->tx_packets = tx_packets;
	}

	for (i =  0, j = 0; i < BCM_SYSPORT_STATS_LEN; i++) {
		const struct bcm_sysport_stats *s;
		char *p;

		s = &bcm_sysport_gstrings_stats[i];
		if (s->type == BCM_SYSPORT_STAT_NETDEV)
			p = (char *)&dev->stats;
		else if (s->type == BCM_SYSPORT_STAT_NETDEV64)
			p = (char *)stats64;
		else
			p = (char *)priv;

		if (priv->is_lite && !bcm_sysport_lite_stat_valid(s->type))
			continue;
		p += s->stat_offset;

		if (s->stat_sizeof == sizeof(u64) &&
		    s->type == BCM_SYSPORT_STAT_NETDEV64) {
			do {
				start = u64_stats_fetch_begin_irq(syncp);
				data[i] = *(u64 *)p;
			} while (u64_stats_fetch_retry_irq(syncp, start));
		} else
			data[i] = *(u32 *)p;
		j++;
	}

	/* For SYSTEMPORT Lite since we have holes in our statistics, j would
	 * be equal to BCM_SYSPORT_STATS_LEN at the end of the loop, but it
	 * needs to point to how many total statistics we have minus the
	 * number of per TX queue statistics
	 */
	j = bcm_sysport_get_sset_count(dev, ETH_SS_STATS) -
	    dev->num_tx_queues * NUM_SYSPORT_TXQ_STAT;

	for (i = 0; i < dev->num_tx_queues; i++) {
		ring = &priv->tx_rings[i];
		data[j] = ring->packets;
		j++;
		data[j] = ring->bytes;
		j++;
	}
}

static void bcm_sysport_get_wol(struct net_device *dev,
				struct ethtool_wolinfo *wol)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	u32 reg;

	wol->supported = WAKE_MAGIC | WAKE_MAGICSECURE;
	wol->wolopts = priv->wolopts;

	if (!(priv->wolopts & WAKE_MAGICSECURE))
		return;

	/* Return the programmed SecureOn password */
	reg = umac_readl(priv, UMAC_PSW_MS);
	put_unaligned_be16(reg, &wol->sopass[0]);
	reg = umac_readl(priv, UMAC_PSW_LS);
	put_unaligned_be32(reg, &wol->sopass[2]);
}

static int bcm_sysport_set_wol(struct net_device *dev,
			       struct ethtool_wolinfo *wol)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	struct device *kdev = &priv->pdev->dev;
	u32 supported = WAKE_MAGIC | WAKE_MAGICSECURE;

	if (!device_can_wakeup(kdev))
		return -ENOTSUPP;

	if (wol->wolopts & ~supported)
		return -EINVAL;

	/* Program the SecureOn password */
	if (wol->wolopts & WAKE_MAGICSECURE) {
		umac_writel(priv, get_unaligned_be16(&wol->sopass[0]),
			    UMAC_PSW_MS);
		umac_writel(priv, get_unaligned_be32(&wol->sopass[2]),
			    UMAC_PSW_LS);
	}

	/* Flag the device and relevant IRQ as wakeup capable */
	if (wol->wolopts) {
		device_set_wakeup_enable(kdev, 1);
		if (priv->wol_irq_disabled)
			enable_irq_wake(priv->wol_irq);
		priv->wol_irq_disabled = 0;
	} else {
		device_set_wakeup_enable(kdev, 0);
		/* Avoid unbalanced disable_irq_wake calls */
		if (!priv->wol_irq_disabled)
			disable_irq_wake(priv->wol_irq);
		priv->wol_irq_disabled = 1;
	}

	priv->wolopts = wol->wolopts;

	return 0;
}

static int bcm_sysport_get_coalesce(struct net_device *dev,
				    struct ethtool_coalesce *ec)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	u32 reg;

	reg = tdma_readl(priv, TDMA_DESC_RING_INTR_CONTROL(0));

	ec->tx_coalesce_usecs = (reg >> RING_TIMEOUT_SHIFT) * 8192 / 1000;
	ec->tx_max_coalesced_frames = reg & RING_INTR_THRESH_MASK;

	reg = rdma_readl(priv, RDMA_MBDONE_INTR);

	ec->rx_coalesce_usecs = (reg >> RDMA_TIMEOUT_SHIFT) * 8192 / 1000;
	ec->rx_max_coalesced_frames = reg & RDMA_INTR_THRESH_MASK;

	return 0;
}

static int bcm_sysport_set_coalesce(struct net_device *dev,
				    struct ethtool_coalesce *ec)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	unsigned int i;
	u32 reg;

	/* Base system clock is 125Mhz, DMA timeout is this reference clock
	 * divided by 1024, which yield roughly 8.192 us, our maximum value has
	 * to fit in the RING_TIMEOUT_MASK (16 bits).
	 */
	if (ec->tx_max_coalesced_frames > RING_INTR_THRESH_MASK ||
	    ec->tx_coalesce_usecs > (RING_TIMEOUT_MASK * 8) + 1 ||
	    ec->rx_max_coalesced_frames > RDMA_INTR_THRESH_MASK ||
	    ec->rx_coalesce_usecs > (RDMA_TIMEOUT_MASK * 8) + 1)
		return -EINVAL;

	if ((ec->tx_coalesce_usecs == 0 && ec->tx_max_coalesced_frames == 0) ||
	    (ec->rx_coalesce_usecs == 0 && ec->rx_max_coalesced_frames == 0))
		return -EINVAL;

	for (i = 0; i < dev->num_tx_queues; i++) {
		reg = tdma_readl(priv, TDMA_DESC_RING_INTR_CONTROL(i));
		reg &= ~(RING_INTR_THRESH_MASK |
			 RING_TIMEOUT_MASK << RING_TIMEOUT_SHIFT);
		reg |= ec->tx_max_coalesced_frames;
		reg |= DIV_ROUND_UP(ec->tx_coalesce_usecs * 1000, 8192) <<
			 RING_TIMEOUT_SHIFT;
		tdma_writel(priv, reg, TDMA_DESC_RING_INTR_CONTROL(i));
	}

	reg = rdma_readl(priv, RDMA_MBDONE_INTR);
	reg &= ~(RDMA_INTR_THRESH_MASK |
		 RDMA_TIMEOUT_MASK << RDMA_TIMEOUT_SHIFT);
	reg |= ec->rx_max_coalesced_frames;
	reg |= DIV_ROUND_UP(ec->rx_coalesce_usecs * 1000, 8192) <<
			    RDMA_TIMEOUT_SHIFT;
	rdma_writel(priv, reg, RDMA_MBDONE_INTR);

	return 0;
}

static void bcm_sysport_free_cb(struct bcm_sysport_cb *cb)
{
	dev_consume_skb_any(cb->skb);
	cb->skb = NULL;
	dma_unmap_addr_set(cb, dma_addr, 0);
}

static struct sk_buff *bcm_sysport_rx_refill(struct bcm_sysport_priv *priv,
					     struct bcm_sysport_cb *cb)
{
	struct device *kdev = &priv->pdev->dev;
	struct net_device *ndev = priv->netdev;
	struct sk_buff *skb, *rx_skb;
	dma_addr_t mapping;

	/* Allocate a new SKB for a new packet */
	skb = netdev_alloc_skb(priv->netdev, RX_BUF_LENGTH);
	if (!skb) {
		priv->mib.alloc_rx_buff_failed++;
		netif_err(priv, rx_err, ndev, "SKB alloc failed\n");
		return NULL;
	}

	mapping = dma_map_single(kdev, skb->data,
				 RX_BUF_LENGTH, DMA_FROM_DEVICE);
	if (dma_mapping_error(kdev, mapping)) {
		priv->mib.rx_dma_failed++;
		dev_kfree_skb_any(skb);
		netif_err(priv, rx_err, ndev, "DMA mapping failure\n");
		return NULL;
	}

	/* Grab the current SKB on the ring */
	rx_skb = cb->skb;
	if (likely(rx_skb))
		dma_unmap_single(kdev, dma_unmap_addr(cb, dma_addr),
				 RX_BUF_LENGTH, DMA_FROM_DEVICE);

	/* Put the new SKB on the ring */
	cb->skb = skb;
	dma_unmap_addr_set(cb, dma_addr, mapping);
	dma_desc_set_addr(priv, cb->bd_addr, mapping);

	netif_dbg(priv, rx_status, ndev, "RX refill\n");

	/* Return the current SKB to the caller */
	return rx_skb;
}

static int bcm_sysport_alloc_rx_bufs(struct bcm_sysport_priv *priv)
{
	struct bcm_sysport_cb *cb;
	struct sk_buff *skb;
	unsigned int i;

	for (i = 0; i < priv->num_rx_bds; i++) {
		cb = &priv->rx_cbs[i];
		skb = bcm_sysport_rx_refill(priv, cb);
		if (skb)
			dev_kfree_skb(skb);
		if (!cb->skb)
			return -ENOMEM;
	}

	return 0;
}

/* Poll the hardware for up to budget packets to process */
static unsigned int bcm_sysport_desc_rx(struct bcm_sysport_priv *priv,
					unsigned int budget)
{
	struct bcm_sysport_stats64 *stats64 = &priv->stats64;
	struct net_device *ndev = priv->netdev;
	unsigned int processed = 0, to_process;
	struct bcm_sysport_cb *cb;
	struct sk_buff *skb;
	unsigned int p_index;
	u16 len, status;
	struct bcm_rsb *rsb;

	/* Clear status before servicing to reduce spurious interrupts */
	intrl2_0_writel(priv, INTRL2_0_RDMA_MBDONE, INTRL2_CPU_CLEAR);

	/* Determine how much we should process since last call, SYSTEMPORT Lite
	 * groups the producer and consumer indexes into the same 32-bit
	 * which we access using RDMA_CONS_INDEX
	 */
	if (!priv->is_lite)
		p_index = rdma_readl(priv, RDMA_PROD_INDEX);
	else
		p_index = rdma_readl(priv, RDMA_CONS_INDEX);
	p_index &= RDMA_PROD_INDEX_MASK;

	to_process = (p_index - priv->rx_c_index) & RDMA_CONS_INDEX_MASK;

	netif_dbg(priv, rx_status, ndev,
		  "p_index=%d rx_c_index=%d to_process=%d\n",
		  p_index, priv->rx_c_index, to_process);

	while ((processed < to_process) && (processed < budget)) {
		cb = &priv->rx_cbs[priv->rx_read_ptr];
		skb = bcm_sysport_rx_refill(priv, cb);


		/* We do not have a backing SKB, so we do not a corresponding
		 * DMA mapping for this incoming packet since
		 * bcm_sysport_rx_refill always either has both skb and mapping
		 * or none.
		 */
		if (unlikely(!skb)) {
			netif_err(priv, rx_err, ndev, "out of memory!\n");
			ndev->stats.rx_dropped++;
			ndev->stats.rx_errors++;
			goto next;
		}

		/* Extract the Receive Status Block prepended */
		rsb = (struct bcm_rsb *)skb->data;
		len = (rsb->rx_status_len >> DESC_LEN_SHIFT) & DESC_LEN_MASK;
		status = (rsb->rx_status_len >> DESC_STATUS_SHIFT) &
			  DESC_STATUS_MASK;

		netif_dbg(priv, rx_status, ndev,
			  "p=%d, c=%d, rd_ptr=%d, len=%d, flag=0x%04x\n",
			  p_index, priv->rx_c_index, priv->rx_read_ptr,
			  len, status);

		if (unlikely(len > RX_BUF_LENGTH)) {
			netif_err(priv, rx_status, ndev, "oversized packet\n");
			ndev->stats.rx_length_errors++;
			ndev->stats.rx_errors++;
			dev_kfree_skb_any(skb);
			goto next;
		}

		if (unlikely(!(status & DESC_EOP) || !(status & DESC_SOP))) {
			netif_err(priv, rx_status, ndev, "fragmented packet!\n");
			ndev->stats.rx_dropped++;
			ndev->stats.rx_errors++;
			dev_kfree_skb_any(skb);
			goto next;
		}

		if (unlikely(status & (RX_STATUS_ERR | RX_STATUS_OVFLOW))) {
			netif_err(priv, rx_err, ndev, "error packet\n");
			if (status & RX_STATUS_OVFLOW)
				ndev->stats.rx_over_errors++;
			ndev->stats.rx_dropped++;
			ndev->stats.rx_errors++;
			dev_kfree_skb_any(skb);
			goto next;
		}

		skb_put(skb, len);

		/* Hardware validated our checksum */
		if (likely(status & DESC_L4_CSUM))
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		/* Hardware pre-pends packets with 2bytes before Ethernet
		 * header plus we have the Receive Status Block, strip off all
		 * of this from the SKB.
		 */
		skb_pull(skb, sizeof(*rsb) + 2);
		len -= (sizeof(*rsb) + 2);

		/* UniMAC may forward CRC */
		if (priv->crc_fwd) {
			skb_trim(skb, len - ETH_FCS_LEN);
			len -= ETH_FCS_LEN;
		}

		skb->protocol = eth_type_trans(skb, ndev);
		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += len;
		u64_stats_update_begin(&priv->syncp);
		stats64->rx_packets++;
		stats64->rx_bytes += len;
		u64_stats_update_end(&priv->syncp);

		napi_gro_receive(&priv->napi, skb);
next:
		processed++;
		priv->rx_read_ptr++;

		if (priv->rx_read_ptr == priv->num_rx_bds)
			priv->rx_read_ptr = 0;
	}

	return processed;
}

static void bcm_sysport_tx_reclaim_one(struct bcm_sysport_tx_ring *ring,
				       struct bcm_sysport_cb *cb,
				       unsigned int *bytes_compl,
				       unsigned int *pkts_compl)
{
	struct bcm_sysport_priv *priv = ring->priv;
	struct device *kdev = &priv->pdev->dev;

	if (cb->skb) {
		*bytes_compl += cb->skb->len;
		dma_unmap_single(kdev, dma_unmap_addr(cb, dma_addr),
				 dma_unmap_len(cb, dma_len),
				 DMA_TO_DEVICE);
		(*pkts_compl)++;
		bcm_sysport_free_cb(cb);
	/* SKB fragment */
	} else if (dma_unmap_addr(cb, dma_addr)) {
		*bytes_compl += dma_unmap_len(cb, dma_len);
		dma_unmap_page(kdev, dma_unmap_addr(cb, dma_addr),
			       dma_unmap_len(cb, dma_len), DMA_TO_DEVICE);
		dma_unmap_addr_set(cb, dma_addr, 0);
	}
}

/* Reclaim queued SKBs for transmission completion, lockless version */
static unsigned int __bcm_sysport_tx_reclaim(struct bcm_sysport_priv *priv,
					     struct bcm_sysport_tx_ring *ring)
{
	unsigned int c_index, last_c_index, last_tx_cn, num_tx_cbs;
	unsigned int pkts_compl = 0, bytes_compl = 0;
	struct net_device *ndev = priv->netdev;
	struct bcm_sysport_cb *cb;
	u32 hw_ind;

	/* Clear status before servicing to reduce spurious interrupts */
	if (!ring->priv->is_lite)
		intrl2_1_writel(ring->priv, BIT(ring->index), INTRL2_CPU_CLEAR);
	else
		intrl2_0_writel(ring->priv, BIT(ring->index +
				INTRL2_0_TDMA_MBDONE_SHIFT), INTRL2_CPU_CLEAR);

	/* Compute how many descriptors have been processed since last call */
	hw_ind = tdma_readl(priv, TDMA_DESC_RING_PROD_CONS_INDEX(ring->index));
	c_index = (hw_ind >> RING_CONS_INDEX_SHIFT) & RING_CONS_INDEX_MASK;
	ring->p_index = (hw_ind & RING_PROD_INDEX_MASK);

	last_c_index = ring->c_index;
	num_tx_cbs = ring->size;

	c_index &= (num_tx_cbs - 1);

	if (c_index >= last_c_index)
		last_tx_cn = c_index - last_c_index;
	else
		last_tx_cn = num_tx_cbs - last_c_index + c_index;

	netif_dbg(priv, tx_done, ndev,
		  "ring=%d c_index=%d last_tx_cn=%d last_c_index=%d\n",
		  ring->index, c_index, last_tx_cn, last_c_index);

	while (last_tx_cn-- > 0) {
		cb = ring->cbs + last_c_index;
		bcm_sysport_tx_reclaim_one(ring, cb, &bytes_compl, &pkts_compl);

		ring->desc_count++;
		last_c_index++;
		last_c_index &= (num_tx_cbs - 1);
	}

	u64_stats_update_begin(&priv->syncp);
	ring->packets += pkts_compl;
	ring->bytes += bytes_compl;
	u64_stats_update_end(&priv->syncp);

	ring->c_index = c_index;

	netif_dbg(priv, tx_done, ndev,
		  "ring=%d c_index=%d pkts_compl=%d, bytes_compl=%d\n",
		  ring->index, ring->c_index, pkts_compl, bytes_compl);

	return pkts_compl;
}

/* Locked version of the per-ring TX reclaim routine */
static unsigned int bcm_sysport_tx_reclaim(struct bcm_sysport_priv *priv,
					   struct bcm_sysport_tx_ring *ring)
{
	struct netdev_queue *txq;
	unsigned int released;
	unsigned long flags;

	txq = netdev_get_tx_queue(priv->netdev, ring->index);

	spin_lock_irqsave(&ring->lock, flags);
	released = __bcm_sysport_tx_reclaim(priv, ring);
	if (released)
		netif_tx_wake_queue(txq);

	spin_unlock_irqrestore(&ring->lock, flags);

	return released;
}

/* Locked version of the per-ring TX reclaim, but does not wake the queue */
static void bcm_sysport_tx_clean(struct bcm_sysport_priv *priv,
				 struct bcm_sysport_tx_ring *ring)
{
	unsigned long flags;

	spin_lock_irqsave(&ring->lock, flags);
	__bcm_sysport_tx_reclaim(priv, ring);
	spin_unlock_irqrestore(&ring->lock, flags);
}

static int bcm_sysport_tx_poll(struct napi_struct *napi, int budget)
{
	struct bcm_sysport_tx_ring *ring =
		container_of(napi, struct bcm_sysport_tx_ring, napi);
	unsigned int work_done = 0;

	work_done = bcm_sysport_tx_reclaim(ring->priv, ring);

	if (work_done == 0) {
		napi_complete(napi);
		/* re-enable TX interrupt */
		if (!ring->priv->is_lite)
			intrl2_1_mask_clear(ring->priv, BIT(ring->index));
		else
			intrl2_0_mask_clear(ring->priv, BIT(ring->index +
					    INTRL2_0_TDMA_MBDONE_SHIFT));

		return 0;
	}

	return budget;
}

static void bcm_sysport_tx_reclaim_all(struct bcm_sysport_priv *priv)
{
	unsigned int q;

	for (q = 0; q < priv->netdev->num_tx_queues; q++)
		bcm_sysport_tx_reclaim(priv, &priv->tx_rings[q]);
}

static int bcm_sysport_poll(struct napi_struct *napi, int budget)
{
	struct bcm_sysport_priv *priv =
		container_of(napi, struct bcm_sysport_priv, napi);
	unsigned int work_done = 0;

	work_done = bcm_sysport_desc_rx(priv, budget);

	priv->rx_c_index += work_done;
	priv->rx_c_index &= RDMA_CONS_INDEX_MASK;

	/* SYSTEMPORT Lite groups the producer/consumer index, producer is
	 * maintained by HW, but writes to it will be ignore while RDMA
	 * is active
	 */
	if (!priv->is_lite)
		rdma_writel(priv, priv->rx_c_index, RDMA_CONS_INDEX);
	else
		rdma_writel(priv, priv->rx_c_index << 16, RDMA_CONS_INDEX);

	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		/* re-enable RX interrupts */
		intrl2_0_mask_clear(priv, INTRL2_0_RDMA_MBDONE);
	}

	return work_done;
}

static void bcm_sysport_resume_from_wol(struct bcm_sysport_priv *priv)
{
	u32 reg;

	/* Stop monitoring MPD interrupt */
	intrl2_0_mask_set(priv, INTRL2_0_MPD);

	/* Clear the MagicPacket detection logic */
	reg = umac_readl(priv, UMAC_MPD_CTRL);
	reg &= ~MPD_EN;
	umac_writel(priv, reg, UMAC_MPD_CTRL);

	netif_dbg(priv, wol, priv->netdev, "resumed from WOL\n");
}

/* RX and misc interrupt routine */
static irqreturn_t bcm_sysport_rx_isr(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	struct bcm_sysport_tx_ring *txr;
	unsigned int ring, ring_bit;

	priv->irq0_stat = intrl2_0_readl(priv, INTRL2_CPU_STATUS) &
			  ~intrl2_0_readl(priv, INTRL2_CPU_MASK_STATUS);
	intrl2_0_writel(priv, priv->irq0_stat, INTRL2_CPU_CLEAR);

	if (unlikely(priv->irq0_stat == 0)) {
		netdev_warn(priv->netdev, "spurious RX interrupt\n");
		return IRQ_NONE;
	}

	if (priv->irq0_stat & INTRL2_0_RDMA_MBDONE) {
		if (likely(napi_schedule_prep(&priv->napi))) {
			/* disable RX interrupts */
			intrl2_0_mask_set(priv, INTRL2_0_RDMA_MBDONE);
			__napi_schedule_irqoff(&priv->napi);
		}
	}

	/* TX ring is full, perform a full reclaim since we do not know
	 * which one would trigger this interrupt
	 */
	if (priv->irq0_stat & INTRL2_0_TX_RING_FULL)
		bcm_sysport_tx_reclaim_all(priv);

	if (priv->irq0_stat & INTRL2_0_MPD) {
		netdev_info(priv->netdev, "Wake-on-LAN interrupt!\n");
		bcm_sysport_resume_from_wol(priv);
	}

	if (!priv->is_lite)
		goto out;

	for (ring = 0; ring < dev->num_tx_queues; ring++) {
		ring_bit = BIT(ring + INTRL2_0_TDMA_MBDONE_SHIFT);
		if (!(priv->irq0_stat & ring_bit))
			continue;

		txr = &priv->tx_rings[ring];

		if (likely(napi_schedule_prep(&txr->napi))) {
			intrl2_0_mask_set(priv, ring_bit);
			__napi_schedule(&txr->napi);
		}
	}
out:
	return IRQ_HANDLED;
}

/* TX interrupt service routine */
static irqreturn_t bcm_sysport_tx_isr(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	struct bcm_sysport_tx_ring *txr;
	unsigned int ring;

	priv->irq1_stat = intrl2_1_readl(priv, INTRL2_CPU_STATUS) &
				~intrl2_1_readl(priv, INTRL2_CPU_MASK_STATUS);
	intrl2_1_writel(priv, 0xffffffff, INTRL2_CPU_CLEAR);

	if (unlikely(priv->irq1_stat == 0)) {
		netdev_warn(priv->netdev, "spurious TX interrupt\n");
		return IRQ_NONE;
	}

	for (ring = 0; ring < dev->num_tx_queues; ring++) {
		if (!(priv->irq1_stat & BIT(ring)))
			continue;

		txr = &priv->tx_rings[ring];

		if (likely(napi_schedule_prep(&txr->napi))) {
			intrl2_1_mask_set(priv, BIT(ring));
			__napi_schedule_irqoff(&txr->napi);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t bcm_sysport_wol_isr(int irq, void *dev_id)
{
	struct bcm_sysport_priv *priv = dev_id;

	pm_wakeup_event(&priv->pdev->dev, 0);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void bcm_sysport_poll_controller(struct net_device *dev)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);

	disable_irq(priv->irq0);
	bcm_sysport_rx_isr(priv->irq0, priv);
	enable_irq(priv->irq0);

	if (!priv->is_lite) {
		disable_irq(priv->irq1);
		bcm_sysport_tx_isr(priv->irq1, priv);
		enable_irq(priv->irq1);
	}
}
#endif

static struct sk_buff *bcm_sysport_insert_tsb(struct sk_buff *skb,
					      struct net_device *dev)
{
	struct sk_buff *nskb;
	struct bcm_tsb *tsb;
	u32 csum_info;
	u8 ip_proto;
	u16 csum_start;
	u16 ip_ver;

	/* Re-allocate SKB if needed */
	if (unlikely(skb_headroom(skb) < sizeof(*tsb))) {
		nskb = skb_realloc_headroom(skb, sizeof(*tsb));
		dev_kfree_skb(skb);
		if (!nskb) {
			dev->stats.tx_errors++;
			dev->stats.tx_dropped++;
			return NULL;
		}
		skb = nskb;
	}

	tsb = skb_push(skb, sizeof(*tsb));
	/* Zero-out TSB by default */
	memset(tsb, 0, sizeof(*tsb));

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		ip_ver = htons(skb->protocol);
		switch (ip_ver) {
		case ETH_P_IP:
			ip_proto = ip_hdr(skb)->protocol;
			break;
		case ETH_P_IPV6:
			ip_proto = ipv6_hdr(skb)->nexthdr;
			break;
		default:
			return skb;
		}

		/* Get the checksum offset and the L4 (transport) offset */
		csum_start = skb_checksum_start_offset(skb) - sizeof(*tsb);
		csum_info = (csum_start + skb->csum_offset) & L4_CSUM_PTR_MASK;
		csum_info |= (csum_start << L4_PTR_SHIFT);

		if (ip_proto == IPPROTO_TCP || ip_proto == IPPROTO_UDP) {
			csum_info |= L4_LENGTH_VALID;
			if (ip_proto == IPPROTO_UDP && ip_ver == ETH_P_IP)
				csum_info |= L4_UDP;
		} else {
			csum_info = 0;
		}

		tsb->l4_ptr_dest_map = csum_info;
	}

	return skb;
}

static netdev_tx_t bcm_sysport_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	struct device *kdev = &priv->pdev->dev;
	struct bcm_sysport_tx_ring *ring;
	struct bcm_sysport_cb *cb;
	struct netdev_queue *txq;
	struct dma_desc *desc;
	unsigned int skb_len;
	unsigned long flags;
	dma_addr_t mapping;
	u32 len_status;
	u16 queue;
	int ret;

	queue = skb_get_queue_mapping(skb);
	txq = netdev_get_tx_queue(dev, queue);
	ring = &priv->tx_rings[queue];

	/* lock against tx reclaim in BH context and TX ring full interrupt */
	spin_lock_irqsave(&ring->lock, flags);
	if (unlikely(ring->desc_count == 0)) {
		netif_tx_stop_queue(txq);
		netdev_err(dev, "queue %d awake and ring full!\n", queue);
		ret = NETDEV_TX_BUSY;
		goto out;
	}

	/* The Ethernet switch we are interfaced with needs packets to be at
	 * least 64 bytes (including FCS) otherwise they will be discarded when
	 * they enter the switch port logic. When Broadcom tags are enabled, we
	 * need to make sure that packets are at least 68 bytes
	 * (including FCS and tag) because the length verification is done after
	 * the Broadcom tag is stripped off the ingress packet.
	 */
	if (skb_put_padto(skb, ETH_ZLEN + ENET_BRCM_TAG_LEN)) {
		ret = NETDEV_TX_OK;
		goto out;
	}

	/* Insert TSB and checksum infos */
	if (priv->tsb_en) {
		skb = bcm_sysport_insert_tsb(skb, dev);
		if (!skb) {
			ret = NETDEV_TX_OK;
			goto out;
		}
	}

	skb_len = skb->len;

	mapping = dma_map_single(kdev, skb->data, skb_len, DMA_TO_DEVICE);
	if (dma_mapping_error(kdev, mapping)) {
		priv->mib.tx_dma_failed++;
		netif_err(priv, tx_err, dev, "DMA map failed at %p (len=%d)\n",
			  skb->data, skb_len);
		ret = NETDEV_TX_OK;
		goto out;
	}

	/* Remember the SKB for future freeing */
	cb = &ring->cbs[ring->curr_desc];
	cb->skb = skb;
	dma_unmap_addr_set(cb, dma_addr, mapping);
	dma_unmap_len_set(cb, dma_len, skb_len);

	/* Fetch a descriptor entry from our pool */
	desc = ring->desc_cpu;

	desc->addr_lo = lower_32_bits(mapping);
	len_status = upper_32_bits(mapping) & DESC_ADDR_HI_MASK;
	len_status |= (skb_len << DESC_LEN_SHIFT);
	len_status |= (DESC_SOP | DESC_EOP | TX_STATUS_APP_CRC) <<
		       DESC_STATUS_SHIFT;
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		len_status |= (DESC_L4_CSUM << DESC_STATUS_SHIFT);

	ring->curr_desc++;
	if (ring->curr_desc == ring->size)
		ring->curr_desc = 0;
	ring->desc_count--;

	/* Ensure write completion of the descriptor status/length
	 * in DRAM before the System Port WRITE_PORT register latches
	 * the value
	 */
	wmb();
	desc->addr_status_len = len_status;
	wmb();

	/* Write this descriptor address to the RING write port */
	tdma_port_write_desc_addr(priv, desc, ring->index);

	/* Check ring space and update SW control flow */
	if (ring->desc_count == 0)
		netif_tx_stop_queue(txq);

	netif_dbg(priv, tx_queued, dev, "ring=%d desc_count=%d, curr_desc=%d\n",
		  ring->index, ring->desc_count, ring->curr_desc);

	ret = NETDEV_TX_OK;
out:
	spin_unlock_irqrestore(&ring->lock, flags);
	return ret;
}

static void bcm_sysport_tx_timeout(struct net_device *dev)
{
	netdev_warn(dev, "transmit timeout!\n");

	netif_trans_update(dev);
	dev->stats.tx_errors++;

	netif_tx_wake_all_queues(dev);
}

/* phylib adjust link callback */
static void bcm_sysport_adj_link(struct net_device *dev)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;
	unsigned int changed = 0;
	u32 cmd_bits = 0, reg;

	if (priv->old_link != phydev->link) {
		changed = 1;
		priv->old_link = phydev->link;
	}

	if (priv->old_duplex != phydev->duplex) {
		changed = 1;
		priv->old_duplex = phydev->duplex;
	}

	if (priv->is_lite)
		goto out;

	switch (phydev->speed) {
	case SPEED_2500:
		cmd_bits = CMD_SPEED_2500;
		break;
	case SPEED_1000:
		cmd_bits = CMD_SPEED_1000;
		break;
	case SPEED_100:
		cmd_bits = CMD_SPEED_100;
		break;
	case SPEED_10:
		cmd_bits = CMD_SPEED_10;
		break;
	default:
		break;
	}
	cmd_bits <<= CMD_SPEED_SHIFT;

	if (phydev->duplex == DUPLEX_HALF)
		cmd_bits |= CMD_HD_EN;

	if (priv->old_pause != phydev->pause) {
		changed = 1;
		priv->old_pause = phydev->pause;
	}

	if (!phydev->pause)
		cmd_bits |= CMD_RX_PAUSE_IGNORE | CMD_TX_PAUSE_IGNORE;

	if (!changed)
		return;

	if (phydev->link) {
		reg = umac_readl(priv, UMAC_CMD);
		reg &= ~((CMD_SPEED_MASK << CMD_SPEED_SHIFT) |
			CMD_HD_EN | CMD_RX_PAUSE_IGNORE |
			CMD_TX_PAUSE_IGNORE);
		reg |= cmd_bits;
		umac_writel(priv, reg, UMAC_CMD);
	}
out:
	if (changed)
		phy_print_status(phydev);
}

static int bcm_sysport_init_tx_ring(struct bcm_sysport_priv *priv,
				    unsigned int index)
{
	struct bcm_sysport_tx_ring *ring = &priv->tx_rings[index];
	struct device *kdev = &priv->pdev->dev;
	size_t size;
	void *p;
	u32 reg;

	/* Simple descriptors partitioning for now */
	size = 256;

	/* We just need one DMA descriptor which is DMA-able, since writing to
	 * the port will allocate a new descriptor in its internal linked-list
	 */
	p = dma_zalloc_coherent(kdev, sizeof(struct dma_desc), &ring->desc_dma,
				GFP_KERNEL);
	if (!p) {
		netif_err(priv, hw, priv->netdev, "DMA alloc failed\n");
		return -ENOMEM;
	}

	ring->cbs = kcalloc(size, sizeof(struct bcm_sysport_cb), GFP_KERNEL);
	if (!ring->cbs) {
		dma_free_coherent(kdev, sizeof(struct dma_desc),
				  ring->desc_cpu, ring->desc_dma);
		netif_err(priv, hw, priv->netdev, "CB allocation failed\n");
		return -ENOMEM;
	}

	/* Initialize SW view of the ring */
	spin_lock_init(&ring->lock);
	ring->priv = priv;
	netif_tx_napi_add(priv->netdev, &ring->napi, bcm_sysport_tx_poll, 64);
	ring->index = index;
	ring->size = size;
	ring->alloc_size = ring->size;
	ring->desc_cpu = p;
	ring->desc_count = ring->size;
	ring->curr_desc = 0;

	/* Initialize HW ring */
	tdma_writel(priv, RING_EN, TDMA_DESC_RING_HEAD_TAIL_PTR(index));
	tdma_writel(priv, 0, TDMA_DESC_RING_COUNT(index));
	tdma_writel(priv, 1, TDMA_DESC_RING_INTR_CONTROL(index));
	tdma_writel(priv, 0, TDMA_DESC_RING_PROD_CONS_INDEX(index));
	tdma_writel(priv, RING_IGNORE_STATUS, TDMA_DESC_RING_MAPPING(index));
	tdma_writel(priv, 0, TDMA_DESC_RING_PCP_DEI_VID(index));

	/* Do not use tdma_control_bit() here because TSB_SWAP1 collides
	 * with the original definition of ACB_ALGO
	 */
	reg = tdma_readl(priv, TDMA_CONTROL);
	if (priv->is_lite)
		reg &= ~BIT(TSB_SWAP1);
	/* Set a correct TSB format based on host endian */
	if (!IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		reg |= tdma_control_bit(priv, TSB_SWAP0);
	else
		reg &= ~tdma_control_bit(priv, TSB_SWAP0);
	tdma_writel(priv, reg, TDMA_CONTROL);

	/* Program the number of descriptors as MAX_THRESHOLD and half of
	 * its size for the hysteresis trigger
	 */
	tdma_writel(priv, ring->size |
			1 << RING_HYST_THRESH_SHIFT,
			TDMA_DESC_RING_MAX_HYST(index));

	/* Enable the ring queue in the arbiter */
	reg = tdma_readl(priv, TDMA_TIER1_ARB_0_QUEUE_EN);
	reg |= (1 << index);
	tdma_writel(priv, reg, TDMA_TIER1_ARB_0_QUEUE_EN);

	napi_enable(&ring->napi);

	netif_dbg(priv, hw, priv->netdev,
		  "TDMA cfg, size=%d, desc_cpu=%p\n",
		  ring->size, ring->desc_cpu);

	return 0;
}

static void bcm_sysport_fini_tx_ring(struct bcm_sysport_priv *priv,
				     unsigned int index)
{
	struct bcm_sysport_tx_ring *ring = &priv->tx_rings[index];
	struct device *kdev = &priv->pdev->dev;
	u32 reg;

	/* Caller should stop the TDMA engine */
	reg = tdma_readl(priv, TDMA_STATUS);
	if (!(reg & TDMA_DISABLED))
		netdev_warn(priv->netdev, "TDMA not stopped!\n");

	/* ring->cbs is the last part in bcm_sysport_init_tx_ring which could
	 * fail, so by checking this pointer we know whether the TX ring was
	 * fully initialized or not.
	 */
	if (!ring->cbs)
		return;

	napi_disable(&ring->napi);
	netif_napi_del(&ring->napi);

	bcm_sysport_tx_clean(priv, ring);

	kfree(ring->cbs);
	ring->cbs = NULL;

	if (ring->desc_dma) {
		dma_free_coherent(kdev, sizeof(struct dma_desc),
				  ring->desc_cpu, ring->desc_dma);
		ring->desc_dma = 0;
	}
	ring->size = 0;
	ring->alloc_size = 0;

	netif_dbg(priv, hw, priv->netdev, "TDMA fini done\n");
}

/* RDMA helper */
static inline int rdma_enable_set(struct bcm_sysport_priv *priv,
				  unsigned int enable)
{
	unsigned int timeout = 1000;
	u32 reg;

	reg = rdma_readl(priv, RDMA_CONTROL);
	if (enable)
		reg |= RDMA_EN;
	else
		reg &= ~RDMA_EN;
	rdma_writel(priv, reg, RDMA_CONTROL);

	/* Poll for RMDA disabling completion */
	do {
		reg = rdma_readl(priv, RDMA_STATUS);
		if (!!(reg & RDMA_DISABLED) == !enable)
			return 0;
		usleep_range(1000, 2000);
	} while (timeout-- > 0);

	netdev_err(priv->netdev, "timeout waiting for RDMA to finish\n");

	return -ETIMEDOUT;
}

/* TDMA helper */
static inline int tdma_enable_set(struct bcm_sysport_priv *priv,
				  unsigned int enable)
{
	unsigned int timeout = 1000;
	u32 reg;

	reg = tdma_readl(priv, TDMA_CONTROL);
	if (enable)
		reg |= tdma_control_bit(priv, TDMA_EN);
	else
		reg &= ~tdma_control_bit(priv, TDMA_EN);
	tdma_writel(priv, reg, TDMA_CONTROL);

	/* Poll for TMDA disabling completion */
	do {
		reg = tdma_readl(priv, TDMA_STATUS);
		if (!!(reg & TDMA_DISABLED) == !enable)
			return 0;

		usleep_range(1000, 2000);
	} while (timeout-- > 0);

	netdev_err(priv->netdev, "timeout waiting for TDMA to finish\n");

	return -ETIMEDOUT;
}

static int bcm_sysport_init_rx_ring(struct bcm_sysport_priv *priv)
{
	struct bcm_sysport_cb *cb;
	u32 reg;
	int ret;
	int i;

	/* Initialize SW view of the RX ring */
	priv->num_rx_bds = priv->num_rx_desc_words / WORDS_PER_DESC;
	priv->rx_bds = priv->base + SYS_PORT_RDMA_OFFSET;
	priv->rx_c_index = 0;
	priv->rx_read_ptr = 0;
	priv->rx_cbs = kcalloc(priv->num_rx_bds, sizeof(struct bcm_sysport_cb),
				GFP_KERNEL);
	if (!priv->rx_cbs) {
		netif_err(priv, hw, priv->netdev, "CB allocation failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < priv->num_rx_bds; i++) {
		cb = priv->rx_cbs + i;
		cb->bd_addr = priv->rx_bds + i * DESC_SIZE;
	}

	ret = bcm_sysport_alloc_rx_bufs(priv);
	if (ret) {
		netif_err(priv, hw, priv->netdev, "SKB allocation failed\n");
		return ret;
	}

	/* Initialize HW, ensure RDMA is disabled */
	reg = rdma_readl(priv, RDMA_STATUS);
	if (!(reg & RDMA_DISABLED))
		rdma_enable_set(priv, 0);

	rdma_writel(priv, 0, RDMA_WRITE_PTR_LO);
	rdma_writel(priv, 0, RDMA_WRITE_PTR_HI);
	rdma_writel(priv, 0, RDMA_PROD_INDEX);
	rdma_writel(priv, 0, RDMA_CONS_INDEX);
	rdma_writel(priv, priv->num_rx_bds << RDMA_RING_SIZE_SHIFT |
			  RX_BUF_LENGTH, RDMA_RING_BUF_SIZE);
	/* Operate the queue in ring mode */
	rdma_writel(priv, 0, RDMA_START_ADDR_HI);
	rdma_writel(priv, 0, RDMA_START_ADDR_LO);
	rdma_writel(priv, 0, RDMA_END_ADDR_HI);
	rdma_writel(priv, priv->num_rx_desc_words - 1, RDMA_END_ADDR_LO);

	rdma_writel(priv, 1, RDMA_MBDONE_INTR);

	netif_dbg(priv, hw, priv->netdev,
		  "RDMA cfg, num_rx_bds=%d, rx_bds=%p\n",
		  priv->num_rx_bds, priv->rx_bds);

	return 0;
}

static void bcm_sysport_fini_rx_ring(struct bcm_sysport_priv *priv)
{
	struct bcm_sysport_cb *cb;
	unsigned int i;
	u32 reg;

	/* Caller should ensure RDMA is disabled */
	reg = rdma_readl(priv, RDMA_STATUS);
	if (!(reg & RDMA_DISABLED))
		netdev_warn(priv->netdev, "RDMA not stopped!\n");

	for (i = 0; i < priv->num_rx_bds; i++) {
		cb = &priv->rx_cbs[i];
		if (dma_unmap_addr(cb, dma_addr))
			dma_unmap_single(&priv->pdev->dev,
					 dma_unmap_addr(cb, dma_addr),
					 RX_BUF_LENGTH, DMA_FROM_DEVICE);
		bcm_sysport_free_cb(cb);
	}

	kfree(priv->rx_cbs);
	priv->rx_cbs = NULL;

	netif_dbg(priv, hw, priv->netdev, "RDMA fini done\n");
}

static void bcm_sysport_set_rx_mode(struct net_device *dev)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	u32 reg;

	if (priv->is_lite)
		return;

	reg = umac_readl(priv, UMAC_CMD);
	if (dev->flags & IFF_PROMISC)
		reg |= CMD_PROMISC;
	else
		reg &= ~CMD_PROMISC;
	umac_writel(priv, reg, UMAC_CMD);

	/* No support for ALLMULTI */
	if (dev->flags & IFF_ALLMULTI)
		return;
}

static inline void umac_enable_set(struct bcm_sysport_priv *priv,
				   u32 mask, unsigned int enable)
{
	u32 reg;

	if (!priv->is_lite) {
		reg = umac_readl(priv, UMAC_CMD);
		if (enable)
			reg |= mask;
		else
			reg &= ~mask;
		umac_writel(priv, reg, UMAC_CMD);
	} else {
		reg = gib_readl(priv, GIB_CONTROL);
		if (enable)
			reg |= mask;
		else
			reg &= ~mask;
		gib_writel(priv, reg, GIB_CONTROL);
	}

	/* UniMAC stops on a packet boundary, wait for a full-sized packet
	 * to be processed (1 msec).
	 */
	if (enable == 0)
		usleep_range(1000, 2000);
}

static inline void umac_reset(struct bcm_sysport_priv *priv)
{
	u32 reg;

	if (priv->is_lite)
		return;

	reg = umac_readl(priv, UMAC_CMD);
	reg |= CMD_SW_RESET;
	umac_writel(priv, reg, UMAC_CMD);
	udelay(10);
	reg = umac_readl(priv, UMAC_CMD);
	reg &= ~CMD_SW_RESET;
	umac_writel(priv, reg, UMAC_CMD);
}

static void umac_set_hw_addr(struct bcm_sysport_priv *priv,
			     unsigned char *addr)
{
	u32 mac0 = (addr[0] << 24) | (addr[1] << 16) | (addr[2] << 8) |
		    addr[3];
	u32 mac1 = (addr[4] << 8) | addr[5];

	if (!priv->is_lite) {
		umac_writel(priv, mac0, UMAC_MAC0);
		umac_writel(priv, mac1, UMAC_MAC1);
	} else {
		gib_writel(priv, mac0, GIB_MAC0);
		gib_writel(priv, mac1, GIB_MAC1);
	}
}

static void topctrl_flush(struct bcm_sysport_priv *priv)
{
	topctrl_writel(priv, RX_FLUSH, RX_FLUSH_CNTL);
	topctrl_writel(priv, TX_FLUSH, TX_FLUSH_CNTL);
	mdelay(1);
	topctrl_writel(priv, 0, RX_FLUSH_CNTL);
	topctrl_writel(priv, 0, TX_FLUSH_CNTL);
}

static int bcm_sysport_change_mac(struct net_device *dev, void *p)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	/* interface is disabled, changes to MAC will be reflected on next
	 * open call
	 */
	if (!netif_running(dev))
		return 0;

	umac_set_hw_addr(priv, dev->dev_addr);

	return 0;
}

static void bcm_sysport_get_stats64(struct net_device *dev,
				    struct rtnl_link_stats64 *stats)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	struct bcm_sysport_stats64 *stats64 = &priv->stats64;
	unsigned int start;

	netdev_stats_to_stats64(stats, &dev->stats);

	bcm_sysport_update_tx_stats(priv, &stats->tx_bytes,
				    &stats->tx_packets);

	do {
		start = u64_stats_fetch_begin_irq(&priv->syncp);
		stats->rx_packets = stats64->rx_packets;
		stats->rx_bytes = stats64->rx_bytes;
	} while (u64_stats_fetch_retry_irq(&priv->syncp, start));
}

static void bcm_sysport_netif_start(struct net_device *dev)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);

	/* Enable NAPI */
	napi_enable(&priv->napi);

	/* Enable RX interrupt and TX ring full interrupt */
	intrl2_0_mask_clear(priv, INTRL2_0_RDMA_MBDONE | INTRL2_0_TX_RING_FULL);

	phy_start(dev->phydev);

	/* Enable TX interrupts for the TXQs */
	if (!priv->is_lite)
		intrl2_1_mask_clear(priv, 0xffffffff);
	else
		intrl2_0_mask_clear(priv, INTRL2_0_TDMA_MBDONE_MASK);

	/* Last call before we start the real business */
	netif_tx_start_all_queues(dev);
}

static void rbuf_init(struct bcm_sysport_priv *priv)
{
	u32 reg;

	reg = rbuf_readl(priv, RBUF_CONTROL);
	reg |= RBUF_4B_ALGN | RBUF_RSB_EN;
	/* Set a correct RSB format on SYSTEMPORT Lite */
	if (priv->is_lite)
		reg &= ~RBUF_RSB_SWAP1;

	/* Set a correct RSB format based on host endian */
	if (!IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		reg |= RBUF_RSB_SWAP0;
	else
		reg &= ~RBUF_RSB_SWAP0;
	rbuf_writel(priv, reg, RBUF_CONTROL);
}

static inline void bcm_sysport_mask_all_intrs(struct bcm_sysport_priv *priv)
{
	intrl2_0_mask_set(priv, 0xffffffff);
	intrl2_0_writel(priv, 0xffffffff, INTRL2_CPU_CLEAR);
	if (!priv->is_lite) {
		intrl2_1_mask_set(priv, 0xffffffff);
		intrl2_1_writel(priv, 0xffffffff, INTRL2_CPU_CLEAR);
	}
}

static inline void gib_set_pad_extension(struct bcm_sysport_priv *priv)
{
	u32 reg;

	reg = gib_readl(priv, GIB_CONTROL);
	/* Include Broadcom tag in pad extension and fix up IPG_LENGTH */
	if (netdev_uses_dsa(priv->netdev)) {
		reg &= ~(GIB_PAD_EXTENSION_MASK << GIB_PAD_EXTENSION_SHIFT);
		reg |= ENET_BRCM_TAG_LEN << GIB_PAD_EXTENSION_SHIFT;
	}
	reg &= ~(GIB_IPG_LEN_MASK << GIB_IPG_LEN_SHIFT);
	reg |= 12 << GIB_IPG_LEN_SHIFT;
	gib_writel(priv, reg, GIB_CONTROL);
}

static int bcm_sysport_open(struct net_device *dev)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	struct phy_device *phydev;
	unsigned int i;
	int ret;

	/* Reset UniMAC */
	umac_reset(priv);

	/* Flush TX and RX FIFOs at TOPCTRL level */
	topctrl_flush(priv);

	/* Disable the UniMAC RX/TX */
	umac_enable_set(priv, CMD_RX_EN | CMD_TX_EN, 0);

	/* Enable RBUF 2bytes alignment and Receive Status Block */
	rbuf_init(priv);

	/* Set maximum frame length */
	if (!priv->is_lite)
		umac_writel(priv, UMAC_MAX_MTU_SIZE, UMAC_MAX_FRAME_LEN);
	else
		gib_set_pad_extension(priv);

	/* Set MAC address */
	umac_set_hw_addr(priv, dev->dev_addr);

	/* Read CRC forward */
	if (!priv->is_lite)
		priv->crc_fwd = !!(umac_readl(priv, UMAC_CMD) & CMD_CRC_FWD);
	else
		priv->crc_fwd = !!(gib_readl(priv, GIB_CONTROL) &
				   GIB_FCS_STRIP);

	phydev = of_phy_connect(dev, priv->phy_dn, bcm_sysport_adj_link,
				0, priv->phy_interface);
	if (!phydev) {
		netdev_err(dev, "could not attach to PHY\n");
		return -ENODEV;
	}

	/* Reset house keeping link status */
	priv->old_duplex = -1;
	priv->old_link = -1;
	priv->old_pause = -1;

	/* mask all interrupts and request them */
	bcm_sysport_mask_all_intrs(priv);

	ret = request_irq(priv->irq0, bcm_sysport_rx_isr, 0, dev->name, dev);
	if (ret) {
		netdev_err(dev, "failed to request RX interrupt\n");
		goto out_phy_disconnect;
	}

	if (!priv->is_lite) {
		ret = request_irq(priv->irq1, bcm_sysport_tx_isr, 0,
				  dev->name, dev);
		if (ret) {
			netdev_err(dev, "failed to request TX interrupt\n");
			goto out_free_irq0;
		}
	}

	/* Initialize both hardware and software ring */
	for (i = 0; i < dev->num_tx_queues; i++) {
		ret = bcm_sysport_init_tx_ring(priv, i);
		if (ret) {
			netdev_err(dev, "failed to initialize TX ring %d\n",
				   i);
			goto out_free_tx_ring;
		}
	}

	/* Initialize linked-list */
	tdma_writel(priv, TDMA_LL_RAM_INIT_BUSY, TDMA_STATUS);

	/* Initialize RX ring */
	ret = bcm_sysport_init_rx_ring(priv);
	if (ret) {
		netdev_err(dev, "failed to initialize RX ring\n");
		goto out_free_rx_ring;
	}

	/* Turn on RDMA */
	ret = rdma_enable_set(priv, 1);
	if (ret)
		goto out_free_rx_ring;

	/* Turn on TDMA */
	ret = tdma_enable_set(priv, 1);
	if (ret)
		goto out_clear_rx_int;

	/* Turn on UniMAC TX/RX */
	umac_enable_set(priv, CMD_RX_EN | CMD_TX_EN, 1);

	bcm_sysport_netif_start(dev);

	return 0;

out_clear_rx_int:
	intrl2_0_mask_set(priv, INTRL2_0_RDMA_MBDONE | INTRL2_0_TX_RING_FULL);
out_free_rx_ring:
	bcm_sysport_fini_rx_ring(priv);
out_free_tx_ring:
	for (i = 0; i < dev->num_tx_queues; i++)
		bcm_sysport_fini_tx_ring(priv, i);
	if (!priv->is_lite)
		free_irq(priv->irq1, dev);
out_free_irq0:
	free_irq(priv->irq0, dev);
out_phy_disconnect:
	phy_disconnect(phydev);
	return ret;
}

static void bcm_sysport_netif_stop(struct net_device *dev)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);

	/* stop all software from updating hardware */
	netif_tx_stop_all_queues(dev);
	napi_disable(&priv->napi);
	phy_stop(dev->phydev);

	/* mask all interrupts */
	bcm_sysport_mask_all_intrs(priv);
}

static int bcm_sysport_stop(struct net_device *dev)
{
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	unsigned int i;
	int ret;

	bcm_sysport_netif_stop(dev);

	/* Disable UniMAC RX */
	umac_enable_set(priv, CMD_RX_EN, 0);

	ret = tdma_enable_set(priv, 0);
	if (ret) {
		netdev_err(dev, "timeout disabling RDMA\n");
		return ret;
	}

	/* Wait for a maximum packet size to be drained */
	usleep_range(2000, 3000);

	ret = rdma_enable_set(priv, 0);
	if (ret) {
		netdev_err(dev, "timeout disabling TDMA\n");
		return ret;
	}

	/* Disable UniMAC TX */
	umac_enable_set(priv, CMD_TX_EN, 0);

	/* Free RX/TX rings SW structures */
	for (i = 0; i < dev->num_tx_queues; i++)
		bcm_sysport_fini_tx_ring(priv, i);
	bcm_sysport_fini_rx_ring(priv);

	free_irq(priv->irq0, dev);
	if (!priv->is_lite)
		free_irq(priv->irq1, dev);

	/* Disconnect from PHY */
	phy_disconnect(dev->phydev);

	return 0;
}

static const struct ethtool_ops bcm_sysport_ethtool_ops = {
	.get_drvinfo		= bcm_sysport_get_drvinfo,
	.get_msglevel		= bcm_sysport_get_msglvl,
	.set_msglevel		= bcm_sysport_set_msglvl,
	.get_link		= ethtool_op_get_link,
	.get_strings		= bcm_sysport_get_strings,
	.get_ethtool_stats	= bcm_sysport_get_stats,
	.get_sset_count		= bcm_sysport_get_sset_count,
	.get_wol		= bcm_sysport_get_wol,
	.set_wol		= bcm_sysport_set_wol,
	.get_coalesce		= bcm_sysport_get_coalesce,
	.set_coalesce		= bcm_sysport_set_coalesce,
	.get_link_ksettings     = phy_ethtool_get_link_ksettings,
	.set_link_ksettings     = phy_ethtool_set_link_ksettings,
};

static const struct net_device_ops bcm_sysport_netdev_ops = {
	.ndo_start_xmit		= bcm_sysport_xmit,
	.ndo_tx_timeout		= bcm_sysport_tx_timeout,
	.ndo_open		= bcm_sysport_open,
	.ndo_stop		= bcm_sysport_stop,
	.ndo_set_features	= bcm_sysport_set_features,
	.ndo_set_rx_mode	= bcm_sysport_set_rx_mode,
	.ndo_set_mac_address	= bcm_sysport_change_mac,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= bcm_sysport_poll_controller,
#endif
	.ndo_get_stats64	= bcm_sysport_get_stats64,
};

#define REV_FMT	"v%2x.%02x"

static const struct bcm_sysport_hw_params bcm_sysport_params[] = {
	[SYSTEMPORT] = {
		.is_lite = false,
		.num_rx_desc_words = SP_NUM_HW_RX_DESC_WORDS,
	},
	[SYSTEMPORT_LITE] = {
		.is_lite = true,
		.num_rx_desc_words = SP_LT_NUM_HW_RX_DESC_WORDS,
	},
};

static const struct of_device_id bcm_sysport_of_match[] = {
	{ .compatible = "brcm,systemportlite-v1.00",
	  .data = &bcm_sysport_params[SYSTEMPORT_LITE] },
	{ .compatible = "brcm,systemport-v1.00",
	  .data = &bcm_sysport_params[SYSTEMPORT] },
	{ .compatible = "brcm,systemport",
	  .data = &bcm_sysport_params[SYSTEMPORT] },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, bcm_sysport_of_match);

static int bcm_sysport_probe(struct platform_device *pdev)
{
	const struct bcm_sysport_hw_params *params;
	const struct of_device_id *of_id = NULL;
	struct bcm_sysport_priv *priv;
	struct device_node *dn;
	struct net_device *dev;
	const void *macaddr;
	struct resource *r;
	u32 txq, rxq;
	int ret;

	dn = pdev->dev.of_node;
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	of_id = of_match_node(bcm_sysport_of_match, dn);
	if (!of_id || !of_id->data)
		return -EINVAL;

	/* Fairly quickly we need to know the type of adapter we have */
	params = of_id->data;

	/* Read the Transmit/Receive Queue properties */
	if (of_property_read_u32(dn, "systemport,num-txq", &txq))
		txq = TDMA_NUM_RINGS;
	if (of_property_read_u32(dn, "systemport,num-rxq", &rxq))
		rxq = 1;

	/* Sanity check the number of transmit queues */
	if (!txq || txq > TDMA_NUM_RINGS)
		return -EINVAL;

	dev = alloc_etherdev_mqs(sizeof(*priv), txq, rxq);
	if (!dev)
		return -ENOMEM;

	/* Initialize private members */
	priv = netdev_priv(dev);

	/* Allocate number of TX rings */
	priv->tx_rings = devm_kcalloc(&pdev->dev, txq,
				      sizeof(struct bcm_sysport_tx_ring),
				      GFP_KERNEL);
	if (!priv->tx_rings)
		return -ENOMEM;

	priv->is_lite = params->is_lite;
	priv->num_rx_desc_words = params->num_rx_desc_words;

	priv->irq0 = platform_get_irq(pdev, 0);
	if (!priv->is_lite) {
		priv->irq1 = platform_get_irq(pdev, 1);
		priv->wol_irq = platform_get_irq(pdev, 2);
	} else {
		priv->wol_irq = platform_get_irq(pdev, 1);
	}
	if (priv->irq0 <= 0 || (priv->irq1 <= 0 && !priv->is_lite)) {
		dev_err(&pdev->dev, "invalid interrupts\n");
		ret = -EINVAL;
		goto err_free_netdev;
	}

	priv->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(priv->base)) {
		ret = PTR_ERR(priv->base);
		goto err_free_netdev;
	}

	priv->netdev = dev;
	priv->pdev = pdev;

	priv->phy_interface = of_get_phy_mode(dn);
	/* Default to GMII interface mode */
	if (priv->phy_interface < 0)
		priv->phy_interface = PHY_INTERFACE_MODE_GMII;

	/* In the case of a fixed PHY, the DT node associated
	 * to the PHY is the Ethernet MAC DT node.
	 */
	if (of_phy_is_fixed_link(dn)) {
		ret = of_phy_register_fixed_link(dn);
		if (ret) {
			dev_err(&pdev->dev, "failed to register fixed PHY\n");
			goto err_free_netdev;
		}

		priv->phy_dn = dn;
	}

	/* Initialize netdevice members */
	macaddr = of_get_mac_address(dn);
	if (!macaddr || !is_valid_ether_addr(macaddr)) {
		dev_warn(&pdev->dev, "using random Ethernet MAC\n");
		eth_hw_addr_random(dev);
	} else {
		ether_addr_copy(dev->dev_addr, macaddr);
	}

	SET_NETDEV_DEV(dev, &pdev->dev);
	dev_set_drvdata(&pdev->dev, dev);
	dev->ethtool_ops = &bcm_sysport_ethtool_ops;
	dev->netdev_ops = &bcm_sysport_netdev_ops;
	netif_napi_add(dev, &priv->napi, bcm_sysport_poll, 64);

	/* HW supported features, none enabled by default */
	dev->hw_features |= NETIF_F_RXCSUM | NETIF_F_HIGHDMA |
				NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;

	/* Request the WOL interrupt and advertise suspend if available */
	priv->wol_irq_disabled = 1;
	ret = devm_request_irq(&pdev->dev, priv->wol_irq,
			       bcm_sysport_wol_isr, 0, dev->name, priv);
	if (!ret)
		device_set_wakeup_capable(&pdev->dev, 1);

	/* Set the needed headroom once and for all */
	BUILD_BUG_ON(sizeof(struct bcm_tsb) != 8);
	dev->needed_headroom += sizeof(struct bcm_tsb);

	/* libphy will adjust the link state accordingly */
	netif_carrier_off(dev);

	u64_stats_init(&priv->syncp);

	ret = register_netdev(dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register net_device\n");
		goto err_deregister_fixed_link;
	}

	priv->rev = topctrl_readl(priv, REV_CNTL) & REV_MASK;
	dev_info(&pdev->dev,
		 "Broadcom SYSTEMPORT%s" REV_FMT
		 " at 0x%p (irqs: %d, %d, TXQs: %d, RXQs: %d)\n",
		 priv->is_lite ? " Lite" : "",
		 (priv->rev >> 8) & 0xff, priv->rev & 0xff,
		 priv->base, priv->irq0, priv->irq1, txq, rxq);

	return 0;

err_deregister_fixed_link:
	if (of_phy_is_fixed_link(dn))
		of_phy_deregister_fixed_link(dn);
err_free_netdev:
	free_netdev(dev);
	return ret;
}

static int bcm_sysport_remove(struct platform_device *pdev)
{
	struct net_device *dev = dev_get_drvdata(&pdev->dev);
	struct device_node *dn = pdev->dev.of_node;

	/* Not much to do, ndo_close has been called
	 * and we use managed allocations
	 */
	unregister_netdev(dev);
	if (of_phy_is_fixed_link(dn))
		of_phy_deregister_fixed_link(dn);
	free_netdev(dev);
	dev_set_drvdata(&pdev->dev, NULL);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int bcm_sysport_suspend_to_wol(struct bcm_sysport_priv *priv)
{
	struct net_device *ndev = priv->netdev;
	unsigned int timeout = 1000;
	u32 reg;

	/* Password has already been programmed */
	reg = umac_readl(priv, UMAC_MPD_CTRL);
	reg |= MPD_EN;
	reg &= ~PSW_EN;
	if (priv->wolopts & WAKE_MAGICSECURE)
		reg |= PSW_EN;
	umac_writel(priv, reg, UMAC_MPD_CTRL);

	/* Make sure RBUF entered WoL mode as result */
	do {
		reg = rbuf_readl(priv, RBUF_STATUS);
		if (reg & RBUF_WOL_MODE)
			break;

		udelay(10);
	} while (timeout-- > 0);

	/* Do not leave the UniMAC RBUF matching only MPD packets */
	if (!timeout) {
		reg = umac_readl(priv, UMAC_MPD_CTRL);
		reg &= ~MPD_EN;
		umac_writel(priv, reg, UMAC_MPD_CTRL);
		netif_err(priv, wol, ndev, "failed to enter WOL mode\n");
		return -ETIMEDOUT;
	}

	/* UniMAC receive needs to be turned on */
	umac_enable_set(priv, CMD_RX_EN, 1);

	/* Enable the interrupt wake-up source */
	intrl2_0_mask_clear(priv, INTRL2_0_MPD);

	netif_dbg(priv, wol, ndev, "entered WOL mode\n");

	return 0;
}

static int bcm_sysport_suspend(struct device *d)
{
	struct net_device *dev = dev_get_drvdata(d);
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	unsigned int i;
	int ret = 0;
	u32 reg;

	if (!netif_running(dev))
		return 0;

	bcm_sysport_netif_stop(dev);

	phy_suspend(dev->phydev);

	netif_device_detach(dev);

	/* Disable UniMAC RX */
	umac_enable_set(priv, CMD_RX_EN, 0);

	ret = rdma_enable_set(priv, 0);
	if (ret) {
		netdev_err(dev, "RDMA timeout!\n");
		return ret;
	}

	/* Disable RXCHK if enabled */
	if (priv->rx_chk_en) {
		reg = rxchk_readl(priv, RXCHK_CONTROL);
		reg &= ~RXCHK_EN;
		rxchk_writel(priv, reg, RXCHK_CONTROL);
	}

	/* Flush RX pipe */
	if (!priv->wolopts)
		topctrl_writel(priv, RX_FLUSH, RX_FLUSH_CNTL);

	ret = tdma_enable_set(priv, 0);
	if (ret) {
		netdev_err(dev, "TDMA timeout!\n");
		return ret;
	}

	/* Wait for a packet boundary */
	usleep_range(2000, 3000);

	umac_enable_set(priv, CMD_TX_EN, 0);

	topctrl_writel(priv, TX_FLUSH, TX_FLUSH_CNTL);

	/* Free RX/TX rings SW structures */
	for (i = 0; i < dev->num_tx_queues; i++)
		bcm_sysport_fini_tx_ring(priv, i);
	bcm_sysport_fini_rx_ring(priv);

	/* Get prepared for Wake-on-LAN */
	if (device_may_wakeup(d) && priv->wolopts)
		ret = bcm_sysport_suspend_to_wol(priv);

	return ret;
}

static int bcm_sysport_resume(struct device *d)
{
	struct net_device *dev = dev_get_drvdata(d);
	struct bcm_sysport_priv *priv = netdev_priv(dev);
	unsigned int i;
	u32 reg;
	int ret;

	if (!netif_running(dev))
		return 0;

	umac_reset(priv);

	/* We may have been suspended and never received a WOL event that
	 * would turn off MPD detection, take care of that now
	 */
	bcm_sysport_resume_from_wol(priv);

	/* Initialize both hardware and software ring */
	for (i = 0; i < dev->num_tx_queues; i++) {
		ret = bcm_sysport_init_tx_ring(priv, i);
		if (ret) {
			netdev_err(dev, "failed to initialize TX ring %d\n",
				   i);
			goto out_free_tx_rings;
		}
	}

	/* Initialize linked-list */
	tdma_writel(priv, TDMA_LL_RAM_INIT_BUSY, TDMA_STATUS);

	/* Initialize RX ring */
	ret = bcm_sysport_init_rx_ring(priv);
	if (ret) {
		netdev_err(dev, "failed to initialize RX ring\n");
		goto out_free_rx_ring;
	}

	netif_device_attach(dev);

	/* RX pipe enable */
	topctrl_writel(priv, 0, RX_FLUSH_CNTL);

	ret = rdma_enable_set(priv, 1);
	if (ret) {
		netdev_err(dev, "failed to enable RDMA\n");
		goto out_free_rx_ring;
	}

	/* Enable rxhck */
	if (priv->rx_chk_en) {
		reg = rxchk_readl(priv, RXCHK_CONTROL);
		reg |= RXCHK_EN;
		rxchk_writel(priv, reg, RXCHK_CONTROL);
	}

	rbuf_init(priv);

	/* Set maximum frame length */
	if (!priv->is_lite)
		umac_writel(priv, UMAC_MAX_MTU_SIZE, UMAC_MAX_FRAME_LEN);
	else
		gib_set_pad_extension(priv);

	/* Set MAC address */
	umac_set_hw_addr(priv, dev->dev_addr);

	umac_enable_set(priv, CMD_RX_EN, 1);

	/* TX pipe enable */
	topctrl_writel(priv, 0, TX_FLUSH_CNTL);

	umac_enable_set(priv, CMD_TX_EN, 1);

	ret = tdma_enable_set(priv, 1);
	if (ret) {
		netdev_err(dev, "TDMA timeout!\n");
		goto out_free_rx_ring;
	}

	phy_resume(dev->phydev);

	bcm_sysport_netif_start(dev);

	return 0;

out_free_rx_ring:
	bcm_sysport_fini_rx_ring(priv);
out_free_tx_rings:
	for (i = 0; i < dev->num_tx_queues; i++)
		bcm_sysport_fini_tx_ring(priv, i);
	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(bcm_sysport_pm_ops,
		bcm_sysport_suspend, bcm_sysport_resume);

static struct platform_driver bcm_sysport_driver = {
	.probe	= bcm_sysport_probe,
	.remove	= bcm_sysport_remove,
	.driver =  {
		.name = "brcm-systemport",
		.of_match_table = bcm_sysport_of_match,
		.pm = &bcm_sysport_pm_ops,
	},
};
module_platform_driver(bcm_sysport_driver);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom System Port Ethernet MAC driver");
MODULE_ALIAS("platform:brcm-systemport");
MODULE_LICENSE("GPL");
