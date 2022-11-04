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
#include <linux/sfp.h>

#include "hinic_hw_qp.h"
#include "hinic_hw_dev.h"
#include "hinic_port.h"
#include "hinic_tx.h"
#include "hinic_rx.h"
#include "hinic_dev.h"

#define SET_LINK_STR_MAX_LEN	16

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

#define COALESCE_PENDING_LIMIT_UNIT	8
#define	COALESCE_TIMER_CFG_UNIT		9
#define COALESCE_ALL_QUEUE		0xFFFF
#define COALESCE_MAX_PENDING_LIMIT	(255 * COALESCE_PENDING_LIMIT_UNIT)
#define COALESCE_MAX_TIMER_CFG		(255 * COALESCE_TIMER_CFG_UNIT)

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

#define LP_DEFAULT_TIME                 5 /* seconds */
#define LP_PKT_LEN                      1514

#define PORT_DOWN_ERR_IDX		0
enum diag_test_index {
	INTERNAL_LP_TEST = 0,
	EXTERNAL_LP_TEST = 1,
	DIAG_TEST_MAX = 2,
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

	linkmode_copy(link_ksettings->link_modes.supported,
		      (unsigned long *)&settings.supported);
	linkmode_copy(link_ksettings->link_modes.advertising,
		      (unsigned long *)&settings.advertising);

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
	const char *autoneg_str;
	struct net_device *netdev = nic_dev->netdev;
	enum nic_speed_level speed_level = 0;
	int err;

	autoneg_str = (set_settings & HILINK_LINK_SET_AUTONEG) ?
		      (autoneg ? "autong enable " : "autong disable ") : "";

	if (set_settings & HILINK_LINK_SET_SPEED) {
		speed_level = hinic_ethtool_to_hw_speed_level(speed);
		err = snprintf(set_link_str, SET_LINK_STR_MAX_LEN,
			       "speed %d ", speed);
		if (err >= SET_LINK_STR_MAX_LEN) {
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
			netif_err(nic_dev, drv, netdev, "Set %s%sfailed\n",
				  autoneg_str, set_link_str);
		else
			netif_info(nic_dev, drv, netdev, "Set %s%ssuccessfully\n",
				   autoneg_str, set_link_str);

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

	strscpy(info->driver, HINIC_DRV_NAME, sizeof(info->driver));
	strscpy(info->bus_info, pci_name(hwif->pdev), sizeof(info->bus_info));

	err = hinic_get_mgmt_version(nic_dev, mgmt_ver);
	if (err)
		return;

	snprintf(info->fw_version, sizeof(info->fw_version), "%s", mgmt_ver);
}

static void hinic_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring,
				struct kernel_ethtool_ringparam *kernel_ring,
				struct netlink_ext_ack *extack)
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
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
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

static int __hinic_get_coalesce(struct net_device *netdev,
				struct ethtool_coalesce *coal, u16 queue)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_intr_coal_info *rx_intr_coal_info;
	struct hinic_intr_coal_info *tx_intr_coal_info;

	if (queue == COALESCE_ALL_QUEUE) {
		/* get tx/rx irq0 as default parameters */
		rx_intr_coal_info = &nic_dev->rx_intr_coalesce[0];
		tx_intr_coal_info = &nic_dev->tx_intr_coalesce[0];
	} else {
		if (queue >= nic_dev->num_qps) {
			netif_err(nic_dev, drv, netdev,
				  "Invalid queue_id: %d\n", queue);
			return -EINVAL;
		}
		rx_intr_coal_info = &nic_dev->rx_intr_coalesce[queue];
		tx_intr_coal_info = &nic_dev->tx_intr_coalesce[queue];
	}

	/* coalesce_timer is in unit of 9us */
	coal->rx_coalesce_usecs = rx_intr_coal_info->coalesce_timer_cfg *
			COALESCE_TIMER_CFG_UNIT;
	/* coalesced_frames is in unit of 8 */
	coal->rx_max_coalesced_frames = rx_intr_coal_info->pending_limt *
			COALESCE_PENDING_LIMIT_UNIT;
	coal->tx_coalesce_usecs = tx_intr_coal_info->coalesce_timer_cfg *
			COALESCE_TIMER_CFG_UNIT;
	coal->tx_max_coalesced_frames = tx_intr_coal_info->pending_limt *
			COALESCE_PENDING_LIMIT_UNIT;

	return 0;
}

