/* Synopsys DesignWare Core Enterprise Ethernet (XLGMAC) Driver
 *
 * Copyright (c) 2017 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is dual-licensed; you may select either version 2 of
 * the GNU General Public License ("GPL") or BSD license ("BSD").
 *
 * This Synopsys DWC XLGMAC software driver and associated documentation
 * (hereinafter the "Software") is an unsupported proprietary work of
 * Synopsys, Inc. unless otherwise expressly agreed to in writing between
 * Synopsys and you. The Software IS NOT an item of Licensed Software or a
 * Licensed Product under any End User Software License Agreement or
 * Agreement for Licensed Products with Synopsys or any supplement thereto.
 * Synopsys is a registered trademark of Synopsys, Inc. Other names included
 * in the SOFTWARE may be the trademarks of their respective owners.
 */

#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

#include "dwc-xlgmac.h"
#include "dwc-xlgmac-reg.h"

struct xlgmac_stats_desc {
	char stat_string[ETH_GSTRING_LEN];
	int stat_offset;
};

#define XLGMAC_STAT(str, var)					\
	{							\
		str,						\
		offsetof(struct xlgmac_pdata, stats.var),	\
	}

static const struct xlgmac_stats_desc xlgmac_gstring_stats[] = {
	/* MMC TX counters */
	XLGMAC_STAT("tx_bytes", txoctetcount_gb),
	XLGMAC_STAT("tx_bytes_good", txoctetcount_g),
	XLGMAC_STAT("tx_packets", txframecount_gb),
	XLGMAC_STAT("tx_packets_good", txframecount_g),
	XLGMAC_STAT("tx_unicast_packets", txunicastframes_gb),
	XLGMAC_STAT("tx_broadcast_packets", txbroadcastframes_gb),
	XLGMAC_STAT("tx_broadcast_packets_good", txbroadcastframes_g),
	XLGMAC_STAT("tx_multicast_packets", txmulticastframes_gb),
	XLGMAC_STAT("tx_multicast_packets_good", txmulticastframes_g),
	XLGMAC_STAT("tx_vlan_packets_good", txvlanframes_g),
	XLGMAC_STAT("tx_64_byte_packets", tx64octets_gb),
	XLGMAC_STAT("tx_65_to_127_byte_packets", tx65to127octets_gb),
	XLGMAC_STAT("tx_128_to_255_byte_packets", tx128to255octets_gb),
	XLGMAC_STAT("tx_256_to_511_byte_packets", tx256to511octets_gb),
	XLGMAC_STAT("tx_512_to_1023_byte_packets", tx512to1023octets_gb),
	XLGMAC_STAT("tx_1024_to_max_byte_packets", tx1024tomaxoctets_gb),
	XLGMAC_STAT("tx_underflow_errors", txunderflowerror),
	XLGMAC_STAT("tx_pause_frames", txpauseframes),

	/* MMC RX counters */
	XLGMAC_STAT("rx_bytes", rxoctetcount_gb),
	XLGMAC_STAT("rx_bytes_good", rxoctetcount_g),
	XLGMAC_STAT("rx_packets", rxframecount_gb),
	XLGMAC_STAT("rx_unicast_packets_good", rxunicastframes_g),
	XLGMAC_STAT("rx_broadcast_packets_good", rxbroadcastframes_g),
	XLGMAC_STAT("rx_multicast_packets_good", rxmulticastframes_g),
	XLGMAC_STAT("rx_vlan_packets", rxvlanframes_gb),
	XLGMAC_STAT("rx_64_byte_packets", rx64octets_gb),
	XLGMAC_STAT("rx_65_to_127_byte_packets", rx65to127octets_gb),
	XLGMAC_STAT("rx_128_to_255_byte_packets", rx128to255octets_gb),
	XLGMAC_STAT("rx_256_to_511_byte_packets", rx256to511octets_gb),
	XLGMAC_STAT("rx_512_to_1023_byte_packets", rx512to1023octets_gb),
	XLGMAC_STAT("rx_1024_to_max_byte_packets", rx1024tomaxoctets_gb),
	XLGMAC_STAT("rx_undersize_packets_good", rxundersize_g),
	XLGMAC_STAT("rx_oversize_packets_good", rxoversize_g),
	XLGMAC_STAT("rx_crc_errors", rxcrcerror),
	XLGMAC_STAT("rx_crc_errors_small_packets", rxrunterror),
	XLGMAC_STAT("rx_crc_errors_giant_packets", rxjabbererror),
	XLGMAC_STAT("rx_length_errors", rxlengtherror),
	XLGMAC_STAT("rx_out_of_range_errors", rxoutofrangetype),
	XLGMAC_STAT("rx_fifo_overflow_errors", rxfifooverflow),
	XLGMAC_STAT("rx_watchdog_errors", rxwatchdogerror),
	XLGMAC_STAT("rx_pause_frames", rxpauseframes),

	/* Extra counters */
	XLGMAC_STAT("tx_tso_packets", tx_tso_packets),
	XLGMAC_STAT("rx_split_header_packets", rx_split_header_packets),
	XLGMAC_STAT("tx_process_stopped", tx_process_stopped),
	XLGMAC_STAT("rx_process_stopped", rx_process_stopped),
	XLGMAC_STAT("tx_buffer_unavailable", tx_buffer_unavailable),
	XLGMAC_STAT("rx_buffer_unavailable", rx_buffer_unavailable),
	XLGMAC_STAT("fatal_bus_error", fatal_bus_error),
	XLGMAC_STAT("tx_vlan_packets", tx_vlan_packets),
	XLGMAC_STAT("rx_vlan_packets", rx_vlan_packets),
	XLGMAC_STAT("napi_poll_isr", napi_poll_isr),
	XLGMAC_STAT("napi_poll_txtimer", napi_poll_txtimer),
};

