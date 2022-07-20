/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 * Copyright 2020-2022 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EF100_MAE_H
#define EF100_MAE_H
/* MCDI interface for the ef100 Match-Action Engine */

#include "net_driver.h"

void efx_mae_mport_vf(struct efx_nic *efx, u32 vf_id, u32 *out);

int efx_mae_lookup_mport(struct efx_nic *efx, u32 selector, u32 *id);

#endif /* EF100_MAE_H */
