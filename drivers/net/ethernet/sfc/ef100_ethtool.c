// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 * Copyright 2019-2020 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
#include <linux/module.h>
#include <linux/netdevice.h>
#include "net_driver.h"
#include "efx.h"
#include "mcdi_port_common.h"
#include "ethtool_common.h"
#include "ef100_ethtool.h"
#include "mcdi_functions.h"

/* This is the maximum number of descriptor rings supported by the QDMA */
#define EFX_EF100_MAX_DMAQ_SIZE 16384UL

static void
ef100_ethtool_get_ringparam(struct net_device *net_dev,
			    struct ethtool_ringparam *ring,
			    struct kernel_ethtool_ringparam *kernel_ring,
			    struct netlink_ext_ack *extack)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	ring->rx_max_pending = EFX_EF100_MAX_DMAQ_SIZE;
	ring->tx_max_pending = EFX_EF100_MAX_DMAQ_SIZE;
	ring->rx_pending = efx->rxq_entries;
	ring->tx_pending = efx->txq_entries;
}

/*	Ethtool options available
 */
const struct ethtool_ops ef100_ethtool_ops = {
	.get_drvinfo		= efx_ethtool_get_drvinfo,
	.get_msglevel		= efx_ethtool_get_msglevel,
	.set_msglevel		= efx_ethtool_set_msglevel,
	.get_pauseparam         = efx_ethtool_get_pauseparam,
	.set_pauseparam         = efx_ethtool_set_pauseparam,
	.get_sset_count		= efx_ethtool_get_sset_count,
	.self_test		= efx_ethtool_self_test,
	.get_strings		= efx_ethtool_get_strings,
	.get_link_ksettings	= efx_ethtool_get_link_ksettings,
	.set_link_ksettings	= efx_ethtool_set_link_ksettings,
	.get_link		= ethtool_op_get_link,
	.get_ringparam		= ef100_ethtool_get_ringparam,
	.get_fecparam		= efx_ethtool_get_fecparam,
	.set_fecparam		= efx_ethtool_set_fecparam,
	.get_ethtool_stats	= efx_ethtool_get_stats,
	.get_rxnfc              = efx_ethtool_get_rxnfc,
	.set_rxnfc              = efx_ethtool_set_rxnfc,
	.reset                  = efx_ethtool_reset,

	.get_rxfh_indir_size	= efx_ethtool_get_rxfh_indir_size,
	.get_rxfh_key_size	= efx_ethtool_get_rxfh_key_size,
	.get_rxfh		= efx_ethtool_get_rxfh,
	.set_rxfh		= efx_ethtool_set_rxfh,
	.get_rxfh_context	= efx_ethtool_get_rxfh_context,
	.set_rxfh_context	= efx_ethtool_set_rxfh_context,

	.get_module_info	= efx_ethtool_get_module_info,
	.get_module_eeprom	= efx_ethtool_get_module_eeprom,
};
