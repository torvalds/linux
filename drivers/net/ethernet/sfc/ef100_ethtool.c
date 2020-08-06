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

/*	Ethtool options available
 */
const struct ethtool_ops ef100_ethtool_ops = {
	.get_drvinfo		= efx_ethtool_get_drvinfo,
};
