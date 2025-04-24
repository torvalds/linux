// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#include <drm/drm_file.h>
#include <linux/pm_runtime.h>

#include "ivpu_drv.h"
#include "ivpu_gem.h"
#include "ivpu_jsm_msg.h"
#include "ivpu_ms.h"
#include "ivpu_pm.h"

#define MS_INFO_BUFFER_SIZE	  SZ_64K
#define MS_NUM_BUFFERS		  2
#define MS_READ_PERIOD_MULTIPLIER 2
#define MS_MIN_SAMPLE_PERIOD_NS   1000000

static struct ivpu_ms_instance *
get_instance_by_mask(struct ivpu_file_priv *file_priv, u64 metric_mask)
{
	struct ivpu_ms_instance *ms;

	lockdep_assert_held(&file_priv->ms_lock);

	list_for_each_entry(ms, &file_priv->ms_instance_list, ms_instance_node)
		if (ms->mask == metric_mask)
			return ms;

	return NULL;
}

int ivpu_ms_start_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct drm_ivpu_metric_streamer_start *args = data;
	struct ivpu_device *vdev = file_priv->vdev;
	struct ivpu_ms_instance *ms;
	u64 single_buff_size;
	u32 sample_size;
	int ret;

	if (!args->metric_group_mask || !args->read_period_samples ||
	    args->sampling_period_ns < MS_MIN_SAMPLE_PERIOD_NS)
		return -EINVAL;

	ret = ivpu_rpm_get(vdev);
	if (ret < 0)
		return ret;

	mutex_lock(&file_priv->ms_lock);

	if (get_instance_by_mask(file_priv, args->metric_group_mask)) {
		ivpu_err(vdev, "Instance already exists (mask %#llx)\n", args->metric_group_mask);
		ret = -EALREADY;
		goto unlock;
	}

	ms = kzalloc(sizeof(*ms), GFP_KERNEL);
	if (!ms) {
		ret = -ENOMEM;
		goto unlock;
	}

	ms->mask = args->metric_group_mask;

	ret = ivpu_jsm_metric_streamer_info(vdev, ms->mask, 0, 0, &sample_size, NULL);
	if (ret)
		goto err_free_ms;

	single_buff_size = sample_size *
		((u64)args->read_period_samples * MS_READ_PERIOD_MULTIPLIER);
	ms->bo = ivpu_bo_create_global(vdev, PAGE_ALIGN(single_buff_size * MS_NUM_BUFFERS),
				       DRM_IVPU_BO_CACHED | DRM_IVPU_BO_MAPPABLE);
	if (!ms->bo) {
		ivpu_err(vdev, "Failed to allocate MS buffer (size %llu)\n", single_buff_size);
		ret = -ENOMEM;
		goto err_free_ms;
	}

	ms->buff_size = ivpu_bo_size(ms->bo) / MS_NUM_BUFFERS;
	ms->active_buff_vpu_addr = ms->bo->vpu_addr;
	ms->inactive_buff_vpu_addr = ms->bo->vpu_addr + ms->buff_size;
	ms->active_buff_ptr = ivpu_bo_vaddr(ms->bo);
	ms->inactive_buff_ptr = ivpu_bo_vaddr(ms->bo) + ms->buff_size;

	ret = ivpu_jsm_metric_streamer_start(vdev, ms->mask, args->sampling_period_ns,
					     ms->active_buff_vpu_addr, ms->buff_size);
	if (ret)
		goto err_free_bo;

	args->sample_size = sample_size;
	args->max_data_size = ivpu_bo_size(ms->bo);
	list_add_tail(&ms->ms_instance_node, &file_priv->ms_instance_list);
	goto unlock;

err_free_bo:
	ivpu_bo_free(ms->bo);
err_free_ms:
	kfree(ms);
unlock:
	mutex_unlock(&file_priv->ms_lock);

	ivpu_rpm_put(vdev);
	return ret;
}

static int
copy_leftover_bytes(struct ivpu_ms_instance *ms,
		    void __user *user_ptr, u64 user_size, u64 *user_bytes_copied)
{
	u64 copy_bytes;

	if (ms->leftover_bytes) {
		copy_bytes = min(user_size - *user_bytes_copied, ms->leftover_bytes);
		if (copy_to_user(user_ptr + *user_bytes_copied, ms->leftover_addr, copy_bytes))
			return -EFAULT;

		ms->leftover_bytes -= copy_bytes;
		ms->leftover_addr += copy_bytes;
		*user_bytes_copied += copy_bytes;
	}

	return 0;
}

static int
copy_samples_to_user(struct ivpu_device *vdev, struct ivpu_ms_instance *ms,
		     void __user *user_ptr, u64 user_size, u64 *user_bytes_copied)
{
	u64 bytes_written;
	int ret;

	*user_bytes_copied = 0;

	ret = copy_leftover_bytes(ms, user_ptr, user_size, user_bytes_copied);
	if (ret)
		return ret;

	if (*user_bytes_copied == user_size)
		return 0;

	ret = ivpu_jsm_metric_streamer_update(vdev, ms->mask, ms->inactive_buff_vpu_addr,
					      ms->buff_size, &bytes_written);
	if (ret)
		return ret;

	swap(ms->active_buff_vpu_addr, ms->inactive_buff_vpu_addr);
	swap(ms->active_buff_ptr, ms->inactive_buff_ptr);

	ms->leftover_bytes = bytes_written;
	ms->leftover_addr = ms->inactive_buff_ptr;

	return copy_leftover_bytes(ms, user_ptr, user_size, user_bytes_copied);
}

