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
#include "ef10_sriov.h"
#include "efx.h"
#include "nic.h"
#include "mcdi_pcol.h"

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

int efx_ef10_sriov_configure(struct efx_nic *efx, int num_vfs)
{
	if (num_vfs == 0)
		return efx_ef10_pci_sriov_disable(efx);
	else
		return efx_ef10_pci_sriov_enable(efx, num_vfs);
}

int efx_ef10_sriov_init(struct efx_nic *efx)
{
	return 0;
}

void efx_ef10_sriov_fini(struct efx_nic *efx)
{
	int rc;

	rc = efx_ef10_pci_sriov_disable(efx);
	if (rc)
		netif_dbg(efx, drv, efx->net_dev,
			  "Disabling SRIOV was not successful rc=%d\n", rc);
	else
		netif_dbg(efx, drv, efx->net_dev, "SRIOV disabled\n");
}

static int efx_ef10_vswitch_alloc(struct efx_nic *efx, unsigned int port_id,
				  unsigned int vswitch_type)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_VSWITCH_ALLOC_IN_LEN);

	MCDI_SET_DWORD(inbuf, VSWITCH_ALLOC_IN_UPSTREAM_PORT_ID, port_id);
	MCDI_SET_DWORD(inbuf, VSWITCH_ALLOC_IN_TYPE, vswitch_type);
	MCDI_SET_DWORD(inbuf, VSWITCH_ALLOC_IN_NUM_VLAN_TAGS, 0);
	MCDI_POPULATE_DWORD_1(inbuf, VSWITCH_ALLOC_IN_FLAGS,
			      VSWITCH_ALLOC_IN_FLAG_AUTO_PORT, 0);

	return efx_mcdi_rpc(efx, MC_CMD_VSWITCH_ALLOC, inbuf, sizeof(inbuf),
			    NULL, 0, NULL);
}

static int efx_ef10_vswitch_free(struct efx_nic *efx, unsigned int port_id)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_VSWITCH_FREE_IN_LEN);

	MCDI_SET_DWORD(inbuf, VSWITCH_FREE_IN_UPSTREAM_PORT_ID, port_id);

	return efx_mcdi_rpc(efx, MC_CMD_VSWITCH_FREE, inbuf, sizeof(inbuf),
			    NULL, 0, NULL);
}

static int efx_ef10_vport_alloc(struct efx_nic *efx,
				unsigned int port_id_in,
				unsigned int vport_type,
				unsigned int *port_id_out)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_VPORT_ALLOC_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_VPORT_ALLOC_OUT_LEN);
	size_t outlen;
	int rc;

	EFX_WARN_ON_PARANOID(!port_id_out);

	MCDI_SET_DWORD(inbuf, VPORT_ALLOC_IN_UPSTREAM_PORT_ID, port_id_in);
	MCDI_SET_DWORD(inbuf, VPORT_ALLOC_IN_TYPE, vport_type);
	MCDI_SET_DWORD(inbuf, VPORT_ALLOC_IN_NUM_VLAN_TAGS, 0);
	MCDI_POPULATE_DWORD_1(inbuf, VPORT_ALLOC_IN_FLAGS,
			      VPORT_ALLOC_IN_FLAG_AUTO_PORT, 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_VPORT_ALLOC, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < MC_CMD_VPORT_ALLOC_OUT_LEN)
		return -EIO;

	*port_id_out = MCDI_DWORD(outbuf, VPORT_ALLOC_OUT_VPORT_ID);
	return 0;
}

static int efx_ef10_vport_free(struct efx_nic *efx, unsigned int port_id)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_VPORT_FREE_IN_LEN);

	MCDI_SET_DWORD(inbuf, VPORT_FREE_IN_VPORT_ID, port_id);

	return efx_mcdi_rpc(efx, MC_CMD_VPORT_FREE, inbuf, sizeof(inbuf),
			    NULL, 0, NULL);
}

/* On top of the default firmware vswitch setup, create a VEB vswitch and
 * expansion vport for use by this function.
 */
int efx_ef10_vswitching_probe(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	int rc;

	if (pci_sriov_get_totalvfs(efx->pci_dev) <= 0)
		return 0; /* vswitch not needed as we have no VFs */

	rc = efx_ef10_vswitch_alloc(efx, EVB_PORT_ID_ASSIGNED,
				    MC_CMD_VSWITCH_ALLOC_IN_VSWITCH_TYPE_VEB);
	if (rc)
		goto fail1;

	rc = efx_ef10_vport_alloc(efx, EVB_PORT_ID_ASSIGNED,
				  MC_CMD_VPORT_ALLOC_IN_VPORT_TYPE_NORMAL,
				  &nic_data->vport_id);
	if (rc)
		goto fail2;

	return 0;
fail2:
	efx_ef10_vswitch_free(efx, EVB_PORT_ID_ASSIGNED);
fail1:
	return rc;
}

int efx_ef10_vswitching_restore(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	int rc;

	if (!nic_data->must_probe_vswitching)
		return 0;

	rc = efx_ef10_vswitching_probe(efx);

	if (!rc)
		nic_data->must_probe_vswitching = false;
	return rc;
}

void efx_ef10_vswitching_remove(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;

	if (nic_data->vport_id == EVB_PORT_ID_ASSIGNED)
		return; /* No vswitch was ever created */

	efx_ef10_vport_free(efx, nic_data->vport_id);
	nic_data->vport_id = EVB_PORT_ID_ASSIGNED;

	efx_ef10_vswitch_free(efx, nic_data->vport_id);
}
