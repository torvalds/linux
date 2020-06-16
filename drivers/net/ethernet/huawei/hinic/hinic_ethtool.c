// SPDX-License-Identifier: GPL-2.0
/* Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>

#include "hinic_hw_qp.h"
#include "hinic_hw_dev.h"
#include "hinic_port.h"
#include "hinic_tx.h"
#include "hinic_rx.h"
#include "hinic_dev.h"

#define SET_LINK_STR_MAX_LEN	128

#define GET_SUPPORTED_MODE	0
#define GET_ADVERTISED_MODE	1

#define ETHTOOL_ADD_SUPPORTED_SPEED_LINK_MODE(ecmd, mode)	\
		((ecmd)->supported |=	\
		(1UL << hw_to_ethtool_link_mode_table[mode].link_mode_bit))
#define ETHTOOL_ADD_ADVERTISED_SPEED_LINK_MODE(ecmd, mode)	\
		((ecmd)->advertising |=	\
		(1UL << hw_to_ethtool_link_mode_table[mode].link_mode_bit))
#define ETHTOOL_ADD_SUPPORTED_LINK_MODE(ecmd, mode)	\
				((ecmd)->supported |= SUPPORTED_##mode)
#define ETHTOOL_ADD_ADVERTISED_LINK_MODE(ecmd, mode)	\
				((ecmd)->advertising |= ADVERTISED_##mode)

struct hw2ethtool_link_mode {
	enum ethtool_link_mode_bit_indices link_mode_bit;
	u32 speed;
	enum hinic_link_mode hw_link_mode;
};

struct cmd_link_settings {
	u64	supported;
	u64	advertising;

	u32	speed;
	u8	duplex;
	u8	port;
	u8	autoneg;
};

static u32 hw_to_ethtool_speed[LINK_SPEED_LEVELS] = {
	SPEED_10, SPEED_100,
	SPEED_1000, SPEED_10000,
	SPEED_25000, SPEED_40000,
	SPEED_100000
};

static struct hw2ethtool_link_mode
	hw_to_ethtool_link_mode_table[HINIC_LINK_MODE_NUMBERS] = {
	{
		.link_mode_bit = ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
		.speed = SPEED_10000,
		.hw_link_mode = HINIC_10GE_BASE_KR,
	},
	{
		.link_mode_bit = ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
		.speed = SPEED_40000,
		.hw_link_mode = HINIC_40GE_BASE_KR4,
	},
	{
		.link_mode_bit = ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
		.speed = SPEED_40000,
		.hw_link_mode = HINIC_40GE_BASE_CR4,
	},
	{
		.link_mode_bit = ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
		.speed = SPEED_100000,
		.hw_link_mode = HINIC_100GE_BASE_KR4,
	},
	{
		.link_mode_bit = ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
		.speed = SPEED_100000,
		.hw_link_mode = HINIC_100GE_BASE_CR4,
	},
	{
		.link_mode_bit = ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
		.speed = SPEED_25000,
		.hw_link_mode = HINIC_25GE_BASE_KR_S,
	},
	{
		.link_mode_bit = ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
		.speed = SPEED_25000,
		.hw_link_mode = HINIC_25GE_BASE_CR_S,
	},
	{
		.link_mode_bit = ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
		.speed = SPEED_25000,
		.hw_link_mode = HINIC_25GE_BASE_KR,
	},
	{
		.link_mode_bit = ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
		.speed = SPEED_25000,
		.hw_link_mode = HINIC_25GE_BASE_CR,
	},
	{
		.link_mode_bit = ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
		.speed = SPEED_1000,
		.hw_link_mode = HINIC_GE_BASE_KX,
	},
};

static void set_link_speed(struct ethtool_link_ksettings *link_ksettings,
			   enum hinic_speed speed)
{
	switch (speed) {
	case HINIC_SPEED_10MB_LINK:
		link_ksettings->base.speed = SPEED_10;
		break;

	case HINIC_SPEED_100MB_LINK:
		link_ksettings->base.speed = SPEED_100;
		break;

	case HINIC_SPEED_1000MB_LINK:
		link_ksettings->base.speed = SPEED_1000;
		break;

	case HINIC_SPEED_10GB_LINK:
		link_ksettings->base.speed = SPEED_10000;
		break;

	case HINIC_SPEED_25GB_LINK:
		link_ksettings->base.speed = SPEED_25000;
		break;

	case HINIC_SPEED_40GB_LINK:
		link_ksettings->base.speed = SPEED_40000;
		break;

	case HINIC_SPEED_100GB_LINK:
		link_ksettings->base.speed = SPEED_100000;
		break;

	default:
		link_ksettings->base.speed = SPEED_UNKNOWN;
		break;
	}
}

static int hinic_get_link_mode_index(enum hinic_link_mode link_mode)
{
	int i = 0;

	for (i = 0; i < HINIC_LINK_MODE_NUMBERS; i++) {
		if (link_mode == hw_to_ethtool_link_mode_table[i].hw_link_mode)
			break;
	}

	return i;
}

static void hinic_add_ethtool_link_mode(struct cmd_link_settings *link_settings,
					enum hinic_link_mode hw_link_mode,
					u32 name)
{
	enum hinic_link_mode link_mode;
	int idx = 0;

	for (link_mode = 0; link_mode < HINIC_LINK_MODE_NUMBERS; link_mode++) {
		if (hw_link_mode & ((u32)1 << link_mode)) {
			idx = hinic_get_link_mode_index(link_mode);
			if (idx >= HINIC_LINK_MODE_NUMBERS)
				continue;

			if (name == GET_SUPPORTED_MODE)
				ETHTOOL_ADD_SUPPORTED_SPEED_LINK_MODE
					(link_settings, idx);
			else
				ETHTOOL_ADD_ADVERTISED_SPEED_LINK_MODE
					(link_settings, idx);
		}
	}
}

static void hinic_link_port_type(struct cmd_link_settings *link_settings,
				 enum hinic_port_type port_type)
{
	switch (port_type) {
	case HINIC_PORT_ELEC:
	case HINIC_PORT_TP:
		ETHTOOL_ADD_SUPPORTED_LINK_MODE(link_settings, TP);
		ETHTOOL_ADD_ADVERTISED_LINK_MODE(link_settings, TP);
		link_settings->port = PORT_TP;
		break;

	case HINIC_PORT_AOC:
	case HINIC_PORT_FIBRE:
		ETHTOOL_ADD_SUPPORTED_LINK_MODE(link_settings, FIBRE);
		ETHTOOL_ADD_ADVERTISED_LINK_MODE(link_settings, FIBRE);
		link_settings->port = PORT_FIBRE;
		break;

	case HINIC_PORT_COPPER:
		ETHTOOL_ADD_SUPPORTED_LINK_MODE(link_settings, FIBRE);
		ETHTOOL_ADD_ADVERTISED_LINK_MODE(link_settings, FIBRE);
		link_settings->port = PORT_DA;
		break;

	case HINIC_PORT_BACKPLANE:
		ETHTOOL_ADD_SUPPORTED_LINK_MODE(link_settings, Backplane);
		ETHTOOL_ADD_ADVERTISED_LINK_MODE(link_settings, Backplane);
		link_settings->port = PORT_NONE;
		break;

	default:
		link_settings->port = PORT_OTHER;
		break;
	}
}

static int hinic_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings
				    *link_ksettings)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_link_mode_cmd link_mode = { 0 };
	struct hinic_pause_config pause_info = { 0 };
	struct cmd_link_settings settings = { 0 };
	enum hinic_port_link_state link_state;
	struct hinic_port_cap port_cap;
	int err;

	ethtool_link_ksettings_zero_link_mode(link_ksettings, supported);
	ethtool_link_ksettings_zero_link_mode(link_ksettings, advertising);

	link_ksettings->base.speed = SPEED_UNKNOWN;
	link_ksettings->base.autoneg = AUTONEG_DISABLE;
	link_ksettings->base.duplex = DUPLEX_UNKNOWN;

	err = hinic_port_get_cap(nic_dev, &port_cap);
	if (err)
		return err;

	hinic_link_port_type(&settings, port_cap.port_type);
	link_ksettings->base.port = settings.port;

	err = hinic_port_link_state(nic_dev, &link_state);
	if (err)
		return err;

	if (link_state == HINIC_LINK_STATE_UP) {
		set_link_speed(link_ksettings, port_cap.speed);
		link_ksettings->base.duplex =
			(port_cap.duplex == HINIC_DUPLEX_FULL) ?
			DUPLEX_FULL : DUPLEX_HALF;
	}

	if (!!(port_cap.autoneg_cap & HINIC_AUTONEG_SUPPORTED))
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, Autoneg);

	if (port_cap.autoneg_state == HINIC_AUTONEG_ACTIVE)
		link_ksettings->base.autoneg = AUTONEG_ENABLE;

	err = hinic_get_link_mode(nic_dev->hwdev, &link_mode);
	if (err || link_mode.supported == HINIC_SUPPORTED_UNKNOWN ||
	    link_mode.advertised == HINIC_SUPPORTED_UNKNOWN)
		return -EIO;

	hinic_add_ethtool_link_mode(&settings, link_mode.supported,
				    GET_SUPPORTED_MODE);
	hinic_add_ethtool_link_mode(&settings, link_mode.advertised,
				    GET_ADVERTISED_MODE);

	if (!HINIC_IS_VF(nic_dev->hwdev->hwif)) {
		err = hinic_get_hw_pause_info(nic_dev->hwdev, &pause_info);
		if (err)
			return err;
		ETHTOOL_ADD_SUPPORTED_LINK_MODE(&settings, Pause);
		if (pause_info.rx_pause && pause_info.tx_pause) {
			ETHTOOL_ADD_ADVERTISED_LINK_MODE(&settings, Pause);
		} else if (pause_info.tx_pause) {
			ETHTOOL_ADD_ADVERTISED_LINK_MODE(&settings, Asym_Pause);
		} else if (pause_info.rx_pause) {
			ETHTOOL_ADD_ADVERTISED_LINK_MODE(&settings, Pause);
			ETHTOOL_ADD_ADVERTISED_LINK_MODE(&settings, Asym_Pause);
		}
	}

	bitmap_copy(link_ksettings->link_modes.supported,
		    (unsigned long *)&settings.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_copy(link_ksettings->link_modes.advertising,
		    (unsigned long *)&settings.advertising,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);

	return 0;
}

static int hinic_ethtool_to_hw_speed_level(u32 speed)
{
	int i;

	for (i = 0; i < LINK_SPEED_LEVELS; i++) {
		if (hw_to_ethtool_speed[i] == speed)
			break;
	}

	return i;
}

static bool hinic_is_support_speed(enum hinic_link_mode supported_link,
				   u32 speed)
{
	enum hinic_link_mode link_mode;
	int idx;

	for (link_mode = 0; link_mode < HINIC_LINK_MODE_NUMBERS; link_mode++) {
		if (!(supported_link & ((u32)1 << link_mode)))
			continue;

		idx = hinic_get_link_mode_index(link_mode);
		if (idx >= HINIC_LINK_MODE_NUMBERS)
			continue;

		if (hw_to_ethtool_link_mode_table[idx].speed == speed)
			return true;
	}

	return false;
}

static bool hinic_is_speed_legal(struct hinic_dev *nic_dev, u32 speed)
{
	struct hinic_link_mode_cmd link_mode = { 0 };
	struct net_device *netdev = nic_dev->netdev;
	enum nic_speed_level speed_level = 0;
	int err;

	err = hinic_get_link_mode(nic_dev->hwdev, &link_mode);
	if (err)
		return false;

	if (link_mode.supported == HINIC_SUPPORTED_UNKNOWN ||
	    link_mode.advertised == HINIC_SUPPORTED_UNKNOWN)
		return false;

	speed_level = hinic_ethtool_to_hw_speed_level(speed);
	if (speed_level >= LINK_SPEED_LEVELS ||
	    !hinic_is_support_speed(link_mode.supported, speed)) {
		netif_err(nic_dev, drv, netdev,
			  "Unsupported speed: %d\n", speed);
		return false;
	}

	return true;
}

static int get_link_settings_type(struct hinic_dev *nic_dev,
				  u8 autoneg, u32 speed, u32 *set_settings)
{
	struct hinic_port_cap port_cap = { 0 };
	int err;

	err = hinic_port_get_cap(nic_dev, &port_cap);
	if (err)
		return err;

	/* always set autonegotiation */
	if (port_cap.autoneg_cap)
		*set_settings |= HILINK_LINK_SET_AUTONEG;

	if (autoneg == AUTONEG_ENABLE) {
		if (!port_cap.autoneg_cap) {
			netif_err(nic_dev, drv, nic_dev->netdev, "Not support autoneg\n");
			return -EOPNOTSUPP;
		}
	} else if (speed != (u32)SPEED_UNKNOWN) {
		/* set speed only when autoneg is disabled */
		if (!hinic_is_speed_legal(nic_dev, speed))
			return -EINVAL;
		*set_settings |= HILINK_LINK_SET_SPEED;
	} else {
		netif_err(nic_dev, drv, nic_dev->netdev, "Need to set speed when autoneg is off\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int set_link_settings_separate_cmd(struct hinic_dev *nic_dev,
					  u32 set_settings, u8 autoneg,
					  u32 speed)
{
	enum nic_speed_level speed_level = 0;
	int err = 0;

	if (set_settings & HILINK_LINK_SET_AUTONEG) {
		err = hinic_set_autoneg(nic_dev->hwdev,
					(autoneg == AUTONEG_ENABLE));
		if (err)
			netif_err(nic_dev, drv, nic_dev->netdev, "%s autoneg failed\n",
				  (autoneg == AUTONEG_ENABLE) ?
				  "Enable" : "Disable");
		else
			netif_info(nic_dev, drv, nic_dev->netdev, "%s autoneg successfully\n",
				   (autoneg == AUTONEG_ENABLE) ?
				   "Enable" : "Disable");
	}

	if (!err && (set_settings & HILINK_LINK_SET_SPEED)) {
		speed_level = hinic_ethtool_to_hw_speed_level(speed);
		err = hinic_set_speed(nic_dev->hwdev, speed_level);
		if (err)
			netif_err(nic_dev, drv, nic_dev->netdev, "Set speed %d failed\n",
				  speed);
		else
			netif_info(nic_dev, drv, nic_dev->netdev, "Set speed %d successfully\n",
				   speed);
	}

	return err;
}

static int hinic_set_settings_to_hw(struct hinic_dev *nic_dev,
				    u32 set_settings, u8 autoneg, u32 speed)
{
	struct hinic_link_ksettings_info settings = {0};
	char set_link_str[SET_LINK_STR_MAX_LEN] = {0};
	struct net_device *netdev = nic_dev->netdev;
	enum nic_speed_level speed_level = 0;
	int err;

	err = snprintf(set_link_str, SET_LINK_STR_MAX_LEN, "%s",
		       (set_settings & HILINK_LINK_SET_AUTONEG) ?
		       (autoneg ? "autong enable " : "autong disable ") : "");
	if (err < 0 || err >= SET_LINK_STR_MAX_LEN) {
		netif_err(nic_dev, drv, netdev, "Failed to snprintf link state, function return(%d) and dest_len(%d)\n",
			  err, SET_LINK_STR_MAX_LEN);
		return -EFAULT;
	}

	if (set_settings & HILINK_LINK_SET_SPEED) {
		speed_level = hinic_ethtool_to_hw_speed_level(speed);
		err = snprintf(set_link_str, SET_LINK_STR_MAX_LEN,
			       "%sspeed %d ", set_link_str, speed);
		if (err <= 0 || err >= SET_LINK_STR_MAX_LEN) {
			netif_err(nic_dev, drv, netdev, "Failed to snprintf link speed, function return(%d) and dest_len(%d)\n",
				  err, SET_LINK_STR_MAX_LEN);
			return -EFAULT;
		}
	}

	settings.func_id = HINIC_HWIF_FUNC_IDX(nic_dev->hwdev->hwif);
	settings.valid_bitmap = set_settings;
	settings.autoneg = autoneg;
	settings.speed = speed_level;

	err = hinic_set_link_settings(nic_dev->hwdev, &settings);
	if (err != HINIC_MGMT_CMD_UNSUPPORTED) {
		if (err)
			netif_err(nic_dev, drv, netdev, "Set %s failed\n",
				  set_link_str);
		else
			netif_info(nic_dev, drv, netdev, "Set %s successfully\n",
				   set_link_str);

		return err;
	}

	return set_link_settings_separate_cmd(nic_dev, set_settings, autoneg,
					      speed);
}

static int set_link_settings(struct net_device *netdev, u8 autoneg, u32 speed)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u32 set_settings = 0;
	int err;

	err = get_link_settings_type(nic_dev, autoneg, speed, &set_settings);
	if (err)
		return err;

	if (set_settings)
		err = hinic_set_settings_to_hw(nic_dev, set_settings,
					       autoneg, speed);
	else
		netif_info(nic_dev, drv, netdev, "Nothing changed, exit without setting anything\n");

	return err;
}

