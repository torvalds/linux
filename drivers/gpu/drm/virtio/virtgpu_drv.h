/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef VIRTIO_DRV_H
#define VIRTIO_DRV_H

#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/virtio_gpu.h>

#include <drm/drm_atomic.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_probe_helper.h>
#include <drm/virtgpu_drm.h>

#define DRIVER_NAME "virtio_gpu"
#define DRIVER_DESC "virtio GPU"
#define DRIVER_DATE "0"

#define DRIVER_MAJOR 0
#define DRIVER_MINOR 1
#define DRIVER_PATCHLEVEL 0

#define STATE_INITIALIZING 0
#define STATE_OK 1
#define STATE_ERR 2

struct virtio_gpu_object_params {
	unsigned long size;
	bool dumb;
	/* 3d */
	bool virgl;
	bool blob;

	/* classic resources only */
	uint32_t format;
	uint32_t width;
	uint32_t height;
	uint32_t target;
	uint32_t bind;
	uint32_t depth;
	uint32_t array_size;
	uint32_t last_level;
	uint32_t nr_samples;
	uint32_t flags;

	/* blob resources only */
	uint32_t ctx_id;
	uint32_t blob_mem;
	uint32_t blob_flags;
	uint64_t blob_id;
};

struct virtio_gpu_object {
	struct drm_gem_shmem_object base;
	uint32_t hw_res_handle;
	bool dumb;
	bool created;
	bool host3d_blob, guest_blob;
	uint32_t blob_mem, blob_flags;

	int uuid_state;
	uuid_t uuid;
};
#define gem_to_virtio_gpu_obj(gobj) \
	container_of((gobj), struct virtio_gpu_object, base.base)

struct virtio_gpu_object_shmem {
	struct virtio_gpu_object base;
	struct sg_table *pages;
	uint32_t mapped;
};

struct virtio_gpu_object_vram {
	struct virtio_gpu_object base;
	uint32_t map_state;
	uint32_t map_info;
	struct drm_mm_node vram_node;
};

#define to_virtio_gpu_shmem(virtio_gpu_object) \
	container_of((virtio_gpu_object), struct virtio_gpu_object_shmem, base)

#define to_virtio_gpu_vram(virtio_gpu_object) \
	container_of((virtio_gpu_object), struct virtio_gpu_object_vram, base)

struct virtio_gpu_object_array {
	struct ww_acquire_ctx ticket;
	struct list_head next;
	u32 nents, total;
	struct drm_gem_object *objs[];
};

struct virtio_gpu_vbuffer;
struct virtio_gpu_device;

typedef void (*virtio_gpu_resp_cb)(struct virtio_gpu_device *vgdev,
				   struct virtio_gpu_vbuffer *vbuf);

struct virtio_gpu_fence_driver {
	atomic64_t       last_seq;
	uint64_t         sync_seq;
	uint64_t         context;
	struct list_head fences;
	spinlock_t       lock;
};

struct virtio_gpu_fence {
	struct dma_fence f;
	struct virtio_gpu_fence_driver *drv;
	struct list_head node;
};

struct virtio_gpu_vbuffer {
	char *buf;
	int size;

	void *data_buf;
	uint32_t data_size;

	char *resp_buf;
	int resp_size;
	virtio_gpu_resp_cb resp_cb;
	void *resp_cb_data;

	struct virtio_gpu_object_array *objs;
	struct list_head list;
};

struct virtio_gpu_output {
	int index;
	struct drm_crtc crtc;
	struct drm_connector conn;
	struct drm_encoder enc;
	struct virtio_gpu_display_one info;
	struct virtio_gpu_update_cursor cursor;
	struct edid *edid;
	int cur_x;
	int cur_y;
	bool needs_modeset;
};
#define drm_crtc_to_virtio_gpu_output(x) \
	container_of(x, struct virtio_gpu_output, crtc)

struct virtio_gpu_framebuffer {
	struct drm_framebuffer base;
	struct virtio_gpu_fence *fence;
};
#define to_virtio_gpu_framebuffer(x) \
	container_of(x, struct virtio_gpu_framebuffer, base)

