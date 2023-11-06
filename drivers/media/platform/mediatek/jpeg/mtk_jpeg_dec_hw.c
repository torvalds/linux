// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 *         Rick Chang <rick.chang@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <media/media-device.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>

#include "mtk_jpeg_core.h"
#include "mtk_jpeg_dec_hw.h"

#define MTK_JPEG_DUNUM_MASK(val)	(((val) - 1) & 0x3)

enum mtk_jpeg_color {
	MTK_JPEG_COLOR_420		= 0x00221111,
	MTK_JPEG_COLOR_422		= 0x00211111,
	MTK_JPEG_COLOR_444		= 0x00111111,
	MTK_JPEG_COLOR_422V		= 0x00121111,
	MTK_JPEG_COLOR_422X2		= 0x00412121,
	MTK_JPEG_COLOR_422VX2		= 0x00222121,
	MTK_JPEG_COLOR_400		= 0x00110000
};

static const struct of_device_id mtk_jpegdec_hw_ids[] = {
	{
		.compatible = "mediatek,mt8195-jpgdec-hw",
	},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_jpegdec_hw_ids);

static inline int mtk_jpeg_verify_align(u32 val, int align, u32 reg)
{
	if (val & (align - 1)) {
		pr_err("mtk-jpeg: write reg %x without %d align\n", reg, align);
		return -1;
	}

	return 0;
}

static int mtk_jpeg_decide_format(struct mtk_jpeg_dec_param *param)
{
	param->src_color = (param->sampling_w[0] << 20) |
			   (param->sampling_h[0] << 16) |
			   (param->sampling_w[1] << 12) |
			   (param->sampling_h[1] << 8) |
			   (param->sampling_w[2] << 4) |
			   (param->sampling_h[2]);

	param->uv_brz_w = 0;
	switch (param->src_color) {
	case MTK_JPEG_COLOR_444:
		param->uv_brz_w = 1;
		param->dst_fourcc = V4L2_PIX_FMT_YUV422M;
		break;
	case MTK_JPEG_COLOR_422X2:
	case MTK_JPEG_COLOR_422:
		param->dst_fourcc = V4L2_PIX_FMT_YUV422M;
		break;
	case MTK_JPEG_COLOR_422V:
	case MTK_JPEG_COLOR_422VX2:
		param->uv_brz_w = 1;
		param->dst_fourcc = V4L2_PIX_FMT_YUV420M;
		break;
	case MTK_JPEG_COLOR_420:
		param->dst_fourcc = V4L2_PIX_FMT_YUV420M;
		break;
	case MTK_JPEG_COLOR_400:
		param->dst_fourcc = V4L2_PIX_FMT_GREY;
		break;
	default:
		param->dst_fourcc = 0;
		return -1;
	}

	return 0;
}

static void mtk_jpeg_calc_mcu(struct mtk_jpeg_dec_param *param)
{
	u32 factor_w, factor_h;
	u32 i, comp, blk;

	factor_w = 2 + param->sampling_w[0];
	factor_h = 2 + param->sampling_h[0];
	param->mcu_w = (param->pic_w + (1 << factor_w) - 1) >> factor_w;
	param->mcu_h = (param->pic_h + (1 << factor_h) - 1) >> factor_h;
	param->total_mcu = param->mcu_w * param->mcu_h;
	param->unit_num = ((param->pic_w + 7) >> 3) * ((param->pic_h + 7) >> 3);
	param->blk_num = 0;
	for (i = 0; i < MTK_JPEG_COMP_MAX; i++) {
		param->blk_comp[i] = 0;
		if (i >= param->comp_num)
			continue;
		param->blk_comp[i] = param->sampling_w[i] *
				     param->sampling_h[i];
		param->blk_num += param->blk_comp[i];
	}

	param->membership = 0;
	for (i = 0, blk = 0, comp = 0; i < MTK_JPEG_BLOCK_MAX; i++) {
		if (i < param->blk_num && comp < param->comp_num) {
			u32 tmp;

			tmp = (0x04 + (comp & 0x3));
			param->membership |= tmp << (i * 3);
			if (++blk == param->blk_comp[comp]) {
				comp++;
				blk = 0;
			}
		} else {
			param->membership |=  7 << (i * 3);
		}
	}
}

