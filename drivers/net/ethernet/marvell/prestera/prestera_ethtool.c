// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved */

#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

#include "prestera_ethtool.h"
#include "prestera.h"
#include "prestera_hw.h"

#define PRESTERA_STATS_CNT \
	(sizeof(struct prestera_port_stats) / sizeof(u64))
#define PRESTERA_STATS_IDX(name) \
	(offsetof(struct prestera_port_stats, name) / sizeof(u64))
#define PRESTERA_STATS_FIELD(name)	\
	[PRESTERA_STATS_IDX(name)] = __stringify(name)

static const char driver_kind[] = "prestera";

static const struct prestera_link_mode {
	enum ethtool_link_mode_bit_indices eth_mode;
	u32 speed;
	u64 pr_mask;
	u8 duplex;
	u8 port_type;
} port_link_modes[PRESTERA_LINK_MODE_MAX] = {
	[PRESTERA_LINK_MODE_10baseT_Half] = {
		.eth_mode =  ETHTOOL_LINK_MODE_10baseT_Half_BIT,
		.speed = 10,
		.pr_mask = 1 << PRESTERA_LINK_MODE_10baseT_Half,
		.duplex = PRESTERA_PORT_DUPLEX_HALF,
		.port_type = PRESTERA_PORT_TYPE_TP,
	},
	[PRESTERA_LINK_MODE_10baseT_Full] = {
		.eth_mode =  ETHTOOL_LINK_MODE_10baseT_Full_BIT,
		.speed = 10,
		.pr_mask = 1 << PRESTERA_LINK_MODE_10baseT_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_TP,
	},
	[PRESTERA_LINK_MODE_100baseT_Half] = {
		.eth_mode =  ETHTOOL_LINK_MODE_100baseT_Half_BIT,
		.speed = 100,
		.pr_mask = 1 << PRESTERA_LINK_MODE_100baseT_Half,
		.duplex = PRESTERA_PORT_DUPLEX_HALF,
		.port_type = PRESTERA_PORT_TYPE_TP,
	},
	[PRESTERA_LINK_MODE_100baseT_Full] = {
		.eth_mode =  ETHTOOL_LINK_MODE_100baseT_Full_BIT,
		.speed = 100,
		.pr_mask = 1 << PRESTERA_LINK_MODE_100baseT_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_TP,
	},
	[PRESTERA_LINK_MODE_1000baseT_Half] = {
		.eth_mode =  ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
		.speed = 1000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_1000baseT_Half,
		.duplex = PRESTERA_PORT_DUPLEX_HALF,
		.port_type = PRESTERA_PORT_TYPE_TP,
	},
	[PRESTERA_LINK_MODE_1000baseT_Full] = {
		.eth_mode =  ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
		.speed = 1000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_1000baseT_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_TP,
	},
	[PRESTERA_LINK_MODE_1000baseX_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
		.speed = 1000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_1000baseX_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_FIBRE,
	},
	[PRESTERA_LINK_MODE_1000baseKX_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
		.speed = 1000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_1000baseKX_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_TP,
	},
	[PRESTERA_LINK_MODE_2500baseX_Full] = {
		.eth_mode =  ETHTOOL_LINK_MODE_2500baseX_Full_BIT,
		.speed = 2500,
		.pr_mask = 1 << PRESTERA_LINK_MODE_2500baseX_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
	},
	[PRESTERA_LINK_MODE_10GbaseKR_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
		.speed = 10000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_10GbaseKR_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_TP,
	},
	[PRESTERA_LINK_MODE_10GbaseSR_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_10000baseSR_Full_BIT,
		.speed = 10000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_10GbaseSR_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_FIBRE,
	},
	[PRESTERA_LINK_MODE_10GbaseLR_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_10000baseLR_Full_BIT,
		.speed = 10000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_10GbaseLR_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_FIBRE,
	},
	[PRESTERA_LINK_MODE_20GbaseKR2_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_20000baseKR2_Full_BIT,
		.speed = 20000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_20GbaseKR2_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_TP,
	},
	[PRESTERA_LINK_MODE_25GbaseCR_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
		.speed = 25000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_25GbaseCR_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_DA,
	},
	[PRESTERA_LINK_MODE_25GbaseKR_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
		.speed = 25000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_25GbaseKR_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_TP,
	},
	[PRESTERA_LINK_MODE_25GbaseSR_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
		.speed = 25000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_25GbaseSR_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_FIBRE,
	},
	[PRESTERA_LINK_MODE_40GbaseKR4_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
		.speed = 40000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_40GbaseKR4_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_TP,
	},
	[PRESTERA_LINK_MODE_40GbaseCR4_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
		.speed = 40000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_40GbaseCR4_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_DA,
	},
	[PRESTERA_LINK_MODE_40GbaseSR4_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT,
		.speed = 40000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_40GbaseSR4_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_FIBRE,
	},
	[PRESTERA_LINK_MODE_50GbaseCR2_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT,
		.speed = 50000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_50GbaseCR2_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_DA,
	},
	[PRESTERA_LINK_MODE_50GbaseKR2_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT,
		.speed = 50000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_50GbaseKR2_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_TP,
	},
	[PRESTERA_LINK_MODE_50GbaseSR2_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT,
		.speed = 50000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_50GbaseSR2_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_FIBRE,
	},
	[PRESTERA_LINK_MODE_100GbaseKR4_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
		.speed = 100000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_100GbaseKR4_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_TP,
	},
	[PRESTERA_LINK_MODE_100GbaseSR4_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
		.speed = 100000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_100GbaseSR4_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_FIBRE,
	},
	[PRESTERA_LINK_MODE_100GbaseCR4_Full] = {
		.eth_mode = ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
		.speed = 100000,
		.pr_mask = 1 << PRESTERA_LINK_MODE_100GbaseCR4_Full,
		.duplex = PRESTERA_PORT_DUPLEX_FULL,
		.port_type = PRESTERA_PORT_TYPE_DA,
	}
};

