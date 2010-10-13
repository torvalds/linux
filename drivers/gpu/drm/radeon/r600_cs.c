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
#include <linux/kernel.h>
#include "drmP.h"
#include "radeon.h"
#include "r600d.h"
#include "r600_reg_safe.h"

static int r600_cs_packet_next_reloc_mm(struct radeon_cs_parser *p,
					struct radeon_cs_reloc **cs_reloc);
static int r600_cs_packet_next_reloc_nomm(struct radeon_cs_parser *p,
					struct radeon_cs_reloc **cs_reloc);
typedef int (*next_reloc_t)(struct radeon_cs_parser*, struct radeon_cs_reloc**);
static next_reloc_t r600_cs_packet_next_reloc = &r600_cs_packet_next_reloc_mm;
extern void r600_cs_legacy_get_tiling_conf(struct drm_device *dev, u32 *npipes, u32 *nbanks, u32 *group_size);


struct r600_cs_track {
	/* configuration we miror so that we use same code btw kms/ums */
	u32			group_size;
	u32			nbanks;
	u32			npipes;
	/* value we track */
	u32			sq_config;
	u32			nsamples;
	u32			cb_color_base_last[8];
	struct radeon_bo	*cb_color_bo[8];
	u32			cb_color_bo_offset[8];
	struct radeon_bo	*cb_color_frag_bo[8];
	struct radeon_bo	*cb_color_tile_bo[8];
	u32			cb_color_info[8];
	u32			cb_color_size_idx[8];
	u32			cb_target_mask;
	u32			cb_shader_mask;
	u32			cb_color_size[8];
	u32			vgt_strmout_en;
	u32			vgt_strmout_buffer_en;
	u32			db_depth_control;
	u32			db_depth_info;
	u32			db_depth_size_idx;
	u32			db_depth_view;
	u32			db_depth_size;
	u32			db_offset;
	struct radeon_bo	*db_bo;
};

static inline int r600_bpe_from_format(u32 *bpe, u32 format)
{
	switch (format) {
	case V_038004_COLOR_8:
	case V_038004_COLOR_4_4:
	case V_038004_COLOR_3_3_2:
	case V_038004_FMT_1:
		*bpe = 1;
		break;
	case V_038004_COLOR_16:
	case V_038004_COLOR_16_FLOAT:
	case V_038004_COLOR_8_8:
	case V_038004_COLOR_5_6_5:
	case V_038004_COLOR_6_5_5:
	case V_038004_COLOR_1_5_5_5:
	case V_038004_COLOR_4_4_4_4:
	case V_038004_COLOR_5_5_5_1:
		*bpe = 2;
		break;
	case V_038004_FMT_8_8_8:
		*bpe = 3;
		break;
	case V_038004_COLOR_32:
	case V_038004_COLOR_32_FLOAT:
	case V_038004_COLOR_16_16:
	case V_038004_COLOR_16_16_FLOAT:
	case V_038004_COLOR_8_24:
	case V_038004_COLOR_8_24_FLOAT:
	case V_038004_COLOR_24_8:
	case V_038004_COLOR_24_8_FLOAT:
	case V_038004_COLOR_10_11_11:
	case V_038004_COLOR_10_11_11_FLOAT:
	case V_038004_COLOR_11_11_10:
	case V_038004_COLOR_11_11_10_FLOAT:
	case V_038004_COLOR_2_10_10_10:
	case V_038004_COLOR_8_8_8_8:
	case V_038004_COLOR_10_10_10_2:
	case V_038004_FMT_5_9_9_9_SHAREDEXP:
	case V_038004_FMT_32_AS_8:
	case V_038004_FMT_32_AS_8_8:
		*bpe = 4;
		break;
	case V_038004_COLOR_X24_8_32_FLOAT:
	case V_038004_COLOR_32_32:
	case V_038004_COLOR_32_32_FLOAT:
	case V_038004_COLOR_16_16_16_16:
	case V_038004_COLOR_16_16_16_16_FLOAT:
		*bpe = 8;
		break;
	case V_038004_FMT_16_16_16:
	case V_038004_FMT_16_16_16_FLOAT:
		*bpe = 6;
		break;
	case V_038004_FMT_32_32_32:
	case V_038004_FMT_32_32_32_FLOAT:
		*bpe = 12;
		break;
	case V_038004_COLOR_32_32_32_32:
	case V_038004_COLOR_32_32_32_32_FLOAT:
		*bpe = 16;
		break;
	case V_038004_FMT_GB_GR:
	case V_038004_FMT_BG_RG:
	case V_038004_COLOR_INVALID:
	default:
		*bpe = 16;
		return -EINVAL;
	}
	return 0;
}

static void r600_cs_track_init(struct r600_cs_track *track)
{
	int i;

	/* assume DX9 mode */
	track->sq_config = DX9_CONSTS;
	for (i = 0; i < 8; i++) {
		track->cb_color_base_last[i] = 0;
		track->cb_color_size[i] = 0;
		track->cb_color_size_idx[i] = 0;
		track->cb_color_info[i] = 0;
		track->cb_color_bo[i] = NULL;
		track->cb_color_bo_offset[i] = 0xFFFFFFFF;
	}
	track->cb_target_mask = 0xFFFFFFFF;
	track->cb_shader_mask = 0xFFFFFFFF;
	track->db_bo = NULL;
	/* assume the biggest format and that htile is enabled */
	track->db_depth_info = 7 | (1 << 25);
	track->db_depth_view = 0xFFFFC000;
	track->db_depth_size = 0xFFFFFFFF;
	track->db_depth_size_idx = 0;
	track->db_depth_control = 0xFFFFFFFF;
}

static inline int r600_cs_track_validate_cb(struct radeon_cs_parser *p, int i)
{
	struct r600_cs_track *track = p->track;
	u32 bpe = 0, pitch, slice_tile_max, size, tmp, height, pitch_align;
	volatile u32 *ib = p->ib->ptr;

	if (G_0280A0_TILE_MODE(track->cb_color_info[i])) {
		dev_warn(p->dev, "FMASK or CMASK buffer are not supported by this kernel\n");
		return -EINVAL;
	}
	size = radeon_bo_size(track->cb_color_bo[i]) - track->cb_color_bo_offset[i];
	if (r600_bpe_from_format(&bpe, G_0280A0_FORMAT(track->cb_color_info[i]))) {
		dev_warn(p->dev, "%s:%d cb invalid format %d for %d (0x%08X)\n",
			 __func__, __LINE__, G_0280A0_FORMAT(track->cb_color_info[i]),
			i, track->cb_color_info[i]);
		return -EINVAL;
	}
	/* pitch is the number of 8x8 tiles per row */
	pitch = G_028060_PITCH_TILE_MAX(track->cb_color_size[i]) + 1;
	slice_tile_max = G_028060_SLICE_TILE_MAX(track->cb_color_size[i]) + 1;
	height = size / (pitch * 8 * bpe);
	if (height > 8192)
		height = 8192;
	if (height > 7)
		height &= ~0x7;
	switch (G_0280A0_ARRAY_MODE(track->cb_color_info[i])) {
	case V_0280A0_ARRAY_LINEAR_GENERAL:
		/* technically height & 0x7 */
		break;
	case V_0280A0_ARRAY_LINEAR_ALIGNED:
		pitch_align = max((u32)64, (u32)(track->group_size / bpe)) / 8;
		if (!IS_ALIGNED(pitch, pitch_align)) {
			dev_warn(p->dev, "%s:%d cb pitch (%d) invalid\n",
				 __func__, __LINE__, pitch);
			return -EINVAL;
		}
		if (!IS_ALIGNED(height, 8)) {
			dev_warn(p->dev, "%s:%d cb height (%d) invalid\n",
				 __func__, __LINE__, height);
			return -EINVAL;
		}
		break;
	case V_0280A0_ARRAY_1D_TILED_THIN1:
		pitch_align = max((u32)8, (u32)(track->group_size / (8 * bpe * track->nsamples))) / 8;
		if (!IS_ALIGNED(pitch, pitch_align)) {
			dev_warn(p->dev, "%s:%d cb pitch (%d) invalid\n",
				 __func__, __LINE__, pitch);
			return -EINVAL;
		}
		if (!IS_ALIGNED(height, 8)) {
			dev_warn(p->dev, "%s:%d cb height (%d) invalid\n",
				 __func__, __LINE__, height);
			return -EINVAL;
		}
		break;
	case V_0280A0_ARRAY_2D_TILED_THIN1:
		pitch_align = max((u32)track->nbanks,
				  (u32)(((track->group_size / 8) / (bpe * track->nsamples)) * track->nbanks));
		if (!IS_ALIGNED(pitch, pitch_align)) {
			dev_warn(p->dev, "%s:%d cb pitch (%d) invalid\n",
				__func__, __LINE__, pitch);
			return -EINVAL;
		}
		if (!IS_ALIGNED((height / 8), track->nbanks)) {
			dev_warn(p->dev, "%s:%d cb height (%d) invalid\n",
				 __func__, __LINE__, height);
			return -EINVAL;
		}
		break;
	default:
		dev_warn(p->dev, "%s invalid tiling %d for %d (0x%08X)\n", __func__,
			G_0280A0_ARRAY_MODE(track->cb_color_info[i]), i,
			track->cb_color_info[i]);
		return -EINVAL;
	}
	/* check offset */
	tmp = height * pitch * 8 * bpe;
	if ((tmp + track->cb_color_bo_offset[i]) > radeon_bo_size(track->cb_color_bo[i])) {
		dev_warn(p->dev, "%s offset[%d] %d too big\n", __func__, i, track->cb_color_bo_offset[i]);
		return -EINVAL;
	}
	if (!IS_ALIGNED(track->cb_color_bo_offset[i], track->group_size)) {
		dev_warn(p->dev, "%s offset[%d] %d not aligned\n", __func__, i, track->cb_color_bo_offset[i]);
		return -EINVAL;
	}
	/* limit max tile */
	tmp = (height * pitch * 8) >> 6;
	if (tmp < slice_tile_max)
		slice_tile_max = tmp;
	tmp = S_028060_PITCH_TILE_MAX(pitch - 1) |
		S_028060_SLICE_TILE_MAX(slice_tile_max - 1);
	ib[track->cb_color_size_idx[i]] = tmp;
	return 0;
}

