#ifndef _VKMS_DRV_H_
#define _VKMS_DRV_H_

#include <drm/drm_simple_kms_helper.h>

struct vkms_device {
	struct drm_device drm;
	struct platform_device *platform;
	struct drm_simple_display_pipe pipe;
	struct drm_connector connector;
};

#endif /* _VKMS_DRV_H_ */
