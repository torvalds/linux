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
