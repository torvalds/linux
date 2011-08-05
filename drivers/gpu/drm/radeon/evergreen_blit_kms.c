/*
 * Copyright 2010 Advanced Micro Devices, Inc.
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
#include "radeon.h"

#include "evergreend.h"
#include "evergreen_blit_shaders.h"
#include "cayman_blit_shaders.h"

#define DI_PT_RECTLIST        0x11
#define DI_INDEX_SIZE_16_BIT  0x0
#define DI_SRC_SEL_AUTO_INDEX 0x2

#define FMT_8                 0x1
#define FMT_5_6_5             0x8
#define FMT_8_8_8_8           0x1a
#define COLOR_8               0x1
#define COLOR_5_6_5           0x8
#define COLOR_8_8_8_8         0x1a

/* emits 17 */
static void
set_render_target(struct radeon_device *rdev, int format,
		  int w, int h, u64 gpu_addr)
{
	u32 cb_color_info;
	int pitch, slice;

	h = ALIGN(h, 8);
	if (h < 8)
		h = 8;

	cb_color_info = ((format << 2) | (1 << 24) | (1 << 8));
	pitch = (w / 8) - 1;
	slice = ((w * h) / 64) - 1;

	radeon_ring_write(rdev, PACKET3(PACKET3_SET_CONTEXT_REG, 15));
	radeon_ring_write(rdev, (CB_COLOR0_BASE - PACKET3_SET_CONTEXT_REG_START) >> 2);
	radeon_ring_write(rdev, gpu_addr >> 8);
	radeon_ring_write(rdev, pitch);
	radeon_ring_write(rdev, slice);
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, cb_color_info);
	radeon_ring_write(rdev, (1 << 4));
	radeon_ring_write(rdev, (w - 1) | ((h - 1) << 16));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, 0);
}

/* emits 5dw */
static void
cp_set_surface_sync(struct radeon_device *rdev,
		    u32 sync_type, u32 size,
		    u64 mc_addr)
{
	u32 cp_coher_size;

	if (size == 0xffffffff)
		cp_coher_size = 0xffffffff;
	else
		cp_coher_size = ((size + 255) >> 8);

	radeon_ring_write(rdev, PACKET3(PACKET3_SURFACE_SYNC, 3));
	radeon_ring_write(rdev, sync_type);
	radeon_ring_write(rdev, cp_coher_size);
	radeon_ring_write(rdev, mc_addr >> 8);
	radeon_ring_write(rdev, 10); /* poll interval */
}

/* emits 11dw + 1 surface sync = 16dw */
static void
set_shaders(struct radeon_device *rdev)
{
	u64 gpu_addr;

	/* VS */
	gpu_addr = rdev->r600_blit.shader_gpu_addr + rdev->r600_blit.vs_offset;
	radeon_ring_write(rdev, PACKET3(PACKET3_SET_CONTEXT_REG, 3));
	radeon_ring_write(rdev, (SQ_PGM_START_VS - PACKET3_SET_CONTEXT_REG_START) >> 2);
	radeon_ring_write(rdev, gpu_addr >> 8);
	radeon_ring_write(rdev, 2);
	radeon_ring_write(rdev, 0);

	/* PS */
	gpu_addr = rdev->r600_blit.shader_gpu_addr + rdev->r600_blit.ps_offset;
	radeon_ring_write(rdev, PACKET3(PACKET3_SET_CONTEXT_REG, 4));
	radeon_ring_write(rdev, (SQ_PGM_START_PS - PACKET3_SET_CONTEXT_REG_START) >> 2);
	radeon_ring_write(rdev, gpu_addr >> 8);
	radeon_ring_write(rdev, 1);
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, 2);

	gpu_addr = rdev->r600_blit.shader_gpu_addr + rdev->r600_blit.vs_offset;
	cp_set_surface_sync(rdev, PACKET3_SH_ACTION_ENA, 512, gpu_addr);
}

