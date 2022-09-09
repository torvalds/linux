/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
#ifndef EFX_MCDI_PORT_COMMON_H
#define EFX_MCDI_PORT_COMMON_H

#include "net_driver.h"
#include "mcdi.h"
#include "mcdi_pcol.h"

struct efx_mcdi_phy_data {
	u32 flags;
	u32 type;
	u32 supported_cap;
	u32 channel;
	u32 port;
	u32 stats_mask;
	u8 name[20];
	u32 media;
	u32 mmd_mask;
	u8 revision[20];
	u32 forced_cap;
};

void efx_siena_link_set_advertising(struct efx_nic *efx,
				    const unsigned long *advertising);
bool efx_siena_mcdi_phy_poll(struct efx_nic *efx);
int efx_siena_mcdi_phy_probe(struct efx_nic *efx);
void efx_siena_mcdi_phy_remove(struct efx_nic *efx);
void efx_siena_mcdi_phy_get_link_ksettings(struct efx_nic *efx,
					   struct ethtool_link_ksettings *cmd);
int efx_siena_mcdi_phy_set_link_ksettings(struct efx_nic *efx,
					  const struct ethtool_link_ksettings *cmd);
int efx_siena_mcdi_phy_get_fecparam(struct efx_nic *efx,
				    struct ethtool_fecparam *fec);
int efx_siena_mcdi_phy_set_fecparam(struct efx_nic *efx,
				    const struct ethtool_fecparam *fec);
int efx_siena_mcdi_phy_test_alive(struct efx_nic *efx);
int efx_siena_mcdi_port_reconfigure(struct efx_nic *efx);
int efx_siena_mcdi_phy_run_tests(struct efx_nic *efx, int *results,
				 unsigned int flags);
const char *efx_siena_mcdi_phy_test_name(struct efx_nic *efx,
					 unsigned int index);
int efx_siena_mcdi_phy_get_module_eeprom(struct efx_nic *efx,
					 struct ethtool_eeprom *ee, u8 *data);
int efx_siena_mcdi_phy_get_module_info(struct efx_nic *efx,
				       struct ethtool_modinfo *modinfo);
int efx_siena_mcdi_set_mac(struct efx_nic *efx);
int efx_siena_mcdi_mac_init_stats(struct efx_nic *efx);
void efx_siena_mcdi_mac_fini_stats(struct efx_nic *efx);

#endif
