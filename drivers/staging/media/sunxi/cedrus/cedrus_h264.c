// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cedrus VPU driver
 *
 * Copyright (c) 2013 Jens Kuske <jenskuske@gmail.com>
 * Copyright (c) 2018 Bootlin
 */

#include <linux/delay.h>
#include <linux/types.h>

#include <media/videobuf2-dma-contig.h>

#include "cedrus.h"
#include "cedrus_hw.h"
#include "cedrus_regs.h"

enum cedrus_h264_sram_off {
	CEDRUS_SRAM_H264_PRED_WEIGHT_TABLE	= 0x000,
	CEDRUS_SRAM_H264_FRAMEBUFFER_LIST	= 0x100,
	CEDRUS_SRAM_H264_REF_LIST_0		= 0x190,
	CEDRUS_SRAM_H264_REF_LIST_1		= 0x199,
	CEDRUS_SRAM_H264_SCALING_LIST_8x8_0	= 0x200,
	CEDRUS_SRAM_H264_SCALING_LIST_8x8_1	= 0x210,
	CEDRUS_SRAM_H264_SCALING_LIST_4x4	= 0x220,
};

struct cedrus_h264_sram_ref_pic {
	__le32	top_field_order_cnt;
	__le32	bottom_field_order_cnt;
	__le32	frame_info;
	__le32	luma_ptr;
	__le32	chroma_ptr;
	__le32	mv_col_top_ptr;
	__le32	mv_col_bot_ptr;
	__le32	reserved;
} __packed;

#define CEDRUS_H264_FRAME_NUM		18

#define CEDRUS_NEIGHBOR_INFO_BUF_SIZE	(32 * SZ_1K)
#define CEDRUS_MIN_PIC_INFO_BUF_SIZE       (130 * SZ_1K)

static void cedrus_h264_write_sram(struct cedrus_dev *dev,
				   enum cedrus_h264_sram_off off,
				   const void *data, size_t len)
{
	const u32 *buffer = data;
	size_t count = DIV_ROUND_UP(len, 4);

	cedrus_write(dev, VE_AVC_SRAM_PORT_OFFSET, off << 2);

	while (count--)
		cedrus_write(dev, VE_AVC_SRAM_PORT_DATA, *buffer++);
}

static dma_addr_t cedrus_h264_mv_col_buf_addr(struct cedrus_buffer *buf,
					      unsigned int field)
{
	dma_addr_t addr = buf->codec.h264.mv_col_buf_dma;

	/* Adjust for the field */
	addr += field * buf->codec.h264.mv_col_buf_size / 2;

	return addr;
}

static void cedrus_fill_ref_pic(struct cedrus_ctx *ctx,
				struct cedrus_buffer *buf,
				unsigned int top_field_order_cnt,
				unsigned int bottom_field_order_cnt,
				struct cedrus_h264_sram_ref_pic *pic)
{
	struct vb2_buffer *vbuf = &buf->m2m_buf.vb.vb2_buf;

	pic->top_field_order_cnt = cpu_to_le32(top_field_order_cnt);
	pic->bottom_field_order_cnt = cpu_to_le32(bottom_field_order_cnt);
	pic->frame_info = cpu_to_le32(buf->codec.h264.pic_type << 8);

	pic->luma_ptr = cpu_to_le32(cedrus_buf_addr(vbuf, &ctx->dst_fmt, 0));
	pic->chroma_ptr = cpu_to_le32(cedrus_buf_addr(vbuf, &ctx->dst_fmt, 1));
	pic->mv_col_top_ptr = cpu_to_le32(cedrus_h264_mv_col_buf_addr(buf, 0));
	pic->mv_col_bot_ptr = cpu_to_le32(cedrus_h264_mv_col_buf_addr(buf, 1));
}

