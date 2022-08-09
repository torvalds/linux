// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016-2017 The Linux Foundation. All rights reserved.
 */

#include <linux/types.h>
#include <linux/debugfs.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>

#include "a5xx_gpu.h"

static void pfp_print(struct msm_gpu *gpu, struct drm_printer *p)
{
	int i;

	drm_printf(p, "PFP state:\n");

	for (i = 0; i < 36; i++) {
		gpu_write(gpu, REG_A5XX_CP_PFP_STAT_ADDR, i);
		drm_printf(p, "  %02x: %08x\n", i,
			gpu_read(gpu, REG_A5XX_CP_PFP_STAT_DATA));
	}
}

static void me_print(struct msm_gpu *gpu, struct drm_printer *p)
{
	int i;

	drm_printf(p, "ME state:\n");

	for (i = 0; i < 29; i++) {
		gpu_write(gpu, REG_A5XX_CP_ME_STAT_ADDR, i);
		drm_printf(p, "  %02x: %08x\n", i,
			gpu_read(gpu, REG_A5XX_CP_ME_STAT_DATA));
	}
}

static void meq_print(struct msm_gpu *gpu, struct drm_printer *p)
{
	int i;

	drm_printf(p, "MEQ state:\n");
	gpu_write(gpu, REG_A5XX_CP_MEQ_DBG_ADDR, 0);

	for (i = 0; i < 64; i++) {
		drm_printf(p, "  %02x: %08x\n", i,
			gpu_read(gpu, REG_A5XX_CP_MEQ_DBG_DATA));
	}
}

static void roq_print(struct msm_gpu *gpu, struct drm_printer *p)
{
	int i;

	drm_printf(p, "ROQ state:\n");
	gpu_write(gpu, REG_A5XX_CP_ROQ_DBG_ADDR, 0);

	for (i = 0; i < 512 / 4; i++) {
		uint32_t val[4];
		int j;
		for (j = 0; j < 4; j++)
			val[j] = gpu_read(gpu, REG_A5XX_CP_ROQ_DBG_DATA);
		drm_printf(p, "  %02x: %08x %08x %08x %08x\n", i,
			val[0], val[1], val[2], val[3]);
	}
}

static int show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_printer p = drm_seq_file_printer(m);
	void (*show)(struct msm_gpu *gpu, struct drm_printer *p) =
		node->info_ent->data;

	show(priv->gpu, &p);
	return 0;
}

#define ENT(n) { .name = #n, .show = show, .data = n ##_print }
static struct drm_info_list a5xx_debugfs_list[] = {
	ENT(pfp),
	ENT(me),
	ENT(meq),
	ENT(roq),
};

/* for debugfs files that can be written to, we can't use drm helper: */
static int
reset_set(void *data, u64 val)
{
	struct drm_device *dev = data;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gpu *gpu = priv->gpu;
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);

	if (!capable(CAP_SYS_ADMIN))
		return -EINVAL;

	/* TODO do we care about trying to make sure the GPU is idle?
	 * Since this is just a debug feature limited to CAP_SYS_ADMIN,
	 * maybe it is fine to let the user keep both pieces if they
	 * try to reset an active GPU.
	 */

	mutex_lock(&dev->struct_mutex);

	release_firmware(adreno_gpu->fw[ADRENO_FW_PM4]);
	adreno_gpu->fw[ADRENO_FW_PM4] = NULL;

	release_firmware(adreno_gpu->fw[ADRENO_FW_PFP]);
	adreno_gpu->fw[ADRENO_FW_PFP] = NULL;

	if (a5xx_gpu->pm4_bo) {
		msm_gem_unpin_iova(a5xx_gpu->pm4_bo, gpu->aspace);
		drm_gem_object_put(a5xx_gpu->pm4_bo);
		a5xx_gpu->pm4_bo = NULL;
	}

	if (a5xx_gpu->pfp_bo) {
		msm_gem_unpin_iova(a5xx_gpu->pfp_bo, gpu->aspace);
		drm_gem_object_put(a5xx_gpu->pfp_bo);
		a5xx_gpu->pfp_bo = NULL;
	}

	gpu->needs_hw_init = true;

	pm_runtime_get_sync(&gpu->pdev->dev);
	gpu->funcs->recover(gpu);

	pm_runtime_put_sync(&gpu->pdev->dev);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(reset_fops, NULL, reset_set, "%llx\n");


void a5xx_debugfs_init(struct msm_gpu *gpu, struct drm_minor *minor)
{
	struct drm_device *dev;

	if (!minor)
		return;

	dev = minor->dev;

	drm_debugfs_create_files(a5xx_debugfs_list,
				 ARRAY_SIZE(a5xx_debugfs_list),
				 minor->debugfs_root, minor);

	debugfs_create_file("reset", S_IWUGO, minor->debugfs_root, dev,
			    &reset_fops);
}
