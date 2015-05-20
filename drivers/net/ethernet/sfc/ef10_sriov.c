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

static int efx_ef10_evb_port_assign(struct efx_nic *efx, unsigned int port_id,
				    unsigned int vf_fn)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_EVB_PORT_ASSIGN_IN_LEN);
	struct efx_ef10_nic_data *nic_data = efx->nic_data;

	MCDI_SET_DWORD(inbuf, EVB_PORT_ASSIGN_IN_PORT_ID, port_id);
	MCDI_POPULATE_DWORD_2(inbuf, EVB_PORT_ASSIGN_IN_FUNCTION,
			      EVB_PORT_ASSIGN_IN_PF, nic_data->pf_index,
			      EVB_PORT_ASSIGN_IN_VF, vf_fn);

	return efx_mcdi_rpc(efx, MC_CMD_EVB_PORT_ASSIGN, inbuf, sizeof(inbuf),
			    NULL, 0, NULL);
}

static int efx_ef10_vport_add_mac(struct efx_nic *efx,
				  unsigned int port_id, u8 *mac)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_VPORT_ADD_MAC_ADDRESS_IN_LEN);

	MCDI_SET_DWORD(inbuf, VPORT_ADD_MAC_ADDRESS_IN_VPORT_ID, port_id);
	ether_addr_copy(MCDI_PTR(inbuf, VPORT_ADD_MAC_ADDRESS_IN_MACADDR), mac);

	return efx_mcdi_rpc(efx, MC_CMD_VPORT_ADD_MAC_ADDRESS, inbuf,
			    sizeof(inbuf), NULL, 0, NULL);
}

static int efx_ef10_vport_del_mac(struct efx_nic *efx,
				  unsigned int port_id, u8 *mac)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_VPORT_DEL_MAC_ADDRESS_IN_LEN);

	MCDI_SET_DWORD(inbuf, VPORT_DEL_MAC_ADDRESS_IN_VPORT_ID, port_id);
	ether_addr_copy(MCDI_PTR(inbuf, VPORT_DEL_MAC_ADDRESS_IN_MACADDR), mac);

	return efx_mcdi_rpc(efx, MC_CMD_VPORT_DEL_MAC_ADDRESS, inbuf,
			    sizeof(inbuf), NULL, 0, NULL);
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

static int efx_ef10_vadaptor_alloc(struct efx_nic *efx, unsigned int port_id)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_VADAPTOR_ALLOC_IN_LEN);

	MCDI_SET_DWORD(inbuf, VADAPTOR_ALLOC_IN_UPSTREAM_PORT_ID, port_id);
	return efx_mcdi_rpc(efx, MC_CMD_VADAPTOR_ALLOC, inbuf, sizeof(inbuf),
			    NULL, 0, NULL);
}

static int efx_ef10_vadaptor_free(struct efx_nic *efx, unsigned int port_id)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_VADAPTOR_FREE_IN_LEN);

	MCDI_SET_DWORD(inbuf, VADAPTOR_FREE_IN_UPSTREAM_PORT_ID, port_id);
	return efx_mcdi_rpc(efx, MC_CMD_VADAPTOR_FREE, inbuf, sizeof(inbuf),
			    NULL, 0, NULL);
}

static void efx_ef10_sriov_free_vf_vports(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	int i;

	if (!nic_data->vf)
		return;

	for (i = 0; i < efx->vf_count; i++) {
		struct ef10_vf *vf = nic_data->vf + i;

		if (vf->vport_assigned) {
			efx_ef10_evb_port_assign(efx, EVB_PORT_ID_NULL, i);
			vf->vport_assigned = 0;
		}

		if (!is_zero_ether_addr(vf->mac)) {
			efx_ef10_vport_del_mac(efx, vf->vport_id, vf->mac);
			eth_zero_addr(vf->mac);
		}

		if (vf->vport_id) {
			efx_ef10_vport_free(efx, vf->vport_id);
			vf->vport_id = 0;
		}

		vf->efx = NULL;
	}
}

static void efx_ef10_sriov_free_vf_vswitching(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;

	efx_ef10_sriov_free_vf_vports(efx);
	kfree(nic_data->vf);
	nic_data->vf = NULL;
}

static int efx_ef10_sriov_assign_vf_vport(struct efx_nic *efx,
					  unsigned int vf_i)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	struct ef10_vf *vf = nic_data->vf + vf_i;
	int rc;

	if (WARN_ON_ONCE(!nic_data->vf))
		return -EOPNOTSUPP;

	rc = efx_ef10_vport_alloc(efx, EVB_PORT_ID_ASSIGNED,
				  MC_CMD_VPORT_ALLOC_IN_VPORT_TYPE_NORMAL,
				  &vf->vport_id);
	if (rc)
		return rc;

	rc = efx_ef10_vport_add_mac(efx, vf->vport_id, vf->mac);
	if (rc) {
		eth_zero_addr(vf->mac);
		return rc;
	}

	rc =  efx_ef10_evb_port_assign(efx, vf->vport_id, vf_i);
	if (rc)
		return rc;

	vf->vport_assigned = 1;
	return 0;
}

