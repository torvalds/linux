/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_ETHTOOL_COMMON_H
#define EFX_ETHTOOL_COMMON_H

void efx_siena_ethtool_get_drvinfo(struct net_device *net_dev,
				   struct ethtool_drvinfo *info);
u32 efx_siena_ethtool_get_msglevel(struct net_device *net_dev);
void efx_siena_ethtool_set_msglevel(struct net_device *net_dev, u32 msg_enable);
void efx_siena_ethtool_self_test(struct net_device *net_dev,
				 struct ethtool_test *test, u64 *data);
void efx_siena_ethtool_get_pauseparam(struct net_device *net_dev,
				      struct ethtool_pauseparam *pause);
int efx_siena_ethtool_set_pauseparam(struct net_device *net_dev,
				     struct ethtool_pauseparam *pause);
int efx_siena_ethtool_get_sset_count(struct net_device *net_dev, int string_set);
void efx_siena_ethtool_get_strings(struct net_device *net_dev, u32 string_set,
				   u8 *strings);
void efx_siena_ethtool_get_stats(struct net_device *net_dev,
				 struct ethtool_stats *stats __always_unused,
				 u64 *data);
int efx_siena_ethtool_get_link_ksettings(struct net_device *net_dev,
					 struct ethtool_link_ksettings *out);
int efx_siena_ethtool_set_link_ksettings(struct net_device *net_dev,
				const struct ethtool_link_ksettings *settings);
int efx_siena_ethtool_get_fecparam(struct net_device *net_dev,
				   struct ethtool_fecparam *fecparam);
int efx_siena_ethtool_set_fecparam(struct net_device *net_dev,
				   struct ethtool_fecparam *fecparam);
int efx_siena_ethtool_get_rxnfc(struct net_device *net_dev,
				struct ethtool_rxnfc *info, u32 *rule_locs);
int efx_siena_ethtool_set_rxnfc(struct net_device *net_dev,
				struct ethtool_rxnfc *info);
u32 efx_siena_ethtool_get_rxfh_indir_size(struct net_device *net_dev);
u32 efx_siena_ethtool_get_rxfh_key_size(struct net_device *net_dev);
int efx_siena_ethtool_get_rxfh(struct net_device *net_dev,
			       struct ethtool_rxfh_param *rxfh);
int efx_siena_ethtool_set_rxfh(struct net_device *net_dev,
			       struct ethtool_rxfh_param *rxfh,
			       struct netlink_ext_ack *extack);
int efx_siena_ethtool_reset(struct net_device *net_dev, u32 *flags);
int efx_siena_ethtool_get_module_eeprom(struct net_device *net_dev,
					struct ethtool_eeprom *ee,
					u8 *data);
int efx_siena_ethtool_get_module_info(struct net_device *net_dev,
				      struct ethtool_modinfo *modinfo);
#endif
