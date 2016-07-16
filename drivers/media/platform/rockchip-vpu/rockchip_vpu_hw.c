/*
 * Rockchip VPU codec driver
 *
 * Copyright (C) 2014 Google, Inc.
 *	Tomasz Figa <tfiga@chromium.org>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "rockchip_vpu_common.h"

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include <linux/dma-iommu.h>

/* Various parameters specific to VP8 encoder. */
#define VP8_KEY_FRAME_HDR_SIZE                  10
#define VP8_INTER_FRAME_HDR_SIZE                3

#define VP8_FRAME_TAG_KEY_FRAME_BIT             BIT(0)
#define VP8_FRAME_TAG_LENGTH_SHIFT              5
#define VP8_FRAME_TAG_LENGTH_MASK               (0x7ffff << 5)

/**
 * struct rockchip_vpu_codec_ops - codec mode specific operations
 *
 * @codec_mode:	Codec mode related to this format. See
 *		enum rockchip_vpu_codec_mode.
 * @init:	Prepare for streaming. Called from VB2 .start_streaming()
 *		when streaming from both queues is being enabled.
 * @exit:	Clean-up after streaming. Called from VB2 .stop_streaming()
 *		when streaming from first of both enabled queues is being
 *		disabled.
 * @irq:	Handle {en,de}code irq. Check and clear interrupt.
 * @run:	Start single {en,de)coding run. Called from non-atomic context
 *		to indicate that a pair of buffers is ready and the hardware
 *		should be programmed and started.
 * @done:	Read back processing results and additional data from hardware.
 * @reset:	Reset the hardware in case of a timeout.
 */
struct rockchip_vpu_codec_ops {
	enum rockchip_vpu_codec_mode codec_mode;

	int (*init)(struct rockchip_vpu_ctx *);
	void (*exit)(struct rockchip_vpu_ctx *);

	int (*irq)(int, struct rockchip_vpu_dev *);
	void (*run)(struct rockchip_vpu_ctx *);
	void (*done)(struct rockchip_vpu_ctx *, enum vb2_buffer_state);
	void (*reset)(struct rockchip_vpu_ctx *);
};

/*
 * Hardware control routines.
 */

void rockchip_vpu_power_on(struct rockchip_vpu_dev *vpu)
{
	vpu_debug_enter();

	/* TODO: Clock gating. */

	pm_runtime_get_sync(vpu->dev);

	vpu_debug_leave();
}

static void rockchip_vpu_power_off(struct rockchip_vpu_dev *vpu)
{
	vpu_debug_enter();

	pm_runtime_mark_last_busy(vpu->dev);
	pm_runtime_put_autosuspend(vpu->dev);

	/* TODO: Clock gating. */

	vpu_debug_leave();
}

/*
 * Interrupt handlers.
 */

static irqreturn_t vepu_irq(int irq, void *dev_id)
{
	struct rockchip_vpu_dev *vpu = dev_id;
	struct rockchip_vpu_ctx *ctx = vpu->current_ctx;

	if (!ctx->hw.codec_ops->irq(irq, vpu)) {
		rockchip_vpu_power_off(vpu);
		cancel_delayed_work(&vpu->watchdog_work);

		ctx->hw.codec_ops->done(ctx, VB2_BUF_STATE_DONE);
	}

	return IRQ_HANDLED;
}

static irqreturn_t vdpu_irq(int irq, void *dev_id)
{
	struct rockchip_vpu_dev *vpu = dev_id;
	struct rockchip_vpu_ctx *ctx = vpu->current_ctx;

	if (!ctx->hw.codec_ops->irq(irq, vpu)) {
		rockchip_vpu_power_off(vpu);
		cancel_delayed_work(&vpu->watchdog_work);

		ctx->hw.codec_ops->done(ctx, VB2_BUF_STATE_DONE);
	}

	return IRQ_HANDLED;
}

