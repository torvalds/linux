/*
 * Rockchip RK3288 VPU codec vp8 decode driver
 *
 * Copyright (C) 2014 Rockchip Electronics Co., Ltd.
 *	ZhiChao Yu <zhichao.yu@rock-chips.com>
 *
 * Copyright (C) 2014 Google, Inc.
 *	Tomasz Figa <tfiga@chromium.org>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "rockchip_vpu_hw.h"
#include "rk3288_vpu_regs.h"
#include "rockchip_vpu_common.h"

#define DEC_8190_ALIGN_MASK	0x07U

/*
 * probs table with packed
 */
struct vp8_prob_tbl_packed {
	u8 prob_mb_skip_false;
	u8 prob_intra;
	u8 prob_ref_last;
	u8 prob_ref_golden;
	u8 prob_segment[3];
	u8 packed0;

	u8 prob_luma_16x16_pred_mode[4];
	u8 prob_chroma_pred_mode[3];
	u8 packed1;

	/* mv prob */
	u8 prob_mv_context[2][19];
	u8 packed2[2];

	/* coeff probs */
	u8 prob_coeffs[4][8][3][11];
	u8 packed3[96];
};

struct vp8d_reg {
	u32 base;
	u32 shift;
	u32 mask;
};

/* dct partiton base address regs */
static const struct vp8d_reg vp8d_dct_base[8] = {
	{ VDPU_REG_ADDR_STR, 0, 0xffffffff },
	{ VDPU_REG_ADDR_REF(8), 0, 0xffffffff },
	{ VDPU_REG_ADDR_REF(9), 0, 0xffffffff },
	{ VDPU_REG_ADDR_REF(10), 0, 0xffffffff },
	{ VDPU_REG_ADDR_REF(11), 0, 0xffffffff },
	{ VDPU_REG_ADDR_REF(12), 0, 0xffffffff },
	{ VDPU_REG_ADDR_REF(14), 0, 0xffffffff },
	{ VDPU_REG_ADDR_REF(15), 0, 0xffffffff },
};

/* loop filter level regs */
static const struct vp8d_reg vp8d_lf_level[4] = {
	{ VDPU_REG_REF_PIC(2), 18, 0x3f },
	{ VDPU_REG_REF_PIC(2), 12, 0x3f },
	{ VDPU_REG_REF_PIC(2), 6, 0x3f },
	{ VDPU_REG_REF_PIC(2), 0, 0x3f },
};

/* macroblock loop filter level adjustment regs */
static const struct vp8d_reg vp8d_mb_adj[4] = {
	{ VDPU_REG_REF_PIC(0), 21, 0x7f },
	{ VDPU_REG_REF_PIC(0), 14, 0x7f },
	{ VDPU_REG_REF_PIC(0), 7, 0x7f },
	{ VDPU_REG_REF_PIC(0), 0, 0x7f },
};

/* reference frame adjustment regs */
static const struct vp8d_reg vp8d_ref_adj[4] = {
	{ VDPU_REG_REF_PIC(1), 21, 0x7f },
	{ VDPU_REG_REF_PIC(1), 14, 0x7f },
	{ VDPU_REG_REF_PIC(1), 7, 0x7f },
	{ VDPU_REG_REF_PIC(1), 0, 0x7f },
};

/* quantizer regs */
static const struct vp8d_reg vp8d_quant[4] = {
	{ VDPU_REG_REF_PIC(3), 11, 0x7ff },
	{ VDPU_REG_REF_PIC(3), 0, 0x7ff },
	{ VDPU_REG_BD_REF_PIC(4), 11, 0x7ff },
	{ VDPU_REG_BD_REF_PIC(4), 0, 0x7ff },
};

/* quantizer delta regs */
static const struct vp8d_reg vp8d_quant_delta[5] = {
	{ VDPU_REG_REF_PIC(3), 27, 0x1f },
	{ VDPU_REG_REF_PIC(3), 22, 0x1f },
	{ VDPU_REG_BD_REF_PIC(4), 27, 0x1f },
	{ VDPU_REG_BD_REF_PIC(4), 22, 0x1f },
	{ VDPU_REG_BD_P_REF_PIC, 27, 0x1f },
};

