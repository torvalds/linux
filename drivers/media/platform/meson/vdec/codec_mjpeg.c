// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <maxi.jourdan@wanadoo.fr>
 */

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "vdec_helpers.h"
#include "dos_regs.h"

/* map FW registers to known MJPEG functions */
#define MREG_DECODE_PARAM	AV_SCRATCH_2
#define MREG_TO_AMRISC		AV_SCRATCH_8
#define MREG_FROM_AMRISC	AV_SCRATCH_9
#define MREG_FRAME_OFFSET	AV_SCRATCH_A

static int codec_mjpeg_can_recycle(struct amvdec_core *core)
{
	return !amvdec_read_dos(core, MREG_TO_AMRISC);
}

static void codec_mjpeg_recycle(struct amvdec_core *core, u32 buf_idx)
{
	amvdec_write_dos(core, MREG_TO_AMRISC, buf_idx + 1);
}

/* 4 point triangle */
static const uint32_t filt_coef[] = {
	0x20402000, 0x20402000, 0x1f3f2101, 0x1f3f2101,
	0x1e3e2202, 0x1e3e2202, 0x1d3d2303, 0x1d3d2303,
	0x1c3c2404, 0x1c3c2404, 0x1b3b2505, 0x1b3b2505,
	0x1a3a2606, 0x1a3a2606, 0x19392707, 0x19392707,
	0x18382808, 0x18382808, 0x17372909, 0x17372909,
	0x16362a0a, 0x16362a0a, 0x15352b0b, 0x15352b0b,
	0x14342c0c, 0x14342c0c, 0x13332d0d, 0x13332d0d,
	0x12322e0e, 0x12322e0e, 0x11312f0f, 0x11312f0f,
	0x10303010
};

static void codec_mjpeg_init_scaler(struct amvdec_core *core)
{
	int i;

	/* PSCALE cbus bmem enable */
	amvdec_write_dos(core, PSCALE_CTRL, 0xc000);

	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 0);
	for (i = 0; i < ARRAY_SIZE(filt_coef); ++i) {
		amvdec_write_dos(core, PSCALE_BMEM_DAT, 0);
		amvdec_write_dos(core, PSCALE_BMEM_DAT, filt_coef[i]);
	}

	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 74);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x0008);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x60000000);

	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 82);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x0008);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x60000000);

	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 78);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x0008);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x60000000);

	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 86);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x0008);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x60000000);

	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 73);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x10000);
	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 81);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x10000);

	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 77);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x10000);
	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 85);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x10000);

	amvdec_write_dos(core, PSCALE_RST, 0x7);
	amvdec_write_dos(core, PSCALE_RST, 0);
}

static int codec_mjpeg_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	amvdec_write_dos(core, AV_SCRATCH_0, 12);
	amvdec_write_dos(core, AV_SCRATCH_1, 0x031a);

	amvdec_set_canvases(sess, (u32[]){ AV_SCRATCH_4, 0 },
				    (u32[]){ 4, 0 });
	codec_mjpeg_init_scaler(core);

	amvdec_write_dos(core, MREG_TO_AMRISC, 0);
	amvdec_write_dos(core, MREG_FROM_AMRISC, 0);
	amvdec_write_dos(core, MCPU_INTR_MSK, 0xffff);
	amvdec_write_dos(core, MREG_DECODE_PARAM,
			 (sess->height << 4) | 0x8000);
	amvdec_write_dos(core, VDEC_ASSIST_AMR1_INT8, 8);

	/* Intra-only codec */
	sess->keyframe_found = 1;

	return 0;
}

static int codec_mjpeg_stop(struct amvdec_session *sess)
{
	return 0;
}

static irqreturn_t codec_mjpeg_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 reg;
	u32 buffer_index;
	u32 offset;

	amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);

	reg = amvdec_read_dos(core, MREG_FROM_AMRISC);
	if (!(reg & 0x7))
		return IRQ_HANDLED;

	buffer_index = ((reg & 0x7) - 1) & 3;
	offset = amvdec_read_dos(core, MREG_FRAME_OFFSET);
	amvdec_dst_buf_done_idx(sess, buffer_index, offset, V4L2_FIELD_NONE);

	amvdec_write_dos(core, MREG_FROM_AMRISC, 0);
	return IRQ_HANDLED;
}

struct amvdec_codec_ops codec_mjpeg_ops = {
	.start = codec_mjpeg_start,
	.stop = codec_mjpeg_stop,
	.isr = codec_mjpeg_isr,
	.can_recycle = codec_mjpeg_can_recycle,
	.recycle = codec_mjpeg_recycle,
};
