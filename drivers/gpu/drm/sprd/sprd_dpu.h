/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef __SPRD_DPU_H__
#define __SPRD_DPU_H__

#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <video/videomode.h>

#include <drm/drm_crtc.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>
#include <uapi/drm/drm_mode.h>

/* DPU Layer registers offset */
#define DPU_LAY_REG_OFFSET	0x30

enum {
	SPRD_DPU_IF_DPI,
	SPRD_DPU_IF_EDPI,
	SPRD_DPU_IF_LIMIT
};

/**
 * Sprd DPU context structure
 *
 * @base: DPU controller base address
 * @irq: IRQ number to install the handler for
 * @if_type: The type of DPI interface, default is DPI mode.
 * @vm: videomode structure to use for DPU and DPI initialization
 * @stopped: indicates whether DPU are stopped
 * @wait_queue: wait queue, used to wait for DPU shadow register update done and
 * DPU stop register done interrupt signal.
 * @evt_update: wait queue condition for DPU shadow register
 * @evt_stop: wait queue condition for DPU stop register
 */
struct dpu_context {
	void __iomem *base;
	int irq;
	u8 if_type;
	struct videomode vm;
	bool stopped;
	wait_queue_head_t wait_queue;
	bool evt_update;
	bool evt_stop;
};

/**
 * Sprd DPU device structure
 *
 * @crtc: crtc object
 * @drm: A point to drm device
 * @ctx: DPU's implementation specific context object
 */
struct sprd_dpu {
	struct drm_crtc base;
	struct drm_device *drm;
	struct dpu_context ctx;
};

static inline struct sprd_dpu *to_sprd_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct sprd_dpu, base);
}

static inline void
dpu_reg_set(struct dpu_context *ctx, u32 offset, u32 set_bits)
{
	u32 bits = readl_relaxed(ctx->base + offset);

	writel(bits | set_bits, ctx->base + offset);
}

static inline void
dpu_reg_clr(struct dpu_context *ctx, u32 offset, u32 clr_bits)
{
	u32 bits = readl_relaxed(ctx->base + offset);

	writel(bits & ~clr_bits, ctx->base + offset);
}

static inline u32
layer_reg_rd(struct dpu_context *ctx, u32 offset, int index)
{
	u32 layer_offset = offset + index * DPU_LAY_REG_OFFSET;

	return readl(ctx->base + layer_offset);
}

static inline void
layer_reg_wr(struct dpu_context *ctx, u32 offset, u32 cfg_bits, int index)
{
	u32 layer_offset =  offset + index * DPU_LAY_REG_OFFSET;

	writel(cfg_bits, ctx->base + layer_offset);
}

void sprd_dpu_run(struct sprd_dpu *dpu);
void sprd_dpu_stop(struct sprd_dpu *dpu);

#endif
