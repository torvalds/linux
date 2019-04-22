/*
 * drivers/media/platform/s5p-mfc/s5p_mfc_opr_v6.c
 *
 * Samsung MFC (Multi Function Codec - FIMV) driver
 * This file contains hw related functions.
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#undef DEBUG

#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/firmware.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>

#include <asm/cacheflush.h>

#include "s5p_mfc_common.h"
#include "s5p_mfc_cmd.h"
#include "s5p_mfc_intr.h"
#include "s5p_mfc_pm.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_opr.h"
#include "s5p_mfc_opr_v6.h"

/* #define S5P_MFC_DEBUG_REGWRITE  */
#ifdef S5P_MFC_DEBUG_REGWRITE
#undef writel
#define writel(v, r)							\
	do {								\
		pr_err("MFCWRITE(%p): %08x\n", r, (unsigned int)v);	\
	__raw_writel(v, r);						\
	} while (0)
#endif /* S5P_MFC_DEBUG_REGWRITE */

#define IS_MFCV6_V2(dev) (!IS_MFCV7_PLUS(dev) && dev->fw_ver == MFC_FW_V2)

/* Allocate temporary buffers for decoding */
static int s5p_mfc_alloc_dec_temp_buffers_v6(struct s5p_mfc_ctx *ctx)
{
	/* NOP */

	return 0;
}

/* Release temporary buffers for decoding */
static void s5p_mfc_release_dec_desc_buffer_v6(struct s5p_mfc_ctx *ctx)
{
	/* NOP */
}

/* Allocate codec buffers */
static int s5p_mfc_alloc_codec_buffers_v6(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned int mb_width, mb_height;
	unsigned int lcu_width = 0, lcu_height = 0;
	int ret;

	mb_width = MB_WIDTH(ctx->img_width);
	mb_height = MB_HEIGHT(ctx->img_height);

	if (ctx->type == MFCINST_DECODER) {
		mfc_debug(2, "Luma size:%d Chroma size:%d MV size:%d\n",
			  ctx->luma_size, ctx->chroma_size, ctx->mv_size);
		mfc_debug(2, "Totals bufs: %d\n", ctx->total_dpb_count);
	} else if (ctx->type == MFCINST_ENCODER) {
		if (IS_MFCV10(dev)) {
			ctx->tmv_buffer_size = 0;
		} else if (IS_MFCV8_PLUS(dev))
			ctx->tmv_buffer_size = S5P_FIMV_NUM_TMV_BUFFERS_V6 *
			ALIGN(S5P_FIMV_TMV_BUFFER_SIZE_V8(mb_width, mb_height),
			S5P_FIMV_TMV_BUFFER_ALIGN_V6);
		else
			ctx->tmv_buffer_size = S5P_FIMV_NUM_TMV_BUFFERS_V6 *
			ALIGN(S5P_FIMV_TMV_BUFFER_SIZE_V6(mb_width, mb_height),
			S5P_FIMV_TMV_BUFFER_ALIGN_V6);
		if (IS_MFCV10(dev)) {
			lcu_width = S5P_MFC_LCU_WIDTH(ctx->img_width);
			lcu_height = S5P_MFC_LCU_HEIGHT(ctx->img_height);
			if (ctx->codec_mode != S5P_FIMV_CODEC_HEVC_ENC) {
				ctx->luma_dpb_size =
					ALIGN((mb_width * 16), 64)
					* ALIGN((mb_height * 16), 32)
						+ 64;
				ctx->chroma_dpb_size =
					ALIGN((mb_width * 16), 64)
							* (mb_height * 8)
							+ 64;
			} else {
				ctx->luma_dpb_size =
					ALIGN((lcu_width * 32), 64)
					* ALIGN((lcu_height * 32), 32)
						+ 64;
				ctx->chroma_dpb_size =
					ALIGN((lcu_width * 32), 64)
							* (lcu_height * 16)
							+ 64;
			}
		} else {
			ctx->luma_dpb_size = ALIGN((mb_width * mb_height) *
					S5P_FIMV_LUMA_MB_TO_PIXEL_V6,
					S5P_FIMV_LUMA_DPB_BUFFER_ALIGN_V6);
			ctx->chroma_dpb_size = ALIGN((mb_width * mb_height) *
					S5P_FIMV_CHROMA_MB_TO_PIXEL_V6,
					S5P_FIMV_CHROMA_DPB_BUFFER_ALIGN_V6);
		}
		if (IS_MFCV8_PLUS(dev))
			ctx->me_buffer_size = ALIGN(S5P_FIMV_ME_BUFFER_SIZE_V8(
						ctx->img_width, ctx->img_height,
						mb_width, mb_height),
						S5P_FIMV_ME_BUFFER_ALIGN_V6);
		else
			ctx->me_buffer_size = ALIGN(S5P_FIMV_ME_BUFFER_SIZE_V6(
						ctx->img_width, ctx->img_height,
						mb_width, mb_height),
						S5P_FIMV_ME_BUFFER_ALIGN_V6);

		mfc_debug(2, "recon luma size: %zu chroma size: %zu\n",
			  ctx->luma_dpb_size, ctx->chroma_dpb_size);
	} else {
		return -EINVAL;
	}

	/* Codecs have different memory requirements */
	switch (ctx->codec_mode) {
	case S5P_MFC_CODEC_H264_DEC:
	case S5P_MFC_CODEC_H264_MVC_DEC:
		if (IS_MFCV10(dev))
			mfc_debug(2, "Use min scratch buffer size\n");
		else if (IS_MFCV8_PLUS(dev))
			ctx->scratch_buf_size =
				S5P_FIMV_SCRATCH_BUF_SIZE_H264_DEC_V8(
					mb_width,
					mb_height);
		else
			ctx->scratch_buf_size =
				S5P_FIMV_SCRATCH_BUF_SIZE_H264_DEC_V6(
					mb_width,
					mb_height);
		ctx->scratch_buf_size = ALIGN(ctx->scratch_buf_size,
				S5P_FIMV_SCRATCH_BUFFER_ALIGN_V6);
		ctx->bank1.size =
			ctx->scratch_buf_size +
			(ctx->mv_count * ctx->mv_size);
		break;
	case S5P_MFC_CODEC_MPEG4_DEC:
		if (IS_MFCV10(dev))
			mfc_debug(2, "Use min scratch buffer size\n");
		else if (IS_MFCV7_PLUS(dev)) {
			ctx->scratch_buf_size =
				S5P_FIMV_SCRATCH_BUF_SIZE_MPEG4_DEC_V7(
						mb_width,
						mb_height);
		} else {
			ctx->scratch_buf_size =
				S5P_FIMV_SCRATCH_BUF_SIZE_MPEG4_DEC_V6(
						mb_width,
						mb_height);
		}

		ctx->scratch_buf_size = ALIGN(ctx->scratch_buf_size,
				S5P_FIMV_SCRATCH_BUFFER_ALIGN_V6);
		ctx->bank1.size = ctx->scratch_buf_size;
		break;
	case S5P_MFC_CODEC_VC1RCV_DEC:
	case S5P_MFC_CODEC_VC1_DEC:
		if (IS_MFCV10(dev))
			mfc_debug(2, "Use min scratch buffer size\n");
		else
			ctx->scratch_buf_size =
				S5P_FIMV_SCRATCH_BUF_SIZE_VC1_DEC_V6(
						mb_width,
						mb_height);

		ctx->scratch_buf_size = ALIGN(ctx->scratch_buf_size,
				S5P_FIMV_SCRATCH_BUFFER_ALIGN_V6);
		ctx->bank1.size = ctx->scratch_buf_size;
		break;
	case S5P_MFC_CODEC_MPEG2_DEC:
		ctx->bank1.size = 0;
		ctx->bank2.size = 0;
		break;
	case S5P_MFC_CODEC_H263_DEC:
		if (IS_MFCV10(dev))
			mfc_debug(2, "Use min scratch buffer size\n");
		else
			ctx->scratch_buf_size =
				S5P_FIMV_SCRATCH_BUF_SIZE_H263_DEC_V6(
						mb_width,
						mb_height);
		ctx->scratch_buf_size = ALIGN(ctx->scratch_buf_size,
				S5P_FIMV_SCRATCH_BUFFER_ALIGN_V6);
		ctx->bank1.size = ctx->scratch_buf_size;
		break;
	case S5P_MFC_CODEC_VP8_DEC:
		if (IS_MFCV10(dev))
			mfc_debug(2, "Use min scratch buffer size\n");
		else if (IS_MFCV8_PLUS(dev))
			ctx->scratch_buf_size =
				S5P_FIMV_SCRATCH_BUF_SIZE_VP8_DEC_V8(
						mb_width,
						mb_height);
		else
			ctx->scratch_buf_size =
				S5P_FIMV_SCRATCH_BUF_SIZE_VP8_DEC_V6(
						mb_width,
						mb_height);
		ctx->scratch_buf_size = ALIGN(ctx->scratch_buf_size,
				S5P_FIMV_SCRATCH_BUFFER_ALIGN_V6);
		ctx->bank1.size = ctx->scratch_buf_size;
		break;
	case S5P_MFC_CODEC_HEVC_DEC:
		mfc_debug(2, "Use min scratch buffer size\n");
		ctx->bank1.size =
			ctx->scratch_buf_size +
			(ctx->mv_count * ctx->mv_size);
		break;
	case S5P_MFC_CODEC_VP9_DEC:
		mfc_debug(2, "Use min scratch buffer size\n");
		ctx->bank1.size =
			ctx->scratch_buf_size +
			DEC_VP9_STATIC_BUFFER_SIZE;
		break;
	case S5P_MFC_CODEC_H264_ENC:
		if (IS_MFCV10(dev)) {
			mfc_debug(2, "Use min scratch buffer size\n");
			ctx->me_buffer_size =
			ALIGN(ENC_V100_H264_ME_SIZE(mb_width, mb_height), 16);
		} else if (IS_MFCV8_PLUS(dev))
			ctx->scratch_buf_size =
				S5P_FIMV_SCRATCH_BUF_SIZE_H264_ENC_V8(
					mb_width,
					mb_height);
		else
			ctx->scratch_buf_size =
				S5P_FIMV_SCRATCH_BUF_SIZE_H264_ENC_V6(
						mb_width,
						mb_height);
		ctx->scratch_buf_size = ALIGN(ctx->scratch_buf_size,
				S5P_FIMV_SCRATCH_BUFFER_ALIGN_V6);
		ctx->bank1.size =
			ctx->scratch_buf_size + ctx->tmv_buffer_size +
			(ctx->pb_count * (ctx->luma_dpb_size +
			ctx->chroma_dpb_size + ctx->me_buffer_size));
		ctx->bank2.size = 0;
		break;
	case S5P_MFC_CODEC_MPEG4_ENC:
	case S5P_MFC_CODEC_H263_ENC:
		if (IS_MFCV10(dev)) {
			mfc_debug(2, "Use min scratch buffer size\n");
			ctx->me_buffer_size =
				ALIGN(ENC_V100_MPEG4_ME_SIZE(mb_width,
							mb_height), 16);
		} else
			ctx->scratch_buf_size =
				S5P_FIMV_SCRATCH_BUF_SIZE_MPEG4_ENC_V6(
						mb_width,
						mb_height);
		ctx->scratch_buf_size = ALIGN(ctx->scratch_buf_size,
				S5P_FIMV_SCRATCH_BUFFER_ALIGN_V6);
		ctx->bank1.size =
			ctx->scratch_buf_size + ctx->tmv_buffer_size +
			(ctx->pb_count * (ctx->luma_dpb_size +
			ctx->chroma_dpb_size + ctx->me_buffer_size));
		ctx->bank2.size = 0;
		break;
	case S5P_MFC_CODEC_VP8_ENC:
		if (IS_MFCV10(dev)) {
			mfc_debug(2, "Use min scratch buffer size\n");
			ctx->me_buffer_size =
				ALIGN(ENC_V100_VP8_ME_SIZE(mb_width, mb_height),
						16);
		} else if (IS_MFCV8_PLUS(dev))
			ctx->scratch_buf_size =
				S5P_FIMV_SCRATCH_BUF_SIZE_VP8_ENC_V8(
					mb_width,
					mb_height);
		else
			ctx->scratch_buf_size =
				S5P_FIMV_SCRATCH_BUF_SIZE_VP8_ENC_V7(
						mb_width,
						mb_height);
		ctx->scratch_buf_size = ALIGN(ctx->scratch_buf_size,
				S5P_FIMV_SCRATCH_BUFFER_ALIGN_V6);
		ctx->bank1.size =
			ctx->scratch_buf_size + ctx->tmv_buffer_size +
			(ctx->pb_count * (ctx->luma_dpb_size +
			ctx->chroma_dpb_size + ctx->me_buffer_size));
		ctx->bank2.size = 0;
		break;
	case S5P_MFC_CODEC_HEVC_ENC:
		mfc_debug(2, "Use min scratch buffer size\n");
		ctx->me_buffer_size =
			ALIGN(ENC_V100_HEVC_ME_SIZE(lcu_width, lcu_height), 16);
		ctx->scratch_buf_size = ALIGN(ctx->scratch_buf_size, 256);
		ctx->bank1.size =
			ctx->scratch_buf_size + ctx->tmv_buffer_size +
			(ctx->pb_count * (ctx->luma_dpb_size +
			ctx->chroma_dpb_size + ctx->me_buffer_size));
		ctx->bank2.size = 0;
		break;
	default:
		break;
	}

	/* Allocate only if memory from bank 1 is necessary */
	if (ctx->bank1.size > 0) {
		ret = s5p_mfc_alloc_generic_buf(dev, BANK_L_CTX, &ctx->bank1);
		if (ret) {
			mfc_err("Failed to allocate Bank1 memory\n");
			return ret;
		}
		BUG_ON(ctx->bank1.dma & ((1 << MFC_BANK1_ALIGN_ORDER) - 1));
	}
	return 0;
}