static const struct prestera_fec {
	u32 eth_fec;
	enum ethtool_link_mode_bit_indices eth_mode;
	u8 pr_fec;
} port_fec_caps[PRESTERA_PORT_FEC_MAX] = {
	[PRESTERA_PORT_FEC_OFF] = {
		.eth_fec = ETHTOOL_FEC_OFF,
		.eth_mode = ETHTOOL_LINK_MODE_FEC_NONE_BIT,
		.pr_fec = 1 << PRESTERA_PORT_FEC_OFF,
	},
	[PRESTERA_PORT_FEC_BASER] = {
		.eth_fec = ETHTOOL_FEC_BASER,
		.eth_mode = ETHTOOL_LINK_MODE_FEC_BASER_BIT,
		.pr_fec = 1 << PRESTERA_PORT_FEC_BASER,
	},
	[PRESTERA_PORT_FEC_RS] = {
		.eth_fec = ETHTOOL_FEC_RS,
		.eth_mode = ETHTOOL_LINK_MODE_FEC_RS_BIT,
		.pr_fec = 1 << PRESTERA_PORT_FEC_RS,
	}
};

static const struct prestera_port_type {
	enum ethtool_link_mode_bit_indices eth_mode;
	u8 eth_type;
} port_types[PRESTERA_PORT_TYPE_MAX] = {
	[PRESTERA_PORT_TYPE_NONE] = {
		.eth_mode = __ETHTOOL_LINK_MODE_MASK_NBITS,
		.eth_type = PORT_NONE,
	},
	[PRESTERA_PORT_TYPE_TP] = {
		.eth_mode = ETHTOOL_LINK_MODE_TP_BIT,
		.eth_type = PORT_TP,
	},
	[PRESTERA_PORT_TYPE_AUI] = {
		.eth_mode = ETHTOOL_LINK_MODE_AUI_BIT,
		.eth_type = PORT_AUI,
	},
	[PRESTERA_PORT_TYPE_MII] = {
		.eth_mode = ETHTOOL_LINK_MODE_MII_BIT,
		.eth_type = PORT_MII,
	},
	[PRESTERA_PORT_TYPE_FIBRE] = {
		.eth_mode = ETHTOOL_LINK_MODE_FIBRE_BIT,
		.eth_type = PORT_FIBRE,
	},
	[PRESTERA_PORT_TYPE_BNC] = {
		.eth_mode = ETHTOOL_LINK_MODE_BNC_BIT,
		.eth_type = PORT_BNC,
	},
	[PRESTERA_PORT_TYPE_DA] = {
		.eth_mode = ETHTOOL_LINK_MODE_TP_BIT,
		.eth_type = PORT_TP,
	},
	[PRESTERA_PORT_TYPE_OTHER] = {
		.eth_mode = __ETHTOOL_LINK_MODE_MASK_NBITS,
		.eth_type = PORT_OTHER,
	}
};

