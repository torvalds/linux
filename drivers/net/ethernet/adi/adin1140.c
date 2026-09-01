// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Analog Devices, Inc. ADIN1140 10BASE-T1S MAC-PHY
 *
 * Copyright 2026 Analog Devices Inc.
 */

#include <linux/cleanup.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/oa_tc6.h>
#include <linux/phy.h>

#define ADIN1140_MAC_CONFIG2_STAT_CLR_RD	BIT(6)
#define ADIN1140_MAC_CONFIG2_FWD_UNK2HOST	BIT(2)

#define ADIN1140_MAC_P1_LOOP_ADDR_REG	0xC4

#define ADIN1140_MAC_ADDR_FILT_UPR_REG		0x50
#define ADIN1140_MAC_ADDR_FILT_APPLY2PORT1	BIT(30)
#define ADIN1140_MAC_ADDR_FILT_TO_HOST		BIT(16)

#define ADIN1140_MAC_ADDR_FILT_LWR_REG		0x51

#define ADIN1140_MAC_ADDR_MASK_UPR_REG	0x70
#define ADIN1140_MAC_ADDR_MASK_LWR_REG	0x71

#define ADIN1140_MAC_FILT_MC_SLOT	0U
#define ADIN1140_MAC_FILT_BC_SLOT	1U
#define ADIN1140_MAC_FILT_UC_SLOT	2U
#define ADIN1140_MAC_FILT_MAX_SLOT	16U
#define ADIN1140_MAC_FILT_MASK_LIMIT	2U

#define ADIN1140_MAC_RX_FRAME_CNT		0xA1
#define ADIN1140_MAC_RX_BC_FRAME_CNT		0xA2
#define ADIN1140_MAC_RX_MC_FRAME_CNT		0xA3
#define ADIN1140_MAC_RX_CRC_ERR_CNT		0xA5
#define ADIN1140_MAC_RX_ALIGN_ERR_CNT		0xA6
#define ADIN1140_MAC_RX_PREAMBLE_ERR_CNT	0xA7
#define ADIN1140_MAC_RX_SHORT_ERR_CNT		0xA8
#define ADIN1140_MAC_RX_LONG_ERR_CNT		0xA9
#define ADIN1140_MAC_RX_PHY_ERR_CNT		0xAA
#define ADIN1140_MAC_RX_DRP_FULL_CNT		0xAB
#define ADIN1140_MAC_RX_DRP_FILTER_CNT		0xAD
#define ADIN1140_MAC_RX_IFG_ERR_CNT		0xAE
#define ADIN1140_MAC_TX_FRAME_CNT		0xB1
#define ADIN1140_MAC_TX_BC_FRAME_CNT		0xB2
#define ADIN1140_MAC_TX_MC_FRAME_CNT		0xB3
#define ADIN1140_MAC_TX_SINGLE_COL_CNT		0xB5
#define ADIN1140_MAC_TX_MULTI_COL_CNT		0xB6
#define ADIN1140_MAC_TX_DEFERRED_CNT		0xB7
#define ADIN1140_MAC_TX_LATE_COL_CNT		0xB8
#define ADIN1140_MAC_TX_EXCESS_COL_CNT		0xB9
#define ADIN1140_MAC_TX_UNDERRUN_CNT		0xBA

/* ADIN1140_MAC_FILT_MAX_SLOT - 3 (multicast, broadcast and unicast
 * reserved slots)
 */
#define ADIN1140_MAC_FILT_AVAIL	13U

#define ADIN1140_PHY_CTRL_DEFAULT	0x1000
#define ADIN1140_PHY_STATUS_DEFAULT	0x082D
#define ADIN1140_PHY_ID1		0x0283
#define ADIN1140_PHY_ID2		0xBE00

#define ADIN1140_STATS_CHECK_DELAY	(3 * HZ)

enum adin1140_statistics_entry {
	rx_frames,
	rx_bc_frames,
	rx_mc_frames,
	rx_crc_errors,
	rx_align_errors,
	rx_preamble_errors,
	rx_short_frame_errors,
	rx_long_frame_errors,
	rx_phy_errors,
	rx_fifo_full_dropped,
	rx_addr_filter_dropped,
	rx_ifg_errors,
	tx_frames,
	tx_bc_frames,
	tx_mc_frames,
	tx_single_collision,
	tx_multi_collision,
	tx_deferred,
	tx_late_collision,
	tx_excess_collision,
	tx_underrun,
	ADIN1140_STATS_CNT,
};