static void mtk_jpeg_calc_dma_group(struct mtk_jpeg_dec_param *param)
{
	u32 factor_mcu = 3;

	if (param->src_color == MTK_JPEG_COLOR_444 &&
	    param->dst_fourcc == V4L2_PIX_FMT_YUV422M)
		factor_mcu = 4;
	else if (param->src_color == MTK_JPEG_COLOR_422V &&
		 param->dst_fourcc == V4L2_PIX_FMT_YUV420M)
		factor_mcu = 4;
	else if (param->src_color == MTK_JPEG_COLOR_422X2 &&
		 param->dst_fourcc == V4L2_PIX_FMT_YUV422M)
		factor_mcu = 2;
	else if (param->src_color == MTK_JPEG_COLOR_400 ||
		 (param->src_color & 0x0FFFF) == 0)
		factor_mcu = 4;

	param->dma_mcu = 1 << factor_mcu;
	param->dma_group = param->mcu_w / param->dma_mcu;
	param->dma_last_mcu = param->mcu_w % param->dma_mcu;
	if (param->dma_last_mcu)
		param->dma_group++;
	else
		param->dma_last_mcu = param->dma_mcu;
}

static int mtk_jpeg_calc_dst_size(struct mtk_jpeg_dec_param *param)
{
	u32 i, padding_w;
	u32 ds_row_h[3];
	u32 brz_w[3];

	brz_w[0] = 0;
	brz_w[1] = param->uv_brz_w;
	brz_w[2] = brz_w[1];

	for (i = 0; i < param->comp_num; i++) {
		if (brz_w[i] > 3)
			return -1;

		padding_w = param->mcu_w * MTK_JPEG_DCTSIZE *
				param->sampling_w[i];
		/* output format is 420/422 */
		param->comp_w[i] = padding_w >> brz_w[i];
		param->comp_w[i] = round_up(param->comp_w[i],
					    MTK_JPEG_DCTSIZE);
		param->img_stride[i] = i ? round_up(param->comp_w[i], 16)
					: round_up(param->comp_w[i], 32);
		ds_row_h[i] = (MTK_JPEG_DCTSIZE * param->sampling_h[i]);
	}
	param->dec_w = param->img_stride[0];
	param->dec_h = ds_row_h[0] * param->mcu_h;

	for (i = 0; i < MTK_JPEG_COMP_MAX; i++) {
		/* They must be equal in frame mode. */
		param->mem_stride[i] = param->img_stride[i];
		param->comp_size[i] = param->mem_stride[i] * ds_row_h[i] *
				      param->mcu_h;
	}

	param->y_size = param->comp_size[0];
	param->uv_size = param->comp_size[1];
	param->dec_size = param->y_size + (param->uv_size << 1);

	return 0;
}

int mtk_jpeg_dec_fill_param(struct mtk_jpeg_dec_param *param)
{
	if (mtk_jpeg_decide_format(param))
		return -1;

	mtk_jpeg_calc_mcu(param);
	mtk_jpeg_calc_dma_group(param);
	if (mtk_jpeg_calc_dst_size(param))
		return -2;

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_jpeg_dec_fill_param);

u32 mtk_jpeg_dec_get_int_status(void __iomem *base)
{
	u32 ret;

	ret = readl(base + JPGDEC_REG_INTERRUPT_STATUS) & BIT_INQST_MASK_ALLIRQ;
	if (ret)
		writel(ret, base + JPGDEC_REG_INTERRUPT_STATUS);

	return ret;
}
EXPORT_SYMBOL_GPL(mtk_jpeg_dec_get_int_status);

u32 mtk_jpeg_dec_enum_result(u32 irq_result)
{
	if (irq_result & BIT_INQST_MASK_EOF)
		return MTK_JPEG_DEC_RESULT_EOF_DONE;
	if (irq_result & BIT_INQST_MASK_PAUSE)
		return MTK_JPEG_DEC_RESULT_PAUSE;
	if (irq_result & BIT_INQST_MASK_UNDERFLOW)
		return MTK_JPEG_DEC_RESULT_UNDERFLOW;
	if (irq_result & BIT_INQST_MASK_OVERFLOW)
		return MTK_JPEG_DEC_RESULT_OVERFLOW;
	if (irq_result & BIT_INQST_MASK_ERROR_BS)
		return MTK_JPEG_DEC_RESULT_ERROR_BS;

	return MTK_JPEG_DEC_RESULT_ERROR_UNKNOWN;
}
EXPORT_SYMBOL_GPL(mtk_jpeg_dec_enum_result);