/* dct partition start bits regs */
static const struct vp8d_reg vp8d_dct_start_bits[8] = {
	{ VDPU_REG_DEC_CTRL2, 26, 0x3f }, { VDPU_REG_DEC_CTRL4, 26, 0x3f },
	{ VDPU_REG_DEC_CTRL4, 20, 0x3f }, { VDPU_REG_DEC_CTRL7, 24, 0x3f },
	{ VDPU_REG_DEC_CTRL7, 18, 0x3f }, { VDPU_REG_DEC_CTRL7, 12, 0x3f },
	{ VDPU_REG_DEC_CTRL7, 6, 0x3f },  { VDPU_REG_DEC_CTRL7, 0, 0x3f },
};

/* precision filter tap regs */
static const struct vp8d_reg vp8d_pred_bc_tap[8][4] = {
	{
		{ VDPU_REG_PRED_FLT, 22, 0x3ff },
		{ VDPU_REG_PRED_FLT, 12, 0x3ff },
		{ VDPU_REG_PRED_FLT, 2, 0x3ff },
		{ VDPU_REG_REF_PIC(4), 22, 0x3ff },
	},
	{
		{ VDPU_REG_REF_PIC(4), 12, 0x3ff },
		{ VDPU_REG_REF_PIC(4), 2, 0x3ff },
		{ VDPU_REG_REF_PIC(5), 22, 0x3ff },
		{ VDPU_REG_REF_PIC(5), 12, 0x3ff },
	},
	{
		{ VDPU_REG_REF_PIC(5), 2, 0x3ff },
		{ VDPU_REG_REF_PIC(6), 22, 0x3ff },
		{ VDPU_REG_REF_PIC(6), 12, 0x3ff },
		{ VDPU_REG_REF_PIC(6), 2, 0x3ff },
	},
	{
		{ VDPU_REG_REF_PIC(7), 22, 0x3ff },
		{ VDPU_REG_REF_PIC(7), 12, 0x3ff },
		{ VDPU_REG_REF_PIC(7), 2, 0x3ff },
		{ VDPU_REG_LT_REF, 22, 0x3ff },
	},
	{
		{ VDPU_REG_LT_REF, 12, 0x3ff },
		{ VDPU_REG_LT_REF, 2, 0x3ff },
		{ VDPU_REG_VALID_REF, 22, 0x3ff },
		{ VDPU_REG_VALID_REF, 12, 0x3ff },
	},
	{
		{ VDPU_REG_VALID_REF, 2, 0x3ff },
		{ VDPU_REG_BD_REF_PIC(0), 22, 0x3ff },
		{ VDPU_REG_BD_REF_PIC(0), 12, 0x3ff },
		{ VDPU_REG_BD_REF_PIC(0), 2, 0x3ff },
	},
	{
		{ VDPU_REG_BD_REF_PIC(1), 22, 0x3ff },
		{ VDPU_REG_BD_REF_PIC(1), 12, 0x3ff },
		{ VDPU_REG_BD_REF_PIC(1), 2, 0x3ff },
		{ VDPU_REG_BD_REF_PIC(2), 22, 0x3ff },
	},
	{
		{ VDPU_REG_BD_REF_PIC(2), 12, 0x3ff },
		{ VDPU_REG_BD_REF_PIC(2), 2, 0x3ff },
		{ VDPU_REG_BD_REF_PIC(3), 22, 0x3ff },
		{ VDPU_REG_BD_REF_PIC(3), 12, 0x3ff },
	},
};

/*
 * filter taps taken to 7-bit precision,
 * reference RFC6386#Page-16, filters[8][6]
 */
static const u32 vp8d_mc_filter[8][6] = {
	{ 0, 0, 128, 0, 0, 0 },
	{ 0, -6, 123, 12, -1, 0 },
	{ 2, -11, 108, 36, -8, 1 },
	{ 0, -9, 93, 50, -6, 0 },
	{ 3, -16, 77, 77, -16, 3 },
	{ 0, -6, 50, 93, -9, 0 },
	{ 1, -8, 36, 108, -11, 2 },
	{ 0, -1, 12, 123, -6, 0 }
};

static inline void vp8d_reg_write(struct rockchip_vpu_dev *vpu,
				  const struct vp8d_reg *reg, u32 val)
{
	u32 v;

	v = vdpu_read(vpu, reg->base);
	v &= ~(reg->mask << reg->shift);
	v |= ((val & reg->mask) << reg->shift);
	vdpu_write_relaxed(vpu, v, reg->base);
}