static const char prestera_cnt_name[PRESTERA_STATS_CNT][ETH_GSTRING_LEN] = {
	PRESTERA_STATS_FIELD(good_octets_received),
	PRESTERA_STATS_FIELD(bad_octets_received),
	PRESTERA_STATS_FIELD(mac_trans_error),
	PRESTERA_STATS_FIELD(broadcast_frames_received),
	PRESTERA_STATS_FIELD(multicast_frames_received),
	PRESTERA_STATS_FIELD(frames_64_octets),
	PRESTERA_STATS_FIELD(frames_65_to_127_octets),
	PRESTERA_STATS_FIELD(frames_128_to_255_octets),
	PRESTERA_STATS_FIELD(frames_256_to_511_octets),
	PRESTERA_STATS_FIELD(frames_512_to_1023_octets),
	PRESTERA_STATS_FIELD(frames_1024_to_max_octets),
	PRESTERA_STATS_FIELD(excessive_collision),
	PRESTERA_STATS_FIELD(multicast_frames_sent),
	PRESTERA_STATS_FIELD(broadcast_frames_sent),
	PRESTERA_STATS_FIELD(fc_sent),
	PRESTERA_STATS_FIELD(fc_received),
	PRESTERA_STATS_FIELD(buffer_overrun),
	PRESTERA_STATS_FIELD(undersize),
	PRESTERA_STATS_FIELD(fragments),
	PRESTERA_STATS_FIELD(oversize),
	PRESTERA_STATS_FIELD(jabber),
	PRESTERA_STATS_FIELD(rx_error_frame_received),
	PRESTERA_STATS_FIELD(bad_crc),
	PRESTERA_STATS_FIELD(collisions),
	PRESTERA_STATS_FIELD(late_collision),
	PRESTERA_STATS_FIELD(unicast_frames_received),
	PRESTERA_STATS_FIELD(unicast_frames_sent),
	PRESTERA_STATS_FIELD(sent_multiple),
	PRESTERA_STATS_FIELD(sent_deferred),
	PRESTERA_STATS_FIELD(good_octets_sent),
};

static void prestera_ethtool_get_drvinfo(struct net_device *dev,
					 struct ethtool_drvinfo *drvinfo)
{
	struct prestera_port *port = netdev_priv(dev);
	struct prestera_switch *sw = port->sw;

	strlcpy(drvinfo->driver, driver_kind, sizeof(drvinfo->driver));
	strlcpy(drvinfo->bus_info, dev_name(prestera_dev(sw)),
		sizeof(drvinfo->bus_info));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%d.%d.%d",
		 sw->dev->fw_rev.maj,
		 sw->dev->fw_rev.min,
		 sw->dev->fw_rev.sub);
}

static u8 prestera_port_type_get(struct prestera_port *port)
{
	if (port->caps.type < PRESTERA_PORT_TYPE_MAX)
		return port_types[port->caps.type].eth_type;

	return PORT_OTHER;
}

static int prestera_port_type_set(const struct ethtool_link_ksettings *ecmd,
				  struct prestera_port *port)
{
	u32 new_mode = PRESTERA_LINK_MODE_MAX;
	u32 type, mode;