static int r600_cs_track_check(struct radeon_cs_parser *p)
{
	struct r600_cs_track *track = p->track;
	u32 tmp;
	int r, i;
	volatile u32 *ib = p->ib->ptr;

	/* on legacy kernel we don't perform advanced check */
	if (p->rdev == NULL)
		return 0;
	/* we don't support out buffer yet */
	if (track->vgt_strmout_en || track->vgt_strmout_buffer_en) {
		dev_warn(p->dev, "this kernel doesn't support SMX output buffer\n");
		return -EINVAL;
	}
	/* check that we have a cb for each enabled target, we don't check
	 * shader_mask because it seems mesa isn't always setting it :(
	 */
	tmp = track->cb_target_mask;
	for (i = 0; i < 8; i++) {
		if ((tmp >> (i * 4)) & 0xF) {
			/* at least one component is enabled */
			if (track->cb_color_bo[i] == NULL) {
				dev_warn(p->dev, "%s:%d mask 0x%08X | 0x%08X no cb for %d\n",
					__func__, __LINE__, track->cb_target_mask, track->cb_shader_mask, i);
				return -EINVAL;
			}
			/* perform rewrite of CB_COLOR[0-7]_SIZE */
			r = r600_cs_track_validate_cb(p, i);
			if (r)
				return r;
		}
	}
	/* Check depth buffer */
	if (G_028800_STENCIL_ENABLE(track->db_depth_control) ||
		G_028800_Z_ENABLE(track->db_depth_control)) {
		u32 nviews, bpe, ntiles, pitch, pitch_align, height, size;
		if (track->db_bo == NULL) {
			dev_warn(p->dev, "z/stencil with no depth buffer\n");
			return -EINVAL;
		}
		if (G_028010_TILE_SURFACE_ENABLE(track->db_depth_info)) {
			dev_warn(p->dev, "this kernel doesn't support z/stencil htile\n");
			return -EINVAL;
		}
		switch (G_028010_FORMAT(track->db_depth_info)) {
		case V_028010_DEPTH_16:
			bpe = 2;
			break;
		case V_028010_DEPTH_X8_24:
		case V_028010_DEPTH_8_24:
		case V_028010_DEPTH_X8_24_FLOAT:
		case V_028010_DEPTH_8_24_FLOAT:
		case V_028010_DEPTH_32_FLOAT:
			bpe = 4;
			break;
		case V_028010_DEPTH_X24_8_32_FLOAT:
			bpe = 8;
			break;
		default:
			dev_warn(p->dev, "z/stencil with invalid format %d\n", G_028010_FORMAT(track->db_depth_info));
			return -EINVAL;
		}
		if ((track->db_depth_size & 0xFFFFFC00) == 0xFFFFFC00) {
			if (!track->db_depth_size_idx) {
				dev_warn(p->dev, "z/stencil buffer size not set\n");
				return -EINVAL;
			}
			tmp = radeon_bo_size(track->db_bo) - track->db_offset;
			tmp = (tmp / bpe) >> 6;
			if (!tmp) {
				dev_warn(p->dev, "z/stencil buffer too small (0x%08X %d %d %ld)\n",
						track->db_depth_size, bpe, track->db_offset,
						radeon_bo_size(track->db_bo));
				return -EINVAL;
			}
			ib[track->db_depth_size_idx] = S_028000_SLICE_TILE_MAX(tmp - 1) | (track->db_depth_size & 0x3FF);
		} else {
			size = radeon_bo_size(track->db_bo);
			pitch = G_028000_PITCH_TILE_MAX(track->db_depth_size) + 1;
			height = size / (pitch * 8 * bpe);
			height &= ~0x7;
			if (!height)
				height = 8;

			switch (G_028010_ARRAY_MODE(track->db_depth_info)) {
			case V_028010_ARRAY_1D_TILED_THIN1:
				pitch_align = (max((u32)8, (u32)(track->group_size / (8 * bpe))) / 8);
				if (!IS_ALIGNED(pitch, pitch_align)) {
					dev_warn(p->dev, "%s:%d db pitch (%d) invalid\n",
						 __func__, __LINE__, pitch);
					return -EINVAL;
				}
				if (!IS_ALIGNED(height, 8)) {
					dev_warn(p->dev, "%s:%d db height (%d) invalid\n",
						 __func__, __LINE__, height);
					return -EINVAL;
				}
				break;
			case V_028010_ARRAY_2D_TILED_THIN1:
				pitch_align = max((u32)track->nbanks,
						  (u32)(((track->group_size / 8) / bpe) * track->nbanks));
				if (!IS_ALIGNED(pitch, pitch_align)) {
					dev_warn(p->dev, "%s:%d db pitch (%d) invalid\n",
						 __func__, __LINE__, pitch);
					return -EINVAL;
				}
				if ((height / 8) & (track->nbanks - 1)) {
					dev_warn(p->dev, "%s:%d db height (%d) invalid\n",
						 __func__, __LINE__, height);
					return -EINVAL;
				}
				break;
			default:
				dev_warn(p->dev, "%s invalid tiling %d (0x%08X)\n", __func__,
					 G_028010_ARRAY_MODE(track->db_depth_info),
					 track->db_depth_info);
				return -EINVAL;
			}
			if (!IS_ALIGNED(track->db_offset, track->group_size)) {
				dev_warn(p->dev, "%s offset[%d] %d not aligned\n", __func__, i, track->db_offset);
				return -EINVAL;
			}
			ntiles = G_028000_SLICE_TILE_MAX(track->db_depth_size) + 1;
			nviews = G_028004_SLICE_MAX(track->db_depth_view) + 1;
			tmp = ntiles * bpe * 64 * nviews;
			if ((tmp + track->db_offset) > radeon_bo_size(track->db_bo)) {
				dev_warn(p->dev, "z/stencil buffer too small (0x%08X %d %d %d -> %d have %ld)\n",
						track->db_depth_size, ntiles, nviews, bpe, tmp + track->db_offset,
						radeon_bo_size(track->db_bo));
				return -EINVAL;
			}
		}
	}
	return 0;
}

/**
 * r600_cs_packet_parse() - parse cp packet and point ib index to next packet
 * @parser:	parser structure holding parsing context.
 * @pkt:	where to store packet informations
 *
 * Assume that chunk_ib_index is properly set. Will return -EINVAL
 * if packet is bigger than remaining ib size. or if packets is unknown.
 **/