static void rockchip_vpu_watchdog(struct work_struct *work)
{
	struct rockchip_vpu_dev *vpu = container_of(to_delayed_work(work),
					struct rockchip_vpu_dev, watchdog_work);
	struct rockchip_vpu_ctx *ctx = vpu->current_ctx;
	unsigned long flags;

	spin_lock_irqsave(&vpu->irqlock, flags);

	ctx->hw.codec_ops->reset(ctx);

	spin_unlock_irqrestore(&vpu->irqlock, flags);

	vpu_err("frame processing timed out!\n");

	rockchip_vpu_power_off(vpu);
	ctx->hw.codec_ops->done(ctx, VB2_BUF_STATE_ERROR);
}

/*
 * Initialization/clean-up.
 */

#if defined(CONFIG_ROCKCHIP_IOMMU)
static int rockchip_vpu_iommu_init(struct rockchip_vpu_dev *vpu)
{
	int ret;

	vpu->dev->dma_parms = devm_kzalloc(vpu->dev,
					   sizeof(*vpu->dev->dma_parms),
					   GFP_KERNEL);
	if (!vpu->dev->dma_parms)
		return -ENOMEM;

	vpu->domain = iommu_domain_alloc(vpu->dev->bus);
	if (!vpu->domain) {
		ret = -ENOMEM;
		goto err_free_parms;
	}

	ret = iommu_get_dma_cookie(vpu->domain);
	if (ret)
		goto err_free_domain;

	ret = dma_set_coherent_mask(vpu->dev, DMA_BIT_MASK(32));
	if (ret)
		goto err_put_cookie;

	dma_set_max_seg_size(vpu->dev, DMA_BIT_MASK(32));

	ret = iommu_attach_device(vpu->domain, vpu->dev);
	if (ret)
		goto err_put_cookie;

	common_iommu_setup_dma_ops(vpu->dev, 0x10000000, SZ_2G,
				   vpu->domain->ops);

	return 0;

err_put_cookie:
	iommu_put_dma_cookie(vpu->domain);
err_free_domain:
	iommu_domain_free(vpu->domain);
err_free_parms:
	return ret;
}

static void rockchip_vpu_iommu_cleanup(struct rockchip_vpu_dev *vpu)
{
	iommu_detach_device(vpu->domain, vpu->dev);
	iommu_put_dma_cookie(vpu->domain);
	iommu_domain_free(vpu->domain);
}
#else /* CONFIG_ROCKCHIP_IOMMU */
static inline int rockchip_vpu_iommu_init(struct rockchip_vpu_dev *vpu)
{
	return 0;
}

static inline void rockchip_vpu_iommu_cleanup(struct rockchip_vpu_dev *vpu) { }
#endif /* CONFIG_ROCKCHIP_IOMMU */

int rockchip_vpu_hw_probe(struct rockchip_vpu_dev *vpu)
{
	struct resource *res;
	int irq_enc, irq_dec;
	int ret;

	pr_info("probe device %s\n", dev_name(vpu->dev));

	INIT_DELAYED_WORK(&vpu->watchdog_work, rockchip_vpu_watchdog);

	vpu->aclk = devm_clk_get(vpu->dev, "aclk");
	if (IS_ERR(vpu->aclk)) {
		dev_err(vpu->dev, "failed to get aclk\n");
		return PTR_ERR(vpu->aclk);
	}

	vpu->hclk = devm_clk_get(vpu->dev, "hclk");
	if (IS_ERR(vpu->hclk)) {
		dev_err(vpu->dev, "failed to get hclk\n");
		return PTR_ERR(vpu->hclk);
	}

	res = platform_get_resource(vpu->pdev, IORESOURCE_MEM, 0);
	vpu->base = devm_ioremap_resource(vpu->dev, res);
	if (IS_ERR(vpu->base))
		return PTR_ERR(vpu->base);

	clk_prepare_enable(vpu->aclk);
	clk_prepare_enable(vpu->hclk);

	vpu->enc_base = vpu->base + vpu->variant->enc_offset;
	vpu->dec_base = vpu->base + vpu->variant->dec_offset;

	ret = dma_set_coherent_mask(vpu->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(vpu->dev, "could not set DMA coherent mask\n");
		goto err_power;
	}

	ret = rockchip_vpu_iommu_init(vpu);
	if (ret)
		goto err_power;

	irq_enc = platform_get_irq_byname(vpu->pdev, "vepu");
	if (irq_enc <= 0) {
		dev_err(vpu->dev, "could not get vepu IRQ\n");
		ret = -ENXIO;
		goto err_iommu;
	}

	ret = devm_request_threaded_irq(vpu->dev, irq_enc, NULL, vepu_irq,
					IRQF_ONESHOT, dev_name(vpu->dev), vpu);
	if (ret) {
		dev_err(vpu->dev, "could not request vepu IRQ\n");
		goto err_iommu;
	}

	irq_dec = platform_get_irq_byname(vpu->pdev, "vdpu");
	if (irq_dec <= 0) {
		dev_err(vpu->dev, "could not get vdpu IRQ\n");
		ret = -ENXIO;
		goto err_iommu;
	}

	ret = devm_request_threaded_irq(vpu->dev, irq_dec, NULL, vdpu_irq,
					IRQF_ONESHOT, dev_name(vpu->dev), vpu);
	if (ret) {
		dev_err(vpu->dev, "could not request vdpu IRQ\n");
		goto err_iommu;
	}

	pm_runtime_set_autosuspend_delay(vpu->dev, 100);
	pm_runtime_use_autosuspend(vpu->dev);
	pm_runtime_enable(vpu->dev);

	return 0;

err_iommu:
	rockchip_vpu_iommu_cleanup(vpu);
err_power:
	clk_disable_unprepare(vpu->hclk);
	clk_disable_unprepare(vpu->aclk);

	return ret;
}