static int hinic_set_link_ksettings(struct net_device *netdev, const struct
				    ethtool_link_ksettings *link_settings)
{
	/* only support to set autoneg and speed */
	return set_link_settings(netdev, link_settings->base.autoneg,
				 link_settings->base.speed);
}

static void hinic_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *info)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u8 mgmt_ver[HINIC_MGMT_VERSION_MAX_LEN] = {0};
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	int err;

	strlcpy(info->driver, HINIC_DRV_NAME, sizeof(info->driver));
	strlcpy(info->bus_info, pci_name(hwif->pdev), sizeof(info->bus_info));

	err = hinic_get_mgmt_version(nic_dev, mgmt_ver);
	if (err)
		return;

	snprintf(info->fw_version, sizeof(info->fw_version), "%s", mgmt_ver);
}

static void hinic_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);

	ring->rx_max_pending = HINIC_MAX_QUEUE_DEPTH;
	ring->tx_max_pending = HINIC_MAX_QUEUE_DEPTH;
	ring->rx_pending = nic_dev->rq_depth;
	ring->tx_pending = nic_dev->sq_depth;
}

static int check_ringparam_valid(struct hinic_dev *nic_dev,
				 struct ethtool_ringparam *ring)
{
	if (ring->rx_jumbo_pending || ring->rx_mini_pending) {
		netif_err(nic_dev, drv, nic_dev->netdev,
			  "Unsupported rx_jumbo_pending/rx_mini_pending\n");
		return -EINVAL;
	}

