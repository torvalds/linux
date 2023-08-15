// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: Xia Jiang <xia.jiang@mediatek.com>
 *
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
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>

#include "mtk_jpeg_core.h"
#include "mtk_jpeg_enc_hw.h"

static const struct mtk_jpeg_enc_qlt mtk_jpeg_enc_quality[] = {
	{.quality_param = 34, .hardware_value = JPEG_ENC_QUALITY_Q34},
	{.quality_param = 39, .hardware_value = JPEG_ENC_QUALITY_Q39},
	{.quality_param = 48, .hardware_value = JPEG_ENC_QUALITY_Q48},
	{.quality_param = 60, .hardware_value = JPEG_ENC_QUALITY_Q60},
	{.quality_param = 64, .hardware_value = JPEG_ENC_QUALITY_Q64},
	{.quality_param = 68, .hardware_value = JPEG_ENC_QUALITY_Q68},
	{.quality_param = 74, .hardware_value = JPEG_ENC_QUALITY_Q74},
	{.quality_param = 80, .hardware_value = JPEG_ENC_QUALITY_Q80},
	{.quality_param = 82, .hardware_value = JPEG_ENC_QUALITY_Q82},
	{.quality_param = 84, .hardware_value = JPEG_ENC_QUALITY_Q84},
	{.quality_param = 87, .hardware_value = JPEG_ENC_QUALITY_Q87},
	{.quality_param = 90, .hardware_value = JPEG_ENC_QUALITY_Q90},
	{.quality_param = 92, .hardware_value = JPEG_ENC_QUALITY_Q92},
	{.quality_param = 95, .hardware_value = JPEG_ENC_QUALITY_Q95},
	{.quality_param = 97, .hardware_value = JPEG_ENC_QUALITY_Q97},
};

static const struct of_device_id mtk_jpegenc_drv_ids[] = {
	{
		.compatible = "mediatek,mt8195-jpgenc-hw",
	},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_jpegenc_drv_ids);

void mtk_jpeg_enc_reset(void __iomem *base)
{
	writel(0, base + JPEG_ENC_RSTB);
	writel(JPEG_ENC_RESET_BIT, base + JPEG_ENC_RSTB);
	writel(0, base + JPEG_ENC_CODEC_SEL);
}
EXPORT_SYMBOL_GPL(mtk_jpeg_enc_reset);

u32 mtk_jpeg_enc_get_file_size(void __iomem *base)
{
	return readl(base + JPEG_ENC_DMA_ADDR0) -
	       readl(base + JPEG_ENC_DST_ADDR0);
}
EXPORT_SYMBOL_GPL(mtk_jpeg_enc_get_file_size);

void mtk_jpeg_enc_start(void __iomem *base)
{
	u32 value;

	value = readl(base + JPEG_ENC_CTRL);
	value |= JPEG_ENC_CTRL_INT_EN_BIT | JPEG_ENC_CTRL_ENABLE_BIT;
	writel(value, base + JPEG_ENC_CTRL);
}
EXPORT_SYMBOL_GPL(mtk_jpeg_enc_start);

void mtk_jpeg_set_enc_src(struct mtk_jpeg_ctx *ctx,  void __iomem *base,
			  struct vb2_buffer *src_buf)
{
	int i;
	dma_addr_t dma_addr;

	for (i = 0; i < src_buf->num_planes; i++) {
		dma_addr = vb2_dma_contig_plane_dma_addr(src_buf, i) +
			   src_buf->planes[i].data_offset;
		if (!i)
			writel(dma_addr, base + JPEG_ENC_SRC_LUMA_ADDR);
		else
			writel(dma_addr, base + JPEG_ENC_SRC_CHROMA_ADDR);
	}
}
EXPORT_SYMBOL_GPL(mtk_jpeg_set_enc_src);

void mtk_jpeg_set_enc_dst(struct mtk_jpeg_ctx *ctx, void __iomem *base,
			  struct vb2_buffer *dst_buf)
{
	dma_addr_t dma_addr;
	size_t size;
	u32 dma_addr_offset;
	u32 dma_addr_offsetmask;

	dma_addr = vb2_dma_contig_plane_dma_addr(dst_buf, 0);
	dma_addr_offset = ctx->enable_exif ? MTK_JPEG_MAX_EXIF_SIZE : 0;
	dma_addr_offsetmask = dma_addr & JPEG_ENC_DST_ADDR_OFFSET_MASK;
	size = vb2_plane_size(dst_buf, 0);

	writel(dma_addr_offset & ~0xf, base + JPEG_ENC_OFFSET_ADDR);
	writel(dma_addr_offsetmask & 0xf, base + JPEG_ENC_BYTE_OFFSET_MASK);
	writel(dma_addr & ~0xf, base + JPEG_ENC_DST_ADDR0);
	writel((dma_addr + size) & ~0xf, base + JPEG_ENC_STALL_ADDR0);
}
EXPORT_SYMBOL_GPL(mtk_jpeg_set_enc_dst);