	for (type = 0; type < PRESTERA_PORT_TYPE_MAX; type++) {
		if (port_types[type].eth_type == ecmd->base.port &&
		    test_bit(port_types[type].eth_mode,
			     ecmd->link_modes.supported)) {
			break;
		}
	}

	if (type == port->caps.type)
		return 0;
	if (type != port->caps.type && ecmd->base.autoneg == AUTONEG_ENABLE)
		return -EINVAL;
	if (type == PRESTERA_PORT_TYPE_MAX)
		return -EOPNOTSUPP;

	for (mode = 0; mode < PRESTERA_LINK_MODE_MAX; mode++) {
		if ((port_link_modes[mode].pr_mask &
		    port->caps.supp_link_modes) &&
		    type == port_link_modes[mode].port_type) {
			new_mode = mode;
		}
	}

	if (new_mode >= PRESTERA_LINK_MODE_MAX)
		return -EINVAL;

	port->caps.type = type;
	port->autoneg = false;

	return 0;
}

static void prestera_modes_to_eth(unsigned long *eth_modes, u64 link_modes,
				  u8 fec, u8 type)
{
	u32 mode;

	for (mode = 0; mode < PRESTERA_LINK_MODE_MAX; mode++) {
		if ((port_link_modes[mode].pr_mask & link_modes) == 0)
			continue;

		if (type != PRESTERA_PORT_TYPE_NONE &&
		    port_link_modes[mode].port_type != type)
			continue;

		__set_bit(port_link_modes[mode].eth_mode, eth_modes);
	}

	for (mode = 0; mode < PRESTERA_PORT_FEC_MAX; mode++) {
		if ((port_fec_caps[mode].pr_fec & fec) == 0)
			continue;

		__set_bit(port_fec_caps[mode].eth_mode, eth_modes);
	}
}

static void prestera_modes_from_eth(const unsigned long *eth_modes,
				    u64 *link_modes, u8 *fec, u8 type)
{
	u64 adver_modes = 0;
	u32 fec_modes = 0;
	u32 mode;

	for (mode = 0; mode < PRESTERA_LINK_MODE_MAX; mode++) {
		if (!test_bit(port_link_modes[mode].eth_mode, eth_modes))
			continue;

		if (port_link_modes[mode].port_type != type)
			continue;

		adver_modes |= port_link_modes[mode].pr_mask;
	}

	for (mode = 0; mode < PRESTERA_PORT_FEC_MAX; mode++) {
		if (!test_bit(port_fec_caps[mode].eth_mode, eth_modes))
			continue;

		fec_modes |= port_fec_caps[mode].pr_fec;
	}

	*link_modes = adver_modes;
	*fec = fec_modes;
}

static void prestera_port_supp_types_get(struct ethtool_link_ksettings *ecmd,
					 struct prestera_port *port)
{
	u32 mode;
	u8 ptype;

	for (mode = 0; mode < PRESTERA_LINK_MODE_MAX; mode++) {
		if ((port_link_modes[mode].pr_mask &
		    port->caps.supp_link_modes) == 0)
			continue;

		ptype = port_link_modes[mode].port_type;
		__set_bit(port_types[ptype].eth_mode,
			  ecmd->link_modes.supported);
	}
}

static void prestera_port_remote_cap_get(struct ethtool_link_ksettings *ecmd,
					 struct prestera_port *port)
{
	struct prestera_port_phy_state *state = &port->state_phy;
	bool asym_pause;
	bool pause;
	u64 bitmap;
	int err;

	err = prestera_hw_port_phy_mode_get(port, NULL, &state->lmode_bmap,
					    &state->remote_fc.pause,
					    &state->remote_fc.asym_pause);
	if (err)
		netdev_warn(port->dev, "Remote link caps get failed %d",
			    port->caps.transceiver);

	bitmap = state->lmode_bmap;

	prestera_modes_to_eth(ecmd->link_modes.lp_advertising,
			      bitmap, 0, PRESTERA_PORT_TYPE_NONE);

