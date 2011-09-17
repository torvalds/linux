/*
 * Copyright 2010 Advanced Micro Devices, Inc.
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
#include "drmP.h"
#include "radeon.h"
#include "evergreend.h"
#include "evergreen_reg_safe.h"
#include "cayman_reg_safe.h"

static int evergreen_cs_packet_next_reloc(struct radeon_cs_parser *p,
					  struct radeon_cs_reloc **cs_reloc);

struct evergreen_cs_track {
	u32			group_size;
	u32			nbanks;
	u32			npipes;
	/* value we track */
	u32			nsamples;
	u32			cb_color_base_last[12];
	struct radeon_bo	*cb_color_bo[12];
	u32			cb_color_bo_offset[12];
	struct radeon_bo	*cb_color_fmask_bo[8];
	struct radeon_bo	*cb_color_cmask_bo[8];
	u32			cb_color_info[12];
	u32			cb_color_view[12];
	u32			cb_color_pitch_idx[12];
	u32			cb_color_slice_idx[12];
	u32			cb_color_dim_idx[12];
	u32			cb_color_dim[12];
	u32			cb_color_pitch[12];
	u32			cb_color_slice[12];
	u32			cb_color_cmask_slice[8];
	u32			cb_color_fmask_slice[8];
	u32			cb_target_mask;
	u32			cb_shader_mask;
	u32			vgt_strmout_config;
	u32			vgt_strmout_buffer_config;
	u32			db_depth_control;
	u32			db_depth_view;
	u32			db_depth_size;
	u32			db_depth_size_idx;
	u32			db_z_info;
	u32			db_z_idx;
	u32			db_z_read_offset;
	u32			db_z_write_offset;
	struct radeon_bo	*db_z_read_bo;
	struct radeon_bo	*db_z_write_bo;
	u32			db_s_info;
	u32			db_s_idx;
	u32			db_s_read_offset;
	u32			db_s_write_offset;
	struct radeon_bo	*db_s_read_bo;
	struct radeon_bo	*db_s_write_bo;
};

static void evergreen_cs_track_init(struct evergreen_cs_track *track)
{
	int i;

	for (i = 0; i < 8; i++) {
		track->cb_color_fmask_bo[i] = NULL;
		track->cb_color_cmask_bo[i] = NULL;
		track->cb_color_cmask_slice[i] = 0;
		track->cb_color_fmask_slice[i] = 0;
	}

	for (i = 0; i < 12; i++) {
		track->cb_color_base_last[i] = 0;
		track->cb_color_bo[i] = NULL;
		track->cb_color_bo_offset[i] = 0xFFFFFFFF;
		track->cb_color_info[i] = 0;
		track->cb_color_view[i] = 0;
		track->cb_color_pitch_idx[i] = 0;
		track->cb_color_slice_idx[i] = 0;
		track->cb_color_dim[i] = 0;
		track->cb_color_pitch[i] = 0;
		track->cb_color_slice[i] = 0;
		track->cb_color_dim[i] = 0;
	}
	track->cb_target_mask = 0xFFFFFFFF;
	track->cb_shader_mask = 0xFFFFFFFF;

	track->db_depth_view = 0xFFFFC000;
	track->db_depth_size = 0xFFFFFFFF;
	track->db_depth_size_idx = 0;
	track->db_depth_control = 0xFFFFFFFF;
	track->db_z_info = 0xFFFFFFFF;
	track->db_z_idx = 0xFFFFFFFF;
	track->db_z_read_offset = 0xFFFFFFFF;
	track->db_z_write_offset = 0xFFFFFFFF;
	track->db_z_read_bo = NULL;
	track->db_z_write_bo = NULL;
	track->db_s_info = 0xFFFFFFFF;
	track->db_s_idx = 0xFFFFFFFF;
	track->db_s_read_offset = 0xFFFFFFFF;
	track->db_s_write_offset = 0xFFFFFFFF;
	track->db_s_read_bo = NULL;
	track->db_s_write_bo = NULL;
}

static inline int evergreen_cs_track_validate_cb(struct radeon_cs_parser *p, int i)
{
	/* XXX fill in */
	return 0;
}

static int evergreen_cs_track_check(struct radeon_cs_parser *p)
{
	struct evergreen_cs_track *track = p->track;

	/* we don't support stream out buffer yet */
	if (track->vgt_strmout_config || track->vgt_strmout_buffer_config) {
		dev_warn(p->dev, "this kernel doesn't support SMX output buffer\n");
		return -EINVAL;
	}

	/* XXX fill in */
	return 0;
}

/**
 * evergreen_cs_packet_parse() - parse cp packet and point ib index to next packet
 * @parser:	parser structure holding parsing context.
 * @pkt:	where to store packet informations
 *
 * Assume that chunk_ib_index is properly set. Will return -EINVAL
 * if packet is bigger than remaining ib size. or if packets is unknown.
 **/
