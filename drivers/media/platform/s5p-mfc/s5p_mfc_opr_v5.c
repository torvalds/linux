/*
 * drivers/media/platform/samsung/mfc5/s5p_mfc_opr_v5.c
 *
 * Samsung MFC (Multi Function Codec - FIMV) driver
 * This file contains hw related functions.
 *
 * Kamil Debski, Copyright (c) 2011 Samsung Electronics
 * http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "s5p_mfc_common.h"
#include "s5p_mfc_cmd.h"
#include "s5p_mfc_ctrl.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_intr.h"
#include "s5p_mfc_pm.h"
#include "s5p_mfc_opr.h"
#include "s5p_mfc_opr_v5.h"
#include <asm/cacheflush.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/mm.h>
#include <linux/sched.h>

#define OFFSETA(x)		(((x) - dev->bank1) >> MFC_OFFSET_SHIFT)
#define OFFSETB(x)		(((x) - dev->bank2) >> MFC_OFFSET_SHIFT)

/* Allocate temporary buffers for decoding */
static int s5p_mfc_alloc_dec_temp_buffers_v5(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf_size_v5 *buf_size = dev->variant->buf_size->priv;
	int ret;

	ctx->dsc.size = buf_size->dsc;
	ret =  s5p_mfc_alloc_priv_buf(dev->mem_dev_l, dev->bank1, &ctx->dsc);
	if (ret) {
		mfc_err("Failed to allocate temporary buffer\n");
		return ret;
	}

	BUG_ON(ctx->dsc.dma & ((1 << MFC_BANK1_ALIGN_ORDER) - 1));
	memset(ctx->dsc.virt, 0, ctx->dsc.size);
	wmb();
	return 0;
}


/* Release temporary buffers for decoding */
static void s5p_mfc_release_dec_desc_buffer_v5(struct s5p_mfc_ctx *ctx)
{
	s5p_mfc_release_priv_buf(ctx->dev->mem_dev_l, &ctx->dsc);
}

/* Allocate codec buffers */
static int s5p_mfc_alloc_codec_buffers_v5(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned int enc_ref_y_size = 0;
	unsigned int enc_ref_c_size = 0;
	unsigned int guard_width, guard_height;
	int ret;

	if (ctx->type == MFCINST_DECODER) {
		mfc_debug(2, "Luma size:%d Chroma size:%d MV size:%d\n",
			  ctx->luma_size, ctx->chroma_size, ctx->mv_size);
		mfc_debug(2, "Totals bufs: %d\n", ctx->total_dpb_count);
	} else if (ctx->type == MFCINST_ENCODER) {
		enc_ref_y_size = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN)
			* ALIGN(ctx->img_height, S5P_FIMV_NV12MT_VALIGN);
		enc_ref_y_size = ALIGN(enc_ref_y_size, S5P_FIMV_NV12MT_SALIGN);

		if (ctx->codec_mode == S5P_MFC_CODEC_H264_ENC) {
			enc_ref_c_size = ALIGN(ctx->img_width,
						S5P_FIMV_NV12MT_HALIGN)
						* ALIGN(ctx->img_height >> 1,
						S5P_FIMV_NV12MT_VALIGN);
			enc_ref_c_size = ALIGN(enc_ref_c_size,
							S5P_FIMV_NV12MT_SALIGN);
		} else {
			guard_width = ALIGN(ctx->img_width + 16,
							S5P_FIMV_NV12MT_HALIGN);
			guard_height = ALIGN((ctx->img_height >> 1) + 4,
							S5P_FIMV_NV12MT_VALIGN);
			enc_ref_c_size = ALIGN(guard_width * guard_height,
					       S5P_FIMV_NV12MT_SALIGN);
		}
		mfc_debug(2, "recon luma size: %d chroma size: %d\n",
			  enc_ref_y_size, enc_ref_c_size);
	} else {
		return -EINVAL;
	}
	/* Codecs have different memory requirements */
	switch (ctx->codec_mode) {
	case S5P_MFC_CODEC_H264_DEC:
		ctx->bank1.size =
		    ALIGN(S5P_FIMV_DEC_NB_IP_SIZE +
					S5P_FIMV_DEC_VERT_NB_MV_SIZE,
					S5P_FIMV_DEC_BUF_ALIGN);
		ctx->bank2.size = ctx->total_dpb_count * ctx->mv_size;
		break;
	case S5P_MFC_CODEC_MPEG4_DEC:
		ctx->bank1.size =
		    ALIGN(S5P_FIMV_DEC_NB_DCAC_SIZE +
				     S5P_FIMV_DEC_UPNB_MV_SIZE +
				     S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE +
				     S5P_FIMV_DEC_STX_PARSER_SIZE +
				     S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE,
				     S5P_FIMV_DEC_BUF_ALIGN);
		ctx->bank2.size = 0;
		break;
	case S5P_MFC_CODEC_VC1RCV_DEC:
	case S5P_MFC_CODEC_VC1_DEC:
		ctx->bank1.size =
		    ALIGN(S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE +
			     S5P_FIMV_DEC_UPNB_MV_SIZE +
			     S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE +
			     S5P_FIMV_DEC_NB_DCAC_SIZE +
			     3 * S5P_FIMV_DEC_VC1_BITPLANE_SIZE,
			     S5P_FIMV_DEC_BUF_ALIGN);
		ctx->bank2.size = 0;
		break;
	case S5P_MFC_CODEC_MPEG2_DEC:
		ctx->bank1.size = 0;
		ctx->bank2.size = 0;
		break;
	case S5P_MFC_CODEC_H263_DEC:
		ctx->bank1.size =
		    ALIGN(S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE +
			     S5P_FIMV_DEC_UPNB_MV_SIZE +
			     S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE +
			     S5P_FIMV_DEC_NB_DCAC_SIZE,
			     S5P_FIMV_DEC_BUF_ALIGN);
		ctx->bank2.size = 0;
		break;
	case S5P_MFC_CODEC_H264_ENC:
		ctx->bank1.size = (enc_ref_y_size * 2) +
				   S5P_FIMV_ENC_UPMV_SIZE +
				   S5P_FIMV_ENC_COLFLG_SIZE +
				   S5P_FIMV_ENC_INTRAMD_SIZE +
				   S5P_FIMV_ENC_NBORINFO_SIZE;
		ctx->bank2.size = (enc_ref_y_size * 2) +
				   (enc_ref_c_size * 4) +
				   S5P_FIMV_ENC_INTRAPRED_SIZE;
		break;
	case S5P_MFC_CODEC_MPEG4_ENC:
		ctx->bank1.size = (enc_ref_y_size * 2) +
				   S5P_FIMV_ENC_UPMV_SIZE +
				   S5P_FIMV_ENC_COLFLG_SIZE +
				   S5P_FIMV_ENC_ACDCCOEF_SIZE;
		ctx->bank2.size = (enc_ref_y_size * 2) +
				   (enc_ref_c_size * 4);
		break;
	case S5P_MFC_CODEC_H263_ENC:
		ctx->bank1.size = (enc_ref_y_size * 2) +
				   S5P_FIMV_ENC_UPMV_SIZE +
				   S5P_FIMV_ENC_ACDCCOEF_SIZE;
		ctx->bank2.size = (enc_ref_y_size * 2) +
				   (enc_ref_c_size * 4);
		break;
	default:
		break;
	}
	/* Allocate only if memory from bank 1 is necessary */
	if (ctx->bank1.size > 0) {

		ret = s5p_mfc_alloc_priv_buf(dev->mem_dev_l, dev->bank1,
					     &ctx->bank1);
		if (ret) {
			mfc_err("Failed to allocate Bank1 temporary buffer\n");
			return ret;
		}
		BUG_ON(ctx->bank1.dma & ((1 << MFC_BANK1_ALIGN_ORDER) - 1));
	}
	/* Allocate only if memory from bank 2 is necessary */
	if (ctx->bank2.size > 0) {
		ret = s5p_mfc_alloc_priv_buf(dev->mem_dev_r, dev->bank2,
					     &ctx->bank2);
		if (ret) {
			mfc_err("Failed to allocate Bank2 temporary buffer\n");
			s5p_mfc_release_priv_buf(ctx->dev->mem_dev_l, &ctx->bank1);
			return ret;
		}
		BUG_ON(ctx->bank2.dma & ((1 << MFC_BANK2_ALIGN_ORDER) - 1));
	}
	return 0;
}

/* Release buffers allocated for codec */
static void s5p_mfc_release_codec_buffers_v5(struct s5p_mfc_ctx *ctx)
{
	s5p_mfc_release_priv_buf(ctx->dev->mem_dev_l, &ctx->bank1);
	s5p_mfc_release_priv_buf(ctx->dev->mem_dev_r, &ctx->bank2);
}

