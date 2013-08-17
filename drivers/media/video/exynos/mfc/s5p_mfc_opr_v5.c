/*
 * drivers/media/video/exynos/mfc/s5p_mfc_opr_v5.c
 *
 * Samsung MFC (Multi Function Codec - FIMV) driver
 * This file contains hw related functions.
 *
 * Kamil Debski, Copyright (c) 2010 Samsung Electronics
 * http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define DEBUG

#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/jiffies.h>

#include <linux/firmware.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/cma.h>

#include <linux/dma-mapping.h>
#include <asm/cacheflush.h>

#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
#include <plat/iovmm.h>
#endif

#include "s5p_mfc_common.h"

#include "s5p_mfc_mem.h"
#include "s5p_mfc_intr.h"
#include "s5p_mfc_inst.h"
#include "s5p_mfc_pm.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_dec.h"
#include "s5p_mfc_enc.h"

/* #define S5P_MFC_DEBUG_REGWRITE  */
#ifdef S5P_MFC_DEBUG_REGWRITE
#undef writel
#define writel(v, r)								\
	do {									\
		printk(KERN_ERR "MFCWRITE(%p): %08x\n", r, (unsigned int)v);	\
		__raw_writel(v, r);						\
	} while (0)
#endif /* S5P_MFC_DEBUG_REGWRITE */

#define READL(offset)		readl(dev->regs_base + (offset))
#define WRITEL(data, offset)	writel((data), dev->regs_base + (offset))
#define OFFSETA(x)		(((x) - dev->port_a) >> S5P_FIMV_MEM_OFFSET)
#define OFFSETB(x)		(((x) - dev->port_b) >> S5P_FIMV_MEM_OFFSET)

/* Allocate temporary buffers for decoding */
int s5p_mfc_alloc_dec_temp_buffers(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_dec *dec = ctx->dec_priv;
	struct s5p_mfc_buf_size_v5 *buf_size = dev->variant->buf_size->buf;
	void *alloc_ctx = dev->alloc_ctx[MFC_CMA_BANK1_ALLOC_CTX];

	mfc_debug_enter();

	if (ctx->is_drm)
		alloc_ctx = dev->alloc_ctx_drm;

	dec->dsc.alloc = s5p_mfc_mem_alloc_priv(alloc_ctx, buf_size->desc_buf);
	if (IS_ERR(dec->dsc.alloc)) {
		mfc_err("Allocating DESC buffer failed.\n");
		return PTR_ERR(dec->dsc.alloc);
	}

	dec->dsc.ofs = OFFSETA(s5p_mfc_mem_daddr_priv(dec->dsc.alloc));

	mfc_debug_leave();

	return 0;
}

/* Release temproary buffers for decoding */
void s5p_mfc_release_dec_desc_buffer(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dec *dec = ctx->dec_priv;

	if (dec->dsc.alloc) {
		s5p_mfc_mem_free_priv(dec->dsc.alloc);
		dec->dsc.alloc = NULL;
		dec->dsc.ofs = 0;
		dec->dsc.virt = NULL;
	}
}

/* Allocate encoder DPB dynamically */
int get_enc_dynamic_dpb(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	struct s5p_mfc_enc_params *p = &enc->params;
	struct s5p_mfc_h264_enc_params *p_264 = &p->codec.h264;
	int ret = ENC_REF_2;

	switch (ctx->codec_mode) {
	case S5P_FIMV_CODEC_H264_ENC:
		if ((p->num_b_frame > 0) || (p_264->num_ref_pic_4p > 1))
			ret = ENC_REF_4;
		break;
	case S5P_FIMV_CODEC_MPEG4_ENC:
		if (p->num_b_frame > 0)
			ret = ENC_REF_4;
		break;
	case S5P_FIMV_CODEC_H263_ENC:
	default:
		break;
	}

	return ret;
}

/* Allocate codec buffers */
int s5p_mfc_alloc_codec_buffers(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_dec *dec = ctx->dec_priv;
	unsigned int enc_ref_y_size = 0;
	unsigned int enc_ref_c_size = 0;
	unsigned int guard_width, guard_height;
	void *alloc_ctx = dev->alloc_ctx[MFC_CMA_BANK1_ALLOC_CTX];

	mfc_debug_enter();

	if (ctx->type == MFCINST_DECODER) {
		mfc_debug(2, "Luma size:%d Chroma size:%d MV size:%d\n",
			  ctx->luma_size, ctx->chroma_size, ctx->mv_size);
		mfc_debug(2, "Totals bufs: %d\n", dec->total_dpb_count);
	} else if (ctx->type == MFCINST_ENCODER) {
		enc_ref_y_size = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN)
			* ALIGN(ctx->img_height, S5P_FIMV_NV12MT_VALIGN);
		enc_ref_y_size = ALIGN(enc_ref_y_size, S5P_FIMV_NV12MT_SALIGN);

		if (ctx->codec_mode == S5P_FIMV_CODEC_H264_ENC) {
			enc_ref_c_size = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN)
				* ALIGN((ctx->img_height >> 1), S5P_FIMV_NV12MT_VALIGN);
			enc_ref_c_size = ALIGN(enc_ref_c_size, S5P_FIMV_NV12MT_SALIGN);
		} else {
			guard_width = ALIGN(ctx->img_width + 16, S5P_FIMV_NV12MT_HALIGN);
			guard_height = ALIGN((ctx->img_height >> 1) + 4, S5P_FIMV_NV12MT_VALIGN);
			enc_ref_c_size = ALIGN(guard_width * guard_height,
					       S5P_FIMV_NV12MT_SALIGN);
		}

		ctx->dpb_count = get_enc_dynamic_dpb(ctx);
		mfc_debug(2, "recon luma size: %d chroma size: %d\n",
			  enc_ref_y_size, enc_ref_c_size);
		mfc_debug(2, "Encoder ref count: %d\n", ctx->dpb_count);
	} else {
		return -EINVAL;
	}

	/* Codecs have different memory requirements */
	switch (ctx->codec_mode) {
	case S5P_FIMV_CODEC_H264_DEC:
		ctx->port_a_size =
		    ALIGN(S5P_FIMV_DEC_NB_IP_SIZE +
					S5P_FIMV_DEC_VERT_NB_MV_SIZE,
					S5P_FIMV_DEC_BUF_ALIGN);
		/* TODO, when merged with FIMC then test will it work without
		 * alignment to 8192. For all codecs. */
		ctx->port_b_size = dec->total_dpb_count * ctx->mv_size;
		break;
	case S5P_FIMV_CODEC_MPEG4_DEC:
	case S5P_FIMV_CODEC_FIMV1_DEC:
	case S5P_FIMV_CODEC_FIMV2_DEC:
	case S5P_FIMV_CODEC_FIMV3_DEC:
	case S5P_FIMV_CODEC_FIMV4_DEC:
		ctx->port_a_size =
		    ALIGN(S5P_FIMV_DEC_NB_DCAC_SIZE +
				     S5P_FIMV_DEC_UPNB_MV_SIZE +
				     S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE +
				     S5P_FIMV_DEC_STX_PARSER_SIZE +
				     S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE,
				     S5P_FIMV_DEC_BUF_ALIGN);
		ctx->port_b_size = 0;
		break;

	case S5P_FIMV_CODEC_VC1RCV_DEC:
	case S5P_FIMV_CODEC_VC1_DEC:
		ctx->port_a_size =
		    ALIGN(S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE +
			     S5P_FIMV_DEC_UPNB_MV_SIZE +
			     S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE +
			     S5P_FIMV_DEC_NB_DCAC_SIZE +
			     3 * S5P_FIMV_DEC_VC1_BITPLANE_SIZE,
			     S5P_FIMV_DEC_BUF_ALIGN);
		ctx->port_b_size = 0;
		break;

	case S5P_FIMV_CODEC_MPEG2_DEC:
		ctx->port_a_size = 0;
		ctx->port_b_size = 0;
		break;
	case S5P_FIMV_CODEC_H263_DEC:
		ctx->port_a_size =
		    ALIGN(S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE +
			     S5P_FIMV_DEC_UPNB_MV_SIZE +
			     S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE +
			     S5P_FIMV_DEC_NB_DCAC_SIZE,
			     S5P_FIMV_DEC_BUF_ALIGN);
		ctx->port_b_size = 0;
		break;

	case S5P_FIMV_CODEC_H264_ENC:
		ctx->port_a_size = (enc_ref_y_size * ENC_REF_2) +
				   S5P_FIMV_ENC_UPMV_SIZE +
				   S5P_FIMV_ENC_COLFLG_SIZE +
				   S5P_FIMV_ENC_INTRAMD_SIZE +
				   S5P_FIMV_ENC_NBORINFO_SIZE;
		ctx->port_b_size = (enc_ref_y_size *
					(ctx->dpb_count - ENC_REF_2)) +
				   (enc_ref_c_size * ctx->dpb_count) +
				   S5P_FIMV_ENC_INTRAPRED_SIZE;
		break;
	case S5P_FIMV_CODEC_MPEG4_ENC:
		ctx->port_a_size = (enc_ref_y_size * ENC_REF_2) +
				   S5P_FIMV_ENC_UPMV_SIZE +
				   S5P_FIMV_ENC_COLFLG_SIZE +
				   S5P_FIMV_ENC_ACDCCOEF_SIZE;
		ctx->port_b_size = (enc_ref_y_size *
					(ctx->dpb_count - ENC_REF_2)) +
				   (enc_ref_c_size * ctx->dpb_count);
		break;
	case S5P_FIMV_CODEC_H263_ENC:
		ctx->port_a_size = (enc_ref_y_size * ctx->dpb_count) +
				   S5P_FIMV_ENC_UPMV_SIZE +
				   S5P_FIMV_ENC_ACDCCOEF_SIZE;
		ctx->port_b_size = (enc_ref_c_size * ctx->dpb_count);
		break;
	default:
		break;
	}

	if (ctx->is_drm)
		alloc_ctx = dev->alloc_ctx_drm;

	/* Allocate only if memory from bank 1 is necessary */
	if (ctx->port_a_size > 0) {
		ctx->port_a_buf = s5p_mfc_mem_alloc_priv(
				alloc_ctx, ctx->port_a_size);
		if (IS_ERR(ctx->port_a_buf)) {
			ctx->port_a_buf = 0;
			printk(KERN_ERR
			       "Buf alloc for decoding failed (port A).\n");
			return -ENOMEM;
		}
		ctx->port_a_phys = s5p_mfc_mem_daddr_priv(ctx->port_a_buf);
	}

	/* Allocate only if memory from bank 2 is necessary */
	if (ctx->port_b_size > 0) {
		ctx->port_b_buf = s5p_mfc_mem_alloc_priv(
				dev->alloc_ctx[MFC_CMA_BANK2_ALLOC_CTX],
				ctx->port_b_size);
		if (IS_ERR(ctx->port_b_buf)) {
			ctx->port_b_buf = 0;
			mfc_err("Buf alloc for decoding failed (port B).\n");
			return -ENOMEM;
		}
		ctx->port_b_phys = s5p_mfc_mem_daddr_priv(ctx->port_b_buf);
	}
	mfc_debug_leave();

	return 0;
}

