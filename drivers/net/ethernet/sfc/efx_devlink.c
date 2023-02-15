// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for AMD network controllers and boards
 * Copyright (C) 2023, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include "efx_devlink.h"

struct efx_devlink {
	struct efx_nic *efx;
};

static const struct devlink_ops sfc_devlink_ops = {
};

void efx_fini_devlink_lock(struct efx_nic *efx)
{
	if (efx->devlink)
		devl_lock(efx->devlink);
}

void efx_fini_devlink_and_unlock(struct efx_nic *efx)
{
	if (efx->devlink) {
		devl_unregister(efx->devlink);
		devl_unlock(efx->devlink);
		devlink_free(efx->devlink);
		efx->devlink = NULL;
	}
}

int efx_probe_devlink_and_lock(struct efx_nic *efx)
{
	struct efx_devlink *devlink_private;

	if (efx->type->is_vf)
		return 0;

	efx->devlink = devlink_alloc(&sfc_devlink_ops,
				     sizeof(struct efx_devlink),
				     &efx->pci_dev->dev);
	if (!efx->devlink)
		return -ENOMEM;

	devl_lock(efx->devlink);
	devlink_private = devlink_priv(efx->devlink);
	devlink_private->efx = efx;

	devl_register(efx->devlink);

	return 0;
}

void efx_probe_devlink_unlock(struct efx_nic *efx)
{
	if (!efx->devlink)
		return;

	devl_unlock(efx->devlink);
}