	if (ring->tx_pending > HINIC_MAX_QUEUE_DEPTH ||
	    ring->tx_pending < HINIC_MIN_QUEUE_DEPTH ||
	    ring->rx_pending > HINIC_MAX_QUEUE_DEPTH ||
	    ring->rx_pending < HINIC_MIN_QUEUE_DEPTH) {
		netif_err(nic_dev, drv, nic_dev->netdev,
			  "Queue depth out of range [%d-%d]\n",
			  HINIC_MIN_QUEUE_DEPTH, HINIC_MAX_QUEUE_DEPTH);
		return -EINVAL;
	}

	return 0;
}

static int hinic_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u16 new_sq_depth, new_rq_depth;
	int err;

	err = check_ringparam_valid(nic_dev, ring);
	if (err)
		return err;

	new_sq_depth = (u16)(1U << (u16)ilog2(ring->tx_pending));
	new_rq_depth = (u16)(1U << (u16)ilog2(ring->rx_pending));

	if (new_sq_depth == nic_dev->sq_depth &&
	    new_rq_depth == nic_dev->rq_depth)
		return 0;

	netif_info(nic_dev, drv, netdev,
		   "Change Tx/Rx ring depth from %d/%d to %d/%d\n",
		   nic_dev->sq_depth, nic_dev->rq_depth,
		   new_sq_depth, new_rq_depth);

	nic_dev->sq_depth = new_sq_depth;
	nic_dev->rq_depth = new_rq_depth;

	if (netif_running(netdev)) {
		netif_info(nic_dev, drv, netdev, "Restarting netdev\n");
		err = hinic_close(netdev);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to close netdev\n");
			return -EFAULT;
		}

		err = hinic_open(netdev);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to open netdev\n");
			return -EFAULT;
		}
	}

	return 0;
}
static void hinic_get_channels(struct net_device *netdev,
			       struct ethtool_channels *channels)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_hwdev *hwdev = nic_dev->hwdev;

	channels->max_combined = nic_dev->max_qps;
	channels->combined_count = hinic_hwdev_num_qps(hwdev);
}