/* dump hw params for debug */
#ifdef DEBUG
static void rk3288_vp8d_dump_hdr(struct rockchip_vpu_ctx *ctx)
{
	const struct v4l2_ctrl_vp8_frame_hdr *hdr = ctx->run.vp8d.frame_hdr;
	int dct_total_len = 0;
	int i;

	vpu_debug(4, "Frame tag: key_frame=0x%02x, version=0x%02x\n",
			!hdr->key_frame, hdr->version);

	vpu_debug(4, "Picture size: w=%d, h=%d\n", hdr->width, hdr->height);

	/* stream addresses */
	vpu_debug(4, "Addresses: segmap=0x%x, probs=0x%x\n",
			(int)ctx->hw.vp8d.segment_map.dma,
			(int)ctx->hw.vp8d.prob_tbl.dma);

	/* reference frame info */
	vpu_debug(4, "Ref frame: last=%d, golden=%d, alt=%d\n",
			hdr->last_frame, hdr->golden_frame, hdr->alt_frame);

	/* bool decoder info */
	vpu_debug(4, "Bool decoder: range=0x%x, value=0x%x, count=0x%x\n",
			hdr->bool_dec_range, hdr->bool_dec_value,
			hdr->bool_dec_count);

	/* control partition info */
	vpu_debug(4, "Control Part: offset=0x%x, size=0x%x\n",
			hdr->first_part_offset, hdr->first_part_size);
	vpu_debug(2, "Macroblock Data: bits_offset=0x%x\n",
			hdr->macroblock_bit_offset);

	/* dct partition info */
	for (i = 0; i < hdr->num_dct_parts; i++) {
		dct_total_len += hdr->dct_part_sizes[i];
		vpu_debug(4, "Dct Part%d Size: 0x%x\n",
				i, hdr->dct_part_sizes[i]);
	}

	dct_total_len += (hdr->num_dct_parts - 1) * 3;
	vpu_debug(4, "Dct Part Total Length: 0x%x\n", dct_total_len);
}
#else
static inline void rk3288_vp8d_dump_hdr(struct rockchip_vpu_ctx *ctx) {}
#endif

static void rk3288_vp8d_prob_update(struct rockchip_vpu_ctx *ctx)
{
	const struct v4l2_ctrl_vp8_frame_hdr *hdr = ctx->run.vp8d.frame_hdr;
	const struct v4l2_vp8_entropy_hdr *entropy_hdr = &hdr->entropy_hdr;
	u32 i, j, k;
	u8 *dst;

	/* first probs */
	dst = ctx->hw.vp8d.prob_tbl.cpu;

	dst[0] = hdr->prob_skip_false;
	dst[1] = hdr->prob_intra;
	dst[2] = hdr->prob_last;
	dst[3] = hdr->prob_gf;
	dst[4] = hdr->sgmnt_hdr.segment_probs[0];
	dst[5] = hdr->sgmnt_hdr.segment_probs[1];
	dst[6] = hdr->sgmnt_hdr.segment_probs[2];
	dst[7] = 0;

	dst += 8;
	dst[0] = entropy_hdr->y_mode_probs[0];
	dst[1] = entropy_hdr->y_mode_probs[1];
	dst[2] = entropy_hdr->y_mode_probs[2];
	dst[3] = entropy_hdr->y_mode_probs[3];
	dst[4] = entropy_hdr->uv_mode_probs[0];
	dst[5] = entropy_hdr->uv_mode_probs[1];
	dst[6] = entropy_hdr->uv_mode_probs[2];
	dst[7] = 0; /*unused */

	/* mv probs */
	dst += 8;
	dst[0] = entropy_hdr->mv_probs[0][0]; /* is short */
	dst[1] = entropy_hdr->mv_probs[1][0];
	dst[2] = entropy_hdr->mv_probs[0][1]; /* sign */
	dst[3] = entropy_hdr->mv_probs[1][1];
	dst[4] = entropy_hdr->mv_probs[0][8 + 9];
	dst[5] = entropy_hdr->mv_probs[0][9 + 9];
	dst[6] = entropy_hdr->mv_probs[1][8 + 9];
	dst[7] = entropy_hdr->mv_probs[1][9 + 9];
	dst += 8;
	for (i = 0; i < 2; ++i) {
		for (j = 0; j < 8; j += 4) {
			dst[0] = entropy_hdr->mv_probs[i][j + 9 + 0];
			dst[1] = entropy_hdr->mv_probs[i][j + 9 + 1];
			dst[2] = entropy_hdr->mv_probs[i][j + 9 + 2];
			dst[3] = entropy_hdr->mv_probs[i][j + 9 + 3];
			dst += 4;
		}
	}
	for (i = 0; i < 2; ++i) {
		dst[0] = entropy_hdr->mv_probs[i][0 + 2];
		dst[1] = entropy_hdr->mv_probs[i][1 + 2];
		dst[2] = entropy_hdr->mv_probs[i][2 + 2];
		dst[3] = entropy_hdr->mv_probs[i][3 + 2];
		dst[4] = entropy_hdr->mv_probs[i][4 + 2];
		dst[5] = entropy_hdr->mv_probs[i][5 + 2];
		dst[6] = entropy_hdr->mv_probs[i][6 + 2];
		dst[7] = 0;	/*unused */
		dst += 8;
	}

	/* coeff probs (header part) */
	dst = ctx->hw.vp8d.prob_tbl.cpu;
	dst += (8 * 7);
	for (i = 0; i < 4; ++i) {
		for (j = 0; j < 8; ++j) {
			for (k = 0; k < 3; ++k) {
				dst[0] = entropy_hdr->coeff_probs[i][j][k][0];
				dst[1] = entropy_hdr->coeff_probs[i][j][k][1];
				dst[2] = entropy_hdr->coeff_probs[i][j][k][2];
				dst[3] = entropy_hdr->coeff_probs[i][j][k][3];
				dst += 4;
			}
		}
	}

	/* coeff probs (footer part) */
	dst = ctx->hw.vp8d.prob_tbl.cpu;
	dst += (8 * 55);
	for (i = 0; i < 4; ++i) {
		for (j = 0; j < 8; ++j) {
			for (k = 0; k < 3; ++k) {
				dst[0] = entropy_hdr->coeff_probs[i][j][k][4];
				dst[1] = entropy_hdr->coeff_probs[i][j][k][5];
				dst[2] = entropy_hdr->coeff_probs[i][j][k][6];
				dst[3] = entropy_hdr->coeff_probs[i][j][k][7];
				dst[4] = entropy_hdr->coeff_probs[i][j][k][8];
				dst[5] = entropy_hdr->coeff_probs[i][j][k][9];
				dst[6] = entropy_hdr->coeff_probs[i][j][k][10];
				dst[7] = 0;	/*unused */
				dst += 8;
			}
		}
	}
}