static int is_coalesce_exceed_limit(const struct ethtool_coalesce *coal)
{
	if (coal->rx_coalesce_usecs > COALESCE_MAX_TIMER_CFG ||
	    coal->rx_max_coalesced_frames > COALESCE_MAX_PENDING_LIMIT ||
	    coal->tx_coalesce_usecs > COALESCE_MAX_TIMER_CFG ||
	    coal->tx_max_coalesced_frames > COALESCE_MAX_PENDING_LIMIT)
		return -ERANGE;

	return 0;
}

static int set_queue_coalesce(struct hinic_dev *nic_dev, u16 q_id,
			      struct hinic_intr_coal_info *coal,
			      bool set_rx_coal)
{
	struct hinic_intr_coal_info *intr_coal = NULL;
	struct hinic_msix_config interrupt_info = {0};
	struct net_device *netdev = nic_dev->netdev;
	u16 msix_idx;
	int err;

	intr_coal = set_rx_coal ? &nic_dev->rx_intr_coalesce[q_id] :
		    &nic_dev->tx_intr_coalesce[q_id];

	intr_coal->coalesce_timer_cfg = coal->coalesce_timer_cfg;
	intr_coal->pending_limt = coal->pending_limt;

	/* netdev not running or qp not in using,
	 * don't need to set coalesce to hw
	 */
	if (!(nic_dev->flags & HINIC_INTF_UP) ||
	    q_id >= nic_dev->num_qps)
		return 0;

	msix_idx = set_rx_coal ? nic_dev->rxqs[q_id].rq->msix_entry :
		   nic_dev->txqs[q_id].sq->msix_entry;
	interrupt_info.msix_index = msix_idx;
	interrupt_info.coalesce_timer_cnt = intr_coal->coalesce_timer_cfg;
	interrupt_info.pending_cnt = intr_coal->pending_limt;
	interrupt_info.resend_timer_cnt = intr_coal->resend_timer_cfg;

	err = hinic_set_interrupt_cfg(nic_dev->hwdev, &interrupt_info);
	if (err)
		netif_warn(nic_dev, drv, netdev,
			   "Failed to set %s queue%d coalesce",
			   set_rx_coal ? "rx" : "tx", q_id);

	return err;
}

static int __set_hw_coal_param(struct hinic_dev *nic_dev,
			       struct hinic_intr_coal_info *intr_coal,
			       u16 queue, bool set_rx_coal)
{
	int err;
	u16 i;

	if (queue == COALESCE_ALL_QUEUE) {
		for (i = 0; i < nic_dev->max_qps; i++) {
			err = set_queue_coalesce(nic_dev, i, intr_coal,
						 set_rx_coal);
			if (err)
				return err;
		}
	} else {
		if (queue >= nic_dev->num_qps) {
			netif_err(nic_dev, drv, nic_dev->netdev,
				  "Invalid queue_id: %d\n", queue);
			return -EINVAL;
		}
		err = set_queue_coalesce(nic_dev, queue, intr_coal,
					 set_rx_coal);
		if (err)
			return err;
	}

	return 0;
}

static int __hinic_set_coalesce(struct net_device *netdev,
				struct ethtool_coalesce *coal, u16 queue)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_intr_coal_info rx_intr_coal = {0};
	struct hinic_intr_coal_info tx_intr_coal = {0};
	bool set_rx_coal = false;
	bool set_tx_coal = false;
	int err;

	err = is_coalesce_exceed_limit(coal);
	if (err)
		return err;

	if (coal->rx_coalesce_usecs || coal->rx_max_coalesced_frames) {
		rx_intr_coal.coalesce_timer_cfg =
		(u8)(coal->rx_coalesce_usecs / COALESCE_TIMER_CFG_UNIT);
		rx_intr_coal.pending_limt = (u8)(coal->rx_max_coalesced_frames /
				COALESCE_PENDING_LIMIT_UNIT);
		set_rx_coal = true;
	}

	if (coal->tx_coalesce_usecs || coal->tx_max_coalesced_frames) {
		tx_intr_coal.coalesce_timer_cfg =
		(u8)(coal->tx_coalesce_usecs / COALESCE_TIMER_CFG_UNIT);
		tx_intr_coal.pending_limt = (u8)(coal->tx_max_coalesced_frames /
		COALESCE_PENDING_LIMIT_UNIT);
		set_tx_coal = true;
	}

	/* setting coalesce timer or pending limit to zero will disable
	 * coalesce
	 */
	if (set_rx_coal && (!rx_intr_coal.coalesce_timer_cfg ||
			    !rx_intr_coal.pending_limt))
		netif_warn(nic_dev, drv, netdev, "RX coalesce will be disabled\n");
	if (set_tx_coal && (!tx_intr_coal.coalesce_timer_cfg ||
			    !tx_intr_coal.pending_limt))
		netif_warn(nic_dev, drv, netdev, "TX coalesce will be disabled\n");

	if (set_rx_coal) {
		err = __set_hw_coal_param(nic_dev, &rx_intr_coal, queue, true);
		if (err)
			return err;
	}
	if (set_tx_coal) {
		err = __set_hw_coal_param(nic_dev, &tx_intr_coal, queue, false);
		if (err)
			return err;
	}
	return 0;
}