static int cedrus_write_frame_list(struct cedrus_ctx *ctx,
				   struct cedrus_run *run)
{
	struct cedrus_h264_sram_ref_pic pic_list[CEDRUS_H264_FRAME_NUM];
	const struct v4l2_ctrl_h264_decode_params *decode = run->h264.decode_params;
	const struct v4l2_ctrl_h264_sps *sps = run->h264.sps;
	struct vb2_queue *cap_q;
	struct cedrus_buffer *output_buf;
	struct cedrus_dev *dev = ctx->dev;
	unsigned long used_dpbs = 0;
	unsigned int position;
	int output = -1;
	unsigned int i;

	cap_q = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);

	memset(pic_list, 0, sizeof(pic_list));

	for (i = 0; i < ARRAY_SIZE(decode->dpb); i++) {
		const struct v4l2_h264_dpb_entry *dpb = &decode->dpb[i];
		struct cedrus_buffer *cedrus_buf;
		struct vb2_buffer *buf;

		if (!(dpb->flags & V4L2_H264_DPB_ENTRY_FLAG_VALID))
			continue;

		buf = vb2_find_buffer(cap_q, dpb->reference_ts);
		if (!buf)
			continue;

		cedrus_buf = vb2_to_cedrus_buffer(buf);
		position = cedrus_buf->codec.h264.position;
		used_dpbs |= BIT(position);

		if (run->dst->vb2_buf.timestamp == dpb->reference_ts) {
			output = position;
			continue;
		}

		if (!(dpb->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE))
			continue;

		cedrus_fill_ref_pic(ctx, cedrus_buf,
				    dpb->top_field_order_cnt,
				    dpb->bottom_field_order_cnt,
				    &pic_list[position]);
	}

	if (output >= 0)
		position = output;
	else
		position = find_first_zero_bit(&used_dpbs, CEDRUS_H264_FRAME_NUM);

	output_buf = vb2_to_cedrus_buffer(&run->dst->vb2_buf);
	output_buf->codec.h264.position = position;

	if (!output_buf->codec.h264.mv_col_buf_size) {
		const struct v4l2_ctrl_h264_sps *sps = run->h264.sps;
		unsigned int field_size;

		field_size = DIV_ROUND_UP(ctx->src_fmt.width, 16) *
			DIV_ROUND_UP(ctx->src_fmt.height, 16) * 16;
		if (!(sps->flags & V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE))
			field_size = field_size * 2;
		if (!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY))
			field_size = field_size * 2;

		output_buf->codec.h264.mv_col_buf_size = field_size * 2;
		/* Buffer is never accessed by CPU, so we can skip kernel mapping. */
		output_buf->codec.h264.mv_col_buf =
			dma_alloc_attrs(dev->dev,
					output_buf->codec.h264.mv_col_buf_size,
					&output_buf->codec.h264.mv_col_buf_dma,
					GFP_KERNEL, DMA_ATTR_NO_KERNEL_MAPPING);

		if (!output_buf->codec.h264.mv_col_buf) {
			output_buf->codec.h264.mv_col_buf_size = 0;
			return -ENOMEM;
		}
	}

	if (decode->flags & V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC)
		output_buf->codec.h264.pic_type = CEDRUS_H264_PIC_TYPE_FIELD;
	else if (sps->flags & V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD)
		output_buf->codec.h264.pic_type = CEDRUS_H264_PIC_TYPE_MBAFF;
	else
		output_buf->codec.h264.pic_type = CEDRUS_H264_PIC_TYPE_FRAME;

	cedrus_fill_ref_pic(ctx, output_buf,
			    decode->top_field_order_cnt,
			    decode->bottom_field_order_cnt,
			    &pic_list[position]);

	cedrus_h264_write_sram(dev, CEDRUS_SRAM_H264_FRAMEBUFFER_LIST,
			       pic_list, sizeof(pic_list));

	cedrus_write(dev, VE_H264_OUTPUT_FRAME_IDX, position);

	return 0;
}

#define CEDRUS_MAX_REF_IDX	32