/* Release buffers allocated for codec */
static void s5p_mfc_release_codec_buffers_v6(struct s5p_mfc_ctx *ctx)
{
	s5p_mfc_release_generic_buf(ctx->dev, &ctx->bank1);
}

/* Allocate memory for instance data buffer */
static int s5p_mfc_alloc_instance_buffer_v6(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf_size_v6 *buf_size = dev->variant->buf_size->priv;
	int ret;

	mfc_debug_enter();

	switch (ctx->codec_mode) {
	case S5P_MFC_CODEC_H264_DEC:
	case S5P_MFC_CODEC_H264_MVC_DEC:
	case S5P_MFC_CODEC_HEVC_DEC:
		ctx->ctx.size = buf_size->h264_dec_ctx;
		break;
	case S5P_MFC_CODEC_MPEG4_DEC:
	case S5P_MFC_CODEC_H263_DEC:
	case S5P_MFC_CODEC_VC1RCV_DEC:
	case S5P_MFC_CODEC_VC1_DEC:
	case S5P_MFC_CODEC_MPEG2_DEC:
	case S5P_MFC_CODEC_VP8_DEC:
	case S5P_MFC_CODEC_VP9_DEC:
		ctx->ctx.size = buf_size->other_dec_ctx;
		break;
	case S5P_MFC_CODEC_H264_ENC:
		ctx->ctx.size = buf_size->h264_enc_ctx;
		break;
	case S5P_MFC_CODEC_HEVC_ENC:
		ctx->ctx.size = buf_size->hevc_enc_ctx;
		break;
	case S5P_MFC_CODEC_MPEG4_ENC:
	case S5P_MFC_CODEC_H263_ENC:
	case S5P_MFC_CODEC_VP8_ENC:
		ctx->ctx.size = buf_size->other_enc_ctx;
		break;
	default:
		ctx->ctx.size = 0;
		mfc_err("Codec type(%d) should be checked!\n", ctx->codec_mode);
		break;
	}

	ret = s5p_mfc_alloc_priv_buf(dev, BANK_L_CTX, &ctx->ctx);
	if (ret) {
		mfc_err("Failed to allocate instance buffer\n");
		return ret;
	}

	memset(ctx->ctx.virt, 0, ctx->ctx.size);
	wmb();

	mfc_debug_leave();

	return 0;
}

/* Release instance buffer */
static void s5p_mfc_release_instance_buffer_v6(struct s5p_mfc_ctx *ctx)
{
	s5p_mfc_release_priv_buf(ctx->dev, &ctx->ctx);
}

/* Allocate context buffers for SYS_INIT */
static int s5p_mfc_alloc_dev_context_buffer_v6(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_buf_size_v6 *buf_size = dev->variant->buf_size->priv;
	int ret;

	mfc_debug_enter();

	dev->ctx_buf.size = buf_size->dev_ctx;
	ret = s5p_mfc_alloc_priv_buf(dev, BANK_L_CTX, &dev->ctx_buf);
	if (ret) {
		mfc_err("Failed to allocate device context buffer\n");
		return ret;
	}

	memset(dev->ctx_buf.virt, 0, buf_size->dev_ctx);
	wmb();

	mfc_debug_leave();

	return 0;
}

/* Release context buffers for SYS_INIT */
static void s5p_mfc_release_dev_context_buffer_v6(struct s5p_mfc_dev *dev)
{
	s5p_mfc_release_priv_buf(dev, &dev->ctx_buf);
}

static int calc_plane(int width, int height)
{
	int mbX, mbY;

	mbX = DIV_ROUND_UP(width, S5P_FIMV_NUM_PIXELS_IN_MB_ROW_V6);
	mbY = DIV_ROUND_UP(height, S5P_FIMV_NUM_PIXELS_IN_MB_COL_V6);

	if (width * height < S5P_FIMV_MAX_FRAME_SIZE_V6)
		mbY = (mbY + 1) / 2 * 2;

	return (mbX * S5P_FIMV_NUM_PIXELS_IN_MB_COL_V6) *
		(mbY * S5P_FIMV_NUM_PIXELS_IN_MB_ROW_V6);
}

static void s5p_mfc_dec_calc_dpb_size_v6(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	ctx->buf_width = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN_V6);
	ctx->buf_height = ALIGN(ctx->img_height, S5P_FIMV_NV12MT_VALIGN_V6);
	mfc_debug(2, "SEQ Done: Movie dimensions %dx%d,\n"
			"buffer dimensions: %dx%d\n", ctx->img_width,
			ctx->img_height, ctx->buf_width, ctx->buf_height);

	ctx->luma_size = calc_plane(ctx->img_width, ctx->img_height);
	ctx->chroma_size = calc_plane(ctx->img_width, (ctx->img_height >> 1));
	if (IS_MFCV8_PLUS(ctx->dev)) {
		/* MFCv8 needs additional 64 bytes for luma,chroma dpb*/
		ctx->luma_size += S5P_FIMV_D_ALIGN_PLANE_SIZE_V8;
		ctx->chroma_size += S5P_FIMV_D_ALIGN_PLANE_SIZE_V8;
	}

	if (ctx->codec_mode == S5P_MFC_CODEC_H264_DEC ||
			ctx->codec_mode == S5P_MFC_CODEC_H264_MVC_DEC) {
		if (IS_MFCV10(dev)) {
			ctx->mv_size = S5P_MFC_DEC_MV_SIZE_V10(ctx->img_width,
					ctx->img_height);
		} else {
			ctx->mv_size = S5P_MFC_DEC_MV_SIZE_V6(ctx->img_width,
					ctx->img_height);
		}
	} else if (ctx->codec_mode == S5P_MFC_CODEC_HEVC_DEC) {
		ctx->mv_size = s5p_mfc_dec_hevc_mv_size(ctx->img_width,
				ctx->img_height);
		ctx->mv_size = ALIGN(ctx->mv_size, 32);
	} else {
		ctx->mv_size = 0;
	}
}

static void s5p_mfc_enc_calc_src_size_v6(struct s5p_mfc_ctx *ctx)
{
	unsigned int mb_width, mb_height;

	mb_width = MB_WIDTH(ctx->img_width);
	mb_height = MB_HEIGHT(ctx->img_height);

	ctx->buf_width = ALIGN(ctx->img_width, S5P_FIMV_NV12M_HALIGN_V6);
	ctx->luma_size = ALIGN((mb_width * mb_height) * 256, 256);
	ctx->chroma_size = ALIGN((mb_width * mb_height) * 128, 256);

	/* MFCv7 needs pad bytes for Luma and Chroma */
	if (IS_MFCV7_PLUS(ctx->dev)) {
		ctx->luma_size += MFC_LUMA_PAD_BYTES_V7;
		ctx->chroma_size += MFC_CHROMA_PAD_BYTES_V7;
	}
}

/* Set registers for decoding stream buffer */
static int s5p_mfc_set_dec_stream_buffer_v6(struct s5p_mfc_ctx *ctx,
			int buf_addr, unsigned int start_num_byte,
			unsigned int strm_size)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	struct s5p_mfc_buf_size *buf_size = dev->variant->buf_size;

	mfc_debug_enter();
	mfc_debug(2, "inst_no: %d, buf_addr: 0x%08x,\n"
		"buf_size: 0x%08x (%d)\n",
		ctx->inst_no, buf_addr, strm_size, strm_size);
	writel(strm_size, mfc_regs->d_stream_data_size);
	writel(buf_addr, mfc_regs->d_cpb_buffer_addr);
	writel(buf_size->cpb, mfc_regs->d_cpb_buffer_size);
	writel(start_num_byte, mfc_regs->d_cpb_buffer_offset);

	mfc_debug_leave();
	return 0;
}

/* Set decoding frame buffer */
static int s5p_mfc_set_dec_frame_buffer_v6(struct s5p_mfc_ctx *ctx)
{
	unsigned int frame_size, i;
	unsigned int frame_size_ch, frame_size_mv;
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	size_t buf_addr1;
	int buf_size1;
	int align_gap;

	buf_addr1 = ctx->bank1.dma;
	buf_size1 = ctx->bank1.size;

	mfc_debug(2, "Buf1: %p (%d)\n", (void *)buf_addr1, buf_size1);
	mfc_debug(2, "Total DPB COUNT: %d\n", ctx->total_dpb_count);
	mfc_debug(2, "Setting display delay to %d\n", ctx->display_delay);

	writel(ctx->total_dpb_count, mfc_regs->d_num_dpb);
	writel(ctx->luma_size, mfc_regs->d_first_plane_dpb_size);
	writel(ctx->chroma_size, mfc_regs->d_second_plane_dpb_size);

	writel(buf_addr1, mfc_regs->d_scratch_buffer_addr);
	writel(ctx->scratch_buf_size, mfc_regs->d_scratch_buffer_size);

	if (IS_MFCV8_PLUS(dev)) {
		writel(ctx->img_width,
			mfc_regs->d_first_plane_dpb_stride_size);
		writel(ctx->img_width,
			mfc_regs->d_second_plane_dpb_stride_size);
	}

	buf_addr1 += ctx->scratch_buf_size;
	buf_size1 -= ctx->scratch_buf_size;

	if (ctx->codec_mode == S5P_FIMV_CODEC_H264_DEC ||
			ctx->codec_mode == S5P_FIMV_CODEC_H264_MVC_DEC ||
			ctx->codec_mode == S5P_FIMV_CODEC_HEVC_DEC) {
		writel(ctx->mv_size, mfc_regs->d_mv_buffer_size);
		writel(ctx->mv_count, mfc_regs->d_num_mv);
	}

	frame_size = ctx->luma_size;
	frame_size_ch = ctx->chroma_size;
	frame_size_mv = ctx->mv_size;
	mfc_debug(2, "Frame size: %d ch: %d mv: %d\n",
			frame_size, frame_size_ch, frame_size_mv);

	for (i = 0; i < ctx->total_dpb_count; i++) {
		/* Bank2 */
		mfc_debug(2, "Luma %d: %zx\n", i,
					ctx->dst_bufs[i].cookie.raw.luma);
		writel(ctx->dst_bufs[i].cookie.raw.luma,
				mfc_regs->d_first_plane_dpb + i * 4);
		mfc_debug(2, "\tChroma %d: %zx\n", i,
					ctx->dst_bufs[i].cookie.raw.chroma);
		writel(ctx->dst_bufs[i].cookie.raw.chroma,
				mfc_regs->d_second_plane_dpb + i * 4);
	}
	if (ctx->codec_mode == S5P_MFC_CODEC_H264_DEC ||
			ctx->codec_mode == S5P_MFC_CODEC_H264_MVC_DEC ||
			ctx->codec_mode == S5P_MFC_CODEC_HEVC_DEC) {
		for (i = 0; i < ctx->mv_count; i++) {
			/* To test alignment */
			align_gap = buf_addr1;
			buf_addr1 = ALIGN(buf_addr1, 16);
			align_gap = buf_addr1 - align_gap;
			buf_size1 -= align_gap;

			mfc_debug(2, "\tBuf1: %zx, size: %d\n",
					buf_addr1, buf_size1);
			writel(buf_addr1, mfc_regs->d_mv_buffer + i * 4);
			buf_addr1 += frame_size_mv;
			buf_size1 -= frame_size_mv;
		}
	}
	if (ctx->codec_mode == S5P_FIMV_CODEC_VP9_DEC) {
		writel(buf_addr1, mfc_regs->d_static_buffer_addr);
		writel(DEC_VP9_STATIC_BUFFER_SIZE,
				mfc_regs->d_static_buffer_size);
		buf_addr1 += DEC_VP9_STATIC_BUFFER_SIZE;
		buf_size1 -= DEC_VP9_STATIC_BUFFER_SIZE;
	}

	mfc_debug(2, "Buf1: %zx, buf_size1: %d (frames %d)\n",
			buf_addr1, buf_size1, ctx->total_dpb_count);
	if (buf_size1 < 0) {
		mfc_debug(2, "Not enough memory has been allocated.\n");
		return -ENOMEM;
	}

	writel(ctx->inst_no, mfc_regs->instance_id);
	s5p_mfc_hw_call(dev->mfc_cmds, cmd_host2risc, dev,
			S5P_FIMV_CH_INIT_BUFS_V6, NULL);

	mfc_debug(2, "After setting buffers.\n");
	return 0;
}

/* Set registers for encoding stream buffer */
static int s5p_mfc_set_enc_stream_buffer_v6(struct s5p_mfc_ctx *ctx,
		unsigned long addr, unsigned int size)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;

	writel(addr, mfc_regs->e_stream_buffer_addr); /* 16B align */
	writel(size, mfc_regs->e_stream_buffer_size);

	mfc_debug(2, "stream buf addr: 0x%08lx, size: 0x%x\n",
		  addr, size);

	return 0;
}

static void s5p_mfc_set_enc_frame_buffer_v6(struct s5p_mfc_ctx *ctx,
		unsigned long y_addr, unsigned long c_addr)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;

	writel(y_addr, mfc_regs->e_source_first_plane_addr);
	writel(c_addr, mfc_regs->e_source_second_plane_addr);

	mfc_debug(2, "enc src y buf addr: 0x%08lx\n", y_addr);
	mfc_debug(2, "enc src c buf addr: 0x%08lx\n", c_addr);
}

static void s5p_mfc_get_enc_frame_buffer_v6(struct s5p_mfc_ctx *ctx,
		unsigned long *y_addr, unsigned long *c_addr)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	unsigned long enc_recon_y_addr, enc_recon_c_addr;

	*y_addr = readl(mfc_regs->e_encoded_source_first_plane_addr);
	*c_addr = readl(mfc_regs->e_encoded_source_second_plane_addr);

	enc_recon_y_addr = readl(mfc_regs->e_recon_luma_dpb_addr);
	enc_recon_c_addr = readl(mfc_regs->e_recon_chroma_dpb_addr);

	mfc_debug(2, "recon y addr: 0x%08lx y_addr: 0x%08lx\n", enc_recon_y_addr, *y_addr);
	mfc_debug(2, "recon c addr: 0x%08lx\n", enc_recon_c_addr);
}

