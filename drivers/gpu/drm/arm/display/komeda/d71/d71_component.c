// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */

#include <drm/drm_print.h>
#include "d71_dev.h"
#include "komeda_kms.h"
#include "malidp_io.h"
#include "komeda_framebuffer.h"

static void get_resources_id(u32 hw_id, u32 *pipe_id, u32 *comp_id)
{
	u32 id = BLOCK_INFO_BLK_ID(hw_id);
	u32 pipe = id;

	switch (BLOCK_INFO_BLK_TYPE(hw_id)) {
	case D71_BLK_TYPE_LPU_WB_LAYER:
		id = KOMEDA_COMPONENT_WB_LAYER;
		break;
	case D71_BLK_TYPE_CU_SPLITTER:
		id = KOMEDA_COMPONENT_SPLITTER;
		break;
	case D71_BLK_TYPE_CU_SCALER:
		pipe = id / D71_PIPELINE_MAX_SCALERS;
		id %= D71_PIPELINE_MAX_SCALERS;
		id += KOMEDA_COMPONENT_SCALER0;
		break;
	case D71_BLK_TYPE_CU:
		id += KOMEDA_COMPONENT_COMPIZ0;
		break;
	case D71_BLK_TYPE_LPU_LAYER:
		pipe = id / D71_PIPELINE_MAX_LAYERS;
		id %= D71_PIPELINE_MAX_LAYERS;
		id += KOMEDA_COMPONENT_LAYER0;
		break;
	case D71_BLK_TYPE_DOU_IPS:
		id += KOMEDA_COMPONENT_IPS0;
		break;
	case D71_BLK_TYPE_CU_MERGER:
		id = KOMEDA_COMPONENT_MERGER;
		break;
	case D71_BLK_TYPE_DOU:
		id = KOMEDA_COMPONENT_TIMING_CTRLR;
		break;
	default:
		id = 0xFFFFFFFF;
	}

	if (comp_id)
		*comp_id = id;

	if (pipe_id)
		*pipe_id = pipe;
}

static u32 get_valid_inputs(struct block_header *blk)
{
	u32 valid_inputs = 0, comp_id;
	int i;

	for (i = 0; i < PIPELINE_INFO_N_VALID_INPUTS(blk->pipeline_info); i++) {
		get_resources_id(blk->input_ids[i], NULL, &comp_id);
		if (comp_id == 0xFFFFFFFF)
			continue;
		valid_inputs |= BIT(comp_id);
	}

	return valid_inputs;
}

static u32 to_rot_ctrl(u32 rot)
{
	u32 lr_ctrl = 0;

	switch (rot & DRM_MODE_ROTATE_MASK) {
	case DRM_MODE_ROTATE_0:
		lr_ctrl |= L_ROT(L_ROT_R0);
		break;
	case DRM_MODE_ROTATE_90:
		lr_ctrl |= L_ROT(L_ROT_R90);
		break;
	case DRM_MODE_ROTATE_180:
		lr_ctrl |= L_ROT(L_ROT_R180);
		break;
	case DRM_MODE_ROTATE_270:
		lr_ctrl |= L_ROT(L_ROT_R270);
		break;
	}

	if (rot & DRM_MODE_REFLECT_X)
		lr_ctrl |= L_HFLIP;
	if (rot & DRM_MODE_REFLECT_Y)
		lr_ctrl |= L_VFLIP;

	return lr_ctrl;
}

static void d71_layer_disable(struct komeda_component *c)
{
	malidp_write32_mask(c->reg, BLK_CONTROL, L_EN, 0);
}

static void d71_layer_update(struct komeda_component *c,
			     struct komeda_component_state *state)
{
	struct komeda_layer_state *st = to_layer_st(state);
	struct drm_plane_state *plane_st = state->plane->state;
	struct drm_framebuffer *fb = plane_st->fb;
	struct komeda_fb *kfb = to_kfb(fb);
	u32 __iomem *reg = c->reg;
	u32 ctrl_mask = L_EN | L_ROT(L_ROT_R270) | L_HFLIP | L_VFLIP | L_TBU_EN;
	u32 ctrl = L_EN | to_rot_ctrl(st->rot);
	int i;

	for (i = 0; i < fb->format->num_planes; i++) {
		malidp_write32(reg,
			       BLK_P0_PTR_LOW + i * LAYER_PER_PLANE_REGS * 4,
			       lower_32_bits(st->addr[i]));
		malidp_write32(reg,
			       BLK_P0_PTR_HIGH + i * LAYER_PER_PLANE_REGS * 4,
			       upper_32_bits(st->addr[i]));
		if (i >= 2)
			break;

		malidp_write32(reg,
			       BLK_P0_STRIDE + i * LAYER_PER_PLANE_REGS * 4,
			       fb->pitches[i] & 0xFFFF);
	}

