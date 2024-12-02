// SPDX-License-Identifier: GPL-2.0+
/*
 * NVIDIA Tegra Video decoder driver
 *
 * Copyright (C) 2016-2022 Dmitry Osipenko <digetx@gmail.com>
 *
 */

#include <linux/iopoll.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <media/v4l2-h264.h>

#include "trace.h"
#include "vde.h"

#define FLAG_B_FRAME		0x1
#define FLAG_REFERENCE		0x2

struct tegra_vde_h264_frame {
	unsigned int frame_num;
	unsigned int flags;
};

struct tegra_vde_h264_decoder_ctx {
	unsigned int dpb_frames_nb;
	unsigned int dpb_ref_frames_with_earlier_poc_nb;
	unsigned int baseline_profile;
	unsigned int level_idc;
	unsigned int log2_max_pic_order_cnt_lsb;
	unsigned int log2_max_frame_num;
	unsigned int pic_order_cnt_type;
	unsigned int direct_8x8_inference_flag;
	unsigned int pic_width_in_mbs;
	unsigned int pic_height_in_mbs;
	unsigned int pic_init_qp;
	unsigned int deblocking_filter_control_present_flag;
	unsigned int constrained_intra_pred_flag;
	unsigned int chroma_qp_index_offset;
	unsigned int pic_order_present_flag;
	unsigned int num_ref_idx_l0_active_minus1;
	unsigned int num_ref_idx_l1_active_minus1;
};

struct h264_reflists {
	struct v4l2_h264_reference p[V4L2_H264_NUM_DPB_ENTRIES];
	struct v4l2_h264_reference b0[V4L2_H264_NUM_DPB_ENTRIES];
	struct v4l2_h264_reference b1[V4L2_H264_NUM_DPB_ENTRIES];
};

static int tegra_vde_wait_mbe(struct tegra_vde *vde)
{
	u32 tmp;

	return readl_relaxed_poll_timeout(vde->mbe + 0x8C, tmp,
					  tmp >= 0x10, 1, 100);
}

static int tegra_vde_setup_mbe_frame_idx(struct tegra_vde *vde,
					 unsigned int refs_nb,
					 bool setup_refs)
{
	u32 value, frame_idx_enb_mask = 0;
	unsigned int frame_idx;
	unsigned int idx;
	int err;

	tegra_vde_writel(vde, 0xD0000000 | (0 << 23), vde->mbe, 0x80);
	tegra_vde_writel(vde, 0xD0200000 | (0 << 23), vde->mbe, 0x80);

	err = tegra_vde_wait_mbe(vde);
	if (err)
		return err;

	if (!setup_refs)
		return 0;

	for (idx = 0, frame_idx = 1; idx < refs_nb; idx++, frame_idx++) {
		tegra_vde_writel(vde, 0xD0000000 | (frame_idx << 23),
				 vde->mbe, 0x80);
		tegra_vde_writel(vde, 0xD0200000 | (frame_idx << 23),
				 vde->mbe, 0x80);

		frame_idx_enb_mask |= frame_idx << (6 * (idx % 4));

		if (idx % 4 == 3 || idx == refs_nb - 1) {
			value = 0xC0000000;
			value |= (idx >> 2) << 24;
			value |= frame_idx_enb_mask;

			tegra_vde_writel(vde, value, vde->mbe, 0x80);

			err = tegra_vde_wait_mbe(vde);
			if (err)
				return err;

			frame_idx_enb_mask = 0;
		}
	}

	return 0;
}

static void tegra_vde_mbe_set_0xa_reg(struct tegra_vde *vde, int reg, u32 val)
{
	tegra_vde_writel(vde, 0xA0000000 | (reg << 24) | (val & 0xFFFF),
			 vde->mbe, 0x80);
	tegra_vde_writel(vde, 0xA0000000 | ((reg + 1) << 24) | (val >> 16),
			 vde->mbe, 0x80);
}

static int tegra_vde_wait_bsev(struct tegra_vde *vde, bool wait_dma)
{
	struct device *dev = vde->dev;
	u32 value;
	int err;

	err = readl_relaxed_poll_timeout(vde->bsev + INTR_STATUS, value,
					 !(value & BIT(2)), 1, 100);
	if (err) {
		dev_err(dev, "BSEV unknown bit timeout\n");
		return err;
	}

	err = readl_relaxed_poll_timeout(vde->bsev + INTR_STATUS, value,
					 (value & BSE_ICMDQUE_EMPTY), 1, 100);
	if (err) {
		dev_err(dev, "BSEV ICMDQUE flush timeout\n");
		return err;
	}

	if (!wait_dma)
		return 0;

	err = readl_relaxed_poll_timeout(vde->bsev + INTR_STATUS, value,
					 !(value & BSE_DMA_BUSY), 1, 1000);
	if (err) {
		dev_err(dev, "BSEV DMA timeout\n");
		return err;
	}

	return 0;
}

