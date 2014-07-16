/*
 * AMD 10Gb Ethernet driver
 *
 * This file is available to you under your choice of the following two
 * licenses:
 *
 * License 1: GPLv2
 *
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
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
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
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

static const struct xgbe_stats xgbe_gstring_stats[] = {
	XGMAC_MMC_STAT("tx_bytes", txoctetcount_gb),
	XGMAC_MMC_STAT("tx_packets", txframecount_gb),
	XGMAC_MMC_STAT("tx_unicast_packets", txunicastframes_gb),
	XGMAC_MMC_STAT("tx_broadcast_packets", txbroadcastframes_gb),
	XGMAC_MMC_STAT("tx_multicast_packets", txmulticastframes_gb),
	XGMAC_MMC_STAT("tx_vlan_packets", txvlanframes_g),
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
};
#define XGBE_STATS_COUNT	ARRAY_SIZE(xgbe_gstring_stats)

static void xgbe_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	int i;

	DBGPR("-->%s\n", __func__);

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < XGBE_STATS_COUNT; i++) {
			memcpy(data, xgbe_gstring_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		break;
	}

	DBGPR("<--%s\n", __func__);
}

static void xgbe_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	u8 *stat;
	int i;

	DBGPR("-->%s\n", __func__);

	pdata->hw_if.read_mmc_stats(pdata);
	for (i = 0; i < XGBE_STATS_COUNT; i++) {
		stat = (u8 *)pdata + xgbe_gstring_stats[i].stat_offset;
		*data++ = *(u64 *)stat;
	}

	DBGPR("<--%s\n", __func__);
}

static int xgbe_get_sset_count(struct net_device *netdev, int stringset)
{
	int ret;

	DBGPR("-->%s\n", __func__);

	switch (stringset) {
	case ETH_SS_STATS:
		ret = XGBE_STATS_COUNT;
		break;

	default:
		ret = -EOPNOTSUPP;
	}

	DBGPR("<--%s\n", __func__);

	return ret;
}

static void xgbe_get_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	DBGPR("-->xgbe_get_pauseparam\n");

	pause->autoneg = pdata->pause_autoneg;
	pause->tx_pause = pdata->tx_pause;
	pause->rx_pause = pdata->rx_pause;

	DBGPR("<--xgbe_get_pauseparam\n");
}

static int xgbe_set_pauseparam(struct net_device *netdev,
			       struct ethtool_pauseparam *pause)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct phy_device *phydev = pdata->phydev;
	int ret = 0;

	DBGPR("-->xgbe_set_pauseparam\n");

	DBGPR("  autoneg = %d, tx_pause = %d, rx_pause = %d\n",
	      pause->autoneg, pause->tx_pause, pause->rx_pause);

	pdata->pause_autoneg = pause->autoneg;
	if (pause->autoneg) {
		phydev->advertising |= ADVERTISED_Pause;
		phydev->advertising |= ADVERTISED_Asym_Pause;

	} else {
		phydev->advertising &= ~ADVERTISED_Pause;
		phydev->advertising &= ~ADVERTISED_Asym_Pause;

		pdata->tx_pause = pause->tx_pause;
		pdata->rx_pause = pause->rx_pause;
	}

	if (netif_running(netdev))
		ret = phy_start_aneg(phydev);

	DBGPR("<--xgbe_set_pauseparam\n");

	return ret;
}

static int xgbe_get_settings(struct net_device *netdev,
			     struct ethtool_cmd *cmd)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	int ret;

	DBGPR("-->xgbe_get_settings\n");

	if (!pdata->phydev)
		return -ENODEV;

	spin_lock_irq(&pdata->lock);

	ret = phy_ethtool_gset(pdata->phydev, cmd);
	cmd->transceiver = XCVR_EXTERNAL;

	spin_unlock_irq(&pdata->lock);

	DBGPR("<--xgbe_get_settings\n");

	return ret;
}

static int xgbe_set_settings(struct net_device *netdev,
			     struct ethtool_cmd *cmd)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct phy_device *phydev = pdata->phydev;
	u32 speed;
	int ret;

	DBGPR("-->xgbe_set_settings\n");

	if (!pdata->phydev)
		return -ENODEV;

	spin_lock_irq(&pdata->lock);

	speed = ethtool_cmd_speed(cmd);

	ret = -EINVAL;
	if (cmd->phy_address != phydev->addr)
		goto unlock;

	if ((cmd->autoneg != AUTONEG_ENABLE) &&
	    (cmd->autoneg != AUTONEG_DISABLE))
		goto unlock;

	if ((cmd->autoneg == AUTONEG_DISABLE) &&
	    (((speed != SPEED_10000) && (speed != SPEED_1000)) ||
	     (cmd->duplex != DUPLEX_FULL)))
		goto unlock;

	if (cmd->autoneg == AUTONEG_ENABLE) {
		/* Clear settings needed to force speeds */
		phydev->supported &= ~SUPPORTED_1000baseT_Full;
		phydev->supported &= ~SUPPORTED_10000baseT_Full;
	} else {
		/* Add settings needed to force speed */
		phydev->supported |= SUPPORTED_1000baseT_Full;
		phydev->supported |= SUPPORTED_10000baseT_Full;
	}

	cmd->advertising &= phydev->supported;
	if ((cmd->autoneg == AUTONEG_ENABLE) && !cmd->advertising)
		goto unlock;

	ret = 0;
	phydev->autoneg = cmd->autoneg;
	phydev->speed = speed;
	phydev->duplex = cmd->duplex;
	phydev->advertising = cmd->advertising;

	if (cmd->autoneg == AUTONEG_ENABLE)
		phydev->advertising |= ADVERTISED_Autoneg;
	else
		phydev->advertising &= ~ADVERTISED_Autoneg;

	if (netif_running(netdev))
		ret = phy_start_aneg(phydev);