/* Allocate memory for instance data buffer */
static int s5p_mfc_alloc_instance_buffer_v5(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf_size_v5 *buf_size = dev->variant->buf_size->priv;
	int ret;

	if (ctx->codec_mode == S5P_MFC_CODEC_H264_DEC ||
		ctx->codec_mode == S5P_MFC_CODEC_H264_ENC)
		ctx->ctx.size = buf_size->h264_ctx;
	else
		ctx->ctx.size = buf_size->non_h264_ctx;

	ret = s5p_mfc_alloc_priv_buf(dev->mem_dev_l, dev->bank1, &ctx->ctx);
	if (ret) {
		mfc_err("Failed to allocate instance buffer\n");
		return ret;
	}
	ctx->ctx.ofs = OFFSETA(ctx->ctx.dma);

	/* Zero content of the allocated memory */
	memset(ctx->ctx.virt, 0, ctx->ctx.size);
	wmb();

	/* Initialize shared memory */
	ctx->shm.size = buf_size->shm;
	ret = s5p_mfc_alloc_priv_buf(dev->mem_dev_l, dev->bank1, &ctx->shm);
	if (ret) {
		mfc_err("Failed to allocate shared memory buffer\n");
		s5p_mfc_release_priv_buf(dev->mem_dev_l, &ctx->ctx);
		return ret;
	}

	/* shared memory offset only keeps the offset from base (port a) */
	ctx->shm.ofs = ctx->shm.dma - dev->bank1;
	BUG_ON(ctx->shm.ofs & ((1 << MFC_BANK1_ALIGN_ORDER) - 1));

	memset(ctx->shm.virt, 0, buf_size->shm);
	wmb();
	return 0;
}

/* Release instance buffer */
static void s5p_mfc_release_instance_buffer_v5(struct s5p_mfc_ctx *ctx)
{
	s5p_mfc_release_priv_buf(ctx->dev->mem_dev_l, &ctx->ctx);
	s5p_mfc_release_priv_buf(ctx->dev->mem_dev_l, &ctx->shm);
}

static int s5p_mfc_alloc_dev_context_buffer_v5(struct s5p_mfc_dev *dev)
{
	/* NOP */

	return 0;
}

static void s5p_mfc_release_dev_context_buffer_v5(struct s5p_mfc_dev *dev)
{
	/* NOP */
}

static void s5p_mfc_write_info_v5(struct s5p_mfc_ctx *ctx, unsigned int data,
			unsigned int ofs)
{
	*(u32 *)(ctx->shm.virt + ofs) = data;
	wmb();
}

static unsigned int s5p_mfc_read_info_v5(struct s5p_mfc_ctx *ctx,
				unsigned long ofs)
{
	rmb();
	return *(u32 *)(ctx->shm.virt + ofs);
}

static void s5p_mfc_dec_calc_dpb_size_v5(struct s5p_mfc_ctx *ctx)
{
	unsigned int guard_width, guard_height;

	ctx->buf_width = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN);
	ctx->buf_height = ALIGN(ctx->img_height, S5P_FIMV_NV12MT_VALIGN);
	mfc_debug(2,
		"SEQ Done: Movie dimensions %dx%d, buffer dimensions: %dx%d\n",
		ctx->img_width,	ctx->img_height, ctx->buf_width,
		ctx->buf_height);

	if (ctx->codec_mode == S5P_MFC_CODEC_H264_DEC) {
		ctx->luma_size = ALIGN(ctx->buf_width * ctx->buf_height,
				S5P_FIMV_DEC_BUF_ALIGN);
		ctx->chroma_size = ALIGN(ctx->buf_width *
				ALIGN((ctx->img_height >> 1),
					S5P_FIMV_NV12MT_VALIGN),
				S5P_FIMV_DEC_BUF_ALIGN);
		ctx->mv_size = ALIGN(ctx->buf_width *
				ALIGN((ctx->buf_height >> 2),
					S5P_FIMV_NV12MT_VALIGN),
				S5P_FIMV_DEC_BUF_ALIGN);
	} else {
		guard_width =
			ALIGN(ctx->img_width + 24, S5P_FIMV_NV12MT_HALIGN);
		guard_height =
			ALIGN(ctx->img_height + 16, S5P_FIMV_NV12MT_VALIGN);
		ctx->luma_size = ALIGN(guard_width * guard_height,
				S5P_FIMV_DEC_BUF_ALIGN);

		guard_width =
			ALIGN(ctx->img_width + 16, S5P_FIMV_NV12MT_HALIGN);
		guard_height =
			ALIGN((ctx->img_height >> 1) + 4,
					S5P_FIMV_NV12MT_VALIGN);
		ctx->chroma_size = ALIGN(guard_width * guard_height,
				S5P_FIMV_DEC_BUF_ALIGN);

		ctx->mv_size = 0;
	}
}

static void s5p_mfc_enc_calc_src_size_v5(struct s5p_mfc_ctx *ctx)
{
	if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12M) {
		ctx->buf_width = ALIGN(ctx->img_width, S5P_FIMV_NV12M_HALIGN);

		ctx->luma_size = ALIGN(ctx->img_width, S5P_FIMV_NV12M_HALIGN)
			* ALIGN(ctx->img_height, S5P_FIMV_NV12M_LVALIGN);
		ctx->chroma_size = ALIGN(ctx->img_width, S5P_FIMV_NV12M_HALIGN)
			* ALIGN((ctx->img_height >> 1), S5P_FIMV_NV12M_CVALIGN);

		ctx->luma_size = ALIGN(ctx->luma_size, S5P_FIMV_NV12M_SALIGN);
		ctx->chroma_size =
			ALIGN(ctx->chroma_size, S5P_FIMV_NV12M_SALIGN);
	} else if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12MT) {
		ctx->buf_width = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN);

		ctx->luma_size = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN)
			* ALIGN(ctx->img_height, S5P_FIMV_NV12MT_VALIGN);
		ctx->chroma_size =
			ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN)
			* ALIGN((ctx->img_height >> 1), S5P_FIMV_NV12MT_VALIGN);

		ctx->luma_size = ALIGN(ctx->luma_size, S5P_FIMV_NV12MT_SALIGN);
		ctx->chroma_size =
			ALIGN(ctx->chroma_size, S5P_FIMV_NV12MT_SALIGN);
	}
}

/* Set registers for decoding temporary buffers */
static void s5p_mfc_set_dec_desc_buffer(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf_size_v5 *buf_size = dev->variant->buf_size->priv;

	mfc_write(dev, OFFSETA(ctx->dsc.dma), S5P_FIMV_SI_CH0_DESC_ADR);
	mfc_write(dev, buf_size->dsc, S5P_FIMV_SI_CH0_DESC_SIZE);
}

/* Set registers for shared buffer */
static void s5p_mfc_set_shared_buffer(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	mfc_write(dev, ctx->shm.ofs, S5P_FIMV_SI_CH0_HOST_WR_ADR);
}

/* Set registers for decoding stream buffer */
static int s5p_mfc_set_dec_stream_buffer_v5(struct s5p_mfc_ctx *ctx,
		int buf_addr, unsigned int start_num_byte,
		unsigned int buf_size)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	mfc_write(dev, OFFSETA(buf_addr), S5P_FIMV_SI_CH0_SB_ST_ADR);
	mfc_write(dev, ctx->dec_src_buf_size, S5P_FIMV_SI_CH0_CPB_SIZE);
	mfc_write(dev, buf_size, S5P_FIMV_SI_CH0_SB_FRM_SIZE);
	s5p_mfc_write_info_v5(ctx, start_num_byte, START_BYTE_NUM);
	return 0;
}

