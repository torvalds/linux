#ifndef _VKMS_DRV_H_
#define _VKMS_DRV_H_

#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_encoder.h>

static const u32 vkms_formats[] = {
	DRM_FORMAT_XRGB8888,
};

struct vkms_output {
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
};

struct vkms_device {
	struct drm_device drm;
	struct platform_device *platform;
	struct vkms_output output;
};

int vkms_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
		   struct drm_plane *primary, struct drm_plane *cursor);

int vkms_output_init(struct vkms_device *vkmsdev);

struct drm_plane *vkms_plane_init(struct vkms_device *vkmsdev);

#endif /* _VKMS_DRV_H_ */