static int tegra_vde_push_to_bsev_icmdqueue(struct tegra_vde *vde,
					    u32 value, bool wait_dma)
{
	tegra_vde_writel(vde, value, vde->bsev, ICMDQUE_WR);

	return tegra_vde_wait_bsev(vde, wait_dma);
}

static void tegra_vde_setup_frameid(struct tegra_vde *vde,
				    struct tegra_video_frame *frame,
				    unsigned int frameid,
				    u32 mbs_width, u32 mbs_height)
{
	u32 y_addr  = frame ? frame->y_addr  : 0x6CDEAD00;
	u32 cb_addr = frame ? frame->cb_addr : 0x6CDEAD00;
	u32 cr_addr = frame ? frame->cr_addr : 0x6CDEAD00;
	u32 value1 = frame ? ((frame->luma_atoms_pitch << 16) | mbs_height) : 0;
	u32 value2 = frame ? ((frame->chroma_atoms_pitch << 6) | 1) : 0;

	tegra_vde_writel(vde, y_addr  >> 8, vde->frameid, 0x000 + frameid * 4);
	tegra_vde_writel(vde, cb_addr >> 8, vde->frameid, 0x100 + frameid * 4);
	tegra_vde_writel(vde, cr_addr >> 8, vde->frameid, 0x180 + frameid * 4);
	tegra_vde_writel(vde, value1,       vde->frameid, 0x080 + frameid * 4);
	tegra_vde_writel(vde, value2,       vde->frameid, 0x280 + frameid * 4);
}

static void tegra_setup_frameidx(struct tegra_vde *vde,
				 struct tegra_video_frame *frames,
				 unsigned int frames_nb,
				 u32 mbs_width, u32 mbs_height)
{
	unsigned int idx;

	for (idx = 0; idx < frames_nb; idx++)
		tegra_vde_setup_frameid(vde, &frames[idx], idx,
					mbs_width, mbs_height);

	for (; idx < 17; idx++)
		tegra_vde_setup_frameid(vde, NULL, idx, 0, 0);
}

static void tegra_vde_setup_iram_entry(struct tegra_vde *vde,
				       unsigned int table,
				       unsigned int row,
				       u32 value1, u32 value2)
{
	u32 *iram_tables = vde->iram;

	trace_vde_setup_iram_entry(table, row, value1, value2);

	iram_tables[0x20 * table + row * 2 + 0] = value1;
	iram_tables[0x20 * table + row * 2 + 1] = value2;
}

static void tegra_vde_setup_iram_tables(struct tegra_vde *vde,
					struct tegra_video_frame *dpb_frames,
					unsigned int ref_frames_nb,
					unsigned int with_earlier_poc_nb)
{
	struct tegra_video_frame *frame;
	int with_later_poc_nb;
	u32 value, aux_addr;
	unsigned int i, k;

	trace_vde_ref_l0(dpb_frames[0].frame_num);

	for (i = 0; i < 16; i++) {
		if (i < ref_frames_nb) {
			frame = &dpb_frames[i + 1];

			aux_addr = frame->aux_addr;

			value  = (i + 1) << 26;
			value |= !(frame->flags & FLAG_B_FRAME) << 25;
			value |= 1 << 24;
			value |= frame->frame_num;
		} else {
			aux_addr = 0x6ADEAD00;
			value = 0x3f;
		}

		tegra_vde_setup_iram_entry(vde, 0, i, value, aux_addr);
		tegra_vde_setup_iram_entry(vde, 1, i, value, aux_addr);
		tegra_vde_setup_iram_entry(vde, 2, i, value, aux_addr);
		tegra_vde_setup_iram_entry(vde, 3, i, value, aux_addr);
	}

	if (!(dpb_frames[0].flags & FLAG_B_FRAME))
		return;

	if (with_earlier_poc_nb >= ref_frames_nb)
		return;

	with_later_poc_nb = ref_frames_nb - with_earlier_poc_nb;

	trace_vde_ref_l1(with_later_poc_nb, with_earlier_poc_nb);

	for (i = 0, k = with_earlier_poc_nb; i < with_later_poc_nb; i++, k++) {
		frame = &dpb_frames[k + 1];

		aux_addr = frame->aux_addr;

		value  = (k + 1) << 26;
		value |= !(frame->flags & FLAG_B_FRAME) << 25;
		value |= 1 << 24;
		value |= frame->frame_num;

		tegra_vde_setup_iram_entry(vde, 2, i, value, aux_addr);
	}

	for (k = 0; i < ref_frames_nb; i++, k++) {
		frame = &dpb_frames[k + 1];

		aux_addr = frame->aux_addr;

		value  = (k + 1) << 26;
		value |= !(frame->flags & FLAG_B_FRAME) << 25;
		value |= 1 << 24;
		value |= frame->frame_num;

		tegra_vde_setup_iram_entry(vde, 2, i, value, aux_addr);
	}
}

