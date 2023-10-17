/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019-2021, Intel Corporation. */

#ifndef _ICE_VF_VSI_VLAN_OPS_H_
#define _ICE_VF_VSI_VLAN_OPS_H_

#include "ice_vsi_vlan_ops.h"

struct ice_vsi;

void ice_vf_vsi_cfg_dvm_legacy_vlan_mode(struct ice_vsi *vsi);
void ice_vf_vsi_cfg_svm_legacy_vlan_mode(struct ice_vsi *vsi);

#ifdef CONFIG_PCI_IOV
void ice_vf_vsi_init_vlan_ops(struct ice_vsi *vsi);
void ice_vf_vsi_enable_port_vlan(struct ice_vsi *vsi);
void ice_vf_vsi_disable_port_vlan(struct ice_vsi *vsi);
#else
static inline void ice_vf_vsi_init_vlan_ops(struct ice_vsi *vsi) { }
static inline void ice_vf_vsi_enable_port_vlan(struct ice_vsi *vsi) { }
static inline void ice_vf_vsi_disable_port_vlan(struct ice_vsi *vsi) { }
#endif /* CONFIG_PCI_IOV */
#endif /* _ICE_PF_VSI_VLAN_OPS_H_ */