/* Release buffers allocated for codec */
void s5p_mfc_release_codec_buffers(struct s5p_mfc_ctx *ctx)
{
	if (ctx->port_a_buf) {
		s5p_mfc_mem_free_priv(ctx->port_a_buf);
		ctx->port_a_buf = 0;
		ctx->port_a_phys = 0;
		ctx->port_a_size = 0;
	}
	if (ctx->port_b_buf) {
		s5p_mfc_mem_free_priv(ctx->port_b_buf);
		ctx->port_b_buf = 0;
		ctx->port_b_phys = 0;
		ctx->port_b_size = 0;
	}
}

/* Allocate memory for instance data buffer */
int s5p_mfc_alloc_instance_buffer(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf_size_v5 *buf_size = dev->variant->buf_size->buf;
	void *alloc_ctx = dev->alloc_ctx[MFC_CMA_BANK1_ALLOC_CTX];

	mfc_debug_enter();

	if (ctx->codec_mode == S5P_FIMV_CODEC_H264_DEC)
		ctx->ctx_buf_size = buf_size->h264_ctx_buf;
	else
		ctx->ctx_buf_size = buf_size->non_h264_ctx_buf;

	if (ctx->is_drm)
		alloc_ctx = dev->alloc_ctx_drm;

	ctx->ctx.alloc = s5p_mfc_mem_alloc_priv(alloc_ctx, ctx->ctx_buf_size);
	if (IS_ERR(ctx->ctx.alloc)) {
		mfc_err("Allocating context buffer failed.\n");
		return PTR_ERR(ctx->ctx.alloc);
	}

	ctx->ctx.ofs = OFFSETA(s5p_mfc_mem_daddr_priv(ctx->ctx.alloc));

	if (!ctx->is_drm) {
		ctx->ctx.virt = s5p_mfc_mem_vaddr_priv(ctx->ctx.alloc);
		if (!ctx->ctx.virt) {
			s5p_mfc_mem_free_priv(ctx->ctx.alloc);
			ctx->ctx.alloc = NULL;
			ctx->ctx.ofs = 0;
			ctx->ctx.virt = NULL;

			mfc_err("Remapping context buffer failed.\n");
			return -ENOMEM;
		}

		memset(ctx->ctx.virt, 0, ctx->ctx_buf_size);
		s5p_mfc_mem_clean_priv(ctx->ctx.alloc, ctx->ctx.virt, 0,
				ctx->ctx_buf_size);
	}
	/*
	ctx->ctx.dma = dma_map_single(ctx->dev->v4l2_dev.dev,
					  ctx->ctx.virt, ctx->ctx_buf_size,
					  DMA_TO_DEVICE);
	*/

	if (s5p_mfc_init_shm(ctx) < 0) {
		s5p_mfc_mem_free_priv(ctx->ctx.alloc);
		ctx->ctx.alloc = NULL;
		ctx->ctx.ofs = 0;
		ctx->ctx.virt = NULL;

		mfc_err("Remapping shared mem buffer failed.\n");
		return -ENOMEM;
	}

	mfc_debug_leave();

	return 0;
}

/* Release instance buffer */
void s5p_mfc_release_instance_buffer(struct s5p_mfc_ctx *ctx)
{
	mfc_debug_enter();

	if (ctx->ctx.alloc) {
		/*
		dma_unmap_single(ctx->dev->v4l2_dev.dev,
				ctx->ctx.dma, ctx->ctx_buf_size,
				DMA_TO_DEVICE);
		*/
		s5p_mfc_mem_free_priv(ctx->ctx.alloc);
		ctx->ctx.alloc = NULL;
		ctx->ctx.ofs = 0;
		ctx->ctx.virt = NULL;
	}
	if (ctx->shm.alloc) {
		s5p_mfc_mem_free_priv(ctx->shm.alloc);
		ctx->shm.alloc = NULL;
		ctx->shm.ofs = 0;
		ctx->shm.virt = NULL;
	}

	mfc_debug_leave();
}

int s5p_mfc_alloc_dev_context_buffer(struct s5p_mfc_dev *dev)
{
	/* NOP */

	return 0;
}

void s5p_mfc_release_dev_context_buffer(struct s5p_mfc_dev *dev)
{
	/* NOP */
}

void s5p_mfc_dec_calc_dpb_size(struct s5p_mfc_ctx *ctx)
{
	unsigned int guard_width, guard_height;

	ctx->buf_width = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN);
	ctx->buf_height = ALIGN(ctx->img_height, S5P_FIMV_NV12MT_VALIGN);
	mfc_debug(2, "SEQ Done: Movie dimensions %dx%d, "
			"buffer dimensions: %dx%d\n", ctx->img_width,
			ctx->img_height, ctx->buf_width, ctx->buf_height);

	if (ctx->codec_mode == S5P_FIMV_CODEC_H264_DEC) {
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
		guard_width = ALIGN(ctx->img_width + 24, S5P_FIMV_NV12MT_HALIGN);
		guard_height = ALIGN(ctx->img_height + 16, S5P_FIMV_NV12MT_VALIGN);
		ctx->luma_size = ALIGN(guard_width * guard_height,
				S5P_FIMV_DEC_BUF_ALIGN);

		guard_width = ALIGN(ctx->img_width + 16, S5P_FIMV_NV12MT_HALIGN);
		guard_height = ALIGN((ctx->img_height >> 1) + 4, S5P_FIMV_NV12MT_VALIGN);
		ctx->chroma_size = ALIGN(guard_width * guard_height,
				S5P_FIMV_DEC_BUF_ALIGN);

		ctx->mv_size = 0;
	}
}

void s5p_mfc_enc_calc_src_size(struct s5p_mfc_ctx *ctx)
{
	if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12M) {
		ctx->buf_width = ALIGN(ctx->img_width, S5P_FIMV_NV12M_HALIGN);

		ctx->luma_size = ALIGN(ctx->img_width, S5P_FIMV_NV12M_HALIGN)
			* ALIGN(ctx->img_height, S5P_FIMV_NV12M_LVALIGN);
		ctx->chroma_size = ALIGN(ctx->img_width, S5P_FIMV_NV12M_HALIGN)
			* ALIGN((ctx->img_height >> 1), S5P_FIMV_NV12M_CVALIGN);

		ctx->luma_size = ALIGN(ctx->luma_size, S5P_FIMV_NV12M_SALIGN);
		ctx->chroma_size = ALIGN(ctx->chroma_size, S5P_FIMV_NV12M_SALIGN);
	} else if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12MT) {
		ctx->buf_width = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN);

		ctx->luma_size = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN)
			* ALIGN(ctx->img_height, S5P_FIMV_NV12MT_VALIGN);
		ctx->chroma_size = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN)
			* ALIGN((ctx->img_height >> 1), S5P_FIMV_NV12MT_VALIGN);

		ctx->luma_size = ALIGN(ctx->luma_size, S5P_FIMV_NV12MT_SALIGN);
		ctx->chroma_size = ALIGN(ctx->chroma_size, S5P_FIMV_NV12MT_SALIGN);
	}
}

/* Set registers for decoding temporary buffers */
void s5p_mfc_set_dec_desc_buffer(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_dec *dec = ctx->dec_priv;
	struct s5p_mfc_buf_size_v5 *buf_size = dev->variant->buf_size->buf;

	WRITEL(dec->dsc.ofs, S5P_FIMV_SI_CH0_DESC_ADR);
	WRITEL(buf_size->desc_buf, S5P_FIMV_SI_CH0_DESC_SIZE);
}

/* Set registers for shared buffer */
void s5p_mfc_set_shared_buffer(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	WRITEL(ctx->shm.ofs, S5P_FIMV_SI_CH0_HOST_WR_ADR);
}

