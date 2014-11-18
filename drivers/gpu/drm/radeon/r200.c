/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include <drm/drmP.h>
#include <drm/radeon_drm.h>
#include "radeon_reg.h"
#include "radeon.h"
#include "radeon_asic.h"

#include "r100d.h"
#include "r200_reg_safe.h"

#include "r100_track.h"

static int r200_get_vtx_size_0(uint32_t vtx_fmt_0)
{
	int vtx_size, i;
	vtx_size = 2;

	if (vtx_fmt_0 & R200_VTX_Z0)
		vtx_size++;
	if (vtx_fmt_0 & R200_VTX_W0)
		vtx_size++;
	/* blend weight */
	if (vtx_fmt_0 & (0x7 << R200_VTX_WEIGHT_COUNT_SHIFT))
		vtx_size += (vtx_fmt_0 >> R200_VTX_WEIGHT_COUNT_SHIFT) & 0x7;
	if (vtx_fmt_0 & R200_VTX_PV_MATRIX_SEL)
		vtx_size++;
	if (vtx_fmt_0 & R200_VTX_N0)
		vtx_size += 3;
	if (vtx_fmt_0 & R200_VTX_POINT_SIZE)
		vtx_size++;
	if (vtx_fmt_0 & R200_VTX_DISCRETE_FOG)
		vtx_size++;
	if (vtx_fmt_0 & R200_VTX_SHININESS_0)
		vtx_size++;
	if (vtx_fmt_0 & R200_VTX_SHININESS_1)
		vtx_size++;
	for (i = 0; i < 8; i++) {
		int color_size = (vtx_fmt_0 >> (11 + 2*i)) & 0x3;
		switch (color_size) {
		case 0: break;
		case 1: vtx_size++; break;
		case 2: vtx_size += 3; break;
		case 3: vtx_size += 4; break;
		}
	}
	if (vtx_fmt_0 & R200_VTX_XY1)
		vtx_size += 2;
	if (vtx_fmt_0 & R200_VTX_Z1)
		vtx_size++;
	if (vtx_fmt_0 & R200_VTX_W1)
		vtx_size++;
	if (vtx_fmt_0 & R200_VTX_N1)
		vtx_size += 3;
	return vtx_size;
}

