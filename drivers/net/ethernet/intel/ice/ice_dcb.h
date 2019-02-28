/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_DCB_H_
#define _ICE_DCB_H_

#include "ice_type.h"

#define ICE_DCBX_STATUS_IN_PROGRESS	1
#define ICE_DCBX_STATUS_DONE		2

u8 ice_get_dcbx_status(struct ice_hw *hw);
#ifdef CONFIG_DCB
enum ice_status ice_aq_start_lldp(struct ice_hw *hw, struct ice_sq_cd *cd);
enum ice_status
ice_aq_start_stop_dcbx(struct ice_hw *hw, bool start_dcbx_agent,
		       bool *dcbx_agent_status, struct ice_sq_cd *cd);
#else /* CONFIG_DCB */
static inline enum ice_status
ice_aq_start_lldp(struct ice_hw __always_unused *hw,
		  struct ice_sq_cd __always_unused *cd)
{
	return 0;
}

static inline enum ice_status
ice_aq_start_stop_dcbx(struct ice_hw __always_unused *hw,
		       bool __always_unused start_dcbx_agent,
		       bool *dcbx_agent_status,
		       struct ice_sq_cd __always_unused *cd)
{
	*dcbx_agent_status = false;

	return 0;
}
#endif /* CONFIG_DCB */
#endif /* _ICE_DCB_H_ */
