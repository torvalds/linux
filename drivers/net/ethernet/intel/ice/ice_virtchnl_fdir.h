/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021, Intel Corporation. */

#ifndef _ICE_VIRTCHNL_FDIR_H_
#define _ICE_VIRTCHNL_FDIR_H_

struct ice_vf;

/* VF FDIR information structure */
struct ice_vf_fdir {
	u16 fdir_fltr_cnt[ICE_FLTR_PTYPE_MAX][ICE_FD_HW_SEG_MAX];
	int prof_entry_cnt[ICE_FLTR_PTYPE_MAX][ICE_FD_HW_SEG_MAX];
	struct ice_fd_hw_prof **fdir_prof;

	struct idr fdir_rule_idr;
	struct list_head fdir_rule_list;
};

int ice_vc_add_fdir_fltr(struct ice_vf *vf, u8 *msg);
int ice_vc_del_fdir_fltr(struct ice_vf *vf, u8 *msg);
void ice_vf_fdir_init(struct ice_vf *vf);
void ice_vf_fdir_exit(struct ice_vf *vf);

#endif /* _ICE_VIRTCHNL_FDIR_H_ */
