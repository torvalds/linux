/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023, Intel Corporation. */

#ifndef _ICE_ESWITCH_BR_H_
#define _ICE_ESWITCH_BR_H_

#include <linux/rhashtable.h>
#include <linux/workqueue.h>

struct ice_esw_br_fdb_data {
	unsigned char addr[ETH_ALEN];
	u16 vid;
};

struct ice_esw_br_flow {
	struct ice_rule_query_data *fwd_rule;
	struct ice_rule_query_data *guard_rule;
};

enum {
	ICE_ESWITCH_BR_FDB_ADDED_BY_USER = BIT(0),
};

struct ice_esw_br_fdb_entry {
	struct ice_esw_br_fdb_data data;
	struct rhash_head ht_node;
	struct list_head list;

	int flags;

	struct net_device *dev;
	struct ice_esw_br_port *br_port;
	struct ice_esw_br_flow *flow;

	unsigned long last_use;
};

enum ice_esw_br_port_type {
	ICE_ESWITCH_BR_UPLINK_PORT = 0,
	ICE_ESWITCH_BR_VF_REPR_PORT = 1,
};

struct ice_esw_br_port {
	struct ice_esw_br *bridge;
	struct ice_vsi *vsi;
	enum ice_esw_br_port_type type;
	u16 vsi_idx;
	u16 pvid;
	u32 repr_id;
	struct xarray vlans;
};

enum {
	ICE_ESWITCH_BR_VLAN_FILTERING = BIT(0),
};

struct ice_esw_br {
	struct ice_esw_br_offloads *br_offloads;
	struct xarray ports;

	struct rhashtable fdb_ht;
	struct list_head fdb_list;

	int ifindex;
	u32 flags;
	unsigned long ageing_time;
};

struct ice_esw_br_offloads {
	struct ice_pf *pf;
	struct ice_esw_br *bridge;
	struct notifier_block netdev_nb;
	struct notifier_block switchdev_blk;
	struct notifier_block switchdev_nb;

	struct workqueue_struct *wq;
	struct delayed_work update_work;
};

struct ice_esw_br_fdb_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct net_device *dev;
	unsigned long event;
};

struct ice_esw_br_vlan {
	u16 vid;
	u16 flags;
};

#define ice_nb_to_br_offloads(nb, nb_name) \
	container_of(nb, \
		     struct ice_esw_br_offloads, \
		     nb_name)

#define ice_work_to_br_offloads(w) \
	container_of(w, \
		     struct ice_esw_br_offloads, \
		     update_work.work)

#define ice_work_to_fdb_work(w) \
	container_of(w, \
		     struct ice_esw_br_fdb_work, \
		     work)

static inline bool ice_eswitch_br_is_vid_valid(u16 vid)
{
	/* In trunk VLAN mode, for untagged traffic the bridge sends requests
	 * to offload VLAN 1 with pvid and untagged flags set. Since these
	 * flags are not supported, add a MAC filter instead.
	 */
	return vid > 1;
}

void
ice_eswitch_br_offloads_deinit(struct ice_pf *pf);
int
ice_eswitch_br_offloads_init(struct ice_pf *pf);

#endif /* _ICE_ESWITCH_BR_H_ */
