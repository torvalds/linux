/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _VKMS_DRV_H_
#define _VKMS_DRV_H_

#include <linux/hrtimer.h>

#include <drm/drm.h>
#include <drm/drm_gem.h>
#include <drm/drm_encoder.h>
#include <drm/drm_writeback.h>

#define XRES_MIN    20
#define YRES_MIN    20

#define XRES_DEF  1024
#define YRES_DEF   768

#define XRES_MAX  8192
#define YRES_MAX  8192

struct vkms_composer {
	struct drm_framebuffer fb;
	struct drm_rect src, dst;
	unsigned int offset;
	unsigned int pitch;
	unsigned int cpp;
};

/**
 * vkms_plane_state - Driver specific plane state
 * @base: base plane state
 * @composer: data required for composing computation
 */
struct vkms_plane_state {
	struct drm_plane_state base;
	struct vkms_composer *composer;
};

struct vkms_plane {
	struct drm_plane base;
};

/**
 * vkms_crtc_state - Driver specific CRTC state
 * @base: base CRTC state
 * @composer_work: work struct to compose and add CRC entries
 * @n_frame_start: start frame number for computed CRC
 * @n_frame_end: end frame number for computed CRC
 */
struct vkms_crtc_state {
	struct drm_crtc_state base;
	struct work_struct composer_work;

	int num_active_planes;
	/* stack of active planes for crc computation, should be in z order */
	struct vkms_plane_state **active_planes;
	void *active_writeback;

	/* below four are protected by vkms_output.composer_lock */
	bool crc_pending;
	bool wb_pending;
	u64 frame_start;
	u64 frame_end;
};

struct vkms_output {
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct drm_writeback_connector wb_connector;
	struct hrtimer vblank_hrtimer;
	ktime_t period_ns;
	struct drm_pending_vblank_event *event;
	/* ordered wq for composer_work */
	struct workqueue_struct *composer_workq;
	/* protects concurrent access to composer */
	spinlock_t lock;

	/* protected by @lock */
	bool composer_enabled;
	struct vkms_crtc_state *composer_state;

	spinlock_t composer_lock;
};

struct vkms_device;

struct vkms_config {
	bool writeback;
	bool cursor;
	bool overlay;
	/* only set when instantiated */
	struct vkms_device *dev;
};

struct vkms_device {
	struct drm_device drm;
	struct platform_device *platform;
	struct vkms_output output;
	const struct vkms_config *config;
};

#define drm_crtc_to_vkms_output(target) \
	container_of(target, struct vkms_output, crtc)

#define drm_device_to_vkms_device(target) \
	container_of(target, struct vkms_device, drm)

#define to_vkms_crtc_state(target)\
	container_of(target, struct vkms_crtc_state, base)

#define to_vkms_plane_state(target)\
	container_of(target, struct vkms_plane_state, base)

/* CRTC */
int vkms_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
		   struct drm_plane *primary, struct drm_plane *cursor);

int vkms_output_init(struct vkms_device *vkmsdev, int index);

struct vkms_plane *vkms_plane_init(struct vkms_device *vkmsdev,
				   enum drm_plane_type type, int index);

/* CRC Support */
const char *const *vkms_get_crc_sources(struct drm_crtc *crtc,
					size_t *count);
int vkms_set_crc_source(struct drm_crtc *crtc, const char *src_name);
int vkms_verify_crc_source(struct drm_crtc *crtc, const char *source_name,
			   size_t *values_cnt);

/* Composer Support */
void vkms_composer_worker(struct work_struct *work);
void vkms_set_composer(struct vkms_output *out, bool enabled);

/* Writeback */
int vkms_enable_writeback_connector(struct vkms_device *vkmsdev);

#endif /* _VKMS_DRV_H_ */