static void _cedrus_write_ref_list(struct cedrus_ctx *ctx,
				   struct cedrus_run *run,
				   const struct v4l2_h264_reference *ref_list,
				   u8 num_ref, enum cedrus_h264_sram_off sram)
{
	const struct v4l2_ctrl_h264_decode_params *decode = run->h264.decode_params;
	struct vb2_queue *cap_q;
	struct cedrus_dev *dev = ctx->dev;
	u8 sram_array[CEDRUS_MAX_REF_IDX];
	unsigned int i;
	size_t size;

	cap_q = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);

	memset(sram_array, 0, sizeof(sram_array));

	for (i = 0; i < num_ref; i++) {
		const struct v4l2_h264_dpb_entry *dpb;
		const struct cedrus_buffer *cedrus_buf;
		unsigned int position;
		struct vb2_buffer *buf;
		u8 dpb_idx;

		dpb_idx = ref_list[i].index;
		dpb = &decode->dpb[dpb_idx];

		if (!(dpb->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE))
			continue;

		buf = vb2_find_buffer(cap_q, dpb->reference_ts);
		if (!buf)
			continue;

		cedrus_buf = vb2_to_cedrus_buffer(buf);
		position = cedrus_buf->codec.h264.position;

		sram_array[i] |= position << 1;
		if (ref_list[i].fields == V4L2_H264_BOTTOM_FIELD_REF)
			sram_array[i] |= BIT(0);
	}

	size = min_t(size_t, ALIGN(num_ref, 4), sizeof(sram_array));
	cedrus_h264_write_sram(dev, sram, &sram_array, size);
}

static void cedrus_write_ref_list0(struct cedrus_ctx *ctx,
				   struct cedrus_run *run)
{
	const struct v4l2_ctrl_h264_slice_params *slice = run->h264.slice_params;

	_cedrus_write_ref_list(ctx, run,
			       slice->ref_pic_list0,
			       slice->num_ref_idx_l0_active_minus1 + 1,
			       CEDRUS_SRAM_H264_REF_LIST_0);
}

static void cedrus_write_ref_list1(struct cedrus_ctx *ctx,
				   struct cedrus_run *run)
{
	const struct v4l2_ctrl_h264_slice_params *slice = run->h264.slice_params;

	_cedrus_write_ref_list(ctx, run,
			       slice->ref_pic_list1,
			       slice->num_ref_idx_l1_active_minus1 + 1,
			       CEDRUS_SRAM_H264_REF_LIST_1);
}

static void cedrus_write_scaling_lists(struct cedrus_ctx *ctx,
				       struct cedrus_run *run)
{
	const struct v4l2_ctrl_h264_scaling_matrix *scaling =
		run->h264.scaling_matrix;
	const struct v4l2_ctrl_h264_pps *pps = run->h264.pps;
	struct cedrus_dev *dev = ctx->dev;

	if (!(pps->flags & V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT))
		return;

	cedrus_h264_write_sram(dev, CEDRUS_SRAM_H264_SCALING_LIST_8x8_0,
			       scaling->scaling_list_8x8[0],
			       sizeof(scaling->scaling_list_8x8[0]));

	cedrus_h264_write_sram(dev, CEDRUS_SRAM_H264_SCALING_LIST_8x8_1,
			       scaling->scaling_list_8x8[1],
			       sizeof(scaling->scaling_list_8x8[1]));

	cedrus_h264_write_sram(dev, CEDRUS_SRAM_H264_SCALING_LIST_4x4,
			       scaling->scaling_list_4x4,
			       sizeof(scaling->scaling_list_4x4));
}

static void cedrus_write_pred_weight_table(struct cedrus_ctx *ctx,
					   struct cedrus_run *run)
{
	const struct v4l2_ctrl_h264_pred_weights *pred_weight =
		run->h264.pred_weights;
	struct cedrus_dev *dev = ctx->dev;
	int i, j, k;

