/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

#ifndef _ICE_VF_LIB_H_
#define _ICE_VF_LIB_H_

#include <linux/types.h>
#include <linux/hashtable.h>
#include <linux/bitmap.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <net/devlink.h>
#include <linux/avf/virtchnl.h>
#include "ice_type.h"
#include "ice_virtchnl_fdir.h"
#include "ice_vsi_vlan_ops.h"

#define ICE_MAX_SRIOV_VFS		256

/* VF resource constraints */
#define ICE_MAX_RSS_QS_PER_VF	16

struct ice_pf;
struct ice_vf;
struct ice_virtchnl_ops;

/* VF capabilities */
enum ice_virtchnl_cap {
	ICE_VIRTCHNL_VF_CAP_PRIVILEGE = 0,
};

/* Specific VF states */
enum ice_vf_states {
	ICE_VF_STATE_INIT = 0,		/* PF is initializing VF */
	ICE_VF_STATE_ACTIVE,		/* VF resources are allocated for use */
	ICE_VF_STATE_QS_ENA,		/* VF queue(s) enabled */
	ICE_VF_STATE_DIS,
	ICE_VF_STATE_MC_PROMISC,
	ICE_VF_STATE_UC_PROMISC,
	ICE_VF_STATES_NBITS
};

struct ice_time_mac {
	unsigned long time_modified;
	u8 addr[ETH_ALEN];
};

/* VF MDD events print structure */
struct ice_mdd_vf_events {
	u16 count;			/* total count of Rx|Tx events */
	/* count number of the last printed event */
	u16 last_printed;
};

/* VF operations */
struct ice_vf_ops {
	enum ice_disq_rst_src reset_type;
	void (*free)(struct ice_vf *vf);
	void (*clear_mbx_register)(struct ice_vf *vf);
	void (*trigger_reset_register)(struct ice_vf *vf, bool is_vflr);
	bool (*poll_reset_status)(struct ice_vf *vf);
	void (*clear_reset_trigger)(struct ice_vf *vf);
	int (*vsi_rebuild)(struct ice_vf *vf);
	void (*post_vsi_rebuild)(struct ice_vf *vf);
};

/* Virtchnl/SR-IOV config info */
struct ice_vfs {
	DECLARE_HASHTABLE(table, 8);	/* table of VF entries */
	struct mutex table_lock;	/* Lock for protecting the hash table */
	u16 num_supported;		/* max supported VFs on this PF */
	u16 num_qps_per;		/* number of queue pairs per VF */
	u16 num_msix_per;		/* number of MSI-X vectors per VF */
	unsigned long last_printed_mdd_jiffies;	/* MDD message rate limit */
	DECLARE_BITMAP(malvfs, ICE_MAX_SRIOV_VFS); /* malicious VF indicator */
};

/* VF information structure */
struct ice_vf {
	struct hlist_node entry;
	struct rcu_head rcu;
	struct kref refcnt;
	struct ice_pf *pf;

	/* Used during virtchnl message handling and NDO ops against the VF
	 * that will trigger a VFR
	 */
	struct mutex cfg_lock;

	u16 vf_id;			/* VF ID in the PF space */
	u16 lan_vsi_idx;		/* index into PF struct */
	u16 ctrl_vsi_idx;
	struct ice_vf_fdir fdir;
	/* first vector index of this VF in the PF space */
	int first_vector_idx;
	struct ice_sw *vf_sw_id;	/* switch ID the VF VSIs connect to */
	struct virtchnl_version_info vf_ver;
	u32 driver_caps;		/* reported by VF driver */
	struct virtchnl_ether_addr dev_lan_addr;
	struct virtchnl_ether_addr hw_lan_addr;
	struct ice_time_mac legacy_last_added_umac;
	DECLARE_BITMAP(txq_ena, ICE_MAX_RSS_QS_PER_VF);
	DECLARE_BITMAP(rxq_ena, ICE_MAX_RSS_QS_PER_VF);
	struct ice_vlan port_vlan_info;	/* Port VLAN ID, QoS, and TPID */
	struct virtchnl_vlan_caps vlan_v2_caps;
	u8 pf_set_mac:1;		/* VF MAC address set by VMM admin */
	u8 trusted:1;
	u8 spoofchk:1;
	u8 link_forced:1;
	u8 link_up:1;			/* only valid if VF link is forced */
	/* VSI indices - actual VSI pointers are maintained in the PF structure
	 * When assigned, these will be non-zero, because VSI 0 is always
	 * the main LAN VSI for the PF.
	 */
	u16 lan_vsi_num;		/* ID as used by firmware */
	unsigned int min_tx_rate;	/* Minimum Tx bandwidth limit in Mbps */
	unsigned int max_tx_rate;	/* Maximum Tx bandwidth limit in Mbps */
	DECLARE_BITMAP(vf_states, ICE_VF_STATES_NBITS);	/* VF runtime states */

	unsigned long vf_caps;		/* VF's adv. capabilities */
	u8 num_req_qs;			/* num of queue pairs requested by VF */
	u16 num_mac;
	u16 num_vf_qs;			/* num of queue configured per VF */
	struct ice_mdd_vf_events mdd_rx_events;
	struct ice_mdd_vf_events mdd_tx_events;
	DECLARE_BITMAP(opcodes_allowlist, VIRTCHNL_OP_MAX);

	struct ice_repr *repr;
	const struct ice_virtchnl_ops *virtchnl_ops;
	const struct ice_vf_ops *vf_ops;

