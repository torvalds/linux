// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>

#include "hinic3_lld.h"
#include "hinic3_hw_comm.h"
#include "hinic3_nic_dev.h"
#include "hinic3_nic_cfg.h"

#define HINIC3_MGMT_VERSION_MAX_LEN     32

static void hinic3_get_drvinfo(struct net_device *netdev,
			       struct ethtool_drvinfo *info)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	u8 mgmt_ver[HINIC3_MGMT_VERSION_MAX_LEN];
	struct pci_dev *pdev = nic_dev->pdev;
	int err;

	strscpy(info->driver, HINIC3_NIC_DRV_NAME, sizeof(info->driver));
	strscpy(info->bus_info, pci_name(pdev), sizeof(info->bus_info));

	err = hinic3_get_mgmt_version(nic_dev->hwdev, mgmt_ver,
				      HINIC3_MGMT_VERSION_MAX_LEN);
	if (err) {
		netdev_err(netdev, "Failed to get fw version\n");
		return;
	}

	snprintf(info->fw_version, sizeof(info->fw_version), "%s", mgmt_ver);
}

static u32 hinic3_get_msglevel(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	return nic_dev->msg_enable;
}

static void hinic3_set_msglevel(struct net_device *netdev, u32 data)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	nic_dev->msg_enable = data;

	netdev_dbg(netdev, "Set message level: 0x%x\n", data);
}

static const u32 hinic3_link_mode_ge[] = {
	ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
	ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
};

static const u32 hinic3_link_mode_10ge_base_r[] = {
	ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseR_FEC_BIT,
	ETHTOOL_LINK_MODE_10000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLRM_Full_BIT,
};

static const u32 hinic3_link_mode_25ge_base_r[] = {
	ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
};

static const u32 hinic3_link_mode_40ge_base_r4[] = {
	ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT,
};

static const u32 hinic3_link_mode_50ge_base_r[] = {
	ETHTOOL_LINK_MODE_50000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseCR_Full_BIT,
};

static const u32 hinic3_link_mode_50ge_base_r2[] = {
	ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT,
};

static const u32 hinic3_link_mode_100ge_base_r[] = {
	ETHTOOL_LINK_MODE_100000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR_Full_BIT,
};

static const u32 hinic3_link_mode_100ge_base_r2[] = {
	ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT,
};

static const u32 hinic3_link_mode_100ge_base_r4[] = {
	ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT,
};

static const u32 hinic3_link_mode_200ge_base_r2[] = {
	ETHTOOL_LINK_MODE_200000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseSR2_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseCR2_Full_BIT,
};

static const u32 hinic3_link_mode_200ge_base_r4[] = {
	ETHTOOL_LINK_MODE_200000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseCR4_Full_BIT,
};

struct hw2ethtool_link_mode {
	const u32 *link_mode_bit_arr;
	u32       arr_size;
	u32       speed;
};

static const struct hw2ethtool_link_mode
	hw2ethtool_link_mode_table[LINK_MODE_MAX_NUMBERS] = {
	[LINK_MODE_GE] = {
		.link_mode_bit_arr = hinic3_link_mode_ge,
		.arr_size          = ARRAY_SIZE(hinic3_link_mode_ge),
		.speed             = SPEED_1000,
	},
	[LINK_MODE_10GE_BASE_R] = {
		.link_mode_bit_arr = hinic3_link_mode_10ge_base_r,
		.arr_size          = ARRAY_SIZE(hinic3_link_mode_10ge_base_r),
		.speed             = SPEED_10000,
	},
	[LINK_MODE_25GE_BASE_R] = {
		.link_mode_bit_arr = hinic3_link_mode_25ge_base_r,
		.arr_size          = ARRAY_SIZE(hinic3_link_mode_25ge_base_r),
		.speed             = SPEED_25000,
	},
	[LINK_MODE_40GE_BASE_R4] = {
		.link_mode_bit_arr = hinic3_link_mode_40ge_base_r4,
		.arr_size          = ARRAY_SIZE(hinic3_link_mode_40ge_base_r4),
		.speed             = SPEED_40000,
	},
	[LINK_MODE_50GE_BASE_R] = {
		.link_mode_bit_arr = hinic3_link_mode_50ge_base_r,
		.arr_size          = ARRAY_SIZE(hinic3_link_mode_50ge_base_r),
		.speed             = SPEED_50000,
	},
	[LINK_MODE_50GE_BASE_R2] = {
		.link_mode_bit_arr = hinic3_link_mode_50ge_base_r2,
		.arr_size          = ARRAY_SIZE(hinic3_link_mode_50ge_base_r2),
		.speed             = SPEED_50000,
	},
	[LINK_MODE_100GE_BASE_R] = {
		.link_mode_bit_arr = hinic3_link_mode_100ge_base_r,
		.arr_size          = ARRAY_SIZE(hinic3_link_mode_100ge_base_r),
		.speed             = SPEED_100000,
	},
	[LINK_MODE_100GE_BASE_R2] = {
		.link_mode_bit_arr = hinic3_link_mode_100ge_base_r2,
		.arr_size          = ARRAY_SIZE(hinic3_link_mode_100ge_base_r2),
		.speed             = SPEED_100000,
	},
	[LINK_MODE_100GE_BASE_R4] = {
		.link_mode_bit_arr = hinic3_link_mode_100ge_base_r4,
		.arr_size          = ARRAY_SIZE(hinic3_link_mode_100ge_base_r4),
		.speed             = SPEED_100000,
	},
	[LINK_MODE_200GE_BASE_R2] = {
		.link_mode_bit_arr = hinic3_link_mode_200ge_base_r2,
		.arr_size          = ARRAY_SIZE(hinic3_link_mode_200ge_base_r2),
		.speed             = SPEED_200000,
	},
	[LINK_MODE_200GE_BASE_R4] = {
		.link_mode_bit_arr = hinic3_link_mode_200ge_base_r4,
		.arr_size          = ARRAY_SIZE(hinic3_link_mode_200ge_base_r4),
		.speed             = SPEED_200000,
	},
};