	cedrus_write(dev, VE_H264_SHS_WP,
		     ((pred_weight->chroma_log2_weight_denom & 0x7) << 4) |
		     ((pred_weight->luma_log2_weight_denom & 0x7) << 0));

	cedrus_write(dev, VE_AVC_SRAM_PORT_OFFSET,
		     CEDRUS_SRAM_H264_PRED_WEIGHT_TABLE << 2);

	for (i = 0; i < ARRAY_SIZE(pred_weight->weight_factors); i++) {
		const struct v4l2_h264_weight_factors *factors =
			&pred_weight->weight_factors[i];

		for (j = 0; j < ARRAY_SIZE(factors->luma_weight); j++) {
			u32 val;

			val = (((u32)factors->luma_offset[j] & 0x1ff) << 16) |
				(factors->luma_weight[j] & 0x1ff);
			cedrus_write(dev, VE_AVC_SRAM_PORT_DATA, val);
		}

		for (j = 0; j < ARRAY_SIZE(factors->chroma_weight); j++) {
			for (k = 0; k < ARRAY_SIZE(factors->chroma_weight[0]); k++) {
				u32 val;

				val = (((u32)factors->chroma_offset[j][k] & 0x1ff) << 16) |
					(factors->chroma_weight[j][k] & 0x1ff);
				cedrus_write(dev, VE_AVC_SRAM_PORT_DATA, val);
			}
		}
	}
}

/*
 * It turns out that using VE_H264_VLD_OFFSET to skip bits is not reliable. In
 * rare cases frame is not decoded correctly. However, setting offset to 0 and
 * skipping appropriate amount of bits with flush bits trigger always works.
 */
static void cedrus_skip_bits(struct cedrus_dev *dev, int num)
{
	int count = 0;

	while (count < num) {
		int tmp = min(num - count, 32);

		cedrus_write(dev, VE_H264_TRIGGER_TYPE,
			     VE_H264_TRIGGER_TYPE_FLUSH_BITS |
			     VE_H264_TRIGGER_TYPE_N_BITS(tmp));
		while (cedrus_read(dev, VE_H264_STATUS) & VE_H264_STATUS_VLD_BUSY)
			udelay(1);

		count += tmp;
	}
}

static void cedrus_set_params(struct cedrus_ctx *ctx,
			      struct cedrus_run *run)
{
	const struct v4l2_ctrl_h264_decode_params *decode = run->h264.decode_params;
	const struct v4l2_ctrl_h264_slice_params *slice = run->h264.slice_params;
	const struct v4l2_ctrl_h264_pps *pps = run->h264.pps;
	const struct v4l2_ctrl_h264_sps *sps = run->h264.sps;
	struct vb2_buffer *src_buf = &run->src->vb2_buf;
	struct cedrus_dev *dev = ctx->dev;
	dma_addr_t src_buf_addr;
	size_t slice_bytes = vb2_get_plane_payload(src_buf, 0);
	unsigned int pic_width_in_mbs;
	bool mbaff_pic;
	u32 reg;

	cedrus_write(dev, VE_H264_VLD_LEN, slice_bytes * 8);
	cedrus_write(dev, VE_H264_VLD_OFFSET, 0);

	src_buf_addr = vb2_dma_contig_plane_dma_addr(src_buf, 0);
	cedrus_write(dev, VE_H264_VLD_END, src_buf_addr + slice_bytes);
	cedrus_write(dev, VE_H264_VLD_ADDR,
		     VE_H264_VLD_ADDR_VAL(src_buf_addr) |
		     VE_H264_VLD_ADDR_FIRST | VE_H264_VLD_ADDR_VALID |
		     VE_H264_VLD_ADDR_LAST);

