/*
 * Copyright (c) 2013 Johannes Berg <johannes@sipsolutions.net>
 *
 *  This file is free software: you may copy, redistribute and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation, either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This file is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 * Copyright (c) 2012 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/pci.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mdio.h>
#include <linux/interrupt.h>
#include <asm/byteorder.h>

#include "alx.h"
#include "reg.h"
#include "hw.h"

/* The order of these strings must match the order of the fields in
 * struct alx_hw_stats
 * See hw.h
 */
static const char alx_gstrings_stats[][ETH_GSTRING_LEN] = {
	"rx_packets",
	"rx_bcast_packets",
	"rx_mcast_packets",
	"rx_pause_packets",
	"rx_ctrl_packets",
	"rx_fcs_errors",
	"rx_length_errors",
	"rx_bytes",
	"rx_runt_packets",
	"rx_fragments",
	"rx_64B_or_less_packets",
	"rx_65B_to_127B_packets",
	"rx_128B_to_255B_packets",
	"rx_256B_to_511B_packets",
	"rx_512B_to_1023B_packets",
	"rx_1024B_to_1518B_packets",
	"rx_1519B_to_mtu_packets",
	"rx_oversize_packets",
	"rx_rxf_ov_drop_packets",
	"rx_rrd_ov_drop_packets",
	"rx_align_errors",
	"rx_bcast_bytes",
	"rx_mcast_bytes",
	"rx_address_errors",
	"tx_packets",
	"tx_bcast_packets",
	"tx_mcast_packets",
	"tx_pause_packets",
	"tx_exc_defer_packets",
	"tx_ctrl_packets",
	"tx_defer_packets",
	"tx_bytes",
	"tx_64B_or_less_packets",
	"tx_65B_to_127B_packets",
	"tx_128B_to_255B_packets",
	"tx_256B_to_511B_packets",
	"tx_512B_to_1023B_packets",
	"tx_1024B_to_1518B_packets",
	"tx_1519B_to_mtu_packets",
	"tx_single_collision",
	"tx_multiple_collisions",
	"tx_late_collision",
	"tx_abort_collision",
	"tx_underrun",
	"tx_trd_eop",
	"tx_length_errors",
	"tx_trunc_packets",
	"tx_bcast_bytes",
	"tx_mcast_bytes",
	"tx_update",
};

#define ALX_NUM_STATS ARRAY_SIZE(alx_gstrings_stats)


static u32 alx_get_supported_speeds(struct alx_hw *hw)
{
	u32 supported = SUPPORTED_10baseT_Half |
			SUPPORTED_10baseT_Full |
			SUPPORTED_100baseT_Half |
			SUPPORTED_100baseT_Full;

	if (alx_hw_giga(hw))
		supported |= SUPPORTED_1000baseT_Full;

	BUILD_BUG_ON(SUPPORTED_10baseT_Half != ADVERTISED_10baseT_Half);
	BUILD_BUG_ON(SUPPORTED_10baseT_Full != ADVERTISED_10baseT_Full);
	BUILD_BUG_ON(SUPPORTED_100baseT_Half != ADVERTISED_100baseT_Half);
	BUILD_BUG_ON(SUPPORTED_100baseT_Full != ADVERTISED_100baseT_Full);
	BUILD_BUG_ON(SUPPORTED_1000baseT_Full != ADVERTISED_1000baseT_Full);

	return supported;
}

static int alx_get_link_ksettings(struct net_device *netdev,
				  struct ethtool_link_ksettings *cmd)
{
	struct alx_priv *alx = netdev_priv(netdev);
	struct alx_hw *hw = &alx->hw;
	u32 supported, advertising;

	supported = SUPPORTED_Autoneg |
			  SUPPORTED_TP |
			  SUPPORTED_Pause |
			  SUPPORTED_Asym_Pause;
	if (alx_hw_giga(hw))
		supported |= SUPPORTED_1000baseT_Full;
	supported |= alx_get_supported_speeds(hw);

	advertising = ADVERTISED_TP;
	if (hw->adv_cfg & ADVERTISED_Autoneg)
		advertising |= hw->adv_cfg;

	cmd->base.port = PORT_TP;
	cmd->base.phy_address = 0;

	if (hw->adv_cfg & ADVERTISED_Autoneg)
		cmd->base.autoneg = AUTONEG_ENABLE;
	else
		cmd->base.autoneg = AUTONEG_DISABLE;

	if (hw->flowctrl & ALX_FC_ANEG && hw->adv_cfg & ADVERTISED_Autoneg) {
		if (hw->flowctrl & ALX_FC_RX) {
			advertising |= ADVERTISED_Pause;

			if (!(hw->flowctrl & ALX_FC_TX))
				advertising |= ADVERTISED_Asym_Pause;
		} else if (hw->flowctrl & ALX_FC_TX) {
			advertising |= ADVERTISED_Asym_Pause;
		}
	}

	mutex_lock(&alx->mtx);
	cmd->base.speed = hw->link_speed;
	cmd->base.duplex = hw->duplex;
	mutex_unlock(&alx->mtx);

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);

	return 0;
}