/*
 * set loop filters
 */
static void rk3288_vp8d_cfg_lf(struct rockchip_vpu_ctx *ctx)
{
	const struct v4l2_ctrl_vp8_frame_hdr *hdr = ctx->run.vp8d.frame_hdr;
	struct rockchip_vpu_dev *vpu = ctx->dev;
	u32 reg;
	int i;

	if (!(hdr->sgmnt_hdr.flags & V4L2_VP8_SEGMNT_HDR_FLAG_ENABLED)) {
		vp8d_reg_write(vpu, &vp8d_lf_level[0], hdr->lf_hdr.level);
	} else if (hdr->sgmnt_hdr.segment_feature_mode) {
		/* absolute mode */
		for (i = 0; i < 4; i++)
			vp8d_reg_write(vpu, &vp8d_lf_level[i],
					hdr->sgmnt_hdr.lf_update[i]);
	} else {
		/* delta mode */
		for (i = 0; i < 4; i++)
			vp8d_reg_write(vpu, &vp8d_lf_level[i],
					clamp(hdr->lf_hdr.level
					+ hdr->sgmnt_hdr.lf_update[i], 0, 63));
	}

	reg = VDPU_REG_REF_PIC_FILT_SHARPNESS(hdr->lf_hdr.sharpness_level);
	if (hdr->lf_hdr.type)
		reg |= VDPU_REG_REF_PIC_FILT_TYPE_E;
	vdpu_write_relaxed(vpu, reg, VDPU_REG_REF_PIC(0));

	if (hdr->lf_hdr.flags & V4L2_VP8_LF_HDR_ADJ_ENABLE) {
		for (i = 0; i < 4; i++) {
			vp8d_reg_write(vpu, &vp8d_mb_adj[i],
				hdr->lf_hdr.mb_mode_delta_magnitude[i]);
			vp8d_reg_write(vpu, &vp8d_ref_adj[i],
				hdr->lf_hdr.ref_frm_delta_magnitude[i]);
		}
	}
}

/*
 * set quantization parameters
 */