void mtk_jpeg_dec_start(void __iomem *base)
{
	writel(0, base + JPGDEC_REG_TRIG);
}
EXPORT_SYMBOL_GPL(mtk_jpeg_dec_start);

static void mtk_jpeg_dec_soft_reset(void __iomem *base)
{
	writel(0x0000FFFF, base + JPGDEC_REG_INTERRUPT_STATUS);
	writel(0x00, base + JPGDEC_REG_RESET);
	writel(0x01, base + JPGDEC_REG_RESET);
}

static void mtk_jpeg_dec_hard_reset(void __iomem *base)
{
	writel(0x00, base + JPGDEC_REG_RESET);
	writel(0x10, base + JPGDEC_REG_RESET);
}

void mtk_jpeg_dec_reset(void __iomem *base)
{
	mtk_jpeg_dec_soft_reset(base);
	mtk_jpeg_dec_hard_reset(base);
}
EXPORT_SYMBOL_GPL(mtk_jpeg_dec_reset);

static void mtk_jpeg_dec_set_brz_factor(void __iomem *base, u8 yscale_w,
					u8 yscale_h, u8 uvscale_w, u8 uvscale_h)
{
	u32 val;

	val = (uvscale_h << 12) | (uvscale_w << 8) |
	      (yscale_h << 4) | yscale_w;
	writel(val, base + JPGDEC_REG_BRZ_FACTOR);
}

static void mtk_jpeg_dec_set_dst_bank0(void __iomem *base, u32 addr_y,
				       u32 addr_u, u32 addr_v)
{
	mtk_jpeg_verify_align(addr_y, 16, JPGDEC_REG_DEST_ADDR0_Y);
	writel(addr_y, base + JPGDEC_REG_DEST_ADDR0_Y);
	mtk_jpeg_verify_align(addr_u, 16, JPGDEC_REG_DEST_ADDR0_U);
	writel(addr_u, base + JPGDEC_REG_DEST_ADDR0_U);
	mtk_jpeg_verify_align(addr_v, 16, JPGDEC_REG_DEST_ADDR0_V);
	writel(addr_v, base + JPGDEC_REG_DEST_ADDR0_V);
}

static void mtk_jpeg_dec_set_dst_bank1(void __iomem *base, u32 addr_y,
				       u32 addr_u, u32 addr_v)
{
	writel(addr_y, base + JPGDEC_REG_DEST_ADDR1_Y);
	writel(addr_u, base + JPGDEC_REG_DEST_ADDR1_U);
	writel(addr_v, base + JPGDEC_REG_DEST_ADDR1_V);
}

static void mtk_jpeg_dec_set_mem_stride(void __iomem *base, u32 stride_y,
					u32 stride_uv)
{
	writel((stride_y & 0xFFFF), base + JPGDEC_REG_STRIDE_Y);
	writel((stride_uv & 0xFFFF), base + JPGDEC_REG_STRIDE_UV);
}

static void mtk_jpeg_dec_set_img_stride(void __iomem *base, u32 stride_y,
					u32 stride_uv)
{
	writel((stride_y & 0xFFFF), base + JPGDEC_REG_IMG_STRIDE_Y);
	writel((stride_uv & 0xFFFF), base + JPGDEC_REG_IMG_STRIDE_UV);
}

static void mtk_jpeg_dec_set_pause_mcu_idx(void __iomem *base, u32 idx)
{
	writel(idx & 0x0003FFFFFF, base + JPGDEC_REG_PAUSE_MCU_NUM);
}

static void mtk_jpeg_dec_set_dec_mode(void __iomem *base, u32 mode)
{
	writel(mode & 0x03, base + JPGDEC_REG_OPERATION_MODE);
}

static void mtk_jpeg_dec_set_bs_write_ptr(void __iomem *base, u32 ptr)
{
	mtk_jpeg_verify_align(ptr, 16, JPGDEC_REG_FILE_BRP);
	writel(ptr, base + JPGDEC_REG_FILE_BRP);
}

static void mtk_jpeg_dec_set_bs_info(void __iomem *base, u32 addr, u32 size,
				     u32 bitstream_size)
{
	mtk_jpeg_verify_align(addr, 16, JPGDEC_REG_FILE_ADDR);
	mtk_jpeg_verify_align(size, 128, JPGDEC_REG_FILE_TOTAL_SIZE);
	writel(addr, base + JPGDEC_REG_FILE_ADDR);
	writel(size, base + JPGDEC_REG_FILE_TOTAL_SIZE);
	writel(bitstream_size, base + JPGDEC_REG_BIT_STREAM_SIZE);
}

