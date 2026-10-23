/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2017-2019 NXP */

#include "enetc.h"
#include <linux/phylink.h>

#define ENETC_PF_NUM_RINGS	8
#define ENETC_VLAN_HT_SIZE	64
#define ENETC_VF_MC_HASH_BITS_MAX	8 /* For untrusted VFs */

enum enetc_vf_flags {
	ENETC_VF_FLAG_PF_SET_MAC	= BIT(0),
	ENETC_VF_FLAG_TRUSTED		= BIT(1),
	ENETC_VF_FLAG_UC_PROMISC	= BIT(2),
	ENETC_VF_FLAG_MC_PROMISC	= BIT(3),
	ENETC_VF_FLAG_SPOOFCHK		= BIT(4),
};

struct enetc_vf_state {
	struct mutex lock; /* Prevent concurrent access */
	enum enetc_vf_flags flags;
	/* Number of consecutive failures to send PF-to-VF messages */
	int msg_fail_cnt;
	u8 tpid; /* SI-based VLAN TPID (0: 0x8100, 1: 0x88a8) */
	u8 qos; /* SI-based VLAN QOS (priority) bits */
	u16 vid; /* SI-based VLAN ID */
};

struct enetc_port_caps {
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
	void (*vf_flr_handler)(struct enetc_pf *pf, int vf_id);
};

struct enetc_pf {
	struct enetc_si *si;
	int num_vfs; /* number of active VFs, after sriov_init */
	int total_vfs; /* max number of VFs, set for PF at probe */
	struct enetc_vf_state *vf_state;
	struct enetc_msg_swbd *rxmsg;

	DECLARE_BITMAP(vlan_ht_filter, ENETC_VLAN_HT_SIZE);
	DECLARE_BITMAP(active_vlans, VLAN_N_VID);

	struct mii_bus *mdio; /* saved for cleanup */
	struct mii_bus *imdio;
	struct phylink_pcs *pcs;

	phy_interface_t if_mode;
	struct phylink_config phylink_config;

	struct enetc_port_caps caps;
	const struct enetc_pf_ops *ops;

	struct work_struct link_status_task;
	bool sriov_enabled;
	bool link_up;
	u16 link_status_ms_mask;
};

#define phylink_to_enetc_pf(config) \
	container_of((config), struct enetc_pf, phylink_config)