#define XLGMAC_STATS_COUNT	ARRAY_SIZE(xlgmac_gstring_stats)

static void xlgmac_ethtool_get_drvinfo(struct net_device *netdev,
				       struct ethtool_drvinfo *drvinfo)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	u32 ver = pdata->hw_feat.version;
	u32 snpsver, devid, userver;

	strlcpy(drvinfo->driver, pdata->drv_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, pdata->drv_ver, sizeof(drvinfo->version));
	strlcpy(drvinfo->bus_info, dev_name(pdata->dev),
		sizeof(drvinfo->bus_info));
	/* S|SNPSVER: Synopsys-defined Version
	 * D|DEVID: Indicates the Device family
	 * U|USERVER: User-defined Version
	 */
	snpsver = XLGMAC_GET_REG_BITS(ver, MAC_VR_SNPSVER_POS,
				      MAC_VR_SNPSVER_LEN);
	devid = XLGMAC_GET_REG_BITS(ver, MAC_VR_DEVID_POS,
				    MAC_VR_DEVID_LEN);
	userver = XLGMAC_GET_REG_BITS(ver, MAC_VR_USERVER_POS,
				      MAC_VR_USERVER_LEN);
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "S.D.U: %x.%x.%x", snpsver, devid, userver);
}

static u32 xlgmac_ethtool_get_msglevel(struct net_device *netdev)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);

	return pdata->msg_enable;
}

static void xlgmac_ethtool_set_msglevel(struct net_device *netdev,
					u32 msglevel)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);

	pdata->msg_enable = msglevel;
}

static void xlgmac_ethtool_get_channels(struct net_device *netdev,
					struct ethtool_channels *channel)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);

	channel->max_rx = XLGMAC_MAX_DMA_CHANNELS;
	channel->max_tx = XLGMAC_MAX_DMA_CHANNELS;
	channel->rx_count = pdata->rx_q_count;
	channel->tx_count = pdata->tx_q_count;
}

static int xlgmac_ethtool_get_coalesce(struct net_device *netdev,
				       struct ethtool_coalesce *ec)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);

	ec->rx_coalesce_usecs = pdata->rx_usecs;
	ec->rx_max_coalesced_frames = pdata->rx_frames;
	ec->tx_max_coalesced_frames = pdata->tx_frames;

	return 0;
}

static int xlgmac_ethtool_set_coalesce(struct net_device *netdev,
				       struct ethtool_coalesce *ec)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	struct xlgmac_hw_ops *hw_ops = &pdata->hw_ops;
	unsigned int rx_frames, rx_riwt, rx_usecs;
	unsigned int tx_frames;

	rx_usecs = ec->rx_coalesce_usecs;
	rx_riwt = hw_ops->usec_to_riwt(pdata, rx_usecs);
	rx_frames = ec->rx_max_coalesced_frames;
	tx_frames = ec->tx_max_coalesced_frames;

	if ((rx_riwt > XLGMAC_MAX_DMA_RIWT) ||
	    (rx_riwt < XLGMAC_MIN_DMA_RIWT) ||
	    (rx_frames > pdata->rx_desc_count))
		return -EINVAL;

	if (tx_frames > pdata->tx_desc_count)
		return -EINVAL;

	pdata->rx_riwt = rx_riwt;
	pdata->rx_usecs = rx_usecs;
	pdata->rx_frames = rx_frames;
	hw_ops->config_rx_coalesce(pdata);

	pdata->tx_frames = tx_frames;
	hw_ops->config_tx_coalesce(pdata);

	return 0;
}

static void xlgmac_ethtool_get_strings(struct net_device *netdev,
				       u32 stringset, u8 *data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < XLGMAC_STATS_COUNT; i++) {
			memcpy(data, xlgmac_gstring_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static int xlgmac_ethtool_get_sset_count(struct net_device *netdev,
					 int stringset)
{
	int ret;

	switch (stringset) {
	case ETH_SS_STATS:
		ret = XLGMAC_STATS_COUNT;
		break;

	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static void xlgmac_ethtool_get_ethtool_stats(struct net_device *netdev,
					     struct ethtool_stats *stats,
					     u64 *data)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	u8 *stat;
	int i;

	pdata->hw_ops.read_mmc_stats(pdata);
	for (i = 0; i < XLGMAC_STATS_COUNT; i++) {
		stat = (u8 *)pdata + xlgmac_gstring_stats[i].stat_offset;
		*data++ = *(u64 *)stat;
	}
}

static const struct ethtool_ops xlgmac_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_USECS |
				     ETHTOOL_COALESCE_MAX_FRAMES,
	.get_drvinfo = xlgmac_ethtool_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_msglevel = xlgmac_ethtool_get_msglevel,
	.set_msglevel = xlgmac_ethtool_set_msglevel,
	.get_channels = xlgmac_ethtool_get_channels,
	.get_coalesce = xlgmac_ethtool_get_coalesce,
	.set_coalesce = xlgmac_ethtool_set_coalesce,
	.get_strings = xlgmac_ethtool_get_strings,
	.get_sset_count = xlgmac_ethtool_get_sset_count,
	.get_ethtool_stats = xlgmac_ethtool_get_ethtool_stats,
};

const struct ethtool_ops *xlgmac_get_ethtool_ops(void)
{
	return &xlgmac_ethtool_ops;
}