static void mtk_jpeg_dec_set_comp_id(void __iomem *base, u32 id_y, u32 id_u,
				     u32 id_v)
{
	u32 val;

	val = ((id_y & 0x00FF) << 24) | ((id_u & 0x00FF) << 16) |
	      ((id_v & 0x00FF) << 8);
	writel(val, base + JPGDEC_REG_COMP_ID);
}

static void mtk_jpeg_dec_set_total_mcu(void __iomem *base, u32 num)
{
	writel(num - 1, base + JPGDEC_REG_TOTAL_MCU_NUM);
}

static void mtk_jpeg_dec_set_comp0_du(void __iomem *base, u32 num)
{
	writel(num - 1, base + JPGDEC_REG_COMP0_DATA_UNIT_NUM);
}

static void mtk_jpeg_dec_set_du_membership(void __iomem *base, u32 member,
					   u32 gmc, u32 isgray)
{
	if (isgray)
		member = 0x3FFFFFFC;
	member |= (isgray << 31) | (gmc << 30);
	writel(member, base + JPGDEC_REG_DU_CTRL);
}

static void mtk_jpeg_dec_set_q_table(void __iomem *base, u32 id0, u32 id1,
				     u32 id2)
{
	u32 val;

	val = ((id0 & 0x0f) << 8) | ((id1 & 0x0f) << 4) | ((id2 & 0x0f) << 0);
	writel(val, base + JPGDEC_REG_QT_ID);
}

static void mtk_jpeg_dec_set_dma_group(void __iomem *base, u32 mcu_group,
				       u32 group_num, u32 last_mcu)
{
	u32 val;

	val = (((mcu_group - 1) & 0x00FF) << 16) |
	      (((group_num - 1) & 0x007F) << 8) |
	      ((last_mcu - 1) & 0x00FF);
	writel(val, base + JPGDEC_REG_WDMA_CTRL);
}

static void mtk_jpeg_dec_set_sampling_factor(void __iomem *base, u32 comp_num,
					     u32 y_w, u32 y_h, u32 u_w,
					     u32 u_h, u32 v_w, u32 v_h)
{
	u32 val;
	u32 y_wh = (MTK_JPEG_DUNUM_MASK(y_w) << 2) | MTK_JPEG_DUNUM_MASK(y_h);
	u32 u_wh = (MTK_JPEG_DUNUM_MASK(u_w) << 2) | MTK_JPEG_DUNUM_MASK(u_h);
	u32 v_wh = (MTK_JPEG_DUNUM_MASK(v_w) << 2) | MTK_JPEG_DUNUM_MASK(v_h);

	if (comp_num == 1)
		val = 0;
	else
		val = (y_wh << 8) | (u_wh << 4) | v_wh;
	writel(val, base + JPGDEC_REG_DU_NUM);
}

void mtk_jpeg_dec_set_config(void __iomem *base,
			     struct mtk_jpeg_dec_param *cfg,
			     u32 bitstream_size,
			     struct mtk_jpeg_bs *bs,
			     struct mtk_jpeg_fb *fb)
{
	mtk_jpeg_dec_set_brz_factor(base, 0, 0, cfg->uv_brz_w, 0);
	mtk_jpeg_dec_set_dec_mode(base, 0);
	mtk_jpeg_dec_set_comp0_du(base, cfg->unit_num);
	mtk_jpeg_dec_set_total_mcu(base, cfg->total_mcu);
	mtk_jpeg_dec_set_bs_info(base, bs->str_addr, bs->size, bitstream_size);
	mtk_jpeg_dec_set_bs_write_ptr(base, bs->end_addr);
	mtk_jpeg_dec_set_du_membership(base, cfg->membership, 1,
				       (cfg->comp_num == 1) ? 1 : 0);
	mtk_jpeg_dec_set_comp_id(base, cfg->comp_id[0], cfg->comp_id[1],
				 cfg->comp_id[2]);
	mtk_jpeg_dec_set_q_table(base, cfg->qtbl_num[0],
				 cfg->qtbl_num[1], cfg->qtbl_num[2]);
	mtk_jpeg_dec_set_sampling_factor(base, cfg->comp_num,
					 cfg->sampling_w[0],
					 cfg->sampling_h[0],
					 cfg->sampling_w[1],
					 cfg->sampling_h[1],
					 cfg->sampling_w[2],
					 cfg->sampling_h[2]);
	mtk_jpeg_dec_set_mem_stride(base, cfg->mem_stride[0],
				    cfg->mem_stride[1]);
	mtk_jpeg_dec_set_img_stride(base, cfg->img_stride[0],
				    cfg->img_stride[1]);
	mtk_jpeg_dec_set_dst_bank0(base, fb->plane_addr[0],
				   fb->plane_addr[1], fb->plane_addr[2]);
	mtk_jpeg_dec_set_dst_bank1(base, 0, 0, 0);
	mtk_jpeg_dec_set_dma_group(base, cfg->dma_mcu, cfg->dma_group,
				   cfg->dma_last_mcu);
	mtk_jpeg_dec_set_pause_mcu_idx(base, cfg->total_mcu);
}
EXPORT_SYMBOL_GPL(mtk_jpeg_dec_set_config);