	if (ctx->src_fmt.width > 2048) {
		cedrus_write(dev, VE_BUF_CTRL,
			     VE_BUF_CTRL_INTRAPRED_MIXED_RAM |
			     VE_BUF_CTRL_DBLK_MIXED_RAM);
		cedrus_write(dev, VE_DBLK_DRAM_BUF_ADDR,
			     ctx->codec.h264.deblk_buf_dma);
		cedrus_write(dev, VE_INTRAPRED_DRAM_BUF_ADDR,
			     ctx->codec.h264.intra_pred_buf_dma);
	} else {
		cedrus_write(dev, VE_BUF_CTRL,
			     VE_BUF_CTRL_INTRAPRED_INT_SRAM |
			     VE_BUF_CTRL_DBLK_INT_SRAM);
	}

	/*
	 * FIXME: Since the bitstream parsing is done in software, and
	 * in userspace, this shouldn't be needed anymore. But it
	 * turns out that removing it breaks the decoding process,
	 * without any clear indication why.
	 */
	cedrus_write(dev, VE_H264_TRIGGER_TYPE,
		     VE_H264_TRIGGER_TYPE_INIT_SWDEC);

	cedrus_skip_bits(dev, slice->header_bit_size);

	if (V4L2_H264_CTRL_PRED_WEIGHTS_REQUIRED(pps, slice))
		cedrus_write_pred_weight_table(ctx, run);

	if ((slice->slice_type == V4L2_H264_SLICE_TYPE_P) ||
	    (slice->slice_type == V4L2_H264_SLICE_TYPE_SP) ||
	    (slice->slice_type == V4L2_H264_SLICE_TYPE_B))
		cedrus_write_ref_list0(ctx, run);

	if (slice->slice_type == V4L2_H264_SLICE_TYPE_B)
		cedrus_write_ref_list1(ctx, run);

	// picture parameters
	reg = 0;
	/*
	 * FIXME: the kernel headers are allowing the default value to
	 * be passed, but the libva doesn't give us that.
	 */
	reg |= (slice->num_ref_idx_l0_active_minus1 & 0x1f) << 10;
	reg |= (slice->num_ref_idx_l1_active_minus1 & 0x1f) << 5;
	reg |= (pps->weighted_bipred_idc & 0x3) << 2;
	if (pps->flags & V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE)
		reg |= VE_H264_PPS_ENTROPY_CODING_MODE;
	if (pps->flags & V4L2_H264_PPS_FLAG_WEIGHTED_PRED)
		reg |= VE_H264_PPS_WEIGHTED_PRED;
	if (pps->flags & V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED)
		reg |= VE_H264_PPS_CONSTRAINED_INTRA_PRED;
	if (pps->flags & V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE)
		reg |= VE_H264_PPS_TRANSFORM_8X8_MODE;
	cedrus_write(dev, VE_H264_PPS, reg);

	// sequence parameters
	reg = 0;
	reg |= (sps->chroma_format_idc & 0x7) << 19;
	reg |= (sps->pic_width_in_mbs_minus1 & 0xff) << 8;
	reg |= sps->pic_height_in_map_units_minus1 & 0xff;
	if (sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY)
		reg |= VE_H264_SPS_MBS_ONLY;
	if (sps->flags & V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD)
		reg |= VE_H264_SPS_MB_ADAPTIVE_FRAME_FIELD;
	if (sps->flags & V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE)
		reg |= VE_H264_SPS_DIRECT_8X8_INFERENCE;
	cedrus_write(dev, VE_H264_SPS, reg);

	mbaff_pic = !(decode->flags & V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC) &&
		    (sps->flags & V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD);
	pic_width_in_mbs = sps->pic_width_in_mbs_minus1 + 1;