int evergreen_cs_packet_parse(struct radeon_cs_parser *p,
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
 * evergreen_cs_packet_next_reloc() - parse next packet which should be reloc packet3
 * @parser:		parser structure holding parsing context.
 * @data:		pointer to relocation data
 * @offset_start:	starting offset
 * @offset_mask:	offset mask (to align start offset on)
 * @reloc:		reloc informations
 *
 * Check next packet is relocation packet3, do bo validation and compute
 * GPU offset using the provided start.
 **/
static int evergreen_cs_packet_next_reloc(struct radeon_cs_parser *p,
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
	r = evergreen_cs_packet_parse(p, &p3reloc, p->idx);
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
 * evergreen_cs_packet_next_is_pkt3_nop() - test if next packet is packet3 nop for reloc
 * @parser:		parser structure holding parsing context.
 *
 * Check next packet is relocation packet3, do bo validation and compute
 * GPU offset using the provided start.
 **/
static inline int evergreen_cs_packet_next_is_pkt3_nop(struct radeon_cs_parser *p)
{
	struct radeon_cs_packet p3reloc;
	int r;

	r = evergreen_cs_packet_parse(p, &p3reloc, p->idx);
	if (r) {
		return 0;
	}
	if (p3reloc.type != PACKET_TYPE3 || p3reloc.opcode != PACKET3_NOP) {
		return 0;
	}
	return 1;
}

/**
 * evergreen_cs_packet_next_vline() - parse userspace VLINE packet
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
static int evergreen_cs_packet_parse_vline(struct radeon_cs_parser *p)
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
	r = evergreen_cs_packet_parse(p, &wait_reg_mem, p->idx);
	if (r)
		return r;

	/* check its a WAIT_REG_MEM */
	if (wait_reg_mem.type != PACKET_TYPE3 ||
	    wait_reg_mem.opcode != PACKET3_WAIT_REG_MEM) {
		DRM_ERROR("vline wait missing WAIT_REG_MEM segment\n");
		return -EINVAL;
	}

	wait_reg_mem_info = radeon_get_ib_value(p, wait_reg_mem.idx + 1);
	/* bit 4 is reg (0) or mem (1) */
	if (wait_reg_mem_info & 0x10) {
		DRM_ERROR("vline WAIT_REG_MEM waiting on MEM rather than REG\n");
		return -EINVAL;
	}
	/* waiting for value to be equal */
	if ((wait_reg_mem_info & 0x7) != 0x3) {
		DRM_ERROR("vline WAIT_REG_MEM function not equal\n");
		return -EINVAL;
	}
	if ((radeon_get_ib_value(p, wait_reg_mem.idx + 2) << 2) != EVERGREEN_VLINE_STATUS) {
		DRM_ERROR("vline WAIT_REG_MEM bad reg\n");
		return -EINVAL;
	}

	if (radeon_get_ib_value(p, wait_reg_mem.idx + 5) != EVERGREEN_VLINE_STAT) {
		DRM_ERROR("vline WAIT_REG_MEM bad bit mask\n");
		return -EINVAL;
	}

	/* jump over the NOP */
	r = evergreen_cs_packet_parse(p, &p3reloc, p->idx + wait_reg_mem.count + 2);
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
		return -EINVAL;
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
	} else {
		switch (reg) {
		case EVERGREEN_VLINE_START_END:
			header &= ~R600_CP_PACKET0_REG_MASK;
			header |= (EVERGREEN_VLINE_START_END + radeon_crtc->crtc_offset) >> 2;
			ib[h_idx] = header;
			ib[h_idx + 4] = (EVERGREEN_VLINE_STATUS + radeon_crtc->crtc_offset) >> 2;
			break;
		default:
			DRM_ERROR("unknown crtc reloc\n");
			return -EINVAL;
		}
	}
	return 0;
}

