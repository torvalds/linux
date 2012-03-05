/*
 * Copyright 2009 Advanced Micro Devices, Inc.
 * Copyright 2009 Red Hat Inc.
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
 */

#include "drmP.h"
#include "drm.h"
#include "radeon_drm.h"
#include "radeon.h"

#include "r600d.h"
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

#define RECT_UNIT_H           32
#define RECT_UNIT_W           (RADEON_GPU_PAGE_SIZE / 4 / RECT_UNIT_H)

/* emits 21 on rv770+, 23 on r600 */
static void
set_render_target(struct radeon_device *rdev, int format,
		  int w, int h, u64 gpu_addr)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	u32 cb_color_info;
	int pitch, slice;

	h = ALIGN(h, 8);
	if (h < 8)
		h = 8;

	cb_color_info = CB_FORMAT(format) |
		CB_SOURCE_FORMAT(CB_SF_EXPORT_NORM) |
		CB_ARRAY_MODE(ARRAY_1D_TILED_THIN1);
	pitch = (w / 8) - 1;
	slice = ((w * h) / 64) - 1;

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (CB_COLOR0_BASE - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, gpu_addr >> 8);

	if (rdev->family > CHIP_R600 && rdev->family < CHIP_RV770) {
		radeon_ring_write(ring, PACKET3(PACKET3_SURFACE_BASE_UPDATE, 0));
		radeon_ring_write(ring, 2 << 0);
	}

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (CB_COLOR0_SIZE - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, (pitch << 0) | (slice << 10));

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (CB_COLOR0_VIEW - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (CB_COLOR0_INFO - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, cb_color_info);

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (CB_COLOR0_TILE - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (CB_COLOR0_FRAG - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (CB_COLOR0_MASK - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, 0);
}

/* emits 5dw */
static void
cp_set_surface_sync(struct radeon_device *rdev,
		    u32 sync_type, u32 size,
		    u64 mc_addr)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	u32 cp_coher_size;

	if (size == 0xffffffff)
		cp_coher_size = 0xffffffff;
	else
		cp_coher_size = ((size + 255) >> 8);

	radeon_ring_write(ring, PACKET3(PACKET3_SURFACE_SYNC, 3));
	radeon_ring_write(ring, sync_type);
	radeon_ring_write(ring, cp_coher_size);
	radeon_ring_write(ring, mc_addr >> 8);
	radeon_ring_write(ring, 10); /* poll interval */
}

/* emits 21dw + 1 surface sync = 26dw */
static void
set_shaders(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	u64 gpu_addr;
	u32 sq_pgm_resources;

	/* setup shader regs */
	sq_pgm_resources = (1 << 0);

	/* VS */
	gpu_addr = rdev->r600_blit.shader_gpu_addr + rdev->r600_blit.vs_offset;
	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (SQ_PGM_START_VS - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, gpu_addr >> 8);

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (SQ_PGM_RESOURCES_VS - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, sq_pgm_resources);

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (SQ_PGM_CF_OFFSET_VS - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, 0);

	/* PS */
	gpu_addr = rdev->r600_blit.shader_gpu_addr + rdev->r600_blit.ps_offset;
	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (SQ_PGM_START_PS - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, gpu_addr >> 8);

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (SQ_PGM_RESOURCES_PS - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, sq_pgm_resources | (1 << 28));

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (SQ_PGM_EXPORTS_PS - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, 2);

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	radeon_ring_write(ring, (SQ_PGM_CF_OFFSET_PS - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, 0);

	gpu_addr = rdev->r600_blit.shader_gpu_addr + rdev->r600_blit.vs_offset;
	cp_set_surface_sync(rdev, PACKET3_SH_ACTION_ENA, 512, gpu_addr);
}

/* emits 9 + 1 sync (5) = 14*/
static void
set_vtx_resource(struct radeon_device *rdev, u64 gpu_addr)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	u32 sq_vtx_constant_word2;

	sq_vtx_constant_word2 = SQ_VTXC_BASE_ADDR_HI(upper_32_bits(gpu_addr) & 0xff) |
		SQ_VTXC_STRIDE(16);
#ifdef __BIG_ENDIAN
	sq_vtx_constant_word2 |=  SQ_VTXC_ENDIAN_SWAP(SQ_ENDIAN_8IN32);
#endif

	radeon_ring_write(ring, PACKET3(PACKET3_SET_RESOURCE, 7));
	radeon_ring_write(ring, 0x460);
	radeon_ring_write(ring, gpu_addr & 0xffffffff);
	radeon_ring_write(ring, 48 - 1);
	radeon_ring_write(ring, sq_vtx_constant_word2);
	radeon_ring_write(ring, 1 << 0);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, SQ_TEX_VTX_VALID_BUFFER << 30);

	if ((rdev->family == CHIP_RV610) ||
	    (rdev->family == CHIP_RV620) ||
	    (rdev->family == CHIP_RS780) ||
	    (rdev->family == CHIP_RS880) ||
	    (rdev->family == CHIP_RV710))
		cp_set_surface_sync(rdev,
				    PACKET3_TC_ACTION_ENA, 48, gpu_addr);
	else
		cp_set_surface_sync(rdev,
				    PACKET3_VC_ACTION_ENA, 48, gpu_addr);
}