/* Set encoding ref & codec buffer */
static int s5p_mfc_set_enc_ref_buffer_v6(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	size_t buf_addr1;
	int i, buf_size1;

	mfc_debug_enter();

	buf_addr1 = ctx->bank1.dma;
	buf_size1 = ctx->bank1.size;

	mfc_debug(2, "Buf1: %p (%d)\n", (void *)buf_addr1, buf_size1);

	if (IS_MFCV10(dev)) {
		/* start address of per buffer is aligned */
		for (i = 0; i < ctx->pb_count; i++) {
			writel(buf_addr1, mfc_regs->e_luma_dpb + (4 * i));
			buf_addr1 += ctx->luma_dpb_size;
			buf_size1 -= ctx->luma_dpb_size;
		}
		for (i = 0; i < ctx->pb_count; i++) {
			writel(buf_addr1, mfc_regs->e_chroma_dpb + (4 * i));
			buf_addr1 += ctx->chroma_dpb_size;
			buf_size1 -= ctx->chroma_dpb_size;
		}
		for (i = 0; i < ctx->pb_count; i++) {
			writel(buf_addr1, mfc_regs->e_me_buffer + (4 * i));
			buf_addr1 += ctx->me_buffer_size;
			buf_size1 -= ctx->me_buffer_size;
		}
	} else {
		for (i = 0; i < ctx->pb_count; i++) {
			writel(buf_addr1, mfc_regs->e_luma_dpb + (4 * i));
			buf_addr1 += ctx->luma_dpb_size;
			writel(buf_addr1, mfc_regs->e_chroma_dpb + (4 * i));
			buf_addr1 += ctx->chroma_dpb_size;
			writel(buf_addr1, mfc_regs->e_me_buffer + (4 * i));
			buf_addr1 += ctx->me_buffer_size;
			buf_size1 -= (ctx->luma_dpb_size + ctx->chroma_dpb_size
					+ ctx->me_buffer_size);
		}
	}

	writel(buf_addr1, mfc_regs->e_scratch_buffer_addr);
	writel(ctx->scratch_buf_size, mfc_regs->e_scratch_buffer_size);
	buf_addr1 += ctx->scratch_buf_size;
	buf_size1 -= ctx->scratch_buf_size;

	writel(buf_addr1, mfc_regs->e_tmv_buffer0);
	buf_addr1 += ctx->tmv_buffer_size >> 1;
	writel(buf_addr1, mfc_regs->e_tmv_buffer1);
	buf_addr1 += ctx->tmv_buffer_size >> 1;
	buf_size1 -= ctx->tmv_buffer_size;

	mfc_debug(2, "Buf1: %zu, buf_size1: %d (ref frames %d)\n",
			buf_addr1, buf_size1, ctx->pb_count);
	if (buf_size1 < 0) {
		mfc_debug(2, "Not enough memory has been allocated.\n");
		return -ENOMEM;
	}

	writel(ctx->inst_no, mfc_regs->instance_id);
	s5p_mfc_hw_call(dev->mfc_cmds, cmd_host2risc, dev,
			S5P_FIMV_CH_INIT_BUFS_V6, NULL);

	mfc_debug_leave();

	return 0;
}

static int s5p_mfc_set_slice_mode(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;

	/* multi-slice control */
	/* multi-slice MB number or bit size */
	writel(ctx->slice_mode, mfc_regs->e_mslice_mode);
	if (ctx->slice_mode == V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_MB) {
		writel(ctx->slice_size.mb, mfc_regs->e_mslice_size_mb);
	} else if (ctx->slice_mode ==
			V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_BYTES) {
		writel(ctx->slice_size.bits, mfc_regs->e_mslice_size_bits);
	} else {
		writel(0x0, mfc_regs->e_mslice_size_mb);
		writel(0x0, mfc_regs->e_mslice_size_bits);
	}

	return 0;
}

static int s5p_mfc_set_enc_params(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	struct s5p_mfc_enc_params *p = &ctx->enc_params;
	unsigned int reg = 0;

	mfc_debug_enter();

	/* width */
	writel(ctx->img_width, mfc_regs->e_frame_width); /* 16 align */
	/* height */
	writel(ctx->img_height, mfc_regs->e_frame_height); /* 16 align */

	/* cropped width */
	writel(ctx->img_width, mfc_regs->e_cropped_frame_width);
	/* cropped height */
	writel(ctx->img_height, mfc_regs->e_cropped_frame_height);
	/* cropped offset */
	writel(0x0, mfc_regs->e_frame_crop_offset);

	/* pictype : IDR period */
	reg = 0;
	reg |= p->gop_size & 0xFFFF;
	writel(reg, mfc_regs->e_gop_config);

	/* multi-slice control */
	/* multi-slice MB number or bit size */
	ctx->slice_mode = p->slice_mode;
	reg = 0;
	if (p->slice_mode == V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_MB) {
		reg |= (0x1 << 3);
		writel(reg, mfc_regs->e_enc_options);
		ctx->slice_size.mb = p->slice_mb;
	} else if (p->slice_mode == V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_BYTES) {
		reg |= (0x1 << 3);
		writel(reg, mfc_regs->e_enc_options);
		ctx->slice_size.bits = p->slice_bit;
	} else {
		reg &= ~(0x1 << 3);
		writel(reg, mfc_regs->e_enc_options);
	}

	s5p_mfc_set_slice_mode(ctx);

	/* cyclic intra refresh */
	writel(p->intra_refresh_mb, mfc_regs->e_ir_size);
	reg = readl(mfc_regs->e_enc_options);
	if (p->intra_refresh_mb == 0)
		reg &= ~(0x1 << 4);
	else
		reg |= (0x1 << 4);
	writel(reg, mfc_regs->e_enc_options);

	/* 'NON_REFERENCE_STORE_ENABLE' for debugging */
	reg = readl(mfc_regs->e_enc_options);
	reg &= ~(0x1 << 9);
	writel(reg, mfc_regs->e_enc_options);

	/* memory structure cur. frame */
	if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12M) {
		/* 0: Linear, 1: 2D tiled*/
		reg = readl(mfc_regs->e_enc_options);
		reg &= ~(0x1 << 7);
		writel(reg, mfc_regs->e_enc_options);
		/* 0: NV12(CbCr), 1: NV21(CrCb) */
		writel(0x0, mfc_regs->pixel_format);
	} else if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV21M) {
		/* 0: Linear, 1: 2D tiled*/
		reg = readl(mfc_regs->e_enc_options);
		reg &= ~(0x1 << 7);
		writel(reg, mfc_regs->e_enc_options);
		/* 0: NV12(CbCr), 1: NV21(CrCb) */
		writel(0x1, mfc_regs->pixel_format);
	} else if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12MT_16X16) {
		/* 0: Linear, 1: 2D tiled*/
		reg = readl(mfc_regs->e_enc_options);
		reg |= (0x1 << 7);
		writel(reg, mfc_regs->e_enc_options);
		/* 0: NV12(CbCr), 1: NV21(CrCb) */
		writel(0x0, mfc_regs->pixel_format);
	}

	/* memory structure recon. frame */
	/* 0: Linear, 1: 2D tiled */
	reg = readl(mfc_regs->e_enc_options);
	reg |= (0x1 << 8);
	writel(reg, mfc_regs->e_enc_options);

	/* padding control & value */
	writel(0x0, mfc_regs->e_padding_ctrl);
	if (p->pad) {
		reg = 0;
		/** enable */
		reg |= (1 << 31);
		/** cr value */
		reg |= ((p->pad_cr & 0xFF) << 16);
		/** cb value */
		reg |= ((p->pad_cb & 0xFF) << 8);
		/** y value */
		reg |= p->pad_luma & 0xFF;
		writel(reg, mfc_regs->e_padding_ctrl);
	}

	/* rate control config. */
	reg = 0;
	/* frame-level rate control */
	reg |= ((p->rc_frame & 0x1) << 9);
	writel(reg, mfc_regs->e_rc_config);

	/* bit rate */
	if (p->rc_frame)
		writel(p->rc_bitrate,
			mfc_regs->e_rc_bit_rate);
	else
		writel(1, mfc_regs->e_rc_bit_rate);

	/* reaction coefficient */
	if (p->rc_frame) {
		if (p->rc_reaction_coeff < TIGHT_CBR_MAX) /* tight CBR */
			writel(1, mfc_regs->e_rc_mode);
		else					  /* loose CBR */
			writel(2, mfc_regs->e_rc_mode);
	}

	/* seq header ctrl */
	reg = readl(mfc_regs->e_enc_options);
	reg &= ~(0x1 << 2);
	reg |= ((p->seq_hdr_mode & 0x1) << 2);

	/* frame skip mode */
	reg &= ~(0x3);
	reg |= (p->frame_skip_mode & 0x3);
	writel(reg, mfc_regs->e_enc_options);

	/* 'DROP_CONTROL_ENABLE', disable */
	reg = readl(mfc_regs->e_rc_config);
	reg &= ~(0x1 << 10);
	writel(reg, mfc_regs->e_rc_config);

	/* setting for MV range [16, 256] */
	reg = (p->mv_h_range & S5P_FIMV_E_MV_RANGE_V6_MASK);
	writel(reg, mfc_regs->e_mv_hor_range);

	reg = (p->mv_v_range & S5P_FIMV_E_MV_RANGE_V6_MASK);
	writel(reg, mfc_regs->e_mv_ver_range);

	writel(0x0, mfc_regs->e_frame_insertion);
	writel(0x0, mfc_regs->e_roi_buffer_addr);
	writel(0x0, mfc_regs->e_param_change);
	writel(0x0, mfc_regs->e_rc_roi_ctrl);
	writel(0x0, mfc_regs->e_picture_tag);

	writel(0x0, mfc_regs->e_bit_count_enable);
	writel(0x0, mfc_regs->e_max_bit_count);
	writel(0x0, mfc_regs->e_min_bit_count);

	writel(0x0, mfc_regs->e_metadata_buffer_addr);
	writel(0x0, mfc_regs->e_metadata_buffer_size);

	mfc_debug_leave();

	return 0;
}