static int tegra_vde_setup_hw_context(struct tegra_vde *vde,
				      struct tegra_vde_h264_decoder_ctx *ctx,
				      struct tegra_video_frame *dpb_frames,
				      dma_addr_t bitstream_data_addr,
				      size_t bitstream_data_size,
				      unsigned int macroblocks_nb)
{
	struct device *dev = vde->dev;
	u32 value;
	int err;

	tegra_vde_set_bits(vde, 0x000A, vde->sxe, 0xF0);
	tegra_vde_set_bits(vde, 0x000B, vde->bsev, CMDQUE_CONTROL);
	tegra_vde_set_bits(vde, 0x8002, vde->mbe, 0x50);
	tegra_vde_set_bits(vde, 0x000A, vde->mbe, 0xA0);
	tegra_vde_set_bits(vde, 0x000A, vde->ppe, 0x14);
	tegra_vde_set_bits(vde, 0x000A, vde->ppe, 0x28);
	tegra_vde_set_bits(vde, 0x0A00, vde->mce, 0x08);
	tegra_vde_set_bits(vde, 0x000A, vde->tfe, 0x00);
	tegra_vde_set_bits(vde, 0x0005, vde->vdma, 0x04);

	tegra_vde_writel(vde, 0x00000000, vde->vdma, 0x1C);
	tegra_vde_writel(vde, 0x00000000, vde->vdma, 0x00);
	tegra_vde_writel(vde, 0x00000007, vde->vdma, 0x04);
	tegra_vde_writel(vde, 0x00000007, vde->frameid, 0x200);
	tegra_vde_writel(vde, 0x00000005, vde->tfe, 0x04);
	tegra_vde_writel(vde, 0x00000000, vde->mbe, 0x84);
	tegra_vde_writel(vde, 0x00000010, vde->sxe, 0x08);
	tegra_vde_writel(vde, 0x00000150, vde->sxe, 0x54);
	tegra_vde_writel(vde, 0x0000054C, vde->sxe, 0x58);
	tegra_vde_writel(vde, 0x00000E34, vde->sxe, 0x5C);
	tegra_vde_writel(vde, 0x063C063C, vde->mce, 0x10);
	tegra_vde_writel(vde, 0x0003FC00, vde->bsev, INTR_STATUS);
	tegra_vde_writel(vde, 0x0000150D, vde->bsev, BSE_CONFIG);
	tegra_vde_writel(vde, 0x00000100, vde->bsev, BSE_INT_ENB);
	tegra_vde_writel(vde, 0x00000000, vde->bsev, 0x98);
	tegra_vde_writel(vde, 0x00000060, vde->bsev, 0x9C);

	memset(vde->iram + 128, 0, macroblocks_nb / 2);

	tegra_setup_frameidx(vde, dpb_frames, ctx->dpb_frames_nb,
			     ctx->pic_width_in_mbs, ctx->pic_height_in_mbs);

	tegra_vde_setup_iram_tables(vde, dpb_frames,
				    ctx->dpb_frames_nb - 1,
				    ctx->dpb_ref_frames_with_earlier_poc_nb);

	/*
	 * The IRAM mapping is write-combine, ensure that CPU buffers have
	 * been flushed at this point.
	 */
	wmb();

	tegra_vde_writel(vde, 0x00000000, vde->bsev, 0x8C);
	tegra_vde_writel(vde, bitstream_data_addr + bitstream_data_size,
			 vde->bsev, 0x54);

	vde->bitstream_data_addr = bitstream_data_addr;

	value = ctx->pic_width_in_mbs << 11 | ctx->pic_height_in_mbs << 3;

	tegra_vde_writel(vde, value, vde->bsev, 0x88);

	err = tegra_vde_wait_bsev(vde, false);
	if (err)
		return err;

	err = tegra_vde_push_to_bsev_icmdqueue(vde, 0x800003FC, false);
	if (err)
		return err;

	value = 0x01500000;
	value |= ((vde->iram_lists_addr + 512) >> 2) & 0xFFFF;

	err = tegra_vde_push_to_bsev_icmdqueue(vde, value, true);
	if (err)
		return err;

	err = tegra_vde_push_to_bsev_icmdqueue(vde, 0x840F054C, false);
	if (err)
		return err;

	err = tegra_vde_push_to_bsev_icmdqueue(vde, 0x80000080, false);
	if (err)
		return err;

	value = 0x0E340000 | ((vde->iram_lists_addr >> 2) & 0xFFFF);

	err = tegra_vde_push_to_bsev_icmdqueue(vde, value, true);
	if (err)
		return err;

	value = 0x00800005;
	value |= ctx->pic_width_in_mbs << 11;
	value |= ctx->pic_height_in_mbs << 3;

	tegra_vde_writel(vde, value, vde->sxe, 0x10);

	value = !ctx->baseline_profile << 17;
	value |= ctx->level_idc << 13;
	value |= ctx->log2_max_pic_order_cnt_lsb << 7;
	value |= ctx->pic_order_cnt_type << 5;
	value |= ctx->log2_max_frame_num;

	tegra_vde_writel(vde, value, vde->sxe, 0x40);

	value = ctx->pic_init_qp << 25;
	value |= !!(ctx->deblocking_filter_control_present_flag) << 2;
	value |= !!ctx->pic_order_present_flag;

	tegra_vde_writel(vde, value, vde->sxe, 0x44);

	value = ctx->chroma_qp_index_offset;
	value |= ctx->num_ref_idx_l0_active_minus1 << 5;
	value |= ctx->num_ref_idx_l1_active_minus1 << 10;
	value |= !!ctx->constrained_intra_pred_flag << 15;

	tegra_vde_writel(vde, value, vde->sxe, 0x48);

	value = 0x0C000000;
	value |= !!(dpb_frames[0].flags & FLAG_B_FRAME) << 24;

	tegra_vde_writel(vde, value, vde->sxe, 0x4C);

	value = 0x03800000;
	value |= bitstream_data_size & GENMASK(19, 15);

	tegra_vde_writel(vde, value, vde->sxe, 0x68);

	tegra_vde_writel(vde, bitstream_data_addr, vde->sxe, 0x6C);

	if (vde->soc->supports_ref_pic_marking)
		tegra_vde_writel(vde, vde->secure_bo->dma_addr, vde->sxe, 0x7c);

	value = 0x10000005;
	value |= ctx->pic_width_in_mbs << 11;
	value |= ctx->pic_height_in_mbs << 3;

	tegra_vde_writel(vde, value, vde->mbe, 0x80);

	value = 0x26800000;
	value |= ctx->level_idc << 4;
	value |= !ctx->baseline_profile << 1;
	value |= !!ctx->direct_8x8_inference_flag;

	tegra_vde_writel(vde, value, vde->mbe, 0x80);

	tegra_vde_writel(vde, 0xF4000001, vde->mbe, 0x80);
	tegra_vde_writel(vde, 0x20000000, vde->mbe, 0x80);
	tegra_vde_writel(vde, 0xF4000101, vde->mbe, 0x80);

	value = 0x20000000;
	value |= ctx->chroma_qp_index_offset << 8;

	tegra_vde_writel(vde, value, vde->mbe, 0x80);

	err = tegra_vde_setup_mbe_frame_idx(vde,
					    ctx->dpb_frames_nb - 1,
					    ctx->pic_order_cnt_type == 0);
	if (err) {
		dev_err(dev, "MBE frames setup failed %d\n", err);
		return err;
	}

	tegra_vde_mbe_set_0xa_reg(vde, 0, 0x000009FC);
	tegra_vde_mbe_set_0xa_reg(vde, 2, 0x61DEAD00);
	tegra_vde_mbe_set_0xa_reg(vde, 4, 0x62DEAD00);
	tegra_vde_mbe_set_0xa_reg(vde, 6, 0x63DEAD00);
	tegra_vde_mbe_set_0xa_reg(vde, 8, dpb_frames[0].aux_addr);

	value = 0xFC000000;
	value |= !!(dpb_frames[0].flags & FLAG_B_FRAME) << 2;

	if (!ctx->baseline_profile)
		value |= !!(dpb_frames[0].flags & FLAG_REFERENCE) << 1;

	tegra_vde_writel(vde, value, vde->mbe, 0x80);

	err = tegra_vde_wait_mbe(vde);
	if (err) {
		dev_err(dev, "MBE programming failed %d\n", err);
		return err;
	}

	return 0;
}