	if (!bitmap_empty(ecmd->link_modes.lp_advertising,
			  __ETHTOOL_LINK_MODE_MASK_NBITS)) {
		ethtool_link_ksettings_add_link_mode(ecmd,
						     lp_advertising,
						     Autoneg);
	}

	pause = state->remote_fc.pause;
	asym_pause = state->remote_fc.asym_pause;

	if (pause)
		ethtool_link_ksettings_add_link_mode(ecmd,
						     lp_advertising,
						     Pause);
	if (asym_pause)
		ethtool_link_ksettings_add_link_mode(ecmd,
						     lp_advertising,
						     Asym_Pause);
}

static void prestera_port_link_mode_get(struct ethtool_link_ksettings *ecmd,
					struct prestera_port *port)
{
	struct prestera_port_mac_state *state = &port->state_mac;
	u32 speed;
	u8 duplex;
	int err;

	if (!port->state_mac.oper)
		return;

	if (state->speed == SPEED_UNKNOWN || state->duplex == DUPLEX_UNKNOWN) {
		err = prestera_hw_port_mac_mode_get(port, NULL, &speed,
						    &duplex, NULL);
		if (err) {
			state->speed = SPEED_UNKNOWN;
			state->duplex = DUPLEX_UNKNOWN;
		} else {
			state->speed = speed;
			state->duplex = duplex == PRESTERA_PORT_DUPLEX_FULL ?
					  DUPLEX_FULL : DUPLEX_HALF;
		}
	}

	ecmd->base.speed = port->state_mac.speed;
	ecmd->base.duplex = port->state_mac.duplex;
}

static void prestera_port_mdix_get(struct ethtool_link_ksettings *ecmd,
				   struct prestera_port *port)
{
	struct prestera_port_phy_state *state = &port->state_phy;

	if (prestera_hw_port_phy_mode_get(port,
					  &state->mdix, NULL, NULL, NULL)) {
		netdev_warn(port->dev, "MDIX params get failed");
		state->mdix = ETH_TP_MDI_INVALID;
	}

	ecmd->base.eth_tp_mdix = port->state_phy.mdix;
	ecmd->base.eth_tp_mdix_ctrl = port->cfg_phy.mdix;
}

static int
prestera_ethtool_get_link_ksettings(struct net_device *dev,
				    struct ethtool_link_ksettings *ecmd)
{
	struct prestera_port *port = netdev_priv(dev);

	ethtool_link_ksettings_zero_link_mode(ecmd, supported);
	ethtool_link_ksettings_zero_link_mode(ecmd, advertising);
	ethtool_link_ksettings_zero_link_mode(ecmd, lp_advertising);
	ecmd->base.speed = SPEED_UNKNOWN;
	ecmd->base.duplex = DUPLEX_UNKNOWN;

	if (port->phy_link)
		return phylink_ethtool_ksettings_get(port->phy_link, ecmd);

	ecmd->base.autoneg = port->autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE;

	if (port->caps.type == PRESTERA_PORT_TYPE_TP) {
		ethtool_link_ksettings_add_link_mode(ecmd, supported, Autoneg);

		if (netif_running(dev) &&
		    (port->autoneg ||
		     port->caps.transceiver == PRESTERA_PORT_TCVR_COPPER))
			ethtool_link_ksettings_add_link_mode(ecmd, advertising,
							     Autoneg);
	}

	prestera_modes_to_eth(ecmd->link_modes.supported,
			      port->caps.supp_link_modes,
			      port->caps.supp_fec,
			      port->caps.type);

	prestera_port_supp_types_get(ecmd, port);

	if (netif_carrier_ok(dev))
		prestera_port_link_mode_get(ecmd, port);

	ecmd->base.port = prestera_port_type_get(port);

	if (port->autoneg) {
		if (netif_running(dev))
			prestera_modes_to_eth(ecmd->link_modes.advertising,
					      port->adver_link_modes,
					      port->adver_fec,
					      port->caps.type);

		if (netif_carrier_ok(dev) &&
		    port->caps.transceiver == PRESTERA_PORT_TCVR_COPPER)
			prestera_port_remote_cap_get(ecmd, port);
	}