static int s5p_mfc_set_enc_params_h264(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	struct s5p_mfc_enc_params *p = &ctx->enc_params;
	struct s5p_mfc_h264_enc_params *p_h264 = &p->codec.h264;
	unsigned int reg = 0;
	int i;

	mfc_debug_enter();

	s5p_mfc_set_enc_params(ctx);

	/* pictype : number of B */
	reg = readl(mfc_regs->e_gop_config);
	reg &= ~(0x3 << 16);
	reg |= ((p->num_b_frame & 0x3) << 16);
	writel(reg, mfc_regs->e_gop_config);

	/* profile & level */
	reg = 0;
	/** level */
	reg |= ((p_h264->level & 0xFF) << 8);
	/** profile - 0 ~ 3 */
	reg |= p_h264->profile & 0x3F;
	writel(reg, mfc_regs->e_picture_profile);

	/* rate control config. */
	reg = readl(mfc_regs->e_rc_config);
	/** macroblock level rate control */
	reg &= ~(0x1 << 8);
	reg |= ((p->rc_mb & 0x1) << 8);
	writel(reg, mfc_regs->e_rc_config);

	/** frame QP */
	reg &= ~(0x3F);
	reg |= p_h264->rc_frame_qp & 0x3F;
	writel(reg, mfc_regs->e_rc_config);

	/* max & min value of QP */
	reg = 0;
	/** max QP */
	reg |= ((p_h264->rc_max_qp & 0x3F) << 8);
	/** min QP */
	reg |= p_h264->rc_min_qp & 0x3F;
	writel(reg, mfc_regs->e_rc_qp_bound);

	/* other QPs */
	writel(0x0, mfc_regs->e_fixed_picture_qp);
	if (!p->rc_frame && !p->rc_mb) {
		reg = 0;
		reg |= ((p_h264->rc_b_frame_qp & 0x3F) << 16);
		reg |= ((p_h264->rc_p_frame_qp & 0x3F) << 8);
		reg |= p_h264->rc_frame_qp & 0x3F;
		writel(reg, mfc_regs->e_fixed_picture_qp);
	}

	/* frame rate */
	if (p->rc_frame && p->rc_framerate_num && p->rc_framerate_denom) {
		reg = 0;
		reg |= ((p->rc_framerate_num & 0xFFFF) << 16);
		reg |= p->rc_framerate_denom & 0xFFFF;
		writel(reg, mfc_regs->e_rc_frame_rate);
	}

	/* vbv buffer size */
	if (p->frame_skip_mode ==
			V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT) {
		writel(p_h264->cpb_size & 0xFFFF,
				mfc_regs->e_vbv_buffer_size);

		if (p->rc_frame)
			writel(p->vbv_delay, mfc_regs->e_vbv_init_delay);
	}

	/* interlace */
	reg = 0;
	reg |= ((p_h264->interlace & 0x1) << 3);
	writel(reg, mfc_regs->e_h264_options);

	/* height */
	if (p_h264->interlace) {
		writel(ctx->img_height >> 1,
				mfc_regs->e_frame_height); /* 32 align */
		/* cropped height */
		writel(ctx->img_height >> 1,
				mfc_regs->e_cropped_frame_height);
	}

	/* loop filter ctrl */
	reg = readl(mfc_regs->e_h264_options);
	reg &= ~(0x3 << 1);
	reg |= ((p_h264->loop_filter_mode & 0x3) << 1);
	writel(reg, mfc_regs->e_h264_options);

	/* loopfilter alpha offset */
	if (p_h264->loop_filter_alpha < 0) {
		reg = 0x10;
		reg |= (0xFF - p_h264->loop_filter_alpha) + 1;
	} else {
		reg = 0x00;
		reg |= (p_h264->loop_filter_alpha & 0xF);
	}
	writel(reg, mfc_regs->e_h264_lf_alpha_offset);

	/* loopfilter beta offset */
	if (p_h264->loop_filter_beta < 0) {
		reg = 0x10;
		reg |= (0xFF - p_h264->loop_filter_beta) + 1;
	} else {
		reg = 0x00;
		reg |= (p_h264->loop_filter_beta & 0xF);
	}
	writel(reg, mfc_regs->e_h264_lf_beta_offset);

	/* entropy coding mode */
	reg = readl(mfc_regs->e_h264_options);
	reg &= ~(0x1);
	reg |= p_h264->entropy_mode & 0x1;
	writel(reg, mfc_regs->e_h264_options);

	/* number of ref. picture */
	reg = readl(mfc_regs->e_h264_options);
	reg &= ~(0x1 << 7);
	reg |= (((p_h264->num_ref_pic_4p - 1) & 0x1) << 7);
	writel(reg, mfc_regs->e_h264_options);

	/* 8x8 transform enable */
	reg = readl(mfc_regs->e_h264_options);
	reg &= ~(0x3 << 12);
	reg |= ((p_h264->_8x8_transform & 0x3) << 12);
	writel(reg, mfc_regs->e_h264_options);

	/* macroblock adaptive scaling features */
	writel(0x0, mfc_regs->e_mb_rc_config);
	if (p->rc_mb) {
		reg = 0;
		/** dark region */
		reg |= ((p_h264->rc_mb_dark & 0x1) << 3);
		/** smooth region */
		reg |= ((p_h264->rc_mb_smooth & 0x1) << 2);
		/** static region */
		reg |= ((p_h264->rc_mb_static & 0x1) << 1);
		/** high activity region */
		reg |= p_h264->rc_mb_activity & 0x1;
		writel(reg, mfc_regs->e_mb_rc_config);
	}

	/* aspect ratio VUI */
	readl(mfc_regs->e_h264_options);
	reg &= ~(0x1 << 5);
	reg |= ((p_h264->vui_sar & 0x1) << 5);
	writel(reg, mfc_regs->e_h264_options);

	writel(0x0, mfc_regs->e_aspect_ratio);
	writel(0x0, mfc_regs->e_extended_sar);
	if (p_h264->vui_sar) {
		/* aspect ration IDC */
		reg = 0;
		reg |= p_h264->vui_sar_idc & 0xFF;
		writel(reg, mfc_regs->e_aspect_ratio);
		if (p_h264->vui_sar_idc == 0xFF) {
			/* extended SAR */
			reg = 0;
			reg |= (p_h264->vui_ext_sar_width & 0xFFFF) << 16;
			reg |= p_h264->vui_ext_sar_height & 0xFFFF;
			writel(reg, mfc_regs->e_extended_sar);
		}
	}

	/* intra picture period for H.264 open GOP */
	/* control */
	readl(mfc_regs->e_h264_options);
	reg &= ~(0x1 << 4);
	reg |= ((p_h264->open_gop & 0x1) << 4);
	writel(reg, mfc_regs->e_h264_options);

	/* value */
	writel(0x0, mfc_regs->e_h264_i_period);
	if (p_h264->open_gop) {
		reg = 0;
		reg |= p_h264->open_gop_size & 0xFFFF;
		writel(reg, mfc_regs->e_h264_i_period);
	}

	/* 'WEIGHTED_BI_PREDICTION' for B is disable */
	readl(mfc_regs->e_h264_options);
	reg &= ~(0x3 << 9);
	writel(reg, mfc_regs->e_h264_options);

	/* 'CONSTRAINED_INTRA_PRED_ENABLE' is disable */
	readl(mfc_regs->e_h264_options);
	reg &= ~(0x1 << 14);
	writel(reg, mfc_regs->e_h264_options);

	/* ASO */
	readl(mfc_regs->e_h264_options);
	reg &= ~(0x1 << 6);
	reg |= ((p_h264->aso & 0x1) << 6);
	writel(reg, mfc_regs->e_h264_options);

	/* hier qp enable */
	readl(mfc_regs->e_h264_options);
	reg &= ~(0x1 << 8);
	reg |= ((p_h264->open_gop & 0x1) << 8);
	writel(reg, mfc_regs->e_h264_options);
	reg = 0;
	if (p_h264->hier_qp && p_h264->hier_qp_layer) {
		reg |= (p_h264->hier_qp_type & 0x1) << 0x3;
		reg |= p_h264->hier_qp_layer & 0x7;
		writel(reg, mfc_regs->e_h264_num_t_layer);
		/* QP value for each layer */
		for (i = 0; i < p_h264->hier_qp_layer &&
				i < ARRAY_SIZE(p_h264->hier_qp_layer_qp); i++) {
			writel(p_h264->hier_qp_layer_qp[i],
				mfc_regs->e_h264_hierarchical_qp_layer0
				+ i * 4);
		}
	}
	/* number of coding layer should be zero when hierarchical is disable */
	writel(reg, mfc_regs->e_h264_num_t_layer);

	/* frame packing SEI generation */
	readl(mfc_regs->e_h264_options);
	reg &= ~(0x1 << 25);
	reg |= ((p_h264->sei_frame_packing & 0x1) << 25);
	writel(reg, mfc_regs->e_h264_options);
	if (p_h264->sei_frame_packing) {
		reg = 0;
		/** current frame0 flag */
		reg |= ((p_h264->sei_fp_curr_frame_0 & 0x1) << 2);
		/** arrangement type */
		reg |= p_h264->sei_fp_arrangement_type & 0x3;
		writel(reg, mfc_regs->e_h264_frame_packing_sei_info);
	}

	if (p_h264->fmo) {
		switch (p_h264->fmo_map_type) {
		case V4L2_MPEG_VIDEO_H264_FMO_MAP_TYPE_INTERLEAVED_SLICES:
			if (p_h264->fmo_slice_grp > 4)
				p_h264->fmo_slice_grp = 4;
			for (i = 0; i < (p_h264->fmo_slice_grp & 0xF); i++)
				writel(p_h264->fmo_run_len[i] - 1,
					mfc_regs->e_h264_fmo_run_length_minus1_0
					+ i * 4);
			break;
		case V4L2_MPEG_VIDEO_H264_FMO_MAP_TYPE_SCATTERED_SLICES:
			if (p_h264->fmo_slice_grp > 4)
				p_h264->fmo_slice_grp = 4;
			break;
		case V4L2_MPEG_VIDEO_H264_FMO_MAP_TYPE_RASTER_SCAN:
		case V4L2_MPEG_VIDEO_H264_FMO_MAP_TYPE_WIPE_SCAN:
			if (p_h264->fmo_slice_grp > 2)
				p_h264->fmo_slice_grp = 2;
			writel(p_h264->fmo_chg_dir & 0x1,
				mfc_regs->e_h264_fmo_slice_grp_change_dir);
			/* the valid range is 0 ~ number of macroblocks -1 */
			writel(p_h264->fmo_chg_rate,
			mfc_regs->e_h264_fmo_slice_grp_change_rate_minus1);
			break;
		default:
			mfc_err("Unsupported map type for FMO: %d\n",
					p_h264->fmo_map_type);
			p_h264->fmo_map_type = 0;
			p_h264->fmo_slice_grp = 1;
			break;
		}

		writel(p_h264->fmo_map_type,
				mfc_regs->e_h264_fmo_slice_grp_map_type);
		writel(p_h264->fmo_slice_grp - 1,
				mfc_regs->e_h264_fmo_num_slice_grp_minus1);
	} else {
		writel(0, mfc_regs->e_h264_fmo_num_slice_grp_minus1);
	}

	mfc_debug_leave();

	return 0;
}

static int s5p_mfc_set_enc_params_mpeg4(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	struct s5p_mfc_enc_params *p = &ctx->enc_params;
	struct s5p_mfc_mpeg4_enc_params *p_mpeg4 = &p->codec.mpeg4;
	unsigned int reg = 0;

	mfc_debug_enter();

	s5p_mfc_set_enc_params(ctx);

	/* pictype : number of B */
	reg = readl(mfc_regs->e_gop_config);
	reg &= ~(0x3 << 16);
	reg |= ((p->num_b_frame & 0x3) << 16);
	writel(reg, mfc_regs->e_gop_config);

	/* profile & level */
	reg = 0;
	/** level */
	reg |= ((p_mpeg4->level & 0xFF) << 8);
	/** profile - 0 ~ 1 */
	reg |= p_mpeg4->profile & 0x3F;
	writel(reg, mfc_regs->e_picture_profile);

	/* rate control config. */
	reg = readl(mfc_regs->e_rc_config);
	/** macroblock level rate control */
	reg &= ~(0x1 << 8);
	reg |= ((p->rc_mb & 0x1) << 8);
	writel(reg, mfc_regs->e_rc_config);

	/** frame QP */
	reg &= ~(0x3F);
	reg |= p_mpeg4->rc_frame_qp & 0x3F;
	writel(reg, mfc_regs->e_rc_config);

	/* max & min value of QP */
	reg = 0;
	/** max QP */
	reg |= ((p_mpeg4->rc_max_qp & 0x3F) << 8);
	/** min QP */
	reg |= p_mpeg4->rc_min_qp & 0x3F;
	writel(reg, mfc_regs->e_rc_qp_bound);

	/* other QPs */
	writel(0x0, mfc_regs->e_fixed_picture_qp);
	if (!p->rc_frame && !p->rc_mb) {
		reg = 0;
		reg |= ((p_mpeg4->rc_b_frame_qp & 0x3F) << 16);
		reg |= ((p_mpeg4->rc_p_frame_qp & 0x3F) << 8);
		reg |= p_mpeg4->rc_frame_qp & 0x3F;
		writel(reg, mfc_regs->e_fixed_picture_qp);
	}

	/* frame rate */
	if (p->rc_frame && p->rc_framerate_num && p->rc_framerate_denom) {
		reg = 0;
		reg |= ((p->rc_framerate_num & 0xFFFF) << 16);
		reg |= p->rc_framerate_denom & 0xFFFF;
		writel(reg, mfc_regs->e_rc_frame_rate);
	}

	/* vbv buffer size */
	if (p->frame_skip_mode ==
			V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT) {
		writel(p->vbv_size & 0xFFFF, mfc_regs->e_vbv_buffer_size);

		if (p->rc_frame)
			writel(p->vbv_delay, mfc_regs->e_vbv_init_delay);
	}

	/* Disable HEC */
	writel(0x0, mfc_regs->e_mpeg4_options);
	writel(0x0, mfc_regs->e_mpeg4_hec_period);

	mfc_debug_leave();

	return 0;
}

static int s5p_mfc_set_enc_params_h263(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	struct s5p_mfc_enc_params *p = &ctx->enc_params;
	struct s5p_mfc_mpeg4_enc_params *p_h263 = &p->codec.mpeg4;
	unsigned int reg = 0;

	mfc_debug_enter();

	s5p_mfc_set_enc_params(ctx);

	/* profile & level */
	reg = 0;
	/** profile */
	reg |= (0x1 << 4);
	writel(reg, mfc_regs->e_picture_profile);

	/* rate control config. */
	reg = readl(mfc_regs->e_rc_config);
	/** macroblock level rate control */
	reg &= ~(0x1 << 8);
	reg |= ((p->rc_mb & 0x1) << 8);
	writel(reg, mfc_regs->e_rc_config);

	/** frame QP */
	reg &= ~(0x3F);
	reg |= p_h263->rc_frame_qp & 0x3F;
	writel(reg, mfc_regs->e_rc_config);

	/* max & min value of QP */
	reg = 0;
	/** max QP */
	reg |= ((p_h263->rc_max_qp & 0x3F) << 8);
	/** min QP */
	reg |= p_h263->rc_min_qp & 0x3F;
	writel(reg, mfc_regs->e_rc_qp_bound);

	/* other QPs */
	writel(0x0, mfc_regs->e_fixed_picture_qp);
	if (!p->rc_frame && !p->rc_mb) {
		reg = 0;
		reg |= ((p_h263->rc_b_frame_qp & 0x3F) << 16);
		reg |= ((p_h263->rc_p_frame_qp & 0x3F) << 8);
		reg |= p_h263->rc_frame_qp & 0x3F;
		writel(reg, mfc_regs->e_fixed_picture_qp);
	}

	/* frame rate */
	if (p->rc_frame && p->rc_framerate_num && p->rc_framerate_denom) {
		reg = 0;
		reg |= ((p->rc_framerate_num & 0xFFFF) << 16);
		reg |= p->rc_framerate_denom & 0xFFFF;
		writel(reg, mfc_regs->e_rc_frame_rate);
	}

	/* vbv buffer size */
	if (p->frame_skip_mode ==
			V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT) {
		writel(p->vbv_size & 0xFFFF, mfc_regs->e_vbv_buffer_size);

		if (p->rc_frame)
			writel(p->vbv_delay, mfc_regs->e_vbv_init_delay);
	}

	mfc_debug_leave();

	return 0;
}