/* Set decoding frame buffer */
static int s5p_mfc_set_dec_frame_buffer_v5(struct s5p_mfc_ctx *ctx)
{
	unsigned int frame_size_lu, i;
	unsigned int frame_size_ch, frame_size_mv;
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned int dpb;
	size_t buf_addr1, buf_addr2;
	int buf_size1, buf_size2;

	buf_addr1 = ctx->bank1.dma;
	buf_size1 = ctx->bank1.size;
	buf_addr2 = ctx->bank2.dma;
	buf_size2 = ctx->bank2.size;
	dpb = mfc_read(dev, S5P_FIMV_SI_CH0_DPB_CONF_CTRL) &
						~S5P_FIMV_DPB_COUNT_MASK;
	mfc_write(dev, ctx->total_dpb_count | dpb,
						S5P_FIMV_SI_CH0_DPB_CONF_CTRL);
	s5p_mfc_set_shared_buffer(ctx);
	switch (ctx->codec_mode) {
	case S5P_MFC_CODEC_H264_DEC:
		mfc_write(dev, OFFSETA(buf_addr1),
						S5P_FIMV_H264_VERT_NB_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_VERT_NB_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_VERT_NB_MV_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_H264_NB_IP_ADR);
		buf_addr1 += S5P_FIMV_DEC_NB_IP_SIZE;
		buf_size1 -= S5P_FIMV_DEC_NB_IP_SIZE;
		break;
	case S5P_MFC_CODEC_MPEG4_DEC:
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_MPEG4_NB_DCAC_ADR);
		buf_addr1 += S5P_FIMV_DEC_NB_DCAC_SIZE;
		buf_size1 -= S5P_FIMV_DEC_NB_DCAC_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_MPEG4_UP_NB_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_UPNB_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_UPNB_MV_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_MPEG4_SA_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_MPEG4_SP_ADR);
		buf_addr1 += S5P_FIMV_DEC_STX_PARSER_SIZE;
		buf_size1 -= S5P_FIMV_DEC_STX_PARSER_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_MPEG4_OT_LINE_ADR);
		buf_addr1 += S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE;
		buf_size1 -= S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE;
		break;
	case S5P_MFC_CODEC_H263_DEC:
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_H263_OT_LINE_ADR);
		buf_addr1 += S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE;
		buf_size1 -= S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_H263_UP_NB_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_UPNB_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_UPNB_MV_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_H263_SA_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_H263_NB_DCAC_ADR);
		buf_addr1 += S5P_FIMV_DEC_NB_DCAC_SIZE;
		buf_size1 -= S5P_FIMV_DEC_NB_DCAC_SIZE;
		break;
	case S5P_MFC_CODEC_VC1_DEC:
	case S5P_MFC_CODEC_VC1RCV_DEC:
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_VC1_NB_DCAC_ADR);
		buf_addr1 += S5P_FIMV_DEC_NB_DCAC_SIZE;
		buf_size1 -= S5P_FIMV_DEC_NB_DCAC_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_VC1_OT_LINE_ADR);
		buf_addr1 += S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE;
		buf_size1 -= S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_VC1_UP_NB_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_UPNB_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_UPNB_MV_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_VC1_SA_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_VC1_BITPLANE3_ADR);
		buf_addr1 += S5P_FIMV_DEC_VC1_BITPLANE_SIZE;
		buf_size1 -= S5P_FIMV_DEC_VC1_BITPLANE_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_VC1_BITPLANE2_ADR);
		buf_addr1 += S5P_FIMV_DEC_VC1_BITPLANE_SIZE;
		buf_size1 -= S5P_FIMV_DEC_VC1_BITPLANE_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_VC1_BITPLANE1_ADR);
		buf_addr1 += S5P_FIMV_DEC_VC1_BITPLANE_SIZE;
		buf_size1 -= S5P_FIMV_DEC_VC1_BITPLANE_SIZE;
		break;
	case S5P_MFC_CODEC_MPEG2_DEC:
		break;
	default:
		mfc_err("Unknown codec for decoding (%x)\n",
			ctx->codec_mode);
		return -EINVAL;
	}
	frame_size_lu = ctx->luma_size;
	frame_size_ch = ctx->chroma_size;
	frame_size_mv = ctx->mv_size;
	mfc_debug(2, "Frm size: %d ch: %d mv: %d\n", frame_size_lu, frame_size_ch,
								frame_size_mv);
	for (i = 0; i < ctx->total_dpb_count; i++) {
		/* Bank2 */
		mfc_debug(2, "Luma %d: %zx\n", i,
					ctx->dst_bufs[i].cookie.raw.luma);
		mfc_write(dev, OFFSETB(ctx->dst_bufs[i].cookie.raw.luma),
						S5P_FIMV_DEC_LUMA_ADR + i * 4);
		mfc_debug(2, "\tChroma %d: %zx\n", i,
					ctx->dst_bufs[i].cookie.raw.chroma);
		mfc_write(dev, OFFSETA(ctx->dst_bufs[i].cookie.raw.chroma),
					       S5P_FIMV_DEC_CHROMA_ADR + i * 4);
		if (ctx->codec_mode == S5P_MFC_CODEC_H264_DEC) {
			mfc_debug(2, "\tBuf2: %zx, size: %d\n",
							buf_addr2, buf_size2);
			mfc_write(dev, OFFSETB(buf_addr2),
						S5P_FIMV_H264_MV_ADR + i * 4);
			buf_addr2 += frame_size_mv;
			buf_size2 -= frame_size_mv;
		}
	}
	mfc_debug(2, "Buf1: %zu, buf_size1: %d\n", buf_addr1, buf_size1);
	mfc_debug(2, "Buf 1/2 size after: %d/%d (frames %d)\n",
			buf_size1,  buf_size2, ctx->total_dpb_count);
	if (buf_size1 < 0 || buf_size2 < 0) {
		mfc_debug(2, "Not enough memory has been allocated\n");
		return -ENOMEM;
	}
	s5p_mfc_write_info_v5(ctx, frame_size_lu, ALLOC_LUMA_DPB_SIZE);
	s5p_mfc_write_info_v5(ctx, frame_size_ch, ALLOC_CHROMA_DPB_SIZE);
	if (ctx->codec_mode == S5P_MFC_CODEC_H264_DEC)
		s5p_mfc_write_info_v5(ctx, frame_size_mv, ALLOC_MV_SIZE);
	mfc_write(dev, ((S5P_FIMV_CH_INIT_BUFS & S5P_FIMV_CH_MASK)
					<< S5P_FIMV_CH_SHIFT) | (ctx->inst_no),
						S5P_FIMV_SI_CH0_INST_ID);
	return 0;
}

/* Set registers for encoding stream buffer */
static int s5p_mfc_set_enc_stream_buffer_v5(struct s5p_mfc_ctx *ctx,
		unsigned long addr, unsigned int size)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	mfc_write(dev, OFFSETA(addr), S5P_FIMV_ENC_SI_CH0_SB_ADR);
	mfc_write(dev, size, S5P_FIMV_ENC_SI_CH0_SB_SIZE);
	return 0;
}

static void s5p_mfc_set_enc_frame_buffer_v5(struct s5p_mfc_ctx *ctx,
		unsigned long y_addr, unsigned long c_addr)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	mfc_write(dev, OFFSETB(y_addr), S5P_FIMV_ENC_SI_CH0_CUR_Y_ADR);
	mfc_write(dev, OFFSETB(c_addr), S5P_FIMV_ENC_SI_CH0_CUR_C_ADR);
}

static void s5p_mfc_get_enc_frame_buffer_v5(struct s5p_mfc_ctx *ctx,
		unsigned long *y_addr, unsigned long *c_addr)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	*y_addr = dev->bank2 + (mfc_read(dev, S5P_FIMV_ENCODED_Y_ADDR)
							<< MFC_OFFSET_SHIFT);
	*c_addr = dev->bank2 + (mfc_read(dev, S5P_FIMV_ENCODED_C_ADDR)
							<< MFC_OFFSET_SHIFT);
}