static void tegra_vde_decode_frame(struct tegra_vde *vde,
				   unsigned int macroblocks_nb)
{
	reinit_completion(&vde->decode_completion);

	tegra_vde_writel(vde, 0x00000001, vde->bsev, 0x8C);
	tegra_vde_writel(vde, 0x20000000 | (macroblocks_nb - 1),
			 vde->sxe, 0x00);
}

static int tegra_vde_validate_h264_ctx(struct device *dev,
				       struct tegra_vde_h264_decoder_ctx *ctx)
{
	if (ctx->dpb_frames_nb == 0 || ctx->dpb_frames_nb > 17) {
		dev_err(dev, "Bad DPB size %u\n", ctx->dpb_frames_nb);
		return -EINVAL;
	}

	if (ctx->level_idc > 15) {
		dev_err(dev, "Bad level value %u\n", ctx->level_idc);
		return -EINVAL;
	}

	if (ctx->pic_init_qp > 52) {
		dev_err(dev, "Bad pic_init_qp value %u\n", ctx->pic_init_qp);
		return -EINVAL;
	}

	if (ctx->log2_max_pic_order_cnt_lsb > 16) {
		dev_err(dev, "Bad log2_max_pic_order_cnt_lsb value %u\n",
			ctx->log2_max_pic_order_cnt_lsb);
		return -EINVAL;
	}