static int s5p_mfc_set_enc_params_vp8(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	struct s5p_mfc_enc_params *p = &ctx->enc_params;
	struct s5p_mfc_vp8_enc_params *p_vp8 = &p->codec.vp8;
	unsigned int reg = 0;
	unsigned int val = 0;

	mfc_debug_enter();

	s5p_mfc_set_enc_params(ctx);

	/* pictype : number of B */
	reg = readl(mfc_regs->e_gop_config);
	reg &= ~(0x3 << 16);
	reg |= ((p->num_b_frame & 0x3) << 16);
	writel(reg, mfc_regs->e_gop_config);

	/* profile - 0 ~ 3 */
	reg = p_vp8->profile & 0x3;
	writel(reg, mfc_regs->e_picture_profile);

	/* rate control config. */
	reg = readl(mfc_regs->e_rc_config);
	/** macroblock level rate control */
	reg &= ~(0x1 << 8);
	reg |= ((p->rc_mb & 0x1) << 8);
	writel(reg, mfc_regs->e_rc_config);

	/* frame rate */
	if (p->rc_frame && p->rc_framerate_num && p->rc_framerate_denom) {
		reg = 0;
		reg |= ((p->rc_framerate_num & 0xFFFF) << 16);
		reg |= p->rc_framerate_denom & 0xFFFF;
		writel(reg, mfc_regs->e_rc_frame_rate);
	}

	/* frame QP */
	reg &= ~(0x7F);
	reg |= p_vp8->rc_frame_qp & 0x7F;
	writel(reg, mfc_regs->e_rc_config);

	/* other QPs */
	writel(0x0, mfc_regs->e_fixed_picture_qp);
	if (!p->rc_frame && !p->rc_mb) {
		reg = 0;
		reg |= ((p_vp8->rc_p_frame_qp & 0x7F) << 8);
		reg |= p_vp8->rc_frame_qp & 0x7F;
		writel(reg, mfc_regs->e_fixed_picture_qp);
	}

	/* max QP */
	reg = ((p_vp8->rc_max_qp & 0x7F) << 8);
	/* min QP */
	reg |= p_vp8->rc_min_qp & 0x7F;
	writel(reg, mfc_regs->e_rc_qp_bound);

	/* vbv buffer size */
	if (p->frame_skip_mode ==
			V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT) {
		writel(p->vbv_size & 0xFFFF, mfc_regs->e_vbv_buffer_size);

		if (p->rc_frame)
			writel(p->vbv_delay, mfc_regs->e_vbv_init_delay);
	}

	/* VP8 specific params */
	reg = 0;
	reg |= (p_vp8->imd_4x4 & 0x1) << 10;
	switch (p_vp8->num_partitions) {
	case V4L2_CID_MPEG_VIDEO_VPX_1_PARTITION:
		val = 0;
		break;
	case V4L2_CID_MPEG_VIDEO_VPX_2_PARTITIONS:
		val = 2;
		break;
	case V4L2_CID_MPEG_VIDEO_VPX_4_PARTITIONS:
		val = 4;
		break;
	case V4L2_CID_MPEG_VIDEO_VPX_8_PARTITIONS:
		val = 8;
		break;
	}
	reg |= (val & 0xF) << 3;
	reg |= (p_vp8->num_ref & 0x2);
	writel(reg, mfc_regs->e_vp8_options);

	mfc_debug_leave();

	return 0;
}

static int s5p_mfc_set_enc_params_hevc(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	struct s5p_mfc_enc_params *p = &ctx->enc_params;
	struct s5p_mfc_hevc_enc_params *p_hevc = &p->codec.hevc;
	unsigned int reg = 0;
	int i;

	mfc_debug_enter();

	s5p_mfc_set_enc_params(ctx);

	/* pictype : number of B */
	reg = readl(mfc_regs->e_gop_config);
	/* num_b_frame - 0 ~ 2 */
	reg &= ~(0x3 << 16);
	reg |= (p->num_b_frame << 16);
	writel(reg, mfc_regs->e_gop_config);

	/* UHD encoding case */
	if ((ctx->img_width == 3840) && (ctx->img_height == 2160)) {
		p_hevc->level = 51;
		p_hevc->tier = 0;
	/* this tier can be changed */
	}

	/* tier & level */
	reg = 0;
	/* profile */
	reg |= p_hevc->profile & 0x3;
	/* level */
	reg &= ~(0xFF << 8);
	reg |= (p_hevc->level << 8);
	/* tier - 0 ~ 1 */
	reg |= (p_hevc->tier << 16);
	writel(reg, mfc_regs->e_picture_profile);

	switch (p_hevc->loopfilter) {
	case V4L2_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE_DISABLED:
		p_hevc->loopfilter_disable = 1;
		break;
	case V4L2_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE_ENABLED:
		p_hevc->loopfilter_disable = 0;
		p_hevc->loopfilter_across = 1;
		break;
	case V4L2_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE_DISABLED_AT_SLICE_BOUNDARY:
		p_hevc->loopfilter_disable = 0;
		p_hevc->loopfilter_across = 0;
		break;
	}

	/* max partition depth */
	reg = 0;
	reg |= (p_hevc->max_partition_depth & 0x1);
	reg |= (p_hevc->num_refs_for_p-1) << 2;
	reg |= (p_hevc->refreshtype & 0x3) << 3;
	reg |= (p_hevc->const_intra_period_enable & 0x1) << 5;
	reg |= (p_hevc->lossless_cu_enable & 0x1) << 6;
	reg |= (p_hevc->wavefront_enable & 0x1) << 7;
	reg |= (p_hevc->loopfilter_disable & 0x1) << 8;
	reg |= (p_hevc->loopfilter_across & 0x1) << 9;
	reg |= (p_hevc->enable_ltr & 0x1) << 10;
	reg |= (p_hevc->hier_qp_enable & 0x1) << 11;
	reg |= (p_hevc->general_pb_enable & 0x1) << 13;
	reg |= (p_hevc->temporal_id_enable & 0x1) << 14;
	reg |= (p_hevc->strong_intra_smooth & 0x1) << 15;
	reg |= (p_hevc->intra_pu_split_disable & 0x1) << 16;
	reg |= (p_hevc->tmv_prediction_disable & 0x1) << 17;
	reg |= (p_hevc->max_num_merge_mv & 0x7) << 18;
	reg |= (p_hevc->encoding_nostartcode_enable & 0x1) << 23;
	reg |= (p_hevc->prepend_sps_pps_to_idr << 26);

	writel(reg, mfc_regs->e_hevc_options);
	/* refresh period */
	if (p_hevc->refreshtype) {
		reg = 0;
		reg |= (p_hevc->refreshperiod & 0xFFFF);
		writel(reg, mfc_regs->e_hevc_refresh_period);
	}
	/* loop filter setting */
	if (!(p_hevc->loopfilter_disable & 0x1)) {
		reg = 0;
		reg |= (p_hevc->lf_beta_offset_div2);
		writel(reg, mfc_regs->e_hevc_lf_beta_offset_div2);
		reg = 0;
		reg |= (p_hevc->lf_tc_offset_div2);
		writel(reg, mfc_regs->e_hevc_lf_tc_offset_div2);
	}
	/* hier qp enable */
	if (p_hevc->num_hier_layer) {
		reg = 0;
		reg |= (p_hevc->hier_qp_type & 0x1) << 0x3;
		reg |= p_hevc->num_hier_layer & 0x7;
		writel(reg, mfc_regs->e_num_t_layer);
		/* QP value for each layer */
		if (p_hevc->hier_qp_enable) {
			for (i = 0; i < 7; i++)
				writel(p_hevc->hier_qp_layer[i],
					mfc_regs->e_hier_qp_layer0 + i * 4);
		}
		if (p->rc_frame) {
			for (i = 0; i < 7; i++)
				writel(p_hevc->hier_bit_layer[i],
						mfc_regs->e_hier_bit_rate_layer0
						+ i * 4);
		}
	}

	/* rate control config. */
	reg = readl(mfc_regs->e_rc_config);
	/* macroblock level rate control */
	reg &= ~(0x1 << 8);
	reg |= (p->rc_mb << 8);
	writel(reg, mfc_regs->e_rc_config);
	/* frame QP */
	reg &= ~(0xFF);
	reg |= p_hevc->rc_frame_qp;
	writel(reg, mfc_regs->e_rc_config);

	/* frame rate */
	if (p->rc_frame) {
		reg = 0;
		reg &= ~(0xFFFF << 16);
		reg |= ((p_hevc->rc_framerate) << 16);
		reg &= ~(0xFFFF);
		reg |= FRAME_DELTA_DEFAULT;
		writel(reg, mfc_regs->e_rc_frame_rate);
	}

	/* max & min value of QP */
	reg = 0;
	/* max QP */
	reg &= ~(0xFF << 8);
	reg |= (p_hevc->rc_max_qp << 8);
	/* min QP */
	reg &= ~(0xFF);
	reg |= p_hevc->rc_min_qp;
	writel(reg, mfc_regs->e_rc_qp_bound);

	writel(0x0, mfc_regs->e_fixed_picture_qp);
	if (!p->rc_frame && !p->rc_mb) {
		reg = 0;
		reg &= ~(0xFF << 16);
		reg |= (p_hevc->rc_b_frame_qp << 16);
		reg &= ~(0xFF << 8);
		reg |= (p_hevc->rc_p_frame_qp << 8);
		reg &= ~(0xFF);
		reg |= p_hevc->rc_frame_qp;
		writel(reg, mfc_regs->e_fixed_picture_qp);
	}
	mfc_debug_leave();

	return 0;
}

/* Initialize decoding */
static int s5p_mfc_init_decode_v6(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	unsigned int reg = 0;
	int fmo_aso_ctrl = 0;

	mfc_debug_enter();
	mfc_debug(2, "InstNo: %d/%d\n", ctx->inst_no,
			S5P_FIMV_CH_SEQ_HEADER_V6);
	mfc_debug(2, "BUFs: %08x %08x %08x\n",
		  readl(mfc_regs->d_cpb_buffer_addr),
		  readl(mfc_regs->d_cpb_buffer_addr),
		  readl(mfc_regs->d_cpb_buffer_addr));

	/* FMO_ASO_CTRL - 0: Enable, 1: Disable */
	reg |= (fmo_aso_ctrl << S5P_FIMV_D_OPT_FMO_ASO_CTRL_MASK_V6);

	if (ctx->display_delay_enable) {
		reg |= (0x1 << S5P_FIMV_D_OPT_DDELAY_EN_SHIFT_V6);
		writel(ctx->display_delay, mfc_regs->d_display_delay);
	}

	if (IS_MFCV7_PLUS(dev) || IS_MFCV6_V2(dev)) {
		writel(reg, mfc_regs->d_dec_options);
		reg = 0;
	}

	/* Setup loop filter, for decoding this is only valid for MPEG4 */
	if (ctx->codec_mode == S5P_MFC_CODEC_MPEG4_DEC) {
		mfc_debug(2, "Set loop filter to: %d\n",
				ctx->loop_filter_mpeg4);
		reg |= (ctx->loop_filter_mpeg4 <<
				S5P_FIMV_D_OPT_LF_CTRL_SHIFT_V6);
	}
	if (ctx->dst_fmt->fourcc == V4L2_PIX_FMT_NV12MT_16X16)
		reg |= (0x1 << S5P_FIMV_D_OPT_TILE_MODE_SHIFT_V6);

	if (IS_MFCV7_PLUS(dev) || IS_MFCV6_V2(dev))
		writel(reg, mfc_regs->d_init_buffer_options);
	else
		writel(reg, mfc_regs->d_dec_options);

	/* 0: NV12(CbCr), 1: NV21(CrCb) */
	if (ctx->dst_fmt->fourcc == V4L2_PIX_FMT_NV21M)
		writel(0x1, mfc_regs->pixel_format);
	else
		writel(0x0, mfc_regs->pixel_format);


	/* sei parse */
	writel(ctx->sei_fp_parse & 0x1, mfc_regs->d_sei_enable);

	writel(ctx->inst_no, mfc_regs->instance_id);
	s5p_mfc_hw_call(dev->mfc_cmds, cmd_host2risc, dev,
			S5P_FIMV_CH_SEQ_HEADER_V6, NULL);

	mfc_debug_leave();
	return 0;
}

static inline void s5p_mfc_set_flush(struct s5p_mfc_ctx *ctx, int flush)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;

	if (flush) {
		dev->curr_ctx = ctx->num;
		writel(ctx->inst_no, mfc_regs->instance_id);
		s5p_mfc_hw_call(dev->mfc_cmds, cmd_host2risc, dev,
				S5P_FIMV_H2R_CMD_FLUSH_V6, NULL);
	}
}

/* Decode a single frame */
static int s5p_mfc_decode_one_frame_v6(struct s5p_mfc_ctx *ctx,
			enum s5p_mfc_decode_arg last_frame)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;

	writel(ctx->dec_dst_flag, mfc_regs->d_available_dpb_flag_lower);
	writel(ctx->slice_interface & 0x1, mfc_regs->d_slice_if_enable);

	writel(ctx->inst_no, mfc_regs->instance_id);
	/* Issue different commands to instance basing on whether it
	 * is the last frame or not. */
	switch (last_frame) {
	case 0:
		s5p_mfc_hw_call(dev->mfc_cmds, cmd_host2risc, dev,
				S5P_FIMV_CH_FRAME_START_V6, NULL);
		break;
	case 1:
		s5p_mfc_hw_call(dev->mfc_cmds, cmd_host2risc, dev,
				S5P_FIMV_CH_LAST_FRAME_V6, NULL);
		break;
	default:
		mfc_err("Unsupported last frame arg.\n");
		return -EINVAL;
	}

	mfc_debug(2, "Decoding a usual frame.\n");
	return 0;
}

static int s5p_mfc_init_encode_v6(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;

	if (ctx->codec_mode == S5P_MFC_CODEC_H264_ENC)
		s5p_mfc_set_enc_params_h264(ctx);
	else if (ctx->codec_mode == S5P_MFC_CODEC_MPEG4_ENC)
		s5p_mfc_set_enc_params_mpeg4(ctx);
	else if (ctx->codec_mode == S5P_MFC_CODEC_H263_ENC)
		s5p_mfc_set_enc_params_h263(ctx);
	else if (ctx->codec_mode == S5P_MFC_CODEC_VP8_ENC)
		s5p_mfc_set_enc_params_vp8(ctx);
	else if (ctx->codec_mode == S5P_FIMV_CODEC_HEVC_ENC)
		s5p_mfc_set_enc_params_hevc(ctx);
	else {
		mfc_err("Unknown codec for encoding (%x).\n",
			ctx->codec_mode);
		return -EINVAL;
	}

	/* Set stride lengths for v7 & above */
	if (IS_MFCV7_PLUS(dev)) {
		writel(ctx->img_width, mfc_regs->e_source_first_plane_stride);
		writel(ctx->img_width, mfc_regs->e_source_second_plane_stride);
	}

	writel(ctx->inst_no, mfc_regs->instance_id);
	s5p_mfc_hw_call(dev->mfc_cmds, cmd_host2risc, dev,
			S5P_FIMV_CH_SEQ_HEADER_V6, NULL);

	return 0;
}

