/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005 Fen Systems Ltd.
 * Copyright 2006 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_ETHTOOL_H
#define EFX_ETHTOOL_H

#include "net_driver.h"

/*
 * Ethtool support
 */

extern int efx_ethtool_get_settings(struct net_device *net_dev,
				    struct ethtool_cmd *ecmd);
extern int efx_ethtool_set_settings(struct net_device *net_dev,
				    struct ethtool_cmd *ecmd);

extern const struct ethtool_ops efx_ethtool_ops;

#endif /* EFX_ETHTOOL_H */
