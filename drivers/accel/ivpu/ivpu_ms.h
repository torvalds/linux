/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */
#ifndef __IVPU_MS_H__
#define __IVPU_MS_H__

#include <linux/list.h>

struct drm_device;
struct drm_file;
struct ivpu_bo;
struct ivpu_device;
struct ivpu_file_priv;

struct ivpu_ms_instance {
	struct ivpu_bo *bo;
	struct list_head ms_instance_node;
	u64 mask;
	u64 buff_size;
	u64 active_buff_vpu_addr;
	u64 inactive_buff_vpu_addr;
	void *active_buff_ptr;
	void *inactive_buff_ptr;
	u64 leftover_bytes;
	void *leftover_addr;
};

int ivpu_ms_start_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
int ivpu_ms_stop_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
int ivpu_ms_get_data_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
int ivpu_ms_get_info_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
void ivpu_ms_cleanup(struct ivpu_file_priv *file_priv);
void ivpu_ms_cleanup_all(struct ivpu_device *vdev);

#endif /* __IVPU_MS_H__ */