static int s5p_mfc_h264_set_aso_slice_order_v6(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	struct s5p_mfc_enc_params *p = &ctx->enc_params;
	struct s5p_mfc_h264_enc_params *p_h264 = &p->codec.h264;
	int i;

	if (p_h264->aso) {
		for (i = 0; i < ARRAY_SIZE(p_h264->aso_slice_order); i++) {
			writel(p_h264->aso_slice_order[i],
				mfc_regs->e_h264_aso_slice_order_0 + i * 4);
		}
	}
	return 0;
}

/* Encode a single frame */
static int s5p_mfc_encode_one_frame_v6(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	int cmd;

	mfc_debug(2, "++\n");

	/* memory structure cur. frame */

	if (ctx->codec_mode == S5P_MFC_CODEC_H264_ENC)
		s5p_mfc_h264_set_aso_slice_order_v6(ctx);

	s5p_mfc_set_slice_mode(ctx);

	if (ctx->state != MFCINST_FINISHING)
		cmd = S5P_FIMV_CH_FRAME_START_V6;
	else
		cmd = S5P_FIMV_CH_LAST_FRAME_V6;

	writel(ctx->inst_no, mfc_regs->instance_id);
	s5p_mfc_hw_call(dev->mfc_cmds, cmd_host2risc, dev, cmd, NULL);

	mfc_debug(2, "--\n");

	return 0;
}

static inline void s5p_mfc_run_dec_last_frames(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	s5p_mfc_set_dec_stream_buffer_v6(ctx, 0, 0, 0);
	dev->curr_ctx = ctx->num;
	s5p_mfc_decode_one_frame_v6(ctx, MFC_DEC_LAST_FRAME);
}

static inline int s5p_mfc_run_dec_frame(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *temp_vb;
	int last_frame = 0;

	if (ctx->state == MFCINST_FINISHING) {
		last_frame = MFC_DEC_LAST_FRAME;
		s5p_mfc_set_dec_stream_buffer_v6(ctx, 0, 0, 0);
		dev->curr_ctx = ctx->num;
		s5p_mfc_clean_ctx_int_flags(ctx);
		s5p_mfc_decode_one_frame_v6(ctx, last_frame);
		return 0;
	}

	/* Frames are being decoded */
	if (list_empty(&ctx->src_queue)) {
		mfc_debug(2, "No src buffers.\n");
		return -EAGAIN;
	}
	/* Get the next source buffer */
	temp_vb = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);
	temp_vb->flags |= MFC_BUF_FLAG_USED;
	s5p_mfc_set_dec_stream_buffer_v6(ctx,
		vb2_dma_contig_plane_dma_addr(&temp_vb->b->vb2_buf, 0),
			ctx->consumed_stream,
			temp_vb->b->vb2_buf.planes[0].bytesused);

	dev->curr_ctx = ctx->num;
	if (temp_vb->b->vb2_buf.planes[0].bytesused == 0) {
		last_frame = 1;
		mfc_debug(2, "Setting ctx->state to FINISHING\n");
		ctx->state = MFCINST_FINISHING;
	}
	s5p_mfc_decode_one_frame_v6(ctx, last_frame);

	return 0;
}

static inline int s5p_mfc_run_enc_frame(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *dst_mb;
	struct s5p_mfc_buf *src_mb;
	unsigned long src_y_addr, src_c_addr, dst_addr;
	/*
	unsigned int src_y_size, src_c_size;
	*/
	unsigned int dst_size;

	if (list_empty(&ctx->src_queue) && ctx->state != MFCINST_FINISHING) {
		mfc_debug(2, "no src buffers.\n");
		return -EAGAIN;
	}

	if (list_empty(&ctx->dst_queue)) {
		mfc_debug(2, "no dst buffers.\n");
		return -EAGAIN;
	}

	if (list_empty(&ctx->src_queue)) {
		/* send null frame */
		s5p_mfc_set_enc_frame_buffer_v6(ctx, 0, 0);
		src_mb = NULL;
	} else {
		src_mb = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);
		src_mb->flags |= MFC_BUF_FLAG_USED;
		if (src_mb->b->vb2_buf.planes[0].bytesused == 0) {
			s5p_mfc_set_enc_frame_buffer_v6(ctx, 0, 0);
			ctx->state = MFCINST_FINISHING;
		} else {
			src_y_addr = vb2_dma_contig_plane_dma_addr(&src_mb->b->vb2_buf, 0);
			src_c_addr = vb2_dma_contig_plane_dma_addr(&src_mb->b->vb2_buf, 1);

			mfc_debug(2, "enc src y addr: 0x%08lx\n", src_y_addr);
			mfc_debug(2, "enc src c addr: 0x%08lx\n", src_c_addr);

			s5p_mfc_set_enc_frame_buffer_v6(ctx, src_y_addr, src_c_addr);
			if (src_mb->flags & MFC_BUF_FLAG_EOS)
				ctx->state = MFCINST_FINISHING;
		}
	}

	dst_mb = list_entry(ctx->dst_queue.next, struct s5p_mfc_buf, list);
	dst_mb->flags |= MFC_BUF_FLAG_USED;
	dst_addr = vb2_dma_contig_plane_dma_addr(&dst_mb->b->vb2_buf, 0);
	dst_size = vb2_plane_size(&dst_mb->b->vb2_buf, 0);

	s5p_mfc_set_enc_stream_buffer_v6(ctx, dst_addr, dst_size);

	dev->curr_ctx = ctx->num;
	s5p_mfc_encode_one_frame_v6(ctx);

	return 0;
}

static inline void s5p_mfc_run_init_dec(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *temp_vb;

	/* Initializing decoding - parsing header */
	mfc_debug(2, "Preparing to init decoding.\n");
	temp_vb = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);
	mfc_debug(2, "Header size: %d\n", temp_vb->b->vb2_buf.planes[0].bytesused);
	s5p_mfc_set_dec_stream_buffer_v6(ctx,
		vb2_dma_contig_plane_dma_addr(&temp_vb->b->vb2_buf, 0), 0,
			temp_vb->b->vb2_buf.planes[0].bytesused);
	dev->curr_ctx = ctx->num;
	s5p_mfc_init_decode_v6(ctx);
}

static inline void s5p_mfc_run_init_enc(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *dst_mb;
	unsigned long dst_addr;
	unsigned int dst_size;

	dst_mb = list_entry(ctx->dst_queue.next, struct s5p_mfc_buf, list);
	dst_addr = vb2_dma_contig_plane_dma_addr(&dst_mb->b->vb2_buf, 0);
	dst_size = vb2_plane_size(&dst_mb->b->vb2_buf, 0);
	s5p_mfc_set_enc_stream_buffer_v6(ctx, dst_addr, dst_size);
	dev->curr_ctx = ctx->num;
	s5p_mfc_init_encode_v6(ctx);
}

static inline int s5p_mfc_run_init_dec_buffers(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	int ret;
	/* Header was parsed now start processing
	 * First set the output frame buffers
	 * s5p_mfc_alloc_dec_buffers(ctx); */

	if (ctx->capture_state != QUEUE_BUFS_MMAPED) {
		mfc_err("It seems that not all destination buffers were\n"
			"mmapped.MFC requires that all destination are mmapped\n"
			"before starting processing.\n");
		return -EAGAIN;
	}

	dev->curr_ctx = ctx->num;
	ret = s5p_mfc_set_dec_frame_buffer_v6(ctx);
	if (ret) {
		mfc_err("Failed to alloc frame mem.\n");
		ctx->state = MFCINST_ERROR;
	}
	return ret;
}

static inline int s5p_mfc_run_init_enc_buffers(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	int ret;

	dev->curr_ctx = ctx->num;
	ret = s5p_mfc_set_enc_ref_buffer_v6(ctx);
	if (ret) {
		mfc_err("Failed to alloc frame mem.\n");
		ctx->state = MFCINST_ERROR;
	}
	return ret;
}

/* Try running an operation on hardware */
static void s5p_mfc_try_run_v6(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_ctx *ctx;
	int new_ctx;
	unsigned int ret = 0;

	mfc_debug(1, "Try run dev: %p\n", dev);

	/* Check whether hardware is not running */
	if (test_and_set_bit(0, &dev->hw_lock) != 0) {
		/* This is perfectly ok, the scheduled ctx should wait */
		mfc_debug(1, "Couldn't lock HW.\n");
		return;
	}

	/* Choose the context to run */
	new_ctx = s5p_mfc_get_new_ctx(dev);
	if (new_ctx < 0) {
		/* No contexts to run */
		if (test_and_clear_bit(0, &dev->hw_lock) == 0) {
			mfc_err("Failed to unlock hardware.\n");
			return;
		}

		mfc_debug(1, "No ctx is scheduled to be run.\n");
		return;
	}

	mfc_debug(1, "New context: %d\n", new_ctx);
	ctx = dev->ctx[new_ctx];
	mfc_debug(1, "Setting new context to %p\n", ctx);
	/* Got context to run in ctx */
	mfc_debug(1, "ctx->dst_queue_cnt=%d ctx->dpb_count=%d ctx->src_queue_cnt=%d\n",
		ctx->dst_queue_cnt, ctx->pb_count, ctx->src_queue_cnt);
	mfc_debug(1, "ctx->state=%d\n", ctx->state);
	/* Last frame has already been sent to MFC
	 * Now obtaining frames from MFC buffer */

	s5p_mfc_clock_on();
	s5p_mfc_clean_ctx_int_flags(ctx);

	if (ctx->type == MFCINST_DECODER) {
		switch (ctx->state) {
		case MFCINST_FINISHING:
			s5p_mfc_run_dec_last_frames(ctx);
			break;
		case MFCINST_RUNNING:
			ret = s5p_mfc_run_dec_frame(ctx);
			break;
		case MFCINST_INIT:
			ret = s5p_mfc_hw_call(dev->mfc_cmds, open_inst_cmd,
					ctx);
			break;
		case MFCINST_RETURN_INST:
			ret = s5p_mfc_hw_call(dev->mfc_cmds, close_inst_cmd,
					ctx);
			break;
		case MFCINST_GOT_INST:
			s5p_mfc_run_init_dec(ctx);
			break;
		case MFCINST_HEAD_PARSED:
			ret = s5p_mfc_run_init_dec_buffers(ctx);
			break;
		case MFCINST_FLUSH:
			s5p_mfc_set_flush(ctx, ctx->dpb_flush_flag);
			break;
		case MFCINST_RES_CHANGE_INIT:
			s5p_mfc_run_dec_last_frames(ctx);
			break;
		case MFCINST_RES_CHANGE_FLUSH:
			s5p_mfc_run_dec_last_frames(ctx);
			break;
		case MFCINST_RES_CHANGE_END:
			mfc_debug(2, "Finished remaining frames after resolution change.\n");
			ctx->capture_state = QUEUE_FREE;
			mfc_debug(2, "Will re-init the codec`.\n");
			s5p_mfc_run_init_dec(ctx);
			break;
		default:
			ret = -EAGAIN;
		}
	} else if (ctx->type == MFCINST_ENCODER) {
		switch (ctx->state) {
		case MFCINST_FINISHING:
		case MFCINST_RUNNING:
			ret = s5p_mfc_run_enc_frame(ctx);
			break;
		case MFCINST_INIT:
			ret = s5p_mfc_hw_call(dev->mfc_cmds, open_inst_cmd,
					ctx);
			break;
		case MFCINST_RETURN_INST:
			ret = s5p_mfc_hw_call(dev->mfc_cmds, close_inst_cmd,
					ctx);
			break;
		case MFCINST_GOT_INST:
			s5p_mfc_run_init_enc(ctx);
			break;
		case MFCINST_HEAD_PRODUCED:
			ret = s5p_mfc_run_init_enc_buffers(ctx);
			break;
		default:
			ret = -EAGAIN;
		}
	} else {
		mfc_err("invalid context type: %d\n", ctx->type);
		ret = -EAGAIN;
	}

	if (ret) {
		/* Free hardware lock */
		if (test_and_clear_bit(0, &dev->hw_lock) == 0)
			mfc_err("Failed to unlock hardware.\n");

		/* This is in deed imporant, as no operation has been
		 * scheduled, reduce the clock count as no one will
		 * ever do this, because no interrupt related to this try_run
		 * will ever come from hardware. */
		s5p_mfc_clock_off();
	}
}

static void s5p_mfc_clear_int_flags_v6(struct s5p_mfc_dev *dev)
{
	const struct s5p_mfc_regs *mfc_regs = dev->mfc_regs;
	writel(0, mfc_regs->risc2host_command);
	writel(0, mfc_regs->risc2host_int);
}

static unsigned int
s5p_mfc_read_info_v6(struct s5p_mfc_ctx *ctx, unsigned long ofs)
{
	int ret;

	s5p_mfc_clock_on();
	ret = readl((void __iomem *)ofs);
	s5p_mfc_clock_off();

	return ret;
}

static int s5p_mfc_get_dspl_y_adr_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->d_display_first_plane_addr);
}

static int s5p_mfc_get_dec_y_adr_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->d_decoded_first_plane_addr);
}

static int s5p_mfc_get_dspl_status_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->d_display_status);
}

static int s5p_mfc_get_dec_status_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->d_decoded_status);
}

static int s5p_mfc_get_dec_frame_type_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->d_decoded_frame_type) &
		S5P_FIMV_DECODE_FRAME_MASK_V6;
}

static int s5p_mfc_get_disp_frame_type_v6(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	return readl(dev->mfc_regs->d_display_frame_type) &
		S5P_FIMV_DECODE_FRAME_MASK_V6;
}

static int s5p_mfc_get_consumed_stream_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->d_decoded_nal_size);
}

static int s5p_mfc_get_int_reason_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->risc2host_command) &
		S5P_FIMV_RISC2HOST_CMD_MASK;
}

static int s5p_mfc_get_int_err_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->error_code);
}

static int s5p_mfc_err_dec_v6(unsigned int err)
{
	return (err & S5P_FIMV_ERR_DEC_MASK_V6) >> S5P_FIMV_ERR_DEC_SHIFT_V6;
}

static int s5p_mfc_get_img_width_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->d_display_frame_width);
}