/* Set encoding ref & codec buffer */
static int s5p_mfc_set_enc_ref_buffer_v5(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	size_t buf_addr1, buf_addr2;
	size_t buf_size1, buf_size2;
	unsigned int enc_ref_y_size, enc_ref_c_size;
	unsigned int guard_width, guard_height;
	int i;

	buf_addr1 = ctx->bank1.dma;
	buf_size1 = ctx->bank1.size;
	buf_addr2 = ctx->bank2.dma;
	buf_size2 = ctx->bank2.size;
	enc_ref_y_size = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN)
		* ALIGN(ctx->img_height, S5P_FIMV_NV12MT_VALIGN);
	enc_ref_y_size = ALIGN(enc_ref_y_size, S5P_FIMV_NV12MT_SALIGN);
	if (ctx->codec_mode == S5P_MFC_CODEC_H264_ENC) {
		enc_ref_c_size = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN)
			* ALIGN((ctx->img_height >> 1), S5P_FIMV_NV12MT_VALIGN);
		enc_ref_c_size = ALIGN(enc_ref_c_size, S5P_FIMV_NV12MT_SALIGN);
	} else {
		guard_width = ALIGN(ctx->img_width + 16,
						S5P_FIMV_NV12MT_HALIGN);
		guard_height = ALIGN((ctx->img_height >> 1) + 4,
						S5P_FIMV_NV12MT_VALIGN);
		enc_ref_c_size = ALIGN(guard_width * guard_height,
				       S5P_FIMV_NV12MT_SALIGN);
	}
	mfc_debug(2, "buf_size1: %zu, buf_size2: %zu\n", buf_size1, buf_size2);
	switch (ctx->codec_mode) {
	case S5P_MFC_CODEC_H264_ENC:
		for (i = 0; i < 2; i++) {
			mfc_write(dev, OFFSETA(buf_addr1),
				S5P_FIMV_ENC_REF0_LUMA_ADR + (4 * i));
			buf_addr1 += enc_ref_y_size;
			buf_size1 -= enc_ref_y_size;

			mfc_write(dev, OFFSETB(buf_addr2),
				S5P_FIMV_ENC_REF2_LUMA_ADR + (4 * i));
			buf_addr2 += enc_ref_y_size;
			buf_size2 -= enc_ref_y_size;
		}
		for (i = 0; i < 4; i++) {
			mfc_write(dev, OFFSETB(buf_addr2),
				S5P_FIMV_ENC_REF0_CHROMA_ADR + (4 * i));
			buf_addr2 += enc_ref_c_size;
			buf_size2 -= enc_ref_c_size;
		}
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_H264_UP_MV_ADR);
		buf_addr1 += S5P_FIMV_ENC_UPMV_SIZE;
		buf_size1 -= S5P_FIMV_ENC_UPMV_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1),
					S5P_FIMV_H264_COZERO_FLAG_ADR);
		buf_addr1 += S5P_FIMV_ENC_COLFLG_SIZE;
		buf_size1 -= S5P_FIMV_ENC_COLFLG_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1),
					S5P_FIMV_H264_UP_INTRA_MD_ADR);
		buf_addr1 += S5P_FIMV_ENC_INTRAMD_SIZE;
		buf_size1 -= S5P_FIMV_ENC_INTRAMD_SIZE;
		mfc_write(dev, OFFSETB(buf_addr2),
					S5P_FIMV_H264_UP_INTRA_PRED_ADR);
		buf_addr2 += S5P_FIMV_ENC_INTRAPRED_SIZE;
		buf_size2 -= S5P_FIMV_ENC_INTRAPRED_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1),
					S5P_FIMV_H264_NBOR_INFO_ADR);
		buf_addr1 += S5P_FIMV_ENC_NBORINFO_SIZE;
		buf_size1 -= S5P_FIMV_ENC_NBORINFO_SIZE;
		mfc_debug(2, "buf_size1: %zu, buf_size2: %zu\n",
			buf_size1, buf_size2);
		break;
	case S5P_MFC_CODEC_MPEG4_ENC:
		for (i = 0; i < 2; i++) {
			mfc_write(dev, OFFSETA(buf_addr1),
				S5P_FIMV_ENC_REF0_LUMA_ADR + (4 * i));
			buf_addr1 += enc_ref_y_size;
			buf_size1 -= enc_ref_y_size;
			mfc_write(dev, OFFSETB(buf_addr2),
				S5P_FIMV_ENC_REF2_LUMA_ADR + (4 * i));
			buf_addr2 += enc_ref_y_size;
			buf_size2 -= enc_ref_y_size;
		}
		for (i = 0; i < 4; i++) {
			mfc_write(dev, OFFSETB(buf_addr2),
				S5P_FIMV_ENC_REF0_CHROMA_ADR + (4 * i));
			buf_addr2 += enc_ref_c_size;
			buf_size2 -= enc_ref_c_size;
		}
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_MPEG4_UP_MV_ADR);
		buf_addr1 += S5P_FIMV_ENC_UPMV_SIZE;
		buf_size1 -= S5P_FIMV_ENC_UPMV_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1),
						S5P_FIMV_MPEG4_COZERO_FLAG_ADR);
		buf_addr1 += S5P_FIMV_ENC_COLFLG_SIZE;
		buf_size1 -= S5P_FIMV_ENC_COLFLG_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1),
						S5P_FIMV_MPEG4_ACDC_COEF_ADR);
		buf_addr1 += S5P_FIMV_ENC_ACDCCOEF_SIZE;
		buf_size1 -= S5P_FIMV_ENC_ACDCCOEF_SIZE;
		mfc_debug(2, "buf_size1: %zu, buf_size2: %zu\n",
			buf_size1, buf_size2);
		break;
	case S5P_MFC_CODEC_H263_ENC:
		for (i = 0; i < 2; i++) {
			mfc_write(dev, OFFSETA(buf_addr1),
				S5P_FIMV_ENC_REF0_LUMA_ADR + (4 * i));
			buf_addr1 += enc_ref_y_size;
			buf_size1 -= enc_ref_y_size;
			mfc_write(dev, OFFSETB(buf_addr2),
				S5P_FIMV_ENC_REF2_LUMA_ADR + (4 * i));
			buf_addr2 += enc_ref_y_size;
			buf_size2 -= enc_ref_y_size;
		}
		for (i = 0; i < 4; i++) {
			mfc_write(dev, OFFSETB(buf_addr2),
				S5P_FIMV_ENC_REF0_CHROMA_ADR + (4 * i));
			buf_addr2 += enc_ref_c_size;
			buf_size2 -= enc_ref_c_size;
		}
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_H263_UP_MV_ADR);
		buf_addr1 += S5P_FIMV_ENC_UPMV_SIZE;
		buf_size1 -= S5P_FIMV_ENC_UPMV_SIZE;
		mfc_write(dev, OFFSETA(buf_addr1), S5P_FIMV_H263_ACDC_COEF_ADR);
		buf_addr1 += S5P_FIMV_ENC_ACDCCOEF_SIZE;
		buf_size1 -= S5P_FIMV_ENC_ACDCCOEF_SIZE;
		mfc_debug(2, "buf_size1: %zu, buf_size2: %zu\n",
			buf_size1, buf_size2);
		break;
	default:
		mfc_err("Unknown codec set for encoding: %d\n",
			ctx->codec_mode);
		return -EINVAL;
	}
	return 0;
}

static int s5p_mfc_set_enc_params(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc_params *p = &ctx->enc_params;
	unsigned int reg;
	unsigned int shm;

	/* width */
	mfc_write(dev, ctx->img_width, S5P_FIMV_ENC_HSIZE_PX);
	/* height */
	mfc_write(dev, ctx->img_height, S5P_FIMV_ENC_VSIZE_PX);
	/* pictype : enable, IDR period */
	reg = mfc_read(dev, S5P_FIMV_ENC_PIC_TYPE_CTRL);
	reg |= (1 << 18);
	reg &= ~(0xFFFF);
	reg |= p->gop_size;
	mfc_write(dev, reg, S5P_FIMV_ENC_PIC_TYPE_CTRL);
	mfc_write(dev, 0, S5P_FIMV_ENC_B_RECON_WRITE_ON);
	/* multi-slice control */
	/* multi-slice MB number or bit size */
	mfc_write(dev, p->slice_mode, S5P_FIMV_ENC_MSLICE_CTRL);
	if (p->slice_mode == V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_MB) {
		mfc_write(dev, p->slice_mb, S5P_FIMV_ENC_MSLICE_MB);
	} else if (p->slice_mode == V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_BYTES) {
		mfc_write(dev, p->slice_bit, S5P_FIMV_ENC_MSLICE_BIT);
	} else {
		mfc_write(dev, 0, S5P_FIMV_ENC_MSLICE_MB);
		mfc_write(dev, 0, S5P_FIMV_ENC_MSLICE_BIT);
	}
	/* cyclic intra refresh */
	mfc_write(dev, p->intra_refresh_mb, S5P_FIMV_ENC_CIR_CTRL);
	/* memory structure cur. frame */
	if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12M)
		mfc_write(dev, 0, S5P_FIMV_ENC_MAP_FOR_CUR);
	else if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12MT)
		mfc_write(dev, 3, S5P_FIMV_ENC_MAP_FOR_CUR);
	/* padding control & value */
	reg = mfc_read(dev, S5P_FIMV_ENC_PADDING_CTRL);
	if (p->pad) {
		/** enable */
		reg |= (1 << 31);
		/** cr value */
		reg &= ~(0xFF << 16);
		reg |= (p->pad_cr << 16);
		/** cb value */
		reg &= ~(0xFF << 8);
		reg |= (p->pad_cb << 8);
		/** y value */
		reg &= ~(0xFF);
		reg |= (p->pad_luma);
	} else {
		/** disable & all value clear */
		reg = 0;
	}
	mfc_write(dev, reg, S5P_FIMV_ENC_PADDING_CTRL);
	/* rate control config. */
	reg = mfc_read(dev, S5P_FIMV_ENC_RC_CONFIG);
	/** frame-level rate control */
	reg &= ~(0x1 << 9);
	reg |= (p->rc_frame << 9);
	mfc_write(dev, reg, S5P_FIMV_ENC_RC_CONFIG);
	/* bit rate */
	if (p->rc_frame)
		mfc_write(dev, p->rc_bitrate,
			S5P_FIMV_ENC_RC_BIT_RATE);
	else
		mfc_write(dev, 0, S5P_FIMV_ENC_RC_BIT_RATE);
	/* reaction coefficient */
	if (p->rc_frame)
		mfc_write(dev, p->rc_reaction_coeff, S5P_FIMV_ENC_RC_RPARA);
	shm = s5p_mfc_read_info_v5(ctx, EXT_ENC_CONTROL);
	/* seq header ctrl */
	shm &= ~(0x1 << 3);
	shm |= (p->seq_hdr_mode << 3);
	/* frame skip mode */
	shm &= ~(0x3 << 1);
	shm |= (p->frame_skip_mode << 1);
	s5p_mfc_write_info_v5(ctx, shm, EXT_ENC_CONTROL);
	/* fixed target bit */
	s5p_mfc_write_info_v5(ctx, p->fixed_target_bit, RC_CONTROL_CONFIG);
	return 0;
}

