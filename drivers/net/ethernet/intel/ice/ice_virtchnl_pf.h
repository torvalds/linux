/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_VIRTCHNL_PF_H_
#define _ICE_VIRTCHNL_PF_H_
#include "ice.h"

#define ICE_MAX_VLANID			4095
#define ICE_VLAN_PRIORITY_S		12
#define ICE_VLAN_M			0xFFF
#define ICE_PRIORITY_M			0x7000

/* Restrict number of MAC Addr and VLAN that non-trusted VF can programmed */
#define ICE_MAX_VLAN_PER_VF		8
#define ICE_MAX_MACADDR_PER_VF		12

/* Malicious Driver Detection */
#define ICE_DFLT_NUM_MDD_EVENTS_ALLOWED		3
#define ICE_DFLT_NUM_INVAL_MSGS_ALLOWED		10

/* Static VF transaction/status register def */
#define VF_DEVICE_STATUS		0xAA
#define VF_TRANS_PENDING_M		0x20

/* Specific VF states */
enum ice_vf_states {
	ICE_VF_STATE_INIT = 0,
	ICE_VF_STATE_ACTIVE,
	ICE_VF_STATE_ENA,
	ICE_VF_STATE_DIS,
	ICE_VF_STATE_MC_PROMISC,
	ICE_VF_STATE_UC_PROMISC,
	/* state to indicate if PF needs to do vector assignment for VF.
	 * This needs to be set during first time VF initialization or later
	 * when VF asks for more Vectors through virtchnl OP.
	 */
	ICE_VF_STATE_CFG_INTR,
	ICE_VF_STATES_NBITS
};

/* VF capabilities */
enum ice_virtchnl_cap {
	ICE_VIRTCHNL_VF_CAP_L2 = 0,
	ICE_VIRTCHNL_VF_CAP_PRIVILEGE,
};

/* VF information structure */
struct ice_vf {
	struct ice_pf *pf;

	s16 vf_id;			/* VF ID in the PF space */
	u32 driver_caps;		/* reported by VF driver */
	int first_vector_idx;		/* first vector index of this VF */
	struct ice_sw *vf_sw_id;	/* switch ID the VF VSIs connect to */
	struct virtchnl_version_info vf_ver;
	struct virtchnl_ether_addr dflt_lan_addr;
	u16 port_vlan_id;
	u8 pf_set_mac;			/* VF MAC address set by VMM admin */
	u8 trusted;
	u16 lan_vsi_idx;		/* index into PF struct */
	u16 lan_vsi_num;		/* ID as used by firmware */
	u64 num_mdd_events;		/* number of MDD events detected */
	u64 num_inval_msgs;		/* number of continuous invalid msgs */
	u64 num_valid_msgs;		/* number of valid msgs detected */
	unsigned long vf_caps;		/* VF's adv. capabilities */
	DECLARE_BITMAP(vf_states, ICE_VF_STATES_NBITS);	/* VF runtime states */
	unsigned int tx_rate;		/* Tx bandwidth limit in Mbps */
	u8 link_forced;
	u8 link_up;			/* only valid if VF link is forced */
	u8 spoofchk;
	u16 num_mac;
	u16 num_vlan;
	u16 num_vf_qs;			/* num of queue configured per VF */
	u8 num_req_qs;			/* num of queue pairs requested by VF */
};

#ifdef CONFIG_PCI_IOV
void ice_process_vflr_event(struct ice_pf *pf);
int ice_sriov_configure(struct pci_dev *pdev, int num_vfs);
int ice_set_vf_mac(struct net_device *netdev, int vf_id, u8 *mac);
int
ice_get_vf_cfg(struct net_device *netdev, int vf_id, struct ifla_vf_info *ivi);

void ice_free_vfs(struct ice_pf *pf);
void ice_vc_process_vf_msg(struct ice_pf *pf, struct ice_rq_event_info *event);
void ice_vc_notify_link_state(struct ice_pf *pf);
void ice_vc_notify_reset(struct ice_pf *pf);
bool ice_reset_all_vfs(struct ice_pf *pf, bool is_vflr);

int
ice_set_vf_port_vlan(struct net_device *netdev, int vf_id, u16 vlan_id, u8 qos,
		     __be16 vlan_proto);

int ice_set_vf_trust(struct net_device *netdev, int vf_id, bool trusted);

int ice_set_vf_link_state(struct net_device *netdev, int vf_id, int link_state);

int ice_set_vf_spoofchk(struct net_device *netdev, int vf_id, bool ena);
#else /* CONFIG_PCI_IOV */
#define ice_process_vflr_event(pf) do {} while (0)
#define ice_free_vfs(pf) do {} while (0)
#define ice_vc_process_vf_msg(pf, event) do {} while (0)
#define ice_vc_notify_link_state(pf) do {} while (0)
#define ice_vc_notify_reset(pf) do {} while (0)

static inline bool
ice_reset_all_vfs(struct ice_pf __always_unused *pf,
		  bool __always_unused is_vflr)
{
	return true;
}

static inline int
ice_sriov_configure(struct pci_dev __always_unused *pdev,
		    int __always_unused num_vfs)
{
	return -EOPNOTSUPP;
}

static inline int
ice_set_vf_mac(struct net_device __always_unused *netdev,
	       int __always_unused vf_id, u8 __always_unused *mac)
{
	return -EOPNOTSUPP;
}

static inline int
ice_get_vf_cfg(struct net_device __always_unused *netdev,
	       int __always_unused vf_id,
	       struct ifla_vf_info __always_unused *ivi)
{
	return -EOPNOTSUPP;
}

static inline int
ice_set_vf_trust(struct net_device __always_unused *netdev,
		 int __always_unused vf_id, bool __always_unused trusted)
{
	return -EOPNOTSUPP;
}

static inline int
ice_set_vf_port_vlan(struct net_device __always_unused *netdev,
		     int __always_unused vf_id, u16 __always_unused vid,
		     u8 __always_unused qos, __be16 __always_unused v_proto)
{
	return -EOPNOTSUPP;
}

static inline int
ice_set_vf_spoofchk(struct net_device __always_unused *netdev,
		    int __always_unused vf_id, bool __always_unused ena)
{
	return -EOPNOTSUPP;
}

static inline int
ice_set_vf_link_state(struct net_device __always_unused *netdev,
		      int __always_unused vf_id, int __always_unused link_state)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_PCI_IOV */
#endif /* _ICE_VIRTCHNL_PF_H_ */