static void rk3288_vp8d_cfg_qp(struct rockchip_vpu_ctx *ctx)
{
	const struct v4l2_ctrl_vp8_frame_hdr *hdr = ctx->run.vp8d.frame_hdr;
	struct rockchip_vpu_dev *vpu = ctx->dev;
	int i;

	if (!(hdr->sgmnt_hdr.flags & V4L2_VP8_SEGMNT_HDR_FLAG_ENABLED)) {
		vp8d_reg_write(vpu, &vp8d_quant[0], hdr->quant_hdr.y_ac_qi);
	} else if (hdr->sgmnt_hdr.segment_feature_mode) {
		/* absolute mode */
		for (i = 0; i < 4; i++)
			vp8d_reg_write(vpu, &vp8d_quant[i],
					hdr->sgmnt_hdr.quant_update[i]);
	} else {
		/* delta mode */
		for (i = 0; i < 4; i++)
			vp8d_reg_write(vpu, &vp8d_quant[i],
					clamp(hdr->quant_hdr.y_ac_qi
					+ hdr->sgmnt_hdr.quant_update[i],
					0, 127));
	}

	vp8d_reg_write(vpu, &vp8d_quant_delta[0], hdr->quant_hdr.y_dc_delta);
	vp8d_reg_write(vpu, &vp8d_quant_delta[1], hdr->quant_hdr.y2_dc_delta);
	vp8d_reg_write(vpu, &vp8d_quant_delta[2], hdr->quant_hdr.y2_ac_delta);
	vp8d_reg_write(vpu, &vp8d_quant_delta[3], hdr->quant_hdr.uv_dc_delta);
	vp8d_reg_write(vpu, &vp8d_quant_delta[4], hdr->quant_hdr.uv_ac_delta);
}

/*
 * set control partition and dct partition regs
 *
 * VP8 frame stream data layout:
 *
 *	                     first_part_size          parttion_sizes[0]
 *                              ^                     ^
 * src_dma                      |                     |
 * ^                   +--------+------+        +-----+-----+
 * |                   | control part  |        |           |
 * +--------+----------------+------------------+-----------+-----+-----------+
 * | tag 3B | extra 7B | hdr | mb_data | dct sz | dct part0 | ... | dct partn |
 * +--------+-----------------------------------+-----------+-----+-----------+
 *                     |     |         |        |                             |
 *                     |     v         +----+---+                             v
 *                     |     mb_start       |                       src_dma_end
 *                     v                    v
 *             first_part_offset         dct size part
 *                                      (num_dct-1)*3B
 * Note:
 *   1. only key frame has extra 7 bytes
 *   2. all offsets are base on src_dma
 *   3. number of dct parts is 1, 2, 4 or 8
 *   4. the addresses set to vpu must be 64bits alignment
 */