static int efx_ef10_sriov_alloc_vf_vswitching(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	unsigned int i;
	int rc;

	nic_data->vf = kcalloc(efx->vf_count, sizeof(struct ef10_vf),
			       GFP_KERNEL);
	if (!nic_data->vf)
		return -ENOMEM;

	for (i = 0; i < efx->vf_count; i++) {
		random_ether_addr(nic_data->vf[i].mac);
		nic_data->vf[i].efx = NULL;

		rc = efx_ef10_sriov_assign_vf_vport(efx, i);
		if (rc)
			goto fail;
	}

	return 0;
fail:
	efx_ef10_sriov_free_vf_vports(efx);
	kfree(nic_data->vf);
	nic_data->vf = NULL;
	return rc;
}

static int efx_ef10_sriov_restore_vf_vswitching(struct efx_nic *efx)
{
	unsigned int i;
	int rc;

	for (i = 0; i < efx->vf_count; i++) {
		rc = efx_ef10_sriov_assign_vf_vport(efx, i);
		if (rc)
			goto fail;
	}

	return 0;
fail:
	efx_ef10_sriov_free_vf_vswitching(efx);
	return rc;
}

/* On top of the default firmware vswitch setup, create a VEB vswitch and
 * expansion vport for use by this function.
 */
int efx_ef10_vswitching_probe_pf(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	struct net_device *net_dev = efx->net_dev;
	int rc;

	if (pci_sriov_get_totalvfs(efx->pci_dev) <= 0) {
		/* vswitch not needed as we have no VFs */
		efx_ef10_vadaptor_alloc(efx, nic_data->vport_id);
		return 0;
	}

	rc = efx_ef10_vswitch_alloc(efx, EVB_PORT_ID_ASSIGNED,
				    MC_CMD_VSWITCH_ALLOC_IN_VSWITCH_TYPE_VEB);
	if (rc)
		goto fail1;

	rc = efx_ef10_vport_alloc(efx, EVB_PORT_ID_ASSIGNED,
				  MC_CMD_VPORT_ALLOC_IN_VPORT_TYPE_NORMAL,
				  &nic_data->vport_id);
	if (rc)
		goto fail2;

	rc = efx_ef10_vport_add_mac(efx, nic_data->vport_id, net_dev->dev_addr);
	if (rc)
		goto fail3;
	ether_addr_copy(nic_data->vport_mac, net_dev->dev_addr);

	rc = efx_ef10_vadaptor_alloc(efx, nic_data->vport_id);
	if (rc)
		goto fail4;

	return 0;
fail4:
	efx_ef10_vport_del_mac(efx, nic_data->vport_id, nic_data->vport_mac);
	eth_zero_addr(nic_data->vport_mac);
fail3:
	efx_ef10_vport_free(efx, nic_data->vport_id);
	nic_data->vport_id = EVB_PORT_ID_ASSIGNED;
fail2:
	efx_ef10_vswitch_free(efx, EVB_PORT_ID_ASSIGNED);
fail1:
	return rc;
}

int efx_ef10_vswitching_probe_vf(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;

	return efx_ef10_vadaptor_alloc(efx, nic_data->vport_id);
}

int efx_ef10_vswitching_restore_pf(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	int rc;

	if (!nic_data->must_probe_vswitching)
		return 0;

	rc = efx_ef10_vswitching_probe_pf(efx);
	if (rc)
		goto fail;

	rc = efx_ef10_sriov_restore_vf_vswitching(efx);
	if (rc)
		goto fail;

	nic_data->must_probe_vswitching = false;
fail:
	return rc;
}

int efx_ef10_vswitching_restore_vf(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	int rc;

	if (!nic_data->must_probe_vswitching)
		return 0;

	rc = efx_ef10_vadaptor_free(efx, EVB_PORT_ID_ASSIGNED);
	if (rc)
		return rc;

	nic_data->must_probe_vswitching = false;
	return 0;
}

void efx_ef10_vswitching_remove_pf(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;

	efx_ef10_sriov_free_vf_vswitching(efx);

	efx_ef10_vadaptor_free(efx, nic_data->vport_id);

	if (nic_data->vport_id == EVB_PORT_ID_ASSIGNED)
		return; /* No vswitch was ever created */

	if (!is_zero_ether_addr(nic_data->vport_mac)) {
		efx_ef10_vport_del_mac(efx, nic_data->vport_id,
				       efx->net_dev->dev_addr);
		eth_zero_addr(nic_data->vport_mac);
	}
	efx_ef10_vport_free(efx, nic_data->vport_id);
	nic_data->vport_id = EVB_PORT_ID_ASSIGNED;

	efx_ef10_vswitch_free(efx, nic_data->vport_id);
}

void efx_ef10_vswitching_remove_vf(struct efx_nic *efx)
{
	efx_ef10_vadaptor_free(efx, EVB_PORT_ID_ASSIGNED);
}