/* Set registers for decoding stream buffer */
int s5p_mfc_set_dec_stream_buffer(struct s5p_mfc_ctx *ctx, dma_addr_t buf_addr,
		  unsigned int start_num_byte, unsigned int buf_size)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf_size *variant_buf_size = dev->variant->buf_size;

	mfc_debug_enter();
	mfc_debug(2, "inst_no: %d, buf_addr: 0x%08lx, buf_size: 0x"\
		"%08x (%d)\n",  ctx->inst_no, (unsigned long)buf_addr,
		buf_size, buf_size);
	WRITEL(OFFSETA(buf_addr), S5P_FIMV_SI_CH0_SB_ST_ADR);
	WRITEL(variant_buf_size->cpb_buf, S5P_FIMV_SI_CH0_CPB_SIZE);
	WRITEL(buf_size, S5P_FIMV_SI_CH0_SB_FRM_SIZE);
	mfc_debug(2, "Shared_virt: %p (start offset: %d)\n",
					ctx->shm.virt, start_num_byte);
	s5p_mfc_write_info(ctx, start_num_byte, START_BYTE_NUM);
	mfc_debug_leave();
	return 0;
}

#define IS_H264(c)	((c)->codec_mode == S5P_FIMV_CODEC_H264_DEC)
/* Set decoding frame buffer */
int s5p_mfc_set_dec_frame_buffer(struct s5p_mfc_ctx *ctx)
{
	unsigned int frame_size, i;
	unsigned int frame_size_ch, frame_size_mv;
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_dec *dec = ctx->dec_priv;
	unsigned int dpb;
	unsigned char *dpb_vir;
	dma_addr_t buf_addr1, buf_addr2;
	int buf_size1, buf_size2;
	struct s5p_mfc_buf *buf;
	struct list_head *buf_queue;

	buf_addr1 = ctx->port_a_phys;
	buf_size1 = ctx->port_a_size;
	buf_addr2 = ctx->port_b_phys;
	buf_size2 = ctx->port_b_size;

	mfc_debug(2, "Buf1: %p (%d) Buf2: %p (%d)\n",
		  (void *)buf_addr1, buf_size1,
		  (void *)buf_addr2, buf_size2);
	mfc_debug(2, "Total DPB COUNT: %d\n", dec->total_dpb_count);
	mfc_debug(2, "Setting display delay to %d\n", dec->display_delay);

	dpb = READL(S5P_FIMV_SI_CH0_DPB_CONF_CTRL) & ~S5P_FIMV_DPB_COUNT_MASK;
	WRITEL(dec->total_dpb_count | dpb, S5P_FIMV_SI_CH0_DPB_CONF_CTRL);

	s5p_mfc_set_shared_buffer(ctx);

	switch (ctx->codec_mode) {
	case S5P_FIMV_CODEC_H264_DEC:
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_H264_VERT_NB_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_VERT_NB_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_VERT_NB_MV_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_H264_NB_IP_ADR);
		buf_addr1 += S5P_FIMV_DEC_NB_IP_SIZE;
		buf_size1 -= S5P_FIMV_DEC_NB_IP_SIZE;
		break;
	case S5P_FIMV_CODEC_MPEG4_DEC:
	case S5P_FIMV_CODEC_FIMV1_DEC:
	case S5P_FIMV_CODEC_FIMV2_DEC:
	case S5P_FIMV_CODEC_FIMV3_DEC:
	case S5P_FIMV_CODEC_FIMV4_DEC:
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_MPEG4_NB_DCAC_ADR);
		buf_addr1 += S5P_FIMV_DEC_NB_DCAC_SIZE;
		buf_size1 -= S5P_FIMV_DEC_NB_DCAC_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_MPEG4_UP_NB_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_UPNB_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_UPNB_MV_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_MPEG4_SA_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_MPEG4_SP_ADR);
		buf_addr1 += S5P_FIMV_DEC_STX_PARSER_SIZE;
		buf_size1 -= S5P_FIMV_DEC_STX_PARSER_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_MPEG4_OT_LINE_ADR);
		buf_addr1 += S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE;
		buf_size1 -= S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE;
		break;
	case S5P_FIMV_CODEC_H263_DEC:
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_H263_OT_LINE_ADR);
		buf_addr1 += S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE;
		buf_size1 -= S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_H263_UP_NB_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_UPNB_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_UPNB_MV_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_H263_SA_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_H263_NB_DCAC_ADR);
		buf_addr1 += S5P_FIMV_DEC_NB_DCAC_SIZE;
		buf_size1 -= S5P_FIMV_DEC_NB_DCAC_SIZE;
		break;
	case S5P_FIMV_CODEC_VC1_DEC:
	case S5P_FIMV_CODEC_VC1RCV_DEC:
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_VC1_NB_DCAC_ADR);
		buf_addr1 += S5P_FIMV_DEC_NB_DCAC_SIZE;
		buf_size1 -= S5P_FIMV_DEC_NB_DCAC_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_VC1_OT_LINE_ADR);
		buf_addr1 += S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE;
		buf_size1 -= S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_VC1_UP_NB_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_UPNB_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_UPNB_MV_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_VC1_SA_MV_ADR);
		buf_addr1 += S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE;
		buf_size1 -= S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_VC1_BITPLANE3_ADR);
		buf_addr1 += S5P_FIMV_DEC_VC1_BITPLANE_SIZE;
		buf_size1 -= S5P_FIMV_DEC_VC1_BITPLANE_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_VC1_BITPLANE2_ADR);
		buf_addr1 += S5P_FIMV_DEC_VC1_BITPLANE_SIZE;
		buf_size1 -= S5P_FIMV_DEC_VC1_BITPLANE_SIZE;
		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_VC1_BITPLANE1_ADR);
		buf_addr1 += S5P_FIMV_DEC_VC1_BITPLANE_SIZE;
		buf_size1 -= S5P_FIMV_DEC_VC1_BITPLANE_SIZE;
		break;
	case S5P_FIMV_CODEC_MPEG2_DEC:
		break;
	default:
		mfc_err("Unknown codec for decoding (%x).\n",
			ctx->codec_mode);
		return -EINVAL;
		break;
	}
	frame_size = ctx->luma_size;
	frame_size_ch = ctx->chroma_size;
	frame_size_mv = ctx->mv_size;
	mfc_debug(2, "Frame size: %d ch: %d mv: %d\n", frame_size, frame_size_ch,
								frame_size_mv);

	i = 0;
	if (dec->dst_memtype == V4L2_MEMORY_USERPTR ||
			dec->dst_memtype == V4L2_MEMORY_DMABUF)
		buf_queue = &ctx->dst_queue;
	else
		buf_queue = &dec->dpb_queue;
	list_for_each_entry(buf, buf_queue, list) {
		mfc_debug(2, "Luma 0x%08lx\n",
				(unsigned long)buf->planes.raw.luma);
		WRITEL(OFFSETB(buf->planes.raw.luma),
					S5P_FIMV_DEC_LUMA_ADR + i * 4);
		mfc_debug(2, "\tChroma 0x%08lx\n",
				(unsigned long)buf->planes.raw.chroma);
		WRITEL(OFFSETA(buf->planes.raw.chroma),
					S5P_FIMV_DEC_CHROMA_ADR + i * 4);

		if (ctx->codec_mode == S5P_FIMV_CODEC_H264_DEC) {
			mfc_debug(2, "\tBuf2: 0x%08lx, size: %d\n",
					(unsigned long)buf_addr2, buf_size2);
			WRITEL(OFFSETB(buf_addr2), S5P_FIMV_H264_MV_ADR + i * 4);
			buf_addr2 += frame_size_mv;
			buf_size2 -= frame_size_mv;
		}

		/*
		 * Fill the default color for the stream starting with p-frame.
		 * Reference frame is different by the codec type.
		 * - H.264 : Last DPB
		 * - Non H.264 : First DPB
		 */
		if ((!ctx->is_drm) &&
			(((IS_H264(ctx)) && (i == dec->total_dpb_count - 1)) ||
			((!IS_H264(ctx)) && (i == 0)))) {
			dpb_vir = vb2_plane_vaddr(&buf->vb, 0);
			if (dpb_vir)
				memset(dpb_vir, 0x0, ctx->luma_size);

			dpb_vir = vb2_plane_vaddr(&buf->vb, 1);
			if (dpb_vir)
				memset(dpb_vir, 0x80, ctx->chroma_size);

			s5p_mfc_mem_clean_vb(&buf->vb, 2);
		}
		i++;
	}

	mfc_debug(2, "Buf1: 0x%08lx, buf_size1: %d\n",
			(unsigned long)buf_addr1, buf_size1);
	mfc_debug(2, "Buf 1/2 size after: %d/%d (frames %d)\n",
			buf_size1,  buf_size2, dec->total_dpb_count);
	if (buf_size1 < 0 || buf_size2 < 0) {
		mfc_debug(2, "Not enough memory has been allocated.\n");
		return -ENOMEM;
	}

	s5p_mfc_write_info(ctx, frame_size, ALLOC_LUMA_DPB_SIZE);
	s5p_mfc_write_info(ctx, frame_size_ch, ALLOC_CHROMA_DPB_SIZE);

	if (ctx->codec_mode == S5P_FIMV_CODEC_H264_DEC)
		s5p_mfc_write_info(ctx, frame_size_mv, ALLOC_MV_SIZE);

	WRITEL(((S5P_FIMV_CH_INIT_BUFS & S5P_FIMV_CH_MASK) << S5P_FIMV_CH_SHIFT)
				| (ctx->inst_no), S5P_FIMV_SI_CH0_INST_ID);

	mfc_debug(2, "After setting buffers.\n");
	return 0;
}

