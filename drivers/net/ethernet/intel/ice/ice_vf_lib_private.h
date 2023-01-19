/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

#ifndef _ICE_VF_LIB_PRIVATE_H_
#define _ICE_VF_LIB_PRIVATE_H_

#include "ice_vf_lib.h"

/* This header file is for exposing functions in ice_vf_lib.c to other files
 * which are also conditionally compiled depending on CONFIG_PCI_IOV.
 * Functions which may be used by other files should be exposed as part of
 * ice_vf_lib.h
 *
 * Functions in this file are exposed only when CONFIG_PCI_IOV is enabled, and
 * thus this header must not be included by .c files which may be compiled
 * with CONFIG_PCI_IOV disabled.
 *
 * To avoid this, only include this header file directly within .c files that
 * are conditionally enabled in the "ice-$(CONFIG_PCI_IOV)" block.
 */

#ifndef CONFIG_PCI_IOV
#warning "Only include ice_vf_lib_private.h in CONFIG_PCI_IOV virtualization files"
#endif

void ice_dis_vf_qs(struct ice_vf *vf);
int ice_check_vf_init(struct ice_vf *vf);
enum virtchnl_status_code ice_err_to_virt_err(int err);
struct ice_port_info *ice_vf_get_port_info(struct ice_vf *vf);
int ice_vsi_apply_spoofchk(struct ice_vsi *vsi, bool enable);
bool ice_is_vf_trusted(struct ice_vf *vf);
bool ice_vf_has_no_qs_ena(struct ice_vf *vf);
bool ice_is_vf_link_up(struct ice_vf *vf);
void ice_vf_rebuild_host_cfg(struct ice_vf *vf);
void ice_vf_ctrl_invalidate_vsi(struct ice_vf *vf);
void ice_vf_ctrl_vsi_release(struct ice_vf *vf);
struct ice_vsi *ice_vf_ctrl_vsi_setup(struct ice_vf *vf);
void ice_vf_invalidate_vsi(struct ice_vf *vf);
void ice_vf_set_initialized(struct ice_vf *vf);

#endif /* _ICE_VF_LIB_PRIVATE_H_ */