static int s5p_mfc_set_enc_params_h264(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc_params *p = &ctx->enc_params;
	struct s5p_mfc_h264_enc_params *p_264 = &p->codec.h264;
	unsigned int reg;
	unsigned int shm;

	s5p_mfc_set_enc_params(ctx);
	/* pictype : number of B */
	reg = mfc_read(dev, S5P_FIMV_ENC_PIC_TYPE_CTRL);
	/* num_b_frame - 0 ~ 2 */
	reg &= ~(0x3 << 16);
	reg |= (p->num_b_frame << 16);
	mfc_write(dev, reg, S5P_FIMV_ENC_PIC_TYPE_CTRL);
	/* profile & level */
	reg = mfc_read(dev, S5P_FIMV_ENC_PROFILE);
	/* level */
	reg &= ~(0xFF << 8);
	reg |= (p_264->level << 8);
	/* profile - 0 ~ 2 */
	reg &= ~(0x3F);
	reg |= p_264->profile;
	mfc_write(dev, reg, S5P_FIMV_ENC_PROFILE);
	/* interlace  */
	mfc_write(dev, p_264->interlace, S5P_FIMV_ENC_PIC_STRUCT);
	/* height */
	if (p_264->interlace)
		mfc_write(dev, ctx->img_height >> 1, S5P_FIMV_ENC_VSIZE_PX);
	/* loopfilter ctrl */
	mfc_write(dev, p_264->loop_filter_mode, S5P_FIMV_ENC_LF_CTRL);
	/* loopfilter alpha offset */
	if (p_264->loop_filter_alpha < 0) {
		reg = 0x10;
		reg |= (0xFF - p_264->loop_filter_alpha) + 1;
	} else {
		reg = 0x00;
		reg |= (p_264->loop_filter_alpha & 0xF);
	}
	mfc_write(dev, reg, S5P_FIMV_ENC_ALPHA_OFF);
	/* loopfilter beta offset */
	if (p_264->loop_filter_beta < 0) {
		reg = 0x10;
		reg |= (0xFF - p_264->loop_filter_beta) + 1;
	} else {
		reg = 0x00;
		reg |= (p_264->loop_filter_beta & 0xF);
	}
	mfc_write(dev, reg, S5P_FIMV_ENC_BETA_OFF);
	/* entropy coding mode */
	if (p_264->entropy_mode == V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC)
		mfc_write(dev, 1, S5P_FIMV_ENC_H264_ENTROPY_MODE);
	else
		mfc_write(dev, 0, S5P_FIMV_ENC_H264_ENTROPY_MODE);
	/* number of ref. picture */
	reg = mfc_read(dev, S5P_FIMV_ENC_H264_NUM_OF_REF);
	/* num of ref. pictures of P */
	reg &= ~(0x3 << 5);
	reg |= (p_264->num_ref_pic_4p << 5);
	/* max number of ref. pictures */
	reg &= ~(0x1F);
	reg |= p_264->max_ref_pic;
	mfc_write(dev, reg, S5P_FIMV_ENC_H264_NUM_OF_REF);
	/* 8x8 transform enable */
	mfc_write(dev, p_264->_8x8_transform, S5P_FIMV_ENC_H264_TRANS_FLAG);
	/* rate control config. */
	reg = mfc_read(dev, S5P_FIMV_ENC_RC_CONFIG);
	/* macroblock level rate control */
	reg &= ~(0x1 << 8);
	reg |= (p->rc_mb << 8);
	/* frame QP */
	reg &= ~(0x3F);
	reg |= p_264->rc_frame_qp;
	mfc_write(dev, reg, S5P_FIMV_ENC_RC_CONFIG);
	/* frame rate */
	if (p->rc_frame && p->rc_framerate_denom)
		mfc_write(dev, p->rc_framerate_num * 1000
			/ p->rc_framerate_denom, S5P_FIMV_ENC_RC_FRAME_RATE);
	else
		mfc_write(dev, 0, S5P_FIMV_ENC_RC_FRAME_RATE);
	/* max & min value of QP */
	reg = mfc_read(dev, S5P_FIMV_ENC_RC_QBOUND);
	/* max QP */
	reg &= ~(0x3F << 8);
	reg |= (p_264->rc_max_qp << 8);
	/* min QP */
	reg &= ~(0x3F);
	reg |= p_264->rc_min_qp;
	mfc_write(dev, reg, S5P_FIMV_ENC_RC_QBOUND);
	/* macroblock adaptive scaling features */
	if (p->rc_mb) {
		reg = mfc_read(dev, S5P_FIMV_ENC_RC_MB_CTRL);
		/* dark region */
		reg &= ~(0x1 << 3);
		reg |= (p_264->rc_mb_dark << 3);
		/* smooth region */
		reg &= ~(0x1 << 2);
		reg |= (p_264->rc_mb_smooth << 2);
		/* static region */
		reg &= ~(0x1 << 1);
		reg |= (p_264->rc_mb_static << 1);
		/* high activity region */
		reg &= ~(0x1);
		reg |= p_264->rc_mb_activity;
		mfc_write(dev, reg, S5P_FIMV_ENC_RC_MB_CTRL);
	}
	if (!p->rc_frame && !p->rc_mb) {
		shm = s5p_mfc_read_info_v5(ctx, P_B_FRAME_QP);
		shm &= ~(0xFFF);
		shm |= ((p_264->rc_b_frame_qp & 0x3F) << 6);
		shm |= (p_264->rc_p_frame_qp & 0x3F);
		s5p_mfc_write_info_v5(ctx, shm, P_B_FRAME_QP);
	}
	/* extended encoder ctrl */
	shm = s5p_mfc_read_info_v5(ctx, EXT_ENC_CONTROL);
	/* AR VUI control */
	shm &= ~(0x1 << 15);
	shm |= (p_264->vui_sar << 1);
	s5p_mfc_write_info_v5(ctx, shm, EXT_ENC_CONTROL);
	if (p_264->vui_sar) {
		/* aspect ration IDC */
		shm = s5p_mfc_read_info_v5(ctx, SAMPLE_ASPECT_RATIO_IDC);
		shm &= ~(0xFF);
		shm |= p_264->vui_sar_idc;
		s5p_mfc_write_info_v5(ctx, shm, SAMPLE_ASPECT_RATIO_IDC);
		if (p_264->vui_sar_idc == 0xFF) {
			/* sample  AR info */
			shm = s5p_mfc_read_info_v5(ctx, EXTENDED_SAR);
			shm &= ~(0xFFFFFFFF);
			shm |= p_264->vui_ext_sar_width << 16;
			shm |= p_264->vui_ext_sar_height;
			s5p_mfc_write_info_v5(ctx, shm, EXTENDED_SAR);
		}
	}
	/* intra picture period for H.264 */
	shm = s5p_mfc_read_info_v5(ctx, H264_I_PERIOD);
	/* control */
	shm &= ~(0x1 << 16);
	shm |= (p_264->open_gop << 16);
	/* value */
	if (p_264->open_gop) {
		shm &= ~(0xFFFF);
		shm |= p_264->open_gop_size;
	}
	s5p_mfc_write_info_v5(ctx, shm, H264_I_PERIOD);
	/* extended encoder ctrl */
	shm = s5p_mfc_read_info_v5(ctx, EXT_ENC_CONTROL);
	/* vbv buffer size */
	if (p->frame_skip_mode ==
			V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT) {
		shm &= ~(0xFFFF << 16);
		shm |= (p_264->cpb_size << 16);
	}
	s5p_mfc_write_info_v5(ctx, shm, EXT_ENC_CONTROL);
	return 0;
}

static int s5p_mfc_set_enc_params_mpeg4(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc_params *p = &ctx->enc_params;
	struct s5p_mfc_mpeg4_enc_params *p_mpeg4 = &p->codec.mpeg4;
	unsigned int reg;
	unsigned int shm;
	unsigned int framerate;

	s5p_mfc_set_enc_params(ctx);
	/* pictype : number of B */
	reg = mfc_read(dev, S5P_FIMV_ENC_PIC_TYPE_CTRL);
	/* num_b_frame - 0 ~ 2 */
	reg &= ~(0x3 << 16);
	reg |= (p->num_b_frame << 16);
	mfc_write(dev, reg, S5P_FIMV_ENC_PIC_TYPE_CTRL);
	/* profile & level */
	reg = mfc_read(dev, S5P_FIMV_ENC_PROFILE);
	/* level */
	reg &= ~(0xFF << 8);
	reg |= (p_mpeg4->level << 8);
	/* profile - 0 ~ 2 */
	reg &= ~(0x3F);
	reg |= p_mpeg4->profile;
	mfc_write(dev, reg, S5P_FIMV_ENC_PROFILE);
	/* quarter_pixel */
	mfc_write(dev, p_mpeg4->quarter_pixel, S5P_FIMV_ENC_MPEG4_QUART_PXL);
	/* qp */
	if (!p->rc_frame) {
		shm = s5p_mfc_read_info_v5(ctx, P_B_FRAME_QP);
		shm &= ~(0xFFF);
		shm |= ((p_mpeg4->rc_b_frame_qp & 0x3F) << 6);
		shm |= (p_mpeg4->rc_p_frame_qp & 0x3F);
		s5p_mfc_write_info_v5(ctx, shm, P_B_FRAME_QP);
	}
	/* frame rate */
	if (p->rc_frame) {
		if (p->rc_framerate_denom > 0) {
			framerate = p->rc_framerate_num * 1000 /
						p->rc_framerate_denom;
			mfc_write(dev, framerate,
				S5P_FIMV_ENC_RC_FRAME_RATE);
			shm = s5p_mfc_read_info_v5(ctx, RC_VOP_TIMING);
			shm &= ~(0xFFFFFFFF);
			shm |= (1 << 31);
			shm |= ((p->rc_framerate_num & 0x7FFF) << 16);
			shm |= (p->rc_framerate_denom & 0xFFFF);
			s5p_mfc_write_info_v5(ctx, shm, RC_VOP_TIMING);
		}
	} else {
		mfc_write(dev, 0, S5P_FIMV_ENC_RC_FRAME_RATE);
	}
	/* rate control config. */
	reg = mfc_read(dev, S5P_FIMV_ENC_RC_CONFIG);
	/* frame QP */
	reg &= ~(0x3F);
	reg |= p_mpeg4->rc_frame_qp;
	mfc_write(dev, reg, S5P_FIMV_ENC_RC_CONFIG);
	/* max & min value of QP */
	reg = mfc_read(dev, S5P_FIMV_ENC_RC_QBOUND);
	/* max QP */
	reg &= ~(0x3F << 8);
	reg |= (p_mpeg4->rc_max_qp << 8);
	/* min QP */
	reg &= ~(0x3F);
	reg |= p_mpeg4->rc_min_qp;
	mfc_write(dev, reg, S5P_FIMV_ENC_RC_QBOUND);
	/* extended encoder ctrl */
	shm = s5p_mfc_read_info_v5(ctx, EXT_ENC_CONTROL);
	/* vbv buffer size */
	if (p->frame_skip_mode ==
			V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT) {
		shm &= ~(0xFFFF << 16);
		shm |= (p->vbv_size << 16);
	}
	s5p_mfc_write_info_v5(ctx, shm, EXT_ENC_CONTROL);
	return 0;
}

