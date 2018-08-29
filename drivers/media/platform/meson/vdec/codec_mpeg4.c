// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <maxi.jourdan@wanadoo.fr>
 */

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "vdec_helpers.h"
#include "dos_regs.h"

#define SIZE_WORKSPACE		SZ_1M
/* Offset added by firmware, to substract from workspace paddr */
#define DCAC_BUFF_START_IP	0x02b00000

/* map firmware registers to known MPEG4 functions */
#define MREG_BUFFERIN		AV_SCRATCH_8
#define MREG_BUFFEROUT		AV_SCRATCH_9
#define MP4_NOT_CODED_CNT	AV_SCRATCH_A
#define MP4_OFFSET_REG		AV_SCRATCH_C
#define MEM_OFFSET_REG		AV_SCRATCH_F
#define MREG_FATAL_ERROR	AV_SCRATCH_L

#define BUF_IDX_MASK		GENMASK(2, 0)
#define INTERLACE_FLAG		BIT(7)
#define TOP_FIELD_FIRST_FLAG	BIT(6)

struct codec_mpeg4 {
	/* Buffer for the MPEG4 Workspace */
	void      *workspace_vaddr;
	dma_addr_t workspace_paddr;
};

static int codec_mpeg4_can_recycle(struct amvdec_core *core)
{
	return !amvdec_read_dos(core, MREG_BUFFERIN);
}

static void codec_mpeg4_recycle(struct amvdec_core *core, u32 buf_idx)
{
	amvdec_write_dos(core, MREG_BUFFERIN, ~BIT(buf_idx));
}

static int codec_mpeg4_start(struct amvdec_session *sess) {
	struct amvdec_core *core = sess->core;
	struct codec_mpeg4 *mpeg4 = sess->priv;
	int ret;

	mpeg4 = kzalloc(sizeof(*mpeg4), GFP_KERNEL);
	if (!mpeg4)
		return -ENOMEM;

	/* Allocate some memory for the MPEG4 decoder's state */
	mpeg4->workspace_vaddr = dma_alloc_coherent(core->dev, SIZE_WORKSPACE,
						    &mpeg4->workspace_paddr,
						    GFP_KERNEL);
	if (!mpeg4->workspace_vaddr) {
		dev_err(core->dev, "Failed to request MPEG4 Workspace\n");
		ret = -ENOMEM;
		goto free_mpeg4;
	}

	/* Canvas regs: AV_SCRATCH_0-AV_SCRATCH_4;AV_SCRATCH_G-AV_SCRATCH_J */
	amvdec_set_canvases(sess, (u32[]){ AV_SCRATCH_0, AV_SCRATCH_G, 0 },
				  (u32[]){ 4, 4, 0 });

	amvdec_write_dos(core, MEM_OFFSET_REG,
			 mpeg4->workspace_paddr - DCAC_BUFF_START_IP);
	amvdec_write_dos(core, PSCALE_CTRL, 0);
	amvdec_write_dos(core, MP4_NOT_CODED_CNT, 0);
	amvdec_write_dos(core, MREG_BUFFERIN, 0);
	amvdec_write_dos(core, MREG_BUFFEROUT, 0);
	amvdec_write_dos(core, MREG_FATAL_ERROR, 0);
	amvdec_write_dos(core, MDEC_PIC_DC_THRESH, 0x404038aa);

	sess->keyframe_found = 1;
	sess->priv = mpeg4;

	return 0;

free_mpeg4:
	kfree(mpeg4);
	return ret;
}

static int codec_mpeg4_stop(struct amvdec_session *sess)
{
	struct codec_mpeg4 *mpeg4 = sess->priv;
	struct amvdec_core *core = sess->core;

	if (mpeg4->workspace_vaddr) {
		dma_free_coherent(core->dev, SIZE_WORKSPACE,
				  mpeg4->workspace_vaddr,
				  mpeg4->workspace_paddr);
		mpeg4->workspace_vaddr = 0;
	}

	return 0;
}

static irqreturn_t codec_mpeg4_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 reg;
	u32 buffer_index;
	u32 field = V4L2_FIELD_NONE;

	reg = amvdec_read_dos(core, MREG_FATAL_ERROR);
	if (reg == 1) {
		dev_err(core->dev, "mpeg4 fatal error\n");
		amvdec_abort(sess);
		return IRQ_HANDLED;
	}

	reg = amvdec_read_dos(core, MREG_BUFFEROUT);
	if (!reg)
		goto end;

	buffer_index = reg & BUF_IDX_MASK;
	if (reg & INTERLACE_FLAG)
		field = (reg & TOP_FIELD_FIRST_FLAG) ?
			V4L2_FIELD_INTERLACED_TB :
			V4L2_FIELD_INTERLACED_BT;

	amvdec_dst_buf_done_idx(sess, buffer_index, -1, field);
	amvdec_write_dos(core, MREG_BUFFEROUT, 0);

end:
	amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);
	return IRQ_HANDLED;
}

struct amvdec_codec_ops codec_mpeg4_ops = {
	.start = codec_mpeg4_start,
	.stop = codec_mpeg4_stop,
	.isr = codec_mpeg4_isr,
	.can_recycle = codec_mpeg4_can_recycle,
	.recycle = codec_mpeg4_recycle,
};