void rockchip_vpu_hw_remove(struct rockchip_vpu_dev *vpu)
{
	rockchip_vpu_iommu_cleanup(vpu);

	pm_runtime_disable(vpu->dev);

	clk_disable_unprepare(vpu->hclk);
	clk_disable_unprepare(vpu->aclk);
}

static const struct rockchip_vpu_codec_ops mode_ops[] = {
	{
		.codec_mode = RK3288_VPU_CODEC_VP8E,
		.init = rk3288_vpu_vp8e_init,
		.exit = rk3288_vpu_vp8e_exit,
		.irq = rk3288_vpu_enc_irq,
		.run = rk3288_vpu_vp8e_run,
		.done = rk3288_vpu_vp8e_done,
		.reset = rk3288_vpu_enc_reset,
	},
	{
		.codec_mode = RK3288_VPU_CODEC_VP8D,
		.init = rk3288_vpu_vp8d_init,
		.exit = rk3288_vpu_vp8d_exit,
		.irq = rk3288_vpu_dec_irq,
		.run = rk3288_vpu_vp8d_run,
		.done = rockchip_vpu_run_done,
		.reset = rk3288_vpu_dec_reset,
	},
	{
		.codec_mode = RK3288_VPU_CODEC_H264E,
		.init = rk3288_vpu_h264e_init,
		.exit = rk3288_vpu_h264e_exit,
		.irq = rk3288_vpu_enc_irq,
		.run = rk3288_vpu_h264e_run,
		.done = rk3288_vpu_h264e_done,
		.reset = rk3288_vpu_enc_reset,
	},
	{
		.codec_mode = RK3288_VPU_CODEC_H264D,
		.init = rk3288_vpu_h264d_init,
		.exit = rk3288_vpu_h264d_exit,
		.irq = rk3288_vpu_dec_irq,
		.run = rk3288_vpu_h264d_run,
		.done = rockchip_vpu_run_done,
		.reset = rk3288_vpu_dec_reset,
	},
};

void rockchip_vpu_run(struct rockchip_vpu_ctx *ctx)
{
	ctx->hw.codec_ops->run(ctx);
}

int rockchip_vpu_init(struct rockchip_vpu_ctx *ctx)
{
	enum rockchip_vpu_codec_mode codec_mode;
	int i;

	if (rockchip_vpu_ctx_is_encoder(ctx))
		codec_mode = ctx->vpu_dst_fmt->codec_mode; /* Encoder */
	else
		codec_mode = ctx->vpu_src_fmt->codec_mode; /* Decoder */

	for (i = 0; i < ARRAY_SIZE(mode_ops); i++) {
		if (mode_ops[i].codec_mode == codec_mode) {
			ctx->hw.codec_ops = &mode_ops[i];
			break;
		}
	}

	if (!ctx->hw.codec_ops)
		return -1;

	return ctx->hw.codec_ops->init(ctx);
}