int r600_cs_packet_parse(struct radeon_cs_parser *p,
			struct radeon_cs_packet *pkt,
			unsigned idx)
{
	struct radeon_cs_chunk *ib_chunk = &p->chunks[p->chunk_ib_idx];
	uint32_t header;

	if (idx >= ib_chunk->length_dw) {
		DRM_ERROR("Can not parse packet at %d after CS end %d !\n",
			  idx, ib_chunk->length_dw);
		return -EINVAL;
	}
	header = radeon_get_ib_value(p, idx);
	pkt->idx = idx;
	pkt->type = CP_PACKET_GET_TYPE(header);
	pkt->count = CP_PACKET_GET_COUNT(header);
	pkt->one_reg_wr = 0;
	switch (pkt->type) {
	case PACKET_TYPE0:
		pkt->reg = CP_PACKET0_GET_REG(header);
		break;
	case PACKET_TYPE3:
		pkt->opcode = CP_PACKET3_GET_OPCODE(header);
		break;
	case PACKET_TYPE2:
		pkt->count = -1;
		break;
	default:
		DRM_ERROR("Unknown packet type %d at %d !\n", pkt->type, idx);
		return -EINVAL;
	}
	if ((pkt->count + 1 + pkt->idx) >= ib_chunk->length_dw) {
		DRM_ERROR("Packet (%d:%d:%d) end after CS buffer (%d) !\n",
			  pkt->idx, pkt->type, pkt->count, ib_chunk->length_dw);
		return -EINVAL;
	}
	return 0;
}

/**
 * r600_cs_packet_next_reloc_mm() - parse next packet which should be reloc packet3
 * @parser:		parser structure holding parsing context.
 * @data:		pointer to relocation data
 * @offset_start:	starting offset
 * @offset_mask:	offset mask (to align start offset on)
 * @reloc:		reloc informations
 *
 * Check next packet is relocation packet3, do bo validation and compute
 * GPU offset using the provided start.
 **/
static int r600_cs_packet_next_reloc_mm(struct radeon_cs_parser *p,
					struct radeon_cs_reloc **cs_reloc)
{
	struct radeon_cs_chunk *relocs_chunk;
	struct radeon_cs_packet p3reloc;
	unsigned idx;
	int r;

	if (p->chunk_relocs_idx == -1) {
		DRM_ERROR("No relocation chunk !\n");
		return -EINVAL;
	}
	*cs_reloc = NULL;
	relocs_chunk = &p->chunks[p->chunk_relocs_idx];
	r = r600_cs_packet_parse(p, &p3reloc, p->idx);
	if (r) {
		return r;
	}
	p->idx += p3reloc.count + 2;
	if (p3reloc.type != PACKET_TYPE3 || p3reloc.opcode != PACKET3_NOP) {
		DRM_ERROR("No packet3 for relocation for packet at %d.\n",
			  p3reloc.idx);
		return -EINVAL;
	}
	idx = radeon_get_ib_value(p, p3reloc.idx + 1);
	if (idx >= relocs_chunk->length_dw) {
		DRM_ERROR("Relocs at %d after relocations chunk end %d !\n",
			  idx, relocs_chunk->length_dw);
		return -EINVAL;
	}
	/* FIXME: we assume reloc size is 4 dwords */
	*cs_reloc = p->relocs_ptr[(idx / 4)];
	return 0;
}

/**
 * r600_cs_packet_next_reloc_nomm() - parse next packet which should be reloc packet3
 * @parser:		parser structure holding parsing context.
 * @data:		pointer to relocation data
 * @offset_start:	starting offset
 * @offset_mask:	offset mask (to align start offset on)
 * @reloc:		reloc informations
 *
 * Check next packet is relocation packet3, do bo validation and compute
 * GPU offset using the provided start.
 **/
static int r600_cs_packet_next_reloc_nomm(struct radeon_cs_parser *p,
					struct radeon_cs_reloc **cs_reloc)
{
	struct radeon_cs_chunk *relocs_chunk;
	struct radeon_cs_packet p3reloc;
	unsigned idx;
	int r;

	if (p->chunk_relocs_idx == -1) {
		DRM_ERROR("No relocation chunk !\n");
		return -EINVAL;
	}
	*cs_reloc = NULL;
	relocs_chunk = &p->chunks[p->chunk_relocs_idx];
	r = r600_cs_packet_parse(p, &p3reloc, p->idx);
	if (r) {
		return r;
	}
	p->idx += p3reloc.count + 2;
	if (p3reloc.type != PACKET_TYPE3 || p3reloc.opcode != PACKET3_NOP) {
		DRM_ERROR("No packet3 for relocation for packet at %d.\n",
			  p3reloc.idx);
		return -EINVAL;
	}
	idx = radeon_get_ib_value(p, p3reloc.idx + 1);
	if (idx >= relocs_chunk->length_dw) {
		DRM_ERROR("Relocs at %d after relocations chunk end %d !\n",
			  idx, relocs_chunk->length_dw);
		return -EINVAL;
	}
	*cs_reloc = p->relocs;
	(*cs_reloc)->lobj.gpu_offset = (u64)relocs_chunk->kdata[idx + 3] << 32;
	(*cs_reloc)->lobj.gpu_offset |= relocs_chunk->kdata[idx + 0];
	return 0;
}

/**
 * r600_cs_packet_next_is_pkt3_nop() - test if next packet is packet3 nop for reloc
 * @parser:		parser structure holding parsing context.
 *
 * Check next packet is relocation packet3, do bo validation and compute
 * GPU offset using the provided start.
 **/
static inline int r600_cs_packet_next_is_pkt3_nop(struct radeon_cs_parser *p)
{
	struct radeon_cs_packet p3reloc;
	int r;

	r = r600_cs_packet_parse(p, &p3reloc, p->idx);
	if (r) {
		return 0;
	}
	if (p3reloc.type != PACKET_TYPE3 || p3reloc.opcode != PACKET3_NOP) {
		return 0;
	}
	return 1;
}

/**
 * r600_cs_packet_next_vline() - parse userspace VLINE packet
 * @parser:		parser structure holding parsing context.
 *
 * Userspace sends a special sequence for VLINE waits.
 * PACKET0 - VLINE_START_END + value
 * PACKET3 - WAIT_REG_MEM poll vline status reg
 * RELOC (P3) - crtc_id in reloc.
 *
 * This function parses this and relocates the VLINE START END
 * and WAIT_REG_MEM packets to the correct crtc.
 * It also detects a switched off crtc and nulls out the
 * wait in that case.
 */
static int r600_cs_packet_parse_vline(struct radeon_cs_parser *p)
{
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;
	struct radeon_cs_packet p3reloc, wait_reg_mem;
	int crtc_id;
	int r;
	uint32_t header, h_idx, reg, wait_reg_mem_info;
	volatile uint32_t *ib;

	ib = p->ib->ptr;

	/* parse the WAIT_REG_MEM */
	r = r600_cs_packet_parse(p, &wait_reg_mem, p->idx);
	if (r)
		return r;

	/* check its a WAIT_REG_MEM */
	if (wait_reg_mem.type != PACKET_TYPE3 ||
	    wait_reg_mem.opcode != PACKET3_WAIT_REG_MEM) {
		DRM_ERROR("vline wait missing WAIT_REG_MEM segment\n");
		r = -EINVAL;
		return r;
	}

	wait_reg_mem_info = radeon_get_ib_value(p, wait_reg_mem.idx + 1);
	/* bit 4 is reg (0) or mem (1) */
	if (wait_reg_mem_info & 0x10) {
		DRM_ERROR("vline WAIT_REG_MEM waiting on MEM rather than REG\n");
		r = -EINVAL;
		return r;
	}
	/* waiting for value to be equal */
	if ((wait_reg_mem_info & 0x7) != 0x3) {
		DRM_ERROR("vline WAIT_REG_MEM function not equal\n");
		r = -EINVAL;
		return r;
	}
	if ((radeon_get_ib_value(p, wait_reg_mem.idx + 2) << 2) != AVIVO_D1MODE_VLINE_STATUS) {
		DRM_ERROR("vline WAIT_REG_MEM bad reg\n");
		r = -EINVAL;
		return r;
	}

	if (radeon_get_ib_value(p, wait_reg_mem.idx + 5) != AVIVO_D1MODE_VLINE_STAT) {
		DRM_ERROR("vline WAIT_REG_MEM bad bit mask\n");
		r = -EINVAL;
		return r;
	}

	/* jump over the NOP */
	r = r600_cs_packet_parse(p, &p3reloc, p->idx + wait_reg_mem.count + 2);
	if (r)
		return r;

	h_idx = p->idx - 2;
	p->idx += wait_reg_mem.count + 2;
	p->idx += p3reloc.count + 2;

	header = radeon_get_ib_value(p, h_idx);
	crtc_id = radeon_get_ib_value(p, h_idx + 2 + 7 + 1);
	reg = CP_PACKET0_GET_REG(header);

	obj = drm_mode_object_find(p->rdev->ddev, crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		DRM_ERROR("cannot find crtc %d\n", crtc_id);
		r = -EINVAL;
		goto out;
	}
	crtc = obj_to_crtc(obj);
	radeon_crtc = to_radeon_crtc(crtc);
	crtc_id = radeon_crtc->crtc_id;

	if (!crtc->enabled) {
		/* if the CRTC isn't enabled - we need to nop out the WAIT_REG_MEM */
		ib[h_idx + 2] = PACKET2(0);
		ib[h_idx + 3] = PACKET2(0);
		ib[h_idx + 4] = PACKET2(0);
		ib[h_idx + 5] = PACKET2(0);
		ib[h_idx + 6] = PACKET2(0);
		ib[h_idx + 7] = PACKET2(0);
		ib[h_idx + 8] = PACKET2(0);
	} else if (crtc_id == 1) {
		switch (reg) {
		case AVIVO_D1MODE_VLINE_START_END:
			header &= ~R600_CP_PACKET0_REG_MASK;
			header |= AVIVO_D2MODE_VLINE_START_END >> 2;
			break;
		default:
			DRM_ERROR("unknown crtc reloc\n");
			r = -EINVAL;
			goto out;
		}
		ib[h_idx] = header;
		ib[h_idx + 4] = AVIVO_D2MODE_VLINE_STATUS >> 2;
	}
out:
	return r;
}