#define GET_SUPPORTED_MODE     0
#define GET_ADVERTISED_MODE    1

struct hinic3_link_settings {
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertising);

	u32 speed;
	u8  duplex;
	u8  port;
	u8  autoneg;
};

#define HINIC3_ADD_SUPPORTED_LINK_MODE(ecmd, mode) \
	set_bit(ETHTOOL_LINK_##mode##_BIT, (ecmd)->supported)
#define HINIC3_ADD_ADVERTISED_LINK_MODE(ecmd, mode) \
	set_bit(ETHTOOL_LINK_##mode##_BIT, (ecmd)->advertising)

static void hinic3_add_speed_link_mode(unsigned long *bitmap, u32 mode)
{
	u32 i;

	for (i = 0; i < hw2ethtool_link_mode_table[mode].arr_size; i++) {
		if (hw2ethtool_link_mode_table[mode].link_mode_bit_arr[i] >=
		    __ETHTOOL_LINK_MODE_MASK_NBITS)
			continue;

		set_bit(hw2ethtool_link_mode_table[mode].link_mode_bit_arr[i],
			bitmap);
	}
}

/* Related to enum mag_cmd_port_speed */
static const u32 hw_to_ethtool_speed[] = {
	(u32)SPEED_UNKNOWN, SPEED_10,    SPEED_100,   SPEED_1000,   SPEED_10000,
	SPEED_25000,        SPEED_40000, SPEED_50000, SPEED_100000, SPEED_200000
};

static void
hinic3_add_ethtool_link_mode(struct hinic3_link_settings *link_settings,
			     u32 hw_link_mode, u32 name)
{
	unsigned long *advertising_mask = link_settings->advertising;
	unsigned long *supported_mask = link_settings->supported;
	u32 link_mode;

	for (link_mode = 0; link_mode < LINK_MODE_MAX_NUMBERS; link_mode++) {
		if (hw_link_mode & BIT(link_mode)) {
			if (name == GET_SUPPORTED_MODE)
				hinic3_add_speed_link_mode(supported_mask,
							   link_mode);
			else
				hinic3_add_speed_link_mode(advertising_mask,
							   link_mode);
		}
	}
}

static void
hinic3_link_speed_set(struct net_device *netdev,
		      struct hinic3_link_settings *link_settings,
		      struct hinic3_nic_port_info *port_info)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	bool link_status_up;
	int err;

	if (port_info->supported_mode != LINK_MODE_UNKNOWN)
		hinic3_add_ethtool_link_mode(link_settings,
					     port_info->supported_mode,
					     GET_SUPPORTED_MODE);
	if (port_info->advertised_mode != LINK_MODE_UNKNOWN)
		hinic3_add_ethtool_link_mode(link_settings,
					     port_info->advertised_mode,
					     GET_ADVERTISED_MODE);

	err = hinic3_get_link_status(nic_dev->hwdev, &link_status_up);
	if (!err && link_status_up) {
		link_settings->speed =
			port_info->speed < ARRAY_SIZE(hw_to_ethtool_speed) ?
			hw_to_ethtool_speed[port_info->speed] :
			(u32)SPEED_UNKNOWN;

		link_settings->duplex = port_info->duplex;
	} else {
		link_settings->speed = (u32)SPEED_UNKNOWN;
		link_settings->duplex = DUPLEX_UNKNOWN;
	}
}

static void
hinic3_link_port_type_set(struct hinic3_link_settings *link_settings,
			  u8 port_type)
{
	switch (port_type) {
	case MAG_CMD_WIRE_TYPE_ELECTRIC:
		HINIC3_ADD_SUPPORTED_LINK_MODE(link_settings, MODE_TP);
		HINIC3_ADD_ADVERTISED_LINK_MODE(link_settings, MODE_TP);
		link_settings->port = PORT_TP;
		break;

	case MAG_CMD_WIRE_TYPE_AOC:
	case MAG_CMD_WIRE_TYPE_MM:
	case MAG_CMD_WIRE_TYPE_SM:
		HINIC3_ADD_SUPPORTED_LINK_MODE(link_settings, MODE_FIBRE);
		HINIC3_ADD_ADVERTISED_LINK_MODE(link_settings, MODE_FIBRE);
		link_settings->port = PORT_FIBRE;
		break;

	case MAG_CMD_WIRE_TYPE_COPPER:
		HINIC3_ADD_SUPPORTED_LINK_MODE(link_settings, MODE_FIBRE);
		HINIC3_ADD_ADVERTISED_LINK_MODE(link_settings, MODE_FIBRE);
		link_settings->port = PORT_DA;
		break;

	case MAG_CMD_WIRE_TYPE_BACKPLANE:
		HINIC3_ADD_SUPPORTED_LINK_MODE(link_settings, MODE_Backplane);
		HINIC3_ADD_ADVERTISED_LINK_MODE(link_settings, MODE_Backplane);
		link_settings->port = PORT_NONE;
		break;

	default:
		link_settings->port = PORT_OTHER;
		break;
	}
}

static int
hinic3_get_link_pause_settings(struct net_device *netdev,
			       struct hinic3_link_settings *link_settings)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_nic_pause_config nic_pause = {};
	int err;

	err = hinic3_get_pause_info(nic_dev, &nic_pause);
	if (err) {
		netdev_err(netdev, "Failed to get pause param from hw\n");
		return err;
	}

	HINIC3_ADD_SUPPORTED_LINK_MODE(link_settings, MODE_Pause);
	if (nic_pause.rx_pause && nic_pause.tx_pause) {
		HINIC3_ADD_ADVERTISED_LINK_MODE(link_settings, MODE_Pause);
	} else if (nic_pause.tx_pause) {
		HINIC3_ADD_ADVERTISED_LINK_MODE(link_settings,
						MODE_Asym_Pause);
	} else if (nic_pause.rx_pause) {
		HINIC3_ADD_ADVERTISED_LINK_MODE(link_settings, MODE_Pause);
		HINIC3_ADD_ADVERTISED_LINK_MODE(link_settings,
						MODE_Asym_Pause);
	}

	return 0;
}

