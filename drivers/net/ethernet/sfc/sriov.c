// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2014-2015 Solarflare Communications Inc.
 */
#include <linux/module.h>
#include "net_driver.h"
#include "nic.h"
#include "sriov.h"

int efx_sriov_set_vf_mac(struct net_device *net_dev, int vf_i, u8 *mac)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	if (efx->type->sriov_set_vf_mac)
		return efx->type->sriov_set_vf_mac(efx, vf_i, mac);
	else
		return -EOPNOTSUPP;
}

int efx_sriov_set_vf_vlan(struct net_device *net_dev, int vf_i, u16 vlan,
			  u8 qos, __be16 vlan_proto)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	if (efx->type->sriov_set_vf_vlan) {
		if ((vlan & ~VLAN_VID_MASK) ||
		    (qos & ~(VLAN_PRIO_MASK >> VLAN_PRIO_SHIFT)))
			return -EINVAL;

		if (vlan_proto != htons(ETH_P_8021Q))
			return -EPROTONOSUPPORT;

		return efx->type->sriov_set_vf_vlan(efx, vf_i, vlan, qos);
	} else {
		return -EOPNOTSUPP;
	}
}

int efx_sriov_set_vf_spoofchk(struct net_device *net_dev, int vf_i,
			      bool spoofchk)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	if (efx->type->sriov_set_vf_spoofchk)
		return efx->type->sriov_set_vf_spoofchk(efx, vf_i, spoofchk);
	else
		return -EOPNOTSUPP;
}

int efx_sriov_get_vf_config(struct net_device *net_dev, int vf_i,
			    struct ifla_vf_info *ivi)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	if (efx->type->sriov_get_vf_config)
		return efx->type->sriov_get_vf_config(efx, vf_i, ivi);
	else
		return -EOPNOTSUPP;
}

int efx_sriov_set_vf_link_state(struct net_device *net_dev, int vf_i,
				int link_state)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	if (efx->type->sriov_set_vf_link_state)
		return efx->type->sriov_set_vf_link_state(efx, vf_i,
							  link_state);
	else
		return -EOPNOTSUPP;
}