static int r600_packet0_check(struct radeon_cs_parser *p,
				struct radeon_cs_packet *pkt,
				unsigned idx, unsigned reg)
{
	int r;

	switch (reg) {
	case AVIVO_D1MODE_VLINE_START_END:
		r = r600_cs_packet_parse_vline(p);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			return r;
		}
		break;
	default:
		printk(KERN_ERR "Forbidden register 0x%04X in cs at %d\n",
		       reg, idx);
		return -EINVAL;
	}
	return 0;
}

static int r600_cs_parse_packet0(struct radeon_cs_parser *p,
				struct radeon_cs_packet *pkt)
{
	unsigned reg, i;
	unsigned idx;
	int r;

	idx = pkt->idx + 1;
	reg = pkt->reg;
	for (i = 0; i <= pkt->count; i++, idx++, reg += 4) {
		r = r600_packet0_check(p, pkt, idx, reg);
		if (r) {
			return r;
		}
	}
	return 0;
}

/**
 * r600_cs_check_reg() - check if register is authorized or not
 * @parser: parser structure holding parsing context
 * @reg: register we are testing
 * @idx: index into the cs buffer
 *
 * This function will test against r600_reg_safe_bm and return 0
 * if register is safe. If register is not flag as safe this function
 * will test it against a list of register needind special handling.
 */