void mtk_jpeg_set_enc_params(struct mtk_jpeg_ctx *ctx,  void __iomem *base)
{
	u32 value;
	u32 width = ctx->out_q.enc_crop_rect.width;
	u32 height = ctx->out_q.enc_crop_rect.height;
	u32 enc_format = ctx->out_q.fmt->fourcc;
	u32 bytesperline = ctx->out_q.pix_mp.plane_fmt[0].bytesperline;
	u32 blk_num;
	u32 img_stride;
	u32 mem_stride;
	u32 i, enc_quality;

	value = width << 16 | height;
	writel(value, base + JPEG_ENC_IMG_SIZE);

	if (enc_format == V4L2_PIX_FMT_NV12M ||
	    enc_format == V4L2_PIX_FMT_NV21M)
	    /*
	     * Total 8 x 8 block number of luma and chroma.
	     * The number of blocks is counted from 0.
	     */
		blk_num = DIV_ROUND_UP(width, 16) *
			  DIV_ROUND_UP(height, 16) * 6 - 1;
	else
		blk_num = DIV_ROUND_UP(width, 16) *
			  DIV_ROUND_UP(height, 8) * 4 - 1;
	writel(blk_num, base + JPEG_ENC_BLK_NUM);

	if (enc_format == V4L2_PIX_FMT_NV12M ||
	    enc_format == V4L2_PIX_FMT_NV21M) {
		/* 4:2:0 */
		img_stride = round_up(width, 16);
		mem_stride = bytesperline;
	} else {
		/* 4:2:2 */
		img_stride = round_up(width * 2, 32);
		mem_stride = img_stride;
	}
	writel(img_stride, base + JPEG_ENC_IMG_STRIDE);
	writel(mem_stride, base + JPEG_ENC_STRIDE);

	enc_quality = mtk_jpeg_enc_quality[0].hardware_value;
	for (i = 0; i < ARRAY_SIZE(mtk_jpeg_enc_quality); i++) {
		if (ctx->enc_quality <= mtk_jpeg_enc_quality[i].quality_param) {
			enc_quality = mtk_jpeg_enc_quality[i].hardware_value;
			break;
		}
	}
	writel(enc_quality, base + JPEG_ENC_QUALITY);

	value = readl(base + JPEG_ENC_CTRL);
	value &= ~JPEG_ENC_CTRL_YUV_FORMAT_MASK;
	value |= (ctx->out_q.fmt->hw_format & 3) << 3;
	if (ctx->enable_exif)
		value |= JPEG_ENC_CTRL_FILE_FORMAT_BIT;
	else
		value &= ~JPEG_ENC_CTRL_FILE_FORMAT_BIT;
	if (ctx->restart_interval)
		value |= JPEG_ENC_CTRL_RESTART_EN_BIT;
	else
		value &= ~JPEG_ENC_CTRL_RESTART_EN_BIT;
	writel(value, base + JPEG_ENC_CTRL);

	writel(ctx->restart_interval, base + JPEG_ENC_RST_MCU_NUM);
}
EXPORT_SYMBOL_GPL(mtk_jpeg_set_enc_params);

static void mtk_jpegenc_put_buf(struct mtk_jpegenc_comp_dev *jpeg)
{
	struct mtk_jpeg_ctx *ctx;
	struct vb2_v4l2_buffer *dst_buffer;
	struct list_head *temp_entry;
	struct list_head *pos = NULL;
	struct mtk_jpeg_src_buf *dst_done_buf, *tmp_dst_done_buf;
	unsigned long flags;

	ctx = jpeg->hw_param.curr_ctx;
	if (!ctx) {
		dev_err(jpeg->dev, "comp_jpeg ctx fail !!!\n");
		return;
	}

	dst_buffer = jpeg->hw_param.dst_buffer;
	if (!dst_buffer) {
		dev_err(jpeg->dev, "comp_jpeg dst_buffer fail !!!\n");
		return;
	}

	dst_done_buf = container_of(dst_buffer,
				    struct mtk_jpeg_src_buf, b);

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

static void mtk_jpegenc_timeout_work(struct work_struct *work)
{
	struct delayed_work *dly_work = to_delayed_work(work);
	struct mtk_jpegenc_comp_dev *cjpeg =
		container_of(dly_work,
			     struct mtk_jpegenc_comp_dev,
			     job_timeout_work);
	struct mtk_jpeg_dev *master_jpeg = cjpeg->master_dev;
	enum vb2_buffer_state buf_state = VB2_BUF_STATE_ERROR;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;

	src_buf = cjpeg->hw_param.src_buffer;
	dst_buf = cjpeg->hw_param.dst_buffer;
	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, true);

	mtk_jpeg_enc_reset(cjpeg->reg_base);
	clk_disable_unprepare(cjpeg->venc_clk.clks->clk);
	pm_runtime_put(cjpeg->dev);
	cjpeg->hw_state = MTK_JPEG_HW_IDLE;
	atomic_inc(&master_jpeg->hw_rdy);
	wake_up(&master_jpeg->hw_wq);
	v4l2_m2m_buf_done(src_buf, buf_state);
	mtk_jpegenc_put_buf(cjpeg);
}

