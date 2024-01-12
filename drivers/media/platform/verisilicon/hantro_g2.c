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
	return ctx->dst_fmt.width * ctx->dst_fmt.height * ctx->bit_depth / 8;
}

size_t hantro_g2_motion_vectors_offset(struct hantro_ctx *ctx)
{
	size_t cr_offset = hantro_g2_chroma_offset(ctx);

	return ALIGN((cr_offset * 3) / 2, G2_ALIGN);
}
