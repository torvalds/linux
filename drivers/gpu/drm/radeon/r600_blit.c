/*
 * Copyright 2009 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *     Alex Deucher <alexander.deucher@amd.com>
 */
#include "drmP.h"
#include "drm.h"
#include "radeon_drm.h"
#include "radeon_drv.h"

#include "r600_blit_shaders.h"

#define DI_PT_RECTLIST        0x11
#define DI_INDEX_SIZE_16_BIT  0x0
#define DI_SRC_SEL_AUTO_INDEX 0x2

#define FMT_8                 0x1
#define FMT_5_6_5             0x8
#define FMT_8_8_8_8           0x1a
#define COLOR_8               0x1
#define COLOR_5_6_5           0x8
#define COLOR_8_8_8_8         0x1a

static void
set_render_target(drm_radeon_private_t *dev_priv, int format, int w, int h, u64 gpu_addr)
{
	u32 cb_color_info;
	int pitch, slice;
	RING_LOCALS;
	DRM_DEBUG("\n");

	h = ALIGN(h, 8);
	if (h < 8)
		h = 8;

	cb_color_info = ((format << 2) | (1 << 27));
	pitch = (w / 8) - 1;
	slice = ((w * h) / 64) - 1;

	if (((dev_priv->flags & RADEON_FAMILY_MASK) > CHIP_R600) &&
	    ((dev_priv->flags & RADEON_FAMILY_MASK) < CHIP_RV770)) {
		BEGIN_RING(21 + 2);
		OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
		OUT_RING((R600_CB_COLOR0_BASE - R600_SET_CONTEXT_REG_OFFSET) >> 2);
		OUT_RING(gpu_addr >> 8);
		OUT_RING(CP_PACKET3(R600_IT_SURFACE_BASE_UPDATE, 0));
		OUT_RING(2 << 0);
	} else {
		BEGIN_RING(21);
		OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
		OUT_RING((R600_CB_COLOR0_BASE - R600_SET_CONTEXT_REG_OFFSET) >> 2);
		OUT_RING(gpu_addr >> 8);
	}

	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
	OUT_RING((R600_CB_COLOR0_SIZE - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING((pitch << 0) | (slice << 10));

	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
	OUT_RING((R600_CB_COLOR0_VIEW - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING(0);

	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
	OUT_RING((R600_CB_COLOR0_INFO - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING(cb_color_info);

	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
	OUT_RING((R600_CB_COLOR0_TILE - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING(0);

	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
	OUT_RING((R600_CB_COLOR0_FRAG - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING(0);

	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
	OUT_RING((R600_CB_COLOR0_MASK - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING(0);

	ADVANCE_RING();
}

static void
cp_set_surface_sync(drm_radeon_private_t *dev_priv,
		    u32 sync_type, u32 size, u64 mc_addr)
{
	u32 cp_coher_size;
	RING_LOCALS;
	DRM_DEBUG("\n");

	if (size == 0xffffffff)
		cp_coher_size = 0xffffffff;
	else
		cp_coher_size = ((size + 255) >> 8);

	BEGIN_RING(5);
	OUT_RING(CP_PACKET3(R600_IT_SURFACE_SYNC, 3));
	OUT_RING(sync_type);
	OUT_RING(cp_coher_size);
	OUT_RING((mc_addr >> 8));
	OUT_RING(10); /* poll interval */
	ADVANCE_RING();
}

static void
set_shaders(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	u64 gpu_addr;
	int i;
	u32 *vs, *ps;
	uint32_t sq_pgm_resources;
	RING_LOCALS;
	DRM_DEBUG("\n");

	/* load shaders */
	vs = (u32 *) ((char *)dev->agp_buffer_map->handle + dev_priv->blit_vb->offset);
	ps = (u32 *) ((char *)dev->agp_buffer_map->handle + dev_priv->blit_vb->offset + 256);

	for (i = 0; i < r6xx_vs_size; i++)
		vs[i] = cpu_to_le32(r6xx_vs[i]);
	for (i = 0; i < r6xx_ps_size; i++)
		ps[i] = cpu_to_le32(r6xx_ps[i]);

	dev_priv->blit_vb->used = 512;

	gpu_addr = dev_priv->gart_buffers_offset + dev_priv->blit_vb->offset;

	/* setup shader regs */
	sq_pgm_resources = (1 << 0);

	BEGIN_RING(9 + 12);
	/* VS */
	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
	OUT_RING((R600_SQ_PGM_START_VS - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING(gpu_addr >> 8);

	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
	OUT_RING((R600_SQ_PGM_RESOURCES_VS - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING(sq_pgm_resources);

	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
	OUT_RING((R600_SQ_PGM_CF_OFFSET_VS - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING(0);

	/* PS */
	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
	OUT_RING((R600_SQ_PGM_START_PS - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING((gpu_addr + 256) >> 8);

	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
	OUT_RING((R600_SQ_PGM_RESOURCES_PS - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING(sq_pgm_resources | (1 << 28));

	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
	OUT_RING((R600_SQ_PGM_EXPORTS_PS - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING(2);

	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 1));
	OUT_RING((R600_SQ_PGM_CF_OFFSET_PS - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING(0);
	ADVANCE_RING();

	cp_set_surface_sync(dev_priv,
			    R600_SH_ACTION_ENA, 512, gpu_addr);
}

static void
set_vtx_resource(drm_radeon_private_t *dev_priv, u64 gpu_addr)
{
	uint32_t sq_vtx_constant_word2;
	RING_LOCALS;
	DRM_DEBUG("\n");

	sq_vtx_constant_word2 = (((gpu_addr >> 32) & 0xff) | (16 << 8));
#ifdef __BIG_ENDIAN
	sq_vtx_constant_word2 |= (2 << 30);
#endif

	BEGIN_RING(9);
	OUT_RING(CP_PACKET3(R600_IT_SET_RESOURCE, 7));
	OUT_RING(0x460);
	OUT_RING(gpu_addr & 0xffffffff);
	OUT_RING(48 - 1);
	OUT_RING(sq_vtx_constant_word2);
	OUT_RING(1 << 0);
	OUT_RING(0);
	OUT_RING(0);
	OUT_RING(R600_SQ_TEX_VTX_VALID_BUFFER << 30);
	ADVANCE_RING();

	if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV610) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV620) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS780) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS880) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV710))
		cp_set_surface_sync(dev_priv,
				    R600_TC_ACTION_ENA, 48, gpu_addr);
	else
		cp_set_surface_sync(dev_priv,
				    R600_VC_ACTION_ENA, 48, gpu_addr);
}

static void
set_tex_resource(drm_radeon_private_t *dev_priv,
		 int format, int w, int h, int pitch, u64 gpu_addr)
{
	uint32_t sq_tex_resource_word0, sq_tex_resource_word1, sq_tex_resource_word4;
	RING_LOCALS;
	DRM_DEBUG("\n");

	if (h < 1)
		h = 1;

	sq_tex_resource_word0 = (1 << 0);
	sq_tex_resource_word0 |= ((((pitch >> 3) - 1) << 8) |
				  ((w - 1) << 19));

	sq_tex_resource_word1 = (format << 26);
	sq_tex_resource_word1 |= ((h - 1) << 0);

	sq_tex_resource_word4 = ((1 << 14) |
				 (0 << 16) |
				 (1 << 19) |
				 (2 << 22) |
				 (3 << 25));

	BEGIN_RING(9);
	OUT_RING(CP_PACKET3(R600_IT_SET_RESOURCE, 7));
	OUT_RING(0);
	OUT_RING(sq_tex_resource_word0);
	OUT_RING(sq_tex_resource_word1);
	OUT_RING(gpu_addr >> 8);
	OUT_RING(gpu_addr >> 8);
	OUT_RING(sq_tex_resource_word4);
	OUT_RING(0);
	OUT_RING(R600_SQ_TEX_VTX_VALID_TEXTURE << 30);
	ADVANCE_RING();

}

static void
set_scissors(drm_radeon_private_t *dev_priv, int x1, int y1, int x2, int y2)
{
	RING_LOCALS;
	DRM_DEBUG("\n");

	BEGIN_RING(12);
	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 2));
	OUT_RING((R600_PA_SC_SCREEN_SCISSOR_TL - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING((x1 << 0) | (y1 << 16));
	OUT_RING((x2 << 0) | (y2 << 16));

	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 2));
	OUT_RING((R600_PA_SC_GENERIC_SCISSOR_TL - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING((x1 << 0) | (y1 << 16) | (1 << 31));
	OUT_RING((x2 << 0) | (y2 << 16));

	OUT_RING(CP_PACKET3(R600_IT_SET_CONTEXT_REG, 2));
	OUT_RING((R600_PA_SC_WINDOW_SCISSOR_TL - R600_SET_CONTEXT_REG_OFFSET) >> 2);
	OUT_RING((x1 << 0) | (y1 << 16) | (1 << 31));
	OUT_RING((x2 << 0) | (y2 << 16));
	ADVANCE_RING();
}

static void
draw_auto(drm_radeon_private_t *dev_priv)
{
	RING_LOCALS;
	DRM_DEBUG("\n");

	BEGIN_RING(10);
	OUT_RING(CP_PACKET3(R600_IT_SET_CONFIG_REG, 1));
	OUT_RING((R600_VGT_PRIMITIVE_TYPE - R600_SET_CONFIG_REG_OFFSET) >> 2);
	OUT_RING(DI_PT_RECTLIST);

	OUT_RING(CP_PACKET3(R600_IT_INDEX_TYPE, 0));
#ifdef __BIG_ENDIAN
	OUT_RING((2 << 2) | DI_INDEX_SIZE_16_BIT);
#else
	OUT_RING(DI_INDEX_SIZE_16_BIT);
#endif

	OUT_RING(CP_PACKET3(R600_IT_NUM_INSTANCES, 0));
	OUT_RING(1);

	OUT_RING(CP_PACKET3(R600_IT_DRAW_INDEX_AUTO, 1));
	OUT_RING(3);
	OUT_RING(DI_SRC_SEL_AUTO_INDEX);

	ADVANCE_RING();
	COMMIT_RING();
}

static void
set_default_state(drm_radeon_private_t *dev_priv)
{
	int i;
	u32 sq_config, sq_gpr_resource_mgmt_1, sq_gpr_resource_mgmt_2;
	u32 sq_thread_resource_mgmt, sq_stack_resource_mgmt_1, sq_stack_resource_mgmt_2;
	int num_ps_gprs, num_vs_gprs, num_temp_gprs, num_gs_gprs, num_es_gprs;
	int num_ps_threads, num_vs_threads, num_gs_threads, num_es_threads;
	int num_ps_stack_entries, num_vs_stack_entries, num_gs_stack_entries, num_es_stack_entries;
	RING_LOCALS;

	switch ((dev_priv->flags & RADEON_FAMILY_MASK)) {
	case CHIP_R600:
		num_ps_gprs = 192;
		num_vs_gprs = 56;
		num_temp_gprs = 4;
		num_gs_gprs = 0;
		num_es_gprs = 0;
		num_ps_threads = 136;
		num_vs_threads = 48;
		num_gs_threads = 4;
		num_es_threads = 4;
		num_ps_stack_entries = 128;
		num_vs_stack_entries = 128;
		num_gs_stack_entries = 0;
		num_es_stack_entries = 0;
		break;
	case CHIP_RV630:
	case CHIP_RV635:
		num_ps_gprs = 84;
		num_vs_gprs = 36;
		num_temp_gprs = 4;
		num_gs_gprs = 0;
		num_es_gprs = 0;
		num_ps_threads = 144;
		num_vs_threads = 40;
		num_gs_threads = 4;
		num_es_threads = 4;
		num_ps_stack_entries = 40;
		num_vs_stack_entries = 40;
		num_gs_stack_entries = 32;
		num_es_stack_entries = 16;
		break;
	case CHIP_RV610:
	case CHIP_RV620:
	case CHIP_RS780:
	case CHIP_RS880:
	default:
		num_ps_gprs = 84;
		num_vs_gprs = 36;
		num_temp_gprs = 4;
		num_gs_gprs = 0;
		num_es_gprs = 0;
		num_ps_threads = 136;
		num_vs_threads = 48;
		num_gs_threads = 4;
		num_es_threads = 4;
		num_ps_stack_entries = 40;
		num_vs_stack_entries = 40;
		num_gs_stack_entries = 32;
		num_es_stack_entries = 16;
		break;
	case CHIP_RV670:
		num_ps_gprs = 144;
		num_vs_gprs = 40;
		num_temp_gprs = 4;
		num_gs_gprs = 0;
		num_es_gprs = 0;
		num_ps_threads = 136;
		num_vs_threads = 48;
		num_gs_threads = 4;
		num_es_threads = 4;
		num_ps_stack_entries = 40;
		num_vs_stack_entries = 40;
		num_gs_stack_entries = 32;
		num_es_stack_entries = 16;
		break;
	case CHIP_RV770:
		num_ps_gprs = 192;
		num_vs_gprs = 56;
		num_temp_gprs = 4;
		num_gs_gprs = 0;
		num_es_gprs = 0;
		num_ps_threads = 188;
		num_vs_threads = 60;
		num_gs_threads = 0;
		num_es_threads = 0;
		num_ps_stack_entries = 256;
		num_vs_stack_entries = 256;
		num_gs_stack_entries = 0;
		num_es_stack_entries = 0;
		break;
	case CHIP_RV730:
	case CHIP_RV740:
		num_ps_gprs = 84;
		num_vs_gprs = 36;
		num_temp_gprs = 4;
		num_gs_gprs = 0;
		num_es_gprs = 0;
		num_ps_threads = 188;
		num_vs_threads = 60;
		num_gs_threads = 0;
		num_es_threads = 0;
		num_ps_stack_entries = 128;
		num_vs_stack_entries = 128;
		num_gs_stack_entries = 0;
		num_es_stack_entries = 0;
		break;
	case CHIP_RV710:
		num_ps_gprs = 192;
		num_vs_gprs = 56;
		num_temp_gprs = 4;
		num_gs_gprs = 0;
		num_es_gprs = 0;
		num_ps_threads = 144;
		num_vs_threads = 48;
		num_gs_threads = 0;
		num_es_threads = 0;
		num_ps_stack_entries = 128;
		num_vs_stack_entries = 128;
		num_gs_stack_entries = 0;
		num_es_stack_entries = 0;
		break;
	}

	if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV610) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV620) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS780) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS880) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV710))
		sq_config = 0;
	else
		sq_config = R600_VC_ENABLE;

	sq_config |= (R600_DX9_CONSTS |
		      R600_ALU_INST_PREFER_VECTOR |
		      R600_PS_PRIO(0) |
		      R600_VS_PRIO(1) |
		      R600_GS_PRIO(2) |
		      R600_ES_PRIO(3));

	sq_gpr_resource_mgmt_1 = (R600_NUM_PS_GPRS(num_ps_gprs) |
				  R600_NUM_VS_GPRS(num_vs_gprs) |
				  R600_NUM_CLAUSE_TEMP_GPRS(num_temp_gprs));
	sq_gpr_resource_mgmt_2 = (R600_NUM_GS_GPRS(num_gs_gprs) |
				  R600_NUM_ES_GPRS(num_es_gprs));
	sq_thread_resource_mgmt = (R600_NUM_PS_THREADS(num_ps_threads) |
				   R600_NUM_VS_THREADS(num_vs_threads) |
				   R600_NUM_GS_THREADS(num_gs_threads) |
				   R600_NUM_ES_THREADS(num_es_threads));
	sq_stack_resource_mgmt_1 = (R600_NUM_PS_STACK_ENTRIES(num_ps_stack_entries) |
				    R600_NUM_VS_STACK_ENTRIES(num_vs_stack_entries));
	sq_stack_resource_mgmt_2 = (R600_NUM_GS_STACK_ENTRIES(num_gs_stack_entries) |
				    R600_NUM_ES_STACK_ENTRIES(num_es_stack_entries));

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RV770) {
		BEGIN_RING(r7xx_default_size + 10);
		for (i = 0; i < r7xx_default_size; i++)
			OUT_RING(r7xx_default_state[i]);
	} else {
		BEGIN_RING(r6xx_default_size + 10);
		for (i = 0; i < r6xx_default_size; i++)
			OUT_RING(r6xx_default_state[i]);
	}
	OUT_RING(CP_PACKET3(R600_IT_EVENT_WRITE, 0));
	OUT_RING(R600_CACHE_FLUSH_AND_INV_EVENT);
	/* SQ config */
	OUT_RING(CP_PACKET3(R600_IT_SET_CONFIG_REG, 6));
	OUT_RING((R600_SQ_CONFIG - R600_SET_CONFIG_REG_OFFSET) >> 2);
	OUT_RING(sq_config);
	OUT_RING(sq_gpr_resource_mgmt_1);
	OUT_RING(sq_gpr_resource_mgmt_2);
	OUT_RING(sq_thread_resource_mgmt);
	OUT_RING(sq_stack_resource_mgmt_1);
	OUT_RING(sq_stack_resource_mgmt_2);
	ADVANCE_RING();
}

static uint32_t i2f(uint32_t input)
{
	u32 result, i, exponent, fraction;

	if ((input & 0x3fff) == 0)
		result = 0; /* 0 is a special case */
	else {
		exponent = 140; /* exponent biased by 127; */
		fraction = (input & 0x3fff) << 10; /* cheat and only
						      handle numbers below 2^^15 */
		for (i = 0; i < 14; i++) {
			if (fraction & 0x800000)
				break;
			else {
				fraction = fraction << 1; /* keep
							     shifting left until top bit = 1 */
				exponent = exponent - 1;
			}
		}
		result = exponent << 23 | (fraction & 0x7fffff); /* mask
								    off top bit; assumed 1 */
	}
	return result;
}


static int r600_nomm_get_vb(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	dev_priv->blit_vb = radeon_freelist_get(dev);
	if (!dev_priv->blit_vb) {
		DRM_ERROR("Unable to allocate vertex buffer for blit\n");
		return -EAGAIN;
	}
	return 0;
}

static void r600_nomm_put_vb(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	dev_priv->blit_vb->used = 0;
	radeon_cp_discard_buffer(dev, dev_priv->blit_vb->file_priv->master, dev_priv->blit_vb);
}

static void *r600_nomm_get_vb_ptr(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	return (((char *)dev->agp_buffer_map->handle +
		 dev_priv->blit_vb->offset + dev_priv->blit_vb->used));
}

int
r600_prepare_blit_copy(struct drm_device *dev, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int ret;
	DRM_DEBUG("\n");

	ret = r600_nomm_get_vb(dev);
	if (ret)
		return ret;

	dev_priv->blit_vb->file_priv = file_priv;

	set_default_state(dev_priv);
	set_shaders(dev);

	return 0;
}


void
r600_done_blit_copy(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;
	DRM_DEBUG("\n");

	BEGIN_RING(5);
	OUT_RING(CP_PACKET3(R600_IT_EVENT_WRITE, 0));
	OUT_RING(R600_CACHE_FLUSH_AND_INV_EVENT);
	/* wait for 3D idle clean */
	OUT_RING(CP_PACKET3(R600_IT_SET_CONFIG_REG, 1));
	OUT_RING((R600_WAIT_UNTIL - R600_SET_CONFIG_REG_OFFSET) >> 2);
	OUT_RING(RADEON_WAIT_3D_IDLE | RADEON_WAIT_3D_IDLECLEAN);

	ADVANCE_RING();
	COMMIT_RING();

	r600_nomm_put_vb(dev);
}

void
r600_blit_copy(struct drm_device *dev,
	       uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
	       int size_bytes)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int max_bytes;
	u64 vb_addr;
	u32 *vb;

	vb = r600_nomm_get_vb_ptr(dev);

	if ((size_bytes & 3) || (src_gpu_addr & 3) || (dst_gpu_addr & 3)) {
		max_bytes = 8192;

		while (size_bytes) {
			int cur_size = size_bytes;
			int src_x = src_gpu_addr & 255;
			int dst_x = dst_gpu_addr & 255;
			int h = 1;
			src_gpu_addr = src_gpu_addr & ~255;
			dst_gpu_addr = dst_gpu_addr & ~255;

			if (!src_x && !dst_x) {
				h = (cur_size / max_bytes);
				if (h > 8192)
					h = 8192;
				if (h == 0)
					h = 1;
				else
					cur_size = max_bytes;
			} else {
				if (cur_size > max_bytes)
					cur_size = max_bytes;
				if (cur_size > (max_bytes - dst_x))
					cur_size = (max_bytes - dst_x);
				if (cur_size > (max_bytes - src_x))
					cur_size = (max_bytes - src_x);
			}

			if ((dev_priv->blit_vb->used + 48) > dev_priv->blit_vb->total) {

				r600_nomm_put_vb(dev);
				r600_nomm_get_vb(dev);
				if (!dev_priv->blit_vb)
					return;
				set_shaders(dev);
				vb = r600_nomm_get_vb_ptr(dev);
			}

			vb[0] = i2f(dst_x);
			vb[1] = 0;
			vb[2] = i2f(src_x);
			vb[3] = 0;

			vb[4] = i2f(dst_x);
			vb[5] = i2f(h);
			vb[6] = i2f(src_x);
			vb[7] = i2f(h);

			vb[8] = i2f(dst_x + cur_size);
			vb[9] = i2f(h);
			vb[10] = i2f(src_x + cur_size);
			vb[11] = i2f(h);

			/* src */
			set_tex_resource(dev_priv, FMT_8,
					 src_x + cur_size, h, src_x + cur_size,
					 src_gpu_addr);

			cp_set_surface_sync(dev_priv,
					    R600_TC_ACTION_ENA, (src_x + cur_size * h), src_gpu_addr);

			/* dst */
			set_render_target(dev_priv, COLOR_8,
					  dst_x + cur_size, h,
					  dst_gpu_addr);

			/* scissors */
			set_scissors(dev_priv, dst_x, 0, dst_x + cur_size, h);

			/* Vertex buffer setup */
			vb_addr = dev_priv->gart_buffers_offset +
				dev_priv->blit_vb->offset +
				dev_priv->blit_vb->used;
			set_vtx_resource(dev_priv, vb_addr);

			/* draw */
			draw_auto(dev_priv);

			cp_set_surface_sync(dev_priv,
					    R600_CB_ACTION_ENA | R600_CB0_DEST_BASE_ENA,
					    cur_size * h, dst_gpu_addr);

			vb += 12;
			dev_priv->blit_vb->used += 12 * 4;

			src_gpu_addr += cur_size * h;
			dst_gpu_addr += cur_size * h;
			size_bytes -= cur_size * h;
		}
	} else {
		max_bytes = 8192 * 4;

		while (size_bytes) {
			int cur_size = size_bytes;
			int src_x = (src_gpu_addr & 255);
			int dst_x = (dst_gpu_addr & 255);
			int h = 1;
			src_gpu_addr = src_gpu_addr & ~255;
			dst_gpu_addr = dst_gpu_addr & ~255;

			if (!src_x && !dst_x) {
				h = (cur_size / max_bytes);
				if (h > 8192)
					h = 8192;
				if (h == 0)
					h = 1;
				else
					cur_size = max_bytes;
			} else {
				if (cur_size > max_bytes)
					cur_size = max_bytes;
				if (cur_size > (max_bytes - dst_x))
					cur_size = (max_bytes - dst_x);
				if (cur_size > (max_bytes - src_x))
					cur_size = (max_bytes - src_x);
			}

			if ((dev_priv->blit_vb->used + 48) > dev_priv->blit_vb->total) {
				r600_nomm_put_vb(dev);
				r600_nomm_get_vb(dev);
				if (!dev_priv->blit_vb)
					return;

				set_shaders(dev);
				vb = r600_nomm_get_vb_ptr(dev);
			}

			vb[0] = i2f(dst_x / 4);
			vb[1] = 0;
			vb[2] = i2f(src_x / 4);
			vb[3] = 0;

			vb[4] = i2f(dst_x / 4);
			vb[5] = i2f(h);
			vb[6] = i2f(src_x / 4);
			vb[7] = i2f(h);

			vb[8] = i2f((dst_x + cur_size) / 4);
			vb[9] = i2f(h);
			vb[10] = i2f((src_x + cur_size) / 4);
			vb[11] = i2f(h);

			/* src */
			set_tex_resource(dev_priv, FMT_8_8_8_8,
					 (src_x + cur_size) / 4,
					 h, (src_x + cur_size) / 4,
					 src_gpu_addr);

			cp_set_surface_sync(dev_priv,
					    R600_TC_ACTION_ENA, (src_x + cur_size * h), src_gpu_addr);

			/* dst */
			set_render_target(dev_priv, COLOR_8_8_8_8,
					  (dst_x + cur_size) / 4, h,
					  dst_gpu_addr);

			/* scissors */
			set_scissors(dev_priv, (dst_x / 4), 0, (dst_x + cur_size / 4), h);

			/* Vertex buffer setup */
			vb_addr = dev_priv->gart_buffers_offset +
				dev_priv->blit_vb->offset +
				dev_priv->blit_vb->used;
			set_vtx_resource(dev_priv, vb_addr);

			/* draw */
			draw_auto(dev_priv);

			cp_set_surface_sync(dev_priv,
					    R600_CB_ACTION_ENA | R600_CB0_DEST_BASE_ENA,
					    cur_size * h, dst_gpu_addr);

			vb += 12;
			dev_priv->blit_vb->used += 12 * 4;

			src_gpu_addr += cur_size * h;
			dst_gpu_addr += cur_size * h;
			size_bytes -= cur_size * h;
		}
	}
}

void
r600_blit_swap(struct drm_device *dev,
	       uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
	       int sx, int sy, int dx, int dy,
	       int w, int h, int src_pitch, int dst_pitch, int cpp)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int cb_format, tex_format;
	int sx2, sy2, dx2, dy2;
	u64 vb_addr;
	u32 *vb;

	if ((dev_priv->blit_vb->used + 48) > dev_priv->blit_vb->total) {

		r600_nomm_put_vb(dev);
		r600_nomm_get_vb(dev);
		if (!dev_priv->blit_vb)
			return;

		set_shaders(dev);
	}
	vb = r600_nomm_get_vb_ptr(dev);

	sx2 = sx + w;
	sy2 = sy + h;
	dx2 = dx + w;
	dy2 = dy + h;

	vb[0] = i2f(dx);
	vb[1] = i2f(dy);
	vb[2] = i2f(sx);
	vb[3] = i2f(sy);

	vb[4] = i2f(dx);
	vb[5] = i2f(dy2);
	vb[6] = i2f(sx);
	vb[7] = i2f(sy2);

	vb[8] = i2f(dx2);
	vb[9] = i2f(dy2);
	vb[10] = i2f(sx2);
	vb[11] = i2f(sy2);

	switch(cpp) {
	case 4:
		cb_format = COLOR_8_8_8_8;
		tex_format = FMT_8_8_8_8;
		break;
	case 2:
		cb_format = COLOR_5_6_5;
		tex_format = FMT_5_6_5;
		break;
	default:
		cb_format = COLOR_8;
		tex_format = FMT_8;
		break;
	}

	/* src */
	set_tex_resource(dev_priv, tex_format,
			 src_pitch / cpp,
			 sy2, src_pitch / cpp,
			 src_gpu_addr);

	cp_set_surface_sync(dev_priv,
			    R600_TC_ACTION_ENA, src_pitch * sy2, src_gpu_addr);

	/* dst */
	set_render_target(dev_priv, cb_format,
			  dst_pitch / cpp, dy2,
			  dst_gpu_addr);

	/* scissors */
	set_scissors(dev_priv, dx, dy, dx2, dy2);

	/* Vertex buffer setup */
	vb_addr = dev_priv->gart_buffers_offset +
		dev_priv->blit_vb->offset +
		dev_priv->blit_vb->used;
	set_vtx_resource(dev_priv, vb_addr);

	/* draw */
	draw_auto(dev_priv);

	cp_set_surface_sync(dev_priv,
			    R600_CB_ACTION_ENA | R600_CB0_DEST_BASE_ENA,
			    dst_pitch * dy2, dst_gpu_addr);

	dev_priv->blit_vb->used += 12 * 4;
}
