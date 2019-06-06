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

static inline u32 to_d71_input_id(struct komeda_component_output *output)
{
	struct komeda_component *comp = output->component;

	return comp ? (comp->hw_id + output->output_port) : 0;
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

static struct komeda_component_funcs d71_layer_funcs = {
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

static int d71_wb_layer_init(struct d71_dev *d71,
			     struct block_header *blk, u32 __iomem *reg)
{
	DRM_DEBUG("Detect D71_Wb_Layer.\n");

	return 0;
}

static void d71_component_disable(struct komeda_component *c)
{
	u32 __iomem *reg = c->reg;
	u32 i;

	malidp_write32(reg, BLK_CONTROL, 0);

	for (i = 0; i < c->max_active_inputs; i++)
		malidp_write32(reg, BLK_INPUT_ID0 + (i << 2), 0);
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
	u32 index, input_hw_id;

	for_each_changed_input(state, index) {
		id_reg = reg + index;
		cfg_reg = reg + index * CU_PER_INPUT_REGS;
		input_hw_id = to_d71_input_id(&state->inputs[index]);
		if (state->active_inputs & BIT(index)) {
			compiz_enable_input(id_reg, cfg_reg,
					    input_hw_id, &st->cins[index]);
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

static struct komeda_component_funcs d71_compiz_funcs = {
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

static void d71_improc_update(struct komeda_component *c,
			      struct komeda_component_state *state)
{
	struct komeda_improc_state *st = to_improc_st(state);
	u32 __iomem *reg = c->reg;
	u32 index, input_hw_id;

	for_each_changed_input(state, index) {
		input_hw_id = state->active_inputs & BIT(index) ?
			      to_d71_input_id(&state->inputs[index]) : 0;
		malidp_write32(reg, BLK_INPUT_ID0 + index * 4, input_hw_id);
	}

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

static struct komeda_component_funcs d71_improc_funcs = {
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
	u32 __iomem *reg = c->reg;
	struct videomode vm;
	u32 value;

	drm_display_mode_to_videomode(&crtc_st->adjusted_mode, &vm);

	malidp_write32(reg, BS_ACTIVESIZE, HV_SIZE(vm.hactive, vm.vactive));
	malidp_write32(reg, BS_HINTERVALS, BS_H_INTVALS(vm.hfront_porch,
							vm.hback_porch));
	malidp_write32(reg, BS_VINTERVALS, BS_V_INTVALS(vm.vfront_porch,
							vm.vback_porch));

	value = BS_SYNC_VSW(vm.vsync_len) | BS_SYNC_HSW(vm.hsync_len);
	value |= vm.flags & DISPLAY_FLAGS_VSYNC_HIGH ? BS_SYNC_VSP : 0;
	value |= vm.flags & DISPLAY_FLAGS_HSYNC_HIGH ? BS_SYNC_HSP : 0;
	malidp_write32(reg, BS_SYNC, value);

	malidp_write32(reg, BS_PROG_LINE, D71_DEFAULT_PREPRETCH_LINE - 1);
	malidp_write32(reg, BS_PREFETCH_LINE, D71_DEFAULT_PREPRETCH_LINE);

	/* configure bs control register */
	value = BS_CTRL_EN | BS_CTRL_VM;

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

static struct komeda_component_funcs d71_timing_ctrlr_funcs = {
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