struct adin1140_statistics_reg {
	const char *name;
	enum adin1140_statistics_entry idx;
};

struct adin1140_priv {
	struct net_device *netdev;
	struct oa_tc6 *tc6;
	struct mii_bus *mdiobus;
	struct phy_device *phydev;
	struct delayed_work stats_work;

	/* Protects stats[] from concurrent updates in adin1140_stats_work
	 * and reads in the get_stats functions
	 */
	spinlock_t stat_lock;
	u64 stats[ADIN1140_STATS_CNT];
};

static const u32 adin1140_stat_regs[] = {
	[rx_frames] = ADIN1140_MAC_RX_FRAME_CNT,
	[rx_bc_frames] = ADIN1140_MAC_RX_BC_FRAME_CNT,
	[rx_mc_frames] = ADIN1140_MAC_RX_MC_FRAME_CNT,
	[rx_crc_errors] = ADIN1140_MAC_RX_CRC_ERR_CNT,
	[rx_align_errors] = ADIN1140_MAC_RX_ALIGN_ERR_CNT,
	[rx_preamble_errors] = ADIN1140_MAC_RX_PREAMBLE_ERR_CNT,
	[rx_short_frame_errors]	= ADIN1140_MAC_RX_SHORT_ERR_CNT,
	[rx_long_frame_errors] = ADIN1140_MAC_RX_LONG_ERR_CNT,
	[rx_phy_errors] = ADIN1140_MAC_RX_PHY_ERR_CNT,
	[rx_fifo_full_dropped] = ADIN1140_MAC_RX_DRP_FULL_CNT,
	[rx_addr_filter_dropped] = ADIN1140_MAC_RX_DRP_FILTER_CNT,
	[rx_ifg_errors] = ADIN1140_MAC_RX_IFG_ERR_CNT,
	[tx_frames] = ADIN1140_MAC_TX_FRAME_CNT,
	[tx_bc_frames] = ADIN1140_MAC_TX_BC_FRAME_CNT,
	[tx_mc_frames] = ADIN1140_MAC_TX_MC_FRAME_CNT,
	[tx_single_collision] = ADIN1140_MAC_TX_SINGLE_COL_CNT,
	[tx_multi_collision] = ADIN1140_MAC_TX_MULTI_COL_CNT,
	[tx_deferred] = ADIN1140_MAC_TX_DEFERRED_CNT,
	[tx_late_collision] = ADIN1140_MAC_TX_LATE_COL_CNT,
	[tx_excess_collision] = ADIN1140_MAC_TX_EXCESS_COL_CNT,
	[tx_underrun] = ADIN1140_MAC_TX_UNDERRUN_CNT,
};

static const struct adin1140_statistics_reg adin1140_stats[] = {
	{.name = "rx_preamble_errors", .idx = rx_preamble_errors},
	{.name = "rx_ifg_errors", .idx = rx_ifg_errors},
};

static int adin1140_mac_filter_set(struct adin1140_priv *priv,
				   const u8 *addr, const u8 *mask,
				   u8 slot)
{
	u32 reg_address;
	u32 val;
	int ret;

	if (slot >= ADIN1140_MAC_FILT_MAX_SLOT)
		return -ENOSPC;

	reg_address = ADIN1140_MAC_ADDR_FILT_UPR_REG + 2 * slot;

	ret = oa_tc6_write_register_mms(priv->tc6, OA_TC6_MAC_MMS1,
					reg_address,
					get_unaligned_be16(&addr[0]) |
					ADIN1140_MAC_ADDR_FILT_APPLY2PORT1 |
					ADIN1140_MAC_ADDR_FILT_TO_HOST);
	if (ret)
		return ret;

	reg_address = ADIN1140_MAC_ADDR_FILT_LWR_REG + 2 * slot;
	ret = oa_tc6_write_register_mms(priv->tc6, OA_TC6_MAC_MMS1,
					reg_address,
					get_unaligned_be32(&addr[2]));
	if (ret)
		return ret;

	/* Only the first 2 destination MAC filter slots support masking.
	 * For the other entries, the destination address in the received
	 * frame must match exactly.
	 */
	if (slot >= ADIN1140_MAC_FILT_MASK_LIMIT)
		return 0;

	val = get_unaligned_be16(&mask[0]);
	reg_address = ADIN1140_MAC_ADDR_MASK_UPR_REG + (2 * slot);

	ret = oa_tc6_write_register_mms(priv->tc6, OA_TC6_MAC_MMS1,
					reg_address, val);
	if (ret)
		return ret;

	val = get_unaligned_be32(&mask[2]);
	reg_address = ADIN1140_MAC_ADDR_MASK_LWR_REG + (2 * slot);

	return oa_tc6_write_register_mms(priv->tc6, OA_TC6_MAC_MMS1,
					 reg_address, val);
}