static int hinic_set_channels(struct net_device *netdev,
			      struct ethtool_channels *channels)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	unsigned int count = channels->combined_count;
	int err;

	netif_info(nic_dev, drv, netdev, "Set max combined queue number from %d to %d\n",
		   hinic_hwdev_num_qps(nic_dev->hwdev), count);

	if (netif_running(netdev)) {
		netif_info(nic_dev, drv, netdev, "Restarting netdev\n");
		hinic_close(netdev);

		nic_dev->hwdev->nic_cap.num_qps = count;

		err = hinic_open(netdev);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to open netdev\n");
			return -EFAULT;
		}
	} else {
		nic_dev->hwdev->nic_cap.num_qps = count;
	}

	return 0;
}

static int hinic_get_rss_hash_opts(struct hinic_dev *nic_dev,
				   struct ethtool_rxnfc *cmd)
{
	struct hinic_rss_type rss_type = { 0 };
	int err;

	cmd->data = 0;

	if (!(nic_dev->flags & HINIC_RSS_ENABLE))
		return 0;

	err = hinic_get_rss_type(nic_dev, nic_dev->rss_tmpl_idx,
				 &rss_type);
	if (err)
		return err;

	cmd->data = RXH_IP_SRC | RXH_IP_DST;
	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		if (rss_type.tcp_ipv4)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case TCP_V6_FLOW:
		if (rss_type.tcp_ipv6)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case UDP_V4_FLOW:
		if (rss_type.udp_ipv4)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case UDP_V6_FLOW:
		if (rss_type.udp_ipv6)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case IPV4_FLOW:
	case IPV6_FLOW:
		break;
	default:
		cmd->data = 0;
		return -EINVAL;
	}

	return 0;
}

