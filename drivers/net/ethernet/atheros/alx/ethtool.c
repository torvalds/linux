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

static int alx_get_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	struct alx_priv *alx = netdev_priv(netdev);
	struct alx_hw *hw = &alx->hw;

	ecmd->supported = SUPPORTED_Autoneg |
			  SUPPORTED_TP |
			  SUPPORTED_Pause |
			  SUPPORTED_Asym_Pause;
	if (alx_hw_giga(hw))
		ecmd->supported |= SUPPORTED_1000baseT_Full;
	ecmd->supported |= alx_get_supported_speeds(hw);

	ecmd->advertising = ADVERTISED_TP;
	if (hw->adv_cfg & ADVERTISED_Autoneg)
		ecmd->advertising |= hw->adv_cfg;

	ecmd->port = PORT_TP;
	ecmd->phy_address = 0;

	if (hw->adv_cfg & ADVERTISED_Autoneg)
		ecmd->autoneg = AUTONEG_ENABLE;
	else
		ecmd->autoneg = AUTONEG_DISABLE;
	ecmd->transceiver = XCVR_INTERNAL;

	if (hw->flowctrl & ALX_FC_ANEG && hw->adv_cfg & ADVERTISED_Autoneg) {
		if (hw->flowctrl & ALX_FC_RX) {
			ecmd->advertising |= ADVERTISED_Pause;

			if (!(hw->flowctrl & ALX_FC_TX))
				ecmd->advertising |= ADVERTISED_Asym_Pause;
		} else if (hw->flowctrl & ALX_FC_TX) {
			ecmd->advertising |= ADVERTISED_Asym_Pause;
		}
	}

	ethtool_cmd_speed_set(ecmd, hw->link_speed);
	ecmd->duplex = hw->duplex;

	return 0;
}

static int alx_set_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	struct alx_priv *alx = netdev_priv(netdev);
	struct alx_hw *hw = &alx->hw;
	u32 adv_cfg;

	ASSERT_RTNL();

	if (ecmd->autoneg == AUTONEG_ENABLE) {
		if (ecmd->advertising & ~alx_get_supported_speeds(hw))
			return -EINVAL;
		adv_cfg = ecmd->advertising | ADVERTISED_Autoneg;
	} else {
		adv_cfg = alx_speed_to_ethadv(ethtool_cmd_speed(ecmd),
					      ecmd->duplex);

		if (!adv_cfg || adv_cfg == ADVERTISED_1000baseT_Full)
			return -EINVAL;
	}

	hw->adv_cfg = adv_cfg;
	return alx_setup_speed_duplex(hw, adv_cfg, hw->flowctrl);
}

static void alx_get_pauseparam(struct net_device *netdev,
			       struct ethtool_pauseparam *pause)
{
	struct alx_priv *alx = netdev_priv(netdev);
	struct alx_hw *hw = &alx->hw;

	pause->autoneg = !!(hw->flowctrl & ALX_FC_ANEG &&
			    hw->adv_cfg & ADVERTISED_Autoneg);
	pause->tx_pause = !!(hw->flowctrl & ALX_FC_TX);
	pause->rx_pause = !!(hw->flowctrl & ALX_FC_RX);
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

	ASSERT_RTNL();

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
		if (err)
			return err;
	}

	/* flow control on mac */
	if ((fc ^ hw->flowctrl) & (ALX_FC_RX | ALX_FC_TX))
		alx_cfg_mac_flowcontrol(hw, fc);

	hw->flowctrl = fc;

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

const struct ethtool_ops alx_ethtool_ops = {
	.get_settings	= alx_get_settings,
	.set_settings	= alx_set_settings,
	.get_pauseparam	= alx_get_pauseparam,
	.set_pauseparam	= alx_set_pauseparam,
	.get_msglevel	= alx_get_msglevel,
	.set_msglevel	= alx_set_msglevel,
	.get_link	= ethtool_op_get_link,
};
