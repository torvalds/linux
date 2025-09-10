/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2024 Intel Corporation. */

#ifndef _IXGBE_X550_H_
#define _IXGBE_X550_H_

#include "ixgbe_type.h"

extern const u32 ixgbe_mvals_x550em_a[IXGBE_MVALS_IDX_LIMIT];

int ixgbe_set_fw_drv_ver_x550(struct ixgbe_hw *hw, u8 maj, u8 min,
			      u8 build, u8 sub, u16 len,
			      const char *driver_ver);
void ixgbe_set_source_address_pruning_x550(struct ixgbe_hw *hw,
					   bool enable,
					   unsigned int pool);
void ixgbe_set_ethertype_anti_spoofing_x550(struct ixgbe_hw *hw,
					    bool enable, int vf);

void ixgbe_enable_mdd_x550(struct ixgbe_hw *hw);
void ixgbe_disable_mdd_x550(struct ixgbe_hw *hw);
void ixgbe_restore_mdd_vf_x550(struct ixgbe_hw *hw, u32 vf);
void ixgbe_handle_mdd_x550(struct ixgbe_hw *hw, unsigned long *vf_bitmap);

#endif /* _IXGBE_X550_H_ */