/* Set registers for encoding stream buffer */
int s5p_mfc_set_enc_stream_buffer(struct s5p_mfc_ctx *ctx,
		dma_addr_t addr, unsigned int size)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	WRITEL(OFFSETA(addr), S5P_FIMV_ENC_SI_CH0_SB_ADR);
	WRITEL(size, S5P_FIMV_ENC_SI_CH0_SB_SIZE);

	return 0;
}

void s5p_mfc_set_enc_frame_buffer(struct s5p_mfc_ctx *ctx,
		dma_addr_t y_addr, dma_addr_t c_addr)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	WRITEL(OFFSETB(y_addr), S5P_FIMV_ENC_SI_CH0_CUR_Y_ADR);
	WRITEL(OFFSETB(c_addr), S5P_FIMV_ENC_SI_CH0_CUR_C_ADR);
}

void s5p_mfc_get_enc_frame_buffer(struct s5p_mfc_ctx *ctx,
		dma_addr_t *y_addr, dma_addr_t *c_addr)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	*y_addr = dev->port_b + (READL(S5P_FIMV_ENCODED_Y_ADDR) << 11);
	*c_addr = dev->port_b + (READL(S5P_FIMV_ENCODED_C_ADDR) << 11);
}

/* Set encoding ref & codec buffer */
int s5p_mfc_set_enc_ref_buffer(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	size_t buf_addr1, buf_addr2;
	size_t buf_size1, buf_size2;
	unsigned int enc_ref_y_size, enc_ref_c_size;
	unsigned int guard_width, guard_height;
	int i;

	mfc_debug_enter();

	buf_addr1 = ctx->port_a_phys;
	buf_size1 = ctx->port_a_size;
	buf_addr2 = ctx->port_b_phys;
	buf_size2 = ctx->port_b_size;

	enc_ref_y_size = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN)
		* ALIGN(ctx->img_height, S5P_FIMV_NV12MT_VALIGN);
	enc_ref_y_size = ALIGN(enc_ref_y_size, S5P_FIMV_NV12MT_SALIGN);

	if (ctx->codec_mode == S5P_FIMV_CODEC_H264_ENC) {
		enc_ref_c_size = ALIGN(ctx->img_width, S5P_FIMV_NV12MT_HALIGN)
			* ALIGN((ctx->img_height >> 1), S5P_FIMV_NV12MT_VALIGN);
		enc_ref_c_size = ALIGN(enc_ref_c_size, S5P_FIMV_NV12MT_SALIGN);
	} else {
		guard_width = ALIGN(ctx->img_width + 16, S5P_FIMV_NV12MT_HALIGN);
		guard_height = ALIGN((ctx->img_height >> 1) + 4, S5P_FIMV_NV12MT_VALIGN);
		enc_ref_c_size = ALIGN(guard_width * guard_height,
				       S5P_FIMV_NV12MT_SALIGN);
	}

	mfc_debug(2, "buf_size1: %d, buf_size2: %d\n", buf_size1, buf_size2);

	switch (ctx->codec_mode) {
	case S5P_FIMV_CODEC_H264_ENC:
		for (i = 0; i < ENC_REF_2; i++) {
			WRITEL(OFFSETA(buf_addr1),
				S5P_FIMV_ENC_REF0_LUMA_ADR + (4 * i));
			buf_addr1 += enc_ref_y_size;
			buf_size1 -= enc_ref_y_size;

			if (ctx->dpb_count == ENC_REF_4) {
				WRITEL(OFFSETB(buf_addr2),
					S5P_FIMV_ENC_REF2_LUMA_ADR + (4 * i));
				buf_addr2 += enc_ref_y_size;
				buf_size2 -= enc_ref_y_size;
			}
		}

		for (i = 0; i < ctx->dpb_count; i++) {
			WRITEL(OFFSETB(buf_addr2),
				S5P_FIMV_ENC_REF0_CHROMA_ADR + (4 * i));
			buf_addr2 += enc_ref_c_size;
			buf_size2 -= enc_ref_c_size;
		}

		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_H264_UP_MV_ADR);
		buf_addr1 += S5P_FIMV_ENC_UPMV_SIZE;
		buf_size1 -= S5P_FIMV_ENC_UPMV_SIZE;

		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_H264_COZERO_FLAG_ADR);
		buf_addr1 += S5P_FIMV_ENC_COLFLG_SIZE;
		buf_size1 -= S5P_FIMV_ENC_COLFLG_SIZE;

		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_H264_UP_INTRA_MD_ADR);
		buf_addr1 += S5P_FIMV_ENC_INTRAMD_SIZE;
		buf_size1 -= S5P_FIMV_ENC_INTRAMD_SIZE;

		WRITEL(OFFSETB(buf_addr2), S5P_FIMV_H264_UP_INTRA_PRED_ADR);
		buf_addr2 += S5P_FIMV_ENC_INTRAPRED_SIZE;
		buf_size2 -= S5P_FIMV_ENC_INTRAPRED_SIZE;

		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_H264_NBOR_INFO_ADR);
		buf_addr1 += S5P_FIMV_ENC_NBORINFO_SIZE;
		buf_size1 -= S5P_FIMV_ENC_NBORINFO_SIZE;

		mfc_debug(2, "buf_size1: %d, buf_size2: %d\n",
			buf_size1, buf_size2);
		break;

	case S5P_FIMV_CODEC_MPEG4_ENC:
		for (i = 0; i < ENC_REF_2; i++) {
			WRITEL(OFFSETA(buf_addr1),
				S5P_FIMV_ENC_REF0_LUMA_ADR + (4 * i));
			buf_addr1 += enc_ref_y_size;
			buf_size1 -= enc_ref_y_size;

			if (ctx->dpb_count == ENC_REF_4) {
				WRITEL(OFFSETB(buf_addr2),
					S5P_FIMV_ENC_REF2_LUMA_ADR + (4 * i));
				buf_addr2 += enc_ref_y_size;
				buf_size2 -= enc_ref_y_size;
			}
		}

		for (i = 0; i < ctx->dpb_count; i++) {
			WRITEL(OFFSETB(buf_addr2),
				S5P_FIMV_ENC_REF0_CHROMA_ADR + (4 * i));
			buf_addr2 += enc_ref_c_size;
			buf_size2 -= enc_ref_c_size;
		}

		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_MPEG4_UP_MV_ADR);
		buf_addr1 += S5P_FIMV_ENC_UPMV_SIZE;
		buf_size1 -= S5P_FIMV_ENC_UPMV_SIZE;

		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_MPEG4_COZERO_FLAG_ADR);
		buf_addr1 += S5P_FIMV_ENC_COLFLG_SIZE;
		buf_size1 -= S5P_FIMV_ENC_COLFLG_SIZE;

		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_MPEG4_ACDC_COEF_ADR);
		buf_addr1 += S5P_FIMV_ENC_ACDCCOEF_SIZE;
		buf_size1 -= S5P_FIMV_ENC_ACDCCOEF_SIZE;

		mfc_debug(2, "buf_size1: %d, buf_size2: %d\n",
			buf_size1, buf_size2);
		break;

	case S5P_FIMV_CODEC_H263_ENC:
		for (i = 0; i < ENC_REF_2; i++) {
			WRITEL(OFFSETA(buf_addr1),
				S5P_FIMV_ENC_REF0_LUMA_ADR + (4 * i));
			buf_addr1 += enc_ref_y_size;
			buf_size1 -= enc_ref_y_size;
		}

		for (i = 0; i < ctx->dpb_count; i++) {
			WRITEL(OFFSETB(buf_addr2),
				S5P_FIMV_ENC_REF0_CHROMA_ADR + (4 * i));
			buf_addr2 += enc_ref_c_size;
			buf_size2 -= enc_ref_c_size;
		}

		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_H263_UP_MV_ADR);
		buf_addr1 += S5P_FIMV_ENC_UPMV_SIZE;
		buf_size1 -= S5P_FIMV_ENC_UPMV_SIZE;

		WRITEL(OFFSETA(buf_addr1), S5P_FIMV_H263_ACDC_COEF_ADR);
		buf_addr1 += S5P_FIMV_ENC_ACDCCOEF_SIZE;
		buf_size1 -= S5P_FIMV_ENC_ACDCCOEF_SIZE;

		mfc_debug(2, "buf_size1: %d, buf_size2: %d\n",
			buf_size1, buf_size2);
		break;

	default:
		mfc_err("Unknown codec set for encoding: %d\n",
			ctx->codec_mode);
		return -EINVAL;
	}

	mfc_debug_leave();

	return 0;
}

