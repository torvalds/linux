/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2019 NXP */
#ifndef DPAA2_MAC_H
#define DPAA2_MAC_H

#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phylink.h>

#include "dpmac.h"
#include "dpmac-cmd.h"

struct dpaa2_mac {
	struct fsl_mc_device *mc_dev;
	struct dpmac_link_state state;
	struct net_device *net_dev;
	struct fsl_mc_io *mc_io;

	struct phylink_config phylink_config;
	struct phylink *phylink;
	phy_interface_t if_mode;
	enum dpmac_link_type if_link_type;
};

bool dpaa2_mac_is_type_fixed(struct fsl_mc_device *dpmac_dev,
			     struct fsl_mc_io *mc_io);

int dpaa2_mac_connect(struct dpaa2_mac *mac);

void dpaa2_mac_disconnect(struct dpaa2_mac *mac);

int dpaa2_mac_get_sset_count(void);

void dpaa2_mac_get_strings(u8 *data);

void dpaa2_mac_get_ethtool_stats(struct dpaa2_mac *mac, u64 *data);

#endif /* DPAA2_MAC_H */
