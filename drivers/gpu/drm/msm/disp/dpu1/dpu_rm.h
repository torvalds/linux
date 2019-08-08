/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#ifndef __DPU_RM_H__
#define __DPU_RM_H__

#include <linux/list.h>

#include "msm_kms.h"
#include "dpu_hw_top.h"

/**
 * struct dpu_rm - DPU dynamic hardware resource manager
 * @hw_blks: array of lists of hardware resources present in the system, one
 *	list per type of hardware block
 * @lm_max_width: cached layer mixer maximum width
 * @rm_lock: resource manager mutex
 */
struct dpu_rm {
	struct list_head hw_blks[DPU_HW_BLK_MAX];
	uint32_t lm_max_width;
	struct mutex rm_lock;
};

/**
 *  struct dpu_rm_hw_blk - resource manager internal structure
 *	forward declaration for single iterator definition without void pointer
 */
struct dpu_rm_hw_blk;

/**
 * struct dpu_rm_hw_iter - iterator for use with dpu_rm
 * @hw: dpu_hw object requested, or NULL on failure
 * @blk: dpu_rm internal block representation. Clients ignore. Used as iterator.
 * @enc_id: DRM ID of Encoder client wishes to search for, or 0 for Any Encoder
 * @type: Hardware Block Type client wishes to search for.
 */
struct dpu_rm_hw_iter {
	void *hw;
	struct dpu_rm_hw_blk *blk;
	uint32_t enc_id;
	enum dpu_hw_blk_type type;
};

/**
 * dpu_rm_init - Read hardware catalog and create reservation tracking objects
 *	for all HW blocks.
 * @rm: DPU Resource Manager handle
 * @cat: Pointer to hardware catalog
 * @mmio: mapped register io address of MDP
 * @Return: 0 on Success otherwise -ERROR
 */
int dpu_rm_init(struct dpu_rm *rm,
		struct dpu_mdss_cfg *cat,
		void __iomem *mmio);

/**
 * dpu_rm_destroy - Free all memory allocated by dpu_rm_init
 * @rm: DPU Resource Manager handle
 * @Return: 0 on Success otherwise -ERROR
 */
int dpu_rm_destroy(struct dpu_rm *rm);

/**
 * dpu_rm_reserve - Given a CRTC->Encoder->Connector display chain, analyze
 *	the use connections and user requirements, specified through related
 *	topology control properties, and reserve hardware blocks to that
 *	display chain.
 *	HW blocks can then be accessed through dpu_rm_get_* functions.
 *	HW Reservations should be released via dpu_rm_release_hw.
 * @rm: DPU Resource Manager handle
 * @drm_enc: DRM Encoder handle
 * @crtc_state: Proposed Atomic DRM CRTC State handle
 * @topology: Pointer to topology info for the display
 * @test_only: Atomic-Test phase, discard results (unless property overrides)
 * @Return: 0 on Success otherwise -ERROR
 */
int dpu_rm_reserve(struct dpu_rm *rm,
		struct drm_encoder *drm_enc,
		struct drm_crtc_state *crtc_state,
		struct msm_display_topology topology,
		bool test_only);

/**
 * dpu_rm_reserve - Given the encoder for the display chain, release any
 *	HW blocks previously reserved for that use case.
 * @rm: DPU Resource Manager handle
 * @enc: DRM Encoder handle
 * @Return: 0 on Success otherwise -ERROR
 */
void dpu_rm_release(struct dpu_rm *rm, struct drm_encoder *enc);

/**
 * dpu_rm_init_hw_iter - setup given iterator for new iteration over hw list
 *	using dpu_rm_get_hw
 * @iter: iter object to initialize
 * @enc_id: DRM ID of Encoder client wishes to search for, or 0 for Any Encoder
 * @type: Hardware Block Type client wishes to search for.
 */
void dpu_rm_init_hw_iter(
		struct dpu_rm_hw_iter *iter,
		uint32_t enc_id,
		enum dpu_hw_blk_type type);
/**
 * dpu_rm_get_hw - retrieve reserved hw object given encoder and hw type
 *	Meant to do a single pass through the hardware list to iteratively
 *	retrieve hardware blocks of a given type for a given encoder.
 *	Initialize an iterator object.
 *	Set hw block type of interest. Set encoder id of interest, 0 for any.
 *	Function returns first hw of type for that encoder.
 *	Subsequent calls will return the next reserved hw of that type in-order.
 *	Iterator HW pointer will be null on failure to find hw.
 * @rm: DPU Resource Manager handle
 * @iter: iterator object
 * @Return: true on match found, false on no match found
 */
bool dpu_rm_get_hw(struct dpu_rm *rm, struct dpu_rm_hw_iter *iter);
#endif /* __DPU_RM_H__ */