static int s5p_mfc_set_enc_params(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	struct s5p_mfc_enc_params *p = &enc->params;
	unsigned int reg;
	unsigned int shm;

	mfc_debug_enter();

	/* width */
	WRITEL(ctx->img_width, S5P_FIMV_ENC_HSIZE_PX);
	/* height */
	WRITEL(ctx->img_height, S5P_FIMV_ENC_VSIZE_PX);

	/* pictype : enable, IDR period */
	reg = READL(S5P_FIMV_ENC_PIC_TYPE_CTRL);
	reg |= (1 << 18);
	reg &= ~(0xFFFF);
	reg |= p->gop_size;
	WRITEL(reg, S5P_FIMV_ENC_PIC_TYPE_CTRL);

	WRITEL(0, S5P_FIMV_ENC_B_RECON_WRITE_ON);

	/* multi-slice control */
	/* multi-slice MB number or bit size */
	if (p->slice_mode == V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_MB) {
		WRITEL(((0x0 << 1) | 0x1), S5P_FIMV_ENC_MSLICE_CTRL);
		WRITEL(p->slice_mb, S5P_FIMV_ENC_MSLICE_MB);
	} else if (p->slice_mode == V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_BYTES) {
		WRITEL(((0x1 << 1) | 0x1), S5P_FIMV_ENC_MSLICE_CTRL);
		WRITEL(p->slice_bit, S5P_FIMV_ENC_MSLICE_BIT);
	} else {
		WRITEL(0, S5P_FIMV_ENC_MSLICE_CTRL);
		WRITEL(0, S5P_FIMV_ENC_MSLICE_MB);
		WRITEL(0, S5P_FIMV_ENC_MSLICE_BIT);
	}

	/* cyclic intra refresh */
	WRITEL(p->intra_refresh_mb, S5P_FIMV_ENC_CIR_CTRL);

	/* memory structure cur. frame */
	if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12M)
		WRITEL(0, S5P_FIMV_ENC_MAP_FOR_CUR);
	else if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12MT)
		WRITEL(3, S5P_FIMV_ENC_MAP_FOR_CUR);

	/* padding control & value */
	reg = READL(S5P_FIMV_ENC_PADDING_CTRL);
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
	WRITEL(reg, S5P_FIMV_ENC_PADDING_CTRL);

	/* rate control config. */
	reg = READL(S5P_FIMV_ENC_RC_CONFIG);
	/** frame-level rate control */
	reg &= ~(0x1 << 9);
	reg |= (p->rc_frame << 9);
	WRITEL(reg, S5P_FIMV_ENC_RC_CONFIG);

	/* bit rate */
	if (p->rc_frame)
		WRITEL(p->rc_bitrate,
			S5P_FIMV_ENC_RC_BIT_RATE);
	else
		WRITEL(0, S5P_FIMV_ENC_RC_BIT_RATE);

	/* reaction coefficient */
	if (p->rc_frame)
		WRITEL(p->rc_reaction_coeff, S5P_FIMV_ENC_RC_RPARA);

	/* extended encoder ctrl */
	shm = s5p_mfc_read_info(ctx, EXT_ENC_CONTROL);
	/** vbv buffer size */
	if (p->frame_skip_mode == V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT) {
		shm &= ~(0xFFFF << 16);
		shm |= (p->vbv_buf_size << 16);
	}
	/** seq header ctrl */
	shm &= ~(0x1 << 3);
	shm |= (p->seq_hdr_mode << 3);
	/** frame skip mode */
	shm &= ~(0x3 << 1);
	shm |= (p->frame_skip_mode << 1);
	s5p_mfc_write_info(ctx, shm, EXT_ENC_CONTROL);

	/* fixed target bit */
	s5p_mfc_write_info(ctx, p->fixed_target_bit, RC_CONTROL_CONFIG);

	mfc_debug_leave();

	return 0;
}

static int s5p_mfc_set_enc_params_h264(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	struct s5p_mfc_enc_params *p = &enc->params;
	struct s5p_mfc_h264_enc_params *p_264 = &p->codec.h264;
	unsigned int reg;
	unsigned int shm;

	mfc_debug_enter();

	s5p_mfc_set_enc_params(ctx);

	/* pictype : number of B */
	reg = READL(S5P_FIMV_ENC_PIC_TYPE_CTRL);
	/** num_b_frame - 0 ~ 2 */
	reg &= ~(0x3 << 16);
	reg |= (p->num_b_frame << 16);
	WRITEL(reg, S5P_FIMV_ENC_PIC_TYPE_CTRL);

	/* profile & level */
	reg = READL(S5P_FIMV_ENC_PROFILE);
	/** level */
	reg &= ~(0xFF << 8);
	reg |= (p_264->level << 8);
	/** profile - 0 ~ 2 */
	reg &= ~(0x3F);
	reg |= p_264->profile;
	WRITEL(reg, S5P_FIMV_ENC_PROFILE);

	/* interlace  */
	WRITEL(p_264->interlace, S5P_FIMV_ENC_PIC_STRUCT);
	/** height */
	if (p_264->interlace)
		WRITEL(ctx->img_height >> 1, S5P_FIMV_ENC_VSIZE_PX);

	/* loopfilter ctrl */
	WRITEL(p_264->loop_filter_mode, S5P_FIMV_ENC_LF_CTRL);

	/* loopfilter alpha offset */
	if (p_264->loop_filter_alpha < 0) {
		reg = 0x10;
		reg |= (0xFF - p_264->loop_filter_alpha) + 1;
	} else {
		reg = 0x00;
		reg |= (p_264->loop_filter_alpha & 0xF);
	}
	WRITEL(reg, S5P_FIMV_ENC_ALPHA_OFF);

	/* loopfilter beta offset */
	if (p_264->loop_filter_beta < 0) {
		reg = 0x10;
		reg |= (0xFF - p_264->loop_filter_beta) + 1;
	} else {
		reg = 0x00;
		reg |= (p_264->loop_filter_beta & 0xF);
	}
	WRITEL(reg, S5P_FIMV_ENC_BETA_OFF);

	/* entropy coding mode */
	WRITEL(p_264->entropy_mode, S5P_FIMV_ENC_H264_ENTRP_MODE);

	/* number of ref. picture */
	reg = READL(S5P_FIMV_ENC_H264_NUM_OF_REF);
	/** num of ref. pictures of P */
	reg &= ~(0x3 << 5);
	reg |= (p_264->num_ref_pic_4p << 5);
	WRITEL(reg, S5P_FIMV_ENC_H264_NUM_OF_REF);

	/* 8x8 transform enable */
	WRITEL(p_264->_8x8_transform, S5P_FIMV_ENC_H264_TRANS_FLAG);

	/* rate control config. */
	reg = READL(S5P_FIMV_ENC_RC_CONFIG);
	/** macroblock level rate control */
	reg &= ~(0x1 << 8);
	reg |= (p->rc_mb << 8);
	/** frame QP */
	reg &= ~(0x3F);
	reg |= p_264->rc_frame_qp;
	WRITEL(reg, S5P_FIMV_ENC_RC_CONFIG);

	/* frame rate */
	if (p->rc_frame)
		WRITEL(p_264->rc_framerate * 1000,
			S5P_FIMV_ENC_RC_FRAME_RATE);
	else
		WRITEL(0, S5P_FIMV_ENC_RC_FRAME_RATE);

	/* max & min value of QP */
	reg = READL(S5P_FIMV_ENC_RC_QBOUND);
	/** max QP */
	reg &= ~(0x3F << 8);
	reg |= (p_264->rc_max_qp << 8);
	/** min QP */
	reg &= ~(0x3F);
	reg |= p_264->rc_min_qp;
	WRITEL(reg, S5P_FIMV_ENC_RC_QBOUND);

	/* macroblock adaptive scaling features */
	if (p->rc_mb) {
		reg = READL(S5P_FIMV_ENC_RC_MB_CTRL);
		/** dark region */
		reg &= ~(0x1 << 3);
		reg |= (p_264->rc_mb_dark << 3);
		/** smooth region */
		reg &= ~(0x1 << 2);
		reg |= (p_264->rc_mb_smooth << 2);
		/** static region */
		reg &= ~(0x1 << 1);
		reg |= (p_264->rc_mb_static << 1);
		/** high activity region */
		reg &= ~(0x1);
		reg |= p_264->rc_mb_activity;
		WRITEL(reg, S5P_FIMV_ENC_RC_MB_CTRL);
	}

	if (!p->rc_frame && !p->rc_mb) {
		shm = s5p_mfc_read_info(ctx, P_B_FRAME_QP);
		shm &= ~(0xFFF);
		shm |= ((p_264->rc_b_frame_qp & 0x3F) << 6);
		shm |= (p_264->rc_p_frame_qp & 0x3F);
		s5p_mfc_write_info(ctx, shm, P_B_FRAME_QP);
	}

	/* extended encoder ctrl */
	shm = s5p_mfc_read_info(ctx, EXT_ENC_CONTROL);
	/** AR VUI control */
	shm &= ~(0x1 << 15);
	shm |= (p_264->ar_vui << 1);
	shm &= ~(0x1 << 8);
	shm |= (p_264->prepend_sps_pps_to_idr << 8);
	s5p_mfc_write_info(ctx, shm, EXT_ENC_CONTROL);

	if (p_264->ar_vui) {
		/* aspect ration IDC */
		shm = s5p_mfc_read_info(ctx, ASPECT_RATIO_IDC);
		shm &= ~(0xFF);
		shm |= p_264->ar_vui_idc;
		s5p_mfc_write_info(ctx, shm, ASPECT_RATIO_IDC);

		if (p_264->ar_vui_idc == 0xFF) {
			/* sample  AR info */
			shm = s5p_mfc_read_info(ctx, EXTENDED_SAR);
			shm &= ~(0xFFFFFFFF);
			shm |= p_264->ext_sar_width << 16;
			shm |= p_264->ext_sar_height;
			s5p_mfc_write_info(ctx, shm, EXTENDED_SAR);
		}
	}

	/* intra picture period for H.264 */
	shm = s5p_mfc_read_info(ctx, H264_I_PERIOD);
	/** control */
	shm &= ~(0x1 << 16);
	shm |= (p_264->open_gop << 16);
	/** value */
	if (p_264->open_gop) {
		shm &= ~(0xFFFF);
		shm |= p_264->open_gop_size;
	}
	s5p_mfc_write_info(ctx, shm, H264_I_PERIOD);

	/* set frame pack sei generation */
	if (p_264->sei_gen_enable) {
		/* frame packing enable */
		shm = s5p_mfc_read_info(ctx, FRAME_PACK_SEI_ENABLE);
		shm |= (1 << 1);
		s5p_mfc_write_info(ctx, shm, FRAME_PACK_SEI_ENABLE);

		/* set current frame0 flag & arrangement type */
		shm = 0;
		/** current frame0 flag */
		shm |= ((p_264->sei_fp_curr_frame_0 & 0x1) << 2);
		/** arrangement type
		 *(spec. Table D-8. Definition of frame_packing_arrangement_type)
		 */
		shm |= (p_264->sei_fp_arrangement_type - 3) & 0x3;
		s5p_mfc_write_info(ctx, shm, FRAME_PACK_SEI_INFO);
	}

	mfc_debug_leave();

	return 0;
}