static int hinic_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coal,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	return __hinic_get_coalesce(netdev, coal, COALESCE_ALL_QUEUE);
}

static int hinic_set_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coal,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	return __hinic_set_coalesce(netdev, coal, COALESCE_ALL_QUEUE);
}

static int hinic_get_per_queue_coalesce(struct net_device *netdev, u32 queue,
					struct ethtool_coalesce *coal)
{
	return __hinic_get_coalesce(netdev, coal, queue);
}

static int hinic_set_per_queue_coalesce(struct net_device *netdev, u32 queue,
					struct ethtool_coalesce *coal)
{
	return __hinic_set_coalesce(netdev, coal, queue);
}

static void hinic_get_pauseparam(struct net_device *netdev,
				 struct ethtool_pauseparam *pause)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_pause_config pause_info = {0};
	struct hinic_nic_cfg *nic_cfg;
	int err;

	nic_cfg = &nic_dev->hwdev->func_to_io.nic_cfg;

	err = hinic_get_hw_pause_info(nic_dev->hwdev, &pause_info);
	if (!err) {
		pause->autoneg = pause_info.auto_neg;
		if (nic_cfg->pause_set || !pause_info.auto_neg) {
			pause->rx_pause = nic_cfg->rx_pause;
			pause->tx_pause = nic_cfg->tx_pause;
		} else {
			pause->rx_pause = pause_info.rx_pause;
			pause->tx_pause = pause_info.tx_pause;
		}
	}
}

static int hinic_set_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_pause_config pause_info = {0};
	struct hinic_port_cap port_cap = {0};
	int err;

	err = hinic_port_get_cap(nic_dev, &port_cap);
	if (err)
		return -EIO;

	if (pause->autoneg != port_cap.autoneg_state)
		return -EOPNOTSUPP;

	pause_info.auto_neg = pause->autoneg;
	pause_info.rx_pause = pause->rx_pause;
	pause_info.tx_pause = pause->tx_pause;

	mutex_lock(&nic_dev->hwdev->func_to_io.nic_cfg.cfg_mutex);
	err = hinic_set_hw_pause_info(nic_dev->hwdev, &pause_info);
	if (err) {
		mutex_unlock(&nic_dev->hwdev->func_to_io.nic_cfg.cfg_mutex);
		return err;
	}
	nic_dev->hwdev->func_to_io.nic_cfg.pause_set = true;
	nic_dev->hwdev->func_to_io.nic_cfg.auto_neg = pause->autoneg;
	nic_dev->hwdev->func_to_io.nic_cfg.rx_pause = pause->rx_pause;
	nic_dev->hwdev->func_to_io.nic_cfg.tx_pause = pause->tx_pause;
	mutex_unlock(&nic_dev->hwdev->func_to_io.nic_cfg.cfg_mutex);

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