static int s5p_mfc_get_img_height_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->d_display_frame_height);
}

static int s5p_mfc_get_dpb_count_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->d_min_num_dpb);
}

static int s5p_mfc_get_mv_count_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->d_min_num_mv);
}

static int s5p_mfc_get_min_scratch_buf_size(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->d_min_scratch_buffer_size);
}

static int s5p_mfc_get_e_min_scratch_buf_size(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->e_min_scratch_buffer_size);
}

static int s5p_mfc_get_inst_no_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->ret_instance_id);
}

static int s5p_mfc_get_enc_dpb_count_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->e_num_dpb);
}

static int s5p_mfc_get_enc_strm_size_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->e_stream_size);
}

static int s5p_mfc_get_enc_slice_type_v6(struct s5p_mfc_dev *dev)
{
	return readl(dev->mfc_regs->e_slice_type);
}

static unsigned int s5p_mfc_get_pic_type_top_v6(struct s5p_mfc_ctx *ctx)
{
	return s5p_mfc_read_info_v6(ctx,
		(__force unsigned long) ctx->dev->mfc_regs->d_ret_picture_tag_top);
}

static unsigned int s5p_mfc_get_pic_type_bot_v6(struct s5p_mfc_ctx *ctx)
{
	return s5p_mfc_read_info_v6(ctx,
		(__force unsigned long) ctx->dev->mfc_regs->d_ret_picture_tag_bot);
}

static unsigned int s5p_mfc_get_crop_info_h_v6(struct s5p_mfc_ctx *ctx)
{
	return s5p_mfc_read_info_v6(ctx,
		(__force unsigned long) ctx->dev->mfc_regs->d_display_crop_info1);
}

static unsigned int s5p_mfc_get_crop_info_v_v6(struct s5p_mfc_ctx *ctx)
{
	return s5p_mfc_read_info_v6(ctx,
		(__force unsigned long) ctx->dev->mfc_regs->d_display_crop_info2);
}

static struct s5p_mfc_regs mfc_regs;

/* Initialize registers for MFC v6 onwards */
const struct s5p_mfc_regs *s5p_mfc_init_regs_v6_plus(struct s5p_mfc_dev *dev)
{
	memset(&mfc_regs, 0, sizeof(mfc_regs));

#define S5P_MFC_REG_ADDR(dev, reg) ((dev)->regs_base + (reg))
#define R(m, r) mfc_regs.m = S5P_MFC_REG_ADDR(dev, r)
	/* codec common registers */
	R(risc_on, S5P_FIMV_RISC_ON_V6);
	R(risc2host_int, S5P_FIMV_RISC2HOST_INT_V6);
	R(host2risc_int, S5P_FIMV_HOST2RISC_INT_V6);
	R(risc_base_address, S5P_FIMV_RISC_BASE_ADDRESS_V6);
	R(mfc_reset, S5P_FIMV_MFC_RESET_V6);
	R(host2risc_command, S5P_FIMV_HOST2RISC_CMD_V6);
	R(risc2host_command, S5P_FIMV_RISC2HOST_CMD_V6);
	R(firmware_version, S5P_FIMV_FW_VERSION_V6);
	R(instance_id, S5P_FIMV_INSTANCE_ID_V6);
	R(codec_type, S5P_FIMV_CODEC_TYPE_V6);
	R(context_mem_addr, S5P_FIMV_CONTEXT_MEM_ADDR_V6);
	R(context_mem_size, S5P_FIMV_CONTEXT_MEM_SIZE_V6);
	R(pixel_format, S5P_FIMV_PIXEL_FORMAT_V6);
	R(ret_instance_id, S5P_FIMV_RET_INSTANCE_ID_V6);
	R(error_code, S5P_FIMV_ERROR_CODE_V6);

	/* decoder registers */
	R(d_crc_ctrl, S5P_FIMV_D_CRC_CTRL_V6);
	R(d_dec_options, S5P_FIMV_D_DEC_OPTIONS_V6);
	R(d_display_delay, S5P_FIMV_D_DISPLAY_DELAY_V6);
	R(d_sei_enable, S5P_FIMV_D_SEI_ENABLE_V6);
	R(d_min_num_dpb, S5P_FIMV_D_MIN_NUM_DPB_V6);
	R(d_min_num_mv, S5P_FIMV_D_MIN_NUM_MV_V6);
	R(d_mvc_num_views, S5P_FIMV_D_MVC_NUM_VIEWS_V6);
	R(d_num_dpb, S5P_FIMV_D_NUM_DPB_V6);
	R(d_num_mv, S5P_FIMV_D_NUM_MV_V6);
	R(d_init_buffer_options, S5P_FIMV_D_INIT_BUFFER_OPTIONS_V6);
	R(d_first_plane_dpb_size, S5P_FIMV_D_LUMA_DPB_SIZE_V6);
	R(d_second_plane_dpb_size, S5P_FIMV_D_CHROMA_DPB_SIZE_V6);
	R(d_mv_buffer_size, S5P_FIMV_D_MV_BUFFER_SIZE_V6);
	R(d_first_plane_dpb, S5P_FIMV_D_LUMA_DPB_V6);
	R(d_second_plane_dpb, S5P_FIMV_D_CHROMA_DPB_V6);
	R(d_mv_buffer, S5P_FIMV_D_MV_BUFFER_V6);
	R(d_scratch_buffer_addr, S5P_FIMV_D_SCRATCH_BUFFER_ADDR_V6);
	R(d_scratch_buffer_size, S5P_FIMV_D_SCRATCH_BUFFER_SIZE_V6);
	R(d_cpb_buffer_addr, S5P_FIMV_D_CPB_BUFFER_ADDR_V6);
	R(d_cpb_buffer_size, S5P_FIMV_D_CPB_BUFFER_SIZE_V6);
	R(d_available_dpb_flag_lower, S5P_FIMV_D_AVAILABLE_DPB_FLAG_LOWER_V6);
	R(d_cpb_buffer_offset, S5P_FIMV_D_CPB_BUFFER_OFFSET_V6);
	R(d_slice_if_enable, S5P_FIMV_D_SLICE_IF_ENABLE_V6);
	R(d_stream_data_size, S5P_FIMV_D_STREAM_DATA_SIZE_V6);
	R(d_display_frame_width, S5P_FIMV_D_DISPLAY_FRAME_WIDTH_V6);
	R(d_display_frame_height, S5P_FIMV_D_DISPLAY_FRAME_HEIGHT_V6);
	R(d_display_status, S5P_FIMV_D_DISPLAY_STATUS_V6);
	R(d_display_first_plane_addr, S5P_FIMV_D_DISPLAY_LUMA_ADDR_V6);
	R(d_display_second_plane_addr, S5P_FIMV_D_DISPLAY_CHROMA_ADDR_V6);
	R(d_display_frame_type, S5P_FIMV_D_DISPLAY_FRAME_TYPE_V6);
	R(d_display_crop_info1, S5P_FIMV_D_DISPLAY_CROP_INFO1_V6);
	R(d_display_crop_info2, S5P_FIMV_D_DISPLAY_CROP_INFO2_V6);
	R(d_display_aspect_ratio, S5P_FIMV_D_DISPLAY_ASPECT_RATIO_V6);
	R(d_display_extended_ar, S5P_FIMV_D_DISPLAY_EXTENDED_AR_V6);
	R(d_decoded_status, S5P_FIMV_D_DECODED_STATUS_V6);
	R(d_decoded_first_plane_addr, S5P_FIMV_D_DECODED_LUMA_ADDR_V6);
	R(d_decoded_second_plane_addr, S5P_FIMV_D_DECODED_CHROMA_ADDR_V6);
	R(d_decoded_frame_type, S5P_FIMV_D_DECODED_FRAME_TYPE_V6);
	R(d_decoded_nal_size, S5P_FIMV_D_DECODED_NAL_SIZE_V6);
	R(d_ret_picture_tag_top, S5P_FIMV_D_RET_PICTURE_TAG_TOP_V6);
	R(d_ret_picture_tag_bot, S5P_FIMV_D_RET_PICTURE_TAG_BOT_V6);
	R(d_h264_info, S5P_FIMV_D_H264_INFO_V6);
	R(d_mvc_view_id, S5P_FIMV_D_MVC_VIEW_ID_V6);
	R(d_frame_pack_sei_avail, S5P_FIMV_D_FRAME_PACK_SEI_AVAIL_V6);

	/* encoder registers */
	R(e_frame_width, S5P_FIMV_E_FRAME_WIDTH_V6);
	R(e_frame_height, S5P_FIMV_E_FRAME_HEIGHT_V6);
	R(e_cropped_frame_width, S5P_FIMV_E_CROPPED_FRAME_WIDTH_V6);
	R(e_cropped_frame_height, S5P_FIMV_E_CROPPED_FRAME_HEIGHT_V6);
	R(e_frame_crop_offset, S5P_FIMV_E_FRAME_CROP_OFFSET_V6);
	R(e_enc_options, S5P_FIMV_E_ENC_OPTIONS_V6);
	R(e_picture_profile, S5P_FIMV_E_PICTURE_PROFILE_V6);
	R(e_vbv_buffer_size, S5P_FIMV_E_VBV_BUFFER_SIZE_V6);
	R(e_vbv_init_delay, S5P_FIMV_E_VBV_INIT_DELAY_V6);
	R(e_fixed_picture_qp, S5P_FIMV_E_FIXED_PICTURE_QP_V6);
	R(e_rc_config, S5P_FIMV_E_RC_CONFIG_V6);
	R(e_rc_qp_bound, S5P_FIMV_E_RC_QP_BOUND_V6);
	R(e_rc_mode, S5P_FIMV_E_RC_RPARAM_V6);
	R(e_mb_rc_config, S5P_FIMV_E_MB_RC_CONFIG_V6);
	R(e_padding_ctrl, S5P_FIMV_E_PADDING_CTRL_V6);
	R(e_mv_hor_range, S5P_FIMV_E_MV_HOR_RANGE_V6);
	R(e_mv_ver_range, S5P_FIMV_E_MV_VER_RANGE_V6);
	R(e_num_dpb, S5P_FIMV_E_NUM_DPB_V6);
	R(e_luma_dpb, S5P_FIMV_E_LUMA_DPB_V6);
	R(e_chroma_dpb, S5P_FIMV_E_CHROMA_DPB_V6);
	R(e_me_buffer, S5P_FIMV_E_ME_BUFFER_V6);
	R(e_scratch_buffer_addr, S5P_FIMV_E_SCRATCH_BUFFER_ADDR_V6);
	R(e_scratch_buffer_size, S5P_FIMV_E_SCRATCH_BUFFER_SIZE_V6);
	R(e_tmv_buffer0, S5P_FIMV_E_TMV_BUFFER0_V6);
	R(e_tmv_buffer1, S5P_FIMV_E_TMV_BUFFER1_V6);
	R(e_source_first_plane_addr, S5P_FIMV_E_SOURCE_LUMA_ADDR_V6);
	R(e_source_second_plane_addr, S5P_FIMV_E_SOURCE_CHROMA_ADDR_V6);
	R(e_stream_buffer_addr, S5P_FIMV_E_STREAM_BUFFER_ADDR_V6);
	R(e_stream_buffer_size, S5P_FIMV_E_STREAM_BUFFER_SIZE_V6);
	R(e_roi_buffer_addr, S5P_FIMV_E_ROI_BUFFER_ADDR_V6);
	R(e_param_change, S5P_FIMV_E_PARAM_CHANGE_V6);
	R(e_ir_size, S5P_FIMV_E_IR_SIZE_V6);
	R(e_gop_config, S5P_FIMV_E_GOP_CONFIG_V6);
	R(e_mslice_mode, S5P_FIMV_E_MSLICE_MODE_V6);
	R(e_mslice_size_mb, S5P_FIMV_E_MSLICE_SIZE_MB_V6);
	R(e_mslice_size_bits, S5P_FIMV_E_MSLICE_SIZE_BITS_V6);
	R(e_frame_insertion, S5P_FIMV_E_FRAME_INSERTION_V6);
	R(e_rc_frame_rate, S5P_FIMV_E_RC_FRAME_RATE_V6);
	R(e_rc_bit_rate, S5P_FIMV_E_RC_BIT_RATE_V6);
	R(e_rc_roi_ctrl, S5P_FIMV_E_RC_ROI_CTRL_V6);
	R(e_picture_tag, S5P_FIMV_E_PICTURE_TAG_V6);
	R(e_bit_count_enable, S5P_FIMV_E_BIT_COUNT_ENABLE_V6);
	R(e_max_bit_count, S5P_FIMV_E_MAX_BIT_COUNT_V6);
	R(e_min_bit_count, S5P_FIMV_E_MIN_BIT_COUNT_V6);
	R(e_metadata_buffer_addr, S5P_FIMV_E_METADATA_BUFFER_ADDR_V6);
	R(e_metadata_buffer_size, S5P_FIMV_E_METADATA_BUFFER_SIZE_V6);
	R(e_encoded_source_first_plane_addr,
			S5P_FIMV_E_ENCODED_SOURCE_LUMA_ADDR_V6);
	R(e_encoded_source_second_plane_addr,
			S5P_FIMV_E_ENCODED_SOURCE_CHROMA_ADDR_V6);
	R(e_stream_size, S5P_FIMV_E_STREAM_SIZE_V6);
	R(e_slice_type, S5P_FIMV_E_SLICE_TYPE_V6);
	R(e_picture_count, S5P_FIMV_E_PICTURE_COUNT_V6);
	R(e_ret_picture_tag, S5P_FIMV_E_RET_PICTURE_TAG_V6);
	R(e_recon_luma_dpb_addr, S5P_FIMV_E_RECON_LUMA_DPB_ADDR_V6);
	R(e_recon_chroma_dpb_addr, S5P_FIMV_E_RECON_CHROMA_DPB_ADDR_V6);
	R(e_mpeg4_options, S5P_FIMV_E_MPEG4_OPTIONS_V6);
	R(e_mpeg4_hec_period, S5P_FIMV_E_MPEG4_HEC_PERIOD_V6);
	R(e_aspect_ratio, S5P_FIMV_E_ASPECT_RATIO_V6);
	R(e_extended_sar, S5P_FIMV_E_EXTENDED_SAR_V6);
	R(e_h264_options, S5P_FIMV_E_H264_OPTIONS_V6);
	R(e_h264_lf_alpha_offset, S5P_FIMV_E_H264_LF_ALPHA_OFFSET_V6);
	R(e_h264_lf_beta_offset, S5P_FIMV_E_H264_LF_BETA_OFFSET_V6);
	R(e_h264_i_period, S5P_FIMV_E_H264_I_PERIOD_V6);
	R(e_h264_fmo_slice_grp_map_type,
			S5P_FIMV_E_H264_FMO_SLICE_GRP_MAP_TYPE_V6);
	R(e_h264_fmo_num_slice_grp_minus1,
			S5P_FIMV_E_H264_FMO_NUM_SLICE_GRP_MINUS1_V6);
	R(e_h264_fmo_slice_grp_change_dir,
			S5P_FIMV_E_H264_FMO_SLICE_GRP_CHANGE_DIR_V6);
	R(e_h264_fmo_slice_grp_change_rate_minus1,
			S5P_FIMV_E_H264_FMO_SLICE_GRP_CHANGE_RATE_MINUS1_V6);
	R(e_h264_fmo_run_length_minus1_0,
			S5P_FIMV_E_H264_FMO_RUN_LENGTH_MINUS1_0_V6);
	R(e_h264_aso_slice_order_0, S5P_FIMV_E_H264_ASO_SLICE_ORDER_0_V6);
	R(e_h264_num_t_layer, S5P_FIMV_E_H264_NUM_T_LAYER_V6);
	R(e_h264_hierarchical_qp_layer0,
			S5P_FIMV_E_H264_HIERARCHICAL_QP_LAYER0_V6);
	R(e_h264_frame_packing_sei_info,
			S5P_FIMV_E_H264_FRAME_PACKING_SEI_INFO_V6);

	if (!IS_MFCV7_PLUS(dev))
		goto done;

	/* Initialize registers used in MFC v7+ */
	R(e_source_first_plane_addr, S5P_FIMV_E_SOURCE_FIRST_ADDR_V7);
	R(e_source_second_plane_addr, S5P_FIMV_E_SOURCE_SECOND_ADDR_V7);
	R(e_source_third_plane_addr, S5P_FIMV_E_SOURCE_THIRD_ADDR_V7);
	R(e_source_first_plane_stride, S5P_FIMV_E_SOURCE_FIRST_STRIDE_V7);
	R(e_source_second_plane_stride, S5P_FIMV_E_SOURCE_SECOND_STRIDE_V7);
	R(e_source_third_plane_stride, S5P_FIMV_E_SOURCE_THIRD_STRIDE_V7);
	R(e_encoded_source_first_plane_addr,
			S5P_FIMV_E_ENCODED_SOURCE_FIRST_ADDR_V7);
	R(e_encoded_source_second_plane_addr,
			S5P_FIMV_E_ENCODED_SOURCE_SECOND_ADDR_V7);
	R(e_vp8_options, S5P_FIMV_E_VP8_OPTIONS_V7);

	if (!IS_MFCV8_PLUS(dev))
		goto done;

	/* Initialize registers used in MFC v8 only.
	 * Also, over-write the registers which have
	 * a different offset for MFC v8. */
	R(d_stream_data_size, S5P_FIMV_D_STREAM_DATA_SIZE_V8);
	R(d_cpb_buffer_addr, S5P_FIMV_D_CPB_BUFFER_ADDR_V8);
	R(d_cpb_buffer_size, S5P_FIMV_D_CPB_BUFFER_SIZE_V8);
	R(d_cpb_buffer_offset, S5P_FIMV_D_CPB_BUFFER_OFFSET_V8);
	R(d_first_plane_dpb_size, S5P_FIMV_D_FIRST_PLANE_DPB_SIZE_V8);
	R(d_second_plane_dpb_size, S5P_FIMV_D_SECOND_PLANE_DPB_SIZE_V8);
	R(d_scratch_buffer_addr, S5P_FIMV_D_SCRATCH_BUFFER_ADDR_V8);
	R(d_scratch_buffer_size, S5P_FIMV_D_SCRATCH_BUFFER_SIZE_V8);
	R(d_first_plane_dpb_stride_size,
			S5P_FIMV_D_FIRST_PLANE_DPB_STRIDE_SIZE_V8);
	R(d_second_plane_dpb_stride_size,
			S5P_FIMV_D_SECOND_PLANE_DPB_STRIDE_SIZE_V8);
	R(d_mv_buffer_size, S5P_FIMV_D_MV_BUFFER_SIZE_V8);
	R(d_num_mv, S5P_FIMV_D_NUM_MV_V8);
	R(d_first_plane_dpb, S5P_FIMV_D_FIRST_PLANE_DPB_V8);
	R(d_second_plane_dpb, S5P_FIMV_D_SECOND_PLANE_DPB_V8);
	R(d_mv_buffer, S5P_FIMV_D_MV_BUFFER_V8);
	R(d_init_buffer_options, S5P_FIMV_D_INIT_BUFFER_OPTIONS_V8);
	R(d_available_dpb_flag_lower, S5P_FIMV_D_AVAILABLE_DPB_FLAG_LOWER_V8);
	R(d_slice_if_enable, S5P_FIMV_D_SLICE_IF_ENABLE_V8);
	R(d_display_first_plane_addr, S5P_FIMV_D_DISPLAY_FIRST_PLANE_ADDR_V8);
	R(d_display_second_plane_addr, S5P_FIMV_D_DISPLAY_SECOND_PLANE_ADDR_V8);
	R(d_decoded_first_plane_addr, S5P_FIMV_D_DECODED_FIRST_PLANE_ADDR_V8);
	R(d_decoded_second_plane_addr, S5P_FIMV_D_DECODED_SECOND_PLANE_ADDR_V8);
	R(d_display_status, S5P_FIMV_D_DISPLAY_STATUS_V8);
	R(d_decoded_status, S5P_FIMV_D_DECODED_STATUS_V8);
	R(d_decoded_frame_type, S5P_FIMV_D_DECODED_FRAME_TYPE_V8);
	R(d_display_frame_type, S5P_FIMV_D_DISPLAY_FRAME_TYPE_V8);
	R(d_decoded_nal_size, S5P_FIMV_D_DECODED_NAL_SIZE_V8);
	R(d_display_frame_width, S5P_FIMV_D_DISPLAY_FRAME_WIDTH_V8);
	R(d_display_frame_height, S5P_FIMV_D_DISPLAY_FRAME_HEIGHT_V8);
	R(d_frame_pack_sei_avail, S5P_FIMV_D_FRAME_PACK_SEI_AVAIL_V8);
	R(d_mvc_num_views, S5P_FIMV_D_MVC_NUM_VIEWS_V8);
	R(d_mvc_view_id, S5P_FIMV_D_MVC_VIEW_ID_V8);
	R(d_ret_picture_tag_top, S5P_FIMV_D_RET_PICTURE_TAG_TOP_V8);
	R(d_ret_picture_tag_bot, S5P_FIMV_D_RET_PICTURE_TAG_BOT_V8);
	R(d_display_crop_info1, S5P_FIMV_D_DISPLAY_CROP_INFO1_V8);
	R(d_display_crop_info2, S5P_FIMV_D_DISPLAY_CROP_INFO2_V8);
	R(d_min_scratch_buffer_size, S5P_FIMV_D_MIN_SCRATCH_BUFFER_SIZE_V8);

	/* encoder registers */
	R(e_padding_ctrl, S5P_FIMV_E_PADDING_CTRL_V8);
	R(e_rc_config, S5P_FIMV_E_RC_CONFIG_V8);
	R(e_rc_mode, S5P_FIMV_E_RC_RPARAM_V8);
	R(e_mv_hor_range, S5P_FIMV_E_MV_HOR_RANGE_V8);
	R(e_mv_ver_range, S5P_FIMV_E_MV_VER_RANGE_V8);
	R(e_rc_qp_bound, S5P_FIMV_E_RC_QP_BOUND_V8);
	R(e_fixed_picture_qp, S5P_FIMV_E_FIXED_PICTURE_QP_V8);
	R(e_vbv_buffer_size, S5P_FIMV_E_VBV_BUFFER_SIZE_V8);
	R(e_vbv_init_delay, S5P_FIMV_E_VBV_INIT_DELAY_V8);
	R(e_mb_rc_config, S5P_FIMV_E_MB_RC_CONFIG_V8);
	R(e_aspect_ratio, S5P_FIMV_E_ASPECT_RATIO_V8);
	R(e_extended_sar, S5P_FIMV_E_EXTENDED_SAR_V8);
	R(e_h264_options, S5P_FIMV_E_H264_OPTIONS_V8);
	R(e_min_scratch_buffer_size, S5P_FIMV_E_MIN_SCRATCH_BUFFER_SIZE_V8);

	if (!IS_MFCV10(dev))
		goto done;

	/* Initialize registers used in MFC v10 only.
	 * Also, over-write the registers which have
	 * a different offset for MFC v10.
	 */

	/* decoder registers */
	R(d_static_buffer_addr, S5P_FIMV_D_STATIC_BUFFER_ADDR_V10);
	R(d_static_buffer_size, S5P_FIMV_D_STATIC_BUFFER_SIZE_V10);

	/* encoder registers */
	R(e_num_t_layer, S5P_FIMV_E_NUM_T_LAYER_V10);
	R(e_hier_qp_layer0, S5P_FIMV_E_HIERARCHICAL_QP_LAYER0_V10);
	R(e_hier_bit_rate_layer0, S5P_FIMV_E_HIERARCHICAL_BIT_RATE_LAYER0_V10);
	R(e_hevc_options, S5P_FIMV_E_HEVC_OPTIONS_V10);
	R(e_hevc_refresh_period, S5P_FIMV_E_HEVC_REFRESH_PERIOD_V10);
	R(e_hevc_lf_beta_offset_div2, S5P_FIMV_E_HEVC_LF_BETA_OFFSET_DIV2_V10);
	R(e_hevc_lf_tc_offset_div2, S5P_FIMV_E_HEVC_LF_TC_OFFSET_DIV2_V10);
	R(e_hevc_nal_control, S5P_FIMV_E_HEVC_NAL_CONTROL_V10);

done:
	return &mfc_regs;
#undef S5P_MFC_REG_ADDR
#undef R
}