static int adin1140_mac_filter_clear(struct adin1140_priv *priv, u8 slot)
{
	u8 mask[ETH_ALEN];
	u8 addr[ETH_ALEN];

	memset(mask, 0xFF, ETH_ALEN);
	memset(addr, 0x0, ETH_ALEN);

	return adin1140_mac_filter_set(priv, addr, mask, slot);
}

static int adin1140_filter_unicast(struct adin1140_priv *priv)
{
	/* Only the first 2 filter slots support masking, so no unicast
	 * address will ever need a mask. The first slots are used for the
	 * all multicast and broadcast filter.
	 */
	return adin1140_mac_filter_set(priv, priv->netdev->dev_addr, NULL,
				       ADIN1140_MAC_FILT_UC_SLOT);
}

static int adin1140_filter_all_multicast(struct adin1140_priv *priv, bool en)
{
	u8 multicast_addr[ETH_ALEN] = {1, 0, 0, 0, 0, 0};

	if (en)
		return adin1140_mac_filter_set(priv, multicast_addr,
					       multicast_addr,
					       ADIN1140_MAC_FILT_MC_SLOT);

	return adin1140_mac_filter_clear(priv, ADIN1140_MAC_FILT_MC_SLOT);
}

static int adin1140_filter_broadcast(struct adin1140_priv *priv, bool enabled)
{
	u8 mask[ETH_ALEN];

	if (enabled) {
		memset(mask, 0xFF, ETH_ALEN);
		return adin1140_mac_filter_set(priv, mask, mask,
					       ADIN1140_MAC_FILT_BC_SLOT);
	}

	return adin1140_mac_filter_clear(priv, ADIN1140_MAC_FILT_BC_SLOT);
}

static int adin1140_default_filter_config(struct adin1140_priv *priv)
{
	int ret;

	ret = adin1140_filter_broadcast(priv, true);
	if (ret)
		return ret;

	return adin1140_filter_unicast(priv);
}

static int adin1140_promiscuous_mode(struct adin1140_priv *priv, bool enabled)
{
	int ret;
	u32 val;

	ret = oa_tc6_read_register(priv->tc6, OA_TC6_REG_CONFIG2, &val);
	if (ret)
		return ret;

	if (enabled)
		val |= ADIN1140_MAC_CONFIG2_FWD_UNK2HOST;
	else
		val &= ~ADIN1140_MAC_CONFIG2_FWD_UNK2HOST;

	return oa_tc6_write_register(priv->tc6, OA_TC6_REG_CONFIG2, val);
}

static int adin1140_rx_mode(struct net_device *dev,
			    struct netdev_hw_addr_list *uc,
			    struct netdev_hw_addr_list *mc)
{
	struct adin1140_priv *priv = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	bool all_multi, promisc;
	u32 mac_addrs;
	u8 slot, i;
	int ret;

	/* The ADIN1140 has 16 dest MAC address filter slots:
	 * 0 - reserved for all multicast filter.
	 * 1 - reserved for broadcast filter.
	 * 2 - reserved for the device's own unicast MAC.
	 * 3 -> 15 - available for other unicast/multicast filters.
	 */

	mac_addrs = netdev_hw_addr_list_count(uc);
	all_multi = false;
	promisc = false;

	if (priv->netdev->flags & IFF_PROMISC)
		promisc = true;
	else if (priv->netdev->flags & IFF_ALLMULTI)
		all_multi = true;
	else
		mac_addrs += netdev_hw_addr_list_count(mc);

	if (mac_addrs > ADIN1140_MAC_FILT_AVAIL) {
		/* The filter table is full. Enable promisc mode. */
		promisc = true;
	}