struct virtio_gpu_queue {
	struct virtqueue *vq;
	spinlock_t qlock;
	wait_queue_head_t ack_queue;
	struct work_struct dequeue_work;
};

struct virtio_gpu_drv_capset {
	uint32_t id;
	uint32_t max_version;
	uint32_t max_size;
};

struct virtio_gpu_drv_cap_cache {
	struct list_head head;
	void *caps_cache;
	uint32_t id;
	uint32_t version;
	uint32_t size;
	atomic_t is_valid;
};

struct virtio_gpu_device {
	struct device *dev;
	struct drm_device *ddev;

	struct virtio_device *vdev;

	struct virtio_gpu_output outputs[VIRTIO_GPU_MAX_SCANOUTS];
	uint32_t num_scanouts;

	struct virtio_gpu_queue ctrlq;
	struct virtio_gpu_queue cursorq;
	struct kmem_cache *vbufs;

	atomic_t pending_commands;

	struct ida	resource_ida;

	wait_queue_head_t resp_wq;
	/* current display info */
	spinlock_t display_info_lock;
	bool display_info_pending;

	struct virtio_gpu_fence_driver fence_drv;

	struct ida	ctx_id_ida;

	bool has_virgl_3d;
	bool has_edid;
	bool has_indirect;
	bool has_resource_assign_uuid;
	bool has_resource_blob;
	bool has_host_visible;
	struct virtio_shm_region host_visible_region;
	struct drm_mm host_visible_mm;

	struct work_struct config_changed_work;

	struct work_struct obj_free_work;
	spinlock_t obj_free_lock;
	struct list_head obj_free_list;

	struct virtio_gpu_drv_capset *capsets;
	uint32_t num_capsets;
	struct list_head cap_cache;

	/* protects uuid state when exporting */
	spinlock_t resource_export_lock;
	/* protects map state and host_visible_mm */
	spinlock_t host_visible_lock;
};

struct virtio_gpu_fpriv {
	uint32_t ctx_id;
	bool context_created;
	struct mutex context_lock;
};

/* virtgpu_ioctl.c */
#define DRM_VIRTIO_NUM_IOCTLS 10
extern struct drm_ioctl_desc virtio_gpu_ioctls[DRM_VIRTIO_NUM_IOCTLS];
void virtio_gpu_create_context(struct drm_device *dev, struct drm_file *file);

/* virtgpu_kms.c */
int virtio_gpu_init(struct drm_device *dev);
void virtio_gpu_deinit(struct drm_device *dev);
void virtio_gpu_release(struct drm_device *dev);
int virtio_gpu_driver_open(struct drm_device *dev, struct drm_file *file);
void virtio_gpu_driver_postclose(struct drm_device *dev, struct drm_file *file);

/* virtgpu_gem.c */
int virtio_gpu_gem_object_open(struct drm_gem_object *obj,
			       struct drm_file *file);
void virtio_gpu_gem_object_close(struct drm_gem_object *obj,
				 struct drm_file *file);
int virtio_gpu_mode_dumb_create(struct drm_file *file_priv,
				struct drm_device *dev,
				struct drm_mode_create_dumb *args);
int virtio_gpu_mode_dumb_mmap(struct drm_file *file_priv,
			      struct drm_device *dev,
			      uint32_t handle, uint64_t *offset_p);

struct virtio_gpu_object_array *virtio_gpu_array_alloc(u32 nents);
struct virtio_gpu_object_array*
virtio_gpu_array_from_handles(struct drm_file *drm_file, u32 *handles, u32 nents);
void virtio_gpu_array_add_obj(struct virtio_gpu_object_array *objs,
			      struct drm_gem_object *obj);
int virtio_gpu_array_lock_resv(struct virtio_gpu_object_array *objs);
void virtio_gpu_array_unlock_resv(struct virtio_gpu_object_array *objs);
void virtio_gpu_array_add_fence(struct virtio_gpu_object_array *objs,
				struct dma_fence *fence);
