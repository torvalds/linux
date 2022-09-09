/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2015 Solarflare Communications Inc.
 */

#ifndef EF10_SRIOV_H
#define EF10_SRIOV_H

#include "net_driver.h"

/**
 * struct ef10_vf - PF's store of VF data
 * @efx: efx_nic struct for the current VF
 * @pci_dev: the pci_dev struct for the VF, retained while the VF is assigned
 * @vport_id: vport ID for the VF
 * @vport_assigned: record whether the vport is currently assigned to the VF
 * @mac: MAC address for the VF, zero when address is removed from the vport
 * @vlan: Default VLAN for the VF or #EFX_EF10_NO_VLAN
 */
struct ef10_vf {
	struct efx_nic *efx;
	struct pci_dev *pci_dev;
	unsigned int vport_id;
	unsigned int vport_assigned;
	u8 mac[ETH_ALEN];
	u16 vlan;
#define EFX_EF10_NO_VLAN       0
};

static inline bool efx_ef10_sriov_wanted(struct efx_nic *efx)
{
	return false;
}

int efx_ef10_sriov_configure(struct efx_nic *efx, int num_vfs);
int efx_ef10_sriov_init(struct efx_nic *efx);
static inline void efx_ef10_sriov_reset(struct efx_nic *efx) {}
void efx_ef10_sriov_fini(struct efx_nic *efx);
static inline void efx_ef10_sriov_flr(struct efx_nic *efx, unsigned vf_i) {}

int efx_ef10_sriov_set_vf_mac(struct efx_nic *efx, int vf, const u8 *mac);

int efx_ef10_sriov_set_vf_vlan(struct efx_nic *efx, int vf_i,
			       u16 vlan, u8 qos);

int efx_ef10_sriov_set_vf_spoofchk(struct efx_nic *efx, int vf,
				   bool spoofchk);

int efx_ef10_sriov_get_vf_config(struct efx_nic *efx, int vf_i,
				 struct ifla_vf_info *ivf);

int efx_ef10_sriov_set_vf_link_state(struct efx_nic *efx, int vf_i,
				     int link_state);

int efx_ef10_vswitching_probe_pf(struct efx_nic *efx);
int efx_ef10_vswitching_probe_vf(struct efx_nic *efx);
int efx_ef10_vswitching_restore_pf(struct efx_nic *efx);
int efx_ef10_vswitching_restore_vf(struct efx_nic *efx);
void efx_ef10_vswitching_remove_pf(struct efx_nic *efx);
void efx_ef10_vswitching_remove_vf(struct efx_nic *efx);
int efx_ef10_vport_add_mac(struct efx_nic *efx,
			   unsigned int port_id, const u8 *mac);
int efx_ef10_vport_del_mac(struct efx_nic *efx,
			   unsigned int port_id, const u8 *mac);
int efx_ef10_vadaptor_alloc(struct efx_nic *efx, unsigned int port_id);
int efx_ef10_vadaptor_query(struct efx_nic *efx, unsigned int port_id,
			    u32 *port_flags, u32 *vadaptor_flags,
			    unsigned int *vlan_tags);
int efx_ef10_vadaptor_free(struct efx_nic *efx, unsigned int port_id);

#endif /* EF10_SRIOV_H */