static int s5p_mfc_set_enc_params_h263(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc_params *p = &ctx->enc_params;
	struct s5p_mfc_mpeg4_enc_params *p_h263 = &p->codec.mpeg4;
	unsigned int reg;
	unsigned int shm;

	s5p_mfc_set_enc_params(ctx);
	/* qp */
	if (!p->rc_frame) {
		shm = s5p_mfc_read_info_v5(ctx, P_B_FRAME_QP);
		shm &= ~(0xFFF);
		shm |= (p_h263->rc_p_frame_qp & 0x3F);
		s5p_mfc_write_info_v5(ctx, shm, P_B_FRAME_QP);
	}
	/* frame rate */
	if (p->rc_frame && p->rc_framerate_denom)
		mfc_write(dev, p->rc_framerate_num * 1000
			/ p->rc_framerate_denom, S5P_FIMV_ENC_RC_FRAME_RATE);
	else
		mfc_write(dev, 0, S5P_FIMV_ENC_RC_FRAME_RATE);
	/* rate control config. */
	reg = mfc_read(dev, S5P_FIMV_ENC_RC_CONFIG);
	/* frame QP */
	reg &= ~(0x3F);
	reg |= p_h263->rc_frame_qp;
	mfc_write(dev, reg, S5P_FIMV_ENC_RC_CONFIG);
	/* max & min value of QP */
	reg = mfc_read(dev, S5P_FIMV_ENC_RC_QBOUND);
	/* max QP */
	reg &= ~(0x3F << 8);
	reg |= (p_h263->rc_max_qp << 8);
	/* min QP */
	reg &= ~(0x3F);
	reg |= p_h263->rc_min_qp;
	mfc_write(dev, reg, S5P_FIMV_ENC_RC_QBOUND);
	/* extended encoder ctrl */
	shm = s5p_mfc_read_info_v5(ctx, EXT_ENC_CONTROL);
	/* vbv buffer size */
	if (p->frame_skip_mode ==
			V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT) {
		shm &= ~(0xFFFF << 16);
		shm |= (p->vbv_size << 16);
	}
	s5p_mfc_write_info_v5(ctx, shm, EXT_ENC_CONTROL);
	return 0;
}

/* Initialize decoding */
static int s5p_mfc_init_decode_v5(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	s5p_mfc_set_shared_buffer(ctx);
	/* Setup loop filter, for decoding this is only valid for MPEG4 */
	if (ctx->codec_mode == S5P_MFC_CODEC_MPEG4_DEC)
		mfc_write(dev, ctx->loop_filter_mpeg4, S5P_FIMV_ENC_LF_CTRL);
	else
		mfc_write(dev, 0, S5P_FIMV_ENC_LF_CTRL);
	mfc_write(dev, ((ctx->slice_interface & S5P_FIMV_SLICE_INT_MASK) <<
		S5P_FIMV_SLICE_INT_SHIFT) | (ctx->display_delay_enable <<
		S5P_FIMV_DDELAY_ENA_SHIFT) | ((ctx->display_delay &
		S5P_FIMV_DDELAY_VAL_MASK) << S5P_FIMV_DDELAY_VAL_SHIFT),
		S5P_FIMV_SI_CH0_DPB_CONF_CTRL);
	mfc_write(dev,
	((S5P_FIMV_CH_SEQ_HEADER & S5P_FIMV_CH_MASK) << S5P_FIMV_CH_SHIFT)
				| (ctx->inst_no), S5P_FIMV_SI_CH0_INST_ID);
	return 0;
}

static void s5p_mfc_set_flush(struct s5p_mfc_ctx *ctx, int flush)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned int dpb;

	if (flush)
		dpb = mfc_read(dev, S5P_FIMV_SI_CH0_DPB_CONF_CTRL) | (
			S5P_FIMV_DPB_FLUSH_MASK << S5P_FIMV_DPB_FLUSH_SHIFT);
	else
		dpb = mfc_read(dev, S5P_FIMV_SI_CH0_DPB_CONF_CTRL) &
			~(S5P_FIMV_DPB_FLUSH_MASK << S5P_FIMV_DPB_FLUSH_SHIFT);
	mfc_write(dev, dpb, S5P_FIMV_SI_CH0_DPB_CONF_CTRL);
}

/* Decode a single frame */
static int s5p_mfc_decode_one_frame_v5(struct s5p_mfc_ctx *ctx,
					enum s5p_mfc_decode_arg last_frame)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	mfc_write(dev, ctx->dec_dst_flag, S5P_FIMV_SI_CH0_RELEASE_BUF);
	s5p_mfc_set_shared_buffer(ctx);
	s5p_mfc_set_flush(ctx, ctx->dpb_flush_flag);
	/* Issue different commands to instance basing on whether it
	 * is the last frame or not. */
	switch (last_frame) {
	case MFC_DEC_FRAME:
		mfc_write(dev, ((S5P_FIMV_CH_FRAME_START & S5P_FIMV_CH_MASK) <<
		S5P_FIMV_CH_SHIFT) | (ctx->inst_no), S5P_FIMV_SI_CH0_INST_ID);
		break;
	case MFC_DEC_LAST_FRAME:
		mfc_write(dev, ((S5P_FIMV_CH_LAST_FRAME & S5P_FIMV_CH_MASK) <<
		S5P_FIMV_CH_SHIFT) | (ctx->inst_no), S5P_FIMV_SI_CH0_INST_ID);
		break;
	case MFC_DEC_RES_CHANGE:
		mfc_write(dev, ((S5P_FIMV_CH_FRAME_START_REALLOC &
		S5P_FIMV_CH_MASK) << S5P_FIMV_CH_SHIFT) | (ctx->inst_no),
		S5P_FIMV_SI_CH0_INST_ID);
		break;
	}
	mfc_debug(2, "Decoding a usual frame\n");
	return 0;
}

static int s5p_mfc_init_encode_v5(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	if (ctx->codec_mode == S5P_MFC_CODEC_H264_ENC)
		s5p_mfc_set_enc_params_h264(ctx);
	else if (ctx->codec_mode == S5P_MFC_CODEC_MPEG4_ENC)
		s5p_mfc_set_enc_params_mpeg4(ctx);
	else if (ctx->codec_mode == S5P_MFC_CODEC_H263_ENC)
		s5p_mfc_set_enc_params_h263(ctx);
	else {
		mfc_err("Unknown codec for encoding (%x)\n",
			ctx->codec_mode);
		return -EINVAL;
	}
	s5p_mfc_set_shared_buffer(ctx);
	mfc_write(dev, ((S5P_FIMV_CH_SEQ_HEADER << 16) & 0x70000) |
		(ctx->inst_no), S5P_FIMV_SI_CH0_INST_ID);
	return 0;
}

/* Encode a single frame */
static int s5p_mfc_encode_one_frame_v5(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	int cmd;
	/* memory structure cur. frame */
	if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12M)
		mfc_write(dev, 0, S5P_FIMV_ENC_MAP_FOR_CUR);
	else if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12MT)
		mfc_write(dev, 3, S5P_FIMV_ENC_MAP_FOR_CUR);
	s5p_mfc_set_shared_buffer(ctx);

	if (ctx->state == MFCINST_FINISHING)
		cmd = S5P_FIMV_CH_LAST_FRAME;
	else
		cmd = S5P_FIMV_CH_FRAME_START;
	mfc_write(dev, ((cmd & S5P_FIMV_CH_MASK) << S5P_FIMV_CH_SHIFT)
				| (ctx->inst_no), S5P_FIMV_SI_CH0_INST_ID);

	return 0;
}

static void s5p_mfc_run_res_change(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	s5p_mfc_set_dec_stream_buffer_v5(ctx, 0, 0, 0);
	dev->curr_ctx = ctx->num;
	s5p_mfc_decode_one_frame_v5(ctx, MFC_DEC_RES_CHANGE);
}

static int s5p_mfc_run_dec_frame(struct s5p_mfc_ctx *ctx, int last_frame)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *temp_vb;

	if (ctx->state == MFCINST_FINISHING) {
		last_frame = MFC_DEC_LAST_FRAME;
		s5p_mfc_set_dec_stream_buffer_v5(ctx, 0, 0, 0);
		dev->curr_ctx = ctx->num;
		s5p_mfc_decode_one_frame_v5(ctx, last_frame);
		return 0;
	}

	/* Frames are being decoded */
	if (list_empty(&ctx->src_queue)) {
		mfc_debug(2, "No src buffers\n");
		return -EAGAIN;
	}
	/* Get the next source buffer */
	temp_vb = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);
	temp_vb->flags |= MFC_BUF_FLAG_USED;
	s5p_mfc_set_dec_stream_buffer_v5(ctx,
		vb2_dma_contig_plane_dma_addr(&temp_vb->b->vb2_buf, 0),
		ctx->consumed_stream, temp_vb->b->vb2_buf.planes[0].bytesused);
	dev->curr_ctx = ctx->num;
	if (temp_vb->b->vb2_buf.planes[0].bytesused == 0) {
		last_frame = MFC_DEC_LAST_FRAME;
		mfc_debug(2, "Setting ctx->state to FINISHING\n");
		ctx->state = MFCINST_FINISHING;
	}
	s5p_mfc_decode_one_frame_v5(ctx, last_frame);
	return 0;
}