	// slice parameters
	reg = 0;
	reg |= ((slice->first_mb_in_slice % pic_width_in_mbs) & 0xff) << 24;
	reg |= (((slice->first_mb_in_slice / pic_width_in_mbs) *
		 (mbaff_pic + 1)) & 0xff) << 16;
	reg |= decode->nal_ref_idc ? BIT(12) : 0;
	reg |= (slice->slice_type & 0xf) << 8;
	reg |= slice->cabac_init_idc & 0x3;
	if (ctx->fh.m2m_ctx->new_frame)
		reg |= VE_H264_SHS_FIRST_SLICE_IN_PIC;
	if (decode->flags & V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC)
		reg |= VE_H264_SHS_FIELD_PIC;
	if (decode->flags & V4L2_H264_DECODE_PARAM_FLAG_BOTTOM_FIELD)
		reg |= VE_H264_SHS_BOTTOM_FIELD;
	if (slice->flags & V4L2_H264_SLICE_FLAG_DIRECT_SPATIAL_MV_PRED)
		reg |= VE_H264_SHS_DIRECT_SPATIAL_MV_PRED;
	cedrus_write(dev, VE_H264_SHS, reg);

	reg = 0;
	reg |= VE_H264_SHS2_NUM_REF_IDX_ACTIVE_OVRD;
	reg |= (slice->num_ref_idx_l0_active_minus1 & 0x1f) << 24;
	reg |= (slice->num_ref_idx_l1_active_minus1 & 0x1f) << 16;
	reg |= (slice->disable_deblocking_filter_idc & 0x3) << 8;
	reg |= (slice->slice_alpha_c0_offset_div2 & 0xf) << 4;
	reg |= slice->slice_beta_offset_div2 & 0xf;
	cedrus_write(dev, VE_H264_SHS2, reg);

	reg = 0;
	reg |= (pps->second_chroma_qp_index_offset & 0x3f) << 16;
	reg |= (pps->chroma_qp_index_offset & 0x3f) << 8;
	reg |= (pps->pic_init_qp_minus26 + 26 + slice->slice_qp_delta) & 0x3f;
	if (!(pps->flags & V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT))
		reg |= VE_H264_SHS_QP_SCALING_MATRIX_DEFAULT;
	cedrus_write(dev, VE_H264_SHS_QP, reg);

	// clear status flags
	cedrus_write(dev, VE_H264_STATUS, cedrus_read(dev, VE_H264_STATUS));

	// enable int
	cedrus_write(dev, VE_H264_CTRL,
		     VE_H264_CTRL_SLICE_DECODE_INT |
		     VE_H264_CTRL_DECODE_ERR_INT |
		     VE_H264_CTRL_VLD_DATA_REQ_INT);
}

static enum cedrus_irq_status
cedrus_h264_irq_status(struct cedrus_ctx *ctx)
{
	struct cedrus_dev *dev = ctx->dev;
	u32 reg = cedrus_read(dev, VE_H264_STATUS);

	if (reg & (VE_H264_STATUS_DECODE_ERR_INT |
		   VE_H264_STATUS_VLD_DATA_REQ_INT))
		return CEDRUS_IRQ_ERROR;

	if (reg & VE_H264_CTRL_SLICE_DECODE_INT)
		return CEDRUS_IRQ_OK;

	return CEDRUS_IRQ_NONE;
}

static void cedrus_h264_irq_clear(struct cedrus_ctx *ctx)
{
	struct cedrus_dev *dev = ctx->dev;

	cedrus_write(dev, VE_H264_STATUS,
		     VE_H264_STATUS_INT_MASK);
}

static void cedrus_h264_irq_disable(struct cedrus_ctx *ctx)
{
	struct cedrus_dev *dev = ctx->dev;
	u32 reg = cedrus_read(dev, VE_H264_CTRL);

	cedrus_write(dev, VE_H264_CTRL,
		     reg & ~VE_H264_CTRL_INT_MASK);
}

static int cedrus_h264_setup(struct cedrus_ctx *ctx, struct cedrus_run *run)
{
	struct cedrus_dev *dev = ctx->dev;
	int ret;

	cedrus_engine_enable(ctx);

	cedrus_write(dev, VE_H264_SDROT_CTRL, 0);
	cedrus_write(dev, VE_H264_EXTRA_BUFFER1,
		     ctx->codec.h264.pic_info_buf_dma);
	cedrus_write(dev, VE_H264_EXTRA_BUFFER2,
		     ctx->codec.h264.neighbor_info_buf_dma);

	cedrus_write_scaling_lists(ctx, run);
	ret = cedrus_write_frame_list(ctx, run);
	if (ret)
		return ret;

	cedrus_set_params(ctx, run);

	return 0;
}