	malidp_write32(reg, LAYER_FMT, kfb->format_caps->hw_id);
	malidp_write32(reg, BLK_IN_SIZE, HV_SIZE(st->hsize, st->vsize));

	malidp_write32_mask(reg, BLK_CONTROL, ctrl_mask, ctrl);
}

static struct komeda_component_funcs d71_layer_funcs = {
	.update		= d71_layer_update,
	.disable	= d71_layer_disable,
};

static int d71_layer_init(struct d71_dev *d71,
			  struct block_header *blk, u32 __iomem *reg)
{
	struct komeda_component *c;
	struct komeda_layer *layer;
	u32 pipe_id, layer_id, layer_info;

	get_resources_id(blk->block_info, &pipe_id, &layer_id);
	c = komeda_component_add(&d71->pipes[pipe_id]->base, sizeof(*layer),
				 layer_id,
				 BLOCK_INFO_INPUT_ID(blk->block_info),
				 &d71_layer_funcs, 0,
				 get_valid_inputs(blk),
				 1, reg, "LPU%d_LAYER%d", pipe_id, layer_id);
	if (IS_ERR(c)) {
		DRM_ERROR("Failed to add layer component\n");
		return PTR_ERR(c);
	}

	layer = to_layer(c);
	layer_info = malidp_read32(reg, LAYER_INFO);

	if (layer_info & L_INFO_RF)
		layer->layer_type = KOMEDA_FMT_RICH_LAYER;
	else
		layer->layer_type = KOMEDA_FMT_SIMPLE_LAYER;

	set_range(&layer->hsize_in, 4, d71->max_line_size);
	set_range(&layer->vsize_in, 4, d71->max_vsize);

	malidp_write32(reg, LAYER_PALPHA, D71_PALPHA_DEF_MAP);

	layer->supported_rots = DRM_MODE_ROTATE_MASK | DRM_MODE_REFLECT_MASK;

	return 0;
}

static int d71_wb_layer_init(struct d71_dev *d71,
			     struct block_header *blk, u32 __iomem *reg)
{
	DRM_DEBUG("Detect D71_Wb_Layer.\n");

	return 0;
}

static int d71_compiz_init(struct d71_dev *d71,
			   struct block_header *blk, u32 __iomem *reg)
{
	DRM_DEBUG("Detect D71_compiz.\n");

	return 0;
}

static int d71_improc_init(struct d71_dev *d71,
			   struct block_header *blk, u32 __iomem *reg)
{
	DRM_DEBUG("Detect D71_improc.\n");

	return 0;
}

static int d71_timing_ctrlr_init(struct d71_dev *d71,
				 struct block_header *blk, u32 __iomem *reg)
{
	DRM_DEBUG("Detect D71_timing_ctrlr.\n");

	return 0;
}

int d71_probe_block(struct d71_dev *d71,
		    struct block_header *blk, u32 __iomem *reg)
{
	struct d71_pipeline *pipe;
	int blk_id = BLOCK_INFO_BLK_ID(blk->block_info);

	int err = 0;

	switch (BLOCK_INFO_BLK_TYPE(blk->block_info)) {
	case D71_BLK_TYPE_GCU:
		break;

	case D71_BLK_TYPE_LPU:
		pipe = d71->pipes[blk_id];
		pipe->lpu_addr = reg;
		break;

	case D71_BLK_TYPE_LPU_LAYER:
		err = d71_layer_init(d71, blk, reg);
		break;

	case D71_BLK_TYPE_LPU_WB_LAYER:
		err = d71_wb_layer_init(d71, blk, reg);
		break;

	case D71_BLK_TYPE_CU:
		pipe = d71->pipes[blk_id];
		pipe->cu_addr = reg;
		err = d71_compiz_init(d71, blk, reg);
		break;

	case D71_BLK_TYPE_CU_SPLITTER:
	case D71_BLK_TYPE_CU_SCALER:
	case D71_BLK_TYPE_CU_MERGER:
		break;

	case D71_BLK_TYPE_DOU:
		pipe = d71->pipes[blk_id];
		pipe->dou_addr = reg;
		break;

	case D71_BLK_TYPE_DOU_IPS:
		err = d71_improc_init(d71, blk, reg);
		break;

	case D71_BLK_TYPE_DOU_FT_COEFF:
		pipe = d71->pipes[blk_id];
		pipe->dou_ft_coeff_addr = reg;
		break;

	case D71_BLK_TYPE_DOU_BS:
		err = d71_timing_ctrlr_init(d71, blk, reg);
		break;

	case D71_BLK_TYPE_GLB_LT_COEFF:
		break;

	case D71_BLK_TYPE_GLB_SCL_COEFF:
		d71->glb_scl_coeff_addr[blk_id] = reg;
		break;

	default:
		DRM_ERROR("Unknown block (block_info: 0x%x) is found\n",
			  blk->block_info);
		err = -EINVAL;
		break;
	}

	return err;
}
