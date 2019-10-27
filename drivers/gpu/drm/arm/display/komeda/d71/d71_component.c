// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include "d71_dev.h"
#include "komeda_kms.h"
#include "malidp_io.h"
#include "komeda_framebuffer.h"
#include "komeda_color_mgmt.h"

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

static void get_values_from_reg(void __iomem *reg, u32 offset,
				u32 count, u32 *val)
{
	u32 i, addr;

	for (i = 0; i < count; i++) {
		addr = offset + (i << 2);
		/* 0xA4 is WO register */
		if (addr != 0xA4)
			val[i] = malidp_read32(reg, addr);
		else
			val[i] = 0xDEADDEAD;
	}
}

static void dump_block_header(struct seq_file *sf, void __iomem *reg)
{
	struct block_header hdr;
	u32 i, n_input, n_output;

	d71_read_block_header(reg, &hdr);
	seq_printf(sf, "BLOCK_INFO:\t\t0x%X\n", hdr.block_info);
	seq_printf(sf, "PIPELINE_INFO:\t\t0x%X\n", hdr.pipeline_info);

	n_output = PIPELINE_INFO_N_OUTPUTS(hdr.pipeline_info);
	n_input  = PIPELINE_INFO_N_VALID_INPUTS(hdr.pipeline_info);

	for (i = 0; i < n_input; i++)
		seq_printf(sf, "VALID_INPUT_ID%u:\t0x%X\n",
			   i, hdr.input_ids[i]);

	for (i = 0; i < n_output; i++)
		seq_printf(sf, "OUTPUT_ID%u:\t\t0x%X\n",
			   i, hdr.output_ids[i]);
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

static u32 to_ad_ctrl(u64 modifier)
{
	u32 afbc_ctrl = AD_AEN;

	if (!modifier)
		return 0;

	if ((modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK) ==
	    AFBC_FORMAT_MOD_BLOCK_SIZE_32x8)
		afbc_ctrl |= AD_WB;

	if (modifier & AFBC_FORMAT_MOD_YTR)
		afbc_ctrl |= AD_YT;
	if (modifier & AFBC_FORMAT_MOD_SPLIT)
		afbc_ctrl |= AD_BS;
	if (modifier & AFBC_FORMAT_MOD_TILED)
		afbc_ctrl |= AD_TH;

	return afbc_ctrl;
}

static inline u32 to_d71_input_id(struct komeda_component_state *st, int idx)
{
	struct komeda_component_output *input = &st->inputs[idx];

	/* if input is not active, set hw input_id(0) to disable it */
	if (has_bit(idx, st->active_inputs))
		return input->component->hw_id + input->output_port;
	else
		return 0;
}

static void d71_layer_update_fb(struct komeda_component *c,
				struct komeda_fb *kfb,
				dma_addr_t *addr)
{
	struct drm_framebuffer *fb = &kfb->base;
	const struct drm_format_info *info = fb->format;
	u32 __iomem *reg = c->reg;
	int block_h;

	if (info->num_planes > 2)
		malidp_write64(reg, BLK_P2_PTR_LOW, addr[2]);

	if (info->num_planes > 1) {
		block_h = drm_format_info_block_height(info, 1);
		malidp_write32(reg, BLK_P1_STRIDE, fb->pitches[1] * block_h);
		malidp_write64(reg, BLK_P1_PTR_LOW, addr[1]);
	}

	block_h = drm_format_info_block_height(info, 0);
	malidp_write32(reg, BLK_P0_STRIDE, fb->pitches[0] * block_h);
	malidp_write64(reg, BLK_P0_PTR_LOW, addr[0]);
	malidp_write32(reg, LAYER_FMT, kfb->format_caps->hw_id);
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

	d71_layer_update_fb(c, kfb, st->addr);

	malidp_write32(reg, AD_CONTROL, to_ad_ctrl(fb->modifier));
	if (fb->modifier) {
		u64 addr;

		malidp_write32(reg, LAYER_AD_H_CROP, HV_CROP(st->afbc_crop_l,
							     st->afbc_crop_r));
		malidp_write32(reg, LAYER_AD_V_CROP, HV_CROP(st->afbc_crop_t,
							     st->afbc_crop_b));
		/* afbc 1.2 wants payload, afbc 1.0/1.1 wants end_addr */
		if (fb->modifier & AFBC_FORMAT_MOD_TILED)
			addr = st->addr[0] + kfb->offset_payload;
		else
			addr = st->addr[0] + kfb->afbc_size - 1;

		malidp_write32(reg, BLK_P1_PTR_LOW, lower_32_bits(addr));
		malidp_write32(reg, BLK_P1_PTR_HIGH, upper_32_bits(addr));
	}

	if (fb->format->is_yuv) {
		u32 upsampling = 0;

		switch (kfb->format_caps->fourcc) {
		case DRM_FORMAT_YUYV:
			upsampling = fb->modifier ? LR_CHI422_BILINEAR :
				     LR_CHI422_REPLICATION;
			break;
		case DRM_FORMAT_UYVY:
			upsampling = LR_CHI422_REPLICATION;
			break;
		case DRM_FORMAT_NV12:
		case DRM_FORMAT_YUV420_8BIT:
		case DRM_FORMAT_YUV420_10BIT:
		case DRM_FORMAT_YUV420:
		case DRM_FORMAT_P010:
		/* these fmt support MPGE/JPEG both, here perfer JPEG*/
			upsampling = LR_CHI420_JPEG;
			break;
		case DRM_FORMAT_X0L2:
			upsampling = LR_CHI420_JPEG;
			break;
		default:
			break;
		}

		malidp_write32(reg, LAYER_R_CONTROL, upsampling);
		malidp_write_group(reg, LAYER_YUV_RGB_COEFF0,
				   KOMEDA_N_YUV2RGB_COEFFS,
				   komeda_select_yuv2rgb_coeffs(
					plane_st->color_encoding,
					plane_st->color_range));
	}

	malidp_write32(reg, BLK_IN_SIZE, HV_SIZE(st->hsize, st->vsize));

	if (kfb->is_va)
		ctrl |= L_TBU_EN;
	malidp_write32_mask(reg, BLK_CONTROL, ctrl_mask, ctrl);
}

static void d71_layer_dump(struct komeda_component *c, struct seq_file *sf)
{
	u32 v[15], i;
	bool rich, rgb2rgb;
	char *prefix;

	get_values_from_reg(c->reg, LAYER_INFO, 1, &v[14]);
	if (v[14] & 0x1) {
		rich = true;
		prefix = "LR_";
	} else {
		rich = false;
		prefix = "LS_";
	}

	rgb2rgb = !!(v[14] & L_INFO_CM);

	dump_block_header(sf, c->reg);

	seq_printf(sf, "%sLAYER_INFO:\t\t0x%X\n", prefix, v[14]);

	get_values_from_reg(c->reg, 0xD0, 1, v);
	seq_printf(sf, "%sCONTROL:\t\t0x%X\n", prefix, v[0]);
	if (rich) {
		get_values_from_reg(c->reg, 0xD4, 1, v);
		seq_printf(sf, "LR_RICH_CONTROL:\t0x%X\n", v[0]);
	}
	get_values_from_reg(c->reg, 0xD8, 4, v);
	seq_printf(sf, "%sFORMAT:\t\t0x%X\n", prefix, v[0]);
	seq_printf(sf, "%sIT_COEFFTAB:\t\t0x%X\n", prefix, v[1]);
	seq_printf(sf, "%sIN_SIZE:\t\t0x%X\n", prefix, v[2]);
	seq_printf(sf, "%sPALPHA:\t\t0x%X\n", prefix, v[3]);

	get_values_from_reg(c->reg, 0x100, 3, v);
	seq_printf(sf, "%sP0_PTR_LOW:\t\t0x%X\n", prefix, v[0]);
	seq_printf(sf, "%sP0_PTR_HIGH:\t\t0x%X\n", prefix, v[1]);
	seq_printf(sf, "%sP0_STRIDE:\t\t0x%X\n", prefix, v[2]);

	get_values_from_reg(c->reg, 0x110, 2, v);
	seq_printf(sf, "%sP1_PTR_LOW:\t\t0x%X\n", prefix, v[0]);
	seq_printf(sf, "%sP1_PTR_HIGH:\t\t0x%X\n", prefix, v[1]);
	if (rich) {
		get_values_from_reg(c->reg, 0x118, 1, v);
		seq_printf(sf, "LR_P1_STRIDE:\t\t0x%X\n", v[0]);

		get_values_from_reg(c->reg, 0x120, 2, v);
		seq_printf(sf, "LR_P2_PTR_LOW:\t\t0x%X\n", v[0]);
		seq_printf(sf, "LR_P2_PTR_HIGH:\t\t0x%X\n", v[1]);

		get_values_from_reg(c->reg, 0x130, 12, v);
		for (i = 0; i < 12; i++)
			seq_printf(sf, "LR_YUV_RGB_COEFF%u:\t0x%X\n", i, v[i]);
	}

	if (rgb2rgb) {
		get_values_from_reg(c->reg, LAYER_RGB_RGB_COEFF0, 12, v);
		for (i = 0; i < 12; i++)
			seq_printf(sf, "LS_RGB_RGB_COEFF%u:\t0x%X\n", i, v[i]);
	}

	get_values_from_reg(c->reg, 0x160, 3, v);
	seq_printf(sf, "%sAD_CONTROL:\t\t0x%X\n", prefix, v[0]);
	seq_printf(sf, "%sAD_H_CROP:\t\t0x%X\n", prefix, v[1]);
	seq_printf(sf, "%sAD_V_CROP:\t\t0x%X\n", prefix, v[2]);
}

static const struct komeda_component_funcs d71_layer_funcs = {
	.update		= d71_layer_update,
	.disable	= d71_layer_disable,
	.dump_register	= d71_layer_dump,
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

static void d71_wb_layer_update(struct komeda_component *c,
				struct komeda_component_state *state)
{
	struct komeda_layer_state *st = to_layer_st(state);
	struct drm_connector_state *conn_st = state->wb_conn->state;
	struct komeda_fb *kfb = to_kfb(conn_st->writeback_job->fb);
	u32 ctrl = L_EN | LW_OFM, mask = L_EN | LW_OFM | LW_TBU_EN;
	u32 __iomem *reg = c->reg;

	d71_layer_update_fb(c, kfb, st->addr);

	if (kfb->is_va)
		ctrl |= LW_TBU_EN;

	malidp_write32(reg, BLK_IN_SIZE, HV_SIZE(st->hsize, st->vsize));
	malidp_write32(reg, BLK_INPUT_ID0, to_d71_input_id(state, 0));
	malidp_write32_mask(reg, BLK_CONTROL, mask, ctrl);
}

static void d71_wb_layer_dump(struct komeda_component *c, struct seq_file *sf)
{
	u32 v[12], i;

	dump_block_header(sf, c->reg);

	get_values_from_reg(c->reg, 0x80, 1, v);
	seq_printf(sf, "LW_INPUT_ID0:\t\t0x%X\n", v[0]);

	get_values_from_reg(c->reg, 0xD0, 3, v);
	seq_printf(sf, "LW_CONTROL:\t\t0x%X\n", v[0]);
	seq_printf(sf, "LW_PROG_LINE:\t\t0x%X\n", v[1]);
	seq_printf(sf, "LW_FORMAT:\t\t0x%X\n", v[2]);

	get_values_from_reg(c->reg, 0xE0, 1, v);
	seq_printf(sf, "LW_IN_SIZE:\t\t0x%X\n", v[0]);

	for (i = 0; i < 2; i++) {
		get_values_from_reg(c->reg, 0x100 + i * 0x10, 3, v);
		seq_printf(sf, "LW_P%u_PTR_LOW:\t\t0x%X\n", i, v[0]);
		seq_printf(sf, "LW_P%u_PTR_HIGH:\t\t0x%X\n", i, v[1]);
		seq_printf(sf, "LW_P%u_STRIDE:\t\t0x%X\n", i, v[2]);
	}

	get_values_from_reg(c->reg, 0x130, 12, v);
	for (i = 0; i < 12; i++)
		seq_printf(sf, "LW_RGB_YUV_COEFF%u:\t0x%X\n", i, v[i]);
}

static void d71_wb_layer_disable(struct komeda_component *c)
{
	malidp_write32(c->reg, BLK_INPUT_ID0, 0);
	malidp_write32_mask(c->reg, BLK_CONTROL, L_EN, 0);
}

static const struct komeda_component_funcs d71_wb_layer_funcs = {
	.update		= d71_wb_layer_update,
	.disable	= d71_wb_layer_disable,
	.dump_register	= d71_wb_layer_dump,
};

static int d71_wb_layer_init(struct d71_dev *d71,
			     struct block_header *blk, u32 __iomem *reg)
{
	struct komeda_component *c;
	struct komeda_layer *wb_layer;
	u32 pipe_id, layer_id;

	get_resources_id(blk->block_info, &pipe_id, &layer_id);

	c = komeda_component_add(&d71->pipes[pipe_id]->base, sizeof(*wb_layer),
				 layer_id, BLOCK_INFO_INPUT_ID(blk->block_info),
				 &d71_wb_layer_funcs,
				 1, get_valid_inputs(blk), 0, reg,
				 "LPU%d_LAYER_WR", pipe_id);
	if (IS_ERR(c)) {
		DRM_ERROR("Failed to add wb_layer component\n");
		return PTR_ERR(c);
	}

	wb_layer = to_layer(c);
	wb_layer->layer_type = KOMEDA_FMT_WB_LAYER;

	set_range(&wb_layer->hsize_in, D71_MIN_LINE_SIZE, d71->max_line_size);
	set_range(&wb_layer->vsize_in, D71_MIN_VERTICAL_SIZE, d71->max_vsize);

	return 0;
}

static void d71_component_disable(struct komeda_component *c)
{
	u32 __iomem *reg = c->reg;
	u32 i;

	malidp_write32(reg, BLK_CONTROL, 0);

	for (i = 0; i < c->max_active_inputs; i++) {
		malidp_write32(reg, BLK_INPUT_ID0 + (i << 2), 0);

		/* Besides clearing the input ID to zero, D71 compiz also has
		 * input enable bit in CU_INPUTx_CONTROL which need to be
		 * cleared.
		 */
		if (has_bit(c->id, KOMEDA_PIPELINE_COMPIZS))
			malidp_write32(reg, CU_INPUT0_CONTROL +
				       i * CU_PER_INPUT_REGS * 4,
				       CU_INPUT_CTRL_ALPHA(0xFF));
	}
}

static void compiz_enable_input(u32 __iomem *id_reg,
				u32 __iomem *cfg_reg,
				u32 input_hw_id,
				struct komeda_compiz_input_cfg *cin)
{
	u32 ctrl = CU_INPUT_CTRL_EN;
	u8 blend = cin->pixel_blend_mode;

	if (blend == DRM_MODE_BLEND_PIXEL_NONE)
		ctrl |= CU_INPUT_CTRL_PAD;
	else if (blend == DRM_MODE_BLEND_PREMULTI)
		ctrl |= CU_INPUT_CTRL_PMUL;

	ctrl |= CU_INPUT_CTRL_ALPHA(cin->layer_alpha);

	malidp_write32(id_reg, BLK_INPUT_ID0, input_hw_id);

	malidp_write32(cfg_reg, CU_INPUT0_SIZE,
		       HV_SIZE(cin->hsize, cin->vsize));
	malidp_write32(cfg_reg, CU_INPUT0_OFFSET,
		       HV_OFFSET(cin->hoffset, cin->voffset));
	malidp_write32(cfg_reg, CU_INPUT0_CONTROL, ctrl);
}

static void d71_compiz_update(struct komeda_component *c,
			      struct komeda_component_state *state)
{
	struct komeda_compiz_state *st = to_compiz_st(state);
	u32 __iomem *reg = c->reg;
	u32 __iomem *id_reg, *cfg_reg;
	u32 index;

	for_each_changed_input(state, index) {
		id_reg = reg + index;
		cfg_reg = reg + index * CU_PER_INPUT_REGS;
		if (state->active_inputs & BIT(index)) {
			compiz_enable_input(id_reg, cfg_reg,
					    to_d71_input_id(state, index),
					    &st->cins[index]);
		} else {
			malidp_write32(id_reg, BLK_INPUT_ID0, 0);
			malidp_write32(cfg_reg, CU_INPUT0_CONTROL, 0);
		}
	}

	malidp_write32(reg, BLK_SIZE, HV_SIZE(st->hsize, st->vsize));
}

static void d71_compiz_dump(struct komeda_component *c, struct seq_file *sf)
{
	u32 v[8], i;

	dump_block_header(sf, c->reg);

	get_values_from_reg(c->reg, 0x80, 5, v);
	for (i = 0; i < 5; i++)
		seq_printf(sf, "CU_INPUT_ID%u:\t\t0x%X\n", i, v[i]);

	get_values_from_reg(c->reg, 0xA0, 5, v);
	seq_printf(sf, "CU_IRQ_RAW_STATUS:\t0x%X\n", v[0]);
	seq_printf(sf, "CU_IRQ_CLEAR:\t\t0x%X\n", v[1]);
	seq_printf(sf, "CU_IRQ_MASK:\t\t0x%X\n", v[2]);
	seq_printf(sf, "CU_IRQ_STATUS:\t\t0x%X\n", v[3]);
	seq_printf(sf, "CU_STATUS:\t\t0x%X\n", v[4]);

	get_values_from_reg(c->reg, 0xD0, 2, v);
	seq_printf(sf, "CU_CONTROL:\t\t0x%X\n", v[0]);
	seq_printf(sf, "CU_SIZE:\t\t0x%X\n", v[1]);

	get_values_from_reg(c->reg, 0xDC, 1, v);
	seq_printf(sf, "CU_BG_COLOR:\t\t0x%X\n", v[0]);

	for (i = 0, v[4] = 0xE0; i < 5; i++, v[4] += 0x10) {
		get_values_from_reg(c->reg, v[4], 3, v);
		seq_printf(sf, "CU_INPUT%u_SIZE:\t\t0x%X\n", i, v[0]);
		seq_printf(sf, "CU_INPUT%u_OFFSET:\t0x%X\n", i, v[1]);
		seq_printf(sf, "CU_INPUT%u_CONTROL:\t0x%X\n", i, v[2]);
	}

	get_values_from_reg(c->reg, 0x130, 2, v);
	seq_printf(sf, "CU_USER_LOW:\t\t0x%X\n", v[0]);
	seq_printf(sf, "CU_USER_HIGH:\t\t0x%X\n", v[1]);
}

static const struct komeda_component_funcs d71_compiz_funcs = {
	.update		= d71_compiz_update,
	.disable	= d71_component_disable,
	.dump_register	= d71_compiz_dump,
};

static int d71_compiz_init(struct d71_dev *d71,
			   struct block_header *blk, u32 __iomem *reg)
{
	struct komeda_component *c;
	struct komeda_compiz *compiz;
	u32 pipe_id, comp_id;

	get_resources_id(blk->block_info, &pipe_id, &comp_id);

	c = komeda_component_add(&d71->pipes[pipe_id]->base, sizeof(*compiz),
				 comp_id,
				 BLOCK_INFO_INPUT_ID(blk->block_info),
				 &d71_compiz_funcs,
				 CU_NUM_INPUT_IDS, get_valid_inputs(blk),
				 CU_NUM_OUTPUT_IDS, reg,
				 "CU%d", pipe_id);
	if (IS_ERR(c))
		return PTR_ERR(c);

	compiz = to_compiz(c);

	set_range(&compiz->hsize, D71_MIN_LINE_SIZE, d71->max_line_size);
	set_range(&compiz->vsize, D71_MIN_VERTICAL_SIZE, d71->max_vsize);

	return 0;
}

static void d71_scaler_update_filter_lut(u32 __iomem *reg, u32 hsize_in,
					 u32 vsize_in, u32 hsize_out,
					 u32 vsize_out)
{
	u32 val = 0;

	if (hsize_in <= hsize_out)
		val  |= 0x62;
	else if (hsize_in <= (hsize_out + hsize_out / 2))
		val |= 0x63;
	else if (hsize_in <= hsize_out * 2)
		val |= 0x64;
	else if (hsize_in <= hsize_out * 2 + (hsize_out * 3) / 4)
		val |= 0x65;
	else
		val |= 0x66;

	if (vsize_in <= vsize_out)
		val  |= SC_VTSEL(0x6A);
	else if (vsize_in <= (vsize_out + vsize_out / 2))
		val |= SC_VTSEL(0x6B);
	else if (vsize_in <= vsize_out * 2)
		val |= SC_VTSEL(0x6C);
	else if (vsize_in <= vsize_out * 2 + vsize_out * 3 / 4)
		val |= SC_VTSEL(0x6D);
	else
		val |= SC_VTSEL(0x6E);

	malidp_write32(reg, SC_COEFFTAB, val);
}

static void d71_scaler_update(struct komeda_component *c,
			      struct komeda_component_state *state)
{
	struct komeda_scaler_state *st = to_scaler_st(state);
	u32 __iomem *reg = c->reg;
	u32 init_ph, delta_ph, ctrl;

	d71_scaler_update_filter_lut(reg, st->hsize_in, st->vsize_in,
				     st->hsize_out, st->vsize_out);

	malidp_write32(reg, BLK_IN_SIZE, HV_SIZE(st->hsize_in, st->vsize_in));
	malidp_write32(reg, SC_OUT_SIZE, HV_SIZE(st->hsize_out, st->vsize_out));
	malidp_write32(reg, SC_H_CROP, HV_CROP(st->left_crop, st->right_crop));

	/* for right part, HW only sample the valid pixel which means the pixels
	 * in left_crop will be jumpped, and the first sample pixel is:
	 *
	 * dst_a = st->total_hsize_out - st->hsize_out + st->left_crop + 0.5;
	 *
	 * Then the corresponding texel in src is:
	 *
	 * h_delta_phase = st->total_hsize_in / st->total_hsize_out;
	 * src_a = dst_A * h_delta_phase;
	 *
	 * and h_init_phase is src_a deduct the real source start src_S;
	 *
	 * src_S = st->total_hsize_in - st->hsize_in;
	 * h_init_phase = src_a - src_S;
	 *
	 * And HW precision for the initial/delta_phase is 16:16 fixed point,
	 * the following is the simplified formula
	 */
	if (st->right_part) {
		u32 dst_a = st->total_hsize_out - st->hsize_out + st->left_crop;

		if (st->en_img_enhancement)
			dst_a -= 1;

		init_ph = ((st->total_hsize_in * (2 * dst_a + 1) -
			    2 * st->total_hsize_out * (st->total_hsize_in -
			    st->hsize_in)) << 15) / st->total_hsize_out;
	} else {
		init_ph = (st->total_hsize_in << 15) / st->total_hsize_out;
	}

	malidp_write32(reg, SC_H_INIT_PH, init_ph);

	delta_ph = (st->total_hsize_in << 16) / st->total_hsize_out;
	malidp_write32(reg, SC_H_DELTA_PH, delta_ph);

	init_ph = (st->total_vsize_in << 15) / st->vsize_out;
	malidp_write32(reg, SC_V_INIT_PH, init_ph);

	delta_ph = (st->total_vsize_in << 16) / st->vsize_out;
	malidp_write32(reg, SC_V_DELTA_PH, delta_ph);

	ctrl = 0;
	ctrl |= st->en_scaling ? SC_CTRL_SCL : 0;
	ctrl |= st->en_alpha ? SC_CTRL_AP : 0;
	ctrl |= st->en_img_enhancement ? SC_CTRL_IENH : 0;
	/* If we use the hardware splitter we shouldn't set SC_CTRL_LS */
	if (st->en_split &&
	    state->inputs[0].component->id != KOMEDA_COMPONENT_SPLITTER)
		ctrl |= SC_CTRL_LS;

	malidp_write32(reg, BLK_CONTROL, ctrl);
	malidp_write32(reg, BLK_INPUT_ID0, to_d71_input_id(state, 0));
}

static void d71_scaler_dump(struct komeda_component *c, struct seq_file *sf)
{
	u32 v[9];

	dump_block_header(sf, c->reg);

	get_values_from_reg(c->reg, 0x80, 1, v);
	seq_printf(sf, "SC_INPUT_ID0:\t\t0x%X\n", v[0]);

	get_values_from_reg(c->reg, 0xD0, 1, v);
	seq_printf(sf, "SC_CONTROL:\t\t0x%X\n", v[0]);

	get_values_from_reg(c->reg, 0xDC, 9, v);
	seq_printf(sf, "SC_COEFFTAB:\t\t0x%X\n", v[0]);
	seq_printf(sf, "SC_IN_SIZE:\t\t0x%X\n", v[1]);
	seq_printf(sf, "SC_OUT_SIZE:\t\t0x%X\n", v[2]);
	seq_printf(sf, "SC_H_CROP:\t\t0x%X\n", v[3]);
	seq_printf(sf, "SC_V_CROP:\t\t0x%X\n", v[4]);
	seq_printf(sf, "SC_H_INIT_PH:\t\t0x%X\n", v[5]);
	seq_printf(sf, "SC_H_DELTA_PH:\t\t0x%X\n", v[6]);
	seq_printf(sf, "SC_V_INIT_PH:\t\t0x%X\n", v[7]);
	seq_printf(sf, "SC_V_DELTA_PH:\t\t0x%X\n", v[8]);
}

static const struct komeda_component_funcs d71_scaler_funcs = {
	.update		= d71_scaler_update,
	.disable	= d71_component_disable,
	.dump_register	= d71_scaler_dump,
};

static int d71_scaler_init(struct d71_dev *d71,
			   struct block_header *blk, u32 __iomem *reg)
{
	struct komeda_component *c;
	struct komeda_scaler *scaler;
	u32 pipe_id, comp_id;

	get_resources_id(blk->block_info, &pipe_id, &comp_id);

	c = komeda_component_add(&d71->pipes[pipe_id]->base, sizeof(*scaler),
				 comp_id, BLOCK_INFO_INPUT_ID(blk->block_info),
				 &d71_scaler_funcs,
				 1, get_valid_inputs(blk), 1, reg,
				 "CU%d_SCALER%d",
				 pipe_id, BLOCK_INFO_BLK_ID(blk->block_info));

	if (IS_ERR(c)) {
		DRM_ERROR("Failed to initialize scaler");
		return PTR_ERR(c);
	}

	scaler = to_scaler(c);
	set_range(&scaler->hsize, 4, 2048);
	set_range(&scaler->vsize, 4, 4096);
	scaler->max_downscaling = 6;
	scaler->max_upscaling = 64;
	scaler->scaling_split_overlap = 8;
	scaler->enh_split_overlap = 1;

	malidp_write32(c->reg, BLK_CONTROL, 0);

	return 0;
}

static int d71_downscaling_clk_check(struct komeda_pipeline *pipe,
				     struct drm_display_mode *mode,
				     unsigned long aclk_rate,
				     struct komeda_data_flow_cfg *dflow)
{
	u32 h_in = dflow->in_w;
	u32 v_in = dflow->in_h;
	u32 v_out = dflow->out_h;
	u64 fraction, denominator;

	/* D71 downscaling must satisfy the following equation
	 *
	 *   ACLK                   h_in * v_in
	 * ------- >= ---------------------------------------------
	 *  PXLCLK     (h_total - (1 + 2 * v_in / v_out)) * v_out
	 *
	 * In only horizontal downscaling situation, the right side should be
	 * multiplied by (h_total - 3) / (h_active - 3), then equation becomes
	 *
	 *   ACLK          h_in
	 * ------- >= ----------------
	 *  PXLCLK     (h_active - 3)
	 *
	 * To avoid precision lost the equation 1 will be convert to:
	 *
	 *   ACLK             h_in * v_in
	 * ------- >= -----------------------------------
	 *  PXLCLK     (h_total -1 ) * v_out -  2 * v_in
	 */
	if (v_in == v_out) {
		fraction = h_in;
		denominator = mode->hdisplay - 3;
	} else {
		fraction = h_in * v_in;
		denominator = (mode->htotal - 1) * v_out -  2 * v_in;
	}

	return aclk_rate * denominator >= mode->crtc_clock * 1000 * fraction ?
	       0 : -EINVAL;
}

static void d71_splitter_update(struct komeda_component *c,
				struct komeda_component_state *state)
{
	struct komeda_splitter_state *st = to_splitter_st(state);
	u32 __iomem *reg = c->reg;

	malidp_write32(reg, BLK_INPUT_ID0, to_d71_input_id(state, 0));
	malidp_write32(reg, BLK_SIZE, HV_SIZE(st->hsize, st->vsize));
	malidp_write32(reg, SP_OVERLAP_SIZE, st->overlap & 0x1FFF);
	malidp_write32(reg, BLK_CONTROL, BLK_CTRL_EN);
}

static void d71_splitter_dump(struct komeda_component *c, struct seq_file *sf)
{
	u32 v[3];

	dump_block_header(sf, c->reg);

	get_values_from_reg(c->reg, BLK_INPUT_ID0, 1, v);
	seq_printf(sf, "SP_INPUT_ID0:\t\t0x%X\n", v[0]);

	get_values_from_reg(c->reg, BLK_CONTROL, 3, v);
	seq_printf(sf, "SP_CONTROL:\t\t0x%X\n", v[0]);
	seq_printf(sf, "SP_SIZE:\t\t0x%X\n", v[1]);
	seq_printf(sf, "SP_OVERLAP_SIZE:\t0x%X\n", v[2]);
}

static const struct komeda_component_funcs d71_splitter_funcs = {
	.update		= d71_splitter_update,
	.disable	= d71_component_disable,
	.dump_register	= d71_splitter_dump,
};

static int d71_splitter_init(struct d71_dev *d71,
			     struct block_header *blk, u32 __iomem *reg)
{
	struct komeda_component *c;
	struct komeda_splitter *splitter;
	u32 pipe_id, comp_id;

	get_resources_id(blk->block_info, &pipe_id, &comp_id);

	c = komeda_component_add(&d71->pipes[pipe_id]->base, sizeof(*splitter),
				 comp_id,
				 BLOCK_INFO_INPUT_ID(blk->block_info),
				 &d71_splitter_funcs,
				 1, get_valid_inputs(blk), 2, reg,
				 "CU%d_SPLITTER", pipe_id);

	if (IS_ERR(c)) {
		DRM_ERROR("Failed to initialize splitter");
		return -1;
	}

	splitter = to_splitter(c);

	set_range(&splitter->hsize, 4, d71->max_line_size);
	set_range(&splitter->vsize, 4, d71->max_vsize);

	return 0;
}

static void d71_merger_update(struct komeda_component *c,
			      struct komeda_component_state *state)
{
	struct komeda_merger_state *st = to_merger_st(state);
	u32 __iomem *reg = c->reg;
	u32 index;

	for_each_changed_input(state, index)
		malidp_write32(reg, MG_INPUT_ID0 + index * 4,
			       to_d71_input_id(state, index));

	malidp_write32(reg, MG_SIZE, HV_SIZE(st->hsize_merged,
					     st->vsize_merged));
	malidp_write32(reg, BLK_CONTROL, BLK_CTRL_EN);
}

static void d71_merger_dump(struct komeda_component *c, struct seq_file *sf)
{
	u32 v;

	dump_block_header(sf, c->reg);

	get_values_from_reg(c->reg, MG_INPUT_ID0, 1, &v);
	seq_printf(sf, "MG_INPUT_ID0:\t\t0x%X\n", v);

	get_values_from_reg(c->reg, MG_INPUT_ID1, 1, &v);
	seq_printf(sf, "MG_INPUT_ID1:\t\t0x%X\n", v);

	get_values_from_reg(c->reg, BLK_CONTROL, 1, &v);
	seq_printf(sf, "MG_CONTROL:\t\t0x%X\n", v);

	get_values_from_reg(c->reg, MG_SIZE, 1, &v);
	seq_printf(sf, "MG_SIZE:\t\t0x%X\n", v);
}

static const struct komeda_component_funcs d71_merger_funcs = {
	.update		= d71_merger_update,
	.disable	= d71_component_disable,
	.dump_register	= d71_merger_dump,
};

static int d71_merger_init(struct d71_dev *d71,
			   struct block_header *blk, u32 __iomem *reg)
{
	struct komeda_component *c;
	struct komeda_merger *merger;
	u32 pipe_id, comp_id;

	get_resources_id(blk->block_info, &pipe_id, &comp_id);

	c = komeda_component_add(&d71->pipes[pipe_id]->base, sizeof(*merger),
				 comp_id,
				 BLOCK_INFO_INPUT_ID(blk->block_info),
				 &d71_merger_funcs,
				 MG_NUM_INPUTS_IDS, get_valid_inputs(blk),
				 MG_NUM_OUTPUTS_IDS, reg,
				 "CU%d_MERGER", pipe_id);

	if (IS_ERR(c)) {
		DRM_ERROR("Failed to initialize merger.\n");
		return PTR_ERR(c);
	}

	merger = to_merger(c);

	set_range(&merger->hsize_merged, 4, 4032);
	set_range(&merger->vsize_merged, 4, 4096);

	return 0;
}

static void d71_improc_update(struct komeda_component *c,
			      struct komeda_component_state *state)
{
	struct komeda_improc_state *st = to_improc_st(state);
	u32 __iomem *reg = c->reg;
	u32 index;

	for_each_changed_input(state, index)
		malidp_write32(reg, BLK_INPUT_ID0 + index * 4,
			       to_d71_input_id(state, index));

	malidp_write32(reg, BLK_SIZE, HV_SIZE(st->hsize, st->vsize));
}

static void d71_improc_dump(struct komeda_component *c, struct seq_file *sf)
{
	u32 v[12], i;

	dump_block_header(sf, c->reg);

	get_values_from_reg(c->reg, 0x80, 2, v);
	seq_printf(sf, "IPS_INPUT_ID0:\t\t0x%X\n", v[0]);
	seq_printf(sf, "IPS_INPUT_ID1:\t\t0x%X\n", v[1]);

	get_values_from_reg(c->reg, 0xC0, 1, v);
	seq_printf(sf, "IPS_INFO:\t\t0x%X\n", v[0]);

	get_values_from_reg(c->reg, 0xD0, 3, v);
	seq_printf(sf, "IPS_CONTROL:\t\t0x%X\n", v[0]);
	seq_printf(sf, "IPS_SIZE:\t\t0x%X\n", v[1]);
	seq_printf(sf, "IPS_DEPTH:\t\t0x%X\n", v[2]);

	get_values_from_reg(c->reg, 0x130, 12, v);
	for (i = 0; i < 12; i++)
		seq_printf(sf, "IPS_RGB_RGB_COEFF%u:\t0x%X\n", i, v[i]);

	get_values_from_reg(c->reg, 0x170, 12, v);
	for (i = 0; i < 12; i++)
		seq_printf(sf, "IPS_RGB_YUV_COEFF%u:\t0x%X\n", i, v[i]);
}

static const struct komeda_component_funcs d71_improc_funcs = {
	.update		= d71_improc_update,
	.disable	= d71_component_disable,
	.dump_register	= d71_improc_dump,
};

static int d71_improc_init(struct d71_dev *d71,
			   struct block_header *blk, u32 __iomem *reg)
{
	struct komeda_component *c;
	struct komeda_improc *improc;
	u32 pipe_id, comp_id, value;

	get_resources_id(blk->block_info, &pipe_id, &comp_id);

	c = komeda_component_add(&d71->pipes[pipe_id]->base, sizeof(*improc),
				 comp_id,
				 BLOCK_INFO_INPUT_ID(blk->block_info),
				 &d71_improc_funcs, IPS_NUM_INPUT_IDS,
				 get_valid_inputs(blk),
				 IPS_NUM_OUTPUT_IDS, reg, "DOU%d_IPS", pipe_id);
	if (IS_ERR(c)) {
		DRM_ERROR("Failed to add improc component\n");
		return PTR_ERR(c);
	}

	improc = to_improc(c);
	improc->supported_color_depths = BIT(8) | BIT(10);
	improc->supported_color_formats = DRM_COLOR_FORMAT_RGB444 |
					  DRM_COLOR_FORMAT_YCRCB444 |
					  DRM_COLOR_FORMAT_YCRCB422;
	value = malidp_read32(reg, BLK_INFO);
	if (value & IPS_INFO_CHD420)
		improc->supported_color_formats |= DRM_COLOR_FORMAT_YCRCB420;

	improc->supports_csc = true;
	improc->supports_gamma = true;

	return 0;
}

static void d71_timing_ctrlr_disable(struct komeda_component *c)
{
	malidp_write32_mask(c->reg, BLK_CONTROL, BS_CTRL_EN, 0);
}

static void d71_timing_ctrlr_update(struct komeda_component *c,
				    struct komeda_component_state *state)
{
	struct drm_crtc_state *crtc_st = state->crtc->state;
	struct drm_display_mode *mode = &crtc_st->adjusted_mode;
	u32 __iomem *reg = c->reg;
	u32 hactive, hfront_porch, hback_porch, hsync_len;
	u32 vactive, vfront_porch, vback_porch, vsync_len;
	u32 value;

	hactive = mode->crtc_hdisplay;
	hfront_porch = mode->crtc_hsync_start - mode->crtc_hdisplay;
	hsync_len = mode->crtc_hsync_end - mode->crtc_hsync_start;
	hback_porch = mode->crtc_htotal - mode->crtc_hsync_end;

	vactive = mode->crtc_vdisplay;
	vfront_porch = mode->crtc_vsync_start - mode->crtc_vdisplay;
	vsync_len = mode->crtc_vsync_end - mode->crtc_vsync_start;
	vback_porch = mode->crtc_vtotal - mode->crtc_vsync_end;

	malidp_write32(reg, BS_ACTIVESIZE, HV_SIZE(hactive, vactive));
	malidp_write32(reg, BS_HINTERVALS, BS_H_INTVALS(hfront_porch,
							hback_porch));
	malidp_write32(reg, BS_VINTERVALS, BS_V_INTVALS(vfront_porch,
							vback_porch));

	value = BS_SYNC_VSW(vsync_len) | BS_SYNC_HSW(hsync_len);
	value |= mode->flags & DRM_MODE_FLAG_PVSYNC ? BS_SYNC_VSP : 0;
	value |= mode->flags & DRM_MODE_FLAG_PHSYNC ? BS_SYNC_HSP : 0;
	malidp_write32(reg, BS_SYNC, value);

	malidp_write32(reg, BS_PROG_LINE, D71_DEFAULT_PREPRETCH_LINE - 1);
	malidp_write32(reg, BS_PREFETCH_LINE, D71_DEFAULT_PREPRETCH_LINE);

	/* configure bs control register */
	value = BS_CTRL_EN | BS_CTRL_VM;
	if (c->pipeline->dual_link) {
		malidp_write32(reg, BS_DRIFT_TO, hfront_porch + 16);
		value |= BS_CTRL_DL;
	}

	malidp_write32(reg, BLK_CONTROL, value);
}

static void d71_timing_ctrlr_dump(struct komeda_component *c,
				  struct seq_file *sf)
{
	u32 v[8], i;

	dump_block_header(sf, c->reg);

	get_values_from_reg(c->reg, 0xC0, 1, v);
	seq_printf(sf, "BS_INFO:\t\t0x%X\n", v[0]);

	get_values_from_reg(c->reg, 0xD0, 8, v);
	seq_printf(sf, "BS_CONTROL:\t\t0x%X\n", v[0]);
	seq_printf(sf, "BS_PROG_LINE:\t\t0x%X\n", v[1]);
	seq_printf(sf, "BS_PREFETCH_LINE:\t0x%X\n", v[2]);
	seq_printf(sf, "BS_BG_COLOR:\t\t0x%X\n", v[3]);
	seq_printf(sf, "BS_ACTIVESIZE:\t\t0x%X\n", v[4]);
	seq_printf(sf, "BS_HINTERVALS:\t\t0x%X\n", v[5]);
	seq_printf(sf, "BS_VINTERVALS:\t\t0x%X\n", v[6]);
	seq_printf(sf, "BS_SYNC:\t\t0x%X\n", v[7]);

	get_values_from_reg(c->reg, 0x100, 3, v);
	seq_printf(sf, "BS_DRIFT_TO:\t\t0x%X\n", v[0]);
	seq_printf(sf, "BS_FRAME_TO:\t\t0x%X\n", v[1]);
	seq_printf(sf, "BS_TE_TO:\t\t0x%X\n", v[2]);

	get_values_from_reg(c->reg, 0x110, 3, v);
	for (i = 0; i < 3; i++)
		seq_printf(sf, "BS_T%u_INTERVAL:\t\t0x%X\n", i, v[i]);

	get_values_from_reg(c->reg, 0x120, 5, v);
	for (i = 0; i < 2; i++) {
		seq_printf(sf, "BS_CRC%u_LOW:\t\t0x%X\n", i, v[i << 1]);
		seq_printf(sf, "BS_CRC%u_HIGH:\t\t0x%X\n", i, v[(i << 1) + 1]);
	}
	seq_printf(sf, "BS_USER:\t\t0x%X\n", v[4]);
}

static const struct komeda_component_funcs d71_timing_ctrlr_funcs = {
	.update		= d71_timing_ctrlr_update,
	.disable	= d71_timing_ctrlr_disable,
	.dump_register	= d71_timing_ctrlr_dump,
};

static int d71_timing_ctrlr_init(struct d71_dev *d71,
				 struct block_header *blk, u32 __iomem *reg)
{
	struct komeda_component *c;
	struct komeda_timing_ctrlr *ctrlr;
	u32 pipe_id, comp_id;

	get_resources_id(blk->block_info, &pipe_id, &comp_id);

	c = komeda_component_add(&d71->pipes[pipe_id]->base, sizeof(*ctrlr),
				 KOMEDA_COMPONENT_TIMING_CTRLR,
				 BLOCK_INFO_INPUT_ID(blk->block_info),
				 &d71_timing_ctrlr_funcs,
				 1, BIT(KOMEDA_COMPONENT_IPS0 + pipe_id),
				 BS_NUM_OUTPUT_IDS, reg, "DOU%d_BS", pipe_id);
	if (IS_ERR(c)) {
		DRM_ERROR("Failed to add display_ctrl component\n");
		return PTR_ERR(c);
	}

	ctrlr = to_ctrlr(c);

	ctrlr->supports_dual_link = true;

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

	case D71_BLK_TYPE_CU_SCALER:
		err = d71_scaler_init(d71, blk, reg);
		break;

	case D71_BLK_TYPE_CU_SPLITTER:
		err = d71_splitter_init(d71, blk, reg);
		break;

	case D71_BLK_TYPE_CU_MERGER:
		err = d71_merger_init(d71, blk, reg);
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

const struct komeda_pipeline_funcs d71_pipeline_funcs = {
	.downscaling_clk_check = d71_downscaling_clk_check,
};
