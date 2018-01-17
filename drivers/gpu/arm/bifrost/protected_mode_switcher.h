/*
 *
 * (C) COPYRIGHT 2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#ifndef _PROTECTED_MODE_SWITCH_H_
#define _PROTECTED_MODE_SWITCH_H_

struct protected_mode_device;

/**
 * struct protected_mode_ops - Callbacks for protected mode switch operations
 *
 * @protected_mode_enable:  Callback to enable protected mode for device
 * @protected_mode_disable: Callback to disable protected mode for device
 */
struct protected_mode_ops {
	/**
	 * protected_mode_enable() - Enable protected mode on device
	 * @dev:	The struct device
	 *
	 * Return: 0 on success, non-zero on error
	 */
	int (*protected_mode_enable)(
			struct protected_mode_device *protected_dev);

	/**
	 * protected_mode_disable() - Disable protected mode on device, and
	 *                            reset device
	 * @dev:	The struct device
	 *
	 * Return: 0 on success, non-zero on error
	 */
	int (*protected_mode_disable)(
			struct protected_mode_device *protected_dev);
};

/**
 * struct protected_mode_device - Device structure for protected mode devices
 *
 * @ops  - Callbacks associated with this device
 * @data - Pointer to device private data
 *
 * This structure should be registered with the platform device using
 * platform_set_drvdata().
 */
struct protected_mode_device {
	struct protected_mode_ops ops;
	void *data;
};

#endif /* _PROTECTED_MODE_SWITCH_H_ */