static void mtk_jpegdec_put_buf(struct mtk_jpegdec_comp_dev *jpeg)
{
	struct mtk_jpeg_src_buf *dst_done_buf, *tmp_dst_done_buf;
	struct vb2_v4l2_buffer *dst_buffer;
	struct list_head *temp_entry;
	struct list_head *pos = NULL;
	struct mtk_jpeg_ctx *ctx;
	unsigned long flags;

	ctx = jpeg->hw_param.curr_ctx;
	if (unlikely(!ctx)) {
		dev_err(jpeg->dev, "comp_jpeg ctx fail !!!\n");
		return;
	}

	dst_buffer = jpeg->hw_param.dst_buffer;
	if (!dst_buffer) {
		dev_err(jpeg->dev, "comp_jpeg dst_buffer fail !!!\n");
		return;
	}

	dst_done_buf = container_of(dst_buffer, struct mtk_jpeg_src_buf, b);

	spin_lock_irqsave(&ctx->done_queue_lock, flags);
	list_add_tail(&dst_done_buf->list, &ctx->dst_done_queue);
	while (!list_empty(&ctx->dst_done_queue) &&
	       (pos != &ctx->dst_done_queue)) {
		list_for_each_prev_safe(pos, temp_entry, &ctx->dst_done_queue) {
			tmp_dst_done_buf = list_entry(pos,
						      struct mtk_jpeg_src_buf,
						      list);
			if (tmp_dst_done_buf->frame_num ==
				ctx->last_done_frame_num) {
				list_del(&tmp_dst_done_buf->list);
				v4l2_m2m_buf_done(&tmp_dst_done_buf->b,
						  VB2_BUF_STATE_DONE);
				ctx->last_done_frame_num++;
			}
		}
	}
	spin_unlock_irqrestore(&ctx->done_queue_lock, flags);
}

static void mtk_jpegdec_timeout_work(struct work_struct *work)
{
	enum vb2_buffer_state buf_state = VB2_BUF_STATE_ERROR;
	struct mtk_jpegdec_comp_dev *cjpeg =
		container_of(work, struct mtk_jpegdec_comp_dev,
			     job_timeout_work.work);
	struct mtk_jpeg_dev *master_jpeg = cjpeg->master_dev;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;

	src_buf = cjpeg->hw_param.src_buffer;
	dst_buf = cjpeg->hw_param.dst_buffer;
	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, true);

	mtk_jpeg_dec_reset(cjpeg->reg_base);
	clk_disable_unprepare(cjpeg->jdec_clk.clks->clk);
	pm_runtime_put(cjpeg->dev);
	cjpeg->hw_state = MTK_JPEG_HW_IDLE;
	atomic_inc(&master_jpeg->hw_rdy);
	wake_up(&master_jpeg->hw_wq);
	v4l2_m2m_buf_done(src_buf, buf_state);
	mtk_jpegdec_put_buf(cjpeg);
}