	ret = adin1140_promiscuous_mode(priv, promisc);
	if (ret)
		return ret;

	ret = adin1140_filter_all_multicast(priv, all_multi);
	if (ret)
		return ret;

	slot = ADIN1140_MAC_FILT_UC_SLOT + 1;
	if (!promisc) {
		netdev_hw_addr_list_for_each(ha, uc) {
			ret = adin1140_mac_filter_set(priv, ha->addr, NULL,
						      slot);
			if (ret)
				return ret;

			slot++;
		}

		if (!all_multi) {
			netdev_hw_addr_list_for_each(ha, mc) {
				ret = adin1140_mac_filter_set(priv, ha->addr,
							      NULL, slot);
				if (ret)
					return ret;

				slot++;
			}
		}
	}

	for (i = slot; i < ADIN1140_MAC_FILT_MAX_SLOT; i++) {
		ret = adin1140_mac_filter_clear(priv, i);
		if (ret)
			return ret;
	}

	return 0;
}

static void adin1140_stats_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct adin1140_priv *priv;
	u32 reg_val;
	int ret;
	u32 i;

	priv = container_of(dwork, struct adin1140_priv, stats_work);

	for (i = 0; i < ARRAY_SIZE(adin1140_stat_regs); i++) {
		ret = oa_tc6_read_register_mms(priv->tc6, OA_TC6_MAC_MMS1,
					       adin1140_stat_regs[i],
					       &reg_val);
		if (ret)
			goto out;

		scoped_guard(spinlock, &priv->stat_lock)
			priv->stats[i] += reg_val;
	}

out:
	schedule_delayed_work(dwork, ADIN1140_STATS_CHECK_DELAY);
}

static int adin1140_configure(struct adin1140_priv *priv)
{
	u32 reg_val;
	int ret;

	ret = oa_tc6_zero_align_receive_frame_enable(priv->tc6);
	if (ret)
		return ret;

	/* Disable MAC loopback */
	ret = oa_tc6_write_register_mms(priv->tc6, OA_TC6_MAC_MMS1,
					ADIN1140_MAC_P1_LOOP_ADDR_REG, 0x0);
	if (ret)
		return ret;

	ret = oa_tc6_read_register(priv->tc6, OA_TC6_REG_CONFIG2, &reg_val);
	if (ret)
		return ret;

	reg_val |= ADIN1140_MAC_CONFIG2_STAT_CLR_RD;

	ret = oa_tc6_write_register(priv->tc6, OA_TC6_REG_CONFIG2, reg_val);
	if (ret)
		return ret;

	return adin1140_default_filter_config(priv);
}

static int adin1140_open(struct net_device *netdev)
{
	struct adin1140_priv *priv = netdev_priv(netdev);

	schedule_delayed_work(&priv->stats_work, ADIN1140_STATS_CHECK_DELAY);

	phy_start(netdev->phydev);
	netif_start_queue(netdev);

	return 0;
}

static int adin1140_close(struct net_device *netdev)
{
	struct adin1140_priv *priv = netdev_priv(netdev);

	cancel_delayed_work_sync(&priv->stats_work);

	netif_stop_queue(netdev);
	phy_stop(netdev->phydev);

	return 0;
}

static netdev_tx_t adin1140_start_xmit(struct sk_buff *skb,
				       struct net_device *netdev)
{
	struct adin1140_priv *priv = netdev_priv(netdev);

	/* The MAC doesn't automatically pad the frame to a 60 byte minimum
	 * size in case the host sends a shorter skb, so we have to do it in
	 * the driver. The FCS will be added by the MAC.
	 */
	if (skb_put_padto(skb, ETH_ZLEN))
		return NETDEV_TX_OK;

	return oa_tc6_start_xmit(priv->tc6, skb);
}

static int adin1140_set_mac_address(struct net_device *netdev, void *addr)
{
	struct adin1140_priv *priv = netdev_priv(netdev);
	struct sockaddr *address = addr;
	u8 mask[ETH_ALEN];
	int ret;

	ret = eth_prepare_mac_addr_change(netdev, addr);
	if (ret < 0)
		return ret;

	if (ether_addr_equal(address->sa_data, netdev->dev_addr))
		return 0;

	memset(mask, 0xFF, ETH_ALEN);
	ret = adin1140_mac_filter_set(priv, address->sa_data, mask,
				      ADIN1140_MAC_FILT_UC_SLOT);
	if (ret)
		return ret;

	eth_commit_mac_addr_change(netdev, addr);

	return 0;
}

