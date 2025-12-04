// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VPU codec driver
 *
 * Copyright (C) 2021 Collabora Ltd, Andrzej Pietrasiewicz <andrzej.p@collabora.com>
 */

#include <linux/delay.h>
#include "hantro_hw.h"
#include "hantro_g2_regs.h"

#define G2_ALIGN	16

static bool hantro_g2_active(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	u32 status;

	status = vdpu_read(vpu, G2_REG_INTERRUPT);

	return (status & G2_REG_INTERRUPT_DEC_E);
}

/**
 * hantro_g2_reset:
 * @ctx: the hantro context
 *
 * Emulates a reset using Hantro abort function. Failing this procedure would
 * results in programming a running IP which leads to CPU hang.
 *
 * Using a hard reset procedure instead is prefferred.
 */
void hantro_g2_reset(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	u32 status;

	status = vdpu_read(vpu, G2_REG_INTERRUPT);
	if (status & G2_REG_INTERRUPT_DEC_E) {
		dev_warn_ratelimited(vpu->dev, "device still running, aborting");
		status |= G2_REG_INTERRUPT_DEC_ABORT_E | G2_REG_INTERRUPT_DEC_IRQ_DIS;
		vdpu_write(vpu, status, G2_REG_INTERRUPT);

		do {
			mdelay(1);
		} while (hantro_g2_active(ctx));
	}
}

irqreturn_t hantro_g2_irq(int irq, void *dev_id)
{
	struct hantro_dev *vpu = dev_id;
	u32 status;

	status = vdpu_read(vpu, G2_REG_INTERRUPT);

	if (!(status & G2_REG_INTERRUPT_DEC_IRQ))
		return IRQ_NONE;

	hantro_reg_write(vpu, &g2_dec_irq, 0);
	hantro_reg_write(vpu, &g2_dec_int_stat, 0);
	hantro_reg_write(vpu, &g2_clk_gate_e, 1);

	if (status & G2_REG_INTERRUPT_DEC_RDY_INT) {
		hantro_irq_done(vpu, VB2_BUF_STATE_DONE);
		return IRQ_HANDLED;
	}

	if (status & G2_REG_INTERRUPT_DEC_ABORT_INT) {
		/* disabled on abort, though lets be safe and handle it */
		dev_warn_ratelimited(vpu->dev, "decode operation aborted.");
		return IRQ_HANDLED;
	}

	if (status & G2_REG_INTERRUPT_DEC_LAST_SLICE_INT)
		dev_warn_ratelimited(vpu->dev, "not all macroblocks were decoded.");

	if (status & G2_REG_INTERRUPT_DEC_BUS_INT)
		dev_warn_ratelimited(vpu->dev, "bus error detected.");

	if (status & G2_REG_INTERRUPT_DEC_ERROR_INT)
		dev_warn_ratelimited(vpu->dev, "decode error detected.");

	if (status & G2_REG_INTERRUPT_DEC_TIMEOUT)
		dev_warn_ratelimited(vpu->dev, "frame decode timed out.");

	/**
	 * If the decoding haven't stopped, let it continue. The hardware timeout
	 * will trigger if it is trully stuck.
	 */
	if (status & G2_REG_INTERRUPT_DEC_E)
		return IRQ_HANDLED;

	hantro_irq_done(vpu, VB2_BUF_STATE_ERROR);
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
