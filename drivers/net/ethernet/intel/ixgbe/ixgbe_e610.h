/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2024 Intel Corporation. */

#ifndef _IXGBE_E610_H_
#define _IXGBE_E610_H_

#include "ixgbe_type.h"

int ixgbe_aci_send_cmd(struct ixgbe_hw *hw, struct ixgbe_aci_desc *desc,
		       void *buf, u16 buf_size);
bool ixgbe_aci_check_event_pending(struct ixgbe_hw *hw);
int ixgbe_aci_get_event(struct ixgbe_hw *hw, struct ixgbe_aci_event *e,
			bool *pending);
void ixgbe_fill_dflt_direct_cmd_desc(struct ixgbe_aci_desc *desc, u16 opcode);
int ixgbe_acquire_res(struct ixgbe_hw *hw, enum ixgbe_aci_res_ids res,
		      enum ixgbe_aci_res_access_type access, u32 timeout);
void ixgbe_release_res(struct ixgbe_hw *hw, enum ixgbe_aci_res_ids res);

#endif /* _IXGBE_E610_H_ */
