// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2020, Intel Corporation. */

#include "ice_common.h"

/**
 * ice_alloc_fd_res_cntr - obtain counter resource for FD type
 * @hw: pointer to the hardware structure
 * @cntr_id: returns counter index
 */
enum ice_status ice_alloc_fd_res_cntr(struct ice_hw *hw, u16 *cntr_id)
{
	return ice_alloc_res_cntr(hw, ICE_AQC_RES_TYPE_FDIR_COUNTER_BLOCK,
				  ICE_AQC_RES_TYPE_FLAG_DEDICATED, 1, cntr_id);
}

/**
 * ice_free_fd_res_cntr - Free counter resource for FD type
 * @hw: pointer to the hardware structure
 * @cntr_id: counter index to be freed
 */
enum ice_status ice_free_fd_res_cntr(struct ice_hw *hw, u16 cntr_id)
{
	return ice_free_res_cntr(hw, ICE_AQC_RES_TYPE_FDIR_COUNTER_BLOCK,
				 ICE_AQC_RES_TYPE_FLAG_DEDICATED, 1, cntr_id);
}

/**
 * ice_alloc_fd_guar_item - allocate resource for FD guaranteed entries
 * @hw: pointer to the hardware structure
 * @cntr_id: returns counter index
 * @num_fltr: number of filter entries to be allocated
 */
enum ice_status
ice_alloc_fd_guar_item(struct ice_hw *hw, u16 *cntr_id, u16 num_fltr)
{
	return ice_alloc_res_cntr(hw, ICE_AQC_RES_TYPE_FDIR_GUARANTEED_ENTRIES,
				  ICE_AQC_RES_TYPE_FLAG_DEDICATED, num_fltr,
				  cntr_id);
}

/**
 * ice_alloc_fd_shrd_item - allocate resource for flow director shared entries
 * @hw: pointer to the hardware structure
 * @cntr_id: returns counter index
 * @num_fltr: number of filter entries to be allocated
 */
enum ice_status
ice_alloc_fd_shrd_item(struct ice_hw *hw, u16 *cntr_id, u16 num_fltr)
{
	return ice_alloc_res_cntr(hw, ICE_AQC_RES_TYPE_FDIR_SHARED_ENTRIES,
				  ICE_AQC_RES_TYPE_FLAG_DEDICATED, num_fltr,
				  cntr_id);
}

/**
 * ice_get_fdir_cnt_all - get the number of Flow Director filters
 * @hw: hardware data structure
 *
 * Returns the number of filters available on device
 */
int ice_get_fdir_cnt_all(struct ice_hw *hw)
{
	return hw->func_caps.fd_fltr_guar + hw->func_caps.fd_fltr_best_effort;
}

/**
 * ice_fdir_find_by_idx - find filter with idx
 * @hw: pointer to hardware structure
 * @fltr_idx: index to find.
 *
 * Returns pointer to filter if found or null
 */
struct ice_fdir_fltr *
ice_fdir_find_fltr_by_idx(struct ice_hw *hw, u32 fltr_idx)
{
	struct ice_fdir_fltr *rule;

	list_for_each_entry(rule, &hw->fdir_list_head, fltr_node) {
		/* rule ID found in the list */
		if (fltr_idx == rule->fltr_id)
			return rule;
		if (fltr_idx < rule->fltr_id)
			break;
	}
	return NULL;
}