static int s5p_mfc_set_enc_params_mpeg4(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	struct s5p_mfc_enc_params *p = &enc->params;
	struct s5p_mfc_mpeg4_enc_params *p_mpeg4 = &p->codec.mpeg4;
	unsigned int reg;
	unsigned int shm;

	mfc_debug_enter();

	s5p_mfc_set_enc_params(ctx);

	/* pictype : number of B */
	reg = READL(S5P_FIMV_ENC_PIC_TYPE_CTRL);
	/** num_b_frame - 0 ~ 2 */
	reg &= ~(0x3 << 16);
	reg |= (p->num_b_frame << 16);
	WRITEL(reg, S5P_FIMV_ENC_PIC_TYPE_CTRL);

	/* profile & level */
	reg = READL(S5P_FIMV_ENC_PROFILE);
	/** level */
	reg &= ~(0xFF << 8);
	reg |= (p_mpeg4->level << 8);
	/** profile - 0 ~ 2 */
	reg &= ~(0x3F);
	reg |= p_mpeg4->profile;
	WRITEL(reg, S5P_FIMV_ENC_PROFILE);

	/* quarter_pixel */
	WRITEL(p_mpeg4->quarter_pixel, S5P_FIMV_ENC_MPEG4_QUART_PXL);

	/* qp */
	if (!p->rc_frame) {
		shm = s5p_mfc_read_info(ctx, P_B_FRAME_QP);
		shm &= ~(0xFFF);
		shm |= ((p_mpeg4->rc_b_frame_qp & 0x3F) << 6);
		shm |= (p_mpeg4->rc_p_frame_qp & 0x3F);
		s5p_mfc_write_info(ctx, shm, P_B_FRAME_QP);
	}

	/* frame rate */
	if (p->rc_frame) {
		if (p_mpeg4->vop_frm_delta > 0) {
			p_mpeg4->rc_framerate = p_mpeg4->vop_time_res /
						p_mpeg4->vop_frm_delta;
			WRITEL(p_mpeg4->rc_framerate * 1000,
				S5P_FIMV_ENC_RC_FRAME_RATE);
			shm = s5p_mfc_read_info(ctx, RC_VOP_TIMING);
			shm &= ~(0xFFFFFFFF);
			shm |= (1 << 31);
			shm |= ((p_mpeg4->vop_time_res & 0x7FFF) << 16);
			shm |= (p_mpeg4->vop_frm_delta & 0xFFFF);
			s5p_mfc_write_info(ctx, shm, RC_VOP_TIMING);
		}
	} else {
		WRITEL(0, S5P_FIMV_ENC_RC_FRAME_RATE);
	}

	/* rate control config. */
	reg = READL(S5P_FIMV_ENC_RC_CONFIG);
	/** frame QP */
	reg &= ~(0x3F);
	reg |= p_mpeg4->rc_frame_qp;
	WRITEL(reg, S5P_FIMV_ENC_RC_CONFIG);

	/* max & min value of QP */
	reg = READL(S5P_FIMV_ENC_RC_QBOUND);
	/** max QP */
	reg &= ~(0x3F << 8);
	reg |= (p_mpeg4->rc_max_qp << 8);
	/** min QP */
	reg &= ~(0x3F);
	reg |= p_mpeg4->rc_min_qp;
	WRITEL(reg, S5P_FIMV_ENC_RC_QBOUND);

	mfc_debug_leave();

	return 0;
}

static int s5p_mfc_set_enc_params_h263(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	struct s5p_mfc_enc_params *p = &enc->params;
	struct s5p_mfc_mpeg4_enc_params *p_mpeg4 = &p->codec.mpeg4;
	unsigned int reg;
	unsigned int shm;

	mfc_debug_enter();

	s5p_mfc_set_enc_params(ctx);

	/* qp */
	if (!p->rc_frame) {
		shm = s5p_mfc_read_info(ctx, P_B_FRAME_QP);
		shm &= ~(0xFFF);
		shm |= (p_mpeg4->rc_p_frame_qp & 0x3F);
		s5p_mfc_write_info(ctx, shm, P_B_FRAME_QP);
	}

	/* frame rate */
	if (p->rc_frame)
		WRITEL(p_mpeg4->rc_framerate * 1000,
			S5P_FIMV_ENC_RC_FRAME_RATE);
	else
		WRITEL(0, S5P_FIMV_ENC_RC_FRAME_RATE);

	/* rate control config. */
	reg = READL(S5P_FIMV_ENC_RC_CONFIG);
	/** frame QP */
	reg &= ~(0x3F);
	reg |= p_mpeg4->rc_frame_qp;
	WRITEL(reg, S5P_FIMV_ENC_RC_CONFIG);

	/* max & min value of QP */
	reg = READL(S5P_FIMV_ENC_RC_QBOUND);
	/** max QP */
	reg &= ~(0x3F << 8);
	reg |= (p_mpeg4->rc_max_qp << 8);
	/** min QP */
	reg &= ~(0x3F);
	reg |= p_mpeg4->rc_min_qp;
	WRITEL(reg, S5P_FIMV_ENC_RC_QBOUND);

	mfc_debug_leave();

	return 0;
}

#if 0
/* Open a new instance and get its number */
int s5p_mfc_open_inst(struct s5p_mfc_ctx *ctx)
{
	int ret;

	mfc_debug_enter();
	mfc_debug(2, "Requested codec mode: %d\n", ctx->codec_mode);
	ret = s5p_mfc_cmd_host2risc(ctx->dev, ctx, \
			S5P_FIMV_H2R_CMD_OPEN_INSTANCE, ctx->codec_mode);
	mfc_debug_leave();
	return ret;
}

/* Close instance */
int s5p_mfc_close_inst(struct s5p_mfc_ctx *ctx)
{
	int ret = 0;
	struct s5p_mfc_dev *dev = ctx->dev;

	mfc_debug_enter();
	if (ctx->state != MFCINST_FREE) {
		ret = s5p_mfc_cmd_host2risc(dev, ctx,
			S5P_FIMV_H2R_CMD_CLOSE_INSTANCE, ctx->inst_no);
	} else {
		ret = -EINVAL;
	}
	mfc_debug_leave();
	return ret;
}
#endif

/* Initialize decoding */
int s5p_mfc_init_decode(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_dec *dec = ctx->dec_priv;

	mfc_debug_enter();
	mfc_debug(2, "InstNo: %d/%d\n", ctx->inst_no, S5P_FIMV_CH_SEQ_HEADER);
	s5p_mfc_set_shared_buffer(ctx);
	mfc_debug(2, "BUFs: %08x %08x %08x %08x %08x\n",
		  READL(S5P_FIMV_SI_CH0_DESC_ADR),
		  READL(S5P_FIMV_SI_CH0_CPB_SIZE),
		  READL(S5P_FIMV_SI_CH0_DESC_SIZE),
		  READL(S5P_FIMV_SI_CH0_SB_ST_ADR),
		  READL(S5P_FIMV_SI_CH0_SB_FRM_SIZE));
	/* Setup loop filter, for decoding this is only valid for MPEG4 */
	if (ctx->codec_mode == S5P_FIMV_CODEC_MPEG4_DEC) {
		mfc_debug(2, "Set loop filter to: %d\n", dec->loop_filter_mpeg4);
		WRITEL(dec->loop_filter_mpeg4, S5P_FIMV_ENC_LF_CTRL);
	} else {
		WRITEL(0, S5P_FIMV_ENC_LF_CTRL);
	}
	/* When user sets desplay_delay to 0,
	 * It works as "display_delay enable" and delay set to 0.
	 * If user wants display_delay disable, It should be
	 * set to negative value. */
	WRITEL(((dec->slice_enable & S5P_FIMV_SLICE_INT_MASK) <<
				S5P_FIMV_SLICE_INT_SHIFT) |
			((dec->display_delay < 0 ? 0 : 1) <<
			S5P_FIMV_DDELAY_ENA_SHIFT) |
			(((dec->display_delay >= 0 ? dec->display_delay : 0) &
			S5P_FIMV_DDELAY_VAL_MASK) << S5P_FIMV_DDELAY_VAL_SHIFT),
			S5P_FIMV_SI_CH0_DPB_CONF_CTRL);
	if (ctx->codec_mode == S5P_FIMV_CODEC_FIMV1_DEC) {
		mfc_debug(2, "Setting FIMV1 resolution to %dx%d\n",
					ctx->img_width, ctx->img_height);
		WRITEL(ctx->img_width, S5P_FIMV_SI_FIMV1_HRESOL);
		WRITEL(ctx->img_height, S5P_FIMV_SI_FIMV1_VRESOL);
	}

	/* sei parse */
	s5p_mfc_write_info(ctx, dec->sei_parse, FRAME_PACK_SEI_ENABLE);

	WRITEL(((S5P_FIMV_CH_SEQ_HEADER & S5P_FIMV_CH_MASK)
			<< S5P_FIMV_CH_SHIFT)
			| (ctx->inst_no), S5P_FIMV_SI_CH0_INST_ID);

	/* Enable CRC data */
	WRITEL(dec->crc_enable << 31, S5P_FIMV_HOST2RISC_ARG2);

	mfc_debug(2, "DELAY : %x\n", READL(S5P_FIMV_SI_CH0_DPB_CONF_CTRL));

	mfc_debug_leave();
	return 0;
}

