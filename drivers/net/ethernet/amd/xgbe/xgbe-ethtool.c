/*
 * AMD 10Gb Ethernet driver
 *
 * This file is available to you under your choice of the following two
 * licenses:
 *
 * License 1: GPLv2
 *
 * Copyright (c) 2014-2016 Advanced Micro Devices, Inc.
 *
 * This file is free software; you may copy, redistribute and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * License 2: Modified BSD
 *
 * Copyright (c) 2014-2016 Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Advanced Micro Devices, Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/spinlock.h>
#include <linux/phy.h>
#include <linux/net_tstamp.h>

#include "xgbe.h"
#include "xgbe-common.h"

struct xgbe_stats {
	char stat_string[ETH_GSTRING_LEN];
	int stat_size;
	int stat_offset;
};

#define XGMAC_MMC_STAT(_string, _var)				\
	{ _string,						\
	  FIELD_SIZEOF(struct xgbe_mmc_stats, _var),		\
	  offsetof(struct xgbe_prv_data, mmc_stats._var),	\
	}

#define XGMAC_EXT_STAT(_string, _var)				\
	{ _string,						\
	  FIELD_SIZEOF(struct xgbe_ext_stats, _var),		\
	  offsetof(struct xgbe_prv_data, ext_stats._var),	\
	}

static const struct xgbe_stats xgbe_gstring_stats[] = {
	XGMAC_MMC_STAT("tx_bytes", txoctetcount_gb),
	XGMAC_MMC_STAT("tx_packets", txframecount_gb),
	XGMAC_MMC_STAT("tx_unicast_packets", txunicastframes_gb),
	XGMAC_MMC_STAT("tx_broadcast_packets", txbroadcastframes_gb),
	XGMAC_MMC_STAT("tx_multicast_packets", txmulticastframes_gb),
	XGMAC_MMC_STAT("tx_vlan_packets", txvlanframes_g),
	XGMAC_EXT_STAT("tx_tso_packets", tx_tso_packets),
	XGMAC_MMC_STAT("tx_64_byte_packets", tx64octets_gb),
	XGMAC_MMC_STAT("tx_65_to_127_byte_packets", tx65to127octets_gb),
	XGMAC_MMC_STAT("tx_128_to_255_byte_packets", tx128to255octets_gb),
	XGMAC_MMC_STAT("tx_256_to_511_byte_packets", tx256to511octets_gb),
	XGMAC_MMC_STAT("tx_512_to_1023_byte_packets", tx512to1023octets_gb),
	XGMAC_MMC_STAT("tx_1024_to_max_byte_packets", tx1024tomaxoctets_gb),
	XGMAC_MMC_STAT("tx_underflow_errors", txunderflowerror),
	XGMAC_MMC_STAT("tx_pause_frames", txpauseframes),

	XGMAC_MMC_STAT("rx_bytes", rxoctetcount_gb),
	XGMAC_MMC_STAT("rx_packets", rxframecount_gb),
	XGMAC_MMC_STAT("rx_unicast_packets", rxunicastframes_g),
	XGMAC_MMC_STAT("rx_broadcast_packets", rxbroadcastframes_g),
	XGMAC_MMC_STAT("rx_multicast_packets", rxmulticastframes_g),
	XGMAC_MMC_STAT("rx_vlan_packets", rxvlanframes_gb),
	XGMAC_MMC_STAT("rx_64_byte_packets", rx64octets_gb),
	XGMAC_MMC_STAT("rx_65_to_127_byte_packets", rx65to127octets_gb),
	XGMAC_MMC_STAT("rx_128_to_255_byte_packets", rx128to255octets_gb),
	XGMAC_MMC_STAT("rx_256_to_511_byte_packets", rx256to511octets_gb),
	XGMAC_MMC_STAT("rx_512_to_1023_byte_packets", rx512to1023octets_gb),
	XGMAC_MMC_STAT("rx_1024_to_max_byte_packets", rx1024tomaxoctets_gb),
	XGMAC_MMC_STAT("rx_undersize_packets", rxundersize_g),
	XGMAC_MMC_STAT("rx_oversize_packets", rxoversize_g),
	XGMAC_MMC_STAT("rx_crc_errors", rxcrcerror),
	XGMAC_MMC_STAT("rx_crc_errors_small_packets", rxrunterror),
	XGMAC_MMC_STAT("rx_crc_errors_giant_packets", rxjabbererror),
	XGMAC_MMC_STAT("rx_length_errors", rxlengtherror),
	XGMAC_MMC_STAT("rx_out_of_range_errors", rxoutofrangetype),
	XGMAC_MMC_STAT("rx_fifo_overflow_errors", rxfifooverflow),
	XGMAC_MMC_STAT("rx_watchdog_errors", rxwatchdogerror),
	XGMAC_MMC_STAT("rx_pause_frames", rxpauseframes),
	XGMAC_EXT_STAT("rx_split_header_packets", rx_split_header_packets),
	XGMAC_EXT_STAT("rx_buffer_unavailable", rx_buffer_unavailable),
};

#define XGBE_STATS_COUNT	ARRAY_SIZE(xgbe_gstring_stats)

static void xgbe_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < XGBE_STATS_COUNT; i++) {
			memcpy(data, xgbe_gstring_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void xgbe_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	u8 *stat;
	int i;

	pdata->hw_if.read_mmc_stats(pdata);
	for (i = 0; i < XGBE_STATS_COUNT; i++) {
		stat = (u8 *)pdata + xgbe_gstring_stats[i].stat_offset;
		*data++ = *(u64 *)stat;
	}
}

static int xgbe_get_sset_count(struct net_device *netdev, int stringset)
{
	int ret;

	switch (stringset) {
	case ETH_SS_STATS:
		ret = XGBE_STATS_COUNT;
		break;

	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static void xgbe_get_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	pause->autoneg = pdata->phy.pause_autoneg;
	pause->tx_pause = pdata->phy.tx_pause;
	pause->rx_pause = pdata->phy.rx_pause;
}

static int xgbe_set_pauseparam(struct net_device *netdev,
			       struct ethtool_pauseparam *pause)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	int ret = 0;

	if (pause->autoneg && (pdata->phy.autoneg != AUTONEG_ENABLE)) {
		netdev_err(netdev,
			   "autoneg disabled, pause autoneg not avialable\n");
		return -EINVAL;
	}

	pdata->phy.pause_autoneg = pause->autoneg;
	pdata->phy.tx_pause = pause->tx_pause;
	pdata->phy.rx_pause = pause->rx_pause;

	pdata->phy.advertising &= ~ADVERTISED_Pause;
	pdata->phy.advertising &= ~ADVERTISED_Asym_Pause;

	if (pause->rx_pause) {
		pdata->phy.advertising |= ADVERTISED_Pause;
		pdata->phy.advertising |= ADVERTISED_Asym_Pause;
	}

	if (pause->tx_pause)
		pdata->phy.advertising ^= ADVERTISED_Asym_Pause;

	if (netif_running(netdev))
		ret = pdata->phy_if.phy_config_aneg(pdata);

	return ret;
}

static int xgbe_get_settings(struct net_device *netdev,
			     struct ethtool_cmd *cmd)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	cmd->phy_address = pdata->phy.address;

	cmd->supported = pdata->phy.supported;
	cmd->advertising = pdata->phy.advertising;
	cmd->lp_advertising = pdata->phy.lp_advertising;

	cmd->autoneg = pdata->phy.autoneg;
	ethtool_cmd_speed_set(cmd, pdata->phy.speed);
	cmd->duplex = pdata->phy.duplex;

	cmd->port = PORT_NONE;
	cmd->transceiver = XCVR_INTERNAL;

	return 0;
}

static int xgbe_set_settings(struct net_device *netdev,
			     struct ethtool_cmd *cmd)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	u32 speed;
	int ret;

	speed = ethtool_cmd_speed(cmd);

	if (cmd->phy_address != pdata->phy.address) {
		netdev_err(netdev, "invalid phy address %hhu\n",
			   cmd->phy_address);
		return -EINVAL;
	}

	if ((cmd->autoneg != AUTONEG_ENABLE) &&
	    (cmd->autoneg != AUTONEG_DISABLE)) {
		netdev_err(netdev, "unsupported autoneg %hhu\n",
			   cmd->autoneg);
		return -EINVAL;
	}

	if (cmd->autoneg == AUTONEG_DISABLE) {
		switch (speed) {
		case SPEED_10000:
			break;
		case SPEED_2500:
			if (pdata->speed_set != XGBE_SPEEDSET_2500_10000) {
				netdev_err(netdev, "unsupported speed %u\n",
					   speed);
				return -EINVAL;
			}
			break;
		case SPEED_1000:
			if (pdata->speed_set != XGBE_SPEEDSET_1000_10000) {
				netdev_err(netdev, "unsupported speed %u\n",
					   speed);
				return -EINVAL;
			}
			break;
		default:
			netdev_err(netdev, "unsupported speed %u\n", speed);
			return -EINVAL;
		}

		if (cmd->duplex != DUPLEX_FULL) {
			netdev_err(netdev, "unsupported duplex %hhu\n",
				   cmd->duplex);
			return -EINVAL;
		}
	}

	netif_dbg(pdata, link, netdev,
		  "requested advertisement %#x, phy supported %#x\n",
		  cmd->advertising, pdata->phy.supported);

	cmd->advertising &= pdata->phy.supported;
	if ((cmd->autoneg == AUTONEG_ENABLE) && !cmd->advertising) {
		netdev_err(netdev,
			   "unsupported requested advertisement\n");
		return -EINVAL;
	}

	ret = 0;
	pdata->phy.autoneg = cmd->autoneg;
	pdata->phy.speed = speed;
	pdata->phy.duplex = cmd->duplex;
	pdata->phy.advertising = cmd->advertising;

	if (cmd->autoneg == AUTONEG_ENABLE)
		pdata->phy.advertising |= ADVERTISED_Autoneg;
	else
		pdata->phy.advertising &= ~ADVERTISED_Autoneg;

	if (netif_running(netdev))
		ret = pdata->phy_if.phy_config_aneg(pdata);

	return ret;
}

static void xgbe_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *drvinfo)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_hw_features *hw_feat = &pdata->hw_feat;

	strlcpy(drvinfo->driver, XGBE_DRV_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, XGBE_DRV_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->bus_info, dev_name(pdata->dev),
		sizeof(drvinfo->bus_info));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version), "%d.%d.%d",
		 XGMAC_GET_BITS(hw_feat->version, MAC_VR, USERVER),
		 XGMAC_GET_BITS(hw_feat->version, MAC_VR, DEVID),
		 XGMAC_GET_BITS(hw_feat->version, MAC_VR, SNPSVER));
}

static u32 xgbe_get_msglevel(struct net_device *netdev)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	return pdata->msg_enable;
}

static void xgbe_set_msglevel(struct net_device *netdev, u32 msglevel)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	pdata->msg_enable = msglevel;
}

static int xgbe_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	memset(ec, 0, sizeof(struct ethtool_coalesce));

	ec->rx_coalesce_usecs = pdata->rx_usecs;
	ec->rx_max_coalesced_frames = pdata->rx_frames;

	ec->tx_max_coalesced_frames = pdata->tx_frames;

	return 0;
}

static int xgbe_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	unsigned int rx_frames, rx_riwt, rx_usecs;
	unsigned int tx_frames;

	/* Check for not supported parameters  */
	if ((ec->rx_coalesce_usecs_irq) ||
	    (ec->rx_max_coalesced_frames_irq) ||
	    (ec->tx_coalesce_usecs) ||
	    (ec->tx_coalesce_usecs_irq) ||
	    (ec->tx_max_coalesced_frames_irq) ||
	    (ec->stats_block_coalesce_usecs) ||
	    (ec->use_adaptive_rx_coalesce) ||
	    (ec->use_adaptive_tx_coalesce) ||
	    (ec->pkt_rate_low) ||
	    (ec->rx_coalesce_usecs_low) ||
	    (ec->rx_max_coalesced_frames_low) ||
	    (ec->tx_coalesce_usecs_low) ||
	    (ec->tx_max_coalesced_frames_low) ||
	    (ec->pkt_rate_high) ||
	    (ec->rx_coalesce_usecs_high) ||
	    (ec->rx_max_coalesced_frames_high) ||
	    (ec->tx_coalesce_usecs_high) ||
	    (ec->tx_max_coalesced_frames_high) ||
	    (ec->rate_sample_interval)) {
		netdev_err(netdev, "unsupported coalescing parameter\n");
		return -EOPNOTSUPP;
	}

	rx_riwt = hw_if->usec_to_riwt(pdata, ec->rx_coalesce_usecs);
	rx_usecs = ec->rx_coalesce_usecs;
	rx_frames = ec->rx_max_coalesced_frames;

	/* Use smallest possible value if conversion resulted in zero */
	if (rx_usecs && !rx_riwt)
		rx_riwt = 1;

	/* Check the bounds of values for Rx */
	if (rx_riwt > XGMAC_MAX_DMA_RIWT) {
		netdev_err(netdev, "rx-usec is limited to %d usecs\n",
			   hw_if->riwt_to_usec(pdata, XGMAC_MAX_DMA_RIWT));
		return -EINVAL;
	}
	if (rx_frames > pdata->rx_desc_count) {
		netdev_err(netdev, "rx-frames is limited to %d frames\n",
			   pdata->rx_desc_count);
		return -EINVAL;
	}

	tx_frames = ec->tx_max_coalesced_frames;

	/* Check the bounds of values for Tx */
	if (tx_frames > pdata->tx_desc_count) {
		netdev_err(netdev, "tx-frames is limited to %d frames\n",
			   pdata->tx_desc_count);
		return -EINVAL;
	}

	pdata->rx_riwt = rx_riwt;
	pdata->rx_usecs = rx_usecs;
	pdata->rx_frames = rx_frames;
	hw_if->config_rx_coalesce(pdata);

	pdata->tx_frames = tx_frames;
	hw_if->config_tx_coalesce(pdata);

	return 0;
}

