/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _VKMS_DRV_H_
#define _VKMS_DRV_H_

#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_gem.h>
#include <drm/drm_encoder.h>
#include <linux/hrtimer.h>

#define XRES_MIN    32
#define YRES_MIN    32

#define XRES_DEF  1024
#define YRES_DEF   768

#define XRES_MAX  8192
#define YRES_MAX  8192

static const u32 vkms_formats[] = {
	DRM_FORMAT_XRGB8888,
};

struct vkms_output {
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct hrtimer vblank_hrtimer;
	ktime_t period_ns;
	struct drm_pending_vblank_event *event;
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
};

#define drm_crtc_to_vkms_output(target) \
	container_of(target, struct vkms_output, crtc)

#define drm_device_to_vkms_device(target) \
	container_of(target, struct vkms_device, drm)

/* CRTC */
int vkms_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
		   struct drm_plane *primary, struct drm_plane *cursor);

bool vkms_get_vblank_timestamp(struct drm_device *dev, unsigned int pipe,
			       int *max_error, ktime_t *vblank_time,
			       bool in_vblank_irq);

int vkms_output_init(struct vkms_device *vkmsdev);

struct drm_plane *vkms_plane_init(struct vkms_device *vkmsdev);

/* Gem stuff */
int vkms_gem_fault(struct vm_fault *vmf);

int vkms_dumb_create(struct drm_file *file, struct drm_device *dev,
		     struct drm_mode_create_dumb *args);

int vkms_dumb_map(struct drm_file *file, struct drm_device *dev,
		  u32 handle, u64 *offset);

void vkms_gem_free_object(struct drm_gem_object *obj);

#endif /* _VKMS_DRV_H_ */