	/* devlink port data */
	struct devlink_port devlink_port;
};

static inline u16 ice_vf_get_port_vlan_id(struct ice_vf *vf)
{
	return vf->port_vlan_info.vid;
}

static inline u8 ice_vf_get_port_vlan_prio(struct ice_vf *vf)
{
	return vf->port_vlan_info.prio;
}

static inline bool ice_vf_is_port_vlan_ena(struct ice_vf *vf)
{
	return (ice_vf_get_port_vlan_id(vf) || ice_vf_get_port_vlan_prio(vf));
}

static inline u16 ice_vf_get_port_vlan_tpid(struct ice_vf *vf)
{
	return vf->port_vlan_info.tpid;
}

/* VF Hash Table access functions
 *
 * These functions provide abstraction for interacting with the VF hash table.
 * In general, direct access to the hash table should be avoided outside of
 * these functions where possible.
 *
 * The VF entries in the hash table are protected by reference counting to
 * track lifetime of accesses from the table. The ice_get_vf_by_id() function
 * obtains a reference to the VF structure which must be dropped by using
 * ice_put_vf().
 */

/**
 * ice_for_each_vf - Iterate over each VF entry
 * @pf: pointer to the PF private structure
 * @bkt: bucket index used for iteration
 * @vf: pointer to the VF entry currently being processed in the loop.
 *
 * The bkt variable is an unsigned integer iterator used to traverse the VF
 * entries. It is *not* guaranteed to be the VF's vf_id. Do not assume it is.
 * Use vf->vf_id to get the id number if needed.
 *
 * The caller is expected to be under the table_lock mutex for the entire
 * loop. Use this iterator if your loop is long or if it might sleep.
 */
#define ice_for_each_vf(pf, bkt, vf) \
	hash_for_each((pf)->vfs.table, (bkt), (vf), entry)

/**
 * ice_for_each_vf_rcu - Iterate over each VF entry protected by RCU
 * @pf: pointer to the PF private structure
 * @bkt: bucket index used for iteration
 * @vf: pointer to the VF entry currently being processed in the loop.
 *
 * The bkt variable is an unsigned integer iterator used to traverse the VF
 * entries. It is *not* guaranteed to be the VF's vf_id. Do not assume it is.
 * Use vf->vf_id to get the id number if needed.
 *
 * The caller is expected to be under rcu_read_lock() for the entire loop.
 * Only use this iterator if your loop is short and you can guarantee it does
 * not sleep.
 */
#define ice_for_each_vf_rcu(pf, bkt, vf) \
	hash_for_each_rcu((pf)->vfs.table, (bkt), (vf), entry)

#ifdef CONFIG_PCI_IOV
struct ice_vf *ice_get_vf_by_id(struct ice_pf *pf, u16 vf_id);
void ice_put_vf(struct ice_vf *vf);
bool ice_has_vfs(struct ice_pf *pf);
u16 ice_get_num_vfs(struct ice_pf *pf);
struct ice_vsi *ice_get_vf_vsi(struct ice_vf *vf);
bool ice_is_vf_disabled(struct ice_vf *vf);
int ice_check_vf_ready_for_cfg(struct ice_vf *vf);
void ice_set_vf_state_qs_dis(struct ice_vf *vf);
bool ice_is_any_vf_in_promisc(struct ice_pf *pf);
int
ice_vf_set_vsi_promisc(struct ice_vf *vf, struct ice_vsi *vsi, u8 promisc_m);
int
ice_vf_clear_vsi_promisc(struct ice_vf *vf, struct ice_vsi *vsi, u8 promisc_m);
int ice_reset_vf(struct ice_vf *vf, bool is_vflr);
void ice_reset_all_vfs(struct ice_pf *pf);
#else /* CONFIG_PCI_IOV */
static inline struct ice_vf *ice_get_vf_by_id(struct ice_pf *pf, u16 vf_id)
{
	return NULL;
}

static inline void ice_put_vf(struct ice_vf *vf)
{
}

static inline bool ice_has_vfs(struct ice_pf *pf)
{
	return false;
}

static inline u16 ice_get_num_vfs(struct ice_pf *pf)
{
	return 0;
}

static inline struct ice_vsi *ice_get_vf_vsi(struct ice_vf *vf)
{
	return NULL;
}

static inline bool ice_is_vf_disabled(struct ice_vf *vf)
{
	return true;
}

static inline int ice_check_vf_ready_for_cfg(struct ice_vf *vf)
{
	return -EOPNOTSUPP;
}

static inline void ice_set_vf_state_qs_dis(struct ice_vf *vf)
{
}

static inline bool ice_is_any_vf_in_promisc(struct ice_pf *pf)
{
	return false;
}

static inline int
ice_vf_set_vsi_promisc(struct ice_vf *vf, struct ice_vsi *vsi, u8 promisc_m)
{
	return -EOPNOTSUPP;
}

static inline int
ice_vf_clear_vsi_promisc(struct ice_vf *vf, struct ice_vsi *vsi, u8 promisc_m)
{
	return -EOPNOTSUPP;
}

static inline int ice_reset_vf(struct ice_vf *vf, bool is_vflr)
{
	return 0;
}

static inline void ice_reset_all_vfs(struct ice_pf *pf)
{
}
#endif /* !CONFIG_PCI_IOV */

#endif /* _ICE_VF_LIB_H_ */
