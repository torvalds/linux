// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VPU codec driver
 *
 * Copyright (C) 2021 Collabora Ltd, Andrzej Pietrasiewicz <andrzej.p@collabora.com>
 */

#include "hantro_hw.h"
#include "hantro_g2_regs.h"

#define G2_ALIGN	16

void hantro_g2_check_idle(struct hantro_dev *vpu)
{
	int i;

	for (i = 0; i < 3; i++) {
		u32 status;

		/* Make sure the VPU is idle */
		status = vdpu_read(vpu, G2_REG_INTERRUPT);
		if (status & G2_REG_INTERRUPT_DEC_E) {
			dev_warn(vpu->dev, "device still running, aborting");
			status |= G2_REG_INTERRUPT_DEC_ABORT_E | G2_REG_INTERRUPT_DEC_IRQ_DIS;
			vdpu_write(vpu, status, G2_REG_INTERRUPT);
		}
	}
}

irqreturn_t hantro_g2_irq(int irq, void *dev_id)
{
	struct hantro_dev *vpu = dev_id;
	enum vb2_buffer_state state;
	u32 status;

	status = vdpu_read(vpu, G2_REG_INTERRUPT);
	state = (status & G2_REG_INTERRUPT_DEC_RDY_INT) ?
		 VB2_BUF_STATE_DONE : VB2_BUF_STATE_ERROR;

	vdpu_write(vpu, 0, G2_REG_INTERRUPT);
	vdpu_write(vpu, G2_REG_CONFIG_DEC_CLK_GATE_E, G2_REG_CONFIG);

	hantro_irq_done(vpu, state);

	return IRQ_HANDLED;
}

size_t hantro_g2_chroma_offset(struct hantro_ctx *ctx)
{
	return ctx->ref_fmt.plane_fmt[0].bytesperline *	ctx->ref_fmt.height;
}

size_t hantro_g2_motion_vectors_offset(struct hantro_ctx *ctx)
{
	size_t cr_offset = hantro_g2_chroma_offset(ctx);

	return ALIGN((cr_offset * 3) / 2, G2_ALIGN);
}

static size_t hantro_g2_mv_size(struct hantro_ctx *ctx)
{
	const struct hantro_hevc_dec_ctrls *ctrls = &ctx->hevc_dec.ctrls;
	const struct v4l2_ctrl_hevc_sps *sps = ctrls->sps;
	unsigned int pic_width_in_ctbs, pic_height_in_ctbs;
	unsigned int max_log2_ctb_size;

	max_log2_ctb_size = sps->log2_min_luma_coding_block_size_minus3 + 3 +
			    sps->log2_diff_max_min_luma_coding_block_size;
	pic_width_in_ctbs = (sps->pic_width_in_luma_samples +
			    (1 << max_log2_ctb_size) - 1) >> max_log2_ctb_size;
	pic_height_in_ctbs = (sps->pic_height_in_luma_samples + (1 << max_log2_ctb_size) - 1)
			     >> max_log2_ctb_size;

	return pic_width_in_ctbs * pic_height_in_ctbs * (1 << (2 * (max_log2_ctb_size - 4))) * 16;
}

size_t hantro_g2_luma_compress_offset(struct hantro_ctx *ctx)
{
	return hantro_g2_motion_vectors_offset(ctx) +
	       hantro_g2_mv_size(ctx);
}

size_t hantro_g2_chroma_compress_offset(struct hantro_ctx *ctx)
{
	return hantro_g2_luma_compress_offset(ctx) +
	       hantro_hevc_luma_compressed_size(ctx->dst_fmt.width, ctx->dst_fmt.height);
}