/* Initialize opr function pointers for MFC v6 */
static struct s5p_mfc_hw_ops s5p_mfc_ops_v6 = {
	.alloc_dec_temp_buffers = s5p_mfc_alloc_dec_temp_buffers_v6,
	.release_dec_desc_buffer = s5p_mfc_release_dec_desc_buffer_v6,
	.alloc_codec_buffers = s5p_mfc_alloc_codec_buffers_v6,
	.release_codec_buffers = s5p_mfc_release_codec_buffers_v6,
	.alloc_instance_buffer = s5p_mfc_alloc_instance_buffer_v6,
	.release_instance_buffer = s5p_mfc_release_instance_buffer_v6,
	.alloc_dev_context_buffer =
		s5p_mfc_alloc_dev_context_buffer_v6,
	.release_dev_context_buffer =
		s5p_mfc_release_dev_context_buffer_v6,
	.dec_calc_dpb_size = s5p_mfc_dec_calc_dpb_size_v6,
	.enc_calc_src_size = s5p_mfc_enc_calc_src_size_v6,
	.set_enc_stream_buffer = s5p_mfc_set_enc_stream_buffer_v6,
	.set_enc_frame_buffer = s5p_mfc_set_enc_frame_buffer_v6,
	.get_enc_frame_buffer = s5p_mfc_get_enc_frame_buffer_v6,
	.try_run = s5p_mfc_try_run_v6,
	.clear_int_flags = s5p_mfc_clear_int_flags_v6,
	.get_dspl_y_adr = s5p_mfc_get_dspl_y_adr_v6,
	.get_dec_y_adr = s5p_mfc_get_dec_y_adr_v6,
	.get_dspl_status = s5p_mfc_get_dspl_status_v6,
	.get_dec_status = s5p_mfc_get_dec_status_v6,
	.get_dec_frame_type = s5p_mfc_get_dec_frame_type_v6,
	.get_disp_frame_type = s5p_mfc_get_disp_frame_type_v6,
	.get_consumed_stream = s5p_mfc_get_consumed_stream_v6,
	.get_int_reason = s5p_mfc_get_int_reason_v6,
	.get_int_err = s5p_mfc_get_int_err_v6,
	.err_dec = s5p_mfc_err_dec_v6,
	.get_img_width = s5p_mfc_get_img_width_v6,
	.get_img_height = s5p_mfc_get_img_height_v6,
	.get_dpb_count = s5p_mfc_get_dpb_count_v6,
	.get_mv_count = s5p_mfc_get_mv_count_v6,
	.get_inst_no = s5p_mfc_get_inst_no_v6,
	.get_enc_strm_size = s5p_mfc_get_enc_strm_size_v6,
	.get_enc_slice_type = s5p_mfc_get_enc_slice_type_v6,
	.get_enc_dpb_count = s5p_mfc_get_enc_dpb_count_v6,
	.get_pic_type_top = s5p_mfc_get_pic_type_top_v6,
	.get_pic_type_bot = s5p_mfc_get_pic_type_bot_v6,
	.get_crop_info_h = s5p_mfc_get_crop_info_h_v6,
	.get_crop_info_v = s5p_mfc_get_crop_info_v_v6,
	.get_min_scratch_buf_size = s5p_mfc_get_min_scratch_buf_size,
	.get_e_min_scratch_buf_size = s5p_mfc_get_e_min_scratch_buf_size,
};

struct s5p_mfc_hw_ops *s5p_mfc_init_hw_ops_v6(void)
{
	return &s5p_mfc_ops_v6;
}