/* emits 10 + 1 sync (5) = 15 */
static void
set_vtx_resource(struct radeon_device *rdev, u64 gpu_addr)
{
	u32 sq_vtx_constant_word2, sq_vtx_constant_word3;

	/* high addr, stride */
	sq_vtx_constant_word2 = ((upper_32_bits(gpu_addr) & 0xff) | (16 << 8));
#ifdef __BIG_ENDIAN
	sq_vtx_constant_word2 |= (2 << 30);
#endif
	/* xyzw swizzles */
	sq_vtx_constant_word3 = (0 << 3) | (1 << 6) | (2 << 9) | (3 << 12);

	radeon_ring_write(rdev, PACKET3(PACKET3_SET_RESOURCE, 8));
	radeon_ring_write(rdev, 0x580);
	radeon_ring_write(rdev, gpu_addr & 0xffffffff);
	radeon_ring_write(rdev, 48 - 1); /* size */
	radeon_ring_write(rdev, sq_vtx_constant_word2);
	radeon_ring_write(rdev, sq_vtx_constant_word3);
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, SQ_TEX_VTX_VALID_BUFFER << 30);

	if ((rdev->family == CHIP_CEDAR) ||
	    (rdev->family == CHIP_PALM) ||
	    (rdev->family == CHIP_SUMO) ||
	    (rdev->family == CHIP_SUMO2) ||
	    (rdev->family == CHIP_CAICOS))
		cp_set_surface_sync(rdev,
				    PACKET3_TC_ACTION_ENA, 48, gpu_addr);
	else
		cp_set_surface_sync(rdev,
				    PACKET3_VC_ACTION_ENA, 48, gpu_addr);

}

/* emits 10 */
static void
set_tex_resource(struct radeon_device *rdev,
		 int format, int w, int h, int pitch,
		 u64 gpu_addr)
{
	u32 sq_tex_resource_word0, sq_tex_resource_word1;
	u32 sq_tex_resource_word4, sq_tex_resource_word7;

	if (h < 1)
		h = 1;

	sq_tex_resource_word0 = (1 << 0); /* 2D */
	sq_tex_resource_word0 |= ((((pitch >> 3) - 1) << 6) |
				  ((w - 1) << 18));
	sq_tex_resource_word1 = ((h - 1) << 0) | (1 << 28);
	/* xyzw swizzles */
	sq_tex_resource_word4 = (0 << 16) | (1 << 19) | (2 << 22) | (3 << 25);

	sq_tex_resource_word7 = format | (SQ_TEX_VTX_VALID_TEXTURE << 30);

	radeon_ring_write(rdev, PACKET3(PACKET3_SET_RESOURCE, 8));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, sq_tex_resource_word0);
	radeon_ring_write(rdev, sq_tex_resource_word1);
	radeon_ring_write(rdev, gpu_addr >> 8);
	radeon_ring_write(rdev, gpu_addr >> 8);
	radeon_ring_write(rdev, sq_tex_resource_word4);
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, sq_tex_resource_word7);
}