	if (port->caps.type == PRESTERA_PORT_TYPE_TP &&
	    port->caps.transceiver == PRESTERA_PORT_TCVR_COPPER)
		prestera_port_mdix_get(ecmd, port);

	return 0;
}

static int prestera_port_mdix_set(const struct ethtool_link_ksettings *ecmd,
				  struct prestera_port *port)
{
	if (ecmd->base.eth_tp_mdix_ctrl != ETH_TP_MDI_INVALID &&
	    port->caps.transceiver ==  PRESTERA_PORT_TCVR_COPPER &&
	    port->caps.type == PRESTERA_PORT_TYPE_TP) {
		port->cfg_phy.mdix = ecmd->base.eth_tp_mdix_ctrl;
		return prestera_hw_port_phy_mode_set(port, port->cfg_phy.admin,
						     port->autoneg,
						     port->cfg_phy.mode,
						     port->adver_link_modes,
						     port->cfg_phy.mdix);
	}
	return 0;

}

static int prestera_port_link_mode_set(struct prestera_port *port,
				       u32 speed, u8 duplex, u8 type)
{
	u32 new_mode = PRESTERA_LINK_MODE_MAX;
	u32 mode;
	int err;

	for (mode = 0; mode < PRESTERA_LINK_MODE_MAX; mode++) {
		if (speed != SPEED_UNKNOWN &&
		    speed != port_link_modes[mode].speed)
			continue;

		if (duplex != DUPLEX_UNKNOWN &&
		    duplex != port_link_modes[mode].duplex)
			continue;

		if (!(port_link_modes[mode].pr_mask &
		    port->caps.supp_link_modes))
			continue;

		if (type != port_link_modes[mode].port_type)
			continue;

		new_mode = mode;
		break;
	}

	if (new_mode == PRESTERA_LINK_MODE_MAX)
		return -EOPNOTSUPP;

	err = prestera_hw_port_phy_mode_set(port, port->cfg_phy.admin,
					    false, new_mode, 0,
					    port->cfg_phy.mdix);
	if (err)
		return err;

	port->adver_fec = BIT(PRESTERA_PORT_FEC_OFF);
	port->adver_link_modes = 0;
	port->cfg_phy.mode = new_mode;
	port->autoneg = false;

	return 0;
}

static int
prestera_port_speed_duplex_set(const struct ethtool_link_ksettings *ecmd,
			       struct prestera_port *port)
{
	u8 duplex = DUPLEX_UNKNOWN;

	if (ecmd->base.duplex != DUPLEX_UNKNOWN)
		duplex = ecmd->base.duplex == DUPLEX_FULL ?
			 PRESTERA_PORT_DUPLEX_FULL : PRESTERA_PORT_DUPLEX_HALF;

	return prestera_port_link_mode_set(port, ecmd->base.speed, duplex,
					   port->caps.type);
}

static int
prestera_ethtool_set_link_ksettings(struct net_device *dev,
				    const struct ethtool_link_ksettings *ecmd)
{
	struct prestera_port *port = netdev_priv(dev);
	u64 adver_modes;
	u8 adver_fec;
	int err;

	if (port->phy_link)
		return phylink_ethtool_ksettings_set(port->phy_link, ecmd);

	err = prestera_port_type_set(ecmd, port);
	if (err)
		return err;

	if (port->caps.transceiver == PRESTERA_PORT_TCVR_COPPER) {
		err = prestera_port_mdix_set(ecmd, port);
		if (err)
			return err;
	}

	prestera_modes_from_eth(ecmd->link_modes.advertising, &adver_modes,
				&adver_fec, port->caps.type);

	if (ecmd->base.autoneg == AUTONEG_ENABLE)
		err = prestera_port_autoneg_set(port, adver_modes);
	else
		err = prestera_port_speed_duplex_set(ecmd, port);

	return err;
}

static int prestera_ethtool_get_fecparam(struct net_device *dev,
					 struct ethtool_fecparam *fecparam)
{
	struct prestera_port *port = netdev_priv(dev);
	u8 active;
	u32 mode;
	int err;