static int s5p_mfc_run_enc_frame(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *dst_mb;
	struct s5p_mfc_buf *src_mb;
	unsigned long src_y_addr, src_c_addr, dst_addr;
	unsigned int dst_size;

	if (list_empty(&ctx->src_queue) && ctx->state != MFCINST_FINISHING) {
		mfc_debug(2, "no src buffers\n");
		return -EAGAIN;
	}
	if (list_empty(&ctx->dst_queue)) {
		mfc_debug(2, "no dst buffers\n");
		return -EAGAIN;
	}
	if (list_empty(&ctx->src_queue)) {
		/* send null frame */
		s5p_mfc_set_enc_frame_buffer_v5(ctx, dev->bank2, dev->bank2);
		src_mb = NULL;
	} else {
		src_mb = list_entry(ctx->src_queue.next, struct s5p_mfc_buf,
									list);
		src_mb->flags |= MFC_BUF_FLAG_USED;
		if (src_mb->b->vb2_buf.planes[0].bytesused == 0) {
			/* send null frame */
			s5p_mfc_set_enc_frame_buffer_v5(ctx, dev->bank2,
								dev->bank2);
			ctx->state = MFCINST_FINISHING;
		} else {
			src_y_addr = vb2_dma_contig_plane_dma_addr(
					&src_mb->b->vb2_buf, 0);
			src_c_addr = vb2_dma_contig_plane_dma_addr(
					&src_mb->b->vb2_buf, 1);
			s5p_mfc_set_enc_frame_buffer_v5(ctx, src_y_addr,
								src_c_addr);
			if (src_mb->flags & MFC_BUF_FLAG_EOS)
				ctx->state = MFCINST_FINISHING;
		}
	}
	dst_mb = list_entry(ctx->dst_queue.next, struct s5p_mfc_buf, list);
	dst_mb->flags |= MFC_BUF_FLAG_USED;
	dst_addr = vb2_dma_contig_plane_dma_addr(&dst_mb->b->vb2_buf, 0);
	dst_size = vb2_plane_size(&dst_mb->b->vb2_buf, 0);
	s5p_mfc_set_enc_stream_buffer_v5(ctx, dst_addr, dst_size);
	dev->curr_ctx = ctx->num;
	mfc_debug(2, "encoding buffer with index=%d state=%d\n",
		  src_mb ? src_mb->b->vb2_buf.index : -1, ctx->state);
	s5p_mfc_encode_one_frame_v5(ctx);
	return 0;
}

static void s5p_mfc_run_init_dec(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *temp_vb;

	/* Initializing decoding - parsing header */
	mfc_debug(2, "Preparing to init decoding\n");
	temp_vb = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);
	s5p_mfc_set_dec_desc_buffer(ctx);
	mfc_debug(2, "Header size: %d\n",
			temp_vb->b->vb2_buf.planes[0].bytesused);
	s5p_mfc_set_dec_stream_buffer_v5(ctx,
			vb2_dma_contig_plane_dma_addr(&temp_vb->b->vb2_buf, 0),
			0, temp_vb->b->vb2_buf.planes[0].bytesused);
	dev->curr_ctx = ctx->num;
	s5p_mfc_init_decode_v5(ctx);
}

static void s5p_mfc_run_init_enc(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *dst_mb;
	unsigned long dst_addr;
	unsigned int dst_size;

	s5p_mfc_set_enc_ref_buffer_v5(ctx);
	dst_mb = list_entry(ctx->dst_queue.next, struct s5p_mfc_buf, list);
	dst_addr = vb2_dma_contig_plane_dma_addr(&dst_mb->b->vb2_buf, 0);
	dst_size = vb2_plane_size(&dst_mb->b->vb2_buf, 0);
	s5p_mfc_set_enc_stream_buffer_v5(ctx, dst_addr, dst_size);
	dev->curr_ctx = ctx->num;
	s5p_mfc_init_encode_v5(ctx);
}

static int s5p_mfc_run_init_dec_buffers(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *temp_vb;
	int ret;

	/*
	 * Header was parsed now starting processing
	 * First set the output frame buffers
	 */
	if (ctx->capture_state != QUEUE_BUFS_MMAPED) {
		mfc_err("It seems that not all destionation buffers were "
			"mmaped\nMFC requires that all destination are mmaped "
			"before starting processing\n");
		return -EAGAIN;
	}
	if (list_empty(&ctx->src_queue)) {
		mfc_err("Header has been deallocated in the middle of"
			" initialization\n");
		return -EIO;
	}
	temp_vb = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);
	mfc_debug(2, "Header size: %d\n",
			temp_vb->b->vb2_buf.planes[0].bytesused);
	s5p_mfc_set_dec_stream_buffer_v5(ctx,
			vb2_dma_contig_plane_dma_addr(&temp_vb->b->vb2_buf, 0),
			0, temp_vb->b->vb2_buf.planes[0].bytesused);
	dev->curr_ctx = ctx->num;
	ret = s5p_mfc_set_dec_frame_buffer_v5(ctx);
	if (ret) {
		mfc_err("Failed to alloc frame mem\n");
		ctx->state = MFCINST_ERROR;
	}
	return ret;
}

/* Try running an operation on hardware */
static void s5p_mfc_try_run_v5(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_ctx *ctx;
	int new_ctx;
	unsigned int ret = 0;

	if (test_bit(0, &dev->enter_suspend)) {
		mfc_debug(1, "Entering suspend so do not schedule any jobs\n");
		return;
	}
	/* Check whether hardware is not running */
	if (test_and_set_bit(0, &dev->hw_lock) != 0) {
		/* This is perfectly ok, the scheduled ctx should wait */
		mfc_debug(1, "Couldn't lock HW\n");
		return;
	}
	/* Choose the context to run */
	new_ctx = s5p_mfc_get_new_ctx(dev);
	if (new_ctx < 0) {
		/* No contexts to run */
		if (test_and_clear_bit(0, &dev->hw_lock) == 0) {
			mfc_err("Failed to unlock hardware\n");
			return;
		}
		mfc_debug(1, "No ctx is scheduled to be run\n");
		return;
	}
	ctx = dev->ctx[new_ctx];
	/* Got context to run in ctx */
	/*
	 * Last frame has already been sent to MFC.
	 * Now obtaining frames from MFC buffer
	 */
	s5p_mfc_clock_on();
	s5p_mfc_clean_ctx_int_flags(ctx);

	if (ctx->type == MFCINST_DECODER) {
		s5p_mfc_set_dec_desc_buffer(ctx);
		switch (ctx->state) {
		case MFCINST_FINISHING:
			s5p_mfc_run_dec_frame(ctx, MFC_DEC_LAST_FRAME);
			break;
		case MFCINST_RUNNING:
			ret = s5p_mfc_run_dec_frame(ctx, MFC_DEC_FRAME);
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
			mfc_debug(1, "head parsed\n");
			break;
		case MFCINST_RES_CHANGE_INIT:
			s5p_mfc_run_res_change(ctx);
			break;
		case MFCINST_RES_CHANGE_FLUSH:
			s5p_mfc_run_dec_frame(ctx, MFC_DEC_FRAME);
			break;
		case MFCINST_RES_CHANGE_END:
			mfc_debug(2, "Finished remaining frames after resolution change\n");
			ctx->capture_state = QUEUE_FREE;
			mfc_debug(2, "Will re-init the codec\n");
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
		default:
			ret = -EAGAIN;
		}
	} else {
		mfc_err("Invalid context type: %d\n", ctx->type);
		ret = -EAGAIN;
	}

	if (ret) {
		/* Free hardware lock */
		if (test_and_clear_bit(0, &dev->hw_lock) == 0)
			mfc_err("Failed to unlock hardware\n");

		/* This is in deed imporant, as no operation has been
		 * scheduled, reduce the clock count as no one will
		 * ever do this, because no interrupt related to this try_run
		 * will ever come from hardware. */
		s5p_mfc_clock_off();
	}
}

static void s5p_mfc_clear_int_flags_v5(struct s5p_mfc_dev *dev)
{
	mfc_write(dev, 0, S5P_FIMV_RISC_HOST_INT);
	mfc_write(dev, 0, S5P_FIMV_RISC2HOST_CMD);
	mfc_write(dev, 0xffff, S5P_FIMV_SI_RTN_CHID);
}

static int s5p_mfc_get_dspl_y_adr_v5(struct s5p_mfc_dev *dev)
{
	return mfc_read(dev, S5P_FIMV_SI_DISPLAY_Y_ADR) << MFC_OFFSET_SHIFT;
}

static int s5p_mfc_get_dec_y_adr_v5(struct s5p_mfc_dev *dev)
{
	return mfc_read(dev, S5P_FIMV_SI_DECODE_Y_ADR) << MFC_OFFSET_SHIFT;
}

static int s5p_mfc_get_dspl_status_v5(struct s5p_mfc_dev *dev)
{
	return mfc_read(dev, S5P_FIMV_SI_DISPLAY_STATUS);
}

