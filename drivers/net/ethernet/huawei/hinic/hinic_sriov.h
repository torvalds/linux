/* SPDX-License-Identifier: GPL-2.0-only */
/* Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_SRIOV_H
#define HINIC_SRIOV_H

#include "hinic_hw_dev.h"

#define OS_VF_ID_TO_HW(os_vf_id) ((os_vf_id) + 1)
#define HW_VF_ID_TO_OS(hw_vf_id) ((hw_vf_id) - 1)

enum hinic_sriov_state {
	HINIC_SRIOV_DISABLE,
	HINIC_SRIOV_ENABLE,
	HINIC_FUNC_REMOVE,
};

enum {
	HINIC_IFLA_VF_LINK_STATE_AUTO,	/* link state of the uplink */
	HINIC_IFLA_VF_LINK_STATE_ENABLE,	/* link always up */
	HINIC_IFLA_VF_LINK_STATE_DISABLE,	/* link always down */
};

struct hinic_sriov_info {
	struct pci_dev *pdev;
	struct hinic_hwdev *hwdev;
	bool sriov_enabled;
	unsigned int num_vfs;
	unsigned long state;
};

struct vf_data_storage {
	u8 vf_mac_addr[ETH_ALEN];
	bool registered;
	bool pf_set_mac;
	u16 pf_vlan;
	u8 pf_qos;
	u32 max_rate;
	u32 min_rate;

	bool link_forced;
	bool link_up;		/* only valid if VF link is forced */
	bool spoofchk;
	bool trust;
};

struct hinic_register_vf {
	u8	status;
	u8	version;
	u8	rsvd0[6];
};

struct hinic_port_mac_update {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	vlan_id;
	u16	rsvd1;
	u8	old_mac[ETH_ALEN];
	u16	rsvd2;
	u8	new_mac[ETH_ALEN];
};

struct hinic_vf_vlan_config {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u16 func_id;
	u16 vlan_id;
	u8  qos;
	u8  rsvd1[7];
};

int hinic_ndo_set_vf_mac(struct net_device *netdev, int vf, u8 *mac);

int hinic_ndo_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan, u8 qos,
			  __be16 vlan_proto);

int hinic_ndo_get_vf_config(struct net_device *netdev,
			    int vf, struct ifla_vf_info *ivi);

int hinic_ndo_set_vf_trust(struct net_device *netdev, int vf, bool setting);

int hinic_ndo_set_vf_bw(struct net_device *netdev,
			int vf, int min_tx_rate, int max_tx_rate);

int hinic_ndo_set_vf_spoofchk(struct net_device *netdev, int vf, bool setting);

int hinic_ndo_set_vf_link_state(struct net_device *netdev, int vf_id, int link);

void hinic_notify_all_vfs_link_changed(struct hinic_hwdev *hwdev,
				       u8 link_status);

int hinic_pci_sriov_disable(struct pci_dev *dev);

int hinic_vf_func_init(struct hinic_hwdev *hwdev);

void hinic_vf_func_free(struct hinic_hwdev *hwdev);

int hinic_pci_sriov_configure(struct pci_dev *dev, int num_vfs);

#endif
