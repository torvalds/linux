/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_I225_H_
#define _IGC_I225_H_

s32 igc_acquire_swfw_sync_i225(struct igc_hw *hw, u16 mask);
void igc_release_swfw_sync_i225(struct igc_hw *hw, u16 mask);

#endif