int ivpu_ms_get_data_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_ivpu_metric_streamer_get_data *args = data;
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct ivpu_device *vdev = file_priv->vdev;
	struct ivpu_ms_instance *ms;
	u64 bytes_written;
	int ret;

	if (!args->metric_group_mask)
		return -EINVAL;

	ret = ivpu_rpm_get(vdev);
	if (ret < 0)
		return ret;

	mutex_lock(&file_priv->ms_lock);

	ms = get_instance_by_mask(file_priv, args->metric_group_mask);
	if (!ms) {
		ivpu_err(vdev, "Instance doesn't exist for mask: %#llx\n", args->metric_group_mask);
		ret = -EINVAL;
		goto unlock;
	}

	if (!args->buffer_size) {
		ret = ivpu_jsm_metric_streamer_update(vdev, ms->mask, 0, 0, &bytes_written);
		if (ret)
			goto unlock;
		args->data_size = bytes_written + ms->leftover_bytes;
		goto unlock;
	}

	if (!args->buffer_ptr) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = copy_samples_to_user(vdev, ms, u64_to_user_ptr(args->buffer_ptr),
				   args->buffer_size, &args->data_size);
unlock:
	mutex_unlock(&file_priv->ms_lock);

	ivpu_rpm_put(vdev);
	return ret;
}

static void free_instance(struct ivpu_file_priv *file_priv, struct ivpu_ms_instance *ms)
{
	lockdep_assert_held(&file_priv->ms_lock);

	list_del(&ms->ms_instance_node);
	ivpu_jsm_metric_streamer_stop(file_priv->vdev, ms->mask);
	ivpu_bo_free(ms->bo);
	kfree(ms);
}

int ivpu_ms_stop_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct drm_ivpu_metric_streamer_stop *args = data;
	struct ivpu_device *vdev = file_priv->vdev;
	struct ivpu_ms_instance *ms;
	int ret;

	if (!args->metric_group_mask)
		return -EINVAL;

	ret = ivpu_rpm_get(vdev);
	if (ret < 0)
		return ret;

	mutex_lock(&file_priv->ms_lock);

	ms = get_instance_by_mask(file_priv, args->metric_group_mask);
	if (ms)
		free_instance(file_priv, ms);

	mutex_unlock(&file_priv->ms_lock);

	ivpu_rpm_put(vdev);
	return ms ? 0 : -EINVAL;
}

static inline struct ivpu_bo *get_ms_info_bo(struct ivpu_file_priv *file_priv)
{
	lockdep_assert_held(&file_priv->ms_lock);

	if (file_priv->ms_info_bo)
		return file_priv->ms_info_bo;

	file_priv->ms_info_bo = ivpu_bo_create_global(file_priv->vdev, MS_INFO_BUFFER_SIZE,
						      DRM_IVPU_BO_CACHED | DRM_IVPU_BO_MAPPABLE);
	return file_priv->ms_info_bo;
}

int ivpu_ms_get_info_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_ivpu_metric_streamer_get_data *args = data;
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct ivpu_device *vdev = file_priv->vdev;
	struct ivpu_bo *bo;
	u64 info_size;
	int ret;

	if (!args->metric_group_mask)
		return -EINVAL;

	if (!args->buffer_size)
		return ivpu_jsm_metric_streamer_info(vdev, args->metric_group_mask,
						     0, 0, NULL, &args->data_size);
	if (!args->buffer_ptr)
		return -EINVAL;

	mutex_lock(&file_priv->ms_lock);

	bo = get_ms_info_bo(file_priv);
	if (!bo) {
		ret = -ENOMEM;
		goto unlock;
	}

	ret = ivpu_jsm_metric_streamer_info(vdev, args->metric_group_mask, bo->vpu_addr,
					    ivpu_bo_size(bo), NULL, &info_size);
	if (ret)
		goto unlock;

	if (args->buffer_size < info_size) {
		ret = -ENOSPC;
		goto unlock;
	}

	if (copy_to_user(u64_to_user_ptr(args->buffer_ptr), ivpu_bo_vaddr(bo), info_size))
		ret = -EFAULT;

	args->data_size = info_size;
unlock:
	mutex_unlock(&file_priv->ms_lock);

	return ret;
}

void ivpu_ms_cleanup(struct ivpu_file_priv *file_priv)
{
	struct ivpu_ms_instance *ms, *tmp;
	struct ivpu_device *vdev = file_priv->vdev;

	pm_runtime_get_sync(vdev->drm.dev);

	mutex_lock(&file_priv->ms_lock);

	if (file_priv->ms_info_bo) {
		ivpu_bo_free(file_priv->ms_info_bo);
		file_priv->ms_info_bo = NULL;
	}

	list_for_each_entry_safe(ms, tmp, &file_priv->ms_instance_list, ms_instance_node)
		free_instance(file_priv, ms);

	mutex_unlock(&file_priv->ms_lock);

	pm_runtime_put_autosuspend(vdev->drm.dev);
}

void ivpu_ms_cleanup_all(struct ivpu_device *vdev)
{
	struct ivpu_file_priv *file_priv;
	unsigned long ctx_id;

	mutex_lock(&vdev->context_list_lock);

	xa_for_each(&vdev->context_xa, ctx_id, file_priv)
		ivpu_ms_cleanup(file_priv);

	mutex_unlock(&vdev->context_list_lock);
}
