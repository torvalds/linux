/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2017-2019 NXP */

#include "enetc.h"

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

struct enetc_pf {
	struct enetc_si *si;
	int num_vfs; /* number of active VFs, after sriov_init */
	int total_vfs; /* max number of VFs, set for PF at probe */

	struct enetc_mac_filter mac_filter[ENETC_MAX_NUM_MAC_FLT];

	char vlan_promisc_simap; /* bitmap of SIs in VLAN promisc mode */
	DECLARE_BITMAP(vlan_ht_filter, ENETC_VLAN_HT_SIZE);
	DECLARE_BITMAP(active_vlans, VLAN_N_VID);
};