static int cedrus_h264_start(struct cedrus_ctx *ctx)
{
	struct cedrus_dev *dev = ctx->dev;
	unsigned int pic_info_size;
	int ret;

	/*
	 * NOTE: All buffers allocated here are only used by HW, so we
	 * can add DMA_ATTR_NO_KERNEL_MAPPING flag when allocating them.
	 */

	/* Formula for picture buffer size is taken from CedarX source. */

	if (ctx->src_fmt.width > 2048)
		pic_info_size = CEDRUS_H264_FRAME_NUM * 0x4000;
	else
		pic_info_size = CEDRUS_H264_FRAME_NUM * 0x1000;

	/*
	 * FIXME: If V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY is set,
	 * there is no need to multiply by 2.
	 */
	pic_info_size += ctx->src_fmt.height * 2 * 64;

	if (pic_info_size < CEDRUS_MIN_PIC_INFO_BUF_SIZE)
		pic_info_size = CEDRUS_MIN_PIC_INFO_BUF_SIZE;

	ctx->codec.h264.pic_info_buf_size = pic_info_size;
	ctx->codec.h264.pic_info_buf =
		dma_alloc_attrs(dev->dev, ctx->codec.h264.pic_info_buf_size,
				&ctx->codec.h264.pic_info_buf_dma,
				GFP_KERNEL, DMA_ATTR_NO_KERNEL_MAPPING);
	if (!ctx->codec.h264.pic_info_buf)
		return -ENOMEM;

	/*
	 * That buffer is supposed to be 16kiB in size, and be aligned
	 * on 16kiB as well. However, dma_alloc_attrs provides the
	 * guarantee that we'll have a DMA address aligned on the
	 * smallest page order that is greater to the requested size,
	 * so we don't have to overallocate.
	 */
	ctx->codec.h264.neighbor_info_buf =
		dma_alloc_attrs(dev->dev, CEDRUS_NEIGHBOR_INFO_BUF_SIZE,
				&ctx->codec.h264.neighbor_info_buf_dma,
				GFP_KERNEL, DMA_ATTR_NO_KERNEL_MAPPING);
	if (!ctx->codec.h264.neighbor_info_buf) {
		ret = -ENOMEM;
		goto err_pic_buf;
	}

	if (ctx->src_fmt.width > 2048) {
		/*
		 * Formulas for deblock and intra prediction buffer sizes
		 * are taken from CedarX source.
		 */

		ctx->codec.h264.deblk_buf_size =
			ALIGN(ctx->src_fmt.width, 32) * 12;
		ctx->codec.h264.deblk_buf =
			dma_alloc_attrs(dev->dev,
					ctx->codec.h264.deblk_buf_size,
					&ctx->codec.h264.deblk_buf_dma,
					GFP_KERNEL, DMA_ATTR_NO_KERNEL_MAPPING);
		if (!ctx->codec.h264.deblk_buf) {
			ret = -ENOMEM;
			goto err_neighbor_buf;
		}

		/*
		 * NOTE: Multiplying by two deviates from CedarX logic, but it
		 * is for some unknown reason needed for H264 4K decoding on H6.
		 */
		ctx->codec.h264.intra_pred_buf_size =
			ALIGN(ctx->src_fmt.width, 64) * 5 * 2;
		ctx->codec.h264.intra_pred_buf =
			dma_alloc_attrs(dev->dev,
					ctx->codec.h264.intra_pred_buf_size,
					&ctx->codec.h264.intra_pred_buf_dma,
					GFP_KERNEL, DMA_ATTR_NO_KERNEL_MAPPING);
		if (!ctx->codec.h264.intra_pred_buf) {
			ret = -ENOMEM;
			goto err_deblk_buf;
		}
	}

	return 0;

err_deblk_buf:
	dma_free_attrs(dev->dev, ctx->codec.h264.deblk_buf_size,
		       ctx->codec.h264.deblk_buf,
		       ctx->codec.h264.deblk_buf_dma,
		       DMA_ATTR_NO_KERNEL_MAPPING);

err_neighbor_buf:
	dma_free_attrs(dev->dev, CEDRUS_NEIGHBOR_INFO_BUF_SIZE,
		       ctx->codec.h264.neighbor_info_buf,
		       ctx->codec.h264.neighbor_info_buf_dma,
		       DMA_ATTR_NO_KERNEL_MAPPING);

err_pic_buf:
	dma_free_attrs(dev->dev, ctx->codec.h264.pic_info_buf_size,
		       ctx->codec.h264.pic_info_buf,
		       ctx->codec.h264.pic_info_buf_dma,
		       DMA_ATTR_NO_KERNEL_MAPPING);
	return ret;
}