/* emits 9 */
static void
set_tex_resource(struct radeon_device *rdev,
		 int format, int w, int h, int pitch,
		 u64 gpu_addr, u32 size)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	uint32_t sq_tex_resource_word0, sq_tex_resource_word1, sq_tex_resource_word4;

	if (h < 1)
		h = 1;

	sq_tex_resource_word0 = S_038000_DIM(V_038000_SQ_TEX_DIM_2D) |
		S_038000_TILE_MODE(V_038000_ARRAY_1D_TILED_THIN1);
	sq_tex_resource_word0 |= S_038000_PITCH((pitch >> 3) - 1) |
		S_038000_TEX_WIDTH(w - 1);

	sq_tex_resource_word1 = S_038004_DATA_FORMAT(format);
	sq_tex_resource_word1 |= S_038004_TEX_HEIGHT(h - 1);

	sq_tex_resource_word4 = S_038010_REQUEST_SIZE(1) |
		S_038010_DST_SEL_X(SQ_SEL_X) |
		S_038010_DST_SEL_Y(SQ_SEL_Y) |
		S_038010_DST_SEL_Z(SQ_SEL_Z) |
		S_038010_DST_SEL_W(SQ_SEL_W);

	cp_set_surface_sync(rdev,
			    PACKET3_TC_ACTION_ENA, size, gpu_addr);

	radeon_ring_write(ring, PACKET3(PACKET3_SET_RESOURCE, 7));
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, sq_tex_resource_word0);
	radeon_ring_write(ring, sq_tex_resource_word1);
	radeon_ring_write(ring, gpu_addr >> 8);
	radeon_ring_write(ring, gpu_addr >> 8);
	radeon_ring_write(ring, sq_tex_resource_word4);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, SQ_TEX_VTX_VALID_TEXTURE << 30);
}