	if (ctx->log2_max_frame_num > 16) {
		dev_err(dev, "Bad log2_max_frame_num value %u\n",
			ctx->log2_max_frame_num);
		return -EINVAL;
	}

	if (ctx->chroma_qp_index_offset > 31) {
		dev_err(dev, "Bad chroma_qp_index_offset value %u\n",
			ctx->chroma_qp_index_offset);
		return -EINVAL;
	}

	if (ctx->pic_order_cnt_type > 2) {
		dev_err(dev, "Bad pic_order_cnt_type value %u\n",
			ctx->pic_order_cnt_type);
		return -EINVAL;
	}

	if (ctx->num_ref_idx_l0_active_minus1 > 15) {
		dev_err(dev, "Bad num_ref_idx_l0_active_minus1 value %u\n",
			ctx->num_ref_idx_l0_active_minus1);
		return -EINVAL;
	}

	if (ctx->num_ref_idx_l1_active_minus1 > 15) {
		dev_err(dev, "Bad num_ref_idx_l1_active_minus1 value %u\n",
			ctx->num_ref_idx_l1_active_minus1);
		return -EINVAL;
	}

	if (!ctx->pic_width_in_mbs || ctx->pic_width_in_mbs > 127) {
		dev_err(dev, "Bad pic_width_in_mbs value %u\n",
			ctx->pic_width_in_mbs);
		return -EINVAL;
	}

	if (!ctx->pic_height_in_mbs || ctx->pic_height_in_mbs > 127) {
		dev_err(dev, "Bad pic_height_in_mbs value %u\n",
			ctx->pic_height_in_mbs);
		return -EINVAL;
	}

	return 0;
}

static int tegra_vde_decode_begin(struct tegra_vde *vde,
				  struct tegra_vde_h264_decoder_ctx *ctx,
				  struct tegra_video_frame *dpb_frames,
				  dma_addr_t bitstream_data_addr,
				  size_t bitstream_data_size)
{
	struct device *dev = vde->dev;
	unsigned int macroblocks_nb;
	int err;

	err = mutex_lock_interruptible(&vde->lock);
	if (err)
		return err;

	err = pm_runtime_resume_and_get(dev);
	if (err < 0)
		goto unlock;

	/*
	 * We rely on the VDE registers reset value, otherwise VDE
	 * causes bus lockup.
	 */
	err = reset_control_assert(vde->rst_mc);
	if (err) {
		dev_err(dev, "DEC start: Failed to assert MC reset: %d\n",
			err);
		goto put_runtime_pm;
	}

	err = reset_control_reset(vde->rst);
	if (err) {
		dev_err(dev, "DEC start: Failed to reset HW: %d\n", err);
		goto put_runtime_pm;
	}

	err = reset_control_deassert(vde->rst_mc);
	if (err) {
		dev_err(dev, "DEC start: Failed to deassert MC reset: %d\n",
			err);
		goto put_runtime_pm;
	}

	macroblocks_nb = ctx->pic_width_in_mbs * ctx->pic_height_in_mbs;

	err = tegra_vde_setup_hw_context(vde, ctx, dpb_frames,
					 bitstream_data_addr,
					 bitstream_data_size,
					 macroblocks_nb);
	if (err)
		goto put_runtime_pm;

	tegra_vde_decode_frame(vde, macroblocks_nb);

	return 0;

put_runtime_pm:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

unlock:
	mutex_unlock(&vde->lock);

	return err;
}

