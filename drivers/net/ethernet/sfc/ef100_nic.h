/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 * Copyright 2019-2020 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include "nic_common.h"

extern const struct efx_nic_type ef100_pf_nic_type;

int ef100_probe_pf(struct efx_nic *efx);
void ef100_remove(struct efx_nic *efx);

struct ef100_nic_data {
	struct efx_nic *efx;
	u16 warm_boot_count;
};

#define efx_ef100_has_cap(caps, flag) \
	(!!((caps) & BIT_ULL(MC_CMD_GET_CAPABILITIES_V4_OUT_ ## flag ## _LBN)))
