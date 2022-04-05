/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 1999 - 2018 Intel Corporation. */

#ifndef _IXGBE_SRIOV_H_
#define _IXGBE_SRIOV_H_

/*  ixgbe driver limit the max number of VFs could be enabled to
 *  63 (IXGBE_MAX_VF_FUNCTIONS - 1)
 */
#define IXGBE_MAX_VFS_DRV_LIMIT  (IXGBE_MAX_VF_FUNCTIONS - 1)
#define IXGBE_MAX_VFS_1TC		IXGBE_MAX_VF_FUNCTIONS
#define IXGBE_MAX_VFS_4TC		32
#define IXGBE_MAX_VFS_8TC		16

#ifdef CONFIG_PCI_IOV
void ixgbe_restore_vf_multicasts(struct ixgbe_adapter *adapter);
#endif
void ixgbe_msg_task(struct ixgbe_adapter *adapter);
int ixgbe_vf_configuration(struct pci_dev *pdev, unsigned int event_mask);
void ixgbe_ping_all_vfs(struct ixgbe_adapter *adapter);
void ixgbe_set_all_vfs(struct ixgbe_adapter *adapter);
int ixgbe_ndo_set_vf_mac(struct net_device *netdev, int queue, u8 *mac);
int ixgbe_ndo_set_vf_vlan(struct net_device *netdev, int queue, u16 vlan,
			   u8 qos, __be16 vlan_proto);
int ixgbe_link_mbps(struct ixgbe_adapter *adapter);
int ixgbe_ndo_set_vf_bw(struct net_device *netdev, int vf, int min_tx_rate,
			int max_tx_rate);
int ixgbe_ndo_set_vf_spoofchk(struct net_device *netdev, int vf, bool setting);
int ixgbe_ndo_set_vf_rss_query_en(struct net_device *netdev, int vf,
				  bool setting);
int ixgbe_ndo_set_vf_trust(struct net_device *netdev, int vf, bool setting);
int ixgbe_ndo_get_vf_config(struct net_device *netdev,
			    int vf, struct ifla_vf_info *ivi);
int ixgbe_ndo_set_vf_link_state(struct net_device *netdev, int vf, int state);
void ixgbe_check_vf_rate_limit(struct ixgbe_adapter *adapter);
void ixgbe_set_vf_link_state(struct ixgbe_adapter *adapter, int vf, int state);
int ixgbe_disable_sriov(struct ixgbe_adapter *adapter);
#ifdef CONFIG_PCI_IOV
void ixgbe_enable_sriov(struct ixgbe_adapter *adapter, unsigned int max_vfs);
#endif
int ixgbe_pci_sriov_configure(struct pci_dev *dev, int num_vfs);

static inline void ixgbe_set_vmvir(struct ixgbe_adapter *adapter,
				   u16 vid, u16 qos, u32 vf)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 vmvir = vid | (qos << VLAN_PRIO_SHIFT) | IXGBE_VMVIR_VLANA_DEFAULT;

	IXGBE_WRITE_REG(hw, IXGBE_VMVIR(vf), vmvir);
}

#endif /* _IXGBE_SRIOV_H_ */

