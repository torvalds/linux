/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_SRIOV_H_
#define _ICE_SRIOV_H_
#include "ice_virtchnl_fdir.h"
#include "ice_vf_lib.h"
#include "ice_virtchnl.h"

/* Static VF transaction/status register def */
#define VF_DEVICE_STATUS		0xAA
#define VF_TRANS_PENDING_M		0x20

/* wait defines for polling PF_PCI_CIAD register status */
#define ICE_PCI_CIAD_WAIT_COUNT		100
#define ICE_PCI_CIAD_WAIT_DELAY_US	1

/* VF resource constraints */
#define ICE_MIN_QS_PER_VF		1
#define ICE_NONQ_VECS_VF		1
#define ICE_NUM_VF_MSIX_MED		17
#define ICE_NUM_VF_MSIX_SMALL		5
#define ICE_NUM_VF_MSIX_MULTIQ_MIN	3
#define ICE_MIN_INTR_PER_VF		(ICE_MIN_QS_PER_VF + 1)
#define ICE_MAX_VF_RESET_TRIES		40
#define ICE_MAX_VF_RESET_SLEEP_MS	20

#ifdef CONFIG_PCI_IOV
void ice_process_vflr_event(struct ice_pf *pf);
int ice_sriov_configure(struct pci_dev *pdev, int num_vfs);
int ice_set_vf_mac(struct net_device *netdev, int vf_id, u8 *mac);
int
ice_get_vf_cfg(struct net_device *netdev, int vf_id, struct ifla_vf_info *ivi);

void ice_free_vfs(struct ice_pf *pf);
void ice_restore_all_vfs_msi_state(struct ice_pf *pf);

int
ice_set_vf_port_vlan(struct net_device *netdev, int vf_id, u16 vlan_id, u8 qos,
		     __be16 vlan_proto);

int
ice_set_vf_bw(struct net_device *netdev, int vf_id, int min_tx_rate,
	      int max_tx_rate);

int ice_set_vf_trust(struct net_device *netdev, int vf_id, bool trusted);

int ice_set_vf_link_state(struct net_device *netdev, int vf_id, int link_state);

int ice_set_vf_spoofchk(struct net_device *netdev, int vf_id, bool ena);

void ice_calc_vf_reg_idx(struct ice_vf *vf, struct ice_q_vector *q_vector);

int
ice_get_vf_stats(struct net_device *netdev, int vf_id,
		 struct ifla_vf_stats *vf_stats);
void
ice_vf_lan_overflow_event(struct ice_pf *pf, struct ice_rq_event_info *event);
void ice_print_vfs_mdd_events(struct ice_pf *pf);
void ice_print_vf_rx_mdd_event(struct ice_vf *vf);
void ice_print_vf_tx_mdd_event(struct ice_vf *vf);
bool
ice_vc_validate_pattern(struct ice_vf *vf, struct virtchnl_proto_hdrs *proto);
u32 ice_sriov_get_vf_total_msix(struct pci_dev *pdev);
int ice_sriov_set_msix_vec_count(struct pci_dev *vf_dev, int msix_vec_count);
#else /* CONFIG_PCI_IOV */
static inline void ice_process_vflr_event(struct ice_pf *pf) { }
static inline void ice_free_vfs(struct ice_pf *pf) { }
static inline
void ice_vf_lan_overflow_event(struct ice_pf *pf, struct ice_rq_event_info *event) { }
static inline void ice_print_vfs_mdd_events(struct ice_pf *pf) { }
static inline void ice_print_vf_rx_mdd_event(struct ice_vf *vf) { }
static inline void ice_print_vf_tx_mdd_event(struct ice_vf *vf) { }
static inline void ice_restore_all_vfs_msi_state(struct ice_pf *pf) { }

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

static inline int
ice_set_vf_bw(struct net_device __always_unused *netdev,
	      int __always_unused vf_id, int __always_unused min_tx_rate,
	      int __always_unused max_tx_rate)
{
	return -EOPNOTSUPP;
}

static inline void
ice_calc_vf_reg_idx(struct ice_vf __always_unused *vf,
		    struct ice_q_vector __always_unused *q_vector)
{
}

static inline int
ice_get_vf_stats(struct net_device __always_unused *netdev,
		 int __always_unused vf_id,
		 struct ifla_vf_stats __always_unused *vf_stats)
{
	return -EOPNOTSUPP;
}

static inline u32 ice_sriov_get_vf_total_msix(struct pci_dev *pdev)
{
	return 0;
}

static inline int
ice_sriov_set_msix_vec_count(struct pci_dev *vf_dev, int msix_vec_count)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_PCI_IOV */
#endif /* _ICE_SRIOV_H_ */