static int xgbe_get_rxnfc(struct net_device *netdev,
			  struct ethtool_rxnfc *rxnfc, u32 *rule_locs)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	switch (rxnfc->cmd) {
	case ETHTOOL_GRXRINGS:
		rxnfc->data = pdata->rx_ring_count;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static u32 xgbe_get_rxfh_key_size(struct net_device *netdev)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	return sizeof(pdata->rss_key);
}

static u32 xgbe_get_rxfh_indir_size(struct net_device *netdev)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	return ARRAY_SIZE(pdata->rss_table);
}

static int xgbe_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			 u8 *hfunc)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	unsigned int i;

	if (indir) {
		for (i = 0; i < ARRAY_SIZE(pdata->rss_table); i++)
			indir[i] = XGMAC_GET_BITS(pdata->rss_table[i],
						  MAC_RSSDR, DMCH);
	}

	if (key)
		memcpy(key, pdata->rss_key, sizeof(pdata->rss_key));

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	return 0;
}

static int xgbe_set_rxfh(struct net_device *netdev, const u32 *indir,
			 const u8 *key, const u8 hfunc)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	unsigned int ret;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP) {
		netdev_err(netdev, "unsupported hash function\n");
		return -EOPNOTSUPP;
	}

	if (indir) {
		ret = hw_if->set_rss_lookup_table(pdata, indir);
		if (ret)
			return ret;
	}

	if (key) {
		ret = hw_if->set_rss_hash_key(pdata, key);
		if (ret)
			return ret;
	}

	return 0;
}

