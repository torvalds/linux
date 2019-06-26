/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _VKMS_DRV_H_
#define _VKMS_DRV_H_

#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_gem.h>
#include <drm/drm_encoder.h>
#include <linux/hrtimer.h>

#define XRES_MIN    20
#define YRES_MIN    20

#define XRES_DEF  1024
#define YRES_DEF   768

#define XRES_MAX  8192
#define YRES_MAX  8192

extern bool enable_cursor;

struct vkms_crc_data {
	struct drm_framebuffer fb;
	struct drm_rect src, dst;
	unsigned int offset;
	unsigned int pitch;
	unsigned int cpp;
};

/**
 * vkms_plane_state - Driver specific plane state
 * @base: base plane state
 * @crc_data: data required for CRC computation
 */
struct vkms_plane_state {
	struct drm_plane_state base;
	struct vkms_crc_data *crc_data;
};

/**
 * vkms_crtc_state - Driver specific CRTC state
 * @base: base CRTC state
 * @crc_work: work struct to compute and add CRC entries
 * @n_frame_start: start frame number for computed CRC
 * @n_frame_end: end frame number for computed CRC
 */
struct vkms_crtc_state {
	struct drm_crtc_state base;
	struct work_struct crc_work;

	int num_active_planes;
	/* stack of active planes for crc computation, should be in z order */
	struct vkms_plane_state **active_planes;

	/* below three are protected by vkms_output.crc_lock */
	bool crc_pending;
	u64 frame_start;
	u64 frame_end;
};

struct vkms_output {
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct hrtimer vblank_hrtimer;
	ktime_t period_ns;
	struct drm_pending_vblank_event *event;
	/* ordered wq for crc_work */
	struct workqueue_struct *crc_workq;
	/* protects concurrent access to crc_data */
	spinlock_t lock;

	/* protected by @lock */
	bool crc_enabled;
	struct vkms_crtc_state *crc_state;

	spinlock_t crc_lock;
};

struct vkms_device {
	struct drm_device drm;
	struct platform_device *platform;
	struct vkms_output output;
};

struct vkms_gem_object {
	struct drm_gem_object gem;
	struct mutex pages_lock; /* Page lock used in page fault handler */
	struct page **pages;
	unsigned int vmap_count;
	void *vaddr;
};

#define drm_crtc_to_vkms_output(target) \
	container_of(target, struct vkms_output, crtc)

#define drm_device_to_vkms_device(target) \
	container_of(target, struct vkms_device, drm)

#define drm_gem_to_vkms_gem(target)\
	container_of(target, struct vkms_gem_object, gem)

#define to_vkms_crtc_state(target)\
	container_of(target, struct vkms_crtc_state, base)

#define to_vkms_plane_state(target)\
	container_of(target, struct vkms_plane_state, base)

/* CRTC */
int vkms_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
		   struct drm_plane *primary, struct drm_plane *cursor);

bool vkms_get_vblank_timestamp(struct drm_device *dev, unsigned int pipe,
			       int *max_error, ktime_t *vblank_time,
			       bool in_vblank_irq);

int vkms_output_init(struct vkms_device *vkmsdev, int index);

struct drm_plane *vkms_plane_init(struct vkms_device *vkmsdev,
				  enum drm_plane_type type, int index);

/* Gem stuff */
struct drm_gem_object *vkms_gem_create(struct drm_device *dev,
				       struct drm_file *file,
				       u32 *handle,
				       u64 size);

vm_fault_t vkms_gem_fault(struct vm_fault *vmf);

int vkms_dumb_create(struct drm_file *file, struct drm_device *dev,
		     struct drm_mode_create_dumb *args);

void vkms_gem_free_object(struct drm_gem_object *obj);

int vkms_gem_vmap(struct drm_gem_object *obj);

void vkms_gem_vunmap(struct drm_gem_object *obj);

/* CRC Support */
const char *const *vkms_get_crc_sources(struct drm_crtc *crtc,
					size_t *count);
int vkms_set_crc_source(struct drm_crtc *crtc, const char *src_name);
int vkms_verify_crc_source(struct drm_crtc *crtc, const char *source_name,
			   size_t *values_cnt);
void vkms_crc_work_handle(struct work_struct *work);

#endif /* _VKMS_DRV_H_ */