static void rk3288_vp8d_cfg_parts(struct rockchip_vpu_ctx *ctx)
{
	const struct v4l2_ctrl_vp8_frame_hdr *hdr = ctx->run.vp8d.frame_hdr;
	struct rockchip_vpu_dev *vpu = ctx->dev;
	u32 dct_part_total_len = 0;
	u32 dct_size_part_size = 0;
	u32 dct_part_offset = 0;
	u32 mb_offset_bytes = 0;
	u32 mb_offset_bits = 0;
	u32 mb_start_bits = 0;
	struct vp8d_reg reg;
	dma_addr_t src_dma;
	u32 mb_size = 0;
	u32 count = 0;
	u32 i;

	src_dma = vb2_dma_contig_plane_dma_addr(&ctx->run.src->vb.vb2_buf, 0);

	/*
	 * Calculate control partition mb data info
	 * @macroblock_bit_offset:	bits offset of mb data from first
	 *				part start pos
	 * @mb_offset_bits:		bits offset of mb data from src_dma
	 *				base addr
	 * @mb_offset_byte:		bytes offset of mb data from src_dma
	 *				base addr
	 * @mb_start_bits:		bits offset of mb data from mb data
	 *				64bits alignment addr
	 */
	mb_offset_bits = hdr->first_part_offset * 8
		+ hdr->macroblock_bit_offset + 8;
	mb_offset_bytes = mb_offset_bits / 8;
	mb_start_bits = mb_offset_bits
		- (mb_offset_bytes & (~DEC_8190_ALIGN_MASK)) * 8;
	mb_size = hdr->first_part_size
		- (mb_offset_bytes - hdr->first_part_offset)
		+ (mb_offset_bytes & DEC_8190_ALIGN_MASK);

	/* mb data aligned base addr */
	vdpu_write_relaxed(vpu, (mb_offset_bytes & (~DEC_8190_ALIGN_MASK))
				+ src_dma, VDPU_REG_ADDR_REF(13));

	/* mb data start bits */
	reg.base = VDPU_REG_DEC_CTRL2;
	reg.mask = 0x3f;
	reg.shift = 18;
	vp8d_reg_write(vpu, &reg, mb_start_bits);

	/* mb aligned data length */
	reg.base = VDPU_REG_DEC_CTRL6;
	reg.mask = 0x3fffff;
	reg.shift = 0;
	vp8d_reg_write(vpu, &reg, mb_size);

	/*
	 * Calculate dct partition info
	 * @dct_size_part_size: Containing sizes of dct part, every dct part
	 *			has 3 bytes to store its size, except the last
	 *			dct part
	 * @dct_part_offset:	bytes offset of dct parts from src_dma base addr
	 * @dct_part_total_len: total size of all dct parts
	 */
	dct_size_part_size = (hdr->num_dct_parts - 1) * 3;
	dct_part_offset = hdr->first_part_offset + hdr->first_part_size;
	for (i = 0; i < hdr->num_dct_parts; i++)
		dct_part_total_len += hdr->dct_part_sizes[i];
	dct_part_total_len += dct_size_part_size;
	dct_part_total_len += (dct_part_offset & DEC_8190_ALIGN_MASK);

	/* number of dct partitions */
	reg.base = VDPU_REG_DEC_CTRL6;
	reg.mask = 0xf;
	reg.shift = 24;
	vp8d_reg_write(vpu, &reg, hdr->num_dct_parts - 1);

	/* dct partition length */
	vdpu_write_relaxed(vpu,
			VDPU_REG_DEC_CTRL3_STREAM_LEN(dct_part_total_len),
			VDPU_REG_DEC_CTRL3);

	/* dct partitions base address */
	for (i = 0; i < hdr->num_dct_parts; i++) {
		u32 byte_offset = dct_part_offset + dct_size_part_size + count;
		u32 base_addr = byte_offset + src_dma;

		vp8d_reg_write(vpu, &vp8d_dct_base[i],
				base_addr & (~DEC_8190_ALIGN_MASK));

		vp8d_reg_write(vpu, &vp8d_dct_start_bits[i],
				(byte_offset & DEC_8190_ALIGN_MASK) * 8);

		count += hdr->dct_part_sizes[i];
	}
}

/*
 * prediction filter taps
 * normal 6-tap filters
 */
static void rk3288_vp8d_cfg_tap(struct rockchip_vpu_ctx *ctx)
{
	const struct v4l2_ctrl_vp8_frame_hdr *hdr = ctx->run.vp8d.frame_hdr;
	struct rockchip_vpu_dev *vpu = ctx->dev;
	struct vp8d_reg reg;
	u32 val = 0;
	int i, j;

	reg.base = VDPU_REG_BD_REF_PIC(3);
	reg.mask = 0xf;

	if ((hdr->version & 0x03) != 0)
		return; /* Tap filter not used. */


	for (i = 0; i < 8; i++) {
		val = (vp8d_mc_filter[i][0] << 2) | vp8d_mc_filter[i][5];

		for (j = 0; j < 4; j++)
			vp8d_reg_write(vpu, &vp8d_pred_bc_tap[i][j],
					vp8d_mc_filter[i][j + 1]);

		switch (i) {
		case 2:
			reg.shift = 8;
			break;
		case 4:
			reg.shift = 4;
			break;
		case 6:
			reg.shift = 0;
			break;
		default:
			continue;
		}

		vp8d_reg_write(vpu, &reg, val);
	}
}

