/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2020, Intel Corporation. */

#ifndef _ICE_FDIR_H_
#define _ICE_FDIR_H_
enum ice_status ice_alloc_fd_res_cntr(struct ice_hw *hw, u16 *cntr_id);
enum ice_status ice_free_fd_res_cntr(struct ice_hw *hw, u16 cntr_id);
enum ice_status
ice_alloc_fd_guar_item(struct ice_hw *hw, u16 *cntr_id, u16 num_fltr);
enum ice_status
ice_alloc_fd_shrd_item(struct ice_hw *hw, u16 *cntr_id, u16 num_fltr);
#endif /* _ICE_FDIR_H_ */