static int evergreen_packet0_check(struct radeon_cs_parser *p,
				   struct radeon_cs_packet *pkt,
				   unsigned idx, unsigned reg)
{
	int r;

	switch (reg) {
	case EVERGREEN_VLINE_START_END:
		r = evergreen_cs_packet_parse_vline(p);
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

static int evergreen_cs_parse_packet0(struct radeon_cs_parser *p,
				      struct radeon_cs_packet *pkt)
{
	unsigned reg, i;
	unsigned idx;
	int r;

	idx = pkt->idx + 1;
	reg = pkt->reg;
	for (i = 0; i <= pkt->count; i++, idx++, reg += 4) {
		r = evergreen_packet0_check(p, pkt, idx, reg);
		if (r) {
			return r;
		}
	}
	return 0;
}

/**
 * evergreen_cs_check_reg() - check if register is authorized or not
 * @parser: parser structure holding parsing context
 * @reg: register we are testing
 * @idx: index into the cs buffer
 *
 * This function will test against evergreen_reg_safe_bm and return 0
 * if register is safe. If register is not flag as safe this function
 * will test it against a list of register needind special handling.
 */
static inline int evergreen_cs_check_reg(struct radeon_cs_parser *p, u32 reg, u32 idx)
{
	struct evergreen_cs_track *track = (struct evergreen_cs_track *)p->track;
	struct radeon_cs_reloc *reloc;
	u32 last_reg;
	u32 m, i, tmp, *ib;
	int r;

	if (p->rdev->family >= CHIP_CAYMAN)
		last_reg = ARRAY_SIZE(cayman_reg_safe_bm);
	else
		last_reg = ARRAY_SIZE(evergreen_reg_safe_bm);

	i = (reg >> 7);
	if (i >= last_reg) {
		dev_warn(p->dev, "forbidden register 0x%08x at %d\n", reg, idx);
		return -EINVAL;
	}
	m = 1 << ((reg >> 2) & 31);
	if (p->rdev->family >= CHIP_CAYMAN) {
		if (!(cayman_reg_safe_bm[i] & m))
			return 0;
	} else {
		if (!(evergreen_reg_safe_bm[i] & m))
			return 0;
	}
	ib = p->ib->ptr;
	switch (reg) {
	/* force following reg to 0 in an attempt to disable out buffer
	 * which will need us to better understand how it works to perform
	 * security check on it (Jerome)
	 */
	case SQ_ESGS_RING_SIZE:
	case SQ_GSVS_RING_SIZE:
	case SQ_ESTMP_RING_SIZE:
	case SQ_GSTMP_RING_SIZE:
	case SQ_HSTMP_RING_SIZE:
	case SQ_LSTMP_RING_SIZE:
	case SQ_PSTMP_RING_SIZE:
	case SQ_VSTMP_RING_SIZE:
	case SQ_ESGS_RING_ITEMSIZE:
	case SQ_ESTMP_RING_ITEMSIZE:
	case SQ_GSTMP_RING_ITEMSIZE:
	case SQ_GSVS_RING_ITEMSIZE:
	case SQ_GS_VERT_ITEMSIZE:
	case SQ_GS_VERT_ITEMSIZE_1:
	case SQ_GS_VERT_ITEMSIZE_2:
	case SQ_GS_VERT_ITEMSIZE_3:
	case SQ_GSVS_RING_OFFSET_1:
	case SQ_GSVS_RING_OFFSET_2:
	case SQ_GSVS_RING_OFFSET_3:
	case SQ_HSTMP_RING_ITEMSIZE:
	case SQ_LSTMP_RING_ITEMSIZE:
	case SQ_PSTMP_RING_ITEMSIZE:
	case SQ_VSTMP_RING_ITEMSIZE:
	case VGT_TF_RING_SIZE:
		/* get value to populate the IB don't remove */
		/*tmp =radeon_get_ib_value(p, idx);
		  ib[idx] = 0;*/
		break;
	case SQ_ESGS_RING_BASE:
	case SQ_GSVS_RING_BASE:
	case SQ_ESTMP_RING_BASE:
	case SQ_GSTMP_RING_BASE:
	case SQ_HSTMP_RING_BASE:
	case SQ_LSTMP_RING_BASE:
	case SQ_PSTMP_RING_BASE:
	case SQ_VSTMP_RING_BASE:
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		break;
	case DB_DEPTH_CONTROL:
		track->db_depth_control = radeon_get_ib_value(p, idx);
		break;
	case CAYMAN_DB_EQAA:
		if (p->rdev->family < CHIP_CAYMAN) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
				 "0x%04X\n", reg);
			return -EINVAL;
		}
		break;
	case CAYMAN_DB_DEPTH_INFO:
		if (p->rdev->family < CHIP_CAYMAN) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
				 "0x%04X\n", reg);
			return -EINVAL;
		}
		break;
	case DB_Z_INFO:
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		track->db_z_info = radeon_get_ib_value(p, idx);
		ib[idx] &= ~Z_ARRAY_MODE(0xf);
		track->db_z_info &= ~Z_ARRAY_MODE(0xf);
		if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO) {
			ib[idx] |= Z_ARRAY_MODE(ARRAY_2D_TILED_THIN1);
			track->db_z_info |= Z_ARRAY_MODE(ARRAY_2D_TILED_THIN1);
		} else {
			ib[idx] |= Z_ARRAY_MODE(ARRAY_1D_TILED_THIN1);
			track->db_z_info |= Z_ARRAY_MODE(ARRAY_1D_TILED_THIN1);
		}
		break;
	case DB_STENCIL_INFO:
		track->db_s_info = radeon_get_ib_value(p, idx);
		break;
	case DB_DEPTH_VIEW:
		track->db_depth_view = radeon_get_ib_value(p, idx);
		break;
	case DB_DEPTH_SIZE:
		track->db_depth_size = radeon_get_ib_value(p, idx);
		track->db_depth_size_idx = idx;
		break;
	case DB_Z_READ_BASE:
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		track->db_z_read_offset = radeon_get_ib_value(p, idx);
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		track->db_z_read_bo = reloc->robj;
		break;
	case DB_Z_WRITE_BASE:
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		track->db_z_write_offset = radeon_get_ib_value(p, idx);
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		track->db_z_write_bo = reloc->robj;
		break;
	case DB_STENCIL_READ_BASE:
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		track->db_s_read_offset = radeon_get_ib_value(p, idx);
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		track->db_s_read_bo = reloc->robj;
		break;
	case DB_STENCIL_WRITE_BASE:
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		track->db_s_write_offset = radeon_get_ib_value(p, idx);
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		track->db_s_write_bo = reloc->robj;
		break;
	case VGT_STRMOUT_CONFIG:
		track->vgt_strmout_config = radeon_get_ib_value(p, idx);
		break;
	case VGT_STRMOUT_BUFFER_CONFIG:
		track->vgt_strmout_buffer_config = radeon_get_ib_value(p, idx);
		break;
	case CB_TARGET_MASK:
		track->cb_target_mask = radeon_get_ib_value(p, idx);
		break;
	case CB_SHADER_MASK:
		track->cb_shader_mask = radeon_get_ib_value(p, idx);
		break;
	case PA_SC_AA_CONFIG:
		if (p->rdev->family >= CHIP_CAYMAN) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
				 "0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = radeon_get_ib_value(p, idx) & MSAA_NUM_SAMPLES_MASK;
		track->nsamples = 1 << tmp;
		break;
	case CAYMAN_PA_SC_AA_CONFIG:
		if (p->rdev->family < CHIP_CAYMAN) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
				 "0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = radeon_get_ib_value(p, idx) & CAYMAN_MSAA_NUM_SAMPLES_MASK;
		track->nsamples = 1 << tmp;
		break;
	case CB_COLOR0_VIEW:
	case CB_COLOR1_VIEW:
	case CB_COLOR2_VIEW:
	case CB_COLOR3_VIEW:
	case CB_COLOR4_VIEW:
	case CB_COLOR5_VIEW:
	case CB_COLOR6_VIEW:
	case CB_COLOR7_VIEW:
		tmp = (reg - CB_COLOR0_VIEW) / 0x3c;
		track->cb_color_view[tmp] = radeon_get_ib_value(p, idx);
		break;
	case CB_COLOR8_VIEW:
	case CB_COLOR9_VIEW:
	case CB_COLOR10_VIEW:
	case CB_COLOR11_VIEW:
		tmp = ((reg - CB_COLOR8_VIEW) / 0x1c) + 8;
		track->cb_color_view[tmp] = radeon_get_ib_value(p, idx);
		break;
	case CB_COLOR0_INFO:
	case CB_COLOR1_INFO:
	case CB_COLOR2_INFO:
	case CB_COLOR3_INFO:
	case CB_COLOR4_INFO:
	case CB_COLOR5_INFO:
	case CB_COLOR6_INFO:
	case CB_COLOR7_INFO:
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = (reg - CB_COLOR0_INFO) / 0x3c;
		track->cb_color_info[tmp] = radeon_get_ib_value(p, idx);
		if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO) {
			ib[idx] |= CB_ARRAY_MODE(ARRAY_2D_TILED_THIN1);
			track->cb_color_info[tmp] |= CB_ARRAY_MODE(ARRAY_2D_TILED_THIN1);
		} else if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO) {
			ib[idx] |= CB_ARRAY_MODE(ARRAY_1D_TILED_THIN1);
			track->cb_color_info[tmp] |= CB_ARRAY_MODE(ARRAY_1D_TILED_THIN1);
		}
		break;
	case CB_COLOR8_INFO:
	case CB_COLOR9_INFO:
	case CB_COLOR10_INFO:
	case CB_COLOR11_INFO:
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = ((reg - CB_COLOR8_INFO) / 0x1c) + 8;
		track->cb_color_info[tmp] = radeon_get_ib_value(p, idx);
		if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO) {
			ib[idx] |= CB_ARRAY_MODE(ARRAY_2D_TILED_THIN1);
			track->cb_color_info[tmp] |= CB_ARRAY_MODE(ARRAY_2D_TILED_THIN1);
		} else if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO) {
			ib[idx] |= CB_ARRAY_MODE(ARRAY_1D_TILED_THIN1);
			track->cb_color_info[tmp] |= CB_ARRAY_MODE(ARRAY_1D_TILED_THIN1);
		}
		break;
	case CB_COLOR0_PITCH:
	case CB_COLOR1_PITCH:
	case CB_COLOR2_PITCH:
	case CB_COLOR3_PITCH:
	case CB_COLOR4_PITCH:
	case CB_COLOR5_PITCH:
	case CB_COLOR6_PITCH:
	case CB_COLOR7_PITCH:
		tmp = (reg - CB_COLOR0_PITCH) / 0x3c;
		track->cb_color_pitch[tmp] = radeon_get_ib_value(p, idx);
		track->cb_color_pitch_idx[tmp] = idx;
		break;
	case CB_COLOR8_PITCH:
	case CB_COLOR9_PITCH:
	case CB_COLOR10_PITCH:
	case CB_COLOR11_PITCH:
		tmp = ((reg - CB_COLOR8_PITCH) / 0x1c) + 8;
		track->cb_color_pitch[tmp] = radeon_get_ib_value(p, idx);
		track->cb_color_pitch_idx[tmp] = idx;
		break;
	case CB_COLOR0_SLICE:
	case CB_COLOR1_SLICE:
	case CB_COLOR2_SLICE:
	case CB_COLOR3_SLICE:
	case CB_COLOR4_SLICE:
	case CB_COLOR5_SLICE:
	case CB_COLOR6_SLICE:
	case CB_COLOR7_SLICE:
		tmp = (reg - CB_COLOR0_SLICE) / 0x3c;
		track->cb_color_slice[tmp] = radeon_get_ib_value(p, idx);
		track->cb_color_slice_idx[tmp] = idx;
		break;
	case CB_COLOR8_SLICE:
	case CB_COLOR9_SLICE:
	case CB_COLOR10_SLICE:
	case CB_COLOR11_SLICE:
		tmp = ((reg - CB_COLOR8_SLICE) / 0x1c) + 8;
		track->cb_color_slice[tmp] = radeon_get_ib_value(p, idx);
		track->cb_color_slice_idx[tmp] = idx;
		break;
	case CB_COLOR0_ATTRIB:
	case CB_COLOR1_ATTRIB:
	case CB_COLOR2_ATTRIB:
	case CB_COLOR3_ATTRIB:
	case CB_COLOR4_ATTRIB:
	case CB_COLOR5_ATTRIB:
	case CB_COLOR6_ATTRIB:
	case CB_COLOR7_ATTRIB:
	case CB_COLOR8_ATTRIB:
	case CB_COLOR9_ATTRIB:
	case CB_COLOR10_ATTRIB:
	case CB_COLOR11_ATTRIB:
		break;
	case CB_COLOR0_DIM:
	case CB_COLOR1_DIM:
	case CB_COLOR2_DIM:
	case CB_COLOR3_DIM:
	case CB_COLOR4_DIM:
	case CB_COLOR5_DIM:
	case CB_COLOR6_DIM:
	case CB_COLOR7_DIM:
		tmp = (reg - CB_COLOR0_DIM) / 0x3c;
		track->cb_color_dim[tmp] = radeon_get_ib_value(p, idx);
		track->cb_color_dim_idx[tmp] = idx;
		break;
	case CB_COLOR8_DIM:
	case CB_COLOR9_DIM:
	case CB_COLOR10_DIM:
	case CB_COLOR11_DIM:
		tmp = ((reg - CB_COLOR8_DIM) / 0x1c) + 8;
		track->cb_color_dim[tmp] = radeon_get_ib_value(p, idx);
		track->cb_color_dim_idx[tmp] = idx;
		break;
	case CB_COLOR0_FMASK:
	case CB_COLOR1_FMASK:
	case CB_COLOR2_FMASK:
	case CB_COLOR3_FMASK:
	case CB_COLOR4_FMASK:
	case CB_COLOR5_FMASK:
	case CB_COLOR6_FMASK:
	case CB_COLOR7_FMASK:
		tmp = (reg - CB_COLOR0_FMASK) / 0x3c;
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_err(p->dev, "bad SET_CONTEXT_REG 0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		track->cb_color_fmask_bo[tmp] = reloc->robj;
		break;
	case CB_COLOR0_CMASK:
	case CB_COLOR1_CMASK:
	case CB_COLOR2_CMASK:
	case CB_COLOR3_CMASK:
	case CB_COLOR4_CMASK:
	case CB_COLOR5_CMASK:
	case CB_COLOR6_CMASK:
	case CB_COLOR7_CMASK:
		tmp = (reg - CB_COLOR0_CMASK) / 0x3c;
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_err(p->dev, "bad SET_CONTEXT_REG 0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		track->cb_color_cmask_bo[tmp] = reloc->robj;
		break;
	case CB_COLOR0_FMASK_SLICE:
	case CB_COLOR1_FMASK_SLICE:
	case CB_COLOR2_FMASK_SLICE:
	case CB_COLOR3_FMASK_SLICE:
	case CB_COLOR4_FMASK_SLICE:
	case CB_COLOR5_FMASK_SLICE:
	case CB_COLOR6_FMASK_SLICE:
	case CB_COLOR7_FMASK_SLICE:
		tmp = (reg - CB_COLOR0_FMASK_SLICE) / 0x3c;
		track->cb_color_fmask_slice[tmp] = radeon_get_ib_value(p, idx);
		break;
	case CB_COLOR0_CMASK_SLICE:
	case CB_COLOR1_CMASK_SLICE:
	case CB_COLOR2_CMASK_SLICE:
	case CB_COLOR3_CMASK_SLICE:
	case CB_COLOR4_CMASK_SLICE:
	case CB_COLOR5_CMASK_SLICE:
	case CB_COLOR6_CMASK_SLICE:
	case CB_COLOR7_CMASK_SLICE:
		tmp = (reg - CB_COLOR0_CMASK_SLICE) / 0x3c;
		track->cb_color_cmask_slice[tmp] = radeon_get_ib_value(p, idx);
		break;
	case CB_COLOR0_BASE:
	case CB_COLOR1_BASE:
	case CB_COLOR2_BASE:
	case CB_COLOR3_BASE:
	case CB_COLOR4_BASE:
	case CB_COLOR5_BASE:
	case CB_COLOR6_BASE:
	case CB_COLOR7_BASE:
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = (reg - CB_COLOR0_BASE) / 0x3c;
		track->cb_color_bo_offset[tmp] = radeon_get_ib_value(p, idx);
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		track->cb_color_base_last[tmp] = ib[idx];
		track->cb_color_bo[tmp] = reloc->robj;
		break;
	case CB_COLOR8_BASE:
	case CB_COLOR9_BASE:
	case CB_COLOR10_BASE:
	case CB_COLOR11_BASE:
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		tmp = ((reg - CB_COLOR8_BASE) / 0x1c) + 8;
		track->cb_color_bo_offset[tmp] = radeon_get_ib_value(p, idx);
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		track->cb_color_base_last[tmp] = ib[idx];
		track->cb_color_bo[tmp] = reloc->robj;
		break;
	case CB_IMMED0_BASE:
	case CB_IMMED1_BASE:
	case CB_IMMED2_BASE:
	case CB_IMMED3_BASE:
	case CB_IMMED4_BASE:
	case CB_IMMED5_BASE:
	case CB_IMMED6_BASE:
	case CB_IMMED7_BASE:
	case CB_IMMED8_BASE:
	case CB_IMMED9_BASE:
	case CB_IMMED10_BASE:
	case CB_IMMED11_BASE:
	case DB_HTILE_DATA_BASE:
	case SQ_PGM_START_FS:
	case SQ_PGM_START_ES:
	case SQ_PGM_START_VS:
	case SQ_PGM_START_GS:
	case SQ_PGM_START_PS:
	case SQ_PGM_START_HS:
	case SQ_PGM_START_LS:
	case SQ_CONST_MEM_BASE:
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
	case SQ_ALU_CONST_CACHE_HS_0:
	case SQ_ALU_CONST_CACHE_HS_1:
	case SQ_ALU_CONST_CACHE_HS_2:
	case SQ_ALU_CONST_CACHE_HS_3:
	case SQ_ALU_CONST_CACHE_HS_4:
	case SQ_ALU_CONST_CACHE_HS_5:
	case SQ_ALU_CONST_CACHE_HS_6:
	case SQ_ALU_CONST_CACHE_HS_7:
	case SQ_ALU_CONST_CACHE_HS_8:
	case SQ_ALU_CONST_CACHE_HS_9:
	case SQ_ALU_CONST_CACHE_HS_10:
	case SQ_ALU_CONST_CACHE_HS_11:
	case SQ_ALU_CONST_CACHE_HS_12:
	case SQ_ALU_CONST_CACHE_HS_13:
	case SQ_ALU_CONST_CACHE_HS_14:
	case SQ_ALU_CONST_CACHE_HS_15:
	case SQ_ALU_CONST_CACHE_LS_0:
	case SQ_ALU_CONST_CACHE_LS_1:
	case SQ_ALU_CONST_CACHE_LS_2:
	case SQ_ALU_CONST_CACHE_LS_3:
	case SQ_ALU_CONST_CACHE_LS_4:
	case SQ_ALU_CONST_CACHE_LS_5:
	case SQ_ALU_CONST_CACHE_LS_6:
	case SQ_ALU_CONST_CACHE_LS_7:
	case SQ_ALU_CONST_CACHE_LS_8:
	case SQ_ALU_CONST_CACHE_LS_9:
	case SQ_ALU_CONST_CACHE_LS_10:
	case SQ_ALU_CONST_CACHE_LS_11:
	case SQ_ALU_CONST_CACHE_LS_12:
	case SQ_ALU_CONST_CACHE_LS_13:
	case SQ_ALU_CONST_CACHE_LS_14:
	case SQ_ALU_CONST_CACHE_LS_15:
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		break;
	case SX_MEMORY_EXPORT_BASE:
		if (p->rdev->family >= CHIP_CAYMAN) {
			dev_warn(p->dev, "bad SET_CONFIG_REG "
				 "0x%04X\n", reg);
			return -EINVAL;
		}
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			dev_warn(p->dev, "bad SET_CONFIG_REG "
					"0x%04X\n", reg);
			return -EINVAL;
		}
		ib[idx] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
		break;
	case CAYMAN_SX_SCATTER_EXPORT_BASE:
		if (p->rdev->family < CHIP_CAYMAN) {
			dev_warn(p->dev, "bad SET_CONTEXT_REG "
				 "0x%04X\n", reg);
			return -EINVAL;
		}
		r = evergreen_cs_packet_next_reloc(p, &reloc);
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

/**
 * evergreen_check_texture_resource() - check if register is authorized or not
 * @p: parser structure holding parsing context
 * @idx: index into the cs buffer
 * @texture: texture's bo structure
 * @mipmap: mipmap's bo structure
 *
 * This function will check that the resource has valid field and that
 * the texture and mipmap bo object are big enough to cover this resource.
 */
static inline int evergreen_check_texture_resource(struct radeon_cs_parser *p,  u32 idx,
						   struct radeon_bo *texture,
						   struct radeon_bo *mipmap)
{
	/* XXX fill in */
	return 0;
}

static int evergreen_packet3_check(struct radeon_cs_parser *p,
				   struct radeon_cs_packet *pkt)
{
	struct radeon_cs_reloc *reloc;
	struct evergreen_cs_track *track;
	volatile u32 *ib;
	unsigned idx;
	unsigned i;
	unsigned start_reg, end_reg, reg;
	int r;
	u32 idx_value;

	track = (struct evergreen_cs_track *)p->track;
	ib = p->ib->ptr;
	idx = pkt->idx + 1;
	idx_value = radeon_get_ib_value(p, idx);

	switch (pkt->opcode) {
	case PACKET3_SET_PREDICATION:
	{
		int pred_op;
		int tmp;
		if (pkt->count != 1) {
			DRM_ERROR("bad SET PREDICATION\n");
			return -EINVAL;
		}

		tmp = radeon_get_ib_value(p, idx + 1);
		pred_op = (tmp >> 16) & 0x7;

		/* for the clear predicate operation */
		if (pred_op == 0)
			return 0;

		if (pred_op > 2) {
			DRM_ERROR("bad SET PREDICATION operation %d\n", pred_op);
			return -EINVAL;
		}

		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("bad SET PREDICATION\n");
			return -EINVAL;
		}

		ib[idx + 0] = idx_value + (u32)(reloc->lobj.gpu_offset & 0xffffffff);
		ib[idx + 1] = tmp + (upper_32_bits(reloc->lobj.gpu_offset) & 0xff);
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
	case PACKET3_CLEAR_STATE:
		if (pkt->count) {
			DRM_ERROR("bad INDEX_TYPE/NUM_INSTANCES/CLEAR_STATE\n");
			return -EINVAL;
		}
		break;
	case CAYMAN_PACKET3_DEALLOC_STATE:
		if (p->rdev->family < CHIP_CAYMAN) {
			DRM_ERROR("bad PACKET3_DEALLOC_STATE\n");
			return -EINVAL;
		}
		if (pkt->count) {
			DRM_ERROR("bad INDEX_TYPE/NUM_INSTANCES/CLEAR_STATE\n");
			return -EINVAL;
		}
		break;
	case PACKET3_INDEX_BASE:
		if (pkt->count != 1) {
			DRM_ERROR("bad INDEX_BASE\n");
			return -EINVAL;
		}
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("bad INDEX_BASE\n");
			return -EINVAL;
		}
		ib[idx+0] = idx_value + (u32)(reloc->lobj.gpu_offset & 0xffffffff);
		ib[idx+1] += upper_32_bits(reloc->lobj.gpu_offset) & 0xff;
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	case PACKET3_DRAW_INDEX:
		if (pkt->count != 3) {
			DRM_ERROR("bad DRAW_INDEX\n");
			return -EINVAL;
		}
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("bad DRAW_INDEX\n");
			return -EINVAL;
		}
		ib[idx+0] = idx_value + (u32)(reloc->lobj.gpu_offset & 0xffffffff);
		ib[idx+1] += upper_32_bits(reloc->lobj.gpu_offset) & 0xff;
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	case PACKET3_DRAW_INDEX_2:
		if (pkt->count != 4) {
			DRM_ERROR("bad DRAW_INDEX_2\n");
			return -EINVAL;
		}
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("bad DRAW_INDEX_2\n");
			return -EINVAL;
		}
		ib[idx+1] = idx_value + (u32)(reloc->lobj.gpu_offset & 0xffffffff);
		ib[idx+2] += upper_32_bits(reloc->lobj.gpu_offset) & 0xff;
		r = evergreen_cs_track_check(p);
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
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream %d\n", __func__, __LINE__, idx);
			return r;
		}
		break;
	case PACKET3_DRAW_INDEX_MULTI_AUTO:
		if (pkt->count != 2) {
			DRM_ERROR("bad DRAW_INDEX_MULTI_AUTO\n");
			return -EINVAL;
		}
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream %d\n", __func__, __LINE__, idx);
			return r;
		}
		break;
	case PACKET3_DRAW_INDEX_IMMD:
		if (pkt->count < 2) {
			DRM_ERROR("bad DRAW_INDEX_IMMD\n");
			return -EINVAL;
		}
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	case PACKET3_DRAW_INDEX_OFFSET:
		if (pkt->count != 2) {
			DRM_ERROR("bad DRAW_INDEX_OFFSET\n");
			return -EINVAL;
		}
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	case PACKET3_DRAW_INDEX_OFFSET_2:
		if (pkt->count != 3) {
			DRM_ERROR("bad DRAW_INDEX_OFFSET_2\n");
			return -EINVAL;
		}
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream\n", __func__, __LINE__);
			return r;
		}
		break;
	case PACKET3_DISPATCH_DIRECT:
		if (pkt->count != 3) {
			DRM_ERROR("bad DISPATCH_DIRECT\n");
			return -EINVAL;
		}
		r = evergreen_cs_track_check(p);
		if (r) {
			dev_warn(p->dev, "%s:%d invalid cmd stream %d\n", __func__, __LINE__, idx);
			return r;
		}
		break;
	case PACKET3_DISPATCH_INDIRECT:
		if (pkt->count != 1) {
			DRM_ERROR("bad DISPATCH_INDIRECT\n");
			return -EINVAL;
		}
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("bad DISPATCH_INDIRECT\n");
			return -EINVAL;
		}
		ib[idx+0] = idx_value + (u32)(reloc->lobj.gpu_offset & 0xffffffff);
		r = evergreen_cs_track_check(p);
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
			r = evergreen_cs_packet_next_reloc(p, &reloc);
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
			r = evergreen_cs_packet_next_reloc(p, &reloc);
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
			r = evergreen_cs_packet_next_reloc(p, &reloc);
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
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("bad EVENT_WRITE_EOP\n");
			return -EINVAL;
		}
		ib[idx+1] += (u32)(reloc->lobj.gpu_offset & 0xffffffff);
		ib[idx+2] += upper_32_bits(reloc->lobj.gpu_offset) & 0xff;
		break;
	case PACKET3_EVENT_WRITE_EOS:
		if (pkt->count != 3) {
			DRM_ERROR("bad EVENT_WRITE_EOS\n");
			return -EINVAL;
		}
		r = evergreen_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("bad EVENT_WRITE_EOS\n");
			return -EINVAL;
		}
		ib[idx+1] += (u32)(reloc->lobj.gpu_offset & 0xffffffff);
		ib[idx+2] += upper_32_bits(reloc->lobj.gpu_offset) & 0xff;
		break;
	case PACKET3_SET_CONFIG_REG:
		start_reg = (idx_value << 2) + PACKET3_SET_CONFIG_REG_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CONFIG_REG_START) ||
		    (start_reg >= PACKET3_SET_CONFIG_REG_END) ||
		    (end_reg >= PACKET3_SET_CONFIG_REG_END)) {
			DRM_ERROR("bad PACKET3_SET_CONFIG_REG\n");
			return -EINVAL;
		}
		for (i = 0; i < pkt->count; i++) {
			reg = start_reg + (4 * i);
			r = evergreen_cs_check_reg(p, reg, idx+1+i);
			if (r)
				return r;
		}
		break;
	case PACKET3_SET_CONTEXT_REG:
		start_reg = (idx_value << 2) + PACKET3_SET_CONTEXT_REG_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CONTEXT_REG_START) ||
		    (start_reg >= PACKET3_SET_CONTEXT_REG_END) ||
		    (end_reg >= PACKET3_SET_CONTEXT_REG_END)) {
			DRM_ERROR("bad PACKET3_SET_CONTEXT_REG\n");
			return -EINVAL;
		}
		for (i = 0; i < pkt->count; i++) {
			reg = start_reg + (4 * i);
			r = evergreen_cs_check_reg(p, reg, idx+1+i);
			if (r)
				return r;
		}
		break;
	case PACKET3_SET_RESOURCE:
		if (pkt->count % 8) {
			DRM_ERROR("bad SET_RESOURCE\n");
			return -EINVAL;
		}
		start_reg = (idx_value << 2) + PACKET3_SET_RESOURCE_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_RESOURCE_START) ||
		    (start_reg >= PACKET3_SET_RESOURCE_END) ||
		    (end_reg >= PACKET3_SET_RESOURCE_END)) {
			DRM_ERROR("bad SET_RESOURCE\n");
			return -EINVAL;
		}
		for (i = 0; i < (pkt->count / 8); i++) {
			struct radeon_bo *texture, *mipmap;
			u32 size, offset;

			switch (G__SQ_CONSTANT_TYPE(radeon_get_ib_value(p, idx+1+(i*8)+7))) {
			case SQ_TEX_VTX_VALID_TEXTURE:
				/* tex base */
				r = evergreen_cs_packet_next_reloc(p, &reloc);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE (tex)\n");
					return -EINVAL;
				}
				ib[idx+1+(i*8)+2] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
				if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO)
					ib[idx+1+(i*8)+1] |= TEX_ARRAY_MODE(ARRAY_2D_TILED_THIN1);
				else if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO)
					ib[idx+1+(i*8)+1] |= TEX_ARRAY_MODE(ARRAY_1D_TILED_THIN1);
				texture = reloc->robj;
				/* tex mip base */
				r = evergreen_cs_packet_next_reloc(p, &reloc);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE (tex)\n");
					return -EINVAL;
				}
				ib[idx+1+(i*8)+3] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
				mipmap = reloc->robj;
				r = evergreen_check_texture_resource(p,  idx+1+(i*8),
						texture, mipmap);
				if (r)
					return r;
				break;
			case SQ_TEX_VTX_VALID_BUFFER:
				/* vtx base */
				r = evergreen_cs_packet_next_reloc(p, &reloc);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE (vtx)\n");
					return -EINVAL;
				}
				offset = radeon_get_ib_value(p, idx+1+(i*8)+0);
				size = radeon_get_ib_value(p, idx+1+(i*8)+1);
				if (p->rdev && (size + offset) > radeon_bo_size(reloc->robj)) {
					/* force size to size of the buffer */
					dev_warn(p->dev, "vbo resource seems too big for the bo\n");
					ib[idx+1+(i*8)+1] = radeon_bo_size(reloc->robj);
				}
				ib[idx+1+(i*8)+0] += (u32)((reloc->lobj.gpu_offset) & 0xffffffff);
				ib[idx+1+(i*8)+2] += upper_32_bits(reloc->lobj.gpu_offset) & 0xff;
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
		/* XXX fix me ALU const buffers only */
		break;
	case PACKET3_SET_BOOL_CONST:
		start_reg = (idx_value << 2) + PACKET3_SET_BOOL_CONST_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_BOOL_CONST_START) ||
		    (start_reg >= PACKET3_SET_BOOL_CONST_END) ||
		    (end_reg >= PACKET3_SET_BOOL_CONST_END)) {
			DRM_ERROR("bad SET_BOOL_CONST\n");
			return -EINVAL;
		}
		break;
	case PACKET3_SET_LOOP_CONST:
		start_reg = (idx_value << 2) + PACKET3_SET_LOOP_CONST_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_LOOP_CONST_START) ||
		    (start_reg >= PACKET3_SET_LOOP_CONST_END) ||
		    (end_reg >= PACKET3_SET_LOOP_CONST_END)) {
			DRM_ERROR("bad SET_LOOP_CONST\n");
			return -EINVAL;
		}
		break;
	case PACKET3_SET_CTL_CONST:
		start_reg = (idx_value << 2) + PACKET3_SET_CTL_CONST_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CTL_CONST_START) ||
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
		start_reg = (idx_value << 2) + PACKET3_SET_SAMPLER_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_SAMPLER_START) ||
		    (start_reg >= PACKET3_SET_SAMPLER_END) ||
		    (end_reg >= PACKET3_SET_SAMPLER_END)) {
			DRM_ERROR("bad SET_SAMPLER\n");
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

int evergreen_cs_parse(struct radeon_cs_parser *p)
{
	struct radeon_cs_packet pkt;
	struct evergreen_cs_track *track;
	int r;

	if (p->track == NULL) {
		/* initialize tracker, we are in kms */
		track = kzalloc(sizeof(*track), GFP_KERNEL);
		if (track == NULL)
			return -ENOMEM;
		evergreen_cs_track_init(track);
		track->npipes = p->rdev->config.evergreen.tiling_npipes;
		track->nbanks = p->rdev->config.evergreen.tiling_nbanks;
		track->group_size = p->rdev->config.evergreen.tiling_group_size;
		p->track = track;
	}
	do {
		r = evergreen_cs_packet_parse(p, &pkt, p->idx);
		if (r) {
			kfree(p->track);
			p->track = NULL;
			return r;
		}
		p->idx += pkt.count + 2;
		switch (pkt.type) {
		case PACKET_TYPE0:
			r = evergreen_cs_parse_packet0(p, &pkt);
			break;
		case PACKET_TYPE2:
			break;
		case PACKET_TYPE3:
			r = evergreen_packet3_check(p, &pkt);
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