static char hinic_test_strings[][ETH_GSTRING_LEN] = {
	"Internal lb test  (on/offline)",
	"External lb test (external_lb)",
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
		for (j = 0; j < ARRAY_SIZE(hinic_tx_queue_stats); j++, i++) {
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
		for (j = 0; j < ARRAY_SIZE(hinic_rx_queue_stats); j++, i++) {
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

	for (j = 0; j < ARRAY_SIZE(hinic_function_stats); j++, i++) {
		p = (char *)&vport_stats + hinic_function_stats[j].offset;
		data[i] = (hinic_function_stats[j].size ==
				sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

	port_stats = kzalloc(sizeof(*port_stats), GFP_KERNEL);
	if (!port_stats) {
		memset(&data[i], 0,
		       ARRAY_SIZE(hinic_port_stats) * sizeof(*data));
		i += ARRAY_SIZE(hinic_port_stats);
		goto get_drv_stats;
	}

	err = hinic_get_phy_port_stats(nic_dev, port_stats);
	if (err)
		netif_err(nic_dev, drv, netdev,
			  "Failed to get port stats from firmware\n");

	for (j = 0; j < ARRAY_SIZE(hinic_port_stats); j++, i++) {
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
	case ETH_SS_TEST:
		return ARRAY_SIZE(hinic_test_strings);
	case ETH_SS_STATS:
		q_num = nic_dev->num_qps;
		count = ARRAY_SIZE(hinic_function_stats) +
			(ARRAY_SIZE(hinic_tx_queue_stats) +
			ARRAY_SIZE(hinic_rx_queue_stats)) * q_num;

		count += ARRAY_SIZE(hinic_port_stats);

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
	case ETH_SS_TEST:
		memcpy(data, *hinic_test_strings, sizeof(hinic_test_strings));
		return;
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(hinic_function_stats); i++) {
			memcpy(p, hinic_function_stats[i].name,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < ARRAY_SIZE(hinic_port_stats); i++) {
			memcpy(p, hinic_port_stats[i].name,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < nic_dev->num_qps; i++) {
			for (j = 0; j < ARRAY_SIZE(hinic_tx_queue_stats); j++) {
				sprintf(p, hinic_tx_queue_stats[j].name, i);
				p += ETH_GSTRING_LEN;
			}
		}

		for (i = 0; i < nic_dev->num_qps; i++) {
			for (j = 0; j < ARRAY_SIZE(hinic_rx_queue_stats); j++) {
				sprintf(p, hinic_rx_queue_stats[j].name, i);
				p += ETH_GSTRING_LEN;
			}
		}

		return;
	default:
		return;
	}
}

static int hinic_run_lp_test(struct hinic_dev *nic_dev, u32 test_time)
{
	u8 *lb_test_rx_buf = nic_dev->lb_test_rx_buf;
	struct net_device *netdev = nic_dev->netdev;
	struct sk_buff *skb_tmp = NULL;
	struct sk_buff *skb = NULL;
	u32 cnt = test_time * 5;
	u8 *test_data = NULL;
	u32 i;
	u8 j;

	skb_tmp = alloc_skb(LP_PKT_LEN, GFP_ATOMIC);
	if (!skb_tmp)
		return -ENOMEM;

	test_data = __skb_put(skb_tmp, LP_PKT_LEN);

	memset(test_data, 0xFF, 2 * ETH_ALEN);
	test_data[ETH_ALEN] = 0xFE;
	test_data[2 * ETH_ALEN] = 0x08;
	test_data[2 * ETH_ALEN + 1] = 0x0;

	for (i = ETH_HLEN; i < LP_PKT_LEN; i++)
		test_data[i] = i & 0xFF;

	skb_tmp->queue_mapping = 0;
	skb_tmp->ip_summed = CHECKSUM_COMPLETE;
	skb_tmp->dev = netdev;

	for (i = 0; i < cnt; i++) {
		nic_dev->lb_test_rx_idx = 0;
		memset(lb_test_rx_buf, 0, LP_PKT_CNT * LP_PKT_LEN);

		for (j = 0; j < LP_PKT_CNT; j++) {
			skb = pskb_copy(skb_tmp, GFP_ATOMIC);
			if (!skb) {
				dev_kfree_skb_any(skb_tmp);
				netif_err(nic_dev, drv, netdev,
					  "Copy skb failed for loopback test\n");
				return -ENOMEM;
			}

			/* mark index for every pkt */
			skb->data[LP_PKT_LEN - 1] = j;

			if (hinic_lb_xmit_frame(skb, netdev)) {
				dev_kfree_skb_any(skb);
				dev_kfree_skb_any(skb_tmp);
				netif_err(nic_dev, drv, netdev,
					  "Xmit pkt failed for loopback test\n");
				return -EBUSY;
			}
		}

		/* wait till all pkts received to RX buffer */
		msleep(200);

		for (j = 0; j < LP_PKT_CNT; j++) {
			if (memcmp(lb_test_rx_buf + j * LP_PKT_LEN,
				   skb_tmp->data, LP_PKT_LEN - 1) ||
			    (*(lb_test_rx_buf + j * LP_PKT_LEN +
			     LP_PKT_LEN - 1) != j)) {
				dev_kfree_skb_any(skb_tmp);
				netif_err(nic_dev, drv, netdev,
					  "Compare pkt failed in loopback test(index=0x%02x, data[%d]=0x%02x)\n",
					  j + i * LP_PKT_CNT,
					  LP_PKT_LEN - 1,
					  *(lb_test_rx_buf + j * LP_PKT_LEN +
					    LP_PKT_LEN - 1));
				return -EIO;
			}
		}
	}

	dev_kfree_skb_any(skb_tmp);
	return 0;
}

static int do_lp_test(struct hinic_dev *nic_dev, u32 flags, u32 test_time,
		      enum diag_test_index *test_index)
{
	struct net_device *netdev = nic_dev->netdev;
	u8 *lb_test_rx_buf = NULL;
	int err = 0;

	if (!(flags & ETH_TEST_FL_EXTERNAL_LB)) {
		*test_index = INTERNAL_LP_TEST;
		if (hinic_set_loopback_mode(nic_dev->hwdev,
					    HINIC_INTERNAL_LP_MODE, true)) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to set port loopback mode before loopback test\n");
			return -EIO;
		}
	} else {
		*test_index = EXTERNAL_LP_TEST;
	}

	lb_test_rx_buf = vmalloc(LP_PKT_CNT * LP_PKT_LEN);
	if (!lb_test_rx_buf) {
		err = -ENOMEM;
	} else {
		nic_dev->lb_test_rx_buf = lb_test_rx_buf;
		nic_dev->lb_pkt_len = LP_PKT_LEN;
		nic_dev->flags |= HINIC_LP_TEST;
		err = hinic_run_lp_test(nic_dev, test_time);
		nic_dev->flags &= ~HINIC_LP_TEST;
		msleep(100);
		vfree(lb_test_rx_buf);
		nic_dev->lb_test_rx_buf = NULL;
	}

	if (!(flags & ETH_TEST_FL_EXTERNAL_LB)) {
		if (hinic_set_loopback_mode(nic_dev->hwdev,
					    HINIC_INTERNAL_LP_MODE, false)) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to cancel port loopback mode after loopback test\n");
			err = -EIO;
		}
	}

	return err;
}

static void hinic_diag_test(struct net_device *netdev,
			    struct ethtool_test *eth_test, u64 *data)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	enum hinic_port_link_state link_state;
	enum diag_test_index test_index = 0;
	int err = 0;

	memset(data, 0, DIAG_TEST_MAX * sizeof(u64));

	/* don't support loopback test when netdev is closed. */
	if (!(nic_dev->flags & HINIC_INTF_UP)) {
		netif_err(nic_dev, drv, netdev,
			  "Do not support loopback test when netdev is closed\n");
		eth_test->flags |= ETH_TEST_FL_FAILED;
		data[PORT_DOWN_ERR_IDX] = 1;
		return;
	}

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	err = do_lp_test(nic_dev, eth_test->flags, LP_DEFAULT_TIME,
			 &test_index);
	if (err) {
		eth_test->flags |= ETH_TEST_FL_FAILED;
		data[test_index] = 1;
	}

	netif_tx_wake_all_queues(netdev);

	err = hinic_port_link_state(nic_dev, &link_state);
	if (!err && link_state == HINIC_LINK_STATE_UP)
		netif_carrier_on(netdev);
}

static int hinic_set_phys_id(struct net_device *netdev,
			     enum ethtool_phys_id_state state)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int err = 0;
	u8 port;

	port = nic_dev->hwdev->port_id;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		err = hinic_set_led_status(nic_dev->hwdev, port,
					   HINIC_LED_TYPE_LINK,
					   HINIC_LED_MODE_FORCE_2HZ);
		if (err)
			netif_err(nic_dev, drv, netdev,
				  "Set LED blinking in 2HZ failed\n");
		break;

	case ETHTOOL_ID_INACTIVE:
		err = hinic_reset_led_status(nic_dev->hwdev, port);
		if (err)
			netif_err(nic_dev, drv, netdev,
				  "Reset LED to original status failed\n");
		break;

	default:
		return -EOPNOTSUPP;
	}

	return err;
}