static int set_l4_rss_hash_ops(struct ethtool_rxnfc *cmd,
			       struct hinic_rss_type *rss_type)
{
	u8 rss_l4_en = 0;

	switch (cmd->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
	case 0:
		rss_l4_en = 0;
		break;
	case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
		rss_l4_en = 1;
		break;
	default:
		return -EINVAL;
	}

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		rss_type->tcp_ipv4 = rss_l4_en;
		break;
	case TCP_V6_FLOW:
		rss_type->tcp_ipv6 = rss_l4_en;
		break;
	case UDP_V4_FLOW:
		rss_type->udp_ipv4 = rss_l4_en;
		break;
	case UDP_V6_FLOW:
		rss_type->udp_ipv6 = rss_l4_en;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int hinic_set_rss_hash_opts(struct hinic_dev *nic_dev,
				   struct ethtool_rxnfc *cmd)
{
	struct hinic_rss_type *rss_type = &nic_dev->rss_type;
	int err;

	if (!(nic_dev->flags & HINIC_RSS_ENABLE)) {
		cmd->data = 0;
		return -EOPNOTSUPP;
	}

	/* RSS does not support anything other than hashing
	 * to queues on src and dst IPs and ports
	 */
	if (cmd->data & ~(RXH_IP_SRC | RXH_IP_DST | RXH_L4_B_0_1 |
		RXH_L4_B_2_3))
		return -EINVAL;

	/* We need at least the IP SRC and DEST fields for hashing */
	if (!(cmd->data & RXH_IP_SRC) || !(cmd->data & RXH_IP_DST))
		return -EINVAL;

	err = hinic_get_rss_type(nic_dev,
				 nic_dev->rss_tmpl_idx, rss_type);
	if (err)
		return -EFAULT;

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
	case UDP_V4_FLOW:
	case UDP_V6_FLOW:
		err = set_l4_rss_hash_ops(cmd, rss_type);
		if (err)
			return err;
		break;
	case IPV4_FLOW:
		rss_type->ipv4 = 1;
		break;
	case IPV6_FLOW:
		rss_type->ipv6 = 1;
		break;
	default:
		return -EINVAL;
	}

	err = hinic_set_rss_type(nic_dev, nic_dev->rss_tmpl_idx,
				 *rss_type);
	if (err)
		return -EFAULT;

	return 0;
}

static int __set_rss_rxfh(struct net_device *netdev,
			  const u32 *indir, const u8 *key)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int err;

	if (indir) {
		if (!nic_dev->rss_indir_user) {
			nic_dev->rss_indir_user =
				kzalloc(sizeof(u32) * HINIC_RSS_INDIR_SIZE,
					GFP_KERNEL);
			if (!nic_dev->rss_indir_user)
				return -ENOMEM;
		}

		memcpy(nic_dev->rss_indir_user, indir,
		       sizeof(u32) * HINIC_RSS_INDIR_SIZE);

		err = hinic_rss_set_indir_tbl(nic_dev,
					      nic_dev->rss_tmpl_idx, indir);
		if (err)
			return -EFAULT;
	}

	if (key) {
		if (!nic_dev->rss_hkey_user) {
			nic_dev->rss_hkey_user =
				kzalloc(HINIC_RSS_KEY_SIZE * 2, GFP_KERNEL);

			if (!nic_dev->rss_hkey_user)
				return -ENOMEM;
		}

		memcpy(nic_dev->rss_hkey_user, key, HINIC_RSS_KEY_SIZE);

		err = hinic_rss_set_template_tbl(nic_dev,
						 nic_dev->rss_tmpl_idx, key);
		if (err)
			return -EFAULT;
	}

	return 0;
}

