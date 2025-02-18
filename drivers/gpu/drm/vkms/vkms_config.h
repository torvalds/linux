/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _VKMS_CONFIG_H_
#define _VKMS_CONFIG_H_

#include <linux/types.h>

#include "vkms_drv.h"

/**
 * struct vkms_config - General configuration for VKMS driver
 *
 * @writeback: If true, a writeback buffer can be attached to the CRTC
 * @cursor: If true, a cursor plane is created in the VKMS device
 * @overlay: If true, NUM_OVERLAY_PLANES will be created for the VKMS device
 * @dev: Used to store the current VKMS device. Only set when the device is instantiated.
 */
struct vkms_config {
	bool writeback;
	bool cursor;
	bool overlay;
	struct vkms_device *dev;
};

/**
 * vkms_config_create() - Create a new VKMS configuration
 *
 * Returns:
 * The new vkms_config or an error. Call vkms_config_destroy() to free the
 * returned configuration.
 */
struct vkms_config *vkms_config_create(void);

/**
 * vkms_config_destroy() - Free a VKMS configuration
 * @config: vkms_config to free
 */
void vkms_config_destroy(struct vkms_config *config);

/**
 * vkms_config_register_debugfs() - Register a debugfs file to show the device's
 * configuration
 * @vkms_device: Device to register
 */
void vkms_config_register_debugfs(struct vkms_device *vkms_device);

#endif /* _VKMS_CONFIG_H_ */