static inline void s5p_mfc_set_flush(struct s5p_mfc_ctx *ctx, int flush)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned int dpb;
	if (flush)
		dpb = READL(S5P_FIMV_SI_CH0_DPB_CONF_CTRL) | (1 << 14);
	else
		dpb = READL(S5P_FIMV_SI_CH0_DPB_CONF_CTRL) & ~(1 << 14);
	WRITEL(dpb, S5P_FIMV_SI_CH0_DPB_CONF_CTRL);
}

/* Decode a single frame */
int s5p_mfc_decode_one_frame(struct s5p_mfc_ctx *ctx, int last_frame)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_dec *dec = ctx->dec_priv;

	mfc_debug(2, "Setting flags to %08lx (free:%d WTF:%d)\n",
		dec->dpb_status, ctx->dst_queue_cnt, dec->dpb_queue_cnt);

	WRITEL(dec->dpb_status, S5P_FIMV_SI_CH0_RELEASE_BUF);
	s5p_mfc_set_shared_buffer(ctx);
	s5p_mfc_set_flush(ctx, dec->dpb_flush);
	/* Issue different commands to instance basing on whether it
	 * is the last frame or not. */
	switch (last_frame) {
	case 0:
		WRITEL(((S5P_FIMV_CH_FRAME_START & S5P_FIMV_CH_MASK) <<
		S5P_FIMV_CH_SHIFT) | (ctx->inst_no), S5P_FIMV_SI_CH0_INST_ID);
		break;
	case 1:
		WRITEL(((S5P_FIMV_CH_LAST_FRAME & S5P_FIMV_CH_MASK) <<
		S5P_FIMV_CH_SHIFT) | (ctx->inst_no), S5P_FIMV_SI_CH0_INST_ID);
		break;
	case 2:
		WRITEL(((S5P_FIMV_CH_FRAME_START_REALLOC & S5P_FIMV_CH_MASK) <<
		S5P_FIMV_CH_SHIFT) | (ctx->inst_no), S5P_FIMV_SI_CH0_INST_ID);
		break;
	}
	mfc_debug(2, "Decoding a usual frame.\n");
	return 0;
}

int s5p_mfc_init_encode(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	mfc_debug(2, "++\n");

	if (ctx->codec_mode == S5P_FIMV_CODEC_H264_ENC)
		s5p_mfc_set_enc_params_h264(ctx);
	else if (ctx->codec_mode == S5P_FIMV_CODEC_MPEG4_ENC)
		s5p_mfc_set_enc_params_mpeg4(ctx);
	else if (ctx->codec_mode == S5P_FIMV_CODEC_H263_ENC)
		s5p_mfc_set_enc_params_h263(ctx);
	else {
		mfc_err("Unknown codec for encoding (%x).\n",
			ctx->codec_mode);
		return -EINVAL;
	}

	s5p_mfc_set_shared_buffer(ctx);

	WRITEL(((S5P_FIMV_CH_SEQ_HEADER << 16) & 0x70000) | (ctx->inst_no),
		S5P_FIMV_SI_CH0_INST_ID);

	mfc_debug(2, "--\n");

	return 0;
}

/* Encode a single frame */
int s5p_mfc_encode_one_frame(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	mfc_debug(2, "++\n");

	/* memory structure cur. frame */
	if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12M)
		WRITEL(0, S5P_FIMV_ENC_MAP_FOR_CUR);
	else if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12MT)
		WRITEL(3, S5P_FIMV_ENC_MAP_FOR_CUR);

	s5p_mfc_set_shared_buffer(ctx);

	WRITEL((S5P_FIMV_CH_FRAME_START << 16 & 0x70000) | (ctx->inst_no),
		S5P_FIMV_SI_CH0_INST_ID);

	mfc_debug(2, "--\n");

	return 0;
}

static inline int s5p_mfc_get_new_ctx(struct s5p_mfc_dev *dev)
{
	int new_ctx;
	int cnt;

	mfc_debug(2, "Previos context: %d (bits %08lx)\n", dev->curr_ctx,
							dev->ctx_work_bits);
	new_ctx = (dev->curr_ctx + 1) % MFC_NUM_CONTEXTS;
	cnt = 0;
	while (!test_bit(new_ctx, &dev->ctx_work_bits)) {
		new_ctx = (new_ctx + 1) % MFC_NUM_CONTEXTS;
		cnt++;
		if (cnt > MFC_NUM_CONTEXTS) {
			/* No contexts to run */
			return -EAGAIN;
		}
	}
	return new_ctx;
}

static inline void s5p_mfc_run_res_change(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	s5p_mfc_set_dec_desc_buffer(ctx);
	s5p_mfc_set_dec_stream_buffer(ctx, 0, 0, 0);
	dev->curr_ctx = ctx->num;
	s5p_mfc_clean_ctx_int_flags(ctx);
	s5p_mfc_decode_one_frame(ctx, 2);
}

static inline void s5p_mfc_run_dec_last_frames(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	s5p_mfc_set_dec_desc_buffer(ctx);
	s5p_mfc_set_dec_stream_buffer(ctx, 0, 0, 0);
	dev->curr_ctx = ctx->num;
	s5p_mfc_clean_ctx_int_flags(ctx);
	s5p_mfc_decode_one_frame(ctx, 1);
}

static inline int s5p_mfc_run_dec_frame(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *temp_vb;
	unsigned long flags;
	int last_frame = 0;
	unsigned int index;

	spin_lock_irqsave(&dev->irqlock, flags);

	/* Frames are being decoded */
	if (list_empty(&ctx->src_queue)) {
		mfc_debug(2, "No src buffers.\n");
		spin_unlock_irqrestore(&dev->irqlock, flags);
		return -EAGAIN;
	}
	if (ctx->dst_queue_cnt < ctx->dpb_count) {
		spin_unlock_irqrestore(&dev->irqlock, flags);
		return -EAGAIN;
	}
	/* Get the next source buffer */
	temp_vb = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);
	temp_vb->used = 1;
	mfc_debug(2, "Temp vb: %p\n", temp_vb);
	mfc_debug(2, "Src Addr: 0x%08lx\n",
		(unsigned long)s5p_mfc_mem_plane_addr(ctx, &temp_vb->vb, 0));
	s5p_mfc_set_dec_desc_buffer(ctx);
	s5p_mfc_set_dec_stream_buffer(ctx,
			s5p_mfc_mem_plane_addr(ctx, &temp_vb->vb, 0),
			0, temp_vb->vb.v4l2_planes[0].bytesused);
	spin_unlock_irqrestore(&dev->irqlock, flags);

	index = temp_vb->vb.v4l2_buf.index;
	if (call_cop(ctx, set_buf_ctrls_val, ctx, &ctx->src_ctrls[index]) < 0)
		mfc_err("failed in set_buf_ctrls_val\n");

	dev->curr_ctx = ctx->num;
	s5p_mfc_clean_ctx_int_flags(ctx);
	if (temp_vb->vb.v4l2_planes[0].bytesused == 0 ||
			temp_vb->vb.v4l2_buf.input == DEC_LAST_FRAME) {
		last_frame = 1;
		mfc_debug(2, "Setting ctx->state to FINISHING\n");
		ctx->state = MFCINST_FINISHING;
	}
	s5p_mfc_decode_one_frame(ctx, last_frame);

	return 0;
}

static inline int s5p_mfc_run_enc_frame(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned long flags;
	struct s5p_mfc_buf *dst_mb;
	struct s5p_mfc_buf *src_mb;
	dma_addr_t src_y_addr, src_c_addr, dst_addr;
	/*
	unsigned int src_y_size, src_c_size;
	*/
	unsigned int dst_size;
	unsigned int index;

	spin_lock_irqsave(&dev->irqlock, flags);

	if (list_empty(&ctx->src_queue)) {
		mfc_debug(2, "no src buffers.\n");
		spin_unlock_irqrestore(&dev->irqlock, flags);
		return -EAGAIN;
	}

	if (list_empty(&ctx->dst_queue)) {
		mfc_debug(2, "no dst buffers.\n");
		spin_unlock_irqrestore(&dev->irqlock, flags);
		return -EAGAIN;
	}

	src_mb = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);
	src_mb->used = 1;
	src_y_addr = s5p_mfc_mem_plane_addr(ctx, &src_mb->vb, 0);
	src_c_addr = s5p_mfc_mem_plane_addr(ctx, &src_mb->vb, 1);

	mfc_debug(2, "enc src y addr: 0x%08lx", (unsigned long)src_y_addr);
	mfc_debug(2, "enc src c addr: 0x%08lx", (unsigned long)src_c_addr);

	s5p_mfc_set_enc_frame_buffer(ctx, src_y_addr, src_c_addr);

	dst_mb = list_entry(ctx->dst_queue.next, struct s5p_mfc_buf, list);
	dst_mb->used = 1;
	dst_addr = s5p_mfc_mem_plane_addr(ctx, &dst_mb->vb, 0);
	dst_size = vb2_plane_size(&dst_mb->vb, 0);

	s5p_mfc_set_enc_stream_buffer(ctx, dst_addr, dst_size);

	spin_unlock_irqrestore(&dev->irqlock, flags);

	index = src_mb->vb.v4l2_buf.index;
	if (call_cop(ctx, set_buf_ctrls_val, ctx, &ctx->src_ctrls[index]) < 0)
		mfc_err("failed in set_buf_ctrls_val\n");

	dev->curr_ctx = ctx->num;
	s5p_mfc_clean_ctx_int_flags(ctx);
	s5p_mfc_encode_one_frame(ctx);

	return 0;
}