static int hinic_get_module_info(struct net_device *netdev,
				 struct ethtool_modinfo *modinfo)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u8 sfp_type_ext;
	u8 sfp_type;
	int err;

	err = hinic_get_sfp_type(nic_dev->hwdev, &sfp_type, &sfp_type_ext);
	if (err)
		return err;

	switch (sfp_type) {
	case SFF8024_ID_SFP:
		modinfo->type = ETH_MODULE_SFF_8472;
		modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		break;
	case SFF8024_ID_QSFP_8438:
		modinfo->type = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		break;
	case SFF8024_ID_QSFP_8436_8636:
		if (sfp_type_ext >= 0x3) {
			modinfo->type = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_MAX_LEN;

		} else {
			modinfo->type = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		}
		break;
	case SFF8024_ID_QSFP28_8636:
		modinfo->type = ETH_MODULE_SFF_8636;
		modinfo->eeprom_len = ETH_MODULE_SFF_8636_MAX_LEN;
		break;
	default:
		netif_warn(nic_dev, drv, netdev,
			   "Optical module unknown: 0x%x\n", sfp_type);
		return -EINVAL;
	}

	return 0;
}

static int hinic_get_module_eeprom(struct net_device *netdev,
				   struct ethtool_eeprom *ee, u8 *data)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u8 sfp_data[STD_SFP_INFO_MAX_SIZE];
	u16 len;
	int err;

	if (!ee->len || ((ee->len + ee->offset) > STD_SFP_INFO_MAX_SIZE))
		return -EINVAL;

	memset(data, 0, ee->len);

	err = hinic_get_sfp_eeprom(nic_dev->hwdev, sfp_data, &len);
	if (err)
		return err;

	memcpy(data, sfp_data + ee->offset, ee->len);

	return 0;
}