/* emits 12 */
static void
set_scissors(struct radeon_device *rdev, int x1, int y1,
	     int x2, int y2)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	radeon_ring_write(ring, (PA_SC_SCREEN_SCISSOR_TL - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, (x1 << 0) | (y1 << 16));
	radeon_ring_write(ring, (x2 << 0) | (y2 << 16));

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	radeon_ring_write(ring, (PA_SC_GENERIC_SCISSOR_TL - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, (x1 << 0) | (y1 << 16) | (1 << 31));
	radeon_ring_write(ring, (x2 << 0) | (y2 << 16));

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	radeon_ring_write(ring, (PA_SC_WINDOW_SCISSOR_TL - PACKET3_SET_CONTEXT_REG_OFFSET) >> 2);
	radeon_ring_write(ring, (x1 << 0) | (y1 << 16) | (1 << 31));
	radeon_ring_write(ring, (x2 << 0) | (y2 << 16));
}

/* emits 10 */
static void
draw_auto(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	radeon_ring_write(ring, (VGT_PRIMITIVE_TYPE - PACKET3_SET_CONFIG_REG_OFFSET) >> 2);
	radeon_ring_write(ring, DI_PT_RECTLIST);

	radeon_ring_write(ring, PACKET3(PACKET3_INDEX_TYPE, 0));
	radeon_ring_write(ring,
#ifdef __BIG_ENDIAN
			  (2 << 2) |
#endif
			  DI_INDEX_SIZE_16_BIT);

	radeon_ring_write(ring, PACKET3(PACKET3_NUM_INSTANCES, 0));
	radeon_ring_write(ring, 1);

	radeon_ring_write(ring, PACKET3(PACKET3_DRAW_INDEX_AUTO, 1));
	radeon_ring_write(ring, 3);
	radeon_ring_write(ring, DI_SRC_SEL_AUTO_INDEX);

}

/* emits 14 */
static void
set_default_state(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	u32 sq_config, sq_gpr_resource_mgmt_1, sq_gpr_resource_mgmt_2;
	u32 sq_thread_resource_mgmt, sq_stack_resource_mgmt_1, sq_stack_resource_mgmt_2;
	int num_ps_gprs, num_vs_gprs, num_temp_gprs, num_gs_gprs, num_es_gprs;
	int num_ps_threads, num_vs_threads, num_gs_threads, num_es_threads;
	int num_ps_stack_entries, num_vs_stack_entries, num_gs_stack_entries, num_es_stack_entries;
	u64 gpu_addr;
	int dwords;

	switch (rdev->family) {
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

	if ((rdev->family == CHIP_RV610) ||
	    (rdev->family == CHIP_RV620) ||
	    (rdev->family == CHIP_RS780) ||
	    (rdev->family == CHIP_RS880) ||
	    (rdev->family == CHIP_RV710))
		sq_config = 0;
	else
		sq_config = VC_ENABLE;

	sq_config |= (DX9_CONSTS |
		      ALU_INST_PREFER_VECTOR |
		      PS_PRIO(0) |
		      VS_PRIO(1) |
		      GS_PRIO(2) |
		      ES_PRIO(3));

	sq_gpr_resource_mgmt_1 = (NUM_PS_GPRS(num_ps_gprs) |
				  NUM_VS_GPRS(num_vs_gprs) |
				  NUM_CLAUSE_TEMP_GPRS(num_temp_gprs));
	sq_gpr_resource_mgmt_2 = (NUM_GS_GPRS(num_gs_gprs) |
				  NUM_ES_GPRS(num_es_gprs));
	sq_thread_resource_mgmt = (NUM_PS_THREADS(num_ps_threads) |
				   NUM_VS_THREADS(num_vs_threads) |
				   NUM_GS_THREADS(num_gs_threads) |
				   NUM_ES_THREADS(num_es_threads));
	sq_stack_resource_mgmt_1 = (NUM_PS_STACK_ENTRIES(num_ps_stack_entries) |
				    NUM_VS_STACK_ENTRIES(num_vs_stack_entries));
	sq_stack_resource_mgmt_2 = (NUM_GS_STACK_ENTRIES(num_gs_stack_entries) |
				    NUM_ES_STACK_ENTRIES(num_es_stack_entries));

	/* emit an IB pointing at default state */
	dwords = ALIGN(rdev->r600_blit.state_len, 0x10);
	gpu_addr = rdev->r600_blit.shader_gpu_addr + rdev->r600_blit.state_offset;
	radeon_ring_write(ring, PACKET3(PACKET3_INDIRECT_BUFFER, 2));
	radeon_ring_write(ring,
#ifdef __BIG_ENDIAN
			  (2 << 0) |
#endif
			  (gpu_addr & 0xFFFFFFFC));
	radeon_ring_write(ring, upper_32_bits(gpu_addr) & 0xFF);
	radeon_ring_write(ring, dwords);

	/* SQ config */
	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 6));
	radeon_ring_write(ring, (SQ_CONFIG - PACKET3_SET_CONFIG_REG_OFFSET) >> 2);
	radeon_ring_write(ring, sq_config);
	radeon_ring_write(ring, sq_gpr_resource_mgmt_1);
	radeon_ring_write(ring, sq_gpr_resource_mgmt_2);
	radeon_ring_write(ring, sq_thread_resource_mgmt);
	radeon_ring_write(ring, sq_stack_resource_mgmt_1);
	radeon_ring_write(ring, sq_stack_resource_mgmt_2);
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

int r600_blit_init(struct radeon_device *rdev)
{
	u32 obj_size;
	int i, r, dwords;
	void *ptr;
	u32 packet2s[16];
	int num_packet2s = 0;

	rdev->r600_blit.primitives.set_render_target = set_render_target;
	rdev->r600_blit.primitives.cp_set_surface_sync = cp_set_surface_sync;
	rdev->r600_blit.primitives.set_shaders = set_shaders;
	rdev->r600_blit.primitives.set_vtx_resource = set_vtx_resource;
	rdev->r600_blit.primitives.set_tex_resource = set_tex_resource;
	rdev->r600_blit.primitives.set_scissors = set_scissors;
	rdev->r600_blit.primitives.draw_auto = draw_auto;
	rdev->r600_blit.primitives.set_default_state = set_default_state;

	rdev->r600_blit.ring_size_common = 40; /* shaders + def state */
	rdev->r600_blit.ring_size_common += 16; /* fence emit for VB IB */
	rdev->r600_blit.ring_size_common += 5; /* done copy */
	rdev->r600_blit.ring_size_common += 16; /* fence emit for done copy */

	rdev->r600_blit.ring_size_per_loop = 76;
	/* set_render_target emits 2 extra dwords on rv6xx */
	if (rdev->family > CHIP_R600 && rdev->family < CHIP_RV770)
		rdev->r600_blit.ring_size_per_loop += 2;

	rdev->r600_blit.max_dim = 8192;

	/* pin copy shader into vram if already initialized */
	if (rdev->r600_blit.shader_obj)
		goto done;

	mutex_init(&rdev->r600_blit.mutex);
	rdev->r600_blit.state_offset = 0;

	if (rdev->family >= CHIP_RV770)
		rdev->r600_blit.state_len = r7xx_default_size;
	else
		rdev->r600_blit.state_len = r6xx_default_size;

	dwords = rdev->r600_blit.state_len;
	while (dwords & 0xf) {
		packet2s[num_packet2s++] = cpu_to_le32(PACKET2(0));
		dwords++;
	}

	obj_size = dwords * 4;
	obj_size = ALIGN(obj_size, 256);

	rdev->r600_blit.vs_offset = obj_size;
	obj_size += r6xx_vs_size * 4;
	obj_size = ALIGN(obj_size, 256);

	rdev->r600_blit.ps_offset = obj_size;
	obj_size += r6xx_ps_size * 4;
	obj_size = ALIGN(obj_size, 256);

	r = radeon_bo_create(rdev, obj_size, PAGE_SIZE, true, RADEON_GEM_DOMAIN_VRAM,
				&rdev->r600_blit.shader_obj);
	if (r) {
		DRM_ERROR("r600 failed to allocate shader\n");
		return r;
	}

	DRM_DEBUG("r6xx blit allocated bo %08x vs %08x ps %08x\n",
		  obj_size,
		  rdev->r600_blit.vs_offset, rdev->r600_blit.ps_offset);

	r = radeon_bo_reserve(rdev->r600_blit.shader_obj, false);
	if (unlikely(r != 0))
		return r;
	r = radeon_bo_kmap(rdev->r600_blit.shader_obj, &ptr);
	if (r) {
		DRM_ERROR("failed to map blit object %d\n", r);
		return r;
	}
	if (rdev->family >= CHIP_RV770)
		memcpy_toio(ptr + rdev->r600_blit.state_offset,
			    r7xx_default_state, rdev->r600_blit.state_len * 4);
	else
		memcpy_toio(ptr + rdev->r600_blit.state_offset,
			    r6xx_default_state, rdev->r600_blit.state_len * 4);
	if (num_packet2s)
		memcpy_toio(ptr + rdev->r600_blit.state_offset + (rdev->r600_blit.state_len * 4),
			    packet2s, num_packet2s * 4);
	for (i = 0; i < r6xx_vs_size; i++)
		*(u32 *)((unsigned long)ptr + rdev->r600_blit.vs_offset + i * 4) = cpu_to_le32(r6xx_vs[i]);
	for (i = 0; i < r6xx_ps_size; i++)
		*(u32 *)((unsigned long)ptr + rdev->r600_blit.ps_offset + i * 4) = cpu_to_le32(r6xx_ps[i]);
	radeon_bo_kunmap(rdev->r600_blit.shader_obj);
	radeon_bo_unreserve(rdev->r600_blit.shader_obj);

done:
	r = radeon_bo_reserve(rdev->r600_blit.shader_obj, false);
	if (unlikely(r != 0))
		return r;
	r = radeon_bo_pin(rdev->r600_blit.shader_obj, RADEON_GEM_DOMAIN_VRAM,
			  &rdev->r600_blit.shader_gpu_addr);
	radeon_bo_unreserve(rdev->r600_blit.shader_obj);
	if (r) {
		dev_err(rdev->dev, "(%d) pin blit object failed\n", r);
		return r;
	}
	radeon_ttm_set_active_vram_size(rdev, rdev->mc.real_vram_size);
	return 0;
}

void r600_blit_fini(struct radeon_device *rdev)
{
	int r;

	radeon_ttm_set_active_vram_size(rdev, rdev->mc.visible_vram_size);
	if (rdev->r600_blit.shader_obj == NULL)
		return;
	/* If we can't reserve the bo, unref should be enough to destroy
	 * it when it becomes idle.
	 */
	r = radeon_bo_reserve(rdev->r600_blit.shader_obj, false);
	if (!r) {
		radeon_bo_unpin(rdev->r600_blit.shader_obj);
		radeon_bo_unreserve(rdev->r600_blit.shader_obj);
	}
	radeon_bo_unref(&rdev->r600_blit.shader_obj);
}

static int r600_vb_ib_get(struct radeon_device *rdev, unsigned size)
{
	int r;
	r = radeon_ib_get(rdev, RADEON_RING_TYPE_GFX_INDEX,
			  &rdev->r600_blit.vb_ib, size);
	if (r) {
		DRM_ERROR("failed to get IB for vertex buffer\n");
		return r;
	}

	rdev->r600_blit.vb_total = size;
	rdev->r600_blit.vb_used = 0;
	return 0;
}

static void r600_vb_ib_put(struct radeon_device *rdev)
{
	radeon_fence_emit(rdev, rdev->r600_blit.vb_ib->fence);
	radeon_ib_free(rdev, &rdev->r600_blit.vb_ib);
}

static unsigned r600_blit_create_rect(unsigned num_gpu_pages,
				      int *width, int *height, int max_dim)
{
	unsigned max_pages;
	unsigned pages = num_gpu_pages;
	int w, h;

	if (num_gpu_pages == 0) {
		/* not supposed to be called with no pages, but just in case */
		h = 0;
		w = 0;
		pages = 0;
		WARN_ON(1);
	} else {
		int rect_order = 2;
		h = RECT_UNIT_H;
		while (num_gpu_pages / rect_order) {
			h *= 2;
			rect_order *= 4;
			if (h >= max_dim) {
				h = max_dim;
				break;
			}
		}
		max_pages = (max_dim * h) / (RECT_UNIT_W * RECT_UNIT_H);
		if (pages > max_pages)
			pages = max_pages;
		w = (pages * RECT_UNIT_W * RECT_UNIT_H) / h;
		w = (w / RECT_UNIT_W) * RECT_UNIT_W;
		pages = (w * h) / (RECT_UNIT_W * RECT_UNIT_H);
		BUG_ON(pages == 0);
	}


	DRM_DEBUG("blit_rectangle: h=%d, w=%d, pages=%d\n", h, w, pages);

	/* return width and height only of the caller wants it */
	if (height)
		*height = h;
	if (width)
		*width = w;

	return pages;
}


int r600_blit_prepare_copy(struct radeon_device *rdev, unsigned num_gpu_pages)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	int r;
	int ring_size;
	int num_loops = 0;
	int dwords_per_loop = rdev->r600_blit.ring_size_per_loop;

	/* num loops */
	while (num_gpu_pages) {
		num_gpu_pages -=
			r600_blit_create_rect(num_gpu_pages, NULL, NULL,
					      rdev->r600_blit.max_dim);
		num_loops++;
	}

	/* 48 bytes for vertex per loop */
	r = r600_vb_ib_get(rdev, (num_loops*48)+256);
	if (r)
		return r;

	/* calculate number of loops correctly */
	ring_size = num_loops * dwords_per_loop;
	ring_size += rdev->r600_blit.ring_size_common;
	r = radeon_ring_lock(rdev, ring, ring_size);
	if (r)
		return r;

	rdev->r600_blit.primitives.set_default_state(rdev);
	rdev->r600_blit.primitives.set_shaders(rdev);
	return 0;
}