static irqreturn_t mtk_jpegdec_hw_irq_handler(int irq, void *priv)
{
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct mtk_jpeg_src_buf *jpeg_src_buf;
	enum vb2_buffer_state buf_state;
	struct mtk_jpeg_ctx *ctx;
	u32 dec_irq_ret;
	u32 irq_status;
	int i;

	struct mtk_jpegdec_comp_dev *jpeg = priv;
	struct mtk_jpeg_dev *master_jpeg = jpeg->master_dev;

	cancel_delayed_work(&jpeg->job_timeout_work);

	ctx = jpeg->hw_param.curr_ctx;
	src_buf = jpeg->hw_param.src_buffer;
	dst_buf = jpeg->hw_param.dst_buffer;
	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, true);

	irq_status = mtk_jpeg_dec_get_int_status(jpeg->reg_base);
	dec_irq_ret = mtk_jpeg_dec_enum_result(irq_status);
	if (dec_irq_ret >= MTK_JPEG_DEC_RESULT_UNDERFLOW)
		mtk_jpeg_dec_reset(jpeg->reg_base);

	if (dec_irq_ret != MTK_JPEG_DEC_RESULT_EOF_DONE)
		dev_warn(jpeg->dev, "Jpg Dec occurs unknown Err.");

	jpeg_src_buf =
		container_of(src_buf, struct mtk_jpeg_src_buf, b);

	for (i = 0; i < dst_buf->vb2_buf.num_planes; i++)
		vb2_set_plane_payload(&dst_buf->vb2_buf, i,
				      jpeg_src_buf->dec_param.comp_size[i]);

	buf_state = VB2_BUF_STATE_DONE;
	v4l2_m2m_buf_done(src_buf, buf_state);
	mtk_jpegdec_put_buf(jpeg);
	pm_runtime_put(ctx->jpeg->dev);
	clk_disable_unprepare(jpeg->jdec_clk.clks->clk);

	jpeg->hw_state = MTK_JPEG_HW_IDLE;
	wake_up(&master_jpeg->hw_wq);
	atomic_inc(&master_jpeg->hw_rdy);

	return IRQ_HANDLED;
}

static int mtk_jpegdec_hw_init_irq(struct mtk_jpegdec_comp_dev *dev)
{
	struct platform_device *pdev = dev->plat_dev;
	int ret;

	dev->jpegdec_irq = platform_get_irq(pdev, 0);
	if (dev->jpegdec_irq < 0)
		return dev->jpegdec_irq;

	ret = devm_request_irq(&pdev->dev,
			       dev->jpegdec_irq,
			       mtk_jpegdec_hw_irq_handler,
			       0,
			       pdev->name, dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to devm_request_irq %d (%d)",
			dev->jpegdec_irq, ret);
		return ret;
	}

	return 0;
}

static void mtk_jpegdec_destroy_workqueue(void *data)
{
	destroy_workqueue(data);
}

static int mtk_jpegdec_hw_probe(struct platform_device *pdev)
{
	struct mtk_jpegdec_clk *jpegdec_clk;
	struct mtk_jpeg_dev *master_dev;
	struct mtk_jpegdec_comp_dev *dev;
	int ret, i;

	struct device *decs = &pdev->dev;

	if (!decs->parent)
		return -EPROBE_DEFER;

	master_dev = dev_get_drvdata(decs->parent);
	if (!master_dev)
		return -EPROBE_DEFER;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->plat_dev = pdev;
	dev->dev = &pdev->dev;

	ret = devm_add_action_or_reset(&pdev->dev,
				       mtk_jpegdec_destroy_workqueue,
				       master_dev->workqueue);
	if (ret)
		return ret;

	spin_lock_init(&dev->hw_lock);
	dev->hw_state = MTK_JPEG_HW_IDLE;

	INIT_DELAYED_WORK(&dev->job_timeout_work,
			  mtk_jpegdec_timeout_work);

	jpegdec_clk = &dev->jdec_clk;

	jpegdec_clk->clk_num = devm_clk_bulk_get_all(&pdev->dev,
						     &jpegdec_clk->clks);
	if (jpegdec_clk->clk_num < 0)
		return dev_err_probe(&pdev->dev,
				      jpegdec_clk->clk_num,
				      "Failed to get jpegdec clock count.\n");

	dev->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dev->reg_base))
		return PTR_ERR(dev->reg_base);

	ret = mtk_jpegdec_hw_init_irq(dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to register JPEGDEC irq handler.\n");

	i = atomic_add_return(1, &master_dev->hw_index) - 1;
	master_dev->dec_hw_dev[i] = dev;
	master_dev->reg_decbase[i] = dev->reg_base;
	dev->master_dev = master_dev;

	platform_set_drvdata(pdev, dev);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static struct platform_driver mtk_jpegdec_hw_driver = {
	.probe = mtk_jpegdec_hw_probe,
	.driver = {
		.name = "mtk-jpegdec-hw",
		.of_match_table = mtk_jpegdec_hw_ids,
	},
};

module_platform_driver(mtk_jpegdec_hw_driver);

MODULE_DESCRIPTION("MediaTek JPEG decode HW driver");
MODULE_LICENSE("GPL");