static int
hinic_get_link_ext_state(struct net_device *netdev,
			 struct ethtool_link_ext_state_info *link_ext_state_info)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);

	if (netif_carrier_ok(netdev))
		return -ENODATA;

	if (nic_dev->cable_unplugged)
		link_ext_state_info->link_ext_state =
			ETHTOOL_LINK_EXT_STATE_NO_CABLE;
	else if (nic_dev->module_unrecognized)
		link_ext_state_info->link_ext_state =
			ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH;

	return 0;
}

static const struct ethtool_ops hinic_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_USECS |
				     ETHTOOL_COALESCE_RX_MAX_FRAMES |
				     ETHTOOL_COALESCE_TX_USECS |
				     ETHTOOL_COALESCE_TX_MAX_FRAMES,

	.get_link_ksettings = hinic_get_link_ksettings,
	.set_link_ksettings = hinic_set_link_ksettings,
	.get_drvinfo = hinic_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_link_ext_state = hinic_get_link_ext_state,
	.get_ringparam = hinic_get_ringparam,
	.set_ringparam = hinic_set_ringparam,
	.get_coalesce = hinic_get_coalesce,
	.set_coalesce = hinic_set_coalesce,
	.get_per_queue_coalesce = hinic_get_per_queue_coalesce,
	.set_per_queue_coalesce = hinic_set_per_queue_coalesce,
	.get_pauseparam = hinic_get_pauseparam,
	.set_pauseparam = hinic_set_pauseparam,
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
	.self_test = hinic_diag_test,
	.set_phys_id = hinic_set_phys_id,
	.get_module_info = hinic_get_module_info,
	.get_module_eeprom = hinic_get_module_eeprom,
};

static const struct ethtool_ops hinicvf_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_USECS |
				     ETHTOOL_COALESCE_RX_MAX_FRAMES |
				     ETHTOOL_COALESCE_TX_USECS |
				     ETHTOOL_COALESCE_TX_MAX_FRAMES,

	.get_link_ksettings = hinic_get_link_ksettings,
	.get_drvinfo = hinic_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_ringparam = hinic_get_ringparam,
	.set_ringparam = hinic_set_ringparam,
	.get_coalesce = hinic_get_coalesce,
	.set_coalesce = hinic_set_coalesce,
	.get_per_queue_coalesce = hinic_get_per_queue_coalesce,
	.set_per_queue_coalesce = hinic_set_per_queue_coalesce,
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
	struct hinic_dev *nic_dev = netdev_priv(netdev);

	if (!HINIC_IS_VF(nic_dev->hwdev->hwif))
		netdev->ethtool_ops = &hinic_ethtool_ops;
	else
		netdev->ethtool_ops = &hinicvf_ethtool_ops;
}
