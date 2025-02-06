/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 1999 - 2024 Intel Corporation. */

#ifndef _IXGBE_X540_H_
#define _IXGBE_X540_H_

#include "ixgbe_type.h"

int ixgbe_get_invariants_X540(struct ixgbe_hw *hw);
int ixgbe_setup_mac_link_X540(struct ixgbe_hw *hw, ixgbe_link_speed speed,
			      bool autoneg_wait_to_complete);
int ixgbe_reset_hw_X540(struct ixgbe_hw *hw);
int ixgbe_start_hw_X540(struct ixgbe_hw *hw);
enum ixgbe_media_type ixgbe_get_media_type_X540(struct ixgbe_hw *hw);
int ixgbe_setup_mac_link_X540(struct ixgbe_hw *hw, ixgbe_link_speed speed,
			      bool autoneg_wait_to_complete);
int ixgbe_blink_led_start_X540(struct ixgbe_hw *hw, u32 index);
int ixgbe_blink_led_stop_X540(struct ixgbe_hw *hw, u32 index);
int ixgbe_acquire_swfw_sync_X540(struct ixgbe_hw *hw, u32 mask);
void ixgbe_release_swfw_sync_X540(struct ixgbe_hw *hw, u32 mask);
void ixgbe_init_swfw_sync_X540(struct ixgbe_hw *hw);
int ixgbe_init_eeprom_params_X540(struct ixgbe_hw *hw);

#endif /* _IXGBE_X540_H_ */