static void cedrus_h264_stop(struct cedrus_ctx *ctx)
{
	struct cedrus_dev *dev = ctx->dev;
	struct cedrus_buffer *buf;
	struct vb2_queue *vq;
	unsigned int i;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);

	for (i = 0; i < vb2_get_num_buffers(vq); i++) {
		struct vb2_buffer *vb = vb2_get_buffer(vq, i);

		if (!vb)
			continue;

		buf = vb2_to_cedrus_buffer(vb);

		if (buf->codec.h264.mv_col_buf_size > 0) {
			dma_free_attrs(dev->dev,
				       buf->codec.h264.mv_col_buf_size,
				       buf->codec.h264.mv_col_buf,
				       buf->codec.h264.mv_col_buf_dma,
				       DMA_ATTR_NO_KERNEL_MAPPING);

			buf->codec.h264.mv_col_buf_size = 0;
		}
	}

	dma_free_attrs(dev->dev, CEDRUS_NEIGHBOR_INFO_BUF_SIZE,
		       ctx->codec.h264.neighbor_info_buf,
		       ctx->codec.h264.neighbor_info_buf_dma,
		       DMA_ATTR_NO_KERNEL_MAPPING);
	dma_free_attrs(dev->dev, ctx->codec.h264.pic_info_buf_size,
		       ctx->codec.h264.pic_info_buf,
		       ctx->codec.h264.pic_info_buf_dma,
		       DMA_ATTR_NO_KERNEL_MAPPING);
	if (ctx->codec.h264.deblk_buf_size)
		dma_free_attrs(dev->dev, ctx->codec.h264.deblk_buf_size,
			       ctx->codec.h264.deblk_buf,
			       ctx->codec.h264.deblk_buf_dma,
			       DMA_ATTR_NO_KERNEL_MAPPING);
	if (ctx->codec.h264.intra_pred_buf_size)
		dma_free_attrs(dev->dev, ctx->codec.h264.intra_pred_buf_size,
			       ctx->codec.h264.intra_pred_buf,
			       ctx->codec.h264.intra_pred_buf_dma,
			       DMA_ATTR_NO_KERNEL_MAPPING);
}

static void cedrus_h264_trigger(struct cedrus_ctx *ctx)
{
	struct cedrus_dev *dev = ctx->dev;

	cedrus_write(dev, VE_H264_TRIGGER_TYPE,
		     VE_H264_TRIGGER_TYPE_AVC_SLICE_DECODE);
}

struct cedrus_dec_ops cedrus_dec_ops_h264 = {
	.irq_clear	= cedrus_h264_irq_clear,
	.irq_disable	= cedrus_h264_irq_disable,
	.irq_status	= cedrus_h264_irq_status,
	.setup		= cedrus_h264_setup,
	.start		= cedrus_h264_start,
	.stop		= cedrus_h264_stop,
	.trigger	= cedrus_h264_trigger,
};