static int
hinic3_get_link_settings(struct net_device *netdev,
			 struct hinic3_link_settings *link_settings)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_nic_port_info port_info = {};
	int err;

	err = hinic3_get_port_info(nic_dev->hwdev, &port_info);
	if (err) {
		netdev_err(netdev, "Failed to get port info\n");
		return err;
	}

	hinic3_link_speed_set(netdev, link_settings, &port_info);

	hinic3_link_port_type_set(link_settings, port_info.port_type);

	link_settings->autoneg = port_info.autoneg_state == PORT_CFG_AN_ON ?
				 AUTONEG_ENABLE : AUTONEG_DISABLE;
	if (port_info.autoneg_cap)
		HINIC3_ADD_SUPPORTED_LINK_MODE(link_settings, MODE_Autoneg);
	if (port_info.autoneg_state == PORT_CFG_AN_ON)
		HINIC3_ADD_ADVERTISED_LINK_MODE(link_settings, MODE_Autoneg);

	if (!HINIC3_IS_VF(nic_dev->hwdev)) {
		err = hinic3_get_link_pause_settings(netdev, link_settings);
		if (err)
			return err;
	}

	return 0;
}

static int
hinic3_get_link_ksettings(struct net_device *netdev,
			  struct ethtool_link_ksettings *link_settings)
{
	struct ethtool_link_settings *base = &link_settings->base;
	struct hinic3_link_settings settings = {};
	int err;

	ethtool_link_ksettings_zero_link_mode(link_settings, supported);
	ethtool_link_ksettings_zero_link_mode(link_settings, advertising);

	err = hinic3_get_link_settings(netdev, &settings);
	if (err)
		return err;

	bitmap_copy(link_settings->link_modes.supported, settings.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_copy(link_settings->link_modes.advertising, settings.advertising,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);

	base->autoneg = settings.autoneg;
	base->speed = settings.speed;
	base->duplex = settings.duplex;
	base->port = settings.port;

	return 0;
}

static const struct ethtool_ops hinic3_ethtool_ops = {
	.supported_coalesce_params      = ETHTOOL_COALESCE_USECS |
					  ETHTOOL_COALESCE_PKT_RATE_RX_USECS,
	.get_link_ksettings             = hinic3_get_link_ksettings,
	.get_drvinfo                    = hinic3_get_drvinfo,
	.get_msglevel                   = hinic3_get_msglevel,
	.set_msglevel                   = hinic3_set_msglevel,
	.get_link                       = ethtool_op_get_link,
};

void hinic3_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &hinic3_ethtool_ops;
}