static int efx_ef10_pci_sriov_enable(struct efx_nic *efx, int num_vfs)
{
	int rc = 0;
	struct pci_dev *dev = efx->pci_dev;

	efx->vf_count = num_vfs;

	rc = efx_ef10_sriov_alloc_vf_vswitching(efx);
	if (rc)
		goto fail1;

	rc = pci_enable_sriov(dev, num_vfs);
	if (rc)
		goto fail2;

	return 0;
fail2:
	efx_ef10_sriov_free_vf_vswitching(efx);
fail1:
	efx->vf_count = 0;
	netif_err(efx, probe, efx->net_dev,
		  "Failed to enable SRIOV VFs\n");
	return rc;
}

static int efx_ef10_pci_sriov_disable(struct efx_nic *efx)
{
	struct pci_dev *dev = efx->pci_dev;

	pci_disable_sriov(dev);
	efx_ef10_sriov_free_vf_vswitching(efx);
	efx->vf_count = 0;
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
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	int rc;

	if (!nic_data->vf)
		return;

	rc = efx_ef10_pci_sriov_disable(efx);
	if (rc)
		netif_dbg(efx, drv, efx->net_dev,
			  "Disabling SRIOV was not successful rc=%d\n", rc);
	else
		netif_dbg(efx, drv, efx->net_dev, "SRIOV disabled\n");
}

static int efx_ef10_vport_del_vf_mac(struct efx_nic *efx, unsigned int port_id,
				     u8 *mac)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_VPORT_DEL_MAC_ADDRESS_IN_LEN);
	MCDI_DECLARE_BUF_ERR(outbuf);
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, VPORT_DEL_MAC_ADDRESS_IN_VPORT_ID, port_id);
	ether_addr_copy(MCDI_PTR(inbuf, VPORT_DEL_MAC_ADDRESS_IN_MACADDR), mac);

	rc = efx_mcdi_rpc(efx, MC_CMD_VPORT_DEL_MAC_ADDRESS, inbuf,
			  sizeof(inbuf), outbuf, sizeof(outbuf), &outlen);

	return rc;
}

int efx_ef10_sriov_set_vf_mac(struct efx_nic *efx, int vf_i, u8 *mac)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	struct ef10_vf *vf;
	int rc;

	if (!nic_data->vf)
		return -EOPNOTSUPP;

	if (vf_i >= efx->vf_count)
		return -EINVAL;
	vf = nic_data->vf + vf_i;

	if (vf->efx) {
		efx_device_detach_sync(vf->efx);
		efx_net_stop(vf->efx->net_dev);

		down_write(&vf->efx->filter_sem);
		vf->efx->type->filter_table_remove(vf->efx);

		rc = efx_ef10_vadaptor_free(vf->efx, EVB_PORT_ID_ASSIGNED);
		if (rc) {
			up_write(&vf->efx->filter_sem);
			return rc;
		}
	}

	rc = efx_ef10_evb_port_assign(efx, EVB_PORT_ID_NULL, vf_i);
	if (rc)
		return rc;

	if (!is_zero_ether_addr(vf->mac)) {
		rc = efx_ef10_vport_del_vf_mac(efx, vf->vport_id, vf->mac);
		if (rc)
			return rc;
	}

	if (!is_zero_ether_addr(mac)) {
		rc = efx_ef10_vport_add_mac(efx, vf->vport_id, mac);
		if (rc) {
			eth_zero_addr(vf->mac);
			goto fail;
		}
		if (vf->efx)
			ether_addr_copy(vf->efx->net_dev->dev_addr, mac);
	}

	ether_addr_copy(vf->mac, mac);

	rc = efx_ef10_evb_port_assign(efx, vf->vport_id, vf_i);
	if (rc)
		goto fail;

	if (vf->efx) {
		/* VF cannot use the vport_id that the PF created */
		rc = efx_ef10_vadaptor_alloc(vf->efx, EVB_PORT_ID_ASSIGNED);
		if (rc) {
			up_write(&vf->efx->filter_sem);
			return rc;
		}
		vf->efx->type->filter_table_probe(vf->efx);
		up_write(&vf->efx->filter_sem);
		efx_net_open(vf->efx->net_dev);
		netif_device_attach(vf->efx->net_dev);
	}

	return 0;

fail:
	memset(vf->mac, 0, ETH_ALEN);
	return rc;
}

int efx_ef10_sriov_get_vf_config(struct efx_nic *efx, int vf_i,
				 struct ifla_vf_info *ivf)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	struct ef10_vf *vf;

	if (vf_i >= efx->vf_count)
		return -EINVAL;

	if (!nic_data->vf)
		return -EOPNOTSUPP;

	vf = nic_data->vf + vf_i;

	ivf->vf = vf_i;
	ivf->min_tx_rate = 0;
	ivf->max_tx_rate = 0;
	ether_addr_copy(ivf->mac, vf->mac);
	ivf->vlan = 0;
	ivf->qos = 0;

	return 0;
}
