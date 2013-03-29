/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#ifndef _QLCNIC_83XX_SRIOV_H_
#define _QLCNIC_83XX_SRIOV_H_

#include "qlcnic.h"
#include <linux/types.h>
#include <linux/pci.h>

struct qlcnic_resources {
	u16 num_tx_mac_filters;
	u16 num_rx_ucast_mac_filters;
	u16 num_rx_mcast_mac_filters;

	u16 num_txvlan_keys;

	u16 num_rx_queues;
	u16 num_tx_queues;

	u16 num_rx_buf_rings;
	u16 num_rx_status_rings;

	u16 num_destip;
	u32 num_lro_flows_supported;
	u16 max_local_ipv6_addrs;
	u16 max_remote_ipv6_addrs;
};

struct qlcnic_sriov {
	u16				vp_handle;
	u8				num_vfs;
	struct qlcnic_resources		ff_max;
};

int qlcnic_sriov_init(struct qlcnic_adapter *, int);
void qlcnic_sriov_cleanup(struct qlcnic_adapter *);
void __qlcnic_sriov_cleanup(struct qlcnic_adapter *);

static inline bool qlcnic_sriov_enable_check(struct qlcnic_adapter *adapter)
{
	return test_bit(__QLCNIC_SRIOV_ENABLE, &adapter->state) ? true : false;
}

#ifdef CONFIG_QLCNIC_SRIOV
void qlcnic_sriov_pf_disable(struct qlcnic_adapter *);
void qlcnic_sriov_pf_cleanup(struct qlcnic_adapter *);
int qlcnic_pci_sriov_configure(struct pci_dev *, int);
#else
static inline void qlcnic_sriov_pf_disable(struct qlcnic_adapter *adapter) {}
static inline void qlcnic_sriov_pf_cleanup(struct qlcnic_adapter *adapter) {}
#endif

#endif