	err = prestera_hw_port_mac_mode_get(port, NULL, NULL, NULL, &active);
	if (err)
		return err;

	fecparam->fec = 0;

	for (mode = 0; mode < PRESTERA_PORT_FEC_MAX; mode++) {
		if ((port_fec_caps[mode].pr_fec & port->caps.supp_fec) == 0)
			continue;

		fecparam->fec |= port_fec_caps[mode].eth_fec;
	}

	if (active < PRESTERA_PORT_FEC_MAX)
		fecparam->active_fec = port_fec_caps[active].eth_fec;
	else
		fecparam->active_fec = ETHTOOL_FEC_AUTO;

	return 0;
}

static int prestera_ethtool_set_fecparam(struct net_device *dev,
					 struct ethtool_fecparam *fecparam)
{
	struct prestera_port *port = netdev_priv(dev);
	struct prestera_port_mac_config cfg_mac;
	u32 mode;
	u8 fec;

	if (port->autoneg) {
		netdev_err(dev, "FEC set is not allowed while autoneg is on\n");
		return -EINVAL;
	}

	if (port->caps.transceiver == PRESTERA_PORT_TCVR_SFP) {
		netdev_err(dev, "FEC set is not allowed on non-SFP ports\n");
		return -EINVAL;
	}

	fec = PRESTERA_PORT_FEC_MAX;
	for (mode = 0; mode < PRESTERA_PORT_FEC_MAX; mode++) {
		if ((port_fec_caps[mode].eth_fec & fecparam->fec) &&
		    (port_fec_caps[mode].pr_fec & port->caps.supp_fec)) {
			fec = mode;
			break;
		}
	}

	prestera_port_cfg_mac_read(port, &cfg_mac);

	if (fec == cfg_mac.fec)
		return 0;

	if (fec == PRESTERA_PORT_FEC_MAX) {
		netdev_err(dev, "Unsupported FEC requested");
		return -EINVAL;
	}

	cfg_mac.fec = fec;

	return prestera_port_cfg_mac_write(port, &cfg_mac);
}

static int prestera_ethtool_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return PRESTERA_STATS_CNT;
	default:
		return -EOPNOTSUPP;
	}
}

static void prestera_ethtool_get_strings(struct net_device *dev,
					 u32 stringset, u8 *data)
{
	if (stringset != ETH_SS_STATS)
		return;

	memcpy(data, prestera_cnt_name, sizeof(prestera_cnt_name));
}

static void prestera_ethtool_get_stats(struct net_device *dev,
				       struct ethtool_stats *stats, u64 *data)
{
	struct prestera_port *port = netdev_priv(dev);
	struct prestera_port_stats *port_stats;

	port_stats = &port->cached_hw_stats.stats;

	memcpy(data, port_stats, sizeof(*port_stats));
}

static int prestera_ethtool_nway_reset(struct net_device *dev)
{
	struct prestera_port *port = netdev_priv(dev);

	if (netif_running(dev) &&
	    port->caps.transceiver == PRESTERA_PORT_TCVR_COPPER &&
	    port->caps.type == PRESTERA_PORT_TYPE_TP)
		return prestera_hw_port_autoneg_restart(port);

	return -EINVAL;
}

const struct ethtool_ops prestera_ethtool_ops = {
	.get_drvinfo = prestera_ethtool_get_drvinfo,
	.get_link_ksettings = prestera_ethtool_get_link_ksettings,
	.set_link_ksettings = prestera_ethtool_set_link_ksettings,
	.get_fecparam = prestera_ethtool_get_fecparam,
	.set_fecparam = prestera_ethtool_set_fecparam,
	.get_sset_count = prestera_ethtool_get_sset_count,
	.get_strings = prestera_ethtool_get_strings,
	.get_ethtool_stats = prestera_ethtool_get_stats,
	.get_link = ethtool_op_get_link,
	.nway_reset = prestera_ethtool_nway_reset
};