/* emits 12 */
static void
set_scissors(struct radeon_device *rdev, int x1, int y1,
	     int x2, int y2)
{
	/* workaround some hw bugs */
	if (x2 == 0)
		x1 = 1;
	if (y2 == 0)
		y1 = 1;
	if (rdev->family == CHIP_CAYMAN) {
		if ((x2 == 1) && (y2 == 1))
			x2 = 2;
	}

	radeon_ring_write(rdev, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	radeon_ring_write(rdev, (PA_SC_SCREEN_SCISSOR_TL - PACKET3_SET_CONTEXT_REG_START) >> 2);
	radeon_ring_write(rdev, (x1 << 0) | (y1 << 16));
	radeon_ring_write(rdev, (x2 << 0) | (y2 << 16));

	radeon_ring_write(rdev, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	radeon_ring_write(rdev, (PA_SC_GENERIC_SCISSOR_TL - PACKET3_SET_CONTEXT_REG_START) >> 2);
	radeon_ring_write(rdev, (x1 << 0) | (y1 << 16) | (1 << 31));
	radeon_ring_write(rdev, (x2 << 0) | (y2 << 16));

	radeon_ring_write(rdev, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	radeon_ring_write(rdev, (PA_SC_WINDOW_SCISSOR_TL - PACKET3_SET_CONTEXT_REG_START) >> 2);
	radeon_ring_write(rdev, (x1 << 0) | (y1 << 16) | (1 << 31));
	radeon_ring_write(rdev, (x2 << 0) | (y2 << 16));
}

/* emits 10 */
static void
draw_auto(struct radeon_device *rdev)
{
	radeon_ring_write(rdev, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	radeon_ring_write(rdev, (VGT_PRIMITIVE_TYPE - PACKET3_SET_CONFIG_REG_START) >> 2);
	radeon_ring_write(rdev, DI_PT_RECTLIST);

	radeon_ring_write(rdev, PACKET3(PACKET3_INDEX_TYPE, 0));
	radeon_ring_write(rdev,
#ifdef __BIG_ENDIAN
			  (2 << 2) |
#endif
			  DI_INDEX_SIZE_16_BIT);

	radeon_ring_write(rdev, PACKET3(PACKET3_NUM_INSTANCES, 0));
	radeon_ring_write(rdev, 1);

	radeon_ring_write(rdev, PACKET3(PACKET3_DRAW_INDEX_AUTO, 1));
	radeon_ring_write(rdev, 3);
	radeon_ring_write(rdev, DI_SRC_SEL_AUTO_INDEX);

}

/* emits 39 */
static void
set_default_state(struct radeon_device *rdev)
{
	u32 sq_config, sq_gpr_resource_mgmt_1, sq_gpr_resource_mgmt_2, sq_gpr_resource_mgmt_3;
	u32 sq_thread_resource_mgmt, sq_thread_resource_mgmt_2;
	u32 sq_stack_resource_mgmt_1, sq_stack_resource_mgmt_2, sq_stack_resource_mgmt_3;
	int num_ps_gprs, num_vs_gprs, num_temp_gprs;
	int num_gs_gprs, num_es_gprs, num_hs_gprs, num_ls_gprs;
	int num_ps_threads, num_vs_threads, num_gs_threads, num_es_threads;
	int num_hs_threads, num_ls_threads;
	int num_ps_stack_entries, num_vs_stack_entries, num_gs_stack_entries, num_es_stack_entries;
	int num_hs_stack_entries, num_ls_stack_entries;
	u64 gpu_addr;
	int dwords;

	/* set clear context state */
	radeon_ring_write(rdev, PACKET3(PACKET3_CLEAR_STATE, 0));
	radeon_ring_write(rdev, 0);

	if (rdev->family < CHIP_CAYMAN) {
		switch (rdev->family) {
		case CHIP_CEDAR:
		default:
			num_ps_gprs = 93;
			num_vs_gprs = 46;
			num_temp_gprs = 4;
			num_gs_gprs = 31;
			num_es_gprs = 31;
			num_hs_gprs = 23;
			num_ls_gprs = 23;
			num_ps_threads = 96;
			num_vs_threads = 16;
			num_gs_threads = 16;
			num_es_threads = 16;
			num_hs_threads = 16;
			num_ls_threads = 16;
			num_ps_stack_entries = 42;
			num_vs_stack_entries = 42;
			num_gs_stack_entries = 42;
			num_es_stack_entries = 42;
			num_hs_stack_entries = 42;
			num_ls_stack_entries = 42;
			break;
		case CHIP_REDWOOD:
			num_ps_gprs = 93;
			num_vs_gprs = 46;
			num_temp_gprs = 4;
			num_gs_gprs = 31;
			num_es_gprs = 31;
			num_hs_gprs = 23;
			num_ls_gprs = 23;
			num_ps_threads = 128;
			num_vs_threads = 20;
			num_gs_threads = 20;
			num_es_threads = 20;
			num_hs_threads = 20;
			num_ls_threads = 20;
			num_ps_stack_entries = 42;
			num_vs_stack_entries = 42;
			num_gs_stack_entries = 42;
			num_es_stack_entries = 42;
			num_hs_stack_entries = 42;
			num_ls_stack_entries = 42;
			break;
		case CHIP_JUNIPER:
			num_ps_gprs = 93;
			num_vs_gprs = 46;
			num_temp_gprs = 4;
			num_gs_gprs = 31;
			num_es_gprs = 31;
			num_hs_gprs = 23;
			num_ls_gprs = 23;
			num_ps_threads = 128;
			num_vs_threads = 20;
			num_gs_threads = 20;
			num_es_threads = 20;
			num_hs_threads = 20;
			num_ls_threads = 20;
			num_ps_stack_entries = 85;
			num_vs_stack_entries = 85;
			num_gs_stack_entries = 85;
			num_es_stack_entries = 85;
			num_hs_stack_entries = 85;
			num_ls_stack_entries = 85;
			break;
		case CHIP_CYPRESS:
		case CHIP_HEMLOCK:
			num_ps_gprs = 93;
			num_vs_gprs = 46;
			num_temp_gprs = 4;
			num_gs_gprs = 31;
			num_es_gprs = 31;
			num_hs_gprs = 23;
			num_ls_gprs = 23;
			num_ps_threads = 128;
			num_vs_threads = 20;
			num_gs_threads = 20;
			num_es_threads = 20;
			num_hs_threads = 20;
			num_ls_threads = 20;
			num_ps_stack_entries = 85;
			num_vs_stack_entries = 85;
			num_gs_stack_entries = 85;
			num_es_stack_entries = 85;
			num_hs_stack_entries = 85;
			num_ls_stack_entries = 85;
			break;
		case CHIP_PALM:
			num_ps_gprs = 93;
			num_vs_gprs = 46;
			num_temp_gprs = 4;
			num_gs_gprs = 31;
			num_es_gprs = 31;
			num_hs_gprs = 23;
			num_ls_gprs = 23;
			num_ps_threads = 96;
			num_vs_threads = 16;
			num_gs_threads = 16;
			num_es_threads = 16;
			num_hs_threads = 16;
			num_ls_threads = 16;
			num_ps_stack_entries = 42;
			num_vs_stack_entries = 42;
			num_gs_stack_entries = 42;
			num_es_stack_entries = 42;
			num_hs_stack_entries = 42;
			num_ls_stack_entries = 42;
			break;
		case CHIP_SUMO:
			num_ps_gprs = 93;
			num_vs_gprs = 46;
			num_temp_gprs = 4;
			num_gs_gprs = 31;
			num_es_gprs = 31;
			num_hs_gprs = 23;
			num_ls_gprs = 23;
			num_ps_threads = 96;
			num_vs_threads = 25;
			num_gs_threads = 25;
			num_es_threads = 25;
			num_hs_threads = 25;
			num_ls_threads = 25;
			num_ps_stack_entries = 42;
			num_vs_stack_entries = 42;
			num_gs_stack_entries = 42;
			num_es_stack_entries = 42;
			num_hs_stack_entries = 42;
			num_ls_stack_entries = 42;
			break;
		case CHIP_SUMO2:
			num_ps_gprs = 93;
			num_vs_gprs = 46;
			num_temp_gprs = 4;
			num_gs_gprs = 31;
			num_es_gprs = 31;
			num_hs_gprs = 23;
			num_ls_gprs = 23;
			num_ps_threads = 96;
			num_vs_threads = 25;
			num_gs_threads = 25;
			num_es_threads = 25;
			num_hs_threads = 25;
			num_ls_threads = 25;
			num_ps_stack_entries = 85;
			num_vs_stack_entries = 85;
			num_gs_stack_entries = 85;
			num_es_stack_entries = 85;
			num_hs_stack_entries = 85;
			num_ls_stack_entries = 85;
			break;
		case CHIP_BARTS:
			num_ps_gprs = 93;
			num_vs_gprs = 46;
			num_temp_gprs = 4;
			num_gs_gprs = 31;
			num_es_gprs = 31;
			num_hs_gprs = 23;
			num_ls_gprs = 23;
			num_ps_threads = 128;
			num_vs_threads = 20;
			num_gs_threads = 20;
			num_es_threads = 20;
			num_hs_threads = 20;
			num_ls_threads = 20;
			num_ps_stack_entries = 85;
			num_vs_stack_entries = 85;
			num_gs_stack_entries = 85;
			num_es_stack_entries = 85;
			num_hs_stack_entries = 85;
			num_ls_stack_entries = 85;
			break;
		case CHIP_TURKS:
			num_ps_gprs = 93;
			num_vs_gprs = 46;
			num_temp_gprs = 4;
			num_gs_gprs = 31;
			num_es_gprs = 31;
			num_hs_gprs = 23;
			num_ls_gprs = 23;
			num_ps_threads = 128;
			num_vs_threads = 20;
			num_gs_threads = 20;
			num_es_threads = 20;
			num_hs_threads = 20;
			num_ls_threads = 20;
			num_ps_stack_entries = 42;
			num_vs_stack_entries = 42;
			num_gs_stack_entries = 42;
			num_es_stack_entries = 42;
			num_hs_stack_entries = 42;
			num_ls_stack_entries = 42;
			break;
		case CHIP_CAICOS:
			num_ps_gprs = 93;
			num_vs_gprs = 46;
			num_temp_gprs = 4;
			num_gs_gprs = 31;
			num_es_gprs = 31;
			num_hs_gprs = 23;
			num_ls_gprs = 23;
			num_ps_threads = 128;
			num_vs_threads = 10;
			num_gs_threads = 10;
			num_es_threads = 10;
			num_hs_threads = 10;
			num_ls_threads = 10;
			num_ps_stack_entries = 42;
			num_vs_stack_entries = 42;
			num_gs_stack_entries = 42;
			num_es_stack_entries = 42;
			num_hs_stack_entries = 42;
			num_ls_stack_entries = 42;
			break;
		}

		if ((rdev->family == CHIP_CEDAR) ||
		    (rdev->family == CHIP_PALM) ||
		    (rdev->family == CHIP_SUMO) ||
		    (rdev->family == CHIP_SUMO2) ||
		    (rdev->family == CHIP_CAICOS))
			sq_config = 0;
		else
			sq_config = VC_ENABLE;

		sq_config |= (EXPORT_SRC_C |
			      CS_PRIO(0) |
			      LS_PRIO(0) |
			      HS_PRIO(0) |
			      PS_PRIO(0) |
			      VS_PRIO(1) |
			      GS_PRIO(2) |
			      ES_PRIO(3));

		sq_gpr_resource_mgmt_1 = (NUM_PS_GPRS(num_ps_gprs) |
					  NUM_VS_GPRS(num_vs_gprs) |
					  NUM_CLAUSE_TEMP_GPRS(num_temp_gprs));
		sq_gpr_resource_mgmt_2 = (NUM_GS_GPRS(num_gs_gprs) |
					  NUM_ES_GPRS(num_es_gprs));
		sq_gpr_resource_mgmt_3 = (NUM_HS_GPRS(num_hs_gprs) |
					  NUM_LS_GPRS(num_ls_gprs));
		sq_thread_resource_mgmt = (NUM_PS_THREADS(num_ps_threads) |
					   NUM_VS_THREADS(num_vs_threads) |
					   NUM_GS_THREADS(num_gs_threads) |
					   NUM_ES_THREADS(num_es_threads));
		sq_thread_resource_mgmt_2 = (NUM_HS_THREADS(num_hs_threads) |
					     NUM_LS_THREADS(num_ls_threads));
		sq_stack_resource_mgmt_1 = (NUM_PS_STACK_ENTRIES(num_ps_stack_entries) |
					    NUM_VS_STACK_ENTRIES(num_vs_stack_entries));
		sq_stack_resource_mgmt_2 = (NUM_GS_STACK_ENTRIES(num_gs_stack_entries) |
					    NUM_ES_STACK_ENTRIES(num_es_stack_entries));
		sq_stack_resource_mgmt_3 = (NUM_HS_STACK_ENTRIES(num_hs_stack_entries) |
					    NUM_LS_STACK_ENTRIES(num_ls_stack_entries));

		/* disable dyn gprs */
		radeon_ring_write(rdev, PACKET3(PACKET3_SET_CONFIG_REG, 1));
		radeon_ring_write(rdev, (SQ_DYN_GPR_CNTL_PS_FLUSH_REQ - PACKET3_SET_CONFIG_REG_START) >> 2);
		radeon_ring_write(rdev, 0);

		/* setup LDS */
		radeon_ring_write(rdev, PACKET3(PACKET3_SET_CONFIG_REG, 1));
		radeon_ring_write(rdev, (SQ_LDS_RESOURCE_MGMT - PACKET3_SET_CONFIG_REG_START) >> 2);
		radeon_ring_write(rdev, 0x10001000);

		/* SQ config */
		radeon_ring_write(rdev, PACKET3(PACKET3_SET_CONFIG_REG, 11));
		radeon_ring_write(rdev, (SQ_CONFIG - PACKET3_SET_CONFIG_REG_START) >> 2);
		radeon_ring_write(rdev, sq_config);
		radeon_ring_write(rdev, sq_gpr_resource_mgmt_1);
		radeon_ring_write(rdev, sq_gpr_resource_mgmt_2);
		radeon_ring_write(rdev, sq_gpr_resource_mgmt_3);
		radeon_ring_write(rdev, 0);
		radeon_ring_write(rdev, 0);
		radeon_ring_write(rdev, sq_thread_resource_mgmt);
		radeon_ring_write(rdev, sq_thread_resource_mgmt_2);
		radeon_ring_write(rdev, sq_stack_resource_mgmt_1);
		radeon_ring_write(rdev, sq_stack_resource_mgmt_2);
		radeon_ring_write(rdev, sq_stack_resource_mgmt_3);
	}

	/* CONTEXT_CONTROL */
	radeon_ring_write(rdev, 0xc0012800);
	radeon_ring_write(rdev, 0x80000000);
	radeon_ring_write(rdev, 0x80000000);

	/* SQ_VTX_BASE_VTX_LOC */
	radeon_ring_write(rdev, 0xc0026f00);
	radeon_ring_write(rdev, 0x00000000);
	radeon_ring_write(rdev, 0x00000000);
	radeon_ring_write(rdev, 0x00000000);

	/* SET_SAMPLER */
	radeon_ring_write(rdev, 0xc0036e00);
	radeon_ring_write(rdev, 0x00000000);
	radeon_ring_write(rdev, 0x00000012);
	radeon_ring_write(rdev, 0x00000000);
	radeon_ring_write(rdev, 0x00000000);

	/* set to DX10/11 mode */
	radeon_ring_write(rdev, PACKET3(PACKET3_MODE_CONTROL, 0));
	radeon_ring_write(rdev, 1);

	/* emit an IB pointing at default state */
	dwords = ALIGN(rdev->r600_blit.state_len, 0x10);
	gpu_addr = rdev->r600_blit.shader_gpu_addr + rdev->r600_blit.state_offset;
	radeon_ring_write(rdev, PACKET3(PACKET3_INDIRECT_BUFFER, 2));
	radeon_ring_write(rdev, gpu_addr & 0xFFFFFFFC);
	radeon_ring_write(rdev, upper_32_bits(gpu_addr) & 0xFF);
	radeon_ring_write(rdev, dwords);

}

static inline uint32_t i2f(uint32_t input)
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

int evergreen_blit_init(struct radeon_device *rdev)
{
	u32 obj_size;
	int i, r, dwords;
	void *ptr;
	u32 packet2s[16];
	int num_packet2s = 0;

	/* pin copy shader into vram if already initialized */
	if (rdev->r600_blit.shader_obj)
		goto done;

	mutex_init(&rdev->r600_blit.mutex);
	rdev->r600_blit.state_offset = 0;

	if (rdev->family < CHIP_CAYMAN)
		rdev->r600_blit.state_len = evergreen_default_size;
	else
		rdev->r600_blit.state_len = cayman_default_size;

	dwords = rdev->r600_blit.state_len;
	while (dwords & 0xf) {
		packet2s[num_packet2s++] = cpu_to_le32(PACKET2(0));
		dwords++;
	}

	obj_size = dwords * 4;
	obj_size = ALIGN(obj_size, 256);

	rdev->r600_blit.vs_offset = obj_size;
	if (rdev->family < CHIP_CAYMAN)
		obj_size += evergreen_vs_size * 4;
	else
		obj_size += cayman_vs_size * 4;
	obj_size = ALIGN(obj_size, 256);

	rdev->r600_blit.ps_offset = obj_size;
	if (rdev->family < CHIP_CAYMAN)
		obj_size += evergreen_ps_size * 4;
	else
		obj_size += cayman_ps_size * 4;
	obj_size = ALIGN(obj_size, 256);

	r = radeon_bo_create(rdev, obj_size, PAGE_SIZE, true, RADEON_GEM_DOMAIN_VRAM,
				&rdev->r600_blit.shader_obj);
	if (r) {
		DRM_ERROR("evergreen failed to allocate shader\n");
		return r;
	}

	DRM_DEBUG("evergreen blit allocated bo %08x vs %08x ps %08x\n",
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

	if (rdev->family < CHIP_CAYMAN) {
		memcpy_toio(ptr + rdev->r600_blit.state_offset,
			    evergreen_default_state, rdev->r600_blit.state_len * 4);

		if (num_packet2s)
			memcpy_toio(ptr + rdev->r600_blit.state_offset + (rdev->r600_blit.state_len * 4),
				    packet2s, num_packet2s * 4);
		for (i = 0; i < evergreen_vs_size; i++)
			*(u32 *)((unsigned long)ptr + rdev->r600_blit.vs_offset + i * 4) = cpu_to_le32(evergreen_vs[i]);
		for (i = 0; i < evergreen_ps_size; i++)
			*(u32 *)((unsigned long)ptr + rdev->r600_blit.ps_offset + i * 4) = cpu_to_le32(evergreen_ps[i]);
	} else {
		memcpy_toio(ptr + rdev->r600_blit.state_offset,
			    cayman_default_state, rdev->r600_blit.state_len * 4);

		if (num_packet2s)
			memcpy_toio(ptr + rdev->r600_blit.state_offset + (rdev->r600_blit.state_len * 4),
				    packet2s, num_packet2s * 4);
		for (i = 0; i < cayman_vs_size; i++)
			*(u32 *)((unsigned long)ptr + rdev->r600_blit.vs_offset + i * 4) = cpu_to_le32(cayman_vs[i]);
		for (i = 0; i < cayman_ps_size; i++)
			*(u32 *)((unsigned long)ptr + rdev->r600_blit.ps_offset + i * 4) = cpu_to_le32(cayman_ps[i]);
	}
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

void evergreen_blit_fini(struct radeon_device *rdev)
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

static int evergreen_vb_ib_get(struct radeon_device *rdev)
{
	int r;
	r = radeon_ib_get(rdev, &rdev->r600_blit.vb_ib);
	if (r) {
		DRM_ERROR("failed to get IB for vertex buffer\n");
		return r;
	}

	rdev->r600_blit.vb_total = 64*1024;
	rdev->r600_blit.vb_used = 0;
	return 0;
}

static void evergreen_vb_ib_put(struct radeon_device *rdev)
{
	radeon_fence_emit(rdev, rdev->r600_blit.vb_ib->fence);
	radeon_ib_free(rdev, &rdev->r600_blit.vb_ib);
}

int evergreen_blit_prepare_copy(struct radeon_device *rdev, int size_bytes)
{
	int r;
	int ring_size, line_size;
	int max_size;
	/* loops of emits + fence emit possible */
	int dwords_per_loop = 74, num_loops;

	r = evergreen_vb_ib_get(rdev);
	if (r)
		return r;

	/* 8 bpp vs 32 bpp for xfer unit */
	if (size_bytes & 3)
		line_size = 8192;
	else
		line_size = 8192 * 4;

	max_size = 8192 * line_size;

	/* major loops cover the max size transfer */
	num_loops = ((size_bytes + max_size) / max_size);
	/* minor loops cover the extra non aligned bits */
	num_loops += ((size_bytes % line_size) ? 1 : 0);
	/* calculate number of loops correctly */
	ring_size = num_loops * dwords_per_loop;
	/* set default  + shaders */
	ring_size += 55; /* shaders + def state */
	ring_size += 10; /* fence emit for VB IB */
	ring_size += 5; /* done copy */
	ring_size += 10; /* fence emit for done copy */
	r = radeon_ring_lock(rdev, ring_size);
	if (r)
		return r;

	set_default_state(rdev); /* 36 */
	set_shaders(rdev); /* 16 */
	return 0;
}

void evergreen_blit_done_copy(struct radeon_device *rdev, struct radeon_fence *fence)
{
	int r;

	if (rdev->r600_blit.vb_ib)
		evergreen_vb_ib_put(rdev);

	if (fence)
		r = radeon_fence_emit(rdev, fence);

	radeon_ring_unlock_commit(rdev);
}

void evergreen_kms_blit_copy(struct radeon_device *rdev,
			     u64 src_gpu_addr, u64 dst_gpu_addr,
			     int size_bytes)
{
	int max_bytes;
	u64 vb_gpu_addr;
	u32 *vb;

	DRM_DEBUG("emitting copy %16llx %16llx %d %d\n", src_gpu_addr, dst_gpu_addr,
		  size_bytes, rdev->r600_blit.vb_used);
	vb = (u32 *)(rdev->r600_blit.vb_ib->ptr + rdev->r600_blit.vb_used);
	if ((size_bytes & 3) || (src_gpu_addr & 3) || (dst_gpu_addr & 3)) {
		max_bytes = 8192;

		while (size_bytes) {
			int cur_size = size_bytes;
			int src_x = src_gpu_addr & 255;
			int dst_x = dst_gpu_addr & 255;
			int h = 1;
			src_gpu_addr = src_gpu_addr & ~255ULL;
			dst_gpu_addr = dst_gpu_addr & ~255ULL;

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

			if ((rdev->r600_blit.vb_used + 48) > rdev->r600_blit.vb_total) {
				WARN_ON(1);
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

			/* src 10 */
			set_tex_resource(rdev, FMT_8,
					 src_x + cur_size, h, src_x + cur_size,
					 src_gpu_addr);

			/* 5 */
			cp_set_surface_sync(rdev,
					    PACKET3_TC_ACTION_ENA, (src_x + cur_size * h), src_gpu_addr);


			/* dst 17 */
			set_render_target(rdev, COLOR_8,
					  dst_x + cur_size, h,
					  dst_gpu_addr);

			/* scissors 12 */
			set_scissors(rdev, dst_x, 0, dst_x + cur_size, h);

			/* 15 */
			vb_gpu_addr = rdev->r600_blit.vb_ib->gpu_addr + rdev->r600_blit.vb_used;
			set_vtx_resource(rdev, vb_gpu_addr);

			/* draw 10 */
			draw_auto(rdev);

			/* 5 */
			cp_set_surface_sync(rdev,
					    PACKET3_CB_ACTION_ENA | PACKET3_CB0_DEST_BASE_ENA,
					    cur_size * h, dst_gpu_addr);

			vb += 12;
			rdev->r600_blit.vb_used += 12 * 4;

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
			src_gpu_addr = src_gpu_addr & ~255ULL;
			dst_gpu_addr = dst_gpu_addr & ~255ULL;

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

			if ((rdev->r600_blit.vb_used + 48) > rdev->r600_blit.vb_total) {
				WARN_ON(1);
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

			/* src 10 */
			set_tex_resource(rdev, FMT_8_8_8_8,
					 (src_x + cur_size) / 4,
					 h, (src_x + cur_size) / 4,
					 src_gpu_addr);
			/* 5 */
			cp_set_surface_sync(rdev,
					    PACKET3_TC_ACTION_ENA, (src_x + cur_size * h), src_gpu_addr);

			/* dst 17 */
			set_render_target(rdev, COLOR_8_8_8_8,
					  (dst_x + cur_size) / 4, h,
					  dst_gpu_addr);

			/* scissors 12  */
			set_scissors(rdev, (dst_x / 4), 0, (dst_x + cur_size / 4), h);

			/* Vertex buffer setup 15 */
			vb_gpu_addr = rdev->r600_blit.vb_ib->gpu_addr + rdev->r600_blit.vb_used;
			set_vtx_resource(rdev, vb_gpu_addr);

			/* draw 10 */
			draw_auto(rdev);

			/* 5 */
			cp_set_surface_sync(rdev,
					    PACKET3_CB_ACTION_ENA | PACKET3_CB0_DEST_BASE_ENA,
					    cur_size * h, dst_gpu_addr);

			/* 74 ring dwords per loop */
			vb += 12;
			rdev->r600_blit.vb_used += 12 * 4;

			src_gpu_addr += cur_size * h;
			dst_gpu_addr += cur_size * h;
			size_bytes -= cur_size * h;
		}
	}
}

