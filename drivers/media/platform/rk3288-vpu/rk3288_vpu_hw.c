/*
 * Rockchip RK3288 VPU codec driver
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

#include "rk3288_vpu_common.h"

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

#include <asm/dma-iommu.h>

#include "rk3288_vpu_regs.h"

/**
 * struct rk3288_vpu_variant - information about VPU hardware variant
 *
 * @hw_id:		Top 16 bits (product ID) of hardware ID register.
 * @enc_offset:		Offset from VPU base to encoder registers.
 * @enc_reg_num:	Number of registers of encoder block.
 * @dec_offset:		Offset from VPU base to decoder registers.
 * @dec_reg_num:	Number of registers of decoder block.
 */
struct rk3288_vpu_variant {
	u16 hw_id;
	unsigned enc_offset;
	unsigned enc_reg_num;
	unsigned dec_offset;
	unsigned dec_reg_num;
};

/* Supported VPU variants. */
static const struct rk3288_vpu_variant rk3288_vpu_variants[] = {
	{
		.hw_id = 0x4831,
		.enc_offset = 0x0,
		.enc_reg_num = 164,
		.dec_offset = 0x400,
		.dec_reg_num = 60 + 41,
	},
};

/**
 * struct rk3288_vpu_codec_ops - codec mode specific operations
 *
 * @init:	Prepare for streaming. Called from VB2 .start_streaming()
 *		when streaming from both queues is being enabled.
 * @exit:	Clean-up after streaming. Called from VB2 .stop_streaming()
 *		when streaming from first of both enabled queues is being
 *		disabled.
 * @run:	Start single {en,de)coding run. Called from non-atomic context
 *		to indicate that a pair of buffers is ready and the hardware
 *		should be programmed and started.
 * @done:	Read back processing results and additional data from hardware.
 * @reset:	Reset the hardware in case of a timeout.
 */
struct rk3288_vpu_codec_ops {
	int (*init)(struct rk3288_vpu_ctx *);
	void (*exit)(struct rk3288_vpu_ctx *);

	void (*run)(struct rk3288_vpu_ctx *);
	void (*done)(struct rk3288_vpu_ctx *, enum vb2_buffer_state);
	void (*reset)(struct rk3288_vpu_ctx *);
};

/*
 * Hardware control routines.
 */

static int rk3288_vpu_identify(struct rk3288_vpu_dev *vpu)
{
	u32 hw_id;
	int i;

	hw_id = readl(vpu->base) >> 16;

	dev_info(vpu->dev, "Read hardware ID: %x\n", hw_id);

	for (i = 0; i < ARRAY_SIZE(rk3288_vpu_variants); ++i) {
		if (hw_id == rk3288_vpu_variants[i].hw_id) {
			vpu->variant = &rk3288_vpu_variants[i];
			return 0;
		}
	}

	return -ENOENT;
}

void rk3288_vpu_power_on(struct rk3288_vpu_dev *vpu)
{
	vpu_debug_enter();

	/* TODO: Clock gating. */

	pm_runtime_get_sync(vpu->dev);

	vpu_debug_leave();
}

static void rk3288_vpu_power_off(struct rk3288_vpu_dev *vpu)
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
	struct rk3288_vpu_dev *vpu = dev_id;
	u32 status = vepu_read(vpu, VEPU_REG_INTERRUPT);

	vepu_write(vpu, 0, VEPU_REG_INTERRUPT);

	if (status & VEPU_REG_INTERRUPT_BIT) {
		struct rk3288_vpu_ctx *ctx = vpu->current_ctx;

		vepu_write(vpu, 0, VEPU_REG_AXI_CTRL);
		rk3288_vpu_power_off(vpu);
		cancel_delayed_work(&vpu->watchdog_work);

		ctx->hw.codec_ops->done(ctx, VB2_BUF_STATE_DONE);
	}

	return IRQ_HANDLED;
}

static void rk3288_vpu_watchdog(struct work_struct *work)
{
	struct rk3288_vpu_dev *vpu = container_of(to_delayed_work(work),
					struct rk3288_vpu_dev, watchdog_work);
	struct rk3288_vpu_ctx *ctx = vpu->current_ctx;
	unsigned long flags;

	spin_lock_irqsave(&vpu->irqlock, flags);

	ctx->hw.codec_ops->reset(ctx);

	spin_unlock_irqrestore(&vpu->irqlock, flags);

	vpu_err("frame processing timed out!\n");

	rk3288_vpu_power_off(vpu);
	ctx->hw.codec_ops->done(ctx, VB2_BUF_STATE_ERROR);
}

/*
 * Initialization/clean-up.
 */

#if defined(CONFIG_ROCKCHIP_IOMMU)
static int rk3288_vpu_iommu_init(struct rk3288_vpu_dev *vpu)
{
	int ret;

	vpu->mapping = arm_iommu_create_mapping(&platform_bus_type,
						0x10000000, SZ_2G);
	if (IS_ERR(vpu->mapping)) {
		ret = PTR_ERR(vpu->mapping);
		return ret;
	}

	vpu->dev->dma_parms = devm_kzalloc(vpu->dev,
				sizeof(*vpu->dev->dma_parms), GFP_KERNEL);
	if (!vpu->dev->dma_parms)
		goto err_release_mapping;

	dma_set_max_seg_size(vpu->dev, 0xffffffffu);

	ret = arm_iommu_attach_device(vpu->dev, vpu->mapping);
	if (ret)
		goto err_release_mapping;

	return 0;

err_release_mapping:
	arm_iommu_release_mapping(vpu->mapping);

	return ret;
}

