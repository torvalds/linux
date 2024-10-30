/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2017-2019 NXP */

#include "enetc.h"
#include <linux/phylink.h>

#define ENETC_PF_NUM_RINGS	8

enum enetc_mac_addr_type {UC, MC, MADDR_TYPE};
#define ENETC_MAX_NUM_MAC_FLT	((ENETC_MAX_NUM_VFS + 1) * MADDR_TYPE)

#define ENETC_MADDR_HASH_TBL_SZ	64
struct enetc_mac_filter {
	union {
		char mac_addr[ETH_ALEN];
		DECLARE_BITMAP(mac_hash_table, ENETC_MADDR_HASH_TBL_SZ);
	};
	int mac_addr_cnt;
};

#define ENETC_VLAN_HT_SIZE	64

enum enetc_vf_flags {
	ENETC_VF_FLAG_PF_SET_MAC	= BIT(0),
};

struct enetc_vf_state {
	enum enetc_vf_flags flags;
};

struct enetc_port_caps {
	u32 half_duplex:1;
	int num_vsi;
	int num_msix;
	int num_rx_bdr;
	int num_tx_bdr;
};

struct enetc_pf;

struct enetc_pf_ops {
	void (*set_si_primary_mac)(struct enetc_hw *hw, int si, const u8 *addr);
	void (*get_si_primary_mac)(struct enetc_hw *hw, int si, u8 *addr);
	struct phylink_pcs *(*create_pcs)(struct enetc_pf *pf, struct mii_bus *bus);
	void (*destroy_pcs)(struct phylink_pcs *pcs);
	int (*enable_psfp)(struct enetc_ndev_priv *priv);
};

struct enetc_pf {
	struct enetc_si *si;
	int num_vfs; /* number of active VFs, after sriov_init */
	int total_vfs; /* max number of VFs, set for PF at probe */
	struct enetc_vf_state *vf_state;

	struct enetc_mac_filter mac_filter[ENETC_MAX_NUM_MAC_FLT];

	struct enetc_msg_swbd rxmsg[ENETC_MAX_NUM_VFS];
	struct work_struct msg_task;
	char msg_int_name[ENETC_INT_NAME_MAX];

	char vlan_promisc_simap; /* bitmap of SIs in VLAN promisc mode */
	DECLARE_BITMAP(vlan_ht_filter, ENETC_VLAN_HT_SIZE);
	DECLARE_BITMAP(active_vlans, VLAN_N_VID);

	struct mii_bus *mdio; /* saved for cleanup */
	struct mii_bus *imdio;
	struct phylink_pcs *pcs;

	phy_interface_t if_mode;
	struct phylink_config phylink_config;

	struct enetc_port_caps caps;
	const struct enetc_pf_ops *ops;
};

#define phylink_to_enetc_pf(config) \
	container_of((config), struct enetc_pf, phylink_config)

int enetc_msg_psi_init(struct enetc_pf *pf);
void enetc_msg_psi_free(struct enetc_pf *pf);
void enetc_msg_handle_rxmsg(struct enetc_pf *pf, int mbox_id, u16 *status);