void r600_blit_done_copy(struct radeon_device *rdev, struct radeon_fence *fence)
{
	int r;

	if (rdev->r600_blit.vb_ib)
		r600_vb_ib_put(rdev);

	if (fence)
		r = radeon_fence_emit(rdev, fence);

	radeon_ring_unlock_commit(rdev, &rdev->ring[RADEON_RING_TYPE_GFX_INDEX]);
}

void r600_kms_blit_copy(struct radeon_device *rdev,
			u64 src_gpu_addr, u64 dst_gpu_addr,
			unsigned num_gpu_pages)
{
	u64 vb_gpu_addr;
	u32 *vb;

	DRM_DEBUG("emitting copy %16llx %16llx %d %d\n",
		  src_gpu_addr, dst_gpu_addr,
		  num_gpu_pages, rdev->r600_blit.vb_used);
	vb = (u32 *)(rdev->r600_blit.vb_ib->ptr + rdev->r600_blit.vb_used);

	while (num_gpu_pages) {
		int w, h;
		unsigned size_in_bytes;
		unsigned pages_per_loop =
			r600_blit_create_rect(num_gpu_pages, &w, &h,
					      rdev->r600_blit.max_dim);

		size_in_bytes = pages_per_loop * RADEON_GPU_PAGE_SIZE;
		DRM_DEBUG("rectangle w=%d h=%d\n", w, h);

		if ((rdev->r600_blit.vb_used + 48) > rdev->r600_blit.vb_total) {
			WARN_ON(1);
		}

		vb[0] = 0;
		vb[1] = 0;
		vb[2] = 0;
		vb[3] = 0;

		vb[4] = 0;
		vb[5] = i2f(h);
		vb[6] = 0;
		vb[7] = i2f(h);

		vb[8] = i2f(w);
		vb[9] = i2f(h);
		vb[10] = i2f(w);
		vb[11] = i2f(h);

		rdev->r600_blit.primitives.set_tex_resource(rdev, FMT_8_8_8_8,
							    w, h, w, src_gpu_addr, size_in_bytes);
		rdev->r600_blit.primitives.set_render_target(rdev, COLOR_8_8_8_8,
							     w, h, dst_gpu_addr);
		rdev->r600_blit.primitives.set_scissors(rdev, 0, 0, w, h);
		vb_gpu_addr = rdev->r600_blit.vb_ib->gpu_addr + rdev->r600_blit.vb_used;
		rdev->r600_blit.primitives.set_vtx_resource(rdev, vb_gpu_addr);
		rdev->r600_blit.primitives.draw_auto(rdev);
		rdev->r600_blit.primitives.cp_set_surface_sync(rdev,
				    PACKET3_CB_ACTION_ENA | PACKET3_CB0_DEST_BASE_ENA,
				    size_in_bytes, dst_gpu_addr);

		vb += 12;
		rdev->r600_blit.vb_used += 4*12;
		src_gpu_addr += size_in_bytes;
		dst_gpu_addr += size_in_bytes;
		num_gpu_pages -= pages_per_loop;
	}
}
