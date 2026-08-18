/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2024 NXP */

#include "enetc_pf.h"

void enetc_set_si_hw_addr(struct enetc_pf *pf, int si, const u8 *mac_addr);
int enetc_pf_set_mac_addr(struct net_device *ndev, void *addr);
int enetc_setup_mac_addresses(struct device_node *np, struct enetc_pf *pf);
void enetc_pf_netdev_setup(struct enetc_si *si, struct net_device *ndev,
			   const struct net_device_ops *ndev_ops);
int enetc_mdiobus_create(struct enetc_pf *pf, struct device_node *node);
void enetc_mdiobus_destroy(struct enetc_pf *pf);
int enetc_phylink_create(struct enetc_ndev_priv *priv, struct device_node *node,
			 const struct phylink_mac_ops *ops);
void enetc_phylink_destroy(struct enetc_ndev_priv *priv);
void enetc_set_default_rss_key(struct enetc_pf *pf);
int enetc_vlan_rx_add_vid(struct net_device *ndev, __be16 prot, u16 vid);
int enetc_vlan_rx_del_vid(struct net_device *ndev, __be16 prot, u16 vid);
int enetc_init_sriov_resources(struct enetc_pf *pf);

static inline u16 enetc_get_ip_revision(struct enetc_hw *hw)
{
	return enetc_global_rd(hw, ENETC_G_EIPBRR0) & EIPBRR0_REVISION;
}

#if IS_ENABLED(CONFIG_PCI_IOV)
int enetc_sriov_configure(struct pci_dev *pdev, int num_vfs);
#else
static inline int enetc_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	return 0;
}
#endif