static void __adin1140_ndo_get_stats64(struct adin1140_priv *priv,
				       struct rtnl_link_stats64 *storage)
{
	storage->rx_errors = priv->stats[rx_crc_errors] +
			     priv->stats[rx_align_errors] +
			     priv->stats[rx_preamble_errors] +
			     priv->stats[rx_short_frame_errors] +
			     priv->stats[rx_long_frame_errors] +
			     priv->stats[rx_phy_errors] +
			     priv->stats[rx_ifg_errors];

	storage->tx_errors = priv->stats[tx_excess_collision] +
			     priv->stats[tx_underrun];

	storage->rx_dropped = priv->stats[rx_fifo_full_dropped] +
			      priv->stats[rx_addr_filter_dropped];

	storage->multicast = priv->stats[rx_mc_frames];

	storage->collisions = priv->stats[tx_single_collision] +
			      priv->stats[tx_multi_collision];

	storage->rx_length_errors = priv->stats[rx_short_frame_errors] +
				    priv->stats[rx_long_frame_errors];
	storage->rx_over_errors = priv->stats[rx_fifo_full_dropped];
	storage->rx_crc_errors = priv->stats[rx_crc_errors];
	storage->rx_frame_errors = priv->stats[rx_align_errors];
	storage->rx_missed_errors = priv->stats[rx_fifo_full_dropped];

	storage->tx_aborted_errors = priv->stats[tx_excess_collision];
	storage->tx_fifo_errors = priv->stats[tx_underrun];
	storage->tx_window_errors = priv->stats[tx_late_collision];
}

static void adin1140_ndo_get_stats64(struct net_device *dev,
				     struct rtnl_link_stats64 *storage)
{
	struct adin1140_priv *priv = netdev_priv(dev);

	storage->rx_packets = priv->netdev->stats.rx_packets;
	storage->tx_packets = priv->netdev->stats.tx_packets;

	storage->rx_bytes = priv->netdev->stats.rx_bytes;
	storage->tx_bytes = priv->netdev->stats.tx_bytes;

	scoped_guard(spinlock, &priv->stat_lock)
		__adin1140_ndo_get_stats64(priv, storage);
}

static void adin1140_get_drvinfo(struct net_device *netdev,
				 struct ethtool_drvinfo *info)
{
	strscpy(info->driver, "ADIN1140", sizeof(info->driver));
	strscpy(info->bus_info, dev_name(netdev->dev.parent),
		sizeof(info->bus_info));
}

static void adin1140_get_ethtool_stats(struct net_device *netdev,
				       struct ethtool_stats *stats, u64 *data)
{
	struct adin1140_priv *priv = netdev_priv(netdev);
	u32 i;

	scoped_guard(spinlock, &priv->stat_lock) {
		for (i = 0; i < ARRAY_SIZE(adin1140_stats); i++)
			data[i] = priv->stats[adin1140_stats[i].idx];
	}
}

static void adin1140_get_ethtool_strings(struct net_device *netdev, u32 sset,
					 u8 *p)
{
	u32 i;

	switch (sset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(adin1140_stats); i++)
			ethtool_puts(&p, adin1140_stats[i].name);

		break;
	}
}

static int adin1140_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(adin1140_stats);
	default:
		return -EOPNOTSUPP;
	}
}

