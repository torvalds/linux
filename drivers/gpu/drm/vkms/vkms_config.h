/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _VKMS_CONFIG_H_
#define _VKMS_CONFIG_H_

#include <linux/types.h>

#include "vkms_drv.h"

/**
 * struct vkms_config - General configuration for VKMS driver
 *
 * @dev_name: Name of the device
 * @writeback: If true, a writeback buffer can be attached to the CRTC
 * @cursor: If true, a cursor plane is created in the VKMS device
 * @overlay: If true, NUM_OVERLAY_PLANES will be created for the VKMS device
 * @dev: Used to store the current VKMS device. Only set when the device is instantiated.
 */
struct vkms_config {
	const char *dev_name;
	bool writeback;
	bool cursor;
	bool overlay;
	struct vkms_device *dev;
};

/**
 * vkms_config_create() - Create a new VKMS configuration
 * @dev_name: Name of the device
 *
 * Returns:
 * The new vkms_config or an error. Call vkms_config_destroy() to free the
 * returned configuration.
 */
struct vkms_config *vkms_config_create(const char *dev_name);

/**
 * vkms_config_default_create() - Create the configuration for the default device
 * @enable_cursor: Create or not a cursor plane
 * @enable_writeback: Create or not a writeback connector
 * @enable_overlay: Create or not overlay planes
 *
 * Returns:
 * The default vkms_config or an error. Call vkms_config_destroy() to free the
 * returned configuration.
 */
struct vkms_config *vkms_config_default_create(bool enable_cursor,
					       bool enable_writeback,
					       bool enable_overlay);

/**
 * vkms_config_destroy() - Free a VKMS configuration
 * @config: vkms_config to free
 */
void vkms_config_destroy(struct vkms_config *config);

/**
 * vkms_config_get_device_name() - Return the name of the device
 * @config: Configuration to get the device name from
 *
 * Returns:
 * The device name. Only valid while @config is valid.
 */
static inline const char *
vkms_config_get_device_name(struct vkms_config *config)
{
	return config->dev_name;
}

/**
 * vkms_config_is_valid() - Validate a configuration
 * @config: Configuration to validate
 *
 * Returns:
 * Whether the configuration is valid or not.
 * For example, a configuration without primary planes is not valid.
 */
bool vkms_config_is_valid(const struct vkms_config *config);

/**
 * vkms_config_register_debugfs() - Register a debugfs file to show the device's
 * configuration
 * @vkms_device: Device to register
 */
void vkms_config_register_debugfs(struct vkms_device *vkms_device);

#endif /* _VKMS_CONFIG_H_ */