/* set reference frame */
static void rk3288_vp8d_cfg_ref(struct rockchip_vpu_ctx *ctx)
{
	u32 reg;
	struct vb2_buffer *buf;
	struct rockchip_vpu_dev *vpu = ctx->dev;
	const struct v4l2_ctrl_vp8_frame_hdr *hdr = ctx->run.vp8d.frame_hdr;

	/* set last frame address */
	if (hdr->last_frame >= ctx->vq_dst.num_buffers)
		buf = &ctx->run.dst->vb.vb2_buf;
	else
		buf = ctx->dst_bufs[hdr->last_frame];

	if (!hdr->key_frame)
		vdpu_write_relaxed(vpu,
			vb2_dma_contig_plane_dma_addr(&ctx->run.dst->vb.vb2_buf, 0),
			VDPU_REG_ADDR_REF(0));
	else
		vdpu_write_relaxed(vpu, vb2_dma_contig_plane_dma_addr(buf, 0),
			VDPU_REG_ADDR_REF(0));

	/* set golden reference frame buffer address */
	if (hdr->golden_frame >= ctx->vq_dst.num_buffers)
		buf = &ctx->run.dst->vb.vb2_buf;
	else
		buf = ctx->dst_bufs[hdr->golden_frame];

	reg = vb2_dma_contig_plane_dma_addr(buf, 0);
	if (hdr->sign_bias_golden)
		reg |= VDPU_REG_ADDR_REF_TOPC_E;
	vdpu_write_relaxed(vpu, reg, VDPU_REG_ADDR_REF(4));

	/* set alternate reference frame buffer address */
	if (hdr->alt_frame >= ctx->vq_dst.num_buffers)
		buf = &ctx->run.dst->vb.vb2_buf;
	else
		buf = ctx->dst_bufs[hdr->alt_frame];

	reg = vb2_dma_contig_plane_dma_addr(buf, 0);
	if (hdr->sign_bias_alternate)
		reg |= VDPU_REG_ADDR_REF_TOPC_E;
	vdpu_write_relaxed(vpu, reg, VDPU_REG_ADDR_REF(5));
}

static void rk3288_vp8d_cfg_buffers(struct rockchip_vpu_ctx *ctx)
{
	const struct v4l2_ctrl_vp8_frame_hdr *hdr = ctx->run.vp8d.frame_hdr;
	struct rockchip_vpu_dev *vpu = ctx->dev;
	u32 reg;

	/* set probability table buffer address */
	vdpu_write_relaxed(vpu, ctx->hw.vp8d.prob_tbl.dma,
				VDPU_REG_ADDR_QTABLE);

	/* set segment map address */
	reg = 0;
	reg = VDPU_REG_FWD_PIC1_SEGMENT_BASE(ctx->hw.vp8d.segment_map.dma);
	if (hdr->sgmnt_hdr.flags & V4L2_VP8_SEGMNT_HDR_FLAG_ENABLED) {
		reg |= VDPU_REG_FWD_PIC1_SEGMENT_E;
		if (hdr->sgmnt_hdr.flags & V4L2_VP8_SEGMNT_HDR_FLAG_UPDATE_MAP)
			reg |= VDPU_REG_FWD_PIC1_SEGMENT_UPD_E;
	}
	vdpu_write_relaxed(vpu, reg, VDPU_REG_FWD_PIC(0));

	/* set output frame buffer address */
	vdpu_write_relaxed(vpu,
			vb2_dma_contig_plane_dma_addr(&ctx->run.dst->vb.vb2_buf, 0),
			VDPU_REG_ADDR_DST);
}

int rk3288_vpu_vp8d_init(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_dev *vpu = ctx->dev;
	unsigned int mb_width, mb_height;
	size_t segment_map_size;
	int ret;

	/* segment map table size calculation */
	mb_width = MB_WIDTH(ctx->dst_fmt.width);
	mb_height = MB_HEIGHT(ctx->dst_fmt.height);
	segment_map_size = round_up(DIV_ROUND_UP(mb_width * mb_height, 4), 64);

	/*
	 * In context init the dma buffer for segment map must be allocated.
	 * And the data in segment map buffer must be set to all zero.
	 */
	ret = rockchip_vpu_aux_buf_alloc(vpu, &ctx->hw.vp8d.segment_map,
					segment_map_size);
	if (ret) {
		vpu_err("allocate segment map mem failed\n");
		return ret;
	}
	memset(ctx->hw.vp8d.segment_map.cpu, 0, ctx->hw.vp8d.segment_map.size);

	/*
	 * Allocate probability table buffer,
	 * total 1208 bytes, 4K page is far enough.
	 */
	ret = rockchip_vpu_aux_buf_alloc(vpu, &ctx->hw.vp8d.prob_tbl,
					sizeof(struct vp8_prob_tbl_packed));
	if (ret) {
		vpu_err("allocate prob table mem failed\n");
		goto prob_table_failed;
	}

	return 0;

prob_table_failed:
	rockchip_vpu_aux_buf_free(vpu, &ctx->hw.vp8d.segment_map);

	return ret;
}