static void rk3288_vpu_iommu_cleanup(struct rk3288_vpu_dev *vpu)
{
	arm_iommu_detach_device(vpu->dev);
	arm_iommu_release_mapping(vpu->mapping);
}
#else
static inline int rk3288_vpu_iommu_init(struct rk3288_vpu_dev *vpu)
{
	return 0;
}

static inline void rk3288_vpu_iommu_cleanup(struct rk3288_vpu_dev *vpu) { }
#endif

int rk3288_vpu_hw_probe(struct rk3288_vpu_dev *vpu)
{
	struct resource *res;
	int irq_enc;
	int ret;

	pr_info("probe device %s\n", dev_name(vpu->dev));

	INIT_DELAYED_WORK(&vpu->watchdog_work, rk3288_vpu_watchdog);

	vpu->aclk_vcodec = devm_clk_get(vpu->dev, "aclk_vcodec");
	if (IS_ERR(vpu->aclk_vcodec)) {
		dev_err(vpu->dev, "failed to get aclk_vcodec\n");
		return PTR_ERR(vpu->aclk_vcodec);
	}

	vpu->hclk_vcodec = devm_clk_get(vpu->dev, "hclk_vcodec");
	if (IS_ERR(vpu->hclk_vcodec)) {
		dev_err(vpu->dev, "failed to get hclk_vcodec\n");
		return PTR_ERR(vpu->hclk_vcodec);
	}

	res = platform_get_resource(vpu->pdev, IORESOURCE_MEM, 0);
	vpu->base = devm_ioremap_resource(vpu->dev, res);
	if (IS_ERR(vpu->base))
		return PTR_ERR(vpu->base);

	clk_prepare_enable(vpu->aclk_vcodec);
	clk_prepare_enable(vpu->hclk_vcodec);

	ret = rk3288_vpu_identify(vpu);
	if (ret < 0) {
		dev_err(vpu->dev, "failed to identify hardware variant\n");
		goto err_power;
	}

	vpu->enc_base = vpu->base + vpu->variant->enc_offset;
	vpu->dec_base = vpu->base + vpu->variant->dec_offset;

	ret = dma_set_coherent_mask(vpu->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(vpu->dev, "could not set DMA coherent mask\n");
		goto err_power;
	}

	ret = rk3288_vpu_iommu_init(vpu);
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

	pm_runtime_set_autosuspend_delay(vpu->dev, 100);
	pm_runtime_use_autosuspend(vpu->dev);
	pm_runtime_enable(vpu->dev);

	return 0;

err_iommu:
	rk3288_vpu_iommu_cleanup(vpu);
err_power:
	clk_disable_unprepare(vpu->hclk_vcodec);
	clk_disable_unprepare(vpu->aclk_vcodec);

	return ret;
}

void rk3288_vpu_hw_remove(struct rk3288_vpu_dev *vpu)
{
	rk3288_vpu_iommu_cleanup(vpu);

	pm_runtime_disable(vpu->dev);

	clk_disable_unprepare(vpu->hclk_vcodec);
	clk_disable_unprepare(vpu->aclk_vcodec);
}

static void rk3288_vpu_enc_reset(struct rk3288_vpu_ctx *ctx)
{
	struct rk3288_vpu_dev *vpu = ctx->dev;

	vepu_write(vpu, VEPU_REG_INTERRUPT_DIS_BIT, VEPU_REG_INTERRUPT);
	vepu_write(vpu, 0, VEPU_REG_ENC_CTRL);
	vepu_write(vpu, 0, VEPU_REG_AXI_CTRL);
}

static const struct rk3288_vpu_codec_ops mode_ops[] = {
	[RK_VPU_CODEC_VP8E] = {
		.init = rk3288_vpu_vp8e_init,
		.exit = rk3288_vpu_vp8e_exit,
		.run = rk3288_vpu_vp8e_run,
		.done = rk3288_vpu_vp8e_done,
		.reset = rk3288_vpu_enc_reset,
	},
};

void rk3288_vpu_run(struct rk3288_vpu_ctx *ctx)
{
	ctx->hw.codec_ops->run(ctx);
}

int rk3288_vpu_init(struct rk3288_vpu_ctx *ctx)
{
	enum rk3288_vpu_codec_mode codec_mode;

	if (ctx->vpu_dst_fmt->codec_mode != RK_VPU_CODEC_NONE)
		codec_mode = ctx->vpu_dst_fmt->codec_mode; /* Encoder */
	else
		codec_mode = ctx->vpu_src_fmt->codec_mode; /* Decoder */

	ctx->hw.codec_ops = &mode_ops[codec_mode];

	return ctx->hw.codec_ops->init(ctx);
}

void rk3288_vpu_deinit(struct rk3288_vpu_ctx *ctx)
{
	ctx->hw.codec_ops->exit(ctx);
}
