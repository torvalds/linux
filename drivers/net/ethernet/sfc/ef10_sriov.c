/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2015 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
#include <linux/pci.h>
#include <linux/module.h>
#include "net_driver.h"
#include "efx.h"
#include "nic.h"
#include "mcdi_pcol.h"

#ifdef CONFIG_SFC_SRIOV
static int efx_ef10_pci_sriov_enable(struct efx_nic *efx, int num_vfs)
{
	int rc = 0;
	struct pci_dev *dev = efx->pci_dev;

	efx->vf_count = num_vfs;
	rc = pci_enable_sriov(dev, num_vfs);
	if (rc) {
		efx->vf_count = 0;
		netif_err(efx, probe, efx->net_dev,
			  "Failed to enable SRIOV VFs\n");
	}
	return rc;
}

static int efx_ef10_pci_sriov_disable(struct efx_nic *efx)
{
	struct pci_dev *dev = efx->pci_dev;

	efx->vf_count = 0;
	pci_disable_sriov(dev);
	return 0;
}
#endif

int efx_ef10_sriov_configure(struct efx_nic *efx, int num_vfs)
{
#ifdef CONFIG_SFC_SRIOV
	if (num_vfs == 0)
		return efx_ef10_pci_sriov_disable(efx);
	else
		return efx_ef10_pci_sriov_enable(efx, num_vfs);
#else
	return -EOPNOTSUPP;
#endif
}