void virtio_gpu_array_put_free(struct virtio_gpu_object_array *objs);
void virtio_gpu_array_put_free_delayed(struct virtio_gpu_device *vgdev,
				       struct virtio_gpu_object_array *objs);
void virtio_gpu_array_put_free_work(struct work_struct *work);

/* virtgpu_vq.c */
int virtio_gpu_alloc_vbufs(struct virtio_gpu_device *vgdev);
void virtio_gpu_free_vbufs(struct virtio_gpu_device *vgdev);
void virtio_gpu_cmd_create_resource(struct virtio_gpu_device *vgdev,
				    struct virtio_gpu_object *bo,
				    struct virtio_gpu_object_params *params,
				    struct virtio_gpu_object_array *objs,
				    struct virtio_gpu_fence *fence);
void virtio_gpu_cmd_unref_resource(struct virtio_gpu_device *vgdev,
				   struct virtio_gpu_object *bo);
void virtio_gpu_cmd_transfer_to_host_2d(struct virtio_gpu_device *vgdev,
					uint64_t offset,
					uint32_t width, uint32_t height,
					uint32_t x, uint32_t y,
					struct virtio_gpu_object_array *objs,
					struct virtio_gpu_fence *fence);
void virtio_gpu_cmd_resource_flush(struct virtio_gpu_device *vgdev,
				   uint32_t resource_id,
				   uint32_t x, uint32_t y,
				   uint32_t width, uint32_t height);
void virtio_gpu_cmd_set_scanout(struct virtio_gpu_device *vgdev,
				uint32_t scanout_id, uint32_t resource_id,
				uint32_t width, uint32_t height,
				uint32_t x, uint32_t y);
void virtio_gpu_object_attach(struct virtio_gpu_device *vgdev,
			      struct virtio_gpu_object *obj,
			      struct virtio_gpu_mem_entry *ents,
			      unsigned int nents);
int virtio_gpu_attach_status_page(struct virtio_gpu_device *vgdev);
int virtio_gpu_detach_status_page(struct virtio_gpu_device *vgdev);
void virtio_gpu_cursor_ping(struct virtio_gpu_device *vgdev,
			    struct virtio_gpu_output *output);
int virtio_gpu_cmd_get_display_info(struct virtio_gpu_device *vgdev);
int virtio_gpu_cmd_get_capset_info(struct virtio_gpu_device *vgdev, int idx);
int virtio_gpu_cmd_get_capset(struct virtio_gpu_device *vgdev,
			      int idx, int version,
			      struct virtio_gpu_drv_cap_cache **cache_p);
int virtio_gpu_cmd_get_edids(struct virtio_gpu_device *vgdev);
void virtio_gpu_cmd_context_create(struct virtio_gpu_device *vgdev, uint32_t id,
				   uint32_t nlen, const char *name);
void virtio_gpu_cmd_context_destroy(struct virtio_gpu_device *vgdev,
				    uint32_t id);
void virtio_gpu_cmd_context_attach_resource(struct virtio_gpu_device *vgdev,
					    uint32_t ctx_id,
					    struct virtio_gpu_object_array *objs);
void virtio_gpu_cmd_context_detach_resource(struct virtio_gpu_device *vgdev,
					    uint32_t ctx_id,
					    struct virtio_gpu_object_array *objs);
void virtio_gpu_cmd_submit(struct virtio_gpu_device *vgdev,
			   void *data, uint32_t data_size,
			   uint32_t ctx_id,
			   struct virtio_gpu_object_array *objs,
			   struct virtio_gpu_fence *fence);
void virtio_gpu_cmd_transfer_from_host_3d(struct virtio_gpu_device *vgdev,
					  uint32_t ctx_id,
					  uint64_t offset, uint32_t level,
					  struct drm_virtgpu_3d_box *box,
					  struct virtio_gpu_object_array *objs,
					  struct virtio_gpu_fence *fence);
