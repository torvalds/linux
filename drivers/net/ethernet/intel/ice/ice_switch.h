/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_SWITCH_H_
#define _ICE_SWITCH_H_

#include "ice_common.h"

#define ICE_SW_CFG_MAX_BUF_LEN 2048
#define ICE_DFLT_VSI_INVAL 0xff

/* VSI context structure for add/get/update/free operations */
struct ice_vsi_ctx {
	u16 vsi_num;
	u16 vsis_allocd;
	u16 vsis_unallocated;
	u16 flags;
	struct ice_aqc_vsi_props info;
	bool alloc_from_pool;
};

/* VSI related commands */
enum ice_status
ice_aq_add_vsi(struct ice_hw *hw, struct ice_vsi_ctx *vsi_ctx,
	       struct ice_sq_cd *cd);
enum ice_status
ice_aq_update_vsi(struct ice_hw *hw, struct ice_vsi_ctx *vsi_ctx,
		  struct ice_sq_cd *cd);
enum ice_status
ice_aq_free_vsi(struct ice_hw *hw, struct ice_vsi_ctx *vsi_ctx,
		bool keep_vsi_alloc, struct ice_sq_cd *cd);

enum ice_status ice_get_initial_sw_cfg(struct ice_hw *hw);

#endif /* _ICE_SWITCH_H_ */