static int hinic_get_rxnfc(struct net_device *netdev,
			   struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int err = 0;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = nic_dev->num_qps;
		break;
	case ETHTOOL_GRXFH:
		err = hinic_get_rss_hash_opts(nic_dev, cmd);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int hinic_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int err = 0;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		err = hinic_set_rss_hash_opts(nic_dev, cmd);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int hinic_get_rxfh(struct net_device *netdev,
			  u32 *indir, u8 *key, u8 *hfunc)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u8 hash_engine_type = 0;
	int err = 0;

	if (!(nic_dev->flags & HINIC_RSS_ENABLE))
		return -EOPNOTSUPP;

	if (hfunc) {
		err = hinic_rss_get_hash_engine(nic_dev,
						nic_dev->rss_tmpl_idx,
						&hash_engine_type);
		if (err)
			return -EFAULT;

		*hfunc = hash_engine_type ? ETH_RSS_HASH_TOP : ETH_RSS_HASH_XOR;
	}

	if (indir) {
		err = hinic_rss_get_indir_tbl(nic_dev,
					      nic_dev->rss_tmpl_idx, indir);
		if (err)
			return -EFAULT;
	}

	if (key)
		err = hinic_rss_get_template_tbl(nic_dev,
						 nic_dev->rss_tmpl_idx, key);

	return err;
}

static int hinic_set_rxfh(struct net_device *netdev, const u32 *indir,
			  const u8 *key, const u8 hfunc)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int err = 0;

	if (!(nic_dev->flags & HINIC_RSS_ENABLE))
		return -EOPNOTSUPP;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE) {
		if (hfunc != ETH_RSS_HASH_TOP && hfunc != ETH_RSS_HASH_XOR)
			return -EOPNOTSUPP;

		nic_dev->rss_hash_engine = (hfunc == ETH_RSS_HASH_XOR) ?
			HINIC_RSS_HASH_ENGINE_TYPE_XOR :
			HINIC_RSS_HASH_ENGINE_TYPE_TOEP;
		err = hinic_rss_set_hash_engine
			(nic_dev, nic_dev->rss_tmpl_idx,
			nic_dev->rss_hash_engine);
		if (err)
			return -EFAULT;
	}

	err = __set_rss_rxfh(netdev, indir, key);

	return err;
}

static u32 hinic_get_rxfh_key_size(struct net_device *netdev)
{
	return HINIC_RSS_KEY_SIZE;
}

static u32 hinic_get_rxfh_indir_size(struct net_device *netdev)
{
	return HINIC_RSS_INDIR_SIZE;
}

#define ARRAY_LEN(arr) ((int)((int)sizeof(arr) / (int)sizeof(arr[0])))

#define HINIC_FUNC_STAT(_stat_item) {	\
	.name = #_stat_item, \
	.size = sizeof_field(struct hinic_vport_stats, _stat_item), \
	.offset = offsetof(struct hinic_vport_stats, _stat_item) \
}

static struct hinic_stats hinic_function_stats[] = {
	HINIC_FUNC_STAT(tx_unicast_pkts_vport),
	HINIC_FUNC_STAT(tx_unicast_bytes_vport),
	HINIC_FUNC_STAT(tx_multicast_pkts_vport),
	HINIC_FUNC_STAT(tx_multicast_bytes_vport),
	HINIC_FUNC_STAT(tx_broadcast_pkts_vport),
	HINIC_FUNC_STAT(tx_broadcast_bytes_vport),

	HINIC_FUNC_STAT(rx_unicast_pkts_vport),
	HINIC_FUNC_STAT(rx_unicast_bytes_vport),
	HINIC_FUNC_STAT(rx_multicast_pkts_vport),
	HINIC_FUNC_STAT(rx_multicast_bytes_vport),
	HINIC_FUNC_STAT(rx_broadcast_pkts_vport),
	HINIC_FUNC_STAT(rx_broadcast_bytes_vport),

	HINIC_FUNC_STAT(tx_discard_vport),
	HINIC_FUNC_STAT(rx_discard_vport),
	HINIC_FUNC_STAT(tx_err_vport),
	HINIC_FUNC_STAT(rx_err_vport),
};

#define HINIC_PORT_STAT(_stat_item) { \
	.name = #_stat_item, \
	.size = sizeof_field(struct hinic_phy_port_stats, _stat_item), \
	.offset = offsetof(struct hinic_phy_port_stats, _stat_item) \
}