void rockchip_vpu_deinit(struct rockchip_vpu_ctx *ctx)
{
	ctx->hw.codec_ops->exit(ctx);
}

/*
 * The hardware takes care only of ext hdr and dct partition. The software
 * must take care of frame header.
 *
 * Buffer layout as received from hardware:
 *   |<--gap-->|<--ext hdr-->|<-gap->|<---dct part---
 *   |<-------dct part offset------->|
 *
 * Required buffer layout:
 *   |<--hdr-->|<--ext hdr-->|<---dct part---
 */
void rockchip_vpu_vp8e_assemble_bitstream(struct rockchip_vpu_ctx *ctx,
					struct rockchip_vpu_buf *dst_buf)
{
	struct vb2_v4l2_buffer *vb2_dst = to_vb2_v4l2_buffer(&dst_buf->vb.vb2_buf);
	size_t ext_hdr_size = dst_buf->vp8e.ext_hdr_size;
	size_t dct_size = dst_buf->vp8e.dct_size;
	size_t hdr_size = dst_buf->vp8e.hdr_size;
	size_t dst_size;
	size_t tag_size;
	void *dst;
	u32 *tag;

	dst_size = vb2_plane_size(&dst_buf->vb.vb2_buf, 0);
	dst = vb2_plane_vaddr(&dst_buf->vb.vb2_buf, 0);
	tag = dst; /* To access frame tag words. */

	if (WARN_ON(hdr_size + ext_hdr_size + dct_size > dst_size))
		return;
	if (WARN_ON(dst_buf->vp8e.dct_offset + dct_size > dst_size))
		return;

	vpu_debug(1, "%s: hdr_size = %d, ext_hdr_size = %d, dct_size = %d\n",
			__func__, hdr_size, ext_hdr_size, dct_size);

	memmove(dst + hdr_size + ext_hdr_size,
		dst + dst_buf->vp8e.dct_offset, dct_size);
	memcpy(dst, dst_buf->vp8e.header, hdr_size);

	/* Patch frame tag at first 32-bit word of the frame. */
	if (vb2_dst->flags & V4L2_BUF_FLAG_KEYFRAME) {
		tag_size = VP8_KEY_FRAME_HDR_SIZE;
		tag[0] &= ~VP8_FRAME_TAG_KEY_FRAME_BIT;
	} else {
		tag_size = VP8_INTER_FRAME_HDR_SIZE;
		tag[0] |= VP8_FRAME_TAG_KEY_FRAME_BIT;
	}

	tag[0] &= ~VP8_FRAME_TAG_LENGTH_MASK;
	tag[0] |= (hdr_size + ext_hdr_size - tag_size)
						<< VP8_FRAME_TAG_LENGTH_SHIFT;

	vb2_set_plane_payload(&dst_buf->vb.vb2_buf, 0,
				hdr_size + ext_hdr_size + dct_size);
}

void rockchip_vpu_h264e_assemble_bitstream(struct rockchip_vpu_ctx *ctx,
					struct rockchip_vpu_buf *dst_buf)
{
	size_t sps_size = dst_buf->h264e.sps_size;
	size_t pps_size = dst_buf->h264e.pps_size;
	size_t slices_size = dst_buf->h264e.slices_size;
	size_t dst_size;
	void *dst;

	struct stream_s *sps = &ctx->run.h264e.sps;
	struct stream_s *pps = &ctx->run.h264e.pps;

	dst_size = vb2_plane_size(&dst_buf->vb.vb2_buf, 0);
	dst = vb2_plane_vaddr(&dst_buf->vb.vb2_buf, 0);

	if (WARN_ON(sps_size + pps_size + slices_size > dst_size))
		return;

	vpu_debug(1, "%s: sps_size = %u, pps_size = %u, slices_size = %u\n",
		__func__, sps_size, pps_size, slices_size);

	memcpy(dst, sps->buffer, sps_size);
	memcpy(dst + sps_size, pps->buffer, pps_size);

	vb2_set_plane_payload(&dst_buf->vb.vb2_buf, 0,
			      sps_size + pps_size + slices_size);
}
