// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 * Copyright 2020-2022 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "ef100_sriov.h"
#include "ef100_nic.h"

static int efx_ef100_pci_sriov_enable(struct efx_nic *efx, int num_vfs)
{
	struct pci_dev *dev = efx->pci_dev;
	int rc;

	efx->vf_count = num_vfs;
	rc = pci_enable_sriov(dev, num_vfs);
	if (rc)
		goto fail;

	return 0;

fail:
	netif_err(efx, probe, efx->net_dev, "Failed to enable SRIOV VFs\n");
	efx->vf_count = 0;
	return rc;
}

int efx_ef100_pci_sriov_disable(struct efx_nic *efx)
{
	struct pci_dev *dev = efx->pci_dev;
	unsigned int vfs_assigned;

	vfs_assigned = pci_vfs_assigned(dev);
	if (vfs_assigned) {
		netif_info(efx, drv, efx->net_dev, "VFs are assigned to guests; "
			   "please detach them before disabling SR-IOV\n");
		return -EBUSY;
	}

	pci_disable_sriov(dev);

	return 0;
}

int efx_ef100_sriov_configure(struct efx_nic *efx, int num_vfs)
{
	if (num_vfs == 0)
		return efx_ef100_pci_sriov_disable(efx);
	else
		return efx_ef100_pci_sriov_enable(efx, num_vfs);
}