static inline int r600_cs_check_reg(struct radeon_cs_parser *p, u32 reg, u32 idx)
{
	struct r600_cs_track *track = (struct r600_cs_track *)p->track;
	struct radeon_cs_reloc *reloc;
	u32 last_reg = ARRAY_SIZE(r600_reg_safe_bm);
	u32 m, i, tmp, *ib;
	int r;

	i = (reg >> 7);
	if (i > last_reg) {
		dev_warn(p->dev, "forbidden register 0x%08x at %d\n", reg, idx);
		return -EINVAL;
	}
	m = 1 << ((reg >> 2) & 31);
	if (!(r600_reg_safe_bm[i] & m))
		return 0;
	ib = p->ib->ptr;
	switch (reg) {
	/* force following reg to 0 in an attemp to disable out buffer
	 * which will need us to better understand how it works to perform
	 * security check on it (Jerome)
	 */
	case R_0288A8_SQ_ESGS_RING_ITEMSIZE:
	case R_008C44_SQ_ESGS_RING_SIZE:
	case R_0288B0_SQ_ESTMP_RING_ITEMSIZE:
	case R_008C54_SQ_ESTMP_RING_SIZE:
	case R_0288C0_SQ_FBUF_RING_ITEMSIZE:
	case R_008C74_SQ_FBUF_RING_SIZE:
	case R_0288B4_SQ_GSTMP_RING_ITEMSIZE:
	case R_008C5C_SQ_GSTMP_RING_SIZE:
	case R_0288AC_SQ_GSVS_RING_ITEMSIZE:
	case R_008C4C_SQ_GSVS_RING_SIZE:
	case R_0288BC_SQ_PSTMP_RING_ITEMSIZE:
	case R_008C6C_SQ_PSTMP_RING_SIZE:
	case R_0288C4_SQ_REDUC_RING_ITEMSIZE:
	case R_008C7C_SQ_REDUC_RING_SIZE:
	case R_0288B8_SQ_VSTMP_RING_ITEMSIZE:
	case R_008C64_SQ_VSTMP_RING_SIZE:
	case R_0288C8_SQ_GS_VERT_ITEMSIZE:
		/* get value to populate the IB don't remove */
		tmp =radeon_get_ib_value(p, idx);
		ib[idx] = 0;
		break;
	case SQ_CONFIG:
		track->sq_config = radeon_get_ib_value(p, idx);
		break;
	case R_028800_DB_DEPTH_CONTROL:
		track->db_depth_control = radeon_get_ib_value(p, idx);
		break;
	case R_028010_DB_DEPTH_INFO:
		if (r600_cs_packet_next_is_pkt3_nop(p)) {
			r = r600_cs_packet_next_reloc(p, &reloc);
			if (r) {
				dev_warn(p->dev, "bad SET_CONTEXT_REG "
					 "0x%04X\n", reg);
				return -EINVAL;
			}
			track->db_depth_info = radeon_get_ib_value(p, idx);
			ib[idx] &= C_028010_ARRAY_MODE;
			track->db_depth_info &= C_028010_ARRAY_MODE;
			if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO) {
				ib[idx] |= S_028010_ARRAY_MODE(V_028010_ARRAY_2D_TILED_THIN1);
				track->db_depth_info |= S_028010_ARRAY_MODE(V_028010_ARRAY_2D_TILED_THIN1);
			} else {
				ib[idx] |= S_028010_ARRAY_MODE(V_028010_ARRAY_1D_TILED_THIN1);
				track->db_depth_info |= S_028010_ARRAY_MODE(V_028010_ARRAY_1D_TILED_THIN1);
			}
		} else
			track->db_depth_info = radeon_get_ib_value(p, idx);
		break;
	case R_028004_DB_DEPTH_VIEW:
		track->db_depth_view = radeon_get_ib_value(p, idx);
		break;
	case R_028000_DB_DEPTH_SIZE:
		track->db_depth_size = radeon_get_ib_value(p, idx);
		track->db_depth_size_idx = idx;
		break;
	case R_028AB0_VGT_STRMOUT_EN:
		track->vgt_strmout_en = radeon_get_ib_value(p, idx);
		break;
	case R_028B20_VGT_STRMOUT_BUFFER_EN:
		track->vgt_strmout_buffer_en = radeon_get_ib_value(p, idx);
		break;
	case R_028238_CB_TARGET_MASK:
		track->cb_target_mask = radeon_get_ib_value(p, idx);
		break;
	case R_02823C_CB_SHADER_MASK:
		track->cb_shader_mask = radeon_get_ib_value(p, idx);
		break;
	case R_028C04_PA_SC_AA_CONFIG:
		tmp = G_028C04_MSAA_NUM_SAMPLES(radeon_get_ib_value(p, idx));
		track->nsamples = 1 << tmp;
		break;
	case R_0280A0_CB_COLOR0_INFO:
	case R_0280A4_CB_COLOR1_INFO:
	case R_0280A8_CB_COLOR2_INFO:
	case R_0280AC_CB_COLOR3_INFO:
	case R_0280B0_CB_COLOR4_INFO:
	case R_0280B4_CB_COLOR5_INFO:
	case R_0280B8_CB_COLOR6_INFO:
	case R_0280BC_CB_COLOR7_INFO:
		if (r600_cs_packet_next_is_pkt3_nop(p)) {
			r = r600_cs_packet_next_reloc(p, &reloc);
			if (r) {
				dev_err(p->dev, "bad SET_CONTEXT_REG 0x%04X\n", reg);
				return -EINVAL;
			}
			tmp = (reg - R_0280A0_CB_COLOR0_INFO) / 4;
			track->cb_color_info[tmp] = radeon_get_ib_value(p, idx);
			if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO) {
				ib[idx] |= S_0280A0_ARRAY_MODE(V_0280A0_ARRAY_2D_TILED_THIN1);
				track->cb_color_info[tmp] |= S_0280A0_ARRAY_MODE(V_0280A0_ARRAY_2D_TILED_THIN1);
			} else if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO) {
				ib[idx] |= S_0280A0_ARRAY_MODE(V_0280A0_ARRAY_1D_TILED_THIN1);
				track->cb_color_info[tmp] |= S_0280A0_ARRAY_MODE(V_0280A0_ARRAY_1D_TILED_THIN1);
			}
		} else {
			tmp = (reg - R_0280A0_CB_COLOR0_INFO) / 4;
			track->cb_color_info[tmp] = radeon_get_ib_value(p, idx);
		}
		break;
	case R_028060_CB_COLOR0_SIZE:
	case R_028064_CB_COLOR1_SIZE:
	case R_028068_CB_COLOR2_SIZE:
	case R_02806C_CB_COLOR3_SIZE:
	case R_028070_CB_COLOR4_SIZE:
	case R_028074_CB_COLOR5_SIZE:
	case R_028078_CB_COLOR6_SIZE:
	case R_02807C_CB_COLOR7_SIZE:
		tmp = (reg - R_028060_CB_COLOR0_SIZE) / 4;
		track->cb_color_size[tmp] = radeon_get_ib_value(p, idx);
		track->cb_color_size_idx[tmp] = idx;
		break;
		/* This register were added late, there is userspace
		 * which does provide relocation for those but set
		 * 0 offset. In order to avoid breaking old userspace
		 * we detect this and set address to point to last
		 * CB_COLOR0_BASE, note that if userspace doesn't set
		 * CB_COLOR0_BASE before this register we will report
		 * error. Old userspace always set CB_COLOR0_BASE
		 * before any of this.
		 */
	case R_0280E0_CB_COLOR0_FRAG:
	case R_0280E4_CB_COLOR1_FRAG:
	case R_0280E8_CB_COLOR2_FRAG:
	case R_0280EC_CB_COLOR3_FRAG:
	case R_0280F0_CB_COLOR4_FRAG:
	case R_0280F4_CB_COLOR5_FRAG:
	case R_0280F8_CB_COLOR6_FRAG:
	case R_0280FC_CB_COLOR7_FRAG:
		tmp = (reg - R_0280E0_CB_COLOR0_FRAG) / 4;
		if (!r600_cs_packet_next_is_pkt3_nop(p)) {
			if (!track->cb_color_base_last[tmp]) {
				dev_err(p->dev, "Broken old userspace ? no cb_color0_base supplied before trying to write 0x%08X\n", reg);
				return -EINVAL;
			}
			ib[idx] = track->cb_color_base_last[tmp];
			track->cb_color_frag_bo[tmp] = track->cb_color_bo[tmp];
		} else {
			r = r600_cs_packet_next_reloc(p, &reloc);
			if (r) {
				dev_err(p->dev, "bad SET_CONTEXT_REG 0x%04X\n", reg);
				return -EINVAL;
			}
			ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
			track->cb_color_frag_bo[tmp] = reloc->robj;
		}
		break;
	case R_0280C0_CB_COLOR0_TILE:
	case R_0280C4_CB_COLOR1_TILE:
	case R_0280C8_CB_COLOR2_TILE:
	case R_0280CC_CB_COLOR3_TILE:
	case R_0280D0_CB_COLOR4_TILE:
	case R_0280D4_CB_COLOR5_TILE:
	case R_0280D8_CB_COLOR6_TILE:
	case R_0280DC_CB_COLOR7_TILE:
		tmp = (reg - R_0280C0_CB_COLOR0_TILE) / 4;
		if (!r600_cs_packet_next_is_pkt3_nop(p)) {
			if (!track->cb_color_base_last[tmp]) {
				dev_err(p->dev, "Broken old userspace ? no cb_color0_base supplied before trying to write 0x%08X\n", reg);
				return -EINVAL;
			}
			ib[idx] = track->cb_color_base_last[tmp];
			track->cb_color_tile_bo[tmp] = track->cb_color_bo[tmp];
		} else {
			r = r600_cs_packet_next_reloc(p, &reloc);
			if (r) {
				dev_err(p->dev, "bad SET_CONTEXT_REG 0x%04X\n", reg);
				return -EINVAL;
			}
			ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
			track->cb_color_tile_bo[tmp] = reloc->robj;
		}
		break;
	case CB_COLOR0_BASE:
	case CB_COLOR1_BASE:
	case CB_COLOR2_BASE:
	case CB_COLOR3_BASE:
	case CB_COLOR4_BASE:
	case CB_COLOR5_BASE:
	case CB_COLOR6_BASE:
	case CB_COLOR7_BASE:
		r = r600_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = (reg - CB_COLOR0_BASE) / 4;
		track->cb_color_bo_offset[tmp] = radeon_get_ib_value(p, idx) << 8;
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		track->cb_color_base_last[tmp] = ib[idx];
		track->cb_color_bo[tmp] = reloc->robj;
		break;
	case DB_DEPTH_BASE:
		r = r600_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		track->db_offset = radeon_get_ib_value(p, idx) << 8;
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		track->db_bo = reloc->robj;
		break;
	case DB_HTILE_DATA_BASE:
	case SQ_PGM_START_FS:
	case SQ_PGM_START_ES:
	case SQ_PGM_START_VS:
	case SQ_PGM_START_GS:
	case SQ_PGM_START_PS:
	case SQ_ALU_CONST_CACHE_GS_0:
	case SQ_ALU_CONST_CACHE_GS_1:
	case SQ_ALU_CONST_CACHE_GS_2:
	case SQ_ALU_CONST_CACHE_GS_3:
	case SQ_ALU_CONST_CACHE_GS_4:
	case SQ_ALU_CONST_CACHE_GS_5:
	case SQ_ALU_CONST_CACHE_GS_6:
	case SQ_ALU_CONST_CACHE_GS_7:
	case SQ_ALU_CONST_CACHE_GS_8:
	case SQ_ALU_CONST_CACHE_GS_9:
	case SQ_ALU_CONST_CACHE_GS_10:
	case SQ_ALU_CONST_CACHE_GS_11:
	case SQ_ALU_CONST_CACHE_GS_12:
	case SQ_ALU_CONST_CACHE_GS_13:
	case SQ_ALU_CONST_CACHE_GS_14:
	case SQ_ALU_CONST_CACHE_GS_15:
	case SQ_ALU_CONST_CACHE_PS_0:
	case SQ_ALU_CONST_CACHE_PS_1:
	case SQ_ALU_CONST_CACHE_PS_2:
	case SQ_ALU_CONST_CACHE_PS_3:
	case SQ_ALU_CONST_CACHE_PS_4:
	case SQ_ALU_CONST_CACHE_PS_5:
	case SQ_ALU_CONST_CACHE_PS_6:
	case SQ_ALU_CONST_CACHE_PS_7:
	case SQ_ALU_CONST_CACHE_PS_8:
	case SQ_ALU_CONST_CACHE_PS_9:
	case SQ_ALU_CONST_CACHE_PS_10:
	case SQ_ALU_CONST_CACHE_PS_11:
	case SQ_ALU_CONST_CACHE_PS_12:
	case SQ_ALU_CONST_CACHE_PS_13:
	case SQ_ALU_CONST_CACHE_PS_14:
	case SQ_ALU_CONST_CACHE_PS_15:
	case SQ_ALU_CONST_CACHE_VS_0:
	case SQ_ALU_CONST_CACHE_VS_1:
	case SQ_ALU_CONST_CACHE_VS_2:
	case SQ_ALU_CONST_CACHE_VS_3:
	case SQ_ALU_CONST_CACHE_VS_4:
	case SQ_ALU_CONST_CACHE_VS_5:
	case SQ_ALU_CONST_CACHE_VS_6:
	case SQ_ALU_CONST_CACHE_VS_7:
	case SQ_ALU_CONST_CACHE_VS_8:
	case SQ_ALU_CONST_CACHE_VS_9:
	case SQ_ALU_CONST_CACHE_VS_10:
	case SQ_ALU_CONST_CACHE_VS_11:
	case SQ_ALU_CONST_CACHE_VS_12:
	case SQ_ALU_CONST_CACHE_VS_13:
	case SQ_ALU_CONST_CACHE_VS_14:
	case SQ_ALU_CONST_CACHE_VS_15:
		r = r600_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		break;
	default:
		dev_warn(p->dev, "forbidden register 0x%08x at %d\n", reg, idx);
		return -EINVAL;
	}
	return 0;
}