static void tegra_vde_decode_abort(struct tegra_vde *vde)
{
	struct device *dev = vde->dev;
	int err;

	/*
	 * At first reset memory client to avoid resetting VDE HW in the
	 * middle of DMA which could result into memory corruption or hang
	 * the whole system.
	 */
	err = reset_control_assert(vde->rst_mc);
	if (err)
		dev_err(dev, "DEC end: Failed to assert MC reset: %d\n", err);

	err = reset_control_assert(vde->rst);
	if (err)
		dev_err(dev, "DEC end: Failed to assert HW reset: %d\n", err);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	mutex_unlock(&vde->lock);
}

static int tegra_vde_decode_end(struct tegra_vde *vde)
{
	unsigned int read_bytes, macroblocks_nb;
	struct device *dev = vde->dev;
	dma_addr_t bsev_ptr;
	long timeout;
	int ret;

	timeout = wait_for_completion_interruptible_timeout(
			&vde->decode_completion, msecs_to_jiffies(1000));
	if (timeout == 0) {
		bsev_ptr = tegra_vde_readl(vde, vde->bsev, 0x10);
		macroblocks_nb = tegra_vde_readl(vde, vde->sxe, 0xC8) & 0x1FFF;
		read_bytes = bsev_ptr ? bsev_ptr - vde->bitstream_data_addr : 0;

		dev_err(dev, "Decoding failed: read 0x%X bytes, %u macroblocks parsed\n",
			read_bytes, macroblocks_nb);

		ret = -EIO;
	} else if (timeout < 0) {
		ret = timeout;
	} else {
		ret = 0;
	}

	tegra_vde_decode_abort(vde);

	return ret;
}

static struct vb2_buffer *get_ref_buf(struct tegra_ctx *ctx,
				      struct vb2_v4l2_buffer *dst,
				      unsigned int dpb_idx)
{
	const struct v4l2_h264_dpb_entry *dpb = ctx->h264.decode_params->dpb;
	struct vb2_queue *cap_q = &ctx->fh.m2m_ctx->cap_q_ctx.q;
	struct vb2_buffer *vb = NULL;

	if (dpb[dpb_idx].flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE)
		vb = vb2_find_buffer(cap_q, dpb[dpb_idx].reference_ts);

	/*
	 * If a DPB entry is unused or invalid, address of current destination
	 * buffer is returned.
	 */
	if (!vb)
		return &dst->vb2_buf;

	return vb;
}

static int tegra_vde_validate_vb_size(struct tegra_ctx *ctx,
				      struct vb2_buffer *vb,
				      unsigned int plane_id,
				      size_t min_size)
{
	u64 offset = vb->planes[plane_id].data_offset;
	struct device *dev = ctx->vde->dev;

	if (offset + min_size > vb2_plane_size(vb, plane_id)) {
		dev_err(dev, "Too small plane[%u] size %lu @0x%llX, should be at least %zu\n",
			plane_id, vb2_plane_size(vb, plane_id), offset, min_size);
		return -EINVAL;
	}

	return 0;
}

