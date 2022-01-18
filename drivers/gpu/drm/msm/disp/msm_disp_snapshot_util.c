// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include "msm_disp_snapshot.h"

static void msm_disp_state_dump_regs(u32 **reg, u32 aligned_len, void __iomem *base_addr)
{
	u32 len_padded;
	u32 num_rows;
	u32 x0, x4, x8, xc;
	void __iomem *addr;
	u32 *dump_addr = NULL;
	void __iomem *end_addr;
	int i;

	len_padded = aligned_len * REG_DUMP_ALIGN;
	num_rows = aligned_len / REG_DUMP_ALIGN;

	addr = base_addr;
	end_addr = base_addr + aligned_len;

	if (!(*reg))
		*reg = kzalloc(len_padded, GFP_KERNEL);

	if (*reg)
		dump_addr = *reg;

	for (i = 0; i < num_rows; i++) {
		x0 = (addr < end_addr) ? readl_relaxed(addr + 0x0) : 0;
		x4 = (addr + 0x4 < end_addr) ? readl_relaxed(addr + 0x4) : 0;
		x8 = (addr + 0x8 < end_addr) ? readl_relaxed(addr + 0x8) : 0;
		xc = (addr + 0xc < end_addr) ? readl_relaxed(addr + 0xc) : 0;

		if (dump_addr) {
			dump_addr[i * 4] = x0;
			dump_addr[i * 4 + 1] = x4;
			dump_addr[i * 4 + 2] = x8;
			dump_addr[i * 4 + 3] = xc;
		}

		addr += REG_DUMP_ALIGN;
	}
}

static void msm_disp_state_print_regs(u32 **reg, u32 len, void __iomem *base_addr,
		struct drm_printer *p)
{
	int i;
	u32 *dump_addr = NULL;
	void __iomem *addr;
	u32 num_rows;

	addr = base_addr;
	num_rows = len / REG_DUMP_ALIGN;

	if (*reg)
		dump_addr = *reg;

	for (i = 0; i < num_rows; i++) {
		drm_printf(p, "0x%lx : %08x %08x %08x %08x\n",
				(unsigned long)(addr - base_addr),
				dump_addr[i * 4], dump_addr[i * 4 + 1],
				dump_addr[i * 4 + 2], dump_addr[i * 4 + 3]);
		addr += REG_DUMP_ALIGN;
	}
}

void msm_disp_state_print(struct msm_disp_state *state, struct drm_printer *p)
{
	struct msm_disp_state_block *block, *tmp;

	if (!p) {
		DRM_ERROR("invalid drm printer\n");
		return;
	}

	drm_printf(p, "---\n");

	drm_printf(p, "module: " KBUILD_MODNAME "\n");
	drm_printf(p, "dpu devcoredump\n");
	drm_printf(p, "timestamp %lld\n", ktime_to_ns(state->timestamp));

	list_for_each_entry_safe(block, tmp, &state->blocks, node) {
		drm_printf(p, "====================%s================\n", block->name);
		msm_disp_state_print_regs(&block->state, block->size, block->base_addr, p);
	}

	drm_printf(p, "===================dpu drm state================\n");

	if (state->atomic_state)
		drm_atomic_print_new_state(state->atomic_state, p);
}

static void msm_disp_capture_atomic_state(struct msm_disp_state *disp_state)
{
	struct drm_device *ddev;
	struct drm_modeset_acquire_ctx ctx;

	disp_state->timestamp = ktime_get();

	ddev = disp_state->drm_dev;

	drm_modeset_acquire_init(&ctx, 0);

	while (drm_modeset_lock_all_ctx(ddev, &ctx) != 0)
		drm_modeset_backoff(&ctx);

	disp_state->atomic_state = drm_atomic_helper_duplicate_state(ddev,
			&ctx);
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

void msm_disp_snapshot_capture_state(struct msm_disp_state *disp_state)
{
	struct msm_drm_private *priv;
	struct drm_device *drm_dev;
	struct msm_kms *kms;
	int i;

	drm_dev = disp_state->drm_dev;
	priv = drm_dev->dev_private;
	kms = priv->kms;

	for (i = 0; i < ARRAY_SIZE(priv->dp); i++) {
		if (!priv->dp[i])
			continue;

		msm_dp_snapshot(disp_state, priv->dp[i]);
	}

	for (i = 0; i < ARRAY_SIZE(priv->dsi); i++) {
		if (!priv->dsi[i])
			continue;

		msm_dsi_snapshot(disp_state, priv->dsi[i]);
	}

	if (kms->funcs->snapshot)
		kms->funcs->snapshot(disp_state, kms);

	msm_disp_capture_atomic_state(disp_state);
}

void msm_disp_state_free(void *data)
{
	struct msm_disp_state *disp_state = data;
	struct msm_disp_state_block *block, *tmp;

	if (disp_state->atomic_state) {
		drm_atomic_state_put(disp_state->atomic_state);
		disp_state->atomic_state = NULL;
	}

	list_for_each_entry_safe(block, tmp, &disp_state->blocks, node) {
		list_del(&block->node);
		kfree(block->state);
		kfree(block);
	}

	kfree(disp_state);
}

void msm_disp_snapshot_add_block(struct msm_disp_state *disp_state, u32 len,
		void __iomem *base_addr, const char *fmt, ...)
{
	struct msm_disp_state_block *new_blk;
	struct va_format vaf;
	va_list va;

	new_blk = kzalloc(sizeof(struct msm_disp_state_block), GFP_KERNEL);

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;
	snprintf(new_blk->name, sizeof(new_blk->name), "%pV", &vaf);

	va_end(va);

	INIT_LIST_HEAD(&new_blk->node);
	new_blk->size = ALIGN(len, REG_DUMP_ALIGN);
	new_blk->base_addr = base_addr;

	msm_disp_state_dump_regs(&new_blk->state, new_blk->size, base_addr);
	list_add(&new_blk->node, &disp_state->blocks);
}