static inline unsigned minify(unsigned size, unsigned levels)
{
	size = size >> levels;
	if (size < 1)
		size = 1;
	return size;
}

static void r600_texture_size(unsigned nfaces, unsigned blevel, unsigned nlevels,
			      unsigned w0, unsigned h0, unsigned d0, unsigned bpe,
			      unsigned pitch_align,
			      unsigned *l0_size, unsigned *mipmap_size)
{
	unsigned offset, i, level, face;
	unsigned width, height, depth, rowstride, size;

	w0 = minify(w0, 0);
	h0 = minify(h0, 0);
	d0 = minify(d0, 0);
	for(i = 0, offset = 0, level = blevel; i < nlevels; i++, level++) {
		width = minify(w0, i);
		height = minify(h0, i);
		depth = minify(d0, i);
		for(face = 0; face < nfaces; face++) {
			rowstride = ALIGN((width * bpe), pitch_align);
			size = height * rowstride * depth;
			offset += size;
			offset = (offset + 0x1f) & ~0x1f;
		}
	}
	*l0_size = ALIGN((w0 * bpe), pitch_align) * h0 * d0;
	*mipmap_size = offset;
	if (!nlevels)
		*mipmap_size = *l0_size;
	if (!blevel)
		*mipmap_size -= *l0_size;
}

/**
 * r600_check_texture_resource() - check if register is authorized or not
 * @p: parser structure holding parsing context
 * @idx: index into the cs buffer
 * @texture: texture's bo structure
 * @mipmap: mipmap's bo structure
 *
 * This function will check that the resource has valid field and that
 * the texture and mipmap bo object are big enough to cover this resource.
 */
static inline int r600_check_texture_resource(struct radeon_cs_parser *p,  u32 idx,
					      struct radeon_bo *texture,
					      struct radeon_bo *mipmap,
					      u32 tiling_flags)
{
	struct r600_cs_track *track = p->track;
	u32 nfaces, nlevels, blevel, w0, h0, d0, bpe = 0;
	u32 word0, word1, l0_size, mipmap_size, pitch, pitch_align;

	/* on legacy kernel we don't perform advanced check */
	if (p->rdev == NULL)
		return 0;

	word0 = radeon_get_ib_value(p, idx + 0);
	if (tiling_flags & RADEON_TILING_MACRO)
		word0 |= S_038000_TILE_MODE(V_038000_ARRAY_2D_TILED_THIN1);
	else if (tiling_flags & RADEON_TILING_MICRO)
		word0 |= S_038000_TILE_MODE(V_038000_ARRAY_1D_TILED_THIN1);
	word1 = radeon_get_ib_value(p, idx + 1);
	w0 = G_038000_TEX_WIDTH(word0) + 1;
	h0 = G_038004_TEX_HEIGHT(word1) + 1;
	d0 = G_038004_TEX_DEPTH(word1);
	nfaces = 1;
	switch (G_038000_DIM(word0)) {
	case V_038000_SQ_TEX_DIM_1D:
	case V_038000_SQ_TEX_DIM_2D:
	case V_038000_SQ_TEX_DIM_3D:
		break;
	case V_038000_SQ_TEX_DIM_CUBEMAP:
		nfaces = 6;
		break;
	case V_038000_SQ_TEX_DIM_1D_ARRAY:
	case V_038000_SQ_TEX_DIM_2D_ARRAY:
	case V_038000_SQ_TEX_DIM_2D_MSAA:
	case V_038000_SQ_TEX_DIM_2D_ARRAY_MSAA:
	default:
		dev_warn(p->dev, "this kernel doesn't support %d texture dim\n", G_038000_DIM(word0));
		return -EINVAL;
	}
	if (r600_bpe_from_format(&bpe,  G_038004_DATA_FORMAT(word1))) {
		dev_warn(p->dev, "%s:%d texture invalid format %d\n",
			 __func__, __LINE__, G_038004_DATA_FORMAT(word1));
		return -EINVAL;
	}

	pitch = G_038000_PITCH(word0) + 1;
	switch (G_038000_TILE_MODE(word0)) {
	case V_038000_ARRAY_LINEAR_GENERAL:
		pitch_align = 1;
		/* XXX check height align */
		break;
	case V_038000_ARRAY_LINEAR_ALIGNED:
		pitch_align = max((u32)64, (u32)(track->group_size / bpe)) / 8;
		if (!IS_ALIGNED(pitch, pitch_align)) {
			dev_warn(p->dev, "%s:%d tex pitch (%d) invalid\n",
				 __func__, __LINE__, pitch);
			return -EINVAL;
		}
		/* XXX check height align */
		break;
	case V_038000_ARRAY_1D_TILED_THIN1:
		pitch_align = max((u32)8, (u32)(track->group_size / (8 * bpe))) / 8;
		if (!IS_ALIGNED(pitch, pitch_align)) {
			dev_warn(p->dev, "%s:%d tex pitch (%d) invalid\n",
				 __func__, __LINE__, pitch);
			return -EINVAL;
		}
		/* XXX check height align */
		break;
	case V_038000_ARRAY_2D_TILED_THIN1:
		pitch_align = max((u32)track->nbanks,
				  (u32)(((track->group_size / 8) / bpe) * track->nbanks));
		if (!IS_ALIGNED(pitch, pitch_align)) {
			dev_warn(p->dev, "%s:%d tex pitch (%d) invalid\n",
				__func__, __LINE__, pitch);
			return -EINVAL;
		}
		/* XXX check height align */
		break;
	default:
		dev_warn(p->dev, "%s invalid tiling %d (0x%08X)\n", __func__,
			 G_038000_TILE_MODE(word0), word0);
		return -EINVAL;
	}
	/* XXX check offset align */

	word0 = radeon_get_ib_value(p, idx + 4);
	word1 = radeon_get_ib_value(p, idx + 5);
	blevel = G_038010_BASE_LEVEL(word0);
	nlevels = G_038014_LAST_LEVEL(word1);
	r600_texture_size(nfaces, blevel, nlevels, w0, h0, d0, bpe,
			  (pitch_align * bpe),
			  &l0_size, &mipmap_size);
	/* using get ib will give us the offset into the texture bo */
	word0 = radeon_get_ib_value(p, idx + 2) << 8;
	if ((l0_size + word0) > radeon_bo_size(texture)) {
		dev_warn(p->dev, "texture bo too small (%d %d %d %d -> %d have %ld)\n",
			w0, h0, bpe, word0, l0_size, radeon_bo_size(texture));
		return -EINVAL;
	}
	/* using get ib will give us the offset into the mipmap bo */
	word0 = radeon_get_ib_value(p, idx + 3) << 8;
	if ((mipmap_size + word0) > radeon_bo_size(mipmap)) {
		/*dev_warn(p->dev, "mipmap bo too small (%d %d %d %d %d %d -> %d have %ld)\n",
		  w0, h0, bpe, blevel, nlevels, word0, mipmap_size, radeon_bo_size(texture));*/
	}
	return 0;
}

static int r600_packet3_check(struct radeon_cs_parser *p,
				struct radeon_cs_packet *pkt)
{
	struct radeon_cs_reloc *reloc;
	struct r600_cs_track *track;
	volatile u32 *ib;
	unsigned idx;
	unsigned i;
	unsigned start_reg, end_reg, reg;
	int r;
	u32 idx_value;

	track = (struct r600_cs_track *)p->track;
	ib = p->ib->ptr;
	idx = pkt->idx + 1;
	idx_value = radeon_get_ib_value(p, idx);