static int tegra_vde_h264_setup_frame(struct tegra_ctx *ctx,
				      struct tegra_vde_h264_decoder_ctx *h264,
				      struct v4l2_h264_reflist_builder *b,
				      struct vb2_buffer *vb,
				      unsigned int ref_id,
				      unsigned int id)
{
	struct v4l2_pix_format_mplane *pixfmt = &ctx->decoded_fmt.fmt.pix_mp;
	struct tegra_m2m_buffer *tb = vb_to_tegra_buf(vb);
	struct tegra_ctx_h264 *h = &ctx->h264;
	struct tegra_vde *vde = ctx->vde;
	struct device *dev = vde->dev;
	unsigned int cstride, lstride;
	unsigned int flags = 0;
	size_t lsize, csize;
	int err, frame_num;

	lsize = h264->pic_width_in_mbs * 16 * h264->pic_height_in_mbs * 16;
	csize = h264->pic_width_in_mbs *  8 * h264->pic_height_in_mbs *  8;
	lstride = pixfmt->plane_fmt[0].bytesperline;
	cstride = pixfmt->plane_fmt[1].bytesperline;

	err = tegra_vde_validate_vb_size(ctx, vb, 0, lsize);
	if (err)
		return err;

	err = tegra_vde_validate_vb_size(ctx, vb, 1, csize);
	if (err)
		return err;

	err = tegra_vde_validate_vb_size(ctx, vb, 2, csize);
	if (err)
		return err;

	if (!tb->aux || tb->aux->size < csize) {
		dev_err(dev, "Too small aux size %zd, should be at least %zu\n",
			tb->aux ? tb->aux->size : -1, csize);
		return -EINVAL;
	}

	if (id == 0) {
		frame_num = h->decode_params->frame_num;

		if (h->decode_params->nal_ref_idc)
			flags |= FLAG_REFERENCE;
	} else {
		frame_num = b->refs[ref_id].frame_num;
	}

	if (tb->b_frame)
		flags |= FLAG_B_FRAME;

	vde->frames[id].flags = flags;
	vde->frames[id].y_addr = tb->dma_addr[0];
	vde->frames[id].cb_addr = tb->dma_addr[1];
	vde->frames[id].cr_addr = tb->dma_addr[2];
	vde->frames[id].aux_addr = tb->aux->dma_addr;
	vde->frames[id].frame_num = frame_num & 0x7fffff;
	vde->frames[id].luma_atoms_pitch = lstride / VDE_ATOM;
	vde->frames[id].chroma_atoms_pitch = cstride / VDE_ATOM;

	return 0;
}

static int tegra_vde_h264_setup_frames(struct tegra_ctx *ctx,
				       struct tegra_vde_h264_decoder_ctx *h264)
{
	struct vb2_v4l2_buffer *src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	struct vb2_v4l2_buffer *dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	const struct v4l2_h264_dpb_entry *dpb = ctx->h264.decode_params->dpb;
	struct tegra_m2m_buffer *tb = vb_to_tegra_buf(&dst->vb2_buf);
	struct tegra_ctx_h264 *h = &ctx->h264;
	struct v4l2_h264_reflist_builder b;
	struct v4l2_h264_reference *dpb_id;
	struct h264_reflists reflists;
	struct vb2_buffer *ref;
	unsigned int i;
	int err;

	/*
	 * Tegra hardware requires information about frame's type, assuming
	 * that frame consists of the same type slices. Userspace must tag
	 * frame's type appropriately.
	 *
	 * Decoding of a non-uniform frames isn't supported by hardware and
	 * require software preprocessing that we don't implement. Decoding
	 * is expected to fail in this case. Such video streams are rare in
	 * practice, so not a big deal.
	 *
	 * If userspace doesn't tell us frame's type, then we will try decode
	 * as-is.
	 */
	v4l2_m2m_buf_copy_metadata(src, dst, true);

	if (h->decode_params->flags & V4L2_H264_DECODE_PARAM_FLAG_BFRAME)
		tb->b_frame = true;
	else
		tb->b_frame = false;

	err = tegra_vde_h264_setup_frame(ctx, h264, NULL, &dst->vb2_buf, 0,
					 h264->dpb_frames_nb++);
	if (err)
		return err;

	if (!(h->decode_params->flags & (V4L2_H264_DECODE_PARAM_FLAG_PFRAME |
					 V4L2_H264_DECODE_PARAM_FLAG_BFRAME)))
		return 0;

	v4l2_h264_init_reflist_builder(&b, h->decode_params, h->sps, dpb);

	if (h->decode_params->flags & V4L2_H264_DECODE_PARAM_FLAG_BFRAME) {
		v4l2_h264_build_b_ref_lists(&b, reflists.b0, reflists.b1);
		dpb_id = reflists.b0;
	} else {
		v4l2_h264_build_p_ref_list(&b, reflists.p);
		dpb_id = reflists.p;
	}

