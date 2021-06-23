// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include "msm_disp_snapshot.h"

static ssize_t __maybe_unused disp_devcoredump_read(char *buffer, loff_t offset,
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

static void _msm_disp_snapshot_work(struct kthread_work *work)
{
	struct msm_kms *kms = container_of(work, struct msm_kms, dump_work);
	struct drm_device *drm_dev = kms->dev;
	struct msm_disp_state *disp_state;
	struct drm_printer p;

	disp_state = kzalloc(sizeof(struct msm_disp_state), GFP_KERNEL);
	if (!disp_state)
		return;

	disp_state->dev = drm_dev->dev;
	disp_state->drm_dev = drm_dev;

	INIT_LIST_HEAD(&disp_state->blocks);

	/* Serialize dumping here */
	mutex_lock(&kms->dump_mutex);

	msm_disp_snapshot_capture_state(disp_state);

	mutex_unlock(&kms->dump_mutex);

	if (MSM_DISP_SNAPSHOT_DUMP_IN_CONSOLE) {
		p = drm_info_printer(disp_state->drm_dev->dev);
		msm_disp_state_print(disp_state, &p);
	}

	/*
	 * If COREDUMP is disabled, the stub will call the free function.
	 * If there is a codedump pending for the device, the dev_coredumpm()
	 * will also free new coredump state.
	 */
	dev_coredumpm(disp_state->dev, THIS_MODULE, disp_state, 0, GFP_KERNEL,
			disp_devcoredump_read, msm_disp_state_free);
}

void msm_disp_snapshot_state(struct drm_device *drm_dev)
{
	struct msm_drm_private *priv;
	struct msm_kms *kms;

	if (!drm_dev) {
		DRM_ERROR("invalid params\n");
		return;
	}

	priv = drm_dev->dev_private;
	kms = priv->kms;

	kthread_queue_work(kms->dump_worker, &kms->dump_work);
}

int msm_disp_snapshot_init(struct drm_device *drm_dev)
{
	struct msm_drm_private *priv;
	struct msm_kms *kms;

	if (!drm_dev) {
		DRM_ERROR("invalid params\n");
		return -EINVAL;
	}

	priv = drm_dev->dev_private;
	kms = priv->kms;

	mutex_init(&kms->dump_mutex);

	kms->dump_worker = kthread_create_worker(0, "%s", "disp_snapshot");
	if (IS_ERR(kms->dump_worker))
		DRM_ERROR("failed to create disp state task\n");

	kthread_init_work(&kms->dump_work, _msm_disp_snapshot_work);

	return 0;
}

void msm_disp_snapshot_destroy(struct drm_device *drm_dev)
{
	struct msm_kms *kms;
	struct msm_drm_private *priv;

	if (!drm_dev) {
		DRM_ERROR("invalid params\n");
		return;
	}

	priv = drm_dev->dev_private;
	kms = priv->kms;

	if (kms->dump_worker)
		kthread_destroy_worker(kms->dump_worker);

	mutex_destroy(&kms->dump_mutex);
}
