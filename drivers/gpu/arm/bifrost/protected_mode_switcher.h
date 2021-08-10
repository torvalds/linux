/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2017, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _PROTECTED_MODE_SWITCH_H_
#define _PROTECTED_MODE_SWITCH_H_

struct protected_mode_device;

/**
 * struct protected_mode_ops - Callbacks for protected mode switch operations
 *
 * @protected_mode_enable:  Callback to enable protected mode for device, and
 *                          reset device
 *                          Returns 0 on success, non-zero on error
 * @protected_mode_disable: Callback to disable protected mode for device
 *                          Returns 0 on success, non-zero on error
 */
struct protected_mode_ops {
	int (*protected_mode_enable)(
			struct protected_mode_device *protected_dev);
	int (*protected_mode_disable)(
			struct protected_mode_device *protected_dev);
};

/**
 * struct protected_mode_device - Device structure for protected mode devices
 * @ops:  Callbacks associated with this device
 * @data: Pointer to device private data
 *
 * This structure should be registered with the platform device using
 * platform_set_drvdata().
 */
struct protected_mode_device {
	struct protected_mode_ops ops;
	void *data;
};

#endif /* _PROTECTED_MODE_SWITCH_H_ */
