/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

#ifndef _ICE_LAG_H_
#define _ICE_LAG_H_

#include <linux/netdevice.h>

/* LAG roles for netdev */
enum ice_lag_role {
	ICE_LAG_NONE,
	ICE_LAG_PRIMARY,
	ICE_LAG_BACKUP,
	ICE_LAG_UNSET
};

#define ICE_LAG_INVALID_PORT		0xFF
#define ICE_LAGP_IDX			0
#define ICE_LAGS_IDX			1
#define ICE_LAGP_M			0x1
#define ICE_LAGS_M			0x2

#define ICE_LAG_RESET_RETRIES		5
#define ICE_SW_DEFAULT_PROFILE		0
#define ICE_FV_PROT_MDID		255
#define ICE_LP_EXT_BUF_OFFSET		32

struct ice_pf;
struct ice_vf;

struct ice_lag_netdev_list {
	struct list_head node;
	struct net_device *netdev;
};

/* LAG info struct */
struct ice_lag {
	struct ice_pf *pf; /* backlink to PF struct */
	struct net_device *netdev; /* this PF's netdev */
	struct net_device *upper_netdev; /* upper bonding netdev */
	struct list_head *netdev_head;
	struct notifier_block notif_block;
	s32 bond_mode;
	u16 bond_swid; /* swid for primary interface */
	u8 active_port; /* lport value for the current active port */
	u8 bonded:1; /* currently bonded */
	u8 primary:1; /* this is primary */
	u8 bond_aa:1; /* is this bond active-active */
	u8 need_fltr_cfg:1; /* fltrs for A/A bond still need to be make */
	u8 port_bitmap:2; /* bitmap of active ports */
	u8 bond_lport_pri; /* lport values for primary PF */
	u8 bond_lport_sec; /* lport values for secondary PF */

	/* q_home keeps track of which interface the q is currently on */
	u8 q_home[ICE_MAX_SRIOV_VFS][ICE_MAX_RSS_QS_PER_VF];

	/* placeholder VSI for hanging VF queues from on secondary interface */
	struct ice_vsi *sec_vf[ICE_MAX_SRIOV_VFS];

	u16 pf_recipe;
	u16 lport_recipe;
	u16 act_act_recipe;
	u16 pf_rx_rule_id;
	u16 pf_tx_rule_id;
	u16 cp_rule_idx;
	u16 lport_rule_idx;
	u16 act_act_rule_idx;
	u8 role;
};

/* LAG workqueue struct */
struct ice_lag_work {
	struct work_struct lag_task;
	struct ice_lag_netdev_list netdev_list;
	struct ice_lag *lag;
	unsigned long event;
	struct net_device *event_netdev;
	union {
		struct netdev_notifier_changeupper_info changeupper_info;
		struct netdev_notifier_bonding_info bonding_info;
		struct netdev_notifier_info notifier_info;
	} info;
};

void ice_lag_aa_failover(struct ice_lag *lag, u8 dest, struct ice_pf *e_pf);
int ice_init_lag(struct ice_pf *pf);
void ice_deinit_lag(struct ice_pf *pf);
void ice_lag_rebuild(struct ice_pf *pf);
bool ice_lag_is_switchdev_running(struct ice_pf *pf);
void ice_lag_move_vf_nodes_cfg(struct ice_lag *lag, u8 src_prt, u8 dst_prt);
u8 ice_lag_prepare_vf_reset(struct ice_lag *lag);
void ice_lag_complete_vf_reset(struct ice_lag *lag, u8 act_prt);
#endif /* _ICE_LAG_H_ */
