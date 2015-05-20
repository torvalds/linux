/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2015 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EF10_SRIOV_H
#define EF10_SRIOV_H

#include "net_driver.h"

/**
 * struct ef10_vf - PF's store of VF data
 * @vport_id: vport ID for the VF
 * @vport_assigned: record whether the vport is currently assigned to the VF
 * @mac: MAC address for the VF, zero when address is removed from the vport
 */
struct ef10_vf {
	unsigned int vport_id;
	unsigned int vport_assigned;
	u8 mac[ETH_ALEN];
};

static inline bool efx_ef10_sriov_wanted(struct efx_nic *efx)
{
	return false;
}

int efx_ef10_sriov_configure(struct efx_nic *efx, int num_vfs);
int efx_ef10_sriov_init(struct efx_nic *efx);
static inline int efx_ef10_sriov_mac_address_changed(struct efx_nic *efx)
{
	return -EOPNOTSUPP;
}
static inline void efx_ef10_sriov_reset(struct efx_nic *efx) {}
void efx_ef10_sriov_fini(struct efx_nic *efx);
static inline void efx_ef10_sriov_flr(struct efx_nic *efx, unsigned vf_i) {}

static inline int efx_ef10_sriov_set_vf_mac(struct efx_nic *efx, int vf,
					    u8 *mac)
{
	return -EOPNOTSUPP;
}

static inline int efx_ef10_sriov_set_vf_vlan(struct efx_nic *efx, int vf,
					     u16 vlan, u8 qos)
{
	return -EOPNOTSUPP;
}

static inline int efx_ef10_sriov_set_vf_spoofchk(struct efx_nic *efx, int vf,
						 bool spoofchk)
{
	return -EOPNOTSUPP;
}

static inline int efx_ef10_sriov_get_vf_config(struct efx_nic *efx, int vf,
					       struct ifla_vf_info *ivf)
{
	return -EOPNOTSUPP;
}

int efx_ef10_vswitching_probe_pf(struct efx_nic *efx);
int efx_ef10_vswitching_probe_vf(struct efx_nic *efx);
int efx_ef10_vswitching_restore_pf(struct efx_nic *efx);
int efx_ef10_vswitching_restore_vf(struct efx_nic *efx);
void efx_ef10_vswitching_remove_pf(struct efx_nic *efx);
void efx_ef10_vswitching_remove_vf(struct efx_nic *efx);

#endif /* EF10_SRIOV_H */