static inline void s5p_mfc_run_init_dec(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned long flags;
	struct s5p_mfc_buf *temp_vb;

	/* Initializing decoding - parsing header */
	spin_lock_irqsave(&dev->irqlock, flags);
	mfc_debug(2, "Preparing to init decoding.\n");
	temp_vb = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);
	s5p_mfc_set_dec_desc_buffer(ctx);
	mfc_debug(2, "Header size: %d\n", temp_vb->vb.v4l2_planes[0].bytesused);
	s5p_mfc_set_dec_stream_buffer(ctx,
			s5p_mfc_mem_plane_addr(ctx, &temp_vb->vb, 0),
			0, temp_vb->vb.v4l2_planes[0].bytesused);
	spin_unlock_irqrestore(&dev->irqlock, flags);
	dev->curr_ctx = ctx->num;
	mfc_debug(2, "Header addr: 0x%08lx\n",
		(unsigned long)s5p_mfc_mem_plane_addr(ctx, &temp_vb->vb, 0));
	s5p_mfc_clean_ctx_int_flags(ctx);
	s5p_mfc_init_decode(ctx);
}

static inline int s5p_mfc_run_init_enc(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned long flags;
	struct s5p_mfc_buf *dst_mb;
	dma_addr_t dst_addr;
	unsigned int dst_size;
	int ret;

	s5p_mfc_set_enc_ref_buffer(ctx);

	spin_lock_irqsave(&dev->irqlock, flags);

	dst_mb = list_entry(ctx->dst_queue.next, struct s5p_mfc_buf, list);
	dst_addr = s5p_mfc_mem_plane_addr(ctx, &dst_mb->vb, 0);
	dst_size = vb2_plane_size(&dst_mb->vb, 0);
	s5p_mfc_set_enc_stream_buffer(ctx, dst_addr, dst_size);

	spin_unlock_irqrestore(&dev->irqlock, flags);

	dev->curr_ctx = ctx->num;
	mfc_debug(2, "Header addr: 0x%08lx\n",
		(unsigned long)s5p_mfc_mem_plane_addr(ctx, &dst_mb->vb, 0));
	s5p_mfc_clean_ctx_int_flags(ctx);
	ret = s5p_mfc_init_encode(ctx);

	return ret;
}

static inline int s5p_mfc_run_init_dec_buffers(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	int ret;
	/* Header was parsed now starting processing
	 * First set the output frame buffers
	 * s5p_mfc_alloc_dec_buffers(ctx); */

	if (ctx->capture_state != QUEUE_BUFS_MMAPED) {
		mfc_err("It seems that not all destionation buffers were "
			"mmaped.\nMFC requires that all destination are mmaped "
			"before starting processing.\n");
		return -EAGAIN;
	}

	dev->curr_ctx = ctx->num;
	s5p_mfc_clean_ctx_int_flags(ctx);
	ret = s5p_mfc_set_dec_frame_buffer(ctx);
	if (ret) {
		mfc_err("Failed to alloc frame mem.\n");
		ctx->state = MFCINST_ERROR;
	}
	return ret;
}

static inline int s5p_mfc_ctx_ready(struct s5p_mfc_ctx *ctx)
{
	if (ctx->type == MFCINST_DECODER)
		return s5p_mfc_dec_ctx_ready(ctx);
	else if (ctx->type == MFCINST_ENCODER)
		return s5p_mfc_enc_ctx_ready(ctx);

	return 0;
}

/* Try running an operation on hardware */
void s5p_mfc_try_run(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_ctx *ctx;
	int new_ctx;
	unsigned int ret = 0;

	mfc_debug(1, "Try run dev: %p\n", dev);

	spin_lock_irq(&dev->condlock);
	/* Check whether hardware is not running */
	if (dev->hw_lock != 0) {
		spin_unlock_irq(&dev->condlock);
		/* This is perfectly ok, the scheduled ctx should wait */
		mfc_debug(1, "Couldn't lock HW.\n");
		return;
	}

	/* Choose the context to run */
	new_ctx = s5p_mfc_get_new_ctx(dev);
	if (new_ctx < 0) {
		/* No contexts to run */
		spin_unlock_irq(&dev->condlock);
		mfc_debug(1, "No ctx is scheduled to be run.\n");
		return;
	}

	ctx = dev->ctx[new_ctx];
	if (test_and_set_bit(ctx->num, &dev->hw_lock) != 0) {
		spin_unlock_irq(&dev->condlock);
		mfc_err("Failed to lock hardware.\n");
		return;
	}
	spin_unlock_irq(&dev->condlock);

	mfc_debug(1, "New context: %d\n", new_ctx);
	mfc_debug(1, "Seting new context to %p\n", ctx);
	/* Got context to run in ctx */
	mfc_debug(1, "ctx->dst_queue_cnt=%d ctx->dpb_count=%d ctx->src_queue_cnt=%d\n",
		ctx->dst_queue_cnt, ctx->dpb_count, ctx->src_queue_cnt);
	mfc_debug(1, "ctx->state=%d\n", ctx->state);
	/* Last frame has already been sent to MFC
	 * Now obtaining frames from MFC buffer */

	dev->curr_ctx_drm = ctx->is_drm;

	s5p_mfc_clock_on();

	if (ctx->type == MFCINST_DECODER) {
		switch (ctx->state) {
		case MFCINST_FINISHING:
			s5p_mfc_run_dec_last_frames(ctx);
			break;
		case MFCINST_RUNNING:
			ret = s5p_mfc_run_dec_frame(ctx);
			break;
		case MFCINST_INIT:
			ret = s5p_mfc_open_inst(ctx);
			break;
		case MFCINST_RETURN_INST:
			ret = s5p_mfc_close_inst(ctx);
			break;
		case MFCINST_GOT_INST:
			s5p_mfc_run_init_dec(ctx);
			break;
		case MFCINST_HEAD_PARSED:
			ret = s5p_mfc_run_init_dec_buffers(ctx);
			break;
		case MFCINST_RES_CHANGE_INIT:
			s5p_mfc_run_res_change(ctx);
			break;
		case MFCINST_RES_CHANGE_FLUSH:
			s5p_mfc_run_dec_frame(ctx);
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
			ret = s5p_mfc_open_inst(ctx);
			break;
		case MFCINST_RETURN_INST:
			ret = s5p_mfc_close_inst(ctx);
			break;
		case MFCINST_GOT_INST:
			ret = s5p_mfc_run_init_enc(ctx);
			break;
		default:
			ret = -EAGAIN;
		}
	} else {
		mfc_err("invalid context type: %d\n", ctx->type);
		ret = -EAGAIN;
	}

	if (ret) {
		/*
		 * Check again the ctx condition and clear work bits
		 * if ctx is not available.
		 */
		if (s5p_mfc_ctx_ready(ctx) == 0) {
			spin_lock_irq(&dev->condlock);
			clear_bit(ctx->num, &dev->ctx_work_bits);
			spin_unlock_irq(&dev->condlock);
		}

		/* Free hardware lock */
		if (test_and_clear_bit(ctx->num, &dev->hw_lock) == 0)
			mfc_err("Failed to unlock hardware.\n");
		s5p_mfc_clock_off();

		/* Trigger again if other instance's work is waiting */
		spin_lock_irq(&dev->condlock);
		if (dev->ctx_work_bits)
			queue_work(dev->sched_wq, &dev->sched_work);
		spin_unlock_irq(&dev->condlock);
	}
}


void s5p_mfc_cleanup_queue(struct list_head *lh, struct vb2_queue *vq)
{
	struct s5p_mfc_buf *b;
	int i;

	while (!list_empty(lh)) {
		b = list_entry(lh->next, struct s5p_mfc_buf, list);
		for (i = 0; i < b->vb.num_planes; i++)
			vb2_set_plane_payload(&b->vb, i, 0);
		vb2_buffer_done(&b->vb, VB2_BUF_STATE_ERROR);
		list_del(&b->list);
	}
}

void s5p_mfc_write_info(struct s5p_mfc_ctx *ctx, unsigned int data, unsigned int ofs)
{
	/* MFC 5.x uses shared memory for information */
	s5p_mfc_write_shm(ctx, data, ofs);
}

unsigned int s5p_mfc_read_info(struct s5p_mfc_ctx *ctx, unsigned int ofs)
{
	/* MFC 5.x uses shared memory for information */
	return s5p_mfc_read_shm(ctx, ofs);
}