void virtio_gpu_cmd_transfer_to_host_3d(struct virtio_gpu_device *vgdev,
					uint32_t ctx_id,
					uint64_t offset, uint32_t level,
					struct drm_virtgpu_3d_box *box,
					struct virtio_gpu_object_array *objs,
					struct virtio_gpu_fence *fence);
void
virtio_gpu_cmd_resource_create_3d(struct virtio_gpu_device *vgdev,
				  struct virtio_gpu_object *bo,
				  struct virtio_gpu_object_params *params,
				  struct virtio_gpu_object_array *objs,
				  struct virtio_gpu_fence *fence);
void virtio_gpu_ctrl_ack(struct virtqueue *vq);
void virtio_gpu_cursor_ack(struct virtqueue *vq);
void virtio_gpu_fence_ack(struct virtqueue *vq);
void virtio_gpu_dequeue_ctrl_func(struct work_struct *work);
void virtio_gpu_dequeue_cursor_func(struct work_struct *work);
void virtio_gpu_dequeue_fence_func(struct work_struct *work);

void virtio_gpu_notify(struct virtio_gpu_device *vgdev);

int
virtio_gpu_cmd_resource_assign_uuid(struct virtio_gpu_device *vgdev,
				    struct virtio_gpu_object_array *objs);

int virtio_gpu_cmd_map(struct virtio_gpu_device *vgdev,
		       struct virtio_gpu_object_array *objs, uint64_t offset);

void virtio_gpu_cmd_unmap(struct virtio_gpu_device *vgdev,
			  struct virtio_gpu_object *bo);

/* virtgpu_display.c */
int virtio_gpu_modeset_init(struct virtio_gpu_device *vgdev);
void virtio_gpu_modeset_fini(struct virtio_gpu_device *vgdev);

/* virtgpu_plane.c */
uint32_t virtio_gpu_translate_format(uint32_t drm_fourcc);
struct drm_plane *virtio_gpu_plane_init(struct virtio_gpu_device *vgdev,
					enum drm_plane_type type,
					int index);

/* virtgpu_fence.c */
struct virtio_gpu_fence *virtio_gpu_fence_alloc(
	struct virtio_gpu_device *vgdev);
void virtio_gpu_fence_emit(struct virtio_gpu_device *vgdev,
			  struct virtio_gpu_ctrl_hdr *cmd_hdr,
			  struct virtio_gpu_fence *fence);
void virtio_gpu_fence_event_process(struct virtio_gpu_device *vdev,
				    u64 last_seq);

/* virtgpu_object.c */
void virtio_gpu_cleanup_object(struct virtio_gpu_object *bo);
struct drm_gem_object *virtio_gpu_create_object(struct drm_device *dev,
						size_t size);
int virtio_gpu_object_create(struct virtio_gpu_device *vgdev,
			     struct virtio_gpu_object_params *params,
			     struct virtio_gpu_object **bo_ptr,
			     struct virtio_gpu_fence *fence);

bool virtio_gpu_is_shmem(struct virtio_gpu_object *bo);

int virtio_gpu_resource_id_get(struct virtio_gpu_device *vgdev,
			       uint32_t *resid);
/* virtgpu_prime.c */
struct dma_buf *virtgpu_gem_prime_export(struct drm_gem_object *obj,
					 int flags);
struct drm_gem_object *virtgpu_gem_prime_import(struct drm_device *dev,
						struct dma_buf *buf);
int virtgpu_gem_prime_get_uuid(struct drm_gem_object *obj,
			       uuid_t *uuid);
struct drm_gem_object *virtgpu_gem_prime_import_sg_table(
	struct drm_device *dev, struct dma_buf_attachment *attach,
	struct sg_table *sgt);

/* virtgpu_debugfs.c */
void virtio_gpu_debugfs_init(struct drm_minor *minor);

/* virtgpu_vram.c */
bool virtio_gpu_is_vram(struct virtio_gpu_object *bo);
int virtio_gpu_vram_create(struct virtio_gpu_device *vgdev,
			   struct virtio_gpu_object_params *params,
			   struct virtio_gpu_object **bo_ptr);
#endif