	for (i = 0; i < b.num_valid; i++) {
		int dpb_idx = dpb_id[i].index;

		ref = get_ref_buf(ctx, dst, dpb_idx);

		err = tegra_vde_h264_setup_frame(ctx, h264, &b, ref, dpb_idx,
						 h264->dpb_frames_nb++);
		if (err)
			return err;

		if (b.refs[dpb_idx].top_field_order_cnt < b.cur_pic_order_count)
			h264->dpb_ref_frames_with_earlier_poc_nb++;
	}

	return 0;
}

static unsigned int to_tegra_vde_h264_level_idc(unsigned int level_idc)
{
	switch (level_idc) {
	case 11:
		return 2;
	case 12:
		return 3;
	case 13:
		return 4;
	case 20:
		return 5;
	case 21:
		return 6;
	case 22:
		return 7;
	case 30:
		return 8;
	case 31:
		return 9;
	case 32:
		return 10;
	case 40:
		return 11;
	case 41:
		return 12;
	case 42:
		return 13;
	case 50:
		return 14;
	default:
		break;
	}

	return 15;
}

static int tegra_vde_h264_setup_context(struct tegra_ctx *ctx,
					struct tegra_vde_h264_decoder_ctx *h264)
{
	struct tegra_ctx_h264 *h = &ctx->h264;
	struct tegra_vde *vde = ctx->vde;
	struct device *dev = vde->dev;
	int err;

	memset(h264, 0, sizeof(*h264));
	memset(vde->frames, 0, sizeof(vde->frames));

	tegra_vde_prepare_control_data(ctx, V4L2_CID_STATELESS_H264_DECODE_PARAMS);
	tegra_vde_prepare_control_data(ctx, V4L2_CID_STATELESS_H264_SPS);
	tegra_vde_prepare_control_data(ctx, V4L2_CID_STATELESS_H264_PPS);

	/* CABAC unsupported by hardware, requires software preprocessing */
	if (h->pps->flags & V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE)
		return -EOPNOTSUPP;

	if (h->decode_params->flags & V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC)
		return -EOPNOTSUPP;

	if (h->sps->profile_idc == 66)
		h264->baseline_profile = 1;

	if (h->sps->flags & V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE)
		h264->direct_8x8_inference_flag = 1;

	if (h->pps->flags & V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED)
		h264->constrained_intra_pred_flag = 1;

	if (h->pps->flags & V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT)
		h264->deblocking_filter_control_present_flag = 1;

	if (h->pps->flags & V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT)
		h264->pic_order_present_flag = 1;

	h264->level_idc				= to_tegra_vde_h264_level_idc(h->sps->level_idc);
	h264->log2_max_pic_order_cnt_lsb	= h->sps->log2_max_pic_order_cnt_lsb_minus4 + 4;
	h264->log2_max_frame_num		= h->sps->log2_max_frame_num_minus4 + 4;
	h264->pic_order_cnt_type		= h->sps->pic_order_cnt_type;
	h264->pic_width_in_mbs			= h->sps->pic_width_in_mbs_minus1 + 1;
	h264->pic_height_in_mbs			= h->sps->pic_height_in_map_units_minus1 + 1;

	h264->num_ref_idx_l0_active_minus1	= h->pps->num_ref_idx_l0_default_active_minus1;
	h264->num_ref_idx_l1_active_minus1	= h->pps->num_ref_idx_l1_default_active_minus1;
	h264->chroma_qp_index_offset		= h->pps->chroma_qp_index_offset & 0x1f;
	h264->pic_init_qp			= h->pps->pic_init_qp_minus26 + 26;

	err = tegra_vde_h264_setup_frames(ctx, h264);
	if (err)
		return err;

	err = tegra_vde_validate_h264_ctx(dev, h264);
	if (err)
		return err;

	return 0;
}

int tegra_vde_h264_decode_run(struct tegra_ctx *ctx)
{
	struct vb2_v4l2_buffer *src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	struct tegra_m2m_buffer *bitstream = vb_to_tegra_buf(&src->vb2_buf);
	size_t bitstream_size = vb2_get_plane_payload(&src->vb2_buf, 0);
	struct tegra_vde_h264_decoder_ctx h264;
	struct tegra_vde *vde = ctx->vde;
	int err;

	err = tegra_vde_h264_setup_context(ctx, &h264);
	if (err)
		return err;

	err = tegra_vde_decode_begin(vde, &h264, vde->frames,
				     bitstream->dma_addr[0],
				     bitstream_size);
	if (err)
		return err;

	return 0;
}

int tegra_vde_h264_decode_wait(struct tegra_ctx *ctx)
{
	return tegra_vde_decode_end(ctx->vde);
}