	switch (pkt->opcode) {
	case PACKET3_START_3D_CMDBUF:
		if (p->family >= CHIP_RV770 || pkt->count) {
			DRM_ERROR("bad START_3D\n");
			return -EINVAL;
		}
		break;
	case PACKET3_CONTEXT_CONTROL:
		if (pkt->count != 1) {
			DRM_ERROR("bad CONTEXT_CONTROL\n");
			return -EINVAL;
		}
		break;
	case PACKET3_INDEX_TYPE:
	case PACKET3_NUM_INSTANCES:
		if (pkt->count) {
			DRM_ERROR("bad INDEX_TYPE/NUM_INSTANCES\n");
			return -EINVAL;
		}
		break;
	case PACKET3_DRAW_INDEX:
		if (pkt->count != 3) {
			DRM_ERROR("bad DRAW_INDEX\n");
			return -EINVAL;
		}
		r = r600_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("bad DRAW_INDEX\n");
			return -EINVAL;
		}
		ib[idx+0] = idx_value + (u32)(reloc->lobj.gpu_offset & 0xffffffff);
		ib[idx+1] += upper_32_bits(reloc->lobj.gpu_offset) & 0xff;
		r = r600_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	case PACKET3_DRAW_INDEX_AUTO:
		if (pkt->count != 1) {
			DRM_ERROR("bad DRAW_INDEX_AUTO\n");
			return -EINVAL;
		}
		r = r600_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream %d\n", __func__, __LINE__, idx);
			return r;
		}
		break;
	case PACKET3_DRAW_INDEX_IMMD_BE:
	case PACKET3_DRAW_INDEX_IMMD:
		if (pkt->count < 2) {
			DRM_ERROR("bad DRAW_INDEX_IMMD\n");
			return -EINVAL;
		}
		r = r600_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	case PACKET3_WAIT_REG_MEM:
		if (pkt->count != 5) {
			DRM_ERROR("bad WAIT_REG_MEM\n");
			return -EINVAL;
		}
		/* bit 4 is reg (0) or mem (1) */
		if (idx_value & 0x10) {
			r = r600_cs_packet_next_reloc(p, &reloc);
			if (r) {
				DRM_ERROR("bad WAIT_REG_MEM\n");
				return -EINVAL;
			}
			ib[idx+1] += (u32)(reloc->lobj.gpu_offset & 0xffffffff);
			ib[idx+2] += upper_32_bits(reloc->lobj.gpu_offset) & 0xff;
		}
		break;
	case PACKET3_SURFACE_SYNC:
		if (pkt->count != 3) {
			DRM_ERROR("bad SURFACE_SYNC\n");
			return -EINVAL;
		}
		/* 0xffffffff/0x0 is flush all cache flag */
		if (radeon_get_ib_value(p, idx + 1) != 0xffffffff ||
		    radeon_get_ib_value(p, idx + 2) != 0) {
			r = r600_cs_packet_next_reloc(p, &reloc);
			if (r) {
				DRM_ERROR("bad SURFACE_SYNC\n");
				return -EINVAL;
			}
			ib[idx+2] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		}
		break;
	case PACKET3_EVENT_WRITE:
		if (pkt->count != 2 && pkt->count != 0) {
			DRM_ERROR("bad EVENT_WRITE\n");
			return -EINVAL;
		}
		if (pkt->count) {
			r = r600_cs_packet_next_reloc(p, &reloc);
			if (r) {
				DRM_ERROR("bad EVENT_WRITE\n");
				return -EINVAL;
			}
			ib[idx+1] += (u32)(reloc->lobj.gpu_offset & 0xffffffff);
			ib[idx+2] += upper_32_bits(reloc->lobj.gpu_offset) & 0xff;
		}
		break;
	case PACKET3_EVENT_WRITE_EOP:
		if (pkt->count != 4) {
			DRM_ERROR("bad EVENT_WRITE_EOP\n");
			return -EINVAL;
		}
		r = r600_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("bad EVENT_WRITE\n");
			return -EINVAL;
		}
		ib[idx+1] += (u32)(reloc->lobj.gpu_offset & 0xffffffff);
		ib[idx+2] += upper_32_bits(reloc->lobj.gpu_offset) & 0xff;
		break;
	case PACKET3_SET_CONFIG_REG:
		start_reg = (idx_value << 2) + PACKET3_SET_CONFIG_REG_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CONFIG_REG_OFFSET) ||
		    (start_reg >= PACKET3_SET_CONFIG_REG_END) ||
		    (end_reg >= PACKET3_SET_CONFIG_REG_END)) {
			DRM_ERROR("bad PACKET3_SET_CONFIG_REG\n");
			return -EINVAL;
		}
		for (i = 0; i < pkt->count; i++) {
			reg = start_reg + (4 * i);
			r = r600_cs_check_reg(p, reg, idx+1+i);
			if (r)
				return r;
		}
		break;
	case PACKET3_SET_CONTEXT_REG:
		start_reg = (idx_value << 2) + PACKET3_SET_CONTEXT_REG_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CONTEXT_REG_OFFSET) ||
		    (start_reg >= PACKET3_SET_CONTEXT_REG_END) ||
		    (end_reg >= PACKET3_SET_CONTEXT_REG_END)) {
			DRM_ERROR("bad PACKET3_SET_CONTEXT_REG\n");
			return -EINVAL;
		}
		for (i = 0; i < pkt->count; i++) {
			reg = start_reg + (4 * i);
			r = r600_cs_check_reg(p, reg, idx+1+i);
			if (r)
				return r;
		}
		break;
	case PACKET3_SET_RESOURCE:
		if (pkt->count % 7) {
			DRM_ERROR("bad SET_RESOURCE\n");
			return -EINVAL;
		}
		start_reg = (idx_value << 2) + PACKET3_SET_RESOURCE_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_RESOURCE_OFFSET) ||
		    (start_reg >= PACKET3_SET_RESOURCE_END) ||
		    (end_reg >= PACKET3_SET_RESOURCE_END)) {
			DRM_ERROR("bad SET_RESOURCE\n");
			return -EINVAL;
		}
		for (i = 0; i < (pkt->count / 7); i++) {
			struct radeon_bo *texture, *mipmap;
			u32 size, offset, base_offset, mip_offset;

			switch (G__SQ_VTX_CONSTANT_TYPE(radeon_get_ib_value(p, idx+(i*7)+6+1))) {
			case SQ_TEX_VTX_VALID_TEXTURE:
				/* tex base */
				r = r600_cs_packet_next_reloc(p, &reloc);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE\n");
					return -EINVAL;
				}
				base_offset = (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
				if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO)
					ib[idx+1+(i*7)+0] |= S_038000_TILE_MODE(V_038000_ARRAY_2D_TILED_THIN1);
				else if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO)
					ib[idx+1+(i*7)+0] |= S_038000_TILE_MODE(V_038000_ARRAY_1D_TILED_THIN1);
				texture = reloc->robj;
				/* tex mip base */
				r = r600_cs_packet_next_reloc(p, &reloc);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE\n");
					return -EINVAL;
				}
				mip_offset = (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
				mipmap = reloc->robj;
				r = r600_check_texture_resource(p,  idx+(i*7)+1,
								texture, mipmap, reloc->lobj.tiling_flags);
				if (r)
					return r;
				ib[idx+1+(i*7)+2] += base_offset;
				ib[idx+1+(i*7)+3] += mip_offset;
				break;
			case SQ_TEX_VTX_VALID_BUFFER:
				/* vtx base */
				r = r600_cs_packet_next_reloc(p, &reloc);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE\n");
					return -EINVAL;
				}
				offset = radeon_get_ib_value(p, idx+1+(i*7)+0);
				size = radeon_get_ib_value(p, idx+1+(i*7)+1) + 1;
				if (p->rdev && (size + offset) > radeon_bo_size(reloc->robj)) {
					/* force size to size of the buffer */
					dev_warn(p->dev, "vbo resource seems too big (%d) for the bo (%ld)\n",
						 size + offset, radeon_bo_size(reloc->robj));
					ib[idx+1+(i*7)+1] = radeon_bo_size(reloc->robj);
				}
				ib[idx+1+(i*7)+0] += (u32)((reloc->lobj.gpu_offset) & 0xffffffff);
				ib[idx+1+(i*7)+2] += upper_32_bits(reloc->lobj.gpu_offset) & 0xff;
				break;
			case SQ_TEX_VTX_INVALID_TEXTURE:
			case SQ_TEX_VTX_INVALID_BUFFER:
			default:
				DRM_ERROR("bad SET_RESOURCE\n");
				return -EINVAL;
			}
		}
		break;
	case PACKET3_SET_ALU_CONST:
		if (track->sq_config & DX9_CONSTS) {
			start_reg = (idx_value << 2) + PACKET3_SET_ALU_CONST_OFFSET;
			end_reg = 4 * pkt->count + start_reg - 4;
			if ((start_reg < PACKET3_SET_ALU_CONST_OFFSET) ||
			    (start_reg >= PACKET3_SET_ALU_CONST_END) ||
			    (end_reg >= PACKET3_SET_ALU_CONST_END)) {
				DRM_ERROR("bad SET_ALU_CONST\n");
				return -EINVAL;
			}
		}
		break;
	case PACKET3_SET_BOOL_CONST:
		start_reg = (idx_value << 2) + PACKET3_SET_BOOL_CONST_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_BOOL_CONST_OFFSET) ||
		    (start_reg >= PACKET3_SET_BOOL_CONST_END) ||
		    (end_reg >= PACKET3_SET_BOOL_CONST_END)) {
			DRM_ERROR("bad SET_BOOL_CONST\n");
			return -EINVAL;
		}
		break;
	case PACKET3_SET_LOOP_CONST:
		start_reg = (idx_value << 2) + PACKET3_SET_LOOP_CONST_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_LOOP_CONST_OFFSET) ||
		    (start_reg >= PACKET3_SET_LOOP_CONST_END) ||
		    (end_reg >= PACKET3_SET_LOOP_CONST_END)) {
			DRM_ERROR("bad SET_LOOP_CONST\n");
			return -EINVAL;
		}
		break;
	case PACKET3_SET_CTL_CONST:
		start_reg = (idx_value << 2) + PACKET3_SET_CTL_CONST_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CTL_CONST_OFFSET) ||
		    (start_reg >= PACKET3_SET_CTL_CONST_END) ||
		    (end_reg >= PACKET3_SET_CTL_CONST_END)) {
			DRM_ERROR("bad SET_CTL_CONST\n");
			return -EINVAL;
		}
		break;
	case PACKET3_SET_SAMPLER:
		if (pkt->count % 3) {
			DRM_ERROR("bad SET_SAMPLER\n");
			return -EINVAL;
		}
		start_reg = (idx_value << 2) + PACKET3_SET_SAMPLER_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_SAMPLER_OFFSET) ||
		    (start_reg >= PACKET3_SET_SAMPLER_END) ||
		    (end_reg >= PACKET3_SET_SAMPLER_END)) {
			DRM_ERROR("bad SET_SAMPLER\n");
			return -EINVAL;
		}
		break;
	case PACKET3_SURFACE_BASE_UPDATE:
		if (p->family >= CHIP_RV770 || p->family == CHIP_R600) {
			DRM_ERROR("bad SURFACE_BASE_UPDATE\n");
			return -EINVAL;
		}
		if (pkt->count) {
			DRM_ERROR("bad SURFACE_BASE_UPDATE\n");
			return -EINVAL;
		}
		break;
	case PACKET3_NOP:
		break;
	default:
		DRM_ERROR("Packet3 opcode %x not supported\n", pkt->opcode);
		return -EINVAL;
	}
	return 0;
}

