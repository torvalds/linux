// SPDX-License-Identifier: MIT

#include <linux/export.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_print.h>

#include "drm_client_internal.h"

static char drm_client_default[16] = CONFIG_DRM_CLIENT_DEFAULT;
module_param_string(active, drm_client_default, sizeof(drm_client_default), 0444);
MODULE_PARM_DESC(active,
		 "Choose which drm client to start, default is "
		 CONFIG_DRM_CLIENT_DEFAULT);

/**
 * drm_client_setup() - Setup in-kernel DRM clients
 * @dev: DRM device
 * @format: Preferred pixel format for the device. Use NULL, unless
 *          there is clearly a driver-preferred format.
 *
 * This function sets up the in-kernel DRM clients. Restore, hotplug
 * events and teardown are all taken care of.
 *
 * Drivers should call drm_client_setup() after registering the new
 * DRM device with drm_dev_register(). This function is safe to call
 * even when there are no connectors present. Setup will be retried
 * on the next hotplug event.
 *
 * The clients are destroyed by drm_dev_unregister().
 */
void drm_client_setup(struct drm_device *dev, const struct drm_format_info *format)
{
	if (!drm_core_check_feature(dev, DRIVER_MODESET)) {
		drm_dbg(dev, "driver does not support mode-setting, skipping DRM clients\n");
		return;
	}

#ifdef CONFIG_DRM_FBDEV_EMULATION
	if (!strcmp(drm_client_default, "fbdev")) {
		int ret;

		ret = drm_fbdev_client_setup(dev, format);
		if (ret)
			drm_warn(dev, "Failed to set up DRM client; error %d\n", ret);
		return;
	}
#endif

#ifdef CONFIG_DRM_CLIENT_LOG
	if (!strcmp(drm_client_default, "log")) {
		drm_log_register(dev);
		return;
	}
#endif
	if (strcmp(drm_client_default, ""))
		drm_warn(dev, "Unknown DRM client %s\n", drm_client_default);
}
EXPORT_SYMBOL(drm_client_setup);

/**
 * drm_client_setup_with_fourcc() - Setup in-kernel DRM clients for color mode
 * @dev: DRM device
 * @fourcc: Preferred pixel format as 4CC code for the device
 *
 * This function sets up the in-kernel DRM clients. It is equivalent
 * to drm_client_setup(), but expects a 4CC code as second argument.
 */
void drm_client_setup_with_fourcc(struct drm_device *dev, u32 fourcc)
{
	drm_client_setup(dev, drm_format_info(fourcc));
}
EXPORT_SYMBOL(drm_client_setup_with_fourcc);

/**
 * drm_client_setup_with_color_mode() - Setup in-kernel DRM clients for color mode
 * @dev: DRM device
 * @color_mode: Preferred color mode for the device
 *
 * This function sets up the in-kernel DRM clients. It is equivalent
 * to drm_client_setup(), but expects a color mode as second argument.
 *
 * Do not use this function in new drivers. Prefer drm_client_setup() with a
 * format of NULL.
 */
void drm_client_setup_with_color_mode(struct drm_device *dev, unsigned int color_mode)
{
	u32 fourcc = drm_driver_color_mode_format(dev, color_mode);

	drm_client_setup_with_fourcc(dev, fourcc);
}
EXPORT_SYMBOL(drm_client_setup_with_color_mode);

MODULE_DESCRIPTION("In-kernel DRM clients");
MODULE_LICENSE("GPL and additional rights");
