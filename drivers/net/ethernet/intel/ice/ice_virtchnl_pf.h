/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_VIRTCHNL_PF_H_
#define _ICE_VIRTCHNL_PF_H_
#include "ice.h"

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

	s16 vf_id;			/* VF id in the PF space */
	int first_vector_idx;		/* first vector index of this VF */
	struct ice_sw *vf_sw_id;	/* switch id the VF VSIs connect to */
	struct virtchnl_ether_addr dflt_lan_addr;
	u16 port_vlan_id;
	u8 trusted;
	u16 lan_vsi_idx;		/* index into PF struct */
	u16 lan_vsi_num;		/* ID as used by firmware */
	unsigned long vf_caps;		/* vf's adv. capabilities */
	DECLARE_BITMAP(vf_states, ICE_VF_STATES_NBITS);	/* VF runtime states */
	u8 spoofchk;
	u16 num_mac;
	u16 num_vlan;
};

#ifdef CONFIG_PCI_IOV
void ice_process_vflr_event(struct ice_pf *pf);
int ice_sriov_configure(struct pci_dev *pdev, int num_vfs);
void ice_free_vfs(struct ice_pf *pf);
void ice_vc_notify_reset(struct ice_pf *pf);
bool ice_reset_all_vfs(struct ice_pf *pf, bool is_vflr);
#else /* CONFIG_PCI_IOV */
#define ice_process_vflr_event(pf) do {} while (0)
#define ice_free_vfs(pf) do {} while (0)
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
#endif /* CONFIG_PCI_IOV */
#endif /* _ICE_VIRTCHNL_PF_H_ */
