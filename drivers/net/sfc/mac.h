/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2007 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_MAC_H
#define EFX_MAC_H

#include "net_driver.h"

extern int falcon_init_xmac(struct efx_nic *efx);
extern void falcon_reconfigure_xmac(struct efx_nic *efx);
extern void falcon_update_stats_xmac(struct efx_nic *efx);
extern void falcon_fini_xmac(struct efx_nic *efx);
extern int falcon_check_xmac(struct efx_nic *efx);
extern void falcon_xmac_sim_phy_event(struct efx_nic *efx);
extern int falcon_xmac_get_settings(struct efx_nic *efx,
				    struct ethtool_cmd *ecmd);
extern int falcon_xmac_set_settings(struct efx_nic *efx,
				    struct ethtool_cmd *ecmd);
extern int falcon_xmac_set_pause(struct efx_nic *efx,
				 enum efx_fc_type pause_params);

#endif
