/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_MAC_H_
#define _IGC_MAC_H_

#include "igc_hw.h"
#include "igc_phy.h"
#include "igc_defines.h"

/* forward declaration */
s32 igc_disable_pcie_master(struct igc_hw *hw);
s32 igc_check_for_copper_link(struct igc_hw *hw);
s32 igc_config_fc_after_link_up(struct igc_hw *hw);
s32 igc_force_mac_fc(struct igc_hw *hw);
void igc_init_rx_addrs(struct igc_hw *hw, u16 rar_count);
s32 igc_setup_link(struct igc_hw *hw);
void igc_clear_hw_cntrs_base(struct igc_hw *hw);
s32 igc_get_auto_rd_done(struct igc_hw *hw);
void igc_put_hw_semaphore(struct igc_hw *hw);
void igc_rar_set(struct igc_hw *hw, u8 *addr, u32 index);
void igc_config_collision_dist(struct igc_hw *hw);

s32 igc_get_speed_and_duplex_copper(struct igc_hw *hw, u16 *speed,
				    u16 *duplex);

bool igc_enable_mng_pass_thru(struct igc_hw *hw);
void igc_update_mc_addr_list(struct igc_hw *hw,
			     u8 *mc_addr_list, u32 mc_addr_count);

enum igc_mng_mode {
	igc_mng_mode_none = 0,
	igc_mng_mode_asf,
	igc_mng_mode_pt,
	igc_mng_mode_ipmi,
	igc_mng_mode_host_if_only
};

#endif
