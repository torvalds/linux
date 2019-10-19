/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_SRIOV_H_
#define _ICE_SRIOV_H_

#include "ice_common.h"

#ifdef CONFIG_PCI_IOV
enum ice_status
ice_aq_send_msg_to_vf(struct ice_hw *hw, u16 vfid, u32 v_opcode, u32 v_retval,
		      u8 *msg, u16 msglen, struct ice_sq_cd *cd);

u32 ice_conv_link_speed_to_virtchnl(bool adv_link_support, u16 link_speed);
#else /* CONFIG_PCI_IOV */
static inline enum ice_status
ice_aq_send_msg_to_vf(struct ice_hw __always_unused *hw,
		      u16 __always_unused vfid, u32 __always_unused v_opcode,
		      u32 __always_unused v_retval, u8 __always_unused *msg,
		      u16 __always_unused msglen,
		      struct ice_sq_cd __always_unused *cd)
{
	return 0;
}

static inline u32
ice_conv_link_speed_to_virtchnl(bool __always_unused adv_link_support,
				u16 __always_unused link_speed)
{
	return 0;
}

#endif /* CONFIG_PCI_IOV */
#endif /* _ICE_SRIOV_H_ */