struct radeon_fence *r200_copy_dma(struct radeon_device *rdev,
				   uint64_t src_offset,
				   uint64_t dst_offset,
				   unsigned num_gpu_pages,
				   struct reservation_object *resv)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	struct radeon_fence *fence;
	uint32_t size;
	uint32_t cur_size;
	int i, num_loops;
	int r = 0;

	/* radeon pitch is /64 */
	size = num_gpu_pages << RADEON_GPU_PAGE_SHIFT;
	num_loops = DIV_ROUND_UP(size, 0x1FFFFF);
	r = radeon_ring_lock(rdev, ring, num_loops * 4 + 64);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d).\n", r);
		return ERR_PTR(r);
	}
	/* Must wait for 2D idle & clean before DMA or hangs might happen */
	radeon_ring_write(ring, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(ring, (1 << 16));
	for (i = 0; i < num_loops; i++) {
		cur_size = size;
		if (cur_size > 0x1FFFFF) {
			cur_size = 0x1FFFFF;
		}
		size -= cur_size;
		radeon_ring_write(ring, PACKET0(0x720, 2));
		radeon_ring_write(ring, src_offset);
		radeon_ring_write(ring, dst_offset);
		radeon_ring_write(ring, cur_size | (1 << 31) | (1 << 30));
		src_offset += cur_size;
		dst_offset += cur_size;
	}
	radeon_ring_write(ring, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(ring, RADEON_WAIT_DMA_GUI_IDLE);
	r = radeon_fence_emit(rdev, &fence, RADEON_RING_TYPE_GFX_INDEX);
	if (r) {
		radeon_ring_unlock_undo(rdev, ring);
		return ERR_PTR(r);
	}
	radeon_ring_unlock_commit(rdev, ring, false);
	return fence;
}


static int r200_get_vtx_size_1(uint32_t vtx_fmt_1)
{
	int vtx_size, i, tex_size;
	vtx_size = 0;
	for (i = 0; i < 6; i++) {
		tex_size = (vtx_fmt_1 >> (i * 3)) & 0x7;
		if (tex_size > 4)
			continue;
		vtx_size += tex_size;
	}
	return vtx_size;
}

int r200_packet0_check(struct radeon_cs_parser *p,
		       struct radeon_cs_packet *pkt,
		       unsigned idx, unsigned reg)
{
	struct radeon_cs_reloc *reloc;
	struct r100_cs_track *track;
	volatile uint32_t *ib;
	uint32_t tmp;
	int r;
	int i;
	int face;
	u32 tile_flags = 0;
	u32 idx_value;

	ib = p->ib.ptr;
	track = (struct r100_cs_track *)p->track;
	idx_value = radeon_get_ib_value(p, idx);
	switch (reg) {
	case RADEON_CRTC_GUI_TRIG_VLINE:
		r = r100_cs_packet_parse_vline(p);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		break;
		/* FIXME: only allow PACKET3 blit? easier to check for out of
		 * range access */
	case RADEON_DST_PITCH_OFFSET:
	case RADEON_SRC_PITCH_OFFSET:
		r = r100_reloc_pitch_offset(p, pkt, idx, reg);
		if (r)
			return r;
		break;
	case RADEON_RB3D_DEPTHOFFSET:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		track->zb.robj = reloc->robj;
		track->zb.offset = idx_value;
		track->zb_dirty = true;
		ib[idx] = idx_value + ((u32)reloc->gpu_offset);
		break;
	case RADEON_RB3D_COLOROFFSET:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		track->cb[0].robj = reloc->robj;
		track->cb[0].offset = idx_value;
		track->cb_dirty = true;
		ib[idx] = idx_value + ((u32)reloc->gpu_offset);
		break;
	case R200_PP_TXOFFSET_0:
	case R200_PP_TXOFFSET_1:
	case R200_PP_TXOFFSET_2:
	case R200_PP_TXOFFSET_3:
	case R200_PP_TXOFFSET_4:
	case R200_PP_TXOFFSET_5:
		i = (reg - R200_PP_TXOFFSET_0) / 24;
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
			if (reloc->tiling_flags & RADEON_TILING_MACRO)
				tile_flags |= R200_TXO_MACRO_TILE;
			if (reloc->tiling_flags & RADEON_TILING_MICRO)
				tile_flags |= R200_TXO_MICRO_TILE;

			tmp = idx_value & ~(0x7 << 2);
			tmp |= tile_flags;
			ib[idx] = tmp + ((u32)reloc->gpu_offset);
		} else
			ib[idx] = idx_value + ((u32)reloc->gpu_offset);
		track->textures[i].robj = reloc->robj;
		track->tex_dirty = true;
		break;
	case R200_PP_CUBIC_OFFSET_F1_0:
	case R200_PP_CUBIC_OFFSET_F2_0:
	case R200_PP_CUBIC_OFFSET_F3_0:
	case R200_PP_CUBIC_OFFSET_F4_0:
	case R200_PP_CUBIC_OFFSET_F5_0:
	case R200_PP_CUBIC_OFFSET_F1_1:
	case R200_PP_CUBIC_OFFSET_F2_1:
	case R200_PP_CUBIC_OFFSET_F3_1:
	case R200_PP_CUBIC_OFFSET_F4_1:
	case R200_PP_CUBIC_OFFSET_F5_1:
	case R200_PP_CUBIC_OFFSET_F1_2:
	case R200_PP_CUBIC_OFFSET_F2_2:
	case R200_PP_CUBIC_OFFSET_F3_2:
	case R200_PP_CUBIC_OFFSET_F4_2:
	case R200_PP_CUBIC_OFFSET_F5_2:
	case R200_PP_CUBIC_OFFSET_F1_3:
	case R200_PP_CUBIC_OFFSET_F2_3:
	case R200_PP_CUBIC_OFFSET_F3_3:
	case R200_PP_CUBIC_OFFSET_F4_3:
	case R200_PP_CUBIC_OFFSET_F5_3:
	case R200_PP_CUBIC_OFFSET_F1_4:
	case R200_PP_CUBIC_OFFSET_F2_4:
	case R200_PP_CUBIC_OFFSET_F3_4:
	case R200_PP_CUBIC_OFFSET_F4_4:
	case R200_PP_CUBIC_OFFSET_F5_4:
	case R200_PP_CUBIC_OFFSET_F1_5:
	case R200_PP_CUBIC_OFFSET_F2_5:
	case R200_PP_CUBIC_OFFSET_F3_5:
	case R200_PP_CUBIC_OFFSET_F4_5:
	case R200_PP_CUBIC_OFFSET_F5_5:
		i = (reg - R200_PP_TXOFFSET_0) / 24;
		face = (reg - ((i * 24) + R200_PP_TXOFFSET_0)) / 4;
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		track->textures[i].cube_info[face - 1].offset = idx_value;
		ib[idx] = idx_value + ((u32)reloc->gpu_offset);
		track->textures[i].cube_info[face - 1].robj = reloc->robj;
		track->tex_dirty = true;
		break;
	case RADEON_RE_WIDTH_HEIGHT:
		track->maxy = ((idx_value >> 16) & 0x7FF);
		track->cb_dirty = true;
		track->zb_dirty = true;
		break;
	case RADEON_RB3D_COLORPITCH:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}

		if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
			if (reloc->tiling_flags & RADEON_TILING_MACRO)
				tile_flags |= RADEON_COLOR_TILE_ENABLE;
			if (reloc->tiling_flags & RADEON_TILING_MICRO)
				tile_flags |= RADEON_COLOR_MICROTILE_ENABLE;

			tmp = idx_value & ~(0x7 << 16);
			tmp |= tile_flags;
			ib[idx] = tmp;
		} else
			ib[idx] = idx_value;

		track->cb[0].pitch = idx_value & RADEON_COLORPITCH_MASK;
		track->cb_dirty = true;
		break;
	case RADEON_RB3D_DEPTHPITCH:
		track->zb.pitch = idx_value & RADEON_DEPTHPITCH_MASK;
		track->zb_dirty = true;
		break;
	case RADEON_RB3D_CNTL:
		switch ((idx_value >> RADEON_RB3D_COLOR_FORMAT_SHIFT) & 0x1f) {
		case 7:
		case 8:
		case 9:
		case 11:
		case 12:
			track->cb[0].cpp = 1;
			break;
		case 3:
		case 4:
		case 15:
			track->cb[0].cpp = 2;
			break;
		case 6:
			track->cb[0].cpp = 4;
			break;
		default:
			DRM_ERROR("Invalid color buffer format (%d) !\n",
				  ((idx_value >> RADEON_RB3D_COLOR_FORMAT_SHIFT) & 0x1f));
			return -EINVAL;
		}
		if (idx_value & RADEON_DEPTHXY_OFFSET_ENABLE) {
			DRM_ERROR("No support for depth xy offset in kms\n");
			return -EINVAL;
		}

		track->z_enabled = !!(idx_value & RADEON_Z_ENABLE);
		track->cb_dirty = true;
		track->zb_dirty = true;
		break;
	case RADEON_RB3D_ZSTENCILCNTL:
		switch (idx_value & 0xf) {
		case 0:
			track->zb.cpp = 2;
			break;
		case 2:
		case 3:
		case 4:
		case 5:
		case 9:
		case 11:
			track->zb.cpp = 4;
			break;
		default:
			break;
		}
		track->zb_dirty = true;
		break;
	case RADEON_RB3D_ZPASS_ADDR:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		ib[idx] = idx_value + ((u32)reloc->gpu_offset);
		break;
	case RADEON_PP_CNTL:
		{
			uint32_t temp = idx_value >> 4;
			for (i = 0; i < track->num_texture; i++)
				track->textures[i].enabled = !!(temp & (1 << i));
			track->tex_dirty = true;
		}
		break;
	case RADEON_SE_VF_CNTL:
		track->vap_vf_cntl = idx_value;
		break;
	case 0x210c:
		/* VAP_VF_MAX_VTX_INDX */
		track->max_indx = idx_value & 0x00FFFFFFUL;
		break;
	case R200_SE_VTX_FMT_0:
		track->vtx_size = r200_get_vtx_size_0(idx_value);
		break;
	case R200_SE_VTX_FMT_1:
		track->vtx_size += r200_get_vtx_size_1(idx_value);
		break;
	case R200_PP_TXSIZE_0:
	case R200_PP_TXSIZE_1:
	case R200_PP_TXSIZE_2:
	case R200_PP_TXSIZE_3:
	case R200_PP_TXSIZE_4:
	case R200_PP_TXSIZE_5:
		i = (reg - R200_PP_TXSIZE_0) / 32;
		track->textures[i].width = (idx_value & RADEON_TEX_USIZE_MASK) + 1;
		track->textures[i].height = ((idx_value & RADEON_TEX_VSIZE_MASK) >> RADEON_TEX_VSIZE_SHIFT) + 1;
		track->tex_dirty = true;
		break;
	case R200_PP_TXPITCH_0:
	case R200_PP_TXPITCH_1:
	case R200_PP_TXPITCH_2:
	case R200_PP_TXPITCH_3:
	case R200_PP_TXPITCH_4:
	case R200_PP_TXPITCH_5:
		i = (reg - R200_PP_TXPITCH_0) / 32;
		track->textures[i].pitch = idx_value + 32;
		track->tex_dirty = true;
		break;
	case R200_PP_TXFILTER_0:
	case R200_PP_TXFILTER_1:
	case R200_PP_TXFILTER_2:
	case R200_PP_TXFILTER_3:
	case R200_PP_TXFILTER_4:
	case R200_PP_TXFILTER_5:
		i = (reg - R200_PP_TXFILTER_0) / 32;
		track->textures[i].num_levels = ((idx_value & R200_MAX_MIP_LEVEL_MASK)
						 >> R200_MAX_MIP_LEVEL_SHIFT);
		tmp = (idx_value >> 23) & 0x7;
		if (tmp == 2 || tmp == 6)
			track->textures[i].roundup_w = false;
		tmp = (idx_value >> 27) & 0x7;
		if (tmp == 2 || tmp == 6)
			track->textures[i].roundup_h = false;
		track->tex_dirty = true;
		break;
	case R200_PP_TXMULTI_CTL_0:
	case R200_PP_TXMULTI_CTL_1:
	case R200_PP_TXMULTI_CTL_2:
	case R200_PP_TXMULTI_CTL_3:
	case R200_PP_TXMULTI_CTL_4:
	case R200_PP_TXMULTI_CTL_5:
		i = (reg - R200_PP_TXMULTI_CTL_0) / 32;
		break;
	case R200_PP_TXFORMAT_X_0:
	case R200_PP_TXFORMAT_X_1:
	case R200_PP_TXFORMAT_X_2:
	case R200_PP_TXFORMAT_X_3:
	case R200_PP_TXFORMAT_X_4:
	case R200_PP_TXFORMAT_X_5:
		i = (reg - R200_PP_TXFORMAT_X_0) / 32;
		track->textures[i].txdepth = idx_value & 0x7;
		tmp = (idx_value >> 16) & 0x3;
		/* 2D, 3D, CUBE */
		switch (tmp) {
		case 0:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			/* 1D/2D */
			track->textures[i].tex_coord_type = 0;
			break;
		case 1:
			/* CUBE */
			track->textures[i].tex_coord_type = 2;
			break;
		case 2:
			/* 3D */
			track->textures[i].tex_coord_type = 1;
			break;
		}
		track->tex_dirty = true;
		break;
	case R200_PP_TXFORMAT_0:
	case R200_PP_TXFORMAT_1:
	case R200_PP_TXFORMAT_2:
	case R200_PP_TXFORMAT_3:
	case R200_PP_TXFORMAT_4:
	case R200_PP_TXFORMAT_5:
		i = (reg - R200_PP_TXFORMAT_0) / 32;
		if (idx_value & R200_TXFORMAT_NON_POWER2) {
			track->textures[i].use_pitch = 1;
		} else {
			track->textures[i].use_pitch = 0;
			track->textures[i].width = 1 << ((idx_value >> RADEON_TXFORMAT_WIDTH_SHIFT) & RADEON_TXFORMAT_WIDTH_MASK);
			track->textures[i].height = 1 << ((idx_value >> RADEON_TXFORMAT_HEIGHT_SHIFT) & RADEON_TXFORMAT_HEIGHT_MASK);
		}
		if (idx_value & R200_TXFORMAT_LOOKUP_DISABLE)
			track->textures[i].lookup_disable = true;
		switch ((idx_value & RADEON_TXFORMAT_FORMAT_MASK)) {
		case R200_TXFORMAT_I8:
		case R200_TXFORMAT_RGB332:
		case R200_TXFORMAT_Y8:
			track->textures[i].cpp = 1;
			track->textures[i].compress_format = R100_TRACK_COMP_NONE;
			break;
		case R200_TXFORMAT_AI88:
		case R200_TXFORMAT_ARGB1555:
		case R200_TXFORMAT_RGB565:
		case R200_TXFORMAT_ARGB4444:
		case R200_TXFORMAT_VYUY422:
		case R200_TXFORMAT_YVYU422:
		case R200_TXFORMAT_LDVDU655:
		case R200_TXFORMAT_DVDU88:
		case R200_TXFORMAT_AVYU4444:
			track->textures[i].cpp = 2;
			track->textures[i].compress_format = R100_TRACK_COMP_NONE;
			break;
		case R200_TXFORMAT_ARGB8888:
		case R200_TXFORMAT_RGBA8888:
		case R200_TXFORMAT_ABGR8888:
		case R200_TXFORMAT_BGR111110:
		case R200_TXFORMAT_LDVDU8888:
			track->textures[i].cpp = 4;
			track->textures[i].compress_format = R100_TRACK_COMP_NONE;
			break;
		case R200_TXFORMAT_DXT1:
			track->textures[i].cpp = 1;
			track->textures[i].compress_format = R100_TRACK_COMP_DXT1;
			break;
		case R200_TXFORMAT_DXT23:
		case R200_TXFORMAT_DXT45:
			track->textures[i].cpp = 1;
			track->textures[i].compress_format = R100_TRACK_COMP_DXT1;
			break;
		}
		track->textures[i].cube_info[4].width = 1 << ((idx_value >> 16) & 0xf);
		track->textures[i].cube_info[4].height = 1 << ((idx_value >> 20) & 0xf);
		track->tex_dirty = true;
		break;
	case R200_PP_CUBIC_FACES_0:
	case R200_PP_CUBIC_FACES_1:
	case R200_PP_CUBIC_FACES_2:
	case R200_PP_CUBIC_FACES_3:
	case R200_PP_CUBIC_FACES_4:
	case R200_PP_CUBIC_FACES_5:
		tmp = idx_value;
		i = (reg - R200_PP_CUBIC_FACES_0) / 32;
		for (face = 0; face < 4; face++) {
			track->textures[i].cube_info[face].width = 1 << ((tmp >> (face * 8)) & 0xf);
			track->textures[i].cube_info[face].height = 1 << ((tmp >> ((face * 8) + 4)) & 0xf);
		}
		track->tex_dirty = true;
		break;
	default:
		printk(KERN_ERR "Forbidden register 0x%04X in cs at %d\n",
		       reg, idx);
		return -EINVAL;
	}
	return 0;
}

void r200_set_safe_registers(struct radeon_device *rdev)
{
	rdev->config.r100.reg_safe_bm = r200_reg_safe_bm;
	rdev->config.r100.reg_safe_bm_size = ARRAY_SIZE(r200_reg_safe_bm);
}