static void __adin1140_eth_mac_stats(struct adin1140_priv *priv,
				     struct ethtool_eth_mac_stats *mac_stats)
{
	mac_stats->FramesReceivedOK = priv->stats[rx_frames];
	mac_stats->BroadcastFramesReceivedOK = priv->stats[rx_bc_frames];
	mac_stats->MulticastFramesReceivedOK = priv->stats[rx_mc_frames];
	mac_stats->FrameCheckSequenceErrors = priv->stats[rx_crc_errors];
	mac_stats->AlignmentErrors = priv->stats[rx_align_errors];
	mac_stats->FrameTooLongErrors = priv->stats[rx_long_frame_errors];
	mac_stats->FramesLostDueToIntMACRcvError =
					priv->stats[rx_fifo_full_dropped];
	mac_stats->FramesTransmittedOK = priv->stats[tx_frames];
	mac_stats->BroadcastFramesXmittedOK = priv->stats[tx_bc_frames];
	mac_stats->MulticastFramesXmittedOK = priv->stats[tx_mc_frames];
	mac_stats->SingleCollisionFrames = priv->stats[tx_single_collision];
	mac_stats->MultipleCollisionFrames = priv->stats[tx_multi_collision];
	mac_stats->FramesWithDeferredXmissions = priv->stats[tx_deferred];
	mac_stats->LateCollisions = priv->stats[tx_late_collision];
	mac_stats->FramesAbortedDueToXSColls =
					priv->stats[tx_excess_collision];
	mac_stats->FramesLostDueToIntMACXmitError = priv->stats[tx_underrun];
}

static void adin1140_get_eth_mac_stats(struct net_device *netdev,
				       struct ethtool_eth_mac_stats *mac_stats)
{
	struct adin1140_priv *priv = netdev_priv(netdev);

	scoped_guard(spinlock, &priv->stat_lock)
		__adin1140_eth_mac_stats(priv, mac_stats);
}

static int adin1140_mdiobus_read(struct mii_bus *bus, int addr, int regnum)
{
	/* The ADIN1140's standard PHY C22 register map (OA TC6 0xFF00 -
	 * 0xFF1F), of which only 0xFF00 - 0xFF03 are implemented) cannot be
	 * accessed while frames are being received by the PHY. In case this
	 * happens the CONFIG0 and CONFIG2 register values will get corrupted,
	 * getting a random value. Both reads and writes cause the same
	 * behavior. This is a workaround that avoids MDIO accesses all
	 * together. Since this is a 10BASE-T1S PHY, only the loopback and
	 * reset (AN) bits in the control register (0x0) can be written.
	 * These functionalities have custom implementations in the PHY
	 * driver. C45 accesses do not cause this issue.
	 */

	switch (regnum) {
	case MII_BMCR:
		return ADIN1140_PHY_CTRL_DEFAULT;
	case MII_BMSR:
		return ADIN1140_PHY_STATUS_DEFAULT;
	case MII_PHYSID1:
		return ADIN1140_PHY_ID1;
	case MII_PHYSID2:
		return ADIN1140_PHY_ID2;
	default:
		return 0xFFFF;
	}
}

static int adin1140_mdiobus_write(struct mii_bus *bus, int addr, int regnum,
				  u16 val)
{
	return -EIO;
}

static int adin1140_mdio_register(struct adin1140_priv *priv,
				  struct spi_device *spidev)
{
	priv->mdiobus = devm_mdiobus_alloc(&spidev->dev);
	if (!priv->mdiobus)
		return dev_err_probe(&spidev->dev, -ENOMEM,
				     "MDIO bus alloc failed\n");

	priv->mdiobus->name = "adin1140-mdiobus";
	priv->mdiobus->priv = priv->tc6;
	priv->mdiobus->parent = &spidev->dev;
	priv->mdiobus->phy_mask = GENMASK(31, 1);
	priv->mdiobus->read = adin1140_mdiobus_read;
	priv->mdiobus->write = adin1140_mdiobus_write;
	priv->mdiobus->read_c45 = oa_tc6_mdiobus_read_c45;
	priv->mdiobus->write_c45 = oa_tc6_mdiobus_write_c45;

	snprintf(priv->mdiobus->id, MII_BUS_ID_SIZE, "adin1140-%s.%u",
		 dev_name(&spidev->dev), spi_get_chipselect(spidev, 0));

	return devm_mdiobus_register(&spidev->dev, priv->mdiobus);
}

static void adin1140_handle_link_change(struct net_device *netdev)
{
	phy_print_status(netdev->phydev);
}

static void adin1140_phy_remove(void *data)
{
	phy_disconnect(data);
}

static int adin1140_phy_init(struct adin1140_priv *priv,
			     struct spi_device *spidev)
{
	int ret;

	ret = adin1140_mdio_register(priv, spidev);
	if (ret)
		return ret;

	priv->phydev = phy_find_first(priv->mdiobus);
	if (!priv->phydev)
		return dev_err_probe(&spidev->dev, -ENODEV, "No PHY found\n");