static struct hinic_stats hinic_port_stats[] = {
	HINIC_PORT_STAT(mac_rx_total_pkt_num),
	HINIC_PORT_STAT(mac_rx_total_oct_num),
	HINIC_PORT_STAT(mac_rx_bad_pkt_num),
	HINIC_PORT_STAT(mac_rx_bad_oct_num),
	HINIC_PORT_STAT(mac_rx_good_pkt_num),
	HINIC_PORT_STAT(mac_rx_good_oct_num),
	HINIC_PORT_STAT(mac_rx_uni_pkt_num),
	HINIC_PORT_STAT(mac_rx_multi_pkt_num),
	HINIC_PORT_STAT(mac_rx_broad_pkt_num),
	HINIC_PORT_STAT(mac_tx_total_pkt_num),
	HINIC_PORT_STAT(mac_tx_total_oct_num),
	HINIC_PORT_STAT(mac_tx_bad_pkt_num),
	HINIC_PORT_STAT(mac_tx_bad_oct_num),
	HINIC_PORT_STAT(mac_tx_good_pkt_num),
	HINIC_PORT_STAT(mac_tx_good_oct_num),
	HINIC_PORT_STAT(mac_tx_uni_pkt_num),
	HINIC_PORT_STAT(mac_tx_multi_pkt_num),
	HINIC_PORT_STAT(mac_tx_broad_pkt_num),
	HINIC_PORT_STAT(mac_rx_fragment_pkt_num),
	HINIC_PORT_STAT(mac_rx_undersize_pkt_num),
	HINIC_PORT_STAT(mac_rx_undermin_pkt_num),
	HINIC_PORT_STAT(mac_rx_64_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_65_127_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_128_255_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_256_511_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_512_1023_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_1024_1518_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_1519_2047_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_2048_4095_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_4096_8191_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_8192_9216_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_9217_12287_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_12288_16383_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_1519_max_good_pkt_num),
	HINIC_PORT_STAT(mac_rx_1519_max_bad_pkt_num),
	HINIC_PORT_STAT(mac_rx_oversize_pkt_num),
	HINIC_PORT_STAT(mac_rx_jabber_pkt_num),
	HINIC_PORT_STAT(mac_rx_pause_num),
	HINIC_PORT_STAT(mac_rx_pfc_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri0_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri1_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri2_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri3_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri4_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri5_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri6_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri7_pkt_num),
	HINIC_PORT_STAT(mac_rx_control_pkt_num),
	HINIC_PORT_STAT(mac_rx_sym_err_pkt_num),
	HINIC_PORT_STAT(mac_rx_fcs_err_pkt_num),
	HINIC_PORT_STAT(mac_rx_send_app_good_pkt_num),
	HINIC_PORT_STAT(mac_rx_send_app_bad_pkt_num),
	HINIC_PORT_STAT(mac_tx_fragment_pkt_num),
	HINIC_PORT_STAT(mac_tx_undersize_pkt_num),
	HINIC_PORT_STAT(mac_tx_undermin_pkt_num),
	HINIC_PORT_STAT(mac_tx_64_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_65_127_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_128_255_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_256_511_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_512_1023_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_1024_1518_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_1519_2047_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_2048_4095_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_4096_8191_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_8192_9216_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_9217_12287_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_12288_16383_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_1519_max_good_pkt_num),
	HINIC_PORT_STAT(mac_tx_1519_max_bad_pkt_num),
	HINIC_PORT_STAT(mac_tx_oversize_pkt_num),
	HINIC_PORT_STAT(mac_tx_jabber_pkt_num),
	HINIC_PORT_STAT(mac_tx_pause_num),
	HINIC_PORT_STAT(mac_tx_pfc_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri0_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri1_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri2_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri3_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri4_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri5_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri6_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri7_pkt_num),
	HINIC_PORT_STAT(mac_tx_control_pkt_num),
	HINIC_PORT_STAT(mac_tx_err_all_pkt_num),
	HINIC_PORT_STAT(mac_tx_from_app_good_pkt_num),
	HINIC_PORT_STAT(mac_tx_from_app_bad_pkt_num),
};

#define HINIC_TXQ_STAT(_stat_item) { \
	.name = "txq%d_"#_stat_item, \
	.size = sizeof_field(struct hinic_txq_stats, _stat_item), \
	.offset = offsetof(struct hinic_txq_stats, _stat_item) \
}

static struct hinic_stats hinic_tx_queue_stats[] = {
	HINIC_TXQ_STAT(pkts),
	HINIC_TXQ_STAT(bytes),
	HINIC_TXQ_STAT(tx_busy),
	HINIC_TXQ_STAT(tx_wake),
	HINIC_TXQ_STAT(tx_dropped),
	HINIC_TXQ_STAT(big_frags_pkts),
};

#define HINIC_RXQ_STAT(_stat_item) { \
	.name = "rxq%d_"#_stat_item, \
	.size = sizeof_field(struct hinic_rxq_stats, _stat_item), \
	.offset = offsetof(struct hinic_rxq_stats, _stat_item) \
}

static struct hinic_stats hinic_rx_queue_stats[] = {
	HINIC_RXQ_STAT(pkts),
	HINIC_RXQ_STAT(bytes),
	HINIC_RXQ_STAT(errors),
	HINIC_RXQ_STAT(csum_errors),
	HINIC_RXQ_STAT(other_errors),
};

