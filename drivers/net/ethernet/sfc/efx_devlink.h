/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for AMD network controllers and boards
 * Copyright (C) 2023, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef _EFX_DEVLINK_H
#define _EFX_DEVLINK_H

#include "net_driver.h"
#include <net/devlink.h>

int efx_probe_devlink_and_lock(struct efx_nic *efx);
void efx_probe_devlink_unlock(struct efx_nic *efx);
void efx_fini_devlink_lock(struct efx_nic *efx);
void efx_fini_devlink_and_unlock(struct efx_nic *efx);

#endif	/* _EFX_DEVLINK_H */