static irqreturn_t mtk_jpegenc_hw_irq_handler(int irq, void *priv)
{
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	enum vb2_buffer_state buf_state;
	struct mtk_jpeg_ctx *ctx;
	u32 result_size;
	u32 irq_status;

	struct mtk_jpegenc_comp_dev *jpeg = priv;
	struct mtk_jpeg_dev *master_jpeg = jpeg->master_dev;

	cancel_delayed_work(&jpeg->job_timeout_work);

	ctx = jpeg->hw_param.curr_ctx;
	src_buf = jpeg->hw_param.src_buffer;
	dst_buf = jpeg->hw_param.dst_buffer;
	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, true);

	irq_status = readl(jpeg->reg_base + JPEG_ENC_INT_STS) &
		JPEG_ENC_INT_STATUS_MASK_ALLIRQ;
	if (irq_status)
		writel(0, jpeg->reg_base + JPEG_ENC_INT_STS);
	if (!(irq_status & JPEG_ENC_INT_STATUS_DONE))
		dev_warn(jpeg->dev, "Jpg Enc occurs unknown Err.");

	result_size = mtk_jpeg_enc_get_file_size(jpeg->reg_base);
	vb2_set_plane_payload(&dst_buf->vb2_buf, 0, result_size);
	buf_state = VB2_BUF_STATE_DONE;
	v4l2_m2m_buf_done(src_buf, buf_state);
	mtk_jpegenc_put_buf(jpeg);
	pm_runtime_put(ctx->jpeg->dev);
	clk_disable_unprepare(jpeg->venc_clk.clks->clk);

	jpeg->hw_state = MTK_JPEG_HW_IDLE;
	wake_up(&master_jpeg->hw_wq);
	atomic_inc(&master_jpeg->hw_rdy);

	return IRQ_HANDLED;
}

static int mtk_jpegenc_hw_init_irq(struct mtk_jpegenc_comp_dev *dev)
{
	struct platform_device *pdev = dev->plat_dev;
	int ret;

	dev->jpegenc_irq = platform_get_irq(pdev, 0);
	if (dev->jpegenc_irq < 0)
		return dev->jpegenc_irq;

	ret = devm_request_irq(&pdev->dev,
			       dev->jpegenc_irq,
			       mtk_jpegenc_hw_irq_handler,
			       0,
			       pdev->name, dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to devm_request_irq %d (%d)",
			dev->jpegenc_irq, ret);
		return ret;
	}

	return 0;
}

static int mtk_jpegenc_hw_probe(struct platform_device *pdev)
{
	struct mtk_jpegenc_clk *jpegenc_clk;
	struct mtk_jpeg_dev *master_dev;
	struct mtk_jpegenc_comp_dev *dev;
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

	spin_lock_init(&dev->hw_lock);
	dev->hw_state = MTK_JPEG_HW_IDLE;

	INIT_DELAYED_WORK(&dev->job_timeout_work,
			  mtk_jpegenc_timeout_work);

	jpegenc_clk = &dev->venc_clk;

	jpegenc_clk->clk_num = devm_clk_bulk_get_all(&pdev->dev,
						     &jpegenc_clk->clks);
	if (jpegenc_clk->clk_num < 0)
		return dev_err_probe(&pdev->dev, jpegenc_clk->clk_num,
				     "Failed to get jpegenc clock count\n");

	dev->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dev->reg_base))
		return PTR_ERR(dev->reg_base);

	ret = mtk_jpegenc_hw_init_irq(dev);
	if (ret)
		return ret;

	i = atomic_add_return(1, &master_dev->hw_index) - 1;
	master_dev->enc_hw_dev[i] = dev;
	master_dev->reg_encbase[i] = dev->reg_base;
	dev->master_dev = master_dev;

	platform_set_drvdata(pdev, dev);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static struct platform_driver mtk_jpegenc_hw_driver = {
	.probe = mtk_jpegenc_hw_probe,
	.driver = {
		.name = "mtk-jpegenc-hw",
		.of_match_table = mtk_jpegenc_drv_ids,
	},
};

module_platform_driver(mtk_jpegenc_hw_driver);

MODULE_DESCRIPTION("MediaTek JPEG encode HW driver");
MODULE_LICENSE("GPL");
