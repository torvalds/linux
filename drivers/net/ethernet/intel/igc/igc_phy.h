/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_PHY_H_
#define _IGC_PHY_H_

#include "igc_mac.h"

s32 igc_check_reset_block(struct igc_hw *hw);
s32 igc_phy_hw_reset(struct igc_hw *hw);
s32 igc_get_phy_id(struct igc_hw *hw);
s32 igc_phy_has_link(struct igc_hw *hw, u32 iterations,
		     u32 usec_interval, bool *success);
s32 igc_check_downshift(struct igc_hw *hw);
s32 igc_setup_copper_link(struct igc_hw *hw);
void igc_power_up_phy_copper(struct igc_hw *hw);
void igc_power_down_phy_copper(struct igc_hw *hw);
s32 igc_write_phy_reg_gpy(struct igc_hw *hw, u32 offset, u16 data);
s32 igc_read_phy_reg_gpy(struct igc_hw *hw, u32 offset, u16 *data);

#endif