static void get_drv_queue_stats(struct hinic_dev *nic_dev, u64 *data)
{
	struct hinic_txq_stats txq_stats;
	struct hinic_rxq_stats rxq_stats;
	u16 i = 0, j = 0, qid = 0;
	char *p;

	for (qid = 0; qid < nic_dev->num_qps; qid++) {
		if (!nic_dev->txqs)
			break;

		hinic_txq_get_stats(&nic_dev->txqs[qid], &txq_stats);
		for (j = 0; j < ARRAY_LEN(hinic_tx_queue_stats); j++, i++) {
			p = (char *)&txq_stats +
				hinic_tx_queue_stats[j].offset;
			data[i] = (hinic_tx_queue_stats[j].size ==
					sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
		}
	}

	for (qid = 0; qid < nic_dev->num_qps; qid++) {
		if (!nic_dev->rxqs)
			break;

		hinic_rxq_get_stats(&nic_dev->rxqs[qid], &rxq_stats);
		for (j = 0; j < ARRAY_LEN(hinic_rx_queue_stats); j++, i++) {
			p = (char *)&rxq_stats +
				hinic_rx_queue_stats[j].offset;
			data[i] = (hinic_rx_queue_stats[j].size ==
					sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
		}
	}
}

static void hinic_get_ethtool_stats(struct net_device *netdev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_vport_stats vport_stats = {0};
	struct hinic_phy_port_stats *port_stats;
	u16 i = 0, j = 0;
	char *p;
	int err;

	err = hinic_get_vport_stats(nic_dev, &vport_stats);
	if (err)
		netif_err(nic_dev, drv, netdev,
			  "Failed to get vport stats from firmware\n");

	for (j = 0; j < ARRAY_LEN(hinic_function_stats); j++, i++) {
		p = (char *)&vport_stats + hinic_function_stats[j].offset;
		data[i] = (hinic_function_stats[j].size ==
				sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

	port_stats = kzalloc(sizeof(*port_stats), GFP_KERNEL);
	if (!port_stats) {
		memset(&data[i], 0,
		       ARRAY_LEN(hinic_port_stats) * sizeof(*data));
		i += ARRAY_LEN(hinic_port_stats);
		goto get_drv_stats;
	}

	err = hinic_get_phy_port_stats(nic_dev, port_stats);
	if (err)
		netif_err(nic_dev, drv, netdev,
			  "Failed to get port stats from firmware\n");

	for (j = 0; j < ARRAY_LEN(hinic_port_stats); j++, i++) {
		p = (char *)port_stats + hinic_port_stats[j].offset;
		data[i] = (hinic_port_stats[j].size ==
				sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

	kfree(port_stats);

get_drv_stats:
	get_drv_queue_stats(nic_dev, data + i);
}

static int hinic_get_sset_count(struct net_device *netdev, int sset)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int count, q_num;

	switch (sset) {
	case ETH_SS_STATS:
		q_num = nic_dev->num_qps;
		count = ARRAY_LEN(hinic_function_stats) +
			(ARRAY_LEN(hinic_tx_queue_stats) +
			ARRAY_LEN(hinic_rx_queue_stats)) * q_num;

		count += ARRAY_LEN(hinic_port_stats);

		return count;
	default:
		return -EOPNOTSUPP;
	}
}

static void hinic_get_strings(struct net_device *netdev,
			      u32 stringset, u8 *data)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	char *p = (char *)data;
	u16 i, j;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_LEN(hinic_function_stats); i++) {
			memcpy(p, hinic_function_stats[i].name,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < ARRAY_LEN(hinic_port_stats); i++) {
			memcpy(p, hinic_port_stats[i].name,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < nic_dev->num_qps; i++) {
			for (j = 0; j < ARRAY_LEN(hinic_tx_queue_stats); j++) {
				sprintf(p, hinic_tx_queue_stats[j].name, i);
				p += ETH_GSTRING_LEN;
			}
		}

		for (i = 0; i < nic_dev->num_qps; i++) {
			for (j = 0; j < ARRAY_LEN(hinic_rx_queue_stats); j++) {
				sprintf(p, hinic_rx_queue_stats[j].name, i);
				p += ETH_GSTRING_LEN;
			}
		}

		return;
	default:
		return;
	}
}

static const struct ethtool_ops hinic_ethtool_ops = {
	.get_link_ksettings = hinic_get_link_ksettings,
	.set_link_ksettings = hinic_set_link_ksettings,
	.get_drvinfo = hinic_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_ringparam = hinic_get_ringparam,
	.set_ringparam = hinic_set_ringparam,
	.get_channels = hinic_get_channels,
	.set_channels = hinic_set_channels,
	.get_rxnfc = hinic_get_rxnfc,
	.set_rxnfc = hinic_set_rxnfc,
	.get_rxfh_key_size = hinic_get_rxfh_key_size,
	.get_rxfh_indir_size = hinic_get_rxfh_indir_size,
	.get_rxfh = hinic_get_rxfh,
	.set_rxfh = hinic_set_rxfh,
	.get_sset_count = hinic_get_sset_count,
	.get_ethtool_stats = hinic_get_ethtool_stats,
	.get_strings = hinic_get_strings,
};

void hinic_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &hinic_ethtool_ops;
}