void rk3288_vpu_vp8d_exit(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_dev *vpu = ctx->dev;

	rockchip_vpu_aux_buf_free(vpu, &ctx->hw.vp8d.segment_map);
	rockchip_vpu_aux_buf_free(vpu, &ctx->hw.vp8d.prob_tbl);
}

void rk3288_vpu_vp8d_run(struct rockchip_vpu_ctx *ctx)
{
	const struct v4l2_ctrl_vp8_frame_hdr *hdr = ctx->run.vp8d.frame_hdr;
	struct rockchip_vpu_dev *vpu = ctx->dev;
	size_t height = ctx->dst_fmt.height;
	size_t width = ctx->dst_fmt.width;
	u32 mb_width, mb_height;
	u32 reg;

	rk3288_vp8d_dump_hdr(ctx);

	/* reset segment_map buffer in keyframe */
	if (!hdr->key_frame && ctx->hw.vp8d.segment_map.cpu)
		memset(ctx->hw.vp8d.segment_map.cpu, 0,
			ctx->hw.vp8d.segment_map.size);

	rk3288_vp8d_prob_update(ctx);

	rockchip_vpu_power_on(vpu);

	reg = VDPU_REG_CONFIG_DEC_TIMEOUT_E
		| VDPU_REG_CONFIG_DEC_STRENDIAN_E
		| VDPU_REG_CONFIG_DEC_INSWAP32_E
		| VDPU_REG_CONFIG_DEC_STRSWAP32_E
		| VDPU_REG_CONFIG_DEC_OUTSWAP32_E
		| VDPU_REG_CONFIG_DEC_CLK_GATE_E
		| VDPU_REG_CONFIG_DEC_IN_ENDIAN
		| VDPU_REG_CONFIG_DEC_OUT_ENDIAN
		| VDPU_REG_CONFIG_DEC_MAX_BURST(16);
	vdpu_write_relaxed(vpu, reg, VDPU_REG_CONFIG);

	reg = VDPU_REG_DEC_CTRL0_DEC_MODE(10);
	if (hdr->key_frame)
		reg |= VDPU_REG_DEC_CTRL0_PIC_INTER_E;
	if (!(hdr->flags & V4L2_VP8_FRAME_HDR_FLAG_MB_NO_SKIP_COEFF))
		reg |= VDPU_REG_DEC_CTRL0_SKIP_MODE;
	if (hdr->lf_hdr.level == 0)
		reg |= VDPU_REG_DEC_CTRL0_FILTERING_DIS;
	vdpu_write_relaxed(vpu, reg, VDPU_REG_DEC_CTRL0);

	/* frame dimensions */
	mb_width = MB_WIDTH(width);
	mb_height = MB_HEIGHT(height);
	reg = VDPU_REG_DEC_CTRL1_PIC_MB_WIDTH(mb_width)
		| VDPU_REG_DEC_CTRL1_PIC_MB_HEIGHT_P(mb_height)
		| VDPU_REG_DEC_CTRL1_PIC_MB_W_EXT(mb_width >> 9)
		| VDPU_REG_DEC_CTRL1_PIC_MB_H_EXT(mb_height >> 8);
	vdpu_write_relaxed(vpu, reg, VDPU_REG_DEC_CTRL1);

	/* bool decode info */
	reg = VDPU_REG_DEC_CTRL2_BOOLEAN_RANGE(hdr->bool_dec_range)
		| VDPU_REG_DEC_CTRL2_BOOLEAN_VALUE(hdr->bool_dec_value);
	vdpu_write_relaxed(vpu, reg, VDPU_REG_DEC_CTRL2);

	reg = 0;
	if (hdr->version != 3)
		reg |= VDPU_REG_DEC_CTRL4_VC1_HEIGHT_EXT;
	if (hdr->version & 0x3)
		reg |= VDPU_REG_DEC_CTRL4_BILIN_MC_E;
	vdpu_write_relaxed(vpu, reg, VDPU_REG_DEC_CTRL4);

	rk3288_vp8d_cfg_lf(ctx);
	rk3288_vp8d_cfg_qp(ctx);
	rk3288_vp8d_cfg_parts(ctx);
	rk3288_vp8d_cfg_tap(ctx);
	rk3288_vp8d_cfg_ref(ctx);
	rk3288_vp8d_cfg_buffers(ctx);

	schedule_delayed_work(&vpu->watchdog_work, msecs_to_jiffies(2000));

	vdpu_write(vpu, VDPU_REG_INTERRUPT_DEC_E, VDPU_REG_INTERRUPT);
}