	priv->phydev->is_internal = true;
	ret = phy_connect_direct(priv->netdev, priv->phydev,
				 &adin1140_handle_link_change,
				 PHY_INTERFACE_MODE_INTERNAL);
	if (ret)
		return dev_err_probe(&spidev->dev, ret,
				     "Can't attach PHY to %s\n",
				     priv->mdiobus->id);

	ret = devm_add_action_or_reset(&spidev->dev, adin1140_phy_remove,
				       priv->phydev);
	if (ret)
		return ret;

	phy_attached_info(priv->phydev);

	return 0;
}

static const struct ethtool_ops adin1140_ethtool_ops = {
	.get_drvinfo = adin1140_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_ethtool_stats = adin1140_get_ethtool_stats,
	.get_sset_count = adin1140_get_sset_count,
	.get_strings = adin1140_get_ethtool_strings,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
	.get_eth_mac_stats = adin1140_get_eth_mac_stats,
};

static const struct net_device_ops adin1140_netdev_ops = {
	.ndo_open = adin1140_open,
	.ndo_stop = adin1140_close,
	.ndo_start_xmit	= adin1140_start_xmit,
	.ndo_set_mac_address = adin1140_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_rx_mode_async = adin1140_rx_mode,
	.ndo_eth_ioctl = phy_do_ioctl_running,
	.ndo_get_stats64 = adin1140_ndo_get_stats64,
};

static void adin1140_oa_tc6_remove(void *data)
{
	oa_tc6_exit(data);
}

static int adin1140_probe(struct spi_device *spi)
{
	struct oa_tc6_quirks tc6_quirks = {};
	struct net_device *netdev;
	struct adin1140_priv *priv;
	int ret;

	netdev = devm_alloc_etherdev(&spi->dev, sizeof(struct adin1140_priv));
	if (!netdev)
		return -ENOMEM;

	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	spi_set_drvdata(spi, priv);
	spin_lock_init(&priv->stat_lock);

	tc6_quirks.quirk_flags = OA_TC6_BROKEN_PHY;

	priv->tc6 = oa_tc6_init(spi, netdev, &tc6_quirks);
	if (!priv->tc6)
		return -ENODEV;

	ret = devm_add_action_or_reset(&spi->dev, adin1140_oa_tc6_remove,
				       priv->tc6);
	if (ret)
		return ret;

	ret = adin1140_phy_init(priv, spi);
	if (ret)
		return ret;

	if (device_get_ethdev_address(&spi->dev, netdev))
		eth_hw_addr_random(netdev);

	ret = adin1140_configure(priv);
	if (ret)
		return ret;

	INIT_DELAYED_WORK(&priv->stats_work, adin1140_stats_work);

	netdev->if_port = IF_PORT_10BASET;
	netdev->irq = spi->irq;
	netdev->netdev_ops = &adin1140_netdev_ops;
	netdev->ethtool_ops = &adin1140_ethtool_ops;
	netdev->netns_immutable = true;
	netdev->priv_flags |= IFF_LIVE_ADDR_CHANGE |
			      IFF_UNICAST_FLT;

	ret = devm_register_netdev(&spi->dev, netdev);
	if (ret)
		return dev_err_probe(&spi->dev, ret,
				     "Failed to register netdev");

	return 0;
}

static const struct spi_device_id adin1140_spi_id[] = {
	{ .name = "ad3306" },
	{ .name = "adin1140" },
	{},
};
MODULE_DEVICE_TABLE(spi, adin1140_spi_id);

static const struct of_device_id adin1140_match_table[] = {
	{ .compatible = "adi,ad3306" },
	{ .compatible = "adi,adin1140" },
	{ }
};
MODULE_DEVICE_TABLE(of, adin1140_match_table);

static struct spi_driver adin1140_driver = {
	.driver = {
		.name = "adin1140",
		.of_match_table = adin1140_match_table,
	 },
	.probe = adin1140_probe,
	.id_table = adin1140_spi_id,
};
module_spi_driver(adin1140_driver);

MODULE_DESCRIPTION("Analog Devices, Inc. ADIN1140 10BASE-T1S MAC-PHY");
MODULE_AUTHOR("Ciprian Regus <ciprian.regus@analog.com>");
MODULE_LICENSE("GPL");