unlock:
	spin_unlock_irq(&pdata->lock);

	DBGPR("<--xgbe_set_settings\n");

	return ret;
}

static void xgbe_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *drvinfo)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	strlcpy(drvinfo->driver, XGBE_DRV_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, XGBE_DRV_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->bus_info, dev_name(pdata->dev),
		sizeof(drvinfo->bus_info));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version), "%d.%d.%d",
		 XGMAC_IOREAD_BITS(pdata, MAC_VR, USERVER),
		 XGMAC_IOREAD_BITS(pdata, MAC_VR, DEVID),
		 XGMAC_IOREAD_BITS(pdata, MAC_VR, SNPSVER));
	drvinfo->n_stats = XGBE_STATS_COUNT;
}

static int xgbe_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	unsigned int riwt;

	DBGPR("-->xgbe_get_coalesce\n");

	memset(ec, 0, sizeof(struct ethtool_coalesce));

	riwt = pdata->rx_riwt;
	ec->rx_coalesce_usecs = hw_if->riwt_to_usec(pdata, riwt);
	ec->rx_max_coalesced_frames = pdata->rx_frames;

	ec->tx_coalesce_usecs = pdata->tx_usecs;
	ec->tx_max_coalesced_frames = pdata->tx_frames;

	DBGPR("<--xgbe_get_coalesce\n");

	return 0;
}

static int xgbe_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	unsigned int rx_frames, rx_riwt, rx_usecs;
	unsigned int tx_frames, tx_usecs;

	DBGPR("-->xgbe_set_coalesce\n");

	/* Check for not supported parameters  */
	if ((ec->rx_coalesce_usecs_irq) ||
	    (ec->rx_max_coalesced_frames_irq) ||
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
	    (ec->rate_sample_interval))
		return -EOPNOTSUPP;

	/* Can only change rx-frames when interface is down (see
	 * rx_descriptor_init in xgbe-dev.c)
	 */
	rx_frames = pdata->rx_frames;
	if (rx_frames != ec->rx_max_coalesced_frames && netif_running(netdev)) {
		netdev_alert(netdev,
			     "interface must be down to change rx-frames\n");
		return -EINVAL;
	}

	rx_riwt = hw_if->usec_to_riwt(pdata, ec->rx_coalesce_usecs);
	rx_frames = ec->rx_max_coalesced_frames;

	/* Use smallest possible value if conversion resulted in zero */
	if (ec->rx_coalesce_usecs && !rx_riwt)
		rx_riwt = 1;

	/* Check the bounds of values for Rx */
	if (rx_riwt > XGMAC_MAX_DMA_RIWT) {
		rx_usecs = hw_if->riwt_to_usec(pdata, XGMAC_MAX_DMA_RIWT);
		netdev_alert(netdev, "rx-usec is limited to %d usecs\n",
			     rx_usecs);
		return -EINVAL;
	}
	if (rx_frames > pdata->channel->rx_ring->rdesc_count) {
		netdev_alert(netdev, "rx-frames is limited to %d frames\n",
			     pdata->channel->rx_ring->rdesc_count);
		return -EINVAL;
	}

	tx_usecs = ec->tx_coalesce_usecs;
	tx_frames = ec->tx_max_coalesced_frames;

	/* Check the bounds of values for Tx */
	if (tx_frames > pdata->channel->tx_ring->rdesc_count) {
		netdev_alert(netdev, "tx-frames is limited to %d frames\n",
			     pdata->channel->tx_ring->rdesc_count);
		return -EINVAL;
	}

	pdata->rx_riwt = rx_riwt;
	pdata->rx_frames = rx_frames;
	hw_if->config_rx_coalesce(pdata);

	pdata->tx_usecs = tx_usecs;
	pdata->tx_frames = tx_frames;
	hw_if->config_tx_coalesce(pdata);

	DBGPR("<--xgbe_set_coalesce\n");

	return 0;
}

static const struct ethtool_ops xgbe_ethtool_ops = {
	.get_settings = xgbe_get_settings,
	.set_settings = xgbe_set_settings,
	.get_drvinfo = xgbe_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_coalesce = xgbe_get_coalesce,
	.set_coalesce = xgbe_set_coalesce,
	.get_pauseparam = xgbe_get_pauseparam,
	.set_pauseparam = xgbe_set_pauseparam,
	.get_strings = xgbe_get_strings,
	.get_ethtool_stats = xgbe_get_ethtool_stats,
	.get_sset_count = xgbe_get_sset_count,
};

struct ethtool_ops *xgbe_get_ethtool_ops(void)
{
	return (struct ethtool_ops *)&xgbe_ethtool_ops;
}