static int s5p_mfc_get_dec_status_v5(struct s5p_mfc_dev *dev)
{
	return mfc_read(dev, S5P_FIMV_SI_DECODE_STATUS);
}

static int s5p_mfc_get_dec_frame_type_v5(struct s5p_mfc_dev *dev)
{
	return mfc_read(dev, S5P_FIMV_DECODE_FRAME_TYPE) &
		S5P_FIMV_DECODE_FRAME_MASK;
}

static int s5p_mfc_get_disp_frame_type_v5(struct s5p_mfc_ctx *ctx)
{
	return (s5p_mfc_read_info_v5(ctx, DISP_PIC_FRAME_TYPE) >>
			S5P_FIMV_SHARED_DISP_FRAME_TYPE_SHIFT) &
			S5P_FIMV_DECODE_FRAME_MASK;
}

static int s5p_mfc_get_consumed_stream_v5(struct s5p_mfc_dev *dev)
{
	return mfc_read(dev, S5P_FIMV_SI_CONSUMED_BYTES);
}

static int s5p_mfc_get_int_reason_v5(struct s5p_mfc_dev *dev)
{
	int reason;
	reason = mfc_read(dev, S5P_FIMV_RISC2HOST_CMD) &
		S5P_FIMV_RISC2HOST_CMD_MASK;
	switch (reason) {
	case S5P_FIMV_R2H_CMD_OPEN_INSTANCE_RET:
		reason = S5P_MFC_R2H_CMD_OPEN_INSTANCE_RET;
		break;
	case S5P_FIMV_R2H_CMD_CLOSE_INSTANCE_RET:
		reason = S5P_MFC_R2H_CMD_CLOSE_INSTANCE_RET;
		break;
	case S5P_FIMV_R2H_CMD_SEQ_DONE_RET:
		reason = S5P_MFC_R2H_CMD_SEQ_DONE_RET;
		break;
	case S5P_FIMV_R2H_CMD_FRAME_DONE_RET:
		reason = S5P_MFC_R2H_CMD_FRAME_DONE_RET;
		break;
	case S5P_FIMV_R2H_CMD_SLICE_DONE_RET:
		reason = S5P_MFC_R2H_CMD_SLICE_DONE_RET;
		break;
	case S5P_FIMV_R2H_CMD_SYS_INIT_RET:
		reason = S5P_MFC_R2H_CMD_SYS_INIT_RET;
		break;
	case S5P_FIMV_R2H_CMD_FW_STATUS_RET:
		reason = S5P_MFC_R2H_CMD_FW_STATUS_RET;
		break;
	case S5P_FIMV_R2H_CMD_SLEEP_RET:
		reason = S5P_MFC_R2H_CMD_SLEEP_RET;
		break;
	case S5P_FIMV_R2H_CMD_WAKEUP_RET:
		reason = S5P_MFC_R2H_CMD_WAKEUP_RET;
		break;
	case S5P_FIMV_R2H_CMD_INIT_BUFFERS_RET:
		reason = S5P_MFC_R2H_CMD_INIT_BUFFERS_RET;
		break;
	case S5P_FIMV_R2H_CMD_ENC_COMPLETE_RET:
		reason = S5P_MFC_R2H_CMD_COMPLETE_SEQ_RET;
		break;
	case S5P_FIMV_R2H_CMD_ERR_RET:
		reason = S5P_MFC_R2H_CMD_ERR_RET;
		break;
	default:
		reason = S5P_MFC_R2H_CMD_EMPTY;
	}
	return reason;
}

static int s5p_mfc_get_int_err_v5(struct s5p_mfc_dev *dev)
{
	return mfc_read(dev, S5P_FIMV_RISC2HOST_ARG2);
}

static int s5p_mfc_err_dec_v5(unsigned int err)
{
	return (err & S5P_FIMV_ERR_DEC_MASK) >> S5P_FIMV_ERR_DEC_SHIFT;
}

static int s5p_mfc_get_img_width_v5(struct s5p_mfc_dev *dev)
{
	return mfc_read(dev, S5P_FIMV_SI_HRESOL);
}

static int s5p_mfc_get_img_height_v5(struct s5p_mfc_dev *dev)
{
	return mfc_read(dev, S5P_FIMV_SI_VRESOL);
}

static int s5p_mfc_get_dpb_count_v5(struct s5p_mfc_dev *dev)
{
	return mfc_read(dev, S5P_FIMV_SI_BUF_NUMBER);
}

static int s5p_mfc_get_mv_count_v5(struct s5p_mfc_dev *dev)
{
	/* NOP */
	return -1;
}

static int s5p_mfc_get_inst_no_v5(struct s5p_mfc_dev *dev)
{
	return mfc_read(dev, S5P_FIMV_RISC2HOST_ARG1);
}

static int s5p_mfc_get_enc_strm_size_v5(struct s5p_mfc_dev *dev)
{
	return mfc_read(dev, S5P_FIMV_ENC_SI_STRM_SIZE);
}

static int s5p_mfc_get_enc_slice_type_v5(struct s5p_mfc_dev *dev)
{
	return mfc_read(dev, S5P_FIMV_ENC_SI_SLICE_TYPE);
}

static int s5p_mfc_get_enc_dpb_count_v5(struct s5p_mfc_dev *dev)
{
	return -1;
}

static unsigned int s5p_mfc_get_pic_type_top_v5(struct s5p_mfc_ctx *ctx)
{
	return s5p_mfc_read_info_v5(ctx, PIC_TIME_TOP);
}

static unsigned int s5p_mfc_get_pic_type_bot_v5(struct s5p_mfc_ctx *ctx)
{
	return s5p_mfc_read_info_v5(ctx, PIC_TIME_BOT);
}

static unsigned int s5p_mfc_get_crop_info_h_v5(struct s5p_mfc_ctx *ctx)
{
	return s5p_mfc_read_info_v5(ctx, CROP_INFO_H);
}

static unsigned int s5p_mfc_get_crop_info_v_v5(struct s5p_mfc_ctx *ctx)
{
	return s5p_mfc_read_info_v5(ctx, CROP_INFO_V);
}

/* Initialize opr function pointers for MFC v5 */
static struct s5p_mfc_hw_ops s5p_mfc_ops_v5 = {
	.alloc_dec_temp_buffers = s5p_mfc_alloc_dec_temp_buffers_v5,
	.release_dec_desc_buffer = s5p_mfc_release_dec_desc_buffer_v5,
	.alloc_codec_buffers = s5p_mfc_alloc_codec_buffers_v5,
	.release_codec_buffers = s5p_mfc_release_codec_buffers_v5,
	.alloc_instance_buffer = s5p_mfc_alloc_instance_buffer_v5,
	.release_instance_buffer = s5p_mfc_release_instance_buffer_v5,
	.alloc_dev_context_buffer = s5p_mfc_alloc_dev_context_buffer_v5,
	.release_dev_context_buffer = s5p_mfc_release_dev_context_buffer_v5,
	.dec_calc_dpb_size = s5p_mfc_dec_calc_dpb_size_v5,
	.enc_calc_src_size = s5p_mfc_enc_calc_src_size_v5,
	.set_enc_stream_buffer = s5p_mfc_set_enc_stream_buffer_v5,
	.set_enc_frame_buffer = s5p_mfc_set_enc_frame_buffer_v5,
	.get_enc_frame_buffer = s5p_mfc_get_enc_frame_buffer_v5,
	.try_run = s5p_mfc_try_run_v5,
	.clear_int_flags = s5p_mfc_clear_int_flags_v5,
	.get_dspl_y_adr = s5p_mfc_get_dspl_y_adr_v5,
	.get_dec_y_adr = s5p_mfc_get_dec_y_adr_v5,
	.get_dspl_status = s5p_mfc_get_dspl_status_v5,
	.get_dec_status = s5p_mfc_get_dec_status_v5,
	.get_dec_frame_type = s5p_mfc_get_dec_frame_type_v5,
	.get_disp_frame_type = s5p_mfc_get_disp_frame_type_v5,
	.get_consumed_stream = s5p_mfc_get_consumed_stream_v5,
	.get_int_reason = s5p_mfc_get_int_reason_v5,
	.get_int_err = s5p_mfc_get_int_err_v5,
	.err_dec = s5p_mfc_err_dec_v5,
	.get_img_width = s5p_mfc_get_img_width_v5,
	.get_img_height = s5p_mfc_get_img_height_v5,
	.get_dpb_count = s5p_mfc_get_dpb_count_v5,
	.get_mv_count = s5p_mfc_get_mv_count_v5,
	.get_inst_no = s5p_mfc_get_inst_no_v5,
	.get_enc_strm_size = s5p_mfc_get_enc_strm_size_v5,
	.get_enc_slice_type = s5p_mfc_get_enc_slice_type_v5,
	.get_enc_dpb_count = s5p_mfc_get_enc_dpb_count_v5,
	.get_pic_type_top = s5p_mfc_get_pic_type_top_v5,
	.get_pic_type_bot = s5p_mfc_get_pic_type_bot_v5,
	.get_crop_info_h = s5p_mfc_get_crop_info_h_v5,
	.get_crop_info_v = s5p_mfc_get_crop_info_v_v5,
};

struct s5p_mfc_hw_ops *s5p_mfc_init_hw_ops_v5(void)
{
	return &s5p_mfc_ops_v5;
}
