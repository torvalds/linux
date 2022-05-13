/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2020, Intel Corporation. */

#ifndef _ICE_ARFS_H_
#define _ICE_ARFS_H_

#include "ice_fdir.h"

enum ice_arfs_fltr_state {
	ICE_ARFS_INACTIVE,
	ICE_ARFS_ACTIVE,
	ICE_ARFS_TODEL,
};

struct ice_arfs_entry {
	struct ice_fdir_fltr fltr_info;
	struct hlist_node list_entry;
	u64 time_activated;	/* only valid for UDP flows */
	u32 flow_id;
	/* fltr_state = 0 - ICE_ARFS_INACTIVE:
	 *	filter needs to be updated or programmed in HW.
	 * fltr_state = 1 - ICE_ARFS_ACTIVE:
	 *	filter is active and programmed in HW.
	 * fltr_state = 2 - ICE_ARFS_TODEL:
	 *	filter has been deleted from HW and needs to be removed from
	 *	the aRFS hash table.
	 */
	u8 fltr_state;
};

struct ice_arfs_entry_ptr {
	struct ice_arfs_entry *arfs_entry;
	struct hlist_node list_entry;
};

struct ice_arfs_active_fltr_cntrs {
	atomic_t active_tcpv4_cnt;
	atomic_t active_tcpv6_cnt;
	atomic_t active_udpv4_cnt;
	atomic_t active_udpv6_cnt;
};

#ifdef CONFIG_RFS_ACCEL
int
ice_rx_flow_steer(struct net_device *netdev, const struct sk_buff *skb,
		  u16 rxq_idx, u32 flow_id);
void ice_clear_arfs(struct ice_vsi *vsi);
void ice_free_cpu_rx_rmap(struct ice_vsi *vsi);
void ice_init_arfs(struct ice_vsi *vsi);
void ice_sync_arfs_fltrs(struct ice_pf *pf);
int ice_set_cpu_rx_rmap(struct ice_vsi *vsi);
void ice_remove_arfs(struct ice_pf *pf);
void ice_rebuild_arfs(struct ice_pf *pf);
bool
ice_is_arfs_using_perfect_flow(struct ice_hw *hw,
			       enum ice_fltr_ptype flow_type);
#else
static inline void ice_clear_arfs(struct ice_vsi *vsi) { }
static inline void ice_free_cpu_rx_rmap(struct ice_vsi *vsi) { }
static inline void ice_init_arfs(struct ice_vsi *vsi) { }
static inline void ice_sync_arfs_fltrs(struct ice_pf *pf) { }
static inline void ice_remove_arfs(struct ice_pf *pf) { }
static inline void ice_rebuild_arfs(struct ice_pf *pf) { }

static inline int ice_set_cpu_rx_rmap(struct ice_vsi __always_unused *vsi)
{
	return 0;
}

static inline int
ice_rx_flow_steer(struct net_device __always_unused *netdev,
		  const struct sk_buff __always_unused *skb,
		  u16 __always_unused rxq_idx, u32 __always_unused flow_id)
{
	return -EOPNOTSUPP;
}

static inline bool
ice_is_arfs_using_perfect_flow(struct ice_hw __always_unused *hw,
			       enum ice_fltr_ptype __always_unused flow_type)
{
	return false;
}
#endif /* CONFIG_RFS_ACCEL */
#endif /* _ICE_ARFS_H_ */