static int alx_set_link_ksettings(struct net_device *netdev,
				  const struct ethtool_link_ksettings *cmd)
{
	struct alx_priv *alx = netdev_priv(netdev);
	struct alx_hw *hw = &alx->hw;
	u32 adv_cfg;
	u32 advertising;
	int ret;

	ethtool_convert_link_mode_to_legacy_u32(&advertising,
						cmd->link_modes.advertising);

	if (cmd->base.autoneg == AUTONEG_ENABLE) {
		if (advertising & ~alx_get_supported_speeds(hw))
			return -EINVAL;
		adv_cfg = advertising | ADVERTISED_Autoneg;
	} else {
		adv_cfg = alx_speed_to_ethadv(cmd->base.speed,
					      cmd->base.duplex);

		if (!adv_cfg || adv_cfg == ADVERTISED_1000baseT_Full)
			return -EINVAL;
	}

	hw->adv_cfg = adv_cfg;

	mutex_lock(&alx->mtx);
	ret = alx_setup_speed_duplex(hw, adv_cfg, hw->flowctrl);
	mutex_unlock(&alx->mtx);

	return ret;
}

static void alx_get_pauseparam(struct net_device *netdev,
			       struct ethtool_pauseparam *pause)
{
	struct alx_priv *alx = netdev_priv(netdev);
	struct alx_hw *hw = &alx->hw;

	mutex_lock(&alx->mtx);
	pause->autoneg = !!(hw->flowctrl & ALX_FC_ANEG &&
			    hw->adv_cfg & ADVERTISED_Autoneg);
	pause->tx_pause = !!(hw->flowctrl & ALX_FC_TX);
	pause->rx_pause = !!(hw->flowctrl & ALX_FC_RX);
	mutex_unlock(&alx->mtx);
}


static int alx_set_pauseparam(struct net_device *netdev,
			      struct ethtool_pauseparam *pause)
{
	struct alx_priv *alx = netdev_priv(netdev);
	struct alx_hw *hw = &alx->hw;
	int err = 0;
	bool reconfig_phy = false;
	u8 fc = 0;

	if (pause->tx_pause)
		fc |= ALX_FC_TX;
	if (pause->rx_pause)
		fc |= ALX_FC_RX;
	if (pause->autoneg)
		fc |= ALX_FC_ANEG;

	mutex_lock(&alx->mtx);

	/* restart auto-neg for auto-mode */
	if (hw->adv_cfg & ADVERTISED_Autoneg) {
		if (!((fc ^ hw->flowctrl) & ALX_FC_ANEG))
			reconfig_phy = true;
		if (fc & hw->flowctrl & ALX_FC_ANEG &&
		    (fc ^ hw->flowctrl) & (ALX_FC_RX | ALX_FC_TX))
			reconfig_phy = true;
	}

	if (reconfig_phy) {
		err = alx_setup_speed_duplex(hw, hw->adv_cfg, fc);
		if (err) {
			mutex_unlock(&alx->mtx);
			return err;
		}
	}

	/* flow control on mac */
	if ((fc ^ hw->flowctrl) & (ALX_FC_RX | ALX_FC_TX))
		alx_cfg_mac_flowcontrol(hw, fc);

	hw->flowctrl = fc;
	mutex_unlock(&alx->mtx);

	return 0;
}

static u32 alx_get_msglevel(struct net_device *netdev)
{
	struct alx_priv *alx = netdev_priv(netdev);

	return alx->msg_enable;
}

static void alx_set_msglevel(struct net_device *netdev, u32 data)
{
	struct alx_priv *alx = netdev_priv(netdev);

	alx->msg_enable = data;
}

static void alx_get_ethtool_stats(struct net_device *netdev,
				  struct ethtool_stats *estats, u64 *data)
{
	struct alx_priv *alx = netdev_priv(netdev);
	struct alx_hw *hw = &alx->hw;

	spin_lock(&alx->stats_lock);

	alx_update_hw_stats(hw);
	BUILD_BUG_ON(sizeof(hw->stats) != ALX_NUM_STATS * sizeof(u64));
	memcpy(data, &hw->stats, sizeof(hw->stats));

	spin_unlock(&alx->stats_lock);
}

static void alx_get_strings(struct net_device *netdev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buf, &alx_gstrings_stats, sizeof(alx_gstrings_stats));
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static int alx_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ALX_NUM_STATS;
	default:
		return -EINVAL;
	}
}

const struct ethtool_ops alx_ethtool_ops = {
	.get_pauseparam	= alx_get_pauseparam,
	.set_pauseparam	= alx_set_pauseparam,
	.get_msglevel	= alx_get_msglevel,
	.set_msglevel	= alx_set_msglevel,
	.get_link	= ethtool_op_get_link,
	.get_strings	= alx_get_strings,
	.get_sset_count	= alx_get_sset_count,
	.get_ethtool_stats	= alx_get_ethtool_stats,
	.get_link_ksettings	= alx_get_link_ksettings,
	.set_link_ksettings	= alx_set_link_ksettings,
};
