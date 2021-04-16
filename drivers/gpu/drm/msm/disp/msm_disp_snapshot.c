// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include "msm_disp_snapshot.h"

#ifdef CONFIG_DEV_COREDUMP
static ssize_t disp_devcoredump_read(char *buffer, loff_t offset,
		size_t count, void *data, size_t datalen)
{
	struct drm_print_iterator iter;
	struct drm_printer p;
	struct msm_disp_state *disp_state;

	disp_state = data;

	iter.data = buffer;
	iter.offset = 0;
	iter.start = offset;
	iter.remain = count;

	p = drm_coredump_printer(&iter);

	msm_disp_state_print(disp_state, &p);

	return count - iter.remain;
}

static void disp_devcoredump_free(void *data)
{
	struct msm_disp_state *disp_state;

	disp_state = data;

	msm_disp_state_free(disp_state);

	disp_state->coredump_pending = false;
}
#endif /* CONFIG_DEV_COREDUMP */

static void _msm_disp_snapshot_work(struct kthread_work *work)
{
	struct msm_disp_state *disp_state = container_of(work, struct msm_disp_state, dump_work);
	struct drm_printer p;

	mutex_lock(&disp_state->mutex);

	msm_disp_snapshot_capture_state(disp_state);

	if (MSM_DISP_SNAPSHOT_DUMP_IN_CONSOLE) {
		p = drm_info_printer(disp_state->drm_dev->dev);
		msm_disp_state_print(disp_state, &p);
	}

	/*
	 * if devcoredump is not defined free the state immediately
	 * otherwise it will be freed in the free handler.
	 */
#ifdef CONFIG_DEV_COREDUMP
	dev_coredumpm(disp_state->dev, THIS_MODULE, disp_state, 0, GFP_KERNEL,
			disp_devcoredump_read, disp_devcoredump_free);
	disp_state->coredump_pending = true;
#else
	msm_disp_state_free(disp_state);
#endif

	mutex_unlock(&disp_state->mutex);
}

void msm_disp_snapshot_state(struct drm_device *drm_dev)
{
	struct msm_drm_private *priv;
	struct msm_kms *kms;
	struct msm_disp_state *disp_state;

	if (!drm_dev) {
		DRM_ERROR("invalid params\n");
		return;
	}

	priv = drm_dev->dev_private;
	kms = priv->kms;
	disp_state = kms->disp_state;

	if (!disp_state) {
		DRM_ERROR("invalid params\n");
		return;
	}

	/*
	 * if there is a coredump pending return immediately till dump
	 * if read by userspace or timeout happens
	 */
	if (disp_state->coredump_pending) {
		DRM_DEBUG("coredump is pending read\n");
		return;
	}

	kthread_queue_work(disp_state->dump_worker,
			&disp_state->dump_work);
}

int msm_disp_snapshot_init(struct drm_device *drm_dev)
{
	struct msm_drm_private *priv;
	struct msm_disp_state *disp_state;
	struct msm_kms *kms;

	if (!drm_dev) {
		DRM_ERROR("invalid params\n");
		return -EINVAL;
	}

	priv = drm_dev->dev_private;
	kms = priv->kms;

	disp_state = devm_kzalloc(drm_dev->dev, sizeof(struct msm_disp_state), GFP_KERNEL);

	mutex_init(&disp_state->mutex);

	disp_state->dev = drm_dev->dev;
	disp_state->drm_dev = drm_dev;

	INIT_LIST_HEAD(&disp_state->blocks);

	disp_state->dump_worker = kthread_create_worker(0, "%s", "disp_snapshot");
	if (IS_ERR(disp_state->dump_worker))
		DRM_ERROR("failed to create disp state task\n");

	kthread_init_work(&disp_state->dump_work, _msm_disp_snapshot_work);

	kms->disp_state = disp_state;

	return 0;
}

void msm_disp_snapshot_destroy(struct drm_device *drm_dev)
{
	struct msm_kms *kms;
	struct msm_drm_private *priv;
	struct msm_disp_state *disp_state;

	if (!drm_dev) {
		DRM_ERROR("invalid params\n");
		return;
	}

	priv = drm_dev->dev_private;
	kms = priv->kms;
	disp_state = kms->disp_state;

	if (disp_state->dump_worker)
		kthread_destroy_worker(disp_state->dump_worker);

	list_del(&disp_state->blocks);

	mutex_destroy(&disp_state->mutex);
}
