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
#include "drmP.h"
#include "radeon.h"
#include "r600d.h"

static int r600_cs_packet_next_reloc_mm(struct radeon_cs_parser *p,
					struct radeon_cs_reloc **cs_reloc);
static int r600_cs_packet_next_reloc_nomm(struct radeon_cs_parser *p,
					struct radeon_cs_reloc **cs_reloc);
typedef int (*next_reloc_t)(struct radeon_cs_parser*, struct radeon_cs_reloc**);
static next_reloc_t r600_cs_packet_next_reloc = &r600_cs_packet_next_reloc_mm;

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
	*cs_reloc = &p->relocs[0];
	(*cs_reloc)->lobj.gpu_offset = (u64)relocs_chunk->kdata[idx + 3] << 32;
	(*cs_reloc)->lobj.gpu_offset |= relocs_chunk->kdata[idx + 0];
	return 0;
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
	reg = header >> 2;
	mutex_lock(&p->rdev->ddev->mode_config.mutex);
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
	mutex_unlock(&p->rdev->ddev->mode_config.mutex);
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

static int r600_packet3_check(struct radeon_cs_parser *p,
				struct radeon_cs_packet *pkt)
{
	struct radeon_cs_reloc *reloc;
	volatile u32 *ib;
	unsigned idx;
	unsigned i;
	unsigned start_reg, end_reg, reg;
	int r;
	u32 idx_value;

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
		break;
	case PACKET3_DRAW_INDEX_AUTO:
		if (pkt->count != 1) {
			DRM_ERROR("bad DRAW_INDEX_AUTO\n");
			return -EINVAL;
		}
		break;
	case PACKET3_DRAW_INDEX_IMMD_BE:
	case PACKET3_DRAW_INDEX_IMMD:
		if (pkt->count < 2) {
			DRM_ERROR("bad DRAW_INDEX_IMMD\n");
			return -EINVAL;
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
			switch (reg) {
			case CP_COHER_BASE:
				/* use PACKET3_SURFACE_SYNC */
				return -EINVAL;
			default:
				break;
			}
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
			switch (reg) {
			case DB_DEPTH_BASE:
			case CB_COLOR0_BASE:
			case CB_COLOR1_BASE:
			case CB_COLOR2_BASE:
			case CB_COLOR3_BASE:
			case CB_COLOR4_BASE:
			case CB_COLOR5_BASE:
			case CB_COLOR6_BASE:
			case CB_COLOR7_BASE:
			case SQ_PGM_START_FS:
			case SQ_PGM_START_ES:
			case SQ_PGM_START_VS:
			case SQ_PGM_START_GS:
			case SQ_PGM_START_PS:
				r = r600_cs_packet_next_reloc(p, &reloc);
				if (r) {
					DRM_ERROR("bad SET_CONTEXT_REG "
							"0x%04X\n", reg);
					return -EINVAL;
				}
				ib[idx+1+i] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
				break;
			case VGT_DMA_BASE:
			case VGT_DMA_BASE_HI:
				/* These should be handled by DRAW_INDEX packet 3 */
			case VGT_STRMOUT_BASE_OFFSET_0:
			case VGT_STRMOUT_BASE_OFFSET_1:
			case VGT_STRMOUT_BASE_OFFSET_2:
			case VGT_STRMOUT_BASE_OFFSET_3:
			case VGT_STRMOUT_BASE_OFFSET_HI_0:
			case VGT_STRMOUT_BASE_OFFSET_HI_1:
			case VGT_STRMOUT_BASE_OFFSET_HI_2:
			case VGT_STRMOUT_BASE_OFFSET_HI_3:
			case VGT_STRMOUT_BUFFER_BASE_0:
			case VGT_STRMOUT_BUFFER_BASE_1:
			case VGT_STRMOUT_BUFFER_BASE_2:
			case VGT_STRMOUT_BUFFER_BASE_3:
			case VGT_STRMOUT_BUFFER_OFFSET_0:
			case VGT_STRMOUT_BUFFER_OFFSET_1:
			case VGT_STRMOUT_BUFFER_OFFSET_2:
			case VGT_STRMOUT_BUFFER_OFFSET_3:
				/* These should be handled by STRMOUT_BUFFER packet 3 */
				DRM_ERROR("bad context reg: 0x%08x\n", reg);
				return -EINVAL;
			default:
				break;
			}
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
			switch (G__SQ_VTX_CONSTANT_TYPE(radeon_get_ib_value(p, idx+(i*7)+6+1))) {
			case SQ_TEX_VTX_VALID_TEXTURE:
				/* tex base */
				r = r600_cs_packet_next_reloc(p, &reloc);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE\n");
					return -EINVAL;
				}
				ib[idx+1+(i*7)+2] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
				/* tex mip base */
				r = r600_cs_packet_next_reloc(p, &reloc);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE\n");
					return -EINVAL;
				}
				ib[idx+1+(i*7)+3] += (u32)((reloc->lobj.gpu_offset >> 8) & 0xffffffff);
				break;
			case SQ_TEX_VTX_VALID_BUFFER:
				/* vtx base */
				r = r600_cs_packet_next_reloc(p, &reloc);
				if (r) {
					DRM_ERROR("bad SET_RESOURCE\n");
					return -EINVAL;
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
		start_reg = (idx_value << 2) + PACKET3_SET_ALU_CONST_OFFSET;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_ALU_CONST_OFFSET) ||
		    (start_reg >= PACKET3_SET_ALU_CONST_END) ||
		    (end_reg >= PACKET3_SET_ALU_CONST_END)) {
			DRM_ERROR("bad SET_ALU_CONST\n");
			return -EINVAL;
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
	int r;

	do {
		r = r600_cs_packet_parse(p, &pkt, p->idx);
		if (r) {
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
			return -EINVAL;
		}
		if (r) {
			return r;
		}
	} while (p->idx < p->chunks[p->chunk_ib_idx].length_dw);
#if 0
	for (r = 0; r < p->ib->length_dw; r++) {
		printk(KERN_INFO "%05d  0x%08X\n", r, p->ib->ptr[r]);
		mdelay(1);
	}
#endif
	return 0;
}

static int r600_cs_parser_relocs_legacy(struct radeon_cs_parser *p)
{
	if (p->chunk_relocs_idx == -1) {
		return 0;
	}
	p->relocs = kcalloc(1, sizeof(struct radeon_cs_reloc), GFP_KERNEL);
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
	struct radeon_ib	fake_ib;
	int r;

	/* initialize parser */
	memset(&parser, 0, sizeof(struct radeon_cs_parser));
	parser.filp = filp;
	parser.rdev = NULL;
	parser.family = family;
	parser.ib = &fake_ib;
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
