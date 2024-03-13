/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021, Intel Corporation. */

#ifndef _ICE_VIRTCHNL_ALLOWLIST_H_
#define _ICE_VIRTCHNL_ALLOWLIST_H_
#include "ice.h"

bool ice_vc_is_opcode_allowed(struct ice_vf *vf, u32 opcode);

void ice_vc_set_default_allowlist(struct ice_vf *vf);
void ice_vc_set_working_allowlist(struct ice_vf *vf);
void ice_vc_set_caps_allowlist(struct ice_vf *vf);
#endif /* _ICE_VIRTCHNL_ALLOWLIST_H_ */