int r600_cs_parse(struct radeon_cs_parser *p)
{
	struct radeon_cs_packet pkt;
	struct r600_cs_track *track;
	int r;

	if (p->track == NULL) {
		/* initialize tracker, we are in kms */
		track = kzalloc(sizeof(*track), GFP_KERNEL);
		if (track == NULL)
			return -ENOMEM;
		r600_cs_track_init(track);
		if (p->rdev->family < CHIP_RV770) {
			track->npipes = p->rdev->config.r600.tiling_npipes;
			track->nbanks = p->rdev->config.r600.tiling_nbanks;
			track->group_size = p->rdev->config.r600.tiling_group_size;
		} else if (p->rdev->family <= CHIP_RV740) {
			track->npipes = p->rdev->config.rv770.tiling_npipes;
			track->nbanks = p->rdev->config.rv770.tiling_nbanks;
			track->group_size = p->rdev->config.rv770.tiling_group_size;
		}
		p->track = track;
	}
	do {
		r = r600_cs_packet_parse(p, &pkt, p->idx);
		if (r) {
			kfree(p->track);
			p->track = NULL;
			return r;
		}
		p->idx += pkt.count + 2;
		switch (pkt.type) {
		case PACKET_TYPE0:
			r = r600_cs_parse_packet0(p, &pkt);
			break;
		case PACKET_TYPE2:
			break;
		case PACKET_TYPE3:
			r = r600_packet3_check(p, &pkt);
			break;
		default:
			DRM_ERROR("Unknown packet type %d !\n", pkt.type);
			kfree(p->track);
			p->track = NULL;
			return -EINVAL;
		}
		if (r) {
			kfree(p->track);
			p->track = NULL;
			return r;
		}
	} while (p->idx < p->chunks[p->chunk_ib_idx].length_dw);
#if 0
	for (r = 0; r < p->ib->length_dw; r++) {
		printk(KERN_INFO "%05d  0x%08X\n", r, p->ib->ptr[r]);
		mdelay(1);
	}
#endif
	kfree(p->track);
	p->track = NULL;
	return 0;
}

static int r600_cs_parser_relocs_legacy(struct radeon_cs_parser *p)
{
	if (p->chunk_relocs_idx == -1) {
		return 0;
	}
	p->relocs = kzalloc(sizeof(struct radeon_cs_reloc), GFP_KERNEL);
	if (p->relocs == NULL) {
		return -ENOMEM;
	}
	return 0;
}

/**
 * cs_parser_fini() - clean parser states
 * @parser:	parser structure holding parsing context.
 * @error:	error number
 *
 * If error is set than unvalidate buffer, otherwise just free memory
 * used by parsing context.
 **/
static void r600_cs_parser_fini(struct radeon_cs_parser *parser, int error)
{
	unsigned i;

	kfree(parser->relocs);
	for (i = 0; i < parser->nchunks; i++) {
		kfree(parser->chunks[i].kdata);
		kfree(parser->chunks[i].kpage[0]);
		kfree(parser->chunks[i].kpage[1]);
	}
	kfree(parser->chunks);
	kfree(parser->chunks_array);
}

int r600_cs_legacy(struct drm_device *dev, void *data, struct drm_file *filp,
			unsigned family, u32 *ib, int *l)
{
	struct radeon_cs_parser parser;
	struct radeon_cs_chunk *ib_chunk;
	struct radeon_ib fake_ib;
	struct r600_cs_track *track;
	int r;

	/* initialize tracker */
	track = kzalloc(sizeof(*track), GFP_KERNEL);
	if (track == NULL)
		return -ENOMEM;
	r600_cs_track_init(track);
	r600_cs_legacy_get_tiling_conf(dev, &track->npipes, &track->nbanks, &track->group_size);
	/* initialize parser */
	memset(&parser, 0, sizeof(struct radeon_cs_parser));
	parser.filp = filp;
	parser.dev = &dev->pdev->dev;
	parser.rdev = NULL;
	parser.family = family;
	parser.ib = &fake_ib;
	parser.track = track;
	fake_ib.ptr = ib;
	r = radeon_cs_parser_init(&parser, data);
	if (r) {
		DRM_ERROR("Failed to initialize parser !\n");
		r600_cs_parser_fini(&parser, r);
		return r;
	}
	r = r600_cs_parser_relocs_legacy(&parser);
	if (r) {
		DRM_ERROR("Failed to parse relocation !\n");
		r600_cs_parser_fini(&parser, r);
		return r;
	}
	/* Copy the packet into the IB, the parser will read from the
	 * input memory (cached) and write to the IB (which can be
	 * uncached). */
	ib_chunk = &parser.chunks[parser.chunk_ib_idx];
	parser.ib->length_dw = ib_chunk->length_dw;
	*l = parser.ib->length_dw;
	r = r600_cs_parse(&parser);
	if (r) {
		DRM_ERROR("Invalid command stream !\n");
		r600_cs_parser_fini(&parser, r);
		return r;
	}
	r = radeon_cs_finish_pages(&parser);
	if (r) {
		DRM_ERROR("Invalid command stream !\n");
		r600_cs_parser_fini(&parser, r);
		return r;
	}
	r600_cs_parser_fini(&parser, r);
	return r;
}

void r600_cs_legacy_init(void)
{
	r600_cs_packet_next_reloc = &r600_cs_packet_next_reloc_nomm;
}