static int xgbe_get_ts_info(struct net_device *netdev,
			    struct ethtool_ts_info *ts_info)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	ts_info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				   SOF_TIMESTAMPING_RX_SOFTWARE |
				   SOF_TIMESTAMPING_SOFTWARE |
				   SOF_TIMESTAMPING_TX_HARDWARE |
				   SOF_TIMESTAMPING_RX_HARDWARE |
				   SOF_TIMESTAMPING_RAW_HARDWARE;

	if (pdata->ptp_clock)
		ts_info->phc_index = ptp_clock_index(pdata->ptp_clock);
	else
		ts_info->phc_index = -1;

	ts_info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);
	ts_info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			      (1 << HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
			      (1 << HWTSTAMP_FILTER_PTP_V1_L4_SYNC) |
			      (1 << HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ) |
			      (1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
			      (1 << HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
			      (1 << HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ) |
			      (1 << HWTSTAMP_FILTER_PTP_V2_EVENT) |
			      (1 << HWTSTAMP_FILTER_PTP_V2_SYNC) |
			      (1 << HWTSTAMP_FILTER_PTP_V2_DELAY_REQ) |
			      (1 << HWTSTAMP_FILTER_ALL);

	return 0;
}

static const struct ethtool_ops xgbe_ethtool_ops = {
	.get_settings = xgbe_get_settings,
	.set_settings = xgbe_set_settings,
	.get_drvinfo = xgbe_get_drvinfo,
	.get_msglevel = xgbe_get_msglevel,
	.set_msglevel = xgbe_set_msglevel,
	.get_link = ethtool_op_get_link,
	.get_coalesce = xgbe_get_coalesce,
	.set_coalesce = xgbe_set_coalesce,
	.get_pauseparam = xgbe_get_pauseparam,
	.set_pauseparam = xgbe_set_pauseparam,
	.get_strings = xgbe_get_strings,
	.get_ethtool_stats = xgbe_get_ethtool_stats,
	.get_sset_count = xgbe_get_sset_count,
	.get_rxnfc = xgbe_get_rxnfc,
	.get_rxfh_key_size = xgbe_get_rxfh_key_size,
	.get_rxfh_indir_size = xgbe_get_rxfh_indir_size,
	.get_rxfh = xgbe_get_rxfh,
	.set_rxfh = xgbe_set_rxfh,
	.get_ts_info = xgbe_get_ts_info,
};

const struct ethtool_ops *xgbe_get_ethtool_ops(void)
{
	return &xgbe_ethtool_ops;
}
